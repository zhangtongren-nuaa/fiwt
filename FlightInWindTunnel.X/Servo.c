/*
 * File:   Servo.c
 * Author: Zheng GONG(matthewzhenggong@gmail.com)
 *
 * This file is part of FIWT.
 *
 * FIWT is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 */


#include "Servo.h"

#if USE_PWM && USE_ADC1

#include "PWMx.h"
#include "AnalogInput.h"
#include <xc.h>
#include <stddef.h>

/* Servos pin layout */
/************************************************************************
System      Servo       Analog Input    PMW Output  Directioning Pins
                                                        (IN_B / IN_A)
-------------------------------------------------------------------------
            Servo1		AN20        PMW1	LATJ4 	/ LATJ5
            Servo2		AN21        PMW2	LATJ6 	/ LATJ7
AC_MODEL    Servo3		AN5         PMW3	LATG14	/ LATG12
            Servo4		AN3         PMW7	LATJ10	/ LATJ11
            Servo5		AN2         PMW5	LATJ12	/ LATJ13
            Servo6		AN1         PMW6	LATG6 	/ LATG7
-------------------------------------------------------------------------
            Servo1		AN20        PMW1	LATJ4 	/ LATJ5
AEROCOMP    Servo2		AN21        PMW2	LATJ6 	/ LATJ7
            Servo3		AN3         PMW7	LATJ10	/ LATJ11
            Servo4		AN2         PMW5	LATJ12	/ LATJ13
 ************************************************************************/

Servo_t Servos[] = {
#if AC_MODEL
    {&ServoPos[0], &PWM1DC, &LATJ, _LATJ_LATJ4_MASK, ~_LATJ_LATJ4_MASK, &LATJ, _LATJ_LATJ5_MASK, ~_LATJ_LATJ5_MASK},
    {&ServoPos[1], &PWM2DC, &LATJ, _LATJ_LATJ6_MASK, ~_LATJ_LATJ6_MASK, &LATJ, _LATJ_LATJ7_MASK, ~_LATJ_LATJ7_MASK},
    {&ServoPos[2], &PWM3DC, &LATG, _LATG_LATG14_MASK, ~_LATG_LATG14_MASK, &LATG, _LATG_LATG12_MASK, ~_LATG_LATG12_MASK},
    {&ServoPos[3], &PWM7DC, &LATJ, _LATJ_LATJ10_MASK, ~_LATJ_LATJ10_MASK, &LATJ, _LATJ_LATJ11_MASK, ~_LATJ_LATJ11_MASK},
    {&ServoPos[4], &PWM5DC, &LATJ, _LATJ_LATJ12_MASK, ~_LATJ_LATJ12_MASK, &LATJ, _LATJ_LATJ13_MASK, ~_LATJ_LATJ13_MASK},
    {&ServoPos[5], &PWM6DC, &LATG, _LATG_LATG6_MASK, ~_LATG_LATG6_MASK, &LATG, _LATG_LATG7_MASK, ~_LATG_LATG7_MASK},
#elif AEROCOMP
    {&ServoPos[0], &PWM1DC, &LATJ, _LATJ_LATJ4_MASK, ~_LATJ_LATJ4_MASK, &LATJ, _LATJ_LATJ5_MASK, ~_LATJ_LATJ5_MASK},
    {&ServoPos[1], &PWM2DC, &LATJ, _LATJ_LATJ6_MASK, ~_LATJ_LATJ6_MASK, &LATJ, _LATJ_LATJ7_MASK, ~_LATJ_LATJ7_MASK},
    {&ServoPos[2], &PWM7DC, &LATJ, _LATJ_LATJ10_MASK, ~_LATJ_LATJ10_MASK, &LATJ, _LATJ_LATJ11_MASK, ~_LATJ_LATJ11_MASK},
    {&ServoPos[3], &PWM5DC, &LATJ, _LATJ_LATJ12_MASK, ~_LATJ_LATJ12_MASK, &LATJ, _LATJ_LATJ13_MASK, ~_LATJ_LATJ13_MASK},
#endif
};

/** two-order butterwolf filter 10Hz */
#define BUTTER_ORDER (2)
//    0.2779,-0.4152, 0.5872,
//    0.4152, 0.8651, 0.1908,
//    0.1468, 0.6594, 0.0675
// x 2^15 = 
static fractional _butter_mat_frac[] = { \
     9106, -13605, 19241,
    13605, 28347, 6252,
    4810, 21607, 2211
};

static fractional _butter_update(fractional input, fractional butt[BUTTER_ORDER+1]) {
    fractional dstM[BUTTER_ORDER];
    int i;

    butt[BUTTER_ORDER] = input;
    MatrixMultiply(BUTTER_ORDER, BUTTER_ORDER+1, 1, dstM, _butter_mat_frac, butt);
    for (i=0; i<BUTTER_ORDER; ++i) {
        butt[i] = dstM[i];
    }
    MatrixMultiply(1, BUTTER_ORDER+1, 1, dstM, _butter_mat_frac + BUTTER_ORDER*(BUTTER_ORDER+1), butt);
    return dstM[0];
}

void ServoInit(void) {

}

void ServoStart(void) {
    size_t i;
    Servo_p servo;
    UpdateAnalogInputs();
    for (i = 0u, servo = Servos; i < SEVERONUM; ++i, ++servo) {
        servo->PrevPosition = *(servo->Position);
        servo->PrevRate = 0;
        servo->Reference = 2096;
        servo->butt[0] = 0;
        servo->butt[1] = 0;
        servo->butt[2] = 0;
        servo->Ctrl = 0;
    }
}

static void _motor_set(Servo_p servo, signed int duty_circle) {
    *(servo->lat_cw) &= servo->lat_cw_mask;
    *(servo->lat_ccw) &= servo->lat_ccw_mask;
    if (duty_circle == 0) {
        servo->Ctrl = 0;
    } else if (duty_circle > 0) {
        *(servo->lat_cw) |= servo->lat_cw_pos;
        if (duty_circle > PWM_PEROID) duty_circle = PWM_PEROID;
        servo->Ctrl = duty_circle;
    } else if (duty_circle < 0) {
        *(servo->lat_ccw) |= servo->lat_ccw_pos;
        if (duty_circle < -PWM_PEROID) duty_circle = -PWM_PEROID;
        servo->Ctrl = duty_circle;
        duty_circle = -duty_circle;
    }
    *(servo->DutyCycle) = duty_circle;
}

void MotorSet(unsigned int ch, signed int duty_circle) {
    Servo_p servo;
    if (ch < SEVERONUM) {
        servo = Servos + ch;
        _motor_set(servo, duty_circle);
    }
}

void ServoUpdate100Hz(unsigned int ch, unsigned int ref) {
    Servo_p servo;
    signed int duty_circle;
    signed int pos;
    signed int rate;
    signed int diff;
    signed int accel;
    //    float butt1;

    if (ch < SEVERONUM) {
        servo = Servos + ch;

        pos = *servo->Position;
        servo->Reference = ref;

        rate = pos - servo->PrevPosition;
        accel = rate - servo->PrevRate;
        /* Accel limitation */
        if (accel > SERVO_ACCEL_LIMIT || accel < -SERVO_ACCEL_LIMIT) {
            rate = 0;
        }
        //else { /* rate limitation */
        //    if (rate > 63) rate = 63;
        //    else if (rate < -63) rate = -63;
        // }
        
        //    butt1 = servo->butt1;
        //    servo->butt1 = servo->butt1 * 0.2779 + servo->butt2*-0.4152 + rate * 0.5872;
        //    servo->butt2 = butt1 * 0.4152 + servo->butt2 * 0.8651 + rate * 0.1908;
        //    servo->butt3 = servo->butt1 * 0.1468 + servo->butt2 * 0.6594 + rate * 0.0675;
        //    rate = servo->butt3;
        rate = _butter_update(rate, servo->butt);

        diff = servo->Reference - pos;
        if (diff > 1724) diff = 1724; /* 1724 = (2^15)/19*/
        else if (diff < -1724) diff = -1724;
        //    ctrl = (15*PWM_PEROID/3.8f * 0.0007669904) * (diff) /* Proportion */
        //            + (15*0.04*PWM_PEROID/3.8*pi/4096*100) * (-rate); /* Difference */
        duty_circle = (19 *  diff >> 3) /* Proportion */
                    + (19 * -rate >> 1); /* Difference */
        if (duty_circle > 0) {
            duty_circle += 250;
        } else {
            duty_circle -= 250;
        }

        _motor_set(servo, duty_circle);

        servo->PrevPosition = pos;
        servo->PrevRate = rate;
    }
}

#endif /* USE_PWM && USE_ADC1 */

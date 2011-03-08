/*
 * drivers/input/touchscreen/calibration_ts.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_TOUCHSCREEN_CALIBRATION_TS_H
#define __DRIVERS_TOUCHSCREEN_CALIBRATION_TS_H

struct adc_point
{
    int x;
    int y;
};

#define TWO_DIMENSIONAL_CALIBRATION 1

#define ADC_PRECISION       12          // Precision of ADC output (in bits)
#define MAX_TERM_PRECISION  27          // Reserve 1 bit for sign and two bits for
                                        //  three terms (there are three terms in
                                        //  each of x and y mapping functions.)

//
// All a1, a2, b1, and b2 must have less than MAX_COEFF_PRECISION bits since
//  they all are multiplied with either an X or a Y to form a term.
// Both c1 and c2 can have up to MAX_TERM_PRECISION since they each alone
//  forms a term.
//
#define MAX_COEFF_PRECISION (MAX_TERM_PRECISION - ADC_PRECISION)

unsigned char
TouchPanelSetCalibration(
    int   cCalibrationPoints,     //@PARM The number of calibration points
    int   *pScreenXBuffer,        //@PARM List of screen X coords displayed
    int   *pScreenYBuffer,        //@PARM List of screen Y coords displayed
    int   *pUncalXBuffer,         //@PARM List of X coords collected
    int   *pUncalYBuffer          //@PARM List of Y coords collected
    );

void
TouchPanelCalibrateAPoint(
    int   UncalX,     //@PARM The uncalibrated X coordinate
    int   UncalY,     //@PARM The uncalibrated Y coordinate
    int   *pCalX,     //@PARM The calibrated X coordinate
    int   *pCalY      //@PARM The calibrated Y coordinate
    );

int  tp_calib_iface_init(int *x,int *y,int *uncali_x, int *uncali_y);
void  tp_calib_iface_exit(void);
#endif

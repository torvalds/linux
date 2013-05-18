/*
 * MinnowBoard Linux platform driver
 * Copyright (c) 2013, Intel Corporation.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Author: Darren Hart <dvhart@linux.intel.com>
 */

/* MinnowBoard GPIO definitions */
#define GPIO_BTN0 0
#define GPIO_BTN1 1
#define GPIO_BTN2 2
#define GPIO_BTN3 3

#define GPIO_PROG_VOLTAGE 4

/*
 * If !LVDS_DETECT, the AUX lines are available as GPIO,
 * otherwise they are used for LVDS signals.
 */
#define GPIO_AUX0 5
#define GPIO_AUX1 6
#define GPIO_AUX2 7
#define GPIO_AUX3 8
#define GPIO_AUX4 9

#define GPIO_LED0 10
#define GPIO_LED1 11

#define GPIO_USB_VBUS_DETECT 12

#define GPIO_PCH0 244
#define GPIO_PCH1 245
#define GPIO_PCH2 246
#define GPIO_PCH3 247
#define GPIO_PCH4 248
#define GPIO_PCH5 249
#define GPIO_PCH6 250
#define GPIO_PCH7 251

#define GPIO_HWID0 252
#define GPIO_HWID1 253
#define GPIO_HWID2 254

#define GPIO_LVDS_DETECT 255

/*
 *  indycam.h - Silicon Graphics IndyCam digital camera driver
 *
 *  Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 *  Copyright (C) 2004,2005 Mikael Nousiainen <tmnousia@cc.hut.fi>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef _INDYCAM_H_
#define _INDYCAM_H_

/* I2C address for the Guinness Camera */
#define INDYCAM_ADDR			0x56

/* Camera version */
#define CAMERA_VERSION_INDY		0x10	/* v1.0 */
#define CAMERA_VERSION_MOOSE		0x12	/* v1.2 */
#define INDYCAM_VERSION_MAJOR(x)	(((x) & 0xf0) >> 4)
#define INDYCAM_VERSION_MINOR(x)	((x) & 0x0f)

/* Register bus addresses */
#define INDYCAM_CONTROL			0x00
#define INDYCAM_SHUTTER			0x01
#define INDYCAM_GAIN			0x02
#define INDYCAM_BRIGHTNESS		0x03 /* read-only */
#define INDYCAM_RED_BALANCE		0x04
#define INDYCAM_BLUE_BALANCE		0x05
#define INDYCAM_RED_SATURATION		0x06
#define INDYCAM_BLUE_SATURATION		0x07
#define INDYCAM_GAMMA			0x08
#define INDYCAM_VERSION			0x0e /* read-only */
#define INDYCAM_RESET			0x0f /* write-only */

#define INDYCAM_LED			0x46
#define INDYCAM_ORIENTATION		0x47
#define INDYCAM_BUTTON			0x48

/* Field definitions of registers */
#define INDYCAM_CONTROL_AGCENA		(1<<0) /* automatic gain control */
#define INDYCAM_CONTROL_AWBCTL		(1<<1) /* automatic white balance */
						/* 2-3 are reserved */
#define INDYCAM_CONTROL_EVNFLD		(1<<4)	/* read-only */

#define INDYCAM_SHUTTER_10000		0x02	/* 1/10000 second */
#define INDYCAM_SHUTTER_4000		0x04	/* 1/4000 second */
#define INDYCAM_SHUTTER_2000		0x08	/* 1/2000 second */
#define INDYCAM_SHUTTER_1000		0x10	/* 1/1000 second */
#define INDYCAM_SHUTTER_500		0x20	/* 1/500 second */
#define INDYCAM_SHUTTER_250		0x3f	/* 1/250 second */
#define INDYCAM_SHUTTER_125		0x7e	/* 1/125 second */
#define INDYCAM_SHUTTER_100		0x9e	/* 1/100 second */
#define INDYCAM_SHUTTER_60		0x00	/* 1/60 second */

#define INDYCAM_LED_ACTIVE			0x10
#define INDYCAM_LED_INACTIVE			0x30
#define INDYCAM_ORIENTATION_BOTTOM_TO_TOP	0x40
#define INDYCAM_BUTTON_RELEASED			0x10

#define INDYCAM_SHUTTER_MIN		0x00
#define INDYCAM_SHUTTER_MAX		0xff
#define INDYCAM_GAIN_MIN                0x00
#define INDYCAM_GAIN_MAX                0xff
#define INDYCAM_RED_BALANCE_MIN 	0x00 /* the effect is the opposite? */
#define INDYCAM_RED_BALANCE_MAX 	0xff
#define INDYCAM_BLUE_BALANCE_MIN        0x00 /* the effect is the opposite? */
#define INDYCAM_BLUE_BALANCE_MAX        0xff
#define INDYCAM_RED_SATURATION_MIN      0x00
#define INDYCAM_RED_SATURATION_MAX      0xff
#define INDYCAM_BLUE_SATURATION_MIN	0x00
#define INDYCAM_BLUE_SATURATION_MAX	0xff
#define INDYCAM_GAMMA_MIN		0x00
#define INDYCAM_GAMMA_MAX		0xff

/* Driver interface definitions */

#define INDYCAM_VALUE_ENABLED		1
#define INDYCAM_VALUE_DISABLED		0
#define INDYCAM_VALUE_UNCHANGED		-1

/* When setting controls, a value of -1 leaves the control unchanged. */
struct indycam_control {
	int agc;	/* boolean */
	int awb;	/* boolean */
	int shutter;
	int gain;
	int red_balance;
	int blue_balance;
	int red_saturation;
	int blue_saturation;
	int gamma;
};

#define	DECODER_INDYCAM_GET_CONTROLS	_IOR('d', 193, struct indycam_control)
#define	DECODER_INDYCAM_SET_CONTROLS	_IOW('d', 194, struct indycam_control)

/* Default values for controls */

#define INDYCAM_AGC_DEFAULT		INDYCAM_VALUE_ENABLED
#define INDYCAM_AWB_DEFAULT		INDYCAM_VALUE_ENABLED

#define INDYCAM_SHUTTER_DEFAULT		INDYCAM_SHUTTER_60
#define INDYCAM_GAIN_DEFAULT		0x80
#define INDYCAM_RED_BALANCE_DEFAULT	0x18
#define INDYCAM_BLUE_BALANCE_DEFAULT	0xa4
#define INDYCAM_RED_SATURATION_DEFAULT	0x80
#define INDYCAM_BLUE_SATURATION_DEFAULT	0xc0
#define INDYCAM_GAMMA_DEFAULT		0x80

#endif

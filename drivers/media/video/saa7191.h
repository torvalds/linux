/*
 *  saa7191.h - Philips SAA7191 video decoder driver
 *
 *  Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 *  Copyright (C) 2004,2005 Mikael Nousiainen <tmnousia@cc.hut.fi>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#ifndef _SAA7191_H_
#define _SAA7191_H_

/* Philips SAA7191 DMSD I2C bus address */
#define SAA7191_ADDR		0x8a

/* Register subaddresses. */
#define SAA7191_REG_IDEL	0x00
#define SAA7191_REG_HSYB	0x01
#define SAA7191_REG_HSYS	0x02
#define SAA7191_REG_HCLB	0x03
#define SAA7191_REG_HCLS	0x04
#define SAA7191_REG_HPHI	0x05
#define SAA7191_REG_LUMA	0x06
#define SAA7191_REG_HUEC	0x07
#define SAA7191_REG_CKTQ	0x08
#define SAA7191_REG_CKTS	0x09
#define SAA7191_REG_PLSE	0x0a
#define SAA7191_REG_SESE	0x0b
#define SAA7191_REG_GAIN	0x0c
#define SAA7191_REG_STDC	0x0d
#define SAA7191_REG_IOCK	0x0e
#define SAA7191_REG_CTL3	0x0f
#define SAA7191_REG_CTL4	0x10
#define SAA7191_REG_CHCV	0x11
#define SAA7191_REG_HS6B	0x14
#define SAA7191_REG_HS6S	0x15
#define SAA7191_REG_HC6B	0x16
#define SAA7191_REG_HC6S	0x17
#define SAA7191_REG_HP6I	0x18
#define SAA7191_REG_STATUS	0xff	/* not really a subaddress */

/* Status Register definitions */
#define SAA7191_STATUS_CODE	0x01	/* color detected flag */
#define SAA7191_STATUS_FIDT	0x20	/* format type NTSC/PAL */
#define SAA7191_STATUS_HLCK	0x40	/* PLL unlocked/locked */
#define SAA7191_STATUS_STTC	0x80	/* tv/vtr time constant */

/* Luminance Control Register definitions */
#define SAA7191_LUMA_BYPS	0x80

/* Chroma Gain Control Settings Register definitions */
/* 0=automatic colour-killer enabled, 1=forced colour on */
#define SAA7191_GAIN_COLO	0x80

/* Standard/Mode Control Register definitions */
/* tv/vtr mode bit: 0=TV mode (slow time constant),
 * 1=VTR mode (fast time constant) */
#define SAA7191_STDC_VTRC	0x80
/* SECAM mode bit: 0=other standards, 1=SECAM */
#define SAA7191_STDC_SECS	0x01
/* the bit fields above must be or'd with this value */
#define SAA7191_STDC_VALUE	0x0c

/* I/O and Clock Control Register definitions */
/* horizontal clock PLL: 0=PLL closed,
 * 1=PLL circuit open and horizontal freq fixed */
#define SAA7191_IOCK_HPLL	0x80
/* S-VHS bit (chrominance from CVBS or from chrominance input):
 * 0=controlled by BYPS-bit, 1=from chrominance input */
#define SAA7191_IOCK_CHRS	0x04
/* general purpose switch 2
 * VINO-specific: 0=used with CVBS, 1=used with S-Video */
#define SAA7191_IOCK_GPSW2	0x02
/* general purpose switch 1 */
/* VINO-specific: 0=always, 1=not used!*/
#define SAA7191_IOCK_GPSW1	0x01

/* Miscellaneous Control #1 Register definitions */
/* automatic field detection (50/60Hz standard) */
#define SAA7191_CTL3_AUFD	0x80
/* field select: (if AUFD=0)
 * 0=50Hz (625 lines), 1=60Hz (525 lines) */
#define SAA7191_CTL3_FSEL	0x40
/* the bit fields above must be or'd with this value */
#define SAA7191_CTL3_VALUE	0x19

/* Chrominance Gain Control Register definitions
 * (nominal value for UV CCIR level) */
#define SAA7191_CHCV_NTSC	0x2c
#define SAA7191_CHCV_PAL	0x59

/* Driver interface definitions */
#define SAA7191_INPUT_COMPOSITE	0
#define SAA7191_INPUT_SVIDEO	1

#define SAA7191_NORM_AUTO	0
#define SAA7191_NORM_PAL	1
#define SAA7191_NORM_NTSC	2
#define SAA7191_NORM_SECAM	3

#define SAA7191_VALUE_ENABLED		1
#define SAA7191_VALUE_DISABLED		0
#define SAA7191_VALUE_UNCHANGED		-1

struct saa7191_status {
	/* 0=no signal, 1=signal active*/
	int signal;
	/* 0=50hz (pal) signal, 1=60hz (ntsc) signal */
	int ntsc;
	/* 0=no color detected, 1=color detected */
	int color;

	/* current SAA7191_INPUT_ */
	int input;
	/* current SAA7191_NORM_ */
	int norm;
};

#define SAA7191_HUE_MIN		0x00
#define SAA7191_HUE_MAX		0xff
#define SAA7191_HUE_DEFAULT	0x80

#define SAA7191_VTRC_MIN	0x00
#define SAA7191_VTRC_MAX	0x01
#define SAA7191_VTRC_DEFAULT	0x00

struct saa7191_control {
	int hue;
	int vtrc;
};

#define	DECODER_SAA7191_GET_STATUS	_IOR('d', 195, struct saa7191_status)
#define	DECODER_SAA7191_SET_NORM	_IOW('d', 196, int)
#define	DECODER_SAA7191_GET_CONTROLS	_IOR('d', 197, struct saa7191_control)
#define	DECODER_SAA7191_SET_CONTROLS	_IOW('d', 198, struct saa7191_control)

#endif

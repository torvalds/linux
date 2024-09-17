/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Zoran ZR36016 basic configuration functions - header file
 *
 * Copyright (C) 2001 Wolfgang Scherr <scherr@net4you.at>
 */

#ifndef ZR36016_H
#define ZR36016_H

/* data stored for each zoran jpeg codec chip */
struct zr36016 {
	char name[32];
	int num;
	/* io datastructure */
	struct videocodec *codec;
	// coder status
	__u8 version;
	// actual coder setup
	int mode;

	__u16 xoff;
	__u16 yoff;
	__u16 width;
	__u16 height;
	__u16 xdec;
	__u16 ydec;
};

/* direct  register addresses */
#define ZR016_GOSTOP      0x00
#define ZR016_MODE        0x01
#define ZR016_IADDR       0x02
#define ZR016_IDATA       0x03

/* indirect  register addresses */
#define ZR016I_SETUP1     0x00
#define ZR016I_SETUP2     0x01
#define ZR016I_NAX_LO     0x02
#define ZR016I_NAX_HI     0x03
#define ZR016I_PAX_LO     0x04
#define ZR016I_PAX_HI     0x05
#define ZR016I_NAY_LO     0x06
#define ZR016I_NAY_HI     0x07
#define ZR016I_PAY_LO     0x08
#define ZR016I_PAY_HI     0x09
#define ZR016I_NOL_LO     0x0a
#define ZR016I_NOL_HI     0x0b

/* possible values for mode register */
#define ZR016_RGB444_YUV444  0x00
#define ZR016_RGB444_YUV422  0x01
#define ZR016_RGB444_YUV411  0x02
#define ZR016_RGB444_Y400    0x03
#define ZR016_RGB444_RGB444  0x04
#define ZR016_YUV444_YUV444  0x08
#define ZR016_YUV444_YUV422  0x09
#define ZR016_YUV444_YUV411  0x0a
#define ZR016_YUV444_Y400    0x0b
#define ZR016_YUV444_RGB444  0x0c
#define ZR016_YUV422_YUV422  0x11
#define ZR016_YUV422_YUV411  0x12
#define ZR016_YUV422_Y400    0x13
#define ZR016_YUV411_YUV411  0x16
#define ZR016_YUV411_Y400    0x17
#define ZR016_4444_4444      0x19
#define ZR016_100_100        0x1b

#define ZR016_RGB444         0x00
#define ZR016_YUV444         0x20
#define ZR016_YUV422         0x40

#define ZR016_COMPRESSION    0x80
#define ZR016_EXPANSION      0x80

/* possible values for setup 1 register */
#define ZR016_CKRT           0x80
#define ZR016_VERT           0x40
#define ZR016_HORZ           0x20
#define ZR016_HRFL           0x10
#define ZR016_DSFL           0x08
#define ZR016_SBFL           0x04
#define ZR016_RSTR           0x02
#define ZR016_CNTI           0x01

/* possible values for setup 2 register */
#define ZR016_SYEN           0x40
#define ZR016_CCIR           0x04
#define ZR016_SIGN           0x02
#define ZR016_YMCS           0x01

int zr36016_init_module(void);
void zr36016_cleanup_module(void);
#endif				/*fndef ZR36016_H */

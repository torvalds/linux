/*
 * OmniVision OV96xx Camera Header File
 *
 * Copyright (C) 2009 Marek Vasut <marek.vasut@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__DRIVERS_MEDIA_VIDEO_OV9640_H__
#define	__DRIVERS_MEDIA_VIDEO_OV9640_H__

/* Register definitions */
#define	OV9640_GAIN	0x00
#define	OV9640_BLUE	0x01
#define	OV9640_RED	0x02
#define	OV9640_VFER	0x03
#define	OV9640_COM1	0x04
#define	OV9640_BAVE	0x05
#define	OV9640_GEAVE	0x06
#define	OV9640_RSID	0x07
#define	OV9640_RAVE	0x08
#define	OV9640_COM2	0x09
#define	OV9640_PID	0x0a
#define	OV9640_VER	0x0b
#define	OV9640_COM3	0x0c
#define	OV9640_COM4	0x0d
#define	OV9640_COM5	0x0e
#define	OV9640_COM6	0x0f
#define	OV9640_AECH	0x10
#define	OV9640_CLKRC	0x11
#define	OV9640_COM7	0x12
#define	OV9640_COM8	0x13
#define	OV9640_COM9	0x14
#define	OV9640_COM10	0x15
/* 0x16 - RESERVED */
#define	OV9640_HSTART	0x17
#define	OV9640_HSTOP	0x18
#define	OV9640_VSTART	0x19
#define	OV9640_VSTOP	0x1a
#define	OV9640_PSHFT	0x1b
#define	OV9640_MIDH	0x1c
#define	OV9640_MIDL	0x1d
#define	OV9640_MVFP	0x1e
#define	OV9640_LAEC	0x1f
#define	OV9640_BOS	0x20
#define	OV9640_GBOS	0x21
#define	OV9640_GROS	0x22
#define	OV9640_ROS	0x23
#define	OV9640_AEW	0x24
#define	OV9640_AEB	0x25
#define	OV9640_VPT	0x26
#define	OV9640_BBIAS	0x27
#define	OV9640_GBBIAS	0x28
/* 0x29 - RESERVED */
#define	OV9640_EXHCH	0x2a
#define	OV9640_EXHCL	0x2b
#define	OV9640_RBIAS	0x2c
#define	OV9640_ADVFL	0x2d
#define	OV9640_ADVFH	0x2e
#define	OV9640_YAVE	0x2f
#define	OV9640_HSYST	0x30
#define	OV9640_HSYEN	0x31
#define	OV9640_HREF	0x32
#define	OV9640_CHLF	0x33
#define	OV9640_ARBLM	0x34
/* 0x35..0x36 - RESERVED */
#define	OV9640_ADC	0x37
#define	OV9640_ACOM	0x38
#define	OV9640_OFON	0x39
#define	OV9640_TSLB	0x3a
#define	OV9640_COM11	0x3b
#define	OV9640_COM12	0x3c
#define	OV9640_COM13	0x3d
#define	OV9640_COM14	0x3e
#define	OV9640_EDGE	0x3f
#define	OV9640_COM15	0x40
#define	OV9640_COM16	0x41
#define	OV9640_COM17	0x42
/* 0x43..0x4e - RESERVED */
#define	OV9640_MTX1	0x4f
#define	OV9640_MTX2	0x50
#define	OV9640_MTX3	0x51
#define	OV9640_MTX4	0x52
#define	OV9640_MTX5	0x53
#define	OV9640_MTX6	0x54
#define	OV9640_MTX7	0x55
#define	OV9640_MTX8	0x56
#define	OV9640_MTX9	0x57
#define	OV9640_MTXS	0x58
/* 0x59..0x61 - RESERVED */
#define	OV9640_LCC1	0x62
#define	OV9640_LCC2	0x63
#define	OV9640_LCC3	0x64
#define	OV9640_LCC4	0x65
#define	OV9640_LCC5	0x66
#define	OV9640_MANU	0x67
#define	OV9640_MANV	0x68
#define	OV9640_HV	0x69
#define	OV9640_MBD	0x6a
#define	OV9640_DBLV	0x6b
#define	OV9640_GSP	0x6c	/* ... till 0x7b */
#define	OV9640_GST	0x7c	/* ... till 0x8a */

#define	OV9640_CLKRC_DPLL_EN	0x80
#define	OV9640_CLKRC_DIRECT	0x40
#define	OV9640_CLKRC_DIV(x)	((x) & 0x3f)

#define	OV9640_PSHFT_VAL(x)	((x) & 0xff)

#define	OV9640_ACOM_2X_ANALOG	0x80
#define	OV9640_ACOM_RSVD	0x12

#define	OV9640_MVFP_V		0x10
#define	OV9640_MVFP_H		0x20

#define	OV9640_COM1_HREF_NOSKIP	0x00
#define	OV9640_COM1_HREF_2SKIP	0x04
#define	OV9640_COM1_HREF_3SKIP	0x08
#define	OV9640_COM1_QQFMT	0x20

#define	OV9640_COM2_SSM		0x10

#define	OV9640_COM3_VP		0x04

#define	OV9640_COM4_QQ_VP	0x80
#define	OV9640_COM4_RSVD	0x40

#define	OV9640_COM5_SYSCLK	0x80
#define	OV9640_COM5_LONGEXP	0x01

#define	OV9640_COM6_OPT_BLC	0x40
#define	OV9640_COM6_ADBLC_BIAS	0x08
#define	OV9640_COM6_FMT_RST	0x82
#define	OV9640_COM6_ADBLC_OPTEN	0x01

#define	OV9640_COM7_RAW_RGB	0x01
#define	OV9640_COM7_RGB		0x04
#define	OV9640_COM7_QCIF	0x08
#define	OV9640_COM7_QVGA	0x10
#define	OV9640_COM7_CIF		0x20
#define	OV9640_COM7_VGA		0x40
#define	OV9640_COM7_SCCB_RESET	0x80

#define	OV9640_TSLB_YVYU_YUYV	0x04
#define	OV9640_TSLB_YUYV_UYVY	0x08

#define	OV9640_COM12_YUV_AVG	0x04
#define	OV9640_COM12_RSVD	0x40

#define	OV9640_COM13_GAMMA_NONE	0x00
#define	OV9640_COM13_GAMMA_Y	0x40
#define	OV9640_COM13_GAMMA_RAW	0x80
#define	OV9640_COM13_RGB_AVG	0x20
#define	OV9640_COM13_MATRIX_EN	0x10
#define	OV9640_COM13_Y_DELAY_EN	0x08
#define	OV9640_COM13_YUV_DLY(x)	((x) & 0x07)

#define	OV9640_COM15_OR_00FF	0x00
#define	OV9640_COM15_OR_01FE	0x40
#define	OV9640_COM15_OR_10F0	0xc0
#define	OV9640_COM15_RGB_NORM	0x00
#define	OV9640_COM15_RGB_565	0x10
#define	OV9640_COM15_RGB_555	0x30

#define	OV9640_COM16_RB_AVG	0x01

/* IDs */
#define	OV9640_V2		0x9648
#define	OV9640_V3		0x9649
#define	VERSION(pid, ver)	(((pid) << 8) | ((ver) & 0xFF))

/* supported resolutions */
enum {
	W_QQCIF	= 88,
	W_QQVGA	= 160,
	W_QCIF	= 176,
	W_QVGA	= 320,
	W_CIF	= 352,
	W_VGA	= 640,
	W_SXGA	= 1280
};
#define	H_SXGA	960

/* Misc. structures */
struct ov9640_reg_alt {
	u8	com7;
	u8	com12;
	u8	com13;
	u8	com15;
};

struct ov9640_reg {
	u8	reg;
	u8	val;
};

struct ov9640_priv {
	struct v4l2_subdev		subdev;
	struct v4l2_ctrl_handler	hdl;

	int				model;
	int				revision;
};

#endif	/* __DRIVERS_MEDIA_VIDEO_OV9640_H__ */

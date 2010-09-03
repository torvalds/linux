/*
 * drivers/video/tegra/dc/hdmi.h
 *
 * non-tegra specific HDMI declarations
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DRIVERS_VIDEO_TEGRA_DC_HDMI_H
#define __DRIVERS_VIDEO_TEGRA_DC_HDMI_H

#define HDMI_INFOFRAME_TYPE_VENDOR	0x81
#define HDMI_INFOFRAME_TYPE_AVI		0x82
#define HDMI_INFOFRAME_TYPE_SPD		0x83
#define HDMI_INFOFRAME_TYPE_AUDIO	0x84
#define HDMI_INFOFRAME_TYPE_MPEG_SRC	0x85
#define HDMI_INFOFRAME_TYPE_NTSC_VBI	0x86

/* all fields little endian */
struct hdmi_avi_infoframe {
	/* PB0 */
	u8		csum;

	/* PB1 */
	unsigned	s:2;	/* scan information */
	unsigned	b:2;	/* bar info data valid */
	unsigned	a:1;	/* active info present */
	unsigned	y:2;	/* RGB or YCbCr */
	unsigned	res1:1;

	/* PB2 */
	unsigned	r:4;	/* active format aspect ratio */
	unsigned	m:2;	/* picture aspect ratio */
	unsigned	c:2;	/* colorimetry */

	/* PB3 */
	unsigned	sc:2;	/* scan information */
	unsigned	q:2;	/* quantization range */
	unsigned	ec:3;	/* extended colorimetry */
	unsigned	itc:1;	/* it content */

	/* PB4 */
	unsigned	vic:7;	/* video format id code */
	unsigned	res4:1;

	/* PB5 */
	unsigned	pr:4;	/* pixel repetition factor */
	unsigned	cn:2;	/* it content type*/
	unsigned	yq:2;	/* ycc quantization range */

	/* PB6-7 */
	u16		top_bar_end_line;

	/* PB8-9 */
	u16		bot_bar_start_line;

	/* PB10-11 */
	u16		left_bar_end_pixel;

	/* PB12-13 */
	u16		right_bar_start_pixel;
} __attribute__((packed));

#define HDMI_AVI_VERSION		0x02

#define HDMI_AVI_Y_RGB			0x0
#define HDMI_AVI_Y_YCBCR_422		0x1
#define HDMI_AVI_Y_YCBCR_444		0x2

#define HDMI_AVI_B_VERT			0x1
#define HDMI_AVI_B_HORIZ		0x2

#define HDMI_AVI_S_NONE			0x0
#define HDMI_AVI_S_OVERSCAN		0x1
#define HDMI_AVI_S_UNDERSCAN		0x2

#define HDMI_AVI_C_NONE			0x0
#define HDMI_AVI_C_SMPTE		0x1
#define HDMI_AVI_C_ITU_R		0x2
#define HDMI_AVI_C_EXTENDED		0x4

#define HDMI_AVI_M_4_3			0x1
#define HDMI_AVI_M_16_9			0x2

#define HDMI_AVI_R_SAME			0x8
#define HDMI_AVI_R_4_3_CENTER		0x9
#define HDMI_AVI_R_16_9_CENTER		0xa
#define HDMI_AVI_R_14_9_CENTER		0xb

/* all fields little endian */
struct hdmi_audio_infoframe {
	/* PB0 */
	u8		csum;

	/* PB1 */
	unsigned	cc:3;		/* channel count */
	unsigned	res1:1;
	unsigned	ct:4;		/* coding type */

	/* PB2 */
	unsigned	ss:2;		/* sample size */
	unsigned	sf:3;		/* sample frequency */
	unsigned	res2:3;

	/* PB3 */
	unsigned	cxt:5;		/* coding extention type */
	unsigned	res3:3;

	/* PB4 */
	u8		ca;		/* channel/speaker allocation */

	/* PB5 */
	unsigned	res5:3;
	unsigned	lsv:4;		/* level shift value */
	unsigned	dm_inh:1;	/* downmix inhibit */

	/* PB6-10 reserved */
	u8		res6;
	u8		res7;
	u8		res8;
	u8		res9;
	u8		res10;
} __attribute__((packed));

#define HDMI_AUDIO_VERSION		0x01

#define HDMI_AUDIO_CC_STREAM		0x0 /* specified by audio stream */
#define HDMI_AUDIO_CC_2			0x1
#define HDMI_AUDIO_CC_3			0x2
#define HDMI_AUDIO_CC_4			0x3
#define HDMI_AUDIO_CC_5			0x4
#define HDMI_AUDIO_CC_6			0x5
#define HDMI_AUDIO_CC_7			0x6
#define HDMI_AUDIO_CC_8			0x7

#define HDMI_AUDIO_CT_STREAM		0x0 /* specified by audio stream */
#define HDMI_AUDIO_CT_PCM		0x1
#define HDMI_AUDIO_CT_AC3		0x2
#define HDMI_AUDIO_CT_MPEG1		0x3
#define HDMI_AUDIO_CT_MP3		0x4
#define HDMI_AUDIO_CT_MPEG2		0x5
#define HDMI_AUDIO_CT_AAC_LC		0x6
#define HDMI_AUDIO_CT_DTS		0x7
#define HDMI_AUDIO_CT_ATRAC		0x8
#define HDMI_AUDIO_CT_DSD		0x9
#define HDMI_AUDIO_CT_E_AC3		0xa
#define HDMI_AUDIO_CT_DTS_HD		0xb
#define HDMI_AUDIO_CT_MLP		0xc
#define HDMI_AUDIO_CT_DST		0xd
#define HDMI_AUDIO_CT_WMA_PRO		0xe
#define HDMI_AUDIO_CT_CXT		0xf

#define HDMI_AUDIO_SF_STREAM		0x0 /* specified by audio stream */
#define HDMI_AUIDO_SF_32K		0x1
#define HDMI_AUDIO_SF_44_1K		0x2
#define HDMI_AUDIO_SF_48K		0x3
#define HDMI_AUDIO_SF_88_2K		0x4
#define HDMI_AUDIO_SF_96K		0x5
#define HDMI_AUDIO_SF_176_4K		0x6
#define HDMI_AUDIO_SF_192K		0x7

#define HDMI_AUDIO_SS_STREAM		0x0 /* specified by audio stream */
#define HDMI_AUDIO_SS_16BIT		0x1
#define HDMI_AUDIO_SS_20BIT		0x2
#define HDMI_AUDIO_SS_24BIT		0x3

#define HDMI_AUDIO_CXT_CT		0x0 /* refer to coding in CT */
#define HDMI_AUDIO_CXT_HE_AAC		0x1
#define HDMI_AUDIO_CXT_HE_AAC_V2	0x2
#define HDMI_AUDIO_CXT_MPEG_SURROUND	0x3

#endif

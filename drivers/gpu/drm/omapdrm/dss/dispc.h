/*
 * linux/drivers/video/omap2/dss/dispc.h
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Archit Taneja <archit@ti.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OMAP2_DISPC_REG_H
#define __OMAP2_DISPC_REG_H

/* DISPC common registers */
#define DISPC_REVISION			0x0000
#define DISPC_SYSCONFIG			0x0010
#define DISPC_SYSSTATUS			0x0014
#define DISPC_IRQSTATUS			0x0018
#define DISPC_IRQENABLE			0x001C
#define DISPC_CONTROL			0x0040
#define DISPC_CONFIG			0x0044
#define DISPC_CAPABLE			0x0048
#define DISPC_LINE_STATUS		0x005C
#define DISPC_LINE_NUMBER		0x0060
#define DISPC_GLOBAL_ALPHA		0x0074
#define DISPC_CONTROL2			0x0238
#define DISPC_CONFIG2			0x0620
#define DISPC_DIVISOR			0x0804
#define DISPC_GLOBAL_BUFFER		0x0800
#define DISPC_CONTROL3                  0x0848
#define DISPC_CONFIG3                   0x084C
#define DISPC_MSTANDBY_CTRL		0x0858
#define DISPC_GLOBAL_MFLAG_ATTRIBUTE	0x085C

#define DISPC_GAMMA_TABLE0		0x0630
#define DISPC_GAMMA_TABLE1		0x0634
#define DISPC_GAMMA_TABLE2		0x0638
#define DISPC_GAMMA_TABLE3		0x0850

/* DISPC overlay registers */
#define DISPC_OVL_BA0(n)		(DISPC_OVL_BASE(n) + \
					DISPC_BA0_OFFSET(n))
#define DISPC_OVL_BA1(n)		(DISPC_OVL_BASE(n) + \
					DISPC_BA1_OFFSET(n))
#define DISPC_OVL_BA0_UV(n)		(DISPC_OVL_BASE(n) + \
					DISPC_BA0_UV_OFFSET(n))
#define DISPC_OVL_BA1_UV(n)		(DISPC_OVL_BASE(n) + \
					DISPC_BA1_UV_OFFSET(n))
#define DISPC_OVL_POSITION(n)		(DISPC_OVL_BASE(n) + \
					DISPC_POS_OFFSET(n))
#define DISPC_OVL_SIZE(n)		(DISPC_OVL_BASE(n) + \
					DISPC_SIZE_OFFSET(n))
#define DISPC_OVL_ATTRIBUTES(n)		(DISPC_OVL_BASE(n) + \
					DISPC_ATTR_OFFSET(n))
#define DISPC_OVL_ATTRIBUTES2(n)	(DISPC_OVL_BASE(n) + \
					DISPC_ATTR2_OFFSET(n))
#define DISPC_OVL_FIFO_THRESHOLD(n)	(DISPC_OVL_BASE(n) + \
					DISPC_FIFO_THRESH_OFFSET(n))
#define DISPC_OVL_FIFO_SIZE_STATUS(n)	(DISPC_OVL_BASE(n) + \
					DISPC_FIFO_SIZE_STATUS_OFFSET(n))
#define DISPC_OVL_ROW_INC(n)		(DISPC_OVL_BASE(n) + \
					DISPC_ROW_INC_OFFSET(n))
#define DISPC_OVL_PIXEL_INC(n)		(DISPC_OVL_BASE(n) + \
					DISPC_PIX_INC_OFFSET(n))
#define DISPC_OVL_WINDOW_SKIP(n)	(DISPC_OVL_BASE(n) + \
					DISPC_WINDOW_SKIP_OFFSET(n))
#define DISPC_OVL_TABLE_BA(n)		(DISPC_OVL_BASE(n) + \
					DISPC_TABLE_BA_OFFSET(n))
#define DISPC_OVL_FIR(n)		(DISPC_OVL_BASE(n) + \
					DISPC_FIR_OFFSET(n))
#define DISPC_OVL_FIR2(n)		(DISPC_OVL_BASE(n) + \
					DISPC_FIR2_OFFSET(n))
#define DISPC_OVL_PICTURE_SIZE(n)	(DISPC_OVL_BASE(n) + \
					DISPC_PIC_SIZE_OFFSET(n))
#define DISPC_OVL_ACCU0(n)		(DISPC_OVL_BASE(n) + \
					DISPC_ACCU0_OFFSET(n))
#define DISPC_OVL_ACCU1(n)		(DISPC_OVL_BASE(n) + \
					DISPC_ACCU1_OFFSET(n))
#define DISPC_OVL_ACCU2_0(n)		(DISPC_OVL_BASE(n) + \
					DISPC_ACCU2_0_OFFSET(n))
#define DISPC_OVL_ACCU2_1(n)		(DISPC_OVL_BASE(n) + \
					DISPC_ACCU2_1_OFFSET(n))
#define DISPC_OVL_FIR_COEF_H(n, i)	(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_H_OFFSET(n, i))
#define DISPC_OVL_FIR_COEF_HV(n, i)	(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_HV_OFFSET(n, i))
#define DISPC_OVL_FIR_COEF_H2(n, i)	(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_H2_OFFSET(n, i))
#define DISPC_OVL_FIR_COEF_HV2(n, i)	(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_HV2_OFFSET(n, i))
#define DISPC_OVL_CONV_COEF(n, i)	(DISPC_OVL_BASE(n) + \
					DISPC_CONV_COEF_OFFSET(n, i))
#define DISPC_OVL_FIR_COEF_V(n, i)	(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_V_OFFSET(n, i))
#define DISPC_OVL_FIR_COEF_V2(n, i)	(DISPC_OVL_BASE(n) + \
					DISPC_FIR_COEF_V2_OFFSET(n, i))
#define DISPC_OVL_PRELOAD(n)		(DISPC_OVL_BASE(n) + \
					DISPC_PRELOAD_OFFSET(n))
#define DISPC_OVL_MFLAG_THRESHOLD(n)	DISPC_MFLAG_THRESHOLD_OFFSET(n)

/* DISPC up/downsampling FIR filter coefficient structure */
struct dispc_coef {
	s8 hc4_vc22;
	s8 hc3_vc2;
	u8 hc2_vc1;
	s8 hc1_vc0;
	s8 hc0_vc00;
};

const struct dispc_coef *dispc_ovl_get_scale_coef(int inc, int five_taps);

/* DISPC manager/channel specific registers */
static inline u16 DISPC_DEFAULT_COLOR(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x004C;
	case OMAP_DSS_CHANNEL_DIGIT:
		return 0x0050;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03AC;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0814;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_TRANS_COLOR(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x0054;
	case OMAP_DSS_CHANNEL_DIGIT:
		return 0x0058;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03B0;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0818;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_TIMING_H(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x0064;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x0400;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0840;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_TIMING_V(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x0068;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x0404;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0844;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_POL_FREQ(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x006C;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x0408;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x083C;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_DIVISORo(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x0070;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x040C;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0838;
	default:
		BUG();
		return 0;
	}
}

/* Named as DISPC_SIZE_LCD, DISPC_SIZE_DIGIT and DISPC_SIZE_LCD2 in TRM */
static inline u16 DISPC_SIZE_MGR(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x007C;
	case OMAP_DSS_CHANNEL_DIGIT:
		return 0x0078;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03CC;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0834;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_DATA_CYCLE1(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x01D4;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03C0;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0828;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_DATA_CYCLE2(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x01D8;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03C4;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x082C;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_DATA_CYCLE3(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x01DC;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03C8;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0830;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_CPR_COEF_R(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x0220;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03BC;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0824;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_CPR_COEF_G(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x0224;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03B8;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x0820;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_CPR_COEF_B(enum omap_channel channel)
{
	switch (channel) {
	case OMAP_DSS_CHANNEL_LCD:
		return 0x0228;
	case OMAP_DSS_CHANNEL_DIGIT:
		BUG();
		return 0;
	case OMAP_DSS_CHANNEL_LCD2:
		return 0x03B4;
	case OMAP_DSS_CHANNEL_LCD3:
		return 0x081C;
	default:
		BUG();
		return 0;
	}
}

/* DISPC overlay register base addresses */
static inline u16 DISPC_OVL_BASE(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0080;
	case OMAP_DSS_VIDEO1:
		return 0x00BC;
	case OMAP_DSS_VIDEO2:
		return 0x014C;
	case OMAP_DSS_VIDEO3:
		return 0x0300;
	case OMAP_DSS_WB:
		return 0x0500;
	default:
		BUG();
		return 0;
	}
}

/* DISPC overlay register offsets */
static inline u16 DISPC_BA0_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0000;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0008;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_BA1_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0004;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x000C;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_BA0_UV_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0544;
	case OMAP_DSS_VIDEO2:
		return 0x04BC;
	case OMAP_DSS_VIDEO3:
		return 0x0310;
	case OMAP_DSS_WB:
		return 0x0118;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_BA1_UV_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0548;
	case OMAP_DSS_VIDEO2:
		return 0x04C0;
	case OMAP_DSS_VIDEO3:
		return 0x0314;
	case OMAP_DSS_WB:
		return 0x011C;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_POS_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0008;
	case OMAP_DSS_VIDEO3:
		return 0x009C;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_SIZE_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x000C;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x00A8;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_ATTR_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0020;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0010;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0070;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_ATTR2_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0568;
	case OMAP_DSS_VIDEO2:
		return 0x04DC;
	case OMAP_DSS_VIDEO3:
		return 0x032C;
	case OMAP_DSS_WB:
		return 0x0310;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_FIFO_THRESH_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0024;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0014;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x008C;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_FIFO_SIZE_STATUS_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0028;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0018;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0088;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_ROW_INC_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x002C;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x001C;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x00A4;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_PIX_INC_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0030;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0020;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0098;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_WINDOW_SKIP_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0034;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
	case OMAP_DSS_VIDEO3:
		BUG();
		return 0;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_TABLE_BA_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0038;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
	case OMAP_DSS_VIDEO3:
		BUG();
		return 0;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_FIR_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0024;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0090;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_FIR2_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0580;
	case OMAP_DSS_VIDEO2:
		return 0x055C;
	case OMAP_DSS_VIDEO3:
		return 0x0424;
	case OMAP_DSS_WB:
		return 0x290;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_PIC_SIZE_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0028;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0094;
	default:
		BUG();
		return 0;
	}
}


static inline u16 DISPC_ACCU0_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x002C;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0000;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_ACCU2_0_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0584;
	case OMAP_DSS_VIDEO2:
		return 0x0560;
	case OMAP_DSS_VIDEO3:
		return 0x0428;
	case OMAP_DSS_WB:
		return 0x0294;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_ACCU1_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0030;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0004;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_ACCU2_1_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0588;
	case OMAP_DSS_VIDEO2:
		return 0x0564;
	case OMAP_DSS_VIDEO3:
		return 0x042C;
	case OMAP_DSS_WB:
		return 0x0298;
	default:
		BUG();
		return 0;
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_H_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0034 + i * 0x8;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0010 + i * 0x8;
	default:
		BUG();
		return 0;
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_H2_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x058C + i * 0x8;
	case OMAP_DSS_VIDEO2:
		return 0x0568 + i * 0x8;
	case OMAP_DSS_VIDEO3:
		return 0x0430 + i * 0x8;
	case OMAP_DSS_WB:
		return 0x02A0 + i * 0x8;
	default:
		BUG();
		return 0;
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_HV_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
		return 0x0038 + i * 0x8;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0014 + i * 0x8;
	default:
		BUG();
		return 0;
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_HV2_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0590 + i * 8;
	case OMAP_DSS_VIDEO2:
		return 0x056C + i * 0x8;
	case OMAP_DSS_VIDEO3:
		return 0x0434 + i * 0x8;
	case OMAP_DSS_WB:
		return 0x02A4 + i * 0x8;
	default:
		BUG();
		return 0;
	}
}

/* coef index i = {0, 1, 2, 3, 4,} */
static inline u16 DISPC_CONV_COEF_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
	case OMAP_DSS_VIDEO2:
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0074 + i * 0x4;
	default:
		BUG();
		return 0;
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_V_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x0124 + i * 0x4;
	case OMAP_DSS_VIDEO2:
		return 0x00B4 + i * 0x4;
	case OMAP_DSS_VIDEO3:
	case OMAP_DSS_WB:
		return 0x0050 + i * 0x4;
	default:
		BUG();
		return 0;
	}
}

/* coef index i = {0, 1, 2, 3, 4, 5, 6, 7} */
static inline u16 DISPC_FIR_COEF_V2_OFFSET(enum omap_plane plane, u16 i)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		BUG();
		return 0;
	case OMAP_DSS_VIDEO1:
		return 0x05CC + i * 0x4;
	case OMAP_DSS_VIDEO2:
		return 0x05A8 + i * 0x4;
	case OMAP_DSS_VIDEO3:
		return 0x0470 + i * 0x4;
	case OMAP_DSS_WB:
		return 0x02E0 + i * 0x4;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_PRELOAD_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x01AC;
	case OMAP_DSS_VIDEO1:
		return 0x0174;
	case OMAP_DSS_VIDEO2:
		return 0x00E8;
	case OMAP_DSS_VIDEO3:
		return 0x00A0;
	default:
		BUG();
		return 0;
	}
}

static inline u16 DISPC_MFLAG_THRESHOLD_OFFSET(enum omap_plane plane)
{
	switch (plane) {
	case OMAP_DSS_GFX:
		return 0x0860;
	case OMAP_DSS_VIDEO1:
		return 0x0864;
	case OMAP_DSS_VIDEO2:
		return 0x0868;
	case OMAP_DSS_VIDEO3:
		return 0x086c;
	case OMAP_DSS_WB:
		return 0x0870;
	default:
		BUG();
		return 0;
	}
}
#endif

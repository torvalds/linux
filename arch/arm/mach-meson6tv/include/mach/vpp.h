/*
 * arch/arm/mach-meson6tvd/include/mach/vpp.h
 *
 * Copyright (C) 2014 Amlogic, Inc.
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

#ifndef __MACH_MESON_VPP_REGS_H
#define __MACH_MESON_VPP_REGS_H


// Bit 28   color management enable
// Bit 27,  if true, vd2 use viu2 output as the input, otherwise use normal vd2 from memory
// Bit 26:18, vd2 alpha
// Bit 17, osd2 enable for preblend
// Bit 16, osd1 enable for preblend
// Bit 15, vd2 enable for preblend
// Bit 14, vd1 enable for preblend
// Bit 13, osd2 enable for postblend
// Bit 12, osd1 enable for postblend
// Bit 11, vd2 enable for postblend
// Bit 10, vd1 enable for postblend
// Bit 9,  if true, osd1 is alpha premultipiled
// Bit 8,  if true, osd2 is alpha premultipiled
// Bit 7,  postblend module enable
// Bit 6,  preblend module enable
// Bit 5,  if true, osd2 foreground compared with osd1 in preblend
// Bit 4,  if true, osd2 foreground compared with osd1 in postblend
// Bit 3,
// Bit 2,  if true, disable resetting async fifo every vsync, otherwise every vsync
//			 the aync fifo will be reseted.
// Bit 1,
// Bit 0	if true, the output result of VPP is saturated
#define VPP_VD2_ALPHA_WID		9
#define VPP_VD2_ALPHA_MASK		0x1ff
#define VPP_VD2_ALPHA_BIT		18
#define VPP_OSD2_PREBLEND		(1 << 17)
#define VPP_OSD1_PREBLEND		(1 << 16)
#define VPP_VD2_PREBLEND		(1 << 15)
#define VPP_VD1_PREBLEND		(1 << 14)
#define VPP_OSD2_POSTBLEND		(1 << 13)
#define VPP_OSD1_POSTBLEND		(1 << 12)
#define VPP_VD2_POSTBLEND		(1 << 11)
#define VPP_VD1_POSTBLEND		(1 << 10)
#define VPP_OSD1_ALPHA			(1 << 9)
#define VPP_OSD2_ALPHA			(1 << 8)
#define VPP_POSTBLEND_EN		(1 << 7)
#define VPP_PREBLEND_EN			(1 << 6)
#define VPP_PRE_FG_SEL_MASK		(1 << 5)
#define VPP_PRE_FG_OSD2			(1 << 5)
#define VPP_PRE_FG_OSD1			(0 << 5)
#define VPP_POST_FG_SEL_MASK		(1 << 4)
#define VPP_POST_FG_OSD2 		(1 << 4)
#define VPP_POST_FG_OSD1		(0 << 4)
#define VPP_FIFO_RESET_DE		(1 << 2)
#define VPP_OUT_SATURATE		(1 << 0)


/*tv relative part*/
#define VFIFO2VD_CTL			0x1b58
#define VFIFO2VD_PIXEL_START		0x1b59
#define VFIFO2VD_PIXEL_END		0x1b5a
#define VFIFO2VD_LINE_TOP_START		0x1b5b
#define VFIFO2VD_LINE_TOP_END		0x1b5c
#define VFIFO2VD_LINE_BOT_START		0x1b5d
#define VFIFO2VD_LINE_BOT_END		0x1b5e


#endif // __MACH_MESON_VPP_REGS_H

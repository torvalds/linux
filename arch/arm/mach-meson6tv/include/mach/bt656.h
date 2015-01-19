/*
 * arch/arm/mach-meson6tvd/include/mach/bt656.h
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

#ifndef __MACH_MESON_BT656_REGS_H
#define __MACH_MESON_BT656_REGS_H

//#define BT_CTRL 0x2240 	///../ucode/register.h
#define BT_SYSCLOCK_RESET		30	//Sync fifo soft  reset_n at system clock domain.     Level reset. 0 = reset. 1 : normal mode.
#define BT_656CLOCK_RESET		29	//Sync fifo soft reset_n at bt656 clock domain.   Level reset.  0 = reset.  1 : normal mode.
#define BT_VSYNC_SEL			25	//25:26 VDIN VS selection.   00 :  SOF.  01: EOF.   10: vbi start point.  11 : vbi end point.
#define BT_HSYNC_SEL			23	//24:23 VDIN HS selection.  00 : EAV.  01: SAV.    10:  EOL.  11: SOL
#define BT_CAMERA_MODE			22	// Camera_mode
#define BT_CLOCK_ENABLE			7	// 1: enable bt656 clock. 0: disable bt656 clock.

//#define BT_PORT_CTRL 0x2249 	///../ucode/register.h
#define BT_VSYNC_MODE			23	//1: use  vsync  as the VBI start point. 0: use the regular vref.
#define BT_HSYNC_MODE			22	//1: use hsync as the active video start point.  0. Use regular sav and eav.
#define BT_SOFT_RESET			31	// Soft reset
#define BT_JPEG_START			30
#define BT_JPEG_IGNORE_BYTES		18	//20:18
#define BT_JPEG_IGNORE_LAST		17
#define BT_UPDATE_ST_SEL		16
#define BT_COLOR_REPEAT			15
#define BT_VIDEO_MODE			13	// 14:13
#define BT_AUTO_FMT			12
#define BT_PROG_MODE			11
#define BT_JPEG_MODE			10
#define BT_XCLK27_EN_BIT		9	// 1 : xclk27 is input.     0 : xclk27 is output.
#define BT_FID_EN_BIT			8	// 1 : enable use FID port.
#define BT_CLK27_SEL_BIT		7	// 1 : external xclk27      0 : internal clk27.
#define BT_CLK27_PHASE_BIT		6	// 1 : no inverted          0 : inverted.
#define BT_ACE_MODE_BIT			5	// 1 : auto cover error by hardware.
#define BT_SLICE_MODE_BIT		4	// 1 : no ancillay flag     0 : with ancillay flag.
#define BT_FMT_MODE_BIT			3	// 1 : ntsc                 0 : pal.
#define BT_REF_MODE_BIT			2	// 1 : from bit stream.     0 : from ports.
#define BT_MODE_BIT			1	// 1 : BT656 model          0 : SAA7118 mode.
#define BT_EN_BIT			0	// 1 : enable.
#define BT_VSYNC_PHASE			0
#define BT_HSYNC_PHASE			1
#define BT_VSYNC_PULSE			2
#define BT_HSYNC_PULSE			3
#define BT_FID_PHASE			4
#define BT_FID_HSVS			5
#define BT_IDQ_EN			6
#define BT_IDQ_PHASE			7
#define BT_D8B				8
#define BT_10BTO8B			9
#define BT_FID_DELAY			10	//12:10
#define BT_VSYNC_DELAY			13	//
#define BT_HSYNC_DELAY			16


#endif // __MACH_MESON_BT656_REGS_H

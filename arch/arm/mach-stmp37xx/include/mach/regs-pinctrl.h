/*
 * stmp37xx: PINCTRL register definitions
 *
 * Copyright (c) 2008 Freescale Semiconductor
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _MACH_REGS_PINCTRL
#define _MACH_REGS_PINCTRL

#define REGS_PINCTRL_BASE	(STMP3XXX_REGS_BASE + 0x18000)

#define HW_PINCTRL_MUXSEL0	0x100
#define HW_PINCTRL_MUXSEL1	0x110
#define HW_PINCTRL_MUXSEL2	0x120
#define HW_PINCTRL_MUXSEL3	0x130
#define HW_PINCTRL_MUXSEL4	0x140
#define HW_PINCTRL_MUXSEL5	0x150
#define HW_PINCTRL_MUXSEL6	0x160
#define HW_PINCTRL_MUXSEL7	0x170

#define HW_PINCTRL_DRIVE0	0x200
#define HW_PINCTRL_DRIVE1	0x210
#define HW_PINCTRL_DRIVE2	0x220
#define HW_PINCTRL_DRIVE3	0x230
#define HW_PINCTRL_DRIVE4	0x240
#define HW_PINCTRL_DRIVE5	0x250
#define HW_PINCTRL_DRIVE6	0x260
#define HW_PINCTRL_DRIVE7	0x270
#define HW_PINCTRL_DRIVE8	0x280
#define HW_PINCTRL_DRIVE9	0x290
#define HW_PINCTRL_DRIVE10	0x2A0
#define HW_PINCTRL_DRIVE11	0x2B0
#define HW_PINCTRL_DRIVE12	0x2C0
#define HW_PINCTRL_DRIVE13	0x2D0
#define HW_PINCTRL_DRIVE14	0x2E0

#define HW_PINCTRL_PULL0	0x300
#define HW_PINCTRL_PULL1	0x310
#define HW_PINCTRL_PULL2	0x320
#define HW_PINCTRL_PULL3	0x330

#define HW_PINCTRL_DOUT0	0x400
#define HW_PINCTRL_DOUT1	0x410
#define HW_PINCTRL_DOUT2	0x420

#define HW_PINCTRL_DIN0		0x500
#define HW_PINCTRL_DIN1		0x510
#define HW_PINCTRL_DIN2		0x520

#define HW_PINCTRL_DOE0		0x600
#define HW_PINCTRL_DOE1		0x610
#define HW_PINCTRL_DOE2		0x620

#define HW_PINCTRL_PIN2IRQ0	0x700
#define HW_PINCTRL_PIN2IRQ1	0x710
#define HW_PINCTRL_PIN2IRQ2	0x720

#define HW_PINCTRL_IRQEN0	0x800
#define HW_PINCTRL_IRQEN1	0x810
#define HW_PINCTRL_IRQEN2	0x820

#define HW_PINCTRL_IRQLEVEL0	0x900
#define HW_PINCTRL_IRQLEVEL1	0x910
#define HW_PINCTRL_IRQLEVEL2	0x920

#define HW_PINCTRL_IRQPOL0	0xA00
#define HW_PINCTRL_IRQPOL1	0xA10
#define HW_PINCTRL_IRQPOL2	0xA20

#define HW_PINCTRL_IRQSTAT0	0xB00
#define HW_PINCTRL_IRQSTAT1	0xB10
#define HW_PINCTRL_IRQSTAT2	0xB20

#endif

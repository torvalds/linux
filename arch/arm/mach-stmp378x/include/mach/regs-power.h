/*
 * stmp378x: POWER register definitions
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
#ifndef _MACH_REGS_POWER
#define _MACH_REGS_POWER

#define REGS_POWER_BASE	(STMP3XXX_REGS_BASE + 0x44000)
#define REGS_POWER_PHYS	0x80044000
#define REGS_POWER_SIZE	0x2000

#define HW_POWER_CTRL		0x0
#define BM_POWER_CTRL_ENIRQ_VDD5V_GT_VDDIO	0x00000001
#define BP_POWER_CTRL_ENIRQ_VDD5V_GT_VDDIO	0
#define BM_POWER_CTRL_ENIRQ_PSWITCH	0x00020000
#define BM_POWER_CTRL_PSWITCH_IRQ	0x00100000
#define BM_POWER_CTRL_CLKGATE	0x40000000

#define HW_POWER_5VCTRL		0x10
#define BM_POWER_5VCTRL_ENABLE_LINREG_ILIMIT	0x00000040

#define HW_POWER_MINPWR		0x20

#define HW_POWER_CHARGE		0x30

#define HW_POWER_VDDDCTRL	0x40

#define HW_POWER_VDDACTRL	0x50

#define HW_POWER_VDDIOCTRL	0x60
#define BM_POWER_VDDIOCTRL_TRG	0x0000001F
#define BP_POWER_VDDIOCTRL_TRG	0

#define HW_POWER_STS		0xC0
#define BM_POWER_STS_VBUSVALID	0x00000002
#define BM_POWER_STS_BVALID	0x00000004
#define BM_POWER_STS_AVALID	0x00000008
#define BM_POWER_STS_DC_OK	0x00000200

#define HW_POWER_RESET		0x100

#define HW_POWER_DEBUG		0x110
#define BM_POWER_DEBUG_BVALIDPIOLOCK	0x00000002
#define BM_POWER_DEBUG_AVALIDPIOLOCK	0x00000004
#define BM_POWER_DEBUG_VBUSVALIDPIOLOCK	0x00000008

#endif

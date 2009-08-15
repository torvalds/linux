/*
 * stmp378x: CLKCTRL register definitions
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
#ifndef _MACH_REGS_CLKCTRL
#define _MACH_REGS_CLKCTRL

#define REGS_CLKCTRL_BASE	(STMP3XXX_REGS_BASE + 0x40000)
#define REGS_CLKCTRL_PHYS	0x80040000
#define REGS_CLKCTRL_SIZE	0x2000

#define HW_CLKCTRL_PLLCTRL0	0x0
#define BM_CLKCTRL_PLLCTRL0_EN_USB_CLKS	0x00040000

#define HW_CLKCTRL_CPU		0x20
#define BM_CLKCTRL_CPU_DIV_CPU	0x0000003F
#define BP_CLKCTRL_CPU_DIV_CPU	0

#define HW_CLKCTRL_HBUS		0x30
#define BM_CLKCTRL_HBUS_DIV	0x0000001F
#define BP_CLKCTRL_HBUS_DIV	0
#define BM_CLKCTRL_HBUS_DIV_FRAC_EN	0x00000020

#define HW_CLKCTRL_XBUS		0x40

#define HW_CLKCTRL_XTAL		0x50
#define BM_CLKCTRL_XTAL_DRI_CLK24M_GATE	0x10000000

#define HW_CLKCTRL_PIX		0x60
#define BM_CLKCTRL_PIX_DIV	0x00000FFF
#define BP_CLKCTRL_PIX_DIV	0
#define BM_CLKCTRL_PIX_CLKGATE	0x80000000

#define HW_CLKCTRL_SSP		0x70

#define HW_CLKCTRL_GPMI		0x80

#define HW_CLKCTRL_SPDIF	0x90

#define HW_CLKCTRL_EMI		0xA0
#define BM_CLKCTRL_EMI_DIV_EMI	0x0000003F
#define BP_CLKCTRL_EMI_DIV_EMI	0
#define BM_CLKCTRL_EMI_DCC_RESYNC_ENABLE	0x00010000
#define BM_CLKCTRL_EMI_BUSY_DCC_RESYNC	0x00020000
#define BM_CLKCTRL_EMI_BUSY_REF_EMI	0x10000000
#define BM_CLKCTRL_EMI_BUSY_REF_XTAL	0x20000000

#define HW_CLKCTRL_IR		0xB0

#define HW_CLKCTRL_SAIF		0xC0

#define HW_CLKCTRL_TV		0xD0

#define HW_CLKCTRL_ETM		0xE0

#define HW_CLKCTRL_FRAC		0xF0
#define BM_CLKCTRL_FRAC_EMIFRAC	0x00003F00
#define BP_CLKCTRL_FRAC_EMIFRAC	8
#define BM_CLKCTRL_FRAC_PIXFRAC	0x003F0000
#define BP_CLKCTRL_FRAC_PIXFRAC	16
#define BM_CLKCTRL_FRAC_CLKGATEPIX	0x00800000

#define HW_CLKCTRL_FRAC1	0x100

#define HW_CLKCTRL_CLKSEQ	0x110
#define BM_CLKCTRL_CLKSEQ_BYPASS_PIX	0x00000002

#define HW_CLKCTRL_RESET	0x120
#define BM_CLKCTRL_RESET_DIG	0x00000001
#define BP_CLKCTRL_RESET_DIG	0

#endif

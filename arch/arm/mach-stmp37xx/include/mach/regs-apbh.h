/*
 * stmp37xx: APBH register definitions
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
#ifndef _MACH_REGS_APBH
#define _MACH_REGS_APBH

#define REGS_APBH_BASE	(STMP3XXX_REGS_BASE + 0x4000)

#define HW_APBH_CTRL0		0x0
#define BM_APBH_CTRL0_RESET_CHANNEL	0x00FF0000
#define BP_APBH_CTRL0_RESET_CHANNEL	16
#define BM_APBH_CTRL0_CLKGATE	0x40000000
#define BM_APBH_CTRL0_SFTRST	0x80000000

#define HW_APBH_CTRL1		0x10
#define BM_APBH_CTRL1_CH0_CMDCMPLT_IRQ	0x00000001
#define BP_APBH_CTRL1_CH0_CMDCMPLT_IRQ	0

#define HW_APBH_DEVSEL		0x20

#define HW_APBH_CH0_NXTCMDAR	(0x50 + 0 * 0x70)
#define HW_APBH_CH1_NXTCMDAR	(0x50 + 1 * 0x70)
#define HW_APBH_CH2_NXTCMDAR	(0x50 + 2 * 0x70)
#define HW_APBH_CH3_NXTCMDAR	(0x50 + 3 * 0x70)
#define HW_APBH_CH4_NXTCMDAR	(0x50 + 4 * 0x70)
#define HW_APBH_CH5_NXTCMDAR	(0x50 + 5 * 0x70)
#define HW_APBH_CH6_NXTCMDAR	(0x50 + 6 * 0x70)
#define HW_APBH_CH7_NXTCMDAR	(0x50 + 7 * 0x70)
#define HW_APBH_CH8_NXTCMDAR	(0x50 + 8 * 0x70)
#define HW_APBH_CH9_NXTCMDAR	(0x50 + 9 * 0x70)
#define HW_APBH_CH10_NXTCMDAR	(0x50 + 10 * 0x70)
#define HW_APBH_CH11_NXTCMDAR	(0x50 + 11 * 0x70)
#define HW_APBH_CH12_NXTCMDAR	(0x50 + 12 * 0x70)
#define HW_APBH_CH13_NXTCMDAR	(0x50 + 13 * 0x70)
#define HW_APBH_CH14_NXTCMDAR	(0x50 + 14 * 0x70)
#define HW_APBH_CH15_NXTCMDAR	(0x50 + 15 * 0x70)

#define HW_APBH_CHn_NXTCMDAR	0x50

#define BM_APBH_CHn_CMD_MODE		0x00000003
#define BP_APBH_CHn_CMD_MODE		0x00000001
#define BV_APBH_CHn_CMD_MODE_NOOP		 0
#define BV_APBH_CHn_CMD_MODE_WRITE		 1
#define BV_APBH_CHn_CMD_MODE_READ		 2
#define BV_APBH_CHn_CMD_MODE_SENSE		 3
#define BM_APBH_CHn_CMD_CHAIN		0x00000004
#define BM_APBH_CHn_CMD_IRQONCMPLT	0x00000008
#define BM_APBH_CHn_CMD_NANDLOCK	0x00000010
#define BM_APBH_CHn_CMD_NANDWAIT4READY	0x00000020
#define BM_APBH_CHn_CMD_SEMAPHORE	0x00000040
#define BM_APBH_CHn_CMD_WAIT4ENDCMD	0x00000080
#define BM_APBH_CHn_CMD_CMDWORDS	0x0000F000
#define BP_APBH_CHn_CMD_CMDWORDS	12
#define BM_APBH_CHn_CMD_XFER_COUNT	0xFFFF0000
#define BP_APBH_CHn_CMD_XFER_COUNT	16

#define HW_APBH_CH0_SEMA	(0x80 + 0 * 0x70)
#define HW_APBH_CH1_SEMA	(0x80 + 1 * 0x70)
#define HW_APBH_CH2_SEMA	(0x80 + 2 * 0x70)
#define HW_APBH_CH3_SEMA	(0x80 + 3 * 0x70)
#define HW_APBH_CH4_SEMA	(0x80 + 4 * 0x70)
#define HW_APBH_CH5_SEMA	(0x80 + 5 * 0x70)
#define HW_APBH_CH6_SEMA	(0x80 + 6 * 0x70)
#define HW_APBH_CH7_SEMA	(0x80 + 7 * 0x70)
#define HW_APBH_CH8_SEMA	(0x80 + 8 * 0x70)
#define HW_APBH_CH9_SEMA	(0x80 + 9 * 0x70)
#define HW_APBH_CH10_SEMA	(0x80 + 10 * 0x70)
#define HW_APBH_CH11_SEMA	(0x80 + 11 * 0x70)
#define HW_APBH_CH12_SEMA	(0x80 + 12 * 0x70)
#define HW_APBH_CH13_SEMA	(0x80 + 13 * 0x70)
#define HW_APBH_CH14_SEMA	(0x80 + 14 * 0x70)
#define HW_APBH_CH15_SEMA	(0x80 + 15 * 0x70)

#define HW_APBH_CHn_SEMA	0x80
#define BM_APBH_CHn_SEMA_INCREMENT_SEMA	0x000000FF
#define BP_APBH_CHn_SEMA_INCREMENT_SEMA	0
#define BM_APBH_CHn_SEMA_PHORE	0x00FF0000
#define BP_APBH_CHn_SEMA_PHORE	16

#endif

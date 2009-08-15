/*
 * stmp378x: APBX register definitions
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
#ifndef _MACH_REGS_APBX
#define _MACH_REGS_APBX

#define REGS_APBX_BASE	(STMP3XXX_REGS_BASE + 0x24000)
#define REGS_APBX_PHYS	0x80024000
#define REGS_APBX_SIZE	0x2000

#define HW_APBX_CTRL0		0x0
#define BM_APBX_CTRL0_CLKGATE	0x40000000
#define BM_APBX_CTRL0_SFTRST	0x80000000

#define HW_APBX_CTRL1		0x10

#define HW_APBX_CTRL2		0x20

#define HW_APBX_CHANNEL_CTRL	0x30
#define BM_APBX_CHANNEL_CTRL_RESET_CHANNEL	0xFFFF0000
#define BP_APBX_CHANNEL_CTRL_RESET_CHANNEL	16

#define HW_APBX_DEVSEL		0x40

#define HW_APBX_CH0_NXTCMDAR	(0x110 + 0 * 0x70)
#define HW_APBX_CH1_NXTCMDAR	(0x110 + 1 * 0x70)
#define HW_APBX_CH2_NXTCMDAR	(0x110 + 2 * 0x70)
#define HW_APBX_CH3_NXTCMDAR	(0x110 + 3 * 0x70)
#define HW_APBX_CH4_NXTCMDAR	(0x110 + 4 * 0x70)
#define HW_APBX_CH5_NXTCMDAR	(0x110 + 5 * 0x70)
#define HW_APBX_CH6_NXTCMDAR	(0x110 + 6 * 0x70)
#define HW_APBX_CH7_NXTCMDAR	(0x110 + 7 * 0x70)
#define HW_APBX_CH8_NXTCMDAR	(0x110 + 8 * 0x70)
#define HW_APBX_CH9_NXTCMDAR	(0x110 + 9 * 0x70)
#define HW_APBX_CH10_NXTCMDAR	(0x110 + 10 * 0x70)
#define HW_APBX_CH11_NXTCMDAR	(0x110 + 11 * 0x70)
#define HW_APBX_CH12_NXTCMDAR	(0x110 + 12 * 0x70)
#define HW_APBX_CH13_NXTCMDAR	(0x110 + 13 * 0x70)
#define HW_APBX_CH14_NXTCMDAR	(0x110 + 14 * 0x70)
#define HW_APBX_CH15_NXTCMDAR	(0x110 + 15 * 0x70)

#define HW_APBX_CHn_NXTCMDAR	0x110
#define BM_APBX_CHn_CMD_COMMAND	0x00000003
#define BP_APBX_CHn_CMD_COMMAND	0
#define BV_APBX_CHn_CMD_COMMAND__NO_DMA_XFER	 0
#define BV_APBX_CHn_CMD_COMMAND__DMA_WRITE	 1
#define BV_APBX_CHn_CMD_COMMAND__DMA_READ	 2
#define BV_APBX_CHn_CMD_COMMAND__DMA_SENSE	 3
#define BM_APBX_CHn_CMD_CHAIN	0x00000004
#define BM_APBX_CHn_CMD_IRQONCMPLT	0x00000008
#define BM_APBX_CHn_CMD_SEMAPHORE	0x00000040
#define BM_APBX_CHn_CMD_WAIT4ENDCMD	0x00000080
#define BM_APBX_CHn_CMD_HALTONTERMINATE	0x00000100
#define BM_APBX_CHn_CMD_CMDWORDS	0x0000F000
#define BP_APBX_CHn_CMD_CMDWORDS	12
#define BM_APBX_CHn_CMD_XFER_COUNT	0xFFFF0000
#define BP_APBX_CHn_CMD_XFER_COUNT	16

#define HW_APBX_CH0_BAR		(0x130 + 0 * 0x70)
#define HW_APBX_CH1_BAR		(0x130 + 1 * 0x70)
#define HW_APBX_CH2_BAR		(0x130 + 2 * 0x70)
#define HW_APBX_CH3_BAR		(0x130 + 3 * 0x70)
#define HW_APBX_CH4_BAR		(0x130 + 4 * 0x70)
#define HW_APBX_CH5_BAR		(0x130 + 5 * 0x70)
#define HW_APBX_CH6_BAR		(0x130 + 6 * 0x70)
#define HW_APBX_CH7_BAR		(0x130 + 7 * 0x70)
#define HW_APBX_CH8_BAR		(0x130 + 8 * 0x70)
#define HW_APBX_CH9_BAR		(0x130 + 9 * 0x70)
#define HW_APBX_CH10_BAR		(0x130 + 10 * 0x70)
#define HW_APBX_CH11_BAR		(0x130 + 11 * 0x70)
#define HW_APBX_CH12_BAR		(0x130 + 12 * 0x70)
#define HW_APBX_CH13_BAR		(0x130 + 13 * 0x70)
#define HW_APBX_CH14_BAR		(0x130 + 14 * 0x70)
#define HW_APBX_CH15_BAR		(0x130 + 15 * 0x70)

#define HW_APBX_CHn_BAR		0x130

#define HW_APBX_CH0_SEMA	(0x140 + 0 * 0x70)
#define HW_APBX_CH1_SEMA	(0x140 + 1 * 0x70)
#define HW_APBX_CH2_SEMA	(0x140 + 2 * 0x70)
#define HW_APBX_CH3_SEMA	(0x140 + 3 * 0x70)
#define HW_APBX_CH4_SEMA	(0x140 + 4 * 0x70)
#define HW_APBX_CH5_SEMA	(0x140 + 5 * 0x70)
#define HW_APBX_CH6_SEMA	(0x140 + 6 * 0x70)
#define HW_APBX_CH7_SEMA	(0x140 + 7 * 0x70)
#define HW_APBX_CH8_SEMA	(0x140 + 8 * 0x70)
#define HW_APBX_CH9_SEMA	(0x140 + 9 * 0x70)
#define HW_APBX_CH10_SEMA	(0x140 + 10 * 0x70)
#define HW_APBX_CH11_SEMA	(0x140 + 11 * 0x70)
#define HW_APBX_CH12_SEMA	(0x140 + 12 * 0x70)
#define HW_APBX_CH13_SEMA	(0x140 + 13 * 0x70)
#define HW_APBX_CH14_SEMA	(0x140 + 14 * 0x70)
#define HW_APBX_CH15_SEMA	(0x140 + 15 * 0x70)

#define HW_APBX_CHn_SEMA	0x140
#define BM_APBX_CHn_SEMA_INCREMENT_SEMA	0x000000FF
#define BP_APBX_CHn_SEMA_INCREMENT_SEMA	0
#define BM_APBX_CHn_SEMA_PHORE	0x00FF0000
#define BP_APBX_CHn_SEMA_PHORE	16

#endif


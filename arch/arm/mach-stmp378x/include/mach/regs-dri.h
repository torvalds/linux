/*
 * stmp378x: DRI register definitions
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
#define REGS_DRI_BASE	(STMP3XXX_REGS_BASE + 0x74000)
#define REGS_DRI_PHYS	0x80074000
#define REGS_DRI_SIZE	0x2000

#define HW_DRI_CTRL		0x0
#define BM_DRI_CTRL_RUN		0x00000001
#define BP_DRI_CTRL_RUN		0
#define BM_DRI_CTRL_ATTENTION_IRQ	0x00000002
#define BM_DRI_CTRL_PILOT_SYNC_LOSS_IRQ	0x00000004
#define BM_DRI_CTRL_OVERFLOW_IRQ	0x00000008
#define BM_DRI_CTRL_ATTENTION_IRQ_EN	0x00000200
#define BM_DRI_CTRL_PILOT_SYNC_LOSS_IRQ_EN	0x00000400
#define BM_DRI_CTRL_OVERFLOW_IRQ_EN	0x00000800
#define BM_DRI_CTRL_REACQUIRE_PHASE	0x00008000
#define BM_DRI_CTRL_STOP_ON_PILOT_ERROR	0x02000000
#define BM_DRI_CTRL_STOP_ON_OFLOW_ERROR	0x04000000
#define BM_DRI_CTRL_ENABLE_INPUTS	0x20000000
#define BM_DRI_CTRL_CLKGATE	0x40000000
#define BM_DRI_CTRL_SFTRST	0x80000000

#define HW_DRI_TIMING		0x10
#define BM_DRI_TIMING_GAP_DETECTION_INTERVAL	0x000000FF
#define BP_DRI_TIMING_GAP_DETECTION_INTERVAL	0
#define BM_DRI_TIMING_PILOT_REP_RATE	0x000F0000
#define BP_DRI_TIMING_PILOT_REP_RATE	16

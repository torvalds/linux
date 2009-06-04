/*
 * stmp378x: SPDIF register definitions
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
#define REGS_SPDIF_BASE	(STMP3XXX_REGS_BASE + 0x54000)
#define REGS_SPDIF_PHYS	0x80054000
#define REGS_SPDIF_SIZE	0x2000

#define HW_SPDIF_CTRL		0x0
#define BM_SPDIF_CTRL_RUN	0x00000001
#define BP_SPDIF_CTRL_RUN	0
#define BM_SPDIF_CTRL_FIFO_ERROR_IRQ_EN	0x00000002
#define BM_SPDIF_CTRL_FIFO_OVERFLOW_IRQ	0x00000004
#define BM_SPDIF_CTRL_FIFO_UNDERFLOW_IRQ	0x00000008
#define BM_SPDIF_CTRL_WORD_LENGTH	0x00000010
#define BM_SPDIF_CTRL_CLKGATE	0x40000000
#define BM_SPDIF_CTRL_SFTRST	0x80000000

#define HW_SPDIF_STAT		0x10

#define HW_SPDIF_FRAMECTRL	0x20

#define HW_SPDIF_SRR		0x30
#define BM_SPDIF_SRR_RATE	0x000FFFFF
#define BP_SPDIF_SRR_RATE	0
#define BM_SPDIF_SRR_BASEMULT	0x70000000
#define BP_SPDIF_SRR_BASEMULT	28

#define HW_SPDIF_DEBUG		0x40

#define HW_SPDIF_DATA		0x50

#define HW_SPDIF_VERSION	0x60

/*
 * stmp378x: BCH register definitions
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
#define REGS_BCH_BASE	(STMP3XXX_REGS_BASE + 0xA000)
#define REGS_BCH_PHYS	0x8000A000
#define REGS_BCH_SIZE	0x2000

#define HW_BCH_CTRL		0x0
#define BM_BCH_CTRL_COMPLETE_IRQ	0x00000001
#define BP_BCH_CTRL_COMPLETE_IRQ	0
#define BM_BCH_CTRL_COMPLETE_IRQ_EN	0x00000100

#define HW_BCH_STATUS0		0x10
#define BM_BCH_STATUS0_UNCORRECTABLE	0x00000004
#define BM_BCH_STATUS0_CORRECTED	0x00000008
#define BM_BCH_STATUS0_STATUS_BLK0	0x0000FF00
#define BP_BCH_STATUS0_STATUS_BLK0	8
#define BM_BCH_STATUS0_COMPLETED_CE	0x000F0000
#define BP_BCH_STATUS0_COMPLETED_CE	16

#define HW_BCH_LAYOUTSELECT	0x70

#define HW_BCH_FLASH0LAYOUT0	0x80
#define BM_BCH_FLASH0LAYOUT0_DATA0_SIZE	0x00000FFF
#define BP_BCH_FLASH0LAYOUT0_DATA0_SIZE	0
#define BM_BCH_FLASH0LAYOUT0_ECC0	0x0000F000
#define BP_BCH_FLASH0LAYOUT0_ECC0	12
#define BM_BCH_FLASH0LAYOUT0_META_SIZE	0x00FF0000
#define BP_BCH_FLASH0LAYOUT0_META_SIZE	16
#define BM_BCH_FLASH0LAYOUT0_NBLOCKS	0xFF000000
#define BP_BCH_FLASH0LAYOUT0_NBLOCKS	24
#define BM_BCH_FLASH0LAYOUT1_DATAN_SIZE	0x00000FFF
#define BP_BCH_FLASH0LAYOUT1_DATAN_SIZE	0
#define BM_BCH_FLASH0LAYOUT1_ECCN	0x0000F000
#define BP_BCH_FLASH0LAYOUT1_ECCN	12
#define BM_BCH_FLASH0LAYOUT1_PAGE_SIZE	0xFFFF0000
#define BP_BCH_FLASH0LAYOUT1_PAGE_SIZE	16

#define HW_BCH_BLOCKNAME	0x150

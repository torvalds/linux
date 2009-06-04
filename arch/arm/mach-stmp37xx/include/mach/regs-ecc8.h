/*
 * stmp37xx: ECC8 register definitions
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
#define REGS_ECC8_BASE	(STMP3XXX_REGS_BASE + 0x8000)

#define HW_ECC8_CTRL		0x0
#define BM_ECC8_CTRL_COMPLETE_IRQ	0x00000001
#define BP_ECC8_CTRL_COMPLETE_IRQ	0
#define BM_ECC8_CTRL_COMPLETE_IRQ_EN	0x00000100
#define BM_ECC8_CTRL_AHBM_SFTRST	0x20000000

#define HW_ECC8_STATUS0		0x10
#define BM_ECC8_STATUS0_UNCORRECTABLE	0x00000004
#define BM_ECC8_STATUS0_CORRECTED	0x00000008
#define BM_ECC8_STATUS0_STATUS_AUX	0x00000F00
#define BP_ECC8_STATUS0_STATUS_AUX	8
#define BM_ECC8_STATUS0_COMPLETED_CE	0x000F0000
#define BP_ECC8_STATUS0_COMPLETED_CE	16

#define HW_ECC8_STATUS1		0x20

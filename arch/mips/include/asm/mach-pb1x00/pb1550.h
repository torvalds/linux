/*
 * AMD Alchemy Semi PB1550 Referrence Board
 * Board Registers defines.
 *
 * Copyright 2004 Embedded Edge LLC.
 * Copyright 2005 Ralf Baechle (ralf@linux-mips.org)
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 *
 */
#ifndef __ASM_PB1550_H
#define __ASM_PB1550_H

#include <linux/types.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#define DBDMA_AC97_TX_CHAN	DSCR_CMD0_PSC1_TX
#define DBDMA_AC97_RX_CHAN	DSCR_CMD0_PSC1_RX
#define DBDMA_I2S_TX_CHAN	DSCR_CMD0_PSC3_TX
#define DBDMA_I2S_RX_CHAN	DSCR_CMD0_PSC3_RX

#define SPI_PSC_BASE		PSC0_BASE_ADDR
#define AC97_PSC_BASE		PSC1_BASE_ADDR
#define SMBUS_PSC_BASE		PSC2_BASE_ADDR
#define I2S_PSC_BASE		PSC3_BASE_ADDR

/*
 * Timing values as described in databook, * ns value stripped of
 * lower 2 bits.
 * These defines are here rather than an SOC1550 generic file because
 * the parts chosen on another board may be different and may require
 * different timings.
 */
#define NAND_T_H		(18 >> 2)
#define NAND_T_PUL		(30 >> 2)
#define NAND_T_SU		(30 >> 2)
#define NAND_T_WH		(30 >> 2)

/* Bitfield shift amounts */
#define NAND_T_H_SHIFT		0
#define NAND_T_PUL_SHIFT	4
#define NAND_T_SU_SHIFT		8
#define NAND_T_WH_SHIFT		12

#define NAND_TIMING	(((NAND_T_H   & 0xF) << NAND_T_H_SHIFT)   | \
			 ((NAND_T_PUL & 0xF) << NAND_T_PUL_SHIFT) | \
			 ((NAND_T_SU  & 0xF) << NAND_T_SU_SHIFT)  | \
			 ((NAND_T_WH  & 0xF) << NAND_T_WH_SHIFT))

#define NAND_CS 1

/* Should be done by YAMON */
#define NAND_STCFG	0x00400005 /* 8-bit NAND */
#define NAND_STTIME	0x00007774 /* valid for 396 MHz SD=2 only */
#define NAND_STADDR	0x12000FFF /* physical address 0x20000000 */

#endif /* __ASM_PB1550_H */

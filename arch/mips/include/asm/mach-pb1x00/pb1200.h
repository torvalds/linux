/*
 * AMD Alchemy Pb1200 Reference Board
 * Board Registers defines.
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
#ifndef __ASM_PB1200_H
#define __ASM_PB1200_H

#include <linux/types.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#define DBDMA_AC97_TX_CHAN	DSCR_CMD0_PSC1_TX
#define DBDMA_AC97_RX_CHAN	DSCR_CMD0_PSC1_RX
#define DBDMA_I2S_TX_CHAN	DSCR_CMD0_PSC1_TX
#define DBDMA_I2S_RX_CHAN	DSCR_CMD0_PSC1_RX

/*
 * SPI and SMB are muxed on the Pb1200 board.
 * Refer to board documentation.
 */
#define SPI_PSC_BASE		PSC0_BASE_ADDR
#define SMBUS_PSC_BASE		PSC0_BASE_ADDR
/*
 * AC97 and I2S are muxed on the Pb1200 board.
 * Refer to board documentation.
 */
#define AC97_PSC_BASE       PSC1_BASE_ADDR
#define I2S_PSC_BASE	PSC1_BASE_ADDR


#define BCSR_SYSTEM_VDDI	0x001F
#define BCSR_SYSTEM_POWEROFF	0x4000
#define BCSR_SYSTEM_RESET	0x8000

/* Bit positions for the different interrupt sources */
#define BCSR_INT_IDE		0x0001
#define BCSR_INT_ETH		0x0002
#define BCSR_INT_PC0		0x0004
#define BCSR_INT_PC0STSCHG	0x0008
#define BCSR_INT_PC1		0x0010
#define BCSR_INT_PC1STSCHG	0x0020
#define BCSR_INT_DC		0x0040
#define BCSR_INT_FLASHBUSY	0x0080
#define BCSR_INT_PC0INSERT	0x0100
#define BCSR_INT_PC0EJECT	0x0200
#define BCSR_INT_PC1INSERT	0x0400
#define BCSR_INT_PC1EJECT	0x0800
#define BCSR_INT_SD0INSERT	0x1000
#define BCSR_INT_SD0EJECT	0x2000
#define BCSR_INT_SD1INSERT	0x4000
#define BCSR_INT_SD1EJECT	0x8000

#define SMC91C111_PHYS_ADDR	0x0D000300
#define SMC91C111_INT		PB1200_ETH_INT

#define IDE_PHYS_ADDR		0x0C800000
#define IDE_REG_SHIFT		5
#define IDE_PHYS_LEN		(16 << IDE_REG_SHIFT)
#define IDE_INT 		PB1200_IDE_INT
#define IDE_DDMA_REQ		DSCR_CMD0_DMA_REQ1
#define IDE_RQSIZE		128

#define NAND_PHYS_ADDR 	0x1C000000

/*
 * Timing values as described in databook, * ns value stripped of
 * lower 2 bits.
 * These defines are here rather than an Au1200 generic file because
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

/*
 * External Interrupts for Pb1200 as of 8/6/2004.
 * Bit positions in the CPLD registers can be calculated by taking
 * the interrupt define and subtracting the PB1200_INT_BEGIN value.
 *
 *   Example: IDE bis pos is  = 64 - 64
 *            ETH bit pos is  = 65 - 64
 */
enum external_pb1200_ints {
	PB1200_INT_BEGIN	= AU1000_MAX_INTR + 1,

	PB1200_IDE_INT		= PB1200_INT_BEGIN,
	PB1200_ETH_INT,
	PB1200_PC0_INT,
	PB1200_PC0_STSCHG_INT,
	PB1200_PC1_INT,
	PB1200_PC1_STSCHG_INT,
	PB1200_DC_INT,
	PB1200_FLASHBUSY_INT,
	PB1200_PC0_INSERT_INT,
	PB1200_PC0_EJECT_INT,
	PB1200_PC1_INSERT_INT,
	PB1200_PC1_EJECT_INT,
	PB1200_SD0_INSERT_INT,
	PB1200_SD0_EJECT_INT,
	PB1200_SD1_INSERT_INT,
	PB1200_SD1_EJECT_INT,

	PB1200_INT_END		= PB1200_INT_BEGIN + 15
};

/* NAND chip select */
#define NAND_CS 1

#endif /* __ASM_PB1200_H */

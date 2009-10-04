/*
 * AMD Alchemy DBAu1200 Reference Board
 * Board register defines.
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
#ifndef __ASM_DB1200_H
#define __ASM_DB1200_H

#include <linux/types.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_psc.h>

#define DBDMA_AC97_TX_CHAN	DSCR_CMD0_PSC1_TX
#define DBDMA_AC97_RX_CHAN	DSCR_CMD0_PSC1_RX
#define DBDMA_I2S_TX_CHAN	DSCR_CMD0_PSC1_TX
#define DBDMA_I2S_RX_CHAN	DSCR_CMD0_PSC1_RX

/*
 * SPI and SMB are muxed on the DBAu1200 board.
 * Refer to board documentation.
 */
#define SPI_PSC_BASE		PSC0_BASE_ADDR
#define SMBUS_PSC_BASE		PSC0_BASE_ADDR
/*
 * AC'97 and I2S are muxed on the DBAu1200 board.
 * Refer to board documentation.
 */
#define AC97_PSC_BASE		PSC1_BASE_ADDR
#define I2S_PSC_BASE		PSC1_BASE_ADDR

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

#define SMC91C111_PHYS_ADDR	0x19000300
#define SMC91C111_INT		DB1200_ETH_INT

#define IDE_PHYS_ADDR		0x18800000
#define IDE_REG_SHIFT		5
#define IDE_PHYS_LEN		(16 << IDE_REG_SHIFT)
#define IDE_INT 		DB1200_IDE_INT
#define IDE_DDMA_REQ		DSCR_CMD0_DMA_REQ1
#define IDE_RQSIZE		128

#define NAND_PHYS_ADDR		0x20000000

/*
 * External Interrupts for DBAu1200 as of 8/6/2004.
 * Bit positions in the CPLD registers can be calculated by taking
 * the interrupt define and subtracting the DB1200_INT_BEGIN value.
 *
 *   Example: IDE bis pos is  = 64 - 64
 *            ETH bit pos is  = 65 - 64
 */
enum external_pb1200_ints {
	DB1200_INT_BEGIN	= AU1000_MAX_INTR + 1,

	DB1200_IDE_INT		= DB1200_INT_BEGIN,
	DB1200_ETH_INT,
	DB1200_PC0_INT,
	DB1200_PC0_STSCHG_INT,
	DB1200_PC1_INT,
	DB1200_PC1_STSCHG_INT,
	DB1200_DC_INT,
	DB1200_FLASHBUSY_INT,
	DB1200_PC0_INSERT_INT,
	DB1200_PC0_EJECT_INT,
	DB1200_PC1_INSERT_INT,
	DB1200_PC1_EJECT_INT,
	DB1200_SD0_INSERT_INT,
	DB1200_SD0_EJECT_INT,

	DB1200_INT_END		= DB1200_INT_BEGIN + 15,
};


/*
 * DBAu1200 specific PCMCIA defines for drivers/pcmcia/au1000_db1x00.c
 */
#define PCMCIA_MAX_SOCK  1
#define PCMCIA_NUM_SOCKS (PCMCIA_MAX_SOCK + 1)

/* VPP/VCC */
#define SET_VCC_VPP(VCC, VPP, SLOT) \
	((((VCC) << 2) | ((VPP) << 0)) << ((SLOT) * 8))

#define BOARD_PC0_INT	DB1200_PC0_INT
#define BOARD_PC1_INT	DB1200_PC1_INT
#define BOARD_CARD_INSERTED(SOCKET) (bcsr_read(BCSR_SIGSTAT) & (1 << (8 + (2 * SOCKET))))

/* NAND chip select */
#define NAND_CS 1

#endif /* __ASM_DB1200_H */

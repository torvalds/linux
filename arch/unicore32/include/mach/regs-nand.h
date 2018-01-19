/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PKUnity NAND Controller Registers
 */
/*
 * ID Reg. 0 NAND_IDR0
 */
#define NAND_IDR0	(PKUNITY_NAND_BASE + 0x0000)
/*
 * ID Reg. 1 NAND_IDR1
 */
#define NAND_IDR1	(PKUNITY_NAND_BASE + 0x0004)
/*
 * ID Reg. 2 NAND_IDR2
 */
#define NAND_IDR2	(PKUNITY_NAND_BASE + 0x0008)
/*
 * ID Reg. 3 NAND_IDR3
 */
#define NAND_IDR3	(PKUNITY_NAND_BASE + 0x000C)
/*
 * Page Address Reg 0 NAND_PAR0
 */
#define NAND_PAR0	(PKUNITY_NAND_BASE + 0x0010)
/*
 * Page Address Reg 1 NAND_PAR1
 */
#define NAND_PAR1	(PKUNITY_NAND_BASE + 0x0014)
/*
 * Page Address Reg 2 NAND_PAR2
 */
#define NAND_PAR2	(PKUNITY_NAND_BASE + 0x0018)
/*
 * ECC Enable Reg NAND_ECCEN
 */
#define NAND_ECCEN	(PKUNITY_NAND_BASE + 0x001C)
/*
 * Buffer Reg NAND_BUF
 */
#define NAND_BUF	(PKUNITY_NAND_BASE + 0x0020)
/*
 * ECC Status Reg NAND_ECCSR
 */
#define NAND_ECCSR	(PKUNITY_NAND_BASE + 0x0024)
/*
 * Command Reg NAND_CMD
 */
#define NAND_CMD	(PKUNITY_NAND_BASE + 0x0028)
/*
 * DMA Configure Reg NAND_DMACR
 */
#define NAND_DMACR	(PKUNITY_NAND_BASE + 0x002C)
/*
 * Interrupt Reg NAND_IR
 */
#define NAND_IR		(PKUNITY_NAND_BASE + 0x0030)
/*
 * Interrupt Mask Reg NAND_IMR
 */
#define NAND_IMR	(PKUNITY_NAND_BASE + 0x0034)
/*
 * Chip Enable Reg NAND_CHIPEN
 */
#define NAND_CHIPEN	(PKUNITY_NAND_BASE + 0x0038)
/*
 * Address Reg NAND_ADDR
 */
#define NAND_ADDR	(PKUNITY_NAND_BASE + 0x003C)

/*
 * Command bits NAND_CMD_CMD_MASK
 */
#define NAND_CMD_CMD_MASK		FMASK(4, 4)
#define NAND_CMD_CMD_READPAGE		FIELD(0x0, 4, 4)
#define NAND_CMD_CMD_ERASEBLOCK		FIELD(0x6, 4, 4)
#define NAND_CMD_CMD_READSTATUS		FIELD(0x7, 4, 4)
#define NAND_CMD_CMD_WRITEPAGE		FIELD(0x8, 4, 4)
#define NAND_CMD_CMD_READID		FIELD(0x9, 4, 4)
#define NAND_CMD_CMD_RESET		FIELD(0xf, 4, 4)


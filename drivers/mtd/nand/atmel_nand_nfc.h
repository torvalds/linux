/*
 * Atmel Nand Flash Controller (NFC) - System peripherals regsters.
 * Based on SAMA5D3 datasheet.
 *
 * © Copyright 2013 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef ATMEL_NAND_NFC_H
#define ATMEL_NAND_NFC_H

/*
 * HSMC NFC registers
 */
#define ATMEL_HSMC_NFC_CFG	0x00		/* NFC Configuration Register */
#define		NFC_CFG_PAGESIZE	(7 << 0)
#define			NFC_CFG_PAGESIZE_512	(0 << 0)
#define			NFC_CFG_PAGESIZE_1024	(1 << 0)
#define			NFC_CFG_PAGESIZE_2048	(2 << 0)
#define			NFC_CFG_PAGESIZE_4096	(3 << 0)
#define			NFC_CFG_PAGESIZE_8192	(4 << 0)
#define		NFC_CFG_WSPARE		(1 << 8)
#define		NFC_CFG_RSPARE		(1 << 9)
#define		NFC_CFG_NFC_DTOCYC	(0xf << 16)
#define		NFC_CFG_NFC_DTOMUL	(0x7 << 20)
#define		NFC_CFG_NFC_SPARESIZE	(0x7f << 24)
#define		NFC_CFG_NFC_SPARESIZE_BIT_POS	24

#define ATMEL_HSMC_NFC_CTRL	0x04		/* NFC Control Register */
#define		NFC_CTRL_ENABLE		(1 << 0)
#define		NFC_CTRL_DISABLE	(1 << 1)

#define ATMEL_HSMC_NFC_SR	0x08		/* NFC Status Register */
#define		NFC_SR_XFR_DONE		(1 << 16)
#define		NFC_SR_CMD_DONE		(1 << 17)
#define		NFC_SR_RB_EDGE		(1 << 24)

#define ATMEL_HSMC_NFC_IER	0x0c
#define ATMEL_HSMC_NFC_IDR	0x10
#define ATMEL_HSMC_NFC_IMR	0x14
#define ATMEL_HSMC_NFC_CYCLE0	0x18		/* NFC Address Cycle Zero */
#define		ATMEL_HSMC_NFC_ADDR_CYCLE0	(0xff)

#define ATMEL_HSMC_NFC_BANK	0x1c		/* NFC Bank Register */
#define		ATMEL_HSMC_NFC_BANK0		(0 << 0)
#define		ATMEL_HSMC_NFC_BANK1		(1 << 0)

#define nfc_writel(addr, reg, value) \
	writel((value), (addr) + ATMEL_HSMC_NFC_##reg)

#define nfc_readl(addr, reg) \
	readl_relaxed((addr) + ATMEL_HSMC_NFC_##reg)

/*
 * NFC Address Command definitions
 */
#define NFCADDR_CMD_CMD1	(0xff << 2)	/* Command for Cycle 1 */
#define NFCADDR_CMD_CMD1_BIT_POS	2
#define NFCADDR_CMD_CMD2	(0xff << 10)	/* Command for Cycle 2 */
#define NFCADDR_CMD_CMD2_BIT_POS	10
#define NFCADDR_CMD_VCMD2	(0x1 << 18)	/* Valid Cycle 2 Command */
#define NFCADDR_CMD_ACYCLE	(0x7 << 19)	/* Number of Address required */
#define		NFCADDR_CMD_ACYCLE_NONE		(0x0 << 19)
#define		NFCADDR_CMD_ACYCLE_1		(0x1 << 19)
#define		NFCADDR_CMD_ACYCLE_2		(0x2 << 19)
#define		NFCADDR_CMD_ACYCLE_3		(0x3 << 19)
#define		NFCADDR_CMD_ACYCLE_4		(0x4 << 19)
#define		NFCADDR_CMD_ACYCLE_5		(0x5 << 19)
#define NFCADDR_CMD_ACYCLE_BIT_POS	19
#define NFCADDR_CMD_CSID	(0x7 << 22)	/* Chip Select Identifier */
#define		NFCADDR_CMD_CSID_0		(0x0 << 22)
#define		NFCADDR_CMD_CSID_1		(0x1 << 22)
#define		NFCADDR_CMD_CSID_2		(0x2 << 22)
#define		NFCADDR_CMD_CSID_3		(0x3 << 22)
#define		NFCADDR_CMD_CSID_4		(0x4 << 22)
#define		NFCADDR_CMD_CSID_5		(0x5 << 22)
#define		NFCADDR_CMD_CSID_6		(0x6 << 22)
#define		NFCADDR_CMD_CSID_7		(0x7 << 22)
#define NFCADDR_CMD_DATAEN	(0x1 << 25)	/* Data Transfer Enable */
#define NFCADDR_CMD_DATADIS	(0x0 << 25)	/* Data Transfer Disable */
#define NFCADDR_CMD_NFCRD	(0x0 << 26)	/* NFC Read Enable */
#define NFCADDR_CMD_NFCWR	(0x1 << 26)	/* NFC Write Enable */
#define NFCADDR_CMD_NFCBUSY	(0x1 << 27)	/* NFC Busy */

#define nfc_cmd_addr1234_writel(cmd, addr1234, nfc_base) \
	writel((addr1234), (cmd) + nfc_base)

#define nfc_cmd_readl(bitstatus, nfc_base) \
	readl_relaxed((bitstatus) + nfc_base)

#define NFC_TIME_OUT_MS		100
#define	NFC_SRAM_BANK1_OFFSET	0x1200

#endif

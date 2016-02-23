/*
 *  Toshiba PCI Secure Digital Host Controller Interface driver
 *
 *  Copyright (C) 2014 Ondrej Zary
 *  Copyright (C) 2007 Richard Betts, All Rights Reserved.
 *
 *      Based on asic3_mmc.c Copyright (c) 2005 SDG Systems, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#define HCLK	33000000	/* 33 MHz (PCI clock) */

#define SD_PCICFG_CLKSTOP	0x40	/* 0x1f = clock controller, 0 = stop */
#define SD_PCICFG_GATEDCLK	0x41	/* Gated clock */
#define SD_PCICFG_CLKMODE	0x42	/* Control clock of SD controller */
#define SD_PCICFG_PINSTATUS	0x44	/* R/O: read status of SD pins */
#define SD_PCICFG_POWER1	0x48
#define SD_PCICFG_POWER2	0x49
#define SD_PCICFG_POWER3	0x4a
#define SD_PCICFG_CARDDETECT	0x4c
#define SD_PCICFG_SLOTS		0x50	/* R/O: define support slot number */
#define SD_PCICFG_EXTGATECLK1	0xf0	/* Could be used for gated clock */
#define SD_PCICFG_EXTGATECLK2	0xf1	/* Could be used for gated clock */
#define SD_PCICFG_EXTGATECLK3	0xf9	/* Bit 1: double buffer/single buffer */
#define SD_PCICFG_SDLED_ENABLE1	0xfa
#define SD_PCICFG_SDLED_ENABLE2	0xfe

#define SD_PCICFG_CLKMODE_DIV_DISABLE	BIT(0)
#define SD_PCICFG_CLKSTOP_ENABLE_ALL	0x1f
#define SD_PCICFG_LED_ENABLE1_START	0x12
#define SD_PCICFG_LED_ENABLE2_START	0x80

#define SD_PCICFG_PWR1_33V	0x08	/* Set for 3.3 volts */
#define SD_PCICFG_PWR1_OFF	0x00	/* Turn off power */
#define SD_PCICFG_PWR2_AUTO	0x02

#define SD_CMD			0x00	/* also for SDIO */
#define SD_ARG0			0x04	/* also for SDIO */
#define SD_ARG1			0x06	/* also for SDIO */
#define SD_STOPINTERNAL		0x08
#define SD_BLOCKCOUNT		0x0a	/* also for SDIO */
#define SD_RESPONSE0		0x0c	/* also for SDIO */
#define SD_RESPONSE1		0x0e	/* also for SDIO */
#define SD_RESPONSE2		0x10	/* also for SDIO */
#define SD_RESPONSE3		0x12	/* also for SDIO */
#define SD_RESPONSE4		0x14	/* also for SDIO */
#define SD_RESPONSE5		0x16	/* also for SDIO */
#define SD_RESPONSE6		0x18	/* also for SDIO */
#define SD_RESPONSE7		0x1a	/* also for SDIO */
#define SD_CARDSTATUS		0x1c	/* also for SDIO */
#define SD_BUFFERCTRL		0x1e	/* also for SDIO */
#define SD_INTMASKCARD		0x20	/* also for SDIO */
#define SD_INTMASKBUFFER	0x22	/* also for SDIO */
#define SD_CARDCLOCKCTRL	0x24
#define SD_CARDXFERDATALEN	0x26	/* also for SDIO */
#define SD_CARDOPTIONSETUP	0x28	/* also for SDIO */
#define SD_ERRORSTATUS0		0x2c	/* also for SDIO */
#define SD_ERRORSTATUS1		0x2e	/* also for SDIO */
#define SD_DATAPORT		0x30	/* also for SDIO */
#define SD_TRANSACTIONCTRL	0x34	/* also for SDIO */
#define SD_SOFTWARERESET	0xe0	/* also for SDIO */

/* registers above marked "also for SDIO" and all SDIO registers below can be
 * accessed at SDIO_BASE + reg address */
#define SDIO_BASE	 0x100

#define SDIO_CARDPORTSEL	0x02
#define SDIO_CARDINTCTRL	0x36
#define SDIO_CLOCKNWAITCTRL	0x38
#define SDIO_HOSTINFORMATION	0x3a
#define SDIO_ERRORCTRL		0x3c
#define SDIO_LEDCTRL		0x3e

#define SD_TRANSCTL_SET		BIT(8)

#define SD_CARDCLK_DIV_DISABLE	BIT(15)
#define SD_CARDCLK_ENABLE_CLOCK	BIT(8)
#define SD_CARDCLK_CLK_DIV_512	BIT(7)
#define SD_CARDCLK_CLK_DIV_256	BIT(6)
#define SD_CARDCLK_CLK_DIV_128	BIT(5)
#define SD_CARDCLK_CLK_DIV_64	BIT(4)
#define SD_CARDCLK_CLK_DIV_32	BIT(3)
#define SD_CARDCLK_CLK_DIV_16	BIT(2)
#define SD_CARDCLK_CLK_DIV_8	BIT(1)
#define SD_CARDCLK_CLK_DIV_4	BIT(0)
#define SD_CARDCLK_CLK_DIV_2	0

#define SD_CARDOPT_REQUIRED		0x000e
#define SD_CARDOPT_DATA_RESP_TIMEOUT(x)	(((x) & 0x0f) << 4) /* 4 bits */
#define SD_CARDOPT_C2_MODULE_ABSENT	BIT(14)
#define SD_CARDOPT_DATA_XFR_WIDTH_1	(1 << 15)
#define SD_CARDOPT_DATA_XFR_WIDTH_4	(0 << 15)

#define SD_CMD_TYPE_CMD			(0 << 6)
#define SD_CMD_TYPE_ACMD		(1 << 6)
#define SD_CMD_TYPE_AUTHEN		(2 << 6)
#define SD_CMD_RESP_TYPE_NONE		(3 << 8)
#define SD_CMD_RESP_TYPE_EXT_R1		(4 << 8)
#define SD_CMD_RESP_TYPE_EXT_R1B	(5 << 8)
#define SD_CMD_RESP_TYPE_EXT_R2		(6 << 8)
#define SD_CMD_RESP_TYPE_EXT_R3		(7 << 8)
#define SD_CMD_RESP_TYPE_EXT_R6		(4 << 8)
#define SD_CMD_RESP_TYPE_EXT_R7		(4 << 8)
#define SD_CMD_DATA_PRESENT		BIT(11)
#define SD_CMD_TRANSFER_READ		BIT(12)
#define SD_CMD_MULTI_BLOCK		BIT(13)
#define SD_CMD_SECURITY_CMD		BIT(14)

#define SD_STOPINT_ISSUE_CMD12		BIT(0)
#define SD_STOPINT_AUTO_ISSUE_CMD12	BIT(8)

#define SD_CARD_RESP_END	BIT(0)
#define SD_CARD_RW_END		BIT(2)
#define SD_CARD_CARD_REMOVED_0	BIT(3)
#define SD_CARD_CARD_INSERTED_0	BIT(4)
#define SD_CARD_PRESENT_0	BIT(5)
#define SD_CARD_UNK6		BIT(6)
#define SD_CARD_WRITE_PROTECT	BIT(7)
#define SD_CARD_CARD_REMOVED_3	BIT(8)
#define SD_CARD_CARD_INSERTED_3	BIT(9)
#define SD_CARD_PRESENT_3	BIT(10)

#define SD_BUF_CMD_INDEX_ERR	BIT(16)
#define SD_BUF_CRC_ERR		BIT(17)
#define SD_BUF_STOP_BIT_END_ERR	BIT(18)
#define SD_BUF_DATA_TIMEOUT	BIT(19)
#define SD_BUF_OVERFLOW		BIT(20)
#define SD_BUF_UNDERFLOW	BIT(21)
#define SD_BUF_CMD_TIMEOUT	BIT(22)
#define SD_BUF_UNK7		BIT(23)
#define SD_BUF_READ_ENABLE	BIT(24)
#define SD_BUF_WRITE_ENABLE	BIT(25)
#define SD_BUF_ILLEGAL_FUNCTION	BIT(29)
#define SD_BUF_CMD_BUSY		BIT(30)
#define SD_BUF_ILLEGAL_ACCESS	BIT(31)

#define SD_ERR0_RESP_CMD_ERR			BIT(0)
#define SD_ERR0_RESP_NON_CMD12_END_BIT_ERR	BIT(2)
#define SD_ERR0_RESP_CMD12_END_BIT_ERR		BIT(3)
#define SD_ERR0_READ_DATA_END_BIT_ERR		BIT(4)
#define SD_ERR0_WRITE_CRC_STATUS_END_BIT_ERR	BIT(5)
#define SD_ERR0_RESP_NON_CMD12_CRC_ERR		BIT(8)
#define SD_ERR0_RESP_CMD12_CRC_ERR		BIT(9)
#define SD_ERR0_READ_DATA_CRC_ERR		BIT(10)
#define SD_ERR0_WRITE_CMD_CRC_ERR		BIT(11)

#define SD_ERR1_NO_CMD_RESP		BIT(16)
#define SD_ERR1_TIMEOUT_READ_DATA	BIT(20)
#define SD_ERR1_TIMEOUT_CRS_STATUS	BIT(21)
#define SD_ERR1_TIMEOUT_CRC_BUSY	BIT(22)

#define IRQ_DONT_CARE_BITS (SD_CARD_PRESENT_3 \
	| SD_CARD_WRITE_PROTECT \
	| SD_CARD_UNK6 \
	| SD_CARD_PRESENT_0 \
	| SD_BUF_UNK7 \
	| SD_BUF_CMD_BUSY)

struct toshsd_host {
	struct pci_dev *pdev;
	struct mmc_host *mmc;

	spinlock_t lock;

	struct mmc_request *mrq;/* Current request */
	struct mmc_command *cmd;/* Current command */
	struct mmc_data *data;	/* Current data request */

	struct sg_mapping_iter sg_miter; /* for PIO */

	void __iomem *ioaddr; /* mapped address */
};

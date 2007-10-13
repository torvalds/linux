/*
 *  linux/drivers/mmc/host/wbsd.h - Winbond W83L51xD SD/MMC driver
 *
 *  Copyright (C) 2004-2007 Pierre Ossman, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#define LOCK_CODE		0xAA

#define WBSD_CONF_SWRST		0x02
#define WBSD_CONF_DEVICE	0x07
#define WBSD_CONF_ID_HI		0x20
#define WBSD_CONF_ID_LO		0x21
#define WBSD_CONF_POWER		0x22
#define WBSD_CONF_PME		0x23
#define WBSD_CONF_PMES		0x24

#define WBSD_CONF_ENABLE	0x30
#define WBSD_CONF_PORT_HI	0x60
#define WBSD_CONF_PORT_LO	0x61
#define WBSD_CONF_IRQ		0x70
#define WBSD_CONF_DRQ		0x74

#define WBSD_CONF_PINS		0xF0

#define DEVICE_SD		0x03

#define WBSD_PINS_DAT3_HI	0x20
#define WBSD_PINS_DAT3_OUT	0x10
#define WBSD_PINS_GP11_HI	0x04
#define WBSD_PINS_DETECT_GP11	0x02
#define WBSD_PINS_DETECT_DAT3	0x01

#define WBSD_CMDR		0x00
#define WBSD_DFR		0x01
#define WBSD_EIR		0x02
#define WBSD_ISR		0x03
#define WBSD_FSR		0x04
#define WBSD_IDXR		0x05
#define WBSD_DATAR		0x06
#define WBSD_CSR		0x07

#define WBSD_EINT_CARD		0x40
#define WBSD_EINT_FIFO_THRE	0x20
#define WBSD_EINT_CRC		0x10
#define WBSD_EINT_TIMEOUT	0x08
#define WBSD_EINT_PROGEND	0x04
#define WBSD_EINT_BUSYEND	0x02
#define WBSD_EINT_TC		0x01

#define WBSD_INT_PENDING	0x80
#define WBSD_INT_CARD		0x40
#define WBSD_INT_FIFO_THRE	0x20
#define WBSD_INT_CRC		0x10
#define WBSD_INT_TIMEOUT	0x08
#define WBSD_INT_PROGEND	0x04
#define WBSD_INT_BUSYEND	0x02
#define WBSD_INT_TC		0x01

#define WBSD_FIFO_EMPTY		0x80
#define WBSD_FIFO_FULL		0x40
#define WBSD_FIFO_EMTHRE	0x20
#define WBSD_FIFO_FUTHRE	0x10
#define WBSD_FIFO_SZMASK	0x0F

#define WBSD_MSLED		0x20
#define WBSD_POWER_N		0x10
#define WBSD_WRPT		0x04
#define WBSD_CARDPRESENT	0x01

#define WBSD_IDX_CLK		0x01
#define WBSD_IDX_PBSMSB		0x02
#define WBSD_IDX_TAAC		0x03
#define WBSD_IDX_NSAC		0x04
#define WBSD_IDX_PBSLSB		0x05
#define WBSD_IDX_SETUP		0x06
#define WBSD_IDX_DMA		0x07
#define WBSD_IDX_FIFOEN		0x08
#define WBSD_IDX_STATUS		0x10
#define WBSD_IDX_RSPLEN		0x1E
#define WBSD_IDX_RESP0		0x1F
#define WBSD_IDX_RESP1		0x20
#define WBSD_IDX_RESP2		0x21
#define WBSD_IDX_RESP3		0x22
#define WBSD_IDX_RESP4		0x23
#define WBSD_IDX_RESP5		0x24
#define WBSD_IDX_RESP6		0x25
#define WBSD_IDX_RESP7		0x26
#define WBSD_IDX_RESP8		0x27
#define WBSD_IDX_RESP9		0x28
#define WBSD_IDX_RESP10		0x29
#define WBSD_IDX_RESP11		0x2A
#define WBSD_IDX_RESP12		0x2B
#define WBSD_IDX_RESP13		0x2C
#define WBSD_IDX_RESP14		0x2D
#define WBSD_IDX_RESP15		0x2E
#define WBSD_IDX_RESP16		0x2F
#define WBSD_IDX_CRCSTATUS	0x30
#define WBSD_IDX_ISR		0x3F

#define WBSD_CLK_375K		0x00
#define WBSD_CLK_12M		0x01
#define WBSD_CLK_16M		0x02
#define WBSD_CLK_24M		0x03

#define WBSD_DATA_WIDTH		0x01

#define WBSD_DAT3_H		0x08
#define WBSD_FIFO_RESET		0x04
#define WBSD_SOFT_RESET		0x02
#define WBSD_INC_INDEX		0x01

#define WBSD_DMA_SINGLE		0x02
#define WBSD_DMA_ENABLE		0x01

#define WBSD_FIFOEN_EMPTY	0x20
#define WBSD_FIFOEN_FULL	0x10
#define WBSD_FIFO_THREMASK	0x0F

#define WBSD_BLOCK_READ		0x80
#define WBSD_BLOCK_WRITE	0x40
#define WBSD_BUSY		0x20
#define WBSD_CARDTRAFFIC	0x04
#define WBSD_SENDCMD		0x02
#define WBSD_RECVRES		0x01

#define WBSD_RSP_SHORT		0x00
#define WBSD_RSP_LONG		0x01

#define WBSD_CRC_MASK		0x1F
#define WBSD_CRC_OK		0x05 /* S010E (00101) */
#define WBSD_CRC_FAIL		0x0B /* S101E (01011) */

#define WBSD_DMA_SIZE		65536

struct wbsd_host
{
	struct mmc_host*	mmc;		/* MMC structure */

	spinlock_t		lock;		/* Mutex */

	int			flags;		/* Driver states */

#define WBSD_FCARD_PRESENT	(1<<0)		/* Card is present */
#define WBSD_FIGNORE_DETECT	(1<<1)		/* Ignore card detection */

	struct mmc_request*	mrq;		/* Current request */

	u8			isr;		/* Accumulated ISR */

	struct scatterlist*	cur_sg;		/* Current SG entry */
	unsigned int		num_sg;		/* Number of entries left */

	unsigned int		offset;		/* Offset into current entry */
	unsigned int		remain;		/* Data left in curren entry */

	char*			dma_buffer;	/* ISA DMA buffer */
	dma_addr_t		dma_addr;	/* Physical address for same */

	int			firsterr;	/* See fifo functions */

	u8			clk;		/* Current clock speed */
	unsigned char		bus_width;	/* Current bus width */

	int			config;		/* Config port */
	u8			unlock_code;	/* Code to unlock config */

	int			chip_id;	/* ID of controller */

	int			base;		/* I/O port base */
	int			irq;		/* Interrupt */
	int			dma;		/* DMA channel */

	struct tasklet_struct	card_tasklet;	/* Tasklet structures */
	struct tasklet_struct	fifo_tasklet;
	struct tasklet_struct	crc_tasklet;
	struct tasklet_struct	timeout_tasklet;
	struct tasklet_struct	finish_tasklet;

	struct timer_list	ignore_timer;	/* Ignore detection timer */
};

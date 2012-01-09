/*
 * Atmel MultiMedia Card Interface driver
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Superset of MCI IP registers integrated in Atmel AVR32 and AT91 Processors
 * Registers and bitfields marked with [2] are only available in MCI2
 */

#ifndef __DRIVERS_MMC_ATMEL_MCI_H__
#define __DRIVERS_MMC_ATMEL_MCI_H__

/* MCI Register Definitions */
#define ATMCI_CR			0x0000	/* Control */
# define ATMCI_CR_MCIEN			(  1 <<  0)	/* MCI Enable */
# define ATMCI_CR_MCIDIS		(  1 <<  1)	/* MCI Disable */
# define ATMCI_CR_PWSEN			(  1 <<  2)	/* Power Save Enable */
# define ATMCI_CR_PWSDIS		(  1 <<  3)	/* Power Save Disable */
# define ATMCI_CR_SWRST			(  1 <<  7)	/* Software Reset */
#define ATMCI_MR			0x0004	/* Mode */
# define ATMCI_MR_CLKDIV(x)		((x) <<  0)	/* Clock Divider */
# define ATMCI_MR_PWSDIV(x)		((x) <<  8)	/* Power Saving Divider */
# define ATMCI_MR_RDPROOF		(  1 << 11)	/* Read Proof */
# define ATMCI_MR_WRPROOF		(  1 << 12)	/* Write Proof */
# define ATMCI_MR_PDCFBYTE		(  1 << 13)	/* Force Byte Transfer */
# define ATMCI_MR_PDCPADV		(  1 << 14)	/* Padding Value */
# define ATMCI_MR_PDCMODE		(  1 << 15)	/* PDC-oriented Mode */
#define ATMCI_DTOR			0x0008	/* Data Timeout */
# define ATMCI_DTOCYC(x)		((x) <<  0)	/* Data Timeout Cycles */
# define ATMCI_DTOMUL(x)		((x) <<  4)	/* Data Timeout Multiplier */
#define ATMCI_SDCR			0x000c	/* SD Card / SDIO */
# define ATMCI_SDCSEL_SLOT_A		(  0 <<  0)	/* Select SD slot A */
# define ATMCI_SDCSEL_SLOT_B		(  1 <<  0)	/* Select SD slot A */
# define ATMCI_SDCSEL_MASK		(  3 <<  0)
# define ATMCI_SDCBUS_1BIT		(  0 <<  6)	/* 1-bit data bus */
# define ATMCI_SDCBUS_4BIT		(  2 <<  6)	/* 4-bit data bus */
# define ATMCI_SDCBUS_8BIT		(  3 <<  6)	/* 8-bit data bus[2] */
# define ATMCI_SDCBUS_MASK		(  3 <<  6)
#define ATMCI_ARGR			0x0010	/* Command Argument */
#define ATMCI_CMDR			0x0014	/* Command */
# define ATMCI_CMDR_CMDNB(x)		((x) <<  0)	/* Command Opcode */
# define ATMCI_CMDR_RSPTYP_NONE		(  0 <<  6)	/* No response */
# define ATMCI_CMDR_RSPTYP_48BIT	(  1 <<  6)	/* 48-bit response */
# define ATMCI_CMDR_RSPTYP_136BIT	(  2 <<  6)	/* 136-bit response */
# define ATMCI_CMDR_SPCMD_INIT		(  1 <<  8)	/* Initialization command */
# define ATMCI_CMDR_SPCMD_SYNC		(  2 <<  8)	/* Synchronized command */
# define ATMCI_CMDR_SPCMD_INT		(  4 <<  8)	/* Interrupt command */
# define ATMCI_CMDR_SPCMD_INTRESP	(  5 <<  8)	/* Interrupt response */
# define ATMCI_CMDR_OPDCMD		(  1 << 11)	/* Open Drain */
# define ATMCI_CMDR_MAXLAT_5CYC		(  0 << 12)	/* Max latency 5 cycles */
# define ATMCI_CMDR_MAXLAT_64CYC	(  1 << 12)	/* Max latency 64 cycles */
# define ATMCI_CMDR_START_XFER		(  1 << 16)	/* Start data transfer */
# define ATMCI_CMDR_STOP_XFER		(  2 << 16)	/* Stop data transfer */
# define ATMCI_CMDR_TRDIR_WRITE		(  0 << 18)	/* Write data */
# define ATMCI_CMDR_TRDIR_READ		(  1 << 18)	/* Read data */
# define ATMCI_CMDR_BLOCK		(  0 << 19)	/* Single-block transfer */
# define ATMCI_CMDR_MULTI_BLOCK		(  1 << 19)	/* Multi-block transfer */
# define ATMCI_CMDR_STREAM		(  2 << 19)	/* MMC Stream transfer */
# define ATMCI_CMDR_SDIO_BYTE		(  4 << 19)	/* SDIO Byte transfer */
# define ATMCI_CMDR_SDIO_BLOCK		(  5 << 19)	/* SDIO Block transfer */
# define ATMCI_CMDR_SDIO_SUSPEND	(  1 << 24)	/* SDIO Suspend Command */
# define ATMCI_CMDR_SDIO_RESUME		(  2 << 24)	/* SDIO Resume Command */
#define ATMCI_BLKR			0x0018	/* Block */
# define ATMCI_BCNT(x)			((x) <<  0)	/* Data Block Count */
# define ATMCI_BLKLEN(x)		((x) << 16)	/* Data Block Length */
#define ATMCI_CSTOR			0x001c	/* Completion Signal Timeout[2] */
# define ATMCI_CSTOCYC(x)		((x) <<  0)	/* CST cycles */
# define ATMCI_CSTOMUL(x)		((x) <<  4)	/* CST multiplier */
#define ATMCI_RSPR			0x0020	/* Response 0 */
#define ATMCI_RSPR1			0x0024	/* Response 1 */
#define ATMCI_RSPR2			0x0028	/* Response 2 */
#define ATMCI_RSPR3			0x002c	/* Response 3 */
#define ATMCI_RDR			0x0030	/* Receive Data */
#define ATMCI_TDR			0x0034	/* Transmit Data */
#define ATMCI_SR			0x0040	/* Status */
#define ATMCI_IER			0x0044	/* Interrupt Enable */
#define ATMCI_IDR			0x0048	/* Interrupt Disable */
#define ATMCI_IMR			0x004c	/* Interrupt Mask */
# define ATMCI_CMDRDY			(  1 <<   0)	/* Command Ready */
# define ATMCI_RXRDY			(  1 <<   1)	/* Receiver Ready */
# define ATMCI_TXRDY			(  1 <<   2)	/* Transmitter Ready */
# define ATMCI_BLKE			(  1 <<   3)	/* Data Block Ended */
# define ATMCI_DTIP			(  1 <<   4)	/* Data Transfer In Progress */
# define ATMCI_NOTBUSY			(  1 <<   5)	/* Data Not Busy */
# define ATMCI_ENDRX			(  1 <<   6)    /* End of RX Buffer */
# define ATMCI_ENDTX			(  1 <<   7)    /* End of TX Buffer */
# define ATMCI_SDIOIRQA			(  1 <<   8)	/* SDIO IRQ in slot A */
# define ATMCI_SDIOIRQB			(  1 <<   9)	/* SDIO IRQ in slot B */
# define ATMCI_SDIOWAIT			(  1 <<  12)    /* SDIO Read Wait Operation Status */
# define ATMCI_CSRCV			(  1 <<  13)    /* CE-ATA Completion Signal Received */
# define ATMCI_RXBUFF			(  1 <<  14)    /* RX Buffer Full */
# define ATMCI_TXBUFE			(  1 <<  15)    /* TX Buffer Empty */
# define ATMCI_RINDE			(  1 <<  16)	/* Response Index Error */
# define ATMCI_RDIRE			(  1 <<  17)	/* Response Direction Error */
# define ATMCI_RCRCE			(  1 <<  18)	/* Response CRC Error */
# define ATMCI_RENDE			(  1 <<  19)	/* Response End Bit Error */
# define ATMCI_RTOE			(  1 <<  20)	/* Response Time-Out Error */
# define ATMCI_DCRCE			(  1 <<  21)	/* Data CRC Error */
# define ATMCI_DTOE			(  1 <<  22)	/* Data Time-Out Error */
# define ATMCI_CSTOE			(  1 <<  23)    /* Completion Signal Time-out Error */
# define ATMCI_BLKOVRE			(  1 <<  24)    /* DMA Block Overrun Error */
# define ATMCI_DMADONE			(  1 <<  25)    /* DMA Transfer Done */
# define ATMCI_FIFOEMPTY		(  1 <<  26)    /* FIFO Empty Flag */
# define ATMCI_XFRDONE			(  1 <<  27)    /* Transfer Done Flag */
# define ATMCI_ACKRCV			(  1 <<  28)    /* Boot Operation Acknowledge Received */
# define ATMCI_ACKRCVE			(  1 <<  29)    /* Boot Operation Acknowledge Error */
# define ATMCI_OVRE			(  1 <<  30)	/* RX Overrun Error */
# define ATMCI_UNRE			(  1 <<  31)	/* TX Underrun Error */
#define ATMCI_DMA			0x0050	/* DMA Configuration[2] */
# define ATMCI_DMA_OFFSET(x)		((x) <<  0)	/* DMA Write Buffer Offset */
# define ATMCI_DMA_CHKSIZE(x)		((x) <<  4)	/* DMA Channel Read and Write Chunk Size */
# define ATMCI_DMAEN			(  1 <<  8)	/* DMA Hardware Handshaking Enable */
#define ATMCI_CFG			0x0054	/* Configuration[2] */
# define ATMCI_CFG_FIFOMODE_1DATA	(  1 <<  0)	/* MCI Internal FIFO control mode */
# define ATMCI_CFG_FERRCTRL_COR		(  1 <<  4)	/* Flow Error flag reset control mode */
# define ATMCI_CFG_HSMODE		(  1 <<  8)	/* High Speed Mode */
# define ATMCI_CFG_LSYNC		(  1 << 12)	/* Synchronize on the last block */
#define ATMCI_WPMR			0x00e4	/* Write Protection Mode[2] */
# define ATMCI_WP_EN			(  1 <<  0)	/* WP Enable */
# define ATMCI_WP_KEY			(0x4d4349 << 8)	/* WP Key */
#define ATMCI_WPSR			0x00e8	/* Write Protection Status[2] */
# define ATMCI_GET_WP_VS(x)		((x) & 0x0f)
# define ATMCI_GET_WP_VSRC(x)		(((x) >> 8) & 0xffff)
#define ATMCI_VERSION			0x00FC  /* Version */
#define ATMCI_FIFO_APERTURE		0x0200	/* FIFO Aperture[2] */

/* This is not including the FIFO Aperture on MCI2 */
#define ATMCI_REGS_SIZE		0x100

/* Register access macros */
#define atmci_readl(port,reg)				\
	__raw_readl((port)->regs + reg)
#define atmci_writel(port,reg,value)			\
	__raw_writel((value), (port)->regs + reg)

#endif /* __DRIVERS_MMC_ATMEL_MCI_H__ */

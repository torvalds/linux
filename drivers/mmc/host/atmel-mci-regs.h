/*
 * Atmel MultiMedia Card Interface driver
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __DRIVERS_MMC_ATMEL_MCI_H__
#define __DRIVERS_MMC_ATMEL_MCI_H__

/* MCI Register Definitions */
#define MCI_CR			0x0000	/* Control */
# define MCI_CR_MCIEN		(  1 <<  0)	/* MCI Enable */
# define MCI_CR_MCIDIS		(  1 <<  1)	/* MCI Disable */
# define MCI_CR_SWRST		(  1 <<  7)	/* Software Reset */
#define MCI_MR			0x0004	/* Mode */
# define MCI_MR_CLKDIV(x)	((x) <<  0)	/* Clock Divider */
# define MCI_MR_RDPROOF		(  1 << 11)	/* Read Proof */
# define MCI_MR_WRPROOF		(  1 << 12)	/* Write Proof */
#define MCI_DTOR		0x0008	/* Data Timeout */
# define MCI_DTOCYC(x)		((x) <<  0)	/* Data Timeout Cycles */
# define MCI_DTOMUL(x)		((x) <<  4)	/* Data Timeout Multiplier */
#define MCI_SDCR		0x000c	/* SD Card / SDIO */
# define MCI_SDCSEL_SLOT_A	(  0 <<  0)	/* Select SD slot A */
# define MCI_SDCSEL_SLOT_B	(  1 <<  0)	/* Select SD slot A */
# define MCI_SDCSEL_MASK	(  3 <<  0)
# define MCI_SDCBUS_1BIT	(  0 <<  6)	/* 1-bit data bus */
# define MCI_SDCBUS_4BIT	(  2 <<  6)	/* 4-bit data bus */
# define MCI_SDCBUS_MASK	(  3 <<  6)
#define MCI_ARGR		0x0010	/* Command Argument */
#define MCI_CMDR		0x0014	/* Command */
# define MCI_CMDR_CMDNB(x)	((x) <<  0)	/* Command Opcode */
# define MCI_CMDR_RSPTYP_NONE	(  0 <<  6)	/* No response */
# define MCI_CMDR_RSPTYP_48BIT	(  1 <<  6)	/* 48-bit response */
# define MCI_CMDR_RSPTYP_136BIT	(  2 <<  6)	/* 136-bit response */
# define MCI_CMDR_SPCMD_INIT	(  1 <<  8)	/* Initialization command */
# define MCI_CMDR_SPCMD_SYNC	(  2 <<  8)	/* Synchronized command */
# define MCI_CMDR_SPCMD_INT	(  4 <<  8)	/* Interrupt command */
# define MCI_CMDR_SPCMD_INTRESP	(  5 <<  8)	/* Interrupt response */
# define MCI_CMDR_OPDCMD	(  1 << 11)	/* Open Drain */
# define MCI_CMDR_MAXLAT_5CYC	(  0 << 12)	/* Max latency 5 cycles */
# define MCI_CMDR_MAXLAT_64CYC	(  1 << 12)	/* Max latency 64 cycles */
# define MCI_CMDR_START_XFER	(  1 << 16)	/* Start data transfer */
# define MCI_CMDR_STOP_XFER	(  2 << 16)	/* Stop data transfer */
# define MCI_CMDR_TRDIR_WRITE	(  0 << 18)	/* Write data */
# define MCI_CMDR_TRDIR_READ	(  1 << 18)	/* Read data */
# define MCI_CMDR_BLOCK		(  0 << 19)	/* Single-block transfer */
# define MCI_CMDR_MULTI_BLOCK	(  1 << 19)	/* Multi-block transfer */
# define MCI_CMDR_STREAM	(  2 << 19)	/* MMC Stream transfer */
# define MCI_CMDR_SDIO_BYTE	(  4 << 19)	/* SDIO Byte transfer */
# define MCI_CMDR_SDIO_BLOCK	(  5 << 19)	/* SDIO Block transfer */
# define MCI_CMDR_SDIO_SUSPEND	(  1 << 24)	/* SDIO Suspend Command */
# define MCI_CMDR_SDIO_RESUME	(  2 << 24)	/* SDIO Resume Command */
#define MCI_BLKR		0x0018	/* Block */
# define MCI_BCNT(x)		((x) <<  0)	/* Data Block Count */
# define MCI_BLKLEN(x)		((x) << 16)	/* Data Block Length */
#define MCI_RSPR		0x0020	/* Response 0 */
#define MCI_RSPR1		0x0024	/* Response 1 */
#define MCI_RSPR2		0x0028	/* Response 2 */
#define MCI_RSPR3		0x002c	/* Response 3 */
#define MCI_RDR			0x0030	/* Receive Data */
#define MCI_TDR			0x0034	/* Transmit Data */
#define MCI_SR			0x0040	/* Status */
#define MCI_IER			0x0044	/* Interrupt Enable */
#define MCI_IDR			0x0048	/* Interrupt Disable */
#define MCI_IMR			0x004c	/* Interrupt Mask */
# define MCI_CMDRDY		(  1 <<   0)	/* Command Ready */
# define MCI_RXRDY		(  1 <<   1)	/* Receiver Ready */
# define MCI_TXRDY		(  1 <<   2)	/* Transmitter Ready */
# define MCI_BLKE		(  1 <<   3)	/* Data Block Ended */
# define MCI_DTIP		(  1 <<   4)	/* Data Transfer In Progress */
# define MCI_NOTBUSY		(  1 <<   5)	/* Data Not Busy */
# define MCI_SDIOIRQA		(  1 <<   8)	/* SDIO IRQ in slot A */
# define MCI_SDIOIRQB		(  1 <<   9)	/* SDIO IRQ in slot B */
# define MCI_RINDE		(  1 <<  16)	/* Response Index Error */
# define MCI_RDIRE		(  1 <<  17)	/* Response Direction Error */
# define MCI_RCRCE		(  1 <<  18)	/* Response CRC Error */
# define MCI_RENDE		(  1 <<  19)	/* Response End Bit Error */
# define MCI_RTOE		(  1 <<  20)	/* Response Time-Out Error */
# define MCI_DCRCE		(  1 <<  21)	/* Data CRC Error */
# define MCI_DTOE		(  1 <<  22)	/* Data Time-Out Error */
# define MCI_OVRE		(  1 <<  30)	/* RX Overrun Error */
# define MCI_UNRE		(  1 <<  31)	/* TX Underrun Error */

#define MCI_REGS_SIZE		0x100

/* Register access macros */
#define mci_readl(port,reg)				\
	__raw_readl((port)->regs + MCI_##reg)
#define mci_writel(port,reg,value)			\
	__raw_writel((value), (port)->regs + MCI_##reg)

#endif /* __DRIVERS_MMC_ATMEL_MCI_H__ */

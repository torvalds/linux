/*
 * arch/arm/mach-at91/include/mach/at91_mci.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * MultiMedia Card Interface (MCI) registers.
 * Based on AT91RM9200 datasheet revision F.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_MCI_H
#define AT91_MCI_H

#define AT91_MCI_CR		0x00		/* Control Register */
#define		AT91_MCI_MCIEN		(1 <<  0)	/* Multi-Media Interface Enable */
#define		AT91_MCI_MCIDIS		(1 <<  1)	/* Multi-Media Interface Disable */
#define		AT91_MCI_PWSEN		(1 <<  2)	/* Power Save Mode Enable */
#define		AT91_MCI_PWSDIS		(1 <<  3)	/* Power Save Mode Disable */
#define		AT91_MCI_SWRST		(1 <<  7)	/* Software Reset */

#define AT91_MCI_MR		0x04		/* Mode Register */
#define		AT91_MCI_CLKDIV		(0xff  <<  0)	/* Clock Divider */
#define		AT91_MCI_PWSDIV		(7     <<  8)	/* Power Saving Divider */
#define		AT91_MCI_RDPROOF	(1     << 11)	/* Read Proof Enable [SAM926[03] only] */
#define		AT91_MCI_WRPROOF	(1     << 12)	/* Write Proof Enable [SAM926[03] only] */
#define		AT91_MCI_PDCFBYTE	(1     << 13)	/* PDC Force Byte Transfer [SAM926[03] only] */
#define		AT91_MCI_PDCPADV	(1     << 14)	/* PDC Padding Value */
#define		AT91_MCI_PDCMODE	(1     << 15)	/* PDC-orientated Mode */
#define		AT91_MCI_BLKLEN		(0xfff << 18)	/* Data Block Length */

#define AT91_MCI_DTOR		0x08		/* Data Timeout Register */
#define		AT91_MCI_DTOCYC		(0xf << 0)	/* Data Timeout Cycle Number */
#define		AT91_MCI_DTOMUL		(7   << 4)	/* Data Timeout Multiplier */
#define		AT91_MCI_DTOMUL_1		(0 <<  4)
#define		AT91_MCI_DTOMUL_16		(1 <<  4)
#define		AT91_MCI_DTOMUL_128		(2 <<  4)
#define		AT91_MCI_DTOMUL_256		(3 <<  4)
#define		AT91_MCI_DTOMUL_1K		(4 <<  4)
#define		AT91_MCI_DTOMUL_4K		(5 <<  4)
#define		AT91_MCI_DTOMUL_64K		(6 <<  4)
#define		AT91_MCI_DTOMUL_1M		(7 <<  4)

#define AT91_MCI_SDCR		0x0c		/* SD Card Register */
#define		AT91_MCI_SDCSEL		(3 << 0)	/* SD Card Selector */
#define		AT91_MCI_SDCBUS		(1 << 7)	/* 1-bit or 4-bit bus */

#define AT91_MCI_ARGR		0x10		/* Argument Register */

#define AT91_MCI_CMDR		0x14		/* Command Register */
#define		AT91_MCI_CMDNB		(0x3f << 0)	/* Command Number */
#define		AT91_MCI_RSPTYP		(3    << 6)	/* Response Type */
#define			AT91_MCI_RSPTYP_NONE	(0 <<  6)
#define			AT91_MCI_RSPTYP_48	(1 <<  6)
#define			AT91_MCI_RSPTYP_136	(2 <<  6)
#define		AT91_MCI_SPCMD		(7    << 8)	/* Special Command */
#define			AT91_MCI_SPCMD_NONE	(0 <<  8)
#define			AT91_MCI_SPCMD_INIT	(1 <<  8)
#define			AT91_MCI_SPCMD_SYNC	(2 <<  8)
#define			AT91_MCI_SPCMD_ICMD	(4 <<  8)
#define			AT91_MCI_SPCMD_IRESP	(5 <<  8)
#define		AT91_MCI_OPDCMD		(1 << 11)	/* Open Drain Command */
#define		AT91_MCI_MAXLAT		(1 << 12)	/* Max Latency for Command to Response */
#define		AT91_MCI_TRCMD		(3 << 16)	/* Transfer Command */
#define			AT91_MCI_TRCMD_NONE	(0 << 16)
#define			AT91_MCI_TRCMD_START	(1 << 16)
#define			AT91_MCI_TRCMD_STOP	(2 << 16)
#define		AT91_MCI_TRDIR		(1 << 18)	/* Transfer Direction */
#define		AT91_MCI_TRTYP		(3 << 19)	/* Transfer Type */
#define			AT91_MCI_TRTYP_BLOCK	(0 << 19)
#define			AT91_MCI_TRTYP_MULTIPLE	(1 << 19)
#define			AT91_MCI_TRTYP_STREAM	(2 << 19)
#define			AT91_MCI_TRTYP_SDIO_BYTE	(4 << 19)
#define			AT91_MCI_TRTYP_SDIO_BLOCK	(5 << 19)

#define AT91_MCI_BLKR		0x18		/* Block Register */
#define		AT91_MCI_BLKR_BCNT(n)	((0xffff & (n)) << 0)	/* Block count */
#define		AT91_MCI_BLKR_BLKLEN(n)	((0xffff & (n)) << 16)	/* Block length */

#define AT91_MCI_RSPR(n)	(0x20 + ((n) * 4))	/* Response Registers 0-3 */
#define AT91_MCR_RDR		0x30		/* Receive Data Register */
#define AT91_MCR_TDR		0x34		/* Transmit Data Register */

#define AT91_MCI_SR		0x40		/* Status Register */
#define		AT91_MCI_CMDRDY		(1 <<  0)	/* Command Ready */
#define		AT91_MCI_RXRDY		(1 <<  1)	/* Receiver Ready */
#define		AT91_MCI_TXRDY		(1 <<  2)	/* Transmit Ready */
#define		AT91_MCI_BLKE		(1 <<  3)	/* Data Block Ended */
#define		AT91_MCI_DTIP		(1 <<  4)	/* Data Transfer in Progress */
#define		AT91_MCI_NOTBUSY	(1 <<  5)	/* Data Not Busy */
#define		AT91_MCI_ENDRX		(1 <<  6)	/* End of RX Buffer */
#define		AT91_MCI_ENDTX		(1 <<  7)	/* End fo TX Buffer */
#define		AT91_MCI_SDIOIRQA	(1 <<  8)	/* SDIO Interrupt for Slot A */
#define		AT91_MCI_SDIOIRQB	(1 <<  9)	/* SDIO Interrupt for Slot B */
#define		AT91_MCI_RXBUFF		(1 << 14)	/* RX Buffer Full */
#define		AT91_MCI_TXBUFE		(1 << 15)	/* TX Buffer Empty */
#define		AT91_MCI_RINDE		(1 << 16)	/* Response Index Error */
#define		AT91_MCI_RDIRE		(1 << 17)	/* Response Direction Error */
#define		AT91_MCI_RCRCE		(1 << 18)	/* Response CRC Error */
#define		AT91_MCI_RENDE		(1 << 19)	/* Response End Bit Error */
#define		AT91_MCI_RTOE		(1 << 20)	/* Response Time-out Error */
#define		AT91_MCI_DCRCE		(1 << 21)	/* Data CRC Error */
#define		AT91_MCI_DTOE		(1 << 22)	/* Data Time-out Error */
#define		AT91_MCI_OVRE		(1 << 30)	/* Overrun */
#define		AT91_MCI_UNRE		(1 << 31)	/* Underrun */

#define AT91_MCI_IER		0x44		/* Interrupt Enable Register */
#define AT91_MCI_IDR		0x48		/* Interrupt Disable Register */
#define AT91_MCI_IMR		0x4c		/* Interrupt Mask Register */

#endif

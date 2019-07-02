/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-ks8695/include/mach/regs-uart.h
 *
 * Copyright (C) 2006 Ben Dooks <ben@simtec.co.uk>
 * Copyright (C) 2006 Simtec Electronics
 *
 * KS8695 - UART register and bit definitions.
 */

#ifndef KS8695_UART_H
#define KS8695_UART_H

#define KS8695_UART_OFFSET	(0xF0000 + 0xE000)
#define KS8695_UART_VA		(KS8695_IO_VA + KS8695_UART_OFFSET)
#define KS8695_UART_PA		(KS8695_IO_PA + KS8695_UART_OFFSET)


/*
 * UART registers
 */
#define KS8695_URRB	(0x00)		/* Receive Buffer Register */
#define KS8695_URTH	(0x04)		/* Transmit Holding Register */
#define KS8695_URFC	(0x08)		/* FIFO Control Register */
#define KS8695_URLC	(0x0C)		/* Line Control Register */
#define KS8695_URMC	(0x10)		/* Modem Control Register */
#define KS8695_URLS	(0x14)		/* Line Status Register */
#define KS8695_URMS	(0x18)		/* Modem Status Register */
#define KS8695_URBD	(0x1C)		/* Baud Rate Divisor Register */
#define KS8695_USR	(0x20)		/* Status Register */


/* FIFO Control Register */
#define URFC_URFRT	(3 << 6)	/* Receive FIFO Trigger Level */
#define		URFC_URFRT_1	(0 << 6)
#define		URFC_URFRT_4	(1 << 6)
#define		URFC_URFRT_8	(2 << 6)
#define		URFC_URFRT_14	(3 << 6)
#define URFC_URTFR	(1 << 2)	/* Transmit FIFO Reset */
#define URFC_URRFR	(1 << 1)	/* Receive FIFO Reset */
#define URFC_URFE	(1 << 0)	/* FIFO Enable */

/* Line Control Register */
#define URLC_URSBC	(1 << 6)	/* Set Break Condition */
#define URLC_PARITY	(7 << 3)	/* Parity */
#define		URPE_NONE	(0 << 3)
#define		URPE_ODD	(1 << 3)
#define		URPE_EVEN	(3 << 3)
#define		URPE_MARK	(5 << 3)
#define		URPE_SPACE	(7 << 3)
#define URLC_URSB	(1 << 2)	/* Stop Bits */
#define URLC_URCL	(3 << 0)	/* Character Length */
#define		URCL_5		(0 << 0)
#define		URCL_6		(1 << 0)
#define		URCL_7		(2 << 0)
#define		URCL_8		(3 << 0)

/* Modem Control Register */
#define URMC_URLB	(1 << 4)	/* Loop-back mode */
#define URMC_UROUT2	(1 << 3)	/* OUT2 signal */
#define URMC_UROUT1	(1 << 2)	/* OUT1 signal */
#define URMC_URRTS	(1 << 1)	/* Request to Send */
#define URMC_URDTR	(1 << 0)	/* Data Terminal Ready */

/* Line Status Register */
#define URLS_URRFE	(1 << 7)	/* Receive FIFO Error */
#define URLS_URTE	(1 << 6)	/* Transmit Empty */
#define URLS_URTHRE	(1 << 5)	/* Transmit Holding Register Empty */
#define URLS_URBI	(1 << 4)	/* Break Interrupt */
#define URLS_URFE	(1 << 3)	/* Framing Error */
#define URLS_URPE	(1 << 2)	/* Parity Error */
#define URLS_URROE	(1 << 1)	/* Receive Overrun Error */
#define URLS_URDR	(1 << 0)	/* Receive Data Ready */

/* Modem Status Register */
#define URMS_URDCD	(1 << 7)	/* Data Carrier Detect */
#define URMS_URRI	(1 << 6)	/* Ring Indicator */
#define URMS_URDSR	(1 << 5)	/* Data Set Ready */
#define URMS_URCTS	(1 << 4)	/* Clear to Send */
#define URMS_URDDCD	(1 << 3)	/* Delta Data Carrier Detect */
#define URMS_URTERI	(1 << 2)	/* Trailing Edge Ring Indicator */
#define URMS_URDDST	(1 << 1)	/* Delta Data Set Ready */
#define URMS_URDCTS	(1 << 0)	/* Delta Clear to Send */

/* Status Register */
#define USR_UTI		(1 << 0)	/* Timeout Indication */


#endif

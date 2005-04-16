/*
 * m32r_sio_reg.h
 *
 * Copyright (C) 1992, 1994 by Theodore Ts'o.
 * Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 *
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL)
 *
 * These are the UART port assignments, expressed as offsets from the base
 * register.  These assignments should hold for any serial port based on
 * a 8250, 16450, or 16550(A).
 */

#ifndef _M32R_SIO_REG_H
#define _M32R_SIO_REG_H

#include <linux/config.h>

#ifdef CONFIG_SERIAL_M32R_PLDSIO

#define SIOCR		0x000
#define SIOMOD0		0x002
#define SIOMOD1		0x004
#define SIOSTS		0x006
#define SIOTRCR		0x008
#define SIOBAUR		0x00a
// #define SIORBAUR	0x018
#define SIOTXB		0x00c
#define SIORXB		0x00e

#define UART_RX		((unsigned long) PLD_ESIO0RXB)
				/* In:  Receive buffer (DLAB=0) */
#define UART_TX		((unsigned long) PLD_ESIO0TXB)
				/* Out: Transmit buffer (DLAB=0) */
#define UART_DLL	0	/* Out: Divisor Latch Low (DLAB=1) */
#define UART_TRG	0	/* (LCR=BF) FCTR bit 7 selects Rx or Tx
				 * In: Fifo count
				 * Out: Fifo custom trigger levels
				 * XR16C85x only */

#define UART_DLM	0	/* Out: Divisor Latch High (DLAB=1) */
#define UART_IER	((unsigned long) PLD_ESIO0INTCR)
				/* Out: Interrupt Enable Register */
#define UART_FCTR	0	/* (LCR=BF) Feature Control Register
				 * XR16C85x only */

#define UART_IIR	0	/* In:  Interrupt ID Register */
#define UART_FCR	0	/* Out: FIFO Control Register */
#define UART_EFR	0	/* I/O: Extended Features Register */
				/* (DLAB=1, 16C660 only) */

#define UART_LCR	0	/* Out: Line Control Register */
#define UART_MCR	0	/* Out: Modem Control Register */
#define UART_LSR	((unsigned long) PLD_ESIO0STS)
				/* In:  Line Status Register */
#define UART_MSR	0	/* In:  Modem Status Register */
#define UART_SCR	0	/* I/O: Scratch Register */
#define UART_EMSR	0	/* (LCR=BF) Extended Mode Select Register
				 * FCTR bit 6 selects SCR or EMSR
				 * XR16c85x only */

#else /* not CONFIG_SERIAL_M32R_PLDSIO */

#define SIOCR		0x000
#define SIOMOD0		0x004
#define SIOMOD1		0x008
#define SIOSTS		0x00c
#define SIOTRCR		0x010
#define SIOBAUR		0x014
#define SIORBAUR	0x018
#define SIOTXB		0x01c
#define SIORXB		0x020

#define UART_RX		M32R_SIO0_RXB_PORTL	/* In:  Receive buffer (DLAB=0) */
#define UART_TX		M32R_SIO0_TXB_PORTL	/* Out: Transmit buffer (DLAB=0) */
#define UART_DLL	0	/* Out: Divisor Latch Low (DLAB=1) */
#define UART_TRG	0	/* (LCR=BF) FCTR bit 7 selects Rx or Tx
				 * In: Fifo count
				 * Out: Fifo custom trigger levels
				 * XR16C85x only */

#define UART_DLM	0	/* Out: Divisor Latch High (DLAB=1) */
#define UART_IER	M32R_SIO0_TRCR_PORTL	/* Out: Interrupt Enable Register */
#define UART_FCTR	0	/* (LCR=BF) Feature Control Register
				 * XR16C85x only */

#define UART_IIR	0	/* In:  Interrupt ID Register */
#define UART_FCR	0	/* Out: FIFO Control Register */
#define UART_EFR	0	/* I/O: Extended Features Register */
				/* (DLAB=1, 16C660 only) */

#define UART_LCR	0	/* Out: Line Control Register */
#define UART_MCR	0	/* Out: Modem Control Register */
#define UART_LSR	M32R_SIO0_STS_PORTL	/* In:  Line Status Register */
#define UART_MSR	0	/* In:  Modem Status Register */
#define UART_SCR	0	/* I/O: Scratch Register */
#define UART_EMSR	0	/* (LCR=BF) Extended Mode Select Register
				 * FCTR bit 6 selects SCR or EMSR
				 * XR16c85x only */

#endif /* CONFIG_SERIAL_M32R_PLDSIO */

#define UART_EMPTY	(UART_LSR_TEMT | UART_LSR_THRE)

/*
 * These are the definitions for the Line Control Register
 *
 * Note: if the word length is 5 bits (UART_LCR_WLEN5), then setting
 * UART_LCR_STOP will select 1.5 stop bits, not 2 stop bits.
 */
#define UART_LCR_DLAB	0x80	/* Divisor latch access bit */
#define UART_LCR_SBC	0x40	/* Set break control */
#define UART_LCR_SPAR	0x20	/* Stick parity (?) */
#define UART_LCR_EPAR	0x10	/* Even parity select */
#define UART_LCR_PARITY	0x08	/* Parity Enable */
#define UART_LCR_STOP	0x04	/* Stop bits: 0=1 stop bit, 1= 2 stop bits */
#define UART_LCR_WLEN5  0x00	/* Wordlength: 5 bits */
#define UART_LCR_WLEN6  0x01	/* Wordlength: 6 bits */
#define UART_LCR_WLEN7  0x02	/* Wordlength: 7 bits */
#define UART_LCR_WLEN8  0x03	/* Wordlength: 8 bits */

/*
 * These are the definitions for the Line Status Register
 */
#define UART_LSR_TEMT	0x02	/* Transmitter empty */
#define UART_LSR_THRE	0x01	/* Transmit-hold-register empty */
#define UART_LSR_BI	0x00	/* Break interrupt indicator */
#define UART_LSR_FE	0x80	/* Frame error indicator */
#define UART_LSR_PE	0x40	/* Parity error indicator */
#define UART_LSR_OE	0x20	/* Overrun error indicator */
#define UART_LSR_DR	0x04	/* Receiver data ready */

/*
 * These are the definitions for the Interrupt Identification Register
 */
#define UART_IIR_NO_INT	0x01	/* No interrupts pending */
#define UART_IIR_ID	0x06	/* Mask for the interrupt ID */

#define UART_IIR_MSI	0x00	/* Modem status interrupt */
#define UART_IIR_THRI	0x02	/* Transmitter holding register empty */
#define UART_IIR_RDI	0x04	/* Receiver data interrupt */
#define UART_IIR_RLSI	0x06	/* Receiver line status interrupt */

/*
 * These are the definitions for the Interrupt Enable Register
 */
#define UART_IER_MSI	0x00	/* Enable Modem status interrupt */
#define UART_IER_RLSI	0x08	/* Enable receiver line status interrupt */
#define UART_IER_THRI	0x03	/* Enable Transmitter holding register int. */
#define UART_IER_RDI	0x04	/* Enable receiver data interrupt */

#endif /* _M32R_SIO_REG_H */

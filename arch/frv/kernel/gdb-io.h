/* gdb-io.h: FR403 GDB I/O port defs
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _GDB_IO_H
#define _GDB_IO_H

#include <asm/serial-regs.h>

#undef UART_RX
#undef UART_TX
#undef UART_DLL
#undef UART_DLM
#undef UART_IER
#undef UART_IIR
#undef UART_FCR
#undef UART_LCR
#undef UART_MCR
#undef UART_LSR
#undef UART_MSR
#undef UART_SCR

#define UART_RX		0*8	/* In:  Receive buffer (DLAB=0) */
#define UART_TX		0*8	/* Out: Transmit buffer (DLAB=0) */
#define UART_DLL	0*8	/* Out: Divisor Latch Low (DLAB=1) */
#define UART_DLM	1*8	/* Out: Divisor Latch High (DLAB=1) */
#define UART_IER	1*8	/* Out: Interrupt Enable Register */
#define UART_IIR	2*8	/* In:  Interrupt ID Register */
#define UART_FCR	2*8	/* Out: FIFO Control Register */
#define UART_LCR	3*8	/* Out: Line Control Register */
#define UART_MCR	4*8	/* Out: Modem Control Register */
#define UART_LSR	5*8	/* In:  Line Status Register */
#define UART_MSR	6*8	/* In:  Modem Status Register */
#define UART_SCR	7*8	/* I/O: Scratch Register */

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


#endif /* _GDB_IO_H */

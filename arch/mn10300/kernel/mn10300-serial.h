/* MN10300 On-chip serial port driver definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _MN10300_SERIAL_H
#define _MN10300_SERIAL_H

#ifndef __ASSEMBLY__
#include <linux/serial_core.h>
#include <linux/termios.h>
#endif

#include <asm/page.h>
#include <asm/serial-regs.h>

#define NR_PORTS		3		/* should be set 3 or 9 or 16 */

#define MNSC_BUFFER_SIZE	+(PAGE_SIZE / 2)

/* intr_flags bits */
#define MNSCx_RX_AVAIL		0x01
#define MNSCx_RX_OVERF		0x02
#define MNSCx_TX_SPACE		0x04
#define MNSCx_TX_EMPTY		0x08

/* tx_flags bits */
#define MNSCx_TX_BREAK		0x01
#define MNSCx_TX_STOP		0x02

#ifndef __ASSEMBLY__

struct mn10300_serial_port {
	char			*rx_buffer;	/* reception buffer base */
	unsigned		rx_inp;		/* pointer to rx input offset */
	unsigned		rx_outp;	/* pointer to rx output offset */
	u8			tx_xchar;	/* high-priority XON/XOFF buffer */
	u8			tx_flags;	/* transmit break/stop request */
	u8			intr_flags;	/* interrupt flags */
	volatile u16		*rx_icr;	/* Rx interrupt control register */
	volatile u16		*tx_icr;	/* Tx interrupt control register */
	int			rx_irq;		/* reception IRQ */
	int			tx_irq;		/* transmission IRQ */
	int			tm_irq;		/* timer IRQ */

	const char		*name;		/* name of serial port */
	const char		*rx_name;	/* Rx interrupt handler name of serial port */
	const char		*tx_name;	/* Tx interrupt handler name of serial port */
	const char		*tm_name;	/* Timer interrupt handler name */
	unsigned short		type;		/* type of serial port */
	unsigned char		isconsole;	/* T if it's a console */
	volatile void		*_iobase;	/* pointer to base of I/O control regs */
	volatile u16		*_control;	/* control register pointer */
	volatile u8		*_status;	/* status register pointer */
	volatile u8		*_intr;		/* interrupt register pointer */
	volatile u8		*_rxb;		/* receive buffer register pointer */
	volatile u8		*_txb;		/* transmit buffer register pointer */
	volatile u16		*_tmicr;	/* timer interrupt control register */
	volatile u8		*_tmxmd;	/* baud rate timer mode register */
	volatile u16		*_tmxbr;	/* baud rate timer base register */

	/* this must come down here so that assembly can use BSET to access the
	 * above fields */
	struct uart_port	uart;

	unsigned short		rx_brk;		/* current break reception status */
	u16			tx_cts;		/* current CTS status */
	int			gdbstub;	/* preemptively stolen by GDB stub */

	u8			clock_src;	/* clock source */
#define MNSCx_CLOCK_SRC_IOCLK	0
#define MNSCx_CLOCK_SRC_IOBCLK	1

	u8			div_timer;	/* timer used as divisor */
#define MNSCx_DIV_TIMER_16BIT	0
#define MNSCx_DIV_TIMER_8BIT	1

	u16			options;	/* options */
#define MNSCx_OPT_CTS		0x0001

	unsigned long		ioclk;		/* base clock rate */
};

#ifdef CONFIG_MN10300_TTYSM0
extern struct mn10300_serial_port mn10300_serial_port_sif0;
#endif

#ifdef CONFIG_MN10300_TTYSM1
extern struct mn10300_serial_port mn10300_serial_port_sif1;
#endif

#ifdef CONFIG_MN10300_TTYSM2
extern struct mn10300_serial_port mn10300_serial_port_sif2;
#endif

extern struct mn10300_serial_port *mn10300_serial_ports[];

struct mn10300_serial_int {
	struct mn10300_serial_port *port;
	asmlinkage void (*vdma)(void);
};

extern struct mn10300_serial_int mn10300_serial_int_tbl[];

extern asmlinkage void mn10300_serial_vdma_interrupt(void);
extern asmlinkage void mn10300_serial_vdma_rx_handler(void);
extern asmlinkage void mn10300_serial_vdma_tx_handler(void);

#endif /* __ASSEMBLY__ */

#if defined(CONFIG_GDBSTUB_ON_TTYSM0)
#define SCgSTR SC0STR
#define SCgRXB SC0RXB
#define SCgRXIRQ SC0RXIRQ
#elif defined(CONFIG_GDBSTUB_ON_TTYSM1)
#define SCgSTR SC1STR
#define SCgRXB SC1RXB
#define SCgRXIRQ SC1RXIRQ
#elif defined(CONFIG_GDBSTUB_ON_TTYSM2)
#define SCgSTR SC2STR
#define SCgRXB SC2RXB
#define SCgRXIRQ SC2RXIRQ
#endif

#endif /* _MN10300_SERIAL_H */

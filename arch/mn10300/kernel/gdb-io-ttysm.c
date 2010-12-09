/* MN10300 On-chip serial driver for gdbstub I/O
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/gdb-stub.h>
#include <asm/exceptions.h>
#include <unit/clock.h>
#include "mn10300-serial.h"

#if defined(CONFIG_GDBSTUB_ON_TTYSM0)
struct mn10300_serial_port *const gdbstub_port = &mn10300_serial_port_sif0;
#elif defined(CONFIG_GDBSTUB_ON_TTYSM1)
struct mn10300_serial_port *const gdbstub_port = &mn10300_serial_port_sif1;
#else
struct mn10300_serial_port *const gdbstub_port = &mn10300_serial_port_sif2;
#endif


/*
 * initialise the GDB stub I/O routines
 */
void __init gdbstub_io_init(void)
{
	uint16_t scxctr;
	int tmp;

	switch (gdbstub_port->clock_src) {
	case MNSCx_CLOCK_SRC_IOCLK:
		gdbstub_port->ioclk = MN10300_IOCLK;
		break;

#ifdef MN10300_IOBCLK
	case MNSCx_CLOCK_SRC_IOBCLK:
		gdbstub_port->ioclk = MN10300_IOBCLK;
		break;
#endif
	default:
		BUG();
	}

	/* set up the serial port */
	gdbstub_io_set_baud(115200);

	/* we want to get serial receive interrupts */
	set_intr_level(gdbstub_port->rx_irq,
		NUM2GxICR_LEVEL(CONFIG_GDBSTUB_IRQ_LEVEL));
	set_intr_level(gdbstub_port->tx_irq,
		NUM2GxICR_LEVEL(CONFIG_GDBSTUB_IRQ_LEVEL));
	set_intr_stub(NUM2EXCEP_IRQ_LEVEL(CONFIG_GDBSTUB_IRQ_LEVEL),
		gdbstub_io_rx_handler);

	*gdbstub_port->rx_icr |= GxICR_ENABLE;
	tmp = *gdbstub_port->rx_icr;

	/* enable the device */
	scxctr = SC01CTR_CLN_8BIT;	/* 1N8 */
	switch (gdbstub_port->div_timer) {
	case MNSCx_DIV_TIMER_16BIT:
		scxctr |= SC0CTR_CK_TM8UFLOW_8; /* == SC1CTR_CK_TM9UFLOW_8
						   == SC2CTR_CK_TM10UFLOW_8 */
		break;

	case MNSCx_DIV_TIMER_8BIT:
		scxctr |= SC0CTR_CK_TM2UFLOW_8;
		break;
	}

	scxctr |= SC01CTR_TXE | SC01CTR_RXE;

	*gdbstub_port->_control = scxctr;
	tmp = *gdbstub_port->_control;

	/* permit level 0 IRQs only */
	local_change_intr_mask_level(NUM2EPSW_IM(CONFIG_GDBSTUB_IRQ_LEVEL + 1));
}

/*
 * set up the GDB stub serial port baud rate timers
 */
void gdbstub_io_set_baud(unsigned baud)
{
	const unsigned bits = 10; /* 1 [start] + 8 [data] + 0 [parity] +
				   * 1 [stop] */
	unsigned long ioclk = gdbstub_port->ioclk;
	unsigned xdiv, tmp;
	uint16_t tmxbr;
	uint8_t tmxmd;

	if (!baud) {
		baud = 9600;
	} else if (baud == 134) {
		baud = 269;	/* 134 is really 134.5 */
		xdiv = 2;
	}

try_alternative:
	xdiv = 1;

	switch (gdbstub_port->div_timer) {
	case MNSCx_DIV_TIMER_16BIT:
		tmxmd = TM8MD_SRC_IOCLK;
		tmxbr = tmp = (ioclk / (baud * xdiv) + 4) / 8 - 1;
		if (tmp > 0 && tmp <= 65535)
			goto timer_okay;

		tmxmd = TM8MD_SRC_IOCLK_8;
		tmxbr = tmp = (ioclk / (baud * 8 * xdiv) + 4) / 8 - 1;
		if (tmp > 0 && tmp <= 65535)
			goto timer_okay;

		tmxmd = TM8MD_SRC_IOCLK_32;
		tmxbr = tmp = (ioclk / (baud * 32 * xdiv) + 4) / 8 - 1;
		if (tmp > 0 && tmp <= 65535)
			goto timer_okay;

		break;

	case MNSCx_DIV_TIMER_8BIT:
		tmxmd = TM2MD_SRC_IOCLK;
		tmxbr = tmp = (ioclk / (baud * xdiv) + 4) / 8 - 1;
		if (tmp > 0 && tmp <= 255)
			goto timer_okay;

		tmxmd = TM2MD_SRC_IOCLK_8;
		tmxbr = tmp = (ioclk / (baud * 8 * xdiv) + 4) / 8 - 1;
		if (tmp > 0 && tmp <= 255)
			goto timer_okay;

		tmxmd = TM2MD_SRC_IOCLK_32;
		tmxbr = tmp = (ioclk / (baud * 32 * xdiv) + 4) / 8 - 1;
		if (tmp > 0 && tmp <= 255)
			goto timer_okay;
		break;
	}

	/* as a last resort, if the quotient is zero, default to 9600 bps */
	baud = 9600;
	goto try_alternative;

timer_okay:
	gdbstub_port->uart.timeout = (2 * bits * HZ) / baud;
	gdbstub_port->uart.timeout += HZ / 50;

	/* set the timer to produce the required baud rate */
	switch (gdbstub_port->div_timer) {
	case MNSCx_DIV_TIMER_16BIT:
		*gdbstub_port->_tmxmd = 0;
		*gdbstub_port->_tmxbr = tmxbr;
		*gdbstub_port->_tmxmd = TM8MD_INIT_COUNTER;
		*gdbstub_port->_tmxmd = tmxmd | TM8MD_COUNT_ENABLE;
		break;

	case MNSCx_DIV_TIMER_8BIT:
		*gdbstub_port->_tmxmd = 0;
		*(volatile u8 *) gdbstub_port->_tmxbr = (u8)tmxbr;
		*gdbstub_port->_tmxmd = TM2MD_INIT_COUNTER;
		*gdbstub_port->_tmxmd = tmxmd | TM2MD_COUNT_ENABLE;
		break;
	}
}

/*
 * wait for a character to come from the debugger
 */
int gdbstub_io_rx_char(unsigned char *_ch, int nonblock)
{
	unsigned ix;
	u8 ch, st;
#if defined(CONFIG_MN10300_WD_TIMER)
	int cpu;
#endif

	*_ch = 0xff;

	if (gdbstub_rx_unget) {
		*_ch = gdbstub_rx_unget;
		gdbstub_rx_unget = 0;
		return 0;
	}

try_again:
	/* pull chars out of the buffer */
	ix = gdbstub_rx_outp;
	barrier();
	if (ix == gdbstub_rx_inp) {
		if (nonblock)
			return -EAGAIN;
#ifdef CONFIG_MN10300_WD_TIMER
	for (cpu = 0; cpu < NR_CPUS; cpu++)
		watchdog_alert_counter[cpu] = 0;
#endif
		goto try_again;
	}

	ch = gdbstub_rx_buffer[ix++];
	st = gdbstub_rx_buffer[ix++];
	barrier();
	gdbstub_rx_outp = ix & (PAGE_SIZE - 1);

	st &= SC01STR_RXF | SC01STR_RBF | SC01STR_FEF | SC01STR_PEF |
		SC01STR_OEF;

	/* deal with what we've got
	 * - note that the UART doesn't do BREAK-detection for us
	 */
	if (st & SC01STR_FEF && ch == 0) {
		switch (gdbstub_port->rx_brk) {
		case 0:	gdbstub_port->rx_brk = 1;	goto try_again;
		case 1:	gdbstub_port->rx_brk = 2;	goto try_again;
		case 2:
			gdbstub_port->rx_brk = 3;
			gdbstub_proto("### GDB MNSERIAL Rx Break Detected"
				      " ###\n");
			return -EINTR;
		default:
			goto try_again;
		}
	} else if (st & SC01STR_FEF) {
		if (gdbstub_port->rx_brk)
			goto try_again;

		gdbstub_proto("### GDB MNSERIAL Framing Error ###\n");
		return -EIO;
	} else if (st & SC01STR_OEF) {
		if (gdbstub_port->rx_brk)
			goto try_again;

		gdbstub_proto("### GDB MNSERIAL Overrun Error ###\n");
		return -EIO;
	} else if (st & SC01STR_PEF) {
		if (gdbstub_port->rx_brk)
			goto try_again;

		gdbstub_proto("### GDB MNSERIAL Parity Error ###\n");
		return -EIO;
	} else {
		/* look for the tail-end char on a break run */
		if (gdbstub_port->rx_brk == 3) {
			switch (ch) {
			case 0xFF:
			case 0xFE:
			case 0xFC:
			case 0xF8:
			case 0xF0:
			case 0xE0:
			case 0xC0:
			case 0x80:
			case 0x00:
				gdbstub_port->rx_brk = 0;
				goto try_again;
			default:
				break;
			}
		}

		gdbstub_port->rx_brk = 0;
		gdbstub_io("### GDB Rx %02x (st=%02x) ###\n", ch, st);
		*_ch = ch & 0x7f;
		return 0;
	}
}

/*
 * send a character to the debugger
 */
void gdbstub_io_tx_char(unsigned char ch)
{
	while (*gdbstub_port->_status & SC01STR_TBF)
		continue;

	if (ch == 0x0a) {
		*(u8 *) gdbstub_port->_txb = 0x0d;
		while (*gdbstub_port->_status & SC01STR_TBF)
			continue;
	}

	*(u8 *) gdbstub_port->_txb = ch;
}

/*
 * flush the transmission buffers
 */
void gdbstub_io_tx_flush(void)
{
	while (*gdbstub_port->_status & (SC01STR_TBF | SC01STR_TXF))
		continue;
}

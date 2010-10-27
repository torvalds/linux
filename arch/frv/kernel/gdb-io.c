/* gdb-io.c: FR403 GDB stub I/O
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/serial_reg.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/irc-regs.h>
#include <asm/timer-regs.h>
#include <asm/gdb-stub.h>
#include "gdb-io.h"

#ifdef CONFIG_GDBSTUB_UART0
#define __UART(X) (*(volatile uint8_t *)(UART0_BASE + (UART_##X)))
#define __UART_IRR_NMI 0xff0f0000
#else /* CONFIG_GDBSTUB_UART1 */
#define __UART(X) (*(volatile uint8_t *)(UART1_BASE + (UART_##X)))
#define __UART_IRR_NMI 0xfff00000
#endif

#define LSR_WAIT_FOR(STATE)			\
do {						\
	gdbstub_do_rx();			\
} while (!(__UART(LSR) & UART_LSR_##STATE))

#define FLOWCTL_QUERY(LINE)	({ __UART(MSR) & UART_MSR_##LINE; })
#define FLOWCTL_CLEAR(LINE)	do { __UART(MCR) &= ~UART_MCR_##LINE; mb(); } while (0)
#define FLOWCTL_SET(LINE)	do { __UART(MCR) |= UART_MCR_##LINE;  mb(); } while (0)

#define FLOWCTL_WAIT_FOR(LINE)			\
do {						\
	gdbstub_do_rx();			\
} while(!FLOWCTL_QUERY(LINE))

/*****************************************************************************/
/*
 * initialise the GDB stub
 * - called with PSR.ET==0, so can't incur external interrupts
 */
void gdbstub_io_init(void)
{
	/* set up the serial port */
	__UART(LCR) = UART_LCR_WLEN8; /* 1N8 */
	__UART(FCR) =
		UART_FCR_ENABLE_FIFO |
		UART_FCR_CLEAR_RCVR |
		UART_FCR_CLEAR_XMIT |
		UART_FCR_TRIGGER_1;

	FLOWCTL_CLEAR(DTR);
	FLOWCTL_SET(RTS);

//	gdbstub_set_baud(115200);

	/* we want to get serial receive interrupts */
	__UART(IER) = UART_IER_RDI | UART_IER_RLSI;
	mb();

	__set_IRR(6, __UART_IRR_NMI);	/* map ERRs and UARTx to NMI */

} /* end gdbstub_io_init() */

/*****************************************************************************/
/*
 * set up the GDB stub serial port baud rate timers
 */
void gdbstub_set_baud(unsigned baud)
{
	unsigned value, high, low;
	u8 lcr;

	/* work out the divisor to give us the nearest higher baud rate */
	value = __serial_clock_speed_HZ / 16 / baud;

	/* determine the baud rate range */
	high = __serial_clock_speed_HZ / 16 / value;
	low = __serial_clock_speed_HZ / 16 / (value + 1);

	/* pick the nearest bound */
	if (low + (high - low) / 2 > baud)
		value++;

	lcr = __UART(LCR);
	__UART(LCR) |= UART_LCR_DLAB;
	mb();
	__UART(DLL) = value & 0xff;
	__UART(DLM) = (value >> 8) & 0xff;
	mb();
	__UART(LCR) = lcr;
	mb();

} /* end gdbstub_set_baud() */

/*****************************************************************************/
/*
 * receive characters into the receive FIFO
 */
void gdbstub_do_rx(void)
{
	unsigned ix, nix;

	ix = gdbstub_rx_inp;

	while (__UART(LSR) & UART_LSR_DR) {
		nix = (ix + 2) & 0xfff;
		if (nix == gdbstub_rx_outp)
			break;

		gdbstub_rx_buffer[ix++] = __UART(LSR);
		gdbstub_rx_buffer[ix++] = __UART(RX);
		ix = nix;
	}

	gdbstub_rx_inp = ix;

	__clr_RC(15);
	__clr_IRL();

} /* end gdbstub_do_rx() */

/*****************************************************************************/
/*
 * wait for a character to come from the debugger
 */
int gdbstub_rx_char(unsigned char *_ch, int nonblock)
{
	unsigned ix;
	u8 ch, st;

	*_ch = 0xff;

	if (gdbstub_rx_unget) {
		*_ch = gdbstub_rx_unget;
		gdbstub_rx_unget = 0;
		return 0;
	}

 try_again:
	gdbstub_do_rx();

	/* pull chars out of the buffer */
	ix = gdbstub_rx_outp;
	if (ix == gdbstub_rx_inp) {
		if (nonblock)
			return -EAGAIN;
		//watchdog_alert_counter = 0;
		goto try_again;
	}

	st = gdbstub_rx_buffer[ix++];
	ch = gdbstub_rx_buffer[ix++];
	gdbstub_rx_outp = ix & 0x00000fff;

	if (st & UART_LSR_BI) {
		gdbstub_proto("### GDB Rx Break Detected ###\n");
		return -EINTR;
	}
	else if (st & (UART_LSR_FE|UART_LSR_OE|UART_LSR_PE)) {
		gdbstub_io("### GDB Rx Error (st=%02x) ###\n",st);
		return -EIO;
	}
	else {
		gdbstub_io("### GDB Rx %02x (st=%02x) ###\n",ch,st);
		*_ch = ch & 0x7f;
		return 0;
	}

} /* end gdbstub_rx_char() */

/*****************************************************************************/
/*
 * send a character to the debugger
 */
void gdbstub_tx_char(unsigned char ch)
{
	FLOWCTL_SET(DTR);
	LSR_WAIT_FOR(THRE);
//	FLOWCTL_WAIT_FOR(CTS);

	if (ch == 0x0a) {
		__UART(TX) = 0x0d;
		mb();
		LSR_WAIT_FOR(THRE);
//		FLOWCTL_WAIT_FOR(CTS);
	}
	__UART(TX) = ch;
	mb();

	FLOWCTL_CLEAR(DTR);
} /* end gdbstub_tx_char() */

/*****************************************************************************/
/*
 * send a character to the debugger
 */
void gdbstub_tx_flush(void)
{
	LSR_WAIT_FOR(TEMT);
	LSR_WAIT_FOR(THRE);
	FLOWCTL_CLEAR(DTR);
} /* end gdbstub_tx_flush() */

/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 * This is the interface to the remote debugger stub.
 *
 */
#include <linux/types.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/serial_reg.h>
#include <linux/serial_ip3106.h>

#include <asm/serial.h>
#include <asm/io.h>

#include <uart.h>

static struct serial_state rs_table[IP3106_NR_PORTS] = {
};
static struct async_struct kdb_port_info = {0};

void rs_kgdb_hook(int tty_no)
{
	struct serial_state *ser = &rs_table[tty_no];

	kdb_port_info.state = ser;
	kdb_port_info.magic = SERIAL_MAGIC;
	kdb_port_info.port  = tty_no;
	kdb_port_info.flags = ser->flags;

	/*
	 * Clear all interrupts
	 */
	/* Clear all the transmitter FIFO counters (pointer and status) */
	ip3106_lcr(UART_BASE, tty_no) |= IP3106_UART_LCR_TX_RST;
	/* Clear all the receiver FIFO counters (pointer and status) */
	ip3106_lcr(UART_BASE, tty_no) |= IP3106_UART_LCR_RX_RST;
	/* Clear all interrupts */
	ip3106_iclr(UART_BASE, tty_no) = IP3106_UART_INT_ALLRX |
		IP3106_UART_INT_ALLTX;

	/*
	 * Now, initialize the UART
	 */
	ip3106_lcr(UART_BASE, tty_no) = IP3106_UART_LCR_8BIT;
	ip3106_baud(UART_BASE, tty_no) = 5; // 38400 Baud
}

int putDebugChar(char c)
{
	/* Wait until FIFO not full */
	while (((ip3106_fifo(UART_BASE, kdb_port_info.port) & IP3106_UART_FIFO_TXFIFO) >> 16) >= 16)
		;
	/* Send one char */
	ip3106_fifo(UART_BASE, kdb_port_info.port) = c;

	return 1;
}

char getDebugChar(void)
{
	char ch;

	/* Wait until there is a char in the FIFO */
	while (!((ip3106_fifo(UART_BASE, kdb_port_info.port) &
					IP3106_UART_FIFO_RXFIFO) >> 8))
		;
	/* Read one char */
	ch = ip3106_fifo(UART_BASE, kdb_port_info.port) &
		IP3106_UART_FIFO_RBRTHR;
	/* Advance the RX FIFO read pointer */
	ip3106_lcr(UART_BASE, kdb_port_info.port) |= IP3106_UART_LCR_RX_NEXT;
	return (ch);
}

void rs_disable_debug_interrupts(void)
{
	ip3106_ien(UART_BASE, kdb_port_info.port) = 0; /* Disable all interrupts */
}

void rs_enable_debug_interrupts(void)
{
	/* Clear all the transmitter FIFO counters (pointer and status) */
	ip3106_lcr(UART_BASE, kdb_port_info.port) |= IP3106_UART_LCR_TX_RST;
	/* Clear all the receiver FIFO counters (pointer and status) */
	ip3106_lcr(UART_BASE, kdb_port_info.port) |= IP3106_UART_LCR_RX_RST;
	/* Clear all interrupts */
	ip3106_iclr(UART_BASE, kdb_port_info.port) = IP3106_UART_INT_ALLRX |
		IP3106_UART_INT_ALLTX;
	ip3106_ien(UART_BASE, kdb_port_info.port)  = IP3106_UART_INT_ALLRX; /* Enable RX interrupts */
}

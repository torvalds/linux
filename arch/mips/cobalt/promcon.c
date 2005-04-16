/*
 * PROM console for Cobalt Raq2
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 * Copyright (C) 2001 by Liam Davies (ldavies@agile.tv)
 *
 */

#include <linux/init.h>
#include <linux/console.h>
#include <linux/kdev_t.h>
#include <linux/serial_reg.h>

#include <asm/delay.h>
#include <asm/serial.h>
#include <asm/io.h>

static unsigned long port = 0xc800000;

static __inline__ void ns16550_cons_put_char(char ch, unsigned long ioaddr)
{
	char lsr;

	do {
		lsr = inb(ioaddr + UART_LSR);
	} while ((lsr & (UART_LSR_TEMT | UART_LSR_THRE)) != (UART_LSR_TEMT | UART_LSR_THRE));
	outb(ch, ioaddr + UART_TX);
}

static __inline__ char ns16550_cons_get_char(unsigned long ioaddr)
{
	while ((inb(ioaddr + UART_LSR) & UART_LSR_DR) == 0)
		udelay(1);
	return inb(ioaddr + UART_RX);
}

void ns16550_console_write(struct console *co, const char *s, unsigned count)
{
	char lsr, ier;
	unsigned i;

	ier = inb(port + UART_IER);
	outb(0x00, port + UART_IER);
	for (i=0; i < count; i++, s++) {

		if(*s == '\n')
			ns16550_cons_put_char('\r', port);
		ns16550_cons_put_char(*s, port);
	}

	do {
		lsr = inb(port + UART_LSR);
   	} while ((lsr & (UART_LSR_TEMT | UART_LSR_THRE)) != (UART_LSR_TEMT | UART_LSR_THRE));

	outb(ier, port + UART_IER);
}

char getDebugChar(void)
{
	return ns16550_cons_get_char(port);
}

void putDebugChar(char kgdb_char)
{
	ns16550_cons_put_char(kgdb_char, port);
}

static struct console ns16550_console = {
    .name	= "prom",
    .setup	= NULL,
    .write	= ns16550_console_write,
    .flags	= CON_PRINTBUFFER,
    .index	= -1,
};

static int __init ns16550_setup_console(void)
{
	register_console(&ns16550_console);

	return 0;
}

console_initcall(ns16550_setup_console);

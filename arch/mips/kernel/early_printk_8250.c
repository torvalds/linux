// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  8250/16550-type serial ports prom_putchar()
 *
 *  Copyright (C) 2010  Yoichi Yuasa <yuasa@linux-mips.org>
 */
#include <linux/io.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <asm/setup.h>

static void __iomem *serial8250_base;
static unsigned int serial8250_reg_shift;
static unsigned int serial8250_tx_timeout;

void setup_8250_early_printk_port(unsigned long base, unsigned int reg_shift,
				  unsigned int timeout)
{
	serial8250_base = (void __iomem *)base;
	serial8250_reg_shift = reg_shift;
	serial8250_tx_timeout = timeout;
}

static inline u8 serial_in(int offset)
{
	return readb(serial8250_base + (offset << serial8250_reg_shift));
}

static inline void serial_out(int offset, char value)
{
	writeb(value, serial8250_base + (offset << serial8250_reg_shift));
}

void prom_putchar(char c)
{
	unsigned int timeout;
	int status, bits;

	if (!serial8250_base)
		return;

	timeout = serial8250_tx_timeout;
	bits = UART_LSR_TEMT | UART_LSR_THRE;

	do {
		status = serial_in(UART_LSR);

		if (--timeout == 0)
			break;
	} while ((status & bits) != bits);

	if (timeout)
		serial_out(UART_TX, c);
}

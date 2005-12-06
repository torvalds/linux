/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
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
 * Putting things on the screen/serial line using YAMONs facilities.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serial_reg.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/system.h>

static inline unsigned int serial_in(int offset)
{
	return inb(0x3f8 + offset);
}

static inline void serial_out(int offset, int value)
{
	outb(value, 0x3f8 + offset);
}

int putPromChar(char c)
{
	while ((serial_in(UART_LSR) & UART_LSR_THRE) == 0)
		;

	serial_out(UART_TX, c);

	return 1;
}

char getPromChar(void)
{
	while (!(serial_in(UART_LSR) & 1))
		;

	return serial_in(UART_RX);
}

void prom_printf(char *fmt, ...)
{
	va_list args;
	int l;
	char *p, *buf_end;
	char buf[1024];

	va_start(args, fmt);
	l = vsprintf(buf, fmt, args); /* hopefully i < sizeof(buf) */
	va_end(args);

	buf_end = buf + l;

	for (p = buf; p < buf_end; p++) {
		/* Crude cr/nl handling is better than none */
		if (*p == '\n')
			putPromChar('\r');
		putPromChar(*p);
	}
}

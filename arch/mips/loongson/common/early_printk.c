/*  early printk support
 *
 *  Copyright (c) 2009 Philippe Vachon <philippe@cowpig.ca>
 *  Copyright (C) 2009 Lemote Inc. & Insititute of Computing Technology
 *  Author: Wu Zhangjin, wuzj@lemote.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/serial_reg.h>

#include <loongson.h>
#include <machine.h>

#define PORT(base, offset) (u8 *)(base + offset)

static inline unsigned int serial_in(phys_addr_t base, int offset)
{
	return readb(PORT(base, offset));
}

static inline void serial_out(phys_addr_t base, int offset, int value)
{
	writeb(value, PORT(base, offset));
}

void prom_putchar(char c)
{
	phys_addr_t uart_base =
		(phys_addr_t) ioremap_nocache(LOONGSON_UART_BASE, 8);

	while ((serial_in(uart_base, UART_LSR) & UART_LSR_THRE) == 0)
		;

	serial_out(uart_base, UART_TX, c);
}

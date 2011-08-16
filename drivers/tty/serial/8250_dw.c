/*
 * Synopsys DesignWare specific 8250 operations.
 *
 * Copyright 2011 Picochip, Jamie Iles.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Synopsys DesignWare 8250 has an extra feature whereby it detects if the
 * LCR is written whilst busy.  If it is, then a busy detect interrupt is
 * raised, the LCR needs to be rewritten and the uart status register read.
 */
#include <linux/io.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>
#include <linux/slab.h>

struct dw8250_data {
	int	last_lcr;
};

static void dw8250_serial_out(struct uart_port *p, int offset, int value)
{
	struct dw8250_data *d = p->private_data;

	if (offset == UART_LCR)
		d->last_lcr = value;

	offset <<= p->regshift;
	writeb(value, p->membase + offset);
}

static unsigned int dw8250_serial_in(struct uart_port *p, int offset)
{
	offset <<= p->regshift;

	return readb(p->membase + offset);
}

static void dw8250_serial_out32(struct uart_port *p, int offset,
				int value)
{
	struct dw8250_data *d = p->private_data;

	if (offset == UART_LCR)
		d->last_lcr = value;

	offset <<= p->regshift;
	writel(value, p->membase + offset);
}

static unsigned int dw8250_serial_in32(struct uart_port *p, int offset)
{
	offset <<= p->regshift;

	return readl(p->membase + offset);
}

/* Offset for the DesignWare's UART Status Register. */
#define UART_USR	0x1f

static int dw8250_handle_irq(struct uart_port *p)
{
	struct dw8250_data *d = p->private_data;
	unsigned int iir = p->serial_in(p, UART_IIR);

	if (serial8250_handle_irq(p, iir)) {
		return 1;
	} else if ((iir & UART_IIR_BUSY) == UART_IIR_BUSY) {
		/* Clear the USR and write the LCR again. */
		(void)p->serial_in(p, UART_USR);
		p->serial_out(p, d->last_lcr, UART_LCR);

		return 1;
	}

	return 0;
}

int serial8250_use_designware_io(struct uart_port *up)
{
	up->private_data = kzalloc(sizeof(struct dw8250_data), GFP_KERNEL);
	if (!up->private_data)
		return -ENOMEM;

	if (up->iotype == UPIO_MEM32) {
		up->serial_out = dw8250_serial_out32;
		up->serial_in = dw8250_serial_in32;
	} else {
		up->serial_out = dw8250_serial_out;
		up->serial_in = dw8250_serial_in;
	}
	up->handle_irq = dw8250_handle_irq;

	return 0;
}

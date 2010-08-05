/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 serial support
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/io.h>
#include <linux/serial_core.h>
#include <linux/serial_reg.h>

void jz4740_serial_out(struct uart_port *p, int offset, int value)
{
	switch (offset) {
	case UART_FCR:
		value |= 0x10; /* Enable uart module */
		break;
	case UART_IER:
		value |= (value & 0x4) << 2;
		break;
	default:
		break;
	}
	writeb(value, p->membase + (offset << p->regshift));
}

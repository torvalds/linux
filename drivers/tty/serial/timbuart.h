// SPDX-License-Identifier: GPL-2.0
/*
 * timbuart.c timberdale FPGA GPIO driver
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Timberdale FPGA UART
 */

#ifndef _TIMBUART_H
#define _TIMBUART_H

#define TIMBUART_FIFO_SIZE	2048

#define TIMBUART_RXFIFO		0x08
#define TIMBUART_TXFIFO		0x0c
#define TIMBUART_IER		0x10
#define TIMBUART_IPR		0x14
#define TIMBUART_ISR		0x18
#define TIMBUART_CTRL		0x1c
#define TIMBUART_BAUDRATE	0x20

#define TIMBUART_CTRL_RTS	0x01
#define TIMBUART_CTRL_CTS	0x02
#define TIMBUART_CTRL_FLSHTX	0x40
#define TIMBUART_CTRL_FLSHRX	0x80

#define TXBF		0x01
#define TXBAE		0x02
#define CTS_DELTA	0x04
#define RXDP		0x08
#define RXBAF		0x10
#define RXBF		0x20
#define RXTT		0x40
#define RXBNAE		0x80
#define TXBE		0x100

#define RXFLAGS (RXDP | RXBAF | RXBF | RXTT | RXBNAE)
#define TXFLAGS (TXBF | TXBAE)

#define TIMBUART_MAJOR 204
#define TIMBUART_MINOR 192

#endif /* _TIMBUART_H */


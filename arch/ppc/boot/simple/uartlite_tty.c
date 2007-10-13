/*
 * Xilinx UARTLITE bootloader driver
 *
 * Copyright (c) 2007 Secret Lab Technologies Ltd.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <asm/serial.h>
#include <asm/io.h>
#include <platforms/4xx/xparameters/xparameters.h>

#define UARTLITE_BASEADDR ((void*)(XPAR_UARTLITE_0_BASEADDR))

unsigned long
serial_init(int chan, void *ignored)
{
	/* Clear the RX FIFO */
	out_be32(UARTLITE_BASEADDR + 0x0C, 0x2);
	return 0;
}

void
serial_putc(unsigned long com_port, unsigned char c)
{
	while ((in_be32(UARTLITE_BASEADDR + 0x8) & 0x08) != 0); /* spin */
	out_be32(UARTLITE_BASEADDR + 0x4, c);
}

unsigned char
serial_getc(unsigned long com_port)
{
	while ((in_be32(UARTLITE_BASEADDR + 0x8) & 0x01) == 0); /* spin */
	return in_be32(UARTLITE_BASEADDR);
}

int
serial_tstc(unsigned long com_port)
{
	return ((in_be32(UARTLITE_BASEADDR + 0x8) & 0x01) != 0);
}

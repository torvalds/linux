/*
 * Xilinx UARTLITE bootloader driver
 *
 * Copyright (C) 2007 Secret Lab Technologies Ltd.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "ops.h"

static void * reg_base;

static int uartlite_open(void)
{
	/* Clear the RX FIFO */
	out_be32(reg_base + 0x0C, 0x2);
	return 0;
}

static void uartlite_putc(unsigned char c)
{
	while ((in_be32(reg_base + 0x8) & 0x08) != 0); /* spin */
	out_be32(reg_base + 0x4, c);
}

static unsigned char uartlite_getc(void)
{
	while ((in_be32(reg_base + 0x8) & 0x01) == 0); /* spin */
	return in_be32(reg_base);
}

static u8 uartlite_tstc(void)
{
	return ((in_be32(reg_base + 0x8) & 0x01) != 0);
}

int uartlite_console_init(void *devp, struct serial_console_data *scdp)
{
	int n;
	unsigned long reg_phys;

	n = getprop(devp, "virtual-reg", &reg_base, sizeof(reg_base));
	if (n != sizeof(reg_base)) {
		if (!dt_xlate_reg(devp, 0, &reg_phys, NULL))
			return -1;

		reg_base = (void *)reg_phys;
	}

	scdp->open = uartlite_open;
	scdp->putc = uartlite_putc;
	scdp->getc = uartlite_getc;
	scdp->tstc = uartlite_tstc;
	scdp->close = NULL;
	return 0;
}

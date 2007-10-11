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

#define ULITE_RX		0x00
#define ULITE_TX		0x04
#define ULITE_STATUS		0x08
#define ULITE_CONTROL		0x0c

#define ULITE_STATUS_RXVALID	0x01
#define ULITE_STATUS_TXFULL	0x08

#define ULITE_CONTROL_RST_RX	0x02

static void * reg_base;

static int uartlite_open(void)
{
	/* Clear the RX FIFO */
	out_be32(reg_base + ULITE_CONTROL, ULITE_CONTROL_RST_RX);
	return 0;
}

static void uartlite_putc(unsigned char c)
{
	u32 reg = ULITE_STATUS_TXFULL;
	while (reg & ULITE_STATUS_TXFULL) /* spin on TXFULL bit */
		reg = in_be32(reg_base + ULITE_STATUS);
	out_be32(reg_base + ULITE_TX, c);
}

static unsigned char uartlite_getc(void)
{
	u32 reg = 0;
	while (!(reg & ULITE_STATUS_RXVALID)) /* spin waiting for RXVALID bit */
		reg = in_be32(reg_base + ULITE_STATUS);
	return in_be32(reg_base + ULITE_RX);
}

static u8 uartlite_tstc(void)
{
	u32 reg = in_be32(reg_base + ULITE_STATUS);
	return reg & ULITE_STATUS_RXVALID;
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

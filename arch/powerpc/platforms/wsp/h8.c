/*
 * Copyright 2008-2011, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/io.h>

#include "wsp.h"

/*
 * The UART connection to the H8 is over ttyS1 which is just a 16550.
 * We assume that FW has it setup right and no one messes with it.
 */


static u8 __iomem *h8;

#define RBR 0		/* Receiver Buffer Register */
#define THR 0		/* Transmitter Holding Register */
#define LSR 5		/* Line Status Register */
#define LSR_DR 0x01	/* LSR value for Data-Ready */
#define LSR_THRE 0x20	/* LSR value for Transmitter-Holding-Register-Empty */
static void wsp_h8_putc(int c)
{
	u8 lsr;

	do {
		lsr = readb(h8 + LSR);
	} while ((lsr & LSR_THRE) != LSR_THRE);
	writeb(c, h8 + THR);
}

static int wsp_h8_getc(void)
{
	u8 lsr;

	do {
		lsr = readb(h8 + LSR);
	} while ((lsr & LSR_DR) != LSR_DR);

	return readb(h8 + RBR);
}

static void wsp_h8_puts(const char *s, int sz)
{
	int i;

	for (i = 0; i < sz; i++) {
		wsp_h8_putc(s[i]);

		/* no flow control so wait for echo */
		wsp_h8_getc();
	}
	wsp_h8_putc('\r');
	wsp_h8_putc('\n');
}

static void wsp_h8_terminal_cmd(const char *cmd, int sz)
{
	hard_irq_disable();
	wsp_h8_puts(cmd, sz);
	/* should never return, but just in case */
	for (;;)
		continue;
}


void wsp_h8_restart(char *cmd)
{
	static const char restart[] = "warm-reset";

	(void)cmd;
	wsp_h8_terminal_cmd(restart, sizeof(restart) - 1);
}

void wsp_h8_power_off(void)
{
	static const char off[] = "power-off";

	wsp_h8_terminal_cmd(off, sizeof(off) - 1);
}

static void __iomem *wsp_h8_getaddr(void)
{
	struct device_node *aliases;
	struct device_node *uart;
	struct property *path;
	void __iomem *va = NULL;

	/*
	 * there is nothing in the devtree to tell us which is mapped
	 * to the H8, but se know it is the second serial port.
	 */

	aliases = of_find_node_by_path("/aliases");
	if (aliases == NULL)
		return NULL;

	path = of_find_property(aliases, "serial1", NULL);
	if (path == NULL)
		goto out;

	uart = of_find_node_by_path(path->value);
	if (uart == NULL)
		goto out;

	va = of_iomap(uart, 0);

	/* remove it so no one messes with it */
	of_detach_node(uart);
	of_node_put(uart);

out:
	of_node_put(aliases);

	return va;
}

void __init wsp_setup_h8(void)
{
	h8 = wsp_h8_getaddr();

	/* Devtree change? lets hard map it anyway */
	if (h8 == NULL) {
		pr_warn("UART to H8 could not be found");
		h8 = ioremap(0xffc0008000ULL, 0x100);
	}
}

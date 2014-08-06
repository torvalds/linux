/*
 * Copyright (C) 2014 Linaro Ltd.
 * Author: Rob Herring <robh@kernel.org>
 *
 * Based on 8250 earlycon:
 * (c) Copyright 2004 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/serial_core.h>
#include <linux/sizes.h>
#include <linux/mod_devicetable.h>

#ifdef CONFIG_FIX_EARLYCON_MEM
#include <asm/fixmap.h>
#endif

#include <asm/serial.h>

static struct console early_con = {
	.name =		"uart", /* 8250 console switch requires this name */
	.flags =	CON_PRINTBUFFER | CON_BOOT,
	.index =	-1,
};

static struct earlycon_device early_console_dev = {
	.con = &early_con,
};

static const struct of_device_id __earlycon_of_table_sentinel
	__used __section(__earlycon_of_table_end);

static void __iomem * __init earlycon_map(unsigned long paddr, size_t size)
{
	void __iomem *base;
#ifdef CONFIG_FIX_EARLYCON_MEM
	set_fixmap_io(FIX_EARLYCON_MEM_BASE, paddr & PAGE_MASK);
	base = (void __iomem *)__fix_to_virt(FIX_EARLYCON_MEM_BASE);
	base += paddr & ~PAGE_MASK;
#else
	base = ioremap(paddr, size);
#endif
	if (!base)
		pr_err("%s: Couldn't map 0x%llx\n", __func__,
		       (unsigned long long)paddr);

	return base;
}

static int __init parse_options(struct earlycon_device *device,
				char *options)
{
	struct uart_port *port = &device->port;
	int mmio, mmio32, length;
	unsigned long addr;

	if (!options)
		return -ENODEV;

	mmio = !strncmp(options, "mmio,", 5);
	mmio32 = !strncmp(options, "mmio32,", 7);
	if (mmio || mmio32) {
		port->iotype = (mmio ? UPIO_MEM : UPIO_MEM32);
		options += mmio ? 5 : 7;
		addr = simple_strtoul(options, NULL, 0);
		port->mapbase = addr;
		if (mmio32)
			port->regshift = 2;
	} else if (!strncmp(options, "io,", 3)) {
		port->iotype = UPIO_PORT;
		options += 3;
		addr = simple_strtoul(options, NULL, 0);
		port->iobase = addr;
		mmio = 0;
	} else if (!strncmp(options, "0x", 2)) {
		port->iotype = UPIO_MEM;
		addr = simple_strtoul(options, NULL, 0);
		port->mapbase = addr;
	} else {
		return -EINVAL;
	}

	port->uartclk = BASE_BAUD * 16;

	options = strchr(options, ',');
	if (options) {
		options++;
		device->baud = simple_strtoul(options, NULL, 0);
		length = min(strcspn(options, " ") + 1,
			     (size_t)(sizeof(device->options)));
		strlcpy(device->options, options, length);
	}

	if (mmio || mmio32)
		pr_info("Early serial console at MMIO%s 0x%llx (options '%s')\n",
			mmio32 ? "32" : "",
			(unsigned long long)port->mapbase,
			device->options);
	else
		pr_info("Early serial console at I/O port 0x%lx (options '%s')\n",
			port->iobase,
			device->options);

	return 0;
}

int __init setup_earlycon(char *buf, const char *match,
			  int (*setup)(struct earlycon_device *, const char *))
{
	int err;
	size_t len;
	struct uart_port *port = &early_console_dev.port;

	if (!buf || !match || !setup)
		return 0;

	len = strlen(match);
	if (strncmp(buf, match, len))
		return 0;
	if (buf[len] && (buf[len] != ','))
		return 0;

	buf += len + 1;

	err = parse_options(&early_console_dev, buf);
	/* On parsing error, pass the options buf to the setup function */
	if (!err)
		buf = NULL;

	if (port->mapbase)
		port->membase = earlycon_map(port->mapbase, 64);

	early_console_dev.con->data = &early_console_dev;
	err = setup(&early_console_dev, buf);
	if (err < 0)
		return err;
	if (!early_console_dev.con->write)
		return -ENODEV;

	register_console(early_console_dev.con);
	return 0;
}

int __init of_setup_earlycon(unsigned long addr,
			     int (*setup)(struct earlycon_device *, const char *))
{
	int err;
	struct uart_port *port = &early_console_dev.port;

	port->iotype = UPIO_MEM;
	port->mapbase = addr;
	port->uartclk = BASE_BAUD * 16;
	port->membase = earlycon_map(addr, SZ_4K);

	early_console_dev.con->data = &early_console_dev;
	err = setup(&early_console_dev, NULL);
	if (err < 0)
		return err;
	if (!early_console_dev.con->write)
		return -ENODEV;


	register_console(early_console_dev.con);
	return 0;
}

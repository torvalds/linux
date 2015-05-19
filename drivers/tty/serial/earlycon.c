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

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

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

extern struct earlycon_id __earlycon_table[];
static const struct earlycon_id __earlycon_table_sentinel
	__used __section(__earlycon_table_end);

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

static int __init parse_options(struct earlycon_device *device, char *options)
{
	struct uart_port *port = &device->port;
	int length;
	unsigned long addr;

	if (uart_parse_earlycon(options, &port->iotype, &addr, &options))
		return -EINVAL;

	switch (port->iotype) {
	case UPIO_MEM32:
		port->regshift = 2;	/* fall-through */
	case UPIO_MEM:
		port->mapbase = addr;
		break;
	case UPIO_PORT:
		port->iobase = addr;
		break;
	default:
		return -EINVAL;
	}

	if (options) {
		device->baud = simple_strtoul(options, NULL, 0);
		length = min(strcspn(options, " ") + 1,
			     (size_t)(sizeof(device->options)));
		strlcpy(device->options, options, length);
	}

	if (port->iotype == UPIO_MEM || port->iotype == UPIO_MEM32)
		pr_info("Early serial console at MMIO%s 0x%llx (options '%s')\n",
			(port->iotype == UPIO_MEM32) ? "32" : "",
			(unsigned long long)port->mapbase,
			device->options);
	else
		pr_info("Early serial console at I/O port 0x%lx (options '%s')\n",
			port->iobase,
			device->options);

	return 0;
}

static int __init register_earlycon(char *buf, const struct earlycon_id *match)
{
	int err;
	struct uart_port *port = &early_console_dev.port;

	/* On parsing error, pass the options buf to the setup function */
	if (buf && !parse_options(&early_console_dev, buf))
		buf = NULL;

	port->uartclk = BASE_BAUD * 16;
	if (port->mapbase)
		port->membase = earlycon_map(port->mapbase, 64);

	early_console_dev.con->data = &early_console_dev;
	err = match->setup(&early_console_dev, buf);
	if (err < 0)
		return err;
	if (!early_console_dev.con->write)
		return -ENODEV;

	register_console(early_console_dev.con);
	return 0;
}

/**
 *	setup_earlycon - match and register earlycon console
 *	@buf:	earlycon param string
 *
 *	Registers the earlycon console matching the earlycon specified
 *	in the param string @buf. Acceptable param strings are of the form
 *	   <name>,io|mmio|mmio32,<addr>,<options>
 *	   <name>,0x<addr>,<options>
 *	   <name>,<options>
 *	   <name>
 *
 *	Only for the third form does the earlycon setup() method receive the
 *	<options> string in the 'options' parameter; all other forms set
 *	the parameter to NULL.
 *
 *	Returns 0 if an attempt to register the earlycon was made,
 *	otherwise negative error code
 */
int __init setup_earlycon(char *buf)
{
	const struct earlycon_id *match;

	if (!buf || !buf[0])
		return -EINVAL;

	if (early_con.flags & CON_ENABLED)
		return -EALREADY;

	for (match = __earlycon_table; match->name[0]; match++) {
		size_t len = strlen(match->name);

		if (strncmp(buf, match->name, len))
			continue;

		if (buf[len]) {
			if (buf[len] != ',')
				continue;
			buf += len + 1;
		} else
			buf = NULL;

		return register_earlycon(buf, match);
	}

	return -ENOENT;
}

/* early_param wrapper for setup_earlycon() */
static int __init param_setup_earlycon(char *buf)
{
	int err;

	/*
	 * Just 'earlycon' is a valid param for devicetree earlycons;
	 * don't generate a warning from parse_early_params() in that case
	 */
	if (!buf || !buf[0])
		return 0;

	err = setup_earlycon(buf);
	if (err == -ENOENT || err == -EALREADY)
		return 0;
	return err;
}
early_param("earlycon", param_setup_earlycon);

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

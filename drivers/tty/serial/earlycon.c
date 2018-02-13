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
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/acpi.h>

#ifdef CONFIG_FIX_EARLYCON_MEM
#include <asm/fixmap.h>
#endif

#include <asm/serial.h>

static struct console early_con = {
	.name =		"uart",		/* fixed up at earlycon registration */
	.flags =	CON_PRINTBUFFER | CON_BOOT,
	.index =	0,
};

static struct earlycon_device early_console_dev = {
	.con = &early_con,
};

static void __iomem * __init earlycon_map(resource_size_t paddr, size_t size)
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
		pr_err("%s: Couldn't map %pa\n", __func__, &paddr);

	return base;
}

static void __init earlycon_init(struct earlycon_device *device,
				 const char *name)
{
	struct console *earlycon = device->con;
	struct uart_port *port = &device->port;
	const char *s;
	size_t len;

	/* scan backwards from end of string for first non-numeral */
	for (s = name + strlen(name);
	     s > name && s[-1] >= '0' && s[-1] <= '9';
	     s--)
		;
	if (*s)
		earlycon->index = simple_strtoul(s, NULL, 10);
	len = s - name;
	strlcpy(earlycon->name, name, min(len + 1, sizeof(earlycon->name)));
	earlycon->data = &early_console_dev;

	if (port->iotype == UPIO_MEM || port->iotype == UPIO_MEM16 ||
	    port->iotype == UPIO_MEM32 || port->iotype == UPIO_MEM32BE)
		pr_info("%s%d at MMIO%s %pa (options '%s')\n",
			earlycon->name, earlycon->index,
			(port->iotype == UPIO_MEM) ? "" :
			(port->iotype == UPIO_MEM16) ? "16" :
			(port->iotype == UPIO_MEM32) ? "32" : "32be",
			&port->mapbase, device->options);
	else
		pr_info("%s%d at I/O port 0x%lx (options '%s')\n",
			earlycon->name, earlycon->index,
			port->iobase, device->options);
}

static int __init parse_options(struct earlycon_device *device, char *options)
{
	struct uart_port *port = &device->port;
	int length;
	resource_size_t addr;

	if (uart_parse_earlycon(options, &port->iotype, &addr, &options))
		return -EINVAL;

	switch (port->iotype) {
	case UPIO_MEM:
		port->mapbase = addr;
		break;
	case UPIO_MEM16:
		port->regshift = 1;
		port->mapbase = addr;
		break;
	case UPIO_MEM32:
	case UPIO_MEM32BE:
		port->regshift = 2;
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

	return 0;
}

static int __init register_earlycon(char *buf, const struct earlycon_id *match)
{
	int err;
	struct uart_port *port = &early_console_dev.port;

	/* On parsing error, pass the options buf to the setup function */
	if (buf && !parse_options(&early_console_dev, buf))
		buf = NULL;

	spin_lock_init(&port->lock);
	port->uartclk = BASE_BAUD * 16;
	if (port->mapbase)
		port->membase = earlycon_map(port->mapbase, 64);

	earlycon_init(&early_console_dev, match->name);
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
 *	   <name>,io|mmio|mmio32|mmio32be,<addr>,<options>
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

	for (match = __earlycon_table; match < __earlycon_table_end; match++) {
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

/*
 * When CONFIG_ACPI_SPCR_TABLE is defined, "earlycon" without parameters in
 * command line does not start DT earlycon immediately, instead it defers
 * starting it until DT/ACPI decision is made.  At that time if ACPI is enabled
 * call parse_spcr(), else call early_init_dt_scan_chosen_stdout()
 */
bool earlycon_init_is_deferred __initdata;

/* early_param wrapper for setup_earlycon() */
static int __init param_setup_earlycon(char *buf)
{
	int err;

	/*
	 * Just 'earlycon' is a valid param for devicetree earlycons;
	 * don't generate a warning from parse_early_params() in that case
	 */
	if (!buf || !buf[0]) {
		if (IS_ENABLED(CONFIG_ACPI_SPCR_TABLE)) {
			earlycon_init_is_deferred = true;
			return 0;
		} else if (!buf) {
			return early_init_dt_scan_chosen_stdout();
		}
	}

	err = setup_earlycon(buf);
	if (err == -ENOENT || err == -EALREADY)
		return 0;
	return err;
}
early_param("earlycon", param_setup_earlycon);

#ifdef CONFIG_OF_EARLY_FLATTREE

int __init of_setup_earlycon(const struct earlycon_id *match,
			     unsigned long node,
			     const char *options)
{
	int err;
	struct uart_port *port = &early_console_dev.port;
	const __be32 *val;
	bool big_endian;
	u64 addr;

	spin_lock_init(&port->lock);
	port->iotype = UPIO_MEM;
	addr = of_flat_dt_translate_address(node);
	if (addr == OF_BAD_ADDR) {
		pr_warn("[%s] bad address\n", match->name);
		return -ENXIO;
	}
	port->mapbase = addr;
	port->uartclk = BASE_BAUD * 16;

	val = of_get_flat_dt_prop(node, "reg-offset", NULL);
	if (val)
		port->mapbase += be32_to_cpu(*val);
	port->membase = earlycon_map(port->mapbase, SZ_4K);

	val = of_get_flat_dt_prop(node, "reg-shift", NULL);
	if (val)
		port->regshift = be32_to_cpu(*val);
	big_endian = of_get_flat_dt_prop(node, "big-endian", NULL) != NULL ||
		(IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) &&
		 of_get_flat_dt_prop(node, "native-endian", NULL) != NULL);
	val = of_get_flat_dt_prop(node, "reg-io-width", NULL);
	if (val) {
		switch (be32_to_cpu(*val)) {
		case 1:
			port->iotype = UPIO_MEM;
			break;
		case 2:
			port->iotype = UPIO_MEM16;
			break;
		case 4:
			port->iotype = (big_endian) ? UPIO_MEM32BE : UPIO_MEM32;
			break;
		default:
			pr_warn("[%s] unsupported reg-io-width\n", match->name);
			return -EINVAL;
		}
	}

	val = of_get_flat_dt_prop(node, "current-speed", NULL);
	if (val)
		early_console_dev.baud = be32_to_cpu(*val);

	if (options) {
		early_console_dev.baud = simple_strtoul(options, NULL, 0);
		strlcpy(early_console_dev.options, options,
			sizeof(early_console_dev.options));
	}
	earlycon_init(&early_console_dev, match->name);
	err = match->setup(&early_console_dev, options);
	if (err < 0)
		return err;
	if (!early_console_dev.con->write)
		return -ENODEV;


	register_console(early_console_dev.con);
	return 0;
}

#endif /* CONFIG_OF_EARLY_FLATTREE */

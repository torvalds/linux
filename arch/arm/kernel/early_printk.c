/*
 *  linux/arch/arm/kernel/early_printk.c
 *
 *  Copyright (C) 2009 Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/string.h>

extern void printascii(const char *);

static void early_write(const char *s, unsigned n)
{
	char buf[128];
	while (n) {
		unsigned l = min(n, sizeof(buf)-1);
		memcpy(buf, s, l);
		buf[l] = 0;
		s += l;
		n -= l;
		printascii(buf);
	}
}

static void early_console_write(struct console *con, const char *s, unsigned n)
{
	early_write(s, n);
}

static struct console early_console_dev = {
	.name =		"earlycon",
	.write =	early_console_write,
	.flags =	CON_PRINTBUFFER | CON_BOOT,
	.index =	-1,
};

static int __init setup_early_printk(char *buf)
{
	early_console = &early_console_dev;
	register_console(&early_console_dev);
	return 0;
}

early_param("earlyprintk", setup_early_printk);

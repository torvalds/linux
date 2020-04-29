// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/kernel/early_printk.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/string.h>
#include <mach/ocd.h>

/* On-Chip-Debugger functions */

static void early_ocd_write(struct console *con, const char *s, unsigned n)
{
	while (*s && n-- > 0) {
		if (*s == '\n')
			ocd_putc((int)'\r');
		ocd_putc((int)*s);
		s++;
	}
}

static struct console early_ocd_console = {
	.name =		"earlyocd",
	.write =	early_ocd_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

static int __init setup_early_printk(char *buf)
{
	if (!buf || early_console)
		return 0;

	early_console = &early_ocd_console;
	if (strstr(buf, "keep"))
		early_console->flags &= ~CON_BOOT;
	else
		early_console->flags |= CON_BOOT;
	register_console(early_console);
	return 0;
}
early_param("earlyprintk", setup_early_printk);

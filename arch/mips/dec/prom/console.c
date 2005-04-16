/*
 *	arch/mips/dec/prom/console.c
 *
 *	DECstation PROM-based early console support.
 *
 *	Copyright (C) 2004  Maciej W. Rozycki
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#include <linux/console.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/dec/prom.h>

static void __init prom_console_write(struct console *con, const char *s,
				      unsigned int c)
{
	static char sfmt[] __initdata = "%%%us";
	char fmt[13];

	snprintf(fmt, sizeof(fmt), sfmt, c);
	prom_printf(fmt, s);
}

static struct console promcons __initdata = {
	.name	= "prom",
	.write	= prom_console_write,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

static int promcons_output __initdata = 0;

void __init register_prom_console(void)
{
	if (!promcons_output) {
		promcons_output = 1;
		register_console(&promcons);
	}
}

void __init unregister_prom_console(void)
{
	if (promcons_output) {
		unregister_console(&promcons);
		promcons_output = 0;
	}
}

void disable_early_printk(void)
	__attribute__((alias("unregister_prom_console")));

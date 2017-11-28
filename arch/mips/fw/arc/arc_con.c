// SPDX-License-Identifier: GPL-2.0
/*
 * Wrap-around code for a console using the
 * ARC io-routines.
 *
 * Copyright (c) 1998 Harald Koerfgen
 * Copyright (c) 2001 Ralf Baechle
 * Copyright (c) 2002 Thiemo Seufer
 */
#include <linux/tty.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/fs.h>
#include <asm/sgialib.h>

static void prom_console_write(struct console *co, const char *s,
			       unsigned count)
{
	/* Do each character */
	while (count--) {
		if (*s == '\n')
			prom_putchar('\r');
		prom_putchar(*s++);
	}
}

static int prom_console_setup(struct console *co, char *options)
{
	return !(prom_flags & PROM_FLAG_USE_AS_CONSOLE);
}

static struct console arc_cons = {
	.name		= "arc",
	.write		= prom_console_write,
	.setup		= prom_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

/*
 *    Register console.
 */

static int __init arc_console_init(void)
{
	register_console(&arc_cons);

	return 0;
}
console_initcall(arc_console_init);

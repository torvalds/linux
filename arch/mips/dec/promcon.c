/*
 * Wrap-around code for a console using the
 * DECstation PROM io-routines.
 *
 * Copyright (c) 1998 Harald Koerfgen
 */

#include <linux/tty.h>
#include <linux/ptrace.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/fs.h>

#include <asm/dec/prom.h>

static void prom_console_write(struct console *co, const char *s,
			       unsigned count)
{
	unsigned i;

	/*
	 *    Now, do each character
	 */
	for (i = 0; i < count; i++) {
		if (*s == 10)
			prom_printf("%c", 13);
		prom_printf("%c", *s++);
	}
}

static int __init prom_console_setup(struct console *co, char *options)
{
	return 0;
}

static struct console sercons =
{
	.name	= "ttyS",
	.write	= prom_console_write,
	.setup	= prom_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

/*
 *    Register console.
 */

static int __init prom_console_init(void)
{
	register_console(&sercons);

	return 0;
}
console_initcall(prom_console_init);

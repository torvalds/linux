/*
 * Wrap-around code for a console using the
 * SGI PROM io-routines.
 *
 * Copyright (c) 1999 Ulf Carlsson
 *
 * Derived from DECstation promcon.c
 * Copyright (c) 1998 Harald Koerfgen
 */
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/console.h>

static void prom_console_write(struct console *co, const char *s,
			       unsigned count)
{
	extern int CONSOLE_CHANNEL;	// The default serial port
	unsigned i;

	for (i = 0; i < count; i++) {
		if (*s == 10)
			serial_putc(CONSOLE_CHANNEL, 13);
		serial_putc(CONSOLE_CHANNEL, *s++);
	}
}

int prom_getchar(void)
{
	return 0;
}

static struct console sercons = {
    .name	= "ttyS",
    .write	= prom_console_write,
    .flags	= CON_PRINTBUFFER,
    .index	= -1,
};

/*
 *    Register console.
 */

static int gal_serial_console_init(void)
{
	//  serial_init();
	//serial_set(115200);

	register_console(&sercons);

	return 0;
}

console_initcall(gal_serial_console_init);

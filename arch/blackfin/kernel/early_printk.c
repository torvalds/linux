/*
 * File:         arch/blackfin/kernel/early_printk.c
 * Based on:     arch/x86_64/kernel/early_printk.c
 * Author:       Robin Getz <rgetz@blackfin.uclinux.org
 *
 * Created:      14Aug2007
 * Description:  allow a console to be used for early printk
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serial_core.h>
#include <linux/console.h>
#include <linux/string.h>
#include <asm/blackfin.h>
#include <asm/irq_handler.h>
#include <asm/early_printk.h>

#ifdef CONFIG_SERIAL_BFIN
extern struct console *bfin_earlyserial_init(unsigned int port,
						unsigned int cflag);
#endif

static struct console *early_console;

/* Default console
 * Port n == ttyBFn
 * cflags == UART output modes
 */
#define DEFAULT_PORT 0
#define DEFAULT_CFLAG CS8|B57600

#ifdef CONFIG_SERIAL_CORE
/* What should get here is "0,57600" */
static struct console * __init earlyserial_init(char *buf)
{
	int baud, bit;
	char parity;
	unsigned int serial_port = DEFAULT_PORT;
	unsigned int cflag = DEFAULT_CFLAG;

	serial_port = simple_strtoul(buf, &buf, 10);
	buf++;

	cflag = 0;
	baud = simple_strtoul(buf, &buf, 10);
	switch (baud) {
	case 1200:
		cflag |= B1200;
		break;
	case 2400:
		cflag |= B2400;
		break;
	case 4800:
		cflag |= B4800;
		break;
	case 9600:
		cflag |= B9600;
		break;
	case 19200:
		cflag |= B19200;
		break;
	case 38400:
		cflag |= B38400;
		break;
	case 115200:
		cflag |= B115200;
		break;
	default:
		cflag |= B57600;
	}

	parity = buf[0];
	buf++;
	switch (parity) {
	case 'e':
		cflag |= PARENB;
		break;
	case 'o':
		cflag |= PARODD;
		break;
	}

	bit = simple_strtoul(buf, &buf, 10);
	switch (bit) {
	case 5:
		cflag |= CS5;
		break;
	case 6:
		cflag |= CS5;
		break;
	case 7:
		cflag |= CS5;
		break;
	default:
		cflag |= CS8;
	}

#ifdef CONFIG_SERIAL_BFIN
	return bfin_earlyserial_init(serial_port, cflag);
#else
	return NULL;
#endif

}
#endif

int __init setup_early_printk(char *buf)
{

	/* Crashing in here would be really bad, so check both the var
	   and the pointer before we start using it
	 */
	if (!buf)
		return 0;

	if (!*buf)
		return 0;

	if (early_console != NULL)
		return 0;

#ifdef CONFIG_SERIAL_BFIN
	/* Check for Blackfin Serial */
	if (!strncmp(buf, "serial,uart", 11)) {
		buf += 11;
		early_console = earlyserial_init(buf);
	}
#endif
#ifdef CONFIG_FB
		/* TODO: add framebuffer console support */
#endif

	if (likely(early_console)) {
		early_console->flags |= CON_BOOT;

		register_console(early_console);
		printk(KERN_INFO "early printk enabled on %s%d\n",
			early_console->name,
			early_console->index);
	}

	return 0;
}

early_param("earlyprintk", setup_early_printk);

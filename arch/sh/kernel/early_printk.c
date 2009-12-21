/*
 * arch/sh/kernel/early_printk.c
 *
 *  Copyright (C) 1999, 2000  Niibe Yutaka
 *  Copyright (C) 2002  M. R. Brown
 *  Copyright (C) 2004 - 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>

#include <asm/sh_bios.h>

/*
 *	Print a string through the BIOS
 */
static void sh_console_write(struct console *co, const char *s,
				 unsigned count)
{
	sh_bios_console_write(s, count);
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first rs_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
static int __init sh_console_setup(struct console *co, char *options)
{
	int	cflag = CREAD | HUPCL | CLOCAL;

	/*
	 *	Now construct a cflag setting.
	 *	TODO: this is a totally bogus cflag, as we have
	 *	no idea what serial settings the BIOS is using, or
	 *	even if its using the serial port at all.
	 */
	cflag |= B115200 | CS8 | /*no parity*/0;

	co->cflag = cflag;

	return 0;
}

static struct console bios_console = {
	.name		= "bios",
	.write		= sh_console_write,
	.setup		= sh_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static struct console *early_console;

static int __init setup_early_printk(char *buf)
{
	int keep_early = 0;

	if (!buf)
		return 0;

	if (strstr(buf, "keep"))
		keep_early = 1;

	if (!strncmp(buf, "bios", 4))
		early_console = &bios_console;

	if (likely(early_console)) {
		if (keep_early)
			early_console->flags &= ~CON_BOOT;
		else
			early_console->flags |= CON_BOOT;
		register_console(early_console);
	}

	return 0;
}
early_param("earlyprintk", setup_early_printk);

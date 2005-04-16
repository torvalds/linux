/*
 * arch/sh/kernel/early_printk.c
 *
 *  Copyright (C) 1999, 2000  Niibe Yutaka
 *  Copyright (C) 2002  M. R. Brown
 *  Copyright (C) 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <asm/io.h>

#ifdef CONFIG_SH_STANDARD_BIOS
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

static struct console early_console = {
	.name		= "bios",
	.write		= sh_console_write,
	.setup		= sh_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};
#endif

#ifdef CONFIG_EARLY_SCIF_CONSOLE
#define SCIF_REG	0xffe80000

static void scif_sercon_putc(int c)
{
	while (!(ctrl_inw(SCIF_REG + 0x10) & 0x20)) ;

	ctrl_outb(c, SCIF_REG + 12);
	ctrl_outw((ctrl_inw(SCIF_REG + 0x10) & 0x9f), SCIF_REG + 0x10);

	if (c == '\n')
		scif_sercon_putc('\r');
}

static void scif_sercon_flush(void)
{
	ctrl_outw((ctrl_inw(SCIF_REG + 0x10) & 0xbf), SCIF_REG + 0x10);

	while (!(ctrl_inw(SCIF_REG + 0x10) & 0x40)) ;

	ctrl_outw((ctrl_inw(SCIF_REG + 0x10) & 0xbf), SCIF_REG + 0x10);
}

static void scif_sercon_write(struct console *con, const char *s, unsigned count)
{
	while (count-- > 0)
		scif_sercon_putc(*s++);

	scif_sercon_flush();
}

static int __init scif_sercon_setup(struct console *con, char *options)
{
	con->cflag = CREAD | HUPCL | CLOCAL | B115200 | CS8;

	return 0;
}

static struct console early_console = {
	.name		= "sercon",
	.write		= scif_sercon_write,
	.setup		= scif_sercon_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

void scif_sercon_init(int baud)
{
	ctrl_outw(0, SCIF_REG + 8);
	ctrl_outw(0, SCIF_REG);

	/* Set baud rate */
	ctrl_outb((CONFIG_SH_PCLK_FREQ + 16 * baud) /
		  (32 * baud) - 1, SCIF_REG + 4);

	ctrl_outw(12, SCIF_REG + 24);
	ctrl_outw(8, SCIF_REG + 24);
	ctrl_outw(0, SCIF_REG + 32);
	ctrl_outw(0x60, SCIF_REG + 16);
	ctrl_outw(0, SCIF_REG + 36);
	ctrl_outw(0x30, SCIF_REG + 8);
}
#endif

void __init enable_early_printk(void)
{
#ifdef CONFIG_EARLY_SCIF_CONSOLE
	scif_sercon_init(115200);
#endif
	register_console(&early_console);
}

void disable_early_printk(void)
{
	unregister_console(&early_console);
}


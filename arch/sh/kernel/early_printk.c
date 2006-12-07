/*
 * arch/sh/kernel/early_printk.c
 *
 *  Copyright (C) 1999, 2000  Niibe Yutaka
 *  Copyright (C) 2002  M. R. Brown
 *  Copyright (C) 2004 - 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/io.h>

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

static struct console bios_console = {
	.name		= "bios",
	.write		= sh_console_write,
	.setup		= sh_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};
#endif

#ifdef CONFIG_EARLY_SCIF_CONSOLE
#include <linux/serial_core.h>
#include "../../../drivers/serial/sh-sci.h"

static struct uart_port scif_port = {
	.mapbase	= CONFIG_EARLY_SCIF_CONSOLE_PORT,
	.membase	= (char __iomem *)CONFIG_EARLY_SCIF_CONSOLE_PORT,
};

static void scif_sercon_putc(int c)
{
	while (((sci_in(&scif_port, SCFDR) & 0x1f00 >> 8) == 16))
		;

	sci_out(&scif_port, SCxTDR, c);
	sci_in(&scif_port, SCxSR);
	sci_out(&scif_port, SCxSR, 0xf3 & ~(0x20 | 0x40));

	while ((sci_in(&scif_port, SCxSR) & 0x40) == 0);
		;

	if (c == '\n')
		scif_sercon_putc('\r');
}

static void scif_sercon_write(struct console *con, const char *s,
			      unsigned count)
{
	while (count-- > 0)
		scif_sercon_putc(*s++);
}

static int __init scif_sercon_setup(struct console *con, char *options)
{
	con->cflag = CREAD | HUPCL | CLOCAL | B115200 | CS8;

	return 0;
}

static struct console scif_console = {
	.name		= "sercon",
	.write		= scif_sercon_write,
	.setup		= scif_sercon_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

#if defined(CONFIG_CPU_SH4) && !defined(CONFIG_SH_STANDARD_BIOS)
/*
 * Simple SCIF init, primarily aimed at SH7750 and other similar SH-4
 * devices that aren't using sh-ipl+g.
 */
static void scif_sercon_init(int baud)
{
	ctrl_outw(0, scif_port.mapbase + 8);
	ctrl_outw(0, scif_port.mapbase);

	/* Set baud rate */
	ctrl_outb((CONFIG_SH_PCLK_FREQ + 16 * baud) /
		  (32 * baud) - 1, scif_port.mapbase + 4);

	ctrl_outw(12, scif_port.mapbase + 24);
	ctrl_outw(8, scif_port.mapbase + 24);
	ctrl_outw(0, scif_port.mapbase + 32);
	ctrl_outw(0x60, scif_port.mapbase + 16);
	ctrl_outw(0, scif_port.mapbase + 36);
	ctrl_outw(0x30, scif_port.mapbase + 8);
}
#endif /* CONFIG_CPU_SH4 && !CONFIG_SH_STANDARD_BIOS */
#endif /* CONFIG_EARLY_SCIF_CONSOLE */

/*
 * Setup a default console, if more than one is compiled in, rely on the
 * earlyprintk= parsing to give priority.
 */
static struct console *early_console =
#ifdef CONFIG_SH_STANDARD_BIOS
	&bios_console
#elif defined(CONFIG_EARLY_SCIF_CONSOLE)
	&scif_console
#else
	NULL
#endif
	;

static int __initdata keep_early;

int __init setup_early_printk(char *opt)
{
	char *space;
	char buf[256];

	strlcpy(buf, opt, sizeof(buf));
	space = strchr(buf, ' ');
	if (space)
		*space = 0;

	if (strstr(buf, "keep"))
		keep_early = 1;

#ifdef CONFIG_SH_STANDARD_BIOS
	if (!strncmp(buf, "bios", 4))
		early_console = &bios_console;
#endif
#if defined(CONFIG_EARLY_SCIF_CONSOLE)
	if (!strncmp(buf, "serial", 6)) {
		early_console = &scif_console;

#if defined(CONFIG_CPU_SH4) && !defined(CONFIG_SH_STANDARD_BIOS)
		scif_sercon_init(115200);
#endif
	}
#endif

	if (likely(early_console))
		register_console(early_console);

	return 1;
}
__setup("earlyprintk=", setup_early_printk);

void __init disable_early_printk(void)
{
	if (!keep_early) {
		printk("disabling early console\n");
		unregister_console(early_console);
	} else
		printk("keeping early console\n");
}

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

#if defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721)
#define EPK_SCSMR_VALUE 0x000
#define EPK_SCBRR_VALUE 0x00C
#define EPK_FIFO_SIZE 64
#define EPK_FIFO_BITS (0x7f00 >> 8)
#else
#define EPK_FIFO_SIZE 16
#define EPK_FIFO_BITS (0x1f00 >> 8)
#endif

static struct uart_port scif_port = {
	.mapbase	= CONFIG_EARLY_SCIF_CONSOLE_PORT,
	.membase	= (char __iomem *)CONFIG_EARLY_SCIF_CONSOLE_PORT,
};

static void scif_sercon_putc(int c)
{
	while (((sci_in(&scif_port, SCFDR) & EPK_FIFO_BITS) >= EPK_FIFO_SIZE))
		;

	sci_out(&scif_port, SCxTDR, c);
	sci_in(&scif_port, SCxSR);
	sci_out(&scif_port, SCxSR, 0xf3 & ~(0x20 | 0x40));

	while ((sci_in(&scif_port, SCxSR) & 0x40) == 0)
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

#if !defined(CONFIG_SH_STANDARD_BIOS)
#if defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721)
static void scif_sercon_init(char *s)
{
	sci_out(&scif_port, SCSCR, 0x0000);	/* clear TE and RE */
	sci_out(&scif_port, SCFCR, 0x4006);	/* reset */
	sci_out(&scif_port, SCSCR, 0x0000);	/* select internal clock */
	sci_out(&scif_port, SCSMR, EPK_SCSMR_VALUE);
	sci_out(&scif_port, SCBRR, EPK_SCBRR_VALUE);

	mdelay(1);	/* wait 1-bit time */

	sci_out(&scif_port, SCFCR, 0x0030);	/* TTRG=b'11 */
	sci_out(&scif_port, SCSCR, 0x0030);	/* TE, RE */
}
#elif defined(CONFIG_CPU_SH4)
#define DEFAULT_BAUD 115200
/*
 * Simple SCIF init, primarily aimed at SH7750 and other similar SH-4
 * devices that aren't using sh-ipl+g.
 */
static void scif_sercon_init(char *s)
{
	struct uart_port *port = &scif_port;
	unsigned baud = DEFAULT_BAUD;
	unsigned int status;
	char *e;

	if (*s == ',')
		++s;

	if (*s) {
		/* ignore ioport/device name */
		s += strcspn(s, ",");
		if (*s == ',')
			s++;
	}

	if (*s) {
		baud = simple_strtoul(s, &e, 0);
		if (baud == 0 || s == e)
			baud = DEFAULT_BAUD;
	}

	do {
		status = sci_in(port, SCxSR);
	} while (!(status & SCxSR_TEND(port)));

	sci_out(port, SCSCR, 0);	 /* TE=0, RE=0 */
	sci_out(port, SCFCR, SCFCR_RFRST | SCFCR_TFRST);
	sci_out(port, SCSMR, 0);

	/* Set baud rate */
	sci_out(port, SCBRR, (CONFIG_SH_PCLK_FREQ + 16 * baud) /
		(32 * baud) - 1);
	udelay((1000000+(baud-1)) / baud); /* Wait one bit interval */

	sci_out(port, SCSPTR, 0);
	sci_out(port, SCxSR, 0x60);
	sci_out(port, SCLSR, 0);

	sci_out(port, SCFCR, 0);
	sci_out(port, SCSCR, 0x30);	 /* TE=1, RE=1 */
}
#endif /* defined(CONFIG_CPU_SUBTYPE_SH7720) */
#endif /* !defined(CONFIG_SH_STANDARD_BIOS) */
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

static int __init setup_early_printk(char *buf)
{
	int keep_early = 0;

	if (!buf)
		return 0;

	if (strstr(buf, "keep"))
		keep_early = 1;

#ifdef CONFIG_SH_STANDARD_BIOS
	if (!strncmp(buf, "bios", 4))
		early_console = &bios_console;
#endif
#if defined(CONFIG_EARLY_SCIF_CONSOLE)
	if (!strncmp(buf, "serial", 6)) {
		early_console = &scif_console;

#if !defined(CONFIG_SH_STANDARD_BIOS)
#if defined(CONFIG_CPU_SH4) || defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721)
		scif_sercon_init(buf + 6);
#endif
#endif
	}
#endif

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

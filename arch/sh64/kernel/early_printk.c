/*
 * arch/sh64/kernel/early_printk.c
 *
 * SH-5 Early SCIF console (cloned and hacked from sh implementation)
 *
 * Copyright (C) 2003, 2004  Paul Mundt <lethal@linux-sh.org>
 * Copyright (C) 2002  M. R. Brown <mrbrown@0xd6.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/hardware.h>

#define SCIF_BASE_ADDR	0x01030000
#define SCIF_ADDR_SH5	PHYS_PERIPHERAL_BLOCK+SCIF_BASE_ADDR

/*
 * Fixed virtual address where SCIF is mapped (should already be done
 * in arch/sh64/kernel/head.S!).
 */
#define SCIF_REG	0xfa030000

enum {
	SCIF_SCSMR2	= SCIF_REG + 0x00,
	SCIF_SCBRR2	= SCIF_REG + 0x04,
	SCIF_SCSCR2	= SCIF_REG + 0x08,
	SCIF_SCFTDR2	= SCIF_REG + 0x0c,
	SCIF_SCFSR2	= SCIF_REG + 0x10,
	SCIF_SCFRDR2	= SCIF_REG + 0x14,
	SCIF_SCFCR2	= SCIF_REG + 0x18,
	SCIF_SCFDR2	= SCIF_REG + 0x1c,
	SCIF_SCSPTR2	= SCIF_REG + 0x20,
	SCIF_SCLSR2	= SCIF_REG + 0x24,
};

static void sh_console_putc(int c)
{
	while (!(ctrl_inw(SCIF_SCFSR2) & 0x20))
		cpu_relax();

	ctrl_outb(c, SCIF_SCFTDR2);
	ctrl_outw((ctrl_inw(SCIF_SCFSR2) & 0x9f), SCIF_SCFSR2);

	if (c == '\n')
		sh_console_putc('\r');
}

static void sh_console_flush(void)
{
	ctrl_outw((ctrl_inw(SCIF_SCFSR2) & 0xbf), SCIF_SCFSR2);

	while (!(ctrl_inw(SCIF_SCFSR2) & 0x40))
		cpu_relax();

	ctrl_outw((ctrl_inw(SCIF_SCFSR2) & 0xbf), SCIF_SCFSR2);
}

static void sh_console_write(struct console *con, const char *s, unsigned count)
{
	while (count-- > 0)
		sh_console_putc(*s++);

	sh_console_flush();
}

static int __init sh_console_setup(struct console *con, char *options)
{
	con->cflag = CREAD | HUPCL | CLOCAL | B19200 | CS8;

	return 0;
}

static struct console sh_console = {
	.name		= "scifcon",
	.write		= sh_console_write,
	.setup		= sh_console_setup,
	.flags		= CON_PRINTBUFFER | CON_BOOT,
	.index		= -1,
};

void __init enable_early_printk(void)
{
	ctrl_outb(0x2a, SCIF_SCBRR2);	/* 19200bps */

	ctrl_outw(0x04, SCIF_SCFCR2);	/* Reset TFRST */
	ctrl_outw(0x10, SCIF_SCFCR2);	/* TTRG0=1 */

	ctrl_outw(0, SCIF_SCSPTR2);
	ctrl_outw(0x60, SCIF_SCFSR2);
	ctrl_outw(0, SCIF_SCLSR2);
	ctrl_outw(0x30, SCIF_SCSCR2);

	register_console(&sh_console);
}

/* 
 *    PDC Console support - ie use firmware to dump text via boot console
 *
 *    Copyright (C) 1999-2003 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2000 Martin K Petersen <mkp at mkp.net>
 *    Copyright (C) 2000 John Marvin <jsm at parisc-linux.org>
 *    Copyright (C) 2000-2003 Paul Bame <bame at parisc-linux.org>
 *    Copyright (C) 2000 Philipp Rumpf <prumpf with tux.org>
 *    Copyright (C) 2000 Michael Ang <mang with subcarrier.org>
 *    Copyright (C) 2000 Grant Grundler <grundler with parisc-linux.org>
 *    Copyright (C) 2001-2002 Ryan Bradetich <rbrad at parisc-linux.org>
 *    Copyright (C) 2001 Helge Deller <deller at parisc-linux.org>
 *    Copyright (C) 2001 Thomas Bogendoerfer <tsbogend at parisc-linux.org>
 *    Copyright (C) 2002 Randolph Chung <tausq with parisc-linux.org>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 *  The PDC console is a simple console, which can be used for debugging 
 *  boot related problems on HP PA-RISC machines.
 *
 *  This code uses the ROM (=PDC) based functions to read and write characters
 *  from and to PDC's boot path.
 *  Since all character read from that path must be polled, this code never
 *  can or will be a fully functional linux console.
 */

/* Define EARLY_BOOTUP_DEBUG to debug kernel related boot problems. 
 * On production kernels EARLY_BOOTUP_DEBUG should be undefined. */
#undef EARLY_BOOTUP_DEBUG


#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/tty.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/system.h>
#include <asm/pdc.h>		/* for iodc_call() proto and friends */


static void pdc_console_write(struct console *co, const char *s, unsigned count)
{
	while(count--)
		pdc_iodc_putc(*s++);
}

void pdc_outc(unsigned char c)
{
	pdc_iodc_outc(c);
}

void pdc_printf(const char *fmt, ...)
{
	va_list args;
	char buf[1024];
	int i, len;

	va_start(args, fmt);
	len = vscnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	for (i = 0; i < len; i++)
		pdc_iodc_outc(buf[i]);
}

int pdc_console_poll_key(struct console *co)
{
	return pdc_iodc_getc();
}

static int pdc_console_setup(struct console *co, char *options)
{
	return 0;
}

#if defined(CONFIG_PDC_CONSOLE)
#define PDC_CONSOLE_DEVICE pdc_console_device
static struct tty_driver * pdc_console_device (struct console *c, int *index)
{
	extern struct tty_driver console_driver;
	*index = c->index ? c->index-1 : fg_console;
	return &console_driver;
}
#else
#define PDC_CONSOLE_DEVICE NULL
#endif

static struct console pdc_cons = {
	.name =		"ttyB",
	.write =	pdc_console_write,
	.device =	PDC_CONSOLE_DEVICE,
	.setup =	pdc_console_setup,
	.flags =	CON_BOOT|CON_PRINTBUFFER|CON_ENABLED,
	.index =	-1,
};

static int pdc_console_initialized;
extern unsigned long con_start;	/* kernel/printk.c */
extern unsigned long log_end;	/* kernel/printk.c */


static void pdc_console_init_force(void)
{
	if (pdc_console_initialized)
		return;
	++pdc_console_initialized;
	
	/* If the console is duplex then copy the COUT parameters to CIN. */
	if (PAGE0->mem_cons.cl_class == CL_DUPLEX)
		memcpy(&PAGE0->mem_kbd, &PAGE0->mem_cons, sizeof(PAGE0->mem_cons));

	/* register the pdc console */
	register_console(&pdc_cons);
}

void __init pdc_console_init(void)
{
#if defined(EARLY_BOOTUP_DEBUG) || defined(CONFIG_PDC_CONSOLE)
	pdc_console_init_force();
#endif
#ifdef EARLY_BOOTUP_DEBUG
	printk(KERN_INFO "Initialized PDC Console for debugging.\n");
#endif
}


/* Unregister the pdc console with the printk console layer */
void pdc_console_die(void)
{
	if (!pdc_console_initialized)
		return;
	--pdc_console_initialized;

	printk(KERN_INFO "Switching from PDC console\n");

	/* Don't repeat what we've already printed */
	con_start = log_end;

	unregister_console(&pdc_cons);
}


/*
 * Used for emergencies. Currently only used if an HPMC occurs. If an
 * HPMC occurs, it is possible that the current console may not be
 * properly initialed after the PDC IO reset. This routine unregisters all
 * of the current consoles, reinitializes the pdc console and
 * registers it.
 */

void pdc_console_restart(void)
{
	struct console *console;

	if (pdc_console_initialized)
		return;

	while ((console = console_drivers) != NULL)
		unregister_console(console_drivers);

	/* Don't repeat what we've already printed */
	con_start = log_end;
	
	/* force registering the pdc console */
	pdc_console_init_force();
}


// SPDX-License-Identifier: GPL-2.0-or-later
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
 *    Copyright (C) 2010 Guy Martin <gmsoft at tuxicoman.be>
 */

/*
 *  The PDC console is a simple console, which can be used for debugging 
 *  boot related problems on HP PA-RISC machines. It is also useful when no
 *  other console works.
 *
 *  This code uses the ROM (=PDC) based functions to read and write characters
 *  from and to PDC's boot path.
 */

/* Define EARLY_BOOTUP_DEBUG to debug kernel related boot problems. 
 * On production kernels EARLY_BOOTUP_DEBUG should be undefined. */
#define EARLY_BOOTUP_DEBUG


#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/tty.h>
#include <asm/page.h>		/* for PAGE0 */
#include <asm/pdc.h>		/* for iodc_call() proto and friends */

static DEFINE_SPINLOCK(pdc_console_lock);
static struct console pdc_cons;

static void pdc_console_write(struct console *co, const char *s, unsigned count)
{
	int i = 0;
	unsigned long flags;

	spin_lock_irqsave(&pdc_console_lock, flags);
	do {
		i += pdc_iodc_print(s + i, count - i);
	} while (i < count);
	spin_unlock_irqrestore(&pdc_console_lock, flags);
}

int pdc_console_poll_key(struct console *co)
{
	int c;
	unsigned long flags;

	spin_lock_irqsave(&pdc_console_lock, flags);
	c = pdc_iodc_getc();
	spin_unlock_irqrestore(&pdc_console_lock, flags);

	return c;
}

static int pdc_console_setup(struct console *co, char *options)
{
	return 0;
}

#if defined(CONFIG_PDC_CONSOLE)
#include <linux/vt_kern.h>
#include <linux/tty_flip.h>

#define PDC_CONS_POLL_DELAY (30 * HZ / 1000)

static void pdc_console_poll(struct timer_list *unused);
static DEFINE_TIMER(pdc_console_timer, pdc_console_poll);
static struct tty_port tty_port;

static int pdc_console_tty_open(struct tty_struct *tty, struct file *filp)
{
	tty_port_tty_set(&tty_port, tty);
	mod_timer(&pdc_console_timer, jiffies + PDC_CONS_POLL_DELAY);

	return 0;
}

static void pdc_console_tty_close(struct tty_struct *tty, struct file *filp)
{
	if (tty->count == 1) {
		del_timer_sync(&pdc_console_timer);
		tty_port_tty_set(&tty_port, NULL);
	}
}

static int pdc_console_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	pdc_console_write(NULL, buf, count);
	return count;
}

static unsigned int pdc_console_tty_write_room(struct tty_struct *tty)
{
	return 32768; /* no limit, no buffer used */
}

static const struct tty_operations pdc_console_tty_ops = {
	.open = pdc_console_tty_open,
	.close = pdc_console_tty_close,
	.write = pdc_console_tty_write,
	.write_room = pdc_console_tty_write_room,
};

static void pdc_console_poll(struct timer_list *unused)
{
	int data, count = 0;

	while (1) {
		data = pdc_console_poll_key(NULL);
		if (data == -1)
			break;
		tty_insert_flip_char(&tty_port, data & 0xFF, TTY_NORMAL);
		count ++;
	}

	if (count)
		tty_flip_buffer_push(&tty_port);

	if (pdc_cons.flags & CON_ENABLED)
		mod_timer(&pdc_console_timer, jiffies + PDC_CONS_POLL_DELAY);
}

static struct tty_driver *pdc_console_tty_driver;

static int __init pdc_console_tty_driver_init(void)
{
	int err;

	/* Check if the console driver is still registered.
	 * It is unregistered if the pdc console was not selected as the
	 * primary console. */

	struct console *tmp;

	console_lock();
	for_each_console(tmp)
		if (tmp == &pdc_cons)
			break;
	console_unlock();

	if (!tmp) {
		printk(KERN_INFO "PDC console driver not registered anymore, not creating %s\n", pdc_cons.name);
		return -ENODEV;
	}

	printk(KERN_INFO "The PDC console driver is still registered, removing CON_BOOT flag\n");
	pdc_cons.flags &= ~CON_BOOT;

	pdc_console_tty_driver = alloc_tty_driver(1);

	if (!pdc_console_tty_driver)
		return -ENOMEM;

	tty_port_init(&tty_port);

	pdc_console_tty_driver->driver_name = "pdc_cons";
	pdc_console_tty_driver->name = "ttyB";
	pdc_console_tty_driver->major = MUX_MAJOR;
	pdc_console_tty_driver->minor_start = 0;
	pdc_console_tty_driver->type = TTY_DRIVER_TYPE_SYSTEM;
	pdc_console_tty_driver->init_termios = tty_std_termios;
	pdc_console_tty_driver->flags = TTY_DRIVER_REAL_RAW |
		TTY_DRIVER_RESET_TERMIOS;
	tty_set_operations(pdc_console_tty_driver, &pdc_console_tty_ops);
	tty_port_link_device(&tty_port, pdc_console_tty_driver, 0);

	err = tty_register_driver(pdc_console_tty_driver);
	if (err) {
		printk(KERN_ERR "Unable to register the PDC console TTY driver\n");
		tty_port_destroy(&tty_port);
		return err;
	}

	return 0;
}
device_initcall(pdc_console_tty_driver_init);

static struct tty_driver * pdc_console_device (struct console *c, int *index)
{
	*index = c->index;
	return pdc_console_tty_driver;
}
#else
#define pdc_console_device NULL
#endif

static struct console pdc_cons = {
	.name =		"ttyB",
	.write =	pdc_console_write,
	.device =	pdc_console_device,
	.setup =	pdc_console_setup,
	.flags =	CON_BOOT | CON_PRINTBUFFER,
	.index =	-1,
};

static int pdc_console_initialized;

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


/*
 * Used for emergencies. Currently only used if an HPMC occurs. If an
 * HPMC occurs, it is possible that the current console may not be
 * properly initialised after the PDC IO reset. This routine unregisters
 * all of the current consoles, reinitializes the pdc console and
 * registers it.
 */

void pdc_console_restart(void)
{
	struct console *console;

	if (pdc_console_initialized)
		return;

	/* If we've already seen the output, don't bother to print it again */
	if (console_drivers != NULL)
		pdc_cons.flags &= ~CON_PRINTBUFFER;

	while ((console = console_drivers) != NULL)
		unregister_console(console_drivers);

	/* force registering the pdc console */
	pdc_console_init_force();
}

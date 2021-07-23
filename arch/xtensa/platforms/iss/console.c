/*
 * arch/xtensa/platforms/iss/console.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2005 Tensilica Inc.
 *   Authors	Christian Zankel, Joe Taylor
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/param.h>
#include <linux/seq_file.h>
#include <linux/serial.h>

#include <linux/uaccess.h>
#include <asm/irq.h>

#include <platform/simcall.h>

#include <linux/tty.h>
#include <linux/tty_flip.h>

#define SERIAL_MAX_NUM_LINES 1
#define SERIAL_TIMER_VALUE (HZ / 10)

static void rs_poll(struct timer_list *);

static struct tty_driver *serial_driver;
static struct tty_port serial_port;
static DEFINE_TIMER(serial_timer, rs_poll);
static DEFINE_SPINLOCK(timer_lock);

static int rs_open(struct tty_struct *tty, struct file * filp)
{
	spin_lock_bh(&timer_lock);
	if (tty->count == 1)
		mod_timer(&serial_timer, jiffies + SERIAL_TIMER_VALUE);
	spin_unlock_bh(&timer_lock);

	return 0;
}

static void rs_close(struct tty_struct *tty, struct file * filp)
{
	spin_lock_bh(&timer_lock);
	if (tty->count == 1)
		del_timer_sync(&serial_timer);
	spin_unlock_bh(&timer_lock);
}


static int rs_write(struct tty_struct * tty,
		    const unsigned char *buf, int count)
{
	/* see drivers/char/serialX.c to reference original version */

	simc_write(1, buf, count);
	return count;
}

static void rs_poll(struct timer_list *unused)
{
	struct tty_port *port = &serial_port;
	int i = 0;
	int rd = 1;
	unsigned char c;

	spin_lock(&timer_lock);

	while (simc_poll(0)) {
		rd = simc_read(0, &c, 1);
		if (rd <= 0)
			break;
		tty_insert_flip_char(port, c, TTY_NORMAL);
		i++;
	}

	if (i)
		tty_flip_buffer_push(port);
	if (rd)
		mod_timer(&serial_timer, jiffies + SERIAL_TIMER_VALUE);
	spin_unlock(&timer_lock);
}


static int rs_put_char(struct tty_struct *tty, unsigned char ch)
{
	return rs_write(tty, &ch, 1);
}

static void rs_flush_chars(struct tty_struct *tty)
{
}

static unsigned int rs_write_room(struct tty_struct *tty)
{
	/* Let's say iss can always accept 2K characters.. */
	return 2 * 1024;
}

static void rs_hangup(struct tty_struct *tty)
{
	/* Stub, once again.. */
}

static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
{
	/* Stub, once again.. */
}

static int rs_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "serinfo:1.0 driver:0.1\n");
	return 0;
}

static const struct tty_operations serial_ops = {
	.open = rs_open,
	.close = rs_close,
	.write = rs_write,
	.put_char = rs_put_char,
	.flush_chars = rs_flush_chars,
	.write_room = rs_write_room,
	.hangup = rs_hangup,
	.wait_until_sent = rs_wait_until_sent,
	.proc_show = rs_proc_show,
};

static int __init rs_init(void)
{
	struct tty_driver *driver;
	int ret;

	driver = tty_alloc_driver(SERIAL_MAX_NUM_LINES, TTY_DRIVER_REAL_RAW);
	if (IS_ERR(driver))
		return PTR_ERR(driver);

	tty_port_init(&serial_port);

	/* Initialize the tty_driver structure */

	driver->driver_name = "iss_serial";
	driver->name = "ttyS";
	driver->major = TTY_MAJOR;
	driver->minor_start = 64;
	driver->type = TTY_DRIVER_TYPE_SERIAL;
	driver->subtype = SERIAL_TYPE_NORMAL;
	driver->init_termios = tty_std_termios;
	driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;

	tty_set_operations(driver, &serial_ops);
	tty_port_link_device(&serial_port, driver, 0);

	ret = tty_register_driver(driver);
	if (ret) {
		pr_err("Couldn't register serial driver\n");
		tty_driver_kref_put(driver);
		tty_port_destroy(&serial_port);

		return ret;
	}

	serial_driver = driver;

	return 0;
}


static __exit void rs_exit(void)
{
	tty_unregister_driver(serial_driver);
	tty_driver_kref_put(serial_driver);
	tty_port_destroy(&serial_port);
}


/* We use `late_initcall' instead of just `__initcall' as a workaround for
 * the fact that (1) simcons_tty_init can't be called before tty_init,
 * (2) tty_init is called via `module_init', (3) if statically linked,
 * module_init == device_init, and (4) there's no ordering of init lists.
 * We can do this easily because simcons is always statically linked, but
 * other tty drivers that depend on tty_init and which must use
 * `module_init' to declare their init routines are likely to be broken.
 */

late_initcall(rs_init);


#ifdef CONFIG_SERIAL_CONSOLE

static void iss_console_write(struct console *co, const char *s, unsigned count)
{
	int len = strlen(s);

	if (s != 0 && *s != 0)
		simc_write(1, s, count < len ? count : len);
}

static struct tty_driver* iss_console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}


static struct console sercons = {
	.name = "ttyS",
	.write = iss_console_write,
	.device = iss_console_device,
	.flags = CON_PRINTBUFFER,
	.index = -1
};

static int __init iss_console_init(void)
{
	register_console(&sercons);
	return 0;
}

console_initcall(iss_console_init);

#endif /* CONFIG_SERIAL_CONSOLE */


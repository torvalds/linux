/*
 * arch/v850/kernel/memcons.c -- Console I/O to a memory buffer
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/init.h>

/* If this device is enabled, the linker map should define start and
   end points for its buffer. */
extern char memcons_output[], memcons_output_end;

/* Current offset into the buffer.  */
static unsigned long memcons_offs = 0;

/* Spinlock protecting memcons_offs.  */
static DEFINE_SPINLOCK(memcons_lock);


static size_t write (const char *buf, size_t len)
{
	int flags;
	char *point;

	spin_lock_irqsave (memcons_lock, flags);

	point = memcons_output + memcons_offs;
	if (point + len >= &memcons_output_end) {
		len = &memcons_output_end - point;
		memcons_offs = 0;
	} else
		memcons_offs += len;

	spin_unlock_irqrestore (memcons_lock, flags);

	memcpy (point, buf, len);

	return len;
}


/*  Low-level console. */

static void memcons_write (struct console *co, const char *buf, unsigned len)
{
	while (len > 0)
		len -= write (buf, len);
}

static struct tty_driver *tty_driver;

static struct tty_driver *memcons_device (struct console *co, int *index)
{
	*index = co->index;
	return tty_driver;
}

static struct console memcons =
{
    .name	= "memcons",
    .write	= memcons_write,
    .device	= memcons_device,
    .flags	= CON_PRINTBUFFER,
    .index	= -1,
};

void memcons_setup (void)
{
	register_console (&memcons);
	printk (KERN_INFO "Console: static memory buffer (memcons)\n");
}

/* Higher level TTY interface.  */

int memcons_tty_open (struct tty_struct *tty, struct file *filp)
{
	return 0;
}

int memcons_tty_write (struct tty_struct *tty, const unsigned char *buf, int len)
{
	return write (buf, len);
}

int memcons_tty_write_room (struct tty_struct *tty)
{
	return &memcons_output_end - (memcons_output + memcons_offs);
}

int memcons_tty_chars_in_buffer (struct tty_struct *tty)
{
	/* We have no buffer.  */
	return 0;
}

static struct tty_operations ops = {
	.open = memcons_tty_open,
	.write = memcons_tty_write,
	.write_room = memcons_tty_write_room,
	.chars_in_buffer = memcons_tty_chars_in_buffer,
};

int __init memcons_tty_init (void)
{
	int err;
	struct tty_driver *driver = alloc_tty_driver(1);
	if (!driver)
		return -ENOMEM;

	driver->name = "memcons";
	driver->major = TTY_MAJOR;
	driver->minor_start = 64;
	driver->type = TTY_DRIVER_TYPE_SYSCONS;
	driver->init_termios = tty_std_termios;
	tty_set_operations(driver, &ops);
	err = tty_register_driver(driver);
	if (err) {
		put_tty_driver(driver);
		return err;
	}
	tty_driver = driver;
	return 0;
}
__initcall (memcons_tty_init);

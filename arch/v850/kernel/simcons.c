/*
 * arch/v850/kernel/simcons.c -- Console I/O for GDB v850e simulator
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
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
#include <linux/tty_flip.h>
#include <linux/tty_driver.h>
#include <linux/init.h>

#include <asm/poll.h>
#include <asm/string.h>
#include <asm/simsyscall.h>


/*  Low-level console. */

static void simcons_write (struct console *co, const char *buf, unsigned len)
{
	V850_SIM_SYSCALL (write, 1, buf, len);
}

static int simcons_read (struct console *co, char *buf, unsigned len)
{
	return V850_SIM_SYSCALL (read, 0, buf, len);
}

static struct tty_driver *tty_driver;
static struct tty_driver *simcons_device (struct console *c, int *index)
{
	*index = c->index;
	return tty_driver;
}

static struct console simcons =
{
    .name	= "simcons",
    .write	= simcons_write,
    .read	= simcons_read,
    .device	= simcons_device,
    .flags	= CON_PRINTBUFFER,
    .index	= -1,
};

/* Higher level TTY interface.  */

int simcons_tty_open (struct tty_struct *tty, struct file *filp)
{
	return 0;
}

int simcons_tty_write (struct tty_struct *tty,
		       const unsigned char *buf, int count)
{
	return V850_SIM_SYSCALL (write, 1, buf, count);
}

int simcons_tty_write_room (struct tty_struct *tty)
{
	/* Completely arbitrary.  */
	return 0x100000;
}

int simcons_tty_chars_in_buffer (struct tty_struct *tty)
{
	/* We have no buffer.  */
	return 0;
}

static struct tty_operations ops = {
	.open = simcons_tty_open,
	.write = simcons_tty_write,
	.write_room = simcons_tty_write_room,
	.chars_in_buffer = simcons_tty_chars_in_buffer,
};

int __init simcons_tty_init (void)
{
	struct tty_driver *driver = alloc_tty_driver(1);
	int err;
	if (!driver)
		return -ENOMEM;
	driver->name = "simcons";
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
/* We use `late_initcall' instead of just `__initcall' as a workaround for
   the fact that (1) simcons_tty_init can't be called before tty_init,
   (2) tty_init is called via `module_init', (3) if statically linked,
   module_init == device_init, and (4) there's no ordering of init lists.
   We can do this easily because simcons is always statically linked, but
   other tty drivers that depend on tty_init and which must use
   `module_init' to declare their init routines are likely to be broken.  */
late_initcall(simcons_tty_init);

/* Poll for input on the console, and if there's any, deliver it to the
   tty driver.  */
void simcons_poll_tty (struct tty_struct *tty)
{
	int flip = 0, send_break = 0;
	struct pollfd pfd;
	pfd.fd = 0;
	pfd.events = POLLIN;

	if (V850_SIM_SYSCALL (poll, &pfd, 1, 0) > 0) {
		if (pfd.revents & POLLIN) {
			int left = TTY_FLIPBUF_SIZE - tty->flip.count;

			if (left > 0) {
				unsigned char *buf = tty->flip.char_buf_ptr;
				int rd = V850_SIM_SYSCALL (read, 0, buf, left);

				if (rd > 0) {
					tty->flip.count += rd;
					tty->flip.char_buf_ptr += rd;
					memset (tty->flip.flag_buf_ptr, 0, rd);
					tty->flip.flag_buf_ptr += rd;
					flip = 1;
				} else
					send_break = 1;
			}
		} else if (pfd.revents & POLLERR)
			send_break = 1;
	}

	if (send_break) {
		tty_insert_flip_char (tty, 0, TTY_BREAK);		
		flip = 1;
	}

	if (flip)
		tty_schedule_flip (tty);
}

void simcons_poll_ttys (void)
{
	if (tty_driver && tty_driver->ttys[0])
		simcons_poll_tty (tty_driver->ttys[0]);
}

void simcons_setup (void)
{
	V850_SIM_SYSCALL (make_raw, 0);
	register_console (&simcons);
	printk (KERN_INFO "Console: GDB V850E simulator stdio\n");
}

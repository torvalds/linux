/* drivers/char/dcc_tty.c
 *
 * Copyright (C) 2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/hrtimer.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>

MODULE_DESCRIPTION("DCC TTY Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

static DEFINE_SPINLOCK(g_dcc_tty_lock);
static struct hrtimer g_dcc_timer;
static char g_dcc_buffer[16];
static int g_dcc_buffer_head;
static int g_dcc_buffer_count;
static unsigned g_dcc_write_delay_usecs = 1;
static struct tty_driver *g_dcc_tty_driver;
static struct tty_struct *g_dcc_tty;
static int g_dcc_tty_open_count;

static void dcc_poll_locked(void)
{
	char ch;
	int rch;
	int written;

	while (g_dcc_buffer_count) {
		ch = g_dcc_buffer[g_dcc_buffer_head];
		asm(
			"mrc 14, 0, r15, c0, c1, 0\n"
			"mcrcc 14, 0, %1, c0, c5, 0\n"
			"movcc %0, #1\n"
			"movcs %0, #0\n"
			: "=r" (written)
			: "r" (ch)
		);
		if (written) {
			if (ch == '\n')
				g_dcc_buffer[g_dcc_buffer_head] = '\r';
			else {
				g_dcc_buffer_head = (g_dcc_buffer_head + 1) % ARRAY_SIZE(g_dcc_buffer);
				g_dcc_buffer_count--;
				if (g_dcc_tty)
					tty_wakeup(g_dcc_tty);
			}
			g_dcc_write_delay_usecs = 1;
		} else {
			if (g_dcc_write_delay_usecs > 0x100)
				break;
			g_dcc_write_delay_usecs <<= 1;
			udelay(g_dcc_write_delay_usecs);
		}
	}

	if (g_dcc_tty && !test_bit(TTY_THROTTLED, &g_dcc_tty->flags)) {
		asm(
			"mrc 14, 0, %0, c0, c1, 0\n"
			"tst %0, #(1 << 30)\n"
			"moveq %0, #-1\n"
			"mrcne 14, 0, %0, c0, c5, 0\n"
			: "=r" (rch)
		);
		if (rch >= 0) {
			ch = rch;
			tty_insert_flip_string(g_dcc_tty, &ch, 1);
			tty_flip_buffer_push(g_dcc_tty);
		}
	}


	if (g_dcc_buffer_count)
		hrtimer_start(&g_dcc_timer, ktime_set(0, g_dcc_write_delay_usecs * NSEC_PER_USEC), HRTIMER_MODE_REL);
	else
		hrtimer_start(&g_dcc_timer, ktime_set(0, 20 * NSEC_PER_MSEC), HRTIMER_MODE_REL);
}

static int dcc_tty_open(struct tty_struct * tty, struct file * filp)
{
	int ret;
	unsigned long irq_flags;

	spin_lock_irqsave(&g_dcc_tty_lock, irq_flags);
	if (g_dcc_tty == NULL || g_dcc_tty == tty) {
		g_dcc_tty = tty;
		g_dcc_tty_open_count++;
		ret = 0;
	} else
		ret = -EBUSY;
	spin_unlock_irqrestore(&g_dcc_tty_lock, irq_flags);

	printk("dcc_tty_open, tty %p, f_flags %x, returned %d\n", tty, filp->f_flags, ret);

	return ret;
}

static void dcc_tty_close(struct tty_struct * tty, struct file * filp)
{
	printk("dcc_tty_close, tty %p, f_flags %x\n", tty, filp->f_flags);
	if (g_dcc_tty == tty) {
		if (--g_dcc_tty_open_count == 0)
			g_dcc_tty = NULL;
	}
}

static int dcc_write(const unsigned char *buf_start, int count)
{
	const unsigned char *buf = buf_start;
	unsigned long irq_flags;
	int copy_len;
	int space_left;
	int tail;

	if (count < 1)
		return 0;

	spin_lock_irqsave(&g_dcc_tty_lock, irq_flags);
	do {
		tail = (g_dcc_buffer_head + g_dcc_buffer_count) % ARRAY_SIZE(g_dcc_buffer);
		copy_len = ARRAY_SIZE(g_dcc_buffer) - tail;
		space_left = ARRAY_SIZE(g_dcc_buffer) - g_dcc_buffer_count;
		if (copy_len > space_left)
			copy_len = space_left;
		if (copy_len > count)
			copy_len = count;
		memcpy(&g_dcc_buffer[tail], buf, copy_len);
		g_dcc_buffer_count += copy_len;
		buf += copy_len;
		count -= copy_len;
		if (copy_len < count && copy_len < space_left) {
			space_left -= copy_len;
			copy_len = count;
			if (copy_len > space_left) {
				copy_len = space_left;
			}
			memcpy(g_dcc_buffer, buf, copy_len);
			buf += copy_len;
			count -= copy_len;
			g_dcc_buffer_count += copy_len;
		}
		dcc_poll_locked();
		space_left = ARRAY_SIZE(g_dcc_buffer) - g_dcc_buffer_count;
	} while(count && space_left);
	spin_unlock_irqrestore(&g_dcc_tty_lock, irq_flags);
	return buf - buf_start;
}

static int dcc_tty_write(struct tty_struct * tty, const unsigned char *buf, int count)
{
	int ret;
	/* printk("dcc_tty_write %p, %d\n", buf, count); */
	ret = dcc_write(buf, count);
	if (ret != count)
		printk("dcc_tty_write %p, %d, returned %d\n", buf, count, ret);
	return ret;
}

static int dcc_tty_write_room(struct tty_struct *tty)
{
	int space_left;
	unsigned long irq_flags;

	spin_lock_irqsave(&g_dcc_tty_lock, irq_flags);
	space_left = ARRAY_SIZE(g_dcc_buffer) - g_dcc_buffer_count;
	spin_unlock_irqrestore(&g_dcc_tty_lock, irq_flags);
	return space_left;
}

static int dcc_tty_chars_in_buffer(struct tty_struct *tty)
{
	int ret;
	asm(
		"mrc 14, 0, %0, c0, c1, 0\n"
		"mov %0, %0, LSR #30\n"
		"and %0, %0, #1\n"
		: "=r" (ret)
	);
	return ret;
}

static void dcc_tty_unthrottle(struct tty_struct * tty)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&g_dcc_tty_lock, irq_flags);
	dcc_poll_locked();
	spin_unlock_irqrestore(&g_dcc_tty_lock, irq_flags);
}

static enum hrtimer_restart dcc_tty_timer_func(struct hrtimer *timer)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&g_dcc_tty_lock, irq_flags);
	dcc_poll_locked();
	spin_unlock_irqrestore(&g_dcc_tty_lock, irq_flags);
	return HRTIMER_NORESTART;
}

void dcc_console_write(struct console *co, const char *b, unsigned count)
{
#if 1
	dcc_write(b, count);
#else
	/* blocking printk */
	while (count > 0) {
		int written;
		written = dcc_write(b, count);
		if (written) {
			b += written;
			count -= written;
		}
	}
#endif
}

static struct tty_driver *dcc_console_device(struct console *c, int *index)
{
	*index = 0;
	return g_dcc_tty_driver;
}

static int __init dcc_console_setup(struct console *co, char *options)
{
	if (co->index != 0)
		return -ENODEV;
	return 0;
}


static struct console dcc_console =
{
	.name		= "ttyDCC",
	.write		= dcc_console_write,
	.device		= dcc_console_device,
	.setup		= dcc_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

static struct tty_operations dcc_tty_ops = {
	.open = dcc_tty_open,
	.close = dcc_tty_close,
	.write = dcc_tty_write,
	.write_room = dcc_tty_write_room,
	.chars_in_buffer = dcc_tty_chars_in_buffer,
	.unthrottle = dcc_tty_unthrottle,
};

static int __init dcc_tty_init(void)
{
	int ret;

	hrtimer_init(&g_dcc_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	g_dcc_timer.function = dcc_tty_timer_func;

	g_dcc_tty_driver = alloc_tty_driver(1);
	if (!g_dcc_tty_driver) {
		printk(KERN_ERR "dcc_tty_probe: alloc_tty_driver failed\n");
		ret = -ENOMEM;
		goto err_alloc_tty_driver_failed;
	}
	g_dcc_tty_driver->owner = THIS_MODULE;
	g_dcc_tty_driver->driver_name = "dcc";
	g_dcc_tty_driver->name = "ttyDCC";
	g_dcc_tty_driver->major = 0; // auto assign
	g_dcc_tty_driver->minor_start = 0;
	g_dcc_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	g_dcc_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	g_dcc_tty_driver->init_termios = tty_std_termios;
	g_dcc_tty_driver->flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_set_operations(g_dcc_tty_driver, &dcc_tty_ops);
	ret = tty_register_driver(g_dcc_tty_driver);
	if (ret) {
		printk(KERN_ERR "dcc_tty_probe: tty_register_driver failed, %d\n", ret);
		goto err_tty_register_driver_failed;
	}
	tty_register_device(g_dcc_tty_driver, 0, NULL);

	register_console(&dcc_console);
	hrtimer_start(&g_dcc_timer, ktime_set(0, 0), HRTIMER_MODE_REL);

	return 0;

err_tty_register_driver_failed:
	put_tty_driver(g_dcc_tty_driver);
	g_dcc_tty_driver = NULL;
err_alloc_tty_driver_failed:
	return ret;
}

static void  __exit dcc_tty_exit(void)
{
	int ret;

	tty_unregister_device(g_dcc_tty_driver, 0);
	ret = tty_unregister_driver(g_dcc_tty_driver);
	if (ret < 0) {
		printk(KERN_ERR "dcc_tty_remove: tty_unregister_driver failed, %d\n", ret);
	} else {
		put_tty_driver(g_dcc_tty_driver);
	}
	g_dcc_tty_driver = NULL;
}

module_init(dcc_tty_init);
module_exit(dcc_tty_exit);



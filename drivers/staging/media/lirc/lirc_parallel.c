/*
 * lirc_parallel.c
 *
 * lirc_parallel - device driver for infra-red signal receiving and
 *                 transmitting unit built by the author
 *
 * Copyright (C) 1998 Christoph Bartelmus <lirc@bartelmus.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/*** Includes ***/

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <asm/div64.h>

#include <linux/poll.h>
#include <linux/parport.h>
#include <linux/platform_device.h>

#include <media/lirc.h>
#include <media/lirc_dev.h>

#include "lirc_parallel.h"

#define LIRC_DRIVER_NAME "lirc_parallel"

#ifndef LIRC_IRQ
#define LIRC_IRQ 7
#endif
#ifndef LIRC_PORT
#define LIRC_PORT 0x378
#endif
#ifndef LIRC_TIMER
#define LIRC_TIMER 65536
#endif

/*** Global Variables ***/

static bool debug;
static bool check_pselecd;

unsigned int irq = LIRC_IRQ;
unsigned int io = LIRC_PORT;
#ifdef LIRC_TIMER
unsigned int timer;
unsigned int default_timer = LIRC_TIMER;
#endif

#define RBUF_SIZE (256) /* this must be a power of 2 larger than 1 */

static int rbuf[RBUF_SIZE];

DECLARE_WAIT_QUEUE_HEAD(lirc_wait);

unsigned int rptr;
unsigned int wptr;
unsigned int lost_irqs;
int is_open;

struct parport *pport;
struct pardevice *ppdevice;
int is_claimed;

unsigned int tx_mask = 1;

/*** Internal Functions ***/

static unsigned int in(int offset)
{
	switch (offset) {
	case LIRC_LP_BASE:
		return parport_read_data(pport);
	case LIRC_LP_STATUS:
		return parport_read_status(pport);
	case LIRC_LP_CONTROL:
		return parport_read_control(pport);
	}
	return 0; /* make compiler happy */
}

static void out(int offset, int value)
{
	switch (offset) {
	case LIRC_LP_BASE:
		parport_write_data(pport, value);
		break;
	case LIRC_LP_CONTROL:
		parport_write_control(pport, value);
		break;
	case LIRC_LP_STATUS:
		pr_info("attempt to write to status register\n");
		break;
	}
}

static unsigned int lirc_get_timer(void)
{
	return in(LIRC_PORT_TIMER) & LIRC_PORT_TIMER_BIT;
}

static unsigned int lirc_get_signal(void)
{
	return in(LIRC_PORT_SIGNAL) & LIRC_PORT_SIGNAL_BIT;
}

static void lirc_on(void)
{
	out(LIRC_PORT_DATA, tx_mask);
}

static void lirc_off(void)
{
	out(LIRC_PORT_DATA, 0);
}

static unsigned int init_lirc_timer(void)
{
	struct timeval tv, now;
	unsigned int level, newlevel, timeelapsed, newtimer;
	int count = 0;

	do_gettimeofday(&tv);
	tv.tv_sec++;                     /* wait max. 1 sec. */
	level = lirc_get_timer();
	do {
		newlevel = lirc_get_timer();
		if (level == 0 && newlevel != 0)
			count++;
		level = newlevel;
		do_gettimeofday(&now);
	} while (count < 1000 && (now.tv_sec < tv.tv_sec
			     || (now.tv_sec == tv.tv_sec
				 && now.tv_usec < tv.tv_usec)));

	timeelapsed = ((now.tv_sec + 1 - tv.tv_sec)*1000000
		     + (now.tv_usec - tv.tv_usec));
	if (count >= 1000 && timeelapsed > 0) {
		if (default_timer == 0) {
			/* autodetect timer */
			newtimer = (1000000*count)/timeelapsed;
			pr_info("%u Hz timer detected\n", newtimer);
			return newtimer;
		}  else {
			newtimer = (1000000*count)/timeelapsed;
			if (abs(newtimer - default_timer) > default_timer/10) {
				/* bad timer */
				pr_notice("bad timer: %u Hz\n", newtimer);
				pr_notice("using default timer: %u Hz\n",
					  default_timer);
				return default_timer;
			} else {
				pr_info("%u Hz timer detected\n", newtimer);
				return newtimer; /* use detected value */
			}
		}
	} else {
		pr_notice("no timer detected\n");
		return 0;
	}
}

static int lirc_claim(void)
{
	if (parport_claim(ppdevice) != 0) {
		pr_warn("could not claim port\n");
		pr_warn("waiting for port becoming available\n");
		if (parport_claim_or_block(ppdevice) < 0) {
			pr_notice("could not claim port, giving up\n");
			return 0;
		}
	}
	out(LIRC_LP_CONTROL, LP_PSELECP|LP_PINITP);
	is_claimed = 1;
	return 1;
}

/*** interrupt handler ***/

static void rbuf_write(int signal)
{
	unsigned int nwptr;

	nwptr = (wptr + 1) & (RBUF_SIZE - 1);
	if (nwptr == rptr) {
		/* no new signals will be accepted */
		lost_irqs++;
		pr_notice("buffer overrun\n");
		return;
	}
	rbuf[wptr] = signal;
	wptr = nwptr;
}

static void irq_handler(void *blah)
{
	struct timeval tv;
	static struct timeval lasttv;
	static int init;
	long signal;
	int data;
	unsigned int level, newlevel;
	unsigned int timeout;

	if (!is_open)
		return;

	if (!is_claimed)
		return;

#if 0
	/* disable interrupt */
	  disable_irq(irq);
	  out(LIRC_PORT_IRQ, in(LIRC_PORT_IRQ) & (~LP_PINTEN));
#endif
	if (check_pselecd && (in(1) & LP_PSELECD))
		return;

#ifdef LIRC_TIMER
	if (init) {
		do_gettimeofday(&tv);

		signal = tv.tv_sec - lasttv.tv_sec;
		if (signal > 15)
			/* really long time */
			data = PULSE_MASK;
		else
			data = (int) (signal*1000000 +
					 tv.tv_usec - lasttv.tv_usec +
					 LIRC_SFH506_DELAY);

		rbuf_write(data); /* space */
	} else {
		if (timer == 0) {
			/*
			 * wake up; we'll lose this signal, but it will be
			 * garbage if the device is turned on anyway
			 */
			timer = init_lirc_timer();
			/* enable_irq(irq); */
			return;
		}
		init = 1;
	}

	timeout = timer/10;	/* timeout after 1/10 sec. */
	signal = 1;
	level = lirc_get_timer();
	do {
		newlevel = lirc_get_timer();
		if (level == 0 && newlevel != 0)
			signal++;
		level = newlevel;

		/* giving up */
		if (signal > timeout
		    || (check_pselecd && (in(1) & LP_PSELECD))) {
			signal = 0;
			pr_notice("timeout\n");
			break;
		}
	} while (lirc_get_signal());

	if (signal != 0) {
		/* adjust value to usecs */
		__u64 helper;

		helper = ((__u64) signal)*1000000;
		do_div(helper, timer);
		signal = (long) helper;

		if (signal > LIRC_SFH506_DELAY)
			data = signal - LIRC_SFH506_DELAY;
		else
			data = 1;
		rbuf_write(PULSE_BIT|data); /* pulse */
	}
	do_gettimeofday(&lasttv);
#else
	/* add your code here */
#endif

	wake_up_interruptible(&lirc_wait);

	/* enable interrupt */
	/*
	  enable_irq(irq);
	  out(LIRC_PORT_IRQ, in(LIRC_PORT_IRQ)|LP_PINTEN);
	*/
}

/*** file operations ***/

static loff_t lirc_lseek(struct file *filep, loff_t offset, int orig)
{
	return -ESPIPE;
}

static ssize_t lirc_read(struct file *filep, char *buf, size_t n, loff_t *ppos)
{
	int result = 0;
	int count = 0;
	DECLARE_WAITQUEUE(wait, current);

	if (n % sizeof(int))
		return -EINVAL;

	add_wait_queue(&lirc_wait, &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	while (count < n) {
		if (rptr != wptr) {
			if (copy_to_user(buf+count, (char *) &rbuf[rptr],
					 sizeof(int))) {
				result = -EFAULT;
				break;
			}
			rptr = (rptr + 1) & (RBUF_SIZE - 1);
			count += sizeof(int);
		} else {
			if (filep->f_flags & O_NONBLOCK) {
				result = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				result = -ERESTARTSYS;
				break;
			}
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
	}
	remove_wait_queue(&lirc_wait, &wait);
	set_current_state(TASK_RUNNING);
	return count ? count : result;
}

static ssize_t lirc_write(struct file *filep, const char *buf, size_t n,
			  loff_t *ppos)
{
	int count;
	unsigned int i;
	unsigned int level, newlevel;
	unsigned long flags;
	int counttimer;
	int *wbuf;
	ssize_t ret;

	if (!is_claimed)
		return -EBUSY;

	count = n / sizeof(int);

	if (n % sizeof(int) || count % 2 == 0)
		return -EINVAL;

	wbuf = memdup_user(buf, n);
	if (IS_ERR(wbuf))
		return PTR_ERR(wbuf);

#ifdef LIRC_TIMER
	if (timer == 0) {
		/* try again if device is ready */
		timer = init_lirc_timer();
		if (timer == 0) {
			ret = -EIO;
			goto out;
		}
	}

	/* adjust values from usecs */
	for (i = 0; i < count; i++) {
		__u64 helper;

		helper = ((__u64) wbuf[i])*timer;
		do_div(helper, 1000000);
		wbuf[i] = (int) helper;
	}

	local_irq_save(flags);
	i = 0;
	while (i < count) {
		level = lirc_get_timer();
		counttimer = 0;
		lirc_on();
		do {
			newlevel = lirc_get_timer();
			if (level == 0 && newlevel != 0)
				counttimer++;
			level = newlevel;
			if (check_pselecd && (in(1) & LP_PSELECD)) {
				lirc_off();
				local_irq_restore(flags);
				ret = -EIO;
				goto out;
			}
		} while (counttimer < wbuf[i]);
		i++;

		lirc_off();
		if (i == count)
			break;
		counttimer = 0;
		do {
			newlevel = lirc_get_timer();
			if (level == 0 && newlevel != 0)
				counttimer++;
			level = newlevel;
			if (check_pselecd && (in(1) & LP_PSELECD)) {
				local_irq_restore(flags);
				ret = -EIO;
				goto out;
			}
		} while (counttimer < wbuf[i]);
		i++;
	}
	local_irq_restore(flags);
#else
	/* place code that handles write without external timer here */
#endif
	ret = n;
out:
	kfree(wbuf);

	return ret;
}

static unsigned int lirc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &lirc_wait, wait);
	if (rptr != wptr)
		return POLLIN | POLLRDNORM;
	return 0;
}

static long lirc_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int result;
	__u32 features = LIRC_CAN_SET_TRANSMITTER_MASK |
			 LIRC_CAN_SEND_PULSE | LIRC_CAN_REC_MODE2;
	__u32 mode;
	__u32 value;

	switch (cmd) {
	case LIRC_GET_FEATURES:
		result = put_user(features, (__u32 *) arg);
		if (result)
			return result;
		break;
	case LIRC_GET_SEND_MODE:
		result = put_user(LIRC_MODE_PULSE, (__u32 *) arg);
		if (result)
			return result;
		break;
	case LIRC_GET_REC_MODE:
		result = put_user(LIRC_MODE_MODE2, (__u32 *) arg);
		if (result)
			return result;
		break;
	case LIRC_SET_SEND_MODE:
		result = get_user(mode, (__u32 *) arg);
		if (result)
			return result;
		if (mode != LIRC_MODE_PULSE)
			return -EINVAL;
		break;
	case LIRC_SET_REC_MODE:
		result = get_user(mode, (__u32 *) arg);
		if (result)
			return result;
		if (mode != LIRC_MODE_MODE2)
			return -ENOSYS;
		break;
	case LIRC_SET_TRANSMITTER_MASK:
		result = get_user(value, (__u32 *) arg);
		if (result)
			return result;
		if ((value & LIRC_PARALLEL_TRANSMITTER_MASK) != value)
			return LIRC_PARALLEL_MAX_TRANSMITTERS;
		tx_mask = value;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static int lirc_open(struct inode *node, struct file *filep)
{
	if (is_open || !lirc_claim())
		return -EBUSY;

	parport_enable_irq(pport);

	/* init read ptr */
	rptr = 0;
	wptr = 0;
	lost_irqs = 0;

	is_open = 1;
	return 0;
}

static int lirc_close(struct inode *node, struct file *filep)
{
	if (is_claimed) {
		is_claimed = 0;
		parport_release(ppdevice);
	}
	is_open = 0;
	return 0;
}

static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.llseek		= lirc_lseek,
	.read		= lirc_read,
	.write		= lirc_write,
	.poll		= lirc_poll,
	.unlocked_ioctl	= lirc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= lirc_ioctl,
#endif
	.open		= lirc_open,
	.release	= lirc_close
};

static int set_use_inc(void *data)
{
	return 0;
}

static void set_use_dec(void *data)
{
}

static struct lirc_driver driver = {
	.name		= LIRC_DRIVER_NAME,
	.minor		= -1,
	.code_length	= 1,
	.sample_rate	= 0,
	.data		= NULL,
	.add_to_buf	= NULL,
	.set_use_inc	= set_use_inc,
	.set_use_dec	= set_use_dec,
	.fops		= &lirc_fops,
	.dev		= NULL,
	.owner		= THIS_MODULE,
};

static struct platform_device *lirc_parallel_dev;

static int lirc_parallel_probe(struct platform_device *dev)
{
	return 0;
}

static int lirc_parallel_remove(struct platform_device *dev)
{
	return 0;
}

static int lirc_parallel_suspend(struct platform_device *dev,
					pm_message_t state)
{
	return 0;
}

static int lirc_parallel_resume(struct platform_device *dev)
{
	return 0;
}

static struct platform_driver lirc_parallel_driver = {
	.probe	= lirc_parallel_probe,
	.remove	= lirc_parallel_remove,
	.suspend	= lirc_parallel_suspend,
	.resume	= lirc_parallel_resume,
	.driver	= {
		.name	= LIRC_DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

static int pf(void *handle)
{
	parport_disable_irq(pport);
	is_claimed = 0;
	return 0;
}

static void kf(void *handle)
{
	if (!is_open)
		return;
	if (!lirc_claim())
		return;
	parport_enable_irq(pport);
	lirc_off();
	/* this is a bit annoying when you actually print...*/
	/*
	printk(KERN_INFO "%s: reclaimed port\n", LIRC_DRIVER_NAME);
	*/
}

/*** module initialization and cleanup ***/

static int __init lirc_parallel_init(void)
{
	int result;

	result = platform_driver_register(&lirc_parallel_driver);
	if (result) {
		pr_notice("platform_driver_register returned %d\n", result);
		return result;
	}

	lirc_parallel_dev = platform_device_alloc(LIRC_DRIVER_NAME, 0);
	if (!lirc_parallel_dev) {
		result = -ENOMEM;
		goto exit_driver_unregister;
	}

	result = platform_device_add(lirc_parallel_dev);
	if (result)
		goto exit_device_put;

	pport = parport_find_base(io);
	if (pport == NULL) {
		pr_notice("no port at %x found\n", io);
		result = -ENXIO;
		goto exit_device_put;
	}
	ppdevice = parport_register_device(pport, LIRC_DRIVER_NAME,
					   pf, kf, irq_handler, 0, NULL);
	parport_put_port(pport);
	if (ppdevice == NULL) {
		pr_notice("parport_register_device() failed\n");
		result = -ENXIO;
		goto exit_device_put;
	}
	if (parport_claim(ppdevice) != 0)
		goto skip_init;
	is_claimed = 1;
	out(LIRC_LP_CONTROL, LP_PSELECP|LP_PINITP);

#ifdef LIRC_TIMER
	if (debug)
		out(LIRC_PORT_DATA, tx_mask);

	timer = init_lirc_timer();

#if 0	/* continue even if device is offline */
	if (timer == 0) {
		is_claimed = 0;
		parport_release(pport);
		parport_unregister_device(ppdevice);
		result = -EIO;
		goto exit_device_put;
	}

#endif
	if (debug)
		out(LIRC_PORT_DATA, 0);
#endif

	is_claimed = 0;
	parport_release(ppdevice);
 skip_init:
	driver.dev = &lirc_parallel_dev->dev;
	driver.minor = lirc_register_driver(&driver);
	if (driver.minor < 0) {
		pr_notice("register_chrdev() failed\n");
		parport_unregister_device(ppdevice);
		result = -EIO;
		goto exit_device_put;
	}
	pr_info("installed using port 0x%04x irq %d\n", io, irq);
	return 0;

exit_device_put:
	platform_device_put(lirc_parallel_dev);
exit_driver_unregister:
	platform_driver_unregister(&lirc_parallel_driver);
	return result;
}

static void __exit lirc_parallel_exit(void)
{
	parport_unregister_device(ppdevice);
	lirc_unregister_driver(driver.minor);

	platform_device_unregister(lirc_parallel_dev);
	platform_driver_unregister(&lirc_parallel_driver);
}

module_init(lirc_parallel_init);
module_exit(lirc_parallel_exit);

MODULE_DESCRIPTION("Infrared receiver driver for parallel ports.");
MODULE_AUTHOR("Christoph Bartelmus");
MODULE_LICENSE("GPL");

module_param(io, int, S_IRUGO);
MODULE_PARM_DESC(io, "I/O address base (0x3bc, 0x378 or 0x278)");

module_param(irq, int, S_IRUGO);
MODULE_PARM_DESC(irq, "Interrupt (7 or 5)");

module_param(tx_mask, int, S_IRUGO);
MODULE_PARM_DESC(tx_maxk, "Transmitter mask (default: 0x01)");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging messages");

module_param(check_pselecd, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(check_pselecd, "Check for printer (default: 0)");

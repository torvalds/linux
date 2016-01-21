/*
 *  n_tracesink.c - Trace data router and sink path through tty space.
 *
 *  Copyright (C) Intel 2011
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * The trace sink uses the Linux line discipline framework to receive
 * trace data coming from the PTI source line discipline driver
 * to a user-desired tty port, like USB.
 * This is to provide a way to extract modem trace data on
 * devices that do not have a PTI HW module, or just need modem
 * trace data to come out of a different HW output port.
 * This is part of a solution for the P1149.7, compact JTAG, standard.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/bug.h>
#include "n_tracesink.h"

/*
 * Other ldisc drivers use 65536 which basically means,
 * 'I can always accept 64k' and flow control is off.
 * This number is deemed appropriate for this driver.
 */
#define RECEIVE_ROOM	65536
#define DRIVERNAME	"n_tracesink"

/*
 * there is a quirk with this ldisc is he can write data
 * to a tty from anyone calling his kernel API, which
 * meets customer requirements in the drivers/misc/pti.c
 * project.  So he needs to know when he can and cannot write when
 * the API is called. In theory, the API can be called
 * after an init() but before a successful open() which
 * would crash the system if tty is not checked.
 */
static struct tty_struct *this_tty;
static DEFINE_MUTEX(writelock);

/**
 * n_tracesink_open() - Called when a tty is opened by a SW entity.
 * @tty: terminal device to the ldisc.
 *
 * Return:
 *      0 for success,
 *      -EFAULT = couldn't get a tty kref n_tracesink will sit
 *       on top of
 *      -EEXIST = open() called successfully once and it cannot
 *      be called again.
 *
 * Caveats: open() should only be successful the first time a
 * SW entity calls it.
 */
static int n_tracesink_open(struct tty_struct *tty)
{
	int retval = -EEXIST;

	mutex_lock(&writelock);
	if (this_tty == NULL) {
		this_tty = tty_kref_get(tty);
		if (this_tty == NULL) {
			retval = -EFAULT;
		} else {
			tty->disc_data = this_tty;
			tty_driver_flush_buffer(tty);
			retval = 0;
		}
	}
	mutex_unlock(&writelock);

	return retval;
}

/**
 * n_tracesink_close() - close connection
 * @tty: terminal device to the ldisc.
 *
 * Called when a software entity wants to close a connection.
 */
static void n_tracesink_close(struct tty_struct *tty)
{
	mutex_lock(&writelock);
	tty_driver_flush_buffer(tty);
	tty_kref_put(this_tty);
	this_tty = NULL;
	tty->disc_data = NULL;
	mutex_unlock(&writelock);
}

/**
 * n_tracesink_read() - read request from user space
 * @tty:  terminal device passed into the ldisc.
 * @file: pointer to open file object.
 * @buf:  pointer to the data buffer that gets eventually returned.
 * @nr:   number of bytes of the data buffer that is returned.
 *
 * function that allows read() functionality in userspace. By default if this
 * is not implemented it returns -EIO. This module is functioning like a
 * router via n_tracesink_receivebuf(), and there is no real requirement
 * to implement this function. However, an error return value other than
 * -EIO should be used just to show that there was an intent not to have
 * this function implemented.  Return value based on read() man pages.
 *
 * Return:
 *	 -EINVAL
 */
static ssize_t n_tracesink_read(struct tty_struct *tty, struct file *file,
				unsigned char __user *buf, size_t nr) {
	return -EINVAL;
}

/**
 * n_tracesink_write() - Function that allows write() in userspace.
 * @tty:  terminal device passed into the ldisc.
 * @file: pointer to open file object.
 * @buf:  pointer to the data buffer that gets eventually returned.
 * @nr:   number of bytes of the data buffer that is returned.
 *
 * By default if this is not implemented, it returns -EIO.
 * This should not be implemented, ever, because
 * 1. this driver is functioning like a router via
 *    n_tracesink_receivebuf()
 * 2. No writes to HW will ever go through this line discpline driver.
 * However, an error return value other than -EIO should be used
 * just to show that there was an intent not to have this function
 * implemented.  Return value based on write() man pages.
 *
 * Return:
 *	-EINVAL
 */
static ssize_t n_tracesink_write(struct tty_struct *tty, struct file *file,
				 const unsigned char *buf, size_t nr) {
	return -EINVAL;
}

/**
 * n_tracesink_datadrain() - Kernel API function used to route
 *			     trace debugging data to user-defined
 *			     port like USB.
 *
 * @buf:   Trace debuging data buffer to write to tty target
 *         port. Null value will return with no write occurring.
 * @count: Size of buf. Value of 0 or a negative number will
 *         return with no write occuring.
 *
 * Caveat: If this line discipline does not set the tty it sits
 * on top of via an open() call, this API function will not
 * call the tty's write() call because it will have no pointer
 * to call the write().
 */
void n_tracesink_datadrain(u8 *buf, int count)
{
	mutex_lock(&writelock);

	if ((buf != NULL) && (count > 0) && (this_tty != NULL))
		this_tty->ops->write(this_tty, buf, count);

	mutex_unlock(&writelock);
}
EXPORT_SYMBOL_GPL(n_tracesink_datadrain);

/*
 * Flush buffer is not impelemented as the ldisc has no internal buffering
 * so the tty_driver_flush_buffer() is sufficient for this driver's needs.
 */

/*
 * tty_ldisc function operations for this driver.
 */
static struct tty_ldisc_ops tty_n_tracesink = {
	.owner		= THIS_MODULE,
	.magic		= TTY_LDISC_MAGIC,
	.name		= DRIVERNAME,
	.open		= n_tracesink_open,
	.close		= n_tracesink_close,
	.read		= n_tracesink_read,
	.write		= n_tracesink_write
};

/**
 * n_tracesink_init-	module initialisation
 *
 * Registers this module as a line discipline driver.
 *
 * Return:
 *	0 for success, any other value error.
 */
static int __init n_tracesink_init(void)
{
	/* Note N_TRACESINK is defined in linux/tty.h */
	int retval = tty_register_ldisc(N_TRACESINK, &tty_n_tracesink);

	if (retval < 0)
		pr_err("%s: Registration failed: %d\n", __func__, retval);

	return retval;
}

/**
 * n_tracesink_exit -	module unload
 *
 * Removes this module as a line discipline driver.
 */
static void __exit n_tracesink_exit(void)
{
	int retval = tty_unregister_ldisc(N_TRACESINK);

	if (retval < 0)
		pr_err("%s: Unregistration failed: %d\n", __func__,  retval);
}

module_init(n_tracesink_init);
module_exit(n_tracesink_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jay Freyensee");
MODULE_ALIAS_LDISC(N_TRACESINK);
MODULE_DESCRIPTION("Trace sink ldisc driver");

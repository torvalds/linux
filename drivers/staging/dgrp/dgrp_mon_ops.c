/*****************************************************************************
 *
 * Copyright 1999 Digi International (www.digi.com)
 *     James Puzzo <jamesp at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 */

/*
 *
 *  Filename:
 *
 *     dgrp_mon_ops.c
 *
 *  Description:
 *
 *     Handle the file operations required for the "monitor" devices.
 *     Includes those functions required to register the "mon" devices
 *     in "/proc".
 *
 *  Author:
 *
 *     James A. Puzzo
 *
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <asm/unaligned.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "dgrp_common.h"

/* File operation declarations */
static int dgrp_mon_open(struct inode *, struct file *);
static int dgrp_mon_release(struct inode *, struct file *);
static ssize_t dgrp_mon_read(struct file *, char __user *, size_t, loff_t *);
static long dgrp_mon_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg);

const struct file_operations dgrp_mon_ops = {
	.owner   = THIS_MODULE,
	.read    = dgrp_mon_read,
	.unlocked_ioctl = dgrp_mon_ioctl,
	.open    = dgrp_mon_open,
	.release = dgrp_mon_release,
};

/**
 * dgrp_mon_open() -- open /proc/dgrp/ports device for a PortServer
 * @inode: struct inode *
 * @file: struct file *
 *
 * Open function to open the /proc/dgrp/ports device for a PortServer.
 */
static int dgrp_mon_open(struct inode *inode, struct file *file)
{
	struct nd_struct *nd;
	struct timeval tv;
	uint32_t time;
	u8 *buf;
	int rtn;

	rtn = try_module_get(THIS_MODULE);
	if (!rtn)
		return -ENXIO;

	rtn = 0;

	if (!capable(CAP_SYS_ADMIN)) {
		rtn = -EPERM;
		goto done;
	}

	/*
	 *  Make sure that the "private_data" field hasn't already been used.
	 */
	if (file->private_data) {
		rtn = -EINVAL;
		goto done;
	}

	/*
	 *  Get the node pointer, and fail if it doesn't exist.
	 */
	nd = PDE_DATA(inode);
	if (!nd) {
		rtn = -ENXIO;
		goto done;
	}

	file->private_data = (void *) nd;

	/*
	 * Allocate the monitor buffer.
	 */

	/*
	 *  Grab the MON lock.
	 */
	down(&nd->nd_mon_semaphore);

	if (nd->nd_mon_buf) {
		rtn = -EBUSY;
		goto done_up;
	}

	nd->nd_mon_buf = kmalloc(MON_MAX, GFP_KERNEL);

	if (!nd->nd_mon_buf) {
		rtn = -ENOMEM;
		goto done_up;
	}

	/*
	 *  Enter an RPDUMP file header into the buffer.
	 */

	buf = nd->nd_mon_buf;

	strcpy(buf, RPDUMP_MAGIC);
	buf += strlen(buf) + 1;

	do_gettimeofday(&tv);

	/*
	 *  tv.tv_sec might be a 64 bit quantity.  Pare
	 *  it down to 32 bits before attempting to encode
	 *  it.
	 */
	time = (uint32_t) (tv.tv_sec & 0xffffffff);

	put_unaligned_be32(time, buf);
	put_unaligned_be16(0, buf + 4);
	buf += 6;

	if (nd->nd_tx_module) {
		buf[0] = RPDUMP_CLIENT;
		put_unaligned_be32(0, buf + 1);
		put_unaligned_be16(1, buf + 5);
		buf[7] = 0xf0 + nd->nd_tx_module;
		buf += 8;
	}

	if (nd->nd_rx_module) {
		buf[0] = RPDUMP_SERVER;
		put_unaligned_be32(0, buf + 1);
		put_unaligned_be16(1, buf + 5);
		buf[7] = 0xf0 + nd->nd_rx_module;
		buf += 8;
	}

	nd->nd_mon_out = 0;
	nd->nd_mon_in  = buf - nd->nd_mon_buf;
	nd->nd_mon_lbolt = jiffies;

done_up:
	up(&nd->nd_mon_semaphore);

done:
	if (rtn)
		module_put(THIS_MODULE);
	return rtn;
}


/**
 * dgrp_mon_release() - Close the MON device for a particular PortServer
 * @inode: struct inode *
 * @file: struct file *
 */
static int dgrp_mon_release(struct inode *inode, struct file *file)
{
	struct nd_struct *nd;

	/*
	 *  Get the node pointer, and quit if it doesn't exist.
	 */
	nd = (struct nd_struct *)(file->private_data);
	if (!nd)
		goto done;

	/*
	 *  Free the monitor buffer.
	 */

	down(&nd->nd_mon_semaphore);

	kfree(nd->nd_mon_buf);
	nd->nd_mon_buf = NULL;
	nd->nd_mon_out = nd->nd_mon_in;

	/*
	 *  Wakeup any thread waiting for buffer space.
	 */

	if (nd->nd_mon_flag & MON_WAIT_SPACE) {
		nd->nd_mon_flag &= ~MON_WAIT_SPACE;
		wake_up_interruptible(&nd->nd_mon_wqueue);
	}

	up(&nd->nd_mon_semaphore);

	/*
	 *  Make sure there is no thread in the middle of writing a packet.
	 */
	down(&nd->nd_net_semaphore);
	up(&nd->nd_net_semaphore);

done:
	module_put(THIS_MODULE);
	file->private_data = NULL;
	return 0;
}

/**
 * dgrp_mon_read() -- Copy data from the monitoring buffer to the user
 */
static ssize_t dgrp_mon_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	struct nd_struct *nd;
	int r;
	int offset = 0;
	int res = 0;
	ssize_t rtn;

	/*
	 *  Get the node pointer, and quit if it doesn't exist.
	 */
	nd = (struct nd_struct *)(file->private_data);
	if (!nd)
		return -ENXIO;

	/*
	 *  Wait for some data to appear in the buffer.
	 */

	down(&nd->nd_mon_semaphore);

	for (;;) {
		res = (nd->nd_mon_in - nd->nd_mon_out) & MON_MASK;

		if (res)
			break;

		nd->nd_mon_flag |= MON_WAIT_DATA;

		up(&nd->nd_mon_semaphore);

		/*
		 * Go to sleep waiting until the condition becomes true.
		 */
		rtn = wait_event_interruptible(nd->nd_mon_wqueue,
					       ((nd->nd_mon_flag & MON_WAIT_DATA) == 0));

		if (rtn)
			return rtn;

		down(&nd->nd_mon_semaphore);
	}

	/*
	 *  Read whatever is there.
	 */

	if (res > count)
		res = count;

	r = MON_MAX - nd->nd_mon_out;

	if (r <= res) {
		rtn = copy_to_user((void __user *)buf,
				   nd->nd_mon_buf + nd->nd_mon_out, r);
		if (rtn) {
			up(&nd->nd_mon_semaphore);
			return -EFAULT;
		}

		nd->nd_mon_out = 0;
		res -= r;
		offset = r;
	}

	rtn = copy_to_user((void __user *) buf + offset,
			   nd->nd_mon_buf + nd->nd_mon_out, res);
	if (rtn) {
		up(&nd->nd_mon_semaphore);
		return -EFAULT;
	}

	nd->nd_mon_out += res;

	*ppos += res;

	up(&nd->nd_mon_semaphore);

	/*
	 *  Wakeup any thread waiting for buffer space.
	 */

	if (nd->nd_mon_flag & MON_WAIT_SPACE) {
		nd->nd_mon_flag &= ~MON_WAIT_SPACE;
		wake_up_interruptible(&nd->nd_mon_wqueue);
	}

	return res;
}

/*  ioctl is not valid on monitor device */
static long dgrp_mon_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	return -EINVAL;
}

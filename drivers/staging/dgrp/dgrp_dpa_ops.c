/*
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
 *     dgrp_dpa_ops.c
 *
 *  Description:
 *
 *     Handle the file operations required for the "dpa" devices.
 *     Includes those functions required to register the "dpa" devices
 *     in "/proc".
 *
 *  Author:
 *
 *     James A. Puzzo
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/tty.h>
#include <linux/poll.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/ratelimit.h>
#include <asm/unaligned.h>

#include "dgrp_common.h"

/* File operation declarations */
static int dgrp_dpa_open(struct inode *, struct file *);
static int dgrp_dpa_release(struct inode *, struct file *);
static ssize_t dgrp_dpa_read(struct file *, char __user *, size_t, loff_t *);
static long dgrp_dpa_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg);
static unsigned int dgrp_dpa_select(struct file *, struct poll_table_struct *);

static const struct file_operations dpa_ops = {
	.owner   =  THIS_MODULE,
	.read    =  dgrp_dpa_read,
	.poll    =  dgrp_dpa_select,
	.unlocked_ioctl =  dgrp_dpa_ioctl,
	.open    =  dgrp_dpa_open,
	.release =  dgrp_dpa_release,
};

static struct inode_operations dpa_inode_ops = {
	.permission = dgrp_inode_permission
};



struct digi_node {
	uint	nd_state;		/* Node state: 1 = up, 0 = down. */
	uint	nd_chan_count;		/* Number of channels found */
	uint	nd_tx_byte;		/* Tx data count */
	uint	nd_rx_byte;		/* RX data count */
	u8	nd_ps_desc[MAX_DESC_LEN]; /* Description from PS */
};

#define DIGI_GETNODE      (('d'<<8) | 249)	/* get board info */


struct digi_chan {
	uint	ch_port;	/* Port number to get info on */
	uint	ch_open;	/* 1 if open, 0 if not */
	uint	ch_txcount;	/* TX data count  */
	uint	ch_rxcount;	/* RX data count  */
	uint	ch_s_brate;	/* Realport BRATE */
	uint	ch_s_estat;	/* Realport ELAST */
	uint	ch_s_cflag;	/* Realport CFLAG */
	uint	ch_s_iflag;	/* Realport IFLAG */
	uint	ch_s_oflag;	/* Realport OFLAG */
	uint	ch_s_xflag;	/* Realport XFLAG */
	uint	ch_s_mstat;	/* Realport MLAST */
};

#define DIGI_GETCHAN      (('d'<<8) | 248)	/* get channel info */


struct digi_vpd {
	int vpd_len;
	char vpd_data[VPDSIZE];
};

#define DIGI_GETVPD       (('d'<<8) | 246)	/* get VPD info */


struct digi_debug {
	int onoff;
	int port;
};

#define DIGI_SETDEBUG      (('d'<<8) | 247)	/* set debug info */


void dgrp_register_dpa_hook(struct proc_dir_entry *de)
{
	struct nd_struct *node = de->data;

	de->proc_iops = &dpa_inode_ops;
	rcu_assign_pointer(de->proc_fops, &dpa_ops);

	node->nd_dpa_de = de;
	spin_lock_init(&node->nd_dpa_lock);
}

/*
 * dgrp_dpa_open -- open the DPA device for a particular PortServer
 */
static int dgrp_dpa_open(struct inode *inode, struct file *file)
{
	struct nd_struct *nd;
	int rtn = 0;

	struct proc_dir_entry *de;

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
	de = PDE(inode);
	if (!de) {
		rtn = -ENXIO;
		goto done;
	}
	nd = (struct nd_struct *)de->data;
	if (!nd) {
		rtn = -ENXIO;
		goto done;
	}

	file->private_data = (void *) nd;

	/*
	 * Allocate the DPA buffer.
	 */

	if (nd->nd_dpa_buf) {
		rtn = -EBUSY;
	} else {
		nd->nd_dpa_buf = kmalloc(DPA_MAX, GFP_KERNEL);

		if (!nd->nd_dpa_buf) {
			rtn = -ENOMEM;
		} else {
			nd->nd_dpa_out = 0;
			nd->nd_dpa_in = 0;
			nd->nd_dpa_lbolt = jiffies;
		}
	}

done:

	if (rtn)
		module_put(THIS_MODULE);
	return rtn;
}

/*
 * dgrp_dpa_release -- close the DPA device for a particular PortServer
 */
static int dgrp_dpa_release(struct inode *inode, struct file *file)
{
	struct nd_struct *nd;
	u8 *buf;
	unsigned long lock_flags;

	/*
	 *  Get the node pointer, and quit if it doesn't exist.
	 */
	nd = (struct nd_struct *)(file->private_data);
	if (!nd)
		goto done;

	/*
	 *  Free the dpa buffer.
	 */

	spin_lock_irqsave(&nd->nd_dpa_lock, lock_flags);

	buf = nd->nd_dpa_buf;

	nd->nd_dpa_buf = NULL;
	nd->nd_dpa_out = nd->nd_dpa_in;

	/*
	 *  Wakeup any thread waiting for buffer space.
	 */

	if (nd->nd_dpa_flag & DPA_WAIT_SPACE) {
		nd->nd_dpa_flag &= ~DPA_WAIT_SPACE;
		wake_up_interruptible(&nd->nd_dpa_wqueue);
	}

	spin_unlock_irqrestore(&nd->nd_dpa_lock, lock_flags);

	kfree(buf);

done:
	module_put(THIS_MODULE);
	file->private_data = NULL;
	return 0;
}

/*
 * dgrp_dpa_read
 *
 * Copy data from the monitoring buffer to the user, freeing space
 * in the monitoring buffer for more messages
 */
static ssize_t dgrp_dpa_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	struct nd_struct *nd;
	int n;
	int r;
	int offset = 0;
	int res = 0;
	ssize_t rtn;
	unsigned long lock_flags;

	/*
	 *  Get the node pointer, and quit if it doesn't exist.
	 */
	nd = (struct nd_struct *)(file->private_data);
	if (!nd)
		return -ENXIO;

	/*
	 *  Wait for some data to appear in the buffer.
	 */

	spin_lock_irqsave(&nd->nd_dpa_lock, lock_flags);

	for (;;) {
		n = (nd->nd_dpa_in - nd->nd_dpa_out) & DPA_MASK;

		if (n != 0)
			break;

		nd->nd_dpa_flag |= DPA_WAIT_DATA;

		spin_unlock_irqrestore(&nd->nd_dpa_lock, lock_flags);

		/*
		 * Go to sleep waiting until the condition becomes true.
		 */
		rtn = wait_event_interruptible(nd->nd_dpa_wqueue,
			((nd->nd_dpa_flag & DPA_WAIT_DATA) == 0));

		if (rtn)
			return rtn;

		spin_lock_irqsave(&nd->nd_dpa_lock, lock_flags);
	}

	/*
	 *  Read whatever is there.
	 */

	if (n > count)
		n = count;

	res = n;

	r = DPA_MAX - nd->nd_dpa_out;

	if (r <= n) {

		spin_unlock_irqrestore(&nd->nd_dpa_lock, lock_flags);
		rtn = copy_to_user((void __user *)buf,
				   nd->nd_dpa_buf + nd->nd_dpa_out, r);
		spin_lock_irqsave(&nd->nd_dpa_lock, lock_flags);

		if (rtn) {
			rtn = -EFAULT;
			goto done;
		}

		nd->nd_dpa_out = 0;
		n -= r;
		offset = r;
	}

	spin_unlock_irqrestore(&nd->nd_dpa_lock, lock_flags);
	rtn = copy_to_user((void __user *)buf + offset,
			   nd->nd_dpa_buf + nd->nd_dpa_out, n);
	spin_lock_irqsave(&nd->nd_dpa_lock, lock_flags);

	if (rtn) {
		rtn = -EFAULT;
		goto done;
	}

	nd->nd_dpa_out += n;

	*ppos += res;

	rtn = res;

	/*
	 *  Wakeup any thread waiting for buffer space.
	 */

	n = (nd->nd_dpa_in - nd->nd_dpa_out) & DPA_MASK;

	if (nd->nd_dpa_flag & DPA_WAIT_SPACE &&
	    (DPA_MAX - n) > DPA_HIGH_WATER) {
		nd->nd_dpa_flag &= ~DPA_WAIT_SPACE;
		wake_up_interruptible(&nd->nd_dpa_wqueue);
	}

 done:
	spin_unlock_irqrestore(&nd->nd_dpa_lock, lock_flags);
	return rtn;
}

static unsigned int dgrp_dpa_select(struct file *file,
				    struct poll_table_struct *table)
{
	unsigned int retval = 0;
	struct nd_struct *nd = file->private_data;

	if (nd->nd_dpa_out != nd->nd_dpa_in)
		retval |= POLLIN | POLLRDNORM; /* Conditionally readable */

	retval |= POLLOUT | POLLWRNORM;        /* Always writeable */

	return retval;
}

static long dgrp_dpa_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{

	struct nd_struct  *nd;
	struct digi_chan getchan;
	struct digi_node getnode;
	struct ch_struct *ch;
	struct digi_debug setdebug;
	struct digi_vpd vpd;
	unsigned int port;
	void __user *uarg = (void __user *) arg;

	nd = file->private_data;

	switch (cmd) {
	case DIGI_GETCHAN:
		if (copy_from_user(&getchan, uarg, sizeof(struct digi_chan)))
			return -EFAULT;

		port = getchan.ch_port;

		if (port > nd->nd_chan_count)
			return -EINVAL;

		ch = nd->nd_chan + port;

		getchan.ch_open = (ch->ch_open_count > 0) ? 1 : 0;
		getchan.ch_txcount = ch->ch_txcount;
		getchan.ch_rxcount = ch->ch_rxcount;
		getchan.ch_s_brate = ch->ch_s_brate;
		getchan.ch_s_estat = ch->ch_s_elast;
		getchan.ch_s_cflag = ch->ch_s_cflag;
		getchan.ch_s_iflag = ch->ch_s_iflag;
		getchan.ch_s_oflag = ch->ch_s_oflag;
		getchan.ch_s_xflag = ch->ch_s_xflag;
		getchan.ch_s_mstat = ch->ch_s_mlast;

		if (copy_to_user(uarg, &getchan, sizeof(struct digi_chan)))
			return -EFAULT;
		break;


	case DIGI_GETNODE:
		getnode.nd_state = (nd->nd_state & NS_READY) ? 1 : 0;
		getnode.nd_chan_count = nd->nd_chan_count;
		getnode.nd_tx_byte = nd->nd_tx_byte;
		getnode.nd_rx_byte = nd->nd_rx_byte;

		memset(&getnode.nd_ps_desc, 0, MAX_DESC_LEN);
		strncpy(getnode.nd_ps_desc, nd->nd_ps_desc, MAX_DESC_LEN);

		if (copy_to_user(uarg, &getnode, sizeof(struct digi_node)))
			return -EFAULT;
		break;


	case DIGI_SETDEBUG:
		if (copy_from_user(&setdebug, uarg, sizeof(struct digi_debug)))
			return -EFAULT;

		nd->nd_dpa_debug = setdebug.onoff;
		nd->nd_dpa_port = setdebug.port;
		break;


	case DIGI_GETVPD:
		memset(&vpd, 0, sizeof(vpd));
		if (nd->nd_vpd_len > 0) {
			vpd.vpd_len = nd->nd_vpd_len;
			memcpy(&vpd.vpd_data, &nd->nd_vpd, nd->nd_vpd_len);
		} else {
			vpd.vpd_len = 0;
		}

		if (copy_to_user(uarg, &vpd, sizeof(struct digi_vpd)))
			return -EFAULT;
		break;
	}

	return 0;
}

/**
 * dgrp_dpa() -- send data to the device monitor queue
 * @nd: pointer to a node structure
 * @buf: buffer of data to copy to the monitoring buffer
 * @len: number of bytes to transfer to the buffer
 *
 * Called by the net device routines to send data to the device
 * monitor queue.  If the device monitor buffer is too full to
 * accept the data, it waits until the buffer is ready.
 */
static void dgrp_dpa(struct nd_struct *nd, u8 *buf, int nbuf)
{
	int n;
	int r;
	unsigned long lock_flags;

	/*
	 *  Grab DPA lock.
	 */
	spin_lock_irqsave(&nd->nd_dpa_lock, lock_flags);

	/*
	 *  Loop while data remains.
	 */
	while (nbuf > 0 && nd->nd_dpa_buf != NULL) {

		n = (nd->nd_dpa_out - nd->nd_dpa_in - 1) & DPA_MASK;

		/*
		 * Enforce flow control on the DPA device.
		 */
		if (n < (DPA_MAX - DPA_HIGH_WATER))
			nd->nd_dpa_flag |= DPA_WAIT_SPACE;

		/*
		 * This should never happen, as the flow control above
		 * should have stopped things before they got to this point.
		 */
		if (n == 0) {
			spin_unlock_irqrestore(&nd->nd_dpa_lock, lock_flags);
			return;
		}

		/*
		 * Copy as much data as will fit.
		 */

		if (n > nbuf)
			n = nbuf;

		r = DPA_MAX - nd->nd_dpa_in;

		if (r <= n) {
			memcpy(nd->nd_dpa_buf + nd->nd_dpa_in, buf, r);

			n -= r;

			nd->nd_dpa_in = 0;

			buf += r;
			nbuf -= r;
		}

		memcpy(nd->nd_dpa_buf + nd->nd_dpa_in, buf, n);

		nd->nd_dpa_in += n;

		buf += n;
		nbuf -= n;

		if (nd->nd_dpa_in >= DPA_MAX)
			pr_info_ratelimited("%s - nd->nd_dpa_in (%i) >= DPA_MAX\n",
					    __func__, nd->nd_dpa_in);

		/*
		 *  Wakeup any thread waiting for data
		 */
		if (nd->nd_dpa_flag & DPA_WAIT_DATA) {
			nd->nd_dpa_flag &= ~DPA_WAIT_DATA;
			wake_up_interruptible(&nd->nd_dpa_wqueue);
		}
	}

	/*
	 *  Release the DPA lock.
	 */
	spin_unlock_irqrestore(&nd->nd_dpa_lock, lock_flags);
}

/**
 * dgrp_monitor_data() -- builds a DPA data packet
 * @nd: pointer to a node structure
 * @type: type of message to be logged in the DPA buffer
 * @buf: buffer of data to be logged in the DPA buffer
 * @size -- number of bytes in the "buf" buffer
 */
void dgrp_dpa_data(struct nd_struct *nd, int type, u8 *buf, int size)
{
	u8 header[5];

	header[0] = type;

	put_unaligned_be32(size, header + 1);

	dgrp_dpa(nd, header, sizeof(header));
	dgrp_dpa(nd, buf, size);
}

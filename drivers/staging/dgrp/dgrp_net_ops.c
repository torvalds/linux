/*
 *
 * Copyright 1999 Digi International (www.digi.com)
 *     James Puzzo  <jamesp at digi dot com>
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
 *     dgrp_net_ops.c
 *
 *  Description:
 *
 *     Handle the file operations required for the "network" devices.
 *     Includes those functions required to register the "net" devices
 *     in "/proc".
 *
 *  Author:
 *
 *     James A. Puzzo
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/ratelimit.h>
#include <asm/unaligned.h>

#define MYFLIPLEN	TBUF_MAX

#include "dgrp_common.h"

#define TTY_FLIPBUF_SIZE 512
#define DEVICE_NAME_SIZE 50

/*
 *  Generic helper function declarations
 */
static void   parity_scan(struct ch_struct *ch, unsigned char *cbuf,
				unsigned char *fbuf, int *len);

/*
 *  File operation declarations
 */
static int dgrp_net_open(struct inode *, struct file *);
static int dgrp_net_release(struct inode *, struct file *);
static ssize_t dgrp_net_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dgrp_net_write(struct file *, const char __user *, size_t,
			      loff_t *);
static long dgrp_net_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg);
static unsigned int dgrp_net_select(struct file *file,
				    struct poll_table_struct *table);

static const struct file_operations net_ops = {
	.owner   =  THIS_MODULE,
	.read    =  dgrp_net_read,
	.write   =  dgrp_net_write,
	.poll    =  dgrp_net_select,
	.unlocked_ioctl =  dgrp_net_ioctl,
	.open    =  dgrp_net_open,
	.release =  dgrp_net_release,
};

static struct inode_operations net_inode_ops = {
	.permission = dgrp_inode_permission
};

void dgrp_register_net_hook(struct proc_dir_entry *de)
{
	struct nd_struct *node = de->data;

	de->proc_iops = &net_inode_ops;
	de->proc_fops = &net_ops;
	node->nd_net_de = de;
	sema_init(&node->nd_net_semaphore, 1);
	node->nd_state = NS_CLOSED;
	dgrp_create_node_class_sysfs_files(node);
}


/**
 * dgrp_dump() -- prints memory for debugging purposes.
 * @mem: Memory location which should be printed to the console
 * @len: Number of bytes to be dumped
 */
static void dgrp_dump(u8 *mem, int len)
{
	int i;

	pr_debug("dgrp dump length = %d, data = ", len);
	for (i = 0; i < len; ++i)
		pr_debug("%.2x ", mem[i]);
	pr_debug("\n");
}

/**
 * dgrp_read_data_block() -- Read a data block
 * @ch: struct ch_struct *
 * @flipbuf: u8 *
 * @flipbuf_size: size of flipbuf
 */
static void dgrp_read_data_block(struct ch_struct *ch, u8 *flipbuf,
				 int flipbuf_size)
{
	int t;
	int n;

	if (flipbuf_size <= 0)
		return;

	t = RBUF_MAX - ch->ch_rout;
	n = flipbuf_size;

	if (n >= t) {
		memcpy(flipbuf, ch->ch_rbuf + ch->ch_rout, t);
		flipbuf += t;
		n -= t;
		ch->ch_rout = 0;
	}

	memcpy(flipbuf, ch->ch_rbuf + ch->ch_rout, n);
	flipbuf += n;
	ch->ch_rout += n;
}


/**
 * dgrp_input() -- send data to the line disipline
 * @ch: pointer to channel struct
 *
 * Copys the rbuf to the flipbuf and sends to line discipline.
 * Sends input buffer data to the line discipline.
 *
 * There are several modes to consider here:
 *    rawreadok, tty->real_raw, and IF_PARMRK
 */
static void dgrp_input(struct ch_struct *ch)
{
	struct nd_struct *nd;
	struct tty_struct *tty;
	int remain;
	int data_len;
	int len;
	int flip_len;
	int tty_count;
	ulong lock_flags;
	struct tty_ldisc *ld;
	u8  *myflipbuf;
	u8  *myflipflagbuf;

	if (!ch)
		return;

	nd = ch->ch_nd;

	if (!nd)
		return;

	spin_lock_irqsave(&nd->nd_lock, lock_flags);

	myflipbuf = nd->nd_inputbuf;
	myflipflagbuf = nd->nd_inputflagbuf;

	if (!ch->ch_open_count) {
		ch->ch_rout = ch->ch_rin;
		goto out;
	}

	if (ch->ch_tun.un_flag & UN_CLOSING) {
		ch->ch_rout = ch->ch_rin;
		goto out;
	}

	tty = (ch->ch_tun).un_tty;


	if (!tty || tty->magic != TTY_MAGIC) {
		ch->ch_rout = ch->ch_rin;
		goto out;
	}

	tty_count = tty->count;
	if (!tty_count) {
		ch->ch_rout = ch->ch_rin;
		goto out;
	}

	if (tty->closing || test_bit(TTY_CLOSING, &tty->flags)) {
		ch->ch_rout = ch->ch_rin;
		goto out;
	}

	spin_unlock_irqrestore(&nd->nd_lock, lock_flags);

	/* Decide how much data we can send into the tty layer */
	if (dgrp_rawreadok && tty->real_raw)
		flip_len = MYFLIPLEN;
	else
		flip_len = TTY_FLIPBUF_SIZE;

	/* data_len should be the number of chars that we read in */
	data_len = (ch->ch_rin - ch->ch_rout) & RBUF_MASK;
	remain = data_len;

	/* len is the amount of data we are going to transfer here */
	len = min(data_len, flip_len);

	/* take into consideration length of ldisc */
	len = min(len, (N_TTY_BUF_SIZE - 1) - tty->read_cnt);

	ld = tty_ldisc_ref(tty);

	/*
	 * If we were unable to get a reference to the ld,
	 * don't flush our buffer, and act like the ld doesn't
	 * have any space to put the data right now.
	 */
	if (!ld) {
		len = 0;
	} else if (!ld->ops->receive_buf) {
		spin_lock_irqsave(&nd->nd_lock, lock_flags);
		ch->ch_rout = ch->ch_rin;
		spin_unlock_irqrestore(&nd->nd_lock, lock_flags);
		len = 0;
	}

	/* Check DPA flow control */
	if ((nd->nd_dpa_debug) &&
	    (nd->nd_dpa_flag & DPA_WAIT_SPACE) &&
	    (nd->nd_dpa_port == MINOR(tty_devnum(ch->ch_tun.un_tty))))
		len = 0;

	if ((len) && !(ch->ch_flag & CH_RXSTOP)) {

		dgrp_read_data_block(ch, myflipbuf, len);

		/*
		 * In high performance mode, we don't have to update
		 * flag_buf or any of the counts or pointers into flip buf.
		 */
		if (!dgrp_rawreadok || !tty->real_raw) {
			if (I_PARMRK(tty) || I_BRKINT(tty) || I_INPCK(tty))
				parity_scan(ch, myflipbuf, myflipflagbuf, &len);
			else
				memset(myflipflagbuf, TTY_NORMAL, len);
		}

		if ((nd->nd_dpa_debug) &&
		    (nd->nd_dpa_port == PORT_NUM(MINOR(tty_devnum(tty)))))
			dgrp_dpa_data(nd, 1, myflipbuf, len);

		/*
		 * If we're doing raw reads, jam it right into the
		 * line disc bypassing the flip buffers.
		 */
		if (dgrp_rawreadok && tty->real_raw)
			ld->ops->receive_buf(tty, myflipbuf, NULL, len);
		else {
			len = tty_buffer_request_room(tty, len);
			tty_insert_flip_string_flags(tty, myflipbuf,
						     myflipflagbuf, len);

			/* Tell the tty layer its okay to "eat" the data now */
			tty_flip_buffer_push(tty);
		}

		ch->ch_rxcount += len;
	}

	if (ld)
		tty_ldisc_deref(ld);

	/*
	 * Wake up any sleepers (maybe dgrp close) that might be waiting
	 * for a channel flag state change.
	 */
	wake_up_interruptible(&ch->ch_flag_wait);
	return;

out:
	spin_unlock_irqrestore(&nd->nd_lock, lock_flags);
}


/*
 *  parity_scan
 *
 *  Loop to inspect each single character or 0xFF escape.
 *
 *  if PARMRK & ~DOSMODE:
 *     0xFF  0xFF           Normal 0xFF character, escaped
 *                          to eliminate confusion.
 *     0xFF  0x00  0x00     Break
 *     0xFF  0x00  CC       Error character CC.
 *     CC                   Normal character CC.
 *
 *  if PARMRK & DOSMODE:
 *     0xFF  0x18  0x00     Break
 *     0xFF  0x08  0x00     Framing Error
 *     0xFF  0x04  0x00     Parity error
 *     0xFF  0x0C  0x00     Both Framing and Parity error
 *
 *  TODO:  do we need to do the XMODEM, XOFF, XON, XANY processing??
 *         as per protocol
 */
static void parity_scan(struct ch_struct *ch, unsigned char *cbuf,
			unsigned char *fbuf, int *len)
{
	int l = *len;
	int count = 0;
	int DOS = ((ch->ch_iflag & IF_DOSMODE) == 0 ? 0 : 1);
	unsigned char *cout; /* character buffer */
	unsigned char *fout; /* flag buffer */
	unsigned char *in;
	unsigned char c;

	in = cbuf;
	cout = cbuf;
	fout = fbuf;

	while (l--) {
		c = *in;
		in++;

		switch (ch->ch_pscan_state) {
		default:
			/* reset to sanity and fall through */
			ch->ch_pscan_state = 0 ;

		case 0:
			/* No FF seen yet */
			if (c == 0xff) /* delete this character from stream */
				ch->ch_pscan_state = 1;
			else {
				*cout++ = c;
				*fout++ = TTY_NORMAL;
				count += 1;
			}
			break;

		case 1:
			/* first FF seen */
			if (c == 0xff) {
				/* doubled ff, transform to single ff */
				*cout++ = c;
				*fout++ = TTY_NORMAL;
				count += 1;
				ch->ch_pscan_state = 0;
			} else {
				/* save value examination in next state */
				ch->ch_pscan_savechar = c;
				ch->ch_pscan_state = 2;
			}
			break;

		case 2:
			/* third character of ff sequence */
			*cout++ = c;
			if (DOS) {
				if (ch->ch_pscan_savechar & 0x10)
					*fout++ = TTY_BREAK;
				else if (ch->ch_pscan_savechar & 0x08)
					*fout++ = TTY_FRAME;
				else
					/*
					 * either marked as a parity error,
					 * indeterminate, or not in DOSMODE
					 * call it a parity error
					 */
					*fout++ = TTY_PARITY;
			} else {
				/* case FF XX ?? where XX is not 00 */
				if (ch->ch_pscan_savechar & 0xff) {
					/* this should not happen */
					pr_info("%s: parity_scan: error unexpected byte\n",
						__func__);
					*fout++ = TTY_PARITY;
				}
				/* case FF 00 XX where XX is not 00 */
				else if (c == 0xff)
					*fout++ = TTY_PARITY;
				/* case FF 00 00 */
				else
					*fout++ = TTY_BREAK;

			}
			count += 1;
			ch->ch_pscan_state = 0;
		}
	}
	*len = count;
}


/**
 * dgrp_net_idle() -- Idle the network connection
 * @nd: pointer to node structure to idle
 */
static void dgrp_net_idle(struct nd_struct *nd)
{
	struct ch_struct *ch;
	int i;

	nd->nd_tx_work = 1;

	nd->nd_state = NS_IDLE;
	nd->nd_flag = 0;

	for (i = nd->nd_seq_out; ; i = (i + 1) & SEQ_MASK) {
		if (!nd->nd_seq_wait[i]) {
			nd->nd_seq_wait[i] = 0;
			wake_up_interruptible(&nd->nd_seq_wque[i]);
		}

		if (i == nd->nd_seq_in)
			break;
	}

	nd->nd_seq_out = nd->nd_seq_in;

	nd->nd_unack = 0;
	nd->nd_remain = 0;

	nd->nd_tx_module = 0x10;
	nd->nd_rx_module = 0x00;

	for (i = 0, ch = nd->nd_chan; i < CHAN_MAX; i++, ch++) {
		ch->ch_state = CS_IDLE;

		ch->ch_otype = 0;
		ch->ch_otype_waiting = 0;
	}
}

/*
 *  Increase the number of channels, waking up any
 *  threads that might be waiting for the channels
 *  to appear.
 */
static void increase_channel_count(struct nd_struct *nd, int n)
{
	struct ch_struct *ch;
	struct device *classp;
	char name[DEVICE_NAME_SIZE];
	int ret;
	u8 *buf;
	int i;

	for (i = nd->nd_chan_count; i < n; ++i) {
		ch = nd->nd_chan + i;

		/* FIXME: return a useful error instead! */
		buf = kmalloc(TBUF_MAX, GFP_KERNEL);
		if (!buf)
			return;

		if (ch->ch_tbuf)
			pr_info_ratelimited("%s - ch_tbuf was not NULL\n",
					    __func__);

		ch->ch_tbuf = buf;

		buf = kmalloc(RBUF_MAX, GFP_KERNEL);
		if (!buf)
			return;

		if (ch->ch_rbuf)
			pr_info("%s - ch_rbuf was not NULL\n",
				__func__);
		ch->ch_rbuf = buf;

		classp = tty_port_register_device(&ch->port,
						  nd->nd_serial_ttdriver, i,
						  NULL);

		ch->ch_tun.un_sysfs = classp;
		snprintf(name, DEVICE_NAME_SIZE, "tty_%d", i);

		dgrp_create_tty_sysfs(&ch->ch_tun, classp);
		ret = sysfs_create_link(&nd->nd_class_dev->kobj,
					&classp->kobj, name);

		/* NOTE: We don't support "cu" devices anymore,
		 * so you will notice we don't register them
		 * here anymore. */
		if (dgrp_register_prdevices) {
			classp = tty_register_device(nd->nd_xprint_ttdriver,
						     i, NULL);
			ch->ch_pun.un_sysfs = classp;
			snprintf(name, DEVICE_NAME_SIZE, "pr_%d", i);

			dgrp_create_tty_sysfs(&ch->ch_pun, classp);
			ret = sysfs_create_link(&nd->nd_class_dev->kobj,
						&classp->kobj, name);
		}

		nd->nd_chan_count = i + 1;
		wake_up_interruptible(&ch->ch_flag_wait);
	}
}

/*
 * Decrease the number of channels, and wake up any threads that might
 * be waiting on the channels that vanished.
 */
static void decrease_channel_count(struct nd_struct *nd, int n)
{
	struct ch_struct *ch;
	char name[DEVICE_NAME_SIZE];
	int i;

	for (i = nd->nd_chan_count - 1; i >= n; --i) {
		ch = nd->nd_chan + i;

		/*
		 *  Make any open ports inoperative.
		 */
		ch->ch_state = CS_IDLE;

		ch->ch_otype = 0;
		ch->ch_otype_waiting = 0;

		/*
		 *  Only "HANGUP" if we care about carrier
		 *  transitions and we are already open.
		 */
		if (ch->ch_open_count != 0) {
			ch->ch_flag |= CH_HANGUP;
			dgrp_carrier(ch);
		}

		/*
		 * Unlike the CH_HANGUP flag above, use another
		 * flag to indicate to the RealPort state machine
		 * that this port has disappeared.
		 */
		if (ch->ch_open_count != 0)
			ch->ch_flag |= CH_PORT_GONE;

		wake_up_interruptible(&ch->ch_flag_wait);

		nd->nd_chan_count = i;

		kfree(ch->ch_tbuf);
		ch->ch_tbuf = NULL;

		kfree(ch->ch_rbuf);
		ch->ch_rbuf = NULL;

		nd->nd_chan_count = i;

		dgrp_remove_tty_sysfs(ch->ch_tun.un_sysfs);
		snprintf(name, DEVICE_NAME_SIZE, "tty_%d", i);
		sysfs_remove_link(&nd->nd_class_dev->kobj, name);
		tty_unregister_device(nd->nd_serial_ttdriver, i);

		/*
		 * NOTE: We don't support "cu" devices anymore, so don't
		 * unregister them here anymore.
		 */

		if (dgrp_register_prdevices) {
			dgrp_remove_tty_sysfs(ch->ch_pun.un_sysfs);
			snprintf(name, DEVICE_NAME_SIZE, "pr_%d", i);
			sysfs_remove_link(&nd->nd_class_dev->kobj, name);
			tty_unregister_device(nd->nd_xprint_ttdriver, i);
		}
	}
}

/**
 * dgrp_chan_count() -- Adjust the node channel count.
 * @nd: pointer to a node structure
 * @n: new value for channel count
 *
 * Adjusts the node channel count.  If new ports have appeared, it tries
 * to signal those processes that might have been waiting for ports to
 * appear.  If ports have disappeared it tries to signal those processes
 * that might be hung waiting for a response for the now non-existant port.
 */
static void dgrp_chan_count(struct nd_struct *nd, int n)
{
	if (n == nd->nd_chan_count)
		return;

	if (n > nd->nd_chan_count)
		increase_channel_count(nd, n);

	if (n < nd->nd_chan_count)
		decrease_channel_count(nd, n);
}

/**
 * dgrp_monitor() -- send data to the device monitor queue
 * @nd: pointer to a node structure
 * @buf: data to copy to the monitoring buffer
 * @len: number of bytes to transfer to the buffer
 *
 * Called by the net device routines to send data to the device
 * monitor queue.  If the device monitor buffer is too full to
 * accept the data, it waits until the buffer is ready.
 */
static void dgrp_monitor(struct nd_struct *nd, u8 *buf, int len)
{
	int n;
	int r;
	int rtn;

	/*
	 *  Grab monitor lock.
	 */
	down(&nd->nd_mon_semaphore);

	/*
	 *  Loop while data remains.
	 */
	while ((len > 0) && (nd->nd_mon_buf)) {
		/*
		 *  Determine the amount of available space left in the
		 *  buffer.  If there's none, wait until some appears.
		 */

		n = (nd->nd_mon_out - nd->nd_mon_in - 1) & MON_MASK;

		if (!n) {
			nd->nd_mon_flag |= MON_WAIT_SPACE;

			up(&nd->nd_mon_semaphore);

			/*
			 * Go to sleep waiting until the condition becomes true.
			 */
			rtn = wait_event_interruptible(nd->nd_mon_wqueue,
						       ((nd->nd_mon_flag & MON_WAIT_SPACE) == 0));

/* FIXME: really ignore rtn? */

			/*
			 *  We can't exit here if we receive a signal, since
			 *  to do so would trash the debug stream.
			 */

			down(&nd->nd_mon_semaphore);

			continue;
		}

		/*
		 * Copy as much data as will fit.
		 */

		if (n > len)
			n = len;

		r = MON_MAX - nd->nd_mon_in;

		if (r <= n) {
			memcpy(nd->nd_mon_buf + nd->nd_mon_in, buf, r);

			n -= r;

			nd->nd_mon_in = 0;

			buf += r;
			len -= r;
		}

		memcpy(nd->nd_mon_buf + nd->nd_mon_in, buf, n);

		nd->nd_mon_in += n;

		buf += n;
		len -= n;

		if (nd->nd_mon_in >= MON_MAX)
			pr_info_ratelimited("%s - nd_mon_in (%i) >= MON_MAX\n",
					    __func__, nd->nd_mon_in);

		/*
		 *  Wakeup any thread waiting for data
		 */

		if (nd->nd_mon_flag & MON_WAIT_DATA) {
			nd->nd_mon_flag &= ~MON_WAIT_DATA;
			wake_up_interruptible(&nd->nd_mon_wqueue);
		}
	}

	/*
	 *  Release the monitor lock.
	 */
	up(&nd->nd_mon_semaphore);
}

/**
 * dgrp_encode_time() -- Encodes rpdump time into a 4-byte quantity.
 * @nd: pointer to a node structure
 * @buf: destination buffer
 *
 * Encodes "rpdump" time into a 4-byte quantity.  Time is measured since
 * open.
 */
static void dgrp_encode_time(struct nd_struct *nd, u8 *buf)
{
	ulong t;

	/*
	 *  Convert time in HZ since open to time in milliseconds
	 *  since open.
	 */
	t = jiffies - nd->nd_mon_lbolt;
	t = 1000 * (t / HZ) + 1000 * (t % HZ) / HZ;

	put_unaligned_be32((uint)(t & 0xffffffff), buf);
}



/**
 * dgrp_monitor_message() -- Builds a rpdump style message.
 * @nd: pointer to a node structure
 * @message: destination buffer
 */
static void dgrp_monitor_message(struct nd_struct *nd, char *message)
{
	u8 header[7];
	int n;

	header[0] = RPDUMP_MESSAGE;

	dgrp_encode_time(nd, header + 1);

	n = strlen(message);

	put_unaligned_be16(n, header + 5);

	dgrp_monitor(nd, header, sizeof(header));
	dgrp_monitor(nd, (u8 *) message, n);
}



/**
 * dgrp_monitor_reset() -- Note a reset in the monitoring buffer.
 * @nd: pointer to a node structure
 */
static void dgrp_monitor_reset(struct nd_struct *nd)
{
	u8 header[5];

	header[0] = RPDUMP_RESET;

	dgrp_encode_time(nd, header + 1);

	dgrp_monitor(nd, header, sizeof(header));
}

/**
 * dgrp_monitor_data() -- builds a monitor data packet
 * @nd: pointer to a node structure
 * @type: type of message to be logged
 * @buf: data to be logged
 * @size: number of bytes in the buffer
 */
static void dgrp_monitor_data(struct nd_struct *nd, u8 type, u8 *buf, int size)
{
	u8 header[7];

	header[0] = type;

	dgrp_encode_time(nd, header + 1);

	put_unaligned_be16(size, header + 5);

	dgrp_monitor(nd, header, sizeof(header));
	dgrp_monitor(nd, buf, size);
}

static int alloc_nd_buffers(struct nd_struct *nd)
{

	nd->nd_iobuf = NULL;
	nd->nd_writebuf = NULL;
	nd->nd_inputbuf = NULL;
	nd->nd_inputflagbuf = NULL;

	/*
	 *  Allocate the network read/write buffer.
	 */
	nd->nd_iobuf = kzalloc(UIO_MAX + 10, GFP_KERNEL);
	if (!nd->nd_iobuf)
		goto out_err;

	/*
	 * Allocate a buffer for doing the copy from user space to
	 * kernel space in the write routines.
	 */
	nd->nd_writebuf = kzalloc(WRITEBUFLEN, GFP_KERNEL);
	if (!nd->nd_writebuf)
		goto out_err;

	/*
	 * Allocate a buffer for doing the copy from kernel space to
	 * tty buffer space in the read routines.
	 */
	nd->nd_inputbuf = kzalloc(MYFLIPLEN, GFP_KERNEL);
	if (!nd->nd_inputbuf)
		goto out_err;

	/*
	 * Allocate a buffer for doing the copy from kernel space to
	 * tty buffer space in the read routines.
	 */
	nd->nd_inputflagbuf = kzalloc(MYFLIPLEN, GFP_KERNEL);
	if (!nd->nd_inputflagbuf)
		goto out_err;

	return 0;

out_err:
	kfree(nd->nd_iobuf);
	kfree(nd->nd_writebuf);
	kfree(nd->nd_inputbuf);
	kfree(nd->nd_inputflagbuf);
	return -ENOMEM;
}

/*
 * dgrp_net_open() -- Open the NET device for a particular PortServer
 */
static int dgrp_net_open(struct inode *inode, struct file *file)
{
	struct nd_struct *nd;
	struct proc_dir_entry *de;
	ulong  lock_flags;
	int rtn;

	rtn = try_module_get(THIS_MODULE);
	if (!rtn)
		return -EAGAIN;

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

	nd = (struct nd_struct *) de->data;
	if (!nd) {
		rtn = -ENXIO;
		goto done;
	}

	file->private_data = (void *) nd;

	/*
	 *  Grab the NET lock.
	 */
	down(&nd->nd_net_semaphore);

	if (nd->nd_state != NS_CLOSED) {
		rtn = -EBUSY;
		goto unlock;
	}

	/*
	 *  Initialize the link speed parameters.
	 */

	nd->nd_link.lk_fast_rate = UIO_MAX;
	nd->nd_link.lk_slow_rate = UIO_MAX;

	nd->nd_link.lk_fast_delay = 1000;
	nd->nd_link.lk_slow_delay = 1000;

	nd->nd_link.lk_header_size = 46;


	rtn = alloc_nd_buffers(nd);
	if (rtn)
		goto unlock;

	/*
	 *  The port is now open, so move it to the IDLE state
	 */
	dgrp_net_idle(nd);

	nd->nd_tx_time = jiffies;

	/*
	 *  If the polling routing is not running, start it running here
	 */
	spin_lock_irqsave(&dgrp_poll_data.poll_lock, lock_flags);

	if (!dgrp_poll_data.node_active_count) {
		dgrp_poll_data.node_active_count = 2;
		dgrp_poll_data.timer.expires = jiffies +
			dgrp_poll_tick * HZ / 1000;
		add_timer(&dgrp_poll_data.timer);
	}

	spin_unlock_irqrestore(&dgrp_poll_data.poll_lock, lock_flags);

	dgrp_monitor_message(nd, "Net Open");

unlock:
	/*
	 *  Release the NET lock.
	 */
	up(&nd->nd_net_semaphore);

done:
	if (rtn)
		module_put(THIS_MODULE);

	return rtn;
}

/* dgrp_net_release() -- close the NET device for a particular PortServer */
static int dgrp_net_release(struct inode *inode, struct file *file)
{
	struct nd_struct *nd;
	ulong  lock_flags;

	nd = (struct nd_struct *)(file->private_data);
	if (!nd)
		goto done;

/* TODO : historical locking placeholder */
/*
 *  In the HPUX version of the RealPort driver (which served as a basis
 *  for this driver) this locking code was used.  Saved if ever we need
 *  to review the locking under Linux.
 */
/*	spinlock(&nd->nd_lock); */


	/*
	 *  Grab the NET lock.
	 */
	down(&nd->nd_net_semaphore);

	/*
	 *  Before "closing" the internal connection, make sure all
	 *  ports are "idle".
	 */
	dgrp_net_idle(nd);

	nd->nd_state = NS_CLOSED;
	nd->nd_flag = 0;

	/*
	 *  TODO ... must the wait queue be reset on close?
	 *  should any pending waiters be reset?
	 *  Let's decide to assert that the waitq is empty... and see
	 *  how soon we break.
	 */
	if (waitqueue_active(&nd->nd_tx_waitq))
		pr_info("%s - expected waitqueue_active to be false\n",
			__func__);

	nd->nd_send = 0;

	kfree(nd->nd_iobuf);
	nd->nd_iobuf = NULL;

/* TODO : historical locking placeholder */
/*
 *  In the HPUX version of the RealPort driver (which served as a basis
 *  for this driver) this locking code was used.  Saved if ever we need
 *  to review the locking under Linux.
 */
/*	spinunlock( &nd->nd_lock ); */


	kfree(nd->nd_writebuf);
	nd->nd_writebuf = NULL;

	kfree(nd->nd_inputbuf);
	nd->nd_inputbuf = NULL;

	kfree(nd->nd_inputflagbuf);
	nd->nd_inputflagbuf = NULL;

/* TODO : historical locking placeholder */
/*
 *  In the HPUX version of the RealPort driver (which served as a basis
 *  for this driver) this locking code was used.  Saved if ever we need
 *  to review the locking under Linux.
 */
/*	spinlock(&nd->nd_lock); */

	/*
	 *  Set the active port count to zero.
	 */
	dgrp_chan_count(nd, 0);

/* TODO : historical locking placeholder */
/*
 *  In the HPUX version of the RealPort driver (which served as a basis
 *  for this driver) this locking code was used.  Saved if ever we need
 *  to review the locking under Linux.
 */
/*	spinunlock(&nd->nd_lock); */

	/*
	 *  Release the NET lock.
	 */
	up(&nd->nd_net_semaphore);

	/*
	 *  Cause the poller to stop scheduling itself if this is
	 *  the last active node.
	 */
	spin_lock_irqsave(&dgrp_poll_data.poll_lock, lock_flags);

	if (dgrp_poll_data.node_active_count == 2) {
		del_timer(&dgrp_poll_data.timer);
		dgrp_poll_data.node_active_count = 0;
	}

	spin_unlock_irqrestore(&dgrp_poll_data.poll_lock, lock_flags);

done:
	down(&nd->nd_net_semaphore);

	dgrp_monitor_message(nd, "Net Close");

	up(&nd->nd_net_semaphore);

	module_put(THIS_MODULE);
	file->private_data = NULL;
	return 0;
}

/* used in dgrp_send to setup command header */
static inline u8 *set_cmd_header(u8 *b, u8 port, u8 cmd)
{
	*b++ = 0xb0 + (port & 0x0f);
	*b++ = cmd;
	return b;
}

/**
 * dgrp_send() -- build a packet for transmission to the server
 * @nd: pointer to a node structure
 * @tmax: maximum bytes to transmit
 *
 * returns number of bytes sent
 */
static int dgrp_send(struct nd_struct *nd, long tmax)
{
	struct ch_struct *ch = nd->nd_chan;
	u8 *b;
	u8 *buf;
	u8 *mbuf;
	u8 port;
	int mod;
	long send;
	int maxport;
	long lastport = -1;
	ushort rwin;
	long in;
	ushort n;
	long t;
	long ttotal;
	long tchan;
	long tsend;
	ushort tsafe;
	long work;
	long send_sync;
	long wanted_sync_port = -1;
	ushort tdata[CHAN_MAX];
	long used_buffer;

	mbuf = nd->nd_iobuf + UIO_BASE;
	buf = b = mbuf;

	send_sync = nd->nd_link.lk_slow_rate < UIO_MAX;

	ttotal = 0;
	tchan = 0;

	memset(tdata, 0, sizeof(tdata));


	/*
	 * If there are any outstanding requests to be serviced,
	 * service them here.
	 */
	if (nd->nd_send & NR_PASSWORD) {

		/*
		 *  Send Password response.
		 */

		b[0] = 0xfc;
		b[1] = 0x20;
		put_unaligned_be16(strlen(nd->password), b + 2);
		b += 4;
		b += strlen(nd->password);
		nd->nd_send &= ~(NR_PASSWORD);
	}


	/*
	 *  Loop over all modules to generate commands, and determine
	 *  the amount of data queued for transmit.
	 */

	for (mod = 0, port = 0; port < nd->nd_chan_count; mod++) {
		/*
		 *  If this is not the current module, enter a module select
		 *  code in the buffer.
		 */

		if (mod != nd->nd_tx_module)
			mbuf = ++b;

		/*
		 *  Loop to process one module.
		 */

		maxport = port + 16;

		if (maxport > nd->nd_chan_count)
			maxport = nd->nd_chan_count;

		for (; port < maxport; port++, ch++) {
			/*
			 *  Switch based on channel state.
			 */

			switch (ch->ch_state) {
			/*
			 *  Send requests when the port is closed, and there
			 *  are no Open, Close or Cancel requests expected.
			 */

			case CS_IDLE:
				/*
				 * Wait until any open error code
				 * has been delivered to all
				 * associated ports.
				 */

				if (ch->ch_open_error) {
					if (ch->ch_wait_count[ch->ch_otype]) {
						work = 1;
						break;
					}

					ch->ch_open_error = 0;
				}

				/*
				 *  Wait until the channel HANGUP flag is reset
				 *  before sending the first open.  We can only
				 *  get to this state after a server disconnect.
				 */

				if ((ch->ch_flag & CH_HANGUP) != 0)
					break;

				/*
				 *  If recovering from a TCP disconnect, or if
				 *  there is an immediate open pending, send an
				 *  Immediate Open request.
				 */
				if ((ch->ch_flag & CH_PORT_GONE) ||
				    ch->ch_wait_count[OTYPE_IMMEDIATE] != 0) {
					b = set_cmd_header(b, port, 10);
					*b++ = 0;

					ch->ch_state = CS_WAIT_OPEN;
					ch->ch_otype = OTYPE_IMMEDIATE;
					break;
				}

	/*
	 *  If there is no Persistent or Incoming Open on the wait
	 *  list in the server, and a thread is waiting for a
	 *  Persistent or Incoming Open, send a Persistent or Incoming
	 *  Open Request.
	 */
				if (ch->ch_otype_waiting == 0) {
					if (ch->ch_wait_count[OTYPE_PERSISTENT] != 0) {
						b = set_cmd_header(b, port, 10);
						*b++ = 1;

						ch->ch_state = CS_WAIT_OPEN;
						ch->ch_otype = OTYPE_PERSISTENT;
					} else if (ch->ch_wait_count[OTYPE_INCOMING] != 0) {
						b = set_cmd_header(b, port, 10);
						*b++ = 2;

						ch->ch_state = CS_WAIT_OPEN;
						ch->ch_otype = OTYPE_INCOMING;
					}
					break;
				}

				/*
				 *  If a Persistent or Incoming Open is pending in
				 *  the server, but there is no longer an open
				 *  thread waiting for it, cancel the request.
				 */

				if (ch->ch_wait_count[ch->ch_otype_waiting] == 0) {
					b = set_cmd_header(b, port, 10);
					*b++ = 4;

					ch->ch_state = CS_WAIT_CANCEL;
					ch->ch_otype = ch->ch_otype_waiting;
				}
				break;

				/*
				 *  Send port parameter queries.
				 */
			case CS_SEND_QUERY:
				/*
				 *  Clear out all FEP state that might remain
				 *  from the last connection.
				 */

				ch->ch_flag |= CH_PARAM;

				ch->ch_flag &= ~CH_RX_FLUSH;

				ch->ch_expect = 0;

				ch->ch_s_tin   = 0;
				ch->ch_s_tpos  = 0;
				ch->ch_s_tsize = 0;
				ch->ch_s_treq  = 0;
				ch->ch_s_elast = 0;

				ch->ch_s_rin   = 0;
				ch->ch_s_rwin  = 0;
				ch->ch_s_rsize = 0;

				ch->ch_s_tmax  = 0;
				ch->ch_s_ttime = 0;
				ch->ch_s_rmax  = 0;
				ch->ch_s_rtime = 0;
				ch->ch_s_rlow  = 0;
				ch->ch_s_rhigh = 0;

				ch->ch_s_brate = 0;
				ch->ch_s_iflag = 0;
				ch->ch_s_cflag = 0;
				ch->ch_s_oflag = 0;
				ch->ch_s_xflag = 0;

				ch->ch_s_mout  = 0;
				ch->ch_s_mflow = 0;
				ch->ch_s_mctrl = 0;
				ch->ch_s_xon   = 0;
				ch->ch_s_xoff  = 0;
				ch->ch_s_lnext = 0;
				ch->ch_s_xxon  = 0;
				ch->ch_s_xxoff = 0;

				/* Send Sequence Request */
				b = set_cmd_header(b, port, 14);

				/* Configure Event Conditions Packet */
				b = set_cmd_header(b, port, 42);
				put_unaligned_be16(0x02c0, b);
				b += 2;
				*b++ = (DM_DTR | DM_RTS | DM_CTS |
					DM_DSR | DM_RI | DM_CD);

				/* Send Status Request */
				b = set_cmd_header(b, port, 16);

				/* Send Buffer Request  */
				b = set_cmd_header(b, port, 20);

				/* Send Port Capability Request */
				b = set_cmd_header(b, port, 22);

				ch->ch_expect = (RR_SEQUENCE |
						 RR_STATUS  |
						 RR_BUFFER |
						 RR_CAPABILITY);

				ch->ch_state = CS_WAIT_QUERY;

				/* Raise modem signals */
				b = set_cmd_header(b, port, 44);

				if (ch->ch_flag & CH_PORT_GONE)
					ch->ch_s_mout = ch->ch_mout;
				else
					ch->ch_s_mout = ch->ch_mout = DM_DTR | DM_RTS;

				*b++ = ch->ch_mout;
				*b++ = ch->ch_s_mflow = 0;
				*b++ = ch->ch_s_mctrl = ch->ch_mctrl = 0;

				if (ch->ch_flag & CH_PORT_GONE)
					ch->ch_flag &= ~CH_PORT_GONE;

				break;

			/*
			 *  Handle normal open and ready mode.
			 */

			case CS_READY:

				/*
				 *  If the port is not open, and there are no
				 *  no longer any ports requesting an open,
				 *  then close the port.
				 */

				if (ch->ch_open_count == 0 &&
				    ch->ch_wait_count[ch->ch_otype] == 0) {
					goto send_close;
				}

	/*
	 *  Process waiting input.
	 *
	 *  If there is no one to read it, discard the data.
	 *
	 *  Otherwise if we are not in fastcook mode, or if there is a
	 *  fastcook thread waiting for data, send the data to the
	 *  line discipline.
	 */
				if (ch->ch_rin != ch->ch_rout) {
					if (ch->ch_tun.un_open_count == 0 ||
					     (ch->ch_tun.un_flag & UN_CLOSING) ||
					    (ch->ch_cflag & CF_CREAD) == 0) {
						ch->ch_rout = ch->ch_rin;
					} else if ((ch->ch_flag & CH_FAST_READ) == 0 ||
							ch->ch_inwait != 0) {
						dgrp_input(ch);

						if (ch->ch_rin != ch->ch_rout)
							work = 1;
					}
				}

				/*
				 *  Handle receive flush, and changes to
				 *  server port parameters.
				 */

				if (ch->ch_flag & (CH_RX_FLUSH | CH_PARAM)) {
				/*
				 *  If we are in receive flush mode,
				 *  and enough data has gone by, reset
				 *  receive flush mode.
				 */
					if (ch->ch_flag & CH_RX_FLUSH) {
						if (((ch->ch_flush_seq - nd->nd_seq_out) & SEQ_MASK) >
						    ((nd->nd_seq_in - nd->nd_seq_out) & SEQ_MASK))
							ch->ch_flag &= ~CH_RX_FLUSH;
						else
							work = 1;
					}

					/*
					 *  Send TMAX, TTIME.
					 */

					if (ch->ch_s_tmax  != ch->ch_tmax ||
					    ch->ch_s_ttime != ch->ch_ttime) {
						b = set_cmd_header(b, port, 48);

						ch->ch_s_tmax = ch->ch_tmax;
						ch->ch_s_ttime = ch->ch_ttime;

						put_unaligned_be16(ch->ch_s_tmax,
								   b);
						b += 2;

						put_unaligned_be16(ch->ch_s_ttime,
								   b);
						b += 2;
					}

					/*
					 *  Send RLOW, RHIGH.
					 */

					if (ch->ch_s_rlow  != ch->ch_rlow ||
					    ch->ch_s_rhigh != ch->ch_rhigh) {
						b = set_cmd_header(b, port, 45);

						ch->ch_s_rlow  = ch->ch_rlow;
						ch->ch_s_rhigh = ch->ch_rhigh;

						put_unaligned_be16(ch->ch_s_rlow,
								   b);
						b += 2;

						put_unaligned_be16(ch->ch_s_rhigh,
								   b);
						b += 2;
					}

					/*
					 *  Send BRATE, CFLAG, IFLAG,
					 *  OFLAG, XFLAG.
					 */

					if (ch->ch_s_brate != ch->ch_brate ||
					    ch->ch_s_cflag != ch->ch_cflag ||
					    ch->ch_s_iflag != ch->ch_iflag ||
					    ch->ch_s_oflag != ch->ch_oflag ||
					    ch->ch_s_xflag != ch->ch_xflag) {
						b = set_cmd_header(b, port, 40);

						ch->ch_s_brate = ch->ch_brate;
						ch->ch_s_cflag = ch->ch_cflag;
						ch->ch_s_iflag = ch->ch_iflag;
						ch->ch_s_oflag = ch->ch_oflag;
						ch->ch_s_xflag = ch->ch_xflag;

						put_unaligned_be16(ch->ch_s_brate,
								   b);
						b += 2;

						put_unaligned_be16(ch->ch_s_cflag,
								   b);
						b += 2;

						put_unaligned_be16(ch->ch_s_iflag,
								   b);
						b += 2;

						put_unaligned_be16(ch->ch_s_oflag,
								   b);
						b += 2;

						put_unaligned_be16(ch->ch_s_xflag,
								   b);
						b += 2;
					}

					/*
					 *  Send MOUT, MFLOW, MCTRL.
					 */

					if (ch->ch_s_mout  != ch->ch_mout  ||
					    ch->ch_s_mflow != ch->ch_mflow ||
					    ch->ch_s_mctrl != ch->ch_mctrl) {
						b = set_cmd_header(b, port, 44);

						*b++ = ch->ch_s_mout  = ch->ch_mout;
						*b++ = ch->ch_s_mflow = ch->ch_mflow;
						*b++ = ch->ch_s_mctrl = ch->ch_mctrl;
					}

					/*
					 *  Send Flow control characters.
					 */

					if (ch->ch_s_xon   != ch->ch_xon   ||
					    ch->ch_s_xoff  != ch->ch_xoff  ||
					    ch->ch_s_lnext != ch->ch_lnext ||
					    ch->ch_s_xxon  != ch->ch_xxon  ||
					    ch->ch_s_xxoff != ch->ch_xxoff) {
						b = set_cmd_header(b, port, 46);

						*b++ = ch->ch_s_xon   = ch->ch_xon;
						*b++ = ch->ch_s_xoff  = ch->ch_xoff;
						*b++ = ch->ch_s_lnext = ch->ch_lnext;
						*b++ = ch->ch_s_xxon  = ch->ch_xxon;
						*b++ = ch->ch_s_xxoff = ch->ch_xxoff;
					}

					/*
					 *  Send RMAX, RTIME.
					 */

					if (ch->ch_s_rmax != ch->ch_rmax ||
					    ch->ch_s_rtime != ch->ch_rtime) {
						b = set_cmd_header(b, port, 47);

						ch->ch_s_rmax  = ch->ch_rmax;
						ch->ch_s_rtime = ch->ch_rtime;

						put_unaligned_be16(ch->ch_s_rmax,
								   b);
						b += 2;

						put_unaligned_be16(ch->ch_s_rtime,
								   b);
						b += 2;
					}

					ch->ch_flag &= ~CH_PARAM;
					wake_up_interruptible(&ch->ch_flag_wait);
				}


				/*
				 *  Handle action commands.
				 */

				if (ch->ch_send != 0) {
					/* int send = ch->ch_send & ~ch->ch_expect; */
					send = ch->ch_send & ~ch->ch_expect;

					/* Send character immediate */
					if ((send & RR_TX_ICHAR) != 0) {
						b = set_cmd_header(b, port, 60);

						*b++ = ch->ch_xon;
						ch->ch_expect |= RR_TX_ICHAR;
					}

					/* BREAK request */
					if ((send & RR_TX_BREAK) != 0) {
						if (ch->ch_break_time != 0) {
							b = set_cmd_header(b, port, 61);
							put_unaligned_be16(ch->ch_break_time,
									   b);
							b += 2;

							ch->ch_expect |= RR_TX_BREAK;
							ch->ch_break_time = 0;
						} else {
							ch->ch_send &= ~RR_TX_BREAK;
							ch->ch_flag &= ~CH_TX_BREAK;
							wake_up_interruptible(&ch->ch_flag_wait);
						}
					}

					/*
					 *  Flush input/output buffers.
					 */

					if ((send & (RR_RX_FLUSH | RR_TX_FLUSH)) != 0) {
						b = set_cmd_header(b, port, 62);

						*b++ = ((send & RR_TX_FLUSH) == 0 ? 1 :
							(send & RR_RX_FLUSH) == 0 ? 2 : 3);

						if (send & RR_RX_FLUSH) {
							ch->ch_flush_seq = nd->nd_seq_in;
							ch->ch_flag |= CH_RX_FLUSH;
							work = 1;
							send_sync = 1;
							wanted_sync_port = port;
						}

						ch->ch_send &= ~(RR_RX_FLUSH | RR_TX_FLUSH);
					}

					/*  Pause input/output */
					if ((send & (RR_RX_STOP | RR_TX_STOP)) != 0) {
						b = set_cmd_header(b, port, 63);
						*b = 0;

						if ((send & RR_TX_STOP) != 0)
							*b |= EV_OPU;

						if ((send & RR_RX_STOP) != 0)
							*b |= EV_IPU;

						b++;

						ch->ch_send &= ~(RR_RX_STOP | RR_TX_STOP);
					}

					/* Start input/output */
					if ((send & (RR_RX_START | RR_TX_START)) != 0) {
						b = set_cmd_header(b, port, 64);
						*b = 0;

						if ((send & RR_TX_START) != 0)
							*b |= EV_OPU | EV_OPS | EV_OPX;

						if ((send & RR_RX_START) != 0)
							*b |= EV_IPU | EV_IPS;

						b++;

						ch->ch_send &= ~(RR_RX_START | RR_TX_START);
					}
				}


				/*
				 *  Send a window sequence to acknowledge received data.
				 */

				rwin = (ch->ch_s_rin +
					((ch->ch_rout - ch->ch_rin - 1) & RBUF_MASK));

				n = (rwin - ch->ch_s_rwin) & 0xffff;

				if (n >= RBUF_MAX / 4) {
					b[0] = 0xa0 + (port & 0xf);
					ch->ch_s_rwin = rwin;
					put_unaligned_be16(rwin, b + 1);
					b += 3;
				}

				/*
				 *  If the terminal is waiting on LOW
				 *  water or EMPTY, and the condition
				 *  is now satisfied, call the line
				 *  discipline to put more data in the
				 *  buffer.
				 */

				n = (ch->ch_tin - ch->ch_tout) & TBUF_MASK;

				if ((ch->ch_tun.un_flag & (UN_EMPTY|UN_LOW)) != 0) {
					if ((ch->ch_tun.un_flag & UN_LOW) != 0 ?
					    (n <= TBUF_LOW) :
					    (n == 0 && ch->ch_s_tpos == ch->ch_s_tin)) {
						ch->ch_tun.un_flag &= ~(UN_EMPTY|UN_LOW);

						if (waitqueue_active(&((ch->ch_tun.un_tty)->write_wait)))
							wake_up_interruptible(&((ch->ch_tun.un_tty)->write_wait));
						tty_wakeup(ch->ch_tun.un_tty);
						n = (ch->ch_tin - ch->ch_tout) & TBUF_MASK;
					}
				}

				/*
				 * If the printer is waiting on LOW
				 * water, TIME, EMPTY or PWAIT, and is
				 * now ready to put more data in the
				 * buffer, call the line discipline to
				 * do the job.
				 */

				if (ch->ch_pun.un_open_count &&
				    (ch->ch_pun.un_flag &
				    (UN_EMPTY|UN_TIME|UN_LOW|UN_PWAIT)) != 0) {

					if ((ch->ch_pun.un_flag & UN_LOW) != 0 ?
					    (n <= TBUF_LOW) :
					    (ch->ch_pun.un_flag & UN_TIME) != 0 ?
					    ((jiffies - ch->ch_waketime) >= 0) :
					    (n == 0 && ch->ch_s_tpos == ch->ch_s_tin) &&
					    ((ch->ch_pun.un_flag & UN_EMPTY) != 0 ||
					    ((ch->ch_tun.un_open_count &&
					      ch->ch_tun.un_tty->ops->chars_in_buffer) ?
					     (ch->ch_tun.un_tty->ops->chars_in_buffer)(ch->ch_tun.un_tty) == 0
					     : 1
					    )
					    )) {
						ch->ch_pun.un_flag &= ~(UN_EMPTY | UN_TIME | UN_LOW | UN_PWAIT);

						if (waitqueue_active(&((ch->ch_pun.un_tty)->write_wait)))
							wake_up_interruptible(&((ch->ch_pun.un_tty)->write_wait));
						tty_wakeup(ch->ch_pun.un_tty);
						n = (ch->ch_tin - ch->ch_tout) & TBUF_MASK;

					} else if ((ch->ch_pun.un_flag & UN_TIME) != 0) {
						work = 1;
					}
				}


				/*
				 *  Determine the max number of bytes
				 *  this port can send, including
				 *  packet header overhead.
				 */

				t = ((ch->ch_s_tsize + ch->ch_s_tpos - ch->ch_s_tin) & 0xffff);

				if (n > t)
					n = t;

				if (n != 0) {
					n += (n <= 8 ? 1 : n <= 255 ? 2 : 3);

					tdata[tchan++] = n;
					ttotal += n;
				}
				break;

			/*
			 *  Close the port.
			 */

send_close:
			case CS_SEND_CLOSE:
				b = set_cmd_header(b, port, 10);
				if (ch->ch_otype == OTYPE_IMMEDIATE)
					*b++ = 3;
				else
					*b++ = 4;

				ch->ch_state = CS_WAIT_CLOSE;
				break;

			/*
			 *  Wait for a previous server request.
			 */

			case CS_WAIT_OPEN:
			case CS_WAIT_CANCEL:
			case CS_WAIT_FAIL:
			case CS_WAIT_QUERY:
			case CS_WAIT_CLOSE:
				break;

			default:
				pr_info("%s - unexpected channel state (%i)\n",
					__func__, ch->ch_state);
			}
		}

		/*
		 *  If a module select code is needed, drop one in.  If space
		 *  was reserved for one, but none is needed, recover the space.
		 */

		if (mod != nd->nd_tx_module) {
			if (b != mbuf) {
				mbuf[-1] = 0xf0 | mod;
				nd->nd_tx_module = mod;
			} else {
				b--;
			}
		}
	}

	/*
	 *  Adjust "tmax" so that under worst case conditions we do
	 *  not overflow either the daemon buffer or the internal
	 *  buffer in the loop that follows.   Leave a safe area
	 *  of 64 bytes so we start getting asserts before we start
	 *  losing data or clobbering memory.
	 */

	n = UIO_MAX - UIO_BASE;

	if (tmax > n)
		tmax = n;

	tmax -= 64;

	tsafe = tmax;

	/*
	 *  Allocate space for 5 Module Selects, 1 Sequence Request,
	 *  and 1 Set TREQ for each active channel.
	 */

	tmax -= 5 + 3 + 4 * nd->nd_chan_count;

	/*
	 *  Further reduce "tmax" to the available transmit credit.
	 *  Note that this is a soft constraint;  The transmit credit
	 *  can go negative for a time and then recover.
	 */

	n = nd->nd_tx_deposit - nd->nd_tx_charge - nd->nd_link.lk_header_size;

	if (tmax > n)
		tmax = n;

	/*
	 *  Finally reduce tmax by the number of bytes already in
	 *  the buffer.
	 */

	tmax -= b - buf;

	/*
	 *  Suspend data transmit unless every ready channel can send
	 *  at least 1 character.
	 */
	if (tmax < 2 * nd->nd_chan_count) {
		tsend = 1;

	} else if (tchan > 1 && ttotal > tmax) {

		/*
		 *  If transmit is limited by the credit budget, find the
		 *  largest number of characters we can send without driving
		 *  the credit negative.
		 */

		long tm = tmax;
		int tc = tchan;
		int try;

		tsend = tm / tc;

		for (try = 0; try < 3; try++) {
			int i;
			int c = 0;

			for (i = 0; i < tc; i++) {
				if (tsend < tdata[i])
					tdata[c++] = tdata[i];
				else
					tm -= tdata[i];
			}

			if (c == tc)
				break;

			tsend = tm / c;

			if (c == 1)
				break;

			tc = c;
		}

		tsend = tm / nd->nd_chan_count;

		if (tsend < 2)
			tsend = 1;

	} else {
		/*
		 *  If no budgetary constraints, or only one channel ready
		 *  to send, set the character limit to the remaining
		 *  buffer size.
		 */

		tsend = tmax;
	}

	tsend -= (tsend <= 9) ? 1 : (tsend <= 257) ? 2 : 3;

	/*
	 *  Loop over all channels, sending queued data.
	 */

	port = 0;
	ch = nd->nd_chan;
	used_buffer = tmax;

	for (mod = 0; port < nd->nd_chan_count; mod++) {
		/*
		 *  If this is not the current module, enter a module select
		 *  code in the buffer.
		 */

		if (mod != nd->nd_tx_module)
			mbuf = ++b;

		/*
		 *  Loop to process one module.
		 */

		maxport = port + 16;

		if (maxport > nd->nd_chan_count)
			maxport = nd->nd_chan_count;

		for (; port < maxport; port++, ch++) {
			if (ch->ch_state != CS_READY)
				continue;

			lastport = port;

			n = (ch->ch_tin - ch->ch_tout) & TBUF_MASK;

			/*
			 *  If there is data that can be sent, send it.
			 */

			if (n != 0 && used_buffer > 0) {
				t = (ch->ch_s_tsize + ch->ch_s_tpos - ch->ch_s_tin) & 0xffff;

				if (n > t)
					n = t;

				if (n > tsend) {
					work = 1;
					n = tsend;
				}

				if (n > used_buffer) {
					work = 1;
					n = used_buffer;
				}

				if (n <= 0)
					continue;

				/*
				 *  Create the correct size transmit header,
				 *  depending on the amount of data to transmit.
				 */

				if (n <= 8) {

					b[0] = ((n - 1) << 4) + (port & 0xf);
					b += 1;

				} else if (n <= 255) {

					b[0] = 0x80 + (port & 0xf);
					b[1] = n;
					b += 2;

				} else {

					b[0] = 0x90 + (port & 0xf);
					put_unaligned_be16(n, b + 1);
					b += 3;
				}

				ch->ch_s_tin = (ch->ch_s_tin + n) & 0xffff;

				/*
				 *  Copy transmit data to the packet.
				 */

				t = TBUF_MAX - ch->ch_tout;

				if (n >= t) {
					memcpy(b, ch->ch_tbuf + ch->ch_tout, t);
					b += t;
					n -= t;
					used_buffer -= t;
					ch->ch_tout = 0;
				}

				memcpy(b, ch->ch_tbuf + ch->ch_tout, n);
				b += n;
				used_buffer -= n;
				ch->ch_tout += n;
				n = (ch->ch_tin - ch->ch_tout) & TBUF_MASK;
			}

			/*
			 *  Wake any terminal unit process waiting in the
			 *  dgrp_write routine for low water.
			 */

			if (n > TBUF_LOW)
				continue;

			if ((ch->ch_flag & CH_LOW) != 0) {
				ch->ch_flag &= ~CH_LOW;
				wake_up_interruptible(&ch->ch_flag_wait);
			}

			/* selwakeup tty_sel */
			if (ch->ch_tun.un_open_count) {
				struct tty_struct *tty = (ch->ch_tun.un_tty);

				if (waitqueue_active(&tty->write_wait))
					wake_up_interruptible(&tty->write_wait);

				tty_wakeup(tty);
			}

			if (ch->ch_pun.un_open_count) {
				struct tty_struct *tty = (ch->ch_pun.un_tty);

				if (waitqueue_active(&tty->write_wait))
					wake_up_interruptible(&tty->write_wait);

				tty_wakeup(tty);
			}

			/*
			 *  Do EMPTY processing.
			 */

			if (n != 0)
				continue;

			if ((ch->ch_flag & (CH_EMPTY | CH_DRAIN)) != 0 ||
			    (ch->ch_pun.un_flag & UN_EMPTY) != 0) {
				/*
				 *  If there is still data in the server, ask the server
				 *  to notify us when its all gone.
				 */

				if (ch->ch_s_treq != ch->ch_s_tin) {
					b = set_cmd_header(b, port, 43);

					ch->ch_s_treq = ch->ch_s_tin;
					put_unaligned_be16(ch->ch_s_treq,
							   b);
					b += 2;
				}

				/*
				 *  If there is a thread waiting for buffer empty,
				 *  and we are truly empty, wake the thread.
				 */

				else if ((ch->ch_flag & CH_EMPTY) != 0 &&
					(ch->ch_send & RR_TX_BREAK) == 0) {
					ch->ch_flag &= ~CH_EMPTY;

					wake_up_interruptible(&ch->ch_flag_wait);
				}
			}
		}

		/*
		 *  If a module select code is needed, drop one in.  If space
		 *  was reserved for one, but none is needed, recover the space.
		 */

		if (mod != nd->nd_tx_module) {
			if (b != mbuf) {
				mbuf[-1] = 0xf0 | mod;
				nd->nd_tx_module = mod;
			} else {
				b--;
			}
		}
	}

	/*
	 *  Send a synchronization sequence associated with the last open
	 *  channel that sent data, and remember the time when the data was
	 *  sent.
	 */

	in = nd->nd_seq_in;

	if ((send_sync || nd->nd_seq_wait[in] != 0) && lastport >= 0) {
		u8 *bb = b;

		/*
		 * Attempt the use the port that really wanted the sync.
		 * This gets around a race condition where the "lastport" is in
		 * the middle of the close() routine, and by the time we
		 * send this command, it will have already acked the close, and
		 * thus not send the sync response.
		 */
		if (wanted_sync_port >= 0)
			lastport = wanted_sync_port;
		/*
		 * Set a flag just in case the port is in the middle of a close,
		 * it will not be permitted to actually close until we get an
		 * sync response, and clear the flag there.
		 */
		ch = nd->nd_chan + lastport;
		ch->ch_flag |= CH_WAITING_SYNC;

		mod = lastport >> 4;

		if (mod != nd->nd_tx_module) {
			bb[0] = 0xf0 + mod;
			bb += 1;

			nd->nd_tx_module = mod;
		}

		bb = set_cmd_header(bb, lastport, 12);
		*bb++ = in;

		nd->nd_seq_size[in] = bb - buf;
		nd->nd_seq_time[in] = jiffies;

		if (++in >= SEQ_MAX)
			in = 0;

		if (in != nd->nd_seq_out) {
			b = bb;
			nd->nd_seq_in = in;
			nd->nd_unack += b - buf;
		}
	}

	/*
	 *  If there are no open ports, a sync cannot be sent.
	 *  There is nothing left to wait for anyway, so wake any
	 *  thread waiting for an acknowledgement.
	 */

	else if (nd->nd_seq_wait[in] != 0) {
		nd->nd_seq_wait[in] = 0;

		wake_up_interruptible(&nd->nd_seq_wque[in]);
	}

	/*
	 *  If there is no traffic for an interval of IDLE_MAX, then
	 *  send a single byte packet.
	 */

	if (b != buf) {
		nd->nd_tx_time = jiffies;
	} else if ((ulong)(jiffies - nd->nd_tx_time) >= IDLE_MAX) {
		*b++ = 0xf0 | nd->nd_tx_module;
		nd->nd_tx_time = jiffies;
	}

	n = b - buf;

	if (n >= tsafe)
		pr_info("%s - n(%i) >= tsafe(%i)\n",
			__func__, n, tsafe);

	if (tsend < 0)
		dgrp_dump(buf, n);

	nd->nd_tx_work = work;

	return n;
}

/*
 * dgrp_net_read()
 * Data to be sent TO the PortServer from the "async." half of the driver.
 */
static ssize_t dgrp_net_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	struct nd_struct *nd;
	long n;
	u8 *local_buf;
	u8 *b;
	ssize_t rtn;

	/*
	 *  Get the node pointer, and quit if it doesn't exist.
	 */
	nd = (struct nd_struct *)(file->private_data);
	if (!nd)
		return -ENXIO;

	if (count < UIO_MIN)
		return -EINVAL;

	/*
	 *  Only one read/write operation may be in progress at
	 *  any given time.
	 */

	/*
	 *  Grab the NET lock.
	 */
	down(&nd->nd_net_semaphore);

	nd->nd_read_count++;

	nd->nd_tx_ready = 0;

	/*
	 *  Determine the effective size of the buffer.
	 */

	if (nd->nd_remain > UIO_BASE)
		pr_info_ratelimited("%s - nd_remain(%i) > UIO_BASE\n",
				    __func__, nd->nd_remain);

	b = local_buf = nd->nd_iobuf + UIO_BASE;

	/*
	 *  Generate data according to the node state.
	 */

	switch (nd->nd_state) {
	/*
	 *  Initialize the connection.
	 */

	case NS_IDLE:
		if (nd->nd_mon_buf)
			dgrp_monitor_reset(nd);

		/*
		 *  Request a Product ID Packet.
		 */

		b[0] = 0xfb;
		b[1] = 0x01;
		b += 2;

		nd->nd_expect |= NR_IDENT;

		/*
		 *  Request a Server Capability ID Response.
		 */

		b[0] = 0xfb;
		b[1] = 0x02;
		b += 2;

		nd->nd_expect |= NR_CAPABILITY;

		/*
		 *  Request a Server VPD Response.
		 */

		b[0] = 0xfb;
		b[1] = 0x18;
		b += 2;

		nd->nd_expect |= NR_VPD;

		nd->nd_state = NS_WAIT_QUERY;
		break;

	/*
	 *  We do serious communication with the server only in
	 *  the READY state.
	 */

	case NS_READY:
		b = dgrp_send(nd, count) + local_buf;
		break;

	/*
	 *  Send off an error after receiving a bogus message
	 *  from the server.
	 */

	case NS_SEND_ERROR:
		n = strlen(nd->nd_error);

		b[0] = 0xff;
		b[1] = n;
		memcpy(b + 2, nd->nd_error, n);
		b += 2 + n;

		dgrp_net_idle(nd);
		/*
		 *  Set the active port count to zero.
		 */
		dgrp_chan_count(nd, 0);
		break;

	default:
		break;
	}

	n = b - local_buf;

	if (n != 0) {
		nd->nd_send_count++;

		nd->nd_tx_byte   += n + nd->nd_link.lk_header_size;
		nd->nd_tx_charge += n + nd->nd_link.lk_header_size;
	}

	rtn = copy_to_user((void __user *)buf, local_buf, n);
	if (rtn) {
		rtn = -EFAULT;
		goto done;
	}

	*ppos += n;

	rtn = n;

	if (nd->nd_mon_buf)
		dgrp_monitor_data(nd, RPDUMP_CLIENT, local_buf, n);

	/*
	 *  Release the NET lock.
	 */
done:
	up(&nd->nd_net_semaphore);

	return rtn;
}

/**
 * dgrp_receive() -- decode data packets received from the remote PortServer.
 * @nd: pointer to a node structure
 */
static void dgrp_receive(struct nd_struct *nd)
{
	struct ch_struct *ch;
	u8 *buf;
	u8 *b;
	u8 *dbuf;
	char *error;
	long port;
	long dlen;
	long plen;
	long remain;
	long n;
	long mlast;
	long elast;
	long mstat;
	long estat;

	char ID[3];

	nd->nd_tx_time = jiffies;

	ID_TO_CHAR(nd->nd_ID, ID);

	b = buf = nd->nd_iobuf;
	remain = nd->nd_remain;

	/*
	 *  Loop to process Realport protocol packets.
	 */

	while (remain > 0) {
		int n0 = b[0] >> 4;
		int n1 = b[0] & 0x0f;

		if (n0 <= 12) {
			port = (nd->nd_rx_module << 4) + n1;

			if (port >= nd->nd_chan_count) {
				error = "Improper Port Number";
				goto prot_error;
			}

			ch = nd->nd_chan + port;
		} else {
			port = -1;
			ch = NULL;
		}

		/*
		 *  Process by major packet type.
		 */

		switch (n0) {

		/*
		 *  Process 1-byte header data packet.
		 */

		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			dlen = n0 + 1;
			plen = dlen + 1;

			dbuf = b + 1;
			goto data;

		/*
		 *  Process 2-byte header data packet.
		 */

		case 8:
			if (remain < 3)
				goto done;

			dlen = b[1];
			plen = dlen + 2;

			dbuf = b + 2;
			goto data;

		/*
		 *  Process 3-byte header data packet.
		 */

		case 9:
			if (remain < 4)
				goto done;

			dlen = get_unaligned_be16(b + 1);
			plen = dlen + 3;

			dbuf = b + 3;

		/*
		 *  Common packet handling code.
		 */

data:
			nd->nd_tx_work = 1;

			/*
			 *  Otherwise data should appear only when we are
			 *  in the CS_READY state.
			 */

			if (ch->ch_state < CS_READY) {
				error = "Data received before RWIN established";
				goto prot_error;
			}

			/*
			 *  Assure that the data received is within the
			 *  allowable window.
			 */

			n = (ch->ch_s_rwin - ch->ch_s_rin) & 0xffff;

			if (dlen > n) {
				error = "Receive data overrun";
				goto prot_error;
			}

			/*
			 *  If we received 3 or less characters,
			 *  assume it is a human typing, and set RTIME
			 *  to 10 milliseconds.
			 *
			 *  If we receive 10 or more characters,
			 *  assume its not a human typing, and set RTIME
			 *  to 100 milliseconds.
			 */

			if (ch->ch_edelay != DGRP_RTIME) {
				if (ch->ch_rtime != ch->ch_edelay) {
					ch->ch_rtime = ch->ch_edelay;
					ch->ch_flag |= CH_PARAM;
				}
			} else if (dlen <= 3) {
				if (ch->ch_rtime != 10) {
					ch->ch_rtime = 10;
					ch->ch_flag |= CH_PARAM;
				}
			} else {
				if (ch->ch_rtime != DGRP_RTIME) {
					ch->ch_rtime = DGRP_RTIME;
					ch->ch_flag |= CH_PARAM;
				}
			}

			/*
			 *  If a portion of the packet is outside the
			 *  buffer, shorten the effective length of the
			 *  data packet to be the amount of data received.
			 */

			if (remain < plen)
				dlen -= plen - remain;

			/*
			 *  Detect if receive flush is now complete.
			 */

			if ((ch->ch_flag & CH_RX_FLUSH) != 0 &&
			    ((ch->ch_flush_seq - nd->nd_seq_out) & SEQ_MASK) >=
			    ((nd->nd_seq_in    - nd->nd_seq_out) & SEQ_MASK)) {
				ch->ch_flag &= ~CH_RX_FLUSH;
			}

			/*
			 *  If we are ready to receive, move the data into
			 *  the receive buffer.
			 */

			ch->ch_s_rin = (ch->ch_s_rin + dlen) & 0xffff;

			if (ch->ch_state == CS_READY &&
			    (ch->ch_tun.un_open_count != 0) &&
			    (ch->ch_tun.un_flag & UN_CLOSING) == 0 &&
			    (ch->ch_cflag & CF_CREAD) != 0 &&
			    (ch->ch_flag & (CH_BAUD0 | CH_RX_FLUSH)) == 0 &&
			    (ch->ch_send & RR_RX_FLUSH) == 0) {

				if (ch->ch_rin + dlen >= RBUF_MAX) {
					n = RBUF_MAX - ch->ch_rin;

					memcpy(ch->ch_rbuf + ch->ch_rin, dbuf, n);

					ch->ch_rin = 0;
					dbuf += n;
					dlen -= n;
				}

				memcpy(ch->ch_rbuf + ch->ch_rin, dbuf, dlen);

				ch->ch_rin += dlen;


				/*
				 *  If we are not in fastcook mode, or
				 *  if there is a fastcook thread
				 *  waiting for data, send the data to
				 *  the line discipline.
				 */

				if ((ch->ch_flag & CH_FAST_READ) == 0 ||
				    ch->ch_inwait != 0) {
					dgrp_input(ch);
				}

				/*
				 *  If there is a read thread waiting
				 *  in select, and we are in fastcook
				 *  mode, wake him up.
				 */

				if (waitqueue_active(&ch->ch_tun.un_tty->read_wait) &&
				    (ch->ch_flag & CH_FAST_READ) != 0)
					wake_up_interruptible(&ch->ch_tun.un_tty->read_wait);

				/*
				 * Wake any thread waiting in the
				 * fastcook loop.
				 */

				if ((ch->ch_flag & CH_INPUT) != 0) {
					ch->ch_flag &= ~CH_INPUT;

					wake_up_interruptible(&ch->ch_flag_wait);
				}
			}

			/*
			 *  Fabricate and insert a data packet header to
			 *  preceed the remaining data when it comes in.
			 */

			if (remain < plen) {
				dlen = plen - remain;
				b = buf;

				b[0] = 0x90 + n1;
				put_unaligned_be16(dlen, b + 1);

				remain = 3;
				goto done;
			}
			break;

		/*
		 *  Handle Window Sequence packets.
		 */

		case 10:
			plen = 3;
			if (remain < plen)
				goto done;

			nd->nd_tx_work = 1;

			{
				ushort tpos   = get_unaligned_be16(b + 1);

				ushort ack    = (tpos          - ch->ch_s_tpos) & 0xffff;
				ushort unack  = (ch->ch_s_tin  - ch->ch_s_tpos) & 0xffff;
				ushort notify = (ch->ch_s_treq - ch->ch_s_tpos) & 0xffff;

				if (ch->ch_state < CS_READY || ack > unack) {
					error = "Improper Window Sequence";
					goto prot_error;
				}

				ch->ch_s_tpos = tpos;

				if (notify <= ack)
					ch->ch_s_treq = tpos;
			}
			break;

		/*
		 *  Handle Command response packets.
		 */

		case 11:

			/*
			 * RealPort engine fix - 03/11/2004
			 *
			 * This check did not used to be here.
			 *
			 * We were using b[1] without verifying that the data
			 * is actually there and valid. On a split packet, it
			 * might not be yet.
			 *
			 * NOTE:  I have never actually seen the failure happen
			 *        under Linux,  but since I have seen it occur
			 *        under both Solaris and HP-UX,  the assumption
			 *        is that it *could* happen here as well...
			 */
			if (remain < 2)
				goto done;


			switch (b[1]) {

			/*
			 *  Handle Open Response.
			 */

			case 11:
				plen = 6;
				if (remain < plen)
					goto done;

				nd->nd_tx_work = 1;

				{
					int req = b[2];
					int resp = b[3];
					port = get_unaligned_be16(b + 4);

					if (port >= nd->nd_chan_count) {
						error = "Open channel number out of range";
						goto prot_error;
					}

					ch = nd->nd_chan + port;

					/*
					 *  How we handle an open response depends primarily
					 *  on our current channel state.
					 */

					switch (ch->ch_state) {
					case CS_IDLE:

						/*
						 *  Handle a delayed open.
						 */

						if (ch->ch_otype_waiting != 0 &&
						    req == ch->ch_otype_waiting &&
						    resp == 0) {
							ch->ch_otype = req;
							ch->ch_otype_waiting = 0;
							ch->ch_state = CS_SEND_QUERY;
							break;
						}
						goto open_error;

					case CS_WAIT_OPEN:

						/*
						 *  Handle the open response.
						 */

						if (req == ch->ch_otype) {
							switch (resp) {

							/*
							 *  On successful response, open the
							 *  port and proceed normally.
							 */

							case 0:
								ch->ch_state = CS_SEND_QUERY;
								break;

							/*
							 *  On a busy response to a persistent open,
							 *  remember that the open is pending.
							 */

							case 1:
							case 2:
								if (req != OTYPE_IMMEDIATE) {
									ch->ch_otype_waiting = req;
									ch->ch_state = CS_IDLE;
									break;
								}

							/*
							 *  Otherwise the server open failed.  If
							 *  the Unix port is open, hang it up.
							 */

							default:
								if (ch->ch_open_count != 0) {
									ch->ch_flag |= CH_HANGUP;
									dgrp_carrier(ch);
									ch->ch_state = CS_IDLE;
									break;
								}

								ch->ch_open_error = resp;
								ch->ch_state = CS_IDLE;

								wake_up_interruptible(&ch->ch_flag_wait);
							}
							break;
						}

						/*
						 *  Handle delayed response arrival preceeding
						 *  the open response we are waiting for.
						 */

						if (ch->ch_otype_waiting != 0 &&
						    req == ch->ch_otype_waiting &&
						    resp == 0) {
							ch->ch_otype = ch->ch_otype_waiting;
							ch->ch_otype_waiting = 0;
							ch->ch_state = CS_WAIT_FAIL;
							break;
						}
						goto open_error;


					case CS_WAIT_FAIL:

						/*
						 *  Handle response to immediate open arriving
						 *  after a delayed open success.
						 */

						if (req == OTYPE_IMMEDIATE) {
							ch->ch_state = CS_SEND_QUERY;
							break;
						}
						goto open_error;


					case CS_WAIT_CANCEL:
						/*
						 *  Handle delayed open response arriving before
						 *  the cancel response.
						 */

						if (req == ch->ch_otype_waiting &&
						    resp == 0) {
							ch->ch_otype_waiting = 0;
							break;
						}

						/*
						 *  Handle cancel response.
						 */

						if (req == 4 && resp == 0) {
							ch->ch_otype_waiting = 0;
							ch->ch_state = CS_IDLE;
							break;
						}
						goto open_error;


					case CS_WAIT_CLOSE:
						/*
						 *  Handle a successful response to a port
						 *  close.
						 */

						if (req >= 3) {
							ch->ch_state = CS_IDLE;
							break;
						}
						goto open_error;

open_error:
					default:
						{
							error = "Improper Open Response";
							goto prot_error;
						}
					}
				}
				break;

			/*
			 *  Handle Synchronize Response.
			 */

			case 13:
				plen = 3;
				if (remain < plen)
					goto done;
				{
					int seq = b[2];
					int s;

					/*
					 * If channel was waiting for this sync response,
					 * unset the flag, and wake up anyone waiting
					 * on the event.
					 */
					if (ch->ch_flag & CH_WAITING_SYNC) {
						ch->ch_flag &= ~(CH_WAITING_SYNC);
						wake_up_interruptible(&ch->ch_flag_wait);
					}

					if (((seq - nd->nd_seq_out) & SEQ_MASK) >=
					    ((nd->nd_seq_in - nd->nd_seq_out) & SEQ_MASK)) {
						break;
					}

					for (s = nd->nd_seq_out;; s = (s + 1) & SEQ_MASK) {
						if (nd->nd_seq_wait[s] != 0) {
							nd->nd_seq_wait[s] = 0;

							wake_up_interruptible(&nd->nd_seq_wque[s]);
						}

						nd->nd_unack -= nd->nd_seq_size[s];

						if (s == seq)
							break;
					}

					nd->nd_seq_out = (seq + 1) & SEQ_MASK;
				}
				break;

			/*
			 *  Handle Sequence Response.
			 */

			case 15:
				plen = 6;
				if (remain < plen)
					goto done;

				{
				/* Record that we have received the Sequence
				 * Response, but we aren't interested in the
				 * sequence numbers.  We were using RIN like it
				 * was ROUT and that was causing problems,
				 * fixed 7-13-2001 David Fries. See comment in
				 * drp.h for ch_s_rin variable.
					int rin = get_unaligned_be16(b + 2);
					int tpos = get_unaligned_be16(b + 4);
				*/

					ch->ch_send   &= ~RR_SEQUENCE;
					ch->ch_expect &= ~RR_SEQUENCE;
				}
				goto check_query;

			/*
			 *  Handle Status Response.
			 */

			case 17:
				plen = 5;
				if (remain < plen)
					goto done;

				{
					ch->ch_s_elast = get_unaligned_be16(b + 2);
					ch->ch_s_mlast = b[4];

					ch->ch_expect &= ~RR_STATUS;
					ch->ch_send   &= ~RR_STATUS;

					/*
					 *  CH_PHYS_CD is cleared because something _could_ be
					 *  waiting for the initial sense of carrier... and if
					 *  carrier is high immediately, we want to be sure to
					 *  wake them as soon as possible.
					 */
					ch->ch_flag &= ~CH_PHYS_CD;

					dgrp_carrier(ch);
				}
				goto check_query;

			/*
			 *  Handle Line Error Response.
			 */

			case 19:
				plen = 14;
				if (remain < plen)
					goto done;

				break;

			/*
			 *  Handle Buffer Response.
			 */

			case 21:
				plen = 6;
				if (remain < plen)
					goto done;

				{
					ch->ch_s_rsize = get_unaligned_be16(b + 2);
					ch->ch_s_tsize = get_unaligned_be16(b + 4);

					ch->ch_send   &= ~RR_BUFFER;
					ch->ch_expect &= ~RR_BUFFER;
				}
				goto check_query;

			/*
			 *  Handle Port Capability Response.
			 */

			case 23:
				plen = 32;
				if (remain < plen)
					goto done;

				{
					ch->ch_send   &= ~RR_CAPABILITY;
					ch->ch_expect &= ~RR_CAPABILITY;
				}

			/*
			 *  When all queries are complete, set those parameters
			 *  derived from the query results, then transition
			 *  to the READY state.
			 */

check_query:
				if (ch->ch_state == CS_WAIT_QUERY &&
				    (ch->ch_expect & (RR_SEQUENCE |
							RR_STATUS |
							RR_BUFFER |
							RR_CAPABILITY)) == 0) {
					ch->ch_tmax  = ch->ch_s_tsize / 4;

					if (ch->ch_edelay == DGRP_TTIME)
						ch->ch_ttime = DGRP_TTIME;
					else
						ch->ch_ttime = ch->ch_edelay;

					ch->ch_rmax = ch->ch_s_rsize / 4;

					if (ch->ch_edelay == DGRP_RTIME)
						ch->ch_rtime = DGRP_RTIME;
					else
						ch->ch_rtime = ch->ch_edelay;

					ch->ch_rlow  = 2 * ch->ch_s_rsize / 8;
					ch->ch_rhigh = 6 * ch->ch_s_rsize / 8;

					ch->ch_state = CS_READY;

					nd->nd_tx_work = 1;
					wake_up_interruptible(&ch->ch_flag_wait);

				}
				break;

			default:
				goto decode_error;
			}
			break;

		/*
		 *  Handle Events.
		 */

		case 12:
			plen = 4;
			if (remain < plen)
				goto done;

			mlast = ch->ch_s_mlast;
			elast = ch->ch_s_elast;

			mstat = ch->ch_s_mlast = b[1];
			estat = ch->ch_s_elast = get_unaligned_be16(b + 2);

			/*
			 *  Handle modem changes.
			 */

			if (((mstat ^ mlast) & DM_CD) != 0)
				dgrp_carrier(ch);


			/*
			 *  Handle received break.
			 */

			if ((estat & ~elast & EV_RXB) != 0 &&
			    (ch->ch_tun.un_open_count != 0) &&
			    I_BRKINT(ch->ch_tun.un_tty) &&
			    !(I_IGNBRK(ch->ch_tun.un_tty))) {

				tty_buffer_request_room(ch->ch_tun.un_tty, 1);
				tty_insert_flip_char(ch->ch_tun.un_tty, 0, TTY_BREAK);
				tty_flip_buffer_push(ch->ch_tun.un_tty);

			}

			/*
			 *  On transmit break complete, if more break traffic
			 *  is waiting then send it.  Otherwise wake any threads
			 *  waiting for transmitter empty.
			 */

			if ((~estat & elast & EV_TXB) != 0 &&
			    (ch->ch_expect & RR_TX_BREAK) != 0) {

				nd->nd_tx_work = 1;

				ch->ch_expect &= ~RR_TX_BREAK;

				if (ch->ch_break_time != 0) {
					ch->ch_send |= RR_TX_BREAK;
				} else {
					ch->ch_send &= ~RR_TX_BREAK;
					ch->ch_flag &= ~CH_TX_BREAK;
					wake_up_interruptible(&ch->ch_flag_wait);
				}
			}
			break;

		case 13:
		case 14:
			error = "Unrecognized command";
			goto prot_error;

		/*
		 *  Decode Special Codes.
		 */

		case 15:
			switch (n1) {
			/*
			 *  One byte module select.
			 */

			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
			case 5:
			case 6:
			case 7:
				plen = 1;
				nd->nd_rx_module = n1;
				break;

			/*
			 *  Two byte module select.
			 */

			case 8:
				plen = 2;
				if (remain < plen)
					goto done;

				nd->nd_rx_module = b[1];
				break;

			/*
			 *  ID Request packet.
			 */

			case 11:
				if (remain < 4)
					goto done;

				plen = get_unaligned_be16(b + 2);

				if (plen < 12 || plen > 1000) {
					error = "Response Packet length error";
					goto prot_error;
				}

				nd->nd_tx_work = 1;

				switch (b[1]) {
				/*
				 *  Echo packet.
				 */

				case 0:
					nd->nd_send |= NR_ECHO;
					break;

				/*
				 *  ID Response packet.
				 */

				case 1:
					nd->nd_send |= NR_IDENT;
					break;

				/*
				 *  ID Response packet.
				 */

				case 32:
					nd->nd_send |= NR_PASSWORD;
					break;

				}
				break;

			/*
			 *  Various node-level response packets.
			 */

			case 12:
				if (remain < 4)
					goto done;

				plen = get_unaligned_be16(b + 2);

				if (plen < 4 || plen > 1000) {
					error = "Response Packet length error";
					goto prot_error;
				}

				nd->nd_tx_work = 1;

				switch (b[1]) {
				/*
				 *  Echo packet.
				 */

				case 0:
					nd->nd_expect &= ~NR_ECHO;
					break;

				/*
				 *  Product Response Packet.
				 */

				case 1:
					{
						int desclen;

						nd->nd_hw_ver = (b[8] << 8) | b[9];
						nd->nd_sw_ver = (b[10] << 8) | b[11];
						nd->nd_hw_id = b[6];
						desclen = ((plen - 12) > MAX_DESC_LEN) ? MAX_DESC_LEN :
							plen - 12;

						if (desclen <= 0) {
							error = "Response Packet desclen error";
							goto prot_error;
						}

						strncpy(nd->nd_ps_desc, b + 12, desclen);
						nd->nd_ps_desc[desclen] = 0;
					}

					nd->nd_expect &= ~NR_IDENT;
					break;

				/*
				 *  Capability Response Packet.
				 */

				case 2:
					{
						int nn = get_unaligned_be16(b + 4);

						if (nn > CHAN_MAX)
							nn = CHAN_MAX;

						dgrp_chan_count(nd, nn);
					}

					nd->nd_expect &= ~NR_CAPABILITY;
					break;

				/*
				 *  VPD Response Packet.
				 */

				case 15:
					/*
					 * NOTE: case 15 is here ONLY because the EtherLite
					 * is broken, and sends a response to 24 back as 15.
					 * To resolve this, the EtherLite firmware is now
					 * fixed to send back 24 correctly, but, for backwards
					 * compatibility, we now have reserved 15 for the
					 * bad EtherLite response to 24 as well.
					 */

					/* Fallthru! */

				case 24:

					/*
					 * If the product doesn't support VPD,
					 * it will send back a null IDRESP,
					 * which is a length of 4 bytes.
					 */
					if (plen > 4) {
						memcpy(nd->nd_vpd, b + 4, min(plen - 4, (long) VPDSIZE));
						nd->nd_vpd_len = min(plen - 4, (long) VPDSIZE);
					}

					nd->nd_expect &= ~NR_VPD;
					break;

				default:
					goto decode_error;
				}

				if (nd->nd_expect == 0 &&
				    nd->nd_state == NS_WAIT_QUERY) {
					nd->nd_state = NS_READY;
				}
				break;

			/*
			 *  Debug packet.
			 */

			case 14:
				if (remain < 4)
					goto done;

				plen = get_unaligned_be16(b + 2) + 4;

				if (plen > 1000) {
					error = "Debug Packet too large";
					goto prot_error;
				}

				if (remain < plen)
					goto done;
				break;

			/*
			 *  Handle reset packet.
			 */

			case 15:
				if (remain < 2)
					goto done;

				plen = 2 + b[1];

				if (remain < plen)
					goto done;

				nd->nd_tx_work = 1;

				n = b[plen];
				b[plen] = 0;

				b[plen] = n;

				error = "Client Reset Acknowledge";
				goto prot_error;

			default:
				goto decode_error;
			}
			break;

		default:
			goto decode_error;
		}

		b += plen;
		remain -= plen;
	}

	/*
	 *  When the buffer is exhausted, copy any data left at the
	 *  top of the buffer back down to the bottom for the next
	 *  read request.
	 */

done:
	if (remain > 0 && b != buf)
		memcpy(buf, b, remain);

	nd->nd_remain = remain;
	return;

/*
 *  Handle a decode error.
 */

decode_error:
	error = "Protocol decode error";

/*
 *  Handle a general protocol error.
 */

prot_error:
	nd->nd_remain = 0;
	nd->nd_state = NS_SEND_ERROR;
	nd->nd_error = error;
}

/*
 * dgrp_net_write() -- write data to the network device.
 *
 * A zero byte write indicates that the connection to the RealPort
 * device has been broken.
 *
 * A non-zero write indicates data from the RealPort device.
 */
static ssize_t dgrp_net_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct nd_struct *nd;
	ssize_t rtn = 0;
	long n;
	long total = 0;

	/*
	 *  Get the node pointer, and quit if it doesn't exist.
	 */
	nd = (struct nd_struct *)(file->private_data);
	if (!nd)
		return -ENXIO;

	/*
	 *  Grab the NET lock.
	 */
	down(&nd->nd_net_semaphore);

	nd->nd_write_count++;

	/*
	 *  Handle disconnect.
	 */

	if (count == 0) {
		dgrp_net_idle(nd);
		/*
		 *  Set the active port count to zero.
		 */
		dgrp_chan_count(nd, 0);
		goto unlock;
	}

	/*
	 *  Loop to process entire receive packet.
	 */

	while (count > 0) {
		n = UIO_MAX - nd->nd_remain;

		if (n > count)
			n = count;

		nd->nd_rx_byte += n + nd->nd_link.lk_header_size;

		rtn = copy_from_user(nd->nd_iobuf + nd->nd_remain,
				     (void __user *) buf + total, n);
		if (rtn) {
			rtn = -EFAULT;
			goto unlock;
		}

		*ppos += n;

		total += n;

		count -= n;

		if (nd->nd_mon_buf)
			dgrp_monitor_data(nd, RPDUMP_SERVER,
					  nd->nd_iobuf + nd->nd_remain, n);

		nd->nd_remain += n;

		dgrp_receive(nd);
	}

	rtn = total;

unlock:
	/*
	 *  Release the NET lock.
	 */
	up(&nd->nd_net_semaphore);

	return rtn;
}


/*
 * dgrp_net_select()
 *  Determine whether a device is ready to be read or written to, and
 *  sleep if not.
 */
static unsigned int dgrp_net_select(struct file *file,
				    struct poll_table_struct *table)
{
	unsigned int retval = 0;
	struct nd_struct *nd = file->private_data;

	poll_wait(file, &nd->nd_tx_waitq, table);

	if (nd->nd_tx_ready)
		retval |= POLLIN | POLLRDNORM; /* Conditionally readable */

	retval |= POLLOUT | POLLWRNORM;        /* Always writeable */

	return retval;
}

/*
 * dgrp_net_ioctl
 *
 * Implement those functions which allow the network daemon to control
 * the network parameters in the driver.  The ioctls include ones to
 * get and set the link speed parameters for the PortServer.
 */
static long dgrp_net_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct nd_struct  *nd;
	int    rtn = 0;
	long   size = _IOC_SIZE(cmd);
	struct link_struct link;

	nd = file->private_data;

	if (_IOC_DIR(cmd) & _IOC_READ)
		rtn = access_ok(VERIFY_WRITE, (void __user *) arg, size);
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		rtn = access_ok(VERIFY_READ,  (void __user *) arg, size);

	if (!rtn)
		return rtn;

	switch (cmd) {
	case DIGI_SETLINK:
		if (size != sizeof(struct link_struct))
			return -EINVAL;

		if (copy_from_user((void *)(&link), (void __user *) arg, size))
			return -EFAULT;

		if (link.lk_fast_rate < 9600)
			link.lk_fast_rate = 9600;

		if (link.lk_slow_rate < 2400)
			link.lk_slow_rate = 2400;

		if (link.lk_fast_rate > 10000000)
			link.lk_fast_rate = 10000000;

		if (link.lk_slow_rate > link.lk_fast_rate)
			link.lk_slow_rate = link.lk_fast_rate;

		if (link.lk_fast_delay > 2000)
			link.lk_fast_delay = 2000;

		if (link.lk_slow_delay > 10000)
			link.lk_slow_delay = 10000;

		if (link.lk_fast_delay < 60)
			link.lk_fast_delay = 60;

		if (link.lk_slow_delay < link.lk_fast_delay)
			link.lk_slow_delay = link.lk_fast_delay;

		if (link.lk_header_size < 2)
			link.lk_header_size = 2;

		if (link.lk_header_size > 128)
			link.lk_header_size = 128;

		link.lk_fast_rate /= 8 * 1000 / dgrp_poll_tick;
		link.lk_slow_rate /= 8 * 1000 / dgrp_poll_tick;

		link.lk_fast_delay /= dgrp_poll_tick;
		link.lk_slow_delay /= dgrp_poll_tick;

		nd->nd_link = link;

		break;

	case DIGI_GETLINK:
		if (size != sizeof(struct link_struct))
			return -EINVAL;

		if (copy_to_user((void __user *)arg, (void *)(&nd->nd_link),
				 size))
			return -EFAULT;

		break;

	default:
		return -EINVAL;

	}

	return 0;
}

/**
 * dgrp_poll_handler() -- handler for poll timer
 *
 * As each timer expires, it determines (a) whether the "transmit"
 * waiter needs to be woken up, and (b) whether the poller needs to
 * be rescheduled.
 */
void dgrp_poll_handler(unsigned long arg)
{
	struct dgrp_poll_data *poll_data;
	struct nd_struct *nd;
	struct link_struct *lk;
	ulong time;
	ulong poll_time;
	ulong freq;
	ulong lock_flags;

	poll_data = (struct dgrp_poll_data *) arg;
	freq = 1000 / poll_data->poll_tick;
	poll_data->poll_round += 17;

	if (poll_data->poll_round >= freq)
		poll_data->poll_round -= freq;

	/*
	 * Loop to process all open nodes.
	 *
	 * For each node, determine the rate at which it should
	 * be transmitting data.  Then if the node should wake up
	 * and transmit data now, enable the net receive select
	 * to get the transmit going.
	 */

	list_for_each_entry(nd, &nd_struct_list, list) {

		lk = &nd->nd_link;

		/*
		 * Decrement statistics.  These are only for use with
		 * KME, so don't worry that the operations are done
		 * unlocked, and so the results are occassionally wrong.
		 */

		nd->nd_read_count -= (nd->nd_read_count +
				      poll_data->poll_round) / freq;
		nd->nd_write_count -= (nd->nd_write_count +
				       poll_data->poll_round) / freq;
		nd->nd_send_count -= (nd->nd_send_count +
				      poll_data->poll_round) / freq;
		nd->nd_tx_byte -= (nd->nd_tx_byte +
				   poll_data->poll_round) / freq;
		nd->nd_rx_byte -= (nd->nd_rx_byte +
				   poll_data->poll_round) / freq;

		/*
		 * Wake the daemon to transmit data only when there is
		 * enough byte credit to send data.
		 *
		 * The results are approximate because the operations
		 * are performed unlocked, and we are inspecting
		 * data asynchronously updated elsewhere.  The whole
		 * thing is just approximation anyway, so that should
		 * be okay.
		 */

		if (lk->lk_slow_rate >= UIO_MAX) {

			nd->nd_delay = 0;
			nd->nd_rate = UIO_MAX;

			nd->nd_tx_deposit = nd->nd_tx_charge + 3 * UIO_MAX;
			nd->nd_tx_credit  = 3 * UIO_MAX;

		} else {

			long rate;
			long delay;
			long deposit;
			long charge;
			long size;
			long excess;

			long seq_in = nd->nd_seq_in;
			long seq_out = nd->nd_seq_out;

			/*
			 * If there are no outstanding packets, run at the
			 * fastest rate.
			 */

			if (seq_in == seq_out) {
				delay = 0;
				rate = lk->lk_fast_rate;
			}

			/*
			 * Otherwise compute the transmit rate based on the
			 * delay since the oldest packet.
			 */

			else {
				/*
				 * The actual delay is computed as the
				 * time since the oldest unacknowledged
				 * packet was sent, minus the time it
				 * took to send that packet to the server.
				 */

				delay = ((jiffies - nd->nd_seq_time[seq_out])
					- (nd->nd_seq_size[seq_out] /
					lk->lk_fast_rate));

				/*
				 * If the delay is less than the "fast"
				 * delay, transmit full speed.  If greater
				 * than the "slow" delay, transmit at the
				 * "slow" speed.   In between, interpolate
				 * between the fast and slow speeds.
				 */

				rate =
				  (delay <= lk->lk_fast_delay ?
				    lk->lk_fast_rate :
				    delay >= lk->lk_slow_delay ?
				      lk->lk_slow_rate :
				      (lk->lk_slow_rate +
				       (lk->lk_slow_delay - delay) *
				       (lk->lk_fast_rate - lk->lk_slow_rate) /
				       (lk->lk_slow_delay - lk->lk_fast_delay)
				      )
				  );
			}

			nd->nd_delay = delay;
			nd->nd_rate = rate;

			/*
			 * Increase the transmit credit by depositing the
			 * current transmit rate.
			 */

			deposit = nd->nd_tx_deposit;
			charge  = nd->nd_tx_charge;

			deposit += rate;

			/*
			 * If the available transmit credit becomes too large,
			 * reduce the deposit to correct the value.
			 *
			 * Too large is the max of:
			 *		6 times the header size
			 *		3 times the current transmit rate.
			 */

			size = 2 * nd->nd_link.lk_header_size;

			if (size < rate)
				size = rate;

			size *= 3;

			excess = deposit - charge - size;

			if (excess > 0)
				deposit -= excess;

			nd->nd_tx_deposit = deposit;
			nd->nd_tx_credit  = deposit - charge;

			/*
			 * Wake the transmit task only if the transmit credit
			 * is at least 3 times the transmit header size.
			 */

			size = 3 * lk->lk_header_size;

			if (nd->nd_tx_credit < size)
				continue;
		}


		/*
		 * Enable the READ select to wake the daemon if there
		 * is useful work for the drp_read routine to perform.
		 */

		if (waitqueue_active(&nd->nd_tx_waitq) &&
		    (nd->nd_tx_work != 0 ||
		    (ulong)(jiffies - nd->nd_tx_time) >= IDLE_MAX)) {
			nd->nd_tx_ready = 1;

			wake_up_interruptible(&nd->nd_tx_waitq);

			/* not needed */
			/* nd->nd_flag &= ~ND_SELECT; */
		}
	}


	/*
	 * Schedule ourself back at the nominal wakeup interval.
	 */
	spin_lock_irqsave(&poll_data->poll_lock, lock_flags);

	poll_data->node_active_count--;
	if (poll_data->node_active_count > 0) {
		poll_data->node_active_count++;
		poll_time = poll_data->timer.expires +
			poll_data->poll_tick * HZ / 1000;

		time = poll_time - jiffies;

		if (time >= 2 * poll_data->poll_tick)
			poll_time = jiffies + dgrp_poll_tick * HZ / 1000;

		poll_data->timer.expires = poll_time;
		add_timer(&poll_data->timer);
	}

	spin_unlock_irqrestore(&poll_data->poll_lock, lock_flags);
}

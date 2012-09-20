/*
 *
 * Copyright 1999-2000 Digi International (www.digi.com)
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 *
 *  Filename:
 *
 *     dgrp_ports_ops.c
 *
 *  Description:
 *
 *     Handle the file operations required for the /proc/dgrp/ports/...
 *     devices.  Basically gathers tty status for the node and returns it.
 *
 *  Author:
 *
 *     James A. Puzzo
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include "dgrp_common.h"

/* File operation declarations */
static int dgrp_ports_open(struct inode *, struct file *);

static const struct file_operations ports_ops = {
	.owner   = THIS_MODULE,
	.open    = dgrp_ports_open,
	.read    = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release
};

static struct inode_operations ports_inode_ops = {
	.permission = dgrp_inode_permission
};


void dgrp_register_ports_hook(struct proc_dir_entry *de)
{
	struct nd_struct *node = de->data;

	de->proc_iops = &ports_inode_ops;
	de->proc_fops = &ports_ops;
	node->nd_ports_de = de;
}

static void *dgrp_ports_seq_start(struct seq_file *seq, loff_t *pos)
{
	if (*pos == 0)
		seq_puts(seq, "#num tty_open pr_open tot_wait MSTAT  IFLAG  OFLAG  CFLAG  BPS    DIGIFLAGS\n");

	return pos;
}

static void *dgrp_ports_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct nd_struct *nd = seq->private;

	if (*pos >= nd->nd_chan_count)
		return NULL;

	*pos += 1;

	return pos;
}

static void dgrp_ports_seq_stop(struct seq_file *seq, void *v)
{
}

static int dgrp_ports_seq_show(struct seq_file *seq, void *v)
{
	loff_t *pos = v;
	struct nd_struct *nd;
	struct ch_struct *ch;
	struct un_struct *tun, *pun;
	unsigned int totcnt;

	nd = seq->private;
	if (!nd)
		return 0;

	if (*pos >= nd->nd_chan_count)
		return 0;

	ch = &nd->nd_chan[*pos];
	tun = &ch->ch_tun;
	pun = &ch->ch_pun;

	/*
	 * If port is not open and no one is waiting to
	 * open it, the modem signal values can't be
	 * trusted, and will be zeroed.
	 */
	totcnt = tun->un_open_count +
		pun->un_open_count +
		ch->ch_wait_count[0] +
		ch->ch_wait_count[1] +
		ch->ch_wait_count[2];

	seq_printf(seq, "%02d      %02d      %02d      %02d     0x%04X 0x%04X 0x%04X 0x%04X %-6d 0x%04X\n",
		   (int) *pos,
		   tun->un_open_count,
		   pun->un_open_count,
		   ch->ch_wait_count[0] +
		   ch->ch_wait_count[1] +
		   ch->ch_wait_count[2],
		   (totcnt ? ch->ch_s_mlast : 0),
		   ch->ch_s_iflag,
		   ch->ch_s_oflag,
		   ch->ch_s_cflag,
		   (ch->ch_s_brate ? (1843200 / ch->ch_s_brate) : 0),
		   ch->ch_digi.digi_flags);

	return 0;
}

static const struct seq_operations ports_seq_ops = {
	.start = dgrp_ports_seq_start,
	.next  = dgrp_ports_seq_next,
	.stop  = dgrp_ports_seq_stop,
	.show  = dgrp_ports_seq_show,
};

/**
 * dgrp_ports_open -- open the /proc/dgrp/ports/... device
 * @inode: struct inode *
 * @file: struct file *
 *
 * Open function to open the /proc/dgrp/ports device for a PortServer.
 * This is the open function for struct file_operations
 */
static int dgrp_ports_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int rtn;

	rtn = seq_open(file, &ports_seq_ops);
	if (!rtn) {
		seq = file->private_data;
		seq->private = PDE(inode)->data;
	}

	return rtn;
}

/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: ipoib_fs.c 1389 2004-12-27 22:56:47Z roland $
 */

#include <linux/err.h>
#include <linux/seq_file.h>

struct file_operations;

#include <linux/debugfs.h>

#include "ipoib.h"

static struct dentry *ipoib_root;

static void *ipoib_mcg_seq_start(struct seq_file *file, loff_t *pos)
{
	struct ipoib_mcast_iter *iter;
	loff_t n = *pos;

	iter = ipoib_mcast_iter_init(file->private);
	if (!iter)
		return NULL;

	while (n--) {
		if (ipoib_mcast_iter_next(iter)) {
			ipoib_mcast_iter_free(iter);
			return NULL;
		}
	}

	return iter;
}

static void *ipoib_mcg_seq_next(struct seq_file *file, void *iter_ptr,
				   loff_t *pos)
{
	struct ipoib_mcast_iter *iter = iter_ptr;

	(*pos)++;

	if (ipoib_mcast_iter_next(iter)) {
		ipoib_mcast_iter_free(iter);
		return NULL;
	}

	return iter;
}

static void ipoib_mcg_seq_stop(struct seq_file *file, void *iter_ptr)
{
	/* nothing for now */
}

static int ipoib_mcg_seq_show(struct seq_file *file, void *iter_ptr)
{
	struct ipoib_mcast_iter *iter = iter_ptr;
	char gid_buf[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"];
	union ib_gid mgid;
	int i, n;
	unsigned long created;
	unsigned int queuelen, complete, send_only;

	if (iter) {
		ipoib_mcast_iter_read(iter, &mgid, &created, &queuelen,
				      &complete, &send_only);

		for (n = 0, i = 0; i < sizeof mgid / 2; ++i) {
			n += sprintf(gid_buf + n, "%x",
				     be16_to_cpu(((__be16 *) mgid.raw)[i]));
			if (i < sizeof mgid / 2 - 1)
				gid_buf[n++] = ':';
		}
	}

	seq_printf(file, "GID: %*s", -(1 + (int) sizeof gid_buf), gid_buf);

	seq_printf(file,
		   " created: %10ld queuelen: %4d complete: %d send_only: %d\n",
		   created, queuelen, complete, send_only);

	return 0;
}

static struct seq_operations ipoib_seq_ops = {
	.start = ipoib_mcg_seq_start,
	.next  = ipoib_mcg_seq_next,
	.stop  = ipoib_mcg_seq_stop,
	.show  = ipoib_mcg_seq_show,
};

static int ipoib_mcg_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &ipoib_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->u.generic_ip;

	return 0;
}

static struct file_operations ipoib_fops = {
	.owner   = THIS_MODULE,
	.open    = ipoib_mcg_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

int ipoib_create_debug_file(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	char name[IFNAMSIZ + sizeof "_mcg"];

	snprintf(name, sizeof name, "%s_mcg", dev->name);

	priv->mcg_dentry = debugfs_create_file(name, S_IFREG | S_IRUGO,
					       ipoib_root, dev, &ipoib_fops);

	return priv->mcg_dentry ? 0 : -ENOMEM;
}

void ipoib_delete_debug_file(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	if (priv->mcg_dentry)
		debugfs_remove(priv->mcg_dentry);
}

int ipoib_register_debugfs(void)
{
	ipoib_root = debugfs_create_dir("ipoib", NULL);
	return ipoib_root ? 0 : -ENOMEM;
}

void ipoib_unregister_debugfs(void)
{
	debugfs_remove(ipoib_root);
}

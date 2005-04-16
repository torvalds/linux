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

#include <linux/pagemap.h>
#include <linux/seq_file.h>

#include "ipoib.h"

enum {
	IPOIB_MAGIC = 0x49504942 /* "IPIB" */
};

static DECLARE_MUTEX(ipoib_fs_mutex);
static struct dentry *ipoib_root;
static struct super_block *ipoib_sb;
static LIST_HEAD(ipoib_device_list);

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
				     be16_to_cpu(((u16 *)mgid.raw)[i]));
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

static struct inode *ipoib_get_inode(void)
{
	struct inode *inode = new_inode(ipoib_sb);

	if (inode) {
		inode->i_mode 	 = S_IFREG | S_IRUGO;
		inode->i_uid 	 = 0;
		inode->i_gid 	 = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks  = 0;
		inode->i_atime 	 = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_fop     = &ipoib_fops;
	}

	return inode;
}

static int __ipoib_create_debug_file(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct dentry *dentry;
	struct inode *inode;
	char name[IFNAMSIZ + sizeof "_mcg"];

	snprintf(name, sizeof name, "%s_mcg", dev->name);

	dentry = d_alloc_name(ipoib_root, name);
	if (!dentry)
		return -ENOMEM;

	inode = ipoib_get_inode();
	if (!inode) {
		dput(dentry);
		return -ENOMEM;
	}

	inode->u.generic_ip = dev;
	priv->mcg_dentry = dentry;

	d_add(dentry, inode);

	return 0;
}

int ipoib_create_debug_file(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	down(&ipoib_fs_mutex);

	list_add_tail(&priv->fs_list, &ipoib_device_list);

	if (!ipoib_sb) {
		up(&ipoib_fs_mutex);
		return 0;
	}

	up(&ipoib_fs_mutex);

	return __ipoib_create_debug_file(dev);
}

void ipoib_delete_debug_file(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	down(&ipoib_fs_mutex);
	list_del(&priv->fs_list);
	if (!ipoib_sb) {
		up(&ipoib_fs_mutex);
		return;
	}
	up(&ipoib_fs_mutex);

	if (priv->mcg_dentry) {
		d_drop(priv->mcg_dentry);
		simple_unlink(ipoib_root->d_inode, priv->mcg_dentry);
	}
}

static int ipoib_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr ipoib_files[] = {
		{ "" }
	};
	struct ipoib_dev_priv *priv;
	int ret;

	ret = simple_fill_super(sb, IPOIB_MAGIC, ipoib_files);
	if (ret)
		return ret;

	ipoib_root = sb->s_root;

	down(&ipoib_fs_mutex);

	ipoib_sb = sb;

	list_for_each_entry(priv, &ipoib_device_list, fs_list) {
		ret = __ipoib_create_debug_file(priv->dev);
		if (ret)
			break;
	}

	up(&ipoib_fs_mutex);

	return ret;
}

static struct super_block *ipoib_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, ipoib_fill_super);
}

static void ipoib_kill_sb(struct super_block *sb)
{
	down(&ipoib_fs_mutex);
	ipoib_sb = NULL;
	up(&ipoib_fs_mutex);

	kill_litter_super(sb);
}

static struct file_system_type ipoib_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ipoib_debugfs",
	.get_sb		= ipoib_get_sb,
	.kill_sb	= ipoib_kill_sb,
};

int ipoib_register_debugfs(void)
{
	return register_filesystem(&ipoib_fs_type);
}

void ipoib_unregister_debugfs(void)
{
	unregister_filesystem(&ipoib_fs_type);
}

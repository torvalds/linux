/*
 * Copyright (C) 2010 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include <linux/debugfs.h>

#include "main.h"
#include "bat_debugfs.h"
#include "translation-table.h"
#include "originator.h"
#include "hard-interface.h"
#include "vis.h"
#include "icmp_socket.h"

static struct dentry *bat_debugfs;

static int originators_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, orig_seq_print_text, net_dev);
}

static int transtable_global_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, hna_global_seq_print_text, net_dev);
}

static int transtable_local_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, hna_local_seq_print_text, net_dev);
}

static int vis_data_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, vis_seq_print_text, net_dev);
}

struct bat_debuginfo {
	struct attribute attr;
	const struct file_operations fops;
};

#define BAT_DEBUGINFO(_name, _mode, _open)	\
struct bat_debuginfo bat_debuginfo_##_name = {	\
	.attr = { .name = __stringify(_name),	\
		  .mode = _mode, },		\
	.fops = { .owner = THIS_MODULE,		\
		  .open = _open,		\
		  .read	= seq_read,		\
		  .llseek = seq_lseek,		\
		  .release = single_release,	\
		}				\
};

static BAT_DEBUGINFO(originators, S_IRUGO, originators_open);
static BAT_DEBUGINFO(transtable_global, S_IRUGO, transtable_global_open);
static BAT_DEBUGINFO(transtable_local, S_IRUGO, transtable_local_open);
static BAT_DEBUGINFO(vis_data, S_IRUGO, vis_data_open);

static struct bat_debuginfo *mesh_debuginfos[] = {
	&bat_debuginfo_originators,
	&bat_debuginfo_transtable_global,
	&bat_debuginfo_transtable_local,
	&bat_debuginfo_vis_data,
	NULL,
};

void debugfs_init(void)
{
	bat_debugfs = debugfs_create_dir(DEBUGFS_BAT_SUBDIR, NULL);
}

void debugfs_destroy(void)
{
	if (bat_debugfs) {
		debugfs_remove_recursive(bat_debugfs);
		bat_debugfs = NULL;
	}
}

int debugfs_add_meshif(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_debuginfo **bat_debug;
	struct dentry *file;

	if (!bat_debugfs)
		goto out;

	bat_priv->debug_dir = debugfs_create_dir(dev->name, bat_debugfs);
	if (!bat_priv->debug_dir)
		goto out;

	bat_socket_setup(bat_priv);

	for (bat_debug = mesh_debuginfos; *bat_debug; ++bat_debug) {
		file = debugfs_create_file(((*bat_debug)->attr).name,
					  S_IFREG | ((*bat_debug)->attr).mode,
					  bat_priv->debug_dir,
					  dev, &(*bat_debug)->fops);
		if (!file) {
			printk(KERN_ERR "batman-adv:Can't add debugfs file: "
			       "%s/%s\n", dev->name, ((*bat_debug)->attr).name);
			goto rem_attr;
		}
	}

	return 0;
rem_attr:
	debugfs_remove_recursive(bat_priv->debug_dir);
	bat_priv->debug_dir = NULL;
out:
#ifdef CONFIG_DEBUG_FS
	return -ENOMEM;
#else
	return 0;
#endif /* CONFIG_DEBUG_FS */
}

void debugfs_del_meshif(struct net_device *dev)
{
	struct bat_priv *bat_priv = netdev_priv(dev);

	if (bat_debugfs) {
		debugfs_remove_recursive(bat_priv->debug_dir);
		bat_priv->debug_dir = NULL;
	}
}

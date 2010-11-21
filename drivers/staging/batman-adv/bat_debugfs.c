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

#include "main.h"

#include <linux/debugfs.h>

#include "bat_debugfs.h"
#include "translation-table.h"
#include "originator.h"
#include "hard-interface.h"
#include "soft-interface.h"
#include "vis.h"
#include "icmp_socket.h"

static struct dentry *bat_debugfs;

#ifdef CONFIG_BATMAN_ADV_DEBUG
#define LOG_BUFF_MASK (log_buff_len-1)
#define LOG_BUFF(idx) (debug_log->log_buff[(idx) & LOG_BUFF_MASK])

static int log_buff_len = LOG_BUF_LEN;

static void emit_log_char(struct debug_log *debug_log, char c)
{
	LOG_BUFF(debug_log->log_end) = c;
	debug_log->log_end++;

	if (debug_log->log_end - debug_log->log_start > log_buff_len)
		debug_log->log_start = debug_log->log_end - log_buff_len;
}

static int fdebug_log(struct debug_log *debug_log, char *fmt, ...)
{
	int printed_len;
	va_list args;
	static char debug_log_buf[256];
	char *p;
	unsigned long flags;

	if (!debug_log)
		return 0;

	spin_lock_irqsave(&debug_log->lock, flags);
	va_start(args, fmt);
	printed_len = vscnprintf(debug_log_buf, sizeof(debug_log_buf),
				 fmt, args);
	va_end(args);

	for (p = debug_log_buf; *p != 0; p++)
		emit_log_char(debug_log, *p);

	spin_unlock_irqrestore(&debug_log->lock, flags);

	wake_up(&debug_log->queue_wait);

	return 0;
}

int debug_log(struct bat_priv *bat_priv, char *fmt, ...)
{
	va_list args;
	char tmp_log_buf[256];

	va_start(args, fmt);
	vscnprintf(tmp_log_buf, sizeof(tmp_log_buf), fmt, args);
	fdebug_log(bat_priv->debug_log, "[%10u] %s",
		   (jiffies / HZ), tmp_log_buf);
	va_end(args);

	return 0;
}

static int log_open(struct inode *inode, struct file *file)
{
	nonseekable_open(inode, file);
	file->private_data = inode->i_private;
	inc_module_count();
	return 0;
}

static int log_release(struct inode *inode, struct file *file)
{
	dec_module_count();
	return 0;
}

static ssize_t log_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct bat_priv *bat_priv = file->private_data;
	struct debug_log *debug_log = bat_priv->debug_log;
	int error, i = 0;
	char c;
	unsigned long flags;

	if ((file->f_flags & O_NONBLOCK) &&
	    !(debug_log->log_end - debug_log->log_start))
		return -EAGAIN;

	if ((!buf) || (count < 0))
		return -EINVAL;

	if (count == 0)
		return 0;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	error = wait_event_interruptible(debug_log->queue_wait,
				(debug_log->log_start - debug_log->log_end));

	if (error)
		return error;

	spin_lock_irqsave(&debug_log->lock, flags);

	while ((!error) && (i < count) &&
	       (debug_log->log_start != debug_log->log_end)) {
		c = LOG_BUFF(debug_log->log_start);

		debug_log->log_start++;

		spin_unlock_irqrestore(&debug_log->lock, flags);

		error = __put_user(c, buf);

		spin_lock_irqsave(&debug_log->lock, flags);

		buf++;
		i++;

	}

	spin_unlock_irqrestore(&debug_log->lock, flags);

	if (!error)
		return i;

	return error;
}

static unsigned int log_poll(struct file *file, poll_table *wait)
{
	struct bat_priv *bat_priv = file->private_data;
	struct debug_log *debug_log = bat_priv->debug_log;

	poll_wait(file, &debug_log->queue_wait, wait);

	if (debug_log->log_end - debug_log->log_start)
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations log_fops = {
	.open           = log_open,
	.release        = log_release,
	.read           = log_read,
	.poll           = log_poll,
	.llseek         = no_llseek,
};

static int debug_log_setup(struct bat_priv *bat_priv)
{
	struct dentry *d;

	if (!bat_priv->debug_dir)
		goto err;

	bat_priv->debug_log = kzalloc(sizeof(struct debug_log), GFP_ATOMIC);
	if (!bat_priv->debug_log)
		goto err;

	spin_lock_init(&bat_priv->debug_log->lock);
	init_waitqueue_head(&bat_priv->debug_log->queue_wait);

	d = debugfs_create_file("log", S_IFREG | S_IRUSR,
				bat_priv->debug_dir, bat_priv, &log_fops);
	if (d)
		goto err;

	return 0;

err:
	return 1;
}

static void debug_log_cleanup(struct bat_priv *bat_priv)
{
	kfree(bat_priv->debug_log);
	bat_priv->debug_log = NULL;
}
#else /* CONFIG_BATMAN_ADV_DEBUG */
static int debug_log_setup(struct bat_priv *bat_priv)
{
	bat_priv->debug_log = NULL;
	return 0;
}

static void debug_log_cleanup(struct bat_priv *bat_priv)
{
	return;
}
#endif

static int originators_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, orig_seq_print_text, net_dev);
}

static int softif_neigh_open(struct inode *inode, struct file *file)
{
	struct net_device *net_dev = (struct net_device *)inode->i_private;
	return single_open(file, softif_neigh_seq_print_text, net_dev);
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
static BAT_DEBUGINFO(softif_neigh, S_IRUGO, softif_neigh_open);
static BAT_DEBUGINFO(transtable_global, S_IRUGO, transtable_global_open);
static BAT_DEBUGINFO(transtable_local, S_IRUGO, transtable_local_open);
static BAT_DEBUGINFO(vis_data, S_IRUGO, vis_data_open);

static struct bat_debuginfo *mesh_debuginfos[] = {
	&bat_debuginfo_originators,
	&bat_debuginfo_softif_neigh,
	&bat_debuginfo_transtable_global,
	&bat_debuginfo_transtable_local,
	&bat_debuginfo_vis_data,
	NULL,
};

void debugfs_init(void)
{
	bat_debugfs = debugfs_create_dir(DEBUGFS_BAT_SUBDIR, NULL);
	if (bat_debugfs == ERR_PTR(-ENODEV))
		bat_debugfs = NULL;
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
	debug_log_setup(bat_priv);

	for (bat_debug = mesh_debuginfos; *bat_debug; ++bat_debug) {
		file = debugfs_create_file(((*bat_debug)->attr).name,
					  S_IFREG | ((*bat_debug)->attr).mode,
					  bat_priv->debug_dir,
					  dev, &(*bat_debug)->fops);
		if (!file) {
			bat_err(dev, "Can't add debugfs file: %s/%s\n",
				dev->name, ((*bat_debug)->attr).name);
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

	debug_log_cleanup(bat_priv);

	if (bat_debugfs) {
		debugfs_remove_recursive(bat_priv->debug_dir);
		bat_priv->debug_dir = NULL;
	}
}

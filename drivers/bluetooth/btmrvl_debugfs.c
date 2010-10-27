/**
 * Marvell Bluetooth driver: debugfs related functions
 *
 * Copyright (C) 2009, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 **/

#include <linux/debugfs.h>
#include <linux/slab.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btmrvl_drv.h"

struct btmrvl_debugfs_data {
	struct dentry *config_dir;
	struct dentry *status_dir;

	/* config */
	struct dentry *psmode;
	struct dentry *pscmd;
	struct dentry *hsmode;
	struct dentry *hscmd;
	struct dentry *gpiogap;
	struct dentry *hscfgcmd;

	/* status */
	struct dentry *curpsmode;
	struct dentry *hsstate;
	struct dentry *psstate;
	struct dentry *txdnldready;
};

static int btmrvl_open_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t btmrvl_hscfgcmd_write(struct file *file,
			const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	long result, ret;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	ret = strict_strtol(buf, 10, &result);

	priv->btmrvl_dev.hscfgcmd = result;

	if (priv->btmrvl_dev.hscfgcmd) {
		btmrvl_prepare_command(priv);
		wake_up_interruptible(&priv->main_thread.wait_q);
	}

	return count;
}

static ssize_t btmrvl_hscfgcmd_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n",
						priv->btmrvl_dev.hscfgcmd);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_hscfgcmd_fops = {
	.read	= btmrvl_hscfgcmd_read,
	.write	= btmrvl_hscfgcmd_write,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_psmode_write(struct file *file, const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	long result, ret;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	ret = strict_strtol(buf, 10, &result);

	priv->btmrvl_dev.psmode = result;

	return count;
}

static ssize_t btmrvl_psmode_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n",
						priv->btmrvl_dev.psmode);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_psmode_fops = {
	.read	= btmrvl_psmode_read,
	.write	= btmrvl_psmode_write,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_pscmd_write(struct file *file, const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	long result, ret;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	ret = strict_strtol(buf, 10, &result);

	priv->btmrvl_dev.pscmd = result;

	if (priv->btmrvl_dev.pscmd) {
		btmrvl_prepare_command(priv);
		wake_up_interruptible(&priv->main_thread.wait_q);
	}

	return count;

}

static ssize_t btmrvl_pscmd_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n", priv->btmrvl_dev.pscmd);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_pscmd_fops = {
	.read = btmrvl_pscmd_read,
	.write = btmrvl_pscmd_write,
	.open = btmrvl_open_generic,
};

static ssize_t btmrvl_gpiogap_write(struct file *file, const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	long result, ret;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	ret = strict_strtol(buf, 16, &result);

	priv->btmrvl_dev.gpio_gap = result;

	return count;
}

static ssize_t btmrvl_gpiogap_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "0x%x\n",
						priv->btmrvl_dev.gpio_gap);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_gpiogap_fops = {
	.read	= btmrvl_gpiogap_read,
	.write	= btmrvl_gpiogap_write,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_hscmd_write(struct file *file, const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	long result, ret;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	ret = strict_strtol(buf, 10, &result);

	priv->btmrvl_dev.hscmd = result;
	if (priv->btmrvl_dev.hscmd) {
		btmrvl_prepare_command(priv);
		wake_up_interruptible(&priv->main_thread.wait_q);
	}

	return count;
}

static ssize_t btmrvl_hscmd_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n", priv->btmrvl_dev.hscmd);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_hscmd_fops = {
	.read	= btmrvl_hscmd_read,
	.write	= btmrvl_hscmd_write,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_hsmode_write(struct file *file, const char __user *ubuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	long result, ret;

	memset(buf, 0, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	ret = strict_strtol(buf, 10, &result);

	priv->btmrvl_dev.hsmode = result;

	return count;
}

static ssize_t btmrvl_hsmode_read(struct file *file, char __user * userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n", priv->btmrvl_dev.hsmode);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_hsmode_fops = {
	.read	= btmrvl_hsmode_read,
	.write	= btmrvl_hsmode_write,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_curpsmode_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n", priv->adapter->psmode);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_curpsmode_fops = {
	.read	= btmrvl_curpsmode_read,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_psstate_read(struct file *file, char __user * userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n", priv->adapter->ps_state);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_psstate_fops = {
	.read	= btmrvl_psstate_read,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_hsstate_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n", priv->adapter->hs_state);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_hsstate_fops = {
	.read	= btmrvl_hsstate_read,
	.open	= btmrvl_open_generic,
};

static ssize_t btmrvl_txdnldready_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	struct btmrvl_private *priv = file->private_data;
	char buf[16];
	int ret;

	ret = snprintf(buf, sizeof(buf) - 1, "%d\n",
					priv->btmrvl_dev.tx_dnld_rdy);

	return simple_read_from_buffer(userbuf, count, ppos, buf, ret);
}

static const struct file_operations btmrvl_txdnldready_fops = {
	.read	= btmrvl_txdnldready_read,
	.open	= btmrvl_open_generic,
};

void btmrvl_debugfs_init(struct hci_dev *hdev)
{
	struct btmrvl_private *priv = hdev->driver_data;
	struct btmrvl_debugfs_data *dbg;

	if (!hdev->debugfs)
		return;

	dbg = kzalloc(sizeof(*dbg), GFP_KERNEL);
	priv->debugfs_data = dbg;

	if (!dbg) {
		BT_ERR("Can not allocate memory for btmrvl_debugfs_data.");
		return;
	}

	dbg->config_dir = debugfs_create_dir("config", hdev->debugfs);

	dbg->psmode = debugfs_create_file("psmode", 0644, dbg->config_dir,
				hdev->driver_data, &btmrvl_psmode_fops);
	dbg->pscmd = debugfs_create_file("pscmd", 0644, dbg->config_dir,
				hdev->driver_data, &btmrvl_pscmd_fops);
	dbg->gpiogap = debugfs_create_file("gpiogap", 0644, dbg->config_dir,
				hdev->driver_data, &btmrvl_gpiogap_fops);
	dbg->hsmode =  debugfs_create_file("hsmode", 0644, dbg->config_dir,
				hdev->driver_data, &btmrvl_hsmode_fops);
	dbg->hscmd = debugfs_create_file("hscmd", 0644, dbg->config_dir,
				hdev->driver_data, &btmrvl_hscmd_fops);
	dbg->hscfgcmd = debugfs_create_file("hscfgcmd", 0644, dbg->config_dir,
				hdev->driver_data, &btmrvl_hscfgcmd_fops);

	dbg->status_dir = debugfs_create_dir("status", hdev->debugfs);
	dbg->curpsmode = debugfs_create_file("curpsmode", 0444,
						dbg->status_dir,
						hdev->driver_data,
						&btmrvl_curpsmode_fops);
	dbg->psstate = debugfs_create_file("psstate", 0444, dbg->status_dir,
				hdev->driver_data, &btmrvl_psstate_fops);
	dbg->hsstate = debugfs_create_file("hsstate", 0444, dbg->status_dir,
				hdev->driver_data, &btmrvl_hsstate_fops);
	dbg->txdnldready = debugfs_create_file("txdnldready", 0444,
						dbg->status_dir,
						hdev->driver_data,
						&btmrvl_txdnldready_fops);
}

void btmrvl_debugfs_remove(struct hci_dev *hdev)
{
	struct btmrvl_private *priv = hdev->driver_data;
	struct btmrvl_debugfs_data *dbg = priv->debugfs_data;

	if (!dbg)
		return;

	debugfs_remove(dbg->psmode);
	debugfs_remove(dbg->pscmd);
	debugfs_remove(dbg->gpiogap);
	debugfs_remove(dbg->hsmode);
	debugfs_remove(dbg->hscmd);
	debugfs_remove(dbg->hscfgcmd);
	debugfs_remove(dbg->config_dir);

	debugfs_remove(dbg->curpsmode);
	debugfs_remove(dbg->psstate);
	debugfs_remove(dbg->hsstate);
	debugfs_remove(dbg->txdnldready);
	debugfs_remove(dbg->status_dir);

	kfree(dbg);
}

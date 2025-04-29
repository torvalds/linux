// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/seq_file.h>
#include <linux/pci.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_edid.h>

#include "hibmc_drm_drv.h"

#define MAX_BUF_SIZE 12

static ssize_t hibmc_control_write(struct file *file, const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct hibmc_drm_private *priv = file_inode(file)->i_private;
	struct hibmc_dp_cbar_cfg *cfg = &priv->dp.cfg;
	int ret, idx;
	u8 buf[MAX_BUF_SIZE];

	if (count >= MAX_BUF_SIZE)
		return -EINVAL;

	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = '\0';

	/* Only 4 parameters is allowed, the ranger are as follow:
	 * [0] enable/disable colorbar feature
	       0: enable colorbar, 1: disable colorbar
	 * [1] the timing source of colorbar displaying
	       0: timing follows XDP, 1: internal self timing
	 * [2] the movment of colorbar displaying
	       0: static colorbar image,
	 *     1~255: right shifting a type of color per (1~255)frames
	 * [3] the color type of colorbar displaying
	       0~9: color bar, white, red, orange,
	 *          yellow, green, cyan, bule, pupper, black
	 */
	if (sscanf(buf, "%hhu %hhu %hhu %u", &cfg->enable, &cfg->self_timing,
		   &cfg->dynamic_rate, &cfg->pattern) != 4) {
		return -EINVAL;
	}

	if (cfg->pattern > 9 || cfg->enable > 1 || cfg->self_timing > 1)
		return -EINVAL;

	ret = drm_dev_enter(&priv->dev, &idx);
	if (!ret)
		return -ENODEV;

	hibmc_dp_set_cbar(&priv->dp, cfg);

	drm_dev_exit(idx);

	return count;
}

static int hibmc_dp_dbgfs_show(struct seq_file *m, void *arg)
{
	struct hibmc_drm_private *priv = m->private;
	struct hibmc_dp_cbar_cfg *cfg = &priv->dp.cfg;
	int idx;

	if (!drm_dev_enter(&priv->dev, &idx))
		return -ENODEV;

	seq_printf(m, "hibmc dp colorbar cfg: %u %u %u %u\n", cfg->enable, cfg->self_timing,
		   cfg->dynamic_rate, cfg->pattern);

	drm_dev_exit(idx);

	return 0;
}

static int hibmc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, hibmc_dp_dbgfs_show, inode->i_private);
}

static const struct file_operations hibmc_dbg_fops = {
	.owner   = THIS_MODULE,
	.write   = hibmc_control_write,
	.read    = seq_read,
	.open    = hibmc_open,
	.llseek  = seq_lseek,
	.release = single_release,
};

void hibmc_debugfs_init(struct drm_connector *connector, struct dentry *root)
{
	struct drm_device *dev = connector->dev;
	struct hibmc_drm_private *priv = to_hibmc_drm_private(dev);

	/* create the file in drm directory, so we don't need to remove manually */
	debugfs_create_file("colorbar-cfg", 0200,
			    root, priv, &hibmc_dbg_fops);
}

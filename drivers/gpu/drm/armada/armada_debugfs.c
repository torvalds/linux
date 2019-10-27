// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 */

#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include <drm/drm_debugfs.h>
#include <drm/drm_file.h>

#include "armada_crtc.h"
#include "armada_drm.h"

static int armada_debugfs_gem_linear_show(struct seq_file *m, void *data)
{
	struct drm_info_node *node = m->private;
	struct drm_device *dev = node->minor->dev;
	struct armada_private *priv = dev->dev_private;
	struct drm_printer p = drm_seq_file_printer(m);

	mutex_lock(&priv->linear_lock);
	drm_mm_print(&priv->linear, &p);
	mutex_unlock(&priv->linear_lock);

	return 0;
}

static int armada_debugfs_crtc_reg_show(struct seq_file *m, void *data)
{
	struct armada_crtc *dcrtc = m->private;
	int i;

	for (i = 0x84; i <= 0x1c4; i += 4) {
		u32 v = readl_relaxed(dcrtc->base + i);
		seq_printf(m, "0x%04x: 0x%08x\n", i, v);
	}

	return 0;
}

static int armada_debugfs_crtc_reg_open(struct inode *inode, struct file *file)
{
	return single_open(file, armada_debugfs_crtc_reg_show,
			   inode->i_private);
}

static int armada_debugfs_crtc_reg_write(struct file *file,
	const char __user *ptr, size_t len, loff_t *off)
{
	struct armada_crtc *dcrtc;
	unsigned long reg, mask, val;
	char buf[32];
	int ret;
	u32 v;

	if (*off != 0)
		return 0;

	if (len > sizeof(buf) - 1)
		len = sizeof(buf) - 1;

	ret = strncpy_from_user(buf, ptr, len);
	if (ret < 0)
		return ret;
	buf[len] = '\0';

	if (sscanf(buf, "%lx %lx %lx", &reg, &mask, &val) != 3)
		return -EINVAL;
	if (reg < 0x84 || reg > 0x1c4 || reg & 3)
		return -ERANGE;

	dcrtc = ((struct seq_file *)file->private_data)->private;
	v = readl(dcrtc->base + reg);
	v &= ~mask;
	v |= val & mask;
	writel(v, dcrtc->base + reg);

	return len;
}

static const struct file_operations armada_debugfs_crtc_reg_fops = {
	.owner = THIS_MODULE,
	.open = armada_debugfs_crtc_reg_open,
	.read = seq_read,
	.write = armada_debugfs_crtc_reg_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void armada_drm_crtc_debugfs_init(struct armada_crtc *dcrtc)
{
	debugfs_create_file("armada-regs", 0600, dcrtc->crtc.debugfs_entry,
			    dcrtc, &armada_debugfs_crtc_reg_fops);
}

static struct drm_info_list armada_debugfs_list[] = {
	{ "gem_linear", armada_debugfs_gem_linear_show, 0 },
};
#define ARMADA_DEBUGFS_ENTRIES ARRAY_SIZE(armada_debugfs_list)

int armada_drm_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(armada_debugfs_list, ARMADA_DEBUGFS_ENTRIES,
				 minor->debugfs_root, minor);

	return 0;
}

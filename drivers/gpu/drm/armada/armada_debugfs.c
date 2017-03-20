/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <drm/drmP.h>
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

static int armada_debugfs_reg_show(struct seq_file *m, void *data)
{
	struct drm_device *dev = m->private;
	struct armada_private *priv = dev->dev_private;
	int n, i;

	if (priv) {
		for (n = 0; n < ARRAY_SIZE(priv->dcrtc); n++) {
			struct armada_crtc *dcrtc = priv->dcrtc[n];
			if (!dcrtc)
				continue;

			for (i = 0x84; i <= 0x1c4; i += 4) {
				uint32_t v = readl_relaxed(dcrtc->base + i);
				seq_printf(m, "%u: 0x%04x: 0x%08x\n", n, i, v);
			}
		}
	}

	return 0;
}

static int armada_debugfs_reg_r_open(struct inode *inode, struct file *file)
{
	return single_open(file, armada_debugfs_reg_show, inode->i_private);
}

static const struct file_operations fops_reg_r = {
	.owner = THIS_MODULE,
	.open = armada_debugfs_reg_r_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int armada_debugfs_write(struct file *file, const char __user *ptr,
	size_t len, loff_t *off)
{
	struct drm_device *dev = file->private_data;
	struct armada_private *priv = dev->dev_private;
	struct armada_crtc *dcrtc = priv->dcrtc[0];
	char buf[32], *p;
	uint32_t reg, val;
	int ret;

	if (*off != 0)
		return 0;

	if (len > sizeof(buf) - 1)
		len = sizeof(buf) - 1;

	ret = strncpy_from_user(buf, ptr, len);
	if (ret < 0)
		return ret;
	buf[len] = '\0';

	reg = simple_strtoul(buf, &p, 16);
	if (!isspace(*p))
		return -EINVAL;
	val = simple_strtoul(p + 1, NULL, 16);

	if (reg >= 0x84 && reg <= 0x1c4)
		writel(val, dcrtc->base + reg);

	return len;
}

static const struct file_operations fops_reg_w = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = armada_debugfs_write,
	.llseek = noop_llseek,
};

static struct drm_info_list armada_debugfs_list[] = {
	{ "gem_linear", armada_debugfs_gem_linear_show, 0 },
};
#define ARMADA_DEBUGFS_ENTRIES ARRAY_SIZE(armada_debugfs_list)

static int drm_add_fake_info_node(struct drm_minor *minor, struct dentry *ent,
	const void *key)
{
	struct drm_info_node *node;

	node = kmalloc(sizeof(struct drm_info_node), GFP_KERNEL);
	if (!node) {
		debugfs_remove(ent);
		return -ENOMEM;
	}

	node->minor = minor;
	node->dent = ent;
	node->info_ent = (void *) key;

	mutex_lock(&minor->debugfs_lock);
	list_add(&node->list, &minor->debugfs_list);
	mutex_unlock(&minor->debugfs_lock);

	return 0;
}

static int armada_debugfs_create(struct dentry *root, struct drm_minor *minor,
	const char *name, umode_t mode, const struct file_operations *fops)
{
	struct dentry *de;

	de = debugfs_create_file(name, mode, root, minor->dev, fops);

	return drm_add_fake_info_node(minor, de, fops);
}

int armada_drm_debugfs_init(struct drm_minor *minor)
{
	int ret;

	ret = drm_debugfs_create_files(armada_debugfs_list,
				       ARMADA_DEBUGFS_ENTRIES,
				       minor->debugfs_root, minor);
	if (ret)
		return ret;

	ret = armada_debugfs_create(minor->debugfs_root, minor,
				   "reg", S_IFREG | S_IRUSR, &fops_reg_r);
	if (ret)
		goto err_1;

	ret = armada_debugfs_create(minor->debugfs_root, minor,
				"reg_wr", S_IFREG | S_IWUSR, &fops_reg_w);
	if (ret)
		goto err_2;
	return ret;

 err_2:
	drm_debugfs_remove_files((struct drm_info_list *)&fops_reg_r, 1, minor);
 err_1:
	drm_debugfs_remove_files(armada_debugfs_list, ARMADA_DEBUGFS_ENTRIES,
				 minor);
	return ret;
}

void armada_drm_debugfs_cleanup(struct drm_minor *minor)
{
	drm_debugfs_remove_files((struct drm_info_list *)&fops_reg_w, 1, minor);
	drm_debugfs_remove_files((struct drm_info_list *)&fops_reg_r, 1, minor);
	drm_debugfs_remove_files(armada_debugfs_list, ARMADA_DEBUGFS_ENTRIES,
				 minor);
}

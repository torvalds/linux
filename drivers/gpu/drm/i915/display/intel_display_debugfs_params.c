// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>

#include <drm/drm_drv.h>
#include <drm/drm_file.h>

#include "intel_display_core.h"
#include "intel_display_debugfs_params.h"
#include "intel_display_params.h"

/* int param */
static int intel_display_param_int_show(struct seq_file *m, void *data)
{
	int *value = m->private;

	seq_printf(m, "%d\n", *value);

	return 0;
}

static int intel_display_param_int_open(struct inode *inode, struct file *file)
{
	return single_open(file, intel_display_param_int_show, inode->i_private);
}

static ssize_t intel_display_param_int_write(struct file *file,
					     const char __user *ubuf, size_t len,
					     loff_t *offp)
{
	struct seq_file *m = file->private_data;
	int *value = m->private;
	int ret;

	ret = kstrtoint_from_user(ubuf, len, 0, value);
	if (ret) {
		/* support boolean values too */
		bool b;

		ret = kstrtobool_from_user(ubuf, len, &b);
		if (!ret)
			*value = b;
	}

	return ret ?: len;
}

static const struct file_operations intel_display_param_int_fops = {
	.owner = THIS_MODULE,
	.open = intel_display_param_int_open,
	.read = seq_read,
	.write = intel_display_param_int_write,
	.llseek = default_llseek,
	.release = single_release,
};

static const struct file_operations intel_display_param_int_fops_ro = {
	.owner = THIS_MODULE,
	.open = intel_display_param_int_open,
	.read = seq_read,
	.llseek = default_llseek,
	.release = single_release,
};

/* unsigned int param */
static int intel_display_param_uint_show(struct seq_file *m, void *data)
{
	unsigned int *value = m->private;

	seq_printf(m, "%u\n", *value);

	return 0;
}

static int intel_display_param_uint_open(struct inode *inode, struct file *file)
{
	return single_open(file, intel_display_param_uint_show, inode->i_private);
}

static ssize_t intel_display_param_uint_write(struct file *file,
					      const char __user *ubuf, size_t len,
					      loff_t *offp)
{
	struct seq_file *m = file->private_data;
	unsigned int *value = m->private;
	int ret;

	ret = kstrtouint_from_user(ubuf, len, 0, value);
	if (ret) {
		/* support boolean values too */
		bool b;

		ret = kstrtobool_from_user(ubuf, len, &b);
		if (!ret)
			*value = b;
	}

	return ret ?: len;
}

static const struct file_operations intel_display_param_uint_fops = {
	.owner = THIS_MODULE,
	.open = intel_display_param_uint_open,
	.read = seq_read,
	.write = intel_display_param_uint_write,
	.llseek = default_llseek,
	.release = single_release,
};

static const struct file_operations intel_display_param_uint_fops_ro = {
	.owner = THIS_MODULE,
	.open = intel_display_param_uint_open,
	.read = seq_read,
	.llseek = default_llseek,
	.release = single_release,
};

#define RO(mode) (((mode) & 0222) == 0)

__maybe_unused static struct dentry *
intel_display_debugfs_create_int(const char *name, umode_t mode,
			struct dentry *parent, int *value)
{
	return debugfs_create_file_unsafe(name, mode, parent, value,
					  RO(mode) ? &intel_display_param_int_fops_ro :
					  &intel_display_param_int_fops);
}

__maybe_unused static struct dentry *
intel_display_debugfs_create_uint(const char *name, umode_t mode,
			 struct dentry *parent, unsigned int *value)
{
	return debugfs_create_file_unsafe(name, mode, parent, value,
					  RO(mode) ? &intel_display_param_uint_fops_ro :
					  &intel_display_param_uint_fops);
}

#define _intel_display_param_create_file(parent, name, mode, valp)	\
	do {								\
		if (mode)						\
			_Generic(valp,					\
				 bool * : debugfs_create_bool,		\
				 int * : intel_display_debugfs_create_int, \
				 unsigned int * : intel_display_debugfs_create_uint, \
				 unsigned long * : debugfs_create_ulong, \
				 char ** : debugfs_create_str) \
				(name, mode, parent, valp);		\
	} while (0)

/* add a subdirectory with files for each intel display param */
void intel_display_debugfs_params(struct intel_display *display)
{
	struct drm_minor *minor = display->drm->primary;
	struct dentry *dir;
	char dirname[16];

	snprintf(dirname, sizeof(dirname), "%s_params", display->drm->driver->name);
	dir = debugfs_lookup(dirname, minor->debugfs_root);
	if (!dir)
		dir = debugfs_create_dir(dirname, minor->debugfs_root);
	if (IS_ERR(dir))
		return;

	/*
	 * Note: We could create files for params needing special handling
	 * here. Set mode in params to 0 to skip the generic create file, or
	 * just let the generic create file fail silently with -EEXIST.
	 */

#define REGISTER(T, x, unused, mode, ...) _intel_display_param_create_file( \
		dir, #x, mode, &display->params.x);
	INTEL_DISPLAY_PARAMS_FOR_EACH(REGISTER);
#undef REGISTER
}

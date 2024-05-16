// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>

#include "i915_debugfs_params.h"
#include "gt/intel_gt.h"
#include "gt/uc/intel_guc.h"
#include "i915_drv.h"
#include "i915_params.h"

#define MATCH_DEBUGFS_NODE_NAME(_file, _name) \
	(strcmp((_file)->f_path.dentry->d_name.name, (_name)) == 0)

#define GET_I915(i915, name, ptr)	\
	do {	\
		struct i915_params *params;	\
		params = container_of(((void *)(ptr)), typeof(*params), name);	\
		(i915) = container_of(params, typeof(*(i915)), params);	\
	} while (0)

/* int param */
static int i915_param_int_show(struct seq_file *m, void *data)
{
	int *value = m->private;

	seq_printf(m, "%d\n", *value);

	return 0;
}

static int i915_param_int_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_param_int_show, inode->i_private);
}

static int notify_guc(struct drm_i915_private *i915)
{
	struct intel_gt *gt;
	int i, ret = 0;

	for_each_gt(gt, i915, i) {
		if (intel_uc_uses_guc_submission(&gt->uc))
			ret = intel_guc_global_policies_update(&gt->uc.guc);
	}

	return ret;
}

static ssize_t i915_param_int_write(struct file *file,
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

static const struct file_operations i915_param_int_fops = {
	.owner = THIS_MODULE,
	.open = i915_param_int_open,
	.read = seq_read,
	.write = i915_param_int_write,
	.llseek = default_llseek,
	.release = single_release,
};

static const struct file_operations i915_param_int_fops_ro = {
	.owner = THIS_MODULE,
	.open = i915_param_int_open,
	.read = seq_read,
	.llseek = default_llseek,
	.release = single_release,
};

/* unsigned int param */
static int i915_param_uint_show(struct seq_file *m, void *data)
{
	unsigned int *value = m->private;

	seq_printf(m, "%u\n", *value);

	return 0;
}

static int i915_param_uint_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_param_uint_show, inode->i_private);
}

static ssize_t i915_param_uint_write(struct file *file,
				     const char __user *ubuf, size_t len,
				     loff_t *offp)
{
	struct drm_i915_private *i915;
	struct seq_file *m = file->private_data;
	unsigned int *value = m->private;
	unsigned int old = *value;
	int ret;

	ret = kstrtouint_from_user(ubuf, len, 0, value);
	if (ret) {
		/* support boolean values too */
		bool b;

		ret = kstrtobool_from_user(ubuf, len, &b);
		if (!ret)
			*value = b;
	}

	if (!ret && MATCH_DEBUGFS_NODE_NAME(file, "reset")) {
		GET_I915(i915, reset, value);

		ret = notify_guc(i915);
		if (ret)
			*value = old;
	}

	return ret ?: len;
}

static const struct file_operations i915_param_uint_fops = {
	.owner = THIS_MODULE,
	.open = i915_param_uint_open,
	.read = seq_read,
	.write = i915_param_uint_write,
	.llseek = default_llseek,
	.release = single_release,
};

static const struct file_operations i915_param_uint_fops_ro = {
	.owner = THIS_MODULE,
	.open = i915_param_uint_open,
	.read = seq_read,
	.llseek = default_llseek,
	.release = single_release,
};

/* char * param */
static int i915_param_charp_show(struct seq_file *m, void *data)
{
	const char **s = m->private;

	seq_printf(m, "%s\n", *s);

	return 0;
}

static int i915_param_charp_open(struct inode *inode, struct file *file)
{
	return single_open(file, i915_param_charp_show, inode->i_private);
}

static ssize_t i915_param_charp_write(struct file *file,
				      const char __user *ubuf, size_t len,
				      loff_t *offp)
{
	struct seq_file *m = file->private_data;
	char **s = m->private;
	char *new, *old;

	old = *s;
	new = strndup_user(ubuf, PAGE_SIZE);
	if (IS_ERR(new)) {
		len = PTR_ERR(new);
		goto out;
	}

	*s = new;

	kfree(old);
out:
	return len;
}

static const struct file_operations i915_param_charp_fops = {
	.owner = THIS_MODULE,
	.open = i915_param_charp_open,
	.read = seq_read,
	.write = i915_param_charp_write,
	.llseek = default_llseek,
	.release = single_release,
};

static const struct file_operations i915_param_charp_fops_ro = {
	.owner = THIS_MODULE,
	.open = i915_param_charp_open,
	.read = seq_read,
	.llseek = default_llseek,
	.release = single_release,
};

#define RO(mode) (((mode) & 0222) == 0)

static struct dentry *
i915_debugfs_create_int(const char *name, umode_t mode,
			struct dentry *parent, int *value)
{
	return debugfs_create_file_unsafe(name, mode, parent, value,
					  RO(mode) ? &i915_param_int_fops_ro :
					  &i915_param_int_fops);
}

static struct dentry *
i915_debugfs_create_uint(const char *name, umode_t mode,
			 struct dentry *parent, unsigned int *value)
{
	return debugfs_create_file_unsafe(name, mode, parent, value,
					  RO(mode) ? &i915_param_uint_fops_ro :
					  &i915_param_uint_fops);
}

static struct dentry *
i915_debugfs_create_charp(const char *name, umode_t mode,
			  struct dentry *parent, char **value)
{
	return debugfs_create_file(name, mode, parent, value,
				   RO(mode) ? &i915_param_charp_fops_ro :
				   &i915_param_charp_fops);
}

#define _i915_param_create_file(parent, name, mode, valp)		\
	do {								\
		if (mode)						\
			_Generic(valp,					\
				 bool *: debugfs_create_bool,		\
				 int *: i915_debugfs_create_int,	\
				 unsigned int *: i915_debugfs_create_uint, \
				 unsigned long *: debugfs_create_ulong,	\
				 char **: i915_debugfs_create_charp)(name, mode, parent, valp); \
	} while(0)

/* add a subdirectory with files for each i915 param */
struct dentry *i915_debugfs_params(struct drm_i915_private *i915)
{
	struct drm_minor *minor = i915->drm.primary;
	struct i915_params *params = &i915->params;
	struct dentry *dir;

	dir = debugfs_create_dir("i915_params", minor->debugfs_root);
	if (IS_ERR(dir))
		return dir;

	/*
	 * Note: We could create files for params needing special handling
	 * here. Set mode in params to 0 to skip the generic create file, or
	 * just let the generic create file fail silently with -EEXIST.
	 */

#define REGISTER(T, x, unused, mode, ...) _i915_param_create_file(dir, #x, mode, &params->x);
	I915_PARAMS_FOR_EACH(REGISTER);
#undef REGISTER

	return dir;
}

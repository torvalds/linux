// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/kernel.h>

#include "i915_debugfs_params.h"
#include "i915_drv.h"
#include "i915_params.h"

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

	/* FIXME: remove locking after params aren't the module params */
	kernel_param_lock(THIS_MODULE);

	old = *s;
	new = strndup_user(ubuf, PAGE_SIZE);
	if (IS_ERR(new)) {
		len = PTR_ERR(new);
		goto out;
	}

	*s = new;

	kfree(old);
out:
	kernel_param_unlock(THIS_MODULE);

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

static __always_inline void
_i915_param_create_file(struct dentry *parent, const char *name,
			const char *type, int mode, void *value)
{
	if (!mode)
		return;

	if (!__builtin_strcmp(type, "bool"))
		debugfs_create_bool(name, mode, parent, value);
	else if (!__builtin_strcmp(type, "int"))
		i915_debugfs_create_int(name, mode, parent, value);
	else if (!__builtin_strcmp(type, "unsigned int"))
		i915_debugfs_create_uint(name, mode, parent, value);
	else if (!__builtin_strcmp(type, "unsigned long"))
		debugfs_create_ulong(name, mode, parent, value);
	else if (!__builtin_strcmp(type, "char *"))
		i915_debugfs_create_charp(name, mode, parent, value);
	else
		WARN(1, "no debugfs fops defined for param type %s (i915.%s)\n",
		     type, name);
}

/* add a subdirectory with files for each i915 param */
struct dentry *i915_debugfs_params(struct drm_i915_private *i915)
{
	struct drm_minor *minor = i915->drm.primary;
	struct i915_params *params = &i915_modparams;
	struct dentry *dir;

	dir = debugfs_create_dir("i915_params", minor->debugfs_root);
	if (IS_ERR(dir))
		return dir;

	/*
	 * Note: We could create files for params needing special handling
	 * here. Set mode in params to 0 to skip the generic create file, or
	 * just let the generic create file fail silently with -EEXIST.
	 */

#define REGISTER(T, x, unused, mode, ...) _i915_param_create_file(dir, #x, #T, mode, &params->x);
	I915_PARAMS_FOR_EACH(REGISTER);
#undef REGISTER

	return dir;
}

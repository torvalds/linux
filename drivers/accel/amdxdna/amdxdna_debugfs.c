// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#include "amdxdna_cbuf.h"
#include "amdxdna_debugfs.h"

#include <drm/drm_file.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define _DBGFS_FOPS(_open, _release, _write) \
{ \
	.owner = THIS_MODULE, \
	.open = _open, \
	.read = seq_read, \
	.llseek = seq_lseek, \
	.release = _release, \
	.write = _write, \
}

#define AMDXDNA_DBGFS_FOPS(_name, _show, _write) \
	static int amdxdna_dbgfs_##_name##_open(struct inode *inode, struct file *file) \
	{ \
		return single_open(file, _show, inode->i_private); \
	} \
	static int amdxdna_dbgfs_##_name##_release(struct inode *inode, struct file *file) \
	{ \
		return single_release(inode, file); \
	} \
	static const struct file_operations amdxdna_fops_##_name = \
		_DBGFS_FOPS(amdxdna_dbgfs_##_name##_open, amdxdna_dbgfs_##_name##_release, _write)

#define AMDXDNA_DBGFS_FILE(_name, _mode) { #_name, &amdxdna_fops_##_name, _mode }

#define file_to_xdna(file) (((struct seq_file *)(file)->private_data)->private)

static ssize_t amdxdna_carveout_write(struct file *file, const char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct amdxdna_dev *xdna = file_to_xdna(file);
	char kbuf[128];
	u64 size, addr;
	char *sep;
	int ret;

	if (count == 0 || count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	kbuf[count] = '\0';
	strim(kbuf);
	XDNA_DBG(xdna, "Trying to set carveout to %s", kbuf);

	sep = strchr(kbuf, '@');
	if (!sep)
		return -EINVAL;
	*sep = '\0';
	sep++;

	ret = kstrtou64(kbuf, 0, &size);
	if (ret)
		return ret;

	ret = kstrtou64(sep, 0, &addr);
	if (ret)
		return ret;

	/* Sanity check the addr and size. */
	if (!size)
		return -EINVAL;
	if (!IS_ALIGNED(addr, PAGE_SIZE) || !IS_ALIGNED(size, PAGE_SIZE))
		return -EINVAL;

	guard(mutex)(&xdna->dev_lock);

	ret = amdxdna_carveout_init(xdna, addr, size);
	if (ret)
		return ret;

	return count;
}

static int amdxdna_carveout_show(struct seq_file *m, void *unused)
{
	struct amdxdna_dev *xdna = m->private;
	u64 addr, size;

	guard(mutex)(&xdna->dev_lock);
	amdxdna_get_carveout_conf(xdna, &addr, &size);
	seq_printf(m, "0x%llx@0x%llx\n", size, addr);
	return 0;
}

/*
 * Input/output format: <carveout_size>@<carveout_address>
 */
AMDXDNA_DBGFS_FOPS(carveout, amdxdna_carveout_show, amdxdna_carveout_write);

static const struct {
	const char *name;
	const struct file_operations *fops;
	umode_t mode;
} amdxdna_dbgfs_files[] = {
	AMDXDNA_DBGFS_FILE(carveout, 0600),
};

void amdxdna_debugfs_init(struct amdxdna_dev *xdna)
{
	struct drm_minor *minor = xdna->ddev.accel;
	int i;

	/*
	 * It should be okay that debugfs fails to init.
	 * We rely on DRM framework to finish debugfs.
	 */
	for (i = 0; i < ARRAY_SIZE(amdxdna_dbgfs_files); i++) {
		debugfs_create_file(amdxdna_dbgfs_files[i].name,
				    amdxdna_dbgfs_files[i].mode,
				    minor->debugfs_root,
				    xdna,
				    amdxdna_dbgfs_files[i].fops);
	}
}

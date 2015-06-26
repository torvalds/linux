/*
 * APEI Error Record Serialization Table debug support
 *
 * ERST is a way provided by APEI to save and retrieve hardware error
 * information to and from a persistent store. This file provide the
 * debugging/testing support for ERST kernel support and firmware
 * implementation.
 *
 * Copyright 2010 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <acpi/apei.h>
#include <linux/miscdevice.h>

#include "apei-internal.h"

#define ERST_DBG_PFX			"ERST DBG: "

#define ERST_DBG_RECORD_LEN_MAX		0x4000

static void *erst_dbg_buf;
static unsigned int erst_dbg_buf_len;

/* Prevent erst_dbg_read/write from being invoked concurrently */
static DEFINE_MUTEX(erst_dbg_mutex);

static int erst_dbg_open(struct inode *inode, struct file *file)
{
	int rc, *pos;

	if (erst_disable)
		return -ENODEV;

	pos = (int *)&file->private_data;

	rc = erst_get_record_id_begin(pos);
	if (rc)
		return rc;

	return nonseekable_open(inode, file);
}

static int erst_dbg_release(struct inode *inode, struct file *file)
{
	erst_get_record_id_end();

	return 0;
}

static long erst_dbg_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int rc;
	u64 record_id;
	u32 record_count;

	switch (cmd) {
	case APEI_ERST_CLEAR_RECORD:
		rc = copy_from_user(&record_id, (void __user *)arg,
				    sizeof(record_id));
		if (rc)
			return -EFAULT;
		return erst_clear(record_id);
	case APEI_ERST_GET_RECORD_COUNT:
		rc = erst_get_record_count();
		if (rc < 0)
			return rc;
		record_count = rc;
		rc = put_user(record_count, (u32 __user *)arg);
		if (rc)
			return rc;
		return 0;
	default:
		return -ENOTTY;
	}
}

static ssize_t erst_dbg_read(struct file *filp, char __user *ubuf,
			     size_t usize, loff_t *off)
{
	int rc, *pos;
	ssize_t len = 0;
	u64 id;

	if (*off)
		return -EINVAL;

	if (mutex_lock_interruptible(&erst_dbg_mutex) != 0)
		return -EINTR;

	pos = (int *)&filp->private_data;

retry_next:
	rc = erst_get_record_id_next(pos, &id);
	if (rc)
		goto out;
	/* no more record */
	if (id == APEI_ERST_INVALID_RECORD_ID) {
		/*
		 * If the persistent store is empty initially, the function
		 * 'erst_read' below will return "-ENOENT" value. This causes
		 * 'retry_next' label is entered again. The returned value
		 * should be zero indicating the read operation is EOF.
		 */
		len = 0;

		goto out;
	}
retry:
	rc = len = erst_read(id, erst_dbg_buf, erst_dbg_buf_len);
	/* The record may be cleared by others, try read next record */
	if (rc == -ENOENT)
		goto retry_next;
	if (rc < 0)
		goto out;
	if (len > ERST_DBG_RECORD_LEN_MAX) {
		pr_warning(ERST_DBG_PFX
			   "Record (ID: 0x%llx) length is too long: %zd\n",
			   id, len);
		rc = -EIO;
		goto out;
	}
	if (len > erst_dbg_buf_len) {
		void *p;
		rc = -ENOMEM;
		p = kmalloc(len, GFP_KERNEL);
		if (!p)
			goto out;
		kfree(erst_dbg_buf);
		erst_dbg_buf = p;
		erst_dbg_buf_len = len;
		goto retry;
	}

	rc = -EINVAL;
	if (len > usize)
		goto out;

	rc = -EFAULT;
	if (copy_to_user(ubuf, erst_dbg_buf, len))
		goto out;
	rc = 0;
out:
	mutex_unlock(&erst_dbg_mutex);
	return rc ? rc : len;
}

static ssize_t erst_dbg_write(struct file *filp, const char __user *ubuf,
			      size_t usize, loff_t *off)
{
	int rc;
	struct cper_record_header *rcd;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (usize > ERST_DBG_RECORD_LEN_MAX) {
		pr_err(ERST_DBG_PFX "Too long record to be written\n");
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&erst_dbg_mutex))
		return -EINTR;
	if (usize > erst_dbg_buf_len) {
		void *p;
		rc = -ENOMEM;
		p = kmalloc(usize, GFP_KERNEL);
		if (!p)
			goto out;
		kfree(erst_dbg_buf);
		erst_dbg_buf = p;
		erst_dbg_buf_len = usize;
	}
	rc = copy_from_user(erst_dbg_buf, ubuf, usize);
	if (rc) {
		rc = -EFAULT;
		goto out;
	}
	rcd = erst_dbg_buf;
	rc = -EINVAL;
	if (rcd->record_length != usize)
		goto out;

	rc = erst_write(erst_dbg_buf);

out:
	mutex_unlock(&erst_dbg_mutex);
	return rc < 0 ? rc : usize;
}

static const struct file_operations erst_dbg_ops = {
	.owner		= THIS_MODULE,
	.open		= erst_dbg_open,
	.release	= erst_dbg_release,
	.read		= erst_dbg_read,
	.write		= erst_dbg_write,
	.unlocked_ioctl	= erst_dbg_ioctl,
	.llseek		= no_llseek,
};

static struct miscdevice erst_dbg_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "erst_dbg",
	.fops	= &erst_dbg_ops,
};

static __init int erst_dbg_init(void)
{
	if (erst_disable) {
		pr_info(ERST_DBG_PFX "ERST support is disabled.\n");
		return -ENODEV;
	}
	return misc_register(&erst_dbg_dev);
}

static __exit void erst_dbg_exit(void)
{
	misc_deregister(&erst_dbg_dev);
	kfree(erst_dbg_buf);
}

module_init(erst_dbg_init);
module_exit(erst_dbg_exit);

MODULE_AUTHOR("Huang Ying");
MODULE_DESCRIPTION("APEI Error Record Serialization Table debug support");
MODULE_LICENSE("GPL");

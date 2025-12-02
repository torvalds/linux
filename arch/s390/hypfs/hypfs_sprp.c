// SPDX-License-Identifier: GPL-2.0
/*
 *    Hypervisor filesystem for Linux on s390.
 *    Set Partition-Resource Parameter interface.
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <asm/diag.h>
#include <asm/sclp.h>
#include "hypfs.h"

#define DIAG304_SET_WEIGHTS	0
#define DIAG304_QUERY_PRP	1
#define DIAG304_SET_CAPPING	2

#define DIAG304_CMD_MAX		2

static inline unsigned long __hypfs_sprp_diag304(void *data, unsigned long cmd)
{
	union register_pair r1 = { .even = virt_to_phys(data), };

	asm volatile("diag %[r1],%[r3],0x304"
		     : [r1] "+&d" (r1.pair)
		     : [r3] "d" (cmd)
		     : "memory");
	return r1.odd;
}

static unsigned long hypfs_sprp_diag304(void *data, unsigned long cmd)
{
	diag_stat_inc(DIAG_STAT_X304);
	return __hypfs_sprp_diag304(data, cmd);
}

static void hypfs_sprp_free(const void *data)
{
	free_page((unsigned long) data);
}

static int hypfs_sprp_create(void **data_ptr, void **free_ptr, size_t *size)
{
	unsigned long rc;
	void *data;

	data = (void *) get_zeroed_page(GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	rc = hypfs_sprp_diag304(data, DIAG304_QUERY_PRP);
	if (rc != 1) {
		*data_ptr = *free_ptr = NULL;
		*size = 0;
		free_page((unsigned long) data);
		return -EIO;
	}
	*data_ptr = *free_ptr = data;
	*size = PAGE_SIZE;
	return 0;
}

static int __hypfs_sprp_ioctl(void __user *user_area)
{
	struct hypfs_diag304 *diag304;
	unsigned long cmd;
	void __user *udata;
	void *data;
	int rc;

	rc = -ENOMEM;
	data = (void *)get_zeroed_page(GFP_KERNEL);
	diag304 = kzalloc(sizeof(*diag304), GFP_KERNEL);
	if (!data || !diag304)
		goto out;

	rc = -EFAULT;
	if (copy_from_user(diag304, user_area, sizeof(*diag304)))
		goto out;
	rc = -EINVAL;
	if ((diag304->args[0] >> 8) != 0 || diag304->args[1] > DIAG304_CMD_MAX)
		goto out;

	rc = -EFAULT;
	udata = (void __user *)(unsigned long) diag304->data;
	if (diag304->args[1] == DIAG304_SET_WEIGHTS ||
	    diag304->args[1] == DIAG304_SET_CAPPING)
		if (copy_from_user(data, udata, PAGE_SIZE))
			goto out;

	cmd = *(unsigned long *) &diag304->args[0];
	diag304->rc = hypfs_sprp_diag304(data, cmd);

	if (diag304->args[1] == DIAG304_QUERY_PRP)
		if (copy_to_user(udata, data, PAGE_SIZE)) {
			rc = -EFAULT;
			goto out;
		}

	rc = copy_to_user(user_area, diag304, sizeof(*diag304)) ? -EFAULT : 0;
out:
	kfree(diag304);
	free_page((unsigned long) data);
	return rc;
}

static long hypfs_sprp_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	void __user *argp;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (is_compat_task())
		argp = compat_ptr(arg);
	else
		argp = (void __user *) arg;
	switch (cmd) {
	case HYPFS_DIAG304:
		return __hypfs_sprp_ioctl(argp);
	default: /* unknown ioctl number */
		return -ENOTTY;
	}
	return 0;
}

static struct hypfs_dbfs_file hypfs_sprp_file = {
	.name		= "diag_304",
	.data_create	= hypfs_sprp_create,
	.data_free	= hypfs_sprp_free,
	.unlocked_ioctl = hypfs_sprp_ioctl,
};

void hypfs_sprp_init(void)
{
	if (!sclp.has_sprp)
		return;
	hypfs_dbfs_create_file(&hypfs_sprp_file);
}

void hypfs_sprp_exit(void)
{
	if (!sclp.has_sprp)
		return;
	hypfs_dbfs_remove_file(&hypfs_sprp_file);
}

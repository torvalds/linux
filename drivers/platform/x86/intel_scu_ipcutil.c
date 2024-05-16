// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Intel SCU IPC mechanism
 *
 * (C) Copyright 2008-2010 Intel Corporation
 * Author: Sreedhara DS (sreedhara.ds@intel.com)
 *
 * This driver provides IOCTL interfaces to call Intel SCU IPC driver API.
 */

#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <asm/intel_scu_ipc.h>

static int major;

static struct intel_scu_ipc_dev *scu;
static DEFINE_MUTEX(scu_lock);

/* IOCTL commands */
#define	INTE_SCU_IPC_REGISTER_READ	0
#define INTE_SCU_IPC_REGISTER_WRITE	1
#define INTE_SCU_IPC_REGISTER_UPDATE	2

struct scu_ipc_data {
	u32     count;  /* No. of registers */
	u16     addr[5]; /* Register addresses */
	u8      data[5]; /* Register data */
	u8      mask; /* Valid for read-modify-write */
};

/**
 *	scu_reg_access		-	implement register access ioctls
 *	@cmd: command we are doing (read/write/update)
 *	@data: kernel copy of ioctl data
 *
 *	Allow the user to perform register accesses on the SCU via the
 *	kernel interface
 */

static int scu_reg_access(u32 cmd, struct scu_ipc_data  *data)
{
	unsigned int count = data->count;

	if (count == 0 || count == 3 || count > 4)
		return -EINVAL;

	switch (cmd) {
	case INTE_SCU_IPC_REGISTER_READ:
		return intel_scu_ipc_dev_readv(scu, data->addr, data->data, count);
	case INTE_SCU_IPC_REGISTER_WRITE:
		return intel_scu_ipc_dev_writev(scu, data->addr, data->data, count);
	case INTE_SCU_IPC_REGISTER_UPDATE:
		return intel_scu_ipc_dev_update(scu, data->addr[0], data->data[0],
						data->mask);
	default:
		return -ENOTTY;
	}
}

/**
 *	scu_ipc_ioctl		-	control ioctls for the SCU
 *	@fp: file handle of the SCU device
 *	@cmd: ioctl coce
 *	@arg: pointer to user passed structure
 *
 *	Support the I/O and firmware flashing interfaces of the SCU
 */
static long scu_ipc_ioctl(struct file *fp, unsigned int cmd,
							unsigned long arg)
{
	int ret;
	struct scu_ipc_data  data;
	void __user *argp = (void __user *)arg;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	if (copy_from_user(&data, argp, sizeof(struct scu_ipc_data)))
		return -EFAULT;
	ret = scu_reg_access(cmd, &data);
	if (ret < 0)
		return ret;
	if (copy_to_user(argp, &data, sizeof(struct scu_ipc_data)))
		return -EFAULT;
	return 0;
}

static int scu_ipc_open(struct inode *inode, struct file *file)
{
	int ret = 0;

	/* Only single open at the time */
	mutex_lock(&scu_lock);
	if (scu) {
		ret = -EBUSY;
		goto unlock;
	}

	scu = intel_scu_ipc_dev_get();
	if (!scu)
		ret = -ENODEV;

unlock:
	mutex_unlock(&scu_lock);
	return ret;
}

static int scu_ipc_release(struct inode *inode, struct file *file)
{
	mutex_lock(&scu_lock);
	intel_scu_ipc_dev_put(scu);
	scu = NULL;
	mutex_unlock(&scu_lock);

	return 0;
}

static const struct file_operations scu_ipc_fops = {
	.unlocked_ioctl = scu_ipc_ioctl,
	.open = scu_ipc_open,
	.release = scu_ipc_release,
};

static int __init ipc_module_init(void)
{
	major = register_chrdev(0, "intel_mid_scu", &scu_ipc_fops);
	if (major < 0)
		return major;

	return 0;
}

static void __exit ipc_module_exit(void)
{
	unregister_chrdev(major, "intel_mid_scu");
}

module_init(ipc_module_init);
module_exit(ipc_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Utility driver for intel scu ipc");
MODULE_AUTHOR("Sreedhara <sreedhara.ds@intel.com>");

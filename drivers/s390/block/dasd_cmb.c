/*
 * Linux on zSeries Channel Measurement Facility support
 *  (dasd device driver interface)
 *
 * Copyright 2000,2003 IBM Corporation
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <asm/ccwdev.h>
#include <asm/cmb.h>

#include "dasd_int.h"

static int
dasd_ioctl_cmf_enable(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;

	device = bdev->bd_disk->private_data;
	if (!device)
		return -EINVAL;

	return enable_cmf(device->cdev);
}

static int
dasd_ioctl_cmf_disable(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;

	device = bdev->bd_disk->private_data;
	if (!device)
		return -EINVAL;

	return disable_cmf(device->cdev);
}

static int
dasd_ioctl_readall_cmb(struct block_device *bdev, int no, long args)
{
	struct dasd_device *device;
	struct cmbdata __user *udata;
	struct cmbdata data;
	size_t size;
	int ret;

	device = bdev->bd_disk->private_data;
	if (!device)
		return -EINVAL;
	udata = (void __user *) args;
	size = _IOC_SIZE(no);

	if (!access_ok(VERIFY_WRITE, udata, size))
		return -EFAULT;
	ret = cmf_readall(device->cdev, &data);
	if (ret)
		return ret;
	if (copy_to_user(udata, &data, min(size, sizeof(*udata))))
		return -EFAULT;
	return 0;
}

/* module initialization below here. dasd already provides a mechanism
 * to dynamically register ioctl functions, so we simply use this. */
static inline int
ioctl_reg(unsigned int no, dasd_ioctl_fn_t handler)
{
	return dasd_ioctl_no_register(THIS_MODULE, no, handler);
}

static inline void
ioctl_unreg(unsigned int no, dasd_ioctl_fn_t handler)
{
	dasd_ioctl_no_unregister(THIS_MODULE, no, handler);
}

static void
dasd_cmf_exit(void)
{
	ioctl_unreg(BIODASDCMFENABLE,  dasd_ioctl_cmf_enable);
	ioctl_unreg(BIODASDCMFDISABLE, dasd_ioctl_cmf_disable);
	ioctl_unreg(BIODASDREADALLCMB, dasd_ioctl_readall_cmb);
}

static int __init
dasd_cmf_init(void)
{
	int ret;
	ret = ioctl_reg (BIODASDCMFENABLE, dasd_ioctl_cmf_enable);
	if (ret)
		goto err;
	ret = ioctl_reg (BIODASDCMFDISABLE, dasd_ioctl_cmf_disable);
	if (ret)
		goto err;
	ret = ioctl_reg (BIODASDREADALLCMB, dasd_ioctl_readall_cmb);
	if (ret)
		goto err;

	return 0;
err:
	dasd_cmf_exit();

	return ret;
}

module_init(dasd_cmf_init);
module_exit(dasd_cmf_exit);

MODULE_AUTHOR("Arnd Bergmann <arndb@de.ibm.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("channel measurement facility interface for dasd\n"
		   "Copyright 2003 IBM Corporation\n");

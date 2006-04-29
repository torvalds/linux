/*
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This file contains support for diagnostic functions.  It is accessed by
 * opening the ipath_diag device, normally minor number 129.  Diagnostic use
 * of the InfiniPath chip may render the chip or board unusable until the
 * driver is unloaded, or in some cases, until the system is rebooted.
 *
 * Accesses to the chip through this interface are not similar to going
 * through the /sys/bus/pci resource mmap interface.
 */

#include <linux/pci.h>
#include <asm/uaccess.h>

#include "ipath_common.h"
#include "ipath_kernel.h"
#include "ips_common.h"
#include "ipath_layer.h"

int ipath_diag_inuse;
static int diag_set_link;

static int ipath_diag_open(struct inode *in, struct file *fp);
static int ipath_diag_release(struct inode *in, struct file *fp);
static ssize_t ipath_diag_read(struct file *fp, char __user *data,
			       size_t count, loff_t *off);
static ssize_t ipath_diag_write(struct file *fp, const char __user *data,
				size_t count, loff_t *off);

static struct file_operations diag_file_ops = {
	.owner = THIS_MODULE,
	.write = ipath_diag_write,
	.read = ipath_diag_read,
	.open = ipath_diag_open,
	.release = ipath_diag_release
};

static struct cdev *diag_cdev;
static struct class_device *diag_class_dev;

int ipath_diag_init(void)
{
	return ipath_cdev_init(IPATH_DIAG_MINOR, "ipath_diag",
			       &diag_file_ops, &diag_cdev, &diag_class_dev);
}

void ipath_diag_cleanup(void)
{
	ipath_cdev_cleanup(&diag_cdev, &diag_class_dev);
}

/**
 * ipath_read_umem64 - read a 64-bit quantity from the chip into user space
 * @dd: the infinipath device
 * @uaddr: the location to store the data in user memory
 * @caddr: the source chip address (full pointer, not offset)
 * @count: number of bytes to copy (multiple of 32 bits)
 *
 * This function also localizes all chip memory accesses.
 * The copy should be written such that we read full cacheline packets
 * from the chip.  This is usually used for a single qword
 *
 * NOTE:  This assumes the chip address is 64-bit aligned.
 */
static int ipath_read_umem64(struct ipath_devdata *dd, void __user *uaddr,
			     const void __iomem *caddr, size_t count)
{
	const u64 __iomem *reg_addr = caddr;
	const u64 __iomem *reg_end = reg_addr + (count / sizeof(u64));
	int ret;

	/* not very efficient, but it works for now */
	if (reg_addr < dd->ipath_kregbase ||
	    reg_end > dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	while (reg_addr < reg_end) {
		u64 data = readq(reg_addr);
		if (copy_to_user(uaddr, &data, sizeof(u64))) {
			ret = -EFAULT;
			goto bail;
		}
		reg_addr++;
		uaddr++;
	}
	ret = 0;
bail:
	return ret;
}

/**
 * ipath_write_umem64 - write a 64-bit quantity to the chip from user space
 * @dd: the infinipath device
 * @caddr: the destination chip address (full pointer, not offset)
 * @uaddr: the source of the data in user memory
 * @count: the number of bytes to copy (multiple of 32 bits)
 *
 * This is usually used for a single qword
 * NOTE:  This assumes the chip address is 64-bit aligned.
 */

static int ipath_write_umem64(struct ipath_devdata *dd, void __iomem *caddr,
			      const void __user *uaddr, size_t count)
{
	u64 __iomem *reg_addr = caddr;
	const u64 __iomem *reg_end = reg_addr + (count / sizeof(u64));
	int ret;

	/* not very efficient, but it works for now */
	if (reg_addr < dd->ipath_kregbase ||
	    reg_end > dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	while (reg_addr < reg_end) {
		u64 data;
		if (copy_from_user(&data, uaddr, sizeof(data))) {
			ret = -EFAULT;
			goto bail;
		}
		writeq(data, reg_addr);

		reg_addr++;
		uaddr++;
	}
	ret = 0;
bail:
	return ret;
}

/**
 * ipath_read_umem32 - read a 32-bit quantity from the chip into user space
 * @dd: the infinipath device
 * @uaddr: the location to store the data in user memory
 * @caddr: the source chip address (full pointer, not offset)
 * @count: number of bytes to copy
 *
 * read 32 bit values, not 64 bit; for memories that only
 * support 32 bit reads; usually a single dword.
 */
static int ipath_read_umem32(struct ipath_devdata *dd, void __user *uaddr,
			     const void __iomem *caddr, size_t count)
{
	const u32 __iomem *reg_addr = caddr;
	const u32 __iomem *reg_end = reg_addr + (count / sizeof(u32));
	int ret;

	if (reg_addr < (u32 __iomem *) dd->ipath_kregbase ||
	    reg_end > (u32 __iomem *) dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	/* not very efficient, but it works for now */
	while (reg_addr < reg_end) {
		u32 data = readl(reg_addr);
		if (copy_to_user(uaddr, &data, sizeof(data))) {
			ret = -EFAULT;
			goto bail;
		}

		reg_addr++;
		uaddr++;
	}
	ret = 0;
bail:
	return ret;
}

/**
 * ipath_write_umem32 - write a 32-bit quantity to the chip from user space
 * @dd: the infinipath device
 * @caddr: the destination chip address (full pointer, not offset)
 * @uaddr: the source of the data in user memory
 * @count: number of bytes to copy
 *
 * write 32 bit values, not 64 bit; for memories that only
 * support 32 bit write; usually a single dword.
 */

static int ipath_write_umem32(struct ipath_devdata *dd, void __iomem *caddr,
			      const void __user *uaddr, size_t count)
{
	u32 __iomem *reg_addr = caddr;
	const u32 __iomem *reg_end = reg_addr + (count / sizeof(u32));
	int ret;

	if (reg_addr < (u32 __iomem *) dd->ipath_kregbase ||
	    reg_end > (u32 __iomem *) dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	while (reg_addr < reg_end) {
		u32 data;
		if (copy_from_user(&data, uaddr, sizeof(data))) {
			ret = -EFAULT;
			goto bail;
		}
		writel(data, reg_addr);

		reg_addr++;
		uaddr++;
	}
	ret = 0;
bail:
	return ret;
}

static int ipath_diag_open(struct inode *in, struct file *fp)
{
	struct ipath_devdata *dd;
	int unit = 0; /* XXX this is bogus */
	unsigned long flags;
	int ret;

	dd = ipath_lookup(unit);

	mutex_lock(&ipath_mutex);
	spin_lock_irqsave(&ipath_devs_lock, flags);

	if (ipath_diag_inuse) {
		ret = -EBUSY;
		goto bail;
	}

	list_for_each_entry(dd, &ipath_dev_list, ipath_list) {
		/*
		 * we need at least one infinipath device to be present
		 * (don't use INITTED, because we want to be able to open
		 * even if device is in freeze mode, which cleared INITTED).
		 * There is a small amount of risk to this, which is why we
		 * also verify kregbase is set.
		 */

		if (!(dd->ipath_flags & IPATH_PRESENT) ||
		    !dd->ipath_kregbase)
			continue;

		ipath_diag_inuse = 1;
		diag_set_link = 0;
		ret = 0;
		goto bail;
	}

	ret = -ENODEV;

bail:
	spin_unlock_irqrestore(&ipath_devs_lock, flags);
	mutex_unlock(&ipath_mutex);

	/* Only expose a way to reset the device if we
	   make it into diag mode. */
	if (ret == 0)
		ipath_expose_reset(&dd->pcidev->dev);

	return ret;
}

static int ipath_diag_release(struct inode *i, struct file *f)
{
	mutex_lock(&ipath_mutex);
	ipath_diag_inuse = 0;
	mutex_unlock(&ipath_mutex);
	return 0;
}

static ssize_t ipath_diag_read(struct file *fp, char __user *data,
			       size_t count, loff_t *off)
{
	int unit = 0; /* XXX provide for reads on other units some day */
	struct ipath_devdata *dd;
	void __iomem *kreg_base;
	ssize_t ret;

	dd = ipath_lookup(unit);
	if (!dd) {
		ret = -ENODEV;
		goto bail;
	}

	kreg_base = dd->ipath_kregbase;

	if (count == 0)
		ret = 0;
	else if ((count % 4) || (*off % 4))
		/* address or length is not 32-bit aligned, hence invalid */
		ret = -EINVAL;
	else if ((count % 8) || (*off % 8))
		/* address or length not 64-bit aligned; do 32-bit reads */
		ret = ipath_read_umem32(dd, data, kreg_base + *off, count);
	else
		ret = ipath_read_umem64(dd, data, kreg_base + *off, count);

	if (ret >= 0) {
		*off += count;
		ret = count;
	}

bail:
	return ret;
}

static ssize_t ipath_diag_write(struct file *fp, const char __user *data,
				size_t count, loff_t *off)
{
	int unit = 0; /* XXX this is bogus */
	struct ipath_devdata *dd;
	void __iomem *kreg_base;
	ssize_t ret;

	dd = ipath_lookup(unit);
	if (!dd) {
		ret = -ENODEV;
		goto bail;
	}
	kreg_base = dd->ipath_kregbase;

	if (count == 0)
		ret = 0;
	else if ((count % 4) || (*off % 4))
		/* address or length is not 32-bit aligned, hence invalid */
		ret = -EINVAL;
	else if ((count % 8) || (*off % 8))
		/* address or length not 64-bit aligned; do 32-bit writes */
		ret = ipath_write_umem32(dd, kreg_base + *off, data, count);
	else
		ret = ipath_write_umem64(dd, kreg_base + *off, data, count);

	if (ret >= 0) {
		*off += count;
		ret = count;
	}

bail:
	return ret;
}

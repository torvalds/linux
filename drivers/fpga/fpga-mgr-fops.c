/*
 * FPGA Framework file operations
 *
 *  Copyright (C) 2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fpga.h>
#include <linux/vmalloc.h>
#include "fpga-mgr.h"

static ssize_t fpga_mgr_read(struct file *file, char __user *buf, size_t count,
		loff_t *offset)
{
	char *tmp;
	int ret = -EFAULT;
	struct fpga_manager *mgr = file->private_data;

	tmp = vmalloc(count);
	if (tmp == NULL)
		return -ENOMEM;

	ret = mgr->mops->read(mgr, tmp, count);
	if ((ret > 0) && copy_to_user(buf, tmp, ret))
		ret = -EFAULT;

	vfree(tmp);
	return ret;
}

static ssize_t fpga_mgr_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct fpga_manager *mgr = file->private_data;
	char *kern_buf;
	int ret;

	kern_buf = memdup_user(buf, count);
	if (IS_ERR(kern_buf))
		return PTR_ERR(kern_buf);

	ret = mgr->mops->write(mgr, kern_buf, count);
	kfree(kern_buf);
	return ret;
}

static int fpga_mgr_open(struct inode *inode, struct file *file)
{
	struct fpga_manager *mgr;
	struct fpga_manager_ops *mops;
	bool fmode_wr = (file->f_mode & FMODE_WRITE) != 0;
	bool fmode_rd = (file->f_mode & FMODE_READ) != 0;
	int ret = 0;

	mgr = container_of(inode->i_cdev, struct fpga_manager, cdev);
	if (!mgr)
		return -ENODEV;

	mops = mgr->mops;

	/* Don't allow read or write if we don't have read/write fns. */
	if ((fmode_wr && !mops->write) || (fmode_rd && !mops->read))
		return -EPERM;

	/* Need to know if we are going to read or write. Can't be both. */
	if (fmode_wr && fmode_rd)
		return -EPERM;

	if (test_and_set_bit_lock(FPGA_MGR_DEV_BUSY, &mgr->flags))
		return -EBUSY;

	file->private_data = mgr;

	if (fmode_wr && mops->write_init)
		ret = mops->write_init(mgr);
	else if (fmode_rd && mops->read_init)
		ret = mops->read_init(mgr);

	return ret;
}

static int fpga_mgr_release(struct inode *inode, struct file *file)
{
	struct fpga_manager *mgr = file->private_data;
	struct fpga_manager_ops *mops = mgr->mops;
	bool fmode_wr = (file->f_mode & FMODE_WRITE) != 0;
	bool fmode_rd = (file->f_mode & FMODE_READ) != 0;
	int ret = 0;

	if (fmode_wr && mops->write_complete)
		ret = mops->write_complete(mgr);
	else if (fmode_rd && mops->read_complete)
		ret = mops->read_complete(mgr);

	file->private_data = NULL;
	clear_bit_unlock(FPGA_MGR_DEV_BUSY, &mgr->flags);

	return ret;
}

const struct file_operations fpga_mgr_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= fpga_mgr_read,
	.write		= fpga_mgr_write,
	.open		= fpga_mgr_open,
	.release	= fpga_mgr_release,
};

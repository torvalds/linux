// SPDX-License-Identifier: GPL-2.0
/*
 * Debugfs interface Support for MPT (Message Passing Technology) based
 * controllers.
 *
 * Copyright (C) 2020  Broadcom Inc.
 *
 * Authors: Broadcom Inc.
 * Sreekanth Reddy  <sreekanth.reddy@broadcom.com>
 * Suganath Prabu <suganath-prabu.subramani@broadcom.com>
 *
 * Send feedback to : MPT-FusionLinux.pdl@broadcom.com)
 *
 **/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/compat.h>
#include <linux/uio.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include "mpt3sas_base.h"
#include <linux/debugfs.h>

static struct dentry *mpt3sas_debugfs_root;

/*
 * _debugfs_iocdump_read - copy ioc dump from debugfs buffer
 * @filep:	File Pointer
 * @ubuf:	Buffer to fill data
 * @cnt:	Length of the buffer
 * @ppos:	Offset in the file
 */

static ssize_t
_debugfs_iocdump_read(struct file *filp, char __user *ubuf, size_t cnt,
	loff_t *ppos)

{
	struct mpt3sas_debugfs_buffer *debug = filp->private_data;

	if (!debug || !debug->buf)
		return 0;

	return simple_read_from_buffer(ubuf, cnt, ppos, debug->buf, debug->len);
}

/*
 * _debugfs_iocdump_open :	open the ioc_dump debugfs attribute file
 */
static int
_debugfs_iocdump_open(struct inode *inode, struct file *file)
{
	struct MPT3SAS_ADAPTER *ioc = inode->i_private;
	struct mpt3sas_debugfs_buffer *debug;

	debug = kzalloc(sizeof(struct mpt3sas_debugfs_buffer), GFP_KERNEL);
	if (!debug)
		return -ENOMEM;

	debug->buf = (void *)ioc;
	debug->len = sizeof(struct MPT3SAS_ADAPTER);
	file->private_data = debug;
	return 0;
}

/*
 * _debugfs_iocdump_release :	release the ioc_dump debugfs attribute
 * @inode: inode structure to the corresponds device
 * @file: File pointer
 */
static int
_debugfs_iocdump_release(struct inode *inode, struct file *file)
{
	struct mpt3sas_debugfs_buffer *debug = file->private_data;

	if (!debug)
		return 0;

	file->private_data = NULL;
	kfree(debug);
	return 0;
}

static const struct file_operations mpt3sas_debugfs_iocdump_fops = {
	.owner		= THIS_MODULE,
	.open           = _debugfs_iocdump_open,
	.read           = _debugfs_iocdump_read,
	.release        = _debugfs_iocdump_release,
};

/*
 * mpt3sas_init_debugfs :	Create debugfs root for mpt3sas driver
 */
void mpt3sas_init_debugfs(void)
{
	mpt3sas_debugfs_root = debugfs_create_dir("mpt3sas", NULL);
	if (!mpt3sas_debugfs_root)
		pr_info("mpt3sas: Cannot create debugfs root\n");
}

/*
 * mpt3sas_exit_debugfs :	Remove debugfs root for mpt3sas driver
 */
void mpt3sas_exit_debugfs(void)
{
	debugfs_remove_recursive(mpt3sas_debugfs_root);
}

/*
 * mpt3sas_setup_debugfs :	Setup debugfs per HBA adapter
 * ioc:				MPT3SAS_ADAPTER object
 */
void
mpt3sas_setup_debugfs(struct MPT3SAS_ADAPTER *ioc)
{
	char name[64];

	snprintf(name, sizeof(name), "scsi_host%d", ioc->shost->host_no);
	if (!ioc->debugfs_root) {
		ioc->debugfs_root =
		    debugfs_create_dir(name, mpt3sas_debugfs_root);
		if (!ioc->debugfs_root) {
			dev_err(&ioc->pdev->dev,
			    "Cannot create per adapter debugfs directory\n");
			return;
		}
	}

	snprintf(name, sizeof(name), "ioc_dump");
	ioc->ioc_dump =	debugfs_create_file(name, 0444,
	    ioc->debugfs_root, ioc, &mpt3sas_debugfs_iocdump_fops);
	if (!ioc->ioc_dump) {
		dev_err(&ioc->pdev->dev,
		    "Cannot create ioc_dump debugfs file\n");
		debugfs_remove(ioc->debugfs_root);
		return;
	}

	snprintf(name, sizeof(name), "host_recovery");
	debugfs_create_u8(name, 0444, ioc->debugfs_root, &ioc->shost_recovery);

}

/*
 * mpt3sas_destroy_debugfs :	Destroy debugfs per HBA adapter
 * @ioc:	MPT3SAS_ADAPTER object
 */
void mpt3sas_destroy_debugfs(struct MPT3SAS_ADAPTER *ioc)
{
	debugfs_remove_recursive(ioc->debugfs_root);
}


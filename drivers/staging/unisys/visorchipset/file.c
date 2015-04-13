/* file.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* This contains the implementation that allows a usermode program to
 * communicate with the visorchipset driver using a device/file interface.
 */

#include "globals.h"
#include "visorchannel.h"
#include <linux/mm.h>
#include <linux/fs.h>
#include "uisutils.h"
#include "file.h"

#define CURRENT_FILE_PC VISOR_CHIPSET_PC_file_c

static struct cdev file_cdev;
static struct visorchannel **file_controlvm_channel;

void
visorchipset_file_cleanup(dev_t major_dev)
{
	if (file_cdev.ops != NULL)
		cdev_del(&file_cdev);
	file_cdev.ops = NULL;
	unregister_chrdev_region(major_dev, 1);
}

static int
visorchipset_open(struct inode *inode, struct file *file)
{
	unsigned minor_number = iminor(inode);

	if (minor_number != 0)
		return -ENODEV;
	file->private_data = NULL;
	return 0;
}

static int
visorchipset_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int
visorchipset_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long physaddr = 0;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	GUEST_PHYSICAL_ADDRESS addr = 0;

	/* sv_enable_dfp(); */
	if (offset & (PAGE_SIZE - 1))
		return -ENXIO;	/* need aligned offsets */

	switch (offset) {
	case VISORCHIPSET_MMAP_CONTROLCHANOFFSET:
		vma->vm_flags |= VM_IO;
		if (*file_controlvm_channel == NULL) {
			return -ENXIO;
		}
		visorchannel_read(*file_controlvm_channel,
			offsetof(struct spar_controlvm_channel_protocol,
				 gp_control_channel),
			&addr, sizeof(addr));
		if (addr == 0) {
			return -ENXIO;
		}
		physaddr = (unsigned long)addr;
		if (remap_pfn_range(vma, vma->vm_start,
				    physaddr >> PAGE_SHIFT,
				    vma->vm_end - vma->vm_start,
				    /*pgprot_noncached */
				    (vma->vm_page_prot))) {
			return -EAGAIN;
		}
		break;
	default:
		return -ENOSYS;
	}
	return 0;
}

static long visorchipset_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	s64 adjustment;
	s64 vrtc_offset;

	switch (cmd) {
	case VMCALL_QUERY_GUEST_VIRTUAL_TIME_OFFSET:
		/* get the physical rtc offset */
		vrtc_offset = issue_vmcall_query_guest_virtual_time_offset();
		if (copy_to_user
		    ((void __user *)arg, &vrtc_offset, sizeof(vrtc_offset))) {
			return -EFAULT;
		}
		return SUCCESS;
	case VMCALL_UPDATE_PHYSICAL_TIME:
		if (copy_from_user
		    (&adjustment, (void __user *)arg, sizeof(adjustment))) {
			return -EFAULT;
		}
		return issue_vmcall_update_physical_time(adjustment);
	default:
		return -EFAULT;
	}
}

static const struct file_operations visorchipset_fops = {
	.owner = THIS_MODULE,
	.open = visorchipset_open,
	.read = NULL,
	.write = NULL,
	.unlocked_ioctl = visorchipset_ioctl,
	.release = visorchipset_release,
	.mmap = visorchipset_mmap,
};

int
visorchipset_file_init(dev_t major_dev, struct visorchannel **controlvm_channel)
{
	int rc = 0;

	file_controlvm_channel = controlvm_channel;
	cdev_init(&file_cdev, &visorchipset_fops);
	file_cdev.owner = THIS_MODULE;
	if (MAJOR(major_dev) == 0) {
		rc = alloc_chrdev_region(&major_dev, 0, 1, MYDRVNAME);
		/* dynamic major device number registration required */
		if (rc < 0)
			return rc;
	} else {
		/* static major device number registration required */
		rc = register_chrdev_region(major_dev, 1, MYDRVNAME);
		if (rc < 0)
			return rc;
	}
	rc = cdev_add(&file_cdev, MKDEV(MAJOR(major_dev), 0), 1);
	if (rc < 0) {
		unregister_chrdev_region(major_dev, 1);
		return rc;
	}
	return 0;
}

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

static struct cdev Cdev;
static VISORCHANNEL **PControlVm_channel;
static dev_t MajorDev = -1; /**< indicates major num for device */
static BOOL Registered = FALSE;

static int visorchipset_open(struct inode *inode, struct file *file);
static int visorchipset_release(struct inode *inode, struct file *file);
static int visorchipset_mmap(struct file *file, struct vm_area_struct *vma);
#ifdef HAVE_UNLOCKED_IOCTL
long visorchipset_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#else
int visorchipset_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg);
#endif

static const struct file_operations visorchipset_fops = {
	.owner = THIS_MODULE,
	.open = visorchipset_open,
	.read = NULL,
	.write = NULL,
#ifdef HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl = visorchipset_ioctl,
#else
	.ioctl = visorchipset_ioctl,
#endif
	.release = visorchipset_release,
	.mmap = visorchipset_mmap,
};

int
visorchipset_file_init(dev_t majorDev, VISORCHANNEL **pControlVm_channel)
{
	int rc = -1;

	PControlVm_channel = pControlVm_channel;
	MajorDev = majorDev;
	cdev_init(&Cdev, &visorchipset_fops);
	Cdev.owner = THIS_MODULE;
	if (MAJOR(MajorDev) == 0) {
		/* dynamic major device number registration required */
		if (alloc_chrdev_region(&MajorDev, 0, 1, MYDRVNAME) < 0) {
			ERRDRV("Unable to allocate+register char device %s",
			       MYDRVNAME);
			goto Away;
		}
		Registered = TRUE;
		INFODRV("New major number %d registered\n", MAJOR(MajorDev));
	} else {
		/* static major device number registration required */
		if (register_chrdev_region(MajorDev, 1, MYDRVNAME) < 0) {
			ERRDRV("Unable to register char device %s", MYDRVNAME);
			goto Away;
		}
		Registered = TRUE;
		INFODRV("Static major number %d registered\n", MAJOR(MajorDev));
	}
	if (cdev_add(&Cdev, MKDEV(MAJOR(MajorDev), 0), 1) < 0) {
		ERRDRV("failed to create char device: (status=%d)\n", rc);
		goto Away;
	}
	INFODRV("Registered char device for %s (major=%d)",
		MYDRVNAME, MAJOR(MajorDev));
	rc = 0;
Away:
	return rc;
}

void
visorchipset_file_cleanup(void)
{
	if (Cdev.ops != NULL)
		cdev_del(&Cdev);
	Cdev.ops = NULL;
	if (Registered) {
		if (MAJOR(MajorDev) >= 0) {
			unregister_chrdev_region(MajorDev, 1);
			MajorDev = MKDEV(0, 0);
		}
		Registered = FALSE;
	}
}

static int
visorchipset_open(struct inode *inode, struct file *file)
{
	unsigned minor_number = iminor(inode);
	int rc = -ENODEV;

	DEBUGDRV("%s", __func__);
	if (minor_number != 0)
		goto Away;
	file->private_data = NULL;
	rc = 0;
Away:
	if (rc < 0)
		ERRDRV("%s minor=%d failed", __func__, minor_number);
	return rc;
}

static int
visorchipset_release(struct inode *inode, struct file *file)
{
	DEBUGDRV("%s", __func__);
	return 0;
}

static int
visorchipset_mmap(struct file *file, struct vm_area_struct *vma)
{
	ulong physAddr = 0;
	ulong offset = vma->vm_pgoff << PAGE_SHIFT;
	GUEST_PHYSICAL_ADDRESS addr = 0;

	/* sv_enable_dfp(); */
	DEBUGDRV("%s", __func__);
	if (offset & (PAGE_SIZE - 1)) {
		ERRDRV("%s virtual address NOT page-aligned!", __func__);
		return -ENXIO;	/* need aligned offsets */
	}
	switch (offset) {
	case VISORCHIPSET_MMAP_CONTROLCHANOFFSET:
		vma->vm_flags |= VM_IO;
		if (*PControlVm_channel == NULL) {
			ERRDRV("%s no controlvm channel yet", __func__);
			return -ENXIO;
		}
		visorchannel_read(*PControlVm_channel,
				  offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
					   gpControlChannel), &addr,
				  sizeof(addr));
		if (addr == 0) {
			ERRDRV("%s control channel address is 0", __func__);
			return -ENXIO;
		}
		physAddr = (ulong) (addr);
		DEBUGDRV("mapping physical address = 0x%lx", physAddr);
		if (remap_pfn_range(vma, vma->vm_start,
				    physAddr >> PAGE_SHIFT,
				    vma->vm_end - vma->vm_start,
				    /*pgprot_noncached */
				    (vma->vm_page_prot))) {
			ERRDRV("%s remap_pfn_range failed", __func__);
			return -EAGAIN;
		}
		break;
	default:
		return -ENOSYS;
	}
	DEBUGDRV("%s success!", __func__);
	return 0;
}

#ifdef HAVE_UNLOCKED_IOCTL
long
visorchipset_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
#else
int
visorchipset_ioctl(struct inode *inode, struct file *file,
		   unsigned int cmd, unsigned long arg)
#endif
{
	int rc = SUCCESS;
	S64 adjustment;
	S64 vrtc_offset;
	DBGINF("entered visorchipset_ioctl, cmd=%d", cmd);
	switch (cmd) {
	case VMCALL_QUERY_GUEST_VIRTUAL_TIME_OFFSET:
		/* get the physical rtc offset */
		vrtc_offset = Issue_VMCALL_QUERY_GUEST_VIRTUAL_TIME_OFFSET();
		if (copy_to_user
		    ((void __user *)arg, &vrtc_offset, sizeof(vrtc_offset))) {
			rc = -EFAULT;
			goto Away;
		}
		DBGINF("insde visorchipset_ioctl, cmd=%d, vrtc_offset=%lld",
		       cmd, vrtc_offset);
		break;
	case VMCALL_UPDATE_PHYSICAL_TIME:
		if (copy_from_user
		    (&adjustment, (void __user *)arg, sizeof(adjustment))) {
			rc = -EFAULT;
			goto Away;
		}
		DBGINF("insde visorchipset_ioctl, cmd=%d, adjustment=%lld", cmd,
		       adjustment);
		rc = Issue_VMCALL_UPDATE_PHYSICAL_TIME(adjustment);
		break;
	default:
		LOGERR("visorchipset_ioctl received invalid command");
		rc = -EFAULT;
		break;
	}
Away:
	DBGINF("exiting %d!", rc);
	return rc;
}

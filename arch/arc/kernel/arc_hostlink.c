// SPDX-License-Identifier: GPL-2.0-only
/*
 * arc_hostlink.c: Pseudo-driver for Metaware provided "hostlink" facility
 *
 * Allows Linux userland access to host in absence of any peripherals.
 *
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/fs.h>		/* file_operations */
#include <linux/miscdevice.h>
#include <linux/mm.h>		/* VM_IO */
#include <linux/module.h>
#include <linux/uaccess.h>

static unsigned char __HOSTLINK__[4 * PAGE_SIZE] __aligned(PAGE_SIZE);

static int arc_hl_mmap(struct file *fp, struct vm_area_struct *vma)
{
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot)) {
		pr_warn("Hostlink buffer mmap ERROR\n");
		return -EAGAIN;
	}
	return 0;
}

static long arc_hl_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	/* we only support, returning the physical addr to mmap in user space */
	put_user((unsigned int)__HOSTLINK__, (int __user *)arg);
	return 0;
}

static const struct file_operations arc_hl_fops = {
	.unlocked_ioctl	= arc_hl_ioctl,
	.mmap		= arc_hl_mmap,
};

static struct miscdevice arc_hl_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "hostlink",
	.fops	= &arc_hl_fops
};

static int __init arc_hl_init(void)
{
	pr_info("ARC Hostlink driver mmap at 0x%p\n", __HOSTLINK__);
	return misc_register(&arc_hl_dev);
}
module_init(arc_hl_init);

// SPDX-License-Identifier: GPL-2.0
/*  Copyright(c) 2016-20 Intel Corporation. */

#include <linux/acpi.h>
#include <linux/miscdevice.h>
#include <linux/mman.h>
#include <linux/security.h>
#include <linux/suspend.h>
#include <asm/traps.h>
#include "driver.h"
#include "encl.h"

static int sgx_open(struct inode *inode, struct file *file)
{
	struct sgx_encl *encl;

	encl = kzalloc(sizeof(*encl), GFP_KERNEL);
	if (!encl)
		return -ENOMEM;

	xa_init(&encl->page_array);
	mutex_init(&encl->lock);

	file->private_data = encl;

	return 0;
}

static int sgx_release(struct inode *inode, struct file *file)
{
	struct sgx_encl *encl = file->private_data;
	struct sgx_encl_page *entry;
	unsigned long index;

	xa_for_each(&encl->page_array, index, entry) {
		if (entry->epc_page) {
			sgx_free_epc_page(entry->epc_page);
			encl->secs_child_cnt--;
			entry->epc_page = NULL;
		}

		kfree(entry);
	}

	xa_destroy(&encl->page_array);

	if (!encl->secs_child_cnt && encl->secs.epc_page) {
		sgx_free_epc_page(encl->secs.epc_page);
		encl->secs.epc_page = NULL;
	}

	/* Detect EPC page leaks. */
	WARN_ON_ONCE(encl->secs_child_cnt);
	WARN_ON_ONCE(encl->secs.epc_page);

	kfree(encl);
	return 0;
}

static int sgx_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sgx_encl *encl = file->private_data;
	int ret;

	ret = sgx_encl_may_map(encl, vma->vm_start, vma->vm_end, vma->vm_flags);
	if (ret)
		return ret;

	vma->vm_ops = &sgx_vm_ops;
	vma->vm_flags |= VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP | VM_IO;
	vma->vm_private_data = encl;

	return 0;
}

static unsigned long sgx_get_unmapped_area(struct file *file,
					   unsigned long addr,
					   unsigned long len,
					   unsigned long pgoff,
					   unsigned long flags)
{
	if ((flags & MAP_TYPE) == MAP_PRIVATE)
		return -EINVAL;

	if (flags & MAP_FIXED)
		return addr;

	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

#ifdef CONFIG_COMPAT
static long sgx_compat_ioctl(struct file *filep, unsigned int cmd,
			      unsigned long arg)
{
	return sgx_ioctl(filep, cmd, arg);
}
#endif

static const struct file_operations sgx_encl_fops = {
	.owner			= THIS_MODULE,
	.open			= sgx_open,
	.release		= sgx_release,
	.unlocked_ioctl		= sgx_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= sgx_compat_ioctl,
#endif
	.mmap			= sgx_mmap,
	.get_unmapped_area	= sgx_get_unmapped_area,
};

static struct miscdevice sgx_dev_enclave = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "sgx_enclave",
	.nodename = "sgx_enclave",
	.fops = &sgx_encl_fops,
};

int __init sgx_drv_init(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_SGX_LC))
		return -ENODEV;

	return misc_register(&sgx_dev_enclave);
}

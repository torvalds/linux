// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016-2018 Intel Corporation. All rights reserved. */
#include <linux/memremap.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pfn_t.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include "dax-private.h"
#include "bus.h"

static int check_vma(struct dev_dax *dev_dax, struct vm_area_struct *vma,
		const char *func)
{
	struct device *dev = &dev_dax->dev;
	unsigned long mask;

	if (!dax_alive(dev_dax->dax_dev))
		return -ENXIO;

	/* prevent private mappings from being established */
	if ((vma->vm_flags & VM_MAYSHARE) != VM_MAYSHARE) {
		dev_info_ratelimited(dev,
				"%s: %s: fail, attempted private mapping\n",
				current->comm, func);
		return -EINVAL;
	}

	mask = dev_dax->align - 1;
	if (vma->vm_start & mask || vma->vm_end & mask) {
		dev_info_ratelimited(dev,
				"%s: %s: fail, unaligned vma (%#lx - %#lx, %#lx)\n",
				current->comm, func, vma->vm_start, vma->vm_end,
				mask);
		return -EINVAL;
	}

	if (!vma_is_dax(vma)) {
		dev_info_ratelimited(dev,
				"%s: %s: fail, vma is not DAX capable\n",
				current->comm, func);
		return -EINVAL;
	}

	return 0;
}

/* see "strong" declaration in tools/testing/nvdimm/dax-dev.c */
__weak phys_addr_t dax_pgoff_to_phys(struct dev_dax *dev_dax, pgoff_t pgoff,
		unsigned long size)
{
	int i;

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct dev_dax_range *dax_range = &dev_dax->ranges[i];
		struct range *range = &dax_range->range;
		unsigned long long pgoff_end;
		phys_addr_t phys;

		pgoff_end = dax_range->pgoff + PHYS_PFN(range_len(range)) - 1;
		if (pgoff < dax_range->pgoff || pgoff > pgoff_end)
			continue;
		phys = PFN_PHYS(pgoff - dax_range->pgoff) + range->start;
		if (phys + size - 1 <= range->end)
			return phys;
		break;
	}
	return -1;
}

static void dax_set_mapping(struct vm_fault *vmf, pfn_t pfn,
			      unsigned long fault_size)
{
	unsigned long i, nr_pages = fault_size / PAGE_SIZE;
	struct file *filp = vmf->vma->vm_file;
	struct dev_dax *dev_dax = filp->private_data;
	pgoff_t pgoff;

	/* mapping is only set on the head */
	if (dev_dax->pgmap->vmemmap_shift)
		nr_pages = 1;

	pgoff = linear_page_index(vmf->vma,
			ALIGN(vmf->address, fault_size));

	for (i = 0; i < nr_pages; i++) {
		struct page *page = pfn_to_page(pfn_t_to_pfn(pfn) + i);

		page = compound_head(page);
		if (page->mapping)
			continue;

		page->mapping = filp->f_mapping;
		page->index = pgoff + i;
	}
}

static vm_fault_t __dev_dax_pte_fault(struct dev_dax *dev_dax,
				struct vm_fault *vmf)
{
	struct device *dev = &dev_dax->dev;
	phys_addr_t phys;
	pfn_t pfn;
	unsigned int fault_size = PAGE_SIZE;

	if (check_vma(dev_dax, vmf->vma, __func__))
		return VM_FAULT_SIGBUS;

	if (dev_dax->align > PAGE_SIZE) {
		dev_dbg(dev, "alignment (%#x) > fault size (%#x)\n",
			dev_dax->align, fault_size);
		return VM_FAULT_SIGBUS;
	}

	if (fault_size != dev_dax->align)
		return VM_FAULT_SIGBUS;

	phys = dax_pgoff_to_phys(dev_dax, vmf->pgoff, PAGE_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "pgoff_to_phys(%#lx) failed\n", vmf->pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, PFN_DEV|PFN_MAP);

	dax_set_mapping(vmf, pfn, fault_size);

	return vmf_insert_mixed(vmf->vma, vmf->address, pfn);
}

static vm_fault_t __dev_dax_pmd_fault(struct dev_dax *dev_dax,
				struct vm_fault *vmf)
{
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	struct device *dev = &dev_dax->dev;
	phys_addr_t phys;
	pgoff_t pgoff;
	pfn_t pfn;
	unsigned int fault_size = PMD_SIZE;

	if (check_vma(dev_dax, vmf->vma, __func__))
		return VM_FAULT_SIGBUS;

	if (dev_dax->align > PMD_SIZE) {
		dev_dbg(dev, "alignment (%#x) > fault size (%#x)\n",
			dev_dax->align, fault_size);
		return VM_FAULT_SIGBUS;
	}

	if (fault_size < dev_dax->align)
		return VM_FAULT_SIGBUS;
	else if (fault_size > dev_dax->align)
		return VM_FAULT_FALLBACK;

	/* if we are outside of the VMA */
	if (pmd_addr < vmf->vma->vm_start ||
			(pmd_addr + PMD_SIZE) > vmf->vma->vm_end)
		return VM_FAULT_SIGBUS;

	pgoff = linear_page_index(vmf->vma, pmd_addr);
	phys = dax_pgoff_to_phys(dev_dax, pgoff, PMD_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "pgoff_to_phys(%#lx) failed\n", pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, PFN_DEV|PFN_MAP);

	dax_set_mapping(vmf, pfn, fault_size);

	return vmf_insert_pfn_pmd(vmf, pfn, vmf->flags & FAULT_FLAG_WRITE);
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static vm_fault_t __dev_dax_pud_fault(struct dev_dax *dev_dax,
				struct vm_fault *vmf)
{
	unsigned long pud_addr = vmf->address & PUD_MASK;
	struct device *dev = &dev_dax->dev;
	phys_addr_t phys;
	pgoff_t pgoff;
	pfn_t pfn;
	unsigned int fault_size = PUD_SIZE;


	if (check_vma(dev_dax, vmf->vma, __func__))
		return VM_FAULT_SIGBUS;

	if (dev_dax->align > PUD_SIZE) {
		dev_dbg(dev, "alignment (%#x) > fault size (%#x)\n",
			dev_dax->align, fault_size);
		return VM_FAULT_SIGBUS;
	}

	if (fault_size < dev_dax->align)
		return VM_FAULT_SIGBUS;
	else if (fault_size > dev_dax->align)
		return VM_FAULT_FALLBACK;

	/* if we are outside of the VMA */
	if (pud_addr < vmf->vma->vm_start ||
			(pud_addr + PUD_SIZE) > vmf->vma->vm_end)
		return VM_FAULT_SIGBUS;

	pgoff = linear_page_index(vmf->vma, pud_addr);
	phys = dax_pgoff_to_phys(dev_dax, pgoff, PUD_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "pgoff_to_phys(%#lx) failed\n", pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, PFN_DEV|PFN_MAP);

	dax_set_mapping(vmf, pfn, fault_size);

	return vmf_insert_pfn_pud(vmf, pfn, vmf->flags & FAULT_FLAG_WRITE);
}
#else
static vm_fault_t __dev_dax_pud_fault(struct dev_dax *dev_dax,
				struct vm_fault *vmf)
{
	return VM_FAULT_FALLBACK;
}
#endif /* !CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static vm_fault_t dev_dax_huge_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size)
{
	struct file *filp = vmf->vma->vm_file;
	vm_fault_t rc = VM_FAULT_SIGBUS;
	int id;
	struct dev_dax *dev_dax = filp->private_data;

	dev_dbg(&dev_dax->dev, "%s: %s (%#lx - %#lx) size = %d\n", current->comm,
			(vmf->flags & FAULT_FLAG_WRITE) ? "write" : "read",
			vmf->vma->vm_start, vmf->vma->vm_end, pe_size);

	id = dax_read_lock();
	switch (pe_size) {
	case PE_SIZE_PTE:
		rc = __dev_dax_pte_fault(dev_dax, vmf);
		break;
	case PE_SIZE_PMD:
		rc = __dev_dax_pmd_fault(dev_dax, vmf);
		break;
	case PE_SIZE_PUD:
		rc = __dev_dax_pud_fault(dev_dax, vmf);
		break;
	default:
		rc = VM_FAULT_SIGBUS;
	}

	dax_read_unlock(id);

	return rc;
}

static vm_fault_t dev_dax_fault(struct vm_fault *vmf)
{
	return dev_dax_huge_fault(vmf, PE_SIZE_PTE);
}

static int dev_dax_may_split(struct vm_area_struct *vma, unsigned long addr)
{
	struct file *filp = vma->vm_file;
	struct dev_dax *dev_dax = filp->private_data;

	if (!IS_ALIGNED(addr, dev_dax->align))
		return -EINVAL;
	return 0;
}

static unsigned long dev_dax_pagesize(struct vm_area_struct *vma)
{
	struct file *filp = vma->vm_file;
	struct dev_dax *dev_dax = filp->private_data;

	return dev_dax->align;
}

static const struct vm_operations_struct dax_vm_ops = {
	.fault = dev_dax_fault,
	.huge_fault = dev_dax_huge_fault,
	.may_split = dev_dax_may_split,
	.pagesize = dev_dax_pagesize,
};

static int dax_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct dev_dax *dev_dax = filp->private_data;
	int rc, id;

	dev_dbg(&dev_dax->dev, "trace\n");

	/*
	 * We lock to check dax_dev liveness and will re-check at
	 * fault time.
	 */
	id = dax_read_lock();
	rc = check_vma(dev_dax, vma, __func__);
	dax_read_unlock(id);
	if (rc)
		return rc;

	vma->vm_ops = &dax_vm_ops;
	vm_flags_set(vma, VM_HUGEPAGE);
	return 0;
}

/* return an unmapped area aligned to the dax region specified alignment */
static unsigned long dax_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	unsigned long off, off_end, off_align, len_align, addr_align, align;
	struct dev_dax *dev_dax = filp ? filp->private_data : NULL;

	if (!dev_dax || addr)
		goto out;

	align = dev_dax->align;
	off = pgoff << PAGE_SHIFT;
	off_end = off + len;
	off_align = round_up(off, align);

	if ((off_end <= off_align) || ((off_end - off_align) < align))
		goto out;

	len_align = len + align;
	if ((off + len_align) < off)
		goto out;

	addr_align = current->mm->get_unmapped_area(filp, addr, len_align,
			pgoff, flags);
	if (!IS_ERR_VALUE(addr_align)) {
		addr_align += (off - addr_align) & (align - 1);
		return addr_align;
	}
 out:
	return current->mm->get_unmapped_area(filp, addr, len, pgoff, flags);
}

static const struct address_space_operations dev_dax_aops = {
	.dirty_folio	= noop_dirty_folio,
};

static int dax_open(struct inode *inode, struct file *filp)
{
	struct dax_device *dax_dev = inode_dax(inode);
	struct inode *__dax_inode = dax_inode(dax_dev);
	struct dev_dax *dev_dax = dax_get_private(dax_dev);

	dev_dbg(&dev_dax->dev, "trace\n");
	inode->i_mapping = __dax_inode->i_mapping;
	inode->i_mapping->host = __dax_inode;
	inode->i_mapping->a_ops = &dev_dax_aops;
	filp->f_mapping = inode->i_mapping;
	filp->f_wb_err = filemap_sample_wb_err(filp->f_mapping);
	filp->f_sb_err = file_sample_sb_err(filp);
	filp->private_data = dev_dax;
	inode->i_flags = S_DAX;

	return 0;
}

static int dax_release(struct inode *inode, struct file *filp)
{
	struct dev_dax *dev_dax = filp->private_data;

	dev_dbg(&dev_dax->dev, "trace\n");
	return 0;
}

static const struct file_operations dax_fops = {
	.llseek = noop_llseek,
	.owner = THIS_MODULE,
	.open = dax_open,
	.release = dax_release,
	.get_unmapped_area = dax_get_unmapped_area,
	.mmap = dax_mmap,
	.mmap_supported_flags = MAP_SYNC,
};

static void dev_dax_cdev_del(void *cdev)
{
	cdev_del(cdev);
}

static void dev_dax_kill(void *dev_dax)
{
	kill_dev_dax(dev_dax);
}

int dev_dax_probe(struct dev_dax *dev_dax)
{
	struct dax_device *dax_dev = dev_dax->dax_dev;
	struct device *dev = &dev_dax->dev;
	struct dev_pagemap *pgmap;
	struct inode *inode;
	struct cdev *cdev;
	void *addr;
	int rc, i;

	if (static_dev_dax(dev_dax))  {
		if (dev_dax->nr_range > 1) {
			dev_warn(dev,
				"static pgmap / multi-range device conflict\n");
			return -EINVAL;
		}

		pgmap = dev_dax->pgmap;
	} else {
		if (dev_dax->pgmap) {
			dev_warn(dev,
				 "dynamic-dax with pre-populated page map\n");
			return -EINVAL;
		}

		pgmap = devm_kzalloc(dev,
                       struct_size(pgmap, ranges, dev_dax->nr_range - 1),
                       GFP_KERNEL);
		if (!pgmap)
			return -ENOMEM;

		pgmap->nr_range = dev_dax->nr_range;
		dev_dax->pgmap = pgmap;

		for (i = 0; i < dev_dax->nr_range; i++) {
			struct range *range = &dev_dax->ranges[i].range;
			pgmap->ranges[i] = *range;
		}
	}

	for (i = 0; i < dev_dax->nr_range; i++) {
		struct range *range = &dev_dax->ranges[i].range;

		if (!devm_request_mem_region(dev, range->start,
					range_len(range), dev_name(dev))) {
			dev_warn(dev, "mapping%d: %#llx-%#llx could not reserve range\n",
					i, range->start, range->end);
			return -EBUSY;
		}
	}

	pgmap->type = MEMORY_DEVICE_GENERIC;
	if (dev_dax->align > PAGE_SIZE)
		pgmap->vmemmap_shift =
			order_base_2(dev_dax->align >> PAGE_SHIFT);
	addr = devm_memremap_pages(dev, pgmap);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	inode = dax_inode(dax_dev);
	cdev = inode->i_cdev;
	cdev_init(cdev, &dax_fops);
	cdev->owner = dev->driver->owner;
	cdev_set_parent(cdev, &dev->kobj);
	rc = cdev_add(cdev, dev->devt, 1);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, dev_dax_cdev_del, cdev);
	if (rc)
		return rc;

	run_dax(dax_dev);
	return devm_add_action_or_reset(dev, dev_dax_kill, dev_dax);
}
EXPORT_SYMBOL_GPL(dev_dax_probe);

static struct dax_device_driver device_dax_driver = {
	.probe = dev_dax_probe,
	.type = DAXDRV_DEVICE_TYPE,
};

static int __init dax_init(void)
{
	return dax_driver_register(&device_dax_driver);
}

static void __exit dax_exit(void)
{
	dax_driver_unregister(&device_dax_driver);
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
module_init(dax_init);
module_exit(dax_exit);
MODULE_ALIAS_DAX_DEVICE(0);

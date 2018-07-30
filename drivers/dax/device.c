/*
 * Copyright(c) 2016 - 2017 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
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
#include "dax.h"

static struct class *dax_class;

/*
 * Rely on the fact that drvdata is set before the attributes are
 * registered, and that the attributes are unregistered before drvdata
 * is cleared to assume that drvdata is always valid.
 */
static ssize_t id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", dax_region->id);
}
static DEVICE_ATTR_RO(id);

static ssize_t region_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);

	return sprintf(buf, "%llu\n", (unsigned long long)
			resource_size(&dax_region->res));
}
static struct device_attribute dev_attr_region_size = __ATTR(size, 0444,
		region_size_show, NULL);

static ssize_t align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", dax_region->align);
}
static DEVICE_ATTR_RO(align);

static struct attribute *dax_region_attributes[] = {
	&dev_attr_region_size.attr,
	&dev_attr_align.attr,
	&dev_attr_id.attr,
	NULL,
};

static const struct attribute_group dax_region_attribute_group = {
	.name = "dax_region",
	.attrs = dax_region_attributes,
};

static const struct attribute_group *dax_region_attribute_groups[] = {
	&dax_region_attribute_group,
	NULL,
};

static void dax_region_free(struct kref *kref)
{
	struct dax_region *dax_region;

	dax_region = container_of(kref, struct dax_region, kref);
	kfree(dax_region);
}

void dax_region_put(struct dax_region *dax_region)
{
	kref_put(&dax_region->kref, dax_region_free);
}
EXPORT_SYMBOL_GPL(dax_region_put);

static void dax_region_unregister(void *region)
{
	struct dax_region *dax_region = region;

	sysfs_remove_groups(&dax_region->dev->kobj,
			dax_region_attribute_groups);
	dax_region_put(dax_region);
}

struct dax_region *alloc_dax_region(struct device *parent, int region_id,
		struct resource *res, unsigned int align, void *addr,
		unsigned long pfn_flags)
{
	struct dax_region *dax_region;

	/*
	 * The DAX core assumes that it can store its private data in
	 * parent->driver_data. This WARN is a reminder / safeguard for
	 * developers of device-dax drivers.
	 */
	if (dev_get_drvdata(parent)) {
		dev_WARN(parent, "dax core failed to setup private data\n");
		return NULL;
	}

	if (!IS_ALIGNED(res->start, align)
			|| !IS_ALIGNED(resource_size(res), align))
		return NULL;

	dax_region = kzalloc(sizeof(*dax_region), GFP_KERNEL);
	if (!dax_region)
		return NULL;

	dev_set_drvdata(parent, dax_region);
	memcpy(&dax_region->res, res, sizeof(*res));
	dax_region->pfn_flags = pfn_flags;
	kref_init(&dax_region->kref);
	dax_region->id = region_id;
	ida_init(&dax_region->ida);
	dax_region->align = align;
	dax_region->dev = parent;
	dax_region->base = addr;
	if (sysfs_create_groups(&parent->kobj, dax_region_attribute_groups)) {
		kfree(dax_region);
		return NULL;
	}

	kref_get(&dax_region->kref);
	if (devm_add_action_or_reset(parent, dax_region_unregister, dax_region))
		return NULL;
	return dax_region;
}
EXPORT_SYMBOL_GPL(alloc_dax_region);

static struct dev_dax *to_dev_dax(struct device *dev)
{
	return container_of(dev, struct dev_dax, dev);
}

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	unsigned long long size = 0;
	int i;

	for (i = 0; i < dev_dax->num_resources; i++)
		size += resource_size(&dev_dax->res[i]);

	return sprintf(buf, "%llu\n", size);
}
static DEVICE_ATTR_RO(size);

static struct attribute *dev_dax_attributes[] = {
	&dev_attr_size.attr,
	NULL,
};

static const struct attribute_group dev_dax_attribute_group = {
	.attrs = dev_dax_attributes,
};

static const struct attribute_group *dax_attribute_groups[] = {
	&dev_dax_attribute_group,
	NULL,
};

static int check_vma(struct dev_dax *dev_dax, struct vm_area_struct *vma,
		const char *func)
{
	struct dax_region *dax_region = dev_dax->region;
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

	mask = dax_region->align - 1;
	if (vma->vm_start & mask || vma->vm_end & mask) {
		dev_info_ratelimited(dev,
				"%s: %s: fail, unaligned vma (%#lx - %#lx, %#lx)\n",
				current->comm, func, vma->vm_start, vma->vm_end,
				mask);
		return -EINVAL;
	}

	if ((dax_region->pfn_flags & (PFN_DEV|PFN_MAP)) == PFN_DEV
			&& (vma->vm_flags & VM_DONTCOPY) == 0) {
		dev_info_ratelimited(dev,
				"%s: %s: fail, dax range requires MADV_DONTFORK\n",
				current->comm, func);
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
	struct resource *res;
	/* gcc-4.6.3-nolibc for i386 complains that this is uninitialized */
	phys_addr_t uninitialized_var(phys);
	int i;

	for (i = 0; i < dev_dax->num_resources; i++) {
		res = &dev_dax->res[i];
		phys = pgoff * PAGE_SIZE + res->start;
		if (phys >= res->start && phys <= res->end)
			break;
		pgoff -= PHYS_PFN(resource_size(res));
	}

	if (i < dev_dax->num_resources) {
		res = &dev_dax->res[i];
		if (phys + size - 1 <= res->end)
			return phys;
	}

	return -1;
}

static int __dev_dax_pte_fault(struct dev_dax *dev_dax, struct vm_fault *vmf)
{
	struct device *dev = &dev_dax->dev;
	struct dax_region *dax_region;
	int rc = VM_FAULT_SIGBUS;
	phys_addr_t phys;
	pfn_t pfn;
	unsigned int fault_size = PAGE_SIZE;

	if (check_vma(dev_dax, vmf->vma, __func__))
		return VM_FAULT_SIGBUS;

	dax_region = dev_dax->region;
	if (dax_region->align > PAGE_SIZE) {
		dev_dbg(dev, "alignment (%#x) > fault size (%#x)\n",
			dax_region->align, fault_size);
		return VM_FAULT_SIGBUS;
	}

	if (fault_size != dax_region->align)
		return VM_FAULT_SIGBUS;

	phys = dax_pgoff_to_phys(dev_dax, vmf->pgoff, PAGE_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "pgoff_to_phys(%#lx) failed\n", vmf->pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, dax_region->pfn_flags);

	rc = vm_insert_mixed(vmf->vma, vmf->address, pfn);

	if (rc == -ENOMEM)
		return VM_FAULT_OOM;
	if (rc < 0 && rc != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

static int __dev_dax_pmd_fault(struct dev_dax *dev_dax, struct vm_fault *vmf)
{
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	struct device *dev = &dev_dax->dev;
	struct dax_region *dax_region;
	phys_addr_t phys;
	pgoff_t pgoff;
	pfn_t pfn;
	unsigned int fault_size = PMD_SIZE;

	if (check_vma(dev_dax, vmf->vma, __func__))
		return VM_FAULT_SIGBUS;

	dax_region = dev_dax->region;
	if (dax_region->align > PMD_SIZE) {
		dev_dbg(dev, "alignment (%#x) > fault size (%#x)\n",
			dax_region->align, fault_size);
		return VM_FAULT_SIGBUS;
	}

	/* dax pmd mappings require pfn_t_devmap() */
	if ((dax_region->pfn_flags & (PFN_DEV|PFN_MAP)) != (PFN_DEV|PFN_MAP)) {
		dev_dbg(dev, "region lacks devmap flags\n");
		return VM_FAULT_SIGBUS;
	}

	if (fault_size < dax_region->align)
		return VM_FAULT_SIGBUS;
	else if (fault_size > dax_region->align)
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

	pfn = phys_to_pfn_t(phys, dax_region->pfn_flags);

	return vmf_insert_pfn_pmd(vmf->vma, vmf->address, vmf->pmd, pfn,
			vmf->flags & FAULT_FLAG_WRITE);
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static int __dev_dax_pud_fault(struct dev_dax *dev_dax, struct vm_fault *vmf)
{
	unsigned long pud_addr = vmf->address & PUD_MASK;
	struct device *dev = &dev_dax->dev;
	struct dax_region *dax_region;
	phys_addr_t phys;
	pgoff_t pgoff;
	pfn_t pfn;
	unsigned int fault_size = PUD_SIZE;


	if (check_vma(dev_dax, vmf->vma, __func__))
		return VM_FAULT_SIGBUS;

	dax_region = dev_dax->region;
	if (dax_region->align > PUD_SIZE) {
		dev_dbg(dev, "alignment (%#x) > fault size (%#x)\n",
			dax_region->align, fault_size);
		return VM_FAULT_SIGBUS;
	}

	/* dax pud mappings require pfn_t_devmap() */
	if ((dax_region->pfn_flags & (PFN_DEV|PFN_MAP)) != (PFN_DEV|PFN_MAP)) {
		dev_dbg(dev, "region lacks devmap flags\n");
		return VM_FAULT_SIGBUS;
	}

	if (fault_size < dax_region->align)
		return VM_FAULT_SIGBUS;
	else if (fault_size > dax_region->align)
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

	pfn = phys_to_pfn_t(phys, dax_region->pfn_flags);

	return vmf_insert_pfn_pud(vmf->vma, vmf->address, vmf->pud, pfn,
			vmf->flags & FAULT_FLAG_WRITE);
}
#else
static int __dev_dax_pud_fault(struct dev_dax *dev_dax, struct vm_fault *vmf)
{
	return VM_FAULT_FALLBACK;
}
#endif /* !CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static int dev_dax_huge_fault(struct vm_fault *vmf,
		enum page_entry_size pe_size)
{
	int rc, id;
	struct file *filp = vmf->vma->vm_file;
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

static int dev_dax_fault(struct vm_fault *vmf)
{
	return dev_dax_huge_fault(vmf, PE_SIZE_PTE);
}

static int dev_dax_split(struct vm_area_struct *vma, unsigned long addr)
{
	struct file *filp = vma->vm_file;
	struct dev_dax *dev_dax = filp->private_data;
	struct dax_region *dax_region = dev_dax->region;

	if (!IS_ALIGNED(addr, dax_region->align))
		return -EINVAL;
	return 0;
}

static unsigned long dev_dax_pagesize(struct vm_area_struct *vma)
{
	struct file *filp = vma->vm_file;
	struct dev_dax *dev_dax = filp->private_data;
	struct dax_region *dax_region = dev_dax->region;

	return dax_region->align;
}

static const struct vm_operations_struct dax_vm_ops = {
	.fault = dev_dax_fault,
	.huge_fault = dev_dax_huge_fault,
	.split = dev_dax_split,
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
	vma->vm_flags |= VM_MIXEDMAP | VM_HUGEPAGE;
	return 0;
}

/* return an unmapped area aligned to the dax region specified alignment */
static unsigned long dax_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	unsigned long off, off_end, off_align, len_align, addr_align, align;
	struct dev_dax *dev_dax = filp ? filp->private_data : NULL;
	struct dax_region *dax_region;

	if (!dev_dax || addr)
		goto out;

	dax_region = dev_dax->region;
	align = dax_region->align;
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

static int dax_open(struct inode *inode, struct file *filp)
{
	struct dax_device *dax_dev = inode_dax(inode);
	struct inode *__dax_inode = dax_inode(dax_dev);
	struct dev_dax *dev_dax = dax_get_private(dax_dev);

	dev_dbg(&dev_dax->dev, "trace\n");
	inode->i_mapping = __dax_inode->i_mapping;
	inode->i_mapping->host = __dax_inode;
	filp->f_mapping = inode->i_mapping;
	filp->f_wb_err = filemap_sample_wb_err(filp->f_mapping);
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

static void dev_dax_release(struct device *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_region *dax_region = dev_dax->region;
	struct dax_device *dax_dev = dev_dax->dax_dev;

	if (dev_dax->id >= 0)
		ida_simple_remove(&dax_region->ida, dev_dax->id);
	dax_region_put(dax_region);
	put_dax(dax_dev);
	kfree(dev_dax);
}

static void kill_dev_dax(struct dev_dax *dev_dax)
{
	struct dax_device *dax_dev = dev_dax->dax_dev;
	struct inode *inode = dax_inode(dax_dev);

	kill_dax(dax_dev);
	unmap_mapping_range(inode->i_mapping, 0, 0, 1);
}

static void unregister_dev_dax(void *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_device *dax_dev = dev_dax->dax_dev;
	struct inode *inode = dax_inode(dax_dev);
	struct cdev *cdev = inode->i_cdev;

	dev_dbg(dev, "trace\n");

	kill_dev_dax(dev_dax);
	cdev_device_del(cdev, dev);
	put_device(dev);
}

struct dev_dax *devm_create_dev_dax(struct dax_region *dax_region,
		int id, struct resource *res, int count)
{
	struct device *parent = dax_region->dev;
	struct dax_device *dax_dev;
	struct dev_dax *dev_dax;
	struct inode *inode;
	struct device *dev;
	struct cdev *cdev;
	int rc, i;

	if (!count)
		return ERR_PTR(-EINVAL);

	dev_dax = kzalloc(struct_size(dev_dax, res, count), GFP_KERNEL);
	if (!dev_dax)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < count; i++) {
		if (!IS_ALIGNED(res[i].start, dax_region->align)
				|| !IS_ALIGNED(resource_size(&res[i]),
					dax_region->align)) {
			rc = -EINVAL;
			break;
		}
		dev_dax->res[i].start = res[i].start;
		dev_dax->res[i].end = res[i].end;
	}

	if (i < count)
		goto err_id;

	if (id < 0) {
		id = ida_simple_get(&dax_region->ida, 0, 0, GFP_KERNEL);
		dev_dax->id = id;
		if (id < 0) {
			rc = id;
			goto err_id;
		}
	} else {
		/* region provider owns @id lifetime */
		dev_dax->id = -1;
	}

	/*
	 * No 'host' or dax_operations since there is no access to this
	 * device outside of mmap of the resulting character device.
	 */
	dax_dev = alloc_dax(dev_dax, NULL, NULL);
	if (!dax_dev) {
		rc = -ENOMEM;
		goto err_dax;
	}

	/* from here on we're committed to teardown via dax_dev_release() */
	dev = &dev_dax->dev;
	device_initialize(dev);

	inode = dax_inode(dax_dev);
	cdev = inode->i_cdev;
	cdev_init(cdev, &dax_fops);
	cdev->owner = parent->driver->owner;

	dev_dax->num_resources = count;
	dev_dax->dax_dev = dax_dev;
	dev_dax->region = dax_region;
	kref_get(&dax_region->kref);

	dev->devt = inode->i_rdev;
	dev->class = dax_class;
	dev->parent = parent;
	dev->groups = dax_attribute_groups;
	dev->release = dev_dax_release;
	dev_set_name(dev, "dax%d.%d", dax_region->id, id);

	rc = cdev_device_add(cdev, dev);
	if (rc) {
		kill_dev_dax(dev_dax);
		put_device(dev);
		return ERR_PTR(rc);
	}

	rc = devm_add_action_or_reset(dax_region->dev, unregister_dev_dax, dev);
	if (rc)
		return ERR_PTR(rc);

	return dev_dax;

 err_dax:
	if (dev_dax->id >= 0)
		ida_simple_remove(&dax_region->ida, dev_dax->id);
 err_id:
	kfree(dev_dax);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_create_dev_dax);

static int __init dax_init(void)
{
	dax_class = class_create(THIS_MODULE, "dax");
	return PTR_ERR_OR_ZERO(dax_class);
}

static void __exit dax_exit(void)
{
	class_destroy(dax_class);
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
subsys_initcall(dax_init);
module_exit(dax_exit);

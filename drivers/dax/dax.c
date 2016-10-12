/*
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
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
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>

static int dax_major;
static struct class *dax_class;
static DEFINE_IDA(dax_minor_ida);

/**
 * struct dax_region - mapping infrastructure for dax devices
 * @id: kernel-wide unique region for a memory range
 * @base: linear address corresponding to @res
 * @kref: to pin while other agents have a need to do lookups
 * @dev: parent device backing this region
 * @align: allocation and mapping alignment for child dax devices
 * @res: physical address range of the region
 * @pfn_flags: identify whether the pfns are paged back or not
 */
struct dax_region {
	int id;
	struct ida ida;
	void *base;
	struct kref kref;
	struct device *dev;
	unsigned int align;
	struct resource res;
	unsigned long pfn_flags;
};

/**
 * struct dax_dev - subdivision of a dax region
 * @region - parent region
 * @dev - device backing the character device
 * @kref - enable this data to be tracked in filp->private_data
 * @alive - !alive + rcu grace period == no new mappings can be established
 * @id - child id in the region
 * @num_resources - number of physical address extents in this device
 * @res - array of physical address ranges
 */
struct dax_dev {
	struct dax_region *region;
	struct device *dev;
	struct kref kref;
	bool alive;
	int id;
	int num_resources;
	struct resource res[0];
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

static void dax_dev_free(struct kref *kref)
{
	struct dax_dev *dax_dev;

	dax_dev = container_of(kref, struct dax_dev, kref);
	dax_region_put(dax_dev->region);
	kfree(dax_dev);
}

static void dax_dev_put(struct dax_dev *dax_dev)
{
	kref_put(&dax_dev->kref, dax_dev_free);
}

struct dax_region *alloc_dax_region(struct device *parent, int region_id,
		struct resource *res, unsigned int align, void *addr,
		unsigned long pfn_flags)
{
	struct dax_region *dax_region;

	dax_region = kzalloc(sizeof(*dax_region), GFP_KERNEL);

	if (!dax_region)
		return NULL;

	memcpy(&dax_region->res, res, sizeof(*res));
	dax_region->pfn_flags = pfn_flags;
	kref_init(&dax_region->kref);
	dax_region->id = region_id;
	ida_init(&dax_region->ida);
	dax_region->align = align;
	dax_region->dev = parent;
	dax_region->base = addr;

	return dax_region;
}
EXPORT_SYMBOL_GPL(alloc_dax_region);

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_dev *dax_dev = dev_get_drvdata(dev);
	unsigned long long size = 0;
	int i;

	for (i = 0; i < dax_dev->num_resources; i++)
		size += resource_size(&dax_dev->res[i]);

	return sprintf(buf, "%llu\n", size);
}
static DEVICE_ATTR_RO(size);

static struct attribute *dax_device_attributes[] = {
	&dev_attr_size.attr,
	NULL,
};

static const struct attribute_group dax_device_attribute_group = {
	.attrs = dax_device_attributes,
};

static const struct attribute_group *dax_attribute_groups[] = {
	&dax_device_attribute_group,
	NULL,
};

static void unregister_dax_dev(void *_dev)
{
	struct device *dev = _dev;
	struct dax_dev *dax_dev = dev_get_drvdata(dev);
	struct dax_region *dax_region = dax_dev->region;

	dev_dbg(dev, "%s\n", __func__);

	/*
	 * Note, rcu is not protecting the liveness of dax_dev, rcu is
	 * ensuring that any fault handlers that might have seen
	 * dax_dev->alive == true, have completed.  Any fault handlers
	 * that start after synchronize_rcu() has started will abort
	 * upon seeing dax_dev->alive == false.
	 */
	dax_dev->alive = false;
	synchronize_rcu();

	get_device(dev);
	device_unregister(dev);
	ida_simple_remove(&dax_region->ida, dax_dev->id);
	ida_simple_remove(&dax_minor_ida, MINOR(dev->devt));
	put_device(dev);
	dax_dev_put(dax_dev);
}

int devm_create_dax_dev(struct dax_region *dax_region, struct resource *res,
		int count)
{
	struct device *parent = dax_region->dev;
	struct dax_dev *dax_dev;
	struct device *dev;
	int rc, minor;
	dev_t dev_t;

	dax_dev = kzalloc(sizeof(*dax_dev) + sizeof(*res) * count, GFP_KERNEL);
	if (!dax_dev)
		return -ENOMEM;
	memcpy(dax_dev->res, res, sizeof(*res) * count);
	dax_dev->num_resources = count;
	kref_init(&dax_dev->kref);
	dax_dev->alive = true;
	dax_dev->region = dax_region;
	kref_get(&dax_region->kref);

	dax_dev->id = ida_simple_get(&dax_region->ida, 0, 0, GFP_KERNEL);
	if (dax_dev->id < 0) {
		rc = dax_dev->id;
		goto err_id;
	}

	minor = ida_simple_get(&dax_minor_ida, 0, 0, GFP_KERNEL);
	if (minor < 0) {
		rc = minor;
		goto err_minor;
	}

	dev_t = MKDEV(dax_major, minor);
	dev = device_create_with_groups(dax_class, parent, dev_t, dax_dev,
			dax_attribute_groups, "dax%d.%d", dax_region->id,
			dax_dev->id);
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		goto err_create;
	}
	dax_dev->dev = dev;

	rc = devm_add_action_or_reset(dax_region->dev, unregister_dax_dev, dev);
	if (rc)
		return rc;

	return 0;

 err_create:
	ida_simple_remove(&dax_minor_ida, minor);
 err_minor:
	ida_simple_remove(&dax_region->ida, dax_dev->id);
 err_id:
	dax_dev_put(dax_dev);

	return rc;
}
EXPORT_SYMBOL_GPL(devm_create_dax_dev);

/* return an unmapped area aligned to the dax region specified alignment */
static unsigned long dax_dev_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	unsigned long off, off_end, off_align, len_align, addr_align, align;
	struct dax_dev *dax_dev = filp ? filp->private_data : NULL;
	struct dax_region *dax_region;

	if (!dax_dev || addr)
		goto out;

	dax_region = dax_dev->region;
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

static int __match_devt(struct device *dev, const void *data)
{
	const dev_t *devt = data;

	return dev->devt == *devt;
}

static struct device *dax_dev_find(dev_t dev_t)
{
	return class_find_device(dax_class, NULL, &dev_t, __match_devt);
}

static int dax_dev_open(struct inode *inode, struct file *filp)
{
	struct dax_dev *dax_dev = NULL;
	struct device *dev;

	dev = dax_dev_find(inode->i_rdev);
	if (!dev)
		return -ENXIO;

	device_lock(dev);
	dax_dev = dev_get_drvdata(dev);
	if (dax_dev) {
		dev_dbg(dev, "%s\n", __func__);
		filp->private_data = dax_dev;
		kref_get(&dax_dev->kref);
		inode->i_flags = S_DAX;
	}
	device_unlock(dev);

	if (!dax_dev) {
		put_device(dev);
		return -ENXIO;
	}
	return 0;
}

static int dax_dev_release(struct inode *inode, struct file *filp)
{
	struct dax_dev *dax_dev = filp->private_data;
	struct device *dev = dax_dev->dev;

	dev_dbg(dax_dev->dev, "%s\n", __func__);
	dax_dev_put(dax_dev);
	put_device(dev);

	return 0;
}

static int check_vma(struct dax_dev *dax_dev, struct vm_area_struct *vma,
		const char *func)
{
	struct dax_region *dax_region = dax_dev->region;
	struct device *dev = dax_dev->dev;
	unsigned long mask;

	if (!dax_dev->alive)
		return -ENXIO;

	/* prevent private / writable mappings from being established */
	if ((vma->vm_flags & (VM_NORESERVE|VM_SHARED|VM_WRITE)) == VM_WRITE) {
		dev_info(dev, "%s: %s: fail, attempted private mapping\n",
				current->comm, func);
		return -EINVAL;
	}

	mask = dax_region->align - 1;
	if (vma->vm_start & mask || vma->vm_end & mask) {
		dev_info(dev, "%s: %s: fail, unaligned vma (%#lx - %#lx, %#lx)\n",
				current->comm, func, vma->vm_start, vma->vm_end,
				mask);
		return -EINVAL;
	}

	if ((dax_region->pfn_flags & (PFN_DEV|PFN_MAP)) == PFN_DEV
			&& (vma->vm_flags & VM_DONTCOPY) == 0) {
		dev_info(dev, "%s: %s: fail, dax range requires MADV_DONTFORK\n",
				current->comm, func);
		return -EINVAL;
	}

	if (!vma_is_dax(vma)) {
		dev_info(dev, "%s: %s: fail, vma is not DAX capable\n",
				current->comm, func);
		return -EINVAL;
	}

	return 0;
}

static phys_addr_t pgoff_to_phys(struct dax_dev *dax_dev, pgoff_t pgoff,
		unsigned long size)
{
	struct resource *res;
	phys_addr_t phys;
	int i;

	for (i = 0; i < dax_dev->num_resources; i++) {
		res = &dax_dev->res[i];
		phys = pgoff * PAGE_SIZE + res->start;
		if (phys >= res->start && phys <= res->end)
			break;
		pgoff -= PHYS_PFN(resource_size(res));
	}

	if (i < dax_dev->num_resources) {
		res = &dax_dev->res[i];
		if (phys + size - 1 <= res->end)
			return phys;
	}

	return -1;
}

static int __dax_dev_fault(struct dax_dev *dax_dev, struct vm_area_struct *vma,
		struct vm_fault *vmf)
{
	unsigned long vaddr = (unsigned long) vmf->virtual_address;
	struct device *dev = dax_dev->dev;
	struct dax_region *dax_region;
	int rc = VM_FAULT_SIGBUS;
	phys_addr_t phys;
	pfn_t pfn;

	if (check_vma(dax_dev, vma, __func__))
		return VM_FAULT_SIGBUS;

	dax_region = dax_dev->region;
	if (dax_region->align > PAGE_SIZE) {
		dev_dbg(dev, "%s: alignment > fault size\n", __func__);
		return VM_FAULT_SIGBUS;
	}

	phys = pgoff_to_phys(dax_dev, vmf->pgoff, PAGE_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "%s: phys_to_pgoff(%#lx) failed\n", __func__,
				vmf->pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, dax_region->pfn_flags);

	rc = vm_insert_mixed(vma, vaddr, pfn);

	if (rc == -ENOMEM)
		return VM_FAULT_OOM;
	if (rc < 0 && rc != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

static int dax_dev_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int rc;
	struct file *filp = vma->vm_file;
	struct dax_dev *dax_dev = filp->private_data;

	dev_dbg(dax_dev->dev, "%s: %s: %s (%#lx - %#lx)\n", __func__,
			current->comm, (vmf->flags & FAULT_FLAG_WRITE)
			? "write" : "read", vma->vm_start, vma->vm_end);
	rcu_read_lock();
	rc = __dax_dev_fault(dax_dev, vma, vmf);
	rcu_read_unlock();

	return rc;
}

static int __dax_dev_pmd_fault(struct dax_dev *dax_dev,
		struct vm_area_struct *vma, unsigned long addr, pmd_t *pmd,
		unsigned int flags)
{
	unsigned long pmd_addr = addr & PMD_MASK;
	struct device *dev = dax_dev->dev;
	struct dax_region *dax_region;
	phys_addr_t phys;
	pgoff_t pgoff;
	pfn_t pfn;

	if (check_vma(dax_dev, vma, __func__))
		return VM_FAULT_SIGBUS;

	dax_region = dax_dev->region;
	if (dax_region->align > PMD_SIZE) {
		dev_dbg(dev, "%s: alignment > fault size\n", __func__);
		return VM_FAULT_SIGBUS;
	}

	/* dax pmd mappings require pfn_t_devmap() */
	if ((dax_region->pfn_flags & (PFN_DEV|PFN_MAP)) != (PFN_DEV|PFN_MAP)) {
		dev_dbg(dev, "%s: alignment > fault size\n", __func__);
		return VM_FAULT_SIGBUS;
	}

	pgoff = linear_page_index(vma, pmd_addr);
	phys = pgoff_to_phys(dax_dev, pgoff, PMD_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "%s: phys_to_pgoff(%#lx) failed\n", __func__,
				pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, dax_region->pfn_flags);

	return vmf_insert_pfn_pmd(vma, addr, pmd, pfn,
			flags & FAULT_FLAG_WRITE);
}

static int dax_dev_pmd_fault(struct vm_area_struct *vma, unsigned long addr,
		pmd_t *pmd, unsigned int flags)
{
	int rc;
	struct file *filp = vma->vm_file;
	struct dax_dev *dax_dev = filp->private_data;

	dev_dbg(dax_dev->dev, "%s: %s: %s (%#lx - %#lx)\n", __func__,
			current->comm, (flags & FAULT_FLAG_WRITE)
			? "write" : "read", vma->vm_start, vma->vm_end);

	rcu_read_lock();
	rc = __dax_dev_pmd_fault(dax_dev, vma, addr, pmd, flags);
	rcu_read_unlock();

	return rc;
}

static void dax_dev_vm_open(struct vm_area_struct *vma)
{
	struct file *filp = vma->vm_file;
	struct dax_dev *dax_dev = filp->private_data;

	dev_dbg(dax_dev->dev, "%s\n", __func__);
	kref_get(&dax_dev->kref);
}

static void dax_dev_vm_close(struct vm_area_struct *vma)
{
	struct file *filp = vma->vm_file;
	struct dax_dev *dax_dev = filp->private_data;

	dev_dbg(dax_dev->dev, "%s\n", __func__);
	dax_dev_put(dax_dev);
}

static const struct vm_operations_struct dax_dev_vm_ops = {
	.fault = dax_dev_fault,
	.pmd_fault = dax_dev_pmd_fault,
	.open = dax_dev_vm_open,
	.close = dax_dev_vm_close,
};

static int dax_dev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct dax_dev *dax_dev = filp->private_data;
	int rc;

	dev_dbg(dax_dev->dev, "%s\n", __func__);

	rc = check_vma(dax_dev, vma, __func__);
	if (rc)
		return rc;

	kref_get(&dax_dev->kref);
	vma->vm_ops = &dax_dev_vm_ops;
	vma->vm_flags |= VM_MIXEDMAP | VM_HUGEPAGE;
	return 0;

}

static const struct file_operations dax_fops = {
	.llseek = noop_llseek,
	.owner = THIS_MODULE,
	.open = dax_dev_open,
	.release = dax_dev_release,
	.get_unmapped_area = dax_dev_get_unmapped_area,
	.mmap = dax_dev_mmap,
};

static int __init dax_init(void)
{
	int rc;

	rc = register_chrdev(0, "dax", &dax_fops);
	if (rc < 0)
		return rc;
	dax_major = rc;

	dax_class = class_create(THIS_MODULE, "dax");
	if (IS_ERR(dax_class)) {
		unregister_chrdev(dax_major, "dax");
		return PTR_ERR(dax_class);
	}

	return 0;
}

static void __exit dax_exit(void)
{
	class_destroy(dax_class);
	unregister_chrdev(dax_major, "dax");
	ida_destroy(&dax_minor_ida);
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
subsys_initcall(dax_init);
module_exit(dax_exit);

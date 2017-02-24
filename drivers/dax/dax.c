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
#include <linux/mount.h>
#include <linux/pfn_t.h>
#include <linux/hash.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "dax.h"

static dev_t dax_devt;
static struct class *dax_class;
static DEFINE_IDA(dax_minor_ida);
static int nr_dax = CONFIG_NR_DEV_DAX;
module_param(nr_dax, int, S_IRUGO);
static struct vfsmount *dax_mnt;
static struct kmem_cache *dax_cache __read_mostly;
static struct super_block *dax_superblock __read_mostly;
MODULE_PARM_DESC(nr_dax, "max number of device-dax instances");

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
 * @cdev - core chardev data
 * @alive - !alive + rcu grace period == no new mappings can be established
 * @id - child id in the region
 * @num_resources - number of physical address extents in this device
 * @res - array of physical address ranges
 */
struct dax_dev {
	struct dax_region *region;
	struct inode *inode;
	struct device dev;
	struct cdev cdev;
	bool alive;
	int id;
	int num_resources;
	struct resource res[0];
};

static ssize_t id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region;
	ssize_t rc = -ENXIO;

	device_lock(dev);
	dax_region = dev_get_drvdata(dev);
	if (dax_region)
		rc = sprintf(buf, "%d\n", dax_region->id);
	device_unlock(dev);

	return rc;
}
static DEVICE_ATTR_RO(id);

static ssize_t region_size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region;
	ssize_t rc = -ENXIO;

	device_lock(dev);
	dax_region = dev_get_drvdata(dev);
	if (dax_region)
		rc = sprintf(buf, "%llu\n", (unsigned long long)
				resource_size(&dax_region->res));
	device_unlock(dev);

	return rc;
}
static struct device_attribute dev_attr_region_size = __ATTR(size, 0444,
		region_size_show, NULL);

static ssize_t align_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_region *dax_region;
	ssize_t rc = -ENXIO;

	device_lock(dev);
	dax_region = dev_get_drvdata(dev);
	if (dax_region)
		rc = sprintf(buf, "%u\n", dax_region->align);
	device_unlock(dev);

	return rc;
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

static struct inode *dax_alloc_inode(struct super_block *sb)
{
	return kmem_cache_alloc(dax_cache, GFP_KERNEL);
}

static void dax_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);

	kmem_cache_free(dax_cache, inode);
}

static void dax_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, dax_i_callback);
}

static const struct super_operations dax_sops = {
	.statfs = simple_statfs,
	.alloc_inode = dax_alloc_inode,
	.destroy_inode = dax_destroy_inode,
	.drop_inode = generic_delete_inode,
};

static struct dentry *dax_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data)
{
	return mount_pseudo(fs_type, "dax:", &dax_sops, NULL, DAXFS_MAGIC);
}

static struct file_system_type dax_type = {
	.name = "dax",
	.mount = dax_mount,
	.kill_sb = kill_anon_super,
};

static int dax_test(struct inode *inode, void *data)
{
	return inode->i_cdev == data;
}

static int dax_set(struct inode *inode, void *data)
{
	inode->i_cdev = data;
	return 0;
}

static struct inode *dax_inode_get(struct cdev *cdev, dev_t devt)
{
	struct inode *inode;

	inode = iget5_locked(dax_superblock, hash_32(devt + DAXFS_MAGIC, 31),
			dax_test, dax_set, cdev);

	if (!inode)
		return NULL;

	if (inode->i_state & I_NEW) {
		inode->i_mode = S_IFCHR;
		inode->i_flags = S_DAX;
		inode->i_rdev = devt;
		mapping_set_gfp_mask(&inode->i_data, GFP_USER);
		unlock_new_inode(inode);
	}
	return inode;
}

static void init_once(void *inode)
{
	inode_init_once(inode);
}

static int dax_inode_init(void)
{
	int rc;

	dax_cache = kmem_cache_create("dax_cache", sizeof(struct inode), 0,
			(SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
			 SLAB_MEM_SPREAD|SLAB_ACCOUNT),
			init_once);
	if (!dax_cache)
		return -ENOMEM;

	rc = register_filesystem(&dax_type);
	if (rc)
		goto err_register_fs;

	dax_mnt = kern_mount(&dax_type);
	if (IS_ERR(dax_mnt)) {
		rc = PTR_ERR(dax_mnt);
		goto err_mount;
	}
	dax_superblock = dax_mnt->mnt_sb;

	return 0;

 err_mount:
	unregister_filesystem(&dax_type);
 err_register_fs:
	kmem_cache_destroy(dax_cache);

	return rc;
}

static void dax_inode_exit(void)
{
	kern_unmount(dax_mnt);
	unregister_filesystem(&dax_type);
	kmem_cache_destroy(dax_cache);
}

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
		return NULL;;
	}

	kref_get(&dax_region->kref);
	if (devm_add_action_or_reset(parent, dax_region_unregister, dax_region))
		return NULL;
	return dax_region;
}
EXPORT_SYMBOL_GPL(alloc_dax_region);

static struct dax_dev *to_dax_dev(struct device *dev)
{
	return container_of(dev, struct dax_dev, dev);
}

static ssize_t size_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dax_dev *dax_dev = to_dax_dev(dev);
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

static int check_vma(struct dax_dev *dax_dev, struct vm_area_struct *vma,
		const char *func)
{
	struct dax_region *dax_region = dax_dev->region;
	struct device *dev = &dax_dev->dev;
	unsigned long mask;

	if (!dax_dev->alive)
		return -ENXIO;

	/* prevent private mappings from being established */
	if ((vma->vm_flags & VM_MAYSHARE) != VM_MAYSHARE) {
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

static int __dax_dev_pte_fault(struct dax_dev *dax_dev, struct vm_fault *vmf)
{
	struct device *dev = &dax_dev->dev;
	struct dax_region *dax_region;
	int rc = VM_FAULT_SIGBUS;
	phys_addr_t phys;
	pfn_t pfn;

	if (check_vma(dax_dev, vmf->vma, __func__))
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

	rc = vm_insert_mixed(vmf->vma, vmf->address, pfn);

	if (rc == -ENOMEM)
		return VM_FAULT_OOM;
	if (rc < 0 && rc != -EBUSY)
		return VM_FAULT_SIGBUS;

	return VM_FAULT_NOPAGE;
}

static int __dax_dev_pmd_fault(struct dax_dev *dax_dev, struct vm_fault *vmf)
{
	unsigned long pmd_addr = vmf->address & PMD_MASK;
	struct device *dev = &dax_dev->dev;
	struct dax_region *dax_region;
	phys_addr_t phys;
	pgoff_t pgoff;
	pfn_t pfn;

	if (check_vma(dax_dev, vmf->vma, __func__))
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

	pgoff = linear_page_index(vmf->vma, pmd_addr);
	phys = pgoff_to_phys(dax_dev, pgoff, PMD_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "%s: phys_to_pgoff(%#lx) failed\n", __func__,
				pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, dax_region->pfn_flags);

	return vmf_insert_pfn_pmd(vmf->vma, vmf->address, vmf->pmd, pfn,
			vmf->flags & FAULT_FLAG_WRITE);
}

#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
static int __dax_dev_pud_fault(struct dax_dev *dax_dev, struct vm_fault *vmf)
{
	unsigned long pud_addr = vmf->address & PUD_MASK;
	struct device *dev = &dax_dev->dev;
	struct dax_region *dax_region;
	phys_addr_t phys;
	pgoff_t pgoff;
	pfn_t pfn;

	if (check_vma(dax_dev, vmf->vma, __func__))
		return VM_FAULT_SIGBUS;

	dax_region = dax_dev->region;
	if (dax_region->align > PUD_SIZE) {
		dev_dbg(dev, "%s: alignment > fault size\n", __func__);
		return VM_FAULT_SIGBUS;
	}

	/* dax pud mappings require pfn_t_devmap() */
	if ((dax_region->pfn_flags & (PFN_DEV|PFN_MAP)) != (PFN_DEV|PFN_MAP)) {
		dev_dbg(dev, "%s: alignment > fault size\n", __func__);
		return VM_FAULT_SIGBUS;
	}

	pgoff = linear_page_index(vmf->vma, pud_addr);
	phys = pgoff_to_phys(dax_dev, pgoff, PUD_SIZE);
	if (phys == -1) {
		dev_dbg(dev, "%s: phys_to_pgoff(%#lx) failed\n", __func__,
				pgoff);
		return VM_FAULT_SIGBUS;
	}

	pfn = phys_to_pfn_t(phys, dax_region->pfn_flags);

	return vmf_insert_pfn_pud(vmf->vma, vmf->address, vmf->pud, pfn,
			vmf->flags & FAULT_FLAG_WRITE);
}
#else
static int __dax_dev_pud_fault(struct dax_dev *dax_dev, struct vm_fault *vmf)
{
	return VM_FAULT_FALLBACK;
}
#endif /* !CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD */

static int dax_dev_fault(struct vm_fault *vmf)
{
	int rc;
	struct file *filp = vmf->vma->vm_file;
	struct dax_dev *dax_dev = filp->private_data;

	dev_dbg(&dax_dev->dev, "%s: %s: %s (%#lx - %#lx)\n", __func__,
			current->comm, (vmf->flags & FAULT_FLAG_WRITE)
			? "write" : "read",
			vmf->vma->vm_start, vmf->vma->vm_end);

	rcu_read_lock();
	switch (vmf->flags & FAULT_FLAG_SIZE_MASK) {
	case FAULT_FLAG_SIZE_PTE:
		rc = __dax_dev_pte_fault(dax_dev, vmf);
		break;
	case FAULT_FLAG_SIZE_PMD:
		rc = __dax_dev_pmd_fault(dax_dev, vmf);
		break;
	case FAULT_FLAG_SIZE_PUD:
		rc = __dax_dev_pud_fault(dax_dev, vmf);
		break;
	default:
		return VM_FAULT_FALLBACK;
	}
	rcu_read_unlock();

	return rc;
}

static const struct vm_operations_struct dax_dev_vm_ops = {
	.fault = dax_dev_fault,
	.huge_fault = dax_dev_fault,
};

static int dax_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct dax_dev *dax_dev = filp->private_data;
	int rc;

	dev_dbg(&dax_dev->dev, "%s\n", __func__);

	rc = check_vma(dax_dev, vma, __func__);
	if (rc)
		return rc;

	vma->vm_ops = &dax_dev_vm_ops;
	vma->vm_flags |= VM_MIXEDMAP | VM_HUGEPAGE;
	return 0;
}

/* return an unmapped area aligned to the dax region specified alignment */
static unsigned long dax_get_unmapped_area(struct file *filp,
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

static int dax_open(struct inode *inode, struct file *filp)
{
	struct dax_dev *dax_dev;

	dax_dev = container_of(inode->i_cdev, struct dax_dev, cdev);
	dev_dbg(&dax_dev->dev, "%s\n", __func__);
	inode->i_mapping = dax_dev->inode->i_mapping;
	inode->i_mapping->host = dax_dev->inode;
	filp->f_mapping = inode->i_mapping;
	filp->private_data = dax_dev;
	inode->i_flags = S_DAX;

	return 0;
}

static int dax_release(struct inode *inode, struct file *filp)
{
	struct dax_dev *dax_dev = filp->private_data;

	dev_dbg(&dax_dev->dev, "%s\n", __func__);
	return 0;
}

static const struct file_operations dax_fops = {
	.llseek = noop_llseek,
	.owner = THIS_MODULE,
	.open = dax_open,
	.release = dax_release,
	.get_unmapped_area = dax_get_unmapped_area,
	.mmap = dax_mmap,
};

static void dax_dev_release(struct device *dev)
{
	struct dax_dev *dax_dev = to_dax_dev(dev);
	struct dax_region *dax_region = dax_dev->region;

	ida_simple_remove(&dax_region->ida, dax_dev->id);
	ida_simple_remove(&dax_minor_ida, MINOR(dev->devt));
	dax_region_put(dax_region);
	iput(dax_dev->inode);
	kfree(dax_dev);
}

static void unregister_dax_dev(void *dev)
{
	struct dax_dev *dax_dev = to_dax_dev(dev);
	struct cdev *cdev = &dax_dev->cdev;

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
	unmap_mapping_range(dax_dev->inode->i_mapping, 0, 0, 1);
	cdev_del(cdev);
	device_unregister(dev);
}

struct dax_dev *devm_create_dax_dev(struct dax_region *dax_region,
		struct resource *res, int count)
{
	struct device *parent = dax_region->dev;
	struct dax_dev *dax_dev;
	int rc = 0, minor, i;
	struct device *dev;
	struct cdev *cdev;
	dev_t dev_t;

	dax_dev = kzalloc(sizeof(*dax_dev) + sizeof(*res) * count, GFP_KERNEL);
	if (!dax_dev)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < count; i++) {
		if (!IS_ALIGNED(res[i].start, dax_region->align)
				|| !IS_ALIGNED(resource_size(&res[i]),
					dax_region->align)) {
			rc = -EINVAL;
			break;
		}
		dax_dev->res[i].start = res[i].start;
		dax_dev->res[i].end = res[i].end;
	}

	if (i < count)
		goto err_id;

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

	dev_t = MKDEV(MAJOR(dax_devt), minor);
	dev = &dax_dev->dev;
	dax_dev->inode = dax_inode_get(&dax_dev->cdev, dev_t);
	if (!dax_dev->inode) {
		rc = -ENOMEM;
		goto err_inode;
	}

	/* device_initialize() so cdev can reference kobj parent */
	device_initialize(dev);

	cdev = &dax_dev->cdev;
	cdev_init(cdev, &dax_fops);
	cdev->owner = parent->driver->owner;
	cdev->kobj.parent = &dev->kobj;
	rc = cdev_add(&dax_dev->cdev, dev_t, 1);
	if (rc)
		goto err_cdev;

	/* from here on we're committed to teardown via dax_dev_release() */
	dax_dev->num_resources = count;
	dax_dev->alive = true;
	dax_dev->region = dax_region;
	kref_get(&dax_region->kref);

	dev->devt = dev_t;
	dev->class = dax_class;
	dev->parent = parent;
	dev->groups = dax_attribute_groups;
	dev->release = dax_dev_release;
	dev_set_name(dev, "dax%d.%d", dax_region->id, dax_dev->id);
	rc = device_add(dev);
	if (rc) {
		put_device(dev);
		return ERR_PTR(rc);
	}

	rc = devm_add_action_or_reset(dax_region->dev, unregister_dax_dev, dev);
	if (rc)
		return ERR_PTR(rc);

	return dax_dev;

 err_cdev:
	iput(dax_dev->inode);
 err_inode:
	ida_simple_remove(&dax_minor_ida, minor);
 err_minor:
	ida_simple_remove(&dax_region->ida, dax_dev->id);
 err_id:
	kfree(dax_dev);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(devm_create_dax_dev);

static int __init dax_init(void)
{
	int rc;

	rc = dax_inode_init();
	if (rc)
		return rc;

	nr_dax = max(nr_dax, 256);
	rc = alloc_chrdev_region(&dax_devt, 0, nr_dax, "dax");
	if (rc)
		goto err_chrdev;

	dax_class = class_create(THIS_MODULE, "dax");
	if (IS_ERR(dax_class)) {
		rc = PTR_ERR(dax_class);
		goto err_class;
	}

	return 0;

 err_class:
	unregister_chrdev_region(dax_devt, nr_dax);
 err_chrdev:
	dax_inode_exit();
	return rc;
}

static void __exit dax_exit(void)
{
	class_destroy(dax_class);
	unregister_chrdev_region(dax_devt, nr_dax);
	ida_destroy(&dax_minor_ida);
	dax_inode_exit();
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
subsys_initcall(dax_init);
module_exit(dax_exit);

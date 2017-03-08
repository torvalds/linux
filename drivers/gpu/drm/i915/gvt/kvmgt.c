/*
 * KVMGT - the implementation of Intel mediated pass-through framework for KVM
 *
 * Copyright(c) 2014-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Kevin Tian <kevin.tian@intel.com>
 *    Jike Song <jike.song@intel.com>
 *    Xiaoguang Chen <xiaoguang.chen@intel.com>
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/eventfd.h>
#include <linux/uuid.h>
#include <linux/kvm_host.h>
#include <linux/vfio.h>
#include <linux/mdev.h>

#include "i915_drv.h"
#include "gvt.h"

static const struct intel_gvt_ops *intel_gvt_ops;

/* helper macros copied from vfio-pci */
#define VFIO_PCI_OFFSET_SHIFT   40
#define VFIO_PCI_OFFSET_TO_INDEX(off)   (off >> VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_INDEX_TO_OFFSET(index) ((u64)(index) << VFIO_PCI_OFFSET_SHIFT)
#define VFIO_PCI_OFFSET_MASK    (((u64)(1) << VFIO_PCI_OFFSET_SHIFT) - 1)

struct vfio_region {
	u32				type;
	u32				subtype;
	size_t				size;
	u32				flags;
};

struct kvmgt_pgfn {
	gfn_t gfn;
	struct hlist_node hnode;
};

struct kvmgt_guest_info {
	struct kvm *kvm;
	struct intel_vgpu *vgpu;
	struct kvm_page_track_notifier_node track_node;
#define NR_BKT (1 << 18)
	struct hlist_head ptable[NR_BKT];
#undef NR_BKT
};

struct gvt_dma {
	struct rb_node node;
	gfn_t gfn;
	unsigned long iova;
};

static inline bool handle_valid(unsigned long handle)
{
	return !!(handle & ~0xff);
}

static int kvmgt_guest_init(struct mdev_device *mdev);
static void intel_vgpu_release_work(struct work_struct *work);
static bool kvmgt_guest_exit(struct kvmgt_guest_info *info);

static int gvt_dma_map_iova(struct intel_vgpu *vgpu, kvm_pfn_t pfn,
		unsigned long *iova)
{
	struct page *page;
	struct device *dev = &vgpu->gvt->dev_priv->drm.pdev->dev;
	dma_addr_t daddr;

	page = pfn_to_page(pfn);
	if (is_error_page(page))
		return -EFAULT;

	daddr = dma_map_page(dev, page, 0, PAGE_SIZE,
			PCI_DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, daddr))
		return -ENOMEM;

	*iova = (unsigned long)(daddr >> PAGE_SHIFT);
	return 0;
}

static void gvt_dma_unmap_iova(struct intel_vgpu *vgpu, unsigned long iova)
{
	struct device *dev = &vgpu->gvt->dev_priv->drm.pdev->dev;
	dma_addr_t daddr;

	daddr = (dma_addr_t)(iova << PAGE_SHIFT);
	dma_unmap_page(dev, daddr, PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
}

static struct gvt_dma *__gvt_cache_find(struct intel_vgpu *vgpu, gfn_t gfn)
{
	struct rb_node *node = vgpu->vdev.cache.rb_node;
	struct gvt_dma *ret = NULL;

	while (node) {
		struct gvt_dma *itr = rb_entry(node, struct gvt_dma, node);

		if (gfn < itr->gfn)
			node = node->rb_left;
		else if (gfn > itr->gfn)
			node = node->rb_right;
		else {
			ret = itr;
			goto out;
		}
	}

out:
	return ret;
}

static unsigned long gvt_cache_find(struct intel_vgpu *vgpu, gfn_t gfn)
{
	struct gvt_dma *entry;
	unsigned long iova;

	mutex_lock(&vgpu->vdev.cache_lock);

	entry = __gvt_cache_find(vgpu, gfn);
	iova = (entry == NULL) ? INTEL_GVT_INVALID_ADDR : entry->iova;

	mutex_unlock(&vgpu->vdev.cache_lock);
	return iova;
}

static void gvt_cache_add(struct intel_vgpu *vgpu, gfn_t gfn,
		unsigned long iova)
{
	struct gvt_dma *new, *itr;
	struct rb_node **link = &vgpu->vdev.cache.rb_node, *parent = NULL;

	new = kzalloc(sizeof(struct gvt_dma), GFP_KERNEL);
	if (!new)
		return;

	new->gfn = gfn;
	new->iova = iova;

	mutex_lock(&vgpu->vdev.cache_lock);
	while (*link) {
		parent = *link;
		itr = rb_entry(parent, struct gvt_dma, node);

		if (gfn == itr->gfn)
			goto out;
		else if (gfn < itr->gfn)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_node(&new->node, parent, link);
	rb_insert_color(&new->node, &vgpu->vdev.cache);
	mutex_unlock(&vgpu->vdev.cache_lock);
	return;

out:
	mutex_unlock(&vgpu->vdev.cache_lock);
	kfree(new);
}

static void __gvt_cache_remove_entry(struct intel_vgpu *vgpu,
				struct gvt_dma *entry)
{
	rb_erase(&entry->node, &vgpu->vdev.cache);
	kfree(entry);
}

static void gvt_cache_remove(struct intel_vgpu *vgpu, gfn_t gfn)
{
	struct device *dev = mdev_dev(vgpu->vdev.mdev);
	struct gvt_dma *this;
	unsigned long g1;
	int rc;

	mutex_lock(&vgpu->vdev.cache_lock);
	this  = __gvt_cache_find(vgpu, gfn);
	if (!this) {
		mutex_unlock(&vgpu->vdev.cache_lock);
		return;
	}

	g1 = gfn;
	gvt_dma_unmap_iova(vgpu, this->iova);
	rc = vfio_unpin_pages(dev, &g1, 1);
	WARN_ON(rc != 1);
	__gvt_cache_remove_entry(vgpu, this);
	mutex_unlock(&vgpu->vdev.cache_lock);
}

static void gvt_cache_init(struct intel_vgpu *vgpu)
{
	vgpu->vdev.cache = RB_ROOT;
	mutex_init(&vgpu->vdev.cache_lock);
}

static void gvt_cache_destroy(struct intel_vgpu *vgpu)
{
	struct gvt_dma *dma;
	struct rb_node *node = NULL;
	struct device *dev = mdev_dev(vgpu->vdev.mdev);
	unsigned long gfn;

	mutex_lock(&vgpu->vdev.cache_lock);
	while ((node = rb_first(&vgpu->vdev.cache))) {
		dma = rb_entry(node, struct gvt_dma, node);
		gvt_dma_unmap_iova(vgpu, dma->iova);
		gfn = dma->gfn;

		vfio_unpin_pages(dev, &gfn, 1);
		__gvt_cache_remove_entry(vgpu, dma);
	}
	mutex_unlock(&vgpu->vdev.cache_lock);
}

static struct intel_vgpu_type *intel_gvt_find_vgpu_type(struct intel_gvt *gvt,
		const char *name)
{
	int i;
	struct intel_vgpu_type *t;
	const char *driver_name = dev_driver_string(
			&gvt->dev_priv->drm.pdev->dev);

	for (i = 0; i < gvt->num_types; i++) {
		t = &gvt->types[i];
		if (!strncmp(t->name, name + strlen(driver_name) + 1,
			sizeof(t->name)))
			return t;
	}

	return NULL;
}

static ssize_t available_instances_show(struct kobject *kobj,
					struct device *dev, char *buf)
{
	struct intel_vgpu_type *type;
	unsigned int num = 0;
	void *gvt = kdev_to_i915(dev)->gvt;

	type = intel_gvt_find_vgpu_type(gvt, kobject_name(kobj));
	if (!type)
		num = 0;
	else
		num = type->avail_instance;

	return sprintf(buf, "%u\n", num);
}

static ssize_t device_api_show(struct kobject *kobj, struct device *dev,
		char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_PCI_STRING);
}

static ssize_t description_show(struct kobject *kobj, struct device *dev,
		char *buf)
{
	struct intel_vgpu_type *type;
	void *gvt = kdev_to_i915(dev)->gvt;

	type = intel_gvt_find_vgpu_type(gvt, kobject_name(kobj));
	if (!type)
		return 0;

	return sprintf(buf, "low_gm_size: %dMB\nhigh_gm_size: %dMB\n"
		       "fence: %d\nresolution: %s\n",
		       BYTES_TO_MB(type->low_gm_size),
		       BYTES_TO_MB(type->high_gm_size),
		       type->fence, vgpu_edid_str(type->resolution));
}

static MDEV_TYPE_ATTR_RO(available_instances);
static MDEV_TYPE_ATTR_RO(device_api);
static MDEV_TYPE_ATTR_RO(description);

static struct attribute *type_attrs[] = {
	&mdev_type_attr_available_instances.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_description.attr,
	NULL,
};

static struct attribute_group *intel_vgpu_type_groups[] = {
	[0 ... NR_MAX_INTEL_VGPU_TYPES - 1] = NULL,
};

static bool intel_gvt_init_vgpu_type_groups(struct intel_gvt *gvt)
{
	int i, j;
	struct intel_vgpu_type *type;
	struct attribute_group *group;

	for (i = 0; i < gvt->num_types; i++) {
		type = &gvt->types[i];

		group = kzalloc(sizeof(struct attribute_group), GFP_KERNEL);
		if (WARN_ON(!group))
			goto unwind;

		group->name = type->name;
		group->attrs = type_attrs;
		intel_vgpu_type_groups[i] = group;
	}

	return true;

unwind:
	for (j = 0; j < i; j++) {
		group = intel_vgpu_type_groups[j];
		kfree(group);
	}

	return false;
}

static void intel_gvt_cleanup_vgpu_type_groups(struct intel_gvt *gvt)
{
	int i;
	struct attribute_group *group;

	for (i = 0; i < gvt->num_types; i++) {
		group = intel_vgpu_type_groups[i];
		kfree(group);
	}
}

static void kvmgt_protect_table_init(struct kvmgt_guest_info *info)
{
	hash_init(info->ptable);
}

static void kvmgt_protect_table_destroy(struct kvmgt_guest_info *info)
{
	struct kvmgt_pgfn *p;
	struct hlist_node *tmp;
	int i;

	hash_for_each_safe(info->ptable, i, tmp, p, hnode) {
		hash_del(&p->hnode);
		kfree(p);
	}
}

static struct kvmgt_pgfn *
__kvmgt_protect_table_find(struct kvmgt_guest_info *info, gfn_t gfn)
{
	struct kvmgt_pgfn *p, *res = NULL;

	hash_for_each_possible(info->ptable, p, hnode, gfn) {
		if (gfn == p->gfn) {
			res = p;
			break;
		}
	}

	return res;
}

static bool kvmgt_gfn_is_write_protected(struct kvmgt_guest_info *info,
				gfn_t gfn)
{
	struct kvmgt_pgfn *p;

	p = __kvmgt_protect_table_find(info, gfn);
	return !!p;
}

static void kvmgt_protect_table_add(struct kvmgt_guest_info *info, gfn_t gfn)
{
	struct kvmgt_pgfn *p;

	if (kvmgt_gfn_is_write_protected(info, gfn))
		return;

	p = kzalloc(sizeof(struct kvmgt_pgfn), GFP_ATOMIC);
	if (WARN(!p, "gfn: 0x%llx\n", gfn))
		return;

	p->gfn = gfn;
	hash_add(info->ptable, &p->hnode, gfn);
}

static void kvmgt_protect_table_del(struct kvmgt_guest_info *info,
				gfn_t gfn)
{
	struct kvmgt_pgfn *p;

	p = __kvmgt_protect_table_find(info, gfn);
	if (p) {
		hash_del(&p->hnode);
		kfree(p);
	}
}

static int intel_vgpu_create(struct kobject *kobj, struct mdev_device *mdev)
{
	struct intel_vgpu *vgpu;
	struct intel_vgpu_type *type;
	struct device *pdev;
	void *gvt;
	int ret;

	pdev = mdev_parent_dev(mdev);
	gvt = kdev_to_i915(pdev)->gvt;

	type = intel_gvt_find_vgpu_type(gvt, kobject_name(kobj));
	if (!type) {
		gvt_err("failed to find type %s to create\n",
						kobject_name(kobj));
		ret = -EINVAL;
		goto out;
	}

	vgpu = intel_gvt_ops->vgpu_create(gvt, type);
	if (IS_ERR_OR_NULL(vgpu)) {
		ret = vgpu == NULL ? -EFAULT : PTR_ERR(vgpu);
		gvt_err("failed to create intel vgpu: %d\n", ret);
		goto out;
	}

	INIT_WORK(&vgpu->vdev.release_work, intel_vgpu_release_work);

	vgpu->vdev.mdev = mdev;
	mdev_set_drvdata(mdev, vgpu);

	gvt_dbg_core("intel_vgpu_create succeeded for mdev: %s\n",
		     dev_name(mdev_dev(mdev)));
	ret = 0;

out:
	return ret;
}

static int intel_vgpu_remove(struct mdev_device *mdev)
{
	struct intel_vgpu *vgpu = mdev_get_drvdata(mdev);

	if (handle_valid(vgpu->handle))
		return -EBUSY;

	intel_gvt_ops->vgpu_destroy(vgpu);
	return 0;
}

static int intel_vgpu_iommu_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct intel_vgpu *vgpu = container_of(nb,
					struct intel_vgpu,
					vdev.iommu_notifier);

	if (action == VFIO_IOMMU_NOTIFY_DMA_UNMAP) {
		struct vfio_iommu_type1_dma_unmap *unmap = data;
		unsigned long gfn, end_gfn;

		gfn = unmap->iova >> PAGE_SHIFT;
		end_gfn = gfn + unmap->size / PAGE_SIZE;

		while (gfn < end_gfn)
			gvt_cache_remove(vgpu, gfn++);
	}

	return NOTIFY_OK;
}

static int intel_vgpu_group_notifier(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct intel_vgpu *vgpu = container_of(nb,
					struct intel_vgpu,
					vdev.group_notifier);

	/* the only action we care about */
	if (action == VFIO_GROUP_NOTIFY_SET_KVM) {
		vgpu->vdev.kvm = data;

		if (!data)
			schedule_work(&vgpu->vdev.release_work);
	}

	return NOTIFY_OK;
}

static int intel_vgpu_open(struct mdev_device *mdev)
{
	struct intel_vgpu *vgpu = mdev_get_drvdata(mdev);
	unsigned long events;
	int ret;

	vgpu->vdev.iommu_notifier.notifier_call = intel_vgpu_iommu_notifier;
	vgpu->vdev.group_notifier.notifier_call = intel_vgpu_group_notifier;

	events = VFIO_IOMMU_NOTIFY_DMA_UNMAP;
	ret = vfio_register_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY, &events,
				&vgpu->vdev.iommu_notifier);
	if (ret != 0) {
		gvt_err("vfio_register_notifier for iommu failed: %d\n", ret);
		goto out;
	}

	events = VFIO_GROUP_NOTIFY_SET_KVM;
	ret = vfio_register_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY, &events,
				&vgpu->vdev.group_notifier);
	if (ret != 0) {
		gvt_err("vfio_register_notifier for group failed: %d\n", ret);
		goto undo_iommu;
	}

	ret = kvmgt_guest_init(mdev);
	if (ret)
		goto undo_group;

	atomic_set(&vgpu->vdev.released, 0);
	return ret;

undo_group:
	vfio_unregister_notifier(mdev_dev(mdev), VFIO_GROUP_NOTIFY,
					&vgpu->vdev.group_notifier);

undo_iommu:
	vfio_unregister_notifier(mdev_dev(mdev), VFIO_IOMMU_NOTIFY,
					&vgpu->vdev.iommu_notifier);
out:
	return ret;
}

static void __intel_vgpu_release(struct intel_vgpu *vgpu)
{
	struct kvmgt_guest_info *info;
	int ret;

	if (!handle_valid(vgpu->handle))
		return;

	if (atomic_cmpxchg(&vgpu->vdev.released, 0, 1))
		return;

	ret = vfio_unregister_notifier(mdev_dev(vgpu->vdev.mdev), VFIO_IOMMU_NOTIFY,
					&vgpu->vdev.iommu_notifier);
	WARN(ret, "vfio_unregister_notifier for iommu failed: %d\n", ret);

	ret = vfio_unregister_notifier(mdev_dev(vgpu->vdev.mdev), VFIO_GROUP_NOTIFY,
					&vgpu->vdev.group_notifier);
	WARN(ret, "vfio_unregister_notifier for group failed: %d\n", ret);

	info = (struct kvmgt_guest_info *)vgpu->handle;
	kvmgt_guest_exit(info);

	vgpu->vdev.kvm = NULL;
	vgpu->handle = 0;
}

static void intel_vgpu_release(struct mdev_device *mdev)
{
	struct intel_vgpu *vgpu = mdev_get_drvdata(mdev);

	__intel_vgpu_release(vgpu);
}

static void intel_vgpu_release_work(struct work_struct *work)
{
	struct intel_vgpu *vgpu = container_of(work, struct intel_vgpu,
					vdev.release_work);

	__intel_vgpu_release(vgpu);
}

static uint64_t intel_vgpu_get_bar0_addr(struct intel_vgpu *vgpu)
{
	u32 start_lo, start_hi;
	u32 mem_type;
	int pos = PCI_BASE_ADDRESS_0;

	start_lo = (*(u32 *)(vgpu->cfg_space.virtual_cfg_space + pos)) &
			PCI_BASE_ADDRESS_MEM_MASK;
	mem_type = (*(u32 *)(vgpu->cfg_space.virtual_cfg_space + pos)) &
			PCI_BASE_ADDRESS_MEM_TYPE_MASK;

	switch (mem_type) {
	case PCI_BASE_ADDRESS_MEM_TYPE_64:
		start_hi = (*(u32 *)(vgpu->cfg_space.virtual_cfg_space
						+ pos + 4));
		break;
	case PCI_BASE_ADDRESS_MEM_TYPE_32:
	case PCI_BASE_ADDRESS_MEM_TYPE_1M:
		/* 1M mem BAR treated as 32-bit BAR */
	default:
		/* mem unknown type treated as 32-bit BAR */
		start_hi = 0;
		break;
	}

	return ((u64)start_hi << 32) | start_lo;
}

static ssize_t intel_vgpu_rw(struct mdev_device *mdev, char *buf,
			size_t count, loff_t *ppos, bool is_write)
{
	struct intel_vgpu *vgpu = mdev_get_drvdata(mdev);
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	uint64_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
	int ret = -EINVAL;


	if (index >= VFIO_PCI_NUM_REGIONS) {
		gvt_err("invalid index: %u\n", index);
		return -EINVAL;
	}

	switch (index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		if (is_write)
			ret = intel_gvt_ops->emulate_cfg_write(vgpu, pos,
						buf, count);
		else
			ret = intel_gvt_ops->emulate_cfg_read(vgpu, pos,
						buf, count);
		break;
	case VFIO_PCI_BAR0_REGION_INDEX:
	case VFIO_PCI_BAR1_REGION_INDEX:
		if (is_write) {
			uint64_t bar0_start = intel_vgpu_get_bar0_addr(vgpu);

			ret = intel_gvt_ops->emulate_mmio_write(vgpu,
						bar0_start + pos, buf, count);
		} else {
			uint64_t bar0_start = intel_vgpu_get_bar0_addr(vgpu);

			ret = intel_gvt_ops->emulate_mmio_read(vgpu,
						bar0_start + pos, buf, count);
		}
		break;
	case VFIO_PCI_BAR2_REGION_INDEX:
	case VFIO_PCI_BAR3_REGION_INDEX:
	case VFIO_PCI_BAR4_REGION_INDEX:
	case VFIO_PCI_BAR5_REGION_INDEX:
	case VFIO_PCI_VGA_REGION_INDEX:
	case VFIO_PCI_ROM_REGION_INDEX:
	default:
		gvt_err("unsupported region: %u\n", index);
	}

	return ret == 0 ? count : ret;
}

static ssize_t intel_vgpu_read(struct mdev_device *mdev, char __user *buf,
			size_t count, loff_t *ppos)
{
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			ret = intel_vgpu_rw(mdev, (char *)&val, sizeof(val),
					ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			ret = intel_vgpu_rw(mdev, (char *)&val, sizeof(val),
					ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 2;
		} else {
			u8 val;

			ret = intel_vgpu_rw(mdev, &val, sizeof(val), ppos,
					false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 1;
		}

		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;

read_err:
	return -EFAULT;
}

static ssize_t intel_vgpu_write(struct mdev_device *mdev,
				const char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = intel_vgpu_rw(mdev, (char *)&val, sizeof(val),
					ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = intel_vgpu_rw(mdev, (char *)&val,
					sizeof(val), ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = intel_vgpu_rw(mdev, &val, sizeof(val),
					ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 1;
		}

		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;
write_err:
	return -EFAULT;
}

static int intel_vgpu_mmap(struct mdev_device *mdev, struct vm_area_struct *vma)
{
	unsigned int index;
	u64 virtaddr;
	unsigned long req_size, pgoff = 0;
	pgprot_t pg_prot;
	struct intel_vgpu *vgpu = mdev_get_drvdata(mdev);

	index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);
	if (index >= VFIO_PCI_ROM_REGION_INDEX)
		return -EINVAL;

	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;
	if (index != VFIO_PCI_BAR2_REGION_INDEX)
		return -EINVAL;

	pg_prot = vma->vm_page_prot;
	virtaddr = vma->vm_start;
	req_size = vma->vm_end - vma->vm_start;
	pgoff = vgpu_aperture_pa_base(vgpu) >> PAGE_SHIFT;

	return remap_pfn_range(vma, virtaddr, pgoff, req_size, pg_prot);
}

static int intel_vgpu_get_irq_count(struct intel_vgpu *vgpu, int type)
{
	if (type == VFIO_PCI_INTX_IRQ_INDEX || type == VFIO_PCI_MSI_IRQ_INDEX)
		return 1;

	return 0;
}

static int intel_vgpu_set_intx_mask(struct intel_vgpu *vgpu,
			unsigned int index, unsigned int start,
			unsigned int count, uint32_t flags,
			void *data)
{
	return 0;
}

static int intel_vgpu_set_intx_unmask(struct intel_vgpu *vgpu,
			unsigned int index, unsigned int start,
			unsigned int count, uint32_t flags, void *data)
{
	return 0;
}

static int intel_vgpu_set_intx_trigger(struct intel_vgpu *vgpu,
		unsigned int index, unsigned int start, unsigned int count,
		uint32_t flags, void *data)
{
	return 0;
}

static int intel_vgpu_set_msi_trigger(struct intel_vgpu *vgpu,
		unsigned int index, unsigned int start, unsigned int count,
		uint32_t flags, void *data)
{
	struct eventfd_ctx *trigger;

	if (flags & VFIO_IRQ_SET_DATA_EVENTFD) {
		int fd = *(int *)data;

		trigger = eventfd_ctx_fdget(fd);
		if (IS_ERR(trigger)) {
			gvt_err("eventfd_ctx_fdget failed\n");
			return PTR_ERR(trigger);
		}
		vgpu->vdev.msi_trigger = trigger;
	}

	return 0;
}

static int intel_vgpu_set_irqs(struct intel_vgpu *vgpu, uint32_t flags,
		unsigned int index, unsigned int start, unsigned int count,
		void *data)
{
	int (*func)(struct intel_vgpu *vgpu, unsigned int index,
			unsigned int start, unsigned int count, uint32_t flags,
			void *data) = NULL;

	switch (index) {
	case VFIO_PCI_INTX_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_MASK:
			func = intel_vgpu_set_intx_mask;
			break;
		case VFIO_IRQ_SET_ACTION_UNMASK:
			func = intel_vgpu_set_intx_unmask;
			break;
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			func = intel_vgpu_set_intx_trigger;
			break;
		}
		break;
	case VFIO_PCI_MSI_IRQ_INDEX:
		switch (flags & VFIO_IRQ_SET_ACTION_TYPE_MASK) {
		case VFIO_IRQ_SET_ACTION_MASK:
		case VFIO_IRQ_SET_ACTION_UNMASK:
			/* XXX Need masking support exported */
			break;
		case VFIO_IRQ_SET_ACTION_TRIGGER:
			func = intel_vgpu_set_msi_trigger;
			break;
		}
		break;
	}

	if (!func)
		return -ENOTTY;

	return func(vgpu, index, start, count, flags, data);
}

static long intel_vgpu_ioctl(struct mdev_device *mdev, unsigned int cmd,
			     unsigned long arg)
{
	struct intel_vgpu *vgpu = mdev_get_drvdata(mdev);
	unsigned long minsz;

	gvt_dbg_core("vgpu%d ioctl, cmd: %d\n", vgpu->id, cmd);

	if (cmd == VFIO_DEVICE_GET_INFO) {
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		info.flags = VFIO_DEVICE_FLAGS_PCI;
		info.flags |= VFIO_DEVICE_FLAGS_RESET;
		info.num_regions = VFIO_PCI_NUM_REGIONS;
		info.num_irqs = VFIO_PCI_NUM_IRQS;

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;

	} else if (cmd == VFIO_DEVICE_GET_REGION_INFO) {
		struct vfio_region_info info;
		struct vfio_info_cap caps = { .buf = NULL, .size = 0 };
		int i, ret;
		struct vfio_region_info_cap_sparse_mmap *sparse = NULL;
		size_t size;
		int nr_areas = 1;
		int cap_type_id;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		switch (info.index) {
		case VFIO_PCI_CONFIG_REGION_INDEX:
			info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
			info.size = INTEL_GVT_MAX_CFG_SPACE_SZ;
			info.flags = VFIO_REGION_INFO_FLAG_READ |
				     VFIO_REGION_INFO_FLAG_WRITE;
			break;
		case VFIO_PCI_BAR0_REGION_INDEX:
			info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
			info.size = vgpu->cfg_space.bar[info.index].size;
			if (!info.size) {
				info.flags = 0;
				break;
			}

			info.flags = VFIO_REGION_INFO_FLAG_READ |
				     VFIO_REGION_INFO_FLAG_WRITE;
			break;
		case VFIO_PCI_BAR1_REGION_INDEX:
			info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
			info.size = 0;
			info.flags = 0;
			break;
		case VFIO_PCI_BAR2_REGION_INDEX:
			info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
			info.flags = VFIO_REGION_INFO_FLAG_CAPS |
					VFIO_REGION_INFO_FLAG_MMAP |
					VFIO_REGION_INFO_FLAG_READ |
					VFIO_REGION_INFO_FLAG_WRITE;
			info.size = gvt_aperture_sz(vgpu->gvt);

			size = sizeof(*sparse) +
					(nr_areas * sizeof(*sparse->areas));
			sparse = kzalloc(size, GFP_KERNEL);
			if (!sparse)
				return -ENOMEM;

			sparse->nr_areas = nr_areas;
			cap_type_id = VFIO_REGION_INFO_CAP_SPARSE_MMAP;
			sparse->areas[0].offset =
					PAGE_ALIGN(vgpu_aperture_offset(vgpu));
			sparse->areas[0].size = vgpu_aperture_sz(vgpu);
			break;

		case VFIO_PCI_BAR3_REGION_INDEX ... VFIO_PCI_BAR5_REGION_INDEX:
			info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);
			info.size = 0;

			info.flags = 0;
			gvt_dbg_core("get region info bar:%d\n", info.index);
			break;

		case VFIO_PCI_ROM_REGION_INDEX:
		case VFIO_PCI_VGA_REGION_INDEX:
			gvt_dbg_core("get region info index:%d\n", info.index);
			break;
		default:
			{
				struct vfio_region_info_cap_type cap_type;

				if (info.index >= VFIO_PCI_NUM_REGIONS +
						vgpu->vdev.num_regions)
					return -EINVAL;

				i = info.index - VFIO_PCI_NUM_REGIONS;

				info.offset =
					VFIO_PCI_INDEX_TO_OFFSET(info.index);
				info.size = vgpu->vdev.region[i].size;
				info.flags = vgpu->vdev.region[i].flags;

				cap_type.type = vgpu->vdev.region[i].type;
				cap_type.subtype = vgpu->vdev.region[i].subtype;

				ret = vfio_info_add_capability(&caps,
						VFIO_REGION_INFO_CAP_TYPE,
						&cap_type);
				if (ret)
					return ret;
			}
		}

		if ((info.flags & VFIO_REGION_INFO_FLAG_CAPS) && sparse) {
			switch (cap_type_id) {
			case VFIO_REGION_INFO_CAP_SPARSE_MMAP:
				ret = vfio_info_add_capability(&caps,
					VFIO_REGION_INFO_CAP_SPARSE_MMAP,
					sparse);
				kfree(sparse);
				if (ret)
					return ret;
				break;
			default:
				return -EINVAL;
			}
		}

		if (caps.size) {
			if (info.argsz < sizeof(info) + caps.size) {
				info.argsz = sizeof(info) + caps.size;
				info.cap_offset = 0;
			} else {
				vfio_info_cap_shift(&caps, sizeof(info));
				if (copy_to_user((void __user *)arg +
						  sizeof(info), caps.buf,
						  caps.size)) {
					kfree(caps.buf);
					return -EFAULT;
				}
				info.cap_offset = sizeof(info);
			}

			kfree(caps.buf);
		}

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;
	} else if (cmd == VFIO_DEVICE_GET_IRQ_INFO) {
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz || info.index >= VFIO_PCI_NUM_IRQS)
			return -EINVAL;

		switch (info.index) {
		case VFIO_PCI_INTX_IRQ_INDEX:
		case VFIO_PCI_MSI_IRQ_INDEX:
			break;
		default:
			return -EINVAL;
		}

		info.flags = VFIO_IRQ_INFO_EVENTFD;

		info.count = intel_vgpu_get_irq_count(vgpu, info.index);

		if (info.index == VFIO_PCI_INTX_IRQ_INDEX)
			info.flags |= (VFIO_IRQ_INFO_MASKABLE |
				       VFIO_IRQ_INFO_AUTOMASKED);
		else
			info.flags |= VFIO_IRQ_INFO_NORESIZE;

		return copy_to_user((void __user *)arg, &info, minsz) ?
			-EFAULT : 0;
	} else if (cmd == VFIO_DEVICE_SET_IRQS) {
		struct vfio_irq_set hdr;
		u8 *data = NULL;
		int ret = 0;
		size_t data_size = 0;

		minsz = offsetofend(struct vfio_irq_set, count);

		if (copy_from_user(&hdr, (void __user *)arg, minsz))
			return -EFAULT;

		if (!(hdr.flags & VFIO_IRQ_SET_DATA_NONE)) {
			int max = intel_vgpu_get_irq_count(vgpu, hdr.index);

			ret = vfio_set_irqs_validate_and_prepare(&hdr, max,
						VFIO_PCI_NUM_IRQS, &data_size);
			if (ret) {
				gvt_err("intel:vfio_set_irqs_validate_and_prepare failed\n");
				return -EINVAL;
			}
			if (data_size) {
				data = memdup_user((void __user *)(arg + minsz),
						   data_size);
				if (IS_ERR(data))
					return PTR_ERR(data);
			}
		}

		ret = intel_vgpu_set_irqs(vgpu, hdr.flags, hdr.index,
					hdr.start, hdr.count, data);
		kfree(data);

		return ret;
	} else if (cmd == VFIO_DEVICE_RESET) {
		intel_gvt_ops->vgpu_reset(vgpu);
		return 0;
	}

	return 0;
}

static const struct mdev_parent_ops intel_vgpu_ops = {
	.supported_type_groups	= intel_vgpu_type_groups,
	.create			= intel_vgpu_create,
	.remove			= intel_vgpu_remove,

	.open			= intel_vgpu_open,
	.release		= intel_vgpu_release,

	.read			= intel_vgpu_read,
	.write			= intel_vgpu_write,
	.mmap			= intel_vgpu_mmap,
	.ioctl			= intel_vgpu_ioctl,
};

static int kvmgt_host_init(struct device *dev, void *gvt, const void *ops)
{
	if (!intel_gvt_init_vgpu_type_groups(gvt))
		return -EFAULT;

	intel_gvt_ops = ops;

	return mdev_register_device(dev, &intel_vgpu_ops);
}

static void kvmgt_host_exit(struct device *dev, void *gvt)
{
	intel_gvt_cleanup_vgpu_type_groups(gvt);
	mdev_unregister_device(dev);
}

static int kvmgt_write_protect_add(unsigned long handle, u64 gfn)
{
	struct kvmgt_guest_info *info;
	struct kvm *kvm;
	struct kvm_memory_slot *slot;
	int idx;

	if (!handle_valid(handle))
		return -ESRCH;

	info = (struct kvmgt_guest_info *)handle;
	kvm = info->kvm;

	idx = srcu_read_lock(&kvm->srcu);
	slot = gfn_to_memslot(kvm, gfn);
	if (!slot) {
		srcu_read_unlock(&kvm->srcu, idx);
		return -EINVAL;
	}

	spin_lock(&kvm->mmu_lock);

	if (kvmgt_gfn_is_write_protected(info, gfn))
		goto out;

	kvm_slot_page_track_add_page(kvm, slot, gfn, KVM_PAGE_TRACK_WRITE);
	kvmgt_protect_table_add(info, gfn);

out:
	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);
	return 0;
}

static int kvmgt_write_protect_remove(unsigned long handle, u64 gfn)
{
	struct kvmgt_guest_info *info;
	struct kvm *kvm;
	struct kvm_memory_slot *slot;
	int idx;

	if (!handle_valid(handle))
		return 0;

	info = (struct kvmgt_guest_info *)handle;
	kvm = info->kvm;

	idx = srcu_read_lock(&kvm->srcu);
	slot = gfn_to_memslot(kvm, gfn);
	if (!slot) {
		srcu_read_unlock(&kvm->srcu, idx);
		return -EINVAL;
	}

	spin_lock(&kvm->mmu_lock);

	if (!kvmgt_gfn_is_write_protected(info, gfn))
		goto out;

	kvm_slot_page_track_remove_page(kvm, slot, gfn, KVM_PAGE_TRACK_WRITE);
	kvmgt_protect_table_del(info, gfn);

out:
	spin_unlock(&kvm->mmu_lock);
	srcu_read_unlock(&kvm->srcu, idx);
	return 0;
}

static void kvmgt_page_track_write(struct kvm_vcpu *vcpu, gpa_t gpa,
		const u8 *val, int len,
		struct kvm_page_track_notifier_node *node)
{
	struct kvmgt_guest_info *info = container_of(node,
					struct kvmgt_guest_info, track_node);

	if (kvmgt_gfn_is_write_protected(info, gpa_to_gfn(gpa)))
		intel_gvt_ops->emulate_mmio_write(info->vgpu, gpa,
					(void *)val, len);
}

static void kvmgt_page_track_flush_slot(struct kvm *kvm,
		struct kvm_memory_slot *slot,
		struct kvm_page_track_notifier_node *node)
{
	int i;
	gfn_t gfn;
	struct kvmgt_guest_info *info = container_of(node,
					struct kvmgt_guest_info, track_node);

	spin_lock(&kvm->mmu_lock);
	for (i = 0; i < slot->npages; i++) {
		gfn = slot->base_gfn + i;
		if (kvmgt_gfn_is_write_protected(info, gfn)) {
			kvm_slot_page_track_remove_page(kvm, slot, gfn,
						KVM_PAGE_TRACK_WRITE);
			kvmgt_protect_table_del(info, gfn);
		}
	}
	spin_unlock(&kvm->mmu_lock);
}

static bool __kvmgt_vgpu_exist(struct intel_vgpu *vgpu, struct kvm *kvm)
{
	struct intel_vgpu *itr;
	struct kvmgt_guest_info *info;
	int id;
	bool ret = false;

	mutex_lock(&vgpu->gvt->lock);
	for_each_active_vgpu(vgpu->gvt, itr, id) {
		if (!handle_valid(itr->handle))
			continue;

		info = (struct kvmgt_guest_info *)itr->handle;
		if (kvm && kvm == info->kvm) {
			ret = true;
			goto out;
		}
	}
out:
	mutex_unlock(&vgpu->gvt->lock);
	return ret;
}

static int kvmgt_guest_init(struct mdev_device *mdev)
{
	struct kvmgt_guest_info *info;
	struct intel_vgpu *vgpu;
	struct kvm *kvm;

	vgpu = mdev_get_drvdata(mdev);
	if (handle_valid(vgpu->handle))
		return -EEXIST;

	kvm = vgpu->vdev.kvm;
	if (!kvm || kvm->mm != current->mm) {
		gvt_err("KVM is required to use Intel vGPU\n");
		return -ESRCH;
	}

	if (__kvmgt_vgpu_exist(vgpu, kvm))
		return -EEXIST;

	info = vzalloc(sizeof(struct kvmgt_guest_info));
	if (!info)
		return -ENOMEM;

	vgpu->handle = (unsigned long)info;
	info->vgpu = vgpu;
	info->kvm = kvm;

	kvmgt_protect_table_init(info);
	gvt_cache_init(vgpu);

	info->track_node.track_write = kvmgt_page_track_write;
	info->track_node.track_flush_slot = kvmgt_page_track_flush_slot;
	kvm_page_track_register_notifier(kvm, &info->track_node);

	return 0;
}

static bool kvmgt_guest_exit(struct kvmgt_guest_info *info)
{
	if (!info) {
		gvt_err("kvmgt_guest_info invalid\n");
		return false;
	}

	kvm_page_track_unregister_notifier(info->kvm, &info->track_node);
	kvmgt_protect_table_destroy(info);
	gvt_cache_destroy(info->vgpu);
	vfree(info);

	return true;
}

static int kvmgt_attach_vgpu(void *vgpu, unsigned long *handle)
{
	/* nothing to do here */
	return 0;
}

static void kvmgt_detach_vgpu(unsigned long handle)
{
	/* nothing to do here */
}

static int kvmgt_inject_msi(unsigned long handle, u32 addr, u16 data)
{
	struct kvmgt_guest_info *info;
	struct intel_vgpu *vgpu;

	if (!handle_valid(handle))
		return -ESRCH;

	info = (struct kvmgt_guest_info *)handle;
	vgpu = info->vgpu;

	if (eventfd_signal(vgpu->vdev.msi_trigger, 1) == 1)
		return 0;

	return -EFAULT;
}

static unsigned long kvmgt_gfn_to_pfn(unsigned long handle, unsigned long gfn)
{
	unsigned long iova, pfn;
	struct kvmgt_guest_info *info;
	struct device *dev;
	int rc;

	if (!handle_valid(handle))
		return INTEL_GVT_INVALID_ADDR;

	info = (struct kvmgt_guest_info *)handle;
	iova = gvt_cache_find(info->vgpu, gfn);
	if (iova != INTEL_GVT_INVALID_ADDR)
		return iova;

	pfn = INTEL_GVT_INVALID_ADDR;
	dev = mdev_dev(info->vgpu->vdev.mdev);
	rc = vfio_pin_pages(dev, &gfn, 1, IOMMU_READ | IOMMU_WRITE, &pfn);
	if (rc != 1) {
		gvt_err("vfio_pin_pages failed for gfn 0x%lx: %d\n", gfn, rc);
		return INTEL_GVT_INVALID_ADDR;
	}
	/* transfer to host iova for GFX to use DMA */
	rc = gvt_dma_map_iova(info->vgpu, pfn, &iova);
	if (rc) {
		gvt_err("gvt_dma_map_iova failed for gfn: 0x%lx\n", gfn);
		vfio_unpin_pages(dev, &gfn, 1);
		return INTEL_GVT_INVALID_ADDR;
	}

	gvt_cache_add(info->vgpu, gfn, iova);
	return iova;
}

static int kvmgt_rw_gpa(unsigned long handle, unsigned long gpa,
			void *buf, unsigned long len, bool write)
{
	struct kvmgt_guest_info *info;
	struct kvm *kvm;
	int ret;
	bool kthread = current->mm == NULL;

	if (!handle_valid(handle))
		return -ESRCH;

	info = (struct kvmgt_guest_info *)handle;
	kvm = info->kvm;

	if (kthread)
		use_mm(kvm->mm);

	ret = write ? kvm_write_guest(kvm, gpa, buf, len) :
		      kvm_read_guest(kvm, gpa, buf, len);

	if (kthread)
		unuse_mm(kvm->mm);

	return ret;
}

static int kvmgt_read_gpa(unsigned long handle, unsigned long gpa,
			void *buf, unsigned long len)
{
	return kvmgt_rw_gpa(handle, gpa, buf, len, false);
}

static int kvmgt_write_gpa(unsigned long handle, unsigned long gpa,
			void *buf, unsigned long len)
{
	return kvmgt_rw_gpa(handle, gpa, buf, len, true);
}

static unsigned long kvmgt_virt_to_pfn(void *addr)
{
	return PFN_DOWN(__pa(addr));
}

struct intel_gvt_mpt kvmgt_mpt = {
	.host_init = kvmgt_host_init,
	.host_exit = kvmgt_host_exit,
	.attach_vgpu = kvmgt_attach_vgpu,
	.detach_vgpu = kvmgt_detach_vgpu,
	.inject_msi = kvmgt_inject_msi,
	.from_virt_to_mfn = kvmgt_virt_to_pfn,
	.set_wp_page = kvmgt_write_protect_add,
	.unset_wp_page = kvmgt_write_protect_remove,
	.read_gpa = kvmgt_read_gpa,
	.write_gpa = kvmgt_write_gpa,
	.gfn_to_mfn = kvmgt_gfn_to_pfn,
};
EXPORT_SYMBOL_GPL(kvmgt_mpt);

static int __init kvmgt_init(void)
{
	return 0;
}

static void __exit kvmgt_exit(void)
{
}

module_init(kvmgt_init);
module_exit(kvmgt_exit);

MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Intel Corporation");

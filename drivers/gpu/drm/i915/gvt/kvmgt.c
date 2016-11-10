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
#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/spinlock.h>
#include <linux/eventfd.h>
#include <linux/uuid.h>
#include <linux/kvm_host.h>
#include <linux/vfio.h>

#include "i915_drv.h"
#include "gvt.h"

#if IS_ENABLED(CONFIG_VFIO_MDEV)
#include <linux/mdev.h>
#else
static inline long vfio_pin_pages(struct device *dev, unsigned long *user_pfn,
			long npage, int prot, unsigned long *phys_pfn)
{
	return 0;
}
static inline long vfio_unpin_pages(struct device *dev, unsigned long *pfn,
			long npage)
{
	return 0;
}
#endif

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
	kvm_pfn_t pfn;
};

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

static kvm_pfn_t gvt_cache_find(struct intel_vgpu *vgpu, gfn_t gfn)
{
	struct gvt_dma *entry;

	mutex_lock(&vgpu->vdev.cache_lock);
	entry = __gvt_cache_find(vgpu, gfn);
	mutex_unlock(&vgpu->vdev.cache_lock);

	return entry == NULL ? 0 : entry->pfn;
}

static void gvt_cache_add(struct intel_vgpu *vgpu, gfn_t gfn, kvm_pfn_t pfn)
{
	struct gvt_dma *new, *itr;
	struct rb_node **link = &vgpu->vdev.cache.rb_node, *parent = NULL;

	new = kzalloc(sizeof(struct gvt_dma), GFP_KERNEL);
	if (!new)
		return;

	new->gfn = gfn;
	new->pfn = pfn;

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
	struct device *dev = vgpu->vdev.mdev;
	struct gvt_dma *this;
	unsigned long pfn;

	mutex_lock(&vgpu->vdev.cache_lock);
	this  = __gvt_cache_find(vgpu, gfn);
	if (!this) {
		mutex_unlock(&vgpu->vdev.cache_lock);
		return;
	}

	pfn = this->pfn;
	WARN_ON((vfio_unpin_pages(dev, &pfn, 1) != 1));
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
	struct device *dev = vgpu->vdev.mdev;
	unsigned long pfn;

	mutex_lock(&vgpu->vdev.cache_lock);
	while ((node = rb_first(&vgpu->vdev.cache))) {
		dma = rb_entry(node, struct gvt_dma, node);
		pfn = dma->pfn;

		vfio_unpin_pages(dev, &pfn, 1);
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

static struct attribute *type_attrs[] = {
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

	p = kmalloc(sizeof(struct kvmgt_pgfn), GFP_ATOMIC);
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

static int kvmgt_host_init(struct device *dev, void *gvt, const void *ops)
{
	if (!intel_gvt_init_vgpu_type_groups(gvt))
		return -EFAULT;

	intel_gvt_ops = ops;

	/* MDEV is not yet available */
	return -ENODEV;
}

static void kvmgt_host_exit(struct device *dev, void *gvt)
{
	intel_gvt_cleanup_vgpu_type_groups(gvt);
}

static int kvmgt_write_protect_add(unsigned long handle, u64 gfn)
{
	struct kvmgt_guest_info *info = (struct kvmgt_guest_info *)handle;
	struct kvm *kvm = info->kvm;
	struct kvm_memory_slot *slot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	slot = gfn_to_memslot(kvm, gfn);

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
	struct kvmgt_guest_info *info = (struct kvmgt_guest_info *)handle;
	struct kvm *kvm = info->kvm;
	struct kvm_memory_slot *slot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);
	slot = gfn_to_memslot(kvm, gfn);

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

static bool kvmgt_check_guest(void)
{
	unsigned int eax, ebx, ecx, edx;
	char s[12];
	unsigned int *i;

	eax = KVM_CPUID_SIGNATURE;
	ebx = ecx = edx = 0;

	asm volatile ("cpuid"
		      : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		      :
		      : "cc", "memory");
	i = (unsigned int *)s;
	i[0] = ebx;
	i[1] = ecx;
	i[2] = edx;

	return !strncmp(s, "KVMKVMKVM", strlen("KVMKVMKVM"));
}

/**
 * NOTE:
 * It's actually impossible to check if we are running in KVM host,
 * since the "KVM host" is simply native. So we only dectect guest here.
 */
static int kvmgt_detect_host(void)
{
#ifdef CONFIG_INTEL_IOMMU
	if (intel_iommu_gfx_mapped) {
		gvt_err("Hardware IOMMU compatibility not yet supported, try to boot with intel_iommu=igfx_off\n");
		return -ENODEV;
	}
#endif
	return kvmgt_check_guest() ? -ENODEV : 0;
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
	struct kvmgt_guest_info *info = (struct kvmgt_guest_info *)handle;
	struct intel_vgpu *vgpu = info->vgpu;

	if (vgpu->vdev.msi_trigger)
		return eventfd_signal(vgpu->vdev.msi_trigger, 1) == 1;

	return false;
}

static unsigned long kvmgt_gfn_to_pfn(unsigned long handle, unsigned long gfn)
{
	unsigned long pfn;
	struct kvmgt_guest_info *info = (struct kvmgt_guest_info *)handle;
	int rc;

	pfn = gvt_cache_find(info->vgpu, gfn);
	if (pfn != 0)
		return pfn;

	rc = vfio_pin_pages(info->vgpu->vdev.mdev, &gfn, 1,
				IOMMU_READ | IOMMU_WRITE, &pfn);
	if (rc != 1) {
		gvt_err("vfio_pin_pages failed for gfn: 0x%lx\n", gfn);
		return 0;
	}

	gvt_cache_add(info->vgpu, gfn, pfn);
	return pfn;
}

static void *kvmgt_gpa_to_hva(unsigned long handle, unsigned long gpa)
{
	unsigned long pfn;
	gfn_t gfn = gpa_to_gfn(gpa);

	pfn = kvmgt_gfn_to_pfn(handle, gfn);
	if (!pfn)
		return NULL;

	return (char *)pfn_to_kaddr(pfn) + offset_in_page(gpa);
}

static int kvmgt_rw_gpa(unsigned long handle, unsigned long gpa,
			void *buf, unsigned long len, bool write)
{
	void *hva = NULL;

	hva = kvmgt_gpa_to_hva(handle, gpa);
	if (!hva)
		return -EFAULT;

	if (write)
		memcpy(hva, buf, len);
	else
		memcpy(buf, hva, len);

	return 0;
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
	.detect_host = kvmgt_detect_host,
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

/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This module enables machines with Intel VT-x extensions to run virtual
 * machines without emulation or binary translation.
 *
 * Copyright (C) 2006 Qumranet, Inc.
 *
 * Authors:
 *   Avi Kivity   <avi@qumranet.com>
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "kvm.h"

#include <linux/kvm.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <asm/processor.h>
#include <linux/percpu.h>
#include <linux/gfp.h>
#include <asm/msr.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <linux/reboot.h>
#include <asm/io.h>
#include <linux/debugfs.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <asm/desc.h>

#include "x86_emulate.h"
#include "segment_descriptor.h"

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

struct kvm_arch_ops *kvm_arch_ops;
struct kvm_stat kvm_stat;
EXPORT_SYMBOL_GPL(kvm_stat);

static struct kvm_stats_debugfs_item {
	const char *name;
	u32 *data;
	struct dentry *dentry;
} debugfs_entries[] = {
	{ "pf_fixed", &kvm_stat.pf_fixed },
	{ "pf_guest", &kvm_stat.pf_guest },
	{ "tlb_flush", &kvm_stat.tlb_flush },
	{ "invlpg", &kvm_stat.invlpg },
	{ "exits", &kvm_stat.exits },
	{ "io_exits", &kvm_stat.io_exits },
	{ "mmio_exits", &kvm_stat.mmio_exits },
	{ "signal_exits", &kvm_stat.signal_exits },
	{ "irq_exits", &kvm_stat.irq_exits },
	{ 0, 0 }
};

static struct dentry *debugfs_dir;

#define MAX_IO_MSRS 256

#define CR0_RESEVED_BITS 0xffffffff1ffaffc0ULL
#define LMSW_GUEST_MASK 0x0eULL
#define CR4_RESEVED_BITS (~((1ULL << 11) - 1))
#define CR8_RESEVED_BITS (~0x0fULL)
#define EFER_RESERVED_BITS 0xfffffffffffff2fe

#ifdef CONFIG_X86_64
// LDT or TSS descriptor in the GDT. 16 bytes.
struct segment_descriptor_64 {
	struct segment_descriptor s;
	u32 base_higher;
	u32 pad_zero;
};

#endif

unsigned long segment_base(u16 selector)
{
	struct descriptor_table gdt;
	struct segment_descriptor *d;
	unsigned long table_base;
	typedef unsigned long ul;
	unsigned long v;

	if (selector == 0)
		return 0;

	asm ("sgdt %0" : "=m"(gdt));
	table_base = gdt.base;

	if (selector & 4) {           /* from ldt */
		u16 ldt_selector;

		asm ("sldt %0" : "=g"(ldt_selector));
		table_base = segment_base(ldt_selector);
	}
	d = (struct segment_descriptor *)(table_base + (selector & ~7));
	v = d->base_low | ((ul)d->base_mid << 16) | ((ul)d->base_high << 24);
#ifdef CONFIG_X86_64
	if (d->system == 0
	    && (d->type == 2 || d->type == 9 || d->type == 11))
		v |= ((ul)((struct segment_descriptor_64 *)d)->base_higher) << 32;
#endif
	return v;
}
EXPORT_SYMBOL_GPL(segment_base);

int kvm_read_guest(struct kvm_vcpu *vcpu,
			     gva_t addr,
			     unsigned long size,
			     void *dest)
{
	unsigned char *host_buf = dest;
	unsigned long req_size = size;

	while (size) {
		hpa_t paddr;
		unsigned now;
		unsigned offset;
		hva_t guest_buf;

		paddr = gva_to_hpa(vcpu, addr);

		if (is_error_hpa(paddr))
			break;

		guest_buf = (hva_t)kmap_atomic(
					pfn_to_page(paddr >> PAGE_SHIFT),
					KM_USER0);
		offset = addr & ~PAGE_MASK;
		guest_buf |= offset;
		now = min(size, PAGE_SIZE - offset);
		memcpy(host_buf, (void*)guest_buf, now);
		host_buf += now;
		addr += now;
		size -= now;
		kunmap_atomic((void *)(guest_buf & PAGE_MASK), KM_USER0);
	}
	return req_size - size;
}
EXPORT_SYMBOL_GPL(kvm_read_guest);

int kvm_write_guest(struct kvm_vcpu *vcpu,
			     gva_t addr,
			     unsigned long size,
			     void *data)
{
	unsigned char *host_buf = data;
	unsigned long req_size = size;

	while (size) {
		hpa_t paddr;
		unsigned now;
		unsigned offset;
		hva_t guest_buf;

		paddr = gva_to_hpa(vcpu, addr);

		if (is_error_hpa(paddr))
			break;

		guest_buf = (hva_t)kmap_atomic(
				pfn_to_page(paddr >> PAGE_SHIFT), KM_USER0);
		offset = addr & ~PAGE_MASK;
		guest_buf |= offset;
		now = min(size, PAGE_SIZE - offset);
		memcpy((void*)guest_buf, host_buf, now);
		host_buf += now;
		addr += now;
		size -= now;
		kunmap_atomic((void *)(guest_buf & PAGE_MASK), KM_USER0);
	}
	return req_size - size;
}
EXPORT_SYMBOL_GPL(kvm_write_guest);

static int vcpu_slot(struct kvm_vcpu *vcpu)
{
	return vcpu - vcpu->kvm->vcpus;
}

/*
 * Switches to specified vcpu, until a matching vcpu_put()
 */
static struct kvm_vcpu *vcpu_load(struct kvm *kvm, int vcpu_slot)
{
	struct kvm_vcpu *vcpu = &kvm->vcpus[vcpu_slot];

	mutex_lock(&vcpu->mutex);
	if (unlikely(!vcpu->vmcs)) {
		mutex_unlock(&vcpu->mutex);
		return 0;
	}
	return kvm_arch_ops->vcpu_load(vcpu);
}

static void vcpu_put(struct kvm_vcpu *vcpu)
{
	kvm_arch_ops->vcpu_put(vcpu);
	put_cpu();
	mutex_unlock(&vcpu->mutex);
}

static int kvm_dev_open(struct inode *inode, struct file *filp)
{
	struct kvm *kvm = kzalloc(sizeof(struct kvm), GFP_KERNEL);
	int i;

	if (!kvm)
		return -ENOMEM;

	spin_lock_init(&kvm->lock);
	INIT_LIST_HEAD(&kvm->active_mmu_pages);
	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		struct kvm_vcpu *vcpu = &kvm->vcpus[i];

		mutex_init(&vcpu->mutex);
		vcpu->mmu.root_hpa = INVALID_PAGE;
		INIT_LIST_HEAD(&vcpu->free_pages);
	}
	filp->private_data = kvm;
	return 0;
}

/*
 * Free any memory in @free but not in @dont.
 */
static void kvm_free_physmem_slot(struct kvm_memory_slot *free,
				  struct kvm_memory_slot *dont)
{
	int i;

	if (!dont || free->phys_mem != dont->phys_mem)
		if (free->phys_mem) {
			for (i = 0; i < free->npages; ++i)
				__free_page(free->phys_mem[i]);
			vfree(free->phys_mem);
		}

	if (!dont || free->dirty_bitmap != dont->dirty_bitmap)
		vfree(free->dirty_bitmap);

	free->phys_mem = 0;
	free->npages = 0;
	free->dirty_bitmap = 0;
}

static void kvm_free_physmem(struct kvm *kvm)
{
	int i;

	for (i = 0; i < kvm->nmemslots; ++i)
		kvm_free_physmem_slot(&kvm->memslots[i], 0);
}

static void kvm_free_vcpu(struct kvm_vcpu *vcpu)
{
	kvm_arch_ops->vcpu_free(vcpu);
	kvm_mmu_destroy(vcpu);
}

static void kvm_free_vcpus(struct kvm *kvm)
{
	unsigned int i;

	for (i = 0; i < KVM_MAX_VCPUS; ++i)
		kvm_free_vcpu(&kvm->vcpus[i]);
}

static int kvm_dev_release(struct inode *inode, struct file *filp)
{
	struct kvm *kvm = filp->private_data;

	kvm_free_vcpus(kvm);
	kvm_free_physmem(kvm);
	kfree(kvm);
	return 0;
}

static void inject_gp(struct kvm_vcpu *vcpu)
{
	kvm_arch_ops->inject_gp(vcpu, 0);
}

static int pdptrs_have_reserved_bits_set(struct kvm_vcpu *vcpu,
					 unsigned long cr3)
{
	gfn_t pdpt_gfn = cr3 >> PAGE_SHIFT;
	unsigned offset = (cr3 & (PAGE_SIZE-1)) >> 5;
	int i;
	u64 pdpte;
	u64 *pdpt;
	struct kvm_memory_slot *memslot;

	spin_lock(&vcpu->kvm->lock);
	memslot = gfn_to_memslot(vcpu->kvm, pdpt_gfn);
	/* FIXME: !memslot - emulate? 0xff? */
	pdpt = kmap_atomic(gfn_to_page(memslot, pdpt_gfn), KM_USER0);

	for (i = 0; i < 4; ++i) {
		pdpte = pdpt[offset + i];
		if ((pdpte & 1) && (pdpte & 0xfffffff0000001e6ull))
			break;
	}

	kunmap_atomic(pdpt, KM_USER0);
	spin_unlock(&vcpu->kvm->lock);

	return i != 4;
}

void set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0)
{
	if (cr0 & CR0_RESEVED_BITS) {
		printk(KERN_DEBUG "set_cr0: 0x%lx #GP, reserved bits 0x%lx\n",
		       cr0, vcpu->cr0);
		inject_gp(vcpu);
		return;
	}

	if ((cr0 & CR0_NW_MASK) && !(cr0 & CR0_CD_MASK)) {
		printk(KERN_DEBUG "set_cr0: #GP, CD == 0 && NW == 1\n");
		inject_gp(vcpu);
		return;
	}

	if ((cr0 & CR0_PG_MASK) && !(cr0 & CR0_PE_MASK)) {
		printk(KERN_DEBUG "set_cr0: #GP, set PG flag "
		       "and a clear PE flag\n");
		inject_gp(vcpu);
		return;
	}

	if (!is_paging(vcpu) && (cr0 & CR0_PG_MASK)) {
#ifdef CONFIG_X86_64
		if ((vcpu->shadow_efer & EFER_LME)) {
			int cs_db, cs_l;

			if (!is_pae(vcpu)) {
				printk(KERN_DEBUG "set_cr0: #GP, start paging "
				       "in long mode while PAE is disabled\n");
				inject_gp(vcpu);
				return;
			}
			kvm_arch_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);
			if (cs_l) {
				printk(KERN_DEBUG "set_cr0: #GP, start paging "
				       "in long mode while CS.L == 1\n");
				inject_gp(vcpu);
				return;

			}
		} else
#endif
		if (is_pae(vcpu) &&
			    pdptrs_have_reserved_bits_set(vcpu, vcpu->cr3)) {
			printk(KERN_DEBUG "set_cr0: #GP, pdptrs "
			       "reserved bits\n");
			inject_gp(vcpu);
			return;
		}

	}

	kvm_arch_ops->set_cr0(vcpu, cr0);
	vcpu->cr0 = cr0;

	spin_lock(&vcpu->kvm->lock);
	kvm_mmu_reset_context(vcpu);
	spin_unlock(&vcpu->kvm->lock);
	return;
}
EXPORT_SYMBOL_GPL(set_cr0);

void lmsw(struct kvm_vcpu *vcpu, unsigned long msw)
{
	set_cr0(vcpu, (vcpu->cr0 & ~0x0ful) | (msw & 0x0f));
}
EXPORT_SYMBOL_GPL(lmsw);

void set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4)
{
	if (cr4 & CR4_RESEVED_BITS) {
		printk(KERN_DEBUG "set_cr4: #GP, reserved bits\n");
		inject_gp(vcpu);
		return;
	}

	if (kvm_arch_ops->is_long_mode(vcpu)) {
		if (!(cr4 & CR4_PAE_MASK)) {
			printk(KERN_DEBUG "set_cr4: #GP, clearing PAE while "
			       "in long mode\n");
			inject_gp(vcpu);
			return;
		}
	} else if (is_paging(vcpu) && !is_pae(vcpu) && (cr4 & CR4_PAE_MASK)
		   && pdptrs_have_reserved_bits_set(vcpu, vcpu->cr3)) {
		printk(KERN_DEBUG "set_cr4: #GP, pdptrs reserved bits\n");
		inject_gp(vcpu);
	}

	if (cr4 & CR4_VMXE_MASK) {
		printk(KERN_DEBUG "set_cr4: #GP, setting VMXE\n");
		inject_gp(vcpu);
		return;
	}
	kvm_arch_ops->set_cr4(vcpu, cr4);
	spin_lock(&vcpu->kvm->lock);
	kvm_mmu_reset_context(vcpu);
	spin_unlock(&vcpu->kvm->lock);
}
EXPORT_SYMBOL_GPL(set_cr4);

void set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	if (kvm_arch_ops->is_long_mode(vcpu)) {
		if ( cr3 & CR3_L_MODE_RESEVED_BITS) {
			printk(KERN_DEBUG "set_cr3: #GP, reserved bits\n");
			inject_gp(vcpu);
			return;
		}
	} else {
		if (cr3 & CR3_RESEVED_BITS) {
			printk(KERN_DEBUG "set_cr3: #GP, reserved bits\n");
			inject_gp(vcpu);
			return;
		}
		if (is_paging(vcpu) && is_pae(vcpu) &&
		    pdptrs_have_reserved_bits_set(vcpu, cr3)) {
			printk(KERN_DEBUG "set_cr3: #GP, pdptrs "
			       "reserved bits\n");
			inject_gp(vcpu);
			return;
		}
	}

	vcpu->cr3 = cr3;
	spin_lock(&vcpu->kvm->lock);
	vcpu->mmu.new_cr3(vcpu);
	spin_unlock(&vcpu->kvm->lock);
}
EXPORT_SYMBOL_GPL(set_cr3);

void set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8)
{
	if ( cr8 & CR8_RESEVED_BITS) {
		printk(KERN_DEBUG "set_cr8: #GP, reserved bits 0x%lx\n", cr8);
		inject_gp(vcpu);
		return;
	}
	vcpu->cr8 = cr8;
}
EXPORT_SYMBOL_GPL(set_cr8);

void fx_init(struct kvm_vcpu *vcpu)
{
	struct __attribute__ ((__packed__)) fx_image_s {
		u16 control; //fcw
		u16 status; //fsw
		u16 tag; // ftw
		u16 opcode; //fop
		u64 ip; // fpu ip
		u64 operand;// fpu dp
		u32 mxcsr;
		u32 mxcsr_mask;

	} *fx_image;

	fx_save(vcpu->host_fx_image);
	fpu_init();
	fx_save(vcpu->guest_fx_image);
	fx_restore(vcpu->host_fx_image);

	fx_image = (struct fx_image_s *)vcpu->guest_fx_image;
	fx_image->mxcsr = 0x1f80;
	memset(vcpu->guest_fx_image + sizeof(struct fx_image_s),
	       0, FX_IMAGE_SIZE - sizeof(struct fx_image_s));
}
EXPORT_SYMBOL_GPL(fx_init);

/*
 * Creates some virtual cpus.  Good luck creating more than one.
 */
static int kvm_dev_ioctl_create_vcpu(struct kvm *kvm, int n)
{
	int r;
	struct kvm_vcpu *vcpu;

	r = -EINVAL;
	if (n < 0 || n >= KVM_MAX_VCPUS)
		goto out;

	vcpu = &kvm->vcpus[n];

	mutex_lock(&vcpu->mutex);

	if (vcpu->vmcs) {
		mutex_unlock(&vcpu->mutex);
		return -EEXIST;
	}

	vcpu->host_fx_image = (char*)ALIGN((hva_t)vcpu->fx_buf,
					   FX_IMAGE_ALIGN);
	vcpu->guest_fx_image = vcpu->host_fx_image + FX_IMAGE_SIZE;

	vcpu->cpu = -1;  /* First load will set up TR */
	vcpu->kvm = kvm;
	r = kvm_arch_ops->vcpu_create(vcpu);
	if (r < 0)
		goto out_free_vcpus;

	kvm_arch_ops->vcpu_load(vcpu);

	r = kvm_arch_ops->vcpu_setup(vcpu);
	if (r >= 0)
		r = kvm_mmu_init(vcpu);

	vcpu_put(vcpu);

	if (r < 0)
		goto out_free_vcpus;

	return 0;

out_free_vcpus:
	kvm_free_vcpu(vcpu);
	mutex_unlock(&vcpu->mutex);
out:
	return r;
}

/*
 * Allocate some memory and give it an address in the guest physical address
 * space.
 *
 * Discontiguous memory is allowed, mostly for framebuffers.
 */
static int kvm_dev_ioctl_set_memory_region(struct kvm *kvm,
					   struct kvm_memory_region *mem)
{
	int r;
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long i;
	struct kvm_memory_slot *memslot;
	struct kvm_memory_slot old, new;
	int memory_config_version;

	r = -EINVAL;
	/* General sanity checks */
	if (mem->memory_size & (PAGE_SIZE - 1))
		goto out;
	if (mem->guest_phys_addr & (PAGE_SIZE - 1))
		goto out;
	if (mem->slot >= KVM_MEMORY_SLOTS)
		goto out;
	if (mem->guest_phys_addr + mem->memory_size < mem->guest_phys_addr)
		goto out;

	memslot = &kvm->memslots[mem->slot];
	base_gfn = mem->guest_phys_addr >> PAGE_SHIFT;
	npages = mem->memory_size >> PAGE_SHIFT;

	if (!npages)
		mem->flags &= ~KVM_MEM_LOG_DIRTY_PAGES;

raced:
	spin_lock(&kvm->lock);

	memory_config_version = kvm->memory_config_version;
	new = old = *memslot;

	new.base_gfn = base_gfn;
	new.npages = npages;
	new.flags = mem->flags;

	/* Disallow changing a memory slot's size. */
	r = -EINVAL;
	if (npages && old.npages && npages != old.npages)
		goto out_unlock;

	/* Check for overlaps */
	r = -EEXIST;
	for (i = 0; i < KVM_MEMORY_SLOTS; ++i) {
		struct kvm_memory_slot *s = &kvm->memslots[i];

		if (s == memslot)
			continue;
		if (!((base_gfn + npages <= s->base_gfn) ||
		      (base_gfn >= s->base_gfn + s->npages)))
			goto out_unlock;
	}
	/*
	 * Do memory allocations outside lock.  memory_config_version will
	 * detect any races.
	 */
	spin_unlock(&kvm->lock);

	/* Deallocate if slot is being removed */
	if (!npages)
		new.phys_mem = 0;

	/* Free page dirty bitmap if unneeded */
	if (!(new.flags & KVM_MEM_LOG_DIRTY_PAGES))
		new.dirty_bitmap = 0;

	r = -ENOMEM;

	/* Allocate if a slot is being created */
	if (npages && !new.phys_mem) {
		new.phys_mem = vmalloc(npages * sizeof(struct page *));

		if (!new.phys_mem)
			goto out_free;

		memset(new.phys_mem, 0, npages * sizeof(struct page *));
		for (i = 0; i < npages; ++i) {
			new.phys_mem[i] = alloc_page(GFP_HIGHUSER
						     | __GFP_ZERO);
			if (!new.phys_mem[i])
				goto out_free;
		}
	}

	/* Allocate page dirty bitmap if needed */
	if ((new.flags & KVM_MEM_LOG_DIRTY_PAGES) && !new.dirty_bitmap) {
		unsigned dirty_bytes = ALIGN(npages, BITS_PER_LONG) / 8;

		new.dirty_bitmap = vmalloc(dirty_bytes);
		if (!new.dirty_bitmap)
			goto out_free;
		memset(new.dirty_bitmap, 0, dirty_bytes);
	}

	spin_lock(&kvm->lock);

	if (memory_config_version != kvm->memory_config_version) {
		spin_unlock(&kvm->lock);
		kvm_free_physmem_slot(&new, &old);
		goto raced;
	}

	r = -EAGAIN;
	if (kvm->busy)
		goto out_unlock;

	if (mem->slot >= kvm->nmemslots)
		kvm->nmemslots = mem->slot + 1;

	*memslot = new;
	++kvm->memory_config_version;

	spin_unlock(&kvm->lock);

	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		struct kvm_vcpu *vcpu;

		vcpu = vcpu_load(kvm, i);
		if (!vcpu)
			continue;
		kvm_mmu_reset_context(vcpu);
		vcpu_put(vcpu);
	}

	kvm_free_physmem_slot(&old, &new);
	return 0;

out_unlock:
	spin_unlock(&kvm->lock);
out_free:
	kvm_free_physmem_slot(&new, &old);
out:
	return r;
}

/*
 * Get (and clear) the dirty memory log for a memory slot.
 */
static int kvm_dev_ioctl_get_dirty_log(struct kvm *kvm,
				       struct kvm_dirty_log *log)
{
	struct kvm_memory_slot *memslot;
	int r, i;
	int n;
	unsigned long any = 0;

	spin_lock(&kvm->lock);

	/*
	 * Prevent changes to guest memory configuration even while the lock
	 * is not taken.
	 */
	++kvm->busy;
	spin_unlock(&kvm->lock);
	r = -EINVAL;
	if (log->slot >= KVM_MEMORY_SLOTS)
		goto out;

	memslot = &kvm->memslots[log->slot];
	r = -ENOENT;
	if (!memslot->dirty_bitmap)
		goto out;

	n = ALIGN(memslot->npages, 8) / 8;

	for (i = 0; !any && i < n; ++i)
		any = memslot->dirty_bitmap[i];

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, memslot->dirty_bitmap, n))
		goto out;


	if (any) {
		spin_lock(&kvm->lock);
		kvm_mmu_slot_remove_write_access(kvm, log->slot);
		spin_unlock(&kvm->lock);
		memset(memslot->dirty_bitmap, 0, n);
		for (i = 0; i < KVM_MAX_VCPUS; ++i) {
			struct kvm_vcpu *vcpu = vcpu_load(kvm, i);

			if (!vcpu)
				continue;
			kvm_arch_ops->tlb_flush(vcpu);
			vcpu_put(vcpu);
		}
	}

	r = 0;

out:
	spin_lock(&kvm->lock);
	--kvm->busy;
	spin_unlock(&kvm->lock);
	return r;
}

struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn)
{
	int i;

	for (i = 0; i < kvm->nmemslots; ++i) {
		struct kvm_memory_slot *memslot = &kvm->memslots[i];

		if (gfn >= memslot->base_gfn
		    && gfn < memslot->base_gfn + memslot->npages)
			return memslot;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(gfn_to_memslot);

void mark_page_dirty(struct kvm *kvm, gfn_t gfn)
{
	int i;
	struct kvm_memory_slot *memslot = 0;
	unsigned long rel_gfn;

	for (i = 0; i < kvm->nmemslots; ++i) {
		memslot = &kvm->memslots[i];

		if (gfn >= memslot->base_gfn
		    && gfn < memslot->base_gfn + memslot->npages) {

			if (!memslot || !memslot->dirty_bitmap)
				return;

			rel_gfn = gfn - memslot->base_gfn;

			/* avoid RMW */
			if (!test_bit(rel_gfn, memslot->dirty_bitmap))
				set_bit(rel_gfn, memslot->dirty_bitmap);
			return;
		}
	}
}

static int emulator_read_std(unsigned long addr,
			     unsigned long *val,
			     unsigned int bytes,
			     struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;
	void *data = val;

	while (bytes) {
		gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, addr);
		unsigned offset = addr & (PAGE_SIZE-1);
		unsigned tocopy = min(bytes, (unsigned)PAGE_SIZE - offset);
		unsigned long pfn;
		struct kvm_memory_slot *memslot;
		void *page;

		if (gpa == UNMAPPED_GVA)
			return X86EMUL_PROPAGATE_FAULT;
		pfn = gpa >> PAGE_SHIFT;
		memslot = gfn_to_memslot(vcpu->kvm, pfn);
		if (!memslot)
			return X86EMUL_UNHANDLEABLE;
		page = kmap_atomic(gfn_to_page(memslot, pfn), KM_USER0);

		memcpy(data, page + offset, tocopy);

		kunmap_atomic(page, KM_USER0);

		bytes -= tocopy;
		data += tocopy;
		addr += tocopy;
	}

	return X86EMUL_CONTINUE;
}

static int emulator_write_std(unsigned long addr,
			      unsigned long val,
			      unsigned int bytes,
			      struct x86_emulate_ctxt *ctxt)
{
	printk(KERN_ERR "emulator_write_std: addr %lx n %d\n",
	       addr, bytes);
	return X86EMUL_UNHANDLEABLE;
}

static int emulator_read_emulated(unsigned long addr,
				  unsigned long *val,
				  unsigned int bytes,
				  struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	if (vcpu->mmio_read_completed) {
		memcpy(val, vcpu->mmio_data, bytes);
		vcpu->mmio_read_completed = 0;
		return X86EMUL_CONTINUE;
	} else if (emulator_read_std(addr, val, bytes, ctxt)
		   == X86EMUL_CONTINUE)
		return X86EMUL_CONTINUE;
	else {
		gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, addr);
		if (gpa == UNMAPPED_GVA)
			return vcpu_printf(vcpu, "not present\n"), X86EMUL_PROPAGATE_FAULT;
		vcpu->mmio_needed = 1;
		vcpu->mmio_phys_addr = gpa;
		vcpu->mmio_size = bytes;
		vcpu->mmio_is_write = 0;

		return X86EMUL_UNHANDLEABLE;
	}
}

static int emulator_write_emulated(unsigned long addr,
				   unsigned long val,
				   unsigned int bytes,
				   struct x86_emulate_ctxt *ctxt)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;
	gpa_t gpa = vcpu->mmu.gva_to_gpa(vcpu, addr);

	if (gpa == UNMAPPED_GVA)
		return X86EMUL_PROPAGATE_FAULT;

	vcpu->mmio_needed = 1;
	vcpu->mmio_phys_addr = gpa;
	vcpu->mmio_size = bytes;
	vcpu->mmio_is_write = 1;
	memcpy(vcpu->mmio_data, &val, bytes);

	return X86EMUL_CONTINUE;
}

static int emulator_cmpxchg_emulated(unsigned long addr,
				     unsigned long old,
				     unsigned long new,
				     unsigned int bytes,
				     struct x86_emulate_ctxt *ctxt)
{
	static int reported;

	if (!reported) {
		reported = 1;
		printk(KERN_WARNING "kvm: emulating exchange as write\n");
	}
	return emulator_write_emulated(addr, new, bytes, ctxt);
}

static unsigned long get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	return kvm_arch_ops->get_segment_base(vcpu, seg);
}

int emulate_invlpg(struct kvm_vcpu *vcpu, gva_t address)
{
	spin_lock(&vcpu->kvm->lock);
	vcpu->mmu.inval_page(vcpu, address);
	spin_unlock(&vcpu->kvm->lock);
	kvm_arch_ops->invlpg(vcpu, address);
	return X86EMUL_CONTINUE;
}

int emulate_clts(struct kvm_vcpu *vcpu)
{
	unsigned long cr0 = vcpu->cr0;

	cr0 &= ~CR0_TS_MASK;
	kvm_arch_ops->set_cr0(vcpu, cr0);
	return X86EMUL_CONTINUE;
}

int emulator_get_dr(struct x86_emulate_ctxt* ctxt, int dr, unsigned long *dest)
{
	struct kvm_vcpu *vcpu = ctxt->vcpu;

	switch (dr) {
	case 0 ... 3:
		*dest = kvm_arch_ops->get_dr(vcpu, dr);
		return X86EMUL_CONTINUE;
	default:
		printk(KERN_DEBUG "%s: unexpected dr %u\n",
		       __FUNCTION__, dr);
		return X86EMUL_UNHANDLEABLE;
	}
}

int emulator_set_dr(struct x86_emulate_ctxt *ctxt, int dr, unsigned long value)
{
	unsigned long mask = (ctxt->mode == X86EMUL_MODE_PROT64) ? ~0ULL : ~0U;
	int exception;

	kvm_arch_ops->set_dr(ctxt->vcpu, dr, value & mask, &exception);
	if (exception) {
		/* FIXME: better handling */
		return X86EMUL_UNHANDLEABLE;
	}
	return X86EMUL_CONTINUE;
}

static void report_emulation_failure(struct x86_emulate_ctxt *ctxt)
{
	static int reported;
	u8 opcodes[4];
	unsigned long rip = ctxt->vcpu->rip;
	unsigned long rip_linear;

	rip_linear = rip + get_segment_base(ctxt->vcpu, VCPU_SREG_CS);

	if (reported)
		return;

	emulator_read_std(rip_linear, (void *)opcodes, 4, ctxt);

	printk(KERN_ERR "emulation failed but !mmio_needed?"
	       " rip %lx %02x %02x %02x %02x\n",
	       rip, opcodes[0], opcodes[1], opcodes[2], opcodes[3]);
	reported = 1;
}

struct x86_emulate_ops emulate_ops = {
	.read_std            = emulator_read_std,
	.write_std           = emulator_write_std,
	.read_emulated       = emulator_read_emulated,
	.write_emulated      = emulator_write_emulated,
	.cmpxchg_emulated    = emulator_cmpxchg_emulated,
};

int emulate_instruction(struct kvm_vcpu *vcpu,
			struct kvm_run *run,
			unsigned long cr2,
			u16 error_code)
{
	struct x86_emulate_ctxt emulate_ctxt;
	int r;
	int cs_db, cs_l;

	kvm_arch_ops->cache_regs(vcpu);

	kvm_arch_ops->get_cs_db_l_bits(vcpu, &cs_db, &cs_l);

	emulate_ctxt.vcpu = vcpu;
	emulate_ctxt.eflags = kvm_arch_ops->get_rflags(vcpu);
	emulate_ctxt.cr2 = cr2;
	emulate_ctxt.mode = (emulate_ctxt.eflags & X86_EFLAGS_VM)
		? X86EMUL_MODE_REAL : cs_l
		? X86EMUL_MODE_PROT64 :	cs_db
		? X86EMUL_MODE_PROT32 : X86EMUL_MODE_PROT16;

	if (emulate_ctxt.mode == X86EMUL_MODE_PROT64) {
		emulate_ctxt.cs_base = 0;
		emulate_ctxt.ds_base = 0;
		emulate_ctxt.es_base = 0;
		emulate_ctxt.ss_base = 0;
	} else {
		emulate_ctxt.cs_base = get_segment_base(vcpu, VCPU_SREG_CS);
		emulate_ctxt.ds_base = get_segment_base(vcpu, VCPU_SREG_DS);
		emulate_ctxt.es_base = get_segment_base(vcpu, VCPU_SREG_ES);
		emulate_ctxt.ss_base = get_segment_base(vcpu, VCPU_SREG_SS);
	}

	emulate_ctxt.gs_base = get_segment_base(vcpu, VCPU_SREG_GS);
	emulate_ctxt.fs_base = get_segment_base(vcpu, VCPU_SREG_FS);

	vcpu->mmio_is_write = 0;
	r = x86_emulate_memop(&emulate_ctxt, &emulate_ops);

	if ((r || vcpu->mmio_is_write) && run) {
		run->mmio.phys_addr = vcpu->mmio_phys_addr;
		memcpy(run->mmio.data, vcpu->mmio_data, 8);
		run->mmio.len = vcpu->mmio_size;
		run->mmio.is_write = vcpu->mmio_is_write;
	}

	if (r) {
		if (!vcpu->mmio_needed) {
			report_emulation_failure(&emulate_ctxt);
			return EMULATE_FAIL;
		}
		return EMULATE_DO_MMIO;
	}

	kvm_arch_ops->decache_regs(vcpu);
	kvm_arch_ops->set_rflags(vcpu, emulate_ctxt.eflags);

	if (vcpu->mmio_is_write)
		return EMULATE_DO_MMIO;

	return EMULATE_DONE;
}
EXPORT_SYMBOL_GPL(emulate_instruction);

static u64 mk_cr_64(u64 curr_cr, u32 new_val)
{
	return (curr_cr & ~((1ULL << 32) - 1)) | new_val;
}

void realmode_lgdt(struct kvm_vcpu *vcpu, u16 limit, unsigned long base)
{
	struct descriptor_table dt = { limit, base };

	kvm_arch_ops->set_gdt(vcpu, &dt);
}

void realmode_lidt(struct kvm_vcpu *vcpu, u16 limit, unsigned long base)
{
	struct descriptor_table dt = { limit, base };

	kvm_arch_ops->set_idt(vcpu, &dt);
}

void realmode_lmsw(struct kvm_vcpu *vcpu, unsigned long msw,
		   unsigned long *rflags)
{
	lmsw(vcpu, msw);
	*rflags = kvm_arch_ops->get_rflags(vcpu);
}

unsigned long realmode_get_cr(struct kvm_vcpu *vcpu, int cr)
{
	switch (cr) {
	case 0:
		return vcpu->cr0;
	case 2:
		return vcpu->cr2;
	case 3:
		return vcpu->cr3;
	case 4:
		return vcpu->cr4;
	default:
		vcpu_printf(vcpu, "%s: unexpected cr %u\n", __FUNCTION__, cr);
		return 0;
	}
}

void realmode_set_cr(struct kvm_vcpu *vcpu, int cr, unsigned long val,
		     unsigned long *rflags)
{
	switch (cr) {
	case 0:
		set_cr0(vcpu, mk_cr_64(vcpu->cr0, val));
		*rflags = kvm_arch_ops->get_rflags(vcpu);
		break;
	case 2:
		vcpu->cr2 = val;
		break;
	case 3:
		set_cr3(vcpu, val);
		break;
	case 4:
		set_cr4(vcpu, mk_cr_64(vcpu->cr4, val));
		break;
	default:
		vcpu_printf(vcpu, "%s: unexpected cr %u\n", __FUNCTION__, cr);
	}
}

/*
 * Reads an msr value (of 'msr_index') into 'pdata'.
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata)
{
	return kvm_arch_ops->get_msr(vcpu, msr_index, pdata);
}

#ifdef CONFIG_X86_64

void set_efer(struct kvm_vcpu *vcpu, u64 efer)
{
	if (efer & EFER_RESERVED_BITS) {
		printk(KERN_DEBUG "set_efer: 0x%llx #GP, reserved bits\n",
		       efer);
		inject_gp(vcpu);
		return;
	}

	if (is_paging(vcpu)
	    && (vcpu->shadow_efer & EFER_LME) != (efer & EFER_LME)) {
		printk(KERN_DEBUG "set_efer: #GP, change LME while paging\n");
		inject_gp(vcpu);
		return;
	}

	kvm_arch_ops->set_efer(vcpu, efer);

	efer &= ~EFER_LMA;
	efer |= vcpu->shadow_efer & EFER_LMA;

	vcpu->shadow_efer = efer;
}
EXPORT_SYMBOL_GPL(set_efer);

#endif

/*
 * Writes msr value into into the appropriate "register".
 * Returns 0 on success, non-0 otherwise.
 * Assumes vcpu_load() was already called.
 */
static int set_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data)
{
	return kvm_arch_ops->set_msr(vcpu, msr_index, data);
}

void kvm_resched(struct kvm_vcpu *vcpu)
{
	vcpu_put(vcpu);
	cond_resched();
	/* Cannot fail -  no vcpu unplug yet. */
	vcpu_load(vcpu->kvm, vcpu_slot(vcpu));
}
EXPORT_SYMBOL_GPL(kvm_resched);

void load_msrs(struct vmx_msr_entry *e, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		wrmsrl(e[i].index, e[i].data);
}
EXPORT_SYMBOL_GPL(load_msrs);

void save_msrs(struct vmx_msr_entry *e, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		rdmsrl(e[i].index, e[i].data);
}
EXPORT_SYMBOL_GPL(save_msrs);

static int kvm_dev_ioctl_run(struct kvm *kvm, struct kvm_run *kvm_run)
{
	struct kvm_vcpu *vcpu;
	int r;

	if (kvm_run->vcpu < 0 || kvm_run->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;

	vcpu = vcpu_load(kvm, kvm_run->vcpu);
	if (!vcpu)
		return -ENOENT;

	if (kvm_run->emulated) {
		kvm_arch_ops->skip_emulated_instruction(vcpu);
		kvm_run->emulated = 0;
	}

	if (kvm_run->mmio_completed) {
		memcpy(vcpu->mmio_data, kvm_run->mmio.data, 8);
		vcpu->mmio_read_completed = 1;
	}

	vcpu->mmio_needed = 0;

	r = kvm_arch_ops->run(vcpu, kvm_run);

	vcpu_put(vcpu);
	return r;
}

static int kvm_dev_ioctl_get_regs(struct kvm *kvm, struct kvm_regs *regs)
{
	struct kvm_vcpu *vcpu;

	if (regs->vcpu < 0 || regs->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;

	vcpu = vcpu_load(kvm, regs->vcpu);
	if (!vcpu)
		return -ENOENT;

	kvm_arch_ops->cache_regs(vcpu);

	regs->rax = vcpu->regs[VCPU_REGS_RAX];
	regs->rbx = vcpu->regs[VCPU_REGS_RBX];
	regs->rcx = vcpu->regs[VCPU_REGS_RCX];
	regs->rdx = vcpu->regs[VCPU_REGS_RDX];
	regs->rsi = vcpu->regs[VCPU_REGS_RSI];
	regs->rdi = vcpu->regs[VCPU_REGS_RDI];
	regs->rsp = vcpu->regs[VCPU_REGS_RSP];
	regs->rbp = vcpu->regs[VCPU_REGS_RBP];
#ifdef CONFIG_X86_64
	regs->r8 = vcpu->regs[VCPU_REGS_R8];
	regs->r9 = vcpu->regs[VCPU_REGS_R9];
	regs->r10 = vcpu->regs[VCPU_REGS_R10];
	regs->r11 = vcpu->regs[VCPU_REGS_R11];
	regs->r12 = vcpu->regs[VCPU_REGS_R12];
	regs->r13 = vcpu->regs[VCPU_REGS_R13];
	regs->r14 = vcpu->regs[VCPU_REGS_R14];
	regs->r15 = vcpu->regs[VCPU_REGS_R15];
#endif

	regs->rip = vcpu->rip;
	regs->rflags = kvm_arch_ops->get_rflags(vcpu);

	/*
	 * Don't leak debug flags in case they were set for guest debugging
	 */
	if (vcpu->guest_debug.enabled && vcpu->guest_debug.singlestep)
		regs->rflags &= ~(X86_EFLAGS_TF | X86_EFLAGS_RF);

	vcpu_put(vcpu);

	return 0;
}

static int kvm_dev_ioctl_set_regs(struct kvm *kvm, struct kvm_regs *regs)
{
	struct kvm_vcpu *vcpu;

	if (regs->vcpu < 0 || regs->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;

	vcpu = vcpu_load(kvm, regs->vcpu);
	if (!vcpu)
		return -ENOENT;

	vcpu->regs[VCPU_REGS_RAX] = regs->rax;
	vcpu->regs[VCPU_REGS_RBX] = regs->rbx;
	vcpu->regs[VCPU_REGS_RCX] = regs->rcx;
	vcpu->regs[VCPU_REGS_RDX] = regs->rdx;
	vcpu->regs[VCPU_REGS_RSI] = regs->rsi;
	vcpu->regs[VCPU_REGS_RDI] = regs->rdi;
	vcpu->regs[VCPU_REGS_RSP] = regs->rsp;
	vcpu->regs[VCPU_REGS_RBP] = regs->rbp;
#ifdef CONFIG_X86_64
	vcpu->regs[VCPU_REGS_R8] = regs->r8;
	vcpu->regs[VCPU_REGS_R9] = regs->r9;
	vcpu->regs[VCPU_REGS_R10] = regs->r10;
	vcpu->regs[VCPU_REGS_R11] = regs->r11;
	vcpu->regs[VCPU_REGS_R12] = regs->r12;
	vcpu->regs[VCPU_REGS_R13] = regs->r13;
	vcpu->regs[VCPU_REGS_R14] = regs->r14;
	vcpu->regs[VCPU_REGS_R15] = regs->r15;
#endif

	vcpu->rip = regs->rip;
	kvm_arch_ops->set_rflags(vcpu, regs->rflags);

	kvm_arch_ops->decache_regs(vcpu);

	vcpu_put(vcpu);

	return 0;
}

static void get_segment(struct kvm_vcpu *vcpu,
			struct kvm_segment *var, int seg)
{
	return kvm_arch_ops->get_segment(vcpu, var, seg);
}

static int kvm_dev_ioctl_get_sregs(struct kvm *kvm, struct kvm_sregs *sregs)
{
	struct kvm_vcpu *vcpu;
	struct descriptor_table dt;

	if (sregs->vcpu < 0 || sregs->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;
	vcpu = vcpu_load(kvm, sregs->vcpu);
	if (!vcpu)
		return -ENOENT;

	get_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	get_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	get_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	get_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	get_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	get_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	get_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	get_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	kvm_arch_ops->get_idt(vcpu, &dt);
	sregs->idt.limit = dt.limit;
	sregs->idt.base = dt.base;
	kvm_arch_ops->get_gdt(vcpu, &dt);
	sregs->gdt.limit = dt.limit;
	sregs->gdt.base = dt.base;

	sregs->cr0 = vcpu->cr0;
	sregs->cr2 = vcpu->cr2;
	sregs->cr3 = vcpu->cr3;
	sregs->cr4 = vcpu->cr4;
	sregs->cr8 = vcpu->cr8;
	sregs->efer = vcpu->shadow_efer;
	sregs->apic_base = vcpu->apic_base;

	memcpy(sregs->interrupt_bitmap, vcpu->irq_pending,
	       sizeof sregs->interrupt_bitmap);

	vcpu_put(vcpu);

	return 0;
}

static void set_segment(struct kvm_vcpu *vcpu,
			struct kvm_segment *var, int seg)
{
	return kvm_arch_ops->set_segment(vcpu, var, seg);
}

static int kvm_dev_ioctl_set_sregs(struct kvm *kvm, struct kvm_sregs *sregs)
{
	struct kvm_vcpu *vcpu;
	int mmu_reset_needed = 0;
	int i;
	struct descriptor_table dt;

	if (sregs->vcpu < 0 || sregs->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;
	vcpu = vcpu_load(kvm, sregs->vcpu);
	if (!vcpu)
		return -ENOENT;

	set_segment(vcpu, &sregs->cs, VCPU_SREG_CS);
	set_segment(vcpu, &sregs->ds, VCPU_SREG_DS);
	set_segment(vcpu, &sregs->es, VCPU_SREG_ES);
	set_segment(vcpu, &sregs->fs, VCPU_SREG_FS);
	set_segment(vcpu, &sregs->gs, VCPU_SREG_GS);
	set_segment(vcpu, &sregs->ss, VCPU_SREG_SS);

	set_segment(vcpu, &sregs->tr, VCPU_SREG_TR);
	set_segment(vcpu, &sregs->ldt, VCPU_SREG_LDTR);

	dt.limit = sregs->idt.limit;
	dt.base = sregs->idt.base;
	kvm_arch_ops->set_idt(vcpu, &dt);
	dt.limit = sregs->gdt.limit;
	dt.base = sregs->gdt.base;
	kvm_arch_ops->set_gdt(vcpu, &dt);

	vcpu->cr2 = sregs->cr2;
	mmu_reset_needed |= vcpu->cr3 != sregs->cr3;
	vcpu->cr3 = sregs->cr3;

	vcpu->cr8 = sregs->cr8;

	mmu_reset_needed |= vcpu->shadow_efer != sregs->efer;
#ifdef CONFIG_X86_64
	kvm_arch_ops->set_efer(vcpu, sregs->efer);
#endif
	vcpu->apic_base = sregs->apic_base;

	mmu_reset_needed |= vcpu->cr0 != sregs->cr0;
	kvm_arch_ops->set_cr0_no_modeswitch(vcpu, sregs->cr0);

	mmu_reset_needed |= vcpu->cr4 != sregs->cr4;
	kvm_arch_ops->set_cr4(vcpu, sregs->cr4);

	if (mmu_reset_needed)
		kvm_mmu_reset_context(vcpu);

	memcpy(vcpu->irq_pending, sregs->interrupt_bitmap,
	       sizeof vcpu->irq_pending);
	vcpu->irq_summary = 0;
	for (i = 0; i < NR_IRQ_WORDS; ++i)
		if (vcpu->irq_pending[i])
			__set_bit(i, &vcpu->irq_summary);

	vcpu_put(vcpu);

	return 0;
}

/*
 * List of msr numbers which we expose to userspace through KVM_GET_MSRS
 * and KVM_SET_MSRS, and KVM_GET_MSR_INDEX_LIST.
 */
static u32 msrs_to_save[] = {
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_K6_STAR,
#ifdef CONFIG_X86_64
	MSR_CSTAR, MSR_KERNEL_GS_BASE, MSR_SYSCALL_MASK, MSR_LSTAR,
#endif
	MSR_IA32_TIME_STAMP_COUNTER,
};


/*
 * Adapt set_msr() to msr_io()'s calling convention
 */
static int do_set_msr(struct kvm_vcpu *vcpu, unsigned index, u64 *data)
{
	return set_msr(vcpu, index, *data);
}

/*
 * Read or write a bunch of msrs. All parameters are kernel addresses.
 *
 * @return number of msrs set successfully.
 */
static int __msr_io(struct kvm *kvm, struct kvm_msrs *msrs,
		    struct kvm_msr_entry *entries,
		    int (*do_msr)(struct kvm_vcpu *vcpu,
				  unsigned index, u64 *data))
{
	struct kvm_vcpu *vcpu;
	int i;

	if (msrs->vcpu < 0 || msrs->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;

	vcpu = vcpu_load(kvm, msrs->vcpu);
	if (!vcpu)
		return -ENOENT;

	for (i = 0; i < msrs->nmsrs; ++i)
		if (do_msr(vcpu, entries[i].index, &entries[i].data))
			break;

	vcpu_put(vcpu);

	return i;
}

/*
 * Read or write a bunch of msrs. Parameters are user addresses.
 *
 * @return number of msrs set successfully.
 */
static int msr_io(struct kvm *kvm, struct kvm_msrs __user *user_msrs,
		  int (*do_msr)(struct kvm_vcpu *vcpu,
				unsigned index, u64 *data),
		  int writeback)
{
	struct kvm_msrs msrs;
	struct kvm_msr_entry *entries;
	int r, n;
	unsigned size;

	r = -EFAULT;
	if (copy_from_user(&msrs, user_msrs, sizeof msrs))
		goto out;

	r = -E2BIG;
	if (msrs.nmsrs >= MAX_IO_MSRS)
		goto out;

	r = -ENOMEM;
	size = sizeof(struct kvm_msr_entry) * msrs.nmsrs;
	entries = vmalloc(size);
	if (!entries)
		goto out;

	r = -EFAULT;
	if (copy_from_user(entries, user_msrs->entries, size))
		goto out_free;

	r = n = __msr_io(kvm, &msrs, entries, do_msr);
	if (r < 0)
		goto out_free;

	r = -EFAULT;
	if (writeback && copy_to_user(user_msrs->entries, entries, size))
		goto out_free;

	r = n;

out_free:
	vfree(entries);
out:
	return r;
}

/*
 * Translate a guest virtual address to a guest physical address.
 */
static int kvm_dev_ioctl_translate(struct kvm *kvm, struct kvm_translation *tr)
{
	unsigned long vaddr = tr->linear_address;
	struct kvm_vcpu *vcpu;
	gpa_t gpa;

	vcpu = vcpu_load(kvm, tr->vcpu);
	if (!vcpu)
		return -ENOENT;
	spin_lock(&kvm->lock);
	gpa = vcpu->mmu.gva_to_gpa(vcpu, vaddr);
	tr->physical_address = gpa;
	tr->valid = gpa != UNMAPPED_GVA;
	tr->writeable = 1;
	tr->usermode = 0;
	spin_unlock(&kvm->lock);
	vcpu_put(vcpu);

	return 0;
}

static int kvm_dev_ioctl_interrupt(struct kvm *kvm, struct kvm_interrupt *irq)
{
	struct kvm_vcpu *vcpu;

	if (irq->vcpu < 0 || irq->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;
	if (irq->irq < 0 || irq->irq >= 256)
		return -EINVAL;
	vcpu = vcpu_load(kvm, irq->vcpu);
	if (!vcpu)
		return -ENOENT;

	set_bit(irq->irq, vcpu->irq_pending);
	set_bit(irq->irq / BITS_PER_LONG, &vcpu->irq_summary);

	vcpu_put(vcpu);

	return 0;
}

static int kvm_dev_ioctl_debug_guest(struct kvm *kvm,
				     struct kvm_debug_guest *dbg)
{
	struct kvm_vcpu *vcpu;
	int r;

	if (dbg->vcpu < 0 || dbg->vcpu >= KVM_MAX_VCPUS)
		return -EINVAL;
	vcpu = vcpu_load(kvm, dbg->vcpu);
	if (!vcpu)
		return -ENOENT;

	r = kvm_arch_ops->set_guest_debug(vcpu, dbg);

	vcpu_put(vcpu);

	return r;
}

static long kvm_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	int r = -EINVAL;

	switch (ioctl) {
	case KVM_CREATE_VCPU: {
		r = kvm_dev_ioctl_create_vcpu(kvm, arg);
		if (r)
			goto out;
		break;
	}
	case KVM_RUN: {
		struct kvm_run kvm_run;

		r = -EFAULT;
		if (copy_from_user(&kvm_run, (void *)arg, sizeof kvm_run))
			goto out;
		r = kvm_dev_ioctl_run(kvm, &kvm_run);
		if (r < 0)
			goto out;
		r = -EFAULT;
		if (copy_to_user((void *)arg, &kvm_run, sizeof kvm_run))
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_REGS: {
		struct kvm_regs kvm_regs;

		r = -EFAULT;
		if (copy_from_user(&kvm_regs, (void *)arg, sizeof kvm_regs))
			goto out;
		r = kvm_dev_ioctl_get_regs(kvm, &kvm_regs);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user((void *)arg, &kvm_regs, sizeof kvm_regs))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_REGS: {
		struct kvm_regs kvm_regs;

		r = -EFAULT;
		if (copy_from_user(&kvm_regs, (void *)arg, sizeof kvm_regs))
			goto out;
		r = kvm_dev_ioctl_set_regs(kvm, &kvm_regs);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_SREGS: {
		struct kvm_sregs kvm_sregs;

		r = -EFAULT;
		if (copy_from_user(&kvm_sregs, (void *)arg, sizeof kvm_sregs))
			goto out;
		r = kvm_dev_ioctl_get_sregs(kvm, &kvm_sregs);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user((void *)arg, &kvm_sregs, sizeof kvm_sregs))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_SREGS: {
		struct kvm_sregs kvm_sregs;

		r = -EFAULT;
		if (copy_from_user(&kvm_sregs, (void *)arg, sizeof kvm_sregs))
			goto out;
		r = kvm_dev_ioctl_set_sregs(kvm, &kvm_sregs);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_TRANSLATE: {
		struct kvm_translation tr;

		r = -EFAULT;
		if (copy_from_user(&tr, (void *)arg, sizeof tr))
			goto out;
		r = kvm_dev_ioctl_translate(kvm, &tr);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user((void *)arg, &tr, sizeof tr))
			goto out;
		r = 0;
		break;
	}
	case KVM_INTERRUPT: {
		struct kvm_interrupt irq;

		r = -EFAULT;
		if (copy_from_user(&irq, (void *)arg, sizeof irq))
			goto out;
		r = kvm_dev_ioctl_interrupt(kvm, &irq);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_DEBUG_GUEST: {
		struct kvm_debug_guest dbg;

		r = -EFAULT;
		if (copy_from_user(&dbg, (void *)arg, sizeof dbg))
			goto out;
		r = kvm_dev_ioctl_debug_guest(kvm, &dbg);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_MEMORY_REGION: {
		struct kvm_memory_region kvm_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_mem, (void *)arg, sizeof kvm_mem))
			goto out;
		r = kvm_dev_ioctl_set_memory_region(kvm, &kvm_mem);
		if (r)
			goto out;
		break;
	}
	case KVM_GET_DIRTY_LOG: {
		struct kvm_dirty_log log;

		r = -EFAULT;
		if (copy_from_user(&log, (void *)arg, sizeof log))
			goto out;
		r = kvm_dev_ioctl_get_dirty_log(kvm, &log);
		if (r)
			goto out;
		break;
	}
	case KVM_GET_MSRS:
		r = msr_io(kvm, (void __user *)arg, get_msr, 1);
		break;
	case KVM_SET_MSRS:
		r = msr_io(kvm, (void __user *)arg, do_set_msr, 0);
		break;
	case KVM_GET_MSR_INDEX_LIST: {
		struct kvm_msr_list __user *user_msr_list = (void __user *)arg;
		struct kvm_msr_list msr_list;
		unsigned n;

		r = -EFAULT;
		if (copy_from_user(&msr_list, user_msr_list, sizeof msr_list))
			goto out;
		n = msr_list.nmsrs;
		msr_list.nmsrs = ARRAY_SIZE(msrs_to_save);
		if (copy_to_user(user_msr_list, &msr_list, sizeof msr_list))
			goto out;
		r = -E2BIG;
		if (n < ARRAY_SIZE(msrs_to_save))
			goto out;
		r = -EFAULT;
		if (copy_to_user(user_msr_list->indices, &msrs_to_save,
				 sizeof msrs_to_save))
			goto out;
		r = 0;
	}
	default:
		;
	}
out:
	return r;
}

static struct page *kvm_dev_nopage(struct vm_area_struct *vma,
				   unsigned long address,
				   int *type)
{
	struct kvm *kvm = vma->vm_file->private_data;
	unsigned long pgoff;
	struct kvm_memory_slot *slot;
	struct page *page;

	*type = VM_FAULT_MINOR;
	pgoff = ((address - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	slot = gfn_to_memslot(kvm, pgoff);
	if (!slot)
		return NOPAGE_SIGBUS;
	page = gfn_to_page(slot, pgoff);
	if (!page)
		return NOPAGE_SIGBUS;
	get_page(page);
	return page;
}

static struct vm_operations_struct kvm_dev_vm_ops = {
	.nopage = kvm_dev_nopage,
};

static int kvm_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &kvm_dev_vm_ops;
	return 0;
}

static struct file_operations kvm_chardev_ops = {
	.open		= kvm_dev_open,
	.release        = kvm_dev_release,
	.unlocked_ioctl = kvm_dev_ioctl,
	.compat_ioctl   = kvm_dev_ioctl,
	.mmap           = kvm_dev_mmap,
};

static struct miscdevice kvm_dev = {
	MISC_DYNAMIC_MINOR,
	"kvm",
	&kvm_chardev_ops,
};

static int kvm_reboot(struct notifier_block *notifier, unsigned long val,
                       void *v)
{
	if (val == SYS_RESTART) {
		/*
		 * Some (well, at least mine) BIOSes hang on reboot if
		 * in vmx root mode.
		 */
		printk(KERN_INFO "kvm: exiting hardware virtualization\n");
		on_each_cpu(kvm_arch_ops->hardware_disable, 0, 0, 1);
	}
	return NOTIFY_OK;
}

static struct notifier_block kvm_reboot_notifier = {
	.notifier_call = kvm_reboot,
	.priority = 0,
};

static __init void kvm_init_debug(void)
{
	struct kvm_stats_debugfs_item *p;

	debugfs_dir = debugfs_create_dir("kvm", 0);
	for (p = debugfs_entries; p->name; ++p)
		p->dentry = debugfs_create_u32(p->name, 0444, debugfs_dir,
					       p->data);
}

static void kvm_exit_debug(void)
{
	struct kvm_stats_debugfs_item *p;

	for (p = debugfs_entries; p->name; ++p)
		debugfs_remove(p->dentry);
	debugfs_remove(debugfs_dir);
}

hpa_t bad_page_address;

int kvm_init_arch(struct kvm_arch_ops *ops, struct module *module)
{
	int r;

	kvm_arch_ops = ops;

	if (!kvm_arch_ops->cpu_has_kvm_support()) {
		printk(KERN_ERR "kvm: no hardware support\n");
		return -EOPNOTSUPP;
	}
	if (kvm_arch_ops->disabled_by_bios()) {
		printk(KERN_ERR "kvm: disabled by bios\n");
		return -EOPNOTSUPP;
	}

	r = kvm_arch_ops->hardware_setup();
	if (r < 0)
	    return r;

	on_each_cpu(kvm_arch_ops->hardware_enable, 0, 0, 1);
	register_reboot_notifier(&kvm_reboot_notifier);

	kvm_chardev_ops.owner = module;

	r = misc_register(&kvm_dev);
	if (r) {
		printk (KERN_ERR "kvm: misc device register failed\n");
		goto out_free;
	}

	return r;

out_free:
	unregister_reboot_notifier(&kvm_reboot_notifier);
	on_each_cpu(kvm_arch_ops->hardware_disable, 0, 0, 1);
	kvm_arch_ops->hardware_unsetup();
	return r;
}

void kvm_exit_arch(void)
{
	misc_deregister(&kvm_dev);

	unregister_reboot_notifier(&kvm_reboot_notifier);
	on_each_cpu(kvm_arch_ops->hardware_disable, 0, 0, 1);
	kvm_arch_ops->hardware_unsetup();
}

static __init int kvm_init(void)
{
	static struct page *bad_page;
	int r = 0;

	kvm_init_debug();

	if ((bad_page = alloc_page(GFP_KERNEL)) == NULL) {
		r = -ENOMEM;
		goto out;
	}

	bad_page_address = page_to_pfn(bad_page) << PAGE_SHIFT;
	memset(__va(bad_page_address), 0, PAGE_SIZE);

	return r;

out:
	kvm_exit_debug();
	return r;
}

static __exit void kvm_exit(void)
{
	kvm_exit_debug();
	__free_page(pfn_to_page(bad_page_address >> PAGE_SHIFT));
}

module_init(kvm_init)
module_exit(kvm_exit)

EXPORT_SYMBOL_GPL(kvm_init_arch);
EXPORT_SYMBOL_GPL(kvm_exit_arch);

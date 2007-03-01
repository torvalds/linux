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
#include <linux/magic.h>
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
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mount.h>

#include "x86_emulate.h"
#include "segment_descriptor.h"

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

static DEFINE_SPINLOCK(kvm_lock);
static LIST_HEAD(vm_list);

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
	{ "irq_window", &kvm_stat.irq_window_exits },
	{ "halt_exits", &kvm_stat.halt_exits },
	{ "request_irq", &kvm_stat.request_irq_exits },
	{ "irq_exits", &kvm_stat.irq_exits },
	{ NULL, NULL }
};

static struct dentry *debugfs_dir;

struct vfsmount *kvmfs_mnt;

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

static long kvm_vcpu_ioctl(struct file *file, unsigned int ioctl,
			   unsigned long arg);

static struct inode *kvmfs_inode(struct file_operations *fops)
{
	int error = -ENOMEM;
	struct inode *inode = new_inode(kvmfs_mnt->mnt_sb);

	if (!inode)
		goto eexit_1;

	inode->i_fop = fops;

	/*
	 * Mark the inode dirty from the very beginning,
	 * that way it will never be moved to the dirty
	 * list because mark_inode_dirty() will think
	 * that it already _is_ on the dirty list.
	 */
	inode->i_state = I_DIRTY;
	inode->i_mode = S_IRUSR | S_IWUSR;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	return inode;

eexit_1:
	return ERR_PTR(error);
}

static struct file *kvmfs_file(struct inode *inode, void *private_data)
{
	struct file *file = get_empty_filp();

	if (!file)
		return ERR_PTR(-ENFILE);

	file->f_path.mnt = mntget(kvmfs_mnt);
	file->f_path.dentry = d_alloc_anon(inode);
	if (!file->f_path.dentry)
		return ERR_PTR(-ENOMEM);
	file->f_mapping = inode->i_mapping;

	file->f_pos = 0;
	file->f_flags = O_RDWR;
	file->f_op = inode->i_fop;
	file->f_mode = FMODE_READ | FMODE_WRITE;
	file->f_version = 0;
	file->private_data = private_data;
	return file;
}

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

static inline int valid_vcpu(int n)
{
	return likely(n >= 0 && n < KVM_MAX_VCPUS);
}

int kvm_read_guest(struct kvm_vcpu *vcpu, gva_t addr, unsigned long size,
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

int kvm_write_guest(struct kvm_vcpu *vcpu, gva_t addr, unsigned long size,
		    void *data)
{
	unsigned char *host_buf = data;
	unsigned long req_size = size;

	while (size) {
		hpa_t paddr;
		unsigned now;
		unsigned offset;
		hva_t guest_buf;
		gfn_t gfn;

		paddr = gva_to_hpa(vcpu, addr);

		if (is_error_hpa(paddr))
			break;

		gfn = vcpu->mmu.gva_to_gpa(vcpu, addr) >> PAGE_SHIFT;
		mark_page_dirty(vcpu->kvm, gfn);
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

/*
 * Switches to specified vcpu, until a matching vcpu_put()
 */
static void vcpu_load(struct kvm_vcpu *vcpu)
{
	mutex_lock(&vcpu->mutex);
	kvm_arch_ops->vcpu_load(vcpu);
}

/*
 * Switches to specified vcpu, until a matching vcpu_put(). Will return NULL
 * if the slot is not populated.
 */
static struct kvm_vcpu *vcpu_load_slot(struct kvm *kvm, int slot)
{
	struct kvm_vcpu *vcpu = &kvm->vcpus[slot];

	mutex_lock(&vcpu->mutex);
	if (!vcpu->vmcs) {
		mutex_unlock(&vcpu->mutex);
		return NULL;
	}
	kvm_arch_ops->vcpu_load(vcpu);
	return vcpu;
}

static void vcpu_put(struct kvm_vcpu *vcpu)
{
	kvm_arch_ops->vcpu_put(vcpu);
	mutex_unlock(&vcpu->mutex);
}

static struct kvm *kvm_create_vm(void)
{
	struct kvm *kvm = kzalloc(sizeof(struct kvm), GFP_KERNEL);
	int i;

	if (!kvm)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&kvm->lock);
	INIT_LIST_HEAD(&kvm->active_mmu_pages);
	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		struct kvm_vcpu *vcpu = &kvm->vcpus[i];

		mutex_init(&vcpu->mutex);
		vcpu->cpu = -1;
		vcpu->kvm = kvm;
		vcpu->mmu.root_hpa = INVALID_PAGE;
		INIT_LIST_HEAD(&vcpu->free_pages);
		spin_lock(&kvm_lock);
		list_add(&kvm->vm_list, &vm_list);
		spin_unlock(&kvm_lock);
	}
	return kvm;
}

static int kvm_dev_open(struct inode *inode, struct file *filp)
{
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
				if (free->phys_mem[i])
					__free_page(free->phys_mem[i]);
			vfree(free->phys_mem);
		}

	if (!dont || free->dirty_bitmap != dont->dirty_bitmap)
		vfree(free->dirty_bitmap);

	free->phys_mem = NULL;
	free->npages = 0;
	free->dirty_bitmap = NULL;
}

static void kvm_free_physmem(struct kvm *kvm)
{
	int i;

	for (i = 0; i < kvm->nmemslots; ++i)
		kvm_free_physmem_slot(&kvm->memslots[i], NULL);
}

static void kvm_free_vcpu(struct kvm_vcpu *vcpu)
{
	if (!vcpu->vmcs)
		return;

	vcpu_load(vcpu);
	kvm_mmu_destroy(vcpu);
	vcpu_put(vcpu);
	kvm_arch_ops->vcpu_free(vcpu);
}

static void kvm_free_vcpus(struct kvm *kvm)
{
	unsigned int i;

	for (i = 0; i < KVM_MAX_VCPUS; ++i)
		kvm_free_vcpu(&kvm->vcpus[i]);
}

static int kvm_dev_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static void kvm_destroy_vm(struct kvm *kvm)
{
	spin_lock(&kvm_lock);
	list_del(&kvm->vm_list);
	spin_unlock(&kvm_lock);
	kvm_free_vcpus(kvm);
	kvm_free_physmem(kvm);
	kfree(kvm);
}

static int kvm_vm_release(struct inode *inode, struct file *filp)
{
	struct kvm *kvm = filp->private_data;

	kvm_destroy_vm(kvm);
	return 0;
}

static void inject_gp(struct kvm_vcpu *vcpu)
{
	kvm_arch_ops->inject_gp(vcpu, 0);
}

/*
 * Load the pae pdptrs.  Return true is they are all valid.
 */
static int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3)
{
	gfn_t pdpt_gfn = cr3 >> PAGE_SHIFT;
	unsigned offset = ((cr3 & (PAGE_SIZE-1)) >> 5) << 2;
	int i;
	u64 pdpte;
	u64 *pdpt;
	int ret;
	struct kvm_memory_slot *memslot;

	spin_lock(&vcpu->kvm->lock);
	memslot = gfn_to_memslot(vcpu->kvm, pdpt_gfn);
	/* FIXME: !memslot - emulate? 0xff? */
	pdpt = kmap_atomic(gfn_to_page(memslot, pdpt_gfn), KM_USER0);

	ret = 1;
	for (i = 0; i < 4; ++i) {
		pdpte = pdpt[offset + i];
		if ((pdpte & 1) && (pdpte & 0xfffffff0000001e6ull)) {
			ret = 0;
			goto out;
		}
	}

	for (i = 0; i < 4; ++i)
		vcpu->pdptrs[i] = pdpt[offset + i];

out:
	kunmap_atomic(pdpt, KM_USER0);
	spin_unlock(&vcpu->kvm->lock);

	return ret;
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
		if (is_pae(vcpu) && !load_pdptrs(vcpu, vcpu->cr3)) {
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
	kvm_arch_ops->decache_cr0_cr4_guest_bits(vcpu);
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

	if (is_long_mode(vcpu)) {
		if (!(cr4 & CR4_PAE_MASK)) {
			printk(KERN_DEBUG "set_cr4: #GP, clearing PAE while "
			       "in long mode\n");
			inject_gp(vcpu);
			return;
		}
	} else if (is_paging(vcpu) && !is_pae(vcpu) && (cr4 & CR4_PAE_MASK)
		   && !load_pdptrs(vcpu, vcpu->cr3)) {
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
	if (is_long_mode(vcpu)) {
		if (cr3 & CR3_L_MODE_RESEVED_BITS) {
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
		    !load_pdptrs(vcpu, cr3)) {
			printk(KERN_DEBUG "set_cr3: #GP, pdptrs "
			       "reserved bits\n");
			inject_gp(vcpu);
			return;
		}
	}

	vcpu->cr3 = cr3;
	spin_lock(&vcpu->kvm->lock);
	/*
	 * Does the new cr3 value map to physical memory? (Note, we
	 * catch an invalid cr3 even in real-mode, because it would
	 * cause trouble later on when we turn on paging anyway.)
	 *
	 * A real CPU would silently accept an invalid cr3 and would
	 * attempt to use it - with largely undefined (and often hard
	 * to debug) behavior on the guest side.
	 */
	if (unlikely(!gfn_to_memslot(vcpu->kvm, cr3 >> PAGE_SHIFT)))
		inject_gp(vcpu);
	else
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

static void do_remove_write_access(struct kvm_vcpu *vcpu, int slot)
{
	spin_lock(&vcpu->kvm->lock);
	kvm_mmu_slot_remove_write_access(vcpu, slot);
	spin_unlock(&vcpu->kvm->lock);
}

/*
 * Allocate some memory and give it an address in the guest physical address
 * space.
 *
 * Discontiguous memory is allowed, mostly for framebuffers.
 */
static int kvm_vm_ioctl_set_memory_region(struct kvm *kvm,
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
		new.phys_mem = NULL;

	/* Free page dirty bitmap if unneeded */
	if (!(new.flags & KVM_MEM_LOG_DIRTY_PAGES))
		new.dirty_bitmap = NULL;

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
			set_page_private(new.phys_mem[i],0);
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

		vcpu = vcpu_load_slot(kvm, i);
		if (!vcpu)
			continue;
		if (new.flags & KVM_MEM_LOG_DIRTY_PAGES)
			do_remove_write_access(vcpu, mem->slot);
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
static int kvm_vm_ioctl_get_dirty_log(struct kvm *kvm,
				      struct kvm_dirty_log *log)
{
	struct kvm_memory_slot *memslot;
	int r, i;
	int n;
	int cleared;
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

	n = ALIGN(memslot->npages, BITS_PER_LONG) / 8;

	for (i = 0; !any && i < n/sizeof(long); ++i)
		any = memslot->dirty_bitmap[i];

	r = -EFAULT;
	if (copy_to_user(log->dirty_bitmap, memslot->dirty_bitmap, n))
		goto out;

	if (any) {
		cleared = 0;
		for (i = 0; i < KVM_MAX_VCPUS; ++i) {
			struct kvm_vcpu *vcpu;

			vcpu = vcpu_load_slot(kvm, i);
			if (!vcpu)
				continue;
			if (!cleared) {
				do_remove_write_access(vcpu, log->slot);
				memset(memslot->dirty_bitmap, 0, n);
				cleared = 1;
			}
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
	return NULL;
}
EXPORT_SYMBOL_GPL(gfn_to_memslot);

void mark_page_dirty(struct kvm *kvm, gfn_t gfn)
{
	int i;
	struct kvm_memory_slot *memslot = NULL;
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
			return X86EMUL_PROPAGATE_FAULT;
		vcpu->mmio_needed = 1;
		vcpu->mmio_phys_addr = gpa;
		vcpu->mmio_size = bytes;
		vcpu->mmio_is_write = 0;

		return X86EMUL_UNHANDLEABLE;
	}
}

static int emulator_write_phys(struct kvm_vcpu *vcpu, gpa_t gpa,
			       unsigned long val, int bytes)
{
	struct kvm_memory_slot *m;
	struct page *page;
	void *virt;

	if (((gpa + bytes - 1) >> PAGE_SHIFT) != (gpa >> PAGE_SHIFT))
		return 0;
	m = gfn_to_memslot(vcpu->kvm, gpa >> PAGE_SHIFT);
	if (!m)
		return 0;
	page = gfn_to_page(m, gpa >> PAGE_SHIFT);
	kvm_mmu_pre_write(vcpu, gpa, bytes);
	mark_page_dirty(vcpu->kvm, gpa >> PAGE_SHIFT);
	virt = kmap_atomic(page, KM_USER0);
	memcpy(virt + offset_in_page(gpa), &val, bytes);
	kunmap_atomic(virt, KM_USER0);
	kvm_mmu_post_write(vcpu, gpa, bytes);
	return 1;
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

	if (emulator_write_phys(vcpu, gpa, val, bytes))
		return X86EMUL_CONTINUE;

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

#ifdef CONFIG_X86_32

static int emulator_cmpxchg8b_emulated(unsigned long addr,
				       unsigned long old_lo,
				       unsigned long old_hi,
				       unsigned long new_lo,
				       unsigned long new_hi,
				       struct x86_emulate_ctxt *ctxt)
{
	static int reported;
	int r;

	if (!reported) {
		reported = 1;
		printk(KERN_WARNING "kvm: emulating exchange8b as write\n");
	}
	r = emulator_write_emulated(addr, new_lo, 4, ctxt);
	if (r != X86EMUL_CONTINUE)
		return r;
	return emulator_write_emulated(addr+4, new_hi, 4, ctxt);
}

#endif

static unsigned long get_segment_base(struct kvm_vcpu *vcpu, int seg)
{
	return kvm_arch_ops->get_segment_base(vcpu, seg);
}

int emulate_invlpg(struct kvm_vcpu *vcpu, gva_t address)
{
	return X86EMUL_CONTINUE;
}

int emulate_clts(struct kvm_vcpu *vcpu)
{
	unsigned long cr0;

	kvm_arch_ops->decache_cr0_cr4_guest_bits(vcpu);
	cr0 = vcpu->cr0 & ~CR0_TS_MASK;
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
#ifdef CONFIG_X86_32
	.cmpxchg8b_emulated  = emulator_cmpxchg8b_emulated,
#endif
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
		if (kvm_mmu_unprotect_page_virt(vcpu, cr2))
			return EMULATE_DONE;
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

int kvm_hypercall(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	unsigned long nr, a0, a1, a2, a3, a4, a5, ret;

	kvm_arch_ops->decache_regs(vcpu);
	ret = -KVM_EINVAL;
#ifdef CONFIG_X86_64
	if (is_long_mode(vcpu)) {
		nr = vcpu->regs[VCPU_REGS_RAX];
		a0 = vcpu->regs[VCPU_REGS_RDI];
		a1 = vcpu->regs[VCPU_REGS_RSI];
		a2 = vcpu->regs[VCPU_REGS_RDX];
		a3 = vcpu->regs[VCPU_REGS_RCX];
		a4 = vcpu->regs[VCPU_REGS_R8];
		a5 = vcpu->regs[VCPU_REGS_R9];
	} else
#endif
	{
		nr = vcpu->regs[VCPU_REGS_RBX] & -1u;
		a0 = vcpu->regs[VCPU_REGS_RAX] & -1u;
		a1 = vcpu->regs[VCPU_REGS_RCX] & -1u;
		a2 = vcpu->regs[VCPU_REGS_RDX] & -1u;
		a3 = vcpu->regs[VCPU_REGS_RSI] & -1u;
		a4 = vcpu->regs[VCPU_REGS_RDI] & -1u;
		a5 = vcpu->regs[VCPU_REGS_RBP] & -1u;
	}
	switch (nr) {
	default:
		;
	}
	vcpu->regs[VCPU_REGS_RAX] = ret;
	kvm_arch_ops->cache_regs(vcpu);
	return 1;
}
EXPORT_SYMBOL_GPL(kvm_hypercall);

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
	kvm_arch_ops->decache_cr0_cr4_guest_bits(vcpu);
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
 * Register the para guest with the host:
 */
static int vcpu_register_para(struct kvm_vcpu *vcpu, gpa_t para_state_gpa)
{
	struct kvm_vcpu_para_state *para_state;
	hpa_t para_state_hpa, hypercall_hpa;
	struct page *para_state_page;
	unsigned char *hypercall;
	gpa_t hypercall_gpa;

	printk(KERN_DEBUG "kvm: guest trying to enter paravirtual mode\n");
	printk(KERN_DEBUG ".... para_state_gpa: %08Lx\n", para_state_gpa);

	/*
	 * Needs to be page aligned:
	 */
	if (para_state_gpa != PAGE_ALIGN(para_state_gpa))
		goto err_gp;

	para_state_hpa = gpa_to_hpa(vcpu, para_state_gpa);
	printk(KERN_DEBUG ".... para_state_hpa: %08Lx\n", para_state_hpa);
	if (is_error_hpa(para_state_hpa))
		goto err_gp;

	mark_page_dirty(vcpu->kvm, para_state_gpa >> PAGE_SHIFT);
	para_state_page = pfn_to_page(para_state_hpa >> PAGE_SHIFT);
	para_state = kmap_atomic(para_state_page, KM_USER0);

	printk(KERN_DEBUG "....  guest version: %d\n", para_state->guest_version);
	printk(KERN_DEBUG "....           size: %d\n", para_state->size);

	para_state->host_version = KVM_PARA_API_VERSION;
	/*
	 * We cannot support guests that try to register themselves
	 * with a newer API version than the host supports:
	 */
	if (para_state->guest_version > KVM_PARA_API_VERSION) {
		para_state->ret = -KVM_EINVAL;
		goto err_kunmap_skip;
	}

	hypercall_gpa = para_state->hypercall_gpa;
	hypercall_hpa = gpa_to_hpa(vcpu, hypercall_gpa);
	printk(KERN_DEBUG ".... hypercall_hpa: %08Lx\n", hypercall_hpa);
	if (is_error_hpa(hypercall_hpa)) {
		para_state->ret = -KVM_EINVAL;
		goto err_kunmap_skip;
	}

	printk(KERN_DEBUG "kvm: para guest successfully registered.\n");
	vcpu->para_state_page = para_state_page;
	vcpu->para_state_gpa = para_state_gpa;
	vcpu->hypercall_gpa = hypercall_gpa;

	mark_page_dirty(vcpu->kvm, hypercall_gpa >> PAGE_SHIFT);
	hypercall = kmap_atomic(pfn_to_page(hypercall_hpa >> PAGE_SHIFT),
				KM_USER1) + (hypercall_hpa & ~PAGE_MASK);
	kvm_arch_ops->patch_hypercall(vcpu, hypercall);
	kunmap_atomic(hypercall, KM_USER1);

	para_state->ret = 0;
err_kunmap_skip:
	kunmap_atomic(para_state, KM_USER0);
	return 0;
err_gp:
	return 1;
}

int kvm_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata)
{
	u64 data;

	switch (msr) {
	case 0xc0010010: /* SYSCFG */
	case 0xc0010015: /* HWCR */
	case MSR_IA32_PLATFORM_ID:
	case MSR_IA32_P5_MC_ADDR:
	case MSR_IA32_P5_MC_TYPE:
	case MSR_IA32_MC0_CTL:
	case MSR_IA32_MCG_STATUS:
	case MSR_IA32_MCG_CAP:
	case MSR_IA32_MC0_MISC:
	case MSR_IA32_MC0_MISC+4:
	case MSR_IA32_MC0_MISC+8:
	case MSR_IA32_MC0_MISC+12:
	case MSR_IA32_MC0_MISC+16:
	case MSR_IA32_UCODE_REV:
	case MSR_IA32_PERF_STATUS:
		/* MTRR registers */
	case 0xfe:
	case 0x200 ... 0x2ff:
		data = 0;
		break;
	case 0xcd: /* fsb frequency */
		data = 3;
		break;
	case MSR_IA32_APICBASE:
		data = vcpu->apic_base;
		break;
	case MSR_IA32_MISC_ENABLE:
		data = vcpu->ia32_misc_enable_msr;
		break;
#ifdef CONFIG_X86_64
	case MSR_EFER:
		data = vcpu->shadow_efer;
		break;
#endif
	default:
		printk(KERN_ERR "kvm: unhandled rdmsr: 0x%x\n", msr);
		return 1;
	}
	*pdata = data;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_get_msr_common);

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

static void set_efer(struct kvm_vcpu *vcpu, u64 efer)
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

#endif

int kvm_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data)
{
	switch (msr) {
#ifdef CONFIG_X86_64
	case MSR_EFER:
		set_efer(vcpu, data);
		break;
#endif
	case MSR_IA32_MC0_STATUS:
		printk(KERN_WARNING "%s: MSR_IA32_MC0_STATUS 0x%llx, nop\n",
		       __FUNCTION__, data);
		break;
	case MSR_IA32_UCODE_REV:
	case MSR_IA32_UCODE_WRITE:
	case 0x200 ... 0x2ff: /* MTRRs */
		break;
	case MSR_IA32_APICBASE:
		vcpu->apic_base = data;
		break;
	case MSR_IA32_MISC_ENABLE:
		vcpu->ia32_misc_enable_msr = data;
		break;
	/*
	 * This is the 'probe whether the host is KVM' logic:
	 */
	case MSR_KVM_API_MAGIC:
		return vcpu_register_para(vcpu, data);

	default:
		printk(KERN_ERR "kvm: unhandled wrmsr: 0x%x\n", msr);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_set_msr_common);

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
	vcpu_load(vcpu);
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

static int kvm_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	int r;

	vcpu_load(vcpu);

	/* re-sync apic's tpr */
	vcpu->cr8 = kvm_run->cr8;

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

static int kvm_vcpu_ioctl_get_regs(struct kvm_vcpu *vcpu,
				   struct kvm_regs *regs)
{
	vcpu_load(vcpu);

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

static int kvm_vcpu_ioctl_set_regs(struct kvm_vcpu *vcpu,
				   struct kvm_regs *regs)
{
	vcpu_load(vcpu);

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

static int kvm_vcpu_ioctl_get_sregs(struct kvm_vcpu *vcpu,
				    struct kvm_sregs *sregs)
{
	struct descriptor_table dt;

	vcpu_load(vcpu);

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

	kvm_arch_ops->decache_cr0_cr4_guest_bits(vcpu);
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

static int kvm_vcpu_ioctl_set_sregs(struct kvm_vcpu *vcpu,
				    struct kvm_sregs *sregs)
{
	int mmu_reset_needed = 0;
	int i;
	struct descriptor_table dt;

	vcpu_load(vcpu);

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

	kvm_arch_ops->decache_cr0_cr4_guest_bits(vcpu);

	mmu_reset_needed |= vcpu->cr0 != sregs->cr0;
	kvm_arch_ops->set_cr0_no_modeswitch(vcpu, sregs->cr0);

	mmu_reset_needed |= vcpu->cr4 != sregs->cr4;
	kvm_arch_ops->set_cr4(vcpu, sregs->cr4);
	if (!is_long_mode(vcpu) && is_pae(vcpu))
		load_pdptrs(vcpu, vcpu->cr3);

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
 *
 * This list is modified at module load time to reflect the
 * capabilities of the host cpu.
 */
static u32 msrs_to_save[] = {
	MSR_IA32_SYSENTER_CS, MSR_IA32_SYSENTER_ESP, MSR_IA32_SYSENTER_EIP,
	MSR_K6_STAR,
#ifdef CONFIG_X86_64
	MSR_CSTAR, MSR_KERNEL_GS_BASE, MSR_SYSCALL_MASK, MSR_LSTAR,
#endif
	MSR_IA32_TIME_STAMP_COUNTER,
};

static unsigned num_msrs_to_save;

static u32 emulated_msrs[] = {
	MSR_IA32_MISC_ENABLE,
};

static __init void kvm_init_msr_list(void)
{
	u32 dummy[2];
	unsigned i, j;

	for (i = j = 0; i < ARRAY_SIZE(msrs_to_save); i++) {
		if (rdmsr_safe(msrs_to_save[i], &dummy[0], &dummy[1]) < 0)
			continue;
		if (j < i)
			msrs_to_save[j] = msrs_to_save[i];
		j++;
	}
	num_msrs_to_save = j;
}

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
static int __msr_io(struct kvm_vcpu *vcpu, struct kvm_msrs *msrs,
		    struct kvm_msr_entry *entries,
		    int (*do_msr)(struct kvm_vcpu *vcpu,
				  unsigned index, u64 *data))
{
	int i;

	vcpu_load(vcpu);

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
static int msr_io(struct kvm_vcpu *vcpu, struct kvm_msrs __user *user_msrs,
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

	r = n = __msr_io(vcpu, &msrs, entries, do_msr);
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
static int kvm_vcpu_ioctl_translate(struct kvm_vcpu *vcpu,
				    struct kvm_translation *tr)
{
	unsigned long vaddr = tr->linear_address;
	gpa_t gpa;

	vcpu_load(vcpu);
	spin_lock(&vcpu->kvm->lock);
	gpa = vcpu->mmu.gva_to_gpa(vcpu, vaddr);
	tr->physical_address = gpa;
	tr->valid = gpa != UNMAPPED_GVA;
	tr->writeable = 1;
	tr->usermode = 0;
	spin_unlock(&vcpu->kvm->lock);
	vcpu_put(vcpu);

	return 0;
}

static int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu,
				    struct kvm_interrupt *irq)
{
	if (irq->irq < 0 || irq->irq >= 256)
		return -EINVAL;
	vcpu_load(vcpu);

	set_bit(irq->irq, vcpu->irq_pending);
	set_bit(irq->irq / BITS_PER_LONG, &vcpu->irq_summary);

	vcpu_put(vcpu);

	return 0;
}

static int kvm_vcpu_ioctl_debug_guest(struct kvm_vcpu *vcpu,
				      struct kvm_debug_guest *dbg)
{
	int r;

	vcpu_load(vcpu);

	r = kvm_arch_ops->set_guest_debug(vcpu, dbg);

	vcpu_put(vcpu);

	return r;
}

static int kvm_vcpu_release(struct inode *inode, struct file *filp)
{
	struct kvm_vcpu *vcpu = filp->private_data;

	fput(vcpu->kvm->filp);
	return 0;
}

static struct file_operations kvm_vcpu_fops = {
	.release        = kvm_vcpu_release,
	.unlocked_ioctl = kvm_vcpu_ioctl,
	.compat_ioctl   = kvm_vcpu_ioctl,
};

/*
 * Allocates an inode for the vcpu.
 */
static int create_vcpu_fd(struct kvm_vcpu *vcpu)
{
	int fd, r;
	struct inode *inode;
	struct file *file;

	atomic_inc(&vcpu->kvm->filp->f_count);
	inode = kvmfs_inode(&kvm_vcpu_fops);
	if (IS_ERR(inode)) {
		r = PTR_ERR(inode);
		goto out1;
	}

	file = kvmfs_file(inode, vcpu);
	if (IS_ERR(file)) {
		r = PTR_ERR(file);
		goto out2;
	}

	r = get_unused_fd();
	if (r < 0)
		goto out3;
	fd = r;
	fd_install(fd, file);

	return fd;

out3:
	fput(file);
out2:
	iput(inode);
out1:
	fput(vcpu->kvm->filp);
	return r;
}

/*
 * Creates some virtual cpus.  Good luck creating more than one.
 */
static int kvm_vm_ioctl_create_vcpu(struct kvm *kvm, int n)
{
	int r;
	struct kvm_vcpu *vcpu;

	r = -EINVAL;
	if (!valid_vcpu(n))
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

	r = kvm_arch_ops->vcpu_create(vcpu);
	if (r < 0)
		goto out_free_vcpus;

	r = kvm_mmu_create(vcpu);
	if (r < 0)
		goto out_free_vcpus;

	kvm_arch_ops->vcpu_load(vcpu);
	r = kvm_mmu_setup(vcpu);
	if (r >= 0)
		r = kvm_arch_ops->vcpu_setup(vcpu);
	vcpu_put(vcpu);

	if (r < 0)
		goto out_free_vcpus;

	r = create_vcpu_fd(vcpu);
	if (r < 0)
		goto out_free_vcpus;

	return r;

out_free_vcpus:
	kvm_free_vcpu(vcpu);
	mutex_unlock(&vcpu->mutex);
out:
	return r;
}

static long kvm_vcpu_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r = -EINVAL;

	switch (ioctl) {
	case KVM_RUN: {
		struct kvm_run kvm_run;

		r = -EFAULT;
		if (copy_from_user(&kvm_run, argp, sizeof kvm_run))
			goto out;
		r = kvm_vcpu_ioctl_run(vcpu, &kvm_run);
		if (r < 0 &&  r != -EINTR)
			goto out;
		if (copy_to_user(argp, &kvm_run, sizeof kvm_run)) {
			r = -EFAULT;
			goto out;
		}
		break;
	}
	case KVM_GET_REGS: {
		struct kvm_regs kvm_regs;

		memset(&kvm_regs, 0, sizeof kvm_regs);
		r = kvm_vcpu_ioctl_get_regs(vcpu, &kvm_regs);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &kvm_regs, sizeof kvm_regs))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_REGS: {
		struct kvm_regs kvm_regs;

		r = -EFAULT;
		if (copy_from_user(&kvm_regs, argp, sizeof kvm_regs))
			goto out;
		r = kvm_vcpu_ioctl_set_regs(vcpu, &kvm_regs);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_SREGS: {
		struct kvm_sregs kvm_sregs;

		memset(&kvm_sregs, 0, sizeof kvm_sregs);
		r = kvm_vcpu_ioctl_get_sregs(vcpu, &kvm_sregs);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &kvm_sregs, sizeof kvm_sregs))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_SREGS: {
		struct kvm_sregs kvm_sregs;

		r = -EFAULT;
		if (copy_from_user(&kvm_sregs, argp, sizeof kvm_sregs))
			goto out;
		r = kvm_vcpu_ioctl_set_sregs(vcpu, &kvm_sregs);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_TRANSLATE: {
		struct kvm_translation tr;

		r = -EFAULT;
		if (copy_from_user(&tr, argp, sizeof tr))
			goto out;
		r = kvm_vcpu_ioctl_translate(vcpu, &tr);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &tr, sizeof tr))
			goto out;
		r = 0;
		break;
	}
	case KVM_INTERRUPT: {
		struct kvm_interrupt irq;

		r = -EFAULT;
		if (copy_from_user(&irq, argp, sizeof irq))
			goto out;
		r = kvm_vcpu_ioctl_interrupt(vcpu, &irq);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_DEBUG_GUEST: {
		struct kvm_debug_guest dbg;

		r = -EFAULT;
		if (copy_from_user(&dbg, argp, sizeof dbg))
			goto out;
		r = kvm_vcpu_ioctl_debug_guest(vcpu, &dbg);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_MSRS:
		r = msr_io(vcpu, argp, get_msr, 1);
		break;
	case KVM_SET_MSRS:
		r = msr_io(vcpu, argp, do_set_msr, 0);
		break;
	default:
		;
	}
out:
	return r;
}

static long kvm_vm_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r = -EINVAL;

	switch (ioctl) {
	case KVM_CREATE_VCPU:
		r = kvm_vm_ioctl_create_vcpu(kvm, arg);
		if (r < 0)
			goto out;
		break;
	case KVM_SET_MEMORY_REGION: {
		struct kvm_memory_region kvm_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_mem, argp, sizeof kvm_mem))
			goto out;
		r = kvm_vm_ioctl_set_memory_region(kvm, &kvm_mem);
		if (r)
			goto out;
		break;
	}
	case KVM_GET_DIRTY_LOG: {
		struct kvm_dirty_log log;

		r = -EFAULT;
		if (copy_from_user(&log, argp, sizeof log))
			goto out;
		r = kvm_vm_ioctl_get_dirty_log(kvm, &log);
		if (r)
			goto out;
		break;
	}
	default:
		;
	}
out:
	return r;
}

static struct page *kvm_vm_nopage(struct vm_area_struct *vma,
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

static struct vm_operations_struct kvm_vm_vm_ops = {
	.nopage = kvm_vm_nopage,
};

static int kvm_vm_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &kvm_vm_vm_ops;
	return 0;
}

static struct file_operations kvm_vm_fops = {
	.release        = kvm_vm_release,
	.unlocked_ioctl = kvm_vm_ioctl,
	.compat_ioctl   = kvm_vm_ioctl,
	.mmap           = kvm_vm_mmap,
};

static int kvm_dev_ioctl_create_vm(void)
{
	int fd, r;
	struct inode *inode;
	struct file *file;
	struct kvm *kvm;

	inode = kvmfs_inode(&kvm_vm_fops);
	if (IS_ERR(inode)) {
		r = PTR_ERR(inode);
		goto out1;
	}

	kvm = kvm_create_vm();
	if (IS_ERR(kvm)) {
		r = PTR_ERR(kvm);
		goto out2;
	}

	file = kvmfs_file(inode, kvm);
	if (IS_ERR(file)) {
		r = PTR_ERR(file);
		goto out3;
	}
	kvm->filp = file;

	r = get_unused_fd();
	if (r < 0)
		goto out4;
	fd = r;
	fd_install(fd, file);

	return fd;

out4:
	fput(file);
out3:
	kvm_destroy_vm(kvm);
out2:
	iput(inode);
out1:
	return r;
}

static long kvm_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int r = -EINVAL;

	switch (ioctl) {
	case KVM_GET_API_VERSION:
		r = KVM_API_VERSION;
		break;
	case KVM_CREATE_VM:
		r = kvm_dev_ioctl_create_vm();
		break;
	case KVM_GET_MSR_INDEX_LIST: {
		struct kvm_msr_list __user *user_msr_list = argp;
		struct kvm_msr_list msr_list;
		unsigned n;

		r = -EFAULT;
		if (copy_from_user(&msr_list, user_msr_list, sizeof msr_list))
			goto out;
		n = msr_list.nmsrs;
		msr_list.nmsrs = num_msrs_to_save + ARRAY_SIZE(emulated_msrs);
		if (copy_to_user(user_msr_list, &msr_list, sizeof msr_list))
			goto out;
		r = -E2BIG;
		if (n < num_msrs_to_save)
			goto out;
		r = -EFAULT;
		if (copy_to_user(user_msr_list->indices, &msrs_to_save,
				 num_msrs_to_save * sizeof(u32)))
			goto out;
		if (copy_to_user(user_msr_list->indices
				 + num_msrs_to_save * sizeof(u32),
				 &emulated_msrs,
				 ARRAY_SIZE(emulated_msrs) * sizeof(u32)))
			goto out;
		r = 0;
		break;
	}
	default:
		;
	}
out:
	return r;
}

static struct file_operations kvm_chardev_ops = {
	.open		= kvm_dev_open,
	.release        = kvm_dev_release,
	.unlocked_ioctl = kvm_dev_ioctl,
	.compat_ioctl   = kvm_dev_ioctl,
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
		on_each_cpu(kvm_arch_ops->hardware_disable, NULL, 0, 1);
	}
	return NOTIFY_OK;
}

static struct notifier_block kvm_reboot_notifier = {
	.notifier_call = kvm_reboot,
	.priority = 0,
};

/*
 * Make sure that a cpu that is being hot-unplugged does not have any vcpus
 * cached on it.
 */
static void decache_vcpus_on_cpu(int cpu)
{
	struct kvm *vm;
	struct kvm_vcpu *vcpu;
	int i;

	spin_lock(&kvm_lock);
	list_for_each_entry(vm, &vm_list, vm_list)
		for (i = 0; i < KVM_MAX_VCPUS; ++i) {
			vcpu = &vm->vcpus[i];
			/*
			 * If the vcpu is locked, then it is running on some
			 * other cpu and therefore it is not cached on the
			 * cpu in question.
			 *
			 * If it's not locked, check the last cpu it executed
			 * on.
			 */
			if (mutex_trylock(&vcpu->mutex)) {
				if (vcpu->cpu == cpu) {
					kvm_arch_ops->vcpu_decache(vcpu);
					vcpu->cpu = -1;
				}
				mutex_unlock(&vcpu->mutex);
			}
		}
	spin_unlock(&kvm_lock);
}

static int kvm_cpu_hotplug(struct notifier_block *notifier, unsigned long val,
			   void *v)
{
	int cpu = (long)v;

	switch (val) {
	case CPU_DOWN_PREPARE:
	case CPU_UP_CANCELED:
		printk(KERN_INFO "kvm: disabling virtualization on CPU%d\n",
		       cpu);
		decache_vcpus_on_cpu(cpu);
		smp_call_function_single(cpu, kvm_arch_ops->hardware_disable,
					 NULL, 0, 1);
		break;
	case CPU_ONLINE:
		printk(KERN_INFO "kvm: enabling virtualization on CPU%d\n",
		       cpu);
		smp_call_function_single(cpu, kvm_arch_ops->hardware_enable,
					 NULL, 0, 1);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block kvm_cpu_notifier = {
	.notifier_call = kvm_cpu_hotplug,
	.priority = 20, /* must be > scheduler priority */
};

static __init void kvm_init_debug(void)
{
	struct kvm_stats_debugfs_item *p;

	debugfs_dir = debugfs_create_dir("kvm", NULL);
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

static int kvm_suspend(struct sys_device *dev, pm_message_t state)
{
	decache_vcpus_on_cpu(raw_smp_processor_id());
	on_each_cpu(kvm_arch_ops->hardware_disable, NULL, 0, 1);
	return 0;
}

static int kvm_resume(struct sys_device *dev)
{
	on_each_cpu(kvm_arch_ops->hardware_enable, NULL, 0, 1);
	return 0;
}

static struct sysdev_class kvm_sysdev_class = {
	set_kset_name("kvm"),
	.suspend = kvm_suspend,
	.resume = kvm_resume,
};

static struct sys_device kvm_sysdev = {
	.id = 0,
	.cls = &kvm_sysdev_class,
};

hpa_t bad_page_address;

static int kvmfs_get_sb(struct file_system_type *fs_type, int flags,
			const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "kvm:", NULL, KVMFS_SUPER_MAGIC, mnt);
}

static struct file_system_type kvm_fs_type = {
	.name		= "kvmfs",
	.get_sb		= kvmfs_get_sb,
	.kill_sb	= kill_anon_super,
};

int kvm_init_arch(struct kvm_arch_ops *ops, struct module *module)
{
	int r;

	if (kvm_arch_ops) {
		printk(KERN_ERR "kvm: already loaded the other module\n");
		return -EEXIST;
	}

	if (!ops->cpu_has_kvm_support()) {
		printk(KERN_ERR "kvm: no hardware support\n");
		return -EOPNOTSUPP;
	}
	if (ops->disabled_by_bios()) {
		printk(KERN_ERR "kvm: disabled by bios\n");
		return -EOPNOTSUPP;
	}

	kvm_arch_ops = ops;

	r = kvm_arch_ops->hardware_setup();
	if (r < 0)
	    return r;

	on_each_cpu(kvm_arch_ops->hardware_enable, NULL, 0, 1);
	r = register_cpu_notifier(&kvm_cpu_notifier);
	if (r)
		goto out_free_1;
	register_reboot_notifier(&kvm_reboot_notifier);

	r = sysdev_class_register(&kvm_sysdev_class);
	if (r)
		goto out_free_2;

	r = sysdev_register(&kvm_sysdev);
	if (r)
		goto out_free_3;

	kvm_chardev_ops.owner = module;

	r = misc_register(&kvm_dev);
	if (r) {
		printk (KERN_ERR "kvm: misc device register failed\n");
		goto out_free;
	}

	return r;

out_free:
	sysdev_unregister(&kvm_sysdev);
out_free_3:
	sysdev_class_unregister(&kvm_sysdev_class);
out_free_2:
	unregister_reboot_notifier(&kvm_reboot_notifier);
	unregister_cpu_notifier(&kvm_cpu_notifier);
out_free_1:
	on_each_cpu(kvm_arch_ops->hardware_disable, NULL, 0, 1);
	kvm_arch_ops->hardware_unsetup();
	return r;
}

void kvm_exit_arch(void)
{
	misc_deregister(&kvm_dev);
	sysdev_unregister(&kvm_sysdev);
	sysdev_class_unregister(&kvm_sysdev_class);
	unregister_reboot_notifier(&kvm_reboot_notifier);
	unregister_cpu_notifier(&kvm_cpu_notifier);
	on_each_cpu(kvm_arch_ops->hardware_disable, NULL, 0, 1);
	kvm_arch_ops->hardware_unsetup();
	kvm_arch_ops = NULL;
}

static __init int kvm_init(void)
{
	static struct page *bad_page;
	int r;

	r = register_filesystem(&kvm_fs_type);
	if (r)
		goto out3;

	kvmfs_mnt = kern_mount(&kvm_fs_type);
	r = PTR_ERR(kvmfs_mnt);
	if (IS_ERR(kvmfs_mnt))
		goto out2;
	kvm_init_debug();

	kvm_init_msr_list();

	if ((bad_page = alloc_page(GFP_KERNEL)) == NULL) {
		r = -ENOMEM;
		goto out;
	}

	bad_page_address = page_to_pfn(bad_page) << PAGE_SHIFT;
	memset(__va(bad_page_address), 0, PAGE_SIZE);

	return 0;

out:
	kvm_exit_debug();
	mntput(kvmfs_mnt);
out2:
	unregister_filesystem(&kvm_fs_type);
out3:
	return r;
}

static __exit void kvm_exit(void)
{
	kvm_exit_debug();
	__free_page(pfn_to_page(bad_page_address >> PAGE_SHIFT));
	mntput(kvmfs_mnt);
	unregister_filesystem(&kvm_fs_type);
}

module_init(kvm_init)
module_exit(kvm_exit)

EXPORT_SYMBOL_GPL(kvm_init_arch);
EXPORT_SYMBOL_GPL(kvm_exit_arch);

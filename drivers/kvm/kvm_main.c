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
#include "x86.h"
#include "irq.h"

#include <linux/kvm.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/percpu.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>
#include <linux/reboot.h>
#include <linux/debugfs.h>
#include <linux/highmem.h>
#include <linux/file.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/smp.h>
#include <linux/anon_inodes.h>
#include <linux/profile.h>
#include <linux/kvm_para.h>
#include <linux/pagemap.h>
#include <linux/mman.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/desc.h>

MODULE_AUTHOR("Qumranet");
MODULE_LICENSE("GPL");

DEFINE_SPINLOCK(kvm_lock);
LIST_HEAD(vm_list);

static cpumask_t cpus_hardware_enabled;

struct kmem_cache *kvm_vcpu_cache;
EXPORT_SYMBOL_GPL(kvm_vcpu_cache);

static __read_mostly struct preempt_ops kvm_preempt_ops;

static struct dentry *debugfs_dir;

static long kvm_vcpu_ioctl(struct file *file, unsigned int ioctl,
			   unsigned long arg);

static inline int valid_vcpu(int n)
{
	return likely(n >= 0 && n < KVM_MAX_VCPUS);
}

/*
 * Switches to specified vcpu, until a matching vcpu_put()
 */
void vcpu_load(struct kvm_vcpu *vcpu)
{
	int cpu;

	mutex_lock(&vcpu->mutex);
	cpu = get_cpu();
	preempt_notifier_register(&vcpu->preempt_notifier);
	kvm_arch_vcpu_load(vcpu, cpu);
	put_cpu();
}

void vcpu_put(struct kvm_vcpu *vcpu)
{
	preempt_disable();
	kvm_arch_vcpu_put(vcpu);
	preempt_notifier_unregister(&vcpu->preempt_notifier);
	preempt_enable();
	mutex_unlock(&vcpu->mutex);
}

static void ack_flush(void *_completed)
{
}

void kvm_flush_remote_tlbs(struct kvm *kvm)
{
	int i, cpu;
	cpumask_t cpus;
	struct kvm_vcpu *vcpu;

	cpus_clear(cpus);
	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		vcpu = kvm->vcpus[i];
		if (!vcpu)
			continue;
		if (test_and_set_bit(KVM_REQ_TLB_FLUSH, &vcpu->requests))
			continue;
		cpu = vcpu->cpu;
		if (cpu != -1 && cpu != raw_smp_processor_id())
			cpu_set(cpu, cpus);
	}
	smp_call_function_mask(cpus, ack_flush, NULL, 1);
}

int kvm_vcpu_init(struct kvm_vcpu *vcpu, struct kvm *kvm, unsigned id)
{
	struct page *page;
	int r;

	mutex_init(&vcpu->mutex);
	vcpu->cpu = -1;
	vcpu->kvm = kvm;
	vcpu->vcpu_id = id;
	init_waitqueue_head(&vcpu->wq);

	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page) {
		r = -ENOMEM;
		goto fail;
	}
	vcpu->run = page_address(page);

	r = kvm_arch_vcpu_init(vcpu);
	if (r < 0)
		goto fail_free_run;
	return 0;

fail_free_run:
	free_page((unsigned long)vcpu->run);
fail:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_vcpu_init);

void kvm_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	kvm_arch_vcpu_uninit(vcpu);
	free_page((unsigned long)vcpu->run);
}
EXPORT_SYMBOL_GPL(kvm_vcpu_uninit);

static struct kvm *kvm_create_vm(void)
{
	struct kvm *kvm = kzalloc(sizeof(struct kvm), GFP_KERNEL);

	if (!kvm)
		return ERR_PTR(-ENOMEM);

	kvm_io_bus_init(&kvm->pio_bus);
	mutex_init(&kvm->lock);
	INIT_LIST_HEAD(&kvm->active_mmu_pages);
	kvm_io_bus_init(&kvm->mmio_bus);
	spin_lock(&kvm_lock);
	list_add(&kvm->vm_list, &vm_list);
	spin_unlock(&kvm_lock);
	return kvm;
}

/*
 * Free any memory in @free but not in @dont.
 */
static void kvm_free_physmem_slot(struct kvm_memory_slot *free,
				  struct kvm_memory_slot *dont)
{
	if (!dont || free->rmap != dont->rmap)
		vfree(free->rmap);

	if (!dont || free->dirty_bitmap != dont->dirty_bitmap)
		vfree(free->dirty_bitmap);

	free->npages = 0;
	free->dirty_bitmap = NULL;
	free->rmap = NULL;
}

static void kvm_free_physmem(struct kvm *kvm)
{
	int i;

	for (i = 0; i < kvm->nmemslots; ++i)
		kvm_free_physmem_slot(&kvm->memslots[i], NULL);
}

static void kvm_unload_vcpu_mmu(struct kvm_vcpu *vcpu)
{
	vcpu_load(vcpu);
	kvm_mmu_unload(vcpu);
	vcpu_put(vcpu);
}

static void kvm_free_vcpus(struct kvm *kvm)
{
	unsigned int i;

	/*
	 * Unpin any mmu pages first.
	 */
	for (i = 0; i < KVM_MAX_VCPUS; ++i)
		if (kvm->vcpus[i])
			kvm_unload_vcpu_mmu(kvm->vcpus[i]);
	for (i = 0; i < KVM_MAX_VCPUS; ++i) {
		if (kvm->vcpus[i]) {
			kvm_arch_vcpu_free(kvm->vcpus[i]);
			kvm->vcpus[i] = NULL;
		}
	}

}

static void kvm_destroy_vm(struct kvm *kvm)
{
	spin_lock(&kvm_lock);
	list_del(&kvm->vm_list);
	spin_unlock(&kvm_lock);
	kvm_io_bus_destroy(&kvm->pio_bus);
	kvm_io_bus_destroy(&kvm->mmio_bus);
	kfree(kvm->vpic);
	kfree(kvm->vioapic);
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

/*
 * Allocate some memory and give it an address in the guest physical address
 * space.
 *
 * Discontiguous memory is allowed, mostly for framebuffers.
 *
 * Must be called holding kvm->lock.
 */
int __kvm_set_memory_region(struct kvm *kvm,
			    struct kvm_userspace_memory_region *mem,
			    int user_alloc)
{
	int r;
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long i;
	struct kvm_memory_slot *memslot;
	struct kvm_memory_slot old, new;

	r = -EINVAL;
	/* General sanity checks */
	if (mem->memory_size & (PAGE_SIZE - 1))
		goto out;
	if (mem->guest_phys_addr & (PAGE_SIZE - 1))
		goto out;
	if (mem->slot >= KVM_MEMORY_SLOTS + KVM_PRIVATE_MEM_SLOTS)
		goto out;
	if (mem->guest_phys_addr + mem->memory_size < mem->guest_phys_addr)
		goto out;

	memslot = &kvm->memslots[mem->slot];
	base_gfn = mem->guest_phys_addr >> PAGE_SHIFT;
	npages = mem->memory_size >> PAGE_SHIFT;

	if (!npages)
		mem->flags &= ~KVM_MEM_LOG_DIRTY_PAGES;

	new = old = *memslot;

	new.base_gfn = base_gfn;
	new.npages = npages;
	new.flags = mem->flags;

	/* Disallow changing a memory slot's size. */
	r = -EINVAL;
	if (npages && old.npages && npages != old.npages)
		goto out_free;

	/* Check for overlaps */
	r = -EEXIST;
	for (i = 0; i < KVM_MEMORY_SLOTS; ++i) {
		struct kvm_memory_slot *s = &kvm->memslots[i];

		if (s == memslot)
			continue;
		if (!((base_gfn + npages <= s->base_gfn) ||
		      (base_gfn >= s->base_gfn + s->npages)))
			goto out_free;
	}

	/* Free page dirty bitmap if unneeded */
	if (!(new.flags & KVM_MEM_LOG_DIRTY_PAGES))
		new.dirty_bitmap = NULL;

	r = -ENOMEM;

	/* Allocate if a slot is being created */
	if (npages && !new.rmap) {
		new.rmap = vmalloc(npages * sizeof(struct page *));

		if (!new.rmap)
			goto out_free;

		memset(new.rmap, 0, npages * sizeof(*new.rmap));

		new.user_alloc = user_alloc;
		if (user_alloc)
			new.userspace_addr = mem->userspace_addr;
		else {
			down_write(&current->mm->mmap_sem);
			new.userspace_addr = do_mmap(NULL, 0,
						     npages * PAGE_SIZE,
						     PROT_READ | PROT_WRITE,
						     MAP_SHARED | MAP_ANONYMOUS,
						     0);
			up_write(&current->mm->mmap_sem);

			if (IS_ERR((void *)new.userspace_addr))
				goto out_free;
		}
	} else {
		if (!old.user_alloc && old.rmap) {
			int ret;

			down_write(&current->mm->mmap_sem);
			ret = do_munmap(current->mm, old.userspace_addr,
					old.npages * PAGE_SIZE);
			up_write(&current->mm->mmap_sem);
			if (ret < 0)
				printk(KERN_WARNING
				       "kvm_vm_ioctl_set_memory_region: "
				       "failed to munmap memory\n");
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

	if (mem->slot >= kvm->nmemslots)
		kvm->nmemslots = mem->slot + 1;

	if (!kvm->n_requested_mmu_pages) {
		unsigned int n_pages;

		if (npages) {
			n_pages = npages * KVM_PERMILLE_MMU_PAGES / 1000;
			kvm_mmu_change_mmu_pages(kvm, kvm->n_alloc_mmu_pages +
						 n_pages);
		} else {
			unsigned int nr_mmu_pages;

			n_pages = old.npages * KVM_PERMILLE_MMU_PAGES / 1000;
			nr_mmu_pages = kvm->n_alloc_mmu_pages - n_pages;
			nr_mmu_pages = max(nr_mmu_pages,
				        (unsigned int) KVM_MIN_ALLOC_MMU_PAGES);
			kvm_mmu_change_mmu_pages(kvm, nr_mmu_pages);
		}
	}

	*memslot = new;

	kvm_mmu_slot_remove_write_access(kvm, mem->slot);
	kvm_flush_remote_tlbs(kvm);

	kvm_free_physmem_slot(&old, &new);
	return 0;

out_free:
	kvm_free_physmem_slot(&new, &old);
out:
	return r;

}
EXPORT_SYMBOL_GPL(__kvm_set_memory_region);

int kvm_set_memory_region(struct kvm *kvm,
			  struct kvm_userspace_memory_region *mem,
			  int user_alloc)
{
	int r;

	mutex_lock(&kvm->lock);
	r = __kvm_set_memory_region(kvm, mem, user_alloc);
	mutex_unlock(&kvm->lock);
	return r;
}
EXPORT_SYMBOL_GPL(kvm_set_memory_region);

int kvm_vm_ioctl_set_memory_region(struct kvm *kvm,
				   struct
				   kvm_userspace_memory_region *mem,
				   int user_alloc)
{
	if (mem->slot >= KVM_MEMORY_SLOTS)
		return -EINVAL;
	return kvm_set_memory_region(kvm, mem, user_alloc);
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
	unsigned long any = 0;

	mutex_lock(&kvm->lock);

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

	/* If nothing is dirty, don't bother messing with page tables. */
	if (any) {
		kvm_mmu_slot_remove_write_access(kvm, log->slot);
		kvm_flush_remote_tlbs(kvm);
		memset(memslot->dirty_bitmap, 0, n);
	}

	r = 0;

out:
	mutex_unlock(&kvm->lock);
	return r;
}

int is_error_page(struct page *page)
{
	return page == bad_page;
}
EXPORT_SYMBOL_GPL(is_error_page);

static inline unsigned long bad_hva(void)
{
	return PAGE_OFFSET;
}

int kvm_is_error_hva(unsigned long addr)
{
	return addr == bad_hva();
}
EXPORT_SYMBOL_GPL(kvm_is_error_hva);

gfn_t unalias_gfn(struct kvm *kvm, gfn_t gfn)
{
	int i;
	struct kvm_mem_alias *alias;

	for (i = 0; i < kvm->naliases; ++i) {
		alias = &kvm->aliases[i];
		if (gfn >= alias->base_gfn
		    && gfn < alias->base_gfn + alias->npages)
			return alias->target_gfn + gfn - alias->base_gfn;
	}
	return gfn;
}

static struct kvm_memory_slot *__gfn_to_memslot(struct kvm *kvm, gfn_t gfn)
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

struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn)
{
	gfn = unalias_gfn(kvm, gfn);
	return __gfn_to_memslot(kvm, gfn);
}

int kvm_is_visible_gfn(struct kvm *kvm, gfn_t gfn)
{
	int i;

	gfn = unalias_gfn(kvm, gfn);
	for (i = 0; i < KVM_MEMORY_SLOTS; ++i) {
		struct kvm_memory_slot *memslot = &kvm->memslots[i];

		if (gfn >= memslot->base_gfn
		    && gfn < memslot->base_gfn + memslot->npages)
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_is_visible_gfn);

static unsigned long gfn_to_hva(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot;

	gfn = unalias_gfn(kvm, gfn);
	slot = __gfn_to_memslot(kvm, gfn);
	if (!slot)
		return bad_hva();
	return (slot->userspace_addr + (gfn - slot->base_gfn) * PAGE_SIZE);
}

/*
 * Requires current->mm->mmap_sem to be held
 */
static struct page *__gfn_to_page(struct kvm *kvm, gfn_t gfn)
{
	struct page *page[1];
	unsigned long addr;
	int npages;

	might_sleep();

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr)) {
		get_page(bad_page);
		return bad_page;
	}

	npages = get_user_pages(current, current->mm, addr, 1, 1, 1, page,
				NULL);

	if (npages != 1) {
		get_page(bad_page);
		return bad_page;
	}

	return page[0];
}

struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn)
{
	struct page *page;

	down_read(&current->mm->mmap_sem);
	page = __gfn_to_page(kvm, gfn);
	up_read(&current->mm->mmap_sem);

	return page;
}

EXPORT_SYMBOL_GPL(gfn_to_page);

void kvm_release_page(struct page *page)
{
	if (!PageReserved(page))
		SetPageDirty(page);
	put_page(page);
}
EXPORT_SYMBOL_GPL(kvm_release_page);

static int next_segment(unsigned long len, int offset)
{
	if (len > PAGE_SIZE - offset)
		return PAGE_SIZE - offset;
	else
		return len;
}

int kvm_read_guest_page(struct kvm *kvm, gfn_t gfn, void *data, int offset,
			int len)
{
	int r;
	unsigned long addr;

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	r = copy_from_user(data, (void __user *)addr + offset, len);
	if (r)
		return -EFAULT;
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_read_guest_page);

int kvm_read_guest(struct kvm *kvm, gpa_t gpa, void *data, unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_read_guest_page(kvm, gfn, data, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		data += seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_read_guest);

int kvm_write_guest_page(struct kvm *kvm, gfn_t gfn, const void *data,
			 int offset, int len)
{
	int r;
	unsigned long addr;

	addr = gfn_to_hva(kvm, gfn);
	if (kvm_is_error_hva(addr))
		return -EFAULT;
	r = copy_to_user((void __user *)addr + offset, data, len);
	if (r)
		return -EFAULT;
	mark_page_dirty(kvm, gfn);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_write_guest_page);

int kvm_write_guest(struct kvm *kvm, gpa_t gpa, const void *data,
		    unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

	while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_write_guest_page(kvm, gfn, data, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		data += seg;
		++gfn;
	}
	return 0;
}

int kvm_clear_guest_page(struct kvm *kvm, gfn_t gfn, int offset, int len)
{
	void *page_virt;
	struct page *page;

	page = gfn_to_page(kvm, gfn);
	if (is_error_page(page)) {
		kvm_release_page(page);
		return -EFAULT;
	}
	page_virt = kmap_atomic(page, KM_USER0);

	memset(page_virt + offset, 0, len);

	kunmap_atomic(page_virt, KM_USER0);
	kvm_release_page(page);
	mark_page_dirty(kvm, gfn);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_clear_guest_page);

int kvm_clear_guest(struct kvm *kvm, gpa_t gpa, unsigned long len)
{
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int seg;
	int offset = offset_in_page(gpa);
	int ret;

        while ((seg = next_segment(len, offset)) != 0) {
		ret = kvm_clear_guest_page(kvm, gfn, offset, seg);
		if (ret < 0)
			return ret;
		offset = 0;
		len -= seg;
		++gfn;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_clear_guest);

void mark_page_dirty(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *memslot;

	gfn = unalias_gfn(kvm, gfn);
	memslot = __gfn_to_memslot(kvm, gfn);
	if (memslot && memslot->dirty_bitmap) {
		unsigned long rel_gfn = gfn - memslot->base_gfn;

		/* avoid RMW */
		if (!test_bit(rel_gfn, memslot->dirty_bitmap))
			set_bit(rel_gfn, memslot->dirty_bitmap);
	}
}

/*
 * The vCPU has executed a HLT instruction with in-kernel mode enabled.
 */
void kvm_vcpu_block(struct kvm_vcpu *vcpu)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&vcpu->wq, &wait);

	/*
	 * We will block until either an interrupt or a signal wakes us up
	 */
	while (!kvm_cpu_has_interrupt(vcpu)
	       && !signal_pending(current)
	       && vcpu->mp_state != VCPU_MP_STATE_RUNNABLE
	       && vcpu->mp_state != VCPU_MP_STATE_SIPI_RECEIVED) {
		set_current_state(TASK_INTERRUPTIBLE);
		vcpu_put(vcpu);
		schedule();
		vcpu_load(vcpu);
	}

	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&vcpu->wq, &wait);
}

void kvm_resched(struct kvm_vcpu *vcpu)
{
	if (!need_resched())
		return;
	cond_resched();
}
EXPORT_SYMBOL_GPL(kvm_resched);

static int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu,
				    struct kvm_interrupt *irq)
{
	if (irq->irq < 0 || irq->irq >= 256)
		return -EINVAL;
	if (irqchip_in_kernel(vcpu->kvm))
		return -ENXIO;
	vcpu_load(vcpu);

	set_bit(irq->irq, vcpu->irq_pending);
	set_bit(irq->irq / BITS_PER_LONG, &vcpu->irq_summary);

	vcpu_put(vcpu);

	return 0;
}

static struct page *kvm_vcpu_nopage(struct vm_area_struct *vma,
				    unsigned long address,
				    int *type)
{
	struct kvm_vcpu *vcpu = vma->vm_file->private_data;
	unsigned long pgoff;
	struct page *page;

	pgoff = ((address - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (pgoff == 0)
		page = virt_to_page(vcpu->run);
	else if (pgoff == KVM_PIO_PAGE_OFFSET)
		page = virt_to_page(vcpu->pio_data);
	else
		return NOPAGE_SIGBUS;
	get_page(page);
	if (type != NULL)
		*type = VM_FAULT_MINOR;

	return page;
}

static struct vm_operations_struct kvm_vcpu_vm_ops = {
	.nopage = kvm_vcpu_nopage,
};

static int kvm_vcpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	vma->vm_ops = &kvm_vcpu_vm_ops;
	return 0;
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
	.mmap           = kvm_vcpu_mmap,
};

/*
 * Allocates an inode for the vcpu.
 */
static int create_vcpu_fd(struct kvm_vcpu *vcpu)
{
	int fd, r;
	struct inode *inode;
	struct file *file;

	r = anon_inode_getfd(&fd, &inode, &file,
			     "kvm-vcpu", &kvm_vcpu_fops, vcpu);
	if (r)
		return r;
	atomic_inc(&vcpu->kvm->filp->f_count);
	return fd;
}

/*
 * Creates some virtual cpus.  Good luck creating more than one.
 */
static int kvm_vm_ioctl_create_vcpu(struct kvm *kvm, int n)
{
	int r;
	struct kvm_vcpu *vcpu;

	if (!valid_vcpu(n))
		return -EINVAL;

	vcpu = kvm_arch_vcpu_create(kvm, n);
	if (IS_ERR(vcpu))
		return PTR_ERR(vcpu);

	preempt_notifier_init(&vcpu->preempt_notifier, &kvm_preempt_ops);

	mutex_lock(&kvm->lock);
	if (kvm->vcpus[n]) {
		r = -EEXIST;
		mutex_unlock(&kvm->lock);
		goto vcpu_destroy;
	}
	kvm->vcpus[n] = vcpu;
	mutex_unlock(&kvm->lock);

	/* Now it's all set up, let userspace reach it */
	r = create_vcpu_fd(vcpu);
	if (r < 0)
		goto unlink;
	return r;

unlink:
	mutex_lock(&kvm->lock);
	kvm->vcpus[n] = NULL;
	mutex_unlock(&kvm->lock);
vcpu_destroy:
	kvm_arch_vcpu_destory(vcpu);
	return r;
}

static int kvm_vcpu_ioctl_set_sigmask(struct kvm_vcpu *vcpu, sigset_t *sigset)
{
	if (sigset) {
		sigdelsetmask(sigset, sigmask(SIGKILL)|sigmask(SIGSTOP));
		vcpu->sigset_active = 1;
		vcpu->sigset = *sigset;
	} else
		vcpu->sigset_active = 0;
	return 0;
}

static long kvm_vcpu_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm_vcpu *vcpu = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	switch (ioctl) {
	case KVM_RUN:
		r = -EINVAL;
		if (arg)
			goto out;
		r = kvm_arch_vcpu_ioctl_run(vcpu, vcpu->run);
		break;
	case KVM_GET_REGS: {
		struct kvm_regs kvm_regs;

		memset(&kvm_regs, 0, sizeof kvm_regs);
		r = kvm_arch_vcpu_ioctl_get_regs(vcpu, &kvm_regs);
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
		r = kvm_arch_vcpu_ioctl_set_regs(vcpu, &kvm_regs);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_GET_SREGS: {
		struct kvm_sregs kvm_sregs;

		memset(&kvm_sregs, 0, sizeof kvm_sregs);
		r = kvm_arch_vcpu_ioctl_get_sregs(vcpu, &kvm_sregs);
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
		r = kvm_arch_vcpu_ioctl_set_sregs(vcpu, &kvm_sregs);
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
		r = kvm_arch_vcpu_ioctl_translate(vcpu, &tr);
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
		r = kvm_arch_vcpu_ioctl_debug_guest(vcpu, &dbg);
		if (r)
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_SIGNAL_MASK: {
		struct kvm_signal_mask __user *sigmask_arg = argp;
		struct kvm_signal_mask kvm_sigmask;
		sigset_t sigset, *p;

		p = NULL;
		if (argp) {
			r = -EFAULT;
			if (copy_from_user(&kvm_sigmask, argp,
					   sizeof kvm_sigmask))
				goto out;
			r = -EINVAL;
			if (kvm_sigmask.len != sizeof sigset)
				goto out;
			r = -EFAULT;
			if (copy_from_user(&sigset, sigmask_arg->sigset,
					   sizeof sigset))
				goto out;
			p = &sigset;
		}
		r = kvm_vcpu_ioctl_set_sigmask(vcpu, &sigset);
		break;
	}
	case KVM_GET_FPU: {
		struct kvm_fpu fpu;

		memset(&fpu, 0, sizeof fpu);
		r = kvm_arch_vcpu_ioctl_get_fpu(vcpu, &fpu);
		if (r)
			goto out;
		r = -EFAULT;
		if (copy_to_user(argp, &fpu, sizeof fpu))
			goto out;
		r = 0;
		break;
	}
	case KVM_SET_FPU: {
		struct kvm_fpu fpu;

		r = -EFAULT;
		if (copy_from_user(&fpu, argp, sizeof fpu))
			goto out;
		r = kvm_arch_vcpu_ioctl_set_fpu(vcpu, &fpu);
		if (r)
			goto out;
		r = 0;
		break;
	}
	default:
		r = kvm_arch_vcpu_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}

static long kvm_vm_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	struct kvm *kvm = filp->private_data;
	void __user *argp = (void __user *)arg;
	int r;

	switch (ioctl) {
	case KVM_CREATE_VCPU:
		r = kvm_vm_ioctl_create_vcpu(kvm, arg);
		if (r < 0)
			goto out;
		break;
	case KVM_SET_USER_MEMORY_REGION: {
		struct kvm_userspace_memory_region kvm_userspace_mem;

		r = -EFAULT;
		if (copy_from_user(&kvm_userspace_mem, argp,
						sizeof kvm_userspace_mem))
			goto out;

		r = kvm_vm_ioctl_set_memory_region(kvm, &kvm_userspace_mem, 1);
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
		r = kvm_arch_vm_ioctl(filp, ioctl, arg);
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
	struct page *page;

	pgoff = ((address - vma->vm_start) >> PAGE_SHIFT) + vma->vm_pgoff;
	if (!kvm_is_visible_gfn(kvm, pgoff))
		return NOPAGE_SIGBUS;
	/* current->mm->mmap_sem is already held so call lockless version */
	page = __gfn_to_page(kvm, pgoff);
	if (is_error_page(page)) {
		kvm_release_page(page);
		return NOPAGE_SIGBUS;
	}
	if (type != NULL)
		*type = VM_FAULT_MINOR;

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

	kvm = kvm_create_vm();
	if (IS_ERR(kvm))
		return PTR_ERR(kvm);
	r = anon_inode_getfd(&fd, &inode, &file, "kvm-vm", &kvm_vm_fops, kvm);
	if (r) {
		kvm_destroy_vm(kvm);
		return r;
	}

	kvm->filp = file;

	return fd;
}

static long kvm_dev_ioctl(struct file *filp,
			  unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long r = -EINVAL;

	switch (ioctl) {
	case KVM_GET_API_VERSION:
		r = -EINVAL;
		if (arg)
			goto out;
		r = KVM_API_VERSION;
		break;
	case KVM_CREATE_VM:
		r = -EINVAL;
		if (arg)
			goto out;
		r = kvm_dev_ioctl_create_vm();
		break;
	case KVM_CHECK_EXTENSION:
		r = kvm_dev_ioctl_check_extension((long)argp);
		break;
	case KVM_GET_VCPU_MMAP_SIZE:
		r = -EINVAL;
		if (arg)
			goto out;
		r = 2 * PAGE_SIZE;
		break;
	default:
		return kvm_arch_dev_ioctl(filp, ioctl, arg);
	}
out:
	return r;
}

static struct file_operations kvm_chardev_ops = {
	.unlocked_ioctl = kvm_dev_ioctl,
	.compat_ioctl   = kvm_dev_ioctl,
};

static struct miscdevice kvm_dev = {
	KVM_MINOR,
	"kvm",
	&kvm_chardev_ops,
};

static void hardware_enable(void *junk)
{
	int cpu = raw_smp_processor_id();

	if (cpu_isset(cpu, cpus_hardware_enabled))
		return;
	cpu_set(cpu, cpus_hardware_enabled);
	kvm_arch_hardware_enable(NULL);
}

static void hardware_disable(void *junk)
{
	int cpu = raw_smp_processor_id();

	if (!cpu_isset(cpu, cpus_hardware_enabled))
		return;
	cpu_clear(cpu, cpus_hardware_enabled);
	decache_vcpus_on_cpu(cpu);
	kvm_arch_hardware_disable(NULL);
}

static int kvm_cpu_hotplug(struct notifier_block *notifier, unsigned long val,
			   void *v)
{
	int cpu = (long)v;

	val &= ~CPU_TASKS_FROZEN;
	switch (val) {
	case CPU_DYING:
		printk(KERN_INFO "kvm: disabling virtualization on CPU%d\n",
		       cpu);
		hardware_disable(NULL);
		break;
	case CPU_UP_CANCELED:
		printk(KERN_INFO "kvm: disabling virtualization on CPU%d\n",
		       cpu);
		smp_call_function_single(cpu, hardware_disable, NULL, 0, 1);
		break;
	case CPU_ONLINE:
		printk(KERN_INFO "kvm: enabling virtualization on CPU%d\n",
		       cpu);
		smp_call_function_single(cpu, hardware_enable, NULL, 0, 1);
		break;
	}
	return NOTIFY_OK;
}

static int kvm_reboot(struct notifier_block *notifier, unsigned long val,
		      void *v)
{
	if (val == SYS_RESTART) {
		/*
		 * Some (well, at least mine) BIOSes hang on reboot if
		 * in vmx root mode.
		 */
		printk(KERN_INFO "kvm: exiting hardware virtualization\n");
		on_each_cpu(hardware_disable, NULL, 0, 1);
	}
	return NOTIFY_OK;
}

static struct notifier_block kvm_reboot_notifier = {
	.notifier_call = kvm_reboot,
	.priority = 0,
};

void kvm_io_bus_init(struct kvm_io_bus *bus)
{
	memset(bus, 0, sizeof(*bus));
}

void kvm_io_bus_destroy(struct kvm_io_bus *bus)
{
	int i;

	for (i = 0; i < bus->dev_count; i++) {
		struct kvm_io_device *pos = bus->devs[i];

		kvm_iodevice_destructor(pos);
	}
}

struct kvm_io_device *kvm_io_bus_find_dev(struct kvm_io_bus *bus, gpa_t addr)
{
	int i;

	for (i = 0; i < bus->dev_count; i++) {
		struct kvm_io_device *pos = bus->devs[i];

		if (pos->in_range(pos, addr))
			return pos;
	}

	return NULL;
}

void kvm_io_bus_register_dev(struct kvm_io_bus *bus, struct kvm_io_device *dev)
{
	BUG_ON(bus->dev_count > (NR_IOBUS_DEVS-1));

	bus->devs[bus->dev_count++] = dev;
}

static struct notifier_block kvm_cpu_notifier = {
	.notifier_call = kvm_cpu_hotplug,
	.priority = 20, /* must be > scheduler priority */
};

static u64 stat_get(void *_offset)
{
	unsigned offset = (long)_offset;
	u64 total = 0;
	struct kvm *kvm;
	struct kvm_vcpu *vcpu;
	int i;

	spin_lock(&kvm_lock);
	list_for_each_entry(kvm, &vm_list, vm_list)
		for (i = 0; i < KVM_MAX_VCPUS; ++i) {
			vcpu = kvm->vcpus[i];
			if (vcpu)
				total += *(u32 *)((void *)vcpu + offset);
		}
	spin_unlock(&kvm_lock);
	return total;
}

DEFINE_SIMPLE_ATTRIBUTE(stat_fops, stat_get, NULL, "%llu\n");

static __init void kvm_init_debug(void)
{
	struct kvm_stats_debugfs_item *p;

	debugfs_dir = debugfs_create_dir("kvm", NULL);
	for (p = debugfs_entries; p->name; ++p)
		p->dentry = debugfs_create_file(p->name, 0444, debugfs_dir,
						(void *)(long)p->offset,
						&stat_fops);
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
	hardware_disable(NULL);
	return 0;
}

static int kvm_resume(struct sys_device *dev)
{
	hardware_enable(NULL);
	return 0;
}

static struct sysdev_class kvm_sysdev_class = {
	.name = "kvm",
	.suspend = kvm_suspend,
	.resume = kvm_resume,
};

static struct sys_device kvm_sysdev = {
	.id = 0,
	.cls = &kvm_sysdev_class,
};

struct page *bad_page;

static inline
struct kvm_vcpu *preempt_notifier_to_vcpu(struct preempt_notifier *pn)
{
	return container_of(pn, struct kvm_vcpu, preempt_notifier);
}

static void kvm_sched_in(struct preempt_notifier *pn, int cpu)
{
	struct kvm_vcpu *vcpu = preempt_notifier_to_vcpu(pn);

	kvm_arch_vcpu_load(vcpu, cpu);
}

static void kvm_sched_out(struct preempt_notifier *pn,
			  struct task_struct *next)
{
	struct kvm_vcpu *vcpu = preempt_notifier_to_vcpu(pn);

	kvm_arch_vcpu_put(vcpu);
}

int kvm_init(void *opaque, unsigned int vcpu_size,
		  struct module *module)
{
	int r;
	int cpu;

	r = kvm_mmu_module_init();
	if (r)
		goto out4;

	kvm_init_debug();

	r = kvm_arch_init(opaque);
	if (r)
		goto out4;

	bad_page = alloc_page(GFP_KERNEL | __GFP_ZERO);

	if (bad_page == NULL) {
		r = -ENOMEM;
		goto out;
	}

	r = kvm_arch_hardware_setup();
	if (r < 0)
		goto out;

	for_each_online_cpu(cpu) {
		smp_call_function_single(cpu,
				kvm_arch_check_processor_compat,
				&r, 0, 1);
		if (r < 0)
			goto out_free_0;
	}

	on_each_cpu(hardware_enable, NULL, 0, 1);
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

	/* A kmem cache lets us meet the alignment requirements of fx_save. */
	kvm_vcpu_cache = kmem_cache_create("kvm_vcpu", vcpu_size,
					   __alignof__(struct kvm_vcpu),
					   0, NULL);
	if (!kvm_vcpu_cache) {
		r = -ENOMEM;
		goto out_free_4;
	}

	kvm_chardev_ops.owner = module;

	r = misc_register(&kvm_dev);
	if (r) {
		printk(KERN_ERR "kvm: misc device register failed\n");
		goto out_free;
	}

	kvm_preempt_ops.sched_in = kvm_sched_in;
	kvm_preempt_ops.sched_out = kvm_sched_out;

	kvm_mmu_set_nonpresent_ptes(0ull, 0ull);

	return 0;

out_free:
	kmem_cache_destroy(kvm_vcpu_cache);
out_free_4:
	sysdev_unregister(&kvm_sysdev);
out_free_3:
	sysdev_class_unregister(&kvm_sysdev_class);
out_free_2:
	unregister_reboot_notifier(&kvm_reboot_notifier);
	unregister_cpu_notifier(&kvm_cpu_notifier);
out_free_1:
	on_each_cpu(hardware_disable, NULL, 0, 1);
out_free_0:
	kvm_arch_hardware_unsetup();
out:
	kvm_arch_exit();
	kvm_exit_debug();
	kvm_mmu_module_exit();
out4:
	return r;
}
EXPORT_SYMBOL_GPL(kvm_init);

void kvm_exit(void)
{
	misc_deregister(&kvm_dev);
	kmem_cache_destroy(kvm_vcpu_cache);
	sysdev_unregister(&kvm_sysdev);
	sysdev_class_unregister(&kvm_sysdev_class);
	unregister_reboot_notifier(&kvm_reboot_notifier);
	unregister_cpu_notifier(&kvm_cpu_notifier);
	on_each_cpu(hardware_disable, NULL, 0, 1);
	kvm_arch_hardware_unsetup();
	kvm_arch_exit();
	kvm_exit_debug();
	__free_page(bad_page);
	kvm_mmu_module_exit();
}
EXPORT_SYMBOL_GPL(kvm_exit);

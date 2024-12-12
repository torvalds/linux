// SPDX-License-Identifier: GPL-2.0 or MIT
/* Copyright 2019 Linaro, Ltd, Rob Herring <robh@kernel.org> */
/* Copyright 2023 Collabora ltd. */

#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_exec.h>
#include <drm/drm_gpuvm.h>
#include <drm/drm_managed.h>
#include <drm/gpu_scheduler.h>
#include <drm/panthor_drm.h>

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/kmemleak.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/shmem_fs.h>
#include <linux/sizes.h>

#include "panthor_device.h"
#include "panthor_gem.h"
#include "panthor_heap.h"
#include "panthor_mmu.h"
#include "panthor_regs.h"
#include "panthor_sched.h"

#define MAX_AS_SLOTS			32

struct panthor_vm;

/**
 * struct panthor_as_slot - Address space slot
 */
struct panthor_as_slot {
	/** @vm: VM bound to this slot. NULL is no VM is bound. */
	struct panthor_vm *vm;
};

/**
 * struct panthor_mmu - MMU related data
 */
struct panthor_mmu {
	/** @irq: The MMU irq. */
	struct panthor_irq irq;

	/** @as: Address space related fields.
	 *
	 * The GPU has a limited number of address spaces (AS) slots, forcing
	 * us to re-assign them to re-assign slots on-demand.
	 */
	struct {
		/** @slots_lock: Lock protecting access to all other AS fields. */
		struct mutex slots_lock;

		/** @alloc_mask: Bitmask encoding the allocated slots. */
		unsigned long alloc_mask;

		/** @faulty_mask: Bitmask encoding the faulty slots. */
		unsigned long faulty_mask;

		/** @slots: VMs currently bound to the AS slots. */
		struct panthor_as_slot slots[MAX_AS_SLOTS];

		/**
		 * @lru_list: List of least recently used VMs.
		 *
		 * We use this list to pick a VM to evict when all slots are
		 * used.
		 *
		 * There should be no more active VMs than there are AS slots,
		 * so this LRU is just here to keep VMs bound until there's
		 * a need to release a slot, thus avoid unnecessary TLB/cache
		 * flushes.
		 */
		struct list_head lru_list;
	} as;

	/** @vm: VMs management fields */
	struct {
		/** @lock: Lock protecting access to list. */
		struct mutex lock;

		/** @list: List containing all VMs. */
		struct list_head list;

		/** @reset_in_progress: True if a reset is in progress. */
		bool reset_in_progress;

		/** @wq: Workqueue used for the VM_BIND queues. */
		struct workqueue_struct *wq;
	} vm;
};

/**
 * struct panthor_vm_pool - VM pool object
 */
struct panthor_vm_pool {
	/** @xa: Array used for VM handle tracking. */
	struct xarray xa;
};

/**
 * struct panthor_vma - GPU mapping object
 *
 * This is used to track GEM mappings in GPU space.
 */
struct panthor_vma {
	/** @base: Inherits from drm_gpuva. */
	struct drm_gpuva base;

	/** @node: Used to implement deferred release of VMAs. */
	struct list_head node;

	/**
	 * @flags: Combination of drm_panthor_vm_bind_op_flags.
	 *
	 * Only map related flags are accepted.
	 */
	u32 flags;
};

/**
 * struct panthor_vm_op_ctx - VM operation context
 *
 * With VM operations potentially taking place in a dma-signaling path, we
 * need to make sure everything that might require resource allocation is
 * pre-allocated upfront. This is what this operation context is far.
 *
 * We also collect resources that have been freed, so we can release them
 * asynchronously, and let the VM_BIND scheduler process the next VM_BIND
 * request.
 */
struct panthor_vm_op_ctx {
	/** @rsvd_page_tables: Pages reserved for the MMU page table update. */
	struct {
		/** @count: Number of pages reserved. */
		u32 count;

		/** @ptr: Point to the first unused page in the @pages table. */
		u32 ptr;

		/**
		 * @page: Array of pages that can be used for an MMU page table update.
		 *
		 * After an VM operation, there might be free pages left in this array.
		 * They should be returned to the pt_cache as part of the op_ctx cleanup.
		 */
		void **pages;
	} rsvd_page_tables;

	/**
	 * @preallocated_vmas: Pre-allocated VMAs to handle the remap case.
	 *
	 * Partial unmap requests or map requests overlapping existing mappings will
	 * trigger a remap call, which need to register up to three panthor_vma objects
	 * (one for the new mapping, and two for the previous and next mappings).
	 */
	struct panthor_vma *preallocated_vmas[3];

	/** @flags: Combination of drm_panthor_vm_bind_op_flags. */
	u32 flags;

	/** @va: Virtual range targeted by the VM operation. */
	struct {
		/** @addr: Start address. */
		u64 addr;

		/** @range: Range size. */
		u64 range;
	} va;

	/**
	 * @returned_vmas: List of panthor_vma objects returned after a VM operation.
	 *
	 * For unmap operations, this will contain all VMAs that were covered by the
	 * specified VA range.
	 *
	 * For map operations, this will contain all VMAs that previously mapped to
	 * the specified VA range.
	 *
	 * Those VMAs, and the resources they point to will be released as part of
	 * the op_ctx cleanup operation.
	 */
	struct list_head returned_vmas;

	/** @map: Fields specific to a map operation. */
	struct {
		/** @vm_bo: Buffer object to map. */
		struct drm_gpuvm_bo *vm_bo;

		/** @bo_offset: Offset in the buffer object. */
		u64 bo_offset;

		/**
		 * @sgt: sg-table pointing to pages backing the GEM object.
		 *
		 * This is gathered at job creation time, such that we don't have
		 * to allocate in ::run_job().
		 */
		struct sg_table *sgt;

		/**
		 * @new_vma: The new VMA object that will be inserted to the VA tree.
		 */
		struct panthor_vma *new_vma;
	} map;
};

/**
 * struct panthor_vm - VM object
 *
 * A VM is an object representing a GPU (or MCU) virtual address space.
 * It embeds the MMU page table for this address space, a tree containing
 * all the virtual mappings of GEM objects, and other things needed to manage
 * the VM.
 *
 * Except for the MCU VM, which is managed by the kernel, all other VMs are
 * created by userspace and mostly managed by userspace, using the
 * %DRM_IOCTL_PANTHOR_VM_BIND ioctl.
 *
 * A portion of the virtual address space is reserved for kernel objects,
 * like heap chunks, and userspace gets to decide how much of the virtual
 * address space is left to the kernel (half of the virtual address space
 * by default).
 */
struct panthor_vm {
	/**
	 * @base: Inherit from drm_gpuvm.
	 *
	 * We delegate all the VA management to the common drm_gpuvm framework
	 * and only implement hooks to update the MMU page table.
	 */
	struct drm_gpuvm base;

	/**
	 * @sched: Scheduler used for asynchronous VM_BIND request.
	 *
	 * We use a 1:1 scheduler here.
	 */
	struct drm_gpu_scheduler sched;

	/**
	 * @entity: Scheduling entity representing the VM_BIND queue.
	 *
	 * There's currently one bind queue per VM. It doesn't make sense to
	 * allow more given the VM operations are serialized anyway.
	 */
	struct drm_sched_entity entity;

	/** @ptdev: Device. */
	struct panthor_device *ptdev;

	/** @memattr: Value to program to the AS_MEMATTR register. */
	u64 memattr;

	/** @pgtbl_ops: Page table operations. */
	struct io_pgtable_ops *pgtbl_ops;

	/** @root_page_table: Stores the root page table pointer. */
	void *root_page_table;

	/**
	 * @op_lock: Lock used to serialize operations on a VM.
	 *
	 * The serialization of jobs queued to the VM_BIND queue is already
	 * taken care of by drm_sched, but we need to serialize synchronous
	 * and asynchronous VM_BIND request. This is what this lock is for.
	 */
	struct mutex op_lock;

	/**
	 * @op_ctx: The context attached to the currently executing VM operation.
	 *
	 * NULL when no operation is in progress.
	 */
	struct panthor_vm_op_ctx *op_ctx;

	/**
	 * @mm: Memory management object representing the auto-VA/kernel-VA.
	 *
	 * Used to auto-allocate VA space for kernel-managed objects (tiler
	 * heaps, ...).
	 *
	 * For the MCU VM, this is managing the VA range that's used to map
	 * all shared interfaces.
	 *
	 * For user VMs, the range is specified by userspace, and must not
	 * exceed half of the VA space addressable.
	 */
	struct drm_mm mm;

	/** @mm_lock: Lock protecting the @mm field. */
	struct mutex mm_lock;

	/** @kernel_auto_va: Automatic VA-range for kernel BOs. */
	struct {
		/** @start: Start of the automatic VA-range for kernel BOs. */
		u64 start;

		/** @size: Size of the automatic VA-range for kernel BOs. */
		u64 end;
	} kernel_auto_va;

	/** @as: Address space related fields. */
	struct {
		/**
		 * @id: ID of the address space this VM is bound to.
		 *
		 * A value of -1 means the VM is inactive/not bound.
		 */
		int id;

		/** @active_cnt: Number of active users of this VM. */
		refcount_t active_cnt;

		/**
		 * @lru_node: Used to instead the VM in the panthor_mmu::as::lru_list.
		 *
		 * Active VMs should not be inserted in the LRU list.
		 */
		struct list_head lru_node;
	} as;

	/**
	 * @heaps: Tiler heap related fields.
	 */
	struct {
		/**
		 * @pool: The heap pool attached to this VM.
		 *
		 * Will stay NULL until someone creates a heap context on this VM.
		 */
		struct panthor_heap_pool *pool;

		/** @lock: Lock used to protect access to @pool. */
		struct mutex lock;
	} heaps;

	/** @node: Used to insert the VM in the panthor_mmu::vm::list. */
	struct list_head node;

	/** @for_mcu: True if this is the MCU VM. */
	bool for_mcu;

	/**
	 * @destroyed: True if the VM was destroyed.
	 *
	 * No further bind requests should be queued to a destroyed VM.
	 */
	bool destroyed;

	/**
	 * @unusable: True if the VM has turned unusable because something
	 * bad happened during an asynchronous request.
	 *
	 * We don't try to recover from such failures, because this implies
	 * informing userspace about the specific operation that failed, and
	 * hoping the userspace driver can replay things from there. This all
	 * sounds very complicated for little gain.
	 *
	 * Instead, we should just flag the VM as unusable, and fail any
	 * further request targeting this VM.
	 *
	 * We also provide a way to query a VM state, so userspace can destroy
	 * it and create a new one.
	 *
	 * As an analogy, this would be mapped to a VK_ERROR_DEVICE_LOST
	 * situation, where the logical device needs to be re-created.
	 */
	bool unusable;

	/**
	 * @unhandled_fault: Unhandled fault happened.
	 *
	 * This should be reported to the scheduler, and the queue/group be
	 * flagged as faulty as a result.
	 */
	bool unhandled_fault;
};

/**
 * struct panthor_vm_bind_job - VM bind job
 */
struct panthor_vm_bind_job {
	/** @base: Inherit from drm_sched_job. */
	struct drm_sched_job base;

	/** @refcount: Reference count. */
	struct kref refcount;

	/** @cleanup_op_ctx_work: Work used to cleanup the VM operation context. */
	struct work_struct cleanup_op_ctx_work;

	/** @vm: VM targeted by the VM operation. */
	struct panthor_vm *vm;

	/** @ctx: Operation context. */
	struct panthor_vm_op_ctx ctx;
};

/**
 * @pt_cache: Cache used to allocate MMU page tables.
 *
 * The pre-allocation pattern forces us to over-allocate to plan for
 * the worst case scenario, and return the pages we didn't use.
 *
 * Having a kmem_cache allows us to speed allocations.
 */
static struct kmem_cache *pt_cache;

/**
 * alloc_pt() - Custom page table allocator
 * @cookie: Cookie passed at page table allocation time.
 * @size: Size of the page table. This size should be fixed,
 * and determined at creation time based on the granule size.
 * @gfp: GFP flags.
 *
 * We want a custom allocator so we can use a cache for page table
 * allocations and amortize the cost of the over-reservation that's
 * done to allow asynchronous VM operations.
 *
 * Return: non-NULL on success, NULL if the allocation failed for any
 * reason.
 */
static void *alloc_pt(void *cookie, size_t size, gfp_t gfp)
{
	struct panthor_vm *vm = cookie;
	void *page;

	/* Allocation of the root page table happening during init. */
	if (unlikely(!vm->root_page_table)) {
		struct page *p;

		drm_WARN_ON(&vm->ptdev->base, vm->op_ctx);
		p = alloc_pages_node(dev_to_node(vm->ptdev->base.dev),
				     gfp | __GFP_ZERO, get_order(size));
		page = p ? page_address(p) : NULL;
		vm->root_page_table = page;
		return page;
	}

	/* We're not supposed to have anything bigger than 4k here, because we picked a
	 * 4k granule size at init time.
	 */
	if (drm_WARN_ON(&vm->ptdev->base, size != SZ_4K))
		return NULL;

	/* We must have some op_ctx attached to the VM and it must have at least one
	 * free page.
	 */
	if (drm_WARN_ON(&vm->ptdev->base, !vm->op_ctx) ||
	    drm_WARN_ON(&vm->ptdev->base,
			vm->op_ctx->rsvd_page_tables.ptr >= vm->op_ctx->rsvd_page_tables.count))
		return NULL;

	page = vm->op_ctx->rsvd_page_tables.pages[vm->op_ctx->rsvd_page_tables.ptr++];
	memset(page, 0, SZ_4K);

	/* Page table entries don't use virtual addresses, which trips out
	 * kmemleak. kmemleak_alloc_phys() might work, but physical addresses
	 * are mixed with other fields, and I fear kmemleak won't detect that
	 * either.
	 *
	 * Let's just ignore memory passed to the page-table driver for now.
	 */
	kmemleak_ignore(page);
	return page;
}

/**
 * @free_pt() - Custom page table free function
 * @cookie: Cookie passed at page table allocation time.
 * @data: Page table to free.
 * @size: Size of the page table. This size should be fixed,
 * and determined at creation time based on the granule size.
 */
static void free_pt(void *cookie, void *data, size_t size)
{
	struct panthor_vm *vm = cookie;

	if (unlikely(vm->root_page_table == data)) {
		free_pages((unsigned long)data, get_order(size));
		vm->root_page_table = NULL;
		return;
	}

	if (drm_WARN_ON(&vm->ptdev->base, size != SZ_4K))
		return;

	/* Return the page to the pt_cache. */
	kmem_cache_free(pt_cache, data);
}

static int wait_ready(struct panthor_device *ptdev, u32 as_nr)
{
	int ret;
	u32 val;

	/* Wait for the MMU status to indicate there is no active command, in
	 * case one is pending.
	 */
	ret = readl_relaxed_poll_timeout_atomic(ptdev->iomem + AS_STATUS(as_nr),
						val, !(val & AS_STATUS_AS_ACTIVE),
						10, 100000);

	if (ret) {
		panthor_device_schedule_reset(ptdev);
		drm_err(&ptdev->base, "AS_ACTIVE bit stuck\n");
	}

	return ret;
}

static int write_cmd(struct panthor_device *ptdev, u32 as_nr, u32 cmd)
{
	int status;

	/* write AS_COMMAND when MMU is ready to accept another command */
	status = wait_ready(ptdev, as_nr);
	if (!status)
		gpu_write(ptdev, AS_COMMAND(as_nr), cmd);

	return status;
}

static void lock_region(struct panthor_device *ptdev, u32 as_nr,
			u64 region_start, u64 size)
{
	u8 region_width;
	u64 region;
	u64 region_end = region_start + size;

	if (!size)
		return;

	/*
	 * The locked region is a naturally aligned power of 2 block encoded as
	 * log2 minus(1).
	 * Calculate the desired start/end and look for the highest bit which
	 * differs. The smallest naturally aligned block must include this bit
	 * change, the desired region starts with this bit (and subsequent bits)
	 * zeroed and ends with the bit (and subsequent bits) set to one.
	 */
	region_width = max(fls64(region_start ^ (region_end - 1)),
			   const_ilog2(AS_LOCK_REGION_MIN_SIZE)) - 1;

	/*
	 * Mask off the low bits of region_start (which would be ignored by
	 * the hardware anyway)
	 */
	region_start &= GENMASK_ULL(63, region_width);

	region = region_width | region_start;

	/* Lock the region that needs to be updated */
	gpu_write(ptdev, AS_LOCKADDR_LO(as_nr), lower_32_bits(region));
	gpu_write(ptdev, AS_LOCKADDR_HI(as_nr), upper_32_bits(region));
	write_cmd(ptdev, as_nr, AS_COMMAND_LOCK);
}

static int mmu_hw_do_operation_locked(struct panthor_device *ptdev, int as_nr,
				      u64 iova, u64 size, u32 op)
{
	lockdep_assert_held(&ptdev->mmu->as.slots_lock);

	if (as_nr < 0)
		return 0;

	/*
	 * If the AS number is greater than zero, then we can be sure
	 * the device is up and running, so we don't need to explicitly
	 * power it up
	 */

	if (op != AS_COMMAND_UNLOCK)
		lock_region(ptdev, as_nr, iova, size);

	/* Run the MMU operation */
	write_cmd(ptdev, as_nr, op);

	/* Wait for the flush to complete */
	return wait_ready(ptdev, as_nr);
}

static int mmu_hw_do_operation(struct panthor_vm *vm,
			       u64 iova, u64 size, u32 op)
{
	struct panthor_device *ptdev = vm->ptdev;
	int ret;

	mutex_lock(&ptdev->mmu->as.slots_lock);
	ret = mmu_hw_do_operation_locked(ptdev, vm->as.id, iova, size, op);
	mutex_unlock(&ptdev->mmu->as.slots_lock);

	return ret;
}

static int panthor_mmu_as_enable(struct panthor_device *ptdev, u32 as_nr,
				 u64 transtab, u64 transcfg, u64 memattr)
{
	int ret;

	ret = mmu_hw_do_operation_locked(ptdev, as_nr, 0, ~0ULL, AS_COMMAND_FLUSH_MEM);
	if (ret)
		return ret;

	gpu_write(ptdev, AS_TRANSTAB_LO(as_nr), lower_32_bits(transtab));
	gpu_write(ptdev, AS_TRANSTAB_HI(as_nr), upper_32_bits(transtab));

	gpu_write(ptdev, AS_MEMATTR_LO(as_nr), lower_32_bits(memattr));
	gpu_write(ptdev, AS_MEMATTR_HI(as_nr), upper_32_bits(memattr));

	gpu_write(ptdev, AS_TRANSCFG_LO(as_nr), lower_32_bits(transcfg));
	gpu_write(ptdev, AS_TRANSCFG_HI(as_nr), upper_32_bits(transcfg));

	return write_cmd(ptdev, as_nr, AS_COMMAND_UPDATE);
}

static int panthor_mmu_as_disable(struct panthor_device *ptdev, u32 as_nr)
{
	int ret;

	ret = mmu_hw_do_operation_locked(ptdev, as_nr, 0, ~0ULL, AS_COMMAND_FLUSH_MEM);
	if (ret)
		return ret;

	gpu_write(ptdev, AS_TRANSTAB_LO(as_nr), 0);
	gpu_write(ptdev, AS_TRANSTAB_HI(as_nr), 0);

	gpu_write(ptdev, AS_MEMATTR_LO(as_nr), 0);
	gpu_write(ptdev, AS_MEMATTR_HI(as_nr), 0);

	gpu_write(ptdev, AS_TRANSCFG_LO(as_nr), AS_TRANSCFG_ADRMODE_UNMAPPED);
	gpu_write(ptdev, AS_TRANSCFG_HI(as_nr), 0);

	return write_cmd(ptdev, as_nr, AS_COMMAND_UPDATE);
}

static u32 panthor_mmu_fault_mask(struct panthor_device *ptdev, u32 value)
{
	/* Bits 16 to 31 mean REQ_COMPLETE. */
	return value & GENMASK(15, 0);
}

static u32 panthor_mmu_as_fault_mask(struct panthor_device *ptdev, u32 as)
{
	return BIT(as);
}

/**
 * panthor_vm_has_unhandled_faults() - Check if a VM has unhandled faults
 * @vm: VM to check.
 *
 * Return: true if the VM has unhandled faults, false otherwise.
 */
bool panthor_vm_has_unhandled_faults(struct panthor_vm *vm)
{
	return vm->unhandled_fault;
}

/**
 * panthor_vm_is_unusable() - Check if the VM is still usable
 * @vm: VM to check.
 *
 * Return: true if the VM is unusable, false otherwise.
 */
bool panthor_vm_is_unusable(struct panthor_vm *vm)
{
	return vm->unusable;
}

static void panthor_vm_release_as_locked(struct panthor_vm *vm)
{
	struct panthor_device *ptdev = vm->ptdev;

	lockdep_assert_held(&ptdev->mmu->as.slots_lock);

	if (drm_WARN_ON(&ptdev->base, vm->as.id < 0))
		return;

	ptdev->mmu->as.slots[vm->as.id].vm = NULL;
	clear_bit(vm->as.id, &ptdev->mmu->as.alloc_mask);
	refcount_set(&vm->as.active_cnt, 0);
	list_del_init(&vm->as.lru_node);
	vm->as.id = -1;
}

/**
 * panthor_vm_active() - Flag a VM as active
 * @VM: VM to flag as active.
 *
 * Assigns an address space to a VM so it can be used by the GPU/MCU.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_vm_active(struct panthor_vm *vm)
{
	struct panthor_device *ptdev = vm->ptdev;
	u32 va_bits = GPU_MMU_FEATURES_VA_BITS(ptdev->gpu_info.mmu_features);
	struct io_pgtable_cfg *cfg = &io_pgtable_ops_to_pgtable(vm->pgtbl_ops)->cfg;
	int ret = 0, as, cookie;
	u64 transtab, transcfg;

	if (!drm_dev_enter(&ptdev->base, &cookie))
		return -ENODEV;

	if (refcount_inc_not_zero(&vm->as.active_cnt))
		goto out_dev_exit;

	mutex_lock(&ptdev->mmu->as.slots_lock);

	if (refcount_inc_not_zero(&vm->as.active_cnt))
		goto out_unlock;

	as = vm->as.id;
	if (as >= 0) {
		/* Unhandled pagefault on this AS, the MMU was disabled. We need to
		 * re-enable the MMU after clearing+unmasking the AS interrupts.
		 */
		if (ptdev->mmu->as.faulty_mask & panthor_mmu_as_fault_mask(ptdev, as))
			goto out_enable_as;

		goto out_make_active;
	}

	/* Check for a free AS */
	if (vm->for_mcu) {
		drm_WARN_ON(&ptdev->base, ptdev->mmu->as.alloc_mask & BIT(0));
		as = 0;
	} else {
		as = ffz(ptdev->mmu->as.alloc_mask | BIT(0));
	}

	if (!(BIT(as) & ptdev->gpu_info.as_present)) {
		struct panthor_vm *lru_vm;

		lru_vm = list_first_entry_or_null(&ptdev->mmu->as.lru_list,
						  struct panthor_vm,
						  as.lru_node);
		if (drm_WARN_ON(&ptdev->base, !lru_vm)) {
			ret = -EBUSY;
			goto out_unlock;
		}

		drm_WARN_ON(&ptdev->base, refcount_read(&lru_vm->as.active_cnt));
		as = lru_vm->as.id;
		panthor_vm_release_as_locked(lru_vm);
	}

	/* Assign the free or reclaimed AS to the FD */
	vm->as.id = as;
	set_bit(as, &ptdev->mmu->as.alloc_mask);
	ptdev->mmu->as.slots[as].vm = vm;

out_enable_as:
	transtab = cfg->arm_lpae_s1_cfg.ttbr;
	transcfg = AS_TRANSCFG_PTW_MEMATTR_WB |
		   AS_TRANSCFG_PTW_RA |
		   AS_TRANSCFG_ADRMODE_AARCH64_4K |
		   AS_TRANSCFG_INA_BITS(55 - va_bits);
	if (ptdev->coherent)
		transcfg |= AS_TRANSCFG_PTW_SH_OS;

	/* If the VM is re-activated, we clear the fault. */
	vm->unhandled_fault = false;

	/* Unhandled pagefault on this AS, clear the fault and re-enable interrupts
	 * before enabling the AS.
	 */
	if (ptdev->mmu->as.faulty_mask & panthor_mmu_as_fault_mask(ptdev, as)) {
		gpu_write(ptdev, MMU_INT_CLEAR, panthor_mmu_as_fault_mask(ptdev, as));
		ptdev->mmu->as.faulty_mask &= ~panthor_mmu_as_fault_mask(ptdev, as);
		gpu_write(ptdev, MMU_INT_MASK, ~ptdev->mmu->as.faulty_mask);
	}

	ret = panthor_mmu_as_enable(vm->ptdev, vm->as.id, transtab, transcfg, vm->memattr);

out_make_active:
	if (!ret) {
		refcount_set(&vm->as.active_cnt, 1);
		list_del_init(&vm->as.lru_node);
	}

out_unlock:
	mutex_unlock(&ptdev->mmu->as.slots_lock);

out_dev_exit:
	drm_dev_exit(cookie);
	return ret;
}

/**
 * panthor_vm_idle() - Flag a VM idle
 * @VM: VM to flag as idle.
 *
 * When we know the GPU is done with the VM (no more jobs to process),
 * we can relinquish the AS slot attached to this VM, if any.
 *
 * We don't release the slot immediately, but instead place the VM in
 * the LRU list, so it can be evicted if another VM needs an AS slot.
 * This way, VMs keep attached to the AS they were given until we run
 * out of free slot, limiting the number of MMU operations (TLB flush
 * and other AS updates).
 */
void panthor_vm_idle(struct panthor_vm *vm)
{
	struct panthor_device *ptdev = vm->ptdev;

	if (!refcount_dec_and_mutex_lock(&vm->as.active_cnt, &ptdev->mmu->as.slots_lock))
		return;

	if (!drm_WARN_ON(&ptdev->base, vm->as.id == -1 || !list_empty(&vm->as.lru_node)))
		list_add_tail(&vm->as.lru_node, &ptdev->mmu->as.lru_list);

	refcount_set(&vm->as.active_cnt, 0);
	mutex_unlock(&ptdev->mmu->as.slots_lock);
}

u32 panthor_vm_page_size(struct panthor_vm *vm)
{
	const struct io_pgtable *pgt = io_pgtable_ops_to_pgtable(vm->pgtbl_ops);
	u32 pg_shift = ffs(pgt->cfg.pgsize_bitmap) - 1;

	return 1u << pg_shift;
}

static void panthor_vm_stop(struct panthor_vm *vm)
{
	drm_sched_stop(&vm->sched, NULL);
}

static void panthor_vm_start(struct panthor_vm *vm)
{
	drm_sched_start(&vm->sched, 0);
}

/**
 * panthor_vm_as() - Get the AS slot attached to a VM
 * @vm: VM to get the AS slot of.
 *
 * Return: -1 if the VM is not assigned an AS slot yet, >= 0 otherwise.
 */
int panthor_vm_as(struct panthor_vm *vm)
{
	return vm->as.id;
}

static size_t get_pgsize(u64 addr, size_t size, size_t *count)
{
	/*
	 * io-pgtable only operates on multiple pages within a single table
	 * entry, so we need to split at boundaries of the table size, i.e.
	 * the next block size up. The distance from address A to the next
	 * boundary of block size B is logically B - A % B, but in unsigned
	 * two's complement where B is a power of two we get the equivalence
	 * B - A % B == (B - A) % B == (n * B - A) % B, and choose n = 0 :)
	 */
	size_t blk_offset = -addr % SZ_2M;

	if (blk_offset || size < SZ_2M) {
		*count = min_not_zero(blk_offset, size) / SZ_4K;
		return SZ_4K;
	}
	blk_offset = -addr % SZ_1G ?: SZ_1G;
	*count = min(blk_offset, size) / SZ_2M;
	return SZ_2M;
}

static int panthor_vm_flush_range(struct panthor_vm *vm, u64 iova, u64 size)
{
	struct panthor_device *ptdev = vm->ptdev;
	int ret = 0, cookie;

	if (vm->as.id < 0)
		return 0;

	/* If the device is unplugged, we just silently skip the flush. */
	if (!drm_dev_enter(&ptdev->base, &cookie))
		return 0;

	ret = mmu_hw_do_operation(vm, iova, size, AS_COMMAND_FLUSH_PT);

	drm_dev_exit(cookie);
	return ret;
}

/**
 * panthor_vm_flush_all() - Flush L2 caches for the entirety of a VM's AS
 * @vm: VM whose cache to flush
 *
 * Return: 0 on success, a negative error code if flush failed.
 */
int panthor_vm_flush_all(struct panthor_vm *vm)
{
	return panthor_vm_flush_range(vm, vm->base.mm_start, vm->base.mm_range);
}

static int panthor_vm_unmap_pages(struct panthor_vm *vm, u64 iova, u64 size)
{
	struct panthor_device *ptdev = vm->ptdev;
	struct io_pgtable_ops *ops = vm->pgtbl_ops;
	u64 offset = 0;

	drm_dbg(&ptdev->base, "unmap: as=%d, iova=%llx, len=%llx", vm->as.id, iova, size);

	while (offset < size) {
		size_t unmapped_sz = 0, pgcount;
		size_t pgsize = get_pgsize(iova + offset, size - offset, &pgcount);

		unmapped_sz = ops->unmap_pages(ops, iova + offset, pgsize, pgcount, NULL);

		if (drm_WARN_ON(&ptdev->base, unmapped_sz != pgsize * pgcount)) {
			drm_err(&ptdev->base, "failed to unmap range %llx-%llx (requested range %llx-%llx)\n",
				iova + offset + unmapped_sz,
				iova + offset + pgsize * pgcount,
				iova, iova + size);
			panthor_vm_flush_range(vm, iova, offset + unmapped_sz);
			return  -EINVAL;
		}
		offset += unmapped_sz;
	}

	return panthor_vm_flush_range(vm, iova, size);
}

static int
panthor_vm_map_pages(struct panthor_vm *vm, u64 iova, int prot,
		     struct sg_table *sgt, u64 offset, u64 size)
{
	struct panthor_device *ptdev = vm->ptdev;
	unsigned int count;
	struct scatterlist *sgl;
	struct io_pgtable_ops *ops = vm->pgtbl_ops;
	u64 start_iova = iova;
	int ret;

	if (!size)
		return 0;

	for_each_sgtable_dma_sg(sgt, sgl, count) {
		dma_addr_t paddr = sg_dma_address(sgl);
		size_t len = sg_dma_len(sgl);

		if (len <= offset) {
			offset -= len;
			continue;
		}

		paddr += offset;
		len -= offset;
		len = min_t(size_t, len, size);
		size -= len;

		drm_dbg(&ptdev->base, "map: as=%d, iova=%llx, paddr=%pad, len=%zx",
			vm->as.id, iova, &paddr, len);

		while (len) {
			size_t pgcount, mapped = 0;
			size_t pgsize = get_pgsize(iova | paddr, len, &pgcount);

			ret = ops->map_pages(ops, iova, paddr, pgsize, pgcount, prot,
					     GFP_KERNEL, &mapped);
			iova += mapped;
			paddr += mapped;
			len -= mapped;

			if (drm_WARN_ON(&ptdev->base, !ret && !mapped))
				ret = -ENOMEM;

			if (ret) {
				/* If something failed, unmap what we've already mapped before
				 * returning. The unmap call is not supposed to fail.
				 */
				drm_WARN_ON(&ptdev->base,
					    panthor_vm_unmap_pages(vm, start_iova,
								   iova - start_iova));
				return ret;
			}
		}

		if (!size)
			break;

		offset = 0;
	}

	return panthor_vm_flush_range(vm, start_iova, iova - start_iova);
}

static int flags_to_prot(u32 flags)
{
	int prot = 0;

	if (flags & DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC)
		prot |= IOMMU_NOEXEC;

	if (!(flags & DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED))
		prot |= IOMMU_CACHE;

	if (flags & DRM_PANTHOR_VM_BIND_OP_MAP_READONLY)
		prot |= IOMMU_READ;
	else
		prot |= IOMMU_READ | IOMMU_WRITE;

	return prot;
}

/**
 * panthor_vm_alloc_va() - Allocate a region in the auto-va space
 * @VM: VM to allocate a region on.
 * @va: start of the VA range. Can be PANTHOR_VM_KERNEL_AUTO_VA if the user
 * wants the VA to be automatically allocated from the auto-VA range.
 * @size: size of the VA range.
 * @va_node: drm_mm_node to initialize. Must be zero-initialized.
 *
 * Some GPU objects, like heap chunks, are fully managed by the kernel and
 * need to be mapped to the userspace VM, in the region reserved for kernel
 * objects.
 *
 * This function takes care of allocating a region in the kernel auto-VA space.
 *
 * Return: 0 on success, an error code otherwise.
 */
int
panthor_vm_alloc_va(struct panthor_vm *vm, u64 va, u64 size,
		    struct drm_mm_node *va_node)
{
	ssize_t vm_pgsz = panthor_vm_page_size(vm);
	int ret;

	if (!size || !IS_ALIGNED(size, vm_pgsz))
		return -EINVAL;

	if (va != PANTHOR_VM_KERNEL_AUTO_VA && !IS_ALIGNED(va, vm_pgsz))
		return -EINVAL;

	mutex_lock(&vm->mm_lock);
	if (va != PANTHOR_VM_KERNEL_AUTO_VA) {
		va_node->start = va;
		va_node->size = size;
		ret = drm_mm_reserve_node(&vm->mm, va_node);
	} else {
		ret = drm_mm_insert_node_in_range(&vm->mm, va_node, size,
						  size >= SZ_2M ? SZ_2M : SZ_4K,
						  0, vm->kernel_auto_va.start,
						  vm->kernel_auto_va.end,
						  DRM_MM_INSERT_BEST);
	}
	mutex_unlock(&vm->mm_lock);

	return ret;
}

/**
 * panthor_vm_free_va() - Free a region allocated with panthor_vm_alloc_va()
 * @VM: VM to free the region on.
 * @va_node: Memory node representing the region to free.
 */
void panthor_vm_free_va(struct panthor_vm *vm, struct drm_mm_node *va_node)
{
	mutex_lock(&vm->mm_lock);
	drm_mm_remove_node(va_node);
	mutex_unlock(&vm->mm_lock);
}

static void panthor_vm_bo_put(struct drm_gpuvm_bo *vm_bo)
{
	struct panthor_gem_object *bo = to_panthor_bo(vm_bo->obj);
	struct drm_gpuvm *vm = vm_bo->vm;
	bool unpin;

	/* We must retain the GEM before calling drm_gpuvm_bo_put(),
	 * otherwise the mutex might be destroyed while we hold it.
	 * Same goes for the VM, since we take the VM resv lock.
	 */
	drm_gem_object_get(&bo->base.base);
	drm_gpuvm_get(vm);

	/* We take the resv lock to protect against concurrent accesses to the
	 * gpuvm evicted/extobj lists that are modified in
	 * drm_gpuvm_bo_destroy(), which is called if drm_gpuvm_bo_put()
	 * releases sthe last vm_bo reference.
	 * We take the BO GPUVA list lock to protect the vm_bo removal from the
	 * GEM vm_bo list.
	 */
	dma_resv_lock(drm_gpuvm_resv(vm), NULL);
	mutex_lock(&bo->gpuva_list_lock);
	unpin = drm_gpuvm_bo_put(vm_bo);
	mutex_unlock(&bo->gpuva_list_lock);
	dma_resv_unlock(drm_gpuvm_resv(vm));

	/* If the vm_bo object was destroyed, release the pin reference that
	 * was hold by this object.
	 */
	if (unpin && !bo->base.base.import_attach)
		drm_gem_shmem_unpin(&bo->base);

	drm_gpuvm_put(vm);
	drm_gem_object_put(&bo->base.base);
}

static void panthor_vm_cleanup_op_ctx(struct panthor_vm_op_ctx *op_ctx,
				      struct panthor_vm *vm)
{
	struct panthor_vma *vma, *tmp_vma;

	u32 remaining_pt_count = op_ctx->rsvd_page_tables.count -
				 op_ctx->rsvd_page_tables.ptr;

	if (remaining_pt_count) {
		kmem_cache_free_bulk(pt_cache, remaining_pt_count,
				     op_ctx->rsvd_page_tables.pages +
				     op_ctx->rsvd_page_tables.ptr);
	}

	kfree(op_ctx->rsvd_page_tables.pages);

	if (op_ctx->map.vm_bo)
		panthor_vm_bo_put(op_ctx->map.vm_bo);

	for (u32 i = 0; i < ARRAY_SIZE(op_ctx->preallocated_vmas); i++)
		kfree(op_ctx->preallocated_vmas[i]);

	list_for_each_entry_safe(vma, tmp_vma, &op_ctx->returned_vmas, node) {
		list_del(&vma->node);
		panthor_vm_bo_put(vma->base.vm_bo);
		kfree(vma);
	}
}

static struct panthor_vma *
panthor_vm_op_ctx_get_vma(struct panthor_vm_op_ctx *op_ctx)
{
	for (u32 i = 0; i < ARRAY_SIZE(op_ctx->preallocated_vmas); i++) {
		struct panthor_vma *vma = op_ctx->preallocated_vmas[i];

		if (vma) {
			op_ctx->preallocated_vmas[i] = NULL;
			return vma;
		}
	}

	return NULL;
}

static int
panthor_vm_op_ctx_prealloc_vmas(struct panthor_vm_op_ctx *op_ctx)
{
	u32 vma_count;

	switch (op_ctx->flags & DRM_PANTHOR_VM_BIND_OP_TYPE_MASK) {
	case DRM_PANTHOR_VM_BIND_OP_TYPE_MAP:
		/* One VMA for the new mapping, and two more VMAs for the remap case
		 * which might contain both a prev and next VA.
		 */
		vma_count = 3;
		break;

	case DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP:
		/* Partial unmaps might trigger a remap with either a prev or a next VA,
		 * but not both.
		 */
		vma_count = 1;
		break;

	default:
		return 0;
	}

	for (u32 i = 0; i < vma_count; i++) {
		struct panthor_vma *vma = kzalloc(sizeof(*vma), GFP_KERNEL);

		if (!vma)
			return -ENOMEM;

		op_ctx->preallocated_vmas[i] = vma;
	}

	return 0;
}

#define PANTHOR_VM_BIND_OP_MAP_FLAGS \
	(DRM_PANTHOR_VM_BIND_OP_MAP_READONLY | \
	 DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC | \
	 DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED | \
	 DRM_PANTHOR_VM_BIND_OP_TYPE_MASK)

static int panthor_vm_prepare_map_op_ctx(struct panthor_vm_op_ctx *op_ctx,
					 struct panthor_vm *vm,
					 struct panthor_gem_object *bo,
					 u64 offset,
					 u64 size, u64 va,
					 u32 flags)
{
	struct drm_gpuvm_bo *preallocated_vm_bo;
	struct sg_table *sgt = NULL;
	u64 pt_count;
	int ret;

	if (!bo)
		return -EINVAL;

	if ((flags & ~PANTHOR_VM_BIND_OP_MAP_FLAGS) ||
	    (flags & DRM_PANTHOR_VM_BIND_OP_TYPE_MASK) != DRM_PANTHOR_VM_BIND_OP_TYPE_MAP)
		return -EINVAL;

	/* Make sure the VA and size are aligned and in-bounds. */
	if (size > bo->base.base.size || offset > bo->base.base.size - size)
		return -EINVAL;

	/* If the BO has an exclusive VM attached, it can't be mapped to other VMs. */
	if (bo->exclusive_vm_root_gem &&
	    bo->exclusive_vm_root_gem != panthor_vm_root_gem(vm))
		return -EINVAL;

	memset(op_ctx, 0, sizeof(*op_ctx));
	INIT_LIST_HEAD(&op_ctx->returned_vmas);
	op_ctx->flags = flags;
	op_ctx->va.range = size;
	op_ctx->va.addr = va;

	ret = panthor_vm_op_ctx_prealloc_vmas(op_ctx);
	if (ret)
		goto err_cleanup;

	if (!bo->base.base.import_attach) {
		/* Pre-reserve the BO pages, so the map operation doesn't have to
		 * allocate.
		 */
		ret = drm_gem_shmem_pin(&bo->base);
		if (ret)
			goto err_cleanup;
	}

	sgt = drm_gem_shmem_get_pages_sgt(&bo->base);
	if (IS_ERR(sgt)) {
		if (!bo->base.base.import_attach)
			drm_gem_shmem_unpin(&bo->base);

		ret = PTR_ERR(sgt);
		goto err_cleanup;
	}

	op_ctx->map.sgt = sgt;

	preallocated_vm_bo = drm_gpuvm_bo_create(&vm->base, &bo->base.base);
	if (!preallocated_vm_bo) {
		if (!bo->base.base.import_attach)
			drm_gem_shmem_unpin(&bo->base);

		ret = -ENOMEM;
		goto err_cleanup;
	}

	/* drm_gpuvm_bo_obtain_prealloc() will call drm_gpuvm_bo_put() on our
	 * pre-allocated BO if the <BO,VM> association exists. Given we
	 * only have one ref on preallocated_vm_bo, drm_gpuvm_bo_destroy() will
	 * be called immediately, and we have to hold the VM resv lock when
	 * calling this function.
	 */
	dma_resv_lock(panthor_vm_resv(vm), NULL);
	mutex_lock(&bo->gpuva_list_lock);
	op_ctx->map.vm_bo = drm_gpuvm_bo_obtain_prealloc(preallocated_vm_bo);
	mutex_unlock(&bo->gpuva_list_lock);
	dma_resv_unlock(panthor_vm_resv(vm));

	/* If the a vm_bo for this <VM,BO> combination exists, it already
	 * retains a pin ref, and we can release the one we took earlier.
	 *
	 * If our pre-allocated vm_bo is picked, it now retains the pin ref,
	 * which will be released in panthor_vm_bo_put().
	 */
	if (preallocated_vm_bo != op_ctx->map.vm_bo &&
	    !bo->base.base.import_attach)
		drm_gem_shmem_unpin(&bo->base);

	op_ctx->map.bo_offset = offset;

	/* L1, L2 and L3 page tables.
	 * We could optimize L3 allocation by iterating over the sgt and merging
	 * 2M contiguous blocks, but it's simpler to over-provision and return
	 * the pages if they're not used.
	 */
	pt_count = ((ALIGN(va + size, 1ull << 39) - ALIGN_DOWN(va, 1ull << 39)) >> 39) +
		   ((ALIGN(va + size, 1ull << 30) - ALIGN_DOWN(va, 1ull << 30)) >> 30) +
		   ((ALIGN(va + size, 1ull << 21) - ALIGN_DOWN(va, 1ull << 21)) >> 21);

	op_ctx->rsvd_page_tables.pages = kcalloc(pt_count,
						 sizeof(*op_ctx->rsvd_page_tables.pages),
						 GFP_KERNEL);
	if (!op_ctx->rsvd_page_tables.pages) {
		ret = -ENOMEM;
		goto err_cleanup;
	}

	ret = kmem_cache_alloc_bulk(pt_cache, GFP_KERNEL, pt_count,
				    op_ctx->rsvd_page_tables.pages);
	op_ctx->rsvd_page_tables.count = ret;
	if (ret != pt_count) {
		ret = -ENOMEM;
		goto err_cleanup;
	}

	/* Insert BO into the extobj list last, when we know nothing can fail. */
	dma_resv_lock(panthor_vm_resv(vm), NULL);
	drm_gpuvm_bo_extobj_add(op_ctx->map.vm_bo);
	dma_resv_unlock(panthor_vm_resv(vm));

	return 0;

err_cleanup:
	panthor_vm_cleanup_op_ctx(op_ctx, vm);
	return ret;
}

static int panthor_vm_prepare_unmap_op_ctx(struct panthor_vm_op_ctx *op_ctx,
					   struct panthor_vm *vm,
					   u64 va, u64 size)
{
	u32 pt_count = 0;
	int ret;

	memset(op_ctx, 0, sizeof(*op_ctx));
	INIT_LIST_HEAD(&op_ctx->returned_vmas);
	op_ctx->va.range = size;
	op_ctx->va.addr = va;
	op_ctx->flags = DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP;

	/* Pre-allocate L3 page tables to account for the split-2M-block
	 * situation on unmap.
	 */
	if (va != ALIGN(va, SZ_2M))
		pt_count++;

	if (va + size != ALIGN(va + size, SZ_2M) &&
	    ALIGN(va + size, SZ_2M) != ALIGN(va, SZ_2M))
		pt_count++;

	ret = panthor_vm_op_ctx_prealloc_vmas(op_ctx);
	if (ret)
		goto err_cleanup;

	if (pt_count) {
		op_ctx->rsvd_page_tables.pages = kcalloc(pt_count,
							 sizeof(*op_ctx->rsvd_page_tables.pages),
							 GFP_KERNEL);
		if (!op_ctx->rsvd_page_tables.pages) {
			ret = -ENOMEM;
			goto err_cleanup;
		}

		ret = kmem_cache_alloc_bulk(pt_cache, GFP_KERNEL, pt_count,
					    op_ctx->rsvd_page_tables.pages);
		if (ret != pt_count) {
			ret = -ENOMEM;
			goto err_cleanup;
		}
		op_ctx->rsvd_page_tables.count = pt_count;
	}

	return 0;

err_cleanup:
	panthor_vm_cleanup_op_ctx(op_ctx, vm);
	return ret;
}

static void panthor_vm_prepare_sync_only_op_ctx(struct panthor_vm_op_ctx *op_ctx,
						struct panthor_vm *vm)
{
	memset(op_ctx, 0, sizeof(*op_ctx));
	INIT_LIST_HEAD(&op_ctx->returned_vmas);
	op_ctx->flags = DRM_PANTHOR_VM_BIND_OP_TYPE_SYNC_ONLY;
}

/**
 * panthor_vm_get_bo_for_va() - Get the GEM object mapped at a virtual address
 * @vm: VM to look into.
 * @va: Virtual address to search for.
 * @bo_offset: Offset of the GEM object mapped at this virtual address.
 * Only valid on success.
 *
 * The object returned by this function might no longer be mapped when the
 * function returns. It's the caller responsibility to ensure there's no
 * concurrent map/unmap operations making the returned value invalid, or
 * make sure it doesn't matter if the object is no longer mapped.
 *
 * Return: A valid pointer on success, an ERR_PTR() otherwise.
 */
struct panthor_gem_object *
panthor_vm_get_bo_for_va(struct panthor_vm *vm, u64 va, u64 *bo_offset)
{
	struct panthor_gem_object *bo = ERR_PTR(-ENOENT);
	struct drm_gpuva *gpuva;
	struct panthor_vma *vma;

	/* Take the VM lock to prevent concurrent map/unmap operations. */
	mutex_lock(&vm->op_lock);
	gpuva = drm_gpuva_find_first(&vm->base, va, 1);
	vma = gpuva ? container_of(gpuva, struct panthor_vma, base) : NULL;
	if (vma && vma->base.gem.obj) {
		drm_gem_object_get(vma->base.gem.obj);
		bo = to_panthor_bo(vma->base.gem.obj);
		*bo_offset = vma->base.gem.offset + (va - vma->base.va.addr);
	}
	mutex_unlock(&vm->op_lock);

	return bo;
}

#define PANTHOR_VM_MIN_KERNEL_VA_SIZE	SZ_256M

static u64
panthor_vm_create_get_user_va_range(const struct drm_panthor_vm_create *args,
				    u64 full_va_range)
{
	u64 user_va_range;

	/* Make sure we have a minimum amount of VA space for kernel objects. */
	if (full_va_range < PANTHOR_VM_MIN_KERNEL_VA_SIZE)
		return 0;

	if (args->user_va_range) {
		/* Use the user provided value if != 0. */
		user_va_range = args->user_va_range;
	} else if (TASK_SIZE_OF(current) < full_va_range) {
		/* If the task VM size is smaller than the GPU VA range, pick this
		 * as our default user VA range, so userspace can CPU/GPU map buffers
		 * at the same address.
		 */
		user_va_range = TASK_SIZE_OF(current);
	} else {
		/* If the GPU VA range is smaller than the task VM size, we
		 * just have to live with the fact we won't be able to map
		 * all buffers at the same GPU/CPU address.
		 *
		 * If the GPU VA range is bigger than 4G (more than 32-bit of
		 * VA), we split the range in two, and assign half of it to
		 * the user and the other half to the kernel, if it's not, we
		 * keep the kernel VA space as small as possible.
		 */
		user_va_range = full_va_range > SZ_4G ?
				full_va_range / 2 :
				full_va_range - PANTHOR_VM_MIN_KERNEL_VA_SIZE;
	}

	if (full_va_range - PANTHOR_VM_MIN_KERNEL_VA_SIZE < user_va_range)
		user_va_range = full_va_range - PANTHOR_VM_MIN_KERNEL_VA_SIZE;

	return user_va_range;
}

#define PANTHOR_VM_CREATE_FLAGS		0

static int
panthor_vm_create_check_args(const struct panthor_device *ptdev,
			     const struct drm_panthor_vm_create *args,
			     u64 *kernel_va_start, u64 *kernel_va_range)
{
	u32 va_bits = GPU_MMU_FEATURES_VA_BITS(ptdev->gpu_info.mmu_features);
	u64 full_va_range = 1ull << va_bits;
	u64 user_va_range;

	if (args->flags & ~PANTHOR_VM_CREATE_FLAGS)
		return -EINVAL;

	user_va_range = panthor_vm_create_get_user_va_range(args, full_va_range);
	if (!user_va_range || (args->user_va_range && args->user_va_range > user_va_range))
		return -EINVAL;

	/* Pick a kernel VA range that's a power of two, to have a clear split. */
	*kernel_va_range = rounddown_pow_of_two(full_va_range - user_va_range);
	*kernel_va_start = full_va_range - *kernel_va_range;
	return 0;
}

/*
 * Only 32 VMs per open file. If that becomes a limiting factor, we can
 * increase this number.
 */
#define PANTHOR_MAX_VMS_PER_FILE	32

/**
 * panthor_vm_pool_create_vm() - Create a VM
 * @pool: The VM to create this VM on.
 * @kernel_va_start: Start of the region reserved for kernel objects.
 * @kernel_va_range: Size of the region reserved for kernel objects.
 *
 * Return: a positive VM ID on success, a negative error code otherwise.
 */
int panthor_vm_pool_create_vm(struct panthor_device *ptdev,
			      struct panthor_vm_pool *pool,
			      struct drm_panthor_vm_create *args)
{
	u64 kernel_va_start, kernel_va_range;
	struct panthor_vm *vm;
	int ret;
	u32 id;

	ret = panthor_vm_create_check_args(ptdev, args, &kernel_va_start, &kernel_va_range);
	if (ret)
		return ret;

	vm = panthor_vm_create(ptdev, false, kernel_va_start, kernel_va_range,
			       kernel_va_start, kernel_va_range);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

	ret = xa_alloc(&pool->xa, &id, vm,
		       XA_LIMIT(1, PANTHOR_MAX_VMS_PER_FILE), GFP_KERNEL);

	if (ret) {
		panthor_vm_put(vm);
		return ret;
	}

	args->user_va_range = kernel_va_start;
	return id;
}

static void panthor_vm_destroy(struct panthor_vm *vm)
{
	if (!vm)
		return;

	vm->destroyed = true;

	mutex_lock(&vm->heaps.lock);
	panthor_heap_pool_destroy(vm->heaps.pool);
	vm->heaps.pool = NULL;
	mutex_unlock(&vm->heaps.lock);

	drm_WARN_ON(&vm->ptdev->base,
		    panthor_vm_unmap_range(vm, vm->base.mm_start, vm->base.mm_range));
	panthor_vm_put(vm);
}

/**
 * panthor_vm_pool_destroy_vm() - Destroy a VM.
 * @pool: VM pool.
 * @handle: VM handle.
 *
 * This function doesn't free the VM object or its resources, it just kills
 * all mappings, and makes sure nothing can be mapped after that point.
 *
 * If there was any active jobs at the time this function is called, these
 * jobs should experience page faults and be killed as a result.
 *
 * The VM resources are freed when the last reference on the VM object is
 * dropped.
 */
int panthor_vm_pool_destroy_vm(struct panthor_vm_pool *pool, u32 handle)
{
	struct panthor_vm *vm;

	vm = xa_erase(&pool->xa, handle);

	panthor_vm_destroy(vm);

	return vm ? 0 : -EINVAL;
}

/**
 * panthor_vm_pool_get_vm() - Retrieve VM object bound to a VM handle
 * @pool: VM pool to check.
 * @handle: Handle of the VM to retrieve.
 *
 * Return: A valid pointer if the VM exists, NULL otherwise.
 */
struct panthor_vm *
panthor_vm_pool_get_vm(struct panthor_vm_pool *pool, u32 handle)
{
	struct panthor_vm *vm;

	xa_lock(&pool->xa);
	vm = panthor_vm_get(xa_load(&pool->xa, handle));
	xa_unlock(&pool->xa);

	return vm;
}

/**
 * panthor_vm_pool_destroy() - Destroy a VM pool.
 * @pfile: File.
 *
 * Destroy all VMs in the pool, and release the pool resources.
 *
 * Note that VMs can outlive the pool they were created from if other
 * objects hold a reference to there VMs.
 */
void panthor_vm_pool_destroy(struct panthor_file *pfile)
{
	struct panthor_vm *vm;
	unsigned long i;

	if (!pfile->vms)
		return;

	xa_for_each(&pfile->vms->xa, i, vm)
		panthor_vm_destroy(vm);

	xa_destroy(&pfile->vms->xa);
	kfree(pfile->vms);
}

/**
 * panthor_vm_pool_create() - Create a VM pool
 * @pfile: File.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_vm_pool_create(struct panthor_file *pfile)
{
	pfile->vms = kzalloc(sizeof(*pfile->vms), GFP_KERNEL);
	if (!pfile->vms)
		return -ENOMEM;

	xa_init_flags(&pfile->vms->xa, XA_FLAGS_ALLOC1);
	return 0;
}

/* dummy TLB ops, the real TLB flush happens in panthor_vm_flush_range() */
static void mmu_tlb_flush_all(void *cookie)
{
}

static void mmu_tlb_flush_walk(unsigned long iova, size_t size, size_t granule, void *cookie)
{
}

static const struct iommu_flush_ops mmu_tlb_ops = {
	.tlb_flush_all = mmu_tlb_flush_all,
	.tlb_flush_walk = mmu_tlb_flush_walk,
};

static const char *access_type_name(struct panthor_device *ptdev,
				    u32 fault_status)
{
	switch (fault_status & AS_FAULTSTATUS_ACCESS_TYPE_MASK) {
	case AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC:
		return "ATOMIC";
	case AS_FAULTSTATUS_ACCESS_TYPE_READ:
		return "READ";
	case AS_FAULTSTATUS_ACCESS_TYPE_WRITE:
		return "WRITE";
	case AS_FAULTSTATUS_ACCESS_TYPE_EX:
		return "EXECUTE";
	default:
		drm_WARN_ON(&ptdev->base, 1);
		return NULL;
	}
}

static void panthor_mmu_irq_handler(struct panthor_device *ptdev, u32 status)
{
	bool has_unhandled_faults = false;

	status = panthor_mmu_fault_mask(ptdev, status);
	while (status) {
		u32 as = ffs(status | (status >> 16)) - 1;
		u32 mask = panthor_mmu_as_fault_mask(ptdev, as);
		u32 new_int_mask;
		u64 addr;
		u32 fault_status;
		u32 exception_type;
		u32 access_type;
		u32 source_id;

		fault_status = gpu_read(ptdev, AS_FAULTSTATUS(as));
		addr = gpu_read(ptdev, AS_FAULTADDRESS_LO(as));
		addr |= (u64)gpu_read(ptdev, AS_FAULTADDRESS_HI(as)) << 32;

		/* decode the fault status */
		exception_type = fault_status & 0xFF;
		access_type = (fault_status >> 8) & 0x3;
		source_id = (fault_status >> 16);

		mutex_lock(&ptdev->mmu->as.slots_lock);

		ptdev->mmu->as.faulty_mask |= mask;
		new_int_mask =
			panthor_mmu_fault_mask(ptdev, ~ptdev->mmu->as.faulty_mask);

		/* terminal fault, print info about the fault */
		drm_err(&ptdev->base,
			"Unhandled Page fault in AS%d at VA 0x%016llX\n"
			"raw fault status: 0x%X\n"
			"decoded fault status: %s\n"
			"exception type 0x%X: %s\n"
			"access type 0x%X: %s\n"
			"source id 0x%X\n",
			as, addr,
			fault_status,
			(fault_status & (1 << 10) ? "DECODER FAULT" : "SLAVE FAULT"),
			exception_type, panthor_exception_name(ptdev, exception_type),
			access_type, access_type_name(ptdev, fault_status),
			source_id);

		/* Ignore MMU interrupts on this AS until it's been
		 * re-enabled.
		 */
		ptdev->mmu->irq.mask = new_int_mask;
		gpu_write(ptdev, MMU_INT_MASK, new_int_mask);

		if (ptdev->mmu->as.slots[as].vm)
			ptdev->mmu->as.slots[as].vm->unhandled_fault = true;

		/* Disable the MMU to kill jobs on this AS. */
		panthor_mmu_as_disable(ptdev, as);
		mutex_unlock(&ptdev->mmu->as.slots_lock);

		status &= ~mask;
		has_unhandled_faults = true;
	}

	if (has_unhandled_faults)
		panthor_sched_report_mmu_fault(ptdev);
}
PANTHOR_IRQ_HANDLER(mmu, MMU, panthor_mmu_irq_handler);

/**
 * panthor_mmu_suspend() - Suspend the MMU logic
 * @ptdev: Device.
 *
 * All we do here is de-assign the AS slots on all active VMs, so things
 * get flushed to the main memory, and no further access to these VMs are
 * possible.
 *
 * We also suspend the MMU IRQ.
 */
void panthor_mmu_suspend(struct panthor_device *ptdev)
{
	mutex_lock(&ptdev->mmu->as.slots_lock);
	for (u32 i = 0; i < ARRAY_SIZE(ptdev->mmu->as.slots); i++) {
		struct panthor_vm *vm = ptdev->mmu->as.slots[i].vm;

		if (vm) {
			drm_WARN_ON(&ptdev->base, panthor_mmu_as_disable(ptdev, i));
			panthor_vm_release_as_locked(vm);
		}
	}
	mutex_unlock(&ptdev->mmu->as.slots_lock);

	panthor_mmu_irq_suspend(&ptdev->mmu->irq);
}

/**
 * panthor_mmu_resume() - Resume the MMU logic
 * @ptdev: Device.
 *
 * Resume the IRQ.
 *
 * We don't re-enable previously active VMs. We assume other parts of the
 * driver will call panthor_vm_active() on the VMs they intend to use.
 */
void panthor_mmu_resume(struct panthor_device *ptdev)
{
	mutex_lock(&ptdev->mmu->as.slots_lock);
	ptdev->mmu->as.alloc_mask = 0;
	ptdev->mmu->as.faulty_mask = 0;
	mutex_unlock(&ptdev->mmu->as.slots_lock);

	panthor_mmu_irq_resume(&ptdev->mmu->irq, panthor_mmu_fault_mask(ptdev, ~0));
}

/**
 * panthor_mmu_pre_reset() - Prepare for a reset
 * @ptdev: Device.
 *
 * Suspend the IRQ, and make sure all VM_BIND queues are stopped, so we
 * don't get asked to do a VM operation while the GPU is down.
 *
 * We don't cleanly shutdown the AS slots here, because the reset might
 * come from an AS_ACTIVE_BIT stuck situation.
 */
void panthor_mmu_pre_reset(struct panthor_device *ptdev)
{
	struct panthor_vm *vm;

	panthor_mmu_irq_suspend(&ptdev->mmu->irq);

	mutex_lock(&ptdev->mmu->vm.lock);
	ptdev->mmu->vm.reset_in_progress = true;
	list_for_each_entry(vm, &ptdev->mmu->vm.list, node)
		panthor_vm_stop(vm);
	mutex_unlock(&ptdev->mmu->vm.lock);
}

/**
 * panthor_mmu_post_reset() - Restore things after a reset
 * @ptdev: Device.
 *
 * Put the MMU logic back in action after a reset. That implies resuming the
 * IRQ and re-enabling the VM_BIND queues.
 */
void panthor_mmu_post_reset(struct panthor_device *ptdev)
{
	struct panthor_vm *vm;

	mutex_lock(&ptdev->mmu->as.slots_lock);

	/* Now that the reset is effective, we can assume that none of the
	 * AS slots are setup, and clear the faulty flags too.
	 */
	ptdev->mmu->as.alloc_mask = 0;
	ptdev->mmu->as.faulty_mask = 0;

	for (u32 i = 0; i < ARRAY_SIZE(ptdev->mmu->as.slots); i++) {
		struct panthor_vm *vm = ptdev->mmu->as.slots[i].vm;

		if (vm)
			panthor_vm_release_as_locked(vm);
	}

	mutex_unlock(&ptdev->mmu->as.slots_lock);

	panthor_mmu_irq_resume(&ptdev->mmu->irq, panthor_mmu_fault_mask(ptdev, ~0));

	/* Restart the VM_BIND queues. */
	mutex_lock(&ptdev->mmu->vm.lock);
	list_for_each_entry(vm, &ptdev->mmu->vm.list, node) {
		panthor_vm_start(vm);
	}
	ptdev->mmu->vm.reset_in_progress = false;
	mutex_unlock(&ptdev->mmu->vm.lock);
}

static void panthor_vm_free(struct drm_gpuvm *gpuvm)
{
	struct panthor_vm *vm = container_of(gpuvm, struct panthor_vm, base);
	struct panthor_device *ptdev = vm->ptdev;

	mutex_lock(&vm->heaps.lock);
	if (drm_WARN_ON(&ptdev->base, vm->heaps.pool))
		panthor_heap_pool_destroy(vm->heaps.pool);
	mutex_unlock(&vm->heaps.lock);
	mutex_destroy(&vm->heaps.lock);

	mutex_lock(&ptdev->mmu->vm.lock);
	list_del(&vm->node);
	/* Restore the scheduler state so we can call drm_sched_entity_destroy()
	 * and drm_sched_fini(). If get there, that means we have no job left
	 * and no new jobs can be queued, so we can start the scheduler without
	 * risking interfering with the reset.
	 */
	if (ptdev->mmu->vm.reset_in_progress)
		panthor_vm_start(vm);
	mutex_unlock(&ptdev->mmu->vm.lock);

	drm_sched_entity_destroy(&vm->entity);
	drm_sched_fini(&vm->sched);

	mutex_lock(&ptdev->mmu->as.slots_lock);
	if (vm->as.id >= 0) {
		int cookie;

		if (drm_dev_enter(&ptdev->base, &cookie)) {
			panthor_mmu_as_disable(ptdev, vm->as.id);
			drm_dev_exit(cookie);
		}

		ptdev->mmu->as.slots[vm->as.id].vm = NULL;
		clear_bit(vm->as.id, &ptdev->mmu->as.alloc_mask);
		list_del(&vm->as.lru_node);
	}
	mutex_unlock(&ptdev->mmu->as.slots_lock);

	free_io_pgtable_ops(vm->pgtbl_ops);

	drm_mm_takedown(&vm->mm);
	kfree(vm);
}

/**
 * panthor_vm_put() - Release a reference on a VM
 * @vm: VM to release the reference on. Can be NULL.
 */
void panthor_vm_put(struct panthor_vm *vm)
{
	drm_gpuvm_put(vm ? &vm->base : NULL);
}

/**
 * panthor_vm_get() - Get a VM reference
 * @vm: VM to get the reference on. Can be NULL.
 *
 * Return: @vm value.
 */
struct panthor_vm *panthor_vm_get(struct panthor_vm *vm)
{
	if (vm)
		drm_gpuvm_get(&vm->base);

	return vm;
}

/**
 * panthor_vm_get_heap_pool() - Get the heap pool attached to a VM
 * @vm: VM to query the heap pool on.
 * @create: True if the heap pool should be created when it doesn't exist.
 *
 * Heap pools are per-VM. This function allows one to retrieve the heap pool
 * attached to a VM.
 *
 * If no heap pool exists yet, and @create is true, we create one.
 *
 * The returned panthor_heap_pool should be released with panthor_heap_pool_put().
 *
 * Return: A valid pointer on success, an ERR_PTR() otherwise.
 */
struct panthor_heap_pool *panthor_vm_get_heap_pool(struct panthor_vm *vm, bool create)
{
	struct panthor_heap_pool *pool;

	mutex_lock(&vm->heaps.lock);
	if (!vm->heaps.pool && create) {
		if (vm->destroyed)
			pool = ERR_PTR(-EINVAL);
		else
			pool = panthor_heap_pool_create(vm->ptdev, vm);

		if (!IS_ERR(pool))
			vm->heaps.pool = panthor_heap_pool_get(pool);
	} else {
		pool = panthor_heap_pool_get(vm->heaps.pool);
		if (!pool)
			pool = ERR_PTR(-ENOENT);
	}
	mutex_unlock(&vm->heaps.lock);

	return pool;
}

static u64 mair_to_memattr(u64 mair, bool coherent)
{
	u64 memattr = 0;
	u32 i;

	for (i = 0; i < 8; i++) {
		u8 in_attr = mair >> (8 * i), out_attr;
		u8 outer = in_attr >> 4, inner = in_attr & 0xf;

		/* For caching to be enabled, inner and outer caching policy
		 * have to be both write-back, if one of them is write-through
		 * or non-cacheable, we just choose non-cacheable. Device
		 * memory is also translated to non-cacheable.
		 */
		if (!(outer & 3) || !(outer & 4) || !(inner & 4)) {
			out_attr = AS_MEMATTR_AARCH64_INNER_OUTER_NC |
				   AS_MEMATTR_AARCH64_SH_MIDGARD_INNER |
				   AS_MEMATTR_AARCH64_INNER_ALLOC_EXPL(false, false);
		} else {
			out_attr = AS_MEMATTR_AARCH64_INNER_OUTER_WB |
				   AS_MEMATTR_AARCH64_INNER_ALLOC_EXPL(inner & 1, inner & 2);
			/* Use SH_MIDGARD_INNER mode when device isn't coherent,
			 * so SH_IS, which is used when IOMMU_CACHE is set, maps
			 * to Mali's internal-shareable mode. As per the Mali
			 * Spec, inner and outer-shareable modes aren't allowed
			 * for WB memory when coherency is disabled.
			 * Use SH_CPU_INNER mode when coherency is enabled, so
			 * that SH_IS actually maps to the standard definition of
			 * inner-shareable.
			 */
			if (!coherent)
				out_attr |= AS_MEMATTR_AARCH64_SH_MIDGARD_INNER;
			else
				out_attr |= AS_MEMATTR_AARCH64_SH_CPU_INNER;
		}

		memattr |= (u64)out_attr << (8 * i);
	}

	return memattr;
}

static void panthor_vma_link(struct panthor_vm *vm,
			     struct panthor_vma *vma,
			     struct drm_gpuvm_bo *vm_bo)
{
	struct panthor_gem_object *bo = to_panthor_bo(vma->base.gem.obj);

	mutex_lock(&bo->gpuva_list_lock);
	drm_gpuva_link(&vma->base, vm_bo);
	drm_WARN_ON(&vm->ptdev->base, drm_gpuvm_bo_put(vm_bo));
	mutex_unlock(&bo->gpuva_list_lock);
}

static void panthor_vma_unlink(struct panthor_vm *vm,
			       struct panthor_vma *vma)
{
	struct panthor_gem_object *bo = to_panthor_bo(vma->base.gem.obj);
	struct drm_gpuvm_bo *vm_bo = drm_gpuvm_bo_get(vma->base.vm_bo);

	mutex_lock(&bo->gpuva_list_lock);
	drm_gpuva_unlink(&vma->base);
	mutex_unlock(&bo->gpuva_list_lock);

	/* drm_gpuva_unlink() release the vm_bo, but we manually retained it
	 * when entering this function, so we can implement deferred VMA
	 * destruction. Re-assign it here.
	 */
	vma->base.vm_bo = vm_bo;
	list_add_tail(&vma->node, &vm->op_ctx->returned_vmas);
}

static void panthor_vma_init(struct panthor_vma *vma, u32 flags)
{
	INIT_LIST_HEAD(&vma->node);
	vma->flags = flags;
}

#define PANTHOR_VM_MAP_FLAGS \
	(DRM_PANTHOR_VM_BIND_OP_MAP_READONLY | \
	 DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC | \
	 DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED)

static int panthor_gpuva_sm_step_map(struct drm_gpuva_op *op, void *priv)
{
	struct panthor_vm *vm = priv;
	struct panthor_vm_op_ctx *op_ctx = vm->op_ctx;
	struct panthor_vma *vma = panthor_vm_op_ctx_get_vma(op_ctx);
	int ret;

	if (!vma)
		return -EINVAL;

	panthor_vma_init(vma, op_ctx->flags & PANTHOR_VM_MAP_FLAGS);

	ret = panthor_vm_map_pages(vm, op->map.va.addr, flags_to_prot(vma->flags),
				   op_ctx->map.sgt, op->map.gem.offset,
				   op->map.va.range);
	if (ret)
		return ret;

	/* Ref owned by the mapping now, clear the obj field so we don't release the
	 * pinning/obj ref behind GPUVA's back.
	 */
	drm_gpuva_map(&vm->base, &vma->base, &op->map);
	panthor_vma_link(vm, vma, op_ctx->map.vm_bo);
	op_ctx->map.vm_bo = NULL;
	return 0;
}

static int panthor_gpuva_sm_step_remap(struct drm_gpuva_op *op,
				       void *priv)
{
	struct panthor_vma *unmap_vma = container_of(op->remap.unmap->va, struct panthor_vma, base);
	struct panthor_vm *vm = priv;
	struct panthor_vm_op_ctx *op_ctx = vm->op_ctx;
	struct panthor_vma *prev_vma = NULL, *next_vma = NULL;
	u64 unmap_start, unmap_range;
	int ret;

	drm_gpuva_op_remap_to_unmap_range(&op->remap, &unmap_start, &unmap_range);
	ret = panthor_vm_unmap_pages(vm, unmap_start, unmap_range);
	if (ret)
		return ret;

	if (op->remap.prev) {
		prev_vma = panthor_vm_op_ctx_get_vma(op_ctx);
		panthor_vma_init(prev_vma, unmap_vma->flags);
	}

	if (op->remap.next) {
		next_vma = panthor_vm_op_ctx_get_vma(op_ctx);
		panthor_vma_init(next_vma, unmap_vma->flags);
	}

	drm_gpuva_remap(prev_vma ? &prev_vma->base : NULL,
			next_vma ? &next_vma->base : NULL,
			&op->remap);

	if (prev_vma) {
		/* panthor_vma_link() transfers the vm_bo ownership to
		 * the VMA object. Since the vm_bo we're passing is still
		 * owned by the old mapping which will be released when this
		 * mapping is destroyed, we need to grab a ref here.
		 */
		panthor_vma_link(vm, prev_vma,
				 drm_gpuvm_bo_get(op->remap.unmap->va->vm_bo));
	}

	if (next_vma) {
		panthor_vma_link(vm, next_vma,
				 drm_gpuvm_bo_get(op->remap.unmap->va->vm_bo));
	}

	panthor_vma_unlink(vm, unmap_vma);
	return 0;
}

static int panthor_gpuva_sm_step_unmap(struct drm_gpuva_op *op,
				       void *priv)
{
	struct panthor_vma *unmap_vma = container_of(op->unmap.va, struct panthor_vma, base);
	struct panthor_vm *vm = priv;
	int ret;

	ret = panthor_vm_unmap_pages(vm, unmap_vma->base.va.addr,
				     unmap_vma->base.va.range);
	if (drm_WARN_ON(&vm->ptdev->base, ret))
		return ret;

	drm_gpuva_unmap(&op->unmap);
	panthor_vma_unlink(vm, unmap_vma);
	return 0;
}

static const struct drm_gpuvm_ops panthor_gpuvm_ops = {
	.vm_free = panthor_vm_free,
	.sm_step_map = panthor_gpuva_sm_step_map,
	.sm_step_remap = panthor_gpuva_sm_step_remap,
	.sm_step_unmap = panthor_gpuva_sm_step_unmap,
};

/**
 * panthor_vm_resv() - Get the dma_resv object attached to a VM.
 * @vm: VM to get the dma_resv of.
 *
 * Return: A dma_resv object.
 */
struct dma_resv *panthor_vm_resv(struct panthor_vm *vm)
{
	return drm_gpuvm_resv(&vm->base);
}

struct drm_gem_object *panthor_vm_root_gem(struct panthor_vm *vm)
{
	if (!vm)
		return NULL;

	return vm->base.r_obj;
}

static int
panthor_vm_exec_op(struct panthor_vm *vm, struct panthor_vm_op_ctx *op,
		   bool flag_vm_unusable_on_failure)
{
	u32 op_type = op->flags & DRM_PANTHOR_VM_BIND_OP_TYPE_MASK;
	int ret;

	if (op_type == DRM_PANTHOR_VM_BIND_OP_TYPE_SYNC_ONLY)
		return 0;

	mutex_lock(&vm->op_lock);
	vm->op_ctx = op;
	switch (op_type) {
	case DRM_PANTHOR_VM_BIND_OP_TYPE_MAP:
		if (vm->unusable) {
			ret = -EINVAL;
			break;
		}

		ret = drm_gpuvm_sm_map(&vm->base, vm, op->va.addr, op->va.range,
				       op->map.vm_bo->obj, op->map.bo_offset);
		break;

	case DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP:
		ret = drm_gpuvm_sm_unmap(&vm->base, vm, op->va.addr, op->va.range);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	if (ret && flag_vm_unusable_on_failure)
		vm->unusable = true;

	vm->op_ctx = NULL;
	mutex_unlock(&vm->op_lock);

	return ret;
}

static struct dma_fence *
panthor_vm_bind_run_job(struct drm_sched_job *sched_job)
{
	struct panthor_vm_bind_job *job = container_of(sched_job, struct panthor_vm_bind_job, base);
	bool cookie;
	int ret;

	/* Not only we report an error whose result is propagated to the
	 * drm_sched finished fence, but we also flag the VM as unusable, because
	 * a failure in the async VM_BIND results in an inconsistent state. VM needs
	 * to be destroyed and recreated.
	 */
	cookie = dma_fence_begin_signalling();
	ret = panthor_vm_exec_op(job->vm, &job->ctx, true);
	dma_fence_end_signalling(cookie);

	return ret ? ERR_PTR(ret) : NULL;
}

static void panthor_vm_bind_job_release(struct kref *kref)
{
	struct panthor_vm_bind_job *job = container_of(kref, struct panthor_vm_bind_job, refcount);

	if (job->base.s_fence)
		drm_sched_job_cleanup(&job->base);

	panthor_vm_cleanup_op_ctx(&job->ctx, job->vm);
	panthor_vm_put(job->vm);
	kfree(job);
}

/**
 * panthor_vm_bind_job_put() - Release a VM_BIND job reference
 * @sched_job: Job to release the reference on.
 */
void panthor_vm_bind_job_put(struct drm_sched_job *sched_job)
{
	struct panthor_vm_bind_job *job =
		container_of(sched_job, struct panthor_vm_bind_job, base);

	if (sched_job)
		kref_put(&job->refcount, panthor_vm_bind_job_release);
}

static void
panthor_vm_bind_free_job(struct drm_sched_job *sched_job)
{
	struct panthor_vm_bind_job *job =
		container_of(sched_job, struct panthor_vm_bind_job, base);

	drm_sched_job_cleanup(sched_job);

	/* Do the heavy cleanups asynchronously, so we're out of the
	 * dma-signaling path and can acquire dma-resv locks safely.
	 */
	queue_work(panthor_cleanup_wq, &job->cleanup_op_ctx_work);
}

static enum drm_gpu_sched_stat
panthor_vm_bind_timedout_job(struct drm_sched_job *sched_job)
{
	WARN(1, "VM_BIND ops are synchronous for now, there should be no timeout!");
	return DRM_GPU_SCHED_STAT_NOMINAL;
}

static const struct drm_sched_backend_ops panthor_vm_bind_ops = {
	.run_job = panthor_vm_bind_run_job,
	.free_job = panthor_vm_bind_free_job,
	.timedout_job = panthor_vm_bind_timedout_job,
};

/**
 * panthor_vm_create() - Create a VM
 * @ptdev: Device.
 * @for_mcu: True if this is the FW MCU VM.
 * @kernel_va_start: Start of the range reserved for kernel BO mapping.
 * @kernel_va_size: Size of the range reserved for kernel BO mapping.
 * @auto_kernel_va_start: Start of the auto-VA kernel range.
 * @auto_kernel_va_size: Size of the auto-VA kernel range.
 *
 * Return: A valid pointer on success, an ERR_PTR() otherwise.
 */
struct panthor_vm *
panthor_vm_create(struct panthor_device *ptdev, bool for_mcu,
		  u64 kernel_va_start, u64 kernel_va_size,
		  u64 auto_kernel_va_start, u64 auto_kernel_va_size)
{
	u32 va_bits = GPU_MMU_FEATURES_VA_BITS(ptdev->gpu_info.mmu_features);
	u32 pa_bits = GPU_MMU_FEATURES_PA_BITS(ptdev->gpu_info.mmu_features);
	u64 full_va_range = 1ull << va_bits;
	struct drm_gem_object *dummy_gem;
	struct drm_gpu_scheduler *sched;
	struct io_pgtable_cfg pgtbl_cfg;
	u64 mair, min_va, va_range;
	struct panthor_vm *vm;
	int ret;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return ERR_PTR(-ENOMEM);

	/* We allocate a dummy GEM for the VM. */
	dummy_gem = drm_gpuvm_resv_object_alloc(&ptdev->base);
	if (!dummy_gem) {
		ret = -ENOMEM;
		goto err_free_vm;
	}

	mutex_init(&vm->heaps.lock);
	vm->for_mcu = for_mcu;
	vm->ptdev = ptdev;
	mutex_init(&vm->op_lock);

	if (for_mcu) {
		/* CSF MCU is a cortex M7, and can only address 4G */
		min_va = 0;
		va_range = SZ_4G;
	} else {
		min_va = 0;
		va_range = full_va_range;
	}

	mutex_init(&vm->mm_lock);
	drm_mm_init(&vm->mm, kernel_va_start, kernel_va_size);
	vm->kernel_auto_va.start = auto_kernel_va_start;
	vm->kernel_auto_va.end = vm->kernel_auto_va.start + auto_kernel_va_size - 1;

	INIT_LIST_HEAD(&vm->node);
	INIT_LIST_HEAD(&vm->as.lru_node);
	vm->as.id = -1;
	refcount_set(&vm->as.active_cnt, 0);

	pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= SZ_4K | SZ_2M,
		.ias		= va_bits,
		.oas		= pa_bits,
		.coherent_walk	= ptdev->coherent,
		.tlb		= &mmu_tlb_ops,
		.iommu_dev	= ptdev->base.dev,
		.alloc		= alloc_pt,
		.free		= free_pt,
	};

	vm->pgtbl_ops = alloc_io_pgtable_ops(ARM_64_LPAE_S1, &pgtbl_cfg, vm);
	if (!vm->pgtbl_ops) {
		ret = -EINVAL;
		goto err_mm_takedown;
	}

	/* Bind operations are synchronous for now, no timeout needed. */
	ret = drm_sched_init(&vm->sched, &panthor_vm_bind_ops, ptdev->mmu->vm.wq,
			     1, 1, 0,
			     MAX_SCHEDULE_TIMEOUT, NULL, NULL,
			     "panthor-vm-bind", ptdev->base.dev);
	if (ret)
		goto err_free_io_pgtable;

	sched = &vm->sched;
	ret = drm_sched_entity_init(&vm->entity, 0, &sched, 1, NULL);
	if (ret)
		goto err_sched_fini;

	mair = io_pgtable_ops_to_pgtable(vm->pgtbl_ops)->cfg.arm_lpae_s1_cfg.mair;
	vm->memattr = mair_to_memattr(mair, ptdev->coherent);

	mutex_lock(&ptdev->mmu->vm.lock);
	list_add_tail(&vm->node, &ptdev->mmu->vm.list);

	/* If a reset is in progress, stop the scheduler. */
	if (ptdev->mmu->vm.reset_in_progress)
		panthor_vm_stop(vm);
	mutex_unlock(&ptdev->mmu->vm.lock);

	/* We intentionally leave the reserved range to zero, because we want kernel VMAs
	 * to be handled the same way user VMAs are.
	 */
	drm_gpuvm_init(&vm->base, for_mcu ? "panthor-MCU-VM" : "panthor-GPU-VM",
		       DRM_GPUVM_RESV_PROTECTED, &ptdev->base, dummy_gem,
		       min_va, va_range, 0, 0, &panthor_gpuvm_ops);
	drm_gem_object_put(dummy_gem);
	return vm;

err_sched_fini:
	drm_sched_fini(&vm->sched);

err_free_io_pgtable:
	free_io_pgtable_ops(vm->pgtbl_ops);

err_mm_takedown:
	drm_mm_takedown(&vm->mm);
	drm_gem_object_put(dummy_gem);

err_free_vm:
	kfree(vm);
	return ERR_PTR(ret);
}

static int
panthor_vm_bind_prepare_op_ctx(struct drm_file *file,
			       struct panthor_vm *vm,
			       const struct drm_panthor_vm_bind_op *op,
			       struct panthor_vm_op_ctx *op_ctx)
{
	ssize_t vm_pgsz = panthor_vm_page_size(vm);
	struct drm_gem_object *gem;
	int ret;

	/* Aligned on page size. */
	if (!IS_ALIGNED(op->va | op->size, vm_pgsz))
		return -EINVAL;

	switch (op->flags & DRM_PANTHOR_VM_BIND_OP_TYPE_MASK) {
	case DRM_PANTHOR_VM_BIND_OP_TYPE_MAP:
		gem = drm_gem_object_lookup(file, op->bo_handle);
		ret = panthor_vm_prepare_map_op_ctx(op_ctx, vm,
						    gem ? to_panthor_bo(gem) : NULL,
						    op->bo_offset,
						    op->size,
						    op->va,
						    op->flags);
		drm_gem_object_put(gem);
		return ret;

	case DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP:
		if (op->flags & ~DRM_PANTHOR_VM_BIND_OP_TYPE_MASK)
			return -EINVAL;

		if (op->bo_handle || op->bo_offset)
			return -EINVAL;

		return panthor_vm_prepare_unmap_op_ctx(op_ctx, vm, op->va, op->size);

	case DRM_PANTHOR_VM_BIND_OP_TYPE_SYNC_ONLY:
		if (op->flags & ~DRM_PANTHOR_VM_BIND_OP_TYPE_MASK)
			return -EINVAL;

		if (op->bo_handle || op->bo_offset)
			return -EINVAL;

		if (op->va || op->size)
			return -EINVAL;

		if (!op->syncs.count)
			return -EINVAL;

		panthor_vm_prepare_sync_only_op_ctx(op_ctx, vm);
		return 0;

	default:
		return -EINVAL;
	}
}

static void panthor_vm_bind_job_cleanup_op_ctx_work(struct work_struct *work)
{
	struct panthor_vm_bind_job *job =
		container_of(work, struct panthor_vm_bind_job, cleanup_op_ctx_work);

	panthor_vm_bind_job_put(&job->base);
}

/**
 * panthor_vm_bind_job_create() - Create a VM_BIND job
 * @file: File.
 * @vm: VM targeted by the VM_BIND job.
 * @op: VM operation data.
 *
 * Return: A valid pointer on success, an ERR_PTR() otherwise.
 */
struct drm_sched_job *
panthor_vm_bind_job_create(struct drm_file *file,
			   struct panthor_vm *vm,
			   const struct drm_panthor_vm_bind_op *op)
{
	struct panthor_vm_bind_job *job;
	int ret;

	if (!vm)
		return ERR_PTR(-EINVAL);

	if (vm->destroyed || vm->unusable)
		return ERR_PTR(-EINVAL);

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return ERR_PTR(-ENOMEM);

	ret = panthor_vm_bind_prepare_op_ctx(file, vm, op, &job->ctx);
	if (ret) {
		kfree(job);
		return ERR_PTR(ret);
	}

	INIT_WORK(&job->cleanup_op_ctx_work, panthor_vm_bind_job_cleanup_op_ctx_work);
	kref_init(&job->refcount);
	job->vm = panthor_vm_get(vm);

	ret = drm_sched_job_init(&job->base, &vm->entity, 1, vm);
	if (ret)
		goto err_put_job;

	return &job->base;

err_put_job:
	panthor_vm_bind_job_put(&job->base);
	return ERR_PTR(ret);
}

/**
 * panthor_vm_bind_job_prepare_resvs() - Prepare VM_BIND job dma_resvs
 * @exec: The locking/preparation context.
 * @sched_job: The job to prepare resvs on.
 *
 * Locks and prepare the VM resv.
 *
 * If this is a map operation, locks and prepares the GEM resv.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_vm_bind_job_prepare_resvs(struct drm_exec *exec,
				      struct drm_sched_job *sched_job)
{
	struct panthor_vm_bind_job *job = container_of(sched_job, struct panthor_vm_bind_job, base);
	int ret;

	/* Acquire the VM lock an reserve a slot for this VM bind job. */
	ret = drm_gpuvm_prepare_vm(&job->vm->base, exec, 1);
	if (ret)
		return ret;

	if (job->ctx.map.vm_bo) {
		/* Lock/prepare the GEM being mapped. */
		ret = drm_exec_prepare_obj(exec, job->ctx.map.vm_bo->obj, 1);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * panthor_vm_bind_job_update_resvs() - Update the resv objects touched by a job
 * @exec: drm_exec context.
 * @sched_job: Job to update the resvs on.
 */
void panthor_vm_bind_job_update_resvs(struct drm_exec *exec,
				      struct drm_sched_job *sched_job)
{
	struct panthor_vm_bind_job *job = container_of(sched_job, struct panthor_vm_bind_job, base);

	/* Explicit sync => we just register our job finished fence as bookkeep. */
	drm_gpuvm_resv_add_fence(&job->vm->base, exec,
				 &sched_job->s_fence->finished,
				 DMA_RESV_USAGE_BOOKKEEP,
				 DMA_RESV_USAGE_BOOKKEEP);
}

void panthor_vm_update_resvs(struct panthor_vm *vm, struct drm_exec *exec,
			     struct dma_fence *fence,
			     enum dma_resv_usage private_usage,
			     enum dma_resv_usage extobj_usage)
{
	drm_gpuvm_resv_add_fence(&vm->base, exec, fence, private_usage, extobj_usage);
}

/**
 * panthor_vm_bind_exec_sync_op() - Execute a VM_BIND operation synchronously.
 * @file: File.
 * @vm: VM targeted by the VM operation.
 * @op: Data describing the VM operation.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_vm_bind_exec_sync_op(struct drm_file *file,
				 struct panthor_vm *vm,
				 struct drm_panthor_vm_bind_op *op)
{
	struct panthor_vm_op_ctx op_ctx;
	int ret;

	/* No sync objects allowed on synchronous operations. */
	if (op->syncs.count)
		return -EINVAL;

	if (!op->size)
		return 0;

	ret = panthor_vm_bind_prepare_op_ctx(file, vm, op, &op_ctx);
	if (ret)
		return ret;

	ret = panthor_vm_exec_op(vm, &op_ctx, false);
	panthor_vm_cleanup_op_ctx(&op_ctx, vm);

	return ret;
}

/**
 * panthor_vm_map_bo_range() - Map a GEM object range to a VM
 * @vm: VM to map the GEM to.
 * @bo: GEM object to map.
 * @offset: Offset in the GEM object.
 * @size: Size to map.
 * @va: Virtual address to map the object to.
 * @flags: Combination of drm_panthor_vm_bind_op_flags flags.
 * Only map-related flags are valid.
 *
 * Internal use only. For userspace requests, use
 * panthor_vm_bind_exec_sync_op() instead.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_vm_map_bo_range(struct panthor_vm *vm, struct panthor_gem_object *bo,
			    u64 offset, u64 size, u64 va, u32 flags)
{
	struct panthor_vm_op_ctx op_ctx;
	int ret;

	ret = panthor_vm_prepare_map_op_ctx(&op_ctx, vm, bo, offset, size, va, flags);
	if (ret)
		return ret;

	ret = panthor_vm_exec_op(vm, &op_ctx, false);
	panthor_vm_cleanup_op_ctx(&op_ctx, vm);

	return ret;
}

/**
 * panthor_vm_unmap_range() - Unmap a portion of the VA space
 * @vm: VM to unmap the region from.
 * @va: Virtual address to unmap. Must be 4k aligned.
 * @size: Size of the region to unmap. Must be 4k aligned.
 *
 * Internal use only. For userspace requests, use
 * panthor_vm_bind_exec_sync_op() instead.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_vm_unmap_range(struct panthor_vm *vm, u64 va, u64 size)
{
	struct panthor_vm_op_ctx op_ctx;
	int ret;

	ret = panthor_vm_prepare_unmap_op_ctx(&op_ctx, vm, va, size);
	if (ret)
		return ret;

	ret = panthor_vm_exec_op(vm, &op_ctx, false);
	panthor_vm_cleanup_op_ctx(&op_ctx, vm);

	return ret;
}

/**
 * panthor_vm_prepare_mapped_bos_resvs() - Prepare resvs on VM BOs.
 * @exec: Locking/preparation context.
 * @vm: VM targeted by the GPU job.
 * @slot_count: Number of slots to reserve.
 *
 * GPU jobs assume all BOs bound to the VM at the time the job is submitted
 * are available when the job is executed. In order to guarantee that, we
 * need to reserve a slot on all BOs mapped to a VM and update this slot with
 * the job fence after its submission.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_vm_prepare_mapped_bos_resvs(struct drm_exec *exec, struct panthor_vm *vm,
					u32 slot_count)
{
	int ret;

	/* Acquire the VM lock and reserve a slot for this GPU job. */
	ret = drm_gpuvm_prepare_vm(&vm->base, exec, slot_count);
	if (ret)
		return ret;

	return drm_gpuvm_prepare_objects(&vm->base, exec, slot_count);
}

/**
 * panthor_mmu_unplug() - Unplug the MMU logic
 * @ptdev: Device.
 *
 * No access to the MMU regs should be done after this function is called.
 * We suspend the IRQ and disable all VMs to guarantee that.
 */
void panthor_mmu_unplug(struct panthor_device *ptdev)
{
	panthor_mmu_irq_suspend(&ptdev->mmu->irq);

	mutex_lock(&ptdev->mmu->as.slots_lock);
	for (u32 i = 0; i < ARRAY_SIZE(ptdev->mmu->as.slots); i++) {
		struct panthor_vm *vm = ptdev->mmu->as.slots[i].vm;

		if (vm) {
			drm_WARN_ON(&ptdev->base, panthor_mmu_as_disable(ptdev, i));
			panthor_vm_release_as_locked(vm);
		}
	}
	mutex_unlock(&ptdev->mmu->as.slots_lock);
}

static void panthor_mmu_release_wq(struct drm_device *ddev, void *res)
{
	destroy_workqueue(res);
}

/**
 * panthor_mmu_init() - Initialize the MMU logic.
 * @ptdev: Device.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_mmu_init(struct panthor_device *ptdev)
{
	u32 va_bits = GPU_MMU_FEATURES_VA_BITS(ptdev->gpu_info.mmu_features);
	struct panthor_mmu *mmu;
	int ret, irq;

	mmu = drmm_kzalloc(&ptdev->base, sizeof(*mmu), GFP_KERNEL);
	if (!mmu)
		return -ENOMEM;

	INIT_LIST_HEAD(&mmu->as.lru_list);

	ret = drmm_mutex_init(&ptdev->base, &mmu->as.slots_lock);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&mmu->vm.list);
	ret = drmm_mutex_init(&ptdev->base, &mmu->vm.lock);
	if (ret)
		return ret;

	ptdev->mmu = mmu;

	irq = platform_get_irq_byname(to_platform_device(ptdev->base.dev), "mmu");
	if (irq <= 0)
		return -ENODEV;

	ret = panthor_request_mmu_irq(ptdev, &mmu->irq, irq,
				      panthor_mmu_fault_mask(ptdev, ~0));
	if (ret)
		return ret;

	mmu->vm.wq = alloc_workqueue("panthor-vm-bind", WQ_UNBOUND, 0);
	if (!mmu->vm.wq)
		return -ENOMEM;

	/* On 32-bit kernels, the VA space is limited by the io_pgtable_ops abstraction,
	 * which passes iova as an unsigned long. Patch the mmu_features to reflect this
	 * limitation.
	 */
	if (va_bits > BITS_PER_LONG) {
		ptdev->gpu_info.mmu_features &= ~GENMASK(7, 0);
		ptdev->gpu_info.mmu_features |= BITS_PER_LONG;
	}

	return drmm_add_action_or_reset(&ptdev->base, panthor_mmu_release_wq, mmu->vm.wq);
}

#ifdef CONFIG_DEBUG_FS
static int show_vm_gpuvas(struct panthor_vm *vm, struct seq_file *m)
{
	int ret;

	mutex_lock(&vm->op_lock);
	ret = drm_debugfs_gpuva_info(m, &vm->base);
	mutex_unlock(&vm->op_lock);

	return ret;
}

static int show_each_vm(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *ddev = node->minor->dev;
	struct panthor_device *ptdev = container_of(ddev, struct panthor_device, base);
	int (*show)(struct panthor_vm *, struct seq_file *) = node->info_ent->data;
	struct panthor_vm *vm;
	int ret = 0;

	mutex_lock(&ptdev->mmu->vm.lock);
	list_for_each_entry(vm, &ptdev->mmu->vm.list, node) {
		ret = show(vm, m);
		if (ret < 0)
			break;

		seq_puts(m, "\n");
	}
	mutex_unlock(&ptdev->mmu->vm.lock);

	return ret;
}

static struct drm_info_list panthor_mmu_debugfs_list[] = {
	DRM_DEBUGFS_GPUVA_INFO(show_each_vm, show_vm_gpuvas),
};

/**
 * panthor_mmu_debugfs_init() - Initialize MMU debugfs entries
 * @minor: Minor.
 */
void panthor_mmu_debugfs_init(struct drm_minor *minor)
{
	drm_debugfs_create_files(panthor_mmu_debugfs_list,
				 ARRAY_SIZE(panthor_mmu_debugfs_list),
				 minor->debugfs_root, minor);
}
#endif /* CONFIG_DEBUG_FS */

/**
 * panthor_mmu_pt_cache_init() - Initialize the page table cache.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int panthor_mmu_pt_cache_init(void)
{
	pt_cache = kmem_cache_create("panthor-mmu-pt", SZ_4K, SZ_4K, 0, NULL);
	if (!pt_cache)
		return -ENOMEM;

	return 0;
}

/**
 * panthor_mmu_pt_cache_fini() - Destroy the page table cache.
 */
void panthor_mmu_pt_cache_fini(void)
{
	kmem_cache_destroy(pt_cache);
}

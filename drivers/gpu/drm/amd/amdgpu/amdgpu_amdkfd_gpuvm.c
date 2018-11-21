/*
 * Copyright 2014-2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#define pr_fmt(fmt) "kfd2kgd: " fmt

#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/sched/mm.h>
#include <linux/dma-buf.h>
#include <drm/drmP.h>
#include "amdgpu_object.h"
#include "amdgpu_vm.h"
#include "amdgpu_amdkfd.h"

/* Special VM and GART address alignment needed for VI pre-Fiji due to
 * a HW bug.
 */
#define VI_BO_SIZE_ALIGN (0x8000)

/* BO flag to indicate a KFD userptr BO */
#define AMDGPU_AMDKFD_USERPTR_BO (1ULL << 63)

/* Userptr restore delay, just long enough to allow consecutive VM
 * changes to accumulate
 */
#define AMDGPU_USERPTR_RESTORE_DELAY_MS 1

/* Impose limit on how much memory KFD can use */
static struct {
	uint64_t max_system_mem_limit;
	uint64_t max_ttm_mem_limit;
	int64_t system_mem_used;
	int64_t ttm_mem_used;
	spinlock_t mem_limit_lock;
} kfd_mem_limit;

/* Struct used for amdgpu_amdkfd_bo_validate */
struct amdgpu_vm_parser {
	uint32_t        domain;
	bool            wait;
};

static const char * const domain_bit_to_string[] = {
		"CPU",
		"GTT",
		"VRAM",
		"GDS",
		"GWS",
		"OA"
};

#define domain_string(domain) domain_bit_to_string[ffs(domain)-1]

static void amdgpu_amdkfd_restore_userptr_worker(struct work_struct *work);


static inline struct amdgpu_device *get_amdgpu_device(struct kgd_dev *kgd)
{
	return (struct amdgpu_device *)kgd;
}

static bool check_if_add_bo_to_vm(struct amdgpu_vm *avm,
		struct kgd_mem *mem)
{
	struct kfd_bo_va_list *entry;

	list_for_each_entry(entry, &mem->bo_va_list, bo_list)
		if (entry->bo_va->base.vm == avm)
			return false;

	return true;
}

/* Set memory usage limits. Current, limits are
 *  System (TTM + userptr) memory - 3/4th System RAM
 *  TTM memory - 3/8th System RAM
 */
void amdgpu_amdkfd_gpuvm_init_mem_limits(void)
{
	struct sysinfo si;
	uint64_t mem;

	si_meminfo(&si);
	mem = si.totalram - si.totalhigh;
	mem *= si.mem_unit;

	spin_lock_init(&kfd_mem_limit.mem_limit_lock);
	kfd_mem_limit.max_system_mem_limit = (mem >> 1) + (mem >> 2);
	kfd_mem_limit.max_ttm_mem_limit = (mem >> 1) - (mem >> 3);
	pr_debug("Kernel memory limit %lluM, TTM limit %lluM\n",
		(kfd_mem_limit.max_system_mem_limit >> 20),
		(kfd_mem_limit.max_ttm_mem_limit >> 20));
}

static int amdgpu_amdkfd_reserve_mem_limit(struct amdgpu_device *adev,
		uint64_t size, u32 domain, bool sg)
{
	size_t acc_size, system_mem_needed, ttm_mem_needed, vram_needed;
	uint64_t reserved_for_pt = amdgpu_amdkfd_total_mem_size >> 9;
	int ret = 0;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	vram_needed = 0;
	if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		/* TTM GTT memory */
		system_mem_needed = acc_size + size;
		ttm_mem_needed = acc_size + size;
	} else if (domain == AMDGPU_GEM_DOMAIN_CPU && !sg) {
		/* Userptr */
		system_mem_needed = acc_size + size;
		ttm_mem_needed = acc_size;
	} else {
		/* VRAM and SG */
		system_mem_needed = acc_size;
		ttm_mem_needed = acc_size;
		if (domain == AMDGPU_GEM_DOMAIN_VRAM)
			vram_needed = size;
	}

	spin_lock(&kfd_mem_limit.mem_limit_lock);

	if ((kfd_mem_limit.system_mem_used + system_mem_needed >
	     kfd_mem_limit.max_system_mem_limit) ||
	    (kfd_mem_limit.ttm_mem_used + ttm_mem_needed >
	     kfd_mem_limit.max_ttm_mem_limit) ||
	    (adev->kfd.vram_used + vram_needed >
	     adev->gmc.real_vram_size - reserved_for_pt)) {
		ret = -ENOMEM;
	} else {
		kfd_mem_limit.system_mem_used += system_mem_needed;
		kfd_mem_limit.ttm_mem_used += ttm_mem_needed;
		adev->kfd.vram_used += vram_needed;
	}

	spin_unlock(&kfd_mem_limit.mem_limit_lock);
	return ret;
}

static void unreserve_mem_limit(struct amdgpu_device *adev,
		uint64_t size, u32 domain, bool sg)
{
	size_t acc_size;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		kfd_mem_limit.system_mem_used -= (acc_size + size);
		kfd_mem_limit.ttm_mem_used -= (acc_size + size);
	} else if (domain == AMDGPU_GEM_DOMAIN_CPU && !sg) {
		kfd_mem_limit.system_mem_used -= (acc_size + size);
		kfd_mem_limit.ttm_mem_used -= acc_size;
	} else {
		kfd_mem_limit.system_mem_used -= acc_size;
		kfd_mem_limit.ttm_mem_used -= acc_size;
		if (domain == AMDGPU_GEM_DOMAIN_VRAM) {
			adev->kfd.vram_used -= size;
			WARN_ONCE(adev->kfd.vram_used < 0,
				  "kfd VRAM memory accounting unbalanced");
		}
	}
	WARN_ONCE(kfd_mem_limit.system_mem_used < 0,
		  "kfd system memory accounting unbalanced");
	WARN_ONCE(kfd_mem_limit.ttm_mem_used < 0,
		  "kfd TTM memory accounting unbalanced");

	spin_unlock(&kfd_mem_limit.mem_limit_lock);
}

void amdgpu_amdkfd_unreserve_memory_limit(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	u32 domain = bo->preferred_domains;
	bool sg = (bo->preferred_domains == AMDGPU_GEM_DOMAIN_CPU);

	if (bo->flags & AMDGPU_AMDKFD_USERPTR_BO) {
		domain = AMDGPU_GEM_DOMAIN_CPU;
		sg = false;
	}

	unreserve_mem_limit(adev, amdgpu_bo_size(bo), domain, sg);
}


/* amdgpu_amdkfd_remove_eviction_fence - Removes eviction fence(s) from BO's
 *  reservation object.
 *
 * @bo: [IN] Remove eviction fence(s) from this BO
 * @ef: [IN] If ef is specified, then this eviction fence is removed if it
 *  is present in the shared list.
 * @ef_list: [OUT] Returns list of eviction fences. These fences are removed
 *  from BO's reservation object shared list.
 * @ef_count: [OUT] Number of fences in ef_list.
 *
 * NOTE: If called with ef_list, then amdgpu_amdkfd_add_eviction_fence must be
 *  called to restore the eviction fences and to avoid memory leak. This is
 *  useful for shared BOs.
 * NOTE: Must be called with BO reserved i.e. bo->tbo.resv->lock held.
 */
static int amdgpu_amdkfd_remove_eviction_fence(struct amdgpu_bo *bo,
					struct amdgpu_amdkfd_fence *ef,
					struct amdgpu_amdkfd_fence ***ef_list,
					unsigned int *ef_count)
{
	struct reservation_object *resv = bo->tbo.resv;
	struct reservation_object_list *old, *new;
	unsigned int i, j, k;

	if (!ef && !ef_list)
		return -EINVAL;

	if (ef_list) {
		*ef_list = NULL;
		*ef_count = 0;
	}

	old = reservation_object_get_list(resv);
	if (!old)
		return 0;

	new = kmalloc(offsetof(typeof(*new), shared[old->shared_max]),
		      GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	/* Go through all the shared fences in the resevation object and sort
	 * the interesting ones to the end of the list.
	 */
	for (i = 0, j = old->shared_count, k = 0; i < old->shared_count; ++i) {
		struct dma_fence *f;

		f = rcu_dereference_protected(old->shared[i],
					      reservation_object_held(resv));

		if ((ef && f->context == ef->base.context) ||
		    (!ef && to_amdgpu_amdkfd_fence(f)))
			RCU_INIT_POINTER(new->shared[--j], f);
		else
			RCU_INIT_POINTER(new->shared[k++], f);
	}
	new->shared_max = old->shared_max;
	new->shared_count = k;

	if (!ef) {
		unsigned int count = old->shared_count - j;

		/* Alloc memory for count number of eviction fence pointers.
		 * Fill the ef_list array and ef_count
		 */
		*ef_list = kcalloc(count, sizeof(**ef_list), GFP_KERNEL);
		*ef_count = count;

		if (!*ef_list) {
			kfree(new);
			return -ENOMEM;
		}
	}

	/* Install the new fence list, seqcount provides the barriers */
	preempt_disable();
	write_seqcount_begin(&resv->seq);
	RCU_INIT_POINTER(resv->fence, new);
	write_seqcount_end(&resv->seq);
	preempt_enable();

	/* Drop the references to the removed fences or move them to ef_list */
	for (i = j, k = 0; i < old->shared_count; ++i) {
		struct dma_fence *f;

		f = rcu_dereference_protected(new->shared[i],
					      reservation_object_held(resv));
		if (!ef)
			(*ef_list)[k++] = to_amdgpu_amdkfd_fence(f);
		else
			dma_fence_put(f);
	}
	kfree_rcu(old, rcu);

	return 0;
}

/* amdgpu_amdkfd_add_eviction_fence - Adds eviction fence(s) back into BO's
 *  reservation object.
 *
 * @bo: [IN] Add eviction fences to this BO
 * @ef_list: [IN] List of eviction fences to be added
 * @ef_count: [IN] Number of fences in ef_list.
 *
 * NOTE: Must call amdgpu_amdkfd_remove_eviction_fence before calling this
 *  function.
 */
static void amdgpu_amdkfd_add_eviction_fence(struct amdgpu_bo *bo,
				struct amdgpu_amdkfd_fence **ef_list,
				unsigned int ef_count)
{
	int i;

	if (!ef_list || !ef_count)
		return;

	for (i = 0; i < ef_count; i++) {
		amdgpu_bo_fence(bo, &ef_list[i]->base, true);
		/* Re-adding the fence takes an additional reference. Drop that
		 * reference.
		 */
		dma_fence_put(&ef_list[i]->base);
	}

	kfree(ef_list);
}

static int amdgpu_amdkfd_bo_validate(struct amdgpu_bo *bo, uint32_t domain,
				     bool wait)
{
	struct ttm_operation_ctx ctx = { false, false };
	int ret;

	if (WARN(amdgpu_ttm_tt_get_usermm(bo->tbo.ttm),
		 "Called with userptr BO"))
		return -EINVAL;

	amdgpu_bo_placement_from_domain(bo, domain);

	ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (ret)
		goto validate_fail;
	if (wait) {
		struct amdgpu_amdkfd_fence **ef_list;
		unsigned int ef_count;

		ret = amdgpu_amdkfd_remove_eviction_fence(bo, NULL, &ef_list,
							  &ef_count);
		if (ret)
			goto validate_fail;

		ttm_bo_wait(&bo->tbo, false, false);
		amdgpu_amdkfd_add_eviction_fence(bo, ef_list, ef_count);
	}

validate_fail:
	return ret;
}

static int amdgpu_amdkfd_validate(void *param, struct amdgpu_bo *bo)
{
	struct amdgpu_vm_parser *p = param;

	return amdgpu_amdkfd_bo_validate(bo, p->domain, p->wait);
}

/* vm_validate_pt_pd_bos - Validate page table and directory BOs
 *
 * Page directories are not updated here because huge page handling
 * during page table updates can invalidate page directory entries
 * again. Page directories are only updated after updating page
 * tables.
 */
static int vm_validate_pt_pd_bos(struct amdgpu_vm *vm)
{
	struct amdgpu_bo *pd = vm->root.base.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);
	struct amdgpu_vm_parser param;
	int ret;

	param.domain = AMDGPU_GEM_DOMAIN_VRAM;
	param.wait = false;

	ret = amdgpu_vm_validate_pt_bos(adev, vm, amdgpu_amdkfd_validate,
					&param);
	if (ret) {
		pr_err("amdgpu: failed to validate PT BOs\n");
		return ret;
	}

	ret = amdgpu_amdkfd_validate(&param, pd);
	if (ret) {
		pr_err("amdgpu: failed to validate PD\n");
		return ret;
	}

	vm->pd_phys_addr = amdgpu_gmc_pd_addr(vm->root.base.bo);

	if (vm->use_cpu_for_update) {
		ret = amdgpu_bo_kmap(pd, NULL);
		if (ret) {
			pr_err("amdgpu: failed to kmap PD, ret=%d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int vm_update_pds(struct amdgpu_vm *vm, struct amdgpu_sync *sync)
{
	struct amdgpu_bo *pd = vm->root.base.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);
	int ret;

	ret = amdgpu_vm_update_directories(adev, vm);
	if (ret)
		return ret;

	return amdgpu_sync_fence(NULL, sync, vm->last_update, false);
}

/* add_bo_to_vm - Add a BO to a VM
 *
 * Everything that needs to bo done only once when a BO is first added
 * to a VM. It can later be mapped and unmapped many times without
 * repeating these steps.
 *
 * 1. Allocate and initialize BO VA entry data structure
 * 2. Add BO to the VM
 * 3. Determine ASIC-specific PTE flags
 * 4. Alloc page tables and directories if needed
 * 4a.  Validate new page tables and directories
 */
static int add_bo_to_vm(struct amdgpu_device *adev, struct kgd_mem *mem,
		struct amdgpu_vm *vm, bool is_aql,
		struct kfd_bo_va_list **p_bo_va_entry)
{
	int ret;
	struct kfd_bo_va_list *bo_va_entry;
	struct amdgpu_bo *pd = vm->root.base.bo;
	struct amdgpu_bo *bo = mem->bo;
	uint64_t va = mem->va;
	struct list_head *list_bo_va = &mem->bo_va_list;
	unsigned long bo_size = bo->tbo.mem.size;

	if (!va) {
		pr_err("Invalid VA when adding BO to VM\n");
		return -EINVAL;
	}

	if (is_aql)
		va += bo_size;

	bo_va_entry = kzalloc(sizeof(*bo_va_entry), GFP_KERNEL);
	if (!bo_va_entry)
		return -ENOMEM;

	pr_debug("\t add VA 0x%llx - 0x%llx to vm %p\n", va,
			va + bo_size, vm);

	/* Add BO to VM internal data structures*/
	bo_va_entry->bo_va = amdgpu_vm_bo_add(adev, vm, bo);
	if (!bo_va_entry->bo_va) {
		ret = -EINVAL;
		pr_err("Failed to add BO object to VM. ret == %d\n",
				ret);
		goto err_vmadd;
	}

	bo_va_entry->va = va;
	bo_va_entry->pte_flags = amdgpu_gmc_get_pte_flags(adev,
							 mem->mapping_flags);
	bo_va_entry->kgd_dev = (void *)adev;
	list_add(&bo_va_entry->bo_list, list_bo_va);

	if (p_bo_va_entry)
		*p_bo_va_entry = bo_va_entry;

	/* Allocate new page tables if needed and validate
	 * them. Clearing of new page tables and validate need to wait
	 * on move fences. We don't want that to trigger the eviction
	 * fence, so remove it temporarily.
	 */
	amdgpu_amdkfd_remove_eviction_fence(pd,
					vm->process_info->eviction_fence,
					NULL, NULL);

	ret = amdgpu_vm_alloc_pts(adev, vm, va, amdgpu_bo_size(bo));
	if (ret) {
		pr_err("Failed to allocate pts, err=%d\n", ret);
		goto err_alloc_pts;
	}

	ret = vm_validate_pt_pd_bos(vm);
	if (ret) {
		pr_err("validate_pt_pd_bos() failed\n");
		goto err_alloc_pts;
	}

	/* Add the eviction fence back */
	amdgpu_bo_fence(pd, &vm->process_info->eviction_fence->base, true);

	return 0;

err_alloc_pts:
	amdgpu_bo_fence(pd, &vm->process_info->eviction_fence->base, true);
	amdgpu_vm_bo_rmv(adev, bo_va_entry->bo_va);
	list_del(&bo_va_entry->bo_list);
err_vmadd:
	kfree(bo_va_entry);
	return ret;
}

static void remove_bo_from_vm(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry, unsigned long size)
{
	pr_debug("\t remove VA 0x%llx - 0x%llx in entry %p\n",
			entry->va,
			entry->va + size, entry);
	amdgpu_vm_bo_rmv(adev, entry->bo_va);
	list_del(&entry->bo_list);
	kfree(entry);
}

static void add_kgd_mem_to_kfd_bo_list(struct kgd_mem *mem,
				struct amdkfd_process_info *process_info,
				bool userptr)
{
	struct ttm_validate_buffer *entry = &mem->validate_list;
	struct amdgpu_bo *bo = mem->bo;

	INIT_LIST_HEAD(&entry->head);
	entry->num_shared = 1;
	entry->bo = &bo->tbo;
	mutex_lock(&process_info->lock);
	if (userptr)
		list_add_tail(&entry->head, &process_info->userptr_valid_list);
	else
		list_add_tail(&entry->head, &process_info->kfd_bo_list);
	mutex_unlock(&process_info->lock);
}

/* Initializes user pages. It registers the MMU notifier and validates
 * the userptr BO in the GTT domain.
 *
 * The BO must already be on the userptr_valid_list. Otherwise an
 * eviction and restore may happen that leaves the new BO unmapped
 * with the user mode queues running.
 *
 * Takes the process_info->lock to protect against concurrent restore
 * workers.
 *
 * Returns 0 for success, negative errno for errors.
 */
static int init_user_pages(struct kgd_mem *mem, struct mm_struct *mm,
			   uint64_t user_addr)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	struct amdgpu_bo *bo = mem->bo;
	struct ttm_operation_ctx ctx = { true, false };
	int ret = 0;

	mutex_lock(&process_info->lock);

	ret = amdgpu_ttm_tt_set_userptr(bo->tbo.ttm, user_addr, 0);
	if (ret) {
		pr_err("%s: Failed to set userptr: %d\n", __func__, ret);
		goto out;
	}

	ret = amdgpu_mn_register(bo, user_addr);
	if (ret) {
		pr_err("%s: Failed to register MMU notifier: %d\n",
		       __func__, ret);
		goto out;
	}

	/* If no restore worker is running concurrently, user_pages
	 * should not be allocated
	 */
	WARN(mem->user_pages, "Leaking user_pages array");

	mem->user_pages = kvmalloc_array(bo->tbo.ttm->num_pages,
					   sizeof(struct page *),
					   GFP_KERNEL | __GFP_ZERO);
	if (!mem->user_pages) {
		pr_err("%s: Failed to allocate pages array\n", __func__);
		ret = -ENOMEM;
		goto unregister_out;
	}

	ret = amdgpu_ttm_tt_get_user_pages(bo->tbo.ttm, mem->user_pages);
	if (ret) {
		pr_err("%s: Failed to get user pages: %d\n", __func__, ret);
		goto free_out;
	}

	amdgpu_ttm_tt_set_user_pages(bo->tbo.ttm, mem->user_pages);

	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		pr_err("%s: Failed to reserve BO\n", __func__);
		goto release_out;
	}
	amdgpu_bo_placement_from_domain(bo, mem->domain);
	ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (ret)
		pr_err("%s: failed to validate BO\n", __func__);
	amdgpu_bo_unreserve(bo);

release_out:
	if (ret)
		release_pages(mem->user_pages, bo->tbo.ttm->num_pages);
free_out:
	kvfree(mem->user_pages);
	mem->user_pages = NULL;
unregister_out:
	if (ret)
		amdgpu_mn_unregister(bo);
out:
	mutex_unlock(&process_info->lock);
	return ret;
}

/* Reserving a BO and its page table BOs must happen atomically to
 * avoid deadlocks. Some operations update multiple VMs at once. Track
 * all the reservation info in a context structure. Optionally a sync
 * object can track VM updates.
 */
struct bo_vm_reservation_context {
	struct amdgpu_bo_list_entry kfd_bo; /* BO list entry for the KFD BO */
	unsigned int n_vms;		    /* Number of VMs reserved	    */
	struct amdgpu_bo_list_entry *vm_pd; /* Array of VM BO list entries  */
	struct ww_acquire_ctx ticket;	    /* Reservation ticket	    */
	struct list_head list, duplicates;  /* BO lists			    */
	struct amdgpu_sync *sync;	    /* Pointer to sync object	    */
	bool reserved;			    /* Whether BOs are reserved	    */
};

enum bo_vm_match {
	BO_VM_NOT_MAPPED = 0,	/* Match VMs where a BO is not mapped */
	BO_VM_MAPPED,		/* Match VMs where a BO is mapped     */
	BO_VM_ALL,		/* Match all VMs a BO was added to    */
};

/**
 * reserve_bo_and_vm - reserve a BO and a VM unconditionally.
 * @mem: KFD BO structure.
 * @vm: the VM to reserve.
 * @ctx: the struct that will be used in unreserve_bo_and_vms().
 */
static int reserve_bo_and_vm(struct kgd_mem *mem,
			      struct amdgpu_vm *vm,
			      struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo = mem->bo;
	int ret;

	WARN_ON(!vm);

	ctx->reserved = false;
	ctx->n_vms = 1;
	ctx->sync = &mem->sync;

	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->duplicates);

	ctx->vm_pd = kcalloc(ctx->n_vms, sizeof(*ctx->vm_pd), GFP_KERNEL);
	if (!ctx->vm_pd)
		return -ENOMEM;

	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.num_shared = 1;
	ctx->kfd_bo.user_pages = NULL;
	list_add(&ctx->kfd_bo.tv.head, &ctx->list);

	amdgpu_vm_get_pd_bo(vm, &ctx->list, &ctx->vm_pd[0]);

	ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
				     false, &ctx->duplicates);
	if (!ret)
		ctx->reserved = true;
	else {
		pr_err("Failed to reserve buffers in ttm\n");
		kfree(ctx->vm_pd);
		ctx->vm_pd = NULL;
	}

	return ret;
}

/**
 * reserve_bo_and_cond_vms - reserve a BO and some VMs conditionally
 * @mem: KFD BO structure.
 * @vm: the VM to reserve. If NULL, then all VMs associated with the BO
 * is used. Otherwise, a single VM associated with the BO.
 * @map_type: the mapping status that will be used to filter the VMs.
 * @ctx: the struct that will be used in unreserve_bo_and_vms().
 *
 * Returns 0 for success, negative for failure.
 */
static int reserve_bo_and_cond_vms(struct kgd_mem *mem,
				struct amdgpu_vm *vm, enum bo_vm_match map_type,
				struct bo_vm_reservation_context *ctx)
{
	struct amdgpu_bo *bo = mem->bo;
	struct kfd_bo_va_list *entry;
	unsigned int i;
	int ret;

	ctx->reserved = false;
	ctx->n_vms = 0;
	ctx->vm_pd = NULL;
	ctx->sync = &mem->sync;

	INIT_LIST_HEAD(&ctx->list);
	INIT_LIST_HEAD(&ctx->duplicates);

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->base.vm) ||
			(entry->is_mapped != map_type
			&& map_type != BO_VM_ALL))
			continue;

		ctx->n_vms++;
	}

	if (ctx->n_vms != 0) {
		ctx->vm_pd = kcalloc(ctx->n_vms, sizeof(*ctx->vm_pd),
				     GFP_KERNEL);
		if (!ctx->vm_pd)
			return -ENOMEM;
	}

	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.num_shared = 1;
	ctx->kfd_bo.user_pages = NULL;
	list_add(&ctx->kfd_bo.tv.head, &ctx->list);

	i = 0;
	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if ((vm && vm != entry->bo_va->base.vm) ||
			(entry->is_mapped != map_type
			&& map_type != BO_VM_ALL))
			continue;

		amdgpu_vm_get_pd_bo(entry->bo_va->base.vm, &ctx->list,
				&ctx->vm_pd[i]);
		i++;
	}

	ret = ttm_eu_reserve_buffers(&ctx->ticket, &ctx->list,
				     false, &ctx->duplicates);
	if (!ret)
		ctx->reserved = true;
	else
		pr_err("Failed to reserve buffers in ttm.\n");

	if (ret) {
		kfree(ctx->vm_pd);
		ctx->vm_pd = NULL;
	}

	return ret;
}

/**
 * unreserve_bo_and_vms - Unreserve BO and VMs from a reservation context
 * @ctx: Reservation context to unreserve
 * @wait: Optionally wait for a sync object representing pending VM updates
 * @intr: Whether the wait is interruptible
 *
 * Also frees any resources allocated in
 * reserve_bo_and_(cond_)vm(s). Returns the status from
 * amdgpu_sync_wait.
 */
static int unreserve_bo_and_vms(struct bo_vm_reservation_context *ctx,
				 bool wait, bool intr)
{
	int ret = 0;

	if (wait)
		ret = amdgpu_sync_wait(ctx->sync, intr);

	if (ctx->reserved)
		ttm_eu_backoff_reservation(&ctx->ticket, &ctx->list);
	kfree(ctx->vm_pd);

	ctx->sync = NULL;

	ctx->reserved = false;
	ctx->vm_pd = NULL;

	return ret;
}

static int unmap_bo_from_gpuvm(struct amdgpu_device *adev,
				struct kfd_bo_va_list *entry,
				struct amdgpu_sync *sync)
{
	struct amdgpu_bo_va *bo_va = entry->bo_va;
	struct amdgpu_vm *vm = bo_va->base.vm;
	struct amdgpu_bo *pd = vm->root.base.bo;

	/* Remove eviction fence from PD (and thereby from PTs too as
	 * they share the resv. object). Otherwise during PT update
	 * job (see amdgpu_vm_bo_update_mapping), eviction fence would
	 * get added to job->sync object and job execution would
	 * trigger the eviction fence.
	 */
	amdgpu_amdkfd_remove_eviction_fence(pd,
					    vm->process_info->eviction_fence,
					    NULL, NULL);
	amdgpu_vm_bo_unmap(adev, bo_va, entry->va);

	amdgpu_vm_clear_freed(adev, vm, &bo_va->last_pt_update);

	/* Add the eviction fence back */
	amdgpu_bo_fence(pd, &vm->process_info->eviction_fence->base, true);

	amdgpu_sync_fence(NULL, sync, bo_va->last_pt_update, false);

	return 0;
}

static int update_gpuvm_pte(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry,
		struct amdgpu_sync *sync)
{
	int ret;
	struct amdgpu_vm *vm;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;

	bo_va = entry->bo_va;
	vm = bo_va->base.vm;
	bo = bo_va->base.bo;

	/* Update the page tables  */
	ret = amdgpu_vm_bo_update(adev, bo_va, false);
	if (ret) {
		pr_err("amdgpu_vm_bo_update failed\n");
		return ret;
	}

	return amdgpu_sync_fence(NULL, sync, bo_va->last_pt_update, false);
}

static int map_bo_to_gpuvm(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry, struct amdgpu_sync *sync,
		bool no_update_pte)
{
	int ret;

	/* Set virtual address for the allocation */
	ret = amdgpu_vm_bo_map(adev, entry->bo_va, entry->va, 0,
			       amdgpu_bo_size(entry->bo_va->base.bo),
			       entry->pte_flags);
	if (ret) {
		pr_err("Failed to map VA 0x%llx in vm. ret %d\n",
				entry->va, ret);
		return ret;
	}

	if (no_update_pte)
		return 0;

	ret = update_gpuvm_pte(adev, entry, sync);
	if (ret) {
		pr_err("update_gpuvm_pte() failed\n");
		goto update_gpuvm_pte_failed;
	}

	return 0;

update_gpuvm_pte_failed:
	unmap_bo_from_gpuvm(adev, entry, sync);
	return ret;
}

static struct sg_table *create_doorbell_sg(uint64_t addr, uint32_t size)
{
	struct sg_table *sg = kmalloc(sizeof(*sg), GFP_KERNEL);

	if (!sg)
		return NULL;
	if (sg_alloc_table(sg, 1, GFP_KERNEL)) {
		kfree(sg);
		return NULL;
	}
	sg->sgl->dma_address = addr;
	sg->sgl->length = size;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
	sg->sgl->dma_length = size;
#endif
	return sg;
}

static int process_validate_vms(struct amdkfd_process_info *process_info)
{
	struct amdgpu_vm *peer_vm;
	int ret;

	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		ret = vm_validate_pt_pd_bos(peer_vm);
		if (ret)
			return ret;
	}

	return 0;
}

static int process_sync_pds_resv(struct amdkfd_process_info *process_info,
				 struct amdgpu_sync *sync)
{
	struct amdgpu_vm *peer_vm;
	int ret;

	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		struct amdgpu_bo *pd = peer_vm->root.base.bo;

		ret = amdgpu_sync_resv(NULL,
					sync, pd->tbo.resv,
					AMDGPU_FENCE_OWNER_UNDEFINED, false);
		if (ret)
			return ret;
	}

	return 0;
}

static int process_update_pds(struct amdkfd_process_info *process_info,
			      struct amdgpu_sync *sync)
{
	struct amdgpu_vm *peer_vm;
	int ret;

	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		ret = vm_update_pds(peer_vm, sync);
		if (ret)
			return ret;
	}

	return 0;
}

static int init_kfd_vm(struct amdgpu_vm *vm, void **process_info,
		       struct dma_fence **ef)
{
	struct amdkfd_process_info *info = NULL;
	int ret;

	if (!*process_info) {
		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info)
			return -ENOMEM;

		mutex_init(&info->lock);
		INIT_LIST_HEAD(&info->vm_list_head);
		INIT_LIST_HEAD(&info->kfd_bo_list);
		INIT_LIST_HEAD(&info->userptr_valid_list);
		INIT_LIST_HEAD(&info->userptr_inval_list);

		info->eviction_fence =
			amdgpu_amdkfd_fence_create(dma_fence_context_alloc(1),
						   current->mm);
		if (!info->eviction_fence) {
			pr_err("Failed to create eviction fence\n");
			ret = -ENOMEM;
			goto create_evict_fence_fail;
		}

		info->pid = get_task_pid(current->group_leader, PIDTYPE_PID);
		atomic_set(&info->evicted_bos, 0);
		INIT_DELAYED_WORK(&info->restore_userptr_work,
				  amdgpu_amdkfd_restore_userptr_worker);

		*process_info = info;
		*ef = dma_fence_get(&info->eviction_fence->base);
	}

	vm->process_info = *process_info;

	/* Validate page directory and attach eviction fence */
	ret = amdgpu_bo_reserve(vm->root.base.bo, true);
	if (ret)
		goto reserve_pd_fail;
	ret = vm_validate_pt_pd_bos(vm);
	if (ret) {
		pr_err("validate_pt_pd_bos() failed\n");
		goto validate_pd_fail;
	}
	ret = ttm_bo_wait(&vm->root.base.bo->tbo, false, false);
	if (ret)
		goto wait_pd_fail;
	amdgpu_bo_fence(vm->root.base.bo,
			&vm->process_info->eviction_fence->base, true);
	amdgpu_bo_unreserve(vm->root.base.bo);

	/* Update process info */
	mutex_lock(&vm->process_info->lock);
	list_add_tail(&vm->vm_list_node,
			&(vm->process_info->vm_list_head));
	vm->process_info->n_vms++;
	mutex_unlock(&vm->process_info->lock);

	return 0;

wait_pd_fail:
validate_pd_fail:
	amdgpu_bo_unreserve(vm->root.base.bo);
reserve_pd_fail:
	vm->process_info = NULL;
	if (info) {
		/* Two fence references: one in info and one in *ef */
		dma_fence_put(&info->eviction_fence->base);
		dma_fence_put(*ef);
		*ef = NULL;
		*process_info = NULL;
		put_pid(info->pid);
create_evict_fence_fail:
		mutex_destroy(&info->lock);
		kfree(info);
	}
	return ret;
}

int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, unsigned int pasid,
					  void **vm, void **process_info,
					  struct dma_fence **ef)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct amdgpu_vm *new_vm;
	int ret;

	new_vm = kzalloc(sizeof(*new_vm), GFP_KERNEL);
	if (!new_vm)
		return -ENOMEM;

	/* Initialize AMDGPU part of the VM */
	ret = amdgpu_vm_init(adev, new_vm, AMDGPU_VM_CONTEXT_COMPUTE, pasid);
	if (ret) {
		pr_err("Failed init vm ret %d\n", ret);
		goto amdgpu_vm_init_fail;
	}

	/* Initialize KFD part of the VM and process info */
	ret = init_kfd_vm(new_vm, process_info, ef);
	if (ret)
		goto init_kfd_vm_fail;

	*vm = (void *) new_vm;

	return 0;

init_kfd_vm_fail:
	amdgpu_vm_fini(adev, new_vm);
amdgpu_vm_init_fail:
	kfree(new_vm);
	return ret;
}

int amdgpu_amdkfd_gpuvm_acquire_process_vm(struct kgd_dev *kgd,
					   struct file *filp, unsigned int pasid,
					   void **vm, void **process_info,
					   struct dma_fence **ef)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct drm_file *drm_priv = filp->private_data;
	struct amdgpu_fpriv *drv_priv = drm_priv->driver_priv;
	struct amdgpu_vm *avm = &drv_priv->vm;
	int ret;

	/* Already a compute VM? */
	if (avm->process_info)
		return -EINVAL;

	/* Convert VM into a compute VM */
	ret = amdgpu_vm_make_compute(adev, avm, pasid);
	if (ret)
		return ret;

	/* Initialize KFD part of the VM and process info */
	ret = init_kfd_vm(avm, process_info, ef);
	if (ret)
		return ret;

	*vm = (void *)avm;

	return 0;
}

void amdgpu_amdkfd_gpuvm_destroy_cb(struct amdgpu_device *adev,
				    struct amdgpu_vm *vm)
{
	struct amdkfd_process_info *process_info = vm->process_info;
	struct amdgpu_bo *pd = vm->root.base.bo;

	if (!process_info)
		return;

	/* Release eviction fence from PD */
	amdgpu_bo_reserve(pd, false);
	amdgpu_bo_fence(pd, NULL, false);
	amdgpu_bo_unreserve(pd);

	/* Update process info */
	mutex_lock(&process_info->lock);
	process_info->n_vms--;
	list_del(&vm->vm_list_node);
	mutex_unlock(&process_info->lock);

	/* Release per-process resources when last compute VM is destroyed */
	if (!process_info->n_vms) {
		WARN_ON(!list_empty(&process_info->kfd_bo_list));
		WARN_ON(!list_empty(&process_info->userptr_valid_list));
		WARN_ON(!list_empty(&process_info->userptr_inval_list));

		dma_fence_put(&process_info->eviction_fence->base);
		cancel_delayed_work_sync(&process_info->restore_userptr_work);
		put_pid(process_info->pid);
		mutex_destroy(&process_info->lock);
		kfree(process_info);
	}
}

void amdgpu_amdkfd_gpuvm_destroy_process_vm(struct kgd_dev *kgd, void *vm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;

	if (WARN_ON(!kgd || !vm))
		return;

	pr_debug("Destroying process vm %p\n", vm);

	/* Release the VM context */
	amdgpu_vm_fini(adev, avm);
	kfree(vm);
}

void amdgpu_amdkfd_gpuvm_release_process_vm(struct kgd_dev *kgd, void *vm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
        struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;

	if (WARN_ON(!kgd || !vm))
                return;

        pr_debug("Releasing process vm %p\n", vm);

        /* The original pasid of amdgpu vm has already been
         * released during making a amdgpu vm to a compute vm
         * The current pasid is managed by kfd and will be
         * released on kfd process destroy. Set amdgpu pasid
         * to 0 to avoid duplicate release.
         */
	amdgpu_vm_release_compute(adev, avm);
}

uint64_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm)
{
	struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;
	struct amdgpu_bo *pd = avm->root.base.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);

	if (adev->asic_type < CHIP_VEGA10)
		return avm->pd_phys_addr >> AMDGPU_GPU_PAGE_SHIFT;
	return avm->pd_phys_addr;
}

int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, uint32_t flags)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct sg_table *sg = NULL;
	uint64_t user_addr = 0;
	struct amdgpu_bo *bo;
	struct amdgpu_bo_param bp;
	int byte_align;
	u32 domain, alloc_domain;
	u64 alloc_flags;
	uint32_t mapping_flags;
	int ret;

	/*
	 * Check on which domain to allocate BO
	 */
	if (flags & ALLOC_MEM_FLAGS_VRAM) {
		domain = alloc_domain = AMDGPU_GEM_DOMAIN_VRAM;
		alloc_flags = AMDGPU_GEM_CREATE_VRAM_CLEARED;
		alloc_flags |= (flags & ALLOC_MEM_FLAGS_PUBLIC) ?
			AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED :
			AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	} else if (flags & ALLOC_MEM_FLAGS_GTT) {
		domain = alloc_domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_flags = 0;
	} else if (flags & ALLOC_MEM_FLAGS_USERPTR) {
		domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_domain = AMDGPU_GEM_DOMAIN_CPU;
		alloc_flags = 0;
		if (!offset || !*offset)
			return -EINVAL;
		user_addr = *offset;
	} else if (flags & ALLOC_MEM_FLAGS_DOORBELL) {
		domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_domain = AMDGPU_GEM_DOMAIN_CPU;
		bo_type = ttm_bo_type_sg;
		alloc_flags = 0;
		if (size > UINT_MAX)
			return -EINVAL;
		sg = create_doorbell_sg(*offset, size);
		if (!sg)
			return -ENOMEM;
	} else {
		return -EINVAL;
	}

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (!*mem) {
		ret = -ENOMEM;
		goto err;
	}
	INIT_LIST_HEAD(&(*mem)->bo_va_list);
	mutex_init(&(*mem)->lock);
	(*mem)->aql_queue = !!(flags & ALLOC_MEM_FLAGS_AQL_QUEUE_MEM);

	/* Workaround for AQL queue wraparound bug. Map the same
	 * memory twice. That means we only actually allocate half
	 * the memory.
	 */
	if ((*mem)->aql_queue)
		size = size >> 1;

	/* Workaround for TLB bug on older VI chips */
	byte_align = (adev->family == AMDGPU_FAMILY_VI &&
			adev->asic_type != CHIP_FIJI &&
			adev->asic_type != CHIP_POLARIS10 &&
			adev->asic_type != CHIP_POLARIS11 &&
			adev->asic_type != CHIP_POLARIS12) ?
			VI_BO_SIZE_ALIGN : 1;

	mapping_flags = AMDGPU_VM_PAGE_READABLE;
	if (flags & ALLOC_MEM_FLAGS_WRITABLE)
		mapping_flags |= AMDGPU_VM_PAGE_WRITEABLE;
	if (flags & ALLOC_MEM_FLAGS_EXECUTABLE)
		mapping_flags |= AMDGPU_VM_PAGE_EXECUTABLE;
	if (flags & ALLOC_MEM_FLAGS_COHERENT)
		mapping_flags |= AMDGPU_VM_MTYPE_UC;
	else
		mapping_flags |= AMDGPU_VM_MTYPE_NC;
	(*mem)->mapping_flags = mapping_flags;

	amdgpu_sync_create(&(*mem)->sync);

	ret = amdgpu_amdkfd_reserve_mem_limit(adev, size, alloc_domain, !!sg);
	if (ret) {
		pr_debug("Insufficient system memory\n");
		goto err_reserve_limit;
	}

	pr_debug("\tcreate BO VA 0x%llx size 0x%llx domain %s\n",
			va, size, domain_string(alloc_domain));

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = byte_align;
	bp.domain = alloc_domain;
	bp.flags = alloc_flags;
	bp.type = bo_type;
	bp.resv = NULL;
	ret = amdgpu_bo_create(adev, &bp, &bo);
	if (ret) {
		pr_debug("Failed to create BO on domain %s. ret %d\n",
				domain_string(alloc_domain), ret);
		goto err_bo_create;
	}
	if (bo_type == ttm_bo_type_sg) {
		bo->tbo.sg = sg;
		bo->tbo.ttm->sg = sg;
	}
	bo->kfd_bo = *mem;
	(*mem)->bo = bo;
	if (user_addr)
		bo->flags |= AMDGPU_AMDKFD_USERPTR_BO;

	(*mem)->va = va;
	(*mem)->domain = domain;
	(*mem)->mapped_to_gpu_memory = 0;
	(*mem)->process_info = avm->process_info;
	add_kgd_mem_to_kfd_bo_list(*mem, avm->process_info, user_addr);

	if (user_addr) {
		ret = init_user_pages(*mem, current->mm, user_addr);
		if (ret) {
			mutex_lock(&avm->process_info->lock);
			list_del(&(*mem)->validate_list.head);
			mutex_unlock(&avm->process_info->lock);
			goto allocate_init_user_pages_failed;
		}
	}

	if (offset)
		*offset = amdgpu_bo_mmap_offset(bo);

	return 0;

allocate_init_user_pages_failed:
	amdgpu_bo_unref(&bo);
	/* Don't unreserve system mem limit twice */
	goto err_reserve_limit;
err_bo_create:
	unreserve_mem_limit(adev, size, alloc_domain, !!sg);
err_reserve_limit:
	mutex_destroy(&(*mem)->lock);
	kfree(*mem);
err:
	if (sg) {
		sg_free_table(sg);
		kfree(sg);
	}
	return ret;
}

int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	unsigned long bo_size = mem->bo->tbo.mem.size;
	struct kfd_bo_va_list *entry, *tmp;
	struct bo_vm_reservation_context ctx;
	struct ttm_validate_buffer *bo_list_entry;
	int ret;

	mutex_lock(&mem->lock);

	if (mem->mapped_to_gpu_memory > 0) {
		pr_debug("BO VA 0x%llx size 0x%lx is still mapped.\n",
				mem->va, bo_size);
		mutex_unlock(&mem->lock);
		return -EBUSY;
	}

	mutex_unlock(&mem->lock);
	/* lock is not needed after this, since mem is unused and will
	 * be freed anyway
	 */

	/* No more MMU notifiers */
	amdgpu_mn_unregister(mem->bo);

	/* Make sure restore workers don't access the BO any more */
	bo_list_entry = &mem->validate_list;
	mutex_lock(&process_info->lock);
	list_del(&bo_list_entry->head);
	mutex_unlock(&process_info->lock);

	/* Free user pages if necessary */
	if (mem->user_pages) {
		pr_debug("%s: Freeing user_pages array\n", __func__);
		if (mem->user_pages[0])
			release_pages(mem->user_pages,
					mem->bo->tbo.ttm->num_pages);
		kvfree(mem->user_pages);
	}

	ret = reserve_bo_and_cond_vms(mem, NULL, BO_VM_ALL, &ctx);
	if (unlikely(ret))
		return ret;

	/* The eviction fence should be removed by the last unmap.
	 * TODO: Log an error condition if the bo still has the eviction fence
	 * attached
	 */
	amdgpu_amdkfd_remove_eviction_fence(mem->bo,
					process_info->eviction_fence,
					NULL, NULL);
	pr_debug("Release VA 0x%llx - 0x%llx\n", mem->va,
		mem->va + bo_size * (1 + mem->aql_queue));

	/* Remove from VM internal data structures */
	list_for_each_entry_safe(entry, tmp, &mem->bo_va_list, bo_list)
		remove_bo_from_vm((struct amdgpu_device *)entry->kgd_dev,
				entry, bo_size);

	ret = unreserve_bo_and_vms(&ctx, false, false);

	/* Free the sync object */
	amdgpu_sync_free(&mem->sync);

	/* If the SG is not NULL, it's one we created for a doorbell
	 * BO. We need to free it.
	 */
	if (mem->bo->tbo.sg) {
		sg_free_table(mem->bo->tbo.sg);
		kfree(mem->bo->tbo.sg);
	}

	/* Free the BO*/
	amdgpu_bo_unref(&mem->bo);
	mutex_destroy(&mem->lock);
	kfree(mem);

	return ret;
}

int amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;
	int ret;
	struct amdgpu_bo *bo;
	uint32_t domain;
	struct kfd_bo_va_list *entry;
	struct bo_vm_reservation_context ctx;
	struct kfd_bo_va_list *bo_va_entry = NULL;
	struct kfd_bo_va_list *bo_va_entry_aql = NULL;
	unsigned long bo_size;
	bool is_invalid_userptr = false;

	bo = mem->bo;
	if (!bo) {
		pr_err("Invalid BO when mapping memory to GPU\n");
		return -EINVAL;
	}

	/* Make sure restore is not running concurrently. Since we
	 * don't map invalid userptr BOs, we rely on the next restore
	 * worker to do the mapping
	 */
	mutex_lock(&mem->process_info->lock);

	/* Lock mmap-sem. If we find an invalid userptr BO, we can be
	 * sure that the MMU notifier is no longer running
	 * concurrently and the queues are actually stopped
	 */
	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		down_write(&current->mm->mmap_sem);
		is_invalid_userptr = atomic_read(&mem->invalid);
		up_write(&current->mm->mmap_sem);
	}

	mutex_lock(&mem->lock);

	domain = mem->domain;
	bo_size = bo->tbo.mem.size;

	pr_debug("Map VA 0x%llx - 0x%llx to vm %p domain %s\n",
			mem->va,
			mem->va + bo_size * (1 + mem->aql_queue),
			vm, domain_string(domain));

	ret = reserve_bo_and_vm(mem, vm, &ctx);
	if (unlikely(ret))
		goto out;

	/* Userptr can be marked as "not invalid", but not actually be
	 * validated yet (still in the system domain). In that case
	 * the queues are still stopped and we can leave mapping for
	 * the next restore worker
	 */
	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) &&
	    bo->tbo.mem.mem_type == TTM_PL_SYSTEM)
		is_invalid_userptr = true;

	if (check_if_add_bo_to_vm(avm, mem)) {
		ret = add_bo_to_vm(adev, mem, avm, false,
				&bo_va_entry);
		if (ret)
			goto add_bo_to_vm_failed;
		if (mem->aql_queue) {
			ret = add_bo_to_vm(adev, mem, avm,
					true, &bo_va_entry_aql);
			if (ret)
				goto add_bo_to_vm_failed_aql;
		}
	} else {
		ret = vm_validate_pt_pd_bos(avm);
		if (unlikely(ret))
			goto add_bo_to_vm_failed;
	}

	if (mem->mapped_to_gpu_memory == 0 &&
	    !amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		/* Validate BO only once. The eviction fence gets added to BO
		 * the first time it is mapped. Validate will wait for all
		 * background evictions to complete.
		 */
		ret = amdgpu_amdkfd_bo_validate(bo, domain, true);
		if (ret) {
			pr_debug("Validate failed\n");
			goto map_bo_to_gpuvm_failed;
		}
	}

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if (entry->bo_va->base.vm == vm && !entry->is_mapped) {
			pr_debug("\t map VA 0x%llx - 0x%llx in entry %p\n",
					entry->va, entry->va + bo_size,
					entry);

			ret = map_bo_to_gpuvm(adev, entry, ctx.sync,
					      is_invalid_userptr);
			if (ret) {
				pr_err("Failed to map radeon bo to gpuvm\n");
				goto map_bo_to_gpuvm_failed;
			}

			ret = vm_update_pds(vm, ctx.sync);
			if (ret) {
				pr_err("Failed to update page directories\n");
				goto map_bo_to_gpuvm_failed;
			}

			entry->is_mapped = true;
			mem->mapped_to_gpu_memory++;
			pr_debug("\t INC mapping count %d\n",
					mem->mapped_to_gpu_memory);
		}
	}

	if (!amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) && !bo->pin_count)
		amdgpu_bo_fence(bo,
				&avm->process_info->eviction_fence->base,
				true);
	ret = unreserve_bo_and_vms(&ctx, false, false);

	goto out;

map_bo_to_gpuvm_failed:
	if (bo_va_entry_aql)
		remove_bo_from_vm(adev, bo_va_entry_aql, bo_size);
add_bo_to_vm_failed_aql:
	if (bo_va_entry)
		remove_bo_from_vm(adev, bo_va_entry, bo_size);
add_bo_to_vm_failed:
	unreserve_bo_and_vms(&ctx, false, false);
out:
	mutex_unlock(&mem->process_info->lock);
	mutex_unlock(&mem->lock);
	return ret;
}

int amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
		struct kgd_dev *kgd, struct kgd_mem *mem, void *vm)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct amdkfd_process_info *process_info =
		((struct amdgpu_vm *)vm)->process_info;
	unsigned long bo_size = mem->bo->tbo.mem.size;
	struct kfd_bo_va_list *entry;
	struct bo_vm_reservation_context ctx;
	int ret;

	mutex_lock(&mem->lock);

	ret = reserve_bo_and_cond_vms(mem, vm, BO_VM_MAPPED, &ctx);
	if (unlikely(ret))
		goto out;
	/* If no VMs were reserved, it means the BO wasn't actually mapped */
	if (ctx.n_vms == 0) {
		ret = -EINVAL;
		goto unreserve_out;
	}

	ret = vm_validate_pt_pd_bos((struct amdgpu_vm *)vm);
	if (unlikely(ret))
		goto unreserve_out;

	pr_debug("Unmap VA 0x%llx - 0x%llx from vm %p\n",
		mem->va,
		mem->va + bo_size * (1 + mem->aql_queue),
		vm);

	list_for_each_entry(entry, &mem->bo_va_list, bo_list) {
		if (entry->bo_va->base.vm == vm && entry->is_mapped) {
			pr_debug("\t unmap VA 0x%llx - 0x%llx from entry %p\n",
					entry->va,
					entry->va + bo_size,
					entry);

			ret = unmap_bo_from_gpuvm(adev, entry, ctx.sync);
			if (ret == 0) {
				entry->is_mapped = false;
			} else {
				pr_err("failed to unmap VA 0x%llx\n",
						mem->va);
				goto unreserve_out;
			}

			mem->mapped_to_gpu_memory--;
			pr_debug("\t DEC mapping count %d\n",
					mem->mapped_to_gpu_memory);
		}
	}

	/* If BO is unmapped from all VMs, unfence it. It can be evicted if
	 * required.
	 */
	if (mem->mapped_to_gpu_memory == 0 &&
	    !amdgpu_ttm_tt_get_usermm(mem->bo->tbo.ttm) && !mem->bo->pin_count)
		amdgpu_amdkfd_remove_eviction_fence(mem->bo,
						process_info->eviction_fence,
						    NULL, NULL);

unreserve_out:
	unreserve_bo_and_vms(&ctx, false, false);
out:
	mutex_unlock(&mem->lock);
	return ret;
}

int amdgpu_amdkfd_gpuvm_sync_memory(
		struct kgd_dev *kgd, struct kgd_mem *mem, bool intr)
{
	struct amdgpu_sync sync;
	int ret;

	amdgpu_sync_create(&sync);

	mutex_lock(&mem->lock);
	amdgpu_sync_clone(&mem->sync, &sync);
	mutex_unlock(&mem->lock);

	ret = amdgpu_sync_wait(&sync, intr);
	amdgpu_sync_free(&sync);
	return ret;
}

int amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(struct kgd_dev *kgd,
		struct kgd_mem *mem, void **kptr, uint64_t *size)
{
	int ret;
	struct amdgpu_bo *bo = mem->bo;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		pr_err("userptr can't be mapped to kernel\n");
		return -EINVAL;
	}

	/* delete kgd_mem from kfd_bo_list to avoid re-validating
	 * this BO in BO's restoring after eviction.
	 */
	mutex_lock(&mem->process_info->lock);

	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		pr_err("Failed to reserve bo. ret %d\n", ret);
		goto bo_reserve_failed;
	}

	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT);
	if (ret) {
		pr_err("Failed to pin bo. ret %d\n", ret);
		goto pin_failed;
	}

	ret = amdgpu_bo_kmap(bo, kptr);
	if (ret) {
		pr_err("Failed to map bo to kernel. ret %d\n", ret);
		goto kmap_failed;
	}

	amdgpu_amdkfd_remove_eviction_fence(
		bo, mem->process_info->eviction_fence, NULL, NULL);
	list_del_init(&mem->validate_list.head);

	if (size)
		*size = amdgpu_bo_size(bo);

	amdgpu_bo_unreserve(bo);

	mutex_unlock(&mem->process_info->lock);
	return 0;

kmap_failed:
	amdgpu_bo_unpin(bo);
pin_failed:
	amdgpu_bo_unreserve(bo);
bo_reserve_failed:
	mutex_unlock(&mem->process_info->lock);

	return ret;
}

int amdgpu_amdkfd_gpuvm_get_vm_fault_info(struct kgd_dev *kgd,
					      struct kfd_vm_fault_info *mem)
{
	struct amdgpu_device *adev;

	adev = (struct amdgpu_device *)kgd;
	if (atomic_read(&adev->gmc.vm_fault_info_updated) == 1) {
		*mem = *adev->gmc.vm_fault_info;
		mb();
		atomic_set(&adev->gmc.vm_fault_info_updated, 0);
	}
	return 0;
}

int amdgpu_amdkfd_gpuvm_import_dmabuf(struct kgd_dev *kgd,
				      struct dma_buf *dma_buf,
				      uint64_t va, void *vm,
				      struct kgd_mem **mem, uint64_t *size,
				      uint64_t *mmap_offset)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)kgd;
	struct drm_gem_object *obj;
	struct amdgpu_bo *bo;
	struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;

	if (dma_buf->ops != &amdgpu_dmabuf_ops)
		/* Can't handle non-graphics buffers */
		return -EINVAL;

	obj = dma_buf->priv;
	if (obj->dev->dev_private != adev)
		/* Can't handle buffers from other devices */
		return -EINVAL;

	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->preferred_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		return -EINVAL;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (!*mem)
		return -ENOMEM;

	if (size)
		*size = amdgpu_bo_size(bo);

	if (mmap_offset)
		*mmap_offset = amdgpu_bo_mmap_offset(bo);

	INIT_LIST_HEAD(&(*mem)->bo_va_list);
	mutex_init(&(*mem)->lock);
	(*mem)->mapping_flags =
		AMDGPU_VM_PAGE_READABLE | AMDGPU_VM_PAGE_WRITEABLE |
		AMDGPU_VM_PAGE_EXECUTABLE | AMDGPU_VM_MTYPE_NC;

	(*mem)->bo = amdgpu_bo_ref(bo);
	(*mem)->va = va;
	(*mem)->domain = (bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
		AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT;
	(*mem)->mapped_to_gpu_memory = 0;
	(*mem)->process_info = avm->process_info;
	add_kgd_mem_to_kfd_bo_list(*mem, avm->process_info, false);
	amdgpu_sync_create(&(*mem)->sync);

	return 0;
}

/* Evict a userptr BO by stopping the queues if necessary
 *
 * Runs in MMU notifier, may be in RECLAIM_FS context. This means it
 * cannot do any memory allocations, and cannot take any locks that
 * are held elsewhere while allocating memory. Therefore this is as
 * simple as possible, using atomic counters.
 *
 * It doesn't do anything to the BO itself. The real work happens in
 * restore, where we get updated page addresses. This function only
 * ensures that GPU access to the BO is stopped.
 */
int amdgpu_amdkfd_evict_userptr(struct kgd_mem *mem,
				struct mm_struct *mm)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	int invalid, evicted_bos;
	int r = 0;

	invalid = atomic_inc_return(&mem->invalid);
	evicted_bos = atomic_inc_return(&process_info->evicted_bos);
	if (evicted_bos == 1) {
		/* First eviction, stop the queues */
		r = kgd2kfd->quiesce_mm(mm);
		if (r)
			pr_err("Failed to quiesce KFD\n");
		schedule_delayed_work(&process_info->restore_userptr_work,
			msecs_to_jiffies(AMDGPU_USERPTR_RESTORE_DELAY_MS));
	}

	return r;
}

/* Update invalid userptr BOs
 *
 * Moves invalidated (evicted) userptr BOs from userptr_valid_list to
 * userptr_inval_list and updates user pages for all BOs that have
 * been invalidated since their last update.
 */
static int update_invalid_user_pages(struct amdkfd_process_info *process_info,
				     struct mm_struct *mm)
{
	struct kgd_mem *mem, *tmp_mem;
	struct amdgpu_bo *bo;
	struct ttm_operation_ctx ctx = { false, false };
	int invalid, ret;

	/* Move all invalidated BOs to the userptr_inval_list and
	 * release their user pages by migration to the CPU domain
	 */
	list_for_each_entry_safe(mem, tmp_mem,
				 &process_info->userptr_valid_list,
				 validate_list.head) {
		if (!atomic_read(&mem->invalid))
			continue; /* BO is still valid */

		bo = mem->bo;

		if (amdgpu_bo_reserve(bo, true))
			return -EAGAIN;
		amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
		amdgpu_bo_unreserve(bo);
		if (ret) {
			pr_err("%s: Failed to invalidate userptr BO\n",
			       __func__);
			return -EAGAIN;
		}

		list_move_tail(&mem->validate_list.head,
			       &process_info->userptr_inval_list);
	}

	if (list_empty(&process_info->userptr_inval_list))
		return 0; /* All evicted userptr BOs were freed */

	/* Go through userptr_inval_list and update any invalid user_pages */
	list_for_each_entry(mem, &process_info->userptr_inval_list,
			    validate_list.head) {
		invalid = atomic_read(&mem->invalid);
		if (!invalid)
			/* BO hasn't been invalidated since the last
			 * revalidation attempt. Keep its BO list.
			 */
			continue;

		bo = mem->bo;

		if (!mem->user_pages) {
			mem->user_pages =
				kvmalloc_array(bo->tbo.ttm->num_pages,
						 sizeof(struct page *),
						 GFP_KERNEL | __GFP_ZERO);
			if (!mem->user_pages) {
				pr_err("%s: Failed to allocate pages array\n",
				       __func__);
				return -ENOMEM;
			}
		} else if (mem->user_pages[0]) {
			release_pages(mem->user_pages, bo->tbo.ttm->num_pages);
		}

		/* Get updated user pages */
		ret = amdgpu_ttm_tt_get_user_pages(bo->tbo.ttm,
						   mem->user_pages);
		if (ret) {
			mem->user_pages[0] = NULL;
			pr_info("%s: Failed to get user pages: %d\n",
				__func__, ret);
			/* Pretend it succeeded. It will fail later
			 * with a VM fault if the GPU tries to access
			 * it. Better than hanging indefinitely with
			 * stalled user mode queues.
			 */
		}

		/* Mark the BO as valid unless it was invalidated
		 * again concurrently
		 */
		if (atomic_cmpxchg(&mem->invalid, invalid, 0) != invalid)
			return -EAGAIN;
	}

	return 0;
}

/* Validate invalid userptr BOs
 *
 * Validates BOs on the userptr_inval_list, and moves them back to the
 * userptr_valid_list. Also updates GPUVM page tables with new page
 * addresses and waits for the page table updates to complete.
 */
static int validate_invalid_user_pages(struct amdkfd_process_info *process_info)
{
	struct amdgpu_bo_list_entry *pd_bo_list_entries;
	struct list_head resv_list, duplicates;
	struct ww_acquire_ctx ticket;
	struct amdgpu_sync sync;

	struct amdgpu_vm *peer_vm;
	struct kgd_mem *mem, *tmp_mem;
	struct amdgpu_bo *bo;
	struct ttm_operation_ctx ctx = { false, false };
	int i, ret;

	pd_bo_list_entries = kcalloc(process_info->n_vms,
				     sizeof(struct amdgpu_bo_list_entry),
				     GFP_KERNEL);
	if (!pd_bo_list_entries) {
		pr_err("%s: Failed to allocate PD BO list entries\n", __func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&resv_list);
	INIT_LIST_HEAD(&duplicates);

	/* Get all the page directory BOs that need to be reserved */
	i = 0;
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node)
		amdgpu_vm_get_pd_bo(peer_vm, &resv_list,
				    &pd_bo_list_entries[i++]);
	/* Add the userptr_inval_list entries to resv_list */
	list_for_each_entry(mem, &process_info->userptr_inval_list,
			    validate_list.head) {
		list_add_tail(&mem->resv_list.head, &resv_list);
		mem->resv_list.bo = mem->validate_list.bo;
		mem->resv_list.num_shared = mem->validate_list.num_shared;
	}

	/* Reserve all BOs and page tables for validation */
	ret = ttm_eu_reserve_buffers(&ticket, &resv_list, false, &duplicates);
	WARN(!list_empty(&duplicates), "Duplicates should be empty");
	if (ret)
		goto out;

	amdgpu_sync_create(&sync);

	/* Avoid triggering eviction fences when unmapping invalid
	 * userptr BOs (waits for all fences, doesn't use
	 * FENCE_OWNER_VM)
	 */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node)
		amdgpu_amdkfd_remove_eviction_fence(peer_vm->root.base.bo,
						process_info->eviction_fence,
						NULL, NULL);

	ret = process_validate_vms(process_info);
	if (ret)
		goto unreserve_out;

	/* Validate BOs and update GPUVM page tables */
	list_for_each_entry_safe(mem, tmp_mem,
				 &process_info->userptr_inval_list,
				 validate_list.head) {
		struct kfd_bo_va_list *bo_va_entry;

		bo = mem->bo;

		/* Copy pages array and validate the BO if we got user pages */
		if (mem->user_pages[0]) {
			amdgpu_ttm_tt_set_user_pages(bo->tbo.ttm,
						     mem->user_pages);
			amdgpu_bo_placement_from_domain(bo, mem->domain);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (ret) {
				pr_err("%s: failed to validate BO\n", __func__);
				goto unreserve_out;
			}
		}

		/* Validate succeeded, now the BO owns the pages, free
		 * our copy of the pointer array. Put this BO back on
		 * the userptr_valid_list. If we need to revalidate
		 * it, we need to start from scratch.
		 */
		kvfree(mem->user_pages);
		mem->user_pages = NULL;
		list_move_tail(&mem->validate_list.head,
			       &process_info->userptr_valid_list);

		/* Update mapping. If the BO was not validated
		 * (because we couldn't get user pages), this will
		 * clear the page table entries, which will result in
		 * VM faults if the GPU tries to access the invalid
		 * memory.
		 */
		list_for_each_entry(bo_va_entry, &mem->bo_va_list, bo_list) {
			if (!bo_va_entry->is_mapped)
				continue;

			ret = update_gpuvm_pte((struct amdgpu_device *)
					       bo_va_entry->kgd_dev,
					       bo_va_entry, &sync);
			if (ret) {
				pr_err("%s: update PTE failed\n", __func__);
				/* make sure this gets validated again */
				atomic_inc(&mem->invalid);
				goto unreserve_out;
			}
		}
	}

	/* Update page directories */
	ret = process_update_pds(process_info, &sync);

unreserve_out:
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node)
		amdgpu_bo_fence(peer_vm->root.base.bo,
				&process_info->eviction_fence->base, true);
	ttm_eu_backoff_reservation(&ticket, &resv_list);
	amdgpu_sync_wait(&sync, false);
	amdgpu_sync_free(&sync);
out:
	kfree(pd_bo_list_entries);

	return ret;
}

/* Worker callback to restore evicted userptr BOs
 *
 * Tries to update and validate all userptr BOs. If successful and no
 * concurrent evictions happened, the queues are restarted. Otherwise,
 * reschedule for another attempt later.
 */
static void amdgpu_amdkfd_restore_userptr_worker(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct amdkfd_process_info *process_info =
		container_of(dwork, struct amdkfd_process_info,
			     restore_userptr_work);
	struct task_struct *usertask;
	struct mm_struct *mm;
	int evicted_bos;

	evicted_bos = atomic_read(&process_info->evicted_bos);
	if (!evicted_bos)
		return;

	/* Reference task and mm in case of concurrent process termination */
	usertask = get_pid_task(process_info->pid, PIDTYPE_PID);
	if (!usertask)
		return;
	mm = get_task_mm(usertask);
	if (!mm) {
		put_task_struct(usertask);
		return;
	}

	mutex_lock(&process_info->lock);

	if (update_invalid_user_pages(process_info, mm))
		goto unlock_out;
	/* userptr_inval_list can be empty if all evicted userptr BOs
	 * have been freed. In that case there is nothing to validate
	 * and we can just restart the queues.
	 */
	if (!list_empty(&process_info->userptr_inval_list)) {
		if (atomic_read(&process_info->evicted_bos) != evicted_bos)
			goto unlock_out; /* Concurrent eviction, try again */

		if (validate_invalid_user_pages(process_info))
			goto unlock_out;
	}
	/* Final check for concurrent evicton and atomic update. If
	 * another eviction happens after successful update, it will
	 * be a first eviction that calls quiesce_mm. The eviction
	 * reference counting inside KFD will handle this case.
	 */
	if (atomic_cmpxchg(&process_info->evicted_bos, evicted_bos, 0) !=
	    evicted_bos)
		goto unlock_out;
	evicted_bos = 0;
	if (kgd2kfd->resume_mm(mm)) {
		pr_err("%s: Failed to resume KFD\n", __func__);
		/* No recovery from this failure. Probably the CP is
		 * hanging. No point trying again.
		 */
	}
unlock_out:
	mutex_unlock(&process_info->lock);
	mmput(mm);
	put_task_struct(usertask);

	/* If validation failed, reschedule another attempt */
	if (evicted_bos)
		schedule_delayed_work(&process_info->restore_userptr_work,
			msecs_to_jiffies(AMDGPU_USERPTR_RESTORE_DELAY_MS));
}

/** amdgpu_amdkfd_gpuvm_restore_process_bos - Restore all BOs for the given
 *   KFD process identified by process_info
 *
 * @process_info: amdkfd_process_info of the KFD process
 *
 * After memory eviction, restore thread calls this function. The function
 * should be called when the Process is still valid. BO restore involves -
 *
 * 1.  Release old eviction fence and create new one
 * 2.  Get two copies of PD BO list from all the VMs. Keep one copy as pd_list.
 * 3   Use the second PD list and kfd_bo_list to create a list (ctx.list) of
 *     BOs that need to be reserved.
 * 4.  Reserve all the BOs
 * 5.  Validate of PD and PT BOs.
 * 6.  Validate all KFD BOs using kfd_bo_list and Map them and add new fence
 * 7.  Add fence to all PD and PT BOs.
 * 8.  Unreserve all BOs
 */
int amdgpu_amdkfd_gpuvm_restore_process_bos(void *info, struct dma_fence **ef)
{
	struct amdgpu_bo_list_entry *pd_bo_list;
	struct amdkfd_process_info *process_info = info;
	struct amdgpu_vm *peer_vm;
	struct kgd_mem *mem;
	struct bo_vm_reservation_context ctx;
	struct amdgpu_amdkfd_fence *new_fence;
	int ret = 0, i;
	struct list_head duplicate_save;
	struct amdgpu_sync sync_obj;

	INIT_LIST_HEAD(&duplicate_save);
	INIT_LIST_HEAD(&ctx.list);
	INIT_LIST_HEAD(&ctx.duplicates);

	pd_bo_list = kcalloc(process_info->n_vms,
			     sizeof(struct amdgpu_bo_list_entry),
			     GFP_KERNEL);
	if (!pd_bo_list)
		return -ENOMEM;

	i = 0;
	mutex_lock(&process_info->lock);
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			vm_list_node)
		amdgpu_vm_get_pd_bo(peer_vm, &ctx.list, &pd_bo_list[i++]);

	/* Reserve all BOs and page tables/directory. Add all BOs from
	 * kfd_bo_list to ctx.list
	 */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
			    validate_list.head) {

		list_add_tail(&mem->resv_list.head, &ctx.list);
		mem->resv_list.bo = mem->validate_list.bo;
		mem->resv_list.num_shared = mem->validate_list.num_shared;
	}

	ret = ttm_eu_reserve_buffers(&ctx.ticket, &ctx.list,
				     false, &duplicate_save);
	if (ret) {
		pr_debug("Memory eviction: TTM Reserve Failed. Try again\n");
		goto ttm_reserve_fail;
	}

	amdgpu_sync_create(&sync_obj);

	/* Validate PDs and PTs */
	ret = process_validate_vms(process_info);
	if (ret)
		goto validate_map_fail;

	ret = process_sync_pds_resv(process_info, &sync_obj);
	if (ret) {
		pr_debug("Memory eviction: Failed to sync to PD BO moving fence. Try again\n");
		goto validate_map_fail;
	}

	/* Validate BOs and map them to GPUVM (update VM page tables). */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
			    validate_list.head) {

		struct amdgpu_bo *bo = mem->bo;
		uint32_t domain = mem->domain;
		struct kfd_bo_va_list *bo_va_entry;

		ret = amdgpu_amdkfd_bo_validate(bo, domain, false);
		if (ret) {
			pr_debug("Memory eviction: Validate BOs failed. Try again\n");
			goto validate_map_fail;
		}
		ret = amdgpu_sync_fence(NULL, &sync_obj, bo->tbo.moving, false);
		if (ret) {
			pr_debug("Memory eviction: Sync BO fence failed. Try again\n");
			goto validate_map_fail;
		}
		list_for_each_entry(bo_va_entry, &mem->bo_va_list,
				    bo_list) {
			ret = update_gpuvm_pte((struct amdgpu_device *)
					      bo_va_entry->kgd_dev,
					      bo_va_entry,
					      &sync_obj);
			if (ret) {
				pr_debug("Memory eviction: update PTE failed. Try again\n");
				goto validate_map_fail;
			}
		}
	}

	/* Update page directories */
	ret = process_update_pds(process_info, &sync_obj);
	if (ret) {
		pr_debug("Memory eviction: update PDs failed. Try again\n");
		goto validate_map_fail;
	}

	/* Wait for validate and PT updates to finish */
	amdgpu_sync_wait(&sync_obj, false);

	/* Release old eviction fence and create new one, because fence only
	 * goes from unsignaled to signaled, fence cannot be reused.
	 * Use context and mm from the old fence.
	 */
	new_fence = amdgpu_amdkfd_fence_create(
				process_info->eviction_fence->base.context,
				process_info->eviction_fence->mm);
	if (!new_fence) {
		pr_err("Failed to create eviction fence\n");
		ret = -ENOMEM;
		goto validate_map_fail;
	}
	dma_fence_put(&process_info->eviction_fence->base);
	process_info->eviction_fence = new_fence;
	*ef = dma_fence_get(&new_fence->base);

	/* Attach new eviction fence to all BOs */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
		validate_list.head)
		amdgpu_bo_fence(mem->bo,
			&process_info->eviction_fence->base, true);

	/* Attach eviction fence to PD / PT BOs */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		struct amdgpu_bo *bo = peer_vm->root.base.bo;

		amdgpu_bo_fence(bo, &process_info->eviction_fence->base, true);
	}

validate_map_fail:
	ttm_eu_backoff_reservation(&ctx.ticket, &ctx.list);
	amdgpu_sync_free(&sync_obj);
ttm_reserve_fail:
	mutex_unlock(&process_info->lock);
	kfree(pd_bo_list);
	return ret;
}

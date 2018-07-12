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
#include <drm/drmP.h>
#include "amdgpu_object.h"
#include "amdgpu_vm.h"
#include "amdgpu_amdkfd.h"

/* Special VM and GART address alignment needed for VI pre-Fiji due to
 * a HW bug.
 */
#define VI_BO_SIZE_ALIGN (0x8000)

/* Impose limit on how much memory KFD can use */
static struct {
	uint64_t max_system_mem_limit;
	int64_t system_mem_used;
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
 *  System (kernel) memory - 3/8th System RAM
 */
void amdgpu_amdkfd_gpuvm_init_mem_limits(void)
{
	struct sysinfo si;
	uint64_t mem;

	si_meminfo(&si);
	mem = si.totalram - si.totalhigh;
	mem *= si.mem_unit;

	spin_lock_init(&kfd_mem_limit.mem_limit_lock);
	kfd_mem_limit.max_system_mem_limit = (mem >> 1) - (mem >> 3);
	pr_debug("Kernel memory limit %lluM\n",
		(kfd_mem_limit.max_system_mem_limit >> 20));
}

static int amdgpu_amdkfd_reserve_system_mem_limit(struct amdgpu_device *adev,
					      uint64_t size, u32 domain)
{
	size_t acc_size;
	int ret = 0;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		if (kfd_mem_limit.system_mem_used + (acc_size + size) >
			kfd_mem_limit.max_system_mem_limit) {
			ret = -ENOMEM;
			goto err_no_mem;
		}
		kfd_mem_limit.system_mem_used += (acc_size + size);
	}
err_no_mem:
	spin_unlock(&kfd_mem_limit.mem_limit_lock);
	return ret;
}

static void unreserve_system_mem_limit(struct amdgpu_device *adev,
				       uint64_t size, u32 domain)
{
	size_t acc_size;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	if (domain == AMDGPU_GEM_DOMAIN_GTT)
		kfd_mem_limit.system_mem_used -= (acc_size + size);
	WARN_ONCE(kfd_mem_limit.system_mem_used < 0,
		  "kfd system memory accounting unbalanced");

	spin_unlock(&kfd_mem_limit.mem_limit_lock);
}

void amdgpu_amdkfd_unreserve_system_memory_limit(struct amdgpu_bo *bo)
{
	spin_lock(&kfd_mem_limit.mem_limit_lock);

	if (bo->preferred_domains == AMDGPU_GEM_DOMAIN_GTT) {
		kfd_mem_limit.system_mem_used -=
			(bo->tbo.acc_size + amdgpu_bo_size(bo));
	}
	WARN_ONCE(kfd_mem_limit.system_mem_used < 0,
		  "kfd system memory accounting unbalanced");

	spin_unlock(&kfd_mem_limit.mem_limit_lock);
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
	struct reservation_object_list *fobj;
	struct reservation_object *resv;
	unsigned int i = 0, j = 0, k = 0, shared_count;
	unsigned int count = 0;
	struct amdgpu_amdkfd_fence **fence_list;

	if (!ef && !ef_list)
		return -EINVAL;

	if (ef_list) {
		*ef_list = NULL;
		*ef_count = 0;
	}

	resv = bo->tbo.resv;
	fobj = reservation_object_get_list(resv);

	if (!fobj)
		return 0;

	preempt_disable();
	write_seqcount_begin(&resv->seq);

	/* Go through all the shared fences in the resevation object. If
	 * ef is specified and it exists in the list, remove it and reduce the
	 * count. If ef is not specified, then get the count of eviction fences
	 * present.
	 */
	shared_count = fobj->shared_count;
	for (i = 0; i < shared_count; ++i) {
		struct dma_fence *f;

		f = rcu_dereference_protected(fobj->shared[i],
					      reservation_object_held(resv));

		if (ef) {
			if (f->context == ef->base.context) {
				dma_fence_put(f);
				fobj->shared_count--;
			} else {
				RCU_INIT_POINTER(fobj->shared[j++], f);
			}
		} else if (to_amdgpu_amdkfd_fence(f))
			count++;
	}
	write_seqcount_end(&resv->seq);
	preempt_enable();

	if (ef || !count)
		return 0;

	/* Alloc memory for count number of eviction fence pointers. Fill the
	 * ef_list array and ef_count
	 */
	fence_list = kcalloc(count, sizeof(struct amdgpu_amdkfd_fence *),
			     GFP_KERNEL);
	if (!fence_list)
		return -ENOMEM;

	preempt_disable();
	write_seqcount_begin(&resv->seq);

	j = 0;
	for (i = 0; i < shared_count; ++i) {
		struct dma_fence *f;
		struct amdgpu_amdkfd_fence *efence;

		f = rcu_dereference_protected(fobj->shared[i],
			reservation_object_held(resv));

		efence = to_amdgpu_amdkfd_fence(f);
		if (efence) {
			fence_list[k++] = efence;
			fobj->shared_count--;
		} else {
			RCU_INIT_POINTER(fobj->shared[j++], f);
		}
	}

	write_seqcount_end(&resv->seq);
	preempt_enable();

	*ef_list = fence_list;
	*ef_count = k;

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

	amdgpu_ttm_placement_from_domain(bo, domain);

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
	uint64_t addr, flags = AMDGPU_PTE_VALID;
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

	addr = amdgpu_bo_gpu_offset(vm->root.base.bo);
	amdgpu_gmc_get_vm_pde(adev, -1, &addr, &flags);
	vm->pd_phys_addr = addr;

	if (vm->use_cpu_for_update) {
		ret = amdgpu_bo_kmap(pd, NULL);
		if (ret) {
			pr_err("amdgpu: failed to kmap PD, ret=%d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int sync_vm_fence(struct amdgpu_device *adev, struct amdgpu_sync *sync,
			 struct dma_fence *f)
{
	int ret = amdgpu_sync_fence(adev, sync, f, false);

	/* Sync objects can't handle multiple GPUs (contexts) updating
	 * sync->last_vm_update. Fortunately we don't need it for
	 * KFD's purposes, so we can just drop that fence.
	 */
	if (sync->last_vm_update) {
		dma_fence_put(sync->last_vm_update);
		sync->last_vm_update = NULL;
	}

	return ret;
}

static int vm_update_pds(struct amdgpu_vm *vm, struct amdgpu_sync *sync)
{
	struct amdgpu_bo *pd = vm->root.base.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);
	int ret;

	ret = amdgpu_vm_update_directories(adev, vm);
	if (ret)
		return ret;

	return sync_vm_fence(adev, sync, vm->last_update);
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
				struct amdkfd_process_info *process_info)
{
	struct ttm_validate_buffer *entry = &mem->validate_list;
	struct amdgpu_bo *bo = mem->bo;

	INIT_LIST_HEAD(&entry->head);
	entry->shared = true;
	entry->bo = &bo->tbo;
	mutex_lock(&process_info->lock);
	list_add_tail(&entry->head, &process_info->kfd_bo_list);
	mutex_unlock(&process_info->lock);
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

	ctx->kfd_bo.robj = bo;
	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.shared = true;
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

	ctx->kfd_bo.robj = bo;
	ctx->kfd_bo.priority = 0;
	ctx->kfd_bo.tv.bo = &bo->tbo;
	ctx->kfd_bo.tv.shared = true;
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

	sync_vm_fence(adev, sync, bo_va->last_pt_update);

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

	return sync_vm_fence(adev, sync, bo_va->last_pt_update);
}

static int map_bo_to_gpuvm(struct amdgpu_device *adev,
		struct kfd_bo_va_list *entry, struct amdgpu_sync *sync)
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

		info->eviction_fence =
			amdgpu_amdkfd_fence_create(dma_fence_context_alloc(1),
						   current->mm);
		if (!info->eviction_fence) {
			pr_err("Failed to create eviction fence\n");
			ret = -ENOMEM;
			goto create_evict_fence_fail;
		}

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
create_evict_fence_fail:
		mutex_destroy(&info->lock);
		kfree(info);
	}
	return ret;
}

int amdgpu_amdkfd_gpuvm_create_process_vm(struct kgd_dev *kgd, void **vm,
					  void **process_info,
					  struct dma_fence **ef)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct amdgpu_vm *new_vm;
	int ret;

	new_vm = kzalloc(sizeof(*new_vm), GFP_KERNEL);
	if (!new_vm)
		return -ENOMEM;

	/* Initialize AMDGPU part of the VM */
	ret = amdgpu_vm_init(adev, new_vm, AMDGPU_VM_CONTEXT_COMPUTE, 0);
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
					   struct file *filp,
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
	ret = amdgpu_vm_make_compute(adev, avm);
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

		dma_fence_put(&process_info->eviction_fence->base);
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

uint32_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *vm)
{
	struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;

	return avm->pd_phys_addr >> AMDGPU_GPU_PAGE_SHIFT;
}

int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct kgd_dev *kgd, uint64_t va, uint64_t size,
		void *vm, struct kgd_mem **mem,
		uint64_t *offset, uint32_t flags)
{
	struct amdgpu_device *adev = get_amdgpu_device(kgd);
	struct amdgpu_vm *avm = (struct amdgpu_vm *)vm;
	struct amdgpu_bo *bo;
	int byte_align;
	u32 alloc_domain;
	u64 alloc_flags;
	uint32_t mapping_flags;
	int ret;

	/*
	 * Check on which domain to allocate BO
	 */
	if (flags & ALLOC_MEM_FLAGS_VRAM) {
		alloc_domain = AMDGPU_GEM_DOMAIN_VRAM;
		alloc_flags = AMDGPU_GEM_CREATE_VRAM_CLEARED;
		alloc_flags |= (flags & ALLOC_MEM_FLAGS_PUBLIC) ?
			AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED :
			AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	} else if (flags & ALLOC_MEM_FLAGS_GTT) {
		alloc_domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_flags = 0;
	} else {
		return -EINVAL;
	}

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (!*mem)
		return -ENOMEM;
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
			adev->asic_type != CHIP_POLARIS11) ?
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

	ret = amdgpu_amdkfd_reserve_system_mem_limit(adev, size, alloc_domain);
	if (ret) {
		pr_debug("Insufficient system memory\n");
		goto err_reserve_system_mem;
	}

	pr_debug("\tcreate BO VA 0x%llx size 0x%llx domain %s\n",
			va, size, domain_string(alloc_domain));

	ret = amdgpu_bo_create(adev, size, byte_align,
				alloc_domain, alloc_flags, ttm_bo_type_device, NULL, &bo);
	if (ret) {
		pr_debug("Failed to create BO on domain %s. ret %d\n",
				domain_string(alloc_domain), ret);
		goto err_bo_create;
	}
	bo->kfd_bo = *mem;
	(*mem)->bo = bo;

	(*mem)->va = va;
	(*mem)->domain = alloc_domain;
	(*mem)->mapped_to_gpu_memory = 0;
	(*mem)->process_info = avm->process_info;
	add_kgd_mem_to_kfd_bo_list(*mem, avm->process_info);

	if (offset)
		*offset = amdgpu_bo_mmap_offset(bo);

	return 0;

err_bo_create:
	unreserve_system_mem_limit(adev, size, alloc_domain);
err_reserve_system_mem:
	mutex_destroy(&(*mem)->lock);
	kfree(*mem);
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

	/* Make sure restore workers don't access the BO any more */
	bo_list_entry = &mem->validate_list;
	mutex_lock(&process_info->lock);
	list_del(&bo_list_entry->head);
	mutex_unlock(&process_info->lock);

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

	/* Make sure restore is not running concurrently.
	 */
	mutex_lock(&mem->process_info->lock);

	mutex_lock(&mem->lock);

	bo = mem->bo;

	if (!bo) {
		pr_err("Invalid BO when mapping memory to GPU\n");
		ret = -EINVAL;
		goto out;
	}

	domain = mem->domain;
	bo_size = bo->tbo.mem.size;

	pr_debug("Map VA 0x%llx - 0x%llx to vm %p domain %s\n",
			mem->va,
			mem->va + bo_size * (1 + mem->aql_queue),
			vm, domain_string(domain));

	ret = reserve_bo_and_vm(mem, vm, &ctx);
	if (unlikely(ret))
		goto out;

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

	if (mem->mapped_to_gpu_memory == 0) {
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

			ret = map_bo_to_gpuvm(adev, entry, ctx.sync);
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

	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT, NULL);
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
		mem->resv_list.shared = mem->validate_list.shared;
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

	/* Wait for PD/PTs validate to finish */
	/* FIXME: I think this isn't needed */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		struct amdgpu_bo *bo = peer_vm->root.base.bo;

		ttm_bo_wait(&bo->tbo, false, false);
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

	/* Wait for validate to finish and attach new eviction fence */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
		validate_list.head)
		ttm_bo_wait(&mem->bo->tbo, false, false);
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

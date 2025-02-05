// SPDX-License-Identifier: MIT
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
#include <linux/dma-buf.h>
#include <linux/list.h>
#include <linux/pagemap.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <drm/ttm/ttm_tt.h>

#include <drm/drm_exec.h>

#include "amdgpu_object.h"
#include "amdgpu_gem.h"
#include "amdgpu_vm.h"
#include "amdgpu_hmm.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_dma_buf.h"
#include <uapi/linux/kfd_ioctl.h>
#include "amdgpu_xgmi.h"
#include "kfd_priv.h"
#include "kfd_smi_events.h"

/* Userptr restore delay, just long enough to allow consecutive VM
 * changes to accumulate
 */
#define AMDGPU_USERPTR_RESTORE_DELAY_MS 1
#define AMDGPU_RESERVE_MEM_LIMIT			(3UL << 29)

/*
 * Align VRAM availability to 2MB to avoid fragmentation caused by 4K allocations in the tail 2MB
 * BO chunk
 */
#define VRAM_AVAILABLITY_ALIGN (1 << 21)

/* Impose limit on how much memory KFD can use */
static struct {
	uint64_t max_system_mem_limit;
	uint64_t max_ttm_mem_limit;
	int64_t system_mem_used;
	int64_t ttm_mem_used;
	spinlock_t mem_limit_lock;
} kfd_mem_limit;

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

static bool kfd_mem_is_attached(struct amdgpu_vm *avm,
		struct kgd_mem *mem)
{
	struct kfd_mem_attachment *entry;

	list_for_each_entry(entry, &mem->attachments, list)
		if (entry->bo_va->base.vm == avm)
			return true;

	return false;
}

/**
 * reuse_dmamap() - Check whether adev can share the original
 * userptr BO
 *
 * If both adev and bo_adev are in direct mapping or
 * in the same iommu group, they can share the original BO.
 *
 * @adev: Device to which can or cannot share the original BO
 * @bo_adev: Device to which allocated BO belongs to
 *
 * Return: returns true if adev can share original userptr BO,
 * false otherwise.
 */
static bool reuse_dmamap(struct amdgpu_device *adev, struct amdgpu_device *bo_adev)
{
	return (adev->ram_is_direct_mapped && bo_adev->ram_is_direct_mapped) ||
			(adev->dev->iommu_group == bo_adev->dev->iommu_group);
}

/* Set memory usage limits. Current, limits are
 *  System (TTM + userptr) memory - 15/16th System RAM
 *  TTM memory - 3/8th System RAM
 */
void amdgpu_amdkfd_gpuvm_init_mem_limits(void)
{
	struct sysinfo si;
	uint64_t mem;

	if (kfd_mem_limit.max_system_mem_limit)
		return;

	si_meminfo(&si);
	mem = si.totalram - si.totalhigh;
	mem *= si.mem_unit;

	spin_lock_init(&kfd_mem_limit.mem_limit_lock);
	kfd_mem_limit.max_system_mem_limit = mem - (mem >> 6);
	if (kfd_mem_limit.max_system_mem_limit < 2 * AMDGPU_RESERVE_MEM_LIMIT)
		kfd_mem_limit.max_system_mem_limit >>= 1;
	else
		kfd_mem_limit.max_system_mem_limit -= AMDGPU_RESERVE_MEM_LIMIT;

	kfd_mem_limit.max_ttm_mem_limit = ttm_tt_pages_limit() << PAGE_SHIFT;
	pr_debug("Kernel memory limit %lluM, TTM limit %lluM\n",
		(kfd_mem_limit.max_system_mem_limit >> 20),
		(kfd_mem_limit.max_ttm_mem_limit >> 20));
}

void amdgpu_amdkfd_reserve_system_mem(uint64_t size)
{
	kfd_mem_limit.system_mem_used += size;
}

/* Estimate page table size needed to represent a given memory size
 *
 * With 4KB pages, we need one 8 byte PTE for each 4KB of memory
 * (factor 512, >> 9). With 2MB pages, we need one 8 byte PTE for 2MB
 * of memory (factor 256K, >> 18). ROCm user mode tries to optimize
 * for 2MB pages for TLB efficiency. However, small allocations and
 * fragmented system memory still need some 4KB pages. We choose a
 * compromise that should work in most cases without reserving too
 * much memory for page tables unnecessarily (factor 16K, >> 14).
 */

#define ESTIMATE_PT_SIZE(mem_size) max(((mem_size) >> 14), AMDGPU_VM_RESERVED_VRAM)

/**
 * amdgpu_amdkfd_reserve_mem_limit() - Decrease available memory by size
 * of buffer.
 *
 * @adev: Device to which allocated BO belongs to
 * @size: Size of buffer, in bytes, encapsulated by B0. This should be
 * equivalent to amdgpu_bo_size(BO)
 * @alloc_flag: Flag used in allocating a BO as noted above
 * @xcp_id: xcp_id is used to get xcp from xcp manager, one xcp is
 * managed as one compute node in driver for app
 *
 * Return:
 *	returns -ENOMEM in case of error, ZERO otherwise
 */
int amdgpu_amdkfd_reserve_mem_limit(struct amdgpu_device *adev,
		uint64_t size, u32 alloc_flag, int8_t xcp_id)
{
	uint64_t reserved_for_pt =
		ESTIMATE_PT_SIZE(amdgpu_amdkfd_total_mem_size);
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	uint64_t reserved_for_ras = (con ? con->reserved_pages_in_bytes : 0);
	size_t system_mem_needed, ttm_mem_needed, vram_needed;
	int ret = 0;
	uint64_t vram_size = 0;

	system_mem_needed = 0;
	ttm_mem_needed = 0;
	vram_needed = 0;
	if (alloc_flag & KFD_IOC_ALLOC_MEM_FLAGS_GTT) {
		system_mem_needed = size;
		ttm_mem_needed = size;
	} else if (alloc_flag & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
		/*
		 * Conservatively round up the allocation requirement to 2 MB
		 * to avoid fragmentation caused by 4K allocations in the tail
		 * 2M BO chunk.
		 */
		vram_needed = size;
		/*
		 * For GFX 9.4.3, get the VRAM size from XCP structs
		 */
		if (WARN_ONCE(xcp_id < 0, "invalid XCP ID %d", xcp_id))
			return -EINVAL;

		vram_size = KFD_XCP_MEMORY_SIZE(adev, xcp_id);
		if (adev->flags & AMD_IS_APU) {
			system_mem_needed = size;
			ttm_mem_needed = size;
		}
	} else if (alloc_flag & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) {
		system_mem_needed = size;
	} else if (!(alloc_flag &
				(KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL |
				 KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP))) {
		pr_err("%s: Invalid BO type %#x\n", __func__, alloc_flag);
		return -ENOMEM;
	}

	spin_lock(&kfd_mem_limit.mem_limit_lock);

	if (kfd_mem_limit.system_mem_used + system_mem_needed >
	    kfd_mem_limit.max_system_mem_limit)
		pr_debug("Set no_system_mem_limit=1 if using shared memory\n");

	if ((kfd_mem_limit.system_mem_used + system_mem_needed >
	     kfd_mem_limit.max_system_mem_limit && !no_system_mem_limit) ||
	    (kfd_mem_limit.ttm_mem_used + ttm_mem_needed >
	     kfd_mem_limit.max_ttm_mem_limit) ||
	    (adev && xcp_id >= 0 && adev->kfd.vram_used[xcp_id] + vram_needed >
	     vram_size - reserved_for_pt - reserved_for_ras - atomic64_read(&adev->vram_pin_size))) {
		ret = -ENOMEM;
		goto release;
	}

	/* Update memory accounting by decreasing available system
	 * memory, TTM memory and GPU memory as computed above
	 */
	WARN_ONCE(vram_needed && !adev,
		  "adev reference can't be null when vram is used");
	if (adev && xcp_id >= 0) {
		adev->kfd.vram_used[xcp_id] += vram_needed;
		adev->kfd.vram_used_aligned[xcp_id] +=
				(adev->flags & AMD_IS_APU) ?
				vram_needed :
				ALIGN(vram_needed, VRAM_AVAILABLITY_ALIGN);
	}
	kfd_mem_limit.system_mem_used += system_mem_needed;
	kfd_mem_limit.ttm_mem_used += ttm_mem_needed;

release:
	spin_unlock(&kfd_mem_limit.mem_limit_lock);
	return ret;
}

void amdgpu_amdkfd_unreserve_mem_limit(struct amdgpu_device *adev,
		uint64_t size, u32 alloc_flag, int8_t xcp_id)
{
	spin_lock(&kfd_mem_limit.mem_limit_lock);

	if (alloc_flag & KFD_IOC_ALLOC_MEM_FLAGS_GTT) {
		kfd_mem_limit.system_mem_used -= size;
		kfd_mem_limit.ttm_mem_used -= size;
	} else if (alloc_flag & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
		WARN_ONCE(!adev,
			  "adev reference can't be null when alloc mem flags vram is set");
		if (WARN_ONCE(xcp_id < 0, "invalid XCP ID %d", xcp_id))
			goto release;

		if (adev) {
			adev->kfd.vram_used[xcp_id] -= size;
			if (adev->flags & AMD_IS_APU) {
				adev->kfd.vram_used_aligned[xcp_id] -= size;
				kfd_mem_limit.system_mem_used -= size;
				kfd_mem_limit.ttm_mem_used -= size;
			} else {
				adev->kfd.vram_used_aligned[xcp_id] -=
					ALIGN(size, VRAM_AVAILABLITY_ALIGN);
			}
		}
	} else if (alloc_flag & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) {
		kfd_mem_limit.system_mem_used -= size;
	} else if (!(alloc_flag &
				(KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL |
				 KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP))) {
		pr_err("%s: Invalid BO type %#x\n", __func__, alloc_flag);
		goto release;
	}
	WARN_ONCE(adev && xcp_id >= 0 && adev->kfd.vram_used[xcp_id] < 0,
		  "KFD VRAM memory accounting unbalanced for xcp: %d", xcp_id);
	WARN_ONCE(kfd_mem_limit.ttm_mem_used < 0,
		  "KFD TTM memory accounting unbalanced");
	WARN_ONCE(kfd_mem_limit.system_mem_used < 0,
		  "KFD system memory accounting unbalanced");

release:
	spin_unlock(&kfd_mem_limit.mem_limit_lock);
}

void amdgpu_amdkfd_release_notify(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	u32 alloc_flags = bo->kfd_bo->alloc_flags;
	u64 size = amdgpu_bo_size(bo);

	amdgpu_amdkfd_unreserve_mem_limit(adev, size, alloc_flags,
					  bo->xcp_id);

	kfree(bo->kfd_bo);
}

/**
 * create_dmamap_sg_bo() - Creates a amdgpu_bo object to reflect information
 * about USERPTR or DOOREBELL or MMIO BO.
 *
 * @adev: Device for which dmamap BO is being created
 * @mem: BO of peer device that is being DMA mapped. Provides parameters
 *	 in building the dmamap BO
 * @bo_out: Output parameter updated with handle of dmamap BO
 */
static int
create_dmamap_sg_bo(struct amdgpu_device *adev,
		 struct kgd_mem *mem, struct amdgpu_bo **bo_out)
{
	struct drm_gem_object *gem_obj;
	int ret;
	uint64_t flags = 0;

	ret = amdgpu_bo_reserve(mem->bo, false);
	if (ret)
		return ret;

	if (mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR)
		flags |= mem->bo->flags & (AMDGPU_GEM_CREATE_COHERENT |
					AMDGPU_GEM_CREATE_UNCACHED);

	ret = amdgpu_gem_object_create(adev, mem->bo->tbo.base.size, 1,
			AMDGPU_GEM_DOMAIN_CPU, AMDGPU_GEM_CREATE_PREEMPTIBLE | flags,
			ttm_bo_type_sg, mem->bo->tbo.base.resv, &gem_obj, 0);

	amdgpu_bo_unreserve(mem->bo);

	if (ret) {
		pr_err("Error in creating DMA mappable SG BO on domain: %d\n", ret);
		return -EINVAL;
	}

	*bo_out = gem_to_amdgpu_bo(gem_obj);
	(*bo_out)->parent = amdgpu_bo_ref(mem->bo);
	return ret;
}

/* amdgpu_amdkfd_remove_eviction_fence - Removes eviction fence from BO's
 *  reservation object.
 *
 * @bo: [IN] Remove eviction fence(s) from this BO
 * @ef: [IN] This eviction fence is removed if it
 *  is present in the shared list.
 *
 * NOTE: Must be called with BO reserved i.e. bo->tbo.resv->lock held.
 */
static int amdgpu_amdkfd_remove_eviction_fence(struct amdgpu_bo *bo,
					struct amdgpu_amdkfd_fence *ef)
{
	struct dma_fence *replacement;

	if (!ef)
		return -EINVAL;

	/* TODO: Instead of block before we should use the fence of the page
	 * table update and TLB flush here directly.
	 */
	replacement = dma_fence_get_stub();
	dma_resv_replace_fences(bo->tbo.base.resv, ef->base.context,
				replacement, DMA_RESV_USAGE_BOOKKEEP);
	dma_fence_put(replacement);
	return 0;
}

int amdgpu_amdkfd_remove_fence_on_pt_pd_bos(struct amdgpu_bo *bo)
{
	struct amdgpu_bo *root = bo;
	struct amdgpu_vm_bo_base *vm_bo;
	struct amdgpu_vm *vm;
	struct amdkfd_process_info *info;
	struct amdgpu_amdkfd_fence *ef;
	int ret;

	/* we can always get vm_bo from root PD bo.*/
	while (root->parent)
		root = root->parent;

	vm_bo = root->vm_bo;
	if (!vm_bo)
		return 0;

	vm = vm_bo->vm;
	if (!vm)
		return 0;

	info = vm->process_info;
	if (!info || !info->eviction_fence)
		return 0;

	ef = container_of(dma_fence_get(&info->eviction_fence->base),
			struct amdgpu_amdkfd_fence, base);

	BUG_ON(!dma_resv_trylock(bo->tbo.base.resv));
	ret = amdgpu_amdkfd_remove_eviction_fence(bo, ef);
	dma_resv_unlock(bo->tbo.base.resv);

	dma_fence_put(&ef->base);
	return ret;
}

static int amdgpu_amdkfd_bo_validate(struct amdgpu_bo *bo, uint32_t domain,
				     bool wait)
{
	struct ttm_operation_ctx ctx = { false, false };
	int ret;

	if (WARN(amdgpu_ttm_tt_get_usermm(bo->tbo.ttm),
		 "Called with userptr BO"))
		return -EINVAL;

	/* bo has been pinned, not need validate it */
	if (bo->tbo.pin_count)
		return 0;

	amdgpu_bo_placement_from_domain(bo, domain);

	ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (ret)
		goto validate_fail;
	if (wait)
		amdgpu_bo_sync_wait(bo, AMDGPU_FENCE_OWNER_KFD, false);

validate_fail:
	return ret;
}

int amdgpu_amdkfd_bo_validate_and_fence(struct amdgpu_bo *bo,
					uint32_t domain,
					struct dma_fence *fence)
{
	int ret = amdgpu_bo_reserve(bo, false);

	if (ret)
		return ret;

	ret = amdgpu_amdkfd_bo_validate(bo, domain, true);
	if (ret)
		goto unreserve_out;

	ret = dma_resv_reserve_fences(bo->tbo.base.resv, 1);
	if (ret)
		goto unreserve_out;

	dma_resv_add_fence(bo->tbo.base.resv, fence,
			   DMA_RESV_USAGE_BOOKKEEP);

unreserve_out:
	amdgpu_bo_unreserve(bo);

	return ret;
}

static int amdgpu_amdkfd_validate_vm_bo(void *_unused, struct amdgpu_bo *bo)
{
	return amdgpu_amdkfd_bo_validate(bo, bo->allowed_domains, false);
}

/* vm_validate_pt_pd_bos - Validate page table and directory BOs
 *
 * Page directories are not updated here because huge page handling
 * during page table updates can invalidate page directory entries
 * again. Page directories are only updated after updating page
 * tables.
 */
static int vm_validate_pt_pd_bos(struct amdgpu_vm *vm,
				 struct ww_acquire_ctx *ticket)
{
	struct amdgpu_bo *pd = vm->root.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);
	int ret;

	ret = amdgpu_vm_validate(adev, vm, ticket,
				 amdgpu_amdkfd_validate_vm_bo, NULL);
	if (ret) {
		pr_err("failed to validate PT BOs\n");
		return ret;
	}

	vm->pd_phys_addr = amdgpu_gmc_pd_addr(vm->root.bo);

	return 0;
}

static int vm_update_pds(struct amdgpu_vm *vm, struct amdgpu_sync *sync)
{
	struct amdgpu_bo *pd = vm->root.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);
	int ret;

	ret = amdgpu_vm_update_pdes(adev, vm, false);
	if (ret)
		return ret;

	return amdgpu_sync_fence(sync, vm->last_update);
}

static uint64_t get_pte_flags(struct amdgpu_device *adev, struct kgd_mem *mem)
{
	uint32_t mapping_flags = AMDGPU_VM_PAGE_READABLE |
				 AMDGPU_VM_MTYPE_DEFAULT;

	if (mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE)
		mapping_flags |= AMDGPU_VM_PAGE_WRITEABLE;
	if (mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE)
		mapping_flags |= AMDGPU_VM_PAGE_EXECUTABLE;

	return amdgpu_gem_va_map_flags(adev, mapping_flags);
}

/**
 * create_sg_table() - Create an sg_table for a contiguous DMA addr range
 * @addr: The starting address to point to
 * @size: Size of memory area in bytes being pointed to
 *
 * Allocates an instance of sg_table and initializes it to point to memory
 * area specified by input parameters. The address used to build is assumed
 * to be DMA mapped, if needed.
 *
 * DOORBELL or MMIO BOs use only one scatterlist node in their sg_table
 * because they are physically contiguous.
 *
 * Return: Initialized instance of SG Table or NULL
 */
static struct sg_table *create_sg_table(uint64_t addr, uint32_t size)
{
	struct sg_table *sg = kmalloc(sizeof(*sg), GFP_KERNEL);

	if (!sg)
		return NULL;
	if (sg_alloc_table(sg, 1, GFP_KERNEL)) {
		kfree(sg);
		return NULL;
	}
	sg_dma_address(sg->sgl) = addr;
	sg->sgl->length = size;
#ifdef CONFIG_NEED_SG_DMA_LENGTH
	sg->sgl->dma_length = size;
#endif
	return sg;
}

static int
kfd_mem_dmamap_userptr(struct kgd_mem *mem,
		       struct kfd_mem_attachment *attachment)
{
	enum dma_data_direction direction =
		mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE ?
		DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	struct ttm_operation_ctx ctx = {.interruptible = true};
	struct amdgpu_bo *bo = attachment->bo_va->base.bo;
	struct amdgpu_device *adev = attachment->adev;
	struct ttm_tt *src_ttm = mem->bo->tbo.ttm;
	struct ttm_tt *ttm = bo->tbo.ttm;
	int ret;

	if (WARN_ON(ttm->num_pages != src_ttm->num_pages))
		return -EINVAL;

	ttm->sg = kmalloc(sizeof(*ttm->sg), GFP_KERNEL);
	if (unlikely(!ttm->sg))
		return -ENOMEM;

	/* Same sequence as in amdgpu_ttm_tt_pin_userptr */
	ret = sg_alloc_table_from_pages(ttm->sg, src_ttm->pages,
					ttm->num_pages, 0,
					(u64)ttm->num_pages << PAGE_SHIFT,
					GFP_KERNEL);
	if (unlikely(ret))
		goto free_sg;

	ret = dma_map_sgtable(adev->dev, ttm->sg, direction, 0);
	if (unlikely(ret))
		goto release_sg;

	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
	ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (ret)
		goto unmap_sg;

	return 0;

unmap_sg:
	dma_unmap_sgtable(adev->dev, ttm->sg, direction, 0);
release_sg:
	pr_err("DMA map userptr failed: %d\n", ret);
	sg_free_table(ttm->sg);
free_sg:
	kfree(ttm->sg);
	ttm->sg = NULL;
	return ret;
}

static int
kfd_mem_dmamap_dmabuf(struct kfd_mem_attachment *attachment)
{
	struct ttm_operation_ctx ctx = {.interruptible = true};
	struct amdgpu_bo *bo = attachment->bo_va->base.bo;
	int ret;

	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
	ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (ret)
		return ret;

	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
	return ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
}

/**
 * kfd_mem_dmamap_sg_bo() - Create DMA mapped sg_table to access DOORBELL or MMIO BO
 * @mem: SG BO of the DOORBELL or MMIO resource on the owning device
 * @attachment: Virtual address attachment of the BO on accessing device
 *
 * An access request from the device that owns DOORBELL does not require DMA mapping.
 * This is because the request doesn't go through PCIe root complex i.e. it instead
 * loops back. The need to DMA map arises only when accessing peer device's DOORBELL
 *
 * In contrast, all access requests for MMIO need to be DMA mapped without regard to
 * device ownership. This is because access requests for MMIO go through PCIe root
 * complex.
 *
 * This is accomplished in two steps:
 *   - Obtain DMA mapped address of DOORBELL or MMIO memory that could be used
 *         in updating requesting device's page table
 *   - Signal TTM to mark memory pointed to by requesting device's BO as GPU
 *         accessible. This allows an update of requesting device's page table
 *         with entries associated with DOOREBELL or MMIO memory
 *
 * This method is invoked in the following contexts:
 *   - Mapping of DOORBELL or MMIO BO of same or peer device
 *   - Validating an evicted DOOREBELL or MMIO BO on device seeking access
 *
 * Return: ZERO if successful, NON-ZERO otherwise
 */
static int
kfd_mem_dmamap_sg_bo(struct kgd_mem *mem,
		     struct kfd_mem_attachment *attachment)
{
	struct ttm_operation_ctx ctx = {.interruptible = true};
	struct amdgpu_bo *bo = attachment->bo_va->base.bo;
	struct amdgpu_device *adev = attachment->adev;
	struct ttm_tt *ttm = bo->tbo.ttm;
	enum dma_data_direction dir;
	dma_addr_t dma_addr;
	bool mmio;
	int ret;

	/* Expect SG Table of dmapmap BO to be NULL */
	mmio = (mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP);
	if (unlikely(ttm->sg)) {
		pr_err("SG Table of %d BO for peer device is UNEXPECTEDLY NON-NULL", mmio);
		return -EINVAL;
	}

	dir = mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE ?
			DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	dma_addr = mem->bo->tbo.sg->sgl->dma_address;
	pr_debug("%d BO size: %d\n", mmio, mem->bo->tbo.sg->sgl->length);
	pr_debug("%d BO address before DMA mapping: %llx\n", mmio, dma_addr);
	dma_addr = dma_map_resource(adev->dev, dma_addr,
			mem->bo->tbo.sg->sgl->length, dir, DMA_ATTR_SKIP_CPU_SYNC);
	ret = dma_mapping_error(adev->dev, dma_addr);
	if (unlikely(ret))
		return ret;
	pr_debug("%d BO address after DMA mapping: %llx\n", mmio, dma_addr);

	ttm->sg = create_sg_table(dma_addr, mem->bo->tbo.sg->sgl->length);
	if (unlikely(!ttm->sg)) {
		ret = -ENOMEM;
		goto unmap_sg;
	}

	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
	ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (unlikely(ret))
		goto free_sg;

	return ret;

free_sg:
	sg_free_table(ttm->sg);
	kfree(ttm->sg);
	ttm->sg = NULL;
unmap_sg:
	dma_unmap_resource(adev->dev, dma_addr, mem->bo->tbo.sg->sgl->length,
			   dir, DMA_ATTR_SKIP_CPU_SYNC);
	return ret;
}

static int
kfd_mem_dmamap_attachment(struct kgd_mem *mem,
			  struct kfd_mem_attachment *attachment)
{
	switch (attachment->type) {
	case KFD_MEM_ATT_SHARED:
		return 0;
	case KFD_MEM_ATT_USERPTR:
		return kfd_mem_dmamap_userptr(mem, attachment);
	case KFD_MEM_ATT_DMABUF:
		return kfd_mem_dmamap_dmabuf(attachment);
	case KFD_MEM_ATT_SG:
		return kfd_mem_dmamap_sg_bo(mem, attachment);
	default:
		WARN_ON_ONCE(1);
	}
	return -EINVAL;
}

static void
kfd_mem_dmaunmap_userptr(struct kgd_mem *mem,
			 struct kfd_mem_attachment *attachment)
{
	enum dma_data_direction direction =
		mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE ?
		DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	struct ttm_operation_ctx ctx = {.interruptible = false};
	struct amdgpu_bo *bo = attachment->bo_va->base.bo;
	struct amdgpu_device *adev = attachment->adev;
	struct ttm_tt *ttm = bo->tbo.ttm;

	if (unlikely(!ttm->sg))
		return;

	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
	(void)ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);

	dma_unmap_sgtable(adev->dev, ttm->sg, direction, 0);
	sg_free_table(ttm->sg);
	kfree(ttm->sg);
	ttm->sg = NULL;
}

static void
kfd_mem_dmaunmap_dmabuf(struct kfd_mem_attachment *attachment)
{
	/* This is a no-op. We don't want to trigger eviction fences when
	 * unmapping DMABufs. Therefore the invalidation (moving to system
	 * domain) is done in kfd_mem_dmamap_dmabuf.
	 */
}

/**
 * kfd_mem_dmaunmap_sg_bo() - Free DMA mapped sg_table of DOORBELL or MMIO BO
 * @mem: SG BO of the DOORBELL or MMIO resource on the owning device
 * @attachment: Virtual address attachment of the BO on accessing device
 *
 * The method performs following steps:
 *   - Signal TTM to mark memory pointed to by BO as GPU inaccessible
 *   - Free SG Table that is used to encapsulate DMA mapped memory of
 *          peer device's DOORBELL or MMIO memory
 *
 * This method is invoked in the following contexts:
 *     UNMapping of DOORBELL or MMIO BO on a device having access to its memory
 *     Eviction of DOOREBELL or MMIO BO on device having access to its memory
 *
 * Return: void
 */
static void
kfd_mem_dmaunmap_sg_bo(struct kgd_mem *mem,
		       struct kfd_mem_attachment *attachment)
{
	struct ttm_operation_ctx ctx = {.interruptible = true};
	struct amdgpu_bo *bo = attachment->bo_va->base.bo;
	struct amdgpu_device *adev = attachment->adev;
	struct ttm_tt *ttm = bo->tbo.ttm;
	enum dma_data_direction dir;

	if (unlikely(!ttm->sg)) {
		pr_debug("SG Table of BO is NULL");
		return;
	}

	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
	(void)ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);

	dir = mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE ?
				DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	dma_unmap_resource(adev->dev, ttm->sg->sgl->dma_address,
			ttm->sg->sgl->length, dir, DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(ttm->sg);
	kfree(ttm->sg);
	ttm->sg = NULL;
	bo->tbo.sg = NULL;
}

static void
kfd_mem_dmaunmap_attachment(struct kgd_mem *mem,
			    struct kfd_mem_attachment *attachment)
{
	switch (attachment->type) {
	case KFD_MEM_ATT_SHARED:
		break;
	case KFD_MEM_ATT_USERPTR:
		kfd_mem_dmaunmap_userptr(mem, attachment);
		break;
	case KFD_MEM_ATT_DMABUF:
		kfd_mem_dmaunmap_dmabuf(attachment);
		break;
	case KFD_MEM_ATT_SG:
		kfd_mem_dmaunmap_sg_bo(mem, attachment);
		break;
	default:
		WARN_ON_ONCE(1);
	}
}

static int kfd_mem_export_dmabuf(struct kgd_mem *mem)
{
	if (!mem->dmabuf) {
		struct amdgpu_device *bo_adev;
		struct dma_buf *dmabuf;

		bo_adev = amdgpu_ttm_adev(mem->bo->tbo.bdev);
		dmabuf = drm_gem_prime_handle_to_dmabuf(&bo_adev->ddev, bo_adev->kfd.client.file,
					       mem->gem_handle,
			mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE ?
					       DRM_RDWR : 0);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		mem->dmabuf = dmabuf;
	}

	return 0;
}

static int
kfd_mem_attach_dmabuf(struct amdgpu_device *adev, struct kgd_mem *mem,
		      struct amdgpu_bo **bo)
{
	struct drm_gem_object *gobj;
	int ret;

	ret = kfd_mem_export_dmabuf(mem);
	if (ret)
		return ret;

	gobj = amdgpu_gem_prime_import(adev_to_drm(adev), mem->dmabuf);
	if (IS_ERR(gobj))
		return PTR_ERR(gobj);

	*bo = gem_to_amdgpu_bo(gobj);
	(*bo)->flags |= AMDGPU_GEM_CREATE_PREEMPTIBLE;

	return 0;
}

/* kfd_mem_attach - Add a BO to a VM
 *
 * Everything that needs to bo done only once when a BO is first added
 * to a VM. It can later be mapped and unmapped many times without
 * repeating these steps.
 *
 * 0. Create BO for DMA mapping, if needed
 * 1. Allocate and initialize BO VA entry data structure
 * 2. Add BO to the VM
 * 3. Determine ASIC-specific PTE flags
 * 4. Alloc page tables and directories if needed
 * 4a.  Validate new page tables and directories
 */
static int kfd_mem_attach(struct amdgpu_device *adev, struct kgd_mem *mem,
		struct amdgpu_vm *vm, bool is_aql)
{
	struct amdgpu_device *bo_adev = amdgpu_ttm_adev(mem->bo->tbo.bdev);
	unsigned long bo_size = mem->bo->tbo.base.size;
	uint64_t va = mem->va;
	struct kfd_mem_attachment *attachment[2] = {NULL, NULL};
	struct amdgpu_bo *bo[2] = {NULL, NULL};
	struct amdgpu_bo_va *bo_va;
	bool same_hive = false;
	int i, ret;

	if (!va) {
		pr_err("Invalid VA when adding BO to VM\n");
		return -EINVAL;
	}

	/* Determine access to VRAM, MMIO and DOORBELL BOs of peer devices
	 *
	 * The access path of MMIO and DOORBELL BOs of is always over PCIe.
	 * In contrast the access path of VRAM BOs depens upon the type of
	 * link that connects the peer device. Access over PCIe is allowed
	 * if peer device has large BAR. In contrast, access over xGMI is
	 * allowed for both small and large BAR configurations of peer device
	 */
	if ((adev != bo_adev && !(adev->flags & AMD_IS_APU)) &&
	    ((mem->domain == AMDGPU_GEM_DOMAIN_VRAM) ||
	     (mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL) ||
	     (mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP))) {
		if (mem->domain == AMDGPU_GEM_DOMAIN_VRAM)
			same_hive = amdgpu_xgmi_same_hive(adev, bo_adev);
		if (!same_hive && !amdgpu_device_is_peer_accessible(bo_adev, adev))
			return -EINVAL;
	}

	for (i = 0; i <= is_aql; i++) {
		attachment[i] = kzalloc(sizeof(*attachment[i]), GFP_KERNEL);
		if (unlikely(!attachment[i])) {
			ret = -ENOMEM;
			goto unwind;
		}

		pr_debug("\t add VA 0x%llx - 0x%llx to vm %p\n", va,
			 va + bo_size, vm);

		if ((adev == bo_adev && !(mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)) ||
		    (amdgpu_ttm_tt_get_usermm(mem->bo->tbo.ttm) && reuse_dmamap(adev, bo_adev)) ||
		    (mem->domain == AMDGPU_GEM_DOMAIN_GTT && reuse_dmamap(adev, bo_adev)) ||
		    same_hive) {
			/* Mappings on the local GPU, or VRAM mappings in the
			 * local hive, or userptr, or GTT mapping can reuse dma map
			 * address space share the original BO
			 */
			attachment[i]->type = KFD_MEM_ATT_SHARED;
			bo[i] = mem->bo;
			drm_gem_object_get(&bo[i]->tbo.base);
		} else if (i > 0) {
			/* Multiple mappings on the same GPU share the BO */
			attachment[i]->type = KFD_MEM_ATT_SHARED;
			bo[i] = bo[0];
			drm_gem_object_get(&bo[i]->tbo.base);
		} else if (amdgpu_ttm_tt_get_usermm(mem->bo->tbo.ttm)) {
			/* Create an SG BO to DMA-map userptrs on other GPUs */
			attachment[i]->type = KFD_MEM_ATT_USERPTR;
			ret = create_dmamap_sg_bo(adev, mem, &bo[i]);
			if (ret)
				goto unwind;
		/* Handle DOORBELL BOs of peer devices and MMIO BOs of local and peer devices */
		} else if (mem->bo->tbo.type == ttm_bo_type_sg) {
			WARN_ONCE(!(mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL ||
				    mem->alloc_flags & KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP),
				  "Handing invalid SG BO in ATTACH request");
			attachment[i]->type = KFD_MEM_ATT_SG;
			ret = create_dmamap_sg_bo(adev, mem, &bo[i]);
			if (ret)
				goto unwind;
		/* Enable acces to GTT and VRAM BOs of peer devices */
		} else if (mem->domain == AMDGPU_GEM_DOMAIN_GTT ||
			   mem->domain == AMDGPU_GEM_DOMAIN_VRAM) {
			attachment[i]->type = KFD_MEM_ATT_DMABUF;
			ret = kfd_mem_attach_dmabuf(adev, mem, &bo[i]);
			if (ret)
				goto unwind;
			pr_debug("Employ DMABUF mechanism to enable peer GPU access\n");
		} else {
			WARN_ONCE(true, "Handling invalid ATTACH request");
			ret = -EINVAL;
			goto unwind;
		}

		/* Add BO to VM internal data structures */
		ret = amdgpu_bo_reserve(bo[i], false);
		if (ret) {
			pr_debug("Unable to reserve BO during memory attach");
			goto unwind;
		}
		bo_va = amdgpu_vm_bo_find(vm, bo[i]);
		if (!bo_va)
			bo_va = amdgpu_vm_bo_add(adev, vm, bo[i]);
		else
			++bo_va->ref_count;
		attachment[i]->bo_va = bo_va;
		amdgpu_bo_unreserve(bo[i]);
		if (unlikely(!attachment[i]->bo_va)) {
			ret = -ENOMEM;
			pr_err("Failed to add BO object to VM. ret == %d\n",
			       ret);
			goto unwind;
		}
		attachment[i]->va = va;
		attachment[i]->pte_flags = get_pte_flags(adev, mem);
		attachment[i]->adev = adev;
		list_add(&attachment[i]->list, &mem->attachments);

		va += bo_size;
	}

	return 0;

unwind:
	for (; i >= 0; i--) {
		if (!attachment[i])
			continue;
		if (attachment[i]->bo_va) {
			(void)amdgpu_bo_reserve(bo[i], true);
			if (--attachment[i]->bo_va->ref_count == 0)
				amdgpu_vm_bo_del(adev, attachment[i]->bo_va);
			amdgpu_bo_unreserve(bo[i]);
			list_del(&attachment[i]->list);
		}
		if (bo[i])
			drm_gem_object_put(&bo[i]->tbo.base);
		kfree(attachment[i]);
	}
	return ret;
}

static void kfd_mem_detach(struct kfd_mem_attachment *attachment)
{
	struct amdgpu_bo *bo = attachment->bo_va->base.bo;

	pr_debug("\t remove VA 0x%llx in entry %p\n",
			attachment->va, attachment);
	if (--attachment->bo_va->ref_count == 0)
		amdgpu_vm_bo_del(attachment->adev, attachment->bo_va);
	drm_gem_object_put(&bo->tbo.base);
	list_del(&attachment->list);
	kfree(attachment);
}

static void add_kgd_mem_to_kfd_bo_list(struct kgd_mem *mem,
				struct amdkfd_process_info *process_info,
				bool userptr)
{
	mutex_lock(&process_info->lock);
	if (userptr)
		list_add_tail(&mem->validate_list,
			      &process_info->userptr_valid_list);
	else
		list_add_tail(&mem->validate_list, &process_info->kfd_bo_list);
	mutex_unlock(&process_info->lock);
}

static void remove_kgd_mem_from_kfd_bo_list(struct kgd_mem *mem,
		struct amdkfd_process_info *process_info)
{
	mutex_lock(&process_info->lock);
	list_del(&mem->validate_list);
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
static int init_user_pages(struct kgd_mem *mem, uint64_t user_addr,
			   bool criu_resume)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	struct amdgpu_bo *bo = mem->bo;
	struct ttm_operation_ctx ctx = { true, false };
	struct hmm_range *range;
	int ret = 0;

	mutex_lock(&process_info->lock);

	ret = amdgpu_ttm_tt_set_userptr(&bo->tbo, user_addr, 0);
	if (ret) {
		pr_err("%s: Failed to set userptr: %d\n", __func__, ret);
		goto out;
	}

	ret = amdgpu_hmm_register(bo, user_addr);
	if (ret) {
		pr_err("%s: Failed to register MMU notifier: %d\n",
		       __func__, ret);
		goto out;
	}

	if (criu_resume) {
		/*
		 * During a CRIU restore operation, the userptr buffer objects
		 * will be validated in the restore_userptr_work worker at a
		 * later stage when it is scheduled by another ioctl called by
		 * CRIU master process for the target pid for restore.
		 */
		mutex_lock(&process_info->notifier_lock);
		mem->invalid++;
		mutex_unlock(&process_info->notifier_lock);
		mutex_unlock(&process_info->lock);
		return 0;
	}

	ret = amdgpu_ttm_tt_get_user_pages(bo, bo->tbo.ttm->pages, &range);
	if (ret) {
		if (ret == -EAGAIN)
			pr_debug("Failed to get user pages, try again\n");
		else
			pr_err("%s: Failed to get user pages: %d\n", __func__, ret);
		goto unregister_out;
	}

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
	amdgpu_ttm_tt_get_user_pages_done(bo->tbo.ttm, range);
unregister_out:
	if (ret)
		amdgpu_hmm_unregister(bo);
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
	/* DRM execution context for the reservation */
	struct drm_exec exec;
	/* Number of VMs reserved */
	unsigned int n_vms;
	/* Pointer to sync object */
	struct amdgpu_sync *sync;
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

	ctx->n_vms = 1;
	ctx->sync = &mem->sync;
	drm_exec_init(&ctx->exec, DRM_EXEC_INTERRUPTIBLE_WAIT, 0);
	drm_exec_until_all_locked(&ctx->exec) {
		ret = amdgpu_vm_lock_pd(vm, &ctx->exec, 2);
		drm_exec_retry_on_contention(&ctx->exec);
		if (unlikely(ret))
			goto error;

		ret = drm_exec_prepare_obj(&ctx->exec, &bo->tbo.base, 1);
		drm_exec_retry_on_contention(&ctx->exec);
		if (unlikely(ret))
			goto error;
	}
	return 0;

error:
	pr_err("Failed to reserve buffers in ttm.\n");
	drm_exec_fini(&ctx->exec);
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
	struct kfd_mem_attachment *entry;
	struct amdgpu_bo *bo = mem->bo;
	int ret;

	ctx->sync = &mem->sync;
	drm_exec_init(&ctx->exec, DRM_EXEC_INTERRUPTIBLE_WAIT |
		      DRM_EXEC_IGNORE_DUPLICATES, 0);
	drm_exec_until_all_locked(&ctx->exec) {
		ctx->n_vms = 0;
		list_for_each_entry(entry, &mem->attachments, list) {
			if ((vm && vm != entry->bo_va->base.vm) ||
				(entry->is_mapped != map_type
				&& map_type != BO_VM_ALL))
				continue;

			ret = amdgpu_vm_lock_pd(entry->bo_va->base.vm,
						&ctx->exec, 2);
			drm_exec_retry_on_contention(&ctx->exec);
			if (unlikely(ret))
				goto error;
			++ctx->n_vms;
		}

		ret = drm_exec_prepare_obj(&ctx->exec, &bo->tbo.base, 1);
		drm_exec_retry_on_contention(&ctx->exec);
		if (unlikely(ret))
			goto error;
	}
	return 0;

error:
	pr_err("Failed to reserve buffers in ttm.\n");
	drm_exec_fini(&ctx->exec);
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

	drm_exec_fini(&ctx->exec);
	ctx->sync = NULL;
	return ret;
}

static int unmap_bo_from_gpuvm(struct kgd_mem *mem,
				struct kfd_mem_attachment *entry,
				struct amdgpu_sync *sync)
{
	struct amdgpu_bo_va *bo_va = entry->bo_va;
	struct amdgpu_device *adev = entry->adev;
	struct amdgpu_vm *vm = bo_va->base.vm;

	if (bo_va->queue_refcount) {
		pr_debug("bo_va->queue_refcount %d\n", bo_va->queue_refcount);
		return -EBUSY;
	}

	(void)amdgpu_vm_bo_unmap(adev, bo_va, entry->va);

	(void)amdgpu_vm_clear_freed(adev, vm, &bo_va->last_pt_update);

	(void)amdgpu_sync_fence(sync, bo_va->last_pt_update);

	return 0;
}

static int update_gpuvm_pte(struct kgd_mem *mem,
			    struct kfd_mem_attachment *entry,
			    struct amdgpu_sync *sync)
{
	struct amdgpu_bo_va *bo_va = entry->bo_va;
	struct amdgpu_device *adev = entry->adev;
	int ret;

	ret = kfd_mem_dmamap_attachment(mem, entry);
	if (ret)
		return ret;

	/* Update the page tables  */
	ret = amdgpu_vm_bo_update(adev, bo_va, false);
	if (ret) {
		pr_err("amdgpu_vm_bo_update failed\n");
		return ret;
	}

	return amdgpu_sync_fence(sync, bo_va->last_pt_update);
}

static int map_bo_to_gpuvm(struct kgd_mem *mem,
			   struct kfd_mem_attachment *entry,
			   struct amdgpu_sync *sync,
			   bool no_update_pte)
{
	int ret;

	/* Set virtual address for the allocation */
	ret = amdgpu_vm_bo_map(entry->adev, entry->bo_va, entry->va, 0,
			       amdgpu_bo_size(entry->bo_va->base.bo),
			       entry->pte_flags);
	if (ret) {
		pr_err("Failed to map VA 0x%llx in vm. ret %d\n",
				entry->va, ret);
		return ret;
	}

	if (no_update_pte)
		return 0;

	ret = update_gpuvm_pte(mem, entry, sync);
	if (ret) {
		pr_err("update_gpuvm_pte() failed\n");
		goto update_gpuvm_pte_failed;
	}

	return 0;

update_gpuvm_pte_failed:
	unmap_bo_from_gpuvm(mem, entry, sync);
	kfd_mem_dmaunmap_attachment(mem, entry);
	return ret;
}

static int process_validate_vms(struct amdkfd_process_info *process_info,
				struct ww_acquire_ctx *ticket)
{
	struct amdgpu_vm *peer_vm;
	int ret;

	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		ret = vm_validate_pt_pd_bos(peer_vm, ticket);
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
		struct amdgpu_bo *pd = peer_vm->root.bo;

		ret = amdgpu_sync_resv(NULL, sync, pd->tbo.base.resv,
				       AMDGPU_SYNC_NE_OWNER,
				       AMDGPU_FENCE_OWNER_KFD);
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
		mutex_init(&info->notifier_lock);
		INIT_LIST_HEAD(&info->vm_list_head);
		INIT_LIST_HEAD(&info->kfd_bo_list);
		INIT_LIST_HEAD(&info->userptr_valid_list);
		INIT_LIST_HEAD(&info->userptr_inval_list);

		info->eviction_fence =
			amdgpu_amdkfd_fence_create(dma_fence_context_alloc(1),
						   current->mm,
						   NULL);
		if (!info->eviction_fence) {
			pr_err("Failed to create eviction fence\n");
			ret = -ENOMEM;
			goto create_evict_fence_fail;
		}

		info->pid = get_task_pid(current->group_leader, PIDTYPE_PID);
		INIT_DELAYED_WORK(&info->restore_userptr_work,
				  amdgpu_amdkfd_restore_userptr_worker);

		*process_info = info;
	}

	vm->process_info = *process_info;

	/* Validate page directory and attach eviction fence */
	ret = amdgpu_bo_reserve(vm->root.bo, true);
	if (ret)
		goto reserve_pd_fail;
	ret = vm_validate_pt_pd_bos(vm, NULL);
	if (ret) {
		pr_err("validate_pt_pd_bos() failed\n");
		goto validate_pd_fail;
	}
	ret = amdgpu_bo_sync_wait(vm->root.bo,
				  AMDGPU_FENCE_OWNER_KFD, false);
	if (ret)
		goto wait_pd_fail;
	ret = dma_resv_reserve_fences(vm->root.bo->tbo.base.resv, 1);
	if (ret)
		goto reserve_shared_fail;
	dma_resv_add_fence(vm->root.bo->tbo.base.resv,
			   &vm->process_info->eviction_fence->base,
			   DMA_RESV_USAGE_BOOKKEEP);
	amdgpu_bo_unreserve(vm->root.bo);

	/* Update process info */
	mutex_lock(&vm->process_info->lock);
	list_add_tail(&vm->vm_list_node,
			&(vm->process_info->vm_list_head));
	vm->process_info->n_vms++;
	if (ef)
		*ef = dma_fence_get(&vm->process_info->eviction_fence->base);
	mutex_unlock(&vm->process_info->lock);

	return 0;

reserve_shared_fail:
wait_pd_fail:
validate_pd_fail:
	amdgpu_bo_unreserve(vm->root.bo);
reserve_pd_fail:
	vm->process_info = NULL;
	if (info) {
		dma_fence_put(&info->eviction_fence->base);
		*process_info = NULL;
		put_pid(info->pid);
create_evict_fence_fail:
		mutex_destroy(&info->lock);
		mutex_destroy(&info->notifier_lock);
		kfree(info);
	}
	return ret;
}

/**
 * amdgpu_amdkfd_gpuvm_pin_bo() - Pins a BO using following criteria
 * @bo: Handle of buffer object being pinned
 * @domain: Domain into which BO should be pinned
 *
 *   - USERPTR BOs are UNPINNABLE and will return error
 *   - All other BO types (GTT, VRAM, MMIO and DOORBELL) will have their
 *     PIN count incremented. It is valid to PIN a BO multiple times
 *
 * Return: ZERO if successful in pinning, Non-Zero in case of error.
 */
static int amdgpu_amdkfd_gpuvm_pin_bo(struct amdgpu_bo *bo, u32 domain)
{
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret))
		return ret;

	if (bo->flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS) {
		/*
		 * If bo is not contiguous on VRAM, move to system memory first to ensure
		 * we can get contiguous VRAM space after evicting other BOs.
		 */
		if (!(bo->tbo.resource->placement & TTM_PL_FLAG_CONTIGUOUS)) {
			struct ttm_operation_ctx ctx = { true, false };

			amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (unlikely(ret)) {
				pr_debug("validate bo 0x%p to GTT failed %d\n", &bo->tbo, ret);
				goto out;
			}
		}
	}

	ret = amdgpu_bo_pin(bo, domain);
	if (ret)
		pr_err("Error in Pinning BO to domain: %d\n", domain);

	amdgpu_bo_sync_wait(bo, AMDGPU_FENCE_OWNER_KFD, false);
out:
	amdgpu_bo_unreserve(bo);
	return ret;
}

/**
 * amdgpu_amdkfd_gpuvm_unpin_bo() - Unpins BO using following criteria
 * @bo: Handle of buffer object being unpinned
 *
 *   - Is a illegal request for USERPTR BOs and is ignored
 *   - All other BO types (GTT, VRAM, MMIO and DOORBELL) will have their
 *     PIN count decremented. Calls to UNPIN must balance calls to PIN
 */
static void amdgpu_amdkfd_gpuvm_unpin_bo(struct amdgpu_bo *bo)
{
	int ret = 0;

	ret = amdgpu_bo_reserve(bo, false);
	if (unlikely(ret))
		return;

	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
}

int amdgpu_amdkfd_gpuvm_set_vm_pasid(struct amdgpu_device *adev,
				     struct amdgpu_vm *avm, u32 pasid)

{
	int ret;

	/* Free the original amdgpu allocated pasid,
	 * will be replaced with kfd allocated pasid.
	 */
	if (avm->pasid) {
		amdgpu_pasid_free(avm->pasid);
		amdgpu_vm_set_pasid(adev, avm, 0);
	}

	ret = amdgpu_vm_set_pasid(adev, avm, pasid);
	if (ret)
		return ret;

	return 0;
}

int amdgpu_amdkfd_gpuvm_acquire_process_vm(struct amdgpu_device *adev,
					   struct amdgpu_vm *avm,
					   void **process_info,
					   struct dma_fence **ef)
{
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

	amdgpu_vm_set_task_info(avm);

	return 0;
}

void amdgpu_amdkfd_gpuvm_destroy_cb(struct amdgpu_device *adev,
				    struct amdgpu_vm *vm)
{
	struct amdkfd_process_info *process_info = vm->process_info;

	if (!process_info)
		return;

	/* Update process info */
	mutex_lock(&process_info->lock);
	process_info->n_vms--;
	list_del(&vm->vm_list_node);
	mutex_unlock(&process_info->lock);

	vm->process_info = NULL;

	/* Release per-process resources when last compute VM is destroyed */
	if (!process_info->n_vms) {
		WARN_ON(!list_empty(&process_info->kfd_bo_list));
		WARN_ON(!list_empty(&process_info->userptr_valid_list));
		WARN_ON(!list_empty(&process_info->userptr_inval_list));

		dma_fence_put(&process_info->eviction_fence->base);
		cancel_delayed_work_sync(&process_info->restore_userptr_work);
		put_pid(process_info->pid);
		mutex_destroy(&process_info->lock);
		mutex_destroy(&process_info->notifier_lock);
		kfree(process_info);
	}
}

void amdgpu_amdkfd_gpuvm_release_process_vm(struct amdgpu_device *adev,
					    void *drm_priv)
{
	struct amdgpu_vm *avm;

	if (WARN_ON(!adev || !drm_priv))
		return;

	avm = drm_priv_to_vm(drm_priv);

	pr_debug("Releasing process vm %p\n", avm);

	/* The original pasid of amdgpu vm has already been
	 * released during making a amdgpu vm to a compute vm
	 * The current pasid is managed by kfd and will be
	 * released on kfd process destroy. Set amdgpu pasid
	 * to 0 to avoid duplicate release.
	 */
	amdgpu_vm_release_compute(adev, avm);
}

uint64_t amdgpu_amdkfd_gpuvm_get_process_page_dir(void *drm_priv)
{
	struct amdgpu_vm *avm = drm_priv_to_vm(drm_priv);
	struct amdgpu_bo *pd = avm->root.bo;
	struct amdgpu_device *adev = amdgpu_ttm_adev(pd->tbo.bdev);

	if (adev->asic_type < CHIP_VEGA10)
		return avm->pd_phys_addr >> AMDGPU_GPU_PAGE_SHIFT;
	return avm->pd_phys_addr;
}

void amdgpu_amdkfd_block_mmu_notifications(void *p)
{
	struct amdkfd_process_info *pinfo = (struct amdkfd_process_info *)p;

	mutex_lock(&pinfo->lock);
	WRITE_ONCE(pinfo->block_mmu_notifications, true);
	mutex_unlock(&pinfo->lock);
}

int amdgpu_amdkfd_criu_resume(void *p)
{
	int ret = 0;
	struct amdkfd_process_info *pinfo = (struct amdkfd_process_info *)p;

	mutex_lock(&pinfo->lock);
	pr_debug("scheduling work\n");
	mutex_lock(&pinfo->notifier_lock);
	pinfo->evicted_bos++;
	mutex_unlock(&pinfo->notifier_lock);
	if (!READ_ONCE(pinfo->block_mmu_notifications)) {
		ret = -EINVAL;
		goto out_unlock;
	}
	WRITE_ONCE(pinfo->block_mmu_notifications, false);
	queue_delayed_work(system_freezable_wq,
			   &pinfo->restore_userptr_work, 0);

out_unlock:
	mutex_unlock(&pinfo->lock);
	return ret;
}

size_t amdgpu_amdkfd_get_available_memory(struct amdgpu_device *adev,
					  uint8_t xcp_id)
{
	uint64_t reserved_for_pt =
		ESTIMATE_PT_SIZE(amdgpu_amdkfd_total_mem_size);
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);
	uint64_t reserved_for_ras = (con ? con->reserved_pages_in_bytes : 0);
	ssize_t available;
	uint64_t vram_available, system_mem_available, ttm_mem_available;

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	vram_available = KFD_XCP_MEMORY_SIZE(adev, xcp_id)
		- adev->kfd.vram_used_aligned[xcp_id]
		- atomic64_read(&adev->vram_pin_size)
		- reserved_for_pt
		- reserved_for_ras;

	if (adev->flags & AMD_IS_APU) {
		system_mem_available = no_system_mem_limit ?
					kfd_mem_limit.max_system_mem_limit :
					kfd_mem_limit.max_system_mem_limit -
					kfd_mem_limit.system_mem_used;

		ttm_mem_available = kfd_mem_limit.max_ttm_mem_limit -
				kfd_mem_limit.ttm_mem_used;

		available = min3(system_mem_available, ttm_mem_available,
				 vram_available);
		available = ALIGN_DOWN(available, PAGE_SIZE);
	} else {
		available = ALIGN_DOWN(vram_available, VRAM_AVAILABLITY_ALIGN);
	}

	spin_unlock(&kfd_mem_limit.mem_limit_lock);

	if (available < 0)
		available = 0;

	return available;
}

int amdgpu_amdkfd_gpuvm_alloc_memory_of_gpu(
		struct amdgpu_device *adev, uint64_t va, uint64_t size,
		void *drm_priv, struct kgd_mem **mem,
		uint64_t *offset, uint32_t flags, bool criu_resume)
{
	struct amdgpu_vm *avm = drm_priv_to_vm(drm_priv);
	struct amdgpu_fpriv *fpriv = container_of(avm, struct amdgpu_fpriv, vm);
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct sg_table *sg = NULL;
	uint64_t user_addr = 0;
	struct amdgpu_bo *bo;
	struct drm_gem_object *gobj = NULL;
	u32 domain, alloc_domain;
	uint64_t aligned_size;
	int8_t xcp_id = -1;
	u64 alloc_flags;
	int ret;

	/*
	 * Check on which domain to allocate BO
	 */
	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_VRAM) {
		domain = alloc_domain = AMDGPU_GEM_DOMAIN_VRAM;

		if (adev->flags & AMD_IS_APU) {
			domain = AMDGPU_GEM_DOMAIN_GTT;
			alloc_domain = AMDGPU_GEM_DOMAIN_GTT;
			alloc_flags = 0;
		} else {
			alloc_flags = AMDGPU_GEM_CREATE_VRAM_WIPE_ON_RELEASE;
			alloc_flags |= (flags & KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC) ?
			AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED : 0;

			/* For contiguous VRAM allocation */
			if (flags & KFD_IOC_ALLOC_MEM_FLAGS_CONTIGUOUS)
				alloc_flags |= AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
		}
		xcp_id = fpriv->xcp_id == AMDGPU_XCP_NO_PARTITION ?
					0 : fpriv->xcp_id;
	} else if (flags & KFD_IOC_ALLOC_MEM_FLAGS_GTT) {
		domain = alloc_domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_flags = 0;
	} else {
		domain = AMDGPU_GEM_DOMAIN_GTT;
		alloc_domain = AMDGPU_GEM_DOMAIN_CPU;
		alloc_flags = AMDGPU_GEM_CREATE_PREEMPTIBLE;

		if (flags & KFD_IOC_ALLOC_MEM_FLAGS_USERPTR) {
			if (!offset || !*offset)
				return -EINVAL;
			user_addr = untagged_addr(*offset);
		} else if (flags & (KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL |
				    KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)) {
			bo_type = ttm_bo_type_sg;
			if (size > UINT_MAX)
				return -EINVAL;
			sg = create_sg_table(*offset, size);
			if (!sg)
				return -ENOMEM;
		} else {
			return -EINVAL;
		}
	}

	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_COHERENT)
		alloc_flags |= AMDGPU_GEM_CREATE_COHERENT;
	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_EXT_COHERENT)
		alloc_flags |= AMDGPU_GEM_CREATE_EXT_COHERENT;
	if (flags & KFD_IOC_ALLOC_MEM_FLAGS_UNCACHED)
		alloc_flags |= AMDGPU_GEM_CREATE_UNCACHED;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (!*mem) {
		ret = -ENOMEM;
		goto err;
	}
	INIT_LIST_HEAD(&(*mem)->attachments);
	mutex_init(&(*mem)->lock);
	(*mem)->aql_queue = !!(flags & KFD_IOC_ALLOC_MEM_FLAGS_AQL_QUEUE_MEM);

	/* Workaround for AQL queue wraparound bug. Map the same
	 * memory twice. That means we only actually allocate half
	 * the memory.
	 */
	if ((*mem)->aql_queue)
		size >>= 1;
	aligned_size = PAGE_ALIGN(size);

	(*mem)->alloc_flags = flags;

	amdgpu_sync_create(&(*mem)->sync);

	ret = amdgpu_amdkfd_reserve_mem_limit(adev, aligned_size, flags,
					      xcp_id);
	if (ret) {
		pr_debug("Insufficient memory\n");
		goto err_reserve_limit;
	}

	pr_debug("\tcreate BO VA 0x%llx size 0x%llx domain %s xcp_id %d\n",
		 va, (*mem)->aql_queue ? size << 1 : size,
		 domain_string(alloc_domain), xcp_id);

	ret = amdgpu_gem_object_create(adev, aligned_size, 1, alloc_domain, alloc_flags,
				       bo_type, NULL, &gobj, xcp_id + 1);
	if (ret) {
		pr_debug("Failed to create BO on domain %s. ret %d\n",
			 domain_string(alloc_domain), ret);
		goto err_bo_create;
	}
	ret = drm_vma_node_allow(&gobj->vma_node, drm_priv);
	if (ret) {
		pr_debug("Failed to allow vma node access. ret %d\n", ret);
		goto err_node_allow;
	}
	ret = drm_gem_handle_create(adev->kfd.client.file, gobj, &(*mem)->gem_handle);
	if (ret)
		goto err_gem_handle_create;
	bo = gem_to_amdgpu_bo(gobj);
	if (bo_type == ttm_bo_type_sg) {
		bo->tbo.sg = sg;
		bo->tbo.ttm->sg = sg;
	}
	bo->kfd_bo = *mem;
	(*mem)->bo = bo;
	if (user_addr)
		bo->flags |= AMDGPU_AMDKFD_CREATE_USERPTR_BO;

	(*mem)->va = va;
	(*mem)->domain = domain;
	(*mem)->mapped_to_gpu_memory = 0;
	(*mem)->process_info = avm->process_info;

	add_kgd_mem_to_kfd_bo_list(*mem, avm->process_info, user_addr);

	if (user_addr) {
		pr_debug("creating userptr BO for user_addr = %llx\n", user_addr);
		ret = init_user_pages(*mem, user_addr, criu_resume);
		if (ret)
			goto allocate_init_user_pages_failed;
	} else  if (flags & (KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL |
				KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)) {
		ret = amdgpu_amdkfd_gpuvm_pin_bo(bo, AMDGPU_GEM_DOMAIN_GTT);
		if (ret) {
			pr_err("Pinning MMIO/DOORBELL BO during ALLOC FAILED\n");
			goto err_pin_bo;
		}
		bo->allowed_domains = AMDGPU_GEM_DOMAIN_GTT;
		bo->preferred_domains = AMDGPU_GEM_DOMAIN_GTT;
	} else {
		mutex_lock(&avm->process_info->lock);
		if (avm->process_info->eviction_fence &&
		    !dma_fence_is_signaled(&avm->process_info->eviction_fence->base))
			ret = amdgpu_amdkfd_bo_validate_and_fence(bo, domain,
				&avm->process_info->eviction_fence->base);
		mutex_unlock(&avm->process_info->lock);
		if (ret)
			goto err_validate_bo;
	}

	if (offset)
		*offset = amdgpu_bo_mmap_offset(bo);

	return 0;

allocate_init_user_pages_failed:
err_pin_bo:
err_validate_bo:
	remove_kgd_mem_from_kfd_bo_list(*mem, avm->process_info);
	drm_gem_handle_delete(adev->kfd.client.file, (*mem)->gem_handle);
err_gem_handle_create:
	drm_vma_node_revoke(&gobj->vma_node, drm_priv);
err_node_allow:
	/* Don't unreserve system mem limit twice */
	goto err_reserve_limit;
err_bo_create:
	amdgpu_amdkfd_unreserve_mem_limit(adev, aligned_size, flags, xcp_id);
err_reserve_limit:
	amdgpu_sync_free(&(*mem)->sync);
	mutex_destroy(&(*mem)->lock);
	if (gobj)
		drm_gem_object_put(gobj);
	else
		kfree(*mem);
err:
	if (sg) {
		sg_free_table(sg);
		kfree(sg);
	}
	return ret;
}

int amdgpu_amdkfd_gpuvm_free_memory_of_gpu(
		struct amdgpu_device *adev, struct kgd_mem *mem, void *drm_priv,
		uint64_t *size)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	unsigned long bo_size = mem->bo->tbo.base.size;
	bool use_release_notifier = (mem->bo->kfd_bo == mem);
	struct kfd_mem_attachment *entry, *tmp;
	struct bo_vm_reservation_context ctx;
	unsigned int mapped_to_gpu_memory;
	int ret;
	bool is_imported = false;

	mutex_lock(&mem->lock);

	/* Unpin MMIO/DOORBELL BO's that were pinned during allocation */
	if (mem->alloc_flags &
	    (KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL |
	     KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP)) {
		amdgpu_amdkfd_gpuvm_unpin_bo(mem->bo);
	}

	mapped_to_gpu_memory = mem->mapped_to_gpu_memory;
	is_imported = mem->is_imported;
	mutex_unlock(&mem->lock);
	/* lock is not needed after this, since mem is unused and will
	 * be freed anyway
	 */

	if (mapped_to_gpu_memory > 0) {
		pr_debug("BO VA 0x%llx size 0x%lx is still mapped.\n",
				mem->va, bo_size);
		return -EBUSY;
	}

	/* Make sure restore workers don't access the BO any more */
	mutex_lock(&process_info->lock);
	list_del(&mem->validate_list);
	mutex_unlock(&process_info->lock);

	/* Cleanup user pages and MMU notifiers */
	if (amdgpu_ttm_tt_get_usermm(mem->bo->tbo.ttm)) {
		amdgpu_hmm_unregister(mem->bo);
		mutex_lock(&process_info->notifier_lock);
		amdgpu_ttm_tt_discard_user_pages(mem->bo->tbo.ttm, mem->range);
		mutex_unlock(&process_info->notifier_lock);
	}

	ret = reserve_bo_and_cond_vms(mem, NULL, BO_VM_ALL, &ctx);
	if (unlikely(ret))
		return ret;

	amdgpu_amdkfd_remove_eviction_fence(mem->bo,
					process_info->eviction_fence);
	pr_debug("Release VA 0x%llx - 0x%llx\n", mem->va,
		mem->va + bo_size * (1 + mem->aql_queue));

	/* Remove from VM internal data structures */
	list_for_each_entry_safe(entry, tmp, &mem->attachments, list) {
		kfd_mem_dmaunmap_attachment(mem, entry);
		kfd_mem_detach(entry);
	}

	ret = unreserve_bo_and_vms(&ctx, false, false);

	/* Free the sync object */
	amdgpu_sync_free(&mem->sync);

	/* If the SG is not NULL, it's one we created for a doorbell or mmio
	 * remap BO. We need to free it.
	 */
	if (mem->bo->tbo.sg) {
		sg_free_table(mem->bo->tbo.sg);
		kfree(mem->bo->tbo.sg);
	}

	/* Update the size of the BO being freed if it was allocated from
	 * VRAM and is not imported. For APP APU VRAM allocations are done
	 * in GTT domain
	 */
	if (size) {
		if (!is_imported &&
		   (mem->bo->preferred_domains == AMDGPU_GEM_DOMAIN_VRAM ||
		   ((adev->flags & AMD_IS_APU) &&
		    mem->bo->preferred_domains == AMDGPU_GEM_DOMAIN_GTT)))
			*size = bo_size;
		else
			*size = 0;
	}

	/* Free the BO*/
	drm_vma_node_revoke(&mem->bo->tbo.base.vma_node, drm_priv);
	drm_gem_handle_delete(adev->kfd.client.file, mem->gem_handle);
	if (mem->dmabuf) {
		dma_buf_put(mem->dmabuf);
		mem->dmabuf = NULL;
	}
	mutex_destroy(&mem->lock);

	/* If this releases the last reference, it will end up calling
	 * amdgpu_amdkfd_release_notify and kfree the mem struct. That's why
	 * this needs to be the last call here.
	 */
	drm_gem_object_put(&mem->bo->tbo.base);

	/*
	 * For kgd_mem allocated in amdgpu_amdkfd_gpuvm_import_dmabuf(),
	 * explicitly free it here.
	 */
	if (!use_release_notifier)
		kfree(mem);

	return ret;
}

int amdgpu_amdkfd_gpuvm_map_memory_to_gpu(
		struct amdgpu_device *adev, struct kgd_mem *mem,
		void *drm_priv)
{
	struct amdgpu_vm *avm = drm_priv_to_vm(drm_priv);
	int ret;
	struct amdgpu_bo *bo;
	uint32_t domain;
	struct kfd_mem_attachment *entry;
	struct bo_vm_reservation_context ctx;
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

	/* Lock notifier lock. If we find an invalid userptr BO, we can be
	 * sure that the MMU notifier is no longer running
	 * concurrently and the queues are actually stopped
	 */
	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		mutex_lock(&mem->process_info->notifier_lock);
		is_invalid_userptr = !!mem->invalid;
		mutex_unlock(&mem->process_info->notifier_lock);
	}

	mutex_lock(&mem->lock);

	domain = mem->domain;
	bo_size = bo->tbo.base.size;

	pr_debug("Map VA 0x%llx - 0x%llx to vm %p domain %s\n",
			mem->va,
			mem->va + bo_size * (1 + mem->aql_queue),
			avm, domain_string(domain));

	if (!kfd_mem_is_attached(avm, mem)) {
		ret = kfd_mem_attach(adev, mem, avm, mem->aql_queue);
		if (ret)
			goto out;
	}

	ret = reserve_bo_and_vm(mem, avm, &ctx);
	if (unlikely(ret))
		goto out;

	/* Userptr can be marked as "not invalid", but not actually be
	 * validated yet (still in the system domain). In that case
	 * the queues are still stopped and we can leave mapping for
	 * the next restore worker
	 */
	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm) &&
	    bo->tbo.resource->mem_type == TTM_PL_SYSTEM)
		is_invalid_userptr = true;

	ret = vm_validate_pt_pd_bos(avm, NULL);
	if (unlikely(ret))
		goto out_unreserve;

	list_for_each_entry(entry, &mem->attachments, list) {
		if (entry->bo_va->base.vm != avm || entry->is_mapped)
			continue;

		pr_debug("\t map VA 0x%llx - 0x%llx in entry %p\n",
			 entry->va, entry->va + bo_size, entry);

		ret = map_bo_to_gpuvm(mem, entry, ctx.sync,
				      is_invalid_userptr);
		if (ret) {
			pr_err("Failed to map bo to gpuvm\n");
			goto out_unreserve;
		}

		ret = vm_update_pds(avm, ctx.sync);
		if (ret) {
			pr_err("Failed to update page directories\n");
			goto out_unreserve;
		}

		entry->is_mapped = true;
		mem->mapped_to_gpu_memory++;
		pr_debug("\t INC mapping count %d\n",
			 mem->mapped_to_gpu_memory);
	}

	ret = unreserve_bo_and_vms(&ctx, false, false);

	goto out;

out_unreserve:
	unreserve_bo_and_vms(&ctx, false, false);
out:
	mutex_unlock(&mem->process_info->lock);
	mutex_unlock(&mem->lock);
	return ret;
}

int amdgpu_amdkfd_gpuvm_dmaunmap_mem(struct kgd_mem *mem, void *drm_priv)
{
	struct kfd_mem_attachment *entry;
	struct amdgpu_vm *vm;
	int ret;

	vm = drm_priv_to_vm(drm_priv);

	mutex_lock(&mem->lock);

	ret = amdgpu_bo_reserve(mem->bo, true);
	if (ret)
		goto out;

	list_for_each_entry(entry, &mem->attachments, list) {
		if (entry->bo_va->base.vm != vm)
			continue;
		if (entry->bo_va->base.bo->tbo.ttm &&
		    !entry->bo_va->base.bo->tbo.ttm->sg)
			continue;

		kfd_mem_dmaunmap_attachment(mem, entry);
	}

	amdgpu_bo_unreserve(mem->bo);
out:
	mutex_unlock(&mem->lock);

	return ret;
}

int amdgpu_amdkfd_gpuvm_unmap_memory_from_gpu(
		struct amdgpu_device *adev, struct kgd_mem *mem, void *drm_priv)
{
	struct amdgpu_vm *avm = drm_priv_to_vm(drm_priv);
	unsigned long bo_size = mem->bo->tbo.base.size;
	struct kfd_mem_attachment *entry;
	struct bo_vm_reservation_context ctx;
	int ret;

	mutex_lock(&mem->lock);

	ret = reserve_bo_and_cond_vms(mem, avm, BO_VM_MAPPED, &ctx);
	if (unlikely(ret))
		goto out;
	/* If no VMs were reserved, it means the BO wasn't actually mapped */
	if (ctx.n_vms == 0) {
		ret = -EINVAL;
		goto unreserve_out;
	}

	ret = vm_validate_pt_pd_bos(avm, NULL);
	if (unlikely(ret))
		goto unreserve_out;

	pr_debug("Unmap VA 0x%llx - 0x%llx from vm %p\n",
		mem->va,
		mem->va + bo_size * (1 + mem->aql_queue),
		avm);

	list_for_each_entry(entry, &mem->attachments, list) {
		if (entry->bo_va->base.vm != avm || !entry->is_mapped)
			continue;

		pr_debug("\t unmap VA 0x%llx - 0x%llx from entry %p\n",
			 entry->va, entry->va + bo_size, entry);

		ret = unmap_bo_from_gpuvm(mem, entry, ctx.sync);
		if (ret)
			goto unreserve_out;

		entry->is_mapped = false;

		mem->mapped_to_gpu_memory--;
		pr_debug("\t DEC mapping count %d\n",
			 mem->mapped_to_gpu_memory);
	}

unreserve_out:
	unreserve_bo_and_vms(&ctx, false, false);
out:
	mutex_unlock(&mem->lock);
	return ret;
}

int amdgpu_amdkfd_gpuvm_sync_memory(
		struct amdgpu_device *adev, struct kgd_mem *mem, bool intr)
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

/**
 * amdgpu_amdkfd_map_gtt_bo_to_gart - Map BO to GART and increment reference count
 * @bo: Buffer object to be mapped
 * @bo_gart: Return bo reference
 *
 * Before return, bo reference count is incremented. To release the reference and unpin/
 * unmap the BO, call amdgpu_amdkfd_free_gtt_mem.
 */
int amdgpu_amdkfd_map_gtt_bo_to_gart(struct amdgpu_bo *bo, struct amdgpu_bo **bo_gart)
{
	int ret;

	ret = amdgpu_bo_reserve(bo, true);
	if (ret) {
		pr_err("Failed to reserve bo. ret %d\n", ret);
		goto err_reserve_bo_failed;
	}

	ret = amdgpu_bo_pin(bo, AMDGPU_GEM_DOMAIN_GTT);
	if (ret) {
		pr_err("Failed to pin bo. ret %d\n", ret);
		goto err_pin_bo_failed;
	}

	ret = amdgpu_ttm_alloc_gart(&bo->tbo);
	if (ret) {
		pr_err("Failed to bind bo to GART. ret %d\n", ret);
		goto err_map_bo_gart_failed;
	}

	amdgpu_amdkfd_remove_eviction_fence(
		bo, bo->vm_bo->vm->process_info->eviction_fence);

	amdgpu_bo_unreserve(bo);

	*bo_gart = amdgpu_bo_ref(bo);

	return 0;

err_map_bo_gart_failed:
	amdgpu_bo_unpin(bo);
err_pin_bo_failed:
	amdgpu_bo_unreserve(bo);
err_reserve_bo_failed:

	return ret;
}

/** amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel() - Map a GTT BO for kernel CPU access
 *
 * @mem: Buffer object to be mapped for CPU access
 * @kptr[out]: pointer in kernel CPU address space
 * @size[out]: size of the buffer
 *
 * Pins the BO and maps it for kernel CPU access. The eviction fence is removed
 * from the BO, since pinned BOs cannot be evicted. The bo must remain on the
 * validate_list, so the GPU mapping can be restored after a page table was
 * evicted.
 *
 * Return: 0 on success, error code on failure
 */
int amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel(struct kgd_mem *mem,
					     void **kptr, uint64_t *size)
{
	int ret;
	struct amdgpu_bo *bo = mem->bo;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm)) {
		pr_err("userptr can't be mapped to kernel\n");
		return -EINVAL;
	}

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
		bo, mem->process_info->eviction_fence);

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

/** amdgpu_amdkfd_gpuvm_map_gtt_bo_to_kernel() - Unmap a GTT BO for kernel CPU access
 *
 * @mem: Buffer object to be unmapped for CPU access
 *
 * Removes the kernel CPU mapping and unpins the BO. It does not restore the
 * eviction fence, so this function should only be used for cleanup before the
 * BO is destroyed.
 */
void amdgpu_amdkfd_gpuvm_unmap_gtt_bo_from_kernel(struct kgd_mem *mem)
{
	struct amdgpu_bo *bo = mem->bo;

	(void)amdgpu_bo_reserve(bo, true);
	amdgpu_bo_kunmap(bo);
	amdgpu_bo_unpin(bo);
	amdgpu_bo_unreserve(bo);
}

int amdgpu_amdkfd_gpuvm_get_vm_fault_info(struct amdgpu_device *adev,
					  struct kfd_vm_fault_info *mem)
{
	if (atomic_read(&adev->gmc.vm_fault_info_updated) == 1) {
		*mem = *adev->gmc.vm_fault_info;
		mb(); /* make sure read happened */
		atomic_set(&adev->gmc.vm_fault_info_updated, 0);
	}
	return 0;
}

static int import_obj_create(struct amdgpu_device *adev,
			     struct dma_buf *dma_buf,
			     struct drm_gem_object *obj,
			     uint64_t va, void *drm_priv,
			     struct kgd_mem **mem, uint64_t *size,
			     uint64_t *mmap_offset)
{
	struct amdgpu_vm *avm = drm_priv_to_vm(drm_priv);
	struct amdgpu_bo *bo;
	int ret;

	bo = gem_to_amdgpu_bo(obj);
	if (!(bo->preferred_domains & (AMDGPU_GEM_DOMAIN_VRAM |
				    AMDGPU_GEM_DOMAIN_GTT)))
		/* Only VRAM and GTT BOs are supported */
		return -EINVAL;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (!*mem)
		return -ENOMEM;

	ret = drm_vma_node_allow(&obj->vma_node, drm_priv);
	if (ret)
		goto err_free_mem;

	if (size)
		*size = amdgpu_bo_size(bo);

	if (mmap_offset)
		*mmap_offset = amdgpu_bo_mmap_offset(bo);

	INIT_LIST_HEAD(&(*mem)->attachments);
	mutex_init(&(*mem)->lock);

	(*mem)->alloc_flags =
		((bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM) ?
		KFD_IOC_ALLOC_MEM_FLAGS_VRAM : KFD_IOC_ALLOC_MEM_FLAGS_GTT)
		| KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE
		| KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE;

	get_dma_buf(dma_buf);
	(*mem)->dmabuf = dma_buf;
	(*mem)->bo = bo;
	(*mem)->va = va;
	(*mem)->domain = (bo->preferred_domains & AMDGPU_GEM_DOMAIN_VRAM) &&
			 !(adev->flags & AMD_IS_APU) ?
			 AMDGPU_GEM_DOMAIN_VRAM : AMDGPU_GEM_DOMAIN_GTT;

	(*mem)->mapped_to_gpu_memory = 0;
	(*mem)->process_info = avm->process_info;
	add_kgd_mem_to_kfd_bo_list(*mem, avm->process_info, false);
	amdgpu_sync_create(&(*mem)->sync);
	(*mem)->is_imported = true;

	mutex_lock(&avm->process_info->lock);
	if (avm->process_info->eviction_fence &&
	    !dma_fence_is_signaled(&avm->process_info->eviction_fence->base))
		ret = amdgpu_amdkfd_bo_validate_and_fence(bo, (*mem)->domain,
				&avm->process_info->eviction_fence->base);
	mutex_unlock(&avm->process_info->lock);
	if (ret)
		goto err_remove_mem;

	return 0;

err_remove_mem:
	remove_kgd_mem_from_kfd_bo_list(*mem, avm->process_info);
	drm_vma_node_revoke(&obj->vma_node, drm_priv);
err_free_mem:
	kfree(*mem);
	return ret;
}

int amdgpu_amdkfd_gpuvm_import_dmabuf_fd(struct amdgpu_device *adev, int fd,
					 uint64_t va, void *drm_priv,
					 struct kgd_mem **mem, uint64_t *size,
					 uint64_t *mmap_offset)
{
	struct drm_gem_object *obj;
	uint32_t handle;
	int ret;

	ret = drm_gem_prime_fd_to_handle(&adev->ddev, adev->kfd.client.file, fd,
					 &handle);
	if (ret)
		return ret;
	obj = drm_gem_object_lookup(adev->kfd.client.file, handle);
	if (!obj) {
		ret = -EINVAL;
		goto err_release_handle;
	}

	ret = import_obj_create(adev, obj->dma_buf, obj, va, drm_priv, mem, size,
				mmap_offset);
	if (ret)
		goto err_put_obj;

	(*mem)->gem_handle = handle;

	return 0;

err_put_obj:
	drm_gem_object_put(obj);
err_release_handle:
	drm_gem_handle_delete(adev->kfd.client.file, handle);
	return ret;
}

int amdgpu_amdkfd_gpuvm_export_dmabuf(struct kgd_mem *mem,
				      struct dma_buf **dma_buf)
{
	int ret;

	mutex_lock(&mem->lock);
	ret = kfd_mem_export_dmabuf(mem);
	if (ret)
		goto out;

	get_dma_buf(mem->dmabuf);
	*dma_buf = mem->dmabuf;
out:
	mutex_unlock(&mem->lock);
	return ret;
}

/* Evict a userptr BO by stopping the queues if necessary
 *
 * Runs in MMU notifier, may be in RECLAIM_FS context. This means it
 * cannot do any memory allocations, and cannot take any locks that
 * are held elsewhere while allocating memory.
 *
 * It doesn't do anything to the BO itself. The real work happens in
 * restore, where we get updated page addresses. This function only
 * ensures that GPU access to the BO is stopped.
 */
int amdgpu_amdkfd_evict_userptr(struct mmu_interval_notifier *mni,
				unsigned long cur_seq, struct kgd_mem *mem)
{
	struct amdkfd_process_info *process_info = mem->process_info;
	int r = 0;

	/* Do not process MMU notifications during CRIU restore until
	 * KFD_CRIU_OP_RESUME IOCTL is received
	 */
	if (READ_ONCE(process_info->block_mmu_notifications))
		return 0;

	mutex_lock(&process_info->notifier_lock);
	mmu_interval_set_seq(mni, cur_seq);

	mem->invalid++;
	if (++process_info->evicted_bos == 1) {
		/* First eviction, stop the queues */
		r = kgd2kfd_quiesce_mm(mni->mm,
				       KFD_QUEUE_EVICTION_TRIGGER_USERPTR);

		if (r && r != -ESRCH)
			pr_err("Failed to quiesce KFD\n");

		if (r != -ESRCH)
			queue_delayed_work(system_freezable_wq,
				&process_info->restore_userptr_work,
				msecs_to_jiffies(AMDGPU_USERPTR_RESTORE_DELAY_MS));
	}
	mutex_unlock(&process_info->notifier_lock);

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
	uint32_t invalid;
	int ret = 0;

	mutex_lock(&process_info->notifier_lock);

	/* Move all invalidated BOs to the userptr_inval_list */
	list_for_each_entry_safe(mem, tmp_mem,
				 &process_info->userptr_valid_list,
				 validate_list)
		if (mem->invalid)
			list_move_tail(&mem->validate_list,
				       &process_info->userptr_inval_list);

	/* Go through userptr_inval_list and update any invalid user_pages */
	list_for_each_entry(mem, &process_info->userptr_inval_list,
			    validate_list) {
		invalid = mem->invalid;
		if (!invalid)
			/* BO hasn't been invalidated since the last
			 * revalidation attempt. Keep its page list.
			 */
			continue;

		bo = mem->bo;

		amdgpu_ttm_tt_discard_user_pages(bo->tbo.ttm, mem->range);
		mem->range = NULL;

		/* BO reservations and getting user pages (hmm_range_fault)
		 * must happen outside the notifier lock
		 */
		mutex_unlock(&process_info->notifier_lock);

		/* Move the BO to system (CPU) domain if necessary to unmap
		 * and free the SG table
		 */
		if (bo->tbo.resource->mem_type != TTM_PL_SYSTEM) {
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
		}

		/* Get updated user pages */
		ret = amdgpu_ttm_tt_get_user_pages(bo, bo->tbo.ttm->pages,
						   &mem->range);
		if (ret) {
			pr_debug("Failed %d to get user pages\n", ret);

			/* Return -EFAULT bad address error as success. It will
			 * fail later with a VM fault if the GPU tries to access
			 * it. Better than hanging indefinitely with stalled
			 * user mode queues.
			 *
			 * Return other error -EBUSY or -ENOMEM to retry restore
			 */
			if (ret != -EFAULT)
				return ret;

			ret = 0;
		}

		mutex_lock(&process_info->notifier_lock);

		/* Mark the BO as valid unless it was invalidated
		 * again concurrently.
		 */
		if (mem->invalid != invalid) {
			ret = -EAGAIN;
			goto unlock_out;
		}
		 /* set mem valid if mem has hmm range associated */
		if (mem->range)
			mem->invalid = 0;
	}

unlock_out:
	mutex_unlock(&process_info->notifier_lock);

	return ret;
}

/* Validate invalid userptr BOs
 *
 * Validates BOs on the userptr_inval_list. Also updates GPUVM page tables
 * with new page addresses and waits for the page table updates to complete.
 */
static int validate_invalid_user_pages(struct amdkfd_process_info *process_info)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct amdgpu_sync sync;
	struct drm_exec exec;

	struct amdgpu_vm *peer_vm;
	struct kgd_mem *mem, *tmp_mem;
	struct amdgpu_bo *bo;
	int ret;

	amdgpu_sync_create(&sync);

	drm_exec_init(&exec, 0, 0);
	/* Reserve all BOs and page tables for validation */
	drm_exec_until_all_locked(&exec) {
		/* Reserve all the page directories */
		list_for_each_entry(peer_vm, &process_info->vm_list_head,
				    vm_list_node) {
			ret = amdgpu_vm_lock_pd(peer_vm, &exec, 2);
			drm_exec_retry_on_contention(&exec);
			if (unlikely(ret))
				goto unreserve_out;
		}

		/* Reserve the userptr_inval_list entries to resv_list */
		list_for_each_entry(mem, &process_info->userptr_inval_list,
				    validate_list) {
			struct drm_gem_object *gobj;

			gobj = &mem->bo->tbo.base;
			ret = drm_exec_prepare_obj(&exec, gobj, 1);
			drm_exec_retry_on_contention(&exec);
			if (unlikely(ret))
				goto unreserve_out;
		}
	}

	ret = process_validate_vms(process_info, NULL);
	if (ret)
		goto unreserve_out;

	/* Validate BOs and update GPUVM page tables */
	list_for_each_entry_safe(mem, tmp_mem,
				 &process_info->userptr_inval_list,
				 validate_list) {
		struct kfd_mem_attachment *attachment;

		bo = mem->bo;

		/* Validate the BO if we got user pages */
		if (bo->tbo.ttm->pages[0]) {
			amdgpu_bo_placement_from_domain(bo, mem->domain);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (ret) {
				pr_err("%s: failed to validate BO\n", __func__);
				goto unreserve_out;
			}
		}

		/* Update mapping. If the BO was not validated
		 * (because we couldn't get user pages), this will
		 * clear the page table entries, which will result in
		 * VM faults if the GPU tries to access the invalid
		 * memory.
		 */
		list_for_each_entry(attachment, &mem->attachments, list) {
			if (!attachment->is_mapped)
				continue;

			kfd_mem_dmaunmap_attachment(mem, attachment);
			ret = update_gpuvm_pte(mem, attachment, &sync);
			if (ret) {
				pr_err("%s: update PTE failed\n", __func__);
				/* make sure this gets validated again */
				mutex_lock(&process_info->notifier_lock);
				mem->invalid++;
				mutex_unlock(&process_info->notifier_lock);
				goto unreserve_out;
			}
		}
	}

	/* Update page directories */
	ret = process_update_pds(process_info, &sync);

unreserve_out:
	drm_exec_fini(&exec);
	amdgpu_sync_wait(&sync, false);
	amdgpu_sync_free(&sync);

	return ret;
}

/* Confirm that all user pages are valid while holding the notifier lock
 *
 * Moves valid BOs from the userptr_inval_list back to userptr_val_list.
 */
static int confirm_valid_user_pages_locked(struct amdkfd_process_info *process_info)
{
	struct kgd_mem *mem, *tmp_mem;
	int ret = 0;

	list_for_each_entry_safe(mem, tmp_mem,
				 &process_info->userptr_inval_list,
				 validate_list) {
		bool valid;

		/* keep mem without hmm range at userptr_inval_list */
		if (!mem->range)
			continue;

		/* Only check mem with hmm range associated */
		valid = amdgpu_ttm_tt_get_user_pages_done(
					mem->bo->tbo.ttm, mem->range);

		mem->range = NULL;
		if (!valid) {
			WARN(!mem->invalid, "Invalid BO not marked invalid");
			ret = -EAGAIN;
			continue;
		}

		if (mem->invalid) {
			WARN(1, "Valid BO is marked invalid");
			ret = -EAGAIN;
			continue;
		}

		list_move_tail(&mem->validate_list,
			       &process_info->userptr_valid_list);
	}

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
	uint32_t evicted_bos;

	mutex_lock(&process_info->notifier_lock);
	evicted_bos = process_info->evicted_bos;
	mutex_unlock(&process_info->notifier_lock);
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
		if (validate_invalid_user_pages(process_info))
			goto unlock_out;
	}
	/* Final check for concurrent evicton and atomic update. If
	 * another eviction happens after successful update, it will
	 * be a first eviction that calls quiesce_mm. The eviction
	 * reference counting inside KFD will handle this case.
	 */
	mutex_lock(&process_info->notifier_lock);
	if (process_info->evicted_bos != evicted_bos)
		goto unlock_notifier_out;

	if (confirm_valid_user_pages_locked(process_info)) {
		WARN(1, "User pages unexpectedly invalid");
		goto unlock_notifier_out;
	}

	process_info->evicted_bos = evicted_bos = 0;

	if (kgd2kfd_resume_mm(mm)) {
		pr_err("%s: Failed to resume KFD\n", __func__);
		/* No recovery from this failure. Probably the CP is
		 * hanging. No point trying again.
		 */
	}

unlock_notifier_out:
	mutex_unlock(&process_info->notifier_lock);
unlock_out:
	mutex_unlock(&process_info->lock);

	/* If validation failed, reschedule another attempt */
	if (evicted_bos) {
		queue_delayed_work(system_freezable_wq,
			&process_info->restore_userptr_work,
			msecs_to_jiffies(AMDGPU_USERPTR_RESTORE_DELAY_MS));

		kfd_smi_event_queue_restore_rescheduled(mm);
	}
	mmput(mm);
	put_task_struct(usertask);
}

static void replace_eviction_fence(struct dma_fence __rcu **ef,
				   struct dma_fence *new_ef)
{
	struct dma_fence *old_ef = rcu_replace_pointer(*ef, new_ef, true
		/* protected by process_info->lock */);

	/* If we're replacing an unsignaled eviction fence, that fence will
	 * never be signaled, and if anyone is still waiting on that fence,
	 * they will hang forever. This should never happen. We should only
	 * replace the fence in restore_work that only gets scheduled after
	 * eviction work signaled the fence.
	 */
	WARN_ONCE(!dma_fence_is_signaled(old_ef),
		  "Replacing unsignaled eviction fence");
	dma_fence_put(old_ef);
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
int amdgpu_amdkfd_gpuvm_restore_process_bos(void *info, struct dma_fence __rcu **ef)
{
	struct amdkfd_process_info *process_info = info;
	struct amdgpu_vm *peer_vm;
	struct kgd_mem *mem;
	struct list_head duplicate_save;
	struct amdgpu_sync sync_obj;
	unsigned long failed_size = 0;
	unsigned long total_size = 0;
	struct drm_exec exec;
	int ret;

	INIT_LIST_HEAD(&duplicate_save);

	mutex_lock(&process_info->lock);

	drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES, 0);
	drm_exec_until_all_locked(&exec) {
		list_for_each_entry(peer_vm, &process_info->vm_list_head,
				    vm_list_node) {
			ret = amdgpu_vm_lock_pd(peer_vm, &exec, 2);
			drm_exec_retry_on_contention(&exec);
			if (unlikely(ret)) {
				pr_err("Locking VM PD failed, ret: %d\n", ret);
				goto ttm_reserve_fail;
			}
		}

		/* Reserve all BOs and page tables/directory. Add all BOs from
		 * kfd_bo_list to ctx.list
		 */
		list_for_each_entry(mem, &process_info->kfd_bo_list,
				    validate_list) {
			struct drm_gem_object *gobj;

			gobj = &mem->bo->tbo.base;
			ret = drm_exec_prepare_obj(&exec, gobj, 1);
			drm_exec_retry_on_contention(&exec);
			if (unlikely(ret)) {
				pr_err("drm_exec_prepare_obj failed, ret: %d\n", ret);
				goto ttm_reserve_fail;
			}
		}
	}

	amdgpu_sync_create(&sync_obj);

	/* Validate BOs managed by KFD */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
			    validate_list) {

		struct amdgpu_bo *bo = mem->bo;
		uint32_t domain = mem->domain;
		struct dma_resv_iter cursor;
		struct dma_fence *fence;

		total_size += amdgpu_bo_size(bo);

		ret = amdgpu_amdkfd_bo_validate(bo, domain, false);
		if (ret) {
			pr_debug("Memory eviction: Validate BOs failed\n");
			failed_size += amdgpu_bo_size(bo);
			ret = amdgpu_amdkfd_bo_validate(bo,
						AMDGPU_GEM_DOMAIN_GTT, false);
			if (ret) {
				pr_debug("Memory eviction: Try again\n");
				goto validate_map_fail;
			}
		}
		dma_resv_for_each_fence(&cursor, bo->tbo.base.resv,
					DMA_RESV_USAGE_KERNEL, fence) {
			ret = amdgpu_sync_fence(&sync_obj, fence);
			if (ret) {
				pr_debug("Memory eviction: Sync BO fence failed. Try again\n");
				goto validate_map_fail;
			}
		}
	}

	if (failed_size)
		pr_debug("0x%lx/0x%lx in system\n", failed_size, total_size);

	/* Validate PDs, PTs and evicted DMABuf imports last. Otherwise BO
	 * validations above would invalidate DMABuf imports again.
	 */
	ret = process_validate_vms(process_info, &exec.ticket);
	if (ret) {
		pr_debug("Validating VMs failed, ret: %d\n", ret);
		goto validate_map_fail;
	}

	/* Update mappings managed by KFD. */
	list_for_each_entry(mem, &process_info->kfd_bo_list,
			    validate_list) {
		struct kfd_mem_attachment *attachment;

		list_for_each_entry(attachment, &mem->attachments, list) {
			if (!attachment->is_mapped)
				continue;

			kfd_mem_dmaunmap_attachment(mem, attachment);
			ret = update_gpuvm_pte(mem, attachment, &sync_obj);
			if (ret) {
				pr_debug("Memory eviction: update PTE failed. Try again\n");
				goto validate_map_fail;
			}
		}
	}

	/* Update mappings not managed by KFD */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			vm_list_node) {
		struct amdgpu_device *adev = amdgpu_ttm_adev(
			peer_vm->root.bo->tbo.bdev);

		ret = amdgpu_vm_handle_moved(adev, peer_vm, &exec.ticket);
		if (ret) {
			pr_debug("Memory eviction: handle moved failed. Try again\n");
			goto validate_map_fail;
		}
	}

	/* Update page directories */
	ret = process_update_pds(process_info, &sync_obj);
	if (ret) {
		pr_debug("Memory eviction: update PDs failed. Try again\n");
		goto validate_map_fail;
	}

	/* Sync with fences on all the page tables. They implicitly depend on any
	 * move fences from amdgpu_vm_handle_moved above.
	 */
	ret = process_sync_pds_resv(process_info, &sync_obj);
	if (ret) {
		pr_debug("Memory eviction: Failed to sync to PD BO moving fence. Try again\n");
		goto validate_map_fail;
	}

	/* Wait for validate and PT updates to finish */
	amdgpu_sync_wait(&sync_obj, false);

	/* The old eviction fence may be unsignaled if restore happens
	 * after a GPU reset or suspend/resume. Keep the old fence in that
	 * case. Otherwise release the old eviction fence and create new
	 * one, because fence only goes from unsignaled to signaled once
	 * and cannot be reused. Use context and mm from the old fence.
	 *
	 * If an old eviction fence signals after this check, that's OK.
	 * Anyone signaling an eviction fence must stop the queues first
	 * and schedule another restore worker.
	 */
	if (dma_fence_is_signaled(&process_info->eviction_fence->base)) {
		struct amdgpu_amdkfd_fence *new_fence =
			amdgpu_amdkfd_fence_create(
				process_info->eviction_fence->base.context,
				process_info->eviction_fence->mm,
				NULL);

		if (!new_fence) {
			pr_err("Failed to create eviction fence\n");
			ret = -ENOMEM;
			goto validate_map_fail;
		}
		dma_fence_put(&process_info->eviction_fence->base);
		process_info->eviction_fence = new_fence;
		replace_eviction_fence(ef, dma_fence_get(&new_fence->base));
	} else {
		WARN_ONCE(*ef != &process_info->eviction_fence->base,
			  "KFD eviction fence doesn't match KGD process_info");
	}

	/* Attach new eviction fence to all BOs except pinned ones */
	list_for_each_entry(mem, &process_info->kfd_bo_list, validate_list) {
		if (mem->bo->tbo.pin_count)
			continue;

		dma_resv_add_fence(mem->bo->tbo.base.resv,
				   &process_info->eviction_fence->base,
				   DMA_RESV_USAGE_BOOKKEEP);
	}
	/* Attach eviction fence to PD / PT BOs and DMABuf imports */
	list_for_each_entry(peer_vm, &process_info->vm_list_head,
			    vm_list_node) {
		struct amdgpu_bo *bo = peer_vm->root.bo;

		dma_resv_add_fence(bo->tbo.base.resv,
				   &process_info->eviction_fence->base,
				   DMA_RESV_USAGE_BOOKKEEP);
	}

validate_map_fail:
	amdgpu_sync_free(&sync_obj);
ttm_reserve_fail:
	drm_exec_fini(&exec);
	mutex_unlock(&process_info->lock);
	return ret;
}

int amdgpu_amdkfd_add_gws_to_process(void *info, void *gws, struct kgd_mem **mem)
{
	struct amdkfd_process_info *process_info = (struct amdkfd_process_info *)info;
	struct amdgpu_bo *gws_bo = (struct amdgpu_bo *)gws;
	int ret;

	if (!info || !gws)
		return -EINVAL;

	*mem = kzalloc(sizeof(struct kgd_mem), GFP_KERNEL);
	if (!*mem)
		return -ENOMEM;

	mutex_init(&(*mem)->lock);
	INIT_LIST_HEAD(&(*mem)->attachments);
	(*mem)->bo = amdgpu_bo_ref(gws_bo);
	(*mem)->domain = AMDGPU_GEM_DOMAIN_GWS;
	(*mem)->process_info = process_info;
	add_kgd_mem_to_kfd_bo_list(*mem, process_info, false);
	amdgpu_sync_create(&(*mem)->sync);


	/* Validate gws bo the first time it is added to process */
	mutex_lock(&(*mem)->process_info->lock);
	ret = amdgpu_bo_reserve(gws_bo, false);
	if (unlikely(ret)) {
		pr_err("Reserve gws bo failed %d\n", ret);
		goto bo_reservation_failure;
	}

	ret = amdgpu_amdkfd_bo_validate(gws_bo, AMDGPU_GEM_DOMAIN_GWS, true);
	if (ret) {
		pr_err("GWS BO validate failed %d\n", ret);
		goto bo_validation_failure;
	}
	/* GWS resource is shared b/t amdgpu and amdkfd
	 * Add process eviction fence to bo so they can
	 * evict each other.
	 */
	ret = dma_resv_reserve_fences(gws_bo->tbo.base.resv, 1);
	if (ret)
		goto reserve_shared_fail;
	dma_resv_add_fence(gws_bo->tbo.base.resv,
			   &process_info->eviction_fence->base,
			   DMA_RESV_USAGE_BOOKKEEP);
	amdgpu_bo_unreserve(gws_bo);
	mutex_unlock(&(*mem)->process_info->lock);

	return ret;

reserve_shared_fail:
bo_validation_failure:
	amdgpu_bo_unreserve(gws_bo);
bo_reservation_failure:
	mutex_unlock(&(*mem)->process_info->lock);
	amdgpu_sync_free(&(*mem)->sync);
	remove_kgd_mem_from_kfd_bo_list(*mem, process_info);
	amdgpu_bo_unref(&gws_bo);
	mutex_destroy(&(*mem)->lock);
	kfree(*mem);
	*mem = NULL;
	return ret;
}

int amdgpu_amdkfd_remove_gws_from_process(void *info, void *mem)
{
	int ret;
	struct amdkfd_process_info *process_info = (struct amdkfd_process_info *)info;
	struct kgd_mem *kgd_mem = (struct kgd_mem *)mem;
	struct amdgpu_bo *gws_bo = kgd_mem->bo;

	/* Remove BO from process's validate list so restore worker won't touch
	 * it anymore
	 */
	remove_kgd_mem_from_kfd_bo_list(kgd_mem, process_info);

	ret = amdgpu_bo_reserve(gws_bo, false);
	if (unlikely(ret)) {
		pr_err("Reserve gws bo failed %d\n", ret);
		//TODO add BO back to validate_list?
		return ret;
	}
	amdgpu_amdkfd_remove_eviction_fence(gws_bo,
			process_info->eviction_fence);
	amdgpu_bo_unreserve(gws_bo);
	amdgpu_sync_free(&kgd_mem->sync);
	amdgpu_bo_unref(&gws_bo);
	mutex_destroy(&kgd_mem->lock);
	kfree(mem);
	return 0;
}

/* Returns GPU-specific tiling mode information */
int amdgpu_amdkfd_get_tile_config(struct amdgpu_device *adev,
				struct tile_config *config)
{
	config->gb_addr_config = adev->gfx.config.gb_addr_config;
	config->tile_config_ptr = adev->gfx.config.tile_mode_array;
	config->num_tile_configs =
			ARRAY_SIZE(adev->gfx.config.tile_mode_array);
	config->macro_tile_config_ptr =
			adev->gfx.config.macrotile_mode_array;
	config->num_macro_tile_configs =
			ARRAY_SIZE(adev->gfx.config.macrotile_mode_array);

	/* Those values are not set from GFX9 onwards */
	config->num_banks = adev->gfx.config.num_banks;
	config->num_ranks = adev->gfx.config.num_ranks;

	return 0;
}

bool amdgpu_amdkfd_bo_mapped_to_dev(void *drm_priv, struct kgd_mem *mem)
{
	struct amdgpu_vm *vm = drm_priv_to_vm(drm_priv);
	struct kfd_mem_attachment *entry;

	list_for_each_entry(entry, &mem->attachments, list) {
		if (entry->is_mapped && entry->bo_va->base.vm == vm)
			return true;
	}
	return false;
}

#if defined(CONFIG_DEBUG_FS)

int kfd_debugfs_kfd_mem_limits(struct seq_file *m, void *data)
{

	spin_lock(&kfd_mem_limit.mem_limit_lock);
	seq_printf(m, "System mem used %lldM out of %lluM\n",
		  (kfd_mem_limit.system_mem_used >> 20),
		  (kfd_mem_limit.max_system_mem_limit >> 20));
	seq_printf(m, "TTM mem used %lldM out of %lluM\n",
		  (kfd_mem_limit.ttm_mem_used >> 20),
		  (kfd_mem_limit.max_ttm_mem_limit >> 20));
	spin_unlock(&kfd_mem_limit.mem_limit_lock);

	return 0;
}

#endif

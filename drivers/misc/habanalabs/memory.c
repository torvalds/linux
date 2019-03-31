// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <uapi/misc/habanalabs.h>
#include "habanalabs.h"
#include "include/hw_ip/mmu/mmu_general.h"

#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/genalloc.h>

#define PGS_IN_2MB_PAGE	(PAGE_SIZE_2MB >> PAGE_SHIFT)
#define HL_MMU_DEBUG	0

/*
 * The va ranges in context object contain a list with the available chunks of
 * device virtual memory.
 * There is one range for host allocations and one for DRAM allocations.
 *
 * On initialization each range contains one chunk of all of its available
 * virtual range which is a half of the total device virtual range.
 *
 * On each mapping of physical pages, a suitable virtual range chunk (with a
 * minimum size) is selected from the list. If the chunk size equals the
 * requested size, the chunk is returned. Otherwise, the chunk is split into
 * two chunks - one to return as result and a remainder to stay in the list.
 *
 * On each Unmapping of a virtual address, the relevant virtual chunk is
 * returned to the list. The chunk is added to the list and if its edges match
 * the edges of the adjacent chunks (means a contiguous chunk can be created),
 * the chunks are merged.
 *
 * On finish, the list is checked to have only one chunk of all the relevant
 * virtual range (which is a half of the device total virtual range).
 * If not (means not all mappings were unmapped), a warning is printed.
 */

/*
 * alloc_device_memory - allocate device memory
 *
 * @ctx                 : current context
 * @args                : host parameters containing the requested size
 * @ret_handle          : result handle
 *
 * This function does the following:
 * - Allocate the requested size rounded up to 2MB pages
 * - Return unique handle
 */
static int alloc_device_memory(struct hl_ctx *ctx, struct hl_mem_in *args,
				u32 *ret_handle)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_vm *vm = &hdev->vm;
	struct hl_vm_phys_pg_pack *phys_pg_pack;
	u64 paddr = 0, total_size, num_pgs, i;
	u32 num_curr_pgs, page_size, page_shift;
	int handle, rc;
	bool contiguous;

	num_curr_pgs = 0;
	page_size = hdev->asic_prop.dram_page_size;
	page_shift = __ffs(page_size);
	num_pgs = (args->alloc.mem_size + (page_size - 1)) >> page_shift;
	total_size = num_pgs << page_shift;

	contiguous = args->flags & HL_MEM_CONTIGUOUS;

	if (contiguous) {
		paddr = (u64) gen_pool_alloc(vm->dram_pg_pool, total_size);
		if (!paddr) {
			dev_err(hdev->dev,
				"failed to allocate %llu huge contiguous pages\n",
				num_pgs);
			return -ENOMEM;
		}
	}

	phys_pg_pack = kzalloc(sizeof(*phys_pg_pack), GFP_KERNEL);
	if (!phys_pg_pack) {
		rc = -ENOMEM;
		goto pages_pack_err;
	}

	phys_pg_pack->vm_type = VM_TYPE_PHYS_PACK;
	phys_pg_pack->asid = ctx->asid;
	phys_pg_pack->npages = num_pgs;
	phys_pg_pack->page_size = page_size;
	phys_pg_pack->total_size = total_size;
	phys_pg_pack->flags = args->flags;
	phys_pg_pack->contiguous = contiguous;

	phys_pg_pack->pages = kvmalloc_array(num_pgs, sizeof(u64), GFP_KERNEL);
	if (!phys_pg_pack->pages) {
		rc = -ENOMEM;
		goto pages_arr_err;
	}

	if (phys_pg_pack->contiguous) {
		for (i = 0 ; i < num_pgs ; i++)
			phys_pg_pack->pages[i] = paddr + i * page_size;
	} else {
		for (i = 0 ; i < num_pgs ; i++) {
			phys_pg_pack->pages[i] = (u64) gen_pool_alloc(
							vm->dram_pg_pool,
							page_size);
			if (!phys_pg_pack->pages[i]) {
				dev_err(hdev->dev,
					"Failed to allocate device memory (out of memory)\n");
				rc = -ENOMEM;
				goto page_err;
			}

			num_curr_pgs++;
		}
	}

	spin_lock(&vm->idr_lock);
	handle = idr_alloc(&vm->phys_pg_pack_handles, phys_pg_pack, 1, 0,
				GFP_ATOMIC);
	spin_unlock(&vm->idr_lock);

	if (handle < 0) {
		dev_err(hdev->dev, "Failed to get handle for page\n");
		rc = -EFAULT;
		goto idr_err;
	}

	for (i = 0 ; i < num_pgs ; i++)
		kref_get(&vm->dram_pg_pool_refcount);

	phys_pg_pack->handle = handle;

	atomic64_add(phys_pg_pack->total_size, &ctx->dram_phys_mem);
	atomic64_add(phys_pg_pack->total_size, &hdev->dram_used_mem);

	*ret_handle = handle;

	return 0;

idr_err:
page_err:
	if (!phys_pg_pack->contiguous)
		for (i = 0 ; i < num_curr_pgs ; i++)
			gen_pool_free(vm->dram_pg_pool, phys_pg_pack->pages[i],
					page_size);

	kvfree(phys_pg_pack->pages);
pages_arr_err:
	kfree(phys_pg_pack);
pages_pack_err:
	if (contiguous)
		gen_pool_free(vm->dram_pg_pool, paddr, total_size);

	return rc;
}

/*
 * get_userptr_from_host_va - initialize userptr structure from given host
 *                            virtual address
 *
 * @hdev                : habanalabs device structure
 * @args                : parameters containing the virtual address and size
 * @p_userptr           : pointer to result userptr structure
 *
 * This function does the following:
 * - Allocate userptr structure
 * - Pin the given host memory using the userptr structure
 * - Perform DMA mapping to have the DMA addresses of the pages
 */
static int get_userptr_from_host_va(struct hl_device *hdev,
		struct hl_mem_in *args, struct hl_userptr **p_userptr)
{
	struct hl_userptr *userptr;
	int rc;

	userptr = kzalloc(sizeof(*userptr), GFP_KERNEL);
	if (!userptr) {
		rc = -ENOMEM;
		goto userptr_err;
	}

	rc = hl_pin_host_memory(hdev, args->map_host.host_virt_addr,
			args->map_host.mem_size, userptr);
	if (rc) {
		dev_err(hdev->dev, "Failed to pin host memory\n");
		goto pin_err;
	}

	rc = hdev->asic_funcs->asic_dma_map_sg(hdev, userptr->sgt->sgl,
					userptr->sgt->nents, DMA_BIDIRECTIONAL);
	if (rc) {
		dev_err(hdev->dev, "failed to map sgt with DMA region\n");
		goto dma_map_err;
	}

	userptr->dma_mapped = true;
	userptr->dir = DMA_BIDIRECTIONAL;
	userptr->vm_type = VM_TYPE_USERPTR;

	*p_userptr = userptr;

	return 0;

dma_map_err:
	hl_unpin_host_memory(hdev, userptr);
pin_err:
	kfree(userptr);
userptr_err:

	return rc;
}

/*
 * free_userptr - free userptr structure
 *
 * @hdev                : habanalabs device structure
 * @userptr             : userptr to free
 *
 * This function does the following:
 * - Unpins the physical pages
 * - Frees the userptr structure
 */
static void free_userptr(struct hl_device *hdev, struct hl_userptr *userptr)
{
	hl_unpin_host_memory(hdev, userptr);
	kfree(userptr);
}

/*
 * dram_pg_pool_do_release - free DRAM pages pool
 *
 * @ref                 : pointer to reference object
 *
 * This function does the following:
 * - Frees the idr structure of physical pages handles
 * - Frees the generic pool of DRAM physical pages
 */
static void dram_pg_pool_do_release(struct kref *ref)
{
	struct hl_vm *vm = container_of(ref, struct hl_vm,
			dram_pg_pool_refcount);

	/*
	 * free the idr here as only here we know for sure that there are no
	 * allocated physical pages and hence there are no handles in use
	 */
	idr_destroy(&vm->phys_pg_pack_handles);
	gen_pool_destroy(vm->dram_pg_pool);
}

/*
 * free_phys_pg_pack   - free physical page pack
 *
 * @hdev               : habanalabs device structure
 * @phys_pg_pack       : physical page pack to free
 *
 * This function does the following:
 * - For DRAM memory only, iterate over the pack and free each physical block
 *   structure by returning it to the general pool
 * - Free the hl_vm_phys_pg_pack structure
 */
static void free_phys_pg_pack(struct hl_device *hdev,
		struct hl_vm_phys_pg_pack *phys_pg_pack)
{
	struct hl_vm *vm = &hdev->vm;
	u64 i;

	if (!phys_pg_pack->created_from_userptr) {
		if (phys_pg_pack->contiguous) {
			gen_pool_free(vm->dram_pg_pool, phys_pg_pack->pages[0],
					phys_pg_pack->total_size);

			for (i = 0; i < phys_pg_pack->npages ; i++)
				kref_put(&vm->dram_pg_pool_refcount,
					dram_pg_pool_do_release);
		} else {
			for (i = 0 ; i < phys_pg_pack->npages ; i++) {
				gen_pool_free(vm->dram_pg_pool,
						phys_pg_pack->pages[i],
						phys_pg_pack->page_size);
				kref_put(&vm->dram_pg_pool_refcount,
					dram_pg_pool_do_release);
			}
		}
	}

	kvfree(phys_pg_pack->pages);
	kfree(phys_pg_pack);
}

/*
 * free_device_memory - free device memory
 *
 * @ctx                  : current context
 * @handle              : handle of the memory chunk to free
 *
 * This function does the following:
 * - Free the device memory related to the given handle
 */
static int free_device_memory(struct hl_ctx *ctx, u32 handle)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_vm *vm = &hdev->vm;
	struct hl_vm_phys_pg_pack *phys_pg_pack;

	spin_lock(&vm->idr_lock);
	phys_pg_pack = idr_find(&vm->phys_pg_pack_handles, handle);
	if (phys_pg_pack) {
		if (atomic_read(&phys_pg_pack->mapping_cnt) > 0) {
			dev_err(hdev->dev, "handle %u is mapped, cannot free\n",
				handle);
			spin_unlock(&vm->idr_lock);
			return -EINVAL;
		}

		/*
		 * must remove from idr before the freeing of the physical
		 * pages as the refcount of the pool is also the trigger of the
		 * idr destroy
		 */
		idr_remove(&vm->phys_pg_pack_handles, handle);
		spin_unlock(&vm->idr_lock);

		atomic64_sub(phys_pg_pack->total_size, &ctx->dram_phys_mem);
		atomic64_sub(phys_pg_pack->total_size, &hdev->dram_used_mem);

		free_phys_pg_pack(hdev, phys_pg_pack);
	} else {
		spin_unlock(&vm->idr_lock);
		dev_err(hdev->dev,
			"free device memory failed, no match for handle %u\n",
			handle);
		return -EINVAL;
	}

	return 0;
}

/*
 * clear_va_list_locked - free virtual addresses list
 *
 * @hdev                : habanalabs device structure
 * @va_list             : list of virtual addresses to free
 *
 * This function does the following:
 * - Iterate over the list and free each virtual addresses block
 *
 * This function should be called only when va_list lock is taken
 */
static void clear_va_list_locked(struct hl_device *hdev,
		struct list_head *va_list)
{
	struct hl_vm_va_block *va_block, *tmp;

	list_for_each_entry_safe(va_block, tmp, va_list, node) {
		list_del(&va_block->node);
		kfree(va_block);
	}
}

/*
 * print_va_list_locked    - print virtual addresses list
 *
 * @hdev                : habanalabs device structure
 * @va_list             : list of virtual addresses to print
 *
 * This function does the following:
 * - Iterate over the list and print each virtual addresses block
 *
 * This function should be called only when va_list lock is taken
 */
static void print_va_list_locked(struct hl_device *hdev,
		struct list_head *va_list)
{
#if HL_MMU_DEBUG
	struct hl_vm_va_block *va_block;

	dev_dbg(hdev->dev, "print va list:\n");

	list_for_each_entry(va_block, va_list, node)
		dev_dbg(hdev->dev,
			"va block, start: 0x%llx, end: 0x%llx, size: %llu\n",
			va_block->start, va_block->end, va_block->size);
#endif
}

/*
 * merge_va_blocks_locked - merge a virtual block if possible
 *
 * @hdev                : pointer to the habanalabs device structure
 * @va_list             : pointer to the virtual addresses block list
 * @va_block            : virtual block to merge with adjacent blocks
 *
 * This function does the following:
 * - Merge the given blocks with the adjacent blocks if their virtual ranges
 *   create a contiguous virtual range
 *
 * This Function should be called only when va_list lock is taken
 */
static void merge_va_blocks_locked(struct hl_device *hdev,
		struct list_head *va_list, struct hl_vm_va_block *va_block)
{
	struct hl_vm_va_block *prev, *next;

	prev = list_prev_entry(va_block, node);
	if (&prev->node != va_list && prev->end + 1 == va_block->start) {
		prev->end = va_block->end;
		prev->size = prev->end - prev->start;
		list_del(&va_block->node);
		kfree(va_block);
		va_block = prev;
	}

	next = list_next_entry(va_block, node);
	if (&next->node != va_list && va_block->end + 1 == next->start) {
		next->start = va_block->start;
		next->size = next->end - next->start;
		list_del(&va_block->node);
		kfree(va_block);
	}
}

/*
 * add_va_block_locked - add a virtual block to the virtual addresses list
 *
 * @hdev                : pointer to the habanalabs device structure
 * @va_list             : pointer to the virtual addresses block list
 * @start               : start virtual address
 * @end                 : end virtual address
 *
 * This function does the following:
 * - Add the given block to the virtual blocks list and merge with other
 * blocks if a contiguous virtual block can be created
 *
 * This Function should be called only when va_list lock is taken
 */
static int add_va_block_locked(struct hl_device *hdev,
		struct list_head *va_list, u64 start, u64 end)
{
	struct hl_vm_va_block *va_block, *res = NULL;
	u64 size = end - start;

	print_va_list_locked(hdev, va_list);

	list_for_each_entry(va_block, va_list, node) {
		/* TODO: remove upon matureness */
		if (hl_mem_area_crosses_range(start, size, va_block->start,
				va_block->end)) {
			dev_err(hdev->dev,
				"block crossing ranges at start 0x%llx, end 0x%llx\n",
				va_block->start, va_block->end);
			return -EINVAL;
		}

		if (va_block->end < start)
			res = va_block;
	}

	va_block = kmalloc(sizeof(*va_block), GFP_KERNEL);
	if (!va_block)
		return -ENOMEM;

	va_block->start = start;
	va_block->end = end;
	va_block->size = size;

	if (!res)
		list_add(&va_block->node, va_list);
	else
		list_add(&va_block->node, &res->node);

	merge_va_blocks_locked(hdev, va_list, va_block);

	print_va_list_locked(hdev, va_list);

	return 0;
}

/*
 * add_va_block - wrapper for add_va_block_locked
 *
 * @hdev                : pointer to the habanalabs device structure
 * @va_list             : pointer to the virtual addresses block list
 * @start               : start virtual address
 * @end                 : end virtual address
 *
 * This function does the following:
 * - Takes the list lock and calls add_va_block_locked
 */
static inline int add_va_block(struct hl_device *hdev,
		struct hl_va_range *va_range, u64 start, u64 end)
{
	int rc;

	mutex_lock(&va_range->lock);
	rc = add_va_block_locked(hdev, &va_range->list, start, end);
	mutex_unlock(&va_range->lock);

	return rc;
}

/*
 * get_va_block - get a virtual block with the requested size
 *
 * @hdev            : pointer to the habanalabs device structure
 * @va_range        : pointer to the virtual addresses range
 * @size            : requested block size
 * @hint_addr       : hint for request address by the user
 * @is_userptr      : is host or DRAM memory
 *
 * This function does the following:
 * - Iterate on the virtual block list to find a suitable virtual block for the
 *   requested size
 * - Reserve the requested block and update the list
 * - Return the start address of the virtual block
 */
static u64 get_va_block(struct hl_device *hdev,
		struct hl_va_range *va_range, u64 size, u64 hint_addr,
		bool is_userptr)
{
	struct hl_vm_va_block *va_block, *new_va_block = NULL;
	u64 valid_start, valid_size, prev_start, prev_end, page_mask,
		res_valid_start = 0, res_valid_size = 0;
	u32 page_size;
	bool add_prev = false;

	if (is_userptr) {
		/*
		 * We cannot know if the user allocated memory with huge pages
		 * or not, hence we continue with the biggest possible
		 * granularity.
		 */
		page_size = PAGE_SIZE_2MB;
		page_mask = PAGE_MASK_2MB;
	} else {
		page_size = hdev->asic_prop.dram_page_size;
		page_mask = ~((u64)page_size - 1);
	}

	mutex_lock(&va_range->lock);

	print_va_list_locked(hdev, &va_range->list);

	list_for_each_entry(va_block, &va_range->list, node) {
		/* calc the first possible aligned addr */
		valid_start = va_block->start;


		if (valid_start & (page_size - 1)) {
			valid_start &= page_mask;
			valid_start += page_size;
			if (valid_start > va_block->end)
				continue;
		}

		valid_size = va_block->end - valid_start;

		if (valid_size >= size &&
			(!new_va_block || valid_size < res_valid_size)) {

			new_va_block = va_block;
			res_valid_start = valid_start;
			res_valid_size = valid_size;
		}

		if (hint_addr && hint_addr >= valid_start &&
				((hint_addr + size) <= va_block->end)) {
			new_va_block = va_block;
			res_valid_start = hint_addr;
			res_valid_size = valid_size;
			break;
		}
	}

	if (!new_va_block) {
		dev_err(hdev->dev, "no available va block for size %llu\n",
				size);
		goto out;
	}

	if (res_valid_start > new_va_block->start) {
		prev_start = new_va_block->start;
		prev_end = res_valid_start - 1;

		new_va_block->start = res_valid_start;
		new_va_block->size = res_valid_size;

		add_prev = true;
	}

	if (new_va_block->size > size) {
		new_va_block->start += size;
		new_va_block->size = new_va_block->end - new_va_block->start;
	} else {
		list_del(&new_va_block->node);
		kfree(new_va_block);
	}

	if (add_prev)
		add_va_block_locked(hdev, &va_range->list, prev_start,
				prev_end);

	print_va_list_locked(hdev, &va_range->list);
out:
	mutex_unlock(&va_range->lock);

	return res_valid_start;
}

/*
 * get_sg_info - get number of pages and the DMA address from SG list
 *
 * @sg                 : the SG list
 * @dma_addr           : pointer to DMA address to return
 *
 * Calculate the number of consecutive pages described by the SG list. Take the
 * offset of the address in the first page, add to it the length and round it up
 * to the number of needed pages.
 */
static u32 get_sg_info(struct scatterlist *sg, dma_addr_t *dma_addr)
{
	*dma_addr = sg_dma_address(sg);

	return ((((*dma_addr) & (PAGE_SIZE - 1)) + sg_dma_len(sg)) +
			(PAGE_SIZE - 1)) >> PAGE_SHIFT;
}

/*
 * init_phys_pg_pack_from_userptr - initialize physical page pack from host
 *                                   memory
 *
 * @ctx                : current context
 * @userptr            : userptr to initialize from
 * @pphys_pg_pack      : res pointer
 *
 * This function does the following:
 * - Pin the physical pages related to the given virtual block
 * - Create a physical page pack from the physical pages related to the given
 *   virtual block
 */
static int init_phys_pg_pack_from_userptr(struct hl_ctx *ctx,
		struct hl_userptr *userptr,
		struct hl_vm_phys_pg_pack **pphys_pg_pack)
{
	struct hl_vm_phys_pg_pack *phys_pg_pack;
	struct scatterlist *sg;
	dma_addr_t dma_addr;
	u64 page_mask, total_npages;
	u32 npages, page_size = PAGE_SIZE;
	bool first = true, is_huge_page_opt = true;
	int rc, i, j;

	phys_pg_pack = kzalloc(sizeof(*phys_pg_pack), GFP_KERNEL);
	if (!phys_pg_pack)
		return -ENOMEM;

	phys_pg_pack->vm_type = userptr->vm_type;
	phys_pg_pack->created_from_userptr = true;
	phys_pg_pack->asid = ctx->asid;
	atomic_set(&phys_pg_pack->mapping_cnt, 1);

	/* Only if all dma_addrs are aligned to 2MB and their
	 * sizes is at least 2MB, we can use huge page mapping.
	 * We limit the 2MB optimization to this condition,
	 * since later on we acquire the related VA range as one
	 * consecutive block.
	 */
	total_npages = 0;
	for_each_sg(userptr->sgt->sgl, sg, userptr->sgt->nents, i) {
		npages = get_sg_info(sg, &dma_addr);

		total_npages += npages;

		if (first) {
			first = false;
			dma_addr &= PAGE_MASK_2MB;
		}

		if ((npages % PGS_IN_2MB_PAGE) ||
					(dma_addr & (PAGE_SIZE_2MB - 1)))
			is_huge_page_opt = false;
	}

	if (is_huge_page_opt) {
		page_size = PAGE_SIZE_2MB;
		total_npages /= PGS_IN_2MB_PAGE;
	}

	page_mask = ~(((u64) page_size) - 1);

	phys_pg_pack->pages = kvmalloc_array(total_npages, sizeof(u64),
						GFP_KERNEL);
	if (!phys_pg_pack->pages) {
		rc = -ENOMEM;
		goto page_pack_arr_mem_err;
	}

	phys_pg_pack->npages = total_npages;
	phys_pg_pack->page_size = page_size;
	phys_pg_pack->total_size = total_npages * page_size;

	j = 0;
	first = true;
	for_each_sg(userptr->sgt->sgl, sg, userptr->sgt->nents, i) {
		npages = get_sg_info(sg, &dma_addr);

		/* align down to physical page size and save the offset */
		if (first) {
			first = false;
			phys_pg_pack->offset = dma_addr & (page_size - 1);
			dma_addr &= page_mask;
		}

		while (npages) {
			phys_pg_pack->pages[j++] = dma_addr;
			dma_addr += page_size;

			if (is_huge_page_opt)
				npages -= PGS_IN_2MB_PAGE;
			else
				npages--;
		}
	}

	*pphys_pg_pack = phys_pg_pack;

	return 0;

page_pack_arr_mem_err:
	kfree(phys_pg_pack);

	return rc;
}

/*
 * map_phys_page_pack - maps the physical page pack
 *
 * @ctx                : current context
 * @vaddr              : start address of the virtual area to map from
 * @phys_pg_pack       : the pack of physical pages to map to
 *
 * This function does the following:
 * - Maps each chunk of virtual memory to matching physical chunk
 * - Stores number of successful mappings in the given argument
 * - Returns 0 on success, error code otherwise.
 */
static int map_phys_page_pack(struct hl_ctx *ctx, u64 vaddr,
		struct hl_vm_phys_pg_pack *phys_pg_pack)
{
	struct hl_device *hdev = ctx->hdev;
	u64 next_vaddr = vaddr, paddr, mapped_pg_cnt = 0, i;
	u32 page_size = phys_pg_pack->page_size;
	int rc = 0;

	for (i = 0 ; i < phys_pg_pack->npages ; i++) {
		paddr = phys_pg_pack->pages[i];

		/* For accessing the host we need to turn on bit 39 */
		if (phys_pg_pack->created_from_userptr)
			paddr += hdev->asic_prop.host_phys_base_address;

		rc = hl_mmu_map(ctx, next_vaddr, paddr, page_size);
		if (rc) {
			dev_err(hdev->dev,
				"map failed for handle %u, npages: %llu, mapped: %llu",
				phys_pg_pack->handle, phys_pg_pack->npages,
				mapped_pg_cnt);
			goto err;
		}

		mapped_pg_cnt++;
		next_vaddr += page_size;
	}

	return 0;

err:
	next_vaddr = vaddr;
	for (i = 0 ; i < mapped_pg_cnt ; i++) {
		if (hl_mmu_unmap(ctx, next_vaddr, page_size))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap handle %u, va: 0x%llx, pa: 0x%llx, page size: %u\n",
					phys_pg_pack->handle, next_vaddr,
					phys_pg_pack->pages[i], page_size);

		next_vaddr += page_size;
	}

	return rc;
}

static int get_paddr_from_handle(struct hl_ctx *ctx, struct hl_mem_in *args,
				u64 *paddr)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_vm *vm = &hdev->vm;
	struct hl_vm_phys_pg_pack *phys_pg_pack;
	u32 handle;

	handle = lower_32_bits(args->map_device.handle);
	spin_lock(&vm->idr_lock);
	phys_pg_pack = idr_find(&vm->phys_pg_pack_handles, handle);
	if (!phys_pg_pack) {
		spin_unlock(&vm->idr_lock);
		dev_err(hdev->dev, "no match for handle %u\n", handle);
		return -EINVAL;
	}

	*paddr = phys_pg_pack->pages[0];

	spin_unlock(&vm->idr_lock);

	return 0;
}

/*
 * map_device_va - map the given memory
 *
 * @ctx	         : current context
 * @args         : host parameters with handle/host virtual address
 * @device_addr	 : pointer to result device virtual address
 *
 * This function does the following:
 * - If given a physical device memory handle, map to a device virtual block
 *   and return the start address of this block
 * - If given a host virtual address and size, find the related physical pages,
 *   map a device virtual block to this pages and return the start address of
 *   this block
 */
static int map_device_va(struct hl_ctx *ctx, struct hl_mem_in *args,
		u64 *device_addr)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_vm *vm = &hdev->vm;
	struct hl_vm_phys_pg_pack *phys_pg_pack;
	struct hl_userptr *userptr = NULL;
	struct hl_vm_hash_node *hnode;
	enum vm_type_t *vm_type;
	u64 ret_vaddr, hint_addr;
	u32 handle = 0;
	int rc;
	bool is_userptr = args->flags & HL_MEM_USERPTR;

	/* Assume failure */
	*device_addr = 0;

	if (is_userptr) {
		rc = get_userptr_from_host_va(hdev, args, &userptr);
		if (rc) {
			dev_err(hdev->dev, "failed to get userptr from va\n");
			return rc;
		}

		rc = init_phys_pg_pack_from_userptr(ctx, userptr,
				&phys_pg_pack);
		if (rc) {
			dev_err(hdev->dev,
				"unable to init page pack for vaddr 0x%llx\n",
				args->map_host.host_virt_addr);
			goto init_page_pack_err;
		}

		vm_type = (enum vm_type_t *) userptr;
		hint_addr = args->map_host.hint_addr;
	} else {
		handle = lower_32_bits(args->map_device.handle);

		spin_lock(&vm->idr_lock);
		phys_pg_pack = idr_find(&vm->phys_pg_pack_handles, handle);
		if (!phys_pg_pack) {
			spin_unlock(&vm->idr_lock);
			dev_err(hdev->dev,
				"no match for handle %u\n", handle);
			return -EINVAL;
		}

		/* increment now to avoid freeing device memory while mapping */
		atomic_inc(&phys_pg_pack->mapping_cnt);

		spin_unlock(&vm->idr_lock);

		vm_type = (enum vm_type_t *) phys_pg_pack;

		hint_addr = args->map_device.hint_addr;
	}

	/*
	 * relevant for mapping device physical memory only, as host memory is
	 * implicitly shared
	 */
	if (!is_userptr && !(phys_pg_pack->flags & HL_MEM_SHARED) &&
			phys_pg_pack->asid != ctx->asid) {
		dev_err(hdev->dev,
			"Failed to map memory, handle %u is not shared\n",
			handle);
		rc = -EPERM;
		goto shared_err;
	}

	hnode = kzalloc(sizeof(*hnode), GFP_KERNEL);
	if (!hnode) {
		rc = -ENOMEM;
		goto hnode_err;
	}

	ret_vaddr = get_va_block(hdev,
			is_userptr ? &ctx->host_va_range : &ctx->dram_va_range,
			phys_pg_pack->total_size, hint_addr, is_userptr);
	if (!ret_vaddr) {
		dev_err(hdev->dev, "no available va block for handle %u\n",
				handle);
		rc = -ENOMEM;
		goto va_block_err;
	}

	mutex_lock(&ctx->mmu_lock);

	rc = map_phys_page_pack(ctx, ret_vaddr, phys_pg_pack);
	if (rc) {
		mutex_unlock(&ctx->mmu_lock);
		dev_err(hdev->dev, "mapping page pack failed for handle %u\n",
				handle);
		goto map_err;
	}

	hdev->asic_funcs->mmu_invalidate_cache(hdev, false);

	mutex_unlock(&ctx->mmu_lock);

	ret_vaddr += phys_pg_pack->offset;

	hnode->ptr = vm_type;
	hnode->vaddr = ret_vaddr;

	mutex_lock(&ctx->mem_hash_lock);
	hash_add(ctx->mem_hash, &hnode->node, ret_vaddr);
	mutex_unlock(&ctx->mem_hash_lock);

	*device_addr = ret_vaddr;

	if (is_userptr)
		free_phys_pg_pack(hdev, phys_pg_pack);

	return 0;

map_err:
	if (add_va_block(hdev,
			is_userptr ? &ctx->host_va_range : &ctx->dram_va_range,
			ret_vaddr,
			ret_vaddr + phys_pg_pack->total_size - 1))
		dev_warn(hdev->dev,
			"release va block failed for handle 0x%x, vaddr: 0x%llx\n",
				handle, ret_vaddr);

va_block_err:
	kfree(hnode);
hnode_err:
shared_err:
	atomic_dec(&phys_pg_pack->mapping_cnt);
	if (is_userptr)
		free_phys_pg_pack(hdev, phys_pg_pack);
init_page_pack_err:
	if (is_userptr)
		free_userptr(hdev, userptr);

	return rc;
}

/*
 * unmap_device_va      - unmap the given device virtual address
 *
 * @ctx                 : current context
 * @vaddr               : device virtual address to unmap
 *
 * This function does the following:
 * - Unmap the physical pages related to the given virtual address
 * - return the device virtual block to the virtual block list
 */
static int unmap_device_va(struct hl_ctx *ctx, u64 vaddr)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_vm_phys_pg_pack *phys_pg_pack = NULL;
	struct hl_vm_hash_node *hnode = NULL;
	struct hl_userptr *userptr = NULL;
	enum vm_type_t *vm_type;
	u64 next_vaddr, i;
	u32 page_size;
	bool is_userptr;
	int rc;

	/* protect from double entrance */
	mutex_lock(&ctx->mem_hash_lock);
	hash_for_each_possible(ctx->mem_hash, hnode, node, (unsigned long)vaddr)
		if (vaddr == hnode->vaddr)
			break;

	if (!hnode) {
		mutex_unlock(&ctx->mem_hash_lock);
		dev_err(hdev->dev,
			"unmap failed, no mem hnode for vaddr 0x%llx\n",
			vaddr);
		return -EINVAL;
	}

	hash_del(&hnode->node);
	mutex_unlock(&ctx->mem_hash_lock);

	vm_type = hnode->ptr;

	if (*vm_type == VM_TYPE_USERPTR) {
		is_userptr = true;
		userptr = hnode->ptr;
		rc = init_phys_pg_pack_from_userptr(ctx, userptr,
				&phys_pg_pack);
		if (rc) {
			dev_err(hdev->dev,
				"unable to init page pack for vaddr 0x%llx\n",
				vaddr);
			goto vm_type_err;
		}
	} else if (*vm_type == VM_TYPE_PHYS_PACK) {
		is_userptr = false;
		phys_pg_pack = hnode->ptr;
	} else {
		dev_warn(hdev->dev,
			"unmap failed, unknown vm desc for vaddr 0x%llx\n",
				vaddr);
		rc = -EFAULT;
		goto vm_type_err;
	}

	if (atomic_read(&phys_pg_pack->mapping_cnt) == 0) {
		dev_err(hdev->dev, "vaddr 0x%llx is not mapped\n", vaddr);
		rc = -EINVAL;
		goto mapping_cnt_err;
	}

	page_size = phys_pg_pack->page_size;
	vaddr &= ~(((u64) page_size) - 1);

	next_vaddr = vaddr;

	mutex_lock(&ctx->mmu_lock);

	for (i = 0 ; i < phys_pg_pack->npages ; i++, next_vaddr += page_size) {
		if (hl_mmu_unmap(ctx, next_vaddr, page_size))
			dev_warn_ratelimited(hdev->dev,
			"unmap failed for vaddr: 0x%llx\n", next_vaddr);

		/* unmapping on Palladium can be really long, so avoid a CPU
		 * soft lockup bug by sleeping a little between unmapping pages
		 */
		if (hdev->pldm)
			usleep_range(500, 1000);
	}

	hdev->asic_funcs->mmu_invalidate_cache(hdev, true);

	mutex_unlock(&ctx->mmu_lock);

	if (add_va_block(hdev,
			is_userptr ? &ctx->host_va_range : &ctx->dram_va_range,
			vaddr,
			vaddr + phys_pg_pack->total_size - 1))
		dev_warn(hdev->dev, "add va block failed for vaddr: 0x%llx\n",
				vaddr);

	atomic_dec(&phys_pg_pack->mapping_cnt);
	kfree(hnode);

	if (is_userptr) {
		free_phys_pg_pack(hdev, phys_pg_pack);
		free_userptr(hdev, userptr);
	}

	return 0;

mapping_cnt_err:
	if (is_userptr)
		free_phys_pg_pack(hdev, phys_pg_pack);
vm_type_err:
	mutex_lock(&ctx->mem_hash_lock);
	hash_add(ctx->mem_hash, &hnode->node, vaddr);
	mutex_unlock(&ctx->mem_hash_lock);

	return rc;
}

int hl_mem_ioctl(struct hl_fpriv *hpriv, void *data)
{
	union hl_mem_args *args = data;
	struct hl_device *hdev = hpriv->hdev;
	struct hl_ctx *ctx = hpriv->ctx;
	u64 device_addr = 0;
	u32 handle = 0;
	int rc;

	if (hl_device_disabled_or_in_reset(hdev)) {
		dev_warn_ratelimited(hdev->dev,
			"Device is disabled or in reset. Can't execute memory IOCTL\n");
		return -EBUSY;
	}

	if (hdev->mmu_enable) {
		switch (args->in.op) {
		case HL_MEM_OP_ALLOC:
			if (!hdev->dram_supports_virtual_memory) {
				dev_err(hdev->dev,
					"DRAM alloc is not supported\n");
				rc = -EINVAL;
				goto out;
			}
			if (args->in.alloc.mem_size == 0) {
				dev_err(hdev->dev,
					"alloc size must be larger than 0\n");
				rc = -EINVAL;
				goto out;
			}
			rc = alloc_device_memory(ctx, &args->in, &handle);

			memset(args, 0, sizeof(*args));
			args->out.handle = (__u64) handle;
			break;

		case HL_MEM_OP_FREE:
			if (!hdev->dram_supports_virtual_memory) {
				dev_err(hdev->dev,
					"DRAM free is not supported\n");
				rc = -EINVAL;
				goto out;
			}
			rc = free_device_memory(ctx, args->in.free.handle);
			break;

		case HL_MEM_OP_MAP:
			rc = map_device_va(ctx, &args->in, &device_addr);

			memset(args, 0, sizeof(*args));
			args->out.device_virt_addr = device_addr;
			break;

		case HL_MEM_OP_UNMAP:
			rc = unmap_device_va(ctx,
					args->in.unmap.device_virt_addr);
			break;

		default:
			dev_err(hdev->dev, "Unknown opcode for memory IOCTL\n");
			rc = -ENOTTY;
			break;
		}
	} else {
		switch (args->in.op) {
		case HL_MEM_OP_ALLOC:
			if (args->in.alloc.mem_size == 0) {
				dev_err(hdev->dev,
					"alloc size must be larger than 0\n");
				rc = -EINVAL;
				goto out;
			}

			/* Force contiguous as there are no real MMU
			 * translations to overcome physical memory gaps
			 */
			args->in.flags |= HL_MEM_CONTIGUOUS;
			rc = alloc_device_memory(ctx, &args->in, &handle);

			memset(args, 0, sizeof(*args));
			args->out.handle = (__u64) handle;
			break;

		case HL_MEM_OP_FREE:
			rc = free_device_memory(ctx, args->in.free.handle);
			break;

		case HL_MEM_OP_MAP:
			if (args->in.flags & HL_MEM_USERPTR) {
				device_addr = args->in.map_host.host_virt_addr;
				rc = 0;
			} else {
				rc = get_paddr_from_handle(ctx, &args->in,
						&device_addr);
			}

			memset(args, 0, sizeof(*args));
			args->out.device_virt_addr = device_addr;
			break;

		case HL_MEM_OP_UNMAP:
			rc = 0;
			break;

		default:
			dev_err(hdev->dev, "Unknown opcode for memory IOCTL\n");
			rc = -ENOTTY;
			break;
		}
	}

out:
	return rc;
}

/*
 * hl_pin_host_memory - pins a chunk of host memory
 *
 * @hdev                : pointer to the habanalabs device structure
 * @addr                : the user-space virtual address of the memory area
 * @size                : the size of the memory area
 * @userptr	        : pointer to hl_userptr structure
 *
 * This function does the following:
 * - Pins the physical pages
 * - Create a SG list from those pages
 */
int hl_pin_host_memory(struct hl_device *hdev, u64 addr, u64 size,
			struct hl_userptr *userptr)
{
	u64 start, end;
	u32 npages, offset;
	int rc;

	if (!size) {
		dev_err(hdev->dev, "size to pin is invalid - %llu\n", size);
		return -EINVAL;
	}

	if (!access_ok((void __user *) (uintptr_t) addr, size)) {
		dev_err(hdev->dev, "user pointer is invalid - 0x%llx\n", addr);
		return -EFAULT;
	}

	/*
	 * If the combination of the address and size requested for this memory
	 * region causes an integer overflow, return error.
	 */
	if (((addr + size) < addr) ||
			PAGE_ALIGN(addr + size) < (addr + size)) {
		dev_err(hdev->dev,
			"user pointer 0x%llx + %llu causes integer overflow\n",
			addr, size);
		return -EINVAL;
	}

	start = addr & PAGE_MASK;
	offset = addr & ~PAGE_MASK;
	end = PAGE_ALIGN(addr + size);
	npages = (end - start) >> PAGE_SHIFT;

	userptr->size = size;
	userptr->addr = addr;
	userptr->dma_mapped = false;
	INIT_LIST_HEAD(&userptr->job_node);

	userptr->vec = frame_vector_create(npages);
	if (!userptr->vec) {
		dev_err(hdev->dev, "Failed to create frame vector\n");
		return -ENOMEM;
	}

	rc = get_vaddr_frames(start, npages, FOLL_FORCE | FOLL_WRITE,
				userptr->vec);

	if (rc != npages) {
		dev_err(hdev->dev,
			"Failed to map host memory, user ptr probably wrong\n");
		if (rc < 0)
			goto destroy_framevec;
		rc = -EFAULT;
		goto put_framevec;
	}

	if (frame_vector_to_pages(userptr->vec) < 0) {
		dev_err(hdev->dev,
			"Failed to translate frame vector to pages\n");
		rc = -EFAULT;
		goto put_framevec;
	}

	userptr->sgt = kzalloc(sizeof(*userptr->sgt), GFP_ATOMIC);
	if (!userptr->sgt) {
		rc = -ENOMEM;
		goto put_framevec;
	}

	rc = sg_alloc_table_from_pages(userptr->sgt,
					frame_vector_pages(userptr->vec),
					npages, offset, size, GFP_ATOMIC);
	if (rc < 0) {
		dev_err(hdev->dev, "failed to create SG table from pages\n");
		goto free_sgt;
	}

	hl_debugfs_add_userptr(hdev, userptr);

	return 0;

free_sgt:
	kfree(userptr->sgt);
put_framevec:
	put_vaddr_frames(userptr->vec);
destroy_framevec:
	frame_vector_destroy(userptr->vec);
	return rc;
}

/*
 * hl_unpin_host_memory - unpins a chunk of host memory
 *
 * @hdev                : pointer to the habanalabs device structure
 * @userptr             : pointer to hl_userptr structure
 *
 * This function does the following:
 * - Unpins the physical pages related to the host memory
 * - Free the SG list
 */
int hl_unpin_host_memory(struct hl_device *hdev, struct hl_userptr *userptr)
{
	struct page **pages;

	hl_debugfs_remove_userptr(hdev, userptr);

	if (userptr->dma_mapped)
		hdev->asic_funcs->hl_dma_unmap_sg(hdev,
				userptr->sgt->sgl,
				userptr->sgt->nents,
				userptr->dir);

	pages = frame_vector_pages(userptr->vec);
	if (!IS_ERR(pages)) {
		int i;

		for (i = 0; i < frame_vector_count(userptr->vec); i++)
			set_page_dirty_lock(pages[i]);
	}
	put_vaddr_frames(userptr->vec);
	frame_vector_destroy(userptr->vec);

	list_del(&userptr->job_node);

	sg_free_table(userptr->sgt);
	kfree(userptr->sgt);

	return 0;
}

/*
 * hl_userptr_delete_list - clear userptr list
 *
 * @hdev                : pointer to the habanalabs device structure
 * @userptr_list        : pointer to the list to clear
 *
 * This function does the following:
 * - Iterates over the list and unpins the host memory and frees the userptr
 *   structure.
 */
void hl_userptr_delete_list(struct hl_device *hdev,
				struct list_head *userptr_list)
{
	struct hl_userptr *userptr, *tmp;

	list_for_each_entry_safe(userptr, tmp, userptr_list, job_node) {
		hl_unpin_host_memory(hdev, userptr);
		kfree(userptr);
	}

	INIT_LIST_HEAD(userptr_list);
}

/*
 * hl_userptr_is_pinned - returns whether the given userptr is pinned
 *
 * @hdev                : pointer to the habanalabs device structure
 * @userptr_list        : pointer to the list to clear
 * @userptr             : pointer to userptr to check
 *
 * This function does the following:
 * - Iterates over the list and checks if the given userptr is in it, means is
 *   pinned. If so, returns true, otherwise returns false.
 */
bool hl_userptr_is_pinned(struct hl_device *hdev, u64 addr,
				u32 size, struct list_head *userptr_list,
				struct hl_userptr **userptr)
{
	list_for_each_entry((*userptr), userptr_list, job_node) {
		if ((addr == (*userptr)->addr) && (size == (*userptr)->size))
			return true;
	}

	return false;
}

/*
 * hl_va_range_init - initialize virtual addresses range
 *
 * @hdev                : pointer to the habanalabs device structure
 * @va_range            : pointer to the range to initialize
 * @start               : range start address
 * @end                 : range end address
 *
 * This function does the following:
 * - Initializes the virtual addresses list of the given range with the given
 *   addresses.
 */
static int hl_va_range_init(struct hl_device *hdev,
		struct hl_va_range *va_range, u64 start, u64 end)
{
	int rc;

	INIT_LIST_HEAD(&va_range->list);

	/* PAGE_SIZE alignment */

	if (start & (PAGE_SIZE - 1)) {
		start &= PAGE_MASK;
		start += PAGE_SIZE;
	}

	if (end & (PAGE_SIZE - 1))
		end &= PAGE_MASK;

	if (start >= end) {
		dev_err(hdev->dev, "too small vm range for va list\n");
		return -EFAULT;
	}

	rc = add_va_block(hdev, va_range, start, end);

	if (rc) {
		dev_err(hdev->dev, "Failed to init host va list\n");
		return rc;
	}

	va_range->start_addr = start;
	va_range->end_addr = end;

	return 0;
}

/*
 * hl_vm_ctx_init_with_ranges - initialize virtual memory for context
 *
 * @ctx                 : pointer to the habanalabs context structure
 * @host_range_start    : host virtual addresses range start
 * @host_range_end      : host virtual addresses range end
 * @dram_range_start    : dram virtual addresses range start
 * @dram_range_end      : dram virtual addresses range end
 *
 * This function initializes the following:
 * - MMU for context
 * - Virtual address to area descriptor hashtable
 * - Virtual block list of available virtual memory
 */
static int hl_vm_ctx_init_with_ranges(struct hl_ctx *ctx, u64 host_range_start,
				u64 host_range_end, u64 dram_range_start,
				u64 dram_range_end)
{
	struct hl_device *hdev = ctx->hdev;
	int rc;

	rc = hl_mmu_ctx_init(ctx);
	if (rc) {
		dev_err(hdev->dev, "failed to init context %d\n", ctx->asid);
		return rc;
	}

	mutex_init(&ctx->mem_hash_lock);
	hash_init(ctx->mem_hash);

	mutex_init(&ctx->host_va_range.lock);

	rc = hl_va_range_init(hdev, &ctx->host_va_range, host_range_start,
			host_range_end);
	if (rc) {
		dev_err(hdev->dev, "failed to init host vm range\n");
		goto host_vm_err;
	}

	mutex_init(&ctx->dram_va_range.lock);

	rc = hl_va_range_init(hdev, &ctx->dram_va_range, dram_range_start,
			dram_range_end);
	if (rc) {
		dev_err(hdev->dev, "failed to init dram vm range\n");
		goto dram_vm_err;
	}

	hl_debugfs_add_ctx_mem_hash(hdev, ctx);

	return 0;

dram_vm_err:
	mutex_destroy(&ctx->dram_va_range.lock);

	mutex_lock(&ctx->host_va_range.lock);
	clear_va_list_locked(hdev, &ctx->host_va_range.list);
	mutex_unlock(&ctx->host_va_range.lock);
host_vm_err:
	mutex_destroy(&ctx->host_va_range.lock);
	mutex_destroy(&ctx->mem_hash_lock);
	hl_mmu_ctx_fini(ctx);

	return rc;
}

int hl_vm_ctx_init(struct hl_ctx *ctx)
{
	struct asic_fixed_properties *prop = &ctx->hdev->asic_prop;
	u64 host_range_start, host_range_end, dram_range_start,
		dram_range_end;

	atomic64_set(&ctx->dram_phys_mem, 0);

	/*
	 * - If MMU is enabled, init the ranges as usual.
	 * - If MMU is disabled, in case of host mapping, the returned address
	 *   is the given one.
	 *   In case of DRAM mapping, the returned address is the physical
	 *   address of the memory related to the given handle.
	 */
	if (ctx->hdev->mmu_enable) {
		dram_range_start = prop->va_space_dram_start_address;
		dram_range_end = prop->va_space_dram_end_address;
		host_range_start = prop->va_space_host_start_address;
		host_range_end = prop->va_space_host_end_address;
	} else {
		dram_range_start = prop->dram_user_base_address;
		dram_range_end = prop->dram_end_address;
		host_range_start = prop->dram_user_base_address;
		host_range_end = prop->dram_end_address;
	}

	return hl_vm_ctx_init_with_ranges(ctx, host_range_start, host_range_end,
			dram_range_start, dram_range_end);
}

/*
 * hl_va_range_fini     - clear a virtual addresses range
 *
 * @hdev                : pointer to the habanalabs structure
 * va_range             : pointer to virtual addresses range
 *
 * This function initializes the following:
 * - Checks that the given range contains the whole initial range
 * - Frees the virtual addresses block list and its lock
 */
static void hl_va_range_fini(struct hl_device *hdev,
		struct hl_va_range *va_range)
{
	struct hl_vm_va_block *va_block;

	if (list_empty(&va_range->list)) {
		dev_warn(hdev->dev,
				"va list should not be empty on cleanup!\n");
		goto out;
	}

	if (!list_is_singular(&va_range->list)) {
		dev_warn(hdev->dev,
			"va list should not contain multiple blocks on cleanup!\n");
		goto free_va_list;
	}

	va_block = list_first_entry(&va_range->list, typeof(*va_block), node);

	if (va_block->start != va_range->start_addr ||
		va_block->end != va_range->end_addr) {
		dev_warn(hdev->dev,
			"wrong va block on cleanup, from 0x%llx to 0x%llx\n",
				va_block->start, va_block->end);
		goto free_va_list;
	}

free_va_list:
	mutex_lock(&va_range->lock);
	clear_va_list_locked(hdev, &va_range->list);
	mutex_unlock(&va_range->lock);

out:
	mutex_destroy(&va_range->lock);
}

/*
 * hl_vm_ctx_fini       - virtual memory teardown of context
 *
 * @ctx                 : pointer to the habanalabs context structure
 *
 * This function perform teardown the following:
 * - Virtual block list of available virtual memory
 * - Virtual address to area descriptor hashtable
 * - MMU for context
 *
 * In addition this function does the following:
 * - Unmaps the existing hashtable nodes if the hashtable is not empty. The
 *   hashtable should be empty as no valid mappings should exist at this
 *   point.
 * - Frees any existing physical page list from the idr which relates to the
 *   current context asid.
 * - This function checks the virtual block list for correctness. At this point
 *   the list should contain one element which describes the whole virtual
 *   memory range of the context. Otherwise, a warning is printed.
 */
void hl_vm_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_vm *vm = &hdev->vm;
	struct hl_vm_phys_pg_pack *phys_pg_list;
	struct hl_vm_hash_node *hnode;
	struct hlist_node *tmp_node;
	int i;

	hl_debugfs_remove_ctx_mem_hash(hdev, ctx);

	if (!hash_empty(ctx->mem_hash))
		dev_notice(hdev->dev, "ctx is freed while it has va in use\n");

	hash_for_each_safe(ctx->mem_hash, i, tmp_node, hnode, node) {
		dev_dbg(hdev->dev,
			"hl_mem_hash_node of vaddr 0x%llx of asid %d is still alive\n",
			hnode->vaddr, ctx->asid);
		unmap_device_va(ctx, hnode->vaddr);
	}

	spin_lock(&vm->idr_lock);
	idr_for_each_entry(&vm->phys_pg_pack_handles, phys_pg_list, i)
		if (phys_pg_list->asid == ctx->asid) {
			dev_dbg(hdev->dev,
				"page list 0x%p of asid %d is still alive\n",
				phys_pg_list, ctx->asid);
			free_phys_pg_pack(hdev, phys_pg_list);
			idr_remove(&vm->phys_pg_pack_handles, i);
		}
	spin_unlock(&vm->idr_lock);

	hl_va_range_fini(hdev, &ctx->dram_va_range);
	hl_va_range_fini(hdev, &ctx->host_va_range);

	mutex_destroy(&ctx->mem_hash_lock);
	hl_mmu_ctx_fini(ctx);
}

/*
 * hl_vm_init           - initialize virtual memory module
 *
 * @hdev                : pointer to the habanalabs device structure
 *
 * This function initializes the following:
 * - MMU module
 * - DRAM physical pages pool of 2MB
 * - Idr for device memory allocation handles
 */
int hl_vm_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_vm *vm = &hdev->vm;
	int rc;

	rc = hl_mmu_init(hdev);
	if (rc) {
		dev_err(hdev->dev, "Failed to init MMU\n");
		return rc;
	}

	vm->dram_pg_pool = gen_pool_create(__ffs(prop->dram_page_size), -1);
	if (!vm->dram_pg_pool) {
		dev_err(hdev->dev, "Failed to create dram page pool\n");
		rc = -ENOMEM;
		goto pool_create_err;
	}

	kref_init(&vm->dram_pg_pool_refcount);

	rc = gen_pool_add(vm->dram_pg_pool, prop->dram_user_base_address,
			prop->dram_end_address - prop->dram_user_base_address,
			-1);

	if (rc) {
		dev_err(hdev->dev,
			"Failed to add memory to dram page pool %d\n", rc);
		goto pool_add_err;
	}

	spin_lock_init(&vm->idr_lock);
	idr_init(&vm->phys_pg_pack_handles);

	atomic64_set(&hdev->dram_used_mem, 0);

	vm->init_done = true;

	return 0;

pool_add_err:
	gen_pool_destroy(vm->dram_pg_pool);
pool_create_err:
	hl_mmu_fini(hdev);

	return rc;
}

/*
 * hl_vm_fini           - virtual memory module teardown
 *
 * @hdev                : pointer to the habanalabs device structure
 *
 * This function perform teardown to the following:
 * - Idr for device memory allocation handles
 * - DRAM physical pages pool of 2MB
 * - MMU module
 */
void hl_vm_fini(struct hl_device *hdev)
{
	struct hl_vm *vm = &hdev->vm;

	if (!vm->init_done)
		return;

	/*
	 * At this point all the contexts should be freed and hence no DRAM
	 * memory should be in use. Hence the DRAM pool should be freed here.
	 */
	if (kref_put(&vm->dram_pg_pool_refcount, dram_pg_pool_do_release) != 1)
		dev_warn(hdev->dev, "dram_pg_pool was not destroyed on %s\n",
				__func__);

	hl_mmu_fini(hdev);

	vm->init_done = false;
}

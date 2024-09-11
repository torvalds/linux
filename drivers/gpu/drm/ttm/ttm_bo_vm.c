/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

#define pr_fmt(fmt) "[TTM] " fmt

#include <drm/ttm/ttm_bo.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>

static vm_fault_t ttm_bo_vm_fault_idle(struct ttm_buffer_object *bo,
				struct vm_fault *vmf)
{
	long err = 0;

	/*
	 * Quick non-stalling check for idle.
	 */
	if (dma_resv_test_signaled(bo->base.resv, DMA_RESV_USAGE_KERNEL))
		return 0;

	/*
	 * If possible, avoid waiting for GPU with mmap_lock
	 * held.  We only do this if the fault allows retry and this
	 * is the first attempt.
	 */
	if (fault_flag_allow_retry_first(vmf->flags)) {
		if (vmf->flags & FAULT_FLAG_RETRY_NOWAIT)
			return VM_FAULT_RETRY;

		ttm_bo_get(bo);
		mmap_read_unlock(vmf->vma->vm_mm);
		(void)dma_resv_wait_timeout(bo->base.resv,
					    DMA_RESV_USAGE_KERNEL, true,
					    MAX_SCHEDULE_TIMEOUT);
		dma_resv_unlock(bo->base.resv);
		ttm_bo_put(bo);
		return VM_FAULT_RETRY;
	}

	/*
	 * Ordinary wait.
	 */
	err = dma_resv_wait_timeout(bo->base.resv, DMA_RESV_USAGE_KERNEL, true,
				    MAX_SCHEDULE_TIMEOUT);
	if (unlikely(err < 0)) {
		return (err != -ERESTARTSYS) ? VM_FAULT_SIGBUS :
			VM_FAULT_NOPAGE;
	}

	return 0;
}

static unsigned long ttm_bo_io_mem_pfn(struct ttm_buffer_object *bo,
				       unsigned long page_offset)
{
	struct ttm_device *bdev = bo->bdev;

	if (bdev->funcs->io_mem_pfn)
		return bdev->funcs->io_mem_pfn(bo, page_offset);

	return (bo->resource->bus.offset >> PAGE_SHIFT) + page_offset;
}

/**
 * ttm_bo_vm_reserve - Reserve a buffer object in a retryable vm callback
 * @bo: The buffer object
 * @vmf: The fault structure handed to the callback
 *
 * vm callbacks like fault() and *_mkwrite() allow for the mmap_lock to be dropped
 * during long waits, and after the wait the callback will be restarted. This
 * is to allow other threads using the same virtual memory space concurrent
 * access to map(), unmap() completely unrelated buffer objects. TTM buffer
 * object reservations sometimes wait for GPU and should therefore be
 * considered long waits. This function reserves the buffer object interruptibly
 * taking this into account. Starvation is avoided by the vm system not
 * allowing too many repeated restarts.
 * This function is intended to be used in customized fault() and _mkwrite()
 * handlers.
 *
 * Return:
 *    0 on success and the bo was reserved.
 *    VM_FAULT_RETRY if blocking wait.
 *    VM_FAULT_NOPAGE if blocking wait and retrying was not allowed.
 */
vm_fault_t ttm_bo_vm_reserve(struct ttm_buffer_object *bo,
			     struct vm_fault *vmf)
{
	/*
	 * Work around locking order reversal in fault / nopfn
	 * between mmap_lock and bo_reserve: Perform a trylock operation
	 * for reserve, and if it fails, retry the fault after waiting
	 * for the buffer to become unreserved.
	 */
	if (unlikely(!dma_resv_trylock(bo->base.resv))) {
		/*
		 * If the fault allows retry and this is the first
		 * fault attempt, we try to release the mmap_lock
		 * before waiting
		 */
		if (fault_flag_allow_retry_first(vmf->flags)) {
			if (!(vmf->flags & FAULT_FLAG_RETRY_NOWAIT)) {
				ttm_bo_get(bo);
				mmap_read_unlock(vmf->vma->vm_mm);
				if (!dma_resv_lock_interruptible(bo->base.resv,
								 NULL))
					dma_resv_unlock(bo->base.resv);
				ttm_bo_put(bo);
			}

			return VM_FAULT_RETRY;
		}

		if (dma_resv_lock_interruptible(bo->base.resv, NULL))
			return VM_FAULT_NOPAGE;
	}

	/*
	 * Refuse to fault imported pages. This should be handled
	 * (if at all) by redirecting mmap to the exporter.
	 */
	if (bo->ttm && (bo->ttm->page_flags & TTM_TT_FLAG_EXTERNAL)) {
		if (!(bo->ttm->page_flags & TTM_TT_FLAG_EXTERNAL_MAPPABLE)) {
			dma_resv_unlock(bo->base.resv);
			return VM_FAULT_SIGBUS;
		}
	}

	return 0;
}
EXPORT_SYMBOL(ttm_bo_vm_reserve);

/**
 * ttm_bo_vm_fault_reserved - TTM fault helper
 * @vmf: The struct vm_fault given as argument to the fault callback
 * @prot: The page protection to be used for this memory area.
 * @num_prefault: Maximum number of prefault pages. The caller may want to
 * specify this based on madvice settings and the size of the GPU object
 * backed by the memory.
 *
 * This function inserts one or more page table entries pointing to the
 * memory backing the buffer object, and then returns a return code
 * instructing the caller to retry the page access.
 *
 * Return:
 *   VM_FAULT_NOPAGE on success or pending signal
 *   VM_FAULT_SIGBUS on unspecified error
 *   VM_FAULT_OOM on out-of-memory
 *   VM_FAULT_RETRY if retryable wait
 */
vm_fault_t ttm_bo_vm_fault_reserved(struct vm_fault *vmf,
				    pgprot_t prot,
				    pgoff_t num_prefault)
{
	struct vm_area_struct *vma = vmf->vma;
	struct ttm_buffer_object *bo = vma->vm_private_data;
	struct ttm_device *bdev = bo->bdev;
	unsigned long page_offset;
	unsigned long page_last;
	unsigned long pfn;
	struct ttm_tt *ttm = NULL;
	struct page *page;
	int err;
	pgoff_t i;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	unsigned long address = vmf->address;

	/*
	 * Wait for buffer data in transit, due to a pipelined
	 * move.
	 */
	ret = ttm_bo_vm_fault_idle(bo, vmf);
	if (unlikely(ret != 0))
		return ret;

	err = ttm_mem_io_reserve(bdev, bo->resource);
	if (unlikely(err != 0))
		return VM_FAULT_SIGBUS;

	page_offset = ((address - vma->vm_start) >> PAGE_SHIFT) +
		vma->vm_pgoff - drm_vma_node_start(&bo->base.vma_node);
	page_last = vma_pages(vma) + vma->vm_pgoff -
		drm_vma_node_start(&bo->base.vma_node);

	if (unlikely(page_offset >= PFN_UP(bo->base.size)))
		return VM_FAULT_SIGBUS;

	prot = ttm_io_prot(bo, bo->resource, prot);
	if (!bo->resource->bus.is_iomem) {
		struct ttm_operation_ctx ctx = {
			.interruptible = true,
			.no_wait_gpu = false,
			.force_alloc = true
		};

		ttm = bo->ttm;
		err = ttm_bo_populate(bo, &ctx);
		if (err) {
			if (err == -EINTR || err == -ERESTARTSYS ||
			    err == -EAGAIN)
				return VM_FAULT_NOPAGE;

			pr_debug("TTM fault hit %pe.\n", ERR_PTR(err));
			return VM_FAULT_SIGBUS;
		}
	} else {
		/* Iomem should not be marked encrypted */
		prot = pgprot_decrypted(prot);
	}

	/*
	 * Speculatively prefault a number of pages. Only error on
	 * first page.
	 */
	for (i = 0; i < num_prefault; ++i) {
		if (bo->resource->bus.is_iomem) {
			pfn = ttm_bo_io_mem_pfn(bo, page_offset);
		} else {
			page = ttm->pages[page_offset];
			if (unlikely(!page && i == 0)) {
				return VM_FAULT_OOM;
			} else if (unlikely(!page)) {
				break;
			}
			pfn = page_to_pfn(page);
		}

		/*
		 * Note that the value of @prot at this point may differ from
		 * the value of @vma->vm_page_prot in the caching- and
		 * encryption bits. This is because the exact location of the
		 * data may not be known at mmap() time and may also change
		 * at arbitrary times while the data is mmap'ed.
		 * See vmf_insert_pfn_prot() for a discussion.
		 */
		ret = vmf_insert_pfn_prot(vma, address, pfn, prot);

		/* Never error on prefaulted PTEs */
		if (unlikely((ret & VM_FAULT_ERROR))) {
			if (i == 0)
				return VM_FAULT_NOPAGE;
			else
				break;
		}

		address += PAGE_SIZE;
		if (unlikely(++page_offset >= page_last))
			break;
	}
	return ret;
}
EXPORT_SYMBOL(ttm_bo_vm_fault_reserved);

static void ttm_bo_release_dummy_page(struct drm_device *dev, void *res)
{
	struct page *dummy_page = (struct page *)res;

	__free_page(dummy_page);
}

vm_fault_t ttm_bo_vm_dummy_page(struct vm_fault *vmf, pgprot_t prot)
{
	struct vm_area_struct *vma = vmf->vma;
	struct ttm_buffer_object *bo = vma->vm_private_data;
	struct drm_device *ddev = bo->base.dev;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	unsigned long address;
	unsigned long pfn;
	struct page *page;

	/* Allocate new dummy page to map all the VA range in this VMA to it*/
	page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	if (!page)
		return VM_FAULT_OOM;

	/* Set the page to be freed using drmm release action */
	if (drmm_add_action_or_reset(ddev, ttm_bo_release_dummy_page, page))
		return VM_FAULT_OOM;

	pfn = page_to_pfn(page);

	/* Prefault the entire VMA range right away to avoid further faults */
	for (address = vma->vm_start; address < vma->vm_end;
	     address += PAGE_SIZE)
		ret = vmf_insert_pfn_prot(vma, address, pfn, prot);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_vm_dummy_page);

vm_fault_t ttm_bo_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	pgprot_t prot;
	struct ttm_buffer_object *bo = vma->vm_private_data;
	struct drm_device *ddev = bo->base.dev;
	vm_fault_t ret;
	int idx;

	ret = ttm_bo_vm_reserve(bo, vmf);
	if (ret)
		return ret;

	prot = vma->vm_page_prot;
	if (drm_dev_enter(ddev, &idx)) {
		ret = ttm_bo_vm_fault_reserved(vmf, prot, TTM_BO_VM_NUM_PREFAULT);
		drm_dev_exit(idx);
	} else {
		ret = ttm_bo_vm_dummy_page(vmf, prot);
	}
	if (ret == VM_FAULT_RETRY && !(vmf->flags & FAULT_FLAG_RETRY_NOWAIT))
		return ret;

	dma_resv_unlock(bo->base.resv);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_vm_fault);

void ttm_bo_vm_open(struct vm_area_struct *vma)
{
	struct ttm_buffer_object *bo = vma->vm_private_data;

	WARN_ON(bo->bdev->dev_mapping != vma->vm_file->f_mapping);

	ttm_bo_get(bo);
}
EXPORT_SYMBOL(ttm_bo_vm_open);

void ttm_bo_vm_close(struct vm_area_struct *vma)
{
	struct ttm_buffer_object *bo = vma->vm_private_data;

	ttm_bo_put(bo);
	vma->vm_private_data = NULL;
}
EXPORT_SYMBOL(ttm_bo_vm_close);

static int ttm_bo_vm_access_kmap(struct ttm_buffer_object *bo,
				 unsigned long offset,
				 uint8_t *buf, int len, int write)
{
	unsigned long page = offset >> PAGE_SHIFT;
	unsigned long bytes_left = len;
	int ret;

	/* Copy a page at a time, that way no extra virtual address
	 * mapping is needed
	 */
	offset -= page << PAGE_SHIFT;
	do {
		unsigned long bytes = min(bytes_left, PAGE_SIZE - offset);
		struct ttm_bo_kmap_obj map;
		void *ptr;
		bool is_iomem;

		ret = ttm_bo_kmap(bo, page, 1, &map);
		if (ret)
			return ret;

		ptr = (uint8_t *)ttm_kmap_obj_virtual(&map, &is_iomem) + offset;
		WARN_ON_ONCE(is_iomem);
		if (write)
			memcpy(ptr, buf, bytes);
		else
			memcpy(buf, ptr, bytes);
		ttm_bo_kunmap(&map);

		page++;
		buf += bytes;
		bytes_left -= bytes;
		offset = 0;
	} while (bytes_left);

	return len;
}

int ttm_bo_vm_access(struct vm_area_struct *vma, unsigned long addr,
		     void *buf, int len, int write)
{
	struct ttm_buffer_object *bo = vma->vm_private_data;
	unsigned long offset = (addr) - vma->vm_start +
		((vma->vm_pgoff - drm_vma_node_start(&bo->base.vma_node))
		 << PAGE_SHIFT);
	int ret;

	if (len < 1 || (offset + len) > bo->base.size)
		return -EIO;

	ret = ttm_bo_reserve(bo, true, false, NULL);
	if (ret)
		return ret;

	switch (bo->resource->mem_type) {
	case TTM_PL_SYSTEM:
		fallthrough;
	case TTM_PL_TT:
		ret = ttm_bo_vm_access_kmap(bo, offset, buf, len, write);
		break;
	default:
		if (bo->bdev->funcs->access_memory)
			ret = bo->bdev->funcs->access_memory(
				bo, offset, buf, len, write);
		else
			ret = -EIO;
	}

	ttm_bo_unreserve(bo);

	return ret;
}
EXPORT_SYMBOL(ttm_bo_vm_access);

static const struct vm_operations_struct ttm_bo_vm_ops = {
	.fault = ttm_bo_vm_fault,
	.open = ttm_bo_vm_open,
	.close = ttm_bo_vm_close,
	.access = ttm_bo_vm_access,
};

/**
 * ttm_bo_mmap_obj - mmap memory backed by a ttm buffer object.
 *
 * @vma:       vma as input from the fbdev mmap method.
 * @bo:        The bo backing the address space.
 *
 * Maps a buffer object.
 */
int ttm_bo_mmap_obj(struct vm_area_struct *vma, struct ttm_buffer_object *bo)
{
	/* Enforce no COW since would have really strange behavior with it. */
	if (is_cow_mapping(vma->vm_flags))
		return -EINVAL;

	ttm_bo_get(bo);

	/*
	 * Drivers may want to override the vm_ops field. Otherwise we
	 * use TTM's default callbacks.
	 */
	if (!vma->vm_ops)
		vma->vm_ops = &ttm_bo_vm_ops;

	/*
	 * Note: We're transferring the bo reference to
	 * vma->vm_private_data here.
	 */

	vma->vm_private_data = bo;

	vm_flags_set(vma, VM_PFNMAP | VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	return 0;
}
EXPORT_SYMBOL(ttm_bo_mmap_obj);

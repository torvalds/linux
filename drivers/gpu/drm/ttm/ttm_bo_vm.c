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

#include <drm/ttm/ttm_module.h>
#include <drm/ttm/ttm_bo_driver.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/drm_vma_manager.h>
#include <linux/mm.h>
#include <linux/pfn_t.h>
#include <linux/rbtree.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/mem_encrypt.h>

static vm_fault_t ttm_bo_vm_fault_idle(struct ttm_buffer_object *bo,
				struct vm_fault *vmf)
{
	vm_fault_t ret = 0;
	int err = 0;

	if (likely(!bo->moving))
		goto out_unlock;

	/*
	 * Quick non-stalling check for idle.
	 */
	if (dma_fence_is_signaled(bo->moving))
		goto out_clear;

	/*
	 * If possible, avoid waiting for GPU with mmap_lock
	 * held.  We only do this if the fault allows retry and this
	 * is the first attempt.
	 */
	if (fault_flag_allow_retry_first(vmf->flags)) {
		ret = VM_FAULT_RETRY;
		if (vmf->flags & FAULT_FLAG_RETRY_NOWAIT)
			goto out_unlock;

		ttm_bo_get(bo);
		mmap_read_unlock(vmf->vma->vm_mm);
		(void) dma_fence_wait(bo->moving, true);
		dma_resv_unlock(bo->base.resv);
		ttm_bo_put(bo);
		goto out_unlock;
	}

	/*
	 * Ordinary wait.
	 */
	err = dma_fence_wait(bo->moving, true);
	if (unlikely(err != 0)) {
		ret = (err != -ERESTARTSYS) ? VM_FAULT_SIGBUS :
			VM_FAULT_NOPAGE;
		goto out_unlock;
	}

out_clear:
	dma_fence_put(bo->moving);
	bo->moving = NULL;

out_unlock:
	return ret;
}

static unsigned long ttm_bo_io_mem_pfn(struct ttm_buffer_object *bo,
				       unsigned long page_offset)
{
	struct ttm_bo_device *bdev = bo->bdev;

	if (bdev->driver->io_mem_pfn)
		return bdev->driver->io_mem_pfn(bo, page_offset);

	return (bo->mem.bus.offset >> PAGE_SHIFT) + page_offset;
}

/**
 * ttm_bo_vm_reserve - Reserve a buffer object in a retryable vm callback
 * @bo: The buffer object
 * @vmf: The fault structure handed to the callback
 *
 * vm callbacks like fault() and *_mkwrite() allow for the mm_sem to be dropped
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
	if (bo->ttm && (bo->ttm->page_flags & TTM_PAGE_FLAG_SG)) {
		dma_resv_unlock(bo->base.resv);
		return VM_FAULT_SIGBUS;
	}

	return 0;
}
EXPORT_SYMBOL(ttm_bo_vm_reserve);

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/**
 * ttm_bo_vm_insert_huge - Insert a pfn for PUD or PMD faults
 * @vmf: Fault data
 * @bo: The buffer object
 * @page_offset: Page offset from bo start
 * @fault_page_size: The size of the fault in pages.
 * @pgprot: The page protections.
 * Does additional checking whether it's possible to insert a PUD or PMD
 * pfn and performs the insertion.
 *
 * Return: VM_FAULT_NOPAGE on successful insertion, VM_FAULT_FALLBACK if
 * a huge fault was not possible, or on insertion error.
 */
static vm_fault_t ttm_bo_vm_insert_huge(struct vm_fault *vmf,
					struct ttm_buffer_object *bo,
					pgoff_t page_offset,
					pgoff_t fault_page_size,
					pgprot_t pgprot)
{
	pgoff_t i;
	vm_fault_t ret;
	unsigned long pfn;
	pfn_t pfnt;
	struct ttm_tt *ttm = bo->ttm;
	bool write = vmf->flags & FAULT_FLAG_WRITE;

	/* Fault should not cross bo boundary. */
	page_offset &= ~(fault_page_size - 1);
	if (page_offset + fault_page_size > bo->num_pages)
		goto out_fallback;

	if (bo->mem.bus.is_iomem)
		pfn = ttm_bo_io_mem_pfn(bo, page_offset);
	else
		pfn = page_to_pfn(ttm->pages[page_offset]);

	/* pfn must be fault_page_size aligned. */
	if ((pfn & (fault_page_size - 1)) != 0)
		goto out_fallback;

	/* Check that memory is contiguous. */
	if (!bo->mem.bus.is_iomem) {
		for (i = 1; i < fault_page_size; ++i) {
			if (page_to_pfn(ttm->pages[page_offset + i]) != pfn + i)
				goto out_fallback;
		}
	} else if (bo->bdev->driver->io_mem_pfn) {
		for (i = 1; i < fault_page_size; ++i) {
			if (ttm_bo_io_mem_pfn(bo, page_offset + i) != pfn + i)
				goto out_fallback;
		}
	}

	pfnt = __pfn_to_pfn_t(pfn, PFN_DEV);
	if (fault_page_size == (HPAGE_PMD_SIZE >> PAGE_SHIFT))
		ret = vmf_insert_pfn_pmd_prot(vmf, pfnt, pgprot, write);
#ifdef CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD
	else if (fault_page_size == (HPAGE_PUD_SIZE >> PAGE_SHIFT))
		ret = vmf_insert_pfn_pud_prot(vmf, pfnt, pgprot, write);
#endif
	else
		WARN_ON_ONCE(ret = VM_FAULT_FALLBACK);

	if (ret != VM_FAULT_NOPAGE)
		goto out_fallback;

	return VM_FAULT_NOPAGE;
out_fallback:
	count_vm_event(THP_FAULT_FALLBACK);
	return VM_FAULT_FALLBACK;
}
#else
static vm_fault_t ttm_bo_vm_insert_huge(struct vm_fault *vmf,
					struct ttm_buffer_object *bo,
					pgoff_t page_offset,
					pgoff_t fault_page_size,
					pgprot_t pgprot)
{
	return VM_FAULT_FALLBACK;
}
#endif

/**
 * ttm_bo_vm_fault_reserved - TTM fault helper
 * @vmf: The struct vm_fault given as argument to the fault callback
 * @prot: The page protection to be used for this memory area.
 * @num_prefault: Maximum number of prefault pages. The caller may want to
 * specify this based on madvice settings and the size of the GPU object
 * backed by the memory.
 * @fault_page_size: The size of the fault in pages.
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
				    pgoff_t num_prefault,
				    pgoff_t fault_page_size)
{
	struct vm_area_struct *vma = vmf->vma;
	struct ttm_buffer_object *bo = vma->vm_private_data;
	struct ttm_bo_device *bdev = bo->bdev;
	unsigned long page_offset;
	unsigned long page_last;
	unsigned long pfn;
	struct ttm_tt *ttm = NULL;
	struct page *page;
	int err;
	pgoff_t i;
	vm_fault_t ret = VM_FAULT_NOPAGE;
	unsigned long address = vmf->address;

	if (bdev->driver->fault_reserve_notify) {
		struct dma_fence *moving = dma_fence_get(bo->moving);

		err = bdev->driver->fault_reserve_notify(bo);
		switch (err) {
		case 0:
			break;
		case -EBUSY:
		case -ERESTARTSYS:
			dma_fence_put(moving);
			return VM_FAULT_NOPAGE;
		default:
			dma_fence_put(moving);
			return VM_FAULT_SIGBUS;
		}

		if (bo->moving != moving) {
			ttm_bo_move_to_lru_tail_unlocked(bo);
		}
		dma_fence_put(moving);
	}

	/*
	 * Wait for buffer data in transit, due to a pipelined
	 * move.
	 */
	ret = ttm_bo_vm_fault_idle(bo, vmf);
	if (unlikely(ret != 0))
		return ret;

	err = ttm_mem_io_reserve(bdev, &bo->mem);
	if (unlikely(err != 0))
		return VM_FAULT_SIGBUS;

	page_offset = ((address - vma->vm_start) >> PAGE_SHIFT) +
		vma->vm_pgoff - drm_vma_node_start(&bo->base.vma_node);
	page_last = vma_pages(vma) + vma->vm_pgoff -
		drm_vma_node_start(&bo->base.vma_node);

	if (unlikely(page_offset >= bo->num_pages))
		return VM_FAULT_SIGBUS;

	prot = ttm_io_prot(bo->mem.placement, prot);
	if (!bo->mem.bus.is_iomem) {
		struct ttm_operation_ctx ctx = {
			.interruptible = false,
			.no_wait_gpu = false,
			.flags = TTM_OPT_FLAG_FORCE_ALLOC

		};

		ttm = bo->ttm;
		if (ttm_tt_populate(bdev, bo->ttm, &ctx))
			return VM_FAULT_OOM;
	} else {
		/* Iomem should not be marked encrypted */
		prot = pgprot_decrypted(prot);
	}

	/* We don't prefault on huge faults. Yet. */
	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) && fault_page_size != 1)
		return ttm_bo_vm_insert_huge(vmf, bo, page_offset,
					     fault_page_size, prot);

	/*
	 * Speculatively prefault a number of pages. Only error on
	 * first page.
	 */
	for (i = 0; i < num_prefault; ++i) {
		if (bo->mem.bus.is_iomem) {
			pfn = ttm_bo_io_mem_pfn(bo, page_offset);
		} else {
			page = ttm->pages[page_offset];
			if (unlikely(!page && i == 0)) {
				return VM_FAULT_OOM;
			} else if (unlikely(!page)) {
				break;
			}
			page->index = drm_vma_node_start(&bo->base.vma_node) +
				page_offset;
			pfn = page_to_pfn(page);
		}

		/*
		 * Note that the value of @prot at this point may differ from
		 * the value of @vma->vm_page_prot in the caching- and
		 * encryption bits. This is because the exact location of the
		 * data may not be known at mmap() time and may also change
		 * at arbitrary times while the data is mmap'ed.
		 * See vmf_insert_mixed_prot() for a discussion.
		 */
		if (vma->vm_flags & VM_MIXEDMAP)
			ret = vmf_insert_mixed_prot(vma, address,
						    __pfn_to_pfn_t(pfn, PFN_DEV),
						    prot);
		else
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

vm_fault_t ttm_bo_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	pgprot_t prot;
	struct ttm_buffer_object *bo = vma->vm_private_data;
	vm_fault_t ret;

	ret = ttm_bo_vm_reserve(bo, vmf);
	if (ret)
		return ret;

	prot = vma->vm_page_prot;
	ret = ttm_bo_vm_fault_reserved(vmf, prot, TTM_BO_VM_NUM_PREFAULT, 1);
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

	if (len < 1 || (offset + len) >> PAGE_SHIFT > bo->num_pages)
		return -EIO;

	ret = ttm_bo_reserve(bo, true, false, NULL);
	if (ret)
		return ret;

	switch (bo->mem.mem_type) {
	case TTM_PL_SYSTEM:
		if (unlikely(bo->ttm->page_flags & TTM_PAGE_FLAG_SWAPPED)) {
			ret = ttm_tt_swapin(bo->ttm);
			if (unlikely(ret != 0))
				return ret;
		}
		fallthrough;
	case TTM_PL_TT:
		ret = ttm_bo_vm_access_kmap(bo, offset, buf, len, write);
		break;
	default:
		if (bo->bdev->driver->access_memory)
			ret = bo->bdev->driver->access_memory(
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

static struct ttm_buffer_object *ttm_bo_vm_lookup(struct ttm_bo_device *bdev,
						  unsigned long offset,
						  unsigned long pages)
{
	struct drm_vma_offset_node *node;
	struct ttm_buffer_object *bo = NULL;

	drm_vma_offset_lock_lookup(bdev->vma_manager);

	node = drm_vma_offset_lookup_locked(bdev->vma_manager, offset, pages);
	if (likely(node)) {
		bo = container_of(node, struct ttm_buffer_object,
				  base.vma_node);
		bo = ttm_bo_get_unless_zero(bo);
	}

	drm_vma_offset_unlock_lookup(bdev->vma_manager);

	if (!bo)
		pr_err("Could not find buffer object to map\n");

	return bo;
}

static void ttm_bo_mmap_vma_setup(struct ttm_buffer_object *bo, struct vm_area_struct *vma)
{
	vma->vm_ops = &ttm_bo_vm_ops;

	/*
	 * Note: We're transferring the bo reference to
	 * vma->vm_private_data here.
	 */

	vma->vm_private_data = bo;

	/*
	 * We'd like to use VM_PFNMAP on shared mappings, where
	 * (vma->vm_flags & VM_SHARED) != 0, for performance reasons,
	 * but for some reason VM_PFNMAP + x86 PAT + write-combine is very
	 * bad for performance. Until that has been sorted out, use
	 * VM_MIXEDMAP on all mappings. See freedesktop.org bug #75719
	 */
	vma->vm_flags |= VM_MIXEDMAP;
	vma->vm_flags |= VM_IO | VM_DONTEXPAND | VM_DONTDUMP;
}

int ttm_bo_mmap(struct file *filp, struct vm_area_struct *vma,
		struct ttm_bo_device *bdev)
{
	struct ttm_bo_driver *driver;
	struct ttm_buffer_object *bo;
	int ret;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET_START))
		return -EINVAL;

	bo = ttm_bo_vm_lookup(bdev, vma->vm_pgoff, vma_pages(vma));
	if (unlikely(!bo))
		return -EINVAL;

	driver = bo->bdev->driver;
	if (unlikely(!driver->verify_access)) {
		ret = -EPERM;
		goto out_unref;
	}
	ret = driver->verify_access(bo, filp);
	if (unlikely(ret != 0))
		goto out_unref;

	ttm_bo_mmap_vma_setup(bo, vma);
	return 0;
out_unref:
	ttm_bo_put(bo);
	return ret;
}
EXPORT_SYMBOL(ttm_bo_mmap);

int ttm_bo_mmap_obj(struct vm_area_struct *vma, struct ttm_buffer_object *bo)
{
	ttm_bo_get(bo);
	ttm_bo_mmap_vma_setup(bo, vma);
	return 0;
}
EXPORT_SYMBOL(ttm_bo_mmap_obj);

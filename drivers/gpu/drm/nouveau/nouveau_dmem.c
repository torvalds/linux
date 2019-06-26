/*
 * Copyright 2018 Red Hat Inc.
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
#include "nouveau_dmem.h"
#include "nouveau_drv.h"
#include "nouveau_chan.h"
#include "nouveau_dma.h"
#include "nouveau_mem.h"
#include "nouveau_bo.h"

#include <nvif/class.h>
#include <nvif/object.h>
#include <nvif/if500b.h>
#include <nvif/if900b.h>

#include <linux/sched/mm.h>
#include <linux/hmm.h>

/*
 * FIXME: this is ugly right now we are using TTM to allocate vram and we pin
 * it in vram while in use. We likely want to overhaul memory management for
 * nouveau to be more page like (not necessarily with system page size but a
 * bigger page size) at lowest level and have some shim layer on top that would
 * provide the same functionality as TTM.
 */
#define DMEM_CHUNK_SIZE (2UL << 20)
#define DMEM_CHUNK_NPAGES (DMEM_CHUNK_SIZE >> PAGE_SHIFT)

struct nouveau_migrate;

enum nouveau_aper {
	NOUVEAU_APER_VIRT,
	NOUVEAU_APER_VRAM,
	NOUVEAU_APER_HOST,
};

typedef int (*nouveau_migrate_copy_t)(struct nouveau_drm *drm, u64 npages,
				      enum nouveau_aper, u64 dst_addr,
				      enum nouveau_aper, u64 src_addr);

struct nouveau_dmem_chunk {
	struct list_head list;
	struct nouveau_bo *bo;
	struct nouveau_drm *drm;
	unsigned long pfn_first;
	unsigned long callocated;
	unsigned long bitmap[BITS_TO_LONGS(DMEM_CHUNK_NPAGES)];
	spinlock_t lock;
};

struct nouveau_dmem_migrate {
	nouveau_migrate_copy_t copy_func;
	struct nouveau_channel *chan;
};

struct nouveau_dmem {
	struct hmm_devmem *devmem;
	struct nouveau_dmem_migrate migrate;
	struct list_head chunk_free;
	struct list_head chunk_full;
	struct list_head chunk_empty;
	struct mutex mutex;
};

struct nouveau_dmem_fault {
	struct nouveau_drm *drm;
	struct nouveau_fence *fence;
	dma_addr_t *dma;
	unsigned long npages;
};

struct nouveau_migrate {
	struct vm_area_struct *vma;
	struct nouveau_drm *drm;
	struct nouveau_fence *fence;
	unsigned long npages;
	dma_addr_t *dma;
	unsigned long dma_nr;
};

static void
nouveau_dmem_free(struct hmm_devmem *devmem, struct page *page)
{
	struct nouveau_dmem_chunk *chunk;
	unsigned long idx;

	chunk = (void *)hmm_devmem_page_get_drvdata(page);
	idx = page_to_pfn(page) - chunk->pfn_first;

	/*
	 * FIXME:
	 *
	 * This is really a bad example, we need to overhaul nouveau memory
	 * management to be more page focus and allow lighter locking scheme
	 * to be use in the process.
	 */
	spin_lock(&chunk->lock);
	clear_bit(idx, chunk->bitmap);
	WARN_ON(!chunk->callocated);
	chunk->callocated--;
	/*
	 * FIXME when chunk->callocated reach 0 we should add the chunk to
	 * a reclaim list so that it can be freed in case of memory pressure.
	 */
	spin_unlock(&chunk->lock);
}

static void
nouveau_dmem_fault_alloc_and_copy(struct vm_area_struct *vma,
				  const unsigned long *src_pfns,
				  unsigned long *dst_pfns,
				  unsigned long start,
				  unsigned long end,
				  void *private)
{
	struct nouveau_dmem_fault *fault = private;
	struct nouveau_drm *drm = fault->drm;
	struct device *dev = drm->dev->dev;
	unsigned long addr, i, npages = 0;
	nouveau_migrate_copy_t copy;
	int ret;


	/* First allocate new memory */
	for (addr = start, i = 0; addr < end; addr += PAGE_SIZE, i++) {
		struct page *dpage, *spage;

		dst_pfns[i] = 0;
		spage = migrate_pfn_to_page(src_pfns[i]);
		if (!spage || !(src_pfns[i] & MIGRATE_PFN_MIGRATE))
			continue;

		dpage = alloc_page_vma(GFP_HIGHUSER, vma, addr);
		if (!dpage) {
			dst_pfns[i] = MIGRATE_PFN_ERROR;
			continue;
		}
		lock_page(dpage);

		dst_pfns[i] = migrate_pfn(page_to_pfn(dpage)) |
			      MIGRATE_PFN_LOCKED;
		npages++;
	}

	/* Allocate storage for DMA addresses, so we can unmap later. */
	fault->dma = kmalloc(sizeof(*fault->dma) * npages, GFP_KERNEL);
	if (!fault->dma)
		goto error;

	/* Copy things over */
	copy = drm->dmem->migrate.copy_func;
	for (addr = start, i = 0; addr < end; addr += PAGE_SIZE, i++) {
		struct nouveau_dmem_chunk *chunk;
		struct page *spage, *dpage;
		u64 src_addr, dst_addr;

		dpage = migrate_pfn_to_page(dst_pfns[i]);
		if (!dpage || dst_pfns[i] == MIGRATE_PFN_ERROR)
			continue;

		spage = migrate_pfn_to_page(src_pfns[i]);
		if (!spage || !(src_pfns[i] & MIGRATE_PFN_MIGRATE)) {
			dst_pfns[i] = MIGRATE_PFN_ERROR;
			__free_page(dpage);
			continue;
		}

		fault->dma[fault->npages] =
			dma_map_page_attrs(dev, dpage, 0, PAGE_SIZE,
					   PCI_DMA_BIDIRECTIONAL,
					   DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(dev, fault->dma[fault->npages])) {
			dst_pfns[i] = MIGRATE_PFN_ERROR;
			__free_page(dpage);
			continue;
		}

		dst_addr = fault->dma[fault->npages++];

		chunk = (void *)hmm_devmem_page_get_drvdata(spage);
		src_addr = page_to_pfn(spage) - chunk->pfn_first;
		src_addr = (src_addr << PAGE_SHIFT) + chunk->bo->bo.offset;

		ret = copy(drm, 1, NOUVEAU_APER_HOST, dst_addr,
				   NOUVEAU_APER_VRAM, src_addr);
		if (ret) {
			dst_pfns[i] = MIGRATE_PFN_ERROR;
			__free_page(dpage);
			continue;
		}
	}

	nouveau_fence_new(drm->dmem->migrate.chan, false, &fault->fence);

	return;

error:
	for (addr = start, i = 0; addr < end; addr += PAGE_SIZE, ++i) {
		struct page *page;

		if (!dst_pfns[i] || dst_pfns[i] == MIGRATE_PFN_ERROR)
			continue;

		page = migrate_pfn_to_page(dst_pfns[i]);
		dst_pfns[i] = MIGRATE_PFN_ERROR;
		if (page == NULL)
			continue;

		__free_page(page);
	}
}

void nouveau_dmem_fault_finalize_and_map(struct vm_area_struct *vma,
					 const unsigned long *src_pfns,
					 const unsigned long *dst_pfns,
					 unsigned long start,
					 unsigned long end,
					 void *private)
{
	struct nouveau_dmem_fault *fault = private;
	struct nouveau_drm *drm = fault->drm;

	if (fault->fence) {
		nouveau_fence_wait(fault->fence, true, false);
		nouveau_fence_unref(&fault->fence);
	} else {
		/*
		 * FIXME wait for channel to be IDLE before calling finalizing
		 * the hmem object below (nouveau_migrate_hmem_fini()).
		 */
	}

	while (fault->npages--) {
		dma_unmap_page(drm->dev->dev, fault->dma[fault->npages],
			       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	}
	kfree(fault->dma);
}

static const struct migrate_vma_ops nouveau_dmem_fault_migrate_ops = {
	.alloc_and_copy		= nouveau_dmem_fault_alloc_and_copy,
	.finalize_and_map	= nouveau_dmem_fault_finalize_and_map,
};

static vm_fault_t
nouveau_dmem_fault(struct hmm_devmem *devmem,
		   struct vm_area_struct *vma,
		   unsigned long addr,
		   const struct page *page,
		   unsigned int flags,
		   pmd_t *pmdp)
{
	struct drm_device *drm_dev = dev_get_drvdata(devmem->device);
	unsigned long src[1] = {0}, dst[1] = {0};
	struct nouveau_dmem_fault fault = {0};
	int ret;



	/*
	 * FIXME what we really want is to find some heuristic to migrate more
	 * than just one page on CPU fault. When such fault happens it is very
	 * likely that more surrounding page will CPU fault too.
	 */
	fault.drm = nouveau_drm(drm_dev);
	ret = migrate_vma(&nouveau_dmem_fault_migrate_ops, vma, addr,
			  addr + PAGE_SIZE, src, dst, &fault);
	if (ret)
		return VM_FAULT_SIGBUS;

	if (dst[0] == MIGRATE_PFN_ERROR)
		return VM_FAULT_SIGBUS;

	return 0;
}

static const struct hmm_devmem_ops
nouveau_dmem_devmem_ops = {
	.free = nouveau_dmem_free,
	.fault = nouveau_dmem_fault,
};

static int
nouveau_dmem_chunk_alloc(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk;
	int ret;

	if (drm->dmem == NULL)
		return -EINVAL;

	mutex_lock(&drm->dmem->mutex);
	chunk = list_first_entry_or_null(&drm->dmem->chunk_empty,
					 struct nouveau_dmem_chunk,
					 list);
	if (chunk == NULL) {
		mutex_unlock(&drm->dmem->mutex);
		return -ENOMEM;
	}

	list_del(&chunk->list);
	mutex_unlock(&drm->dmem->mutex);

	ret = nouveau_bo_new(&drm->client, DMEM_CHUNK_SIZE, 0,
			     TTM_PL_FLAG_VRAM, 0, 0, NULL, NULL,
			     &chunk->bo);
	if (ret)
		goto out;

	ret = nouveau_bo_pin(chunk->bo, TTM_PL_FLAG_VRAM, false);
	if (ret) {
		nouveau_bo_ref(NULL, &chunk->bo);
		goto out;
	}

	bitmap_zero(chunk->bitmap, DMEM_CHUNK_NPAGES);
	spin_lock_init(&chunk->lock);

out:
	mutex_lock(&drm->dmem->mutex);
	if (chunk->bo)
		list_add(&chunk->list, &drm->dmem->chunk_empty);
	else
		list_add_tail(&chunk->list, &drm->dmem->chunk_empty);
	mutex_unlock(&drm->dmem->mutex);

	return ret;
}

static struct nouveau_dmem_chunk *
nouveau_dmem_chunk_first_free_locked(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk;

	chunk = list_first_entry_or_null(&drm->dmem->chunk_free,
					 struct nouveau_dmem_chunk,
					 list);
	if (chunk)
		return chunk;

	chunk = list_first_entry_or_null(&drm->dmem->chunk_empty,
					 struct nouveau_dmem_chunk,
					 list);
	if (chunk->bo)
		return chunk;

	return NULL;
}

static int
nouveau_dmem_pages_alloc(struct nouveau_drm *drm,
			 unsigned long npages,
			 unsigned long *pages)
{
	struct nouveau_dmem_chunk *chunk;
	unsigned long c;
	int ret;

	memset(pages, 0xff, npages * sizeof(*pages));

	mutex_lock(&drm->dmem->mutex);
	for (c = 0; c < npages;) {
		unsigned long i;

		chunk = nouveau_dmem_chunk_first_free_locked(drm);
		if (chunk == NULL) {
			mutex_unlock(&drm->dmem->mutex);
			ret = nouveau_dmem_chunk_alloc(drm);
			if (ret) {
				if (c)
					break;
				return ret;
			}
			continue;
		}

		spin_lock(&chunk->lock);
		i = find_first_zero_bit(chunk->bitmap, DMEM_CHUNK_NPAGES);
		while (i < DMEM_CHUNK_NPAGES && c < npages) {
			pages[c] = chunk->pfn_first + i;
			set_bit(i, chunk->bitmap);
			chunk->callocated++;
			c++;

			i = find_next_zero_bit(chunk->bitmap,
					DMEM_CHUNK_NPAGES, i);
		}
		spin_unlock(&chunk->lock);
	}
	mutex_unlock(&drm->dmem->mutex);

	return 0;
}

static struct page *
nouveau_dmem_page_alloc_locked(struct nouveau_drm *drm)
{
	unsigned long pfns[1];
	struct page *page;
	int ret;

	/* FIXME stop all the miss-match API ... */
	ret = nouveau_dmem_pages_alloc(drm, 1, pfns);
	if (ret)
		return NULL;

	page = pfn_to_page(pfns[0]);
	get_page(page);
	lock_page(page);
	return page;
}

static void
nouveau_dmem_page_free_locked(struct nouveau_drm *drm, struct page *page)
{
	unlock_page(page);
	put_page(page);
}

void
nouveau_dmem_resume(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk;
	int ret;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);
	list_for_each_entry (chunk, &drm->dmem->chunk_free, list) {
		ret = nouveau_bo_pin(chunk->bo, TTM_PL_FLAG_VRAM, false);
		/* FIXME handle pin failure */
		WARN_ON(ret);
	}
	list_for_each_entry (chunk, &drm->dmem->chunk_full, list) {
		ret = nouveau_bo_pin(chunk->bo, TTM_PL_FLAG_VRAM, false);
		/* FIXME handle pin failure */
		WARN_ON(ret);
	}
	mutex_unlock(&drm->dmem->mutex);
}

void
nouveau_dmem_suspend(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);
	list_for_each_entry (chunk, &drm->dmem->chunk_free, list) {
		nouveau_bo_unpin(chunk->bo);
	}
	list_for_each_entry (chunk, &drm->dmem->chunk_full, list) {
		nouveau_bo_unpin(chunk->bo);
	}
	mutex_unlock(&drm->dmem->mutex);
}

void
nouveau_dmem_fini(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk, *tmp;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);

	WARN_ON(!list_empty(&drm->dmem->chunk_free));
	WARN_ON(!list_empty(&drm->dmem->chunk_full));

	list_for_each_entry_safe (chunk, tmp, &drm->dmem->chunk_empty, list) {
		if (chunk->bo) {
			nouveau_bo_unpin(chunk->bo);
			nouveau_bo_ref(NULL, &chunk->bo);
		}
		list_del(&chunk->list);
		kfree(chunk);
	}

	mutex_unlock(&drm->dmem->mutex);
}

static int
nvc0b5_migrate_copy(struct nouveau_drm *drm, u64 npages,
		    enum nouveau_aper dst_aper, u64 dst_addr,
		    enum nouveau_aper src_aper, u64 src_addr)
{
	struct nouveau_channel *chan = drm->dmem->migrate.chan;
	u32 launch_dma = (1 << 9) /* MULTI_LINE_ENABLE. */ |
			 (1 << 8) /* DST_MEMORY_LAYOUT_PITCH. */ |
			 (1 << 7) /* SRC_MEMORY_LAYOUT_PITCH. */ |
			 (1 << 2) /* FLUSH_ENABLE_TRUE. */ |
			 (2 << 0) /* DATA_TRANSFER_TYPE_NON_PIPELINED. */;
	int ret;

	ret = RING_SPACE(chan, 13);
	if (ret)
		return ret;

	if (src_aper != NOUVEAU_APER_VIRT) {
		switch (src_aper) {
		case NOUVEAU_APER_VRAM:
			BEGIN_IMC0(chan, NvSubCopy, 0x0260, 0);
			break;
		case NOUVEAU_APER_HOST:
			BEGIN_IMC0(chan, NvSubCopy, 0x0260, 1);
			break;
		default:
			return -EINVAL;
		}
		launch_dma |= 0x00001000; /* SRC_TYPE_PHYSICAL. */
	}

	if (dst_aper != NOUVEAU_APER_VIRT) {
		switch (dst_aper) {
		case NOUVEAU_APER_VRAM:
			BEGIN_IMC0(chan, NvSubCopy, 0x0264, 0);
			break;
		case NOUVEAU_APER_HOST:
			BEGIN_IMC0(chan, NvSubCopy, 0x0264, 1);
			break;
		default:
			return -EINVAL;
		}
		launch_dma |= 0x00002000; /* DST_TYPE_PHYSICAL. */
	}

	BEGIN_NVC0(chan, NvSubCopy, 0x0400, 8);
	OUT_RING  (chan, upper_32_bits(src_addr));
	OUT_RING  (chan, lower_32_bits(src_addr));
	OUT_RING  (chan, upper_32_bits(dst_addr));
	OUT_RING  (chan, lower_32_bits(dst_addr));
	OUT_RING  (chan, PAGE_SIZE);
	OUT_RING  (chan, PAGE_SIZE);
	OUT_RING  (chan, PAGE_SIZE);
	OUT_RING  (chan, npages);
	BEGIN_NVC0(chan, NvSubCopy, 0x0300, 1);
	OUT_RING  (chan, launch_dma);
	return 0;
}

static int
nouveau_dmem_migrate_init(struct nouveau_drm *drm)
{
	switch (drm->ttm.copy.oclass) {
	case PASCAL_DMA_COPY_A:
	case PASCAL_DMA_COPY_B:
	case  VOLTA_DMA_COPY_A:
	case TURING_DMA_COPY_A:
		drm->dmem->migrate.copy_func = nvc0b5_migrate_copy;
		drm->dmem->migrate.chan = drm->ttm.chan;
		return 0;
	default:
		break;
	}
	return -ENODEV;
}

void
nouveau_dmem_init(struct nouveau_drm *drm)
{
	struct device *device = drm->dev->dev;
	unsigned long i, size;
	int ret;

	/* This only make sense on PASCAL or newer */
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_PASCAL)
		return;

	if (!(drm->dmem = kzalloc(sizeof(*drm->dmem), GFP_KERNEL)))
		return;

	mutex_init(&drm->dmem->mutex);
	INIT_LIST_HEAD(&drm->dmem->chunk_free);
	INIT_LIST_HEAD(&drm->dmem->chunk_full);
	INIT_LIST_HEAD(&drm->dmem->chunk_empty);

	size = ALIGN(drm->client.device.info.ram_user, DMEM_CHUNK_SIZE);

	/* Initialize migration dma helpers before registering memory */
	ret = nouveau_dmem_migrate_init(drm);
	if (ret) {
		kfree(drm->dmem);
		drm->dmem = NULL;
		return;
	}

	/*
	 * FIXME we need some kind of policy to decide how much VRAM we
	 * want to register with HMM. For now just register everything
	 * and latter if we want to do thing like over commit then we
	 * could revisit this.
	 */
	drm->dmem->devmem = hmm_devmem_add(&nouveau_dmem_devmem_ops,
					   device, size);
	if (IS_ERR(drm->dmem->devmem)) {
		kfree(drm->dmem);
		drm->dmem = NULL;
		return;
	}

	for (i = 0; i < (size / DMEM_CHUNK_SIZE); ++i) {
		struct nouveau_dmem_chunk *chunk;
		struct page *page;
		unsigned long j;

		chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
		if (chunk == NULL) {
			nouveau_dmem_fini(drm);
			return;
		}

		chunk->drm = drm;
		chunk->pfn_first = drm->dmem->devmem->pfn_first;
		chunk->pfn_first += (i * DMEM_CHUNK_NPAGES);
		list_add_tail(&chunk->list, &drm->dmem->chunk_empty);

		page = pfn_to_page(chunk->pfn_first);
		for (j = 0; j < DMEM_CHUNK_NPAGES; ++j, ++page) {
			hmm_devmem_page_set_drvdata(page, (long)chunk);
		}
	}

	NV_INFO(drm, "DMEM: registered %ldMB of device memory\n", size >> 20);
}

static void
nouveau_dmem_migrate_alloc_and_copy(struct vm_area_struct *vma,
				    const unsigned long *src_pfns,
				    unsigned long *dst_pfns,
				    unsigned long start,
				    unsigned long end,
				    void *private)
{
	struct nouveau_migrate *migrate = private;
	struct nouveau_drm *drm = migrate->drm;
	struct device *dev = drm->dev->dev;
	unsigned long addr, i, npages = 0;
	nouveau_migrate_copy_t copy;
	int ret;

	/* First allocate new memory */
	for (addr = start, i = 0; addr < end; addr += PAGE_SIZE, i++) {
		struct page *dpage, *spage;

		dst_pfns[i] = 0;
		spage = migrate_pfn_to_page(src_pfns[i]);
		if (!spage || !(src_pfns[i] & MIGRATE_PFN_MIGRATE))
			continue;

		dpage = nouveau_dmem_page_alloc_locked(drm);
		if (!dpage)
			continue;

		dst_pfns[i] = migrate_pfn(page_to_pfn(dpage)) |
			      MIGRATE_PFN_LOCKED |
			      MIGRATE_PFN_DEVICE;
		npages++;
	}

	if (!npages)
		return;

	/* Allocate storage for DMA addresses, so we can unmap later. */
	migrate->dma = kmalloc(sizeof(*migrate->dma) * npages, GFP_KERNEL);
	if (!migrate->dma)
		goto error;

	/* Copy things over */
	copy = drm->dmem->migrate.copy_func;
	for (addr = start, i = 0; addr < end; addr += PAGE_SIZE, i++) {
		struct nouveau_dmem_chunk *chunk;
		struct page *spage, *dpage;
		u64 src_addr, dst_addr;

		dpage = migrate_pfn_to_page(dst_pfns[i]);
		if (!dpage || dst_pfns[i] == MIGRATE_PFN_ERROR)
			continue;

		chunk = (void *)hmm_devmem_page_get_drvdata(dpage);
		dst_addr = page_to_pfn(dpage) - chunk->pfn_first;
		dst_addr = (dst_addr << PAGE_SHIFT) + chunk->bo->bo.offset;

		spage = migrate_pfn_to_page(src_pfns[i]);
		if (!spage || !(src_pfns[i] & MIGRATE_PFN_MIGRATE)) {
			nouveau_dmem_page_free_locked(drm, dpage);
			dst_pfns[i] = 0;
			continue;
		}

		migrate->dma[migrate->dma_nr] =
			dma_map_page_attrs(dev, spage, 0, PAGE_SIZE,
					   PCI_DMA_BIDIRECTIONAL,
					   DMA_ATTR_SKIP_CPU_SYNC);
		if (dma_mapping_error(dev, migrate->dma[migrate->dma_nr])) {
			nouveau_dmem_page_free_locked(drm, dpage);
			dst_pfns[i] = 0;
			continue;
		}

		src_addr = migrate->dma[migrate->dma_nr++];

		ret = copy(drm, 1, NOUVEAU_APER_VRAM, dst_addr,
				   NOUVEAU_APER_HOST, src_addr);
		if (ret) {
			nouveau_dmem_page_free_locked(drm, dpage);
			dst_pfns[i] = 0;
			continue;
		}
	}

	nouveau_fence_new(drm->dmem->migrate.chan, false, &migrate->fence);

	return;

error:
	for (addr = start, i = 0; addr < end; addr += PAGE_SIZE, ++i) {
		struct page *page;

		if (!dst_pfns[i] || dst_pfns[i] == MIGRATE_PFN_ERROR)
			continue;

		page = migrate_pfn_to_page(dst_pfns[i]);
		dst_pfns[i] = MIGRATE_PFN_ERROR;
		if (page == NULL)
			continue;

		__free_page(page);
	}
}

void nouveau_dmem_migrate_finalize_and_map(struct vm_area_struct *vma,
					   const unsigned long *src_pfns,
					   const unsigned long *dst_pfns,
					   unsigned long start,
					   unsigned long end,
					   void *private)
{
	struct nouveau_migrate *migrate = private;
	struct nouveau_drm *drm = migrate->drm;

	if (migrate->fence) {
		nouveau_fence_wait(migrate->fence, true, false);
		nouveau_fence_unref(&migrate->fence);
	} else {
		/*
		 * FIXME wait for channel to be IDLE before finalizing
		 * the hmem object below (nouveau_migrate_hmem_fini()) ?
		 */
	}

	while (migrate->dma_nr--) {
		dma_unmap_page(drm->dev->dev, migrate->dma[migrate->dma_nr],
			       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
	}
	kfree(migrate->dma);

	/*
	 * FIXME optimization: update GPU page table to point to newly
	 * migrated memory.
	 */
}

static const struct migrate_vma_ops nouveau_dmem_migrate_ops = {
	.alloc_and_copy		= nouveau_dmem_migrate_alloc_and_copy,
	.finalize_and_map	= nouveau_dmem_migrate_finalize_and_map,
};

int
nouveau_dmem_migrate_vma(struct nouveau_drm *drm,
			 struct vm_area_struct *vma,
			 unsigned long start,
			 unsigned long end)
{
	unsigned long *src_pfns, *dst_pfns, npages;
	struct nouveau_migrate migrate = {0};
	unsigned long i, c, max;
	int ret = 0;

	npages = (end - start) >> PAGE_SHIFT;
	max = min(SG_MAX_SINGLE_ALLOC, npages);
	src_pfns = kzalloc(sizeof(long) * max, GFP_KERNEL);
	if (src_pfns == NULL)
		return -ENOMEM;
	dst_pfns = kzalloc(sizeof(long) * max, GFP_KERNEL);
	if (dst_pfns == NULL) {
		kfree(src_pfns);
		return -ENOMEM;
	}

	migrate.drm = drm;
	migrate.vma = vma;
	migrate.npages = npages;
	for (i = 0; i < npages; i += c) {
		unsigned long next;

		c = min(SG_MAX_SINGLE_ALLOC, npages);
		next = start + (c << PAGE_SHIFT);
		ret = migrate_vma(&nouveau_dmem_migrate_ops, vma, start,
				  next, src_pfns, dst_pfns, &migrate);
		if (ret)
			goto out;
		start = next;
	}

out:
	kfree(dst_pfns);
	kfree(src_pfns);
	return ret;
}

static inline bool
nouveau_dmem_page(struct nouveau_drm *drm, struct page *page)
{
	if (!is_device_private_page(page))
		return false;

	if (drm->dmem->devmem != page->pgmap->data)
		return false;

	return true;
}

void
nouveau_dmem_convert_pfn(struct nouveau_drm *drm,
			 struct hmm_range *range)
{
	unsigned long i, npages;

	npages = (range->end - range->start) >> PAGE_SHIFT;
	for (i = 0; i < npages; ++i) {
		struct nouveau_dmem_chunk *chunk;
		struct page *page;
		uint64_t addr;

		page = hmm_pfn_to_page(range, range->pfns[i]);
		if (page == NULL)
			continue;

		if (!(range->pfns[i] & range->flags[HMM_PFN_DEVICE_PRIVATE])) {
			continue;
		}

		if (!nouveau_dmem_page(drm, page)) {
			WARN(1, "Some unknown device memory !\n");
			range->pfns[i] = 0;
			continue;
		}

		chunk = (void *)hmm_devmem_page_get_drvdata(page);
		addr = page_to_pfn(page) - chunk->pfn_first;
		addr = (addr + chunk->bo->bo.mem.start) << PAGE_SHIFT;

		range->pfns[i] &= ((1UL << range->pfn_shift) - 1);
		range->pfns[i] |= (addr >> PAGE_SHIFT) << range->pfn_shift;
	}
}

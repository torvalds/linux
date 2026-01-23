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
#include "nouveau_svm.h"

#include <nvif/class.h>
#include <nvif/object.h>
#include <nvif/push906f.h>
#include <nvif/if000c.h>
#include <nvif/if500b.h>
#include <nvif/if900b.h>

#include <nvhw/class/cla0b5.h>

#include <linux/sched/mm.h>
#include <linux/hmm.h>
#include <linux/memremap.h>
#include <linux/migrate.h>

/*
 * FIXME: this is ugly right now we are using TTM to allocate vram and we pin
 * it in vram while in use. We likely want to overhaul memory management for
 * nouveau to be more page like (not necessarily with system page size but a
 * bigger page size) at lowest level and have some shim layer on top that would
 * provide the same functionality as TTM.
 */
#define DMEM_CHUNK_SIZE (2UL << 20)
#define DMEM_CHUNK_NPAGES (DMEM_CHUNK_SIZE >> PAGE_SHIFT)
#define NR_CHUNKS (128)

enum nouveau_aper {
	NOUVEAU_APER_VIRT,
	NOUVEAU_APER_VRAM,
	NOUVEAU_APER_HOST,
};

typedef int (*nouveau_migrate_copy_t)(struct nouveau_drm *drm, u64 npages,
				      enum nouveau_aper, u64 dst_addr,
				      enum nouveau_aper, u64 src_addr);
typedef int (*nouveau_clear_page_t)(struct nouveau_drm *drm, u32 length,
				      enum nouveau_aper, u64 dst_addr);

struct nouveau_dmem_chunk {
	struct list_head list;
	struct nouveau_bo *bo;
	struct nouveau_drm *drm;
	unsigned long callocated;
	struct dev_pagemap pagemap;
};

struct nouveau_dmem_migrate {
	nouveau_migrate_copy_t copy_func;
	nouveau_clear_page_t clear_func;
	struct nouveau_channel *chan;
};

struct nouveau_dmem {
	struct nouveau_drm *drm;
	struct nouveau_dmem_migrate migrate;
	struct list_head chunks;
	struct mutex mutex;
	struct page *free_pages;
	struct folio *free_folios;
	spinlock_t lock;
};

struct nouveau_dmem_dma_info {
	dma_addr_t dma_addr;
	size_t size;
};

static struct nouveau_dmem_chunk *nouveau_page_to_chunk(struct page *page)
{
	return container_of(page_pgmap(page), struct nouveau_dmem_chunk,
			    pagemap);
}

static struct nouveau_drm *page_to_drm(struct page *page)
{
	struct nouveau_dmem_chunk *chunk = nouveau_page_to_chunk(page);

	return chunk->drm;
}

unsigned long nouveau_dmem_page_addr(struct page *page)
{
	struct nouveau_dmem_chunk *chunk = nouveau_page_to_chunk(page);
	unsigned long off = (page_to_pfn(page) << PAGE_SHIFT) -
				chunk->pagemap.range.start;

	return chunk->bo->offset + off;
}

static void nouveau_dmem_folio_free(struct folio *folio)
{
	struct page *page = &folio->page;
	struct nouveau_dmem_chunk *chunk = nouveau_page_to_chunk(page);
	struct nouveau_dmem *dmem = chunk->drm->dmem;

	spin_lock(&dmem->lock);
	if (folio_order(folio)) {
		page->zone_device_data = dmem->free_folios;
		dmem->free_folios = folio;
	} else {
		page->zone_device_data = dmem->free_pages;
		dmem->free_pages = page;
	}

	WARN_ON(!chunk->callocated);
	chunk->callocated--;
	/*
	 * FIXME when chunk->callocated reach 0 we should add the chunk to
	 * a reclaim list so that it can be freed in case of memory pressure.
	 */
	spin_unlock(&dmem->lock);
}

static void nouveau_dmem_fence_done(struct nouveau_fence **fence)
{
	if (fence) {
		nouveau_fence_wait(*fence, true, false);
		nouveau_fence_unref(fence);
	} else {
		/*
		 * FIXME wait for channel to be IDLE before calling finalizing
		 * the hmem object.
		 */
	}
}

static int nouveau_dmem_copy_folio(struct nouveau_drm *drm,
				   struct folio *sfolio, struct folio *dfolio,
				   struct nouveau_dmem_dma_info *dma_info)
{
	struct device *dev = drm->dev->dev;
	struct page *dpage = folio_page(dfolio, 0);
	struct page *spage = folio_page(sfolio, 0);

	folio_lock(dfolio);

	dma_info->dma_addr = dma_map_page(dev, dpage, 0, page_size(dpage),
					DMA_BIDIRECTIONAL);
	dma_info->size = page_size(dpage);
	if (dma_mapping_error(dev, dma_info->dma_addr))
		return -EIO;

	if (drm->dmem->migrate.copy_func(drm, folio_nr_pages(sfolio),
					 NOUVEAU_APER_HOST, dma_info->dma_addr,
					 NOUVEAU_APER_VRAM,
					 nouveau_dmem_page_addr(spage))) {
		dma_unmap_page(dev, dma_info->dma_addr, page_size(dpage),
					DMA_BIDIRECTIONAL);
		return -EIO;
	}

	return 0;
}

static vm_fault_t nouveau_dmem_migrate_to_ram(struct vm_fault *vmf)
{
	struct nouveau_drm *drm = page_to_drm(vmf->page);
	struct nouveau_dmem *dmem = drm->dmem;
	struct nouveau_fence *fence;
	struct nouveau_svmm *svmm;
	struct page *dpage;
	vm_fault_t ret = 0;
	int err;
	struct migrate_vma args = {
		.vma		= vmf->vma,
		.pgmap_owner	= drm->dev,
		.fault_page	= vmf->page,
		.flags		= MIGRATE_VMA_SELECT_DEVICE_PRIVATE |
				  MIGRATE_VMA_SELECT_COMPOUND,
		.src = NULL,
		.dst = NULL,
	};
	unsigned int order, nr;
	struct folio *sfolio, *dfolio;
	struct nouveau_dmem_dma_info dma_info;

	sfolio = page_folio(vmf->page);
	order = folio_order(sfolio);
	nr = 1 << order;

	/*
	 * Handle partial unmap faults, where the folio is large, but
	 * the pmd is split.
	 */
	if (vmf->pte) {
		order = 0;
		nr = 1;
	}

	if (order)
		args.flags |= MIGRATE_VMA_SELECT_COMPOUND;

	args.start = ALIGN_DOWN(vmf->address, (PAGE_SIZE << order));
	args.vma = vmf->vma;
	args.end = args.start + (PAGE_SIZE << order);
	args.src = kcalloc(nr, sizeof(*args.src), GFP_KERNEL);
	args.dst = kcalloc(nr, sizeof(*args.dst), GFP_KERNEL);

	if (!args.src || !args.dst) {
		ret = VM_FAULT_OOM;
		goto err;
	}
	/*
	 * FIXME what we really want is to find some heuristic to migrate more
	 * than just one page on CPU fault. When such fault happens it is very
	 * likely that more surrounding page will CPU fault too.
	 */
	if (migrate_vma_setup(&args) < 0)
		return VM_FAULT_SIGBUS;
	if (!args.cpages)
		return 0;

	if (order)
		dpage = folio_page(vma_alloc_folio(GFP_HIGHUSER | __GFP_ZERO,
					order, vmf->vma, vmf->address), 0);
	else
		dpage = alloc_page_vma(GFP_HIGHUSER | __GFP_ZERO, vmf->vma,
					vmf->address);
	if (!dpage) {
		ret = VM_FAULT_OOM;
		goto done;
	}

	args.dst[0] = migrate_pfn(page_to_pfn(dpage));
	if (order)
		args.dst[0] |= MIGRATE_PFN_COMPOUND;
	dfolio = page_folio(dpage);

	svmm = folio_zone_device_data(sfolio);
	mutex_lock(&svmm->mutex);
	nouveau_svmm_invalidate(svmm, args.start, args.end);
	err = nouveau_dmem_copy_folio(drm, sfolio, dfolio, &dma_info);
	mutex_unlock(&svmm->mutex);
	if (err) {
		ret = VM_FAULT_SIGBUS;
		goto done;
	}

	nouveau_fence_new(&fence, dmem->migrate.chan);
	migrate_vma_pages(&args);
	nouveau_dmem_fence_done(&fence);
	dma_unmap_page(drm->dev->dev, dma_info.dma_addr, PAGE_SIZE,
				DMA_BIDIRECTIONAL);
done:
	migrate_vma_finalize(&args);
err:
	kfree(args.src);
	kfree(args.dst);
	return ret;
}

static void nouveau_dmem_folio_split(struct folio *head, struct folio *tail)
{
	if (tail == NULL)
		return;
	tail->pgmap = head->pgmap;
	tail->mapping = head->mapping;
	folio_set_zone_device_data(tail, folio_zone_device_data(head));
}

static const struct dev_pagemap_ops nouveau_dmem_pagemap_ops = {
	.folio_free		= nouveau_dmem_folio_free,
	.migrate_to_ram		= nouveau_dmem_migrate_to_ram,
	.folio_split		= nouveau_dmem_folio_split,
};

static int
nouveau_dmem_chunk_alloc(struct nouveau_drm *drm, struct page **ppage,
			 bool is_large)
{
	struct nouveau_dmem_chunk *chunk;
	struct resource *res;
	struct page *page;
	void *ptr;
	unsigned long i, pfn_first, pfn;
	int ret;

	chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
	if (chunk == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* Allocate unused physical address space for device private pages. */
	res = request_free_mem_region(&iomem_resource, DMEM_CHUNK_SIZE * NR_CHUNKS,
				      "nouveau_dmem");
	if (IS_ERR(res)) {
		ret = PTR_ERR(res);
		goto out_free;
	}

	chunk->drm = drm;
	chunk->pagemap.type = MEMORY_DEVICE_PRIVATE;
	chunk->pagemap.range.start = res->start;
	chunk->pagemap.range.end = res->end;
	chunk->pagemap.nr_range = 1;
	chunk->pagemap.ops = &nouveau_dmem_pagemap_ops;
	chunk->pagemap.owner = drm->dev;

	ret = nouveau_bo_new_pin(&drm->client, NOUVEAU_GEM_DOMAIN_VRAM, DMEM_CHUNK_SIZE,
				 &chunk->bo);
	if (ret)
		goto out_release;

	ptr = memremap_pages(&chunk->pagemap, numa_node_id());
	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		goto out_bo_free;
	}

	mutex_lock(&drm->dmem->mutex);
	list_add(&chunk->list, &drm->dmem->chunks);
	mutex_unlock(&drm->dmem->mutex);

	pfn_first = chunk->pagemap.range.start >> PAGE_SHIFT;
	page = pfn_to_page(pfn_first);
	spin_lock(&drm->dmem->lock);

	pfn = pfn_first;
	for (i = 0; i < NR_CHUNKS; i++) {
		int j;

		if (!IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE) || !is_large) {
			for (j = 0; j < DMEM_CHUNK_NPAGES - 1; j++, pfn++) {
				page = pfn_to_page(pfn);
				page->zone_device_data = drm->dmem->free_pages;
				drm->dmem->free_pages = page;
			}
		} else {
			page = pfn_to_page(pfn);
			page->zone_device_data = drm->dmem->free_folios;
			drm->dmem->free_folios = page_folio(page);
			pfn += DMEM_CHUNK_NPAGES;
		}
	}

	/* Move to next page */
	if (is_large) {
		*ppage = &drm->dmem->free_folios->page;
		drm->dmem->free_folios = (*ppage)->zone_device_data;
	} else {
		*ppage = drm->dmem->free_pages;
		drm->dmem->free_pages = (*ppage)->zone_device_data;
	}

	chunk->callocated++;
	spin_unlock(&drm->dmem->lock);

	NV_INFO(drm, "DMEM: registered %ldMB of %sdevice memory %lx %lx\n",
		NR_CHUNKS * DMEM_CHUNK_SIZE >> 20, is_large ? "THP " : "", pfn_first,
		nouveau_dmem_page_addr(page));

	return 0;

out_bo_free:
	nouveau_bo_unpin_del(&chunk->bo);
out_release:
	release_mem_region(chunk->pagemap.range.start, range_len(&chunk->pagemap.range));
out_free:
	kfree(chunk);
out:
	return ret;
}

static struct page *
nouveau_dmem_page_alloc_locked(struct nouveau_drm *drm, bool is_large)
{
	struct nouveau_dmem_chunk *chunk;
	struct page *page = NULL;
	struct folio *folio = NULL;
	int ret;
	unsigned int order = 0;

	spin_lock(&drm->dmem->lock);
	if (is_large && drm->dmem->free_folios) {
		folio = drm->dmem->free_folios;
		page = &folio->page;
		drm->dmem->free_folios = page->zone_device_data;
		chunk = nouveau_page_to_chunk(&folio->page);
		chunk->callocated++;
		spin_unlock(&drm->dmem->lock);
		order = ilog2(DMEM_CHUNK_NPAGES);
	} else if (!is_large && drm->dmem->free_pages) {
		page = drm->dmem->free_pages;
		drm->dmem->free_pages = page->zone_device_data;
		chunk = nouveau_page_to_chunk(page);
		chunk->callocated++;
		spin_unlock(&drm->dmem->lock);
		folio = page_folio(page);
	} else {
		spin_unlock(&drm->dmem->lock);
		ret = nouveau_dmem_chunk_alloc(drm, &page, is_large);
		if (ret)
			return NULL;
		folio = page_folio(page);
		if (is_large)
			order = ilog2(DMEM_CHUNK_NPAGES);
	}

	zone_device_folio_init(folio, page_pgmap(folio_page(folio, 0)), order);
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
	list_for_each_entry(chunk, &drm->dmem->chunks, list) {
		ret = nouveau_bo_pin(chunk->bo, NOUVEAU_GEM_DOMAIN_VRAM, false);
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
	list_for_each_entry(chunk, &drm->dmem->chunks, list)
		nouveau_bo_unpin(chunk->bo);
	mutex_unlock(&drm->dmem->mutex);
}

/*
 * Evict all pages mapping a chunk.
 */
static void
nouveau_dmem_evict_chunk(struct nouveau_dmem_chunk *chunk)
{
	unsigned long i, npages = range_len(&chunk->pagemap.range) >> PAGE_SHIFT;
	unsigned long *src_pfns, *dst_pfns;
	struct nouveau_dmem_dma_info *dma_info;
	struct nouveau_fence *fence;

	src_pfns = kvcalloc(npages, sizeof(*src_pfns), GFP_KERNEL | __GFP_NOFAIL);
	dst_pfns = kvcalloc(npages, sizeof(*dst_pfns), GFP_KERNEL | __GFP_NOFAIL);
	dma_info = kvcalloc(npages, sizeof(*dma_info), GFP_KERNEL | __GFP_NOFAIL);

	migrate_device_range(src_pfns, chunk->pagemap.range.start >> PAGE_SHIFT,
			npages);

	for (i = 0; i < npages; i++) {
		if (src_pfns[i] & MIGRATE_PFN_MIGRATE) {
			struct page *dpage;
			struct folio *folio = page_folio(
				migrate_pfn_to_page(src_pfns[i]));
			unsigned int order = folio_order(folio);

			if (src_pfns[i] & MIGRATE_PFN_COMPOUND) {
				dpage = folio_page(
						folio_alloc(
						GFP_HIGHUSER_MOVABLE, order), 0);
			} else {
				/*
				 * _GFP_NOFAIL because the GPU is going away and there
				 * is nothing sensible we can do if we can't copy the
				 * data back.
				 */
				dpage = alloc_page(GFP_HIGHUSER | __GFP_NOFAIL);
			}

			dst_pfns[i] = migrate_pfn(page_to_pfn(dpage));
			nouveau_dmem_copy_folio(chunk->drm,
				page_folio(migrate_pfn_to_page(src_pfns[i])),
				page_folio(dpage),
				&dma_info[i]);
		}
	}

	nouveau_fence_new(&fence, chunk->drm->dmem->migrate.chan);
	migrate_device_pages(src_pfns, dst_pfns, npages);
	nouveau_dmem_fence_done(&fence);
	migrate_device_finalize(src_pfns, dst_pfns, npages);
	kvfree(src_pfns);
	kvfree(dst_pfns);
	for (i = 0; i < npages; i++)
		dma_unmap_page(chunk->drm->dev->dev, dma_info[i].dma_addr,
				dma_info[i].size, DMA_BIDIRECTIONAL);
	kvfree(dma_info);
}

void
nouveau_dmem_fini(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk, *tmp;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);

	list_for_each_entry_safe(chunk, tmp, &drm->dmem->chunks, list) {
		nouveau_dmem_evict_chunk(chunk);
		nouveau_bo_unpin_del(&chunk->bo);
		WARN_ON(chunk->callocated);
		list_del(&chunk->list);
		memunmap_pages(&chunk->pagemap);
		release_mem_region(chunk->pagemap.range.start,
				   range_len(&chunk->pagemap.range));
		kfree(chunk);
	}

	mutex_unlock(&drm->dmem->mutex);
}

static int
nvc0b5_migrate_copy(struct nouveau_drm *drm, u64 npages,
		    enum nouveau_aper dst_aper, u64 dst_addr,
		    enum nouveau_aper src_aper, u64 src_addr)
{
	struct nvif_push *push = &drm->dmem->migrate.chan->chan.push;
	u32 launch_dma = 0;
	int ret;

	ret = PUSH_WAIT(push, 13);
	if (ret)
		return ret;

	if (src_aper != NOUVEAU_APER_VIRT) {
		switch (src_aper) {
		case NOUVEAU_APER_VRAM:
			PUSH_IMMD(push, NVA0B5, SET_SRC_PHYS_MODE,
				  NVDEF(NVA0B5, SET_SRC_PHYS_MODE, TARGET, LOCAL_FB));
			break;
		case NOUVEAU_APER_HOST:
			PUSH_IMMD(push, NVA0B5, SET_SRC_PHYS_MODE,
				  NVDEF(NVA0B5, SET_SRC_PHYS_MODE, TARGET, COHERENT_SYSMEM));
			break;
		default:
			return -EINVAL;
		}

		launch_dma |= NVDEF(NVA0B5, LAUNCH_DMA, SRC_TYPE, PHYSICAL);
	}

	if (dst_aper != NOUVEAU_APER_VIRT) {
		switch (dst_aper) {
		case NOUVEAU_APER_VRAM:
			PUSH_IMMD(push, NVA0B5, SET_DST_PHYS_MODE,
				  NVDEF(NVA0B5, SET_DST_PHYS_MODE, TARGET, LOCAL_FB));
			break;
		case NOUVEAU_APER_HOST:
			PUSH_IMMD(push, NVA0B5, SET_DST_PHYS_MODE,
				  NVDEF(NVA0B5, SET_DST_PHYS_MODE, TARGET, COHERENT_SYSMEM));
			break;
		default:
			return -EINVAL;
		}

		launch_dma |= NVDEF(NVA0B5, LAUNCH_DMA, DST_TYPE, PHYSICAL);
	}

	PUSH_MTHD(push, NVA0B5, OFFSET_IN_UPPER,
		  NVVAL(NVA0B5, OFFSET_IN_UPPER, UPPER, upper_32_bits(src_addr)),

				OFFSET_IN_LOWER, lower_32_bits(src_addr),

				OFFSET_OUT_UPPER,
		  NVVAL(NVA0B5, OFFSET_OUT_UPPER, UPPER, upper_32_bits(dst_addr)),

				OFFSET_OUT_LOWER, lower_32_bits(dst_addr),
				PITCH_IN, PAGE_SIZE,
				PITCH_OUT, PAGE_SIZE,
				LINE_LENGTH_IN, PAGE_SIZE,
				LINE_COUNT, npages);

	PUSH_MTHD(push, NVA0B5, LAUNCH_DMA, launch_dma |
		  NVDEF(NVA0B5, LAUNCH_DMA, DATA_TRANSFER_TYPE, NON_PIPELINED) |
		  NVDEF(NVA0B5, LAUNCH_DMA, FLUSH_ENABLE, TRUE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, SEMAPHORE_TYPE, NONE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, INTERRUPT_TYPE, NONE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, SRC_MEMORY_LAYOUT, PITCH) |
		  NVDEF(NVA0B5, LAUNCH_DMA, DST_MEMORY_LAYOUT, PITCH) |
		  NVDEF(NVA0B5, LAUNCH_DMA, MULTI_LINE_ENABLE, TRUE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, REMAP_ENABLE, FALSE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, BYPASS_L2, USE_PTE_SETTING));
	return 0;
}

static int
nvc0b5_migrate_clear(struct nouveau_drm *drm, u32 length,
		     enum nouveau_aper dst_aper, u64 dst_addr)
{
	struct nvif_push *push = &drm->dmem->migrate.chan->chan.push;
	u32 launch_dma = 0;
	int ret;

	ret = PUSH_WAIT(push, 12);
	if (ret)
		return ret;

	switch (dst_aper) {
	case NOUVEAU_APER_VRAM:
		PUSH_IMMD(push, NVA0B5, SET_DST_PHYS_MODE,
			  NVDEF(NVA0B5, SET_DST_PHYS_MODE, TARGET, LOCAL_FB));
		break;
	case NOUVEAU_APER_HOST:
		PUSH_IMMD(push, NVA0B5, SET_DST_PHYS_MODE,
			  NVDEF(NVA0B5, SET_DST_PHYS_MODE, TARGET, COHERENT_SYSMEM));
		break;
	default:
		return -EINVAL;
	}

	launch_dma |= NVDEF(NVA0B5, LAUNCH_DMA, DST_TYPE, PHYSICAL);

	PUSH_MTHD(push, NVA0B5, SET_REMAP_CONST_A, 0,
				SET_REMAP_CONST_B, 0,

				SET_REMAP_COMPONENTS,
		  NVDEF(NVA0B5, SET_REMAP_COMPONENTS, DST_X, CONST_A) |
		  NVDEF(NVA0B5, SET_REMAP_COMPONENTS, DST_Y, CONST_B) |
		  NVDEF(NVA0B5, SET_REMAP_COMPONENTS, COMPONENT_SIZE, FOUR) |
		  NVDEF(NVA0B5, SET_REMAP_COMPONENTS, NUM_DST_COMPONENTS, TWO));

	PUSH_MTHD(push, NVA0B5, OFFSET_OUT_UPPER,
		  NVVAL(NVA0B5, OFFSET_OUT_UPPER, UPPER, upper_32_bits(dst_addr)),

				OFFSET_OUT_LOWER, lower_32_bits(dst_addr));

	PUSH_MTHD(push, NVA0B5, LINE_LENGTH_IN, length >> 3);

	PUSH_MTHD(push, NVA0B5, LAUNCH_DMA, launch_dma |
		  NVDEF(NVA0B5, LAUNCH_DMA, DATA_TRANSFER_TYPE, NON_PIPELINED) |
		  NVDEF(NVA0B5, LAUNCH_DMA, FLUSH_ENABLE, TRUE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, SEMAPHORE_TYPE, NONE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, INTERRUPT_TYPE, NONE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, SRC_MEMORY_LAYOUT, PITCH) |
		  NVDEF(NVA0B5, LAUNCH_DMA, DST_MEMORY_LAYOUT, PITCH) |
		  NVDEF(NVA0B5, LAUNCH_DMA, MULTI_LINE_ENABLE, FALSE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, REMAP_ENABLE, TRUE) |
		  NVDEF(NVA0B5, LAUNCH_DMA, BYPASS_L2, USE_PTE_SETTING));
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
		drm->dmem->migrate.clear_func = nvc0b5_migrate_clear;
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
	int ret;

	/* This only make sense on PASCAL or newer */
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_PASCAL)
		return;

	if (!(drm->dmem = kzalloc(sizeof(*drm->dmem), GFP_KERNEL)))
		return;

	drm->dmem->drm = drm;
	mutex_init(&drm->dmem->mutex);
	INIT_LIST_HEAD(&drm->dmem->chunks);
	mutex_init(&drm->dmem->mutex);
	spin_lock_init(&drm->dmem->lock);

	/* Initialize migration dma helpers before registering memory */
	ret = nouveau_dmem_migrate_init(drm);
	if (ret) {
		kfree(drm->dmem);
		drm->dmem = NULL;
	}
}

static unsigned long nouveau_dmem_migrate_copy_one(struct nouveau_drm *drm,
		struct nouveau_svmm *svmm, unsigned long src,
		struct nouveau_dmem_dma_info *dma_info, u64 *pfn)
{
	struct device *dev = drm->dev->dev;
	struct page *dpage, *spage;
	unsigned long paddr;
	bool is_large = false;
	unsigned long mpfn;

	spage = migrate_pfn_to_page(src);
	if (!(src & MIGRATE_PFN_MIGRATE))
		goto out;

	is_large = src & MIGRATE_PFN_COMPOUND;
	dpage = nouveau_dmem_page_alloc_locked(drm, is_large);
	if (!dpage)
		goto out;

	paddr = nouveau_dmem_page_addr(dpage);
	if (spage) {
		dma_info->dma_addr = dma_map_page(dev, spage, 0, page_size(spage),
					 DMA_BIDIRECTIONAL);
		dma_info->size = page_size(spage);
		if (dma_mapping_error(dev, dma_info->dma_addr))
			goto out_free_page;
		if (drm->dmem->migrate.copy_func(drm, folio_nr_pages(page_folio(spage)),
			NOUVEAU_APER_VRAM, paddr, NOUVEAU_APER_HOST,
			dma_info->dma_addr))
			goto out_dma_unmap;
	} else {
		dma_info->dma_addr = DMA_MAPPING_ERROR;
		if (drm->dmem->migrate.clear_func(drm, page_size(dpage),
			NOUVEAU_APER_VRAM, paddr))
			goto out_free_page;
	}

	dpage->zone_device_data = svmm;
	*pfn = NVIF_VMM_PFNMAP_V0_V | NVIF_VMM_PFNMAP_V0_VRAM |
		((paddr >> PAGE_SHIFT) << NVIF_VMM_PFNMAP_V0_ADDR_SHIFT);
	if (src & MIGRATE_PFN_WRITE)
		*pfn |= NVIF_VMM_PFNMAP_V0_W;
	mpfn = migrate_pfn(page_to_pfn(dpage));
	if (folio_order(page_folio(dpage)))
		mpfn |= MIGRATE_PFN_COMPOUND;
	return mpfn;

out_dma_unmap:
	dma_unmap_page(dev, dma_info->dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
out_free_page:
	nouveau_dmem_page_free_locked(drm, dpage);
out:
	*pfn = NVIF_VMM_PFNMAP_V0_NONE;
	return 0;
}

static void nouveau_dmem_migrate_chunk(struct nouveau_drm *drm,
		struct nouveau_svmm *svmm, struct migrate_vma *args,
		struct nouveau_dmem_dma_info *dma_info, u64 *pfns)
{
	struct nouveau_fence *fence;
	unsigned long addr = args->start, nr_dma = 0, i;
	unsigned long order = 0;

	for (i = 0; addr < args->end; ) {
		struct folio *folio;

		args->dst[i] = nouveau_dmem_migrate_copy_one(drm, svmm,
				args->src[i], dma_info + nr_dma, pfns + i);
		if (!args->dst[i]) {
			i++;
			addr += PAGE_SIZE;
			continue;
		}
		if (!dma_mapping_error(drm->dev->dev, dma_info[nr_dma].dma_addr))
			nr_dma++;
		folio = page_folio(migrate_pfn_to_page(args->dst[i]));
		order = folio_order(folio);
		i += 1 << order;
		addr += (1 << order) * PAGE_SIZE;
	}

	nouveau_fence_new(&fence, drm->dmem->migrate.chan);
	migrate_vma_pages(args);
	nouveau_dmem_fence_done(&fence);
	nouveau_pfns_map(svmm, args->vma->vm_mm, args->start, pfns, i, order);

	while (nr_dma--) {
		dma_unmap_page(drm->dev->dev, dma_info[nr_dma].dma_addr,
				dma_info[nr_dma].size, DMA_BIDIRECTIONAL);
	}
	migrate_vma_finalize(args);
}

int
nouveau_dmem_migrate_vma(struct nouveau_drm *drm,
			 struct nouveau_svmm *svmm,
			 struct vm_area_struct *vma,
			 unsigned long start,
			 unsigned long end)
{
	unsigned long npages = (end - start) >> PAGE_SHIFT;
	unsigned long max = npages;
	struct migrate_vma args = {
		.vma		= vma,
		.start		= start,
		.pgmap_owner	= drm->dev,
		.flags		= MIGRATE_VMA_SELECT_SYSTEM
				  | MIGRATE_VMA_SELECT_COMPOUND,
	};
	unsigned long i;
	u64 *pfns;
	int ret = -ENOMEM;
	struct nouveau_dmem_dma_info *dma_info;

	if (drm->dmem == NULL) {
		ret = -ENODEV;
		goto out;
	}

	if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE))
		if (max > (unsigned long)HPAGE_PMD_NR)
			max = (unsigned long)HPAGE_PMD_NR;

	args.src = kcalloc(max, sizeof(*args.src), GFP_KERNEL);
	if (!args.src)
		goto out;
	args.dst = kcalloc(max, sizeof(*args.dst), GFP_KERNEL);
	if (!args.dst)
		goto out_free_src;

	dma_info = kmalloc_array(max, sizeof(*dma_info), GFP_KERNEL);
	if (!dma_info)
		goto out_free_dst;

	pfns = nouveau_pfns_alloc(max);
	if (!pfns)
		goto out_free_dma;

	for (i = 0; i < npages; i += max) {
		if (args.start + (max << PAGE_SHIFT) > end)
			args.end = end;
		else
			args.end = args.start + (max << PAGE_SHIFT);

		ret = migrate_vma_setup(&args);
		if (ret)
			goto out_free_pfns;

		if (args.cpages)
			nouveau_dmem_migrate_chunk(drm, svmm, &args, dma_info,
						   pfns);
		args.start = args.end;
	}

	ret = 0;
out_free_pfns:
	nouveau_pfns_free(pfns);
out_free_dma:
	kfree(dma_info);
out_free_dst:
	kfree(args.dst);
out_free_src:
	kfree(args.src);
out:
	return ret;
}

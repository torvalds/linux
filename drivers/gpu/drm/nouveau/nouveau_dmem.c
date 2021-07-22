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
#include <nvif/if000c.h>

#include <nvhw/class/cla0b5.h>

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
	spinlock_t lock;
};

static struct nouveau_dmem_chunk *nouveau_page_to_chunk(struct page *page)
{
	return container_of(page->pgmap, struct nouveau_dmem_chunk, pagemap);
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

static void nouveau_dmem_page_free(struct page *page)
{
	struct nouveau_dmem_chunk *chunk = nouveau_page_to_chunk(page);
	struct nouveau_dmem *dmem = chunk->drm->dmem;

	spin_lock(&dmem->lock);
	page->zone_device_data = dmem->free_pages;
	dmem->free_pages = page;

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

static vm_fault_t nouveau_dmem_fault_copy_one(struct nouveau_drm *drm,
		struct vm_fault *vmf, struct migrate_vma *args,
		dma_addr_t *dma_addr)
{
	struct device *dev = drm->dev->dev;
	struct page *dpage, *spage;
	struct nouveau_svmm *svmm;

	spage = migrate_pfn_to_page(args->src[0]);
	if (!spage || !(args->src[0] & MIGRATE_PFN_MIGRATE))
		return 0;

	dpage = alloc_page_vma(GFP_HIGHUSER, vmf->vma, vmf->address);
	if (!dpage)
		return VM_FAULT_SIGBUS;
	lock_page(dpage);

	*dma_addr = dma_map_page(dev, dpage, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, *dma_addr))
		goto error_free_page;

	svmm = spage->zone_device_data;
	mutex_lock(&svmm->mutex);
	nouveau_svmm_invalidate(svmm, args->start, args->end);
	if (drm->dmem->migrate.copy_func(drm, 1, NOUVEAU_APER_HOST, *dma_addr,
			NOUVEAU_APER_VRAM, nouveau_dmem_page_addr(spage)))
		goto error_dma_unmap;
	mutex_unlock(&svmm->mutex);

	args->dst[0] = migrate_pfn(page_to_pfn(dpage)) | MIGRATE_PFN_LOCKED;
	return 0;

error_dma_unmap:
	mutex_unlock(&svmm->mutex);
	dma_unmap_page(dev, *dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
error_free_page:
	__free_page(dpage);
	return VM_FAULT_SIGBUS;
}

static vm_fault_t nouveau_dmem_migrate_to_ram(struct vm_fault *vmf)
{
	struct nouveau_drm *drm = page_to_drm(vmf->page);
	struct nouveau_dmem *dmem = drm->dmem;
	struct nouveau_fence *fence;
	unsigned long src = 0, dst = 0;
	dma_addr_t dma_addr = 0;
	vm_fault_t ret;
	struct migrate_vma args = {
		.vma		= vmf->vma,
		.start		= vmf->address,
		.end		= vmf->address + PAGE_SIZE,
		.src		= &src,
		.dst		= &dst,
		.pgmap_owner	= drm->dev,
		.flags		= MIGRATE_VMA_SELECT_DEVICE_PRIVATE,
	};

	/*
	 * FIXME what we really want is to find some heuristic to migrate more
	 * than just one page on CPU fault. When such fault happens it is very
	 * likely that more surrounding page will CPU fault too.
	 */
	if (migrate_vma_setup(&args) < 0)
		return VM_FAULT_SIGBUS;
	if (!args.cpages)
		return 0;

	ret = nouveau_dmem_fault_copy_one(drm, vmf, &args, &dma_addr);
	if (ret || dst == 0)
		goto done;

	nouveau_fence_new(dmem->migrate.chan, false, &fence);
	migrate_vma_pages(&args);
	nouveau_dmem_fence_done(&fence);
	dma_unmap_page(drm->dev->dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
done:
	migrate_vma_finalize(&args);
	return ret;
}

static const struct dev_pagemap_ops nouveau_dmem_pagemap_ops = {
	.page_free		= nouveau_dmem_page_free,
	.migrate_to_ram		= nouveau_dmem_migrate_to_ram,
};

static int
nouveau_dmem_chunk_alloc(struct nouveau_drm *drm, struct page **ppage)
{
	struct nouveau_dmem_chunk *chunk;
	struct resource *res;
	struct page *page;
	void *ptr;
	unsigned long i, pfn_first;
	int ret;

	chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
	if (chunk == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* Allocate unused physical address space for device private pages. */
	res = request_free_mem_region(&iomem_resource, DMEM_CHUNK_SIZE,
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

	ret = nouveau_bo_new(&drm->client, DMEM_CHUNK_SIZE, 0,
			     NOUVEAU_GEM_DOMAIN_VRAM, 0, 0, NULL, NULL,
			     &chunk->bo);
	if (ret)
		goto out_release;

	ret = nouveau_bo_pin(chunk->bo, NOUVEAU_GEM_DOMAIN_VRAM, false);
	if (ret)
		goto out_bo_free;

	ptr = memremap_pages(&chunk->pagemap, numa_node_id());
	if (IS_ERR(ptr)) {
		ret = PTR_ERR(ptr);
		goto out_bo_unpin;
	}

	mutex_lock(&drm->dmem->mutex);
	list_add(&chunk->list, &drm->dmem->chunks);
	mutex_unlock(&drm->dmem->mutex);

	pfn_first = chunk->pagemap.range.start >> PAGE_SHIFT;
	page = pfn_to_page(pfn_first);
	spin_lock(&drm->dmem->lock);
	for (i = 0; i < DMEM_CHUNK_NPAGES - 1; ++i, ++page) {
		page->zone_device_data = drm->dmem->free_pages;
		drm->dmem->free_pages = page;
	}
	*ppage = page;
	chunk->callocated++;
	spin_unlock(&drm->dmem->lock);

	NV_INFO(drm, "DMEM: registered %ldMB of device memory\n",
		DMEM_CHUNK_SIZE >> 20);

	return 0;

out_bo_unpin:
	nouveau_bo_unpin(chunk->bo);
out_bo_free:
	nouveau_bo_ref(NULL, &chunk->bo);
out_release:
	release_mem_region(chunk->pagemap.range.start, range_len(&chunk->pagemap.range));
out_free:
	kfree(chunk);
out:
	return ret;
}

static struct page *
nouveau_dmem_page_alloc_locked(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk;
	struct page *page = NULL;
	int ret;

	spin_lock(&drm->dmem->lock);
	if (drm->dmem->free_pages) {
		page = drm->dmem->free_pages;
		drm->dmem->free_pages = page->zone_device_data;
		chunk = nouveau_page_to_chunk(page);
		chunk->callocated++;
		spin_unlock(&drm->dmem->lock);
	} else {
		spin_unlock(&drm->dmem->lock);
		ret = nouveau_dmem_chunk_alloc(drm, &page);
		if (ret)
			return NULL;
	}

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

void
nouveau_dmem_fini(struct nouveau_drm *drm)
{
	struct nouveau_dmem_chunk *chunk, *tmp;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);

	list_for_each_entry_safe(chunk, tmp, &drm->dmem->chunks, list) {
		nouveau_bo_unpin(chunk->bo);
		nouveau_bo_ref(NULL, &chunk->bo);
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
	struct nvif_push *push = drm->dmem->migrate.chan->chan.push;
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
	struct nvif_push *push = drm->dmem->migrate.chan->chan.push;
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
		dma_addr_t *dma_addr, u64 *pfn)
{
	struct device *dev = drm->dev->dev;
	struct page *dpage, *spage;
	unsigned long paddr;

	spage = migrate_pfn_to_page(src);
	if (!(src & MIGRATE_PFN_MIGRATE))
		goto out;

	dpage = nouveau_dmem_page_alloc_locked(drm);
	if (!dpage)
		goto out;

	paddr = nouveau_dmem_page_addr(dpage);
	if (spage) {
		*dma_addr = dma_map_page(dev, spage, 0, page_size(spage),
					 DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, *dma_addr))
			goto out_free_page;
		if (drm->dmem->migrate.copy_func(drm, 1,
			NOUVEAU_APER_VRAM, paddr, NOUVEAU_APER_HOST, *dma_addr))
			goto out_dma_unmap;
	} else {
		*dma_addr = DMA_MAPPING_ERROR;
		if (drm->dmem->migrate.clear_func(drm, page_size(dpage),
			NOUVEAU_APER_VRAM, paddr))
			goto out_free_page;
	}

	dpage->zone_device_data = svmm;
	*pfn = NVIF_VMM_PFNMAP_V0_V | NVIF_VMM_PFNMAP_V0_VRAM |
		((paddr >> PAGE_SHIFT) << NVIF_VMM_PFNMAP_V0_ADDR_SHIFT);
	if (src & MIGRATE_PFN_WRITE)
		*pfn |= NVIF_VMM_PFNMAP_V0_W;
	return migrate_pfn(page_to_pfn(dpage)) | MIGRATE_PFN_LOCKED;

out_dma_unmap:
	dma_unmap_page(dev, *dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
out_free_page:
	nouveau_dmem_page_free_locked(drm, dpage);
out:
	*pfn = NVIF_VMM_PFNMAP_V0_NONE;
	return 0;
}

static void nouveau_dmem_migrate_chunk(struct nouveau_drm *drm,
		struct nouveau_svmm *svmm, struct migrate_vma *args,
		dma_addr_t *dma_addrs, u64 *pfns)
{
	struct nouveau_fence *fence;
	unsigned long addr = args->start, nr_dma = 0, i;

	for (i = 0; addr < args->end; i++) {
		args->dst[i] = nouveau_dmem_migrate_copy_one(drm, svmm,
				args->src[i], dma_addrs + nr_dma, pfns + i);
		if (!dma_mapping_error(drm->dev->dev, dma_addrs[nr_dma]))
			nr_dma++;
		addr += PAGE_SIZE;
	}

	nouveau_fence_new(drm->dmem->migrate.chan, false, &fence);
	migrate_vma_pages(args);
	nouveau_dmem_fence_done(&fence);
	nouveau_pfns_map(svmm, args->vma->vm_mm, args->start, pfns, i);

	while (nr_dma--) {
		dma_unmap_page(drm->dev->dev, dma_addrs[nr_dma], PAGE_SIZE,
				DMA_BIDIRECTIONAL);
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
	unsigned long max = min(SG_MAX_SINGLE_ALLOC, npages);
	dma_addr_t *dma_addrs;
	struct migrate_vma args = {
		.vma		= vma,
		.start		= start,
		.pgmap_owner	= drm->dev,
		.flags		= MIGRATE_VMA_SELECT_SYSTEM,
	};
	unsigned long i;
	u64 *pfns;
	int ret = -ENOMEM;

	if (drm->dmem == NULL)
		return -ENODEV;

	args.src = kcalloc(max, sizeof(*args.src), GFP_KERNEL);
	if (!args.src)
		goto out;
	args.dst = kcalloc(max, sizeof(*args.dst), GFP_KERNEL);
	if (!args.dst)
		goto out_free_src;

	dma_addrs = kmalloc_array(max, sizeof(*dma_addrs), GFP_KERNEL);
	if (!dma_addrs)
		goto out_free_dst;

	pfns = nouveau_pfns_alloc(max);
	if (!pfns)
		goto out_free_dma;

	for (i = 0; i < npages; i += max) {
		args.end = start + (max << PAGE_SHIFT);
		ret = migrate_vma_setup(&args);
		if (ret)
			goto out_free_pfns;

		if (args.cpages)
			nouveau_dmem_migrate_chunk(drm, svmm, &args, dma_addrs,
						   pfns);
		args.start = args.end;
	}

	ret = 0;
out_free_pfns:
	nouveau_pfns_free(pfns);
out_free_dma:
	kfree(dma_addrs);
out_free_dst:
	kfree(args.dst);
out_free_src:
	kfree(args.src);
out:
	return ret;
}

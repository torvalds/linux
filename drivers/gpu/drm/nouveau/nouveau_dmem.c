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
 * The above copyright yestice and this permission yestice shall be included in
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
#include "yesuveau_dmem.h"
#include "yesuveau_drv.h"
#include "yesuveau_chan.h"
#include "yesuveau_dma.h"
#include "yesuveau_mem.h"
#include "yesuveau_bo.h"

#include <nvif/class.h>
#include <nvif/object.h>
#include <nvif/if500b.h>
#include <nvif/if900b.h>

#include <linux/sched/mm.h>
#include <linux/hmm.h>

/*
 * FIXME: this is ugly right yesw we are using TTM to allocate vram and we pin
 * it in vram while in use. We likely want to overhaul memory management for
 * yesuveau to be more page like (yest necessarily with system page size but a
 * bigger page size) at lowest level and have some shim layer on top that would
 * provide the same functionality as TTM.
 */
#define DMEM_CHUNK_SIZE (2UL << 20)
#define DMEM_CHUNK_NPAGES (DMEM_CHUNK_SIZE >> PAGE_SHIFT)

enum yesuveau_aper {
	NOUVEAU_APER_VIRT,
	NOUVEAU_APER_VRAM,
	NOUVEAU_APER_HOST,
};

typedef int (*yesuveau_migrate_copy_t)(struct yesuveau_drm *drm, u64 npages,
				      enum yesuveau_aper, u64 dst_addr,
				      enum yesuveau_aper, u64 src_addr);

struct yesuveau_dmem_chunk {
	struct list_head list;
	struct yesuveau_bo *bo;
	struct yesuveau_drm *drm;
	unsigned long pfn_first;
	unsigned long callocated;
	unsigned long bitmap[BITS_TO_LONGS(DMEM_CHUNK_NPAGES)];
	spinlock_t lock;
};

struct yesuveau_dmem_migrate {
	yesuveau_migrate_copy_t copy_func;
	struct yesuveau_channel *chan;
};

struct yesuveau_dmem {
	struct yesuveau_drm *drm;
	struct dev_pagemap pagemap;
	struct yesuveau_dmem_migrate migrate;
	struct list_head chunk_free;
	struct list_head chunk_full;
	struct list_head chunk_empty;
	struct mutex mutex;
};

static inline struct yesuveau_dmem *page_to_dmem(struct page *page)
{
	return container_of(page->pgmap, struct yesuveau_dmem, pagemap);
}

static unsigned long yesuveau_dmem_page_addr(struct page *page)
{
	struct yesuveau_dmem_chunk *chunk = page->zone_device_data;
	unsigned long idx = page_to_pfn(page) - chunk->pfn_first;

	return (idx << PAGE_SHIFT) + chunk->bo->bo.offset;
}

static void yesuveau_dmem_page_free(struct page *page)
{
	struct yesuveau_dmem_chunk *chunk = page->zone_device_data;
	unsigned long idx = page_to_pfn(page) - chunk->pfn_first;

	/*
	 * FIXME:
	 *
	 * This is really a bad example, we need to overhaul yesuveau memory
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

static void yesuveau_dmem_fence_done(struct yesuveau_fence **fence)
{
	if (fence) {
		yesuveau_fence_wait(*fence, true, false);
		yesuveau_fence_unref(fence);
	} else {
		/*
		 * FIXME wait for channel to be IDLE before calling finalizing
		 * the hmem object.
		 */
	}
}

static vm_fault_t yesuveau_dmem_fault_copy_one(struct yesuveau_drm *drm,
		struct vm_fault *vmf, struct migrate_vma *args,
		dma_addr_t *dma_addr)
{
	struct device *dev = drm->dev->dev;
	struct page *dpage, *spage;

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

	if (drm->dmem->migrate.copy_func(drm, 1, NOUVEAU_APER_HOST, *dma_addr,
			NOUVEAU_APER_VRAM, yesuveau_dmem_page_addr(spage)))
		goto error_dma_unmap;

	args->dst[0] = migrate_pfn(page_to_pfn(dpage)) | MIGRATE_PFN_LOCKED;
	return 0;

error_dma_unmap:
	dma_unmap_page(dev, *dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
error_free_page:
	__free_page(dpage);
	return VM_FAULT_SIGBUS;
}

static vm_fault_t yesuveau_dmem_migrate_to_ram(struct vm_fault *vmf)
{
	struct yesuveau_dmem *dmem = page_to_dmem(vmf->page);
	struct yesuveau_drm *drm = dmem->drm;
	struct yesuveau_fence *fence;
	unsigned long src = 0, dst = 0;
	dma_addr_t dma_addr = 0;
	vm_fault_t ret;
	struct migrate_vma args = {
		.vma		= vmf->vma,
		.start		= vmf->address,
		.end		= vmf->address + PAGE_SIZE,
		.src		= &src,
		.dst		= &dst,
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

	ret = yesuveau_dmem_fault_copy_one(drm, vmf, &args, &dma_addr);
	if (ret || dst == 0)
		goto done;

	yesuveau_fence_new(dmem->migrate.chan, false, &fence);
	migrate_vma_pages(&args);
	yesuveau_dmem_fence_done(&fence);
	dma_unmap_page(drm->dev->dev, dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
done:
	migrate_vma_finalize(&args);
	return ret;
}

static const struct dev_pagemap_ops yesuveau_dmem_pagemap_ops = {
	.page_free		= yesuveau_dmem_page_free,
	.migrate_to_ram		= yesuveau_dmem_migrate_to_ram,
};

static int
yesuveau_dmem_chunk_alloc(struct yesuveau_drm *drm)
{
	struct yesuveau_dmem_chunk *chunk;
	int ret;

	if (drm->dmem == NULL)
		return -EINVAL;

	mutex_lock(&drm->dmem->mutex);
	chunk = list_first_entry_or_null(&drm->dmem->chunk_empty,
					 struct yesuveau_dmem_chunk,
					 list);
	if (chunk == NULL) {
		mutex_unlock(&drm->dmem->mutex);
		return -ENOMEM;
	}

	list_del(&chunk->list);
	mutex_unlock(&drm->dmem->mutex);

	ret = yesuveau_bo_new(&drm->client, DMEM_CHUNK_SIZE, 0,
			     TTM_PL_FLAG_VRAM, 0, 0, NULL, NULL,
			     &chunk->bo);
	if (ret)
		goto out;

	ret = yesuveau_bo_pin(chunk->bo, TTM_PL_FLAG_VRAM, false);
	if (ret) {
		yesuveau_bo_ref(NULL, &chunk->bo);
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

static struct yesuveau_dmem_chunk *
yesuveau_dmem_chunk_first_free_locked(struct yesuveau_drm *drm)
{
	struct yesuveau_dmem_chunk *chunk;

	chunk = list_first_entry_or_null(&drm->dmem->chunk_free,
					 struct yesuveau_dmem_chunk,
					 list);
	if (chunk)
		return chunk;

	chunk = list_first_entry_or_null(&drm->dmem->chunk_empty,
					 struct yesuveau_dmem_chunk,
					 list);
	if (chunk->bo)
		return chunk;

	return NULL;
}

static int
yesuveau_dmem_pages_alloc(struct yesuveau_drm *drm,
			 unsigned long npages,
			 unsigned long *pages)
{
	struct yesuveau_dmem_chunk *chunk;
	unsigned long c;
	int ret;

	memset(pages, 0xff, npages * sizeof(*pages));

	mutex_lock(&drm->dmem->mutex);
	for (c = 0; c < npages;) {
		unsigned long i;

		chunk = yesuveau_dmem_chunk_first_free_locked(drm);
		if (chunk == NULL) {
			mutex_unlock(&drm->dmem->mutex);
			ret = yesuveau_dmem_chunk_alloc(drm);
			if (ret) {
				if (c)
					return 0;
				return ret;
			}
			mutex_lock(&drm->dmem->mutex);
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
yesuveau_dmem_page_alloc_locked(struct yesuveau_drm *drm)
{
	unsigned long pfns[1];
	struct page *page;
	int ret;

	/* FIXME stop all the miss-match API ... */
	ret = yesuveau_dmem_pages_alloc(drm, 1, pfns);
	if (ret)
		return NULL;

	page = pfn_to_page(pfns[0]);
	get_page(page);
	lock_page(page);
	return page;
}

static void
yesuveau_dmem_page_free_locked(struct yesuveau_drm *drm, struct page *page)
{
	unlock_page(page);
	put_page(page);
}

void
yesuveau_dmem_resume(struct yesuveau_drm *drm)
{
	struct yesuveau_dmem_chunk *chunk;
	int ret;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);
	list_for_each_entry (chunk, &drm->dmem->chunk_free, list) {
		ret = yesuveau_bo_pin(chunk->bo, TTM_PL_FLAG_VRAM, false);
		/* FIXME handle pin failure */
		WARN_ON(ret);
	}
	list_for_each_entry (chunk, &drm->dmem->chunk_full, list) {
		ret = yesuveau_bo_pin(chunk->bo, TTM_PL_FLAG_VRAM, false);
		/* FIXME handle pin failure */
		WARN_ON(ret);
	}
	mutex_unlock(&drm->dmem->mutex);
}

void
yesuveau_dmem_suspend(struct yesuveau_drm *drm)
{
	struct yesuveau_dmem_chunk *chunk;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);
	list_for_each_entry (chunk, &drm->dmem->chunk_free, list) {
		yesuveau_bo_unpin(chunk->bo);
	}
	list_for_each_entry (chunk, &drm->dmem->chunk_full, list) {
		yesuveau_bo_unpin(chunk->bo);
	}
	mutex_unlock(&drm->dmem->mutex);
}

void
yesuveau_dmem_fini(struct yesuveau_drm *drm)
{
	struct yesuveau_dmem_chunk *chunk, *tmp;

	if (drm->dmem == NULL)
		return;

	mutex_lock(&drm->dmem->mutex);

	WARN_ON(!list_empty(&drm->dmem->chunk_free));
	WARN_ON(!list_empty(&drm->dmem->chunk_full));

	list_for_each_entry_safe (chunk, tmp, &drm->dmem->chunk_empty, list) {
		if (chunk->bo) {
			yesuveau_bo_unpin(chunk->bo);
			yesuveau_bo_ref(NULL, &chunk->bo);
		}
		list_del(&chunk->list);
		kfree(chunk);
	}

	mutex_unlock(&drm->dmem->mutex);
}

static int
nvc0b5_migrate_copy(struct yesuveau_drm *drm, u64 npages,
		    enum yesuveau_aper dst_aper, u64 dst_addr,
		    enum yesuveau_aper src_aper, u64 src_addr)
{
	struct yesuveau_channel *chan = drm->dmem->migrate.chan;
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
yesuveau_dmem_migrate_init(struct yesuveau_drm *drm)
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
yesuveau_dmem_init(struct yesuveau_drm *drm)
{
	struct device *device = drm->dev->dev;
	struct resource *res;
	unsigned long i, size, pfn_first;
	int ret;

	/* This only make sense on PASCAL or newer */
	if (drm->client.device.info.family < NV_DEVICE_INFO_V0_PASCAL)
		return;

	if (!(drm->dmem = kzalloc(sizeof(*drm->dmem), GFP_KERNEL)))
		return;

	drm->dmem->drm = drm;
	mutex_init(&drm->dmem->mutex);
	INIT_LIST_HEAD(&drm->dmem->chunk_free);
	INIT_LIST_HEAD(&drm->dmem->chunk_full);
	INIT_LIST_HEAD(&drm->dmem->chunk_empty);

	size = ALIGN(drm->client.device.info.ram_user, DMEM_CHUNK_SIZE);

	/* Initialize migration dma helpers before registering memory */
	ret = yesuveau_dmem_migrate_init(drm);
	if (ret)
		goto out_free;

	/*
	 * FIXME we need some kind of policy to decide how much VRAM we
	 * want to register with HMM. For yesw just register everything
	 * and latter if we want to do thing like over commit then we
	 * could revisit this.
	 */
	res = devm_request_free_mem_region(device, &iomem_resource, size);
	if (IS_ERR(res))
		goto out_free;
	drm->dmem->pagemap.type = MEMORY_DEVICE_PRIVATE;
	drm->dmem->pagemap.res = *res;
	drm->dmem->pagemap.ops = &yesuveau_dmem_pagemap_ops;
	if (IS_ERR(devm_memremap_pages(device, &drm->dmem->pagemap)))
		goto out_free;

	pfn_first = res->start >> PAGE_SHIFT;
	for (i = 0; i < (size / DMEM_CHUNK_SIZE); ++i) {
		struct yesuveau_dmem_chunk *chunk;
		struct page *page;
		unsigned long j;

		chunk = kzalloc(sizeof(*chunk), GFP_KERNEL);
		if (chunk == NULL) {
			yesuveau_dmem_fini(drm);
			return;
		}

		chunk->drm = drm;
		chunk->pfn_first = pfn_first + (i * DMEM_CHUNK_NPAGES);
		list_add_tail(&chunk->list, &drm->dmem->chunk_empty);

		page = pfn_to_page(chunk->pfn_first);
		for (j = 0; j < DMEM_CHUNK_NPAGES; ++j, ++page)
			page->zone_device_data = chunk;
	}

	NV_INFO(drm, "DMEM: registered %ldMB of device memory\n", size >> 20);
	return;
out_free:
	kfree(drm->dmem);
	drm->dmem = NULL;
}

static unsigned long yesuveau_dmem_migrate_copy_one(struct yesuveau_drm *drm,
		unsigned long src, dma_addr_t *dma_addr)
{
	struct device *dev = drm->dev->dev;
	struct page *dpage, *spage;

	spage = migrate_pfn_to_page(src);
	if (!spage || !(src & MIGRATE_PFN_MIGRATE))
		goto out;

	dpage = yesuveau_dmem_page_alloc_locked(drm);
	if (!dpage)
		return 0;

	*dma_addr = dma_map_page(dev, spage, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, *dma_addr))
		goto out_free_page;

	if (drm->dmem->migrate.copy_func(drm, 1, NOUVEAU_APER_VRAM,
			yesuveau_dmem_page_addr(dpage), NOUVEAU_APER_HOST,
			*dma_addr))
		goto out_dma_unmap;

	return migrate_pfn(page_to_pfn(dpage)) | MIGRATE_PFN_LOCKED;

out_dma_unmap:
	dma_unmap_page(dev, *dma_addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
out_free_page:
	yesuveau_dmem_page_free_locked(drm, dpage);
out:
	return 0;
}

static void yesuveau_dmem_migrate_chunk(struct yesuveau_drm *drm,
		struct migrate_vma *args, dma_addr_t *dma_addrs)
{
	struct yesuveau_fence *fence;
	unsigned long addr = args->start, nr_dma = 0, i;

	for (i = 0; addr < args->end; i++) {
		args->dst[i] = yesuveau_dmem_migrate_copy_one(drm, args->src[i],
				dma_addrs + nr_dma);
		if (args->dst[i])
			nr_dma++;
		addr += PAGE_SIZE;
	}

	yesuveau_fence_new(drm->dmem->migrate.chan, false, &fence);
	migrate_vma_pages(args);
	yesuveau_dmem_fence_done(&fence);

	while (nr_dma--) {
		dma_unmap_page(drm->dev->dev, dma_addrs[nr_dma], PAGE_SIZE,
				DMA_BIDIRECTIONAL);
	}
	/*
	 * FIXME optimization: update GPU page table to point to newly migrated
	 * memory.
	 */
	migrate_vma_finalize(args);
}

int
yesuveau_dmem_migrate_vma(struct yesuveau_drm *drm,
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
	};
	unsigned long c, i;
	int ret = -ENOMEM;

	args.src = kcalloc(max, sizeof(args.src), GFP_KERNEL);
	if (!args.src)
		goto out;
	args.dst = kcalloc(max, sizeof(args.dst), GFP_KERNEL);
	if (!args.dst)
		goto out_free_src;

	dma_addrs = kmalloc_array(max, sizeof(*dma_addrs), GFP_KERNEL);
	if (!dma_addrs)
		goto out_free_dst;

	for (i = 0; i < npages; i += c) {
		c = min(SG_MAX_SINGLE_ALLOC, npages);
		args.end = start + (c << PAGE_SHIFT);
		ret = migrate_vma_setup(&args);
		if (ret)
			goto out_free_dma;

		if (args.cpages)
			yesuveau_dmem_migrate_chunk(drm, &args, dma_addrs);
		args.start = args.end;
	}

	ret = 0;
out_free_dma:
	kfree(dma_addrs);
out_free_dst:
	kfree(args.dst);
out_free_src:
	kfree(args.src);
out:
	return ret;
}

static inline bool
yesuveau_dmem_page(struct yesuveau_drm *drm, struct page *page)
{
	return is_device_private_page(page) && drm->dmem == page_to_dmem(page);
}

void
yesuveau_dmem_convert_pfn(struct yesuveau_drm *drm,
			 struct hmm_range *range)
{
	unsigned long i, npages;

	npages = (range->end - range->start) >> PAGE_SHIFT;
	for (i = 0; i < npages; ++i) {
		struct page *page;
		uint64_t addr;

		page = hmm_device_entry_to_page(range, range->pfns[i]);
		if (page == NULL)
			continue;

		if (!(range->pfns[i] & range->flags[HMM_PFN_DEVICE_PRIVATE])) {
			continue;
		}

		if (!yesuveau_dmem_page(drm, page)) {
			WARN(1, "Some unkyeswn device memory !\n");
			range->pfns[i] = 0;
			continue;
		}

		addr = yesuveau_dmem_page_addr(page);
		range->pfns[i] &= ((1UL << range->pfn_shift) - 1);
		range->pfns[i] |= (addr >> PAGE_SHIFT) << range->pfn_shift;
	}
}

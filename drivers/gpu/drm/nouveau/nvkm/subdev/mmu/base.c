/*
 * Copyright 2010 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 */
#include "priv.h"
#include "vmm.h"

#include <subdev/bar.h>
#include <subdev/fb.h>

#include <nvif/if500d.h>
#include <nvif/if900d.h>

struct nvkm_mmu_ptp {
	struct nvkm_mmu_pt *pt;
	struct list_head head;
	u8  shift;
	u16 mask;
	u16 free;
};

static void
nvkm_mmu_ptp_put(struct nvkm_mmu *mmu, bool force, struct nvkm_mmu_pt *pt)
{
	const int slot = pt->base >> pt->ptp->shift;
	struct nvkm_mmu_ptp *ptp = pt->ptp;

	/* If there were no free slots in the parent allocation before,
	 * there will be now, so return PTP to the cache.
	 */
	if (!ptp->free)
		list_add(&ptp->head, &mmu->ptp.list);
	ptp->free |= BIT(slot);

	/* If there's no more sub-allocations, destroy PTP. */
	if (ptp->free == ptp->mask) {
		nvkm_mmu_ptc_put(mmu, force, &ptp->pt);
		list_del(&ptp->head);
		kfree(ptp);
	}

	kfree(pt);
}

struct nvkm_mmu_pt *
nvkm_mmu_ptp_get(struct nvkm_mmu *mmu, u32 size, bool zero)
{
	struct nvkm_mmu_pt *pt;
	struct nvkm_mmu_ptp *ptp;
	int slot;

	if (!(pt = kzalloc(sizeof(*pt), GFP_KERNEL)))
		return NULL;

	ptp = list_first_entry_or_null(&mmu->ptp.list, typeof(*ptp), head);
	if (!ptp) {
		/* Need to allocate a new parent to sub-allocate from. */
		if (!(ptp = kmalloc(sizeof(*ptp), GFP_KERNEL))) {
			kfree(pt);
			return NULL;
		}

		ptp->pt = nvkm_mmu_ptc_get(mmu, 0x1000, 0x1000, false);
		if (!ptp->pt) {
			kfree(ptp);
			kfree(pt);
			return NULL;
		}

		ptp->shift = order_base_2(size);
		slot = nvkm_memory_size(ptp->pt->memory) >> ptp->shift;
		ptp->mask = (1 << slot) - 1;
		ptp->free = ptp->mask;
		list_add(&ptp->head, &mmu->ptp.list);
	}
	pt->ptp = ptp;
	pt->sub = true;

	/* Sub-allocate from parent object, removing PTP from cache
	 * if there's no more free slots left.
	 */
	slot = __ffs(ptp->free);
	ptp->free &= ~BIT(slot);
	if (!ptp->free)
		list_del(&ptp->head);

	pt->memory = pt->ptp->pt->memory;
	pt->base = slot << ptp->shift;
	pt->addr = pt->ptp->pt->addr + pt->base;
	return pt;
}

struct nvkm_mmu_ptc {
	struct list_head head;
	struct list_head item;
	u32 size;
	u32 refs;
};

static inline struct nvkm_mmu_ptc *
nvkm_mmu_ptc_find(struct nvkm_mmu *mmu, u32 size)
{
	struct nvkm_mmu_ptc *ptc;

	list_for_each_entry(ptc, &mmu->ptc.list, head) {
		if (ptc->size == size)
			return ptc;
	}

	ptc = kmalloc(sizeof(*ptc), GFP_KERNEL);
	if (ptc) {
		INIT_LIST_HEAD(&ptc->item);
		ptc->size = size;
		ptc->refs = 0;
		list_add(&ptc->head, &mmu->ptc.list);
	}

	return ptc;
}

void
nvkm_mmu_ptc_put(struct nvkm_mmu *mmu, bool force, struct nvkm_mmu_pt **ppt)
{
	struct nvkm_mmu_pt *pt = *ppt;
	if (pt) {
		/* Handle sub-allocated page tables. */
		if (pt->sub) {
			mutex_lock(&mmu->ptp.mutex);
			nvkm_mmu_ptp_put(mmu, force, pt);
			mutex_unlock(&mmu->ptp.mutex);
			return;
		}

		/* Either cache or free the object. */
		mutex_lock(&mmu->ptc.mutex);
		if (pt->ptc->refs < 8 /* Heuristic. */ && !force) {
			list_add_tail(&pt->head, &pt->ptc->item);
			pt->ptc->refs++;
		} else {
			nvkm_memory_unref(&pt->memory);
			kfree(pt);
		}
		mutex_unlock(&mmu->ptc.mutex);
	}
}

struct nvkm_mmu_pt *
nvkm_mmu_ptc_get(struct nvkm_mmu *mmu, u32 size, u32 align, bool zero)
{
	struct nvkm_mmu_ptc *ptc;
	struct nvkm_mmu_pt *pt;
	int ret;

	/* Sub-allocated page table (ie. GP100 LPT). */
	if (align < 0x1000) {
		mutex_lock(&mmu->ptp.mutex);
		pt = nvkm_mmu_ptp_get(mmu, align, zero);
		mutex_unlock(&mmu->ptp.mutex);
		return pt;
	}

	/* Lookup cache for this page table size. */
	mutex_lock(&mmu->ptc.mutex);
	ptc = nvkm_mmu_ptc_find(mmu, size);
	if (!ptc) {
		mutex_unlock(&mmu->ptc.mutex);
		return NULL;
	}

	/* If there's a free PT in the cache, reuse it. */
	pt = list_first_entry_or_null(&ptc->item, typeof(*pt), head);
	if (pt) {
		if (zero)
			nvkm_fo64(pt->memory, 0, 0, size >> 3);
		list_del(&pt->head);
		ptc->refs--;
		mutex_unlock(&mmu->ptc.mutex);
		return pt;
	}
	mutex_unlock(&mmu->ptc.mutex);

	/* No such luck, we need to allocate. */
	if (!(pt = kmalloc(sizeof(*pt), GFP_KERNEL)))
		return NULL;
	pt->ptc = ptc;
	pt->sub = false;

	ret = nvkm_memory_new(mmu->subdev.device, NVKM_MEM_TARGET_INST,
			      size, align, zero, &pt->memory);
	if (ret) {
		kfree(pt);
		return NULL;
	}

	pt->base = 0;
	pt->addr = nvkm_memory_addr(pt->memory);
	return pt;
}

static void
nvkm_vm_map_(const struct nvkm_vmm_page *page, struct nvkm_vma *vma, u64 delta,
	     struct nvkm_mem *mem, nvkm_vmm_pte_func fn,
	     struct nvkm_vmm_map *map)
{
	union {
		struct nv50_vmm_map_v0 nv50;
		struct gf100_vmm_map_v0 gf100;
	} args;
	struct nvkm_vmm *vmm = vma->vm;
	void *argv = NULL;
	u32 argc = 0;
	int ret;

	map->memory = mem->memory;
	map->page = page;

	if (vmm->func->valid) {
		switch (vmm->mmu->subdev.device->card_type) {
		case NV_50:
			args.nv50.version = 0;
			args.nv50.ro = !(vma->access & NV_MEM_ACCESS_WO);
			args.nv50.priv = !!(vma->access & NV_MEM_ACCESS_SYS);
			args.nv50.kind = (mem->memtype & 0x07f);
			args.nv50.comp = (mem->memtype & 0x180) >> 7;
			argv = &args.nv50;
			argc = sizeof(args.nv50);
			break;
		case NV_C0:
		case NV_E0:
		case GM100:
		case GP100: {
			args.gf100.version = 0;
			args.gf100.vol = (nvkm_memory_target(map->memory) != NVKM_MEM_TARGET_VRAM);
			args.gf100.ro = !(vma->access & NV_MEM_ACCESS_WO);
			args.gf100.priv = !!(vma->access & NV_MEM_ACCESS_SYS);
			args.gf100.kind = (mem->memtype & 0x0ff);
			argv = &args.gf100;
			argc = sizeof(args.gf100);
		}
			break;
		default:
			break;
		}

		ret = vmm->func->valid(vmm, argv, argc, map);
		if (WARN_ON(ret))
			return;
	}

	mutex_lock(&vmm->mutex);
	nvkm_vmm_ptes_map(vmm, page, vma->node->addr + delta,
				     vma->node->size, map, fn);
	mutex_unlock(&vmm->mutex);

	nvkm_memory_tags_put(vma->node->memory, vmm->mmu->subdev.device, &vma->node->tags);
	nvkm_memory_unref(&vma->node->memory);
	vma->node->memory = nvkm_memory_ref(map->memory);
	vma->node->tags = map->tags;
}

void
nvkm_mmu_ptc_dump(struct nvkm_mmu *mmu)
{
	struct nvkm_mmu_ptc *ptc;
	list_for_each_entry(ptc, &mmu->ptc.list, head) {
		struct nvkm_mmu_pt *pt, *tt;
		list_for_each_entry_safe(pt, tt, &ptc->item, head) {
			nvkm_memory_unref(&pt->memory);
			list_del(&pt->head);
			kfree(pt);
		}
	}
}

static void
nvkm_mmu_ptc_fini(struct nvkm_mmu *mmu)
{
	struct nvkm_mmu_ptc *ptc, *ptct;

	list_for_each_entry_safe(ptc, ptct, &mmu->ptc.list, head) {
		WARN_ON(!list_empty(&ptc->item));
		list_del(&ptc->head);
		kfree(ptc);
	}
}

static void
nvkm_mmu_ptc_init(struct nvkm_mmu *mmu)
{
	mutex_init(&mmu->ptc.mutex);
	INIT_LIST_HEAD(&mmu->ptc.list);
	mutex_init(&mmu->ptp.mutex);
	INIT_LIST_HEAD(&mmu->ptp.list);
}

void
nvkm_vm_map_at(struct nvkm_vma *vma, u64 delta, struct nvkm_mem *node)
{
	const struct nvkm_vmm_page *page = &vma->vm->func->page[vma->node->page];
	if (page->desc->func->unmap) {
		struct nvkm_vmm_map map = { .mem = node->mem };
		nvkm_vm_map_(page, vma, delta, node, page->desc->func->mem, &map);
		return;
	}
}

static void
nvkm_vm_map_sg_table(struct nvkm_vma *vma, u64 delta, u64 length,
		     struct nvkm_mem *mem)
{
	const struct nvkm_vmm_page *page = &vma->vm->func->page[vma->node->page];
	if (page->desc->func->unmap) {
		struct nvkm_vmm_map map = { .sgl = mem->sg->sgl };
		nvkm_vm_map_(page, vma, delta, mem, page->desc->func->sgl, &map);
		return;
	}
}

static void
nvkm_vm_map_sg(struct nvkm_vma *vma, u64 delta, u64 length,
	       struct nvkm_mem *mem)
{
	const struct nvkm_vmm_page *page = &vma->vm->func->page[vma->node->page];
	if (page->desc->func->unmap) {
		struct nvkm_vmm_map map = { .dma = mem->pages };
		nvkm_vm_map_(page, vma, delta, mem, page->desc->func->dma, &map);
		return;
	}
}

void
nvkm_vm_map(struct nvkm_vma *vma, struct nvkm_mem *node)
{
	if (node->sg)
		nvkm_vm_map_sg_table(vma, 0, node->size << 12, node);
	else
	if (node->pages)
		nvkm_vm_map_sg(vma, 0, node->size << 12, node);
	else
		nvkm_vm_map_at(vma, 0, node);
}

void
nvkm_vm_unmap(struct nvkm_vma *vma)
{
	nvkm_vmm_unmap(vma->vm, vma->node);
}

int
nvkm_vm_get(struct nvkm_vm *vm, u64 size, u32 page_shift, u32 access,
	    struct nvkm_vma *vma)
{
	int ret;

	mutex_lock(&vm->mutex);
	ret = nvkm_vmm_get_locked(vm, true, false, false, page_shift, 0,
				  size, &vma->node);
	mutex_unlock(&vm->mutex);
	if (ret)
		return ret;

	vma->memory = NULL;
	vma->tags = NULL;
	vma->vm = NULL;
	nvkm_vm_ref(vm, &vma->vm, NULL);
	vma->offset = vma->addr = vma->node->addr;
	vma->access = access;
	return 0;
}

void
nvkm_vm_put(struct nvkm_vma *vma)
{
	nvkm_vmm_put(vma->vm, &vma->node);
	nvkm_vm_ref(NULL, &vma->vm, NULL);
}

int
nvkm_vm_boot(struct nvkm_vm *vm, u64 size)
{
	return nvkm_vmm_boot(vm);
}

int
nvkm_vm_new(struct nvkm_device *device, u64 offset, u64 length, u64 mm_offset,
	    struct lock_class_key *key, struct nvkm_vm **pvm)
{
	struct nvkm_mmu *mmu = device->mmu;

	*pvm = NULL;
	if (mmu->func->vmm.ctor) {
		int ret = mmu->func->vmm.ctor(mmu, mm_offset,
					      offset + length - mm_offset,
					      NULL, 0, key, "legacy", pvm);
		if (ret) {
			nvkm_vm_ref(NULL, pvm, NULL);
			return ret;
		}

		return ret;
	}

	return -EINVAL;
}

int
nvkm_vm_ref(struct nvkm_vm *ref, struct nvkm_vm **ptr, struct nvkm_memory *inst)
{
	if (ref) {
		if (inst) {
			int ret = nvkm_vmm_join(ref, inst);
			if (ret)
				return ret;
		}

		nvkm_vmm_ref(ref);
	}

	if (*ptr) {
		nvkm_vmm_part(*ptr, inst);
		nvkm_vmm_unref(ptr);
	}

	*ptr = ref;
	return 0;
}

static void
nvkm_mmu_type(struct nvkm_mmu *mmu, int heap, u8 type)
{
	if (heap >= 0 && !WARN_ON(mmu->type_nr == ARRAY_SIZE(mmu->type))) {
		mmu->type[mmu->type_nr].type = type | mmu->heap[heap].type;
		mmu->type[mmu->type_nr].heap = heap;
		mmu->type_nr++;
	}
}

static int
nvkm_mmu_heap(struct nvkm_mmu *mmu, u8 type, u64 size)
{
	if (size) {
		if (!WARN_ON(mmu->heap_nr == ARRAY_SIZE(mmu->heap))) {
			mmu->heap[mmu->heap_nr].type = type;
			mmu->heap[mmu->heap_nr].size = size;
			return mmu->heap_nr++;
		}
	}
	return -EINVAL;
}

static void
nvkm_mmu_host(struct nvkm_mmu *mmu)
{
	struct nvkm_device *device = mmu->subdev.device;
	u8 type = NVKM_MEM_KIND * !!mmu->func->kind_sys;
	int heap;

	/* Non-mappable system memory. */
	heap = nvkm_mmu_heap(mmu, NVKM_MEM_HOST, ~0ULL);
	nvkm_mmu_type(mmu, heap, type);

	/* Non-coherent, cached, system memory.
	 *
	 * Block-linear mappings of system memory must be done through
	 * BAR1, and cannot be supported on systems where we're unable
	 * to map BAR1 with write-combining.
	 */
	type |= NVKM_MEM_MAPPABLE;
	if (!device->bar || device->bar->iomap_uncached)
		nvkm_mmu_type(mmu, heap, type & ~NVKM_MEM_KIND);
	else
		nvkm_mmu_type(mmu, heap, type);

	/* Coherent, cached, system memory.
	 *
	 * Unsupported on systems that aren't able to support snooped
	 * mappings, and also for block-linear mappings which must be
	 * done through BAR1.
	 */
	type |= NVKM_MEM_COHERENT;
	if (device->func->cpu_coherent)
		nvkm_mmu_type(mmu, heap, type & ~NVKM_MEM_KIND);

	/* Uncached system memory. */
	nvkm_mmu_type(mmu, heap, type |= NVKM_MEM_UNCACHED);
}

static void
nvkm_mmu_vram(struct nvkm_mmu *mmu)
{
	struct nvkm_device *device = mmu->subdev.device;
	struct nvkm_mm *mm = &device->fb->ram->vram;
	const u32 sizeN = nvkm_mm_heap_size(mm, NVKM_RAM_MM_NORMAL);
	const u32 sizeU = nvkm_mm_heap_size(mm, NVKM_RAM_MM_NOMAP);
	const u32 sizeM = nvkm_mm_heap_size(mm, NVKM_RAM_MM_MIXED);
	u8 type = NVKM_MEM_KIND * !!mmu->func->kind;
	u8 heap = NVKM_MEM_VRAM;
	int heapM, heapN, heapU;

	/* Mixed-memory doesn't support compression or display. */
	heapM = nvkm_mmu_heap(mmu, heap, sizeM << NVKM_RAM_MM_SHIFT);

	heap |= NVKM_MEM_COMP;
	heap |= NVKM_MEM_DISP;
	heapN = nvkm_mmu_heap(mmu, heap, sizeN << NVKM_RAM_MM_SHIFT);
	heapU = nvkm_mmu_heap(mmu, heap, sizeU << NVKM_RAM_MM_SHIFT);

	/* Add non-mappable VRAM types first so that they're preferred
	 * over anything else.  Mixed-memory will be slower than other
	 * heaps, it's prioritised last.
	 */
	nvkm_mmu_type(mmu, heapU, type);
	nvkm_mmu_type(mmu, heapN, type);
	nvkm_mmu_type(mmu, heapM, type);

	/* Add host memory types next, under the assumption that users
	 * wanting mappable memory want to use them as staging buffers
	 * or the like.
	 */
	nvkm_mmu_host(mmu);

	/* Mappable VRAM types go last, as they're basically the worst
	 * possible type to ask for unless there's no other choice.
	 */
	if (device->bar) {
		/* Write-combined BAR1 access. */
		type |= NVKM_MEM_MAPPABLE;
		if (!device->bar->iomap_uncached) {
			nvkm_mmu_type(mmu, heapN, type);
			nvkm_mmu_type(mmu, heapM, type);
		}

		/* Uncached BAR1 access. */
		type |= NVKM_MEM_COHERENT;
		type |= NVKM_MEM_UNCACHED;
		nvkm_mmu_type(mmu, heapN, type);
		nvkm_mmu_type(mmu, heapM, type);
	}
}

static int
nvkm_mmu_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_mmu *mmu = nvkm_mmu(subdev);

	/* Determine available memory types. */
	if (mmu->subdev.device->fb && mmu->subdev.device->fb->ram)
		nvkm_mmu_vram(mmu);
	else
		nvkm_mmu_host(mmu);

	if (mmu->func->vmm.global) {
		int ret = nvkm_vmm_new(subdev->device, 0, 0, NULL, 0, NULL,
				       "gart", &mmu->vmm);
		if (ret)
			return ret;
	}

	return 0;
}

static int
nvkm_mmu_init(struct nvkm_subdev *subdev)
{
	struct nvkm_mmu *mmu = nvkm_mmu(subdev);
	if (mmu->func->init)
		mmu->func->init(mmu);
	return 0;
}

static void *
nvkm_mmu_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_mmu *mmu = nvkm_mmu(subdev);

	nvkm_vmm_unref(&mmu->vmm);

	nvkm_mmu_ptc_fini(mmu);
	return mmu;
}

static const struct nvkm_subdev_func
nvkm_mmu = {
	.dtor = nvkm_mmu_dtor,
	.oneinit = nvkm_mmu_oneinit,
	.init = nvkm_mmu_init,
};

void
nvkm_mmu_ctor(const struct nvkm_mmu_func *func, struct nvkm_device *device,
	      int index, struct nvkm_mmu *mmu)
{
	nvkm_subdev_ctor(&nvkm_mmu, device, index, &mmu->subdev);
	mmu->func = func;
	mmu->limit = func->limit;
	mmu->dma_bits = func->dma_bits;
	mmu->lpg_shift = func->lpg_shift;
	nvkm_mmu_ptc_init(mmu);
}

int
nvkm_mmu_new_(const struct nvkm_mmu_func *func, struct nvkm_device *device,
	      int index, struct nvkm_mmu **pmmu)
{
	if (!(*pmmu = kzalloc(sizeof(**pmmu), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_mmu_ctor(func, device, index, *pmmu);
	return 0;
}

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

#include <core/gpuobj.h>
#include <subdev/fb.h>

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
	struct nvkm_vmm *vmm = vma->vm;
	void *argv = NULL;
	u32 argc = 0;
	int ret;

	map->memory = mem->memory;
	map->page = page;

	if (vmm->func->valid) {
		ret = vmm->func->valid(vmm, argv, argc, map);
		if (WARN_ON(ret))
			return;
	}

	mutex_lock(&vmm->mutex);
	nvkm_vmm_ptes_map(vmm, page, ((u64)vma->node->offset << 12) + delta,
				      (u64)vma->node->length << 12, map, fn);
	mutex_unlock(&vmm->mutex);

	nvkm_memory_tags_put(vma->memory, vmm->mmu->subdev.device, &vma->tags);
	nvkm_memory_unref(&vma->memory);
	vma->memory = nvkm_memory_ref(map->memory);
	vma->tags = map->tags;
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
	const struct nvkm_vmm_page *page = vma->vm->func->page;
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_mm_node *r = node->mem;
	int big = vma->node->type != mmu->func->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 pde  = (offset >> mmu->func->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->func->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->func->pgt_bits - bits);
	u32 end, len;

	if (page->desc->func->unmap) {
		struct nvkm_vmm_map map = { .mem = node->mem };
		while (page->shift != vma->node->type)
			page++;
		nvkm_vm_map_(page, vma, delta, node, page->desc->func->mem, &map);
		return;
	}

	delta = 0;
	while (r) {
		u64 phys = (u64)r->offset << 12;
		u32 num  = r->length >> bits;

		while (num) {
			struct nvkm_memory *pgt = vm->pgt[pde].mem[big];

			end = (pte + num);
			if (unlikely(end >= max))
				end = max;
			len = end - pte;

			mmu->func->map(vma, pgt, node, pte, len, phys, delta);

			num -= len;
			pte += len;
			if (unlikely(end >= max)) {
				phys += len << (bits + 12);
				pde++;
				pte = 0;
			}

			delta += (u64)len << vma->node->type;
		}
		r = r->next;
	}

	mmu->func->flush(vm);
}

static void
nvkm_vm_map_sg_table(struct nvkm_vma *vma, u64 delta, u64 length,
		     struct nvkm_mem *mem)
{
	const struct nvkm_vmm_page *page = vma->vm->func->page;
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	int big = vma->node->type != mmu->func->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> mmu->func->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->func->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->func->pgt_bits - bits);
	unsigned m, sglen;
	u32 end, len;
	int i;
	struct scatterlist *sg;

	if (page->desc->func->unmap) {
		struct nvkm_vmm_map map = { .sgl = mem->sg->sgl };
		while (page->shift != vma->node->type)
			page++;
		nvkm_vm_map_(page, vma, delta, mem, page->desc->func->sgl, &map);
		return;
	}

	for_each_sg(mem->sg->sgl, sg, mem->sg->nents, i) {
		struct nvkm_memory *pgt = vm->pgt[pde].mem[big];
		sglen = sg_dma_len(sg) >> PAGE_SHIFT;

		end = pte + sglen;
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		for (m = 0; m < len; m++) {
			dma_addr_t addr = sg_dma_address(sg) + (m << PAGE_SHIFT);

			mmu->func->map_sg(vma, pgt, mem, pte, 1, &addr);
			num--;
			pte++;

			if (num == 0)
				goto finish;
		}
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
		if (m < sglen) {
			for (; m < sglen; m++) {
				dma_addr_t addr = sg_dma_address(sg) + (m << PAGE_SHIFT);

				mmu->func->map_sg(vma, pgt, mem, pte, 1, &addr);
				num--;
				pte++;
				if (num == 0)
					goto finish;
			}
		}

	}
finish:
	mmu->func->flush(vm);
}

static void
nvkm_vm_map_sg(struct nvkm_vma *vma, u64 delta, u64 length,
	       struct nvkm_mem *mem)
{
	const struct nvkm_vmm_page *page = vma->vm->func->page;
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	dma_addr_t *list = mem->pages;
	int big = vma->node->type != mmu->func->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> mmu->func->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->func->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->func->pgt_bits - bits);
	u32 end, len;

	if (page->desc->func->unmap) {
		struct nvkm_vmm_map map = { .dma = mem->pages };
		while (page->shift != vma->node->type)
			page++;
		nvkm_vm_map_(page, vma, delta, mem, page->desc->func->dma, &map);
		return;
	}

	while (num) {
		struct nvkm_memory *pgt = vm->pgt[pde].mem[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		mmu->func->map_sg(vma, pgt, mem, pte, len, list);

		num  -= len;
		pte  += len;
		list += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	mmu->func->flush(vm);
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
nvkm_vm_unmap_at(struct nvkm_vma *vma, u64 delta, u64 length)
{
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	int big = vma->node->type != mmu->func->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> mmu->func->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->func->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->func->pgt_bits - bits);
	u32 end, len;

	if (vm->func->page->desc->func->unmap) {
		const struct nvkm_vmm_page *page = vm->func->page;
		while (page->shift != vma->node->type)
			page++;
		mutex_lock(&vm->mutex);
		nvkm_vmm_ptes_unmap(vm, page, (vma->node->offset << 12) + delta,
					       vma->node->length << 12, false);
		mutex_unlock(&vm->mutex);
		return;
	}

	while (num) {
		struct nvkm_memory *pgt = vm->pgt[pde].mem[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		mmu->func->unmap(vma, pgt, pte, len);

		num -= len;
		pte += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	mmu->func->flush(vm);
}

void
nvkm_vm_unmap(struct nvkm_vma *vma)
{
	nvkm_vm_unmap_at(vma, 0, (u64)vma->node->length << 12);

	nvkm_memory_tags_put(vma->memory, vma->vm->mmu->subdev.device, &vma->tags);
	nvkm_memory_unref(&vma->memory);
}

static void
nvkm_vm_unmap_pgt(struct nvkm_vm *vm, int big, u32 fpde, u32 lpde)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_vm_pgt *vpgt;
	struct nvkm_memory *pgt;
	u32 pde;

	for (pde = fpde; pde <= lpde; pde++) {
		vpgt = &vm->pgt[pde - vm->fpde];
		if (--vpgt->refcount[big])
			continue;

		pgt = vpgt->mem[big];
		vpgt->mem[big] = NULL;

		if (mmu->func->map_pgt)
			mmu->func->map_pgt(vm, pde, vpgt->mem);

		mmu->func->flush(vm);

		nvkm_memory_unref(&pgt);
	}
}

static int
nvkm_vm_map_pgt(struct nvkm_vm *vm, u32 pde, u32 type)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
	int big = (type != mmu->func->spg_shift);
	u32 pgt_size;
	int ret;

	pgt_size  = (1 << (mmu->func->pgt_bits + 12)) >> type;
	pgt_size *= 8;

	ret = nvkm_memory_new(mmu->subdev.device, NVKM_MEM_TARGET_INST,
			      pgt_size, 0x1000, true, &vpgt->mem[big]);
	if (unlikely(ret))
		return ret;

	if (mmu->func->map_pgt)
		mmu->func->map_pgt(vm, pde, vpgt->mem);

	vpgt->refcount[big]++;
	return 0;
}

int
nvkm_vm_get(struct nvkm_vm *vm, u64 size, u32 page_shift, u32 access,
	    struct nvkm_vma *vma)
{
	struct nvkm_mmu *mmu = vm->mmu;
	u32 align = (1 << page_shift) >> 12;
	u32 msize = size >> 12;
	u32 fpde, lpde, pde;
	int ret;

	mutex_lock(&vm->mutex);
	ret = nvkm_mm_head(&vm->mm, 0, page_shift, msize, msize, align,
			   &vma->node);
	if (unlikely(ret != 0)) {
		mutex_unlock(&vm->mutex);
		return ret;
	}

	if (vm->func->page->desc->func->unmap) {
		const struct nvkm_vmm_page *page = vm->func->page;
		while (page->shift != page_shift)
			page++;

		ret = nvkm_vmm_ptes_get(vm, page, vma->node->offset << 12,
						  vma->node->length << 12);
		if (ret) {
			nvkm_mm_free(&vm->mm, &vma->node);
			mutex_unlock(&vm->mutex);
			return ret;
		}

		goto done;
	}

	fpde = (vma->node->offset >> mmu->func->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> mmu->func->pgt_bits;

	for (pde = fpde; pde <= lpde; pde++) {
		struct nvkm_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
		int big = (vma->node->type != mmu->func->spg_shift);

		if (likely(vpgt->refcount[big])) {
			vpgt->refcount[big]++;
			continue;
		}

		ret = nvkm_vm_map_pgt(vm, pde, vma->node->type);
		if (ret) {
			if (pde != fpde)
				nvkm_vm_unmap_pgt(vm, big, fpde, pde - 1);
			nvkm_mm_free(&vm->mm, &vma->node);
			mutex_unlock(&vm->mutex);
			return ret;
		}
	}
done:
	mutex_unlock(&vm->mutex);

	vma->memory = NULL;
	vma->tags = NULL;
	vma->vm = NULL;
	nvkm_vm_ref(vm, &vma->vm, NULL);
	vma->offset = (u64)vma->node->offset << 12;
	vma->access = access;
	return 0;
}

void
nvkm_vm_put(struct nvkm_vma *vma)
{
	struct nvkm_mmu *mmu;
	struct nvkm_vm *vm;
	u32 fpde, lpde;

	if (unlikely(vma->node == NULL))
		return;
	vm = vma->vm;
	mmu = vm->mmu;

	nvkm_memory_tags_put(vma->memory, mmu->subdev.device, &vma->tags);
	nvkm_memory_unref(&vma->memory);

	fpde = (vma->node->offset >> mmu->func->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> mmu->func->pgt_bits;

	mutex_lock(&vm->mutex);
	if (vm->func->page->desc->func->unmap) {
		const struct nvkm_vmm_page *page = vm->func->page;
		while (page->shift != vma->node->type)
			page++;

		nvkm_vmm_ptes_put(vm, page, vma->node->offset << 12,
					    vma->node->length << 12);
		goto done;
	}

	nvkm_vm_unmap_pgt(vm, vma->node->type != mmu->func->spg_shift, fpde, lpde);
done:
	nvkm_mm_free(&vm->mm, &vma->node);
	mutex_unlock(&vm->mutex);

	nvkm_vm_ref(NULL, &vma->vm, NULL);
}

int
nvkm_vm_boot(struct nvkm_vm *vm, u64 size)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_memory *pgt;
	int ret;

	if (vm->func->page->desc->func->unmap)
		return nvkm_vmm_boot(vm);

	ret = nvkm_memory_new(mmu->subdev.device, NVKM_MEM_TARGET_INST,
			      (size >> mmu->func->spg_shift) * 8, 0x1000, true, &pgt);
	if (ret == 0) {
		vm->pgt[0].refcount[0] = 1;
		vm->pgt[0].mem[0] = pgt;
		nvkm_memory_boot(pgt, vm);
		vm->bootstrapped = true;
	}

	return ret;
}

static int
nvkm_vm_legacy(struct nvkm_mmu *mmu, u64 offset, u64 length, u64 mm_offset,
	       u32 block, struct nvkm_vm *vm)
{
	u64 mm_length = (offset + length) - mm_offset;
	int ret;

	kref_init(&vm->refcount);
	vm->fpde = offset >> (mmu->func->pgt_bits + 12);
	vm->lpde = (offset + length - 1) >> (mmu->func->pgt_bits + 12);

	vm->pgt  = vzalloc((vm->lpde - vm->fpde + 1) * sizeof(*vm->pgt));
	if (!vm->pgt) {
		kfree(vm);
		return -ENOMEM;
	}

	if (block > length)
		block = length;

	ret = nvkm_mm_init(&vm->mm, 0, mm_offset >> 12, mm_length >> 12,
			   block >> 12);
	if (ret) {
		vfree(vm->pgt);
		return ret;
	}

	return 0;
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

		ret = nvkm_vm_legacy(mmu, offset, length, mm_offset,
				     (*pvm)->func->page_block ?
				     (*pvm)->func->page_block : 4096, *pvm);
		if (ret)
			nvkm_vm_ref(NULL, pvm, NULL);

		return ret;
	}

	return -EINVAL;
}

static void
nvkm_vm_del(struct kref *kref)
{
	struct nvkm_vm *vm = container_of(kref, typeof(*vm), refcount);

	nvkm_mm_fini(&vm->mm);
	vfree(vm->pgt);
	if (vm->func)
		nvkm_vmm_dtor(vm);
	kfree(vm);
}

int
nvkm_vm_ref(struct nvkm_vm *ref, struct nvkm_vm **ptr, struct nvkm_memory *inst)
{
	if (ref) {
		if (ref->func->join && inst) {
			int ret = ref->func->join(ref, inst), i;
			if (ret)
				return ret;

			if (!ref->func->page->desc->func->unmap && ref->mmu->func->map_pgt) {
				for (i = ref->fpde; i <= ref->lpde; i++)
					ref->mmu->func->map_pgt(ref, i, ref->pgt[i - ref->fpde].mem);
			}
		}

		kref_get(&ref->refcount);
	}

	if (*ptr) {
		if ((*ptr)->func->part && inst)
			(*ptr)->func->part(*ptr, inst);
		if ((*ptr)->bootstrapped && inst) {
			if (!(*ptr)->func->page->desc->func->unmap) {
				nvkm_memory_unref(&(*ptr)->pgt[0].mem[0]);
				(*ptr)->bootstrapped = false;
			}
		}
		kref_put(&(*ptr)->refcount, nvkm_vm_del);
	}

	*ptr = ref;
	return 0;
}

static int
nvkm_mmu_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_mmu *mmu = nvkm_mmu(subdev);

	if (mmu->func->vmm.global) {
		int ret = nvkm_vm_new(subdev->device, 0, mmu->limit, 0,
				      NULL, &mmu->vmm);
		if (ret)
			return ret;
	}

	if (mmu->func->oneinit)
		return mmu->func->oneinit(mmu);

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

	nvkm_vm_ref(NULL, &mmu->vmm, NULL);

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

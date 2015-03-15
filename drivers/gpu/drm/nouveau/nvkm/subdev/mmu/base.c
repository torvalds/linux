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
#include <subdev/mmu.h>
#include <subdev/fb.h>

#include <core/gpuobj.h>

void
nvkm_vm_map_at(struct nvkm_vma *vma, u64 delta, struct nvkm_mem *node)
{
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_mm_node *r;
	int big = vma->node->type != mmu->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 pde  = (offset >> mmu->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->pgt_bits - bits);
	u32 end, len;

	delta = 0;
	list_for_each_entry(r, &node->regions, rl_entry) {
		u64 phys = (u64)r->offset << 12;
		u32 num  = r->length >> bits;

		while (num) {
			struct nvkm_gpuobj *pgt = vm->pgt[pde].obj[big];

			end = (pte + num);
			if (unlikely(end >= max))
				end = max;
			len = end - pte;

			mmu->map(vma, pgt, node, pte, len, phys, delta);

			num -= len;
			pte += len;
			if (unlikely(end >= max)) {
				phys += len << (bits + 12);
				pde++;
				pte = 0;
			}

			delta += (u64)len << vma->node->type;
		}
	}

	mmu->flush(vm);
}

static void
nvkm_vm_map_sg_table(struct nvkm_vma *vma, u64 delta, u64 length,
		     struct nvkm_mem *mem)
{
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	int big = vma->node->type != mmu->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> mmu->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->pgt_bits - bits);
	unsigned m, sglen;
	u32 end, len;
	int i;
	struct scatterlist *sg;

	for_each_sg(mem->sg->sgl, sg, mem->sg->nents, i) {
		struct nvkm_gpuobj *pgt = vm->pgt[pde].obj[big];
		sglen = sg_dma_len(sg) >> PAGE_SHIFT;

		end = pte + sglen;
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		for (m = 0; m < len; m++) {
			dma_addr_t addr = sg_dma_address(sg) + (m << PAGE_SHIFT);

			mmu->map_sg(vma, pgt, mem, pte, 1, &addr);
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

				mmu->map_sg(vma, pgt, mem, pte, 1, &addr);
				num--;
				pte++;
				if (num == 0)
					goto finish;
			}
		}

	}
finish:
	mmu->flush(vm);
}

static void
nvkm_vm_map_sg(struct nvkm_vma *vma, u64 delta, u64 length,
	       struct nvkm_mem *mem)
{
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	dma_addr_t *list = mem->pages;
	int big = vma->node->type != mmu->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> mmu->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->pgt_bits - bits);
	u32 end, len;

	while (num) {
		struct nvkm_gpuobj *pgt = vm->pgt[pde].obj[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		mmu->map_sg(vma, pgt, mem, pte, len, list);

		num  -= len;
		pte  += len;
		list += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	mmu->flush(vm);
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
	int big = vma->node->type != mmu->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> mmu->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << mmu->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (mmu->pgt_bits - bits);
	u32 end, len;

	while (num) {
		struct nvkm_gpuobj *pgt = vm->pgt[pde].obj[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		mmu->unmap(pgt, pte, len);

		num -= len;
		pte += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	mmu->flush(vm);
}

void
nvkm_vm_unmap(struct nvkm_vma *vma)
{
	nvkm_vm_unmap_at(vma, 0, (u64)vma->node->length << 12);
}

static void
nvkm_vm_unmap_pgt(struct nvkm_vm *vm, int big, u32 fpde, u32 lpde)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_vm_pgd *vpgd;
	struct nvkm_vm_pgt *vpgt;
	struct nvkm_gpuobj *pgt;
	u32 pde;

	for (pde = fpde; pde <= lpde; pde++) {
		vpgt = &vm->pgt[pde - vm->fpde];
		if (--vpgt->refcount[big])
			continue;

		pgt = vpgt->obj[big];
		vpgt->obj[big] = NULL;

		list_for_each_entry(vpgd, &vm->pgd_list, head) {
			mmu->map_pgt(vpgd->obj, pde, vpgt->obj);
		}

		mutex_unlock(&nv_subdev(mmu)->mutex);
		nvkm_gpuobj_ref(NULL, &pgt);
		mutex_lock(&nv_subdev(mmu)->mutex);
	}
}

static int
nvkm_vm_map_pgt(struct nvkm_vm *vm, u32 pde, u32 type)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
	struct nvkm_vm_pgd *vpgd;
	struct nvkm_gpuobj *pgt;
	int big = (type != mmu->spg_shift);
	u32 pgt_size;
	int ret;

	pgt_size  = (1 << (mmu->pgt_bits + 12)) >> type;
	pgt_size *= 8;

	mutex_unlock(&nv_subdev(mmu)->mutex);
	ret = nvkm_gpuobj_new(nv_object(vm->mmu), NULL, pgt_size, 0x1000,
			      NVOBJ_FLAG_ZERO_ALLOC, &pgt);
	mutex_lock(&nv_subdev(mmu)->mutex);
	if (unlikely(ret))
		return ret;

	/* someone beat us to filling the PDE while we didn't have the lock */
	if (unlikely(vpgt->refcount[big]++)) {
		mutex_unlock(&nv_subdev(mmu)->mutex);
		nvkm_gpuobj_ref(NULL, &pgt);
		mutex_lock(&nv_subdev(mmu)->mutex);
		return 0;
	}

	vpgt->obj[big] = pgt;
	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		mmu->map_pgt(vpgd->obj, pde, vpgt->obj);
	}

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

	mutex_lock(&nv_subdev(mmu)->mutex);
	ret = nvkm_mm_head(&vm->mm, 0, page_shift, msize, msize, align,
			   &vma->node);
	if (unlikely(ret != 0)) {
		mutex_unlock(&nv_subdev(mmu)->mutex);
		return ret;
	}

	fpde = (vma->node->offset >> mmu->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> mmu->pgt_bits;

	for (pde = fpde; pde <= lpde; pde++) {
		struct nvkm_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
		int big = (vma->node->type != mmu->spg_shift);

		if (likely(vpgt->refcount[big])) {
			vpgt->refcount[big]++;
			continue;
		}

		ret = nvkm_vm_map_pgt(vm, pde, vma->node->type);
		if (ret) {
			if (pde != fpde)
				nvkm_vm_unmap_pgt(vm, big, fpde, pde - 1);
			nvkm_mm_free(&vm->mm, &vma->node);
			mutex_unlock(&nv_subdev(mmu)->mutex);
			return ret;
		}
	}
	mutex_unlock(&nv_subdev(mmu)->mutex);

	vma->vm = NULL;
	nvkm_vm_ref(vm, &vma->vm, NULL);
	vma->offset = (u64)vma->node->offset << 12;
	vma->access = access;
	return 0;
}

void
nvkm_vm_put(struct nvkm_vma *vma)
{
	struct nvkm_vm *vm = vma->vm;
	struct nvkm_mmu *mmu = vm->mmu;
	u32 fpde, lpde;

	if (unlikely(vma->node == NULL))
		return;
	fpde = (vma->node->offset >> mmu->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> mmu->pgt_bits;

	mutex_lock(&nv_subdev(mmu)->mutex);
	nvkm_vm_unmap_pgt(vm, vma->node->type != mmu->spg_shift, fpde, lpde);
	nvkm_mm_free(&vm->mm, &vma->node);
	mutex_unlock(&nv_subdev(mmu)->mutex);

	nvkm_vm_ref(NULL, &vma->vm, NULL);
}

int
nvkm_vm_create(struct nvkm_mmu *mmu, u64 offset, u64 length, u64 mm_offset,
	       u32 block, struct nvkm_vm **pvm)
{
	struct nvkm_vm *vm;
	u64 mm_length = (offset + length) - mm_offset;
	int ret;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return -ENOMEM;

	INIT_LIST_HEAD(&vm->pgd_list);
	vm->mmu = mmu;
	kref_init(&vm->refcount);
	vm->fpde = offset >> (mmu->pgt_bits + 12);
	vm->lpde = (offset + length - 1) >> (mmu->pgt_bits + 12);

	vm->pgt  = vzalloc((vm->lpde - vm->fpde + 1) * sizeof(*vm->pgt));
	if (!vm->pgt) {
		kfree(vm);
		return -ENOMEM;
	}

	ret = nvkm_mm_init(&vm->mm, mm_offset >> 12, mm_length >> 12,
			   block >> 12);
	if (ret) {
		vfree(vm->pgt);
		kfree(vm);
		return ret;
	}

	*pvm = vm;

	return 0;
}

int
nvkm_vm_new(struct nvkm_device *device, u64 offset, u64 length, u64 mm_offset,
	    struct nvkm_vm **pvm)
{
	struct nvkm_mmu *mmu = nvkm_mmu(device);
	return mmu->create(mmu, offset, length, mm_offset, pvm);
}

static int
nvkm_vm_link(struct nvkm_vm *vm, struct nvkm_gpuobj *pgd)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_vm_pgd *vpgd;
	int i;

	if (!pgd)
		return 0;

	vpgd = kzalloc(sizeof(*vpgd), GFP_KERNEL);
	if (!vpgd)
		return -ENOMEM;

	nvkm_gpuobj_ref(pgd, &vpgd->obj);

	mutex_lock(&nv_subdev(mmu)->mutex);
	for (i = vm->fpde; i <= vm->lpde; i++)
		mmu->map_pgt(pgd, i, vm->pgt[i - vm->fpde].obj);
	list_add(&vpgd->head, &vm->pgd_list);
	mutex_unlock(&nv_subdev(mmu)->mutex);
	return 0;
}

static void
nvkm_vm_unlink(struct nvkm_vm *vm, struct nvkm_gpuobj *mpgd)
{
	struct nvkm_mmu *mmu = vm->mmu;
	struct nvkm_vm_pgd *vpgd, *tmp;
	struct nvkm_gpuobj *pgd = NULL;

	if (!mpgd)
		return;

	mutex_lock(&nv_subdev(mmu)->mutex);
	list_for_each_entry_safe(vpgd, tmp, &vm->pgd_list, head) {
		if (vpgd->obj == mpgd) {
			pgd = vpgd->obj;
			list_del(&vpgd->head);
			kfree(vpgd);
			break;
		}
	}
	mutex_unlock(&nv_subdev(mmu)->mutex);

	nvkm_gpuobj_ref(NULL, &pgd);
}

static void
nvkm_vm_del(struct kref *kref)
{
	struct nvkm_vm *vm = container_of(kref, typeof(*vm), refcount);
	struct nvkm_vm_pgd *vpgd, *tmp;

	list_for_each_entry_safe(vpgd, tmp, &vm->pgd_list, head) {
		nvkm_vm_unlink(vm, vpgd->obj);
	}

	nvkm_mm_fini(&vm->mm);
	vfree(vm->pgt);
	kfree(vm);
}

int
nvkm_vm_ref(struct nvkm_vm *ref, struct nvkm_vm **ptr, struct nvkm_gpuobj *pgd)
{
	if (ref) {
		int ret = nvkm_vm_link(ref, pgd);
		if (ret)
			return ret;

		kref_get(&ref->refcount);
	}

	if (*ptr) {
		nvkm_vm_unlink(*ptr, pgd);
		kref_put(&(*ptr)->refcount, nvkm_vm_del);
	}

	*ptr = ref;
	return 0;
}

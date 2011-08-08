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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_mm.h"
#include "nouveau_vm.h"

void
nouveau_vm_map_at(struct nouveau_vma *vma, u64 delta, struct nouveau_mem *node)
{
	struct nouveau_vm *vm = vma->vm;
	struct nouveau_mm_node *r;
	int big = vma->node->type != vm->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 pde  = (offset >> vm->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << vm->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (vm->pgt_bits - bits);
	u32 end, len;

	delta = 0;
	list_for_each_entry(r, &node->regions, rl_entry) {
		u64 phys = (u64)r->offset << 12;
		u32 num  = r->length >> bits;

		while (num) {
			struct nouveau_gpuobj *pgt = vm->pgt[pde].obj[big];

			end = (pte + num);
			if (unlikely(end >= max))
				end = max;
			len = end - pte;

			vm->map(vma, pgt, node, pte, len, phys, delta);

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

	vm->flush(vm);
}

void
nouveau_vm_map(struct nouveau_vma *vma, struct nouveau_mem *node)
{
	nouveau_vm_map_at(vma, 0, node);
}

void
nouveau_vm_map_sg(struct nouveau_vma *vma, u64 delta, u64 length,
		  struct nouveau_mem *mem, dma_addr_t *list)
{
	struct nouveau_vm *vm = vma->vm;
	int big = vma->node->type != vm->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> vm->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << vm->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (vm->pgt_bits - bits);
	u32 end, len;

	while (num) {
		struct nouveau_gpuobj *pgt = vm->pgt[pde].obj[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		vm->map_sg(vma, pgt, mem, pte, len, list);

		num  -= len;
		pte  += len;
		list += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	vm->flush(vm);
}

void
nouveau_vm_unmap_at(struct nouveau_vma *vma, u64 delta, u64 length)
{
	struct nouveau_vm *vm = vma->vm;
	int big = vma->node->type != vm->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> vm->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << vm->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (vm->pgt_bits - bits);
	u32 end, len;

	while (num) {
		struct nouveau_gpuobj *pgt = vm->pgt[pde].obj[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		vm->unmap(pgt, pte, len);

		num -= len;
		pte += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	vm->flush(vm);
}

void
nouveau_vm_unmap(struct nouveau_vma *vma)
{
	nouveau_vm_unmap_at(vma, 0, (u64)vma->node->length << 12);
}

static void
nouveau_vm_unmap_pgt(struct nouveau_vm *vm, int big, u32 fpde, u32 lpde)
{
	struct nouveau_vm_pgd *vpgd;
	struct nouveau_vm_pgt *vpgt;
	struct nouveau_gpuobj *pgt;
	u32 pde;

	for (pde = fpde; pde <= lpde; pde++) {
		vpgt = &vm->pgt[pde - vm->fpde];
		if (--vpgt->refcount[big])
			continue;

		pgt = vpgt->obj[big];
		vpgt->obj[big] = NULL;

		list_for_each_entry(vpgd, &vm->pgd_list, head) {
			vm->map_pgt(vpgd->obj, pde, vpgt->obj);
		}

		mutex_unlock(&vm->mm->mutex);
		nouveau_gpuobj_ref(NULL, &pgt);
		mutex_lock(&vm->mm->mutex);
	}
}

static int
nouveau_vm_map_pgt(struct nouveau_vm *vm, u32 pde, u32 type)
{
	struct nouveau_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
	struct nouveau_vm_pgd *vpgd;
	struct nouveau_gpuobj *pgt;
	int big = (type != vm->spg_shift);
	u32 pgt_size;
	int ret;

	pgt_size  = (1 << (vm->pgt_bits + 12)) >> type;
	pgt_size *= 8;

	mutex_unlock(&vm->mm->mutex);
	ret = nouveau_gpuobj_new(vm->dev, NULL, pgt_size, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &pgt);
	mutex_lock(&vm->mm->mutex);
	if (unlikely(ret))
		return ret;

	/* someone beat us to filling the PDE while we didn't have the lock */
	if (unlikely(vpgt->refcount[big]++)) {
		mutex_unlock(&vm->mm->mutex);
		nouveau_gpuobj_ref(NULL, &pgt);
		mutex_lock(&vm->mm->mutex);
		return 0;
	}

	vpgt->obj[big] = pgt;
	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		vm->map_pgt(vpgd->obj, pde, vpgt->obj);
	}

	return 0;
}

int
nouveau_vm_get(struct nouveau_vm *vm, u64 size, u32 page_shift,
	       u32 access, struct nouveau_vma *vma)
{
	u32 align = (1 << page_shift) >> 12;
	u32 msize = size >> 12;
	u32 fpde, lpde, pde;
	int ret;

	mutex_lock(&vm->mm->mutex);
	ret = nouveau_mm_get(vm->mm, page_shift, msize, 0, align, &vma->node);
	if (unlikely(ret != 0)) {
		mutex_unlock(&vm->mm->mutex);
		return ret;
	}

	fpde = (vma->node->offset >> vm->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> vm->pgt_bits;
	for (pde = fpde; pde <= lpde; pde++) {
		struct nouveau_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
		int big = (vma->node->type != vm->spg_shift);

		if (likely(vpgt->refcount[big])) {
			vpgt->refcount[big]++;
			continue;
		}

		ret = nouveau_vm_map_pgt(vm, pde, vma->node->type);
		if (ret) {
			if (pde != fpde)
				nouveau_vm_unmap_pgt(vm, big, fpde, pde - 1);
			nouveau_mm_put(vm->mm, vma->node);
			mutex_unlock(&vm->mm->mutex);
			vma->node = NULL;
			return ret;
		}
	}
	mutex_unlock(&vm->mm->mutex);

	vma->vm     = vm;
	vma->offset = (u64)vma->node->offset << 12;
	vma->access = access;
	return 0;
}

void
nouveau_vm_put(struct nouveau_vma *vma)
{
	struct nouveau_vm *vm = vma->vm;
	u32 fpde, lpde;

	if (unlikely(vma->node == NULL))
		return;
	fpde = (vma->node->offset >> vm->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> vm->pgt_bits;

	mutex_lock(&vm->mm->mutex);
	nouveau_vm_unmap_pgt(vm, vma->node->type != vm->spg_shift, fpde, lpde);
	nouveau_mm_put(vm->mm, vma->node);
	vma->node = NULL;
	mutex_unlock(&vm->mm->mutex);
}

int
nouveau_vm_new(struct drm_device *dev, u64 offset, u64 length, u64 mm_offset,
	       struct nouveau_vm **pvm)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_vm *vm;
	u64 mm_length = (offset + length) - mm_offset;
	u32 block, pgt_bits;
	int ret;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return -ENOMEM;

	if (dev_priv->card_type == NV_50) {
		vm->map_pgt = nv50_vm_map_pgt;
		vm->map = nv50_vm_map;
		vm->map_sg = nv50_vm_map_sg;
		vm->unmap = nv50_vm_unmap;
		vm->flush = nv50_vm_flush;
		vm->spg_shift = 12;
		vm->lpg_shift = 16;

		pgt_bits = 29;
		block = (1 << pgt_bits);
		if (length < block)
			block = length;

	} else
	if (dev_priv->card_type == NV_C0) {
		vm->map_pgt = nvc0_vm_map_pgt;
		vm->map = nvc0_vm_map;
		vm->map_sg = nvc0_vm_map_sg;
		vm->unmap = nvc0_vm_unmap;
		vm->flush = nvc0_vm_flush;
		vm->spg_shift = 12;
		vm->lpg_shift = 17;
		pgt_bits = 27;
		block = 4096;
	} else {
		kfree(vm);
		return -ENOSYS;
	}

	vm->fpde   = offset >> pgt_bits;
	vm->lpde   = (offset + length - 1) >> pgt_bits;
	vm->pgt = kcalloc(vm->lpde - vm->fpde + 1, sizeof(*vm->pgt), GFP_KERNEL);
	if (!vm->pgt) {
		kfree(vm);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vm->pgd_list);
	vm->dev = dev;
	vm->refcount = 1;
	vm->pgt_bits = pgt_bits - 12;

	ret = nouveau_mm_init(&vm->mm, mm_offset >> 12, mm_length >> 12,
			      block >> 12);
	if (ret) {
		kfree(vm);
		return ret;
	}

	*pvm = vm;
	return 0;
}

static int
nouveau_vm_link(struct nouveau_vm *vm, struct nouveau_gpuobj *pgd)
{
	struct nouveau_vm_pgd *vpgd;
	int i;

	if (!pgd)
		return 0;

	vpgd = kzalloc(sizeof(*vpgd), GFP_KERNEL);
	if (!vpgd)
		return -ENOMEM;

	nouveau_gpuobj_ref(pgd, &vpgd->obj);

	mutex_lock(&vm->mm->mutex);
	for (i = vm->fpde; i <= vm->lpde; i++)
		vm->map_pgt(pgd, i, vm->pgt[i - vm->fpde].obj);
	list_add(&vpgd->head, &vm->pgd_list);
	mutex_unlock(&vm->mm->mutex);
	return 0;
}

static void
nouveau_vm_unlink(struct nouveau_vm *vm, struct nouveau_gpuobj *mpgd)
{
	struct nouveau_vm_pgd *vpgd, *tmp;
	struct nouveau_gpuobj *pgd = NULL;

	if (!mpgd)
		return;

	mutex_lock(&vm->mm->mutex);
	list_for_each_entry_safe(vpgd, tmp, &vm->pgd_list, head) {
		if (vpgd->obj == mpgd) {
			pgd = vpgd->obj;
			list_del(&vpgd->head);
			kfree(vpgd);
			break;
		}
	}
	mutex_unlock(&vm->mm->mutex);

	nouveau_gpuobj_ref(NULL, &pgd);
}

static void
nouveau_vm_del(struct nouveau_vm *vm)
{
	struct nouveau_vm_pgd *vpgd, *tmp;

	list_for_each_entry_safe(vpgd, tmp, &vm->pgd_list, head) {
		nouveau_vm_unlink(vm, vpgd->obj);
	}

	nouveau_mm_fini(&vm->mm);
	kfree(vm->pgt);
	kfree(vm);
}

int
nouveau_vm_ref(struct nouveau_vm *ref, struct nouveau_vm **ptr,
	       struct nouveau_gpuobj *pgd)
{
	struct nouveau_vm *vm;
	int ret;

	vm = ref;
	if (vm) {
		ret = nouveau_vm_link(vm, pgd);
		if (ret)
			return ret;

		vm->refcount++;
	}

	vm = *ptr;
	*ptr = ref;

	if (vm) {
		nouveau_vm_unlink(vm, pgd);

		if (--vm->refcount == 0)
			nouveau_vm_del(vm);
	}

	return 0;
}

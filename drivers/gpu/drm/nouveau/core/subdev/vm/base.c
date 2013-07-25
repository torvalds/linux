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

#include <core/gpuobj.h>
#include <core/mm.h>

#include <subdev/fb.h>
#include <subdev/vm.h>

void
nouveau_vm_map_at(struct nouveau_vma *vma, u64 delta, struct nouveau_mem *node)
{
	struct nouveau_vm *vm = vma->vm;
	struct nouveau_vmmgr *vmm = vm->vmm;
	struct nouveau_mm_node *r;
	int big = vma->node->type != vmm->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 pde  = (offset >> vmm->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << vmm->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (vmm->pgt_bits - bits);
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

			vmm->map(vma, pgt, node, pte, len, phys, delta);

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

	vmm->flush(vm);
}

void
nouveau_vm_map(struct nouveau_vma *vma, struct nouveau_mem *node)
{
	nouveau_vm_map_at(vma, 0, node);
}

void
nouveau_vm_map_sg_table(struct nouveau_vma *vma, u64 delta, u64 length,
			struct nouveau_mem *mem)
{
	struct nouveau_vm *vm = vma->vm;
	struct nouveau_vmmgr *vmm = vm->vmm;
	int big = vma->node->type != vmm->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> vmm->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << vmm->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (vmm->pgt_bits - bits);
	unsigned m, sglen;
	u32 end, len;
	int i;
	struct scatterlist *sg;

	for_each_sg(mem->sg->sgl, sg, mem->sg->nents, i) {
		struct nouveau_gpuobj *pgt = vm->pgt[pde].obj[big];
		sglen = sg_dma_len(sg) >> PAGE_SHIFT;

		end = pte + sglen;
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		for (m = 0; m < len; m++) {
			dma_addr_t addr = sg_dma_address(sg) + (m << PAGE_SHIFT);

			vmm->map_sg(vma, pgt, mem, pte, 1, &addr);
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

				vmm->map_sg(vma, pgt, mem, pte, 1, &addr);
				num--;
				pte++;
				if (num == 0)
					goto finish;
			}
		}

	}
finish:
	vmm->flush(vm);
}

void
nouveau_vm_map_sg(struct nouveau_vma *vma, u64 delta, u64 length,
		  struct nouveau_mem *mem)
{
	struct nouveau_vm *vm = vma->vm;
	struct nouveau_vmmgr *vmm = vm->vmm;
	dma_addr_t *list = mem->pages;
	int big = vma->node->type != vmm->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> vmm->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << vmm->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (vmm->pgt_bits - bits);
	u32 end, len;

	while (num) {
		struct nouveau_gpuobj *pgt = vm->pgt[pde].obj[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		vmm->map_sg(vma, pgt, mem, pte, len, list);

		num  -= len;
		pte  += len;
		list += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	vmm->flush(vm);
}

void
nouveau_vm_unmap_at(struct nouveau_vma *vma, u64 delta, u64 length)
{
	struct nouveau_vm *vm = vma->vm;
	struct nouveau_vmmgr *vmm = vm->vmm;
	int big = vma->node->type != vmm->spg_shift;
	u32 offset = vma->node->offset + (delta >> 12);
	u32 bits = vma->node->type - 12;
	u32 num  = length >> vma->node->type;
	u32 pde  = (offset >> vmm->pgt_bits) - vm->fpde;
	u32 pte  = (offset & ((1 << vmm->pgt_bits) - 1)) >> bits;
	u32 max  = 1 << (vmm->pgt_bits - bits);
	u32 end, len;

	while (num) {
		struct nouveau_gpuobj *pgt = vm->pgt[pde].obj[big];

		end = (pte + num);
		if (unlikely(end >= max))
			end = max;
		len = end - pte;

		vmm->unmap(pgt, pte, len);

		num -= len;
		pte += len;
		if (unlikely(end >= max)) {
			pde++;
			pte = 0;
		}
	}

	vmm->flush(vm);
}

void
nouveau_vm_unmap(struct nouveau_vma *vma)
{
	nouveau_vm_unmap_at(vma, 0, (u64)vma->node->length << 12);
}

static void
nouveau_vm_unmap_pgt(struct nouveau_vm *vm, int big, u32 fpde, u32 lpde)
{
	struct nouveau_vmmgr *vmm = vm->vmm;
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
			vmm->map_pgt(vpgd->obj, pde, vpgt->obj);
		}

		mutex_unlock(&vm->mm.mutex);
		nouveau_gpuobj_ref(NULL, &pgt);
		mutex_lock(&vm->mm.mutex);
	}
}

static int
nouveau_vm_map_pgt(struct nouveau_vm *vm, u32 pde, u32 type)
{
	struct nouveau_vmmgr *vmm = vm->vmm;
	struct nouveau_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
	struct nouveau_vm_pgd *vpgd;
	struct nouveau_gpuobj *pgt;
	int big = (type != vmm->spg_shift);
	u32 pgt_size;
	int ret;

	pgt_size  = (1 << (vmm->pgt_bits + 12)) >> type;
	pgt_size *= 8;

	mutex_unlock(&vm->mm.mutex);
	ret = nouveau_gpuobj_new(nv_object(vm->vmm), NULL, pgt_size, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &pgt);
	mutex_lock(&vm->mm.mutex);
	if (unlikely(ret))
		return ret;

	/* someone beat us to filling the PDE while we didn't have the lock */
	if (unlikely(vpgt->refcount[big]++)) {
		mutex_unlock(&vm->mm.mutex);
		nouveau_gpuobj_ref(NULL, &pgt);
		mutex_lock(&vm->mm.mutex);
		return 0;
	}

	vpgt->obj[big] = pgt;
	list_for_each_entry(vpgd, &vm->pgd_list, head) {
		vmm->map_pgt(vpgd->obj, pde, vpgt->obj);
	}

	return 0;
}

int
nouveau_vm_get(struct nouveau_vm *vm, u64 size, u32 page_shift,
	       u32 access, struct nouveau_vma *vma)
{
	struct nouveau_vmmgr *vmm = vm->vmm;
	u32 align = (1 << page_shift) >> 12;
	u32 msize = size >> 12;
	u32 fpde, lpde, pde;
	int ret;

	mutex_lock(&vm->mm.mutex);
	ret = nouveau_mm_head(&vm->mm, page_shift, msize, msize, align,
			     &vma->node);
	if (unlikely(ret != 0)) {
		mutex_unlock(&vm->mm.mutex);
		return ret;
	}

	fpde = (vma->node->offset >> vmm->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> vmm->pgt_bits;

	for (pde = fpde; pde <= lpde; pde++) {
		struct nouveau_vm_pgt *vpgt = &vm->pgt[pde - vm->fpde];
		int big = (vma->node->type != vmm->spg_shift);

		if (likely(vpgt->refcount[big])) {
			vpgt->refcount[big]++;
			continue;
		}

		ret = nouveau_vm_map_pgt(vm, pde, vma->node->type);
		if (ret) {
			if (pde != fpde)
				nouveau_vm_unmap_pgt(vm, big, fpde, pde - 1);
			nouveau_mm_free(&vm->mm, &vma->node);
			mutex_unlock(&vm->mm.mutex);
			return ret;
		}
	}
	mutex_unlock(&vm->mm.mutex);

	vma->vm     = vm;
	vma->offset = (u64)vma->node->offset << 12;
	vma->access = access;
	return 0;
}

void
nouveau_vm_put(struct nouveau_vma *vma)
{
	struct nouveau_vm *vm = vma->vm;
	struct nouveau_vmmgr *vmm = vm->vmm;
	u32 fpde, lpde;

	if (unlikely(vma->node == NULL))
		return;
	fpde = (vma->node->offset >> vmm->pgt_bits);
	lpde = (vma->node->offset + vma->node->length - 1) >> vmm->pgt_bits;

	mutex_lock(&vm->mm.mutex);
	nouveau_vm_unmap_pgt(vm, vma->node->type != vmm->spg_shift, fpde, lpde);
	nouveau_mm_free(&vm->mm, &vma->node);
	mutex_unlock(&vm->mm.mutex);
}

int
nouveau_vm_create(struct nouveau_vmmgr *vmm, u64 offset, u64 length,
		  u64 mm_offset, u32 block, struct nouveau_vm **pvm)
{
	struct nouveau_vm *vm;
	u64 mm_length = (offset + length) - mm_offset;
	int ret;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return -ENOMEM;

	INIT_LIST_HEAD(&vm->pgd_list);
	vm->vmm = vmm;
	vm->refcount = 1;
	vm->fpde = offset >> (vmm->pgt_bits + 12);
	vm->lpde = (offset + length - 1) >> (vmm->pgt_bits + 12);

	vm->pgt  = vzalloc((vm->lpde - vm->fpde + 1) * sizeof(*vm->pgt));
	if (!vm->pgt) {
		kfree(vm);
		return -ENOMEM;
	}

	ret = nouveau_mm_init(&vm->mm, mm_offset >> 12, mm_length >> 12,
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
nouveau_vm_new(struct nouveau_device *device, u64 offset, u64 length,
	       u64 mm_offset, struct nouveau_vm **pvm)
{
	struct nouveau_vmmgr *vmm = nouveau_vmmgr(device);
	return vmm->create(vmm, offset, length, mm_offset, pvm);
}

static int
nouveau_vm_link(struct nouveau_vm *vm, struct nouveau_gpuobj *pgd)
{
	struct nouveau_vmmgr *vmm = vm->vmm;
	struct nouveau_vm_pgd *vpgd;
	int i;

	if (!pgd)
		return 0;

	vpgd = kzalloc(sizeof(*vpgd), GFP_KERNEL);
	if (!vpgd)
		return -ENOMEM;

	nouveau_gpuobj_ref(pgd, &vpgd->obj);

	mutex_lock(&vm->mm.mutex);
	for (i = vm->fpde; i <= vm->lpde; i++)
		vmm->map_pgt(pgd, i, vm->pgt[i - vm->fpde].obj);
	list_add(&vpgd->head, &vm->pgd_list);
	mutex_unlock(&vm->mm.mutex);
	return 0;
}

static void
nouveau_vm_unlink(struct nouveau_vm *vm, struct nouveau_gpuobj *mpgd)
{
	struct nouveau_vm_pgd *vpgd, *tmp;
	struct nouveau_gpuobj *pgd = NULL;

	if (!mpgd)
		return;

	mutex_lock(&vm->mm.mutex);
	list_for_each_entry_safe(vpgd, tmp, &vm->pgd_list, head) {
		if (vpgd->obj == mpgd) {
			pgd = vpgd->obj;
			list_del(&vpgd->head);
			kfree(vpgd);
			break;
		}
	}
	mutex_unlock(&vm->mm.mutex);

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
	vfree(vm->pgt);
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

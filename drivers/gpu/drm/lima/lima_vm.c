// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include "lima_device.h"
#include "lima_vm.h"
#include "lima_gem.h"
#include "lima_regs.h"

struct lima_bo_va {
	struct list_head list;
	unsigned int ref_count;

	struct drm_mm_node node;

	struct lima_vm *vm;
};

#define LIMA_VM_PD_SHIFT 22
#define LIMA_VM_PT_SHIFT 12
#define LIMA_VM_PB_SHIFT (LIMA_VM_PD_SHIFT + LIMA_VM_NUM_PT_PER_BT_SHIFT)
#define LIMA_VM_BT_SHIFT LIMA_VM_PT_SHIFT

#define LIMA_VM_PT_MASK ((1 << LIMA_VM_PD_SHIFT) - 1)
#define LIMA_VM_BT_MASK ((1 << LIMA_VM_PB_SHIFT) - 1)

#define LIMA_PDE(va) (va >> LIMA_VM_PD_SHIFT)
#define LIMA_PTE(va) ((va & LIMA_VM_PT_MASK) >> LIMA_VM_PT_SHIFT)
#define LIMA_PBE(va) (va >> LIMA_VM_PB_SHIFT)
#define LIMA_BTE(va) ((va & LIMA_VM_BT_MASK) >> LIMA_VM_BT_SHIFT)


static void lima_vm_unmap_range(struct lima_vm *vm, u32 start, u32 end)
{
	u32 addr;

	for (addr = start; addr <= end; addr += LIMA_PAGE_SIZE) {
		u32 pbe = LIMA_PBE(addr);
		u32 bte = LIMA_BTE(addr);

		vm->bts[pbe].cpu[bte] = 0;
	}
}

static int lima_vm_map_page(struct lima_vm *vm, dma_addr_t pa, u32 va)
{
	u32 pbe = LIMA_PBE(va);
	u32 bte = LIMA_BTE(va);

	if (!vm->bts[pbe].cpu) {
		dma_addr_t pts;
		u32 *pd;
		int j;

		vm->bts[pbe].cpu = dma_alloc_wc(
			vm->dev->dev, LIMA_PAGE_SIZE << LIMA_VM_NUM_PT_PER_BT_SHIFT,
			&vm->bts[pbe].dma, GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO);
		if (!vm->bts[pbe].cpu)
			return -ENOMEM;

		pts = vm->bts[pbe].dma;
		pd = vm->pd.cpu + (pbe << LIMA_VM_NUM_PT_PER_BT_SHIFT);
		for (j = 0; j < LIMA_VM_NUM_PT_PER_BT; j++) {
			pd[j] = pts | LIMA_VM_FLAG_PRESENT;
			pts += LIMA_PAGE_SIZE;
		}
	}

	vm->bts[pbe].cpu[bte] = pa | LIMA_VM_FLAGS_CACHE;

	return 0;
}

static struct lima_bo_va *
lima_vm_bo_find(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va, *ret = NULL;

	list_for_each_entry(bo_va, &bo->va, list) {
		if (bo_va->vm == vm) {
			ret = bo_va;
			break;
		}
	}

	return ret;
}

int lima_vm_bo_add(struct lima_vm *vm, struct lima_bo *bo, bool create)
{
	struct lima_bo_va *bo_va;
	struct sg_dma_page_iter sg_iter;
	int offset = 0, err;

	mutex_lock(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	if (bo_va) {
		bo_va->ref_count++;
		mutex_unlock(&bo->lock);
		return 0;
	}

	/* should not create new bo_va if not asked by caller */
	if (!create) {
		mutex_unlock(&bo->lock);
		return -ENOENT;
	}

	bo_va = kzalloc(sizeof(*bo_va), GFP_KERNEL);
	if (!bo_va) {
		err = -ENOMEM;
		goto err_out0;
	}

	bo_va->vm = vm;
	bo_va->ref_count = 1;

	mutex_lock(&vm->lock);

	err = drm_mm_insert_node(&vm->mm, &bo_va->node, lima_bo_size(bo));
	if (err)
		goto err_out1;

	for_each_sgtable_dma_page(bo->base.sgt, &sg_iter, 0) {
		err = lima_vm_map_page(vm, sg_page_iter_dma_address(&sg_iter),
				       bo_va->node.start + offset);
		if (err)
			goto err_out2;

		offset += PAGE_SIZE;
	}

	mutex_unlock(&vm->lock);

	list_add_tail(&bo_va->list, &bo->va);

	mutex_unlock(&bo->lock);
	return 0;

err_out2:
	if (offset)
		lima_vm_unmap_range(vm, bo_va->node.start, bo_va->node.start + offset - 1);
	drm_mm_remove_node(&bo_va->node);
err_out1:
	mutex_unlock(&vm->lock);
	kfree(bo_va);
err_out0:
	mutex_unlock(&bo->lock);
	return err;
}

void lima_vm_bo_del(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va;
	u32 size;

	mutex_lock(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	if (--bo_va->ref_count > 0) {
		mutex_unlock(&bo->lock);
		return;
	}

	mutex_lock(&vm->lock);

	size = bo->heap_size ? bo->heap_size : bo_va->node.size;
	lima_vm_unmap_range(vm, bo_va->node.start,
			    bo_va->node.start + size - 1);

	drm_mm_remove_node(&bo_va->node);

	mutex_unlock(&vm->lock);

	list_del(&bo_va->list);

	mutex_unlock(&bo->lock);

	kfree(bo_va);
}

u32 lima_vm_get_va(struct lima_vm *vm, struct lima_bo *bo)
{
	struct lima_bo_va *bo_va;
	u32 ret;

	mutex_lock(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	ret = bo_va->node.start;

	mutex_unlock(&bo->lock);

	return ret;
}

struct lima_vm *lima_vm_create(struct lima_device *dev)
{
	struct lima_vm *vm;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return NULL;

	vm->dev = dev;
	mutex_init(&vm->lock);
	kref_init(&vm->refcount);

	vm->pd.cpu = dma_alloc_wc(dev->dev, LIMA_PAGE_SIZE, &vm->pd.dma,
				  GFP_KERNEL | __GFP_NOWARN | __GFP_ZERO);
	if (!vm->pd.cpu)
		goto err_out0;

	if (dev->dlbu_cpu) {
		int err = lima_vm_map_page(
			vm, dev->dlbu_dma, LIMA_VA_RESERVE_DLBU);
		if (err)
			goto err_out1;
	}

	drm_mm_init(&vm->mm, dev->va_start, dev->va_end - dev->va_start);

	return vm;

err_out1:
	dma_free_wc(dev->dev, LIMA_PAGE_SIZE, vm->pd.cpu, vm->pd.dma);
err_out0:
	kfree(vm);
	return NULL;
}

void lima_vm_release(struct kref *kref)
{
	struct lima_vm *vm = container_of(kref, struct lima_vm, refcount);
	int i;

	drm_mm_takedown(&vm->mm);

	for (i = 0; i < LIMA_VM_NUM_BT; i++) {
		if (vm->bts[i].cpu)
			dma_free_wc(vm->dev->dev, LIMA_PAGE_SIZE << LIMA_VM_NUM_PT_PER_BT_SHIFT,
				    vm->bts[i].cpu, vm->bts[i].dma);
	}

	if (vm->pd.cpu)
		dma_free_wc(vm->dev->dev, LIMA_PAGE_SIZE, vm->pd.cpu, vm->pd.dma);

	kfree(vm);
}

void lima_vm_print(struct lima_vm *vm)
{
	int i, j, k;
	u32 *pd, *pt;

	if (!vm->pd.cpu)
		return;

	pd = vm->pd.cpu;
	for (i = 0; i < LIMA_VM_NUM_BT; i++) {
		if (!vm->bts[i].cpu)
			continue;

		pt = vm->bts[i].cpu;
		for (j = 0; j < LIMA_VM_NUM_PT_PER_BT; j++) {
			int idx = (i << LIMA_VM_NUM_PT_PER_BT_SHIFT) + j;

			printk(KERN_INFO "lima vm pd %03x:%08x\n", idx, pd[idx]);

			for (k = 0; k < LIMA_PAGE_ENT_NUM; k++) {
				u32 pte = *pt++;

				if (pte)
					printk(KERN_INFO "  pt %03x:%08x\n", k, pte);
			}
		}
	}
}

int lima_vm_map_bo(struct lima_vm *vm, struct lima_bo *bo, int pageoff)
{
	struct lima_bo_va *bo_va;
	struct sg_dma_page_iter sg_iter;
	int offset = 0, err;
	u32 base;

	mutex_lock(&bo->lock);

	bo_va = lima_vm_bo_find(vm, bo);
	if (!bo_va) {
		err = -ENOENT;
		goto err_out0;
	}

	mutex_lock(&vm->lock);

	base = bo_va->node.start + (pageoff << PAGE_SHIFT);
	for_each_sgtable_dma_page(bo->base.sgt, &sg_iter, pageoff) {
		err = lima_vm_map_page(vm, sg_page_iter_dma_address(&sg_iter),
				       base + offset);
		if (err)
			goto err_out1;

		offset += PAGE_SIZE;
	}

	mutex_unlock(&vm->lock);

	mutex_unlock(&bo->lock);
	return 0;

err_out1:
	if (offset)
		lima_vm_unmap_range(vm, base, base + offset - 1);
	mutex_unlock(&vm->lock);
err_out0:
	mutex_unlock(&bo->lock);
	return err;
}

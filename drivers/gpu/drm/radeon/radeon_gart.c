/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
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
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"
#include "radeon_reg.h"

/*
 * Common GART table functions.
 */
int radeon_gart_table_ram_alloc(struct radeon_device *rdev)
{
	void *ptr;

	ptr = pci_alloc_consistent(rdev->pdev, rdev->gart.table_size,
				   &rdev->gart.table_addr);
	if (ptr == NULL) {
		return -ENOMEM;
	}
#ifdef CONFIG_X86
	if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480 ||
	    rdev->family == CHIP_RS690 || rdev->family == CHIP_RS740) {
		set_memory_uc((unsigned long)ptr,
			      rdev->gart.table_size >> PAGE_SHIFT);
	}
#endif
	rdev->gart.ptr = ptr;
	memset((void *)rdev->gart.ptr, 0, rdev->gart.table_size);
	return 0;
}

void radeon_gart_table_ram_free(struct radeon_device *rdev)
{
	if (rdev->gart.ptr == NULL) {
		return;
	}
#ifdef CONFIG_X86
	if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480 ||
	    rdev->family == CHIP_RS690 || rdev->family == CHIP_RS740) {
		set_memory_wb((unsigned long)rdev->gart.ptr,
			      rdev->gart.table_size >> PAGE_SHIFT);
	}
#endif
	pci_free_consistent(rdev->pdev, rdev->gart.table_size,
			    (void *)rdev->gart.ptr,
			    rdev->gart.table_addr);
	rdev->gart.ptr = NULL;
	rdev->gart.table_addr = 0;
}

int radeon_gart_table_vram_alloc(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.robj == NULL) {
		r = radeon_bo_create(rdev, rdev->gart.table_size,
				     PAGE_SIZE, true, RADEON_GEM_DOMAIN_VRAM,
				     &rdev->gart.robj);
		if (r) {
			return r;
		}
	}
	return 0;
}

int radeon_gart_table_vram_pin(struct radeon_device *rdev)
{
	uint64_t gpu_addr;
	int r;

	r = radeon_bo_reserve(rdev->gart.robj, false);
	if (unlikely(r != 0))
		return r;
	r = radeon_bo_pin(rdev->gart.robj,
				RADEON_GEM_DOMAIN_VRAM, &gpu_addr);
	if (r) {
		radeon_bo_unreserve(rdev->gart.robj);
		return r;
	}
	r = radeon_bo_kmap(rdev->gart.robj, &rdev->gart.ptr);
	if (r)
		radeon_bo_unpin(rdev->gart.robj);
	radeon_bo_unreserve(rdev->gart.robj);
	rdev->gart.table_addr = gpu_addr;
	return r;
}

void radeon_gart_table_vram_unpin(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.robj == NULL) {
		return;
	}
	r = radeon_bo_reserve(rdev->gart.robj, false);
	if (likely(r == 0)) {
		radeon_bo_kunmap(rdev->gart.robj);
		radeon_bo_unpin(rdev->gart.robj);
		radeon_bo_unreserve(rdev->gart.robj);
		rdev->gart.ptr = NULL;
	}
}

void radeon_gart_table_vram_free(struct radeon_device *rdev)
{
	if (rdev->gart.robj == NULL) {
		return;
	}
	radeon_gart_table_vram_unpin(rdev);
	radeon_bo_unref(&rdev->gart.robj);
}




/*
 * Common gart functions.
 */
void radeon_gart_unbind(struct radeon_device *rdev, unsigned offset,
			int pages)
{
	unsigned t;
	unsigned p;
	int i, j;
	u64 page_base;

	if (!rdev->gart.ready) {
		WARN(1, "trying to unbind memory from uninitialized GART !\n");
		return;
	}
	t = offset / RADEON_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / RADEON_GPU_PAGE_SIZE);
	for (i = 0; i < pages; i++, p++) {
		if (rdev->gart.pages[p]) {
			rdev->gart.pages[p] = NULL;
			rdev->gart.pages_addr[p] = rdev->dummy_page.addr;
			page_base = rdev->gart.pages_addr[p];
			for (j = 0; j < (PAGE_SIZE / RADEON_GPU_PAGE_SIZE); j++, t++) {
				if (rdev->gart.ptr) {
					radeon_gart_set_page(rdev, t, page_base);
				}
				page_base += RADEON_GPU_PAGE_SIZE;
			}
		}
	}
	mb();
	radeon_gart_tlb_flush(rdev);
}

int radeon_gart_bind(struct radeon_device *rdev, unsigned offset,
		     int pages, struct page **pagelist, dma_addr_t *dma_addr)
{
	unsigned t;
	unsigned p;
	uint64_t page_base;
	int i, j;

	if (!rdev->gart.ready) {
		WARN(1, "trying to bind memory to uninitialized GART !\n");
		return -EINVAL;
	}
	t = offset / RADEON_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / RADEON_GPU_PAGE_SIZE);

	for (i = 0; i < pages; i++, p++) {
		rdev->gart.pages_addr[p] = dma_addr[i];
		rdev->gart.pages[p] = pagelist[i];
		if (rdev->gart.ptr) {
			page_base = rdev->gart.pages_addr[p];
			for (j = 0; j < (PAGE_SIZE / RADEON_GPU_PAGE_SIZE); j++, t++) {
				radeon_gart_set_page(rdev, t, page_base);
				page_base += RADEON_GPU_PAGE_SIZE;
			}
		}
	}
	mb();
	radeon_gart_tlb_flush(rdev);
	return 0;
}

void radeon_gart_restore(struct radeon_device *rdev)
{
	int i, j, t;
	u64 page_base;

	if (!rdev->gart.ptr) {
		return;
	}
	for (i = 0, t = 0; i < rdev->gart.num_cpu_pages; i++) {
		page_base = rdev->gart.pages_addr[i];
		for (j = 0; j < (PAGE_SIZE / RADEON_GPU_PAGE_SIZE); j++, t++) {
			radeon_gart_set_page(rdev, t, page_base);
			page_base += RADEON_GPU_PAGE_SIZE;
		}
	}
	mb();
	radeon_gart_tlb_flush(rdev);
}

int radeon_gart_init(struct radeon_device *rdev)
{
	int r, i;

	if (rdev->gart.pages) {
		return 0;
	}
	/* We need PAGE_SIZE >= RADEON_GPU_PAGE_SIZE */
	if (PAGE_SIZE < RADEON_GPU_PAGE_SIZE) {
		DRM_ERROR("Page size is smaller than GPU page size!\n");
		return -EINVAL;
	}
	r = radeon_dummy_page_init(rdev);
	if (r)
		return r;
	/* Compute table size */
	rdev->gart.num_cpu_pages = rdev->mc.gtt_size / PAGE_SIZE;
	rdev->gart.num_gpu_pages = rdev->mc.gtt_size / RADEON_GPU_PAGE_SIZE;
	DRM_INFO("GART: num cpu pages %u, num gpu pages %u\n",
		 rdev->gart.num_cpu_pages, rdev->gart.num_gpu_pages);
	/* Allocate pages table */
	rdev->gart.pages = kzalloc(sizeof(void *) * rdev->gart.num_cpu_pages,
				   GFP_KERNEL);
	if (rdev->gart.pages == NULL) {
		radeon_gart_fini(rdev);
		return -ENOMEM;
	}
	rdev->gart.pages_addr = kzalloc(sizeof(dma_addr_t) *
					rdev->gart.num_cpu_pages, GFP_KERNEL);
	if (rdev->gart.pages_addr == NULL) {
		radeon_gart_fini(rdev);
		return -ENOMEM;
	}
	/* set GART entry to point to the dummy page by default */
	for (i = 0; i < rdev->gart.num_cpu_pages; i++) {
		rdev->gart.pages_addr[i] = rdev->dummy_page.addr;
	}
	return 0;
}

void radeon_gart_fini(struct radeon_device *rdev)
{
	if (rdev->gart.pages && rdev->gart.pages_addr && rdev->gart.ready) {
		/* unbind pages */
		radeon_gart_unbind(rdev, 0, rdev->gart.num_cpu_pages);
	}
	rdev->gart.ready = false;
	kfree(rdev->gart.pages);
	kfree(rdev->gart.pages_addr);
	rdev->gart.pages = NULL;
	rdev->gart.pages_addr = NULL;

	radeon_dummy_page_fini(rdev);
}

/*
 * vm helpers
 *
 * TODO bind a default page at vm initialization for default address
 */
int radeon_vm_manager_init(struct radeon_device *rdev)
{
	int r;

	rdev->vm_manager.enabled = false;

	/* mark first vm as always in use, it's the system one */
	r = radeon_sa_bo_manager_init(rdev, &rdev->vm_manager.sa_manager,
				      rdev->vm_manager.max_pfn * 8,
				      RADEON_GEM_DOMAIN_VRAM);
	if (r) {
		dev_err(rdev->dev, "failed to allocate vm bo (%dKB)\n",
			(rdev->vm_manager.max_pfn * 8) >> 10);
		return r;
	}

	r = rdev->vm_manager.funcs->init(rdev);
	if (r == 0)
		rdev->vm_manager.enabled = true;

	return r;
}

/* cs mutex must be lock */
static void radeon_vm_unbind_locked(struct radeon_device *rdev,
				    struct radeon_vm *vm)
{
	struct radeon_bo_va *bo_va;

	if (vm->id == -1) {
		return;
	}

	/* wait for vm use to end */
	if (vm->fence) {
		radeon_fence_wait(vm->fence, false);
		radeon_fence_unref(&vm->fence);
	}

	/* hw unbind */
	rdev->vm_manager.funcs->unbind(rdev, vm);
	rdev->vm_manager.use_bitmap &= ~(1 << vm->id);
	list_del_init(&vm->list);
	vm->id = -1;
	radeon_sa_bo_free(rdev, &vm->sa_bo);
	vm->pt = NULL;

	list_for_each_entry(bo_va, &vm->va, vm_list) {
		bo_va->valid = false;
	}
}

void radeon_vm_manager_fini(struct radeon_device *rdev)
{
	if (rdev->vm_manager.sa_manager.bo == NULL)
		return;
	radeon_vm_manager_suspend(rdev);
	rdev->vm_manager.funcs->fini(rdev);
	radeon_sa_bo_manager_fini(rdev, &rdev->vm_manager.sa_manager);
	rdev->vm_manager.enabled = false;
}

int radeon_vm_manager_start(struct radeon_device *rdev)
{
	if (rdev->vm_manager.sa_manager.bo == NULL) {
		return -EINVAL;
	}
	return radeon_sa_bo_manager_start(rdev, &rdev->vm_manager.sa_manager);
}

int radeon_vm_manager_suspend(struct radeon_device *rdev)
{
	struct radeon_vm *vm, *tmp;

	radeon_mutex_lock(&rdev->cs_mutex);
	/* unbind all active vm */
	list_for_each_entry_safe(vm, tmp, &rdev->vm_manager.lru_vm, list) {
		radeon_vm_unbind_locked(rdev, vm);
	}
	rdev->vm_manager.funcs->fini(rdev);
	radeon_mutex_unlock(&rdev->cs_mutex);
	return radeon_sa_bo_manager_suspend(rdev, &rdev->vm_manager.sa_manager);
}

/* cs mutex must be lock */
void radeon_vm_unbind(struct radeon_device *rdev, struct radeon_vm *vm)
{
	mutex_lock(&vm->mutex);
	radeon_vm_unbind_locked(rdev, vm);
	mutex_unlock(&vm->mutex);
}

/* cs mutex must be lock & vm mutex must be lock */
int radeon_vm_bind(struct radeon_device *rdev, struct radeon_vm *vm)
{
	struct radeon_vm *vm_evict;
	unsigned i;
	int id = -1, r;

	if (vm == NULL) {
		return -EINVAL;
	}

	if (vm->id != -1) {
		/* update lru */
		list_del_init(&vm->list);
		list_add_tail(&vm->list, &rdev->vm_manager.lru_vm);
		return 0;
	}

retry:
	r = radeon_sa_bo_new(rdev, &rdev->vm_manager.sa_manager, &vm->sa_bo,
			     RADEON_GPU_PAGE_ALIGN(vm->last_pfn * 8),
			     RADEON_GPU_PAGE_SIZE);
	if (r) {
		if (list_empty(&rdev->vm_manager.lru_vm)) {
			return r;
		}
		vm_evict = list_first_entry(&rdev->vm_manager.lru_vm, struct radeon_vm, list);
		radeon_vm_unbind(rdev, vm_evict);
		goto retry;
	}
	vm->pt = rdev->vm_manager.sa_manager.cpu_ptr;
	vm->pt += (vm->sa_bo.offset >> 3);
	vm->pt_gpu_addr = rdev->vm_manager.sa_manager.gpu_addr;
	vm->pt_gpu_addr += vm->sa_bo.offset;
	memset(vm->pt, 0, RADEON_GPU_PAGE_ALIGN(vm->last_pfn * 8));

retry_id:
	/* search for free vm */
	for (i = 0; i < rdev->vm_manager.nvm; i++) {
		if (!(rdev->vm_manager.use_bitmap & (1 << i))) {
			id = i;
			break;
		}
	}
	/* evict vm if necessary */
	if (id == -1) {
		vm_evict = list_first_entry(&rdev->vm_manager.lru_vm, struct radeon_vm, list);
		radeon_vm_unbind(rdev, vm_evict);
		goto retry_id;
	}

	/* do hw bind */
	r = rdev->vm_manager.funcs->bind(rdev, vm, id);
	if (r) {
		radeon_sa_bo_free(rdev, &vm->sa_bo);
		return r;
	}
	rdev->vm_manager.use_bitmap |= 1 << id;
	vm->id = id;
	list_add_tail(&vm->list, &rdev->vm_manager.lru_vm);
	return radeon_vm_bo_update_pte(rdev, vm, rdev->ib_pool.sa_manager.bo,
				       &rdev->ib_pool.sa_manager.bo->tbo.mem);
}

/* object have to be reserved */
int radeon_vm_bo_add(struct radeon_device *rdev,
		     struct radeon_vm *vm,
		     struct radeon_bo *bo,
		     uint64_t offset,
		     uint32_t flags)
{
	struct radeon_bo_va *bo_va, *tmp;
	struct list_head *head;
	uint64_t size = radeon_bo_size(bo), last_offset = 0;
	unsigned last_pfn;

	bo_va = kzalloc(sizeof(struct radeon_bo_va), GFP_KERNEL);
	if (bo_va == NULL) {
		return -ENOMEM;
	}
	bo_va->vm = vm;
	bo_va->bo = bo;
	bo_va->soffset = offset;
	bo_va->eoffset = offset + size;
	bo_va->flags = flags;
	bo_va->valid = false;
	INIT_LIST_HEAD(&bo_va->bo_list);
	INIT_LIST_HEAD(&bo_va->vm_list);
	/* make sure object fit at this offset */
	if (bo_va->soffset >= bo_va->eoffset) {
		kfree(bo_va);
		return -EINVAL;
	}

	last_pfn = bo_va->eoffset / RADEON_GPU_PAGE_SIZE;
	if (last_pfn > rdev->vm_manager.max_pfn) {
		kfree(bo_va);
		dev_err(rdev->dev, "va above limit (0x%08X > 0x%08X)\n",
			last_pfn, rdev->vm_manager.max_pfn);
		return -EINVAL;
	}

	mutex_lock(&vm->mutex);
	if (last_pfn > vm->last_pfn) {
		/* grow va space 32M by 32M */
		unsigned align = ((32 << 20) >> 12) - 1;
		radeon_mutex_lock(&rdev->cs_mutex);
		radeon_vm_unbind_locked(rdev, vm);
		radeon_mutex_unlock(&rdev->cs_mutex);
		vm->last_pfn = (last_pfn + align) & ~align;
	}
	head = &vm->va;
	last_offset = 0;
	list_for_each_entry(tmp, &vm->va, vm_list) {
		if (bo_va->soffset >= last_offset && bo_va->eoffset < tmp->soffset) {
			/* bo can be added before this one */
			break;
		}
		if (bo_va->soffset >= tmp->soffset && bo_va->soffset < tmp->eoffset) {
			/* bo and tmp overlap, invalid offset */
			dev_err(rdev->dev, "bo %p va 0x%08X conflict with (bo %p 0x%08X 0x%08X)\n",
				bo, (unsigned)bo_va->soffset, tmp->bo,
				(unsigned)tmp->soffset, (unsigned)tmp->eoffset);
			kfree(bo_va);
			mutex_unlock(&vm->mutex);
			return -EINVAL;
		}
		last_offset = tmp->eoffset;
		head = &tmp->vm_list;
	}
	list_add(&bo_va->vm_list, head);
	list_add_tail(&bo_va->bo_list, &bo->va);
	mutex_unlock(&vm->mutex);
	return 0;
}

static u64 radeon_vm_get_addr(struct radeon_device *rdev,
			      struct ttm_mem_reg *mem,
			      unsigned pfn)
{
	u64 addr = 0;

	switch (mem->mem_type) {
	case TTM_PL_VRAM:
		addr = (mem->start << PAGE_SHIFT);
		addr += pfn * RADEON_GPU_PAGE_SIZE;
		addr += rdev->vm_manager.vram_base_offset;
		break;
	case TTM_PL_TT:
		/* offset inside page table */
		addr = mem->start << PAGE_SHIFT;
		addr += pfn * RADEON_GPU_PAGE_SIZE;
		addr = addr >> PAGE_SHIFT;
		/* page table offset */
		addr = rdev->gart.pages_addr[addr];
		/* in case cpu page size != gpu page size*/
		addr += (pfn * RADEON_GPU_PAGE_SIZE) & (~PAGE_MASK);
		break;
	default:
		break;
	}
	return addr;
}

/* object have to be reserved & cs mutex took & vm mutex took */
int radeon_vm_bo_update_pte(struct radeon_device *rdev,
			    struct radeon_vm *vm,
			    struct radeon_bo *bo,
			    struct ttm_mem_reg *mem)
{
	struct radeon_bo_va *bo_va;
	unsigned ngpu_pages, i;
	uint64_t addr = 0, pfn;
	uint32_t flags;

	/* nothing to do if vm isn't bound */
	if (vm->id == -1)
		return 0;

	bo_va = radeon_bo_va(bo, vm);
	if (bo_va == NULL) {
		dev_err(rdev->dev, "bo %p not in vm %p\n", bo, vm);
		return -EINVAL;
	}

	if (bo_va->valid)
		return 0;

	ngpu_pages = radeon_bo_ngpu_pages(bo);
	bo_va->flags &= ~RADEON_VM_PAGE_VALID;
	bo_va->flags &= ~RADEON_VM_PAGE_SYSTEM;
	if (mem) {
		if (mem->mem_type != TTM_PL_SYSTEM) {
			bo_va->flags |= RADEON_VM_PAGE_VALID;
			bo_va->valid = true;
		}
		if (mem->mem_type == TTM_PL_TT) {
			bo_va->flags |= RADEON_VM_PAGE_SYSTEM;
		}
	}
	pfn = bo_va->soffset / RADEON_GPU_PAGE_SIZE;
	flags = rdev->vm_manager.funcs->page_flags(rdev, bo_va->vm, bo_va->flags);
	for (i = 0, addr = 0; i < ngpu_pages; i++) {
		if (mem && bo_va->valid) {
			addr = radeon_vm_get_addr(rdev, mem, i);
		}
		rdev->vm_manager.funcs->set_page(rdev, bo_va->vm, i + pfn, addr, flags);
	}
	rdev->vm_manager.funcs->tlb_flush(rdev, bo_va->vm);
	return 0;
}

/* object have to be reserved */
int radeon_vm_bo_rmv(struct radeon_device *rdev,
		     struct radeon_vm *vm,
		     struct radeon_bo *bo)
{
	struct radeon_bo_va *bo_va;

	bo_va = radeon_bo_va(bo, vm);
	if (bo_va == NULL)
		return 0;

	mutex_lock(&vm->mutex);
	radeon_mutex_lock(&rdev->cs_mutex);
	radeon_vm_bo_update_pte(rdev, vm, bo, NULL);
	radeon_mutex_unlock(&rdev->cs_mutex);
	list_del(&bo_va->vm_list);
	mutex_unlock(&vm->mutex);
	list_del(&bo_va->bo_list);

	kfree(bo_va);
	return 0;
}

void radeon_vm_bo_invalidate(struct radeon_device *rdev,
			     struct radeon_bo *bo)
{
	struct radeon_bo_va *bo_va;

	BUG_ON(!atomic_read(&bo->tbo.reserved));
	list_for_each_entry(bo_va, &bo->va, bo_list) {
		bo_va->valid = false;
	}
}

int radeon_vm_init(struct radeon_device *rdev, struct radeon_vm *vm)
{
	int r;

	vm->id = -1;
	vm->fence = NULL;
	mutex_init(&vm->mutex);
	INIT_LIST_HEAD(&vm->list);
	INIT_LIST_HEAD(&vm->va);
	vm->last_pfn = 0;
	/* map the ib pool buffer at 0 in virtual address space, set
	 * read only
	 */
	r = radeon_vm_bo_add(rdev, vm, rdev->ib_pool.sa_manager.bo, 0,
			     RADEON_VM_PAGE_READABLE | RADEON_VM_PAGE_SNOOPED);
	return r;
}

void radeon_vm_fini(struct radeon_device *rdev, struct radeon_vm *vm)
{
	struct radeon_bo_va *bo_va, *tmp;
	int r;

	mutex_lock(&vm->mutex);

	radeon_mutex_lock(&rdev->cs_mutex);
	radeon_vm_unbind_locked(rdev, vm);
	radeon_mutex_unlock(&rdev->cs_mutex);

	/* remove all bo */
	r = radeon_bo_reserve(rdev->ib_pool.sa_manager.bo, false);
	if (!r) {
		bo_va = radeon_bo_va(rdev->ib_pool.sa_manager.bo, vm);
		list_del_init(&bo_va->bo_list);
		list_del_init(&bo_va->vm_list);
		radeon_bo_unreserve(rdev->ib_pool.sa_manager.bo);
		kfree(bo_va);
	}
	if (!list_empty(&vm->va)) {
		dev_err(rdev->dev, "still active bo inside vm\n");
	}
	list_for_each_entry_safe(bo_va, tmp, &vm->va, vm_list) {
		list_del_init(&bo_va->vm_list);
		r = radeon_bo_reserve(bo_va->bo, false);
		if (!r) {
			list_del_init(&bo_va->bo_list);
			radeon_bo_unreserve(bo_va->bo);
			kfree(bo_va);
		}
	}
	mutex_unlock(&vm->mutex);
}

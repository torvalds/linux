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
	rdev->gart.table.ram.ptr = ptr;
	memset((void *)rdev->gart.table.ram.ptr, 0, rdev->gart.table_size);
	return 0;
}

void radeon_gart_table_ram_free(struct radeon_device *rdev)
{
	if (rdev->gart.table.ram.ptr == NULL) {
		return;
	}
#ifdef CONFIG_X86
	if (rdev->family == CHIP_RS400 || rdev->family == CHIP_RS480 ||
	    rdev->family == CHIP_RS690 || rdev->family == CHIP_RS740) {
		set_memory_wb((unsigned long)rdev->gart.table.ram.ptr,
			      rdev->gart.table_size >> PAGE_SHIFT);
	}
#endif
	pci_free_consistent(rdev->pdev, rdev->gart.table_size,
			    (void *)rdev->gart.table.ram.ptr,
			    rdev->gart.table_addr);
	rdev->gart.table.ram.ptr = NULL;
	rdev->gart.table_addr = 0;
}

int radeon_gart_table_vram_alloc(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.table.vram.robj == NULL) {
		r = radeon_bo_create(rdev, NULL, rdev->gart.table_size,
					true, RADEON_GEM_DOMAIN_VRAM,
					&rdev->gart.table.vram.robj);
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

	r = radeon_bo_reserve(rdev->gart.table.vram.robj, false);
	if (unlikely(r != 0))
		return r;
	r = radeon_bo_pin(rdev->gart.table.vram.robj,
				RADEON_GEM_DOMAIN_VRAM, &gpu_addr);
	if (r) {
		radeon_bo_unreserve(rdev->gart.table.vram.robj);
		return r;
	}
	r = radeon_bo_kmap(rdev->gart.table.vram.robj,
				(void **)&rdev->gart.table.vram.ptr);
	if (r)
		radeon_bo_unpin(rdev->gart.table.vram.robj);
	radeon_bo_unreserve(rdev->gart.table.vram.robj);
	rdev->gart.table_addr = gpu_addr;
	return r;
}

void radeon_gart_table_vram_free(struct radeon_device *rdev)
{
	int r;

	if (rdev->gart.table.vram.robj == NULL) {
		return;
	}
	r = radeon_bo_reserve(rdev->gart.table.vram.robj, false);
	if (likely(r == 0)) {
		radeon_bo_kunmap(rdev->gart.table.vram.robj);
		radeon_bo_unpin(rdev->gart.table.vram.robj);
		radeon_bo_unreserve(rdev->gart.table.vram.robj);
	}
	radeon_bo_unref(&rdev->gart.table.vram.robj);
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
		WARN(1, "trying to unbind memory to unitialized GART !\n");
		return;
	}
	t = offset / RADEON_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / RADEON_GPU_PAGE_SIZE);
	for (i = 0; i < pages; i++, p++) {
		if (rdev->gart.pages[p]) {
			pci_unmap_page(rdev->pdev, rdev->gart.pages_addr[p],
				       PAGE_SIZE, PCI_DMA_BIDIRECTIONAL);
			rdev->gart.pages[p] = NULL;
			rdev->gart.pages_addr[p] = rdev->dummy_page.addr;
			page_base = rdev->gart.pages_addr[p];
			for (j = 0; j < (PAGE_SIZE / RADEON_GPU_PAGE_SIZE); j++, t++) {
				radeon_gart_set_page(rdev, t, page_base);
				page_base += RADEON_GPU_PAGE_SIZE;
			}
		}
	}
	mb();
	radeon_gart_tlb_flush(rdev);
}

int radeon_gart_bind(struct radeon_device *rdev, unsigned offset,
		     int pages, struct page **pagelist)
{
	unsigned t;
	unsigned p;
	uint64_t page_base;
	int i, j;

	if (!rdev->gart.ready) {
		DRM_ERROR("trying to bind memory to unitialized GART !\n");
		return -EINVAL;
	}
	t = offset / RADEON_GPU_PAGE_SIZE;
	p = t / (PAGE_SIZE / RADEON_GPU_PAGE_SIZE);

	for (i = 0; i < pages; i++, p++) {
		/* we need to support large memory configurations */
		/* assume that unbind have already been call on the range */
		rdev->gart.pages_addr[p] = pci_map_page(rdev->pdev, pagelist[i],
							0, PAGE_SIZE,
							PCI_DMA_BIDIRECTIONAL);
		if (pci_dma_mapping_error(rdev->pdev, rdev->gart.pages_addr[p])) {
			/* FIXME: failed to map page (return -ENOMEM?) */
			radeon_gart_unbind(rdev, offset, pages);
			return -ENOMEM;
		}
		rdev->gart.pages[p] = pagelist[i];
		page_base = rdev->gart.pages_addr[p];
		for (j = 0; j < (PAGE_SIZE / RADEON_GPU_PAGE_SIZE); j++, t++) {
			radeon_gart_set_page(rdev, t, page_base);
			page_base += RADEON_GPU_PAGE_SIZE;
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
}

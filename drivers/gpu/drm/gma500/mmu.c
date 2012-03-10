/**************************************************************************
 * Copyright (c) 2007, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/
#include <drm/drmP.h>
#include "psb_drv.h"
#include "psb_reg.h"

/*
 * Code for the SGX MMU:
 */

/*
 * clflush on one processor only:
 * clflush should apparently flush the cache line on all processors in an
 * SMP system.
 */

/*
 * kmap atomic:
 * The usage of the slots must be completely encapsulated within a spinlock, and
 * no other functions that may be using the locks for other purposed may be
 * called from within the locked region.
 * Since the slots are per processor, this will guarantee that we are the only
 * user.
 */

/*
 * TODO: Inserting ptes from an interrupt handler:
 * This may be desirable for some SGX functionality where the GPU can fault in
 * needed pages. For that, we need to make an atomic insert_pages function, that
 * may fail.
 * If it fails, the caller need to insert the page using a workqueue function,
 * but on average it should be fast.
 */

struct psb_mmu_driver {
	/* protects driver- and pd structures. Always take in read mode
	 * before taking the page table spinlock.
	 */
	struct rw_semaphore sem;

	/* protects page tables, directory tables and pt tables.
	 * and pt structures.
	 */
	spinlock_t lock;

	atomic_t needs_tlbflush;

	uint8_t __iomem *register_map;
	struct psb_mmu_pd *default_pd;
	/*uint32_t bif_ctrl;*/
	int has_clflush;
	int clflush_add;
	unsigned long clflush_mask;

	struct drm_psb_private *dev_priv;
};

struct psb_mmu_pd;

struct psb_mmu_pt {
	struct psb_mmu_pd *pd;
	uint32_t index;
	uint32_t count;
	struct page *p;
	uint32_t *v;
};

struct psb_mmu_pd {
	struct psb_mmu_driver *driver;
	int hw_context;
	struct psb_mmu_pt **tables;
	struct page *p;
	struct page *dummy_pt;
	struct page *dummy_page;
	uint32_t pd_mask;
	uint32_t invalid_pde;
	uint32_t invalid_pte;
};

static inline uint32_t psb_mmu_pt_index(uint32_t offset)
{
	return (offset >> PSB_PTE_SHIFT) & 0x3FF;
}

static inline uint32_t psb_mmu_pd_index(uint32_t offset)
{
	return offset >> PSB_PDE_SHIFT;
}

static inline void psb_clflush(void *addr)
{
	__asm__ __volatile__("clflush (%0)\n" : : "r"(addr) : "memory");
}

static inline void psb_mmu_clflush(struct psb_mmu_driver *driver,
				   void *addr)
{
	if (!driver->has_clflush)
		return;

	mb();
	psb_clflush(addr);
	mb();
}

static void psb_page_clflush(struct psb_mmu_driver *driver, struct page* page)
{
	uint32_t clflush_add = driver->clflush_add >> PAGE_SHIFT;
	uint32_t clflush_count = PAGE_SIZE / clflush_add;
	int i;
	uint8_t *clf;

	clf = kmap_atomic(page, KM_USER0);
	mb();
	for (i = 0; i < clflush_count; ++i) {
		psb_clflush(clf);
		clf += clflush_add;
	}
	mb();
	kunmap_atomic(clf, KM_USER0);
}

static void psb_pages_clflush(struct psb_mmu_driver *driver,
				struct page *page[], unsigned long num_pages)
{
	int i;

	if (!driver->has_clflush)
		return ;

	for (i = 0; i < num_pages; i++)
		psb_page_clflush(driver, *page++);
}

static void psb_mmu_flush_pd_locked(struct psb_mmu_driver *driver,
				    int force)
{
	atomic_set(&driver->needs_tlbflush, 0);
}

static void psb_mmu_flush_pd(struct psb_mmu_driver *driver, int force)
{
	down_write(&driver->sem);
	psb_mmu_flush_pd_locked(driver, force);
	up_write(&driver->sem);
}

void psb_mmu_flush(struct psb_mmu_driver *driver, int rc_prot)
{
	if (rc_prot)
		down_write(&driver->sem);
	if (rc_prot)
		up_write(&driver->sem);
}

void psb_mmu_set_pd_context(struct psb_mmu_pd *pd, int hw_context)
{
	/*ttm_tt_cache_flush(&pd->p, 1);*/
	psb_pages_clflush(pd->driver, &pd->p, 1);
	down_write(&pd->driver->sem);
	wmb();
	psb_mmu_flush_pd_locked(pd->driver, 1);
	pd->hw_context = hw_context;
	up_write(&pd->driver->sem);

}

static inline unsigned long psb_pd_addr_end(unsigned long addr,
					    unsigned long end)
{

	addr = (addr + PSB_PDE_MASK + 1) & ~PSB_PDE_MASK;
	return (addr < end) ? addr : end;
}

static inline uint32_t psb_mmu_mask_pte(uint32_t pfn, int type)
{
	uint32_t mask = PSB_PTE_VALID;

	if (type & PSB_MMU_CACHED_MEMORY)
		mask |= PSB_PTE_CACHED;
	if (type & PSB_MMU_RO_MEMORY)
		mask |= PSB_PTE_RO;
	if (type & PSB_MMU_WO_MEMORY)
		mask |= PSB_PTE_WO;

	return (pfn << PAGE_SHIFT) | mask;
}

struct psb_mmu_pd *psb_mmu_alloc_pd(struct psb_mmu_driver *driver,
				    int trap_pagefaults, int invalid_type)
{
	struct psb_mmu_pd *pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	uint32_t *v;
	int i;

	if (!pd)
		return NULL;

	pd->p = alloc_page(GFP_DMA32);
	if (!pd->p)
		goto out_err1;
	pd->dummy_pt = alloc_page(GFP_DMA32);
	if (!pd->dummy_pt)
		goto out_err2;
	pd->dummy_page = alloc_page(GFP_DMA32);
	if (!pd->dummy_page)
		goto out_err3;

	if (!trap_pagefaults) {
		pd->invalid_pde =
		    psb_mmu_mask_pte(page_to_pfn(pd->dummy_pt),
				     invalid_type);
		pd->invalid_pte =
		    psb_mmu_mask_pte(page_to_pfn(pd->dummy_page),
				     invalid_type);
	} else {
		pd->invalid_pde = 0;
		pd->invalid_pte = 0;
	}

	v = kmap(pd->dummy_pt);
	for (i = 0; i < (PAGE_SIZE / sizeof(uint32_t)); ++i)
		v[i] = pd->invalid_pte;

	kunmap(pd->dummy_pt);

	v = kmap(pd->p);
	for (i = 0; i < (PAGE_SIZE / sizeof(uint32_t)); ++i)
		v[i] = pd->invalid_pde;

	kunmap(pd->p);

	clear_page(kmap(pd->dummy_page));
	kunmap(pd->dummy_page);

	pd->tables = vmalloc_user(sizeof(struct psb_mmu_pt *) * 1024);
	if (!pd->tables)
		goto out_err4;

	pd->hw_context = -1;
	pd->pd_mask = PSB_PTE_VALID;
	pd->driver = driver;

	return pd;

out_err4:
	__free_page(pd->dummy_page);
out_err3:
	__free_page(pd->dummy_pt);
out_err2:
	__free_page(pd->p);
out_err1:
	kfree(pd);
	return NULL;
}

static void psb_mmu_free_pt(struct psb_mmu_pt *pt)
{
	__free_page(pt->p);
	kfree(pt);
}

void psb_mmu_free_pagedir(struct psb_mmu_pd *pd)
{
	struct psb_mmu_driver *driver = pd->driver;
	struct psb_mmu_pt *pt;
	int i;

	down_write(&driver->sem);
	if (pd->hw_context != -1)
		psb_mmu_flush_pd_locked(driver, 1);

	/* Should take the spinlock here, but we don't need to do that
	   since we have the semaphore in write mode. */

	for (i = 0; i < 1024; ++i) {
		pt = pd->tables[i];
		if (pt)
			psb_mmu_free_pt(pt);
	}

	vfree(pd->tables);
	__free_page(pd->dummy_page);
	__free_page(pd->dummy_pt);
	__free_page(pd->p);
	kfree(pd);
	up_write(&driver->sem);
}

static struct psb_mmu_pt *psb_mmu_alloc_pt(struct psb_mmu_pd *pd)
{
	struct psb_mmu_pt *pt = kmalloc(sizeof(*pt), GFP_KERNEL);
	void *v;
	uint32_t clflush_add = pd->driver->clflush_add >> PAGE_SHIFT;
	uint32_t clflush_count = PAGE_SIZE / clflush_add;
	spinlock_t *lock = &pd->driver->lock;
	uint8_t *clf;
	uint32_t *ptes;
	int i;

	if (!pt)
		return NULL;

	pt->p = alloc_page(GFP_DMA32);
	if (!pt->p) {
		kfree(pt);
		return NULL;
	}

	spin_lock(lock);

	v = kmap_atomic(pt->p, KM_USER0);
	clf = (uint8_t *) v;
	ptes = (uint32_t *) v;
	for (i = 0; i < (PAGE_SIZE / sizeof(uint32_t)); ++i)
		*ptes++ = pd->invalid_pte;


	if (pd->driver->has_clflush && pd->hw_context != -1) {
		mb();
		for (i = 0; i < clflush_count; ++i) {
			psb_clflush(clf);
			clf += clflush_add;
		}
		mb();
	}

	kunmap_atomic(v, KM_USER0);
	spin_unlock(lock);

	pt->count = 0;
	pt->pd = pd;
	pt->index = 0;

	return pt;
}

static struct psb_mmu_pt *psb_mmu_pt_alloc_map_lock(struct psb_mmu_pd *pd,
					     unsigned long addr)
{
	uint32_t index = psb_mmu_pd_index(addr);
	struct psb_mmu_pt *pt;
	uint32_t *v;
	spinlock_t *lock = &pd->driver->lock;

	spin_lock(lock);
	pt = pd->tables[index];
	while (!pt) {
		spin_unlock(lock);
		pt = psb_mmu_alloc_pt(pd);
		if (!pt)
			return NULL;
		spin_lock(lock);

		if (pd->tables[index]) {
			spin_unlock(lock);
			psb_mmu_free_pt(pt);
			spin_lock(lock);
			pt = pd->tables[index];
			continue;
		}

		v = kmap_atomic(pd->p, KM_USER0);
		pd->tables[index] = pt;
		v[index] = (page_to_pfn(pt->p) << 12) | pd->pd_mask;
		pt->index = index;
		kunmap_atomic((void *) v, KM_USER0);

		if (pd->hw_context != -1) {
			psb_mmu_clflush(pd->driver, (void *) &v[index]);
			atomic_set(&pd->driver->needs_tlbflush, 1);
		}
	}
	pt->v = kmap_atomic(pt->p, KM_USER0);
	return pt;
}

static struct psb_mmu_pt *psb_mmu_pt_map_lock(struct psb_mmu_pd *pd,
					      unsigned long addr)
{
	uint32_t index = psb_mmu_pd_index(addr);
	struct psb_mmu_pt *pt;
	spinlock_t *lock = &pd->driver->lock;

	spin_lock(lock);
	pt = pd->tables[index];
	if (!pt) {
		spin_unlock(lock);
		return NULL;
	}
	pt->v = kmap_atomic(pt->p, KM_USER0);
	return pt;
}

static void psb_mmu_pt_unmap_unlock(struct psb_mmu_pt *pt)
{
	struct psb_mmu_pd *pd = pt->pd;
	uint32_t *v;

	kunmap_atomic(pt->v, KM_USER0);
	if (pt->count == 0) {
		v = kmap_atomic(pd->p, KM_USER0);
		v[pt->index] = pd->invalid_pde;
		pd->tables[pt->index] = NULL;

		if (pd->hw_context != -1) {
			psb_mmu_clflush(pd->driver,
					(void *) &v[pt->index]);
			atomic_set(&pd->driver->needs_tlbflush, 1);
		}
		kunmap_atomic(pt->v, KM_USER0);
		spin_unlock(&pd->driver->lock);
		psb_mmu_free_pt(pt);
		return;
	}
	spin_unlock(&pd->driver->lock);
}

static inline void psb_mmu_set_pte(struct psb_mmu_pt *pt,
				   unsigned long addr, uint32_t pte)
{
	pt->v[psb_mmu_pt_index(addr)] = pte;
}

static inline void psb_mmu_invalidate_pte(struct psb_mmu_pt *pt,
					  unsigned long addr)
{
	pt->v[psb_mmu_pt_index(addr)] = pt->pd->invalid_pte;
}


void psb_mmu_mirror_gtt(struct psb_mmu_pd *pd,
			uint32_t mmu_offset, uint32_t gtt_start,
			uint32_t gtt_pages)
{
	uint32_t *v;
	uint32_t start = psb_mmu_pd_index(mmu_offset);
	struct psb_mmu_driver *driver = pd->driver;
	int num_pages = gtt_pages;

	down_read(&driver->sem);
	spin_lock(&driver->lock);

	v = kmap_atomic(pd->p, KM_USER0);
	v += start;

	while (gtt_pages--) {
		*v++ = gtt_start | pd->pd_mask;
		gtt_start += PAGE_SIZE;
	}

	/*ttm_tt_cache_flush(&pd->p, num_pages);*/
	psb_pages_clflush(pd->driver, &pd->p, num_pages);
	kunmap_atomic(v, KM_USER0);
	spin_unlock(&driver->lock);

	if (pd->hw_context != -1)
		atomic_set(&pd->driver->needs_tlbflush, 1);

	up_read(&pd->driver->sem);
	psb_mmu_flush_pd(pd->driver, 0);
}

struct psb_mmu_pd *psb_mmu_get_default_pd(struct psb_mmu_driver *driver)
{
	struct psb_mmu_pd *pd;

	/* down_read(&driver->sem); */
	pd = driver->default_pd;
	/* up_read(&driver->sem); */

	return pd;
}

void psb_mmu_driver_takedown(struct psb_mmu_driver *driver)
{
	psb_mmu_free_pagedir(driver->default_pd);
	kfree(driver);
}

struct psb_mmu_driver *psb_mmu_driver_init(uint8_t __iomem * registers,
					int trap_pagefaults,
					int invalid_type,
					struct drm_psb_private *dev_priv)
{
	struct psb_mmu_driver *driver;

	driver = kmalloc(sizeof(*driver), GFP_KERNEL);

	if (!driver)
		return NULL;
	driver->dev_priv = dev_priv;

	driver->default_pd = psb_mmu_alloc_pd(driver, trap_pagefaults,
					      invalid_type);
	if (!driver->default_pd)
		goto out_err1;

	spin_lock_init(&driver->lock);
	init_rwsem(&driver->sem);
	down_write(&driver->sem);
	driver->register_map = registers;
	atomic_set(&driver->needs_tlbflush, 1);

	driver->has_clflush = 0;

	if (boot_cpu_has(X86_FEATURE_CLFLSH)) {
		uint32_t tfms, misc, cap0, cap4, clflush_size;

		/*
		 * clflush size is determined at kernel setup for x86_64
		 *  but not for i386. We have to do it here.
		 */

		cpuid(0x00000001, &tfms, &misc, &cap0, &cap4);
		clflush_size = ((misc >> 8) & 0xff) * 8;
		driver->has_clflush = 1;
		driver->clflush_add =
		    PAGE_SIZE * clflush_size / sizeof(uint32_t);
		driver->clflush_mask = driver->clflush_add - 1;
		driver->clflush_mask = ~driver->clflush_mask;
	}

	up_write(&driver->sem);
	return driver;

out_err1:
	kfree(driver);
	return NULL;
}

static void psb_mmu_flush_ptes(struct psb_mmu_pd *pd,
			       unsigned long address, uint32_t num_pages,
			       uint32_t desired_tile_stride,
			       uint32_t hw_tile_stride)
{
	struct psb_mmu_pt *pt;
	uint32_t rows = 1;
	uint32_t i;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long add;
	unsigned long row_add;
	unsigned long clflush_add = pd->driver->clflush_add;
	unsigned long clflush_mask = pd->driver->clflush_mask;

	if (!pd->driver->has_clflush) {
		/*ttm_tt_cache_flush(&pd->p, num_pages);*/
		psb_pages_clflush(pd->driver, &pd->p, num_pages);
		return;
	}

	if (hw_tile_stride)
		rows = num_pages / desired_tile_stride;
	else
		desired_tile_stride = num_pages;

	add = desired_tile_stride << PAGE_SHIFT;
	row_add = hw_tile_stride << PAGE_SHIFT;
	mb();
	for (i = 0; i < rows; ++i) {

		addr = address;
		end = addr + add;

		do {
			next = psb_pd_addr_end(addr, end);
			pt = psb_mmu_pt_map_lock(pd, addr);
			if (!pt)
				continue;
			do {
				psb_clflush(&pt->v
					    [psb_mmu_pt_index(addr)]);
			} while (addr +=
				 clflush_add,
				 (addr & clflush_mask) < next);

			psb_mmu_pt_unmap_unlock(pt);
		} while (addr = next, next != end);
		address += row_add;
	}
	mb();
}

void psb_mmu_remove_pfn_sequence(struct psb_mmu_pd *pd,
				 unsigned long address, uint32_t num_pages)
{
	struct psb_mmu_pt *pt;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long f_address = address;

	down_read(&pd->driver->sem);

	addr = address;
	end = addr + (num_pages << PAGE_SHIFT);

	do {
		next = psb_pd_addr_end(addr, end);
		pt = psb_mmu_pt_alloc_map_lock(pd, addr);
		if (!pt)
			goto out;
		do {
			psb_mmu_invalidate_pte(pt, addr);
			--pt->count;
		} while (addr += PAGE_SIZE, addr < next);
		psb_mmu_pt_unmap_unlock(pt);

	} while (addr = next, next != end);

out:
	if (pd->hw_context != -1)
		psb_mmu_flush_ptes(pd, f_address, num_pages, 1, 1);

	up_read(&pd->driver->sem);

	if (pd->hw_context != -1)
		psb_mmu_flush(pd->driver, 0);

	return;
}

void psb_mmu_remove_pages(struct psb_mmu_pd *pd, unsigned long address,
			  uint32_t num_pages, uint32_t desired_tile_stride,
			  uint32_t hw_tile_stride)
{
	struct psb_mmu_pt *pt;
	uint32_t rows = 1;
	uint32_t i;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long add;
	unsigned long row_add;
	unsigned long f_address = address;

	if (hw_tile_stride)
		rows = num_pages / desired_tile_stride;
	else
		desired_tile_stride = num_pages;

	add = desired_tile_stride << PAGE_SHIFT;
	row_add = hw_tile_stride << PAGE_SHIFT;

	/* down_read(&pd->driver->sem); */

	/* Make sure we only need to flush this processor's cache */

	for (i = 0; i < rows; ++i) {

		addr = address;
		end = addr + add;

		do {
			next = psb_pd_addr_end(addr, end);
			pt = psb_mmu_pt_map_lock(pd, addr);
			if (!pt)
				continue;
			do {
				psb_mmu_invalidate_pte(pt, addr);
				--pt->count;

			} while (addr += PAGE_SIZE, addr < next);
			psb_mmu_pt_unmap_unlock(pt);

		} while (addr = next, next != end);
		address += row_add;
	}
	if (pd->hw_context != -1)
		psb_mmu_flush_ptes(pd, f_address, num_pages,
				   desired_tile_stride, hw_tile_stride);

	/* up_read(&pd->driver->sem); */

	if (pd->hw_context != -1)
		psb_mmu_flush(pd->driver, 0);
}

int psb_mmu_insert_pfn_sequence(struct psb_mmu_pd *pd, uint32_t start_pfn,
				unsigned long address, uint32_t num_pages,
				int type)
{
	struct psb_mmu_pt *pt;
	uint32_t pte;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long f_address = address;
	int ret = 0;

	down_read(&pd->driver->sem);

	addr = address;
	end = addr + (num_pages << PAGE_SHIFT);

	do {
		next = psb_pd_addr_end(addr, end);
		pt = psb_mmu_pt_alloc_map_lock(pd, addr);
		if (!pt) {
			ret = -ENOMEM;
			goto out;
		}
		do {
			pte = psb_mmu_mask_pte(start_pfn++, type);
			psb_mmu_set_pte(pt, addr, pte);
			pt->count++;
		} while (addr += PAGE_SIZE, addr < next);
		psb_mmu_pt_unmap_unlock(pt);

	} while (addr = next, next != end);

out:
	if (pd->hw_context != -1)
		psb_mmu_flush_ptes(pd, f_address, num_pages, 1, 1);

	up_read(&pd->driver->sem);

	if (pd->hw_context != -1)
		psb_mmu_flush(pd->driver, 1);

	return ret;
}

int psb_mmu_insert_pages(struct psb_mmu_pd *pd, struct page **pages,
			 unsigned long address, uint32_t num_pages,
			 uint32_t desired_tile_stride,
			 uint32_t hw_tile_stride, int type)
{
	struct psb_mmu_pt *pt;
	uint32_t rows = 1;
	uint32_t i;
	uint32_t pte;
	unsigned long addr;
	unsigned long end;
	unsigned long next;
	unsigned long add;
	unsigned long row_add;
	unsigned long f_address = address;
	int ret = 0;

	if (hw_tile_stride) {
		if (num_pages % desired_tile_stride != 0)
			return -EINVAL;
		rows = num_pages / desired_tile_stride;
	} else {
		desired_tile_stride = num_pages;
	}

	add = desired_tile_stride << PAGE_SHIFT;
	row_add = hw_tile_stride << PAGE_SHIFT;

	down_read(&pd->driver->sem);

	for (i = 0; i < rows; ++i) {

		addr = address;
		end = addr + add;

		do {
			next = psb_pd_addr_end(addr, end);
			pt = psb_mmu_pt_alloc_map_lock(pd, addr);
			if (!pt) {
				ret = -ENOMEM;
				goto out;
			}
			do {
				pte =
				    psb_mmu_mask_pte(page_to_pfn(*pages++),
						     type);
				psb_mmu_set_pte(pt, addr, pte);
				pt->count++;
			} while (addr += PAGE_SIZE, addr < next);
			psb_mmu_pt_unmap_unlock(pt);

		} while (addr = next, next != end);

		address += row_add;
	}
out:
	if (pd->hw_context != -1)
		psb_mmu_flush_ptes(pd, f_address, num_pages,
				   desired_tile_stride, hw_tile_stride);

	up_read(&pd->driver->sem);

	if (pd->hw_context != -1)
		psb_mmu_flush(pd->driver, 1);

	return ret;
}

int psb_mmu_virtual_to_pfn(struct psb_mmu_pd *pd, uint32_t virtual,
			   unsigned long *pfn)
{
	int ret;
	struct psb_mmu_pt *pt;
	uint32_t tmp;
	spinlock_t *lock = &pd->driver->lock;

	down_read(&pd->driver->sem);
	pt = psb_mmu_pt_map_lock(pd, virtual);
	if (!pt) {
		uint32_t *v;

		spin_lock(lock);
		v = kmap_atomic(pd->p, KM_USER0);
		tmp = v[psb_mmu_pd_index(virtual)];
		kunmap_atomic(v, KM_USER0);
		spin_unlock(lock);

		if (tmp != pd->invalid_pde || !(tmp & PSB_PTE_VALID) ||
		    !(pd->invalid_pte & PSB_PTE_VALID)) {
			ret = -EINVAL;
			goto out;
		}
		ret = 0;
		*pfn = pd->invalid_pte >> PAGE_SHIFT;
		goto out;
	}
	tmp = pt->v[psb_mmu_pt_index(virtual)];
	if (!(tmp & PSB_PTE_VALID)) {
		ret = -EINVAL;
	} else {
		ret = 0;
		*pfn = tmp >> PAGE_SHIFT;
	}
	psb_mmu_pt_unmap_unlock(pt);
out:
	up_read(&pd->driver->sem);
	return ret;
}

/*
 * Copyright IBM Corp. 2012
 *
 * Author(s):
 *   Jan Glauber <jang@linux.vnet.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/iommu-helper.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <asm/pci_dma.h>

static struct kmem_cache *dma_region_table_cache;
static struct kmem_cache *dma_page_table_cache;
static int s390_iommu_strict;

static int zpci_refresh_global(struct zpci_dev *zdev)
{
	return zpci_refresh_trans((u64) zdev->fh << 32, zdev->start_dma,
				  zdev->iommu_pages * PAGE_SIZE);
}

unsigned long *dma_alloc_cpu_table(void)
{
	unsigned long *table, *entry;

	table = kmem_cache_alloc(dma_region_table_cache, GFP_ATOMIC);
	if (!table)
		return NULL;

	for (entry = table; entry < table + ZPCI_TABLE_ENTRIES; entry++)
		*entry = ZPCI_TABLE_INVALID;
	return table;
}

static void dma_free_cpu_table(void *table)
{
	kmem_cache_free(dma_region_table_cache, table);
}

static unsigned long *dma_alloc_page_table(void)
{
	unsigned long *table, *entry;

	table = kmem_cache_alloc(dma_page_table_cache, GFP_ATOMIC);
	if (!table)
		return NULL;

	for (entry = table; entry < table + ZPCI_PT_ENTRIES; entry++)
		*entry = ZPCI_PTE_INVALID;
	return table;
}

static void dma_free_page_table(void *table)
{
	kmem_cache_free(dma_page_table_cache, table);
}

static unsigned long *dma_get_seg_table_origin(unsigned long *entry)
{
	unsigned long *sto;

	if (reg_entry_isvalid(*entry))
		sto = get_rt_sto(*entry);
	else {
		sto = dma_alloc_cpu_table();
		if (!sto)
			return NULL;

		set_rt_sto(entry, sto);
		validate_rt_entry(entry);
		entry_clr_protected(entry);
	}
	return sto;
}

static unsigned long *dma_get_page_table_origin(unsigned long *entry)
{
	unsigned long *pto;

	if (reg_entry_isvalid(*entry))
		pto = get_st_pto(*entry);
	else {
		pto = dma_alloc_page_table();
		if (!pto)
			return NULL;
		set_st_pto(entry, pto);
		validate_st_entry(entry);
		entry_clr_protected(entry);
	}
	return pto;
}

unsigned long *dma_walk_cpu_trans(unsigned long *rto, dma_addr_t dma_addr)
{
	unsigned long *sto, *pto;
	unsigned int rtx, sx, px;

	rtx = calc_rtx(dma_addr);
	sto = dma_get_seg_table_origin(&rto[rtx]);
	if (!sto)
		return NULL;

	sx = calc_sx(dma_addr);
	pto = dma_get_page_table_origin(&sto[sx]);
	if (!pto)
		return NULL;

	px = calc_px(dma_addr);
	return &pto[px];
}

void dma_update_cpu_trans(unsigned long *entry, void *page_addr, int flags)
{
	if (flags & ZPCI_PTE_INVALID) {
		invalidate_pt_entry(entry);
	} else {
		set_pt_pfaa(entry, page_addr);
		validate_pt_entry(entry);
	}

	if (flags & ZPCI_TABLE_PROTECTED)
		entry_set_protected(entry);
	else
		entry_clr_protected(entry);
}

static int __dma_update_trans(struct zpci_dev *zdev, unsigned long pa,
			      dma_addr_t dma_addr, size_t size, int flags)
{
	unsigned int nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	u8 *page_addr = (u8 *) (pa & PAGE_MASK);
	unsigned long irq_flags;
	unsigned long *entry;
	int i, rc = 0;

	if (!nr_pages)
		return -EINVAL;

	spin_lock_irqsave(&zdev->dma_table_lock, irq_flags);
	if (!zdev->dma_table) {
		rc = -EINVAL;
		goto out_unlock;
	}

	for (i = 0; i < nr_pages; i++) {
		entry = dma_walk_cpu_trans(zdev->dma_table, dma_addr);
		if (!entry) {
			rc = -ENOMEM;
			goto undo_cpu_trans;
		}
		dma_update_cpu_trans(entry, page_addr, flags);
		page_addr += PAGE_SIZE;
		dma_addr += PAGE_SIZE;
	}

undo_cpu_trans:
	if (rc && ((flags & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_VALID)) {
		flags = ZPCI_PTE_INVALID;
		while (i-- > 0) {
			page_addr -= PAGE_SIZE;
			dma_addr -= PAGE_SIZE;
			entry = dma_walk_cpu_trans(zdev->dma_table, dma_addr);
			if (!entry)
				break;
			dma_update_cpu_trans(entry, page_addr, flags);
		}
	}
out_unlock:
	spin_unlock_irqrestore(&zdev->dma_table_lock, irq_flags);
	return rc;
}

static int __dma_purge_tlb(struct zpci_dev *zdev, dma_addr_t dma_addr,
			   size_t size, int flags)
{
	/*
	 * With zdev->tlb_refresh == 0, rpcit is not required to establish new
	 * translations when previously invalid translation-table entries are
	 * validated. With lazy unmap, rpcit is skipped for previously valid
	 * entries, but a global rpcit is then required before any address can
	 * be re-used, i.e. after each iommu bitmap wrap-around.
	 */
	if ((flags & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_VALID) {
		if (!zdev->tlb_refresh)
			return 0;
	} else {
		if (!s390_iommu_strict)
			return 0;
	}

	return zpci_refresh_trans((u64) zdev->fh << 32, dma_addr,
				  PAGE_ALIGN(size));
}

static int dma_update_trans(struct zpci_dev *zdev, unsigned long pa,
			    dma_addr_t dma_addr, size_t size, int flags)
{
	int rc;

	rc = __dma_update_trans(zdev, pa, dma_addr, size, flags);
	if (rc)
		return rc;

	rc = __dma_purge_tlb(zdev, dma_addr, size, flags);
	if (rc && ((flags & ZPCI_PTE_VALID_MASK) == ZPCI_PTE_VALID))
		__dma_update_trans(zdev, pa, dma_addr, size, ZPCI_PTE_INVALID);

	return rc;
}

void dma_free_seg_table(unsigned long entry)
{
	unsigned long *sto = get_rt_sto(entry);
	int sx;

	for (sx = 0; sx < ZPCI_TABLE_ENTRIES; sx++)
		if (reg_entry_isvalid(sto[sx]))
			dma_free_page_table(get_st_pto(sto[sx]));

	dma_free_cpu_table(sto);
}

void dma_cleanup_tables(unsigned long *table)
{
	int rtx;

	if (!table)
		return;

	for (rtx = 0; rtx < ZPCI_TABLE_ENTRIES; rtx++)
		if (reg_entry_isvalid(table[rtx]))
			dma_free_seg_table(table[rtx]);

	dma_free_cpu_table(table);
}

static unsigned long __dma_alloc_iommu(struct device *dev,
				       unsigned long start, int size)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	unsigned long boundary_size;

	boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
			      PAGE_SIZE) >> PAGE_SHIFT;
	return iommu_area_alloc(zdev->iommu_bitmap, zdev->iommu_pages,
				start, size, zdev->start_dma >> PAGE_SHIFT,
				boundary_size, 0);
}

static dma_addr_t dma_alloc_address(struct device *dev, int size)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	unsigned long offset, flags;

	spin_lock_irqsave(&zdev->iommu_bitmap_lock, flags);
	offset = __dma_alloc_iommu(dev, zdev->next_bit, size);
	if (offset == -1) {
		if (!s390_iommu_strict) {
			/* global flush before DMA addresses are reused */
			if (zpci_refresh_global(zdev))
				goto out_error;

			bitmap_andnot(zdev->iommu_bitmap, zdev->iommu_bitmap,
				      zdev->lazy_bitmap, zdev->iommu_pages);
			bitmap_zero(zdev->lazy_bitmap, zdev->iommu_pages);
		}
		/* wrap-around */
		offset = __dma_alloc_iommu(dev, 0, size);
		if (offset == -1)
			goto out_error;
	}
	zdev->next_bit = offset + size;
	spin_unlock_irqrestore(&zdev->iommu_bitmap_lock, flags);

	return zdev->start_dma + offset * PAGE_SIZE;

out_error:
	spin_unlock_irqrestore(&zdev->iommu_bitmap_lock, flags);
	return DMA_ERROR_CODE;
}

static void dma_free_address(struct device *dev, dma_addr_t dma_addr, int size)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	unsigned long flags, offset;

	offset = (dma_addr - zdev->start_dma) >> PAGE_SHIFT;

	spin_lock_irqsave(&zdev->iommu_bitmap_lock, flags);
	if (!zdev->iommu_bitmap)
		goto out;

	if (s390_iommu_strict)
		bitmap_clear(zdev->iommu_bitmap, offset, size);
	else
		bitmap_set(zdev->lazy_bitmap, offset, size);

out:
	spin_unlock_irqrestore(&zdev->iommu_bitmap_lock, flags);
}

static inline void zpci_err_dma(unsigned long rc, unsigned long addr)
{
	struct {
		unsigned long rc;
		unsigned long addr;
	} __packed data = {rc, addr};

	zpci_err_hex(&data, sizeof(data));
}

static dma_addr_t s390_dma_map_pages(struct device *dev, struct page *page,
				     unsigned long offset, size_t size,
				     enum dma_data_direction direction,
				     unsigned long attrs)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	unsigned long pa = page_to_phys(page) + offset;
	int flags = ZPCI_PTE_VALID;
	unsigned long nr_pages;
	dma_addr_t dma_addr;
	int ret;

	/* This rounds up number of pages based on size and offset */
	nr_pages = iommu_num_pages(pa, size, PAGE_SIZE);
	dma_addr = dma_alloc_address(dev, nr_pages);
	if (dma_addr == DMA_ERROR_CODE) {
		ret = -ENOSPC;
		goto out_err;
	}

	/* Use rounded up size */
	size = nr_pages * PAGE_SIZE;

	if (direction == DMA_NONE || direction == DMA_TO_DEVICE)
		flags |= ZPCI_TABLE_PROTECTED;

	ret = dma_update_trans(zdev, pa, dma_addr, size, flags);
	if (ret)
		goto out_free;

	atomic64_add(nr_pages, &zdev->mapped_pages);
	return dma_addr + (offset & ~PAGE_MASK);

out_free:
	dma_free_address(dev, dma_addr, nr_pages);
out_err:
	zpci_err("map error:\n");
	zpci_err_dma(ret, pa);
	return DMA_ERROR_CODE;
}

static void s390_dma_unmap_pages(struct device *dev, dma_addr_t dma_addr,
				 size_t size, enum dma_data_direction direction,
				 unsigned long attrs)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	int npages, ret;

	npages = iommu_num_pages(dma_addr, size, PAGE_SIZE);
	dma_addr = dma_addr & PAGE_MASK;
	ret = dma_update_trans(zdev, 0, dma_addr, npages * PAGE_SIZE,
			       ZPCI_PTE_INVALID);
	if (ret) {
		zpci_err("unmap error:\n");
		zpci_err_dma(ret, dma_addr);
		return;
	}

	atomic64_add(npages, &zdev->unmapped_pages);
	dma_free_address(dev, dma_addr, npages);
}

static void *s390_dma_alloc(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t flag,
			    unsigned long attrs)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	struct page *page;
	unsigned long pa;
	dma_addr_t map;

	size = PAGE_ALIGN(size);
	page = alloc_pages(flag, get_order(size));
	if (!page)
		return NULL;

	pa = page_to_phys(page);
	map = s390_dma_map_pages(dev, page, 0, size, DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(dev, map)) {
		free_pages(pa, get_order(size));
		return NULL;
	}

	atomic64_add(size / PAGE_SIZE, &zdev->allocated_pages);
	if (dma_handle)
		*dma_handle = map;
	return (void *) pa;
}

static void s390_dma_free(struct device *dev, size_t size,
			  void *pa, dma_addr_t dma_handle,
			  unsigned long attrs)
{
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));

	size = PAGE_ALIGN(size);
	atomic64_sub(size / PAGE_SIZE, &zdev->allocated_pages);
	s390_dma_unmap_pages(dev, dma_handle, size, DMA_BIDIRECTIONAL, 0);
	free_pages((unsigned long) pa, get_order(size));
}

/* Map a segment into a contiguous dma address area */
static int __s390_dma_map_sg(struct device *dev, struct scatterlist *sg,
			     size_t size, dma_addr_t *handle,
			     enum dma_data_direction dir)
{
	unsigned long nr_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
	struct zpci_dev *zdev = to_zpci(to_pci_dev(dev));
	dma_addr_t dma_addr_base, dma_addr;
	int flags = ZPCI_PTE_VALID;
	struct scatterlist *s;
	unsigned long pa = 0;
	int ret;

	dma_addr_base = dma_alloc_address(dev, nr_pages);
	if (dma_addr_base == DMA_ERROR_CODE)
		return -ENOMEM;

	dma_addr = dma_addr_base;
	if (dir == DMA_NONE || dir == DMA_TO_DEVICE)
		flags |= ZPCI_TABLE_PROTECTED;

	for (s = sg; dma_addr < dma_addr_base + size; s = sg_next(s)) {
		pa = page_to_phys(sg_page(s));
		ret = __dma_update_trans(zdev, pa, dma_addr,
					 s->offset + s->length, flags);
		if (ret)
			goto unmap;

		dma_addr += s->offset + s->length;
	}
	ret = __dma_purge_tlb(zdev, dma_addr_base, size, flags);
	if (ret)
		goto unmap;

	*handle = dma_addr_base;
	atomic64_add(nr_pages, &zdev->mapped_pages);

	return ret;

unmap:
	dma_update_trans(zdev, 0, dma_addr_base, dma_addr - dma_addr_base,
			 ZPCI_PTE_INVALID);
	dma_free_address(dev, dma_addr_base, nr_pages);
	zpci_err("map error:\n");
	zpci_err_dma(ret, pa);
	return ret;
}

static int s390_dma_map_sg(struct device *dev, struct scatterlist *sg,
			   int nr_elements, enum dma_data_direction dir,
			   unsigned long attrs)
{
	struct scatterlist *s = sg, *start = sg, *dma = sg;
	unsigned int max = dma_get_max_seg_size(dev);
	unsigned int size = s->offset + s->length;
	unsigned int offset = s->offset;
	int count = 0, i;

	for (i = 1; i < nr_elements; i++) {
		s = sg_next(s);

		s->dma_address = DMA_ERROR_CODE;
		s->dma_length = 0;

		if (s->offset || (size & ~PAGE_MASK) ||
		    size + s->length > max) {
			if (__s390_dma_map_sg(dev, start, size,
					      &dma->dma_address, dir))
				goto unmap;

			dma->dma_address += offset;
			dma->dma_length = size - offset;

			size = offset = s->offset;
			start = s;
			dma = sg_next(dma);
			count++;
		}
		size += s->length;
	}
	if (__s390_dma_map_sg(dev, start, size, &dma->dma_address, dir))
		goto unmap;

	dma->dma_address += offset;
	dma->dma_length = size - offset;

	return count + 1;
unmap:
	for_each_sg(sg, s, count, i)
		s390_dma_unmap_pages(dev, sg_dma_address(s), sg_dma_len(s),
				     dir, attrs);

	return 0;
}

static void s390_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
			      int nr_elements, enum dma_data_direction dir,
			      unsigned long attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nr_elements, i) {
		if (s->dma_length)
			s390_dma_unmap_pages(dev, s->dma_address, s->dma_length,
					     dir, attrs);
		s->dma_address = 0;
		s->dma_length = 0;
	}
}

int zpci_dma_init_device(struct zpci_dev *zdev)
{
	int rc;

	/*
	 * At this point, if the device is part of an IOMMU domain, this would
	 * be a strong hint towards a bug in the IOMMU API (common) code and/or
	 * simultaneous access via IOMMU and DMA API. So let's issue a warning.
	 */
	WARN_ON(zdev->s390_domain);

	spin_lock_init(&zdev->iommu_bitmap_lock);
	spin_lock_init(&zdev->dma_table_lock);

	zdev->dma_table = dma_alloc_cpu_table();
	if (!zdev->dma_table) {
		rc = -ENOMEM;
		goto out;
	}

	/*
	 * Restrict the iommu bitmap size to the minimum of the following:
	 * - main memory size
	 * - 3-level pagetable address limit minus start_dma offset
	 * - DMA address range allowed by the hardware (clp query pci fn)
	 *
	 * Also set zdev->end_dma to the actual end address of the usable
	 * range, instead of the theoretical maximum as reported by hardware.
	 */
	zdev->start_dma = PAGE_ALIGN(zdev->start_dma);
	zdev->iommu_size = min3((u64) high_memory,
				ZPCI_TABLE_SIZE_RT - zdev->start_dma,
				zdev->end_dma - zdev->start_dma + 1);
	zdev->end_dma = zdev->start_dma + zdev->iommu_size - 1;
	zdev->iommu_pages = zdev->iommu_size >> PAGE_SHIFT;
	zdev->iommu_bitmap = vzalloc(zdev->iommu_pages / 8);
	if (!zdev->iommu_bitmap) {
		rc = -ENOMEM;
		goto free_dma_table;
	}
	if (!s390_iommu_strict) {
		zdev->lazy_bitmap = vzalloc(zdev->iommu_pages / 8);
		if (!zdev->lazy_bitmap) {
			rc = -ENOMEM;
			goto free_bitmap;
		}

	}
	rc = zpci_register_ioat(zdev, 0, zdev->start_dma, zdev->end_dma,
				(u64) zdev->dma_table);
	if (rc)
		goto free_bitmap;

	return 0;
free_bitmap:
	vfree(zdev->iommu_bitmap);
	zdev->iommu_bitmap = NULL;
	vfree(zdev->lazy_bitmap);
	zdev->lazy_bitmap = NULL;
free_dma_table:
	dma_free_cpu_table(zdev->dma_table);
	zdev->dma_table = NULL;
out:
	return rc;
}

void zpci_dma_exit_device(struct zpci_dev *zdev)
{
	/*
	 * At this point, if the device is part of an IOMMU domain, this would
	 * be a strong hint towards a bug in the IOMMU API (common) code and/or
	 * simultaneous access via IOMMU and DMA API. So let's issue a warning.
	 */
	WARN_ON(zdev->s390_domain);

	zpci_unregister_ioat(zdev, 0);
	dma_cleanup_tables(zdev->dma_table);
	zdev->dma_table = NULL;
	vfree(zdev->iommu_bitmap);
	zdev->iommu_bitmap = NULL;
	vfree(zdev->lazy_bitmap);
	zdev->lazy_bitmap = NULL;

	zdev->next_bit = 0;
}

static int __init dma_alloc_cpu_table_caches(void)
{
	dma_region_table_cache = kmem_cache_create("PCI_DMA_region_tables",
					ZPCI_TABLE_SIZE, ZPCI_TABLE_ALIGN,
					0, NULL);
	if (!dma_region_table_cache)
		return -ENOMEM;

	dma_page_table_cache = kmem_cache_create("PCI_DMA_page_tables",
					ZPCI_PT_SIZE, ZPCI_PT_ALIGN,
					0, NULL);
	if (!dma_page_table_cache) {
		kmem_cache_destroy(dma_region_table_cache);
		return -ENOMEM;
	}
	return 0;
}

int __init zpci_dma_init(void)
{
	return dma_alloc_cpu_table_caches();
}

void zpci_dma_exit(void)
{
	kmem_cache_destroy(dma_page_table_cache);
	kmem_cache_destroy(dma_region_table_cache);
}

#define PREALLOC_DMA_DEBUG_ENTRIES	(1 << 16)

static int __init dma_debug_do_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
fs_initcall(dma_debug_do_init);

struct dma_map_ops s390_pci_dma_ops = {
	.alloc		= s390_dma_alloc,
	.free		= s390_dma_free,
	.map_sg		= s390_dma_map_sg,
	.unmap_sg	= s390_dma_unmap_sg,
	.map_page	= s390_dma_map_pages,
	.unmap_page	= s390_dma_unmap_pages,
	/* if we support direct DMA this must be conditional */
	.is_phys	= 0,
	/* dma_supported is unconditionally true without a callback */
};
EXPORT_SYMBOL_GPL(s390_pci_dma_ops);

static int __init s390_iommu_setup(char *str)
{
	if (!strncmp(str, "strict", 6))
		s390_iommu_strict = 1;
	return 0;
}

__setup("s390_iommu=", s390_iommu_setup);

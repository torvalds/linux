/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * Rewrite, cleanup:
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _ASM_IOMMU_H
#define _ASM_IOMMU_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <asm/machdep.h>
#include <asm/types.h>

#define IOMMU_PAGE_SHIFT_4K      12
#define IOMMU_PAGE_SIZE_4K       (ASM_CONST(1) << IOMMU_PAGE_SHIFT_4K)
#define IOMMU_PAGE_MASK_4K       (~((1 << IOMMU_PAGE_SHIFT_4K) - 1))
#define IOMMU_PAGE_ALIGN_4K(addr) _ALIGN_UP(addr, IOMMU_PAGE_SIZE_4K)

#define IOMMU_PAGE_SIZE(tblptr) (ASM_CONST(1) << (tblptr)->it_page_shift)
#define IOMMU_PAGE_MASK(tblptr) (~((1 << (tblptr)->it_page_shift) - 1))
#define IOMMU_PAGE_ALIGN(addr, tblptr) _ALIGN_UP(addr, IOMMU_PAGE_SIZE(tblptr))

/* Boot time flags */
extern int iommu_is_off;
extern int iommu_force_on;

/*
 * IOMAP_MAX_ORDER defines the largest contiguous block
 * of dma space we can get.  IOMAP_MAX_ORDER = 13
 * allows up to 2**12 pages (4096 * 4096) = 16 MB
 */
#define IOMAP_MAX_ORDER		13

#define IOMMU_POOL_HASHBITS	2
#define IOMMU_NR_POOLS		(1 << IOMMU_POOL_HASHBITS)

struct iommu_pool {
	unsigned long start;
	unsigned long end;
	unsigned long hint;
	spinlock_t lock;
} ____cacheline_aligned_in_smp;

struct iommu_table {
	unsigned long  it_busno;     /* Bus number this table belongs to */
	unsigned long  it_size;      /* Size of iommu table in entries */
	unsigned long  it_offset;    /* Offset into global table */
	unsigned long  it_base;      /* mapped address of tce table */
	unsigned long  it_index;     /* which iommu table this is */
	unsigned long  it_type;      /* type: PCI or Virtual Bus */
	unsigned long  it_blocksize; /* Entries in each block (cacheline) */
	unsigned long  poolsize;
	unsigned long  nr_pools;
	struct iommu_pool large_pool;
	struct iommu_pool pools[IOMMU_NR_POOLS];
	unsigned long *it_map;       /* A simple allocation bitmap for now */
	unsigned long  it_page_shift;/* table iommu page size */
#ifdef CONFIG_IOMMU_API
	struct iommu_group *it_group;
#endif
};

/* Pure 2^n version of get_order */
static inline __attribute_const__
int get_iommu_order(unsigned long size, struct iommu_table *tbl)
{
	return __ilog2((size - 1) >> tbl->it_page_shift) + 1;
}


struct scatterlist;

static inline void set_iommu_table_base(struct device *dev, void *base)
{
	dev->archdata.dma_data.iommu_table_base = base;
}

static inline void *get_iommu_table_base(struct device *dev)
{
	return dev->archdata.dma_data.iommu_table_base;
}

/* Frees table for an individual device node */
extern void iommu_free_table(struct iommu_table *tbl, const char *node_name);

/* Initializes an iommu_table based in values set in the passed-in
 * structure
 */
extern struct iommu_table *iommu_init_table(struct iommu_table * tbl,
					    int nid);
#ifdef CONFIG_IOMMU_API
extern void iommu_register_group(struct iommu_table *tbl,
				 int pci_domain_number, unsigned long pe_num);
extern int iommu_add_device(struct device *dev);
extern void iommu_del_device(struct device *dev);
#else
static inline void iommu_register_group(struct iommu_table *tbl,
					int pci_domain_number,
					unsigned long pe_num)
{
}

static inline int iommu_add_device(struct device *dev)
{
	return 0;
}

static inline void iommu_del_device(struct device *dev)
{
}
#endif /* !CONFIG_IOMMU_API */

static inline void set_iommu_table_base_and_group(struct device *dev,
						  void *base)
{
	set_iommu_table_base(dev, base);
	iommu_add_device(dev);
}

extern int iommu_map_sg(struct device *dev, struct iommu_table *tbl,
			struct scatterlist *sglist, int nelems,
			unsigned long mask, enum dma_data_direction direction,
			struct dma_attrs *attrs);
extern void iommu_unmap_sg(struct iommu_table *tbl, struct scatterlist *sglist,
			   int nelems, enum dma_data_direction direction,
			   struct dma_attrs *attrs);

extern void *iommu_alloc_coherent(struct device *dev, struct iommu_table *tbl,
				  size_t size, dma_addr_t *dma_handle,
				  unsigned long mask, gfp_t flag, int node);
extern void iommu_free_coherent(struct iommu_table *tbl, size_t size,
				void *vaddr, dma_addr_t dma_handle);
extern dma_addr_t iommu_map_page(struct device *dev, struct iommu_table *tbl,
				 struct page *page, unsigned long offset,
				 size_t size, unsigned long mask,
				 enum dma_data_direction direction,
				 struct dma_attrs *attrs);
extern void iommu_unmap_page(struct iommu_table *tbl, dma_addr_t dma_handle,
			     size_t size, enum dma_data_direction direction,
			     struct dma_attrs *attrs);

extern void iommu_init_early_pSeries(void);
extern void iommu_init_early_dart(void);
extern void iommu_init_early_pasemi(void);

extern void alloc_dart_table(void);
#if defined(CONFIG_PPC64) && defined(CONFIG_PM)
static inline void iommu_save(void)
{
	if (ppc_md.iommu_save)
		ppc_md.iommu_save();
}

static inline void iommu_restore(void)
{
	if (ppc_md.iommu_restore)
		ppc_md.iommu_restore();
}
#endif

/* The API to support IOMMU operations for VFIO */
extern int iommu_tce_clear_param_check(struct iommu_table *tbl,
		unsigned long ioba, unsigned long tce_value,
		unsigned long npages);
extern int iommu_tce_put_param_check(struct iommu_table *tbl,
		unsigned long ioba, unsigned long tce);
extern int iommu_tce_build(struct iommu_table *tbl, unsigned long entry,
		unsigned long hwaddr, enum dma_data_direction direction);
extern unsigned long iommu_clear_tce(struct iommu_table *tbl,
		unsigned long entry);
extern int iommu_clear_tces_and_put_pages(struct iommu_table *tbl,
		unsigned long entry, unsigned long pages);
extern int iommu_put_tce_user_mode(struct iommu_table *tbl,
		unsigned long entry, unsigned long tce);

extern void iommu_flush_tce(struct iommu_table *tbl);
extern int iommu_take_ownership(struct iommu_table *tbl);
extern void iommu_release_ownership(struct iommu_table *tbl);

extern enum dma_data_direction iommu_tce_direction(unsigned long tce);

#endif /* __KERNEL__ */
#endif /* _ASM_IOMMU_H */

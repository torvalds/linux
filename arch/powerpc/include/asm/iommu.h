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

#define IOMMU_PAGE_SHIFT      12
#define IOMMU_PAGE_SIZE       (ASM_CONST(1) << IOMMU_PAGE_SHIFT)
#define IOMMU_PAGE_MASK       (~((1 << IOMMU_PAGE_SHIFT) - 1))
#define IOMMU_PAGE_ALIGN(addr) _ALIGN_UP(addr, IOMMU_PAGE_SIZE)

/* Boot time flags */
extern int iommu_is_off;
extern int iommu_force_on;

/* Pure 2^n version of get_order */
static __inline__ __attribute_const__ int get_iommu_order(unsigned long size)
{
	return __ilog2((size - 1) >> IOMMU_PAGE_SHIFT) + 1;
}


/*
 * IOMAP_MAX_ORDER defines the largest contiguous block
 * of dma space we can get.  IOMAP_MAX_ORDER = 13
 * allows up to 2**12 pages (4096 * 4096) = 16 MB
 */
#define IOMAP_MAX_ORDER		13

struct iommu_table {
	unsigned long  it_busno;     /* Bus number this table belongs to */
	unsigned long  it_size;      /* Size of iommu table in entries */
	unsigned long  it_offset;    /* Offset into global table */
	unsigned long  it_base;      /* mapped address of tce table */
	unsigned long  it_index;     /* which iommu table this is */
	unsigned long  it_type;      /* type: PCI or Virtual Bus */
	unsigned long  it_blocksize; /* Entries in each block (cacheline) */
	unsigned long  it_hint;      /* Hint for next alloc */
	unsigned long  it_largehint; /* Hint for large allocs */
	unsigned long  it_halfpoint; /* Breaking point for small/large allocs */
	spinlock_t     it_lock;      /* Protects it_map */
	unsigned long *it_map;       /* A simple allocation bitmap for now */
};

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
extern void iommu_init_early_iSeries(void);
extern void iommu_init_early_dart(void);
extern void iommu_init_early_pasemi(void);

#ifdef CONFIG_PCI
extern void pci_iommu_init(void);
extern void pci_direct_iommu_init(void);
#else
static inline void pci_iommu_init(void) { }
#endif

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

#endif /* __KERNEL__ */
#endif /* _ASM_IOMMU_H */

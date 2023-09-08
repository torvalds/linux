/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * Rewrite, cleanup:
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 */

#ifndef _ASM_IOMMU_H
#define _ASM_IOMMU_H
#ifdef __KERNEL__

#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/dma-map-ops.h>
#include <linux/bitops.h>
#include <asm/machdep.h>
#include <asm/types.h>
#include <asm/pci-bridge.h>
#include <asm/asm-const.h>

#define IOMMU_PAGE_SHIFT_4K      12
#define IOMMU_PAGE_SIZE_4K       (ASM_CONST(1) << IOMMU_PAGE_SHIFT_4K)
#define IOMMU_PAGE_MASK_4K       (~((1 << IOMMU_PAGE_SHIFT_4K) - 1))
#define IOMMU_PAGE_ALIGN_4K(addr) ALIGN(addr, IOMMU_PAGE_SIZE_4K)

#define IOMMU_PAGE_SIZE(tblptr) (ASM_CONST(1) << (tblptr)->it_page_shift)
#define IOMMU_PAGE_MASK(tblptr) (~((1 << (tblptr)->it_page_shift) - 1))
#define IOMMU_PAGE_ALIGN(addr, tblptr) ALIGN(addr, IOMMU_PAGE_SIZE(tblptr))

/* Boot time flags */
extern int iommu_is_off;
extern int iommu_force_on;

struct iommu_table_ops {
	/*
	 * When called with direction==DMA_NONE, it is equal to clear().
	 * uaddr is a linear map address.
	 */
	int (*set)(struct iommu_table *tbl,
			long index, long npages,
			unsigned long uaddr,
			enum dma_data_direction direction,
			unsigned long attrs);
#ifdef CONFIG_IOMMU_API
	/*
	 * Exchanges existing TCE with new TCE plus direction bits;
	 * returns old TCE and DMA direction mask.
	 * @tce is a physical address.
	 */
	int (*xchg_no_kill)(struct iommu_table *tbl,
			long index,
			unsigned long *hpa,
			enum dma_data_direction *direction);

	void (*tce_kill)(struct iommu_table *tbl,
			unsigned long index,
			unsigned long pages);

	__be64 *(*useraddrptr)(struct iommu_table *tbl, long index, bool alloc);
#endif
	void (*clear)(struct iommu_table *tbl,
			long index, long npages);
	/* get() returns a physical address */
	unsigned long (*get)(struct iommu_table *tbl, long index);
	void (*flush)(struct iommu_table *tbl);
	void (*free)(struct iommu_table *tbl);
};

/* These are used by VIO */
extern struct iommu_table_ops iommu_table_lpar_multi_ops;
extern struct iommu_table_ops iommu_table_pseries_ops;

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
	unsigned long  it_indirect_levels;
	unsigned long  it_level_size;
	unsigned long  it_allocated_size;
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
	struct list_head it_group_list;/* List of iommu_table_group_link */
	__be64 *it_userspace; /* userspace view of the table */
	struct iommu_table_ops *it_ops;
	struct kref    it_kref;
	int it_nid;
	unsigned long it_reserved_start; /* Start of not-DMA-able (MMIO) area */
	unsigned long it_reserved_end;
};

#define IOMMU_TABLE_USERSPACE_ENTRY_RO(tbl, entry) \
		((tbl)->it_ops->useraddrptr((tbl), (entry), false))
#define IOMMU_TABLE_USERSPACE_ENTRY(tbl, entry) \
		((tbl)->it_ops->useraddrptr((tbl), (entry), true))

/* Pure 2^n version of get_order */
static inline __attribute_const__
int get_iommu_order(unsigned long size, struct iommu_table *tbl)
{
	return __ilog2((size - 1) >> tbl->it_page_shift) + 1;
}


struct scatterlist;

#ifdef CONFIG_PPC64

static inline void set_iommu_table_base(struct device *dev,
					struct iommu_table *base)
{
	dev->archdata.iommu_table_base = base;
}

static inline void *get_iommu_table_base(struct device *dev)
{
	return dev->archdata.iommu_table_base;
}

extern int dma_iommu_dma_supported(struct device *dev, u64 mask);

extern struct iommu_table *iommu_tce_table_get(struct iommu_table *tbl);
extern int iommu_tce_table_put(struct iommu_table *tbl);

/* Initializes an iommu_table based in values set in the passed-in
 * structure
 */
extern struct iommu_table *iommu_init_table(struct iommu_table *tbl,
		int nid, unsigned long res_start, unsigned long res_end);
bool iommu_table_in_use(struct iommu_table *tbl);

#define IOMMU_TABLE_GROUP_MAX_TABLES	2

struct iommu_table_group;

struct iommu_table_group_ops {
	unsigned long (*get_table_size)(
			__u32 page_shift,
			__u64 window_size,
			__u32 levels);
	long (*create_table)(struct iommu_table_group *table_group,
			int num,
			__u32 page_shift,
			__u64 window_size,
			__u32 levels,
			struct iommu_table **ptbl);
	long (*set_window)(struct iommu_table_group *table_group,
			int num,
			struct iommu_table *tblnew);
	long (*unset_window)(struct iommu_table_group *table_group,
			int num);
	/* Switch ownership from platform code to external user (e.g. VFIO) */
	long (*take_ownership)(struct iommu_table_group *table_group);
	/* Switch ownership from external user (e.g. VFIO) back to core */
	void (*release_ownership)(struct iommu_table_group *table_group);
};

struct iommu_table_group_link {
	struct list_head next;
	struct rcu_head rcu;
	struct iommu_table_group *table_group;
};

struct iommu_table_group {
	/* IOMMU properties */
	__u32 tce32_start;
	__u32 tce32_size;
	__u64 pgsizes; /* Bitmap of supported page sizes */
	__u32 max_dynamic_windows_supported;
	__u32 max_levels;

	struct iommu_group *group;
	struct iommu_table *tables[IOMMU_TABLE_GROUP_MAX_TABLES];
	struct iommu_table_group_ops *ops;
};

#ifdef CONFIG_IOMMU_API

extern void iommu_register_group(struct iommu_table_group *table_group,
				 int pci_domain_number, unsigned long pe_num);
extern int iommu_add_device(struct iommu_table_group *table_group,
		struct device *dev);
extern long iommu_tce_xchg(struct mm_struct *mm, struct iommu_table *tbl,
		unsigned long entry, unsigned long *hpa,
		enum dma_data_direction *direction);
extern long iommu_tce_xchg_no_kill(struct mm_struct *mm,
		struct iommu_table *tbl,
		unsigned long entry, unsigned long *hpa,
		enum dma_data_direction *direction);
extern void iommu_tce_kill(struct iommu_table *tbl,
		unsigned long entry, unsigned long pages);

extern struct iommu_table_group_ops spapr_tce_table_group_ops;
#else
static inline void iommu_register_group(struct iommu_table_group *table_group,
					int pci_domain_number,
					unsigned long pe_num)
{
}

static inline int iommu_add_device(struct iommu_table_group *table_group,
		struct device *dev)
{
	return 0;
}
#endif /* !CONFIG_IOMMU_API */

u64 dma_iommu_get_required_mask(struct device *dev);
#else

static inline void *get_iommu_table_base(struct device *dev)
{
	return NULL;
}

static inline int dma_iommu_dma_supported(struct device *dev, u64 mask)
{
	return 0;
}

#endif /* CONFIG_PPC64 */

extern int ppc_iommu_map_sg(struct device *dev, struct iommu_table *tbl,
			    struct scatterlist *sglist, int nelems,
			    unsigned long mask,
			    enum dma_data_direction direction,
			    unsigned long attrs);
extern void ppc_iommu_unmap_sg(struct iommu_table *tbl,
			       struct scatterlist *sglist,
			       int nelems,
			       enum dma_data_direction direction,
			       unsigned long attrs);

extern void *iommu_alloc_coherent(struct device *dev, struct iommu_table *tbl,
				  size_t size, dma_addr_t *dma_handle,
				  unsigned long mask, gfp_t flag, int node);
extern void iommu_free_coherent(struct iommu_table *tbl, size_t size,
				void *vaddr, dma_addr_t dma_handle);
extern dma_addr_t iommu_map_page(struct device *dev, struct iommu_table *tbl,
				 struct page *page, unsigned long offset,
				 size_t size, unsigned long mask,
				 enum dma_data_direction direction,
				 unsigned long attrs);
extern void iommu_unmap_page(struct iommu_table *tbl, dma_addr_t dma_handle,
			     size_t size, enum dma_data_direction direction,
			     unsigned long attrs);

void __init iommu_init_early_pSeries(void);
extern void iommu_init_early_dart(struct pci_controller_ops *controller_ops);
extern void iommu_init_early_pasemi(void);

#if defined(CONFIG_PPC64) && defined(CONFIG_PM)
static inline void iommu_restore(void)
{
	if (ppc_md.iommu_restore)
		ppc_md.iommu_restore();
}
#endif

/* The API to support IOMMU operations for VFIO */
extern int iommu_tce_check_ioba(unsigned long page_shift,
		unsigned long offset, unsigned long size,
		unsigned long ioba, unsigned long npages);
extern int iommu_tce_check_gpa(unsigned long page_shift,
		unsigned long gpa);

#define iommu_tce_clear_param_check(tbl, ioba, tce_value, npages) \
		(iommu_tce_check_ioba((tbl)->it_page_shift,       \
				(tbl)->it_offset, (tbl)->it_size, \
				(ioba), (npages)) || (tce_value))
#define iommu_tce_put_param_check(tbl, ioba, gpa)                 \
		(iommu_tce_check_ioba((tbl)->it_page_shift,       \
				(tbl)->it_offset, (tbl)->it_size, \
				(ioba), 1) ||                     \
		iommu_tce_check_gpa((tbl)->it_page_shift, (gpa)))

extern void iommu_flush_tce(struct iommu_table *tbl);

extern enum dma_data_direction iommu_tce_direction(unsigned long tce);
extern unsigned long iommu_direction_to_tce_perm(enum dma_data_direction dir);

#ifdef CONFIG_PPC_CELL_NATIVE
extern bool iommu_fixed_is_weak;
#else
#define iommu_fixed_is_weak false
#endif

extern const struct dma_map_ops dma_iommu_ops;

#endif /* __KERNEL__ */
#endif /* _ASM_IOMMU_H */

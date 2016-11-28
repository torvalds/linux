/* iommu.h: Definitions for the sun5 IOMMU.
 *
 * Copyright (C) 1996, 1999, 2007 David S. Miller (davem@davemloft.net)
 */
#ifndef _SPARC64_IOMMU_H
#define _SPARC64_IOMMU_H

/* The format of an iopte in the page tables. */
#define IOPTE_VALID   0x8000000000000000UL
#define IOPTE_64K     0x2000000000000000UL
#define IOPTE_STBUF   0x1000000000000000UL
#define IOPTE_INTRA   0x0800000000000000UL
#define IOPTE_CONTEXT 0x07ff800000000000UL
#define IOPTE_PAGE    0x00007fffffffe000UL
#define IOPTE_CACHE   0x0000000000000010UL
#define IOPTE_WRITE   0x0000000000000002UL

#define IOMMU_NUM_CTXS	4096
#include <linux/iommu-common.h>

struct iommu_arena {
	unsigned long	*map;
	unsigned int	hint;
	unsigned int	limit;
};

#define ATU_64_SPACE_SIZE 0x800000000 /* 32G */

/* Data structures for SPARC ATU architecture */
struct atu_iotsb {
	void	*table;		/* IOTSB table base virtual addr*/
	u64	ra;		/* IOTSB table real addr */
	u64	dvma_size;	/* ranges[3].size or OS slected 32G size */
	u64	dvma_base;	/* ranges[3].base */
	u64	table_size;	/* IOTSB table size */
	u64	page_size;	/* IO PAGE size for IOTSB */
	u32	iotsb_num;	/* tsbnum is same as iotsb_handle */
};

struct atu_ranges {
	u64	base;
	u64	size;
};

struct atu {
	struct	atu_ranges	*ranges;
	struct	atu_iotsb	*iotsb;
	struct	iommu_map_table	tbl;
	u64			base;
	u64			size;
	u64			dma_addr_mask;
};

struct iommu {
	struct iommu_map_table	tbl;
	struct atu		*atu;
	spinlock_t		lock;
	u32			dma_addr_mask;
	iopte_t			*page_table;
	unsigned long		iommu_control;
	unsigned long		iommu_tsbbase;
	unsigned long		iommu_flush;
	unsigned long		iommu_flushinv;
	unsigned long		iommu_tags;
	unsigned long		iommu_ctxflush;
	unsigned long		write_complete_reg;
	unsigned long		dummy_page;
	unsigned long		dummy_page_pa;
	unsigned long		ctx_lowest_free;
	DECLARE_BITMAP(ctx_bitmap, IOMMU_NUM_CTXS);
};

struct strbuf {
	int			strbuf_enabled;
	unsigned long		strbuf_control;
	unsigned long		strbuf_pflush;
	unsigned long		strbuf_fsync;
	unsigned long		strbuf_err_stat;
	unsigned long		strbuf_tag_diag;
	unsigned long		strbuf_line_diag;
	unsigned long		strbuf_ctxflush;
	unsigned long		strbuf_ctxmatch_base;
	unsigned long		strbuf_flushflag_pa;
	volatile unsigned long *strbuf_flushflag;
	volatile unsigned long	__flushflag_buf[(64+(64-1)) / sizeof(long)];
};

int iommu_table_init(struct iommu *iommu, int tsbsize,
		     u32 dma_offset, u32 dma_addr_mask,
		     int numa_node);

#endif /* !(_SPARC64_IOMMU_H) */

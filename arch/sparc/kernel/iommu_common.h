/* SPDX-License-Identifier: GPL-2.0 */
/* iommu_common.h: UltraSparc SBUS/PCI common iommu declarations.
 *
 * Copyright (C) 1999, 2008 David S. Miller (davem@davemloft.net)
 */

#ifndef _IOMMU_COMMON_H
#define _IOMMU_COMMON_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/iommu-helper.h>

#include <asm/iommu.h>

/*
 * These give mapping size of each iommu pte/tlb.
 */
#define IO_PAGE_SHIFT			13
#define IO_PAGE_SIZE			(1UL << IO_PAGE_SHIFT)
#define IO_PAGE_MASK			(~(IO_PAGE_SIZE-1))
#define IO_PAGE_ALIGN(addr)		ALIGN(addr, IO_PAGE_SIZE)

#define IO_TSB_ENTRIES			(128*1024)
#define IO_TSB_SIZE			(IO_TSB_ENTRIES * 8)

/*
 * This is the hardwired shift in the iotlb tag/data parts.
 */
#define IOMMU_PAGE_SHIFT		13

#define SG_ENT_PHYS_ADDRESS(SG)	(__pa(sg_virt((SG))))

static inline int is_span_boundary(unsigned long entry,
				   unsigned long shift,
				   unsigned long boundary_size,
				   struct scatterlist *outs,
				   struct scatterlist *sg)
{
	unsigned long paddr = SG_ENT_PHYS_ADDRESS(outs);
	int nr = iommu_num_pages(paddr, outs->dma_length + sg->length,
				 IO_PAGE_SIZE);

	return iommu_is_span_boundary(entry, nr, shift, boundary_size);
}

#endif /* _IOMMU_COMMON_H */

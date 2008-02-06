/* $Id: iommu_common.h,v 1.5 2001/12/11 09:41:01 davem Exp $
 * iommu_common.h: UltraSparc SBUS/PCI common iommu declarations.
 *
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/device.h>

#include <asm/iommu.h>
#include <asm/scatterlist.h>

/*
 * These give mapping size of each iommu pte/tlb.
 */
#define IO_PAGE_SHIFT			13
#define IO_PAGE_SIZE			(1UL << IO_PAGE_SHIFT)
#define IO_PAGE_MASK			(~(IO_PAGE_SIZE-1))
#define IO_PAGE_ALIGN(addr)		(((addr)+IO_PAGE_SIZE-1)&IO_PAGE_MASK)

#define IO_TSB_ENTRIES			(128*1024)
#define IO_TSB_SIZE			(IO_TSB_ENTRIES * 8)

/*
 * This is the hardwired shift in the iotlb tag/data parts.
 */
#define IOMMU_PAGE_SHIFT		13

/* You are _strongly_ advised to enable the following debugging code
 * any time you make changes to the sg code below, run it for a while
 * with filesystems mounted read-only before buying the farm... -DaveM
 */
#undef VERIFY_SG

#ifdef VERIFY_SG
extern void verify_sglist(struct scatterlist *sg, int nents, iopte_t *iopte, int npages);
#endif

/* Two addresses are "virtually contiguous" if and only if:
 * 1) They are equal, or...
 * 2) They are both on a page boundary
 */
#define VCONTIG(__X, __Y)	(((__X) == (__Y)) || \
				 (((__X) | (__Y)) << (64UL - PAGE_SHIFT)) == 0UL)

extern unsigned long prepare_sg(struct device *dev, struct scatterlist *sg, int nents);

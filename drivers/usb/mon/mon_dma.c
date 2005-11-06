/*
 * The USB Monitor, inspired by Dave Harding's USBMon.
 *
 * mon_dma.c: Library which snoops on DMA areas.
 *
 * Copyright (C) 2005 Pete Zaitcev (zaitcev@redhat.com)
 */
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include <asm/page.h>

#include <linux/usb.h>	/* Only needed for declarations in usb_mon.h */
#include "usb_mon.h"

#ifdef __i386__		/* CONFIG_ARCH_I386 does not exit */
#define MON_HAS_UNMAP 1

#define phys_to_page(phys)	pfn_to_page((phys) >> PAGE_SHIFT)

char mon_dmapeek(unsigned char *dst, dma_addr_t dma_addr, int len)
{
	struct page *pg;
	unsigned long flags;
	unsigned char *map;
	unsigned char *ptr;

	/*
	 * On i386, a DMA handle is the "physical" address of a page.
	 * In other words, the bus address is equal to physical address.
	 * There is no IOMMU.
	 */
	pg = phys_to_page(dma_addr);

	/*
	 * We are called from hardware IRQs in case of callbacks.
	 * But we can be called from softirq or process context in case
	 * of submissions. In such case, we need to protect KM_IRQ0.
	 */
	local_irq_save(flags);
	map = kmap_atomic(pg, KM_IRQ0);
	ptr = map + (dma_addr & (PAGE_SIZE-1));
	memcpy(dst, ptr, len);
	kunmap_atomic(map, KM_IRQ0);
	local_irq_restore(flags);
	return 0;
}
#endif /* __i386__ */

#ifndef MON_HAS_UNMAP
char mon_dmapeek(unsigned char *dst, dma_addr_t dma_addr, int len)
{
	return 'D';
}
#endif

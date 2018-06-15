// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-direct.h>
#include <linux/init.h>
#include <linux/swiotlb.h>

dma_addr_t __phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	long nid;
#ifdef CONFIG_PHYS48_TO_HT40
	/* We extract 2bit node id (bit 44~47, only bit 44~45 used now) from
	 * Loongson-3's 48bit address space and embed it into 40bit */
	nid = (paddr >> 44) & 0x3;
	paddr = ((nid << 44) ^ paddr) | (nid << 37);
#endif
	return paddr;
}

phys_addr_t __dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	long nid;
#ifdef CONFIG_PHYS48_TO_HT40
	/* We extract 2bit node id (bit 44~47, only bit 44~45 used now) from
	 * Loongson-3's 48bit address space and embed it into 40bit */
	nid = (daddr >> 37) & 0x3;
	daddr = ((nid << 37) ^ daddr) | (nid << 44);
#endif
	return daddr;
}

void __init plat_swiotlb_setup(void)
{
	swiotlb_init(1);
}

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/swiotlb.h>

#include <asm/bootinfo.h>
#include <asm/dma.h>
#include <asm/loongson.h>

/*
 * We extract 4bit node id (bit 44~47) from Loongson-3's
 * 48bit physical address space and embed it into 40bit.
 */

static int node_id_offset;

dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	long nid = (paddr >> 44) & 0xf;

	return ((nid << 44) ^ paddr) | (nid << node_id_offset);
}

phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	long nid = (daddr >> node_id_offset) & 0xf;

	return ((nid << node_id_offset) ^ daddr) | (nid << 44);
}

void __init plat_swiotlb_setup(void)
{
	swiotlb_init(true, SWIOTLB_VERBOSE);
	node_id_offset = ((readl(LS7A_DMA_CFG) & LS7A_DMA_NODE_MASK) >> LS7A_DMA_NODE_SHF) + 36;
}

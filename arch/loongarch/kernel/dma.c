// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/acpi.h>
#include <linux/dma-direct.h>

void acpi_arch_dma_setup(struct device *dev)
{
	int ret;
	u64 mask, end;
	const struct bus_dma_region *map = NULL;

	ret = acpi_dma_get_range(dev, &map);
	if (!ret && map) {
		end = dma_range_map_max(map);

		mask = DMA_BIT_MASK(ilog2(end) + 1);
		dev->bus_dma_limit = end;
		dev->dma_range_map = map;
		dev->coherent_dma_mask = min(dev->coherent_dma_mask, mask);
		*dev->dma_mask = min(*dev->dma_mask, mask);
	}

}

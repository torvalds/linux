// SPDX-License-Identifier: GPL-2.0-only
#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/device.h>
#include <linux/dma-direct.h>

void acpi_arch_dma_setup(struct device *dev)
{
	int ret;
	u64 end, mask;
	const struct bus_dma_region *map = NULL;

	/*
	 * If @dev is expected to be DMA-capable then the bus code that created
	 * it should have initialised its dma_mask pointer by this point. For
	 * now, we'll continue the legacy behaviour of coercing it to the
	 * coherent mask if not, but we'll no longer do so quietly.
	 */
	if (!dev->dma_mask) {
		dev_warn(dev, "DMA mask not set\n");
		dev->dma_mask = &dev->coherent_dma_mask;
	}

	if (dev->coherent_dma_mask)
		end = dev->coherent_dma_mask;
	else
		end = (1ULL << 32) - 1;

	if (dev->dma_range_map) {
		dev_dbg(dev, "dma_range_map already set\n");
		return;
	}

	ret = acpi_dma_get_range(dev, &map);
	if (!ret && map) {
		end = dma_range_map_max(map);
		dev->dma_range_map = map;
	}

	if (ret == -ENODEV)
		ret = iort_dma_get_ranges(dev, &end);
	if (!ret) {
		/*
		 * Limit coherent and dma mask based on size retrieved from
		 * firmware.
		 */
		mask = DMA_BIT_MASK(ilog2(end) + 1);
		dev->bus_dma_limit = end;
		dev->coherent_dma_mask = min(dev->coherent_dma_mask, mask);
		*dev->dma_mask = min(*dev->dma_mask, mask);
	}
}

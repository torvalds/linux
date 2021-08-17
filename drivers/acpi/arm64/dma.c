// SPDX-License-Identifier: GPL-2.0-only
#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/device.h>
#include <linux/dma-direct.h>

void acpi_arch_dma_setup(struct device *dev, u64 *dma_addr, u64 *dma_size)
{
	int ret;
	u64 end, mask;
	u64 dmaaddr = 0, size = 0, offset = 0;

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
		size = max(dev->coherent_dma_mask, dev->coherent_dma_mask + 1);
	else
		size = 1ULL << 32;

	ret = acpi_dma_get_range(dev, &dmaaddr, &offset, &size);
	if (ret == -ENODEV)
		ret = iort_dma_get_ranges(dev, &size);
	if (!ret) {
		/*
		 * Limit coherent and dma mask based on size retrieved from
		 * firmware.
		 */
		end = dmaaddr + size - 1;
		mask = DMA_BIT_MASK(ilog2(end) + 1);
		dev->bus_dma_limit = end;
		dev->coherent_dma_mask = min(dev->coherent_dma_mask, mask);
		*dev->dma_mask = min(*dev->dma_mask, mask);
	}

	*dma_addr = dmaaddr;
	*dma_size = size;

	ret = dma_direct_set_offset(dev, dmaaddr + offset, dmaaddr, size);

	dev_dbg(dev, "dma_offset(%#08llx)%s\n", offset, ret ? " failed!" : "");
}

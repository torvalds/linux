#include <linux/dma-mapping.h>

const struct dma_mapping_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}
EXPORT_SYMBOL(dma_set_mask);



#include <linux/dma-mapping.h>

struct dma_map_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

struct dma_map_ops *dma_get_ops(struct device *dev)
{
	return dma_ops;
}
EXPORT_SYMBOL(dma_get_ops);

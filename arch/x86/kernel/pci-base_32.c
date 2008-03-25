#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <asm/dma-mapping.h>

static dma_addr_t pci32_map_single(struct device *dev, void *ptr,
				   size_t size, int direction)
{
	WARN_ON(size == 0);
	flush_write_buffers();
	return virt_to_phys(ptr);
}

static const struct dma_mapping_ops pci32_dma_ops = {
	.map_single = pci32_map_single,
};

const struct dma_mapping_ops *dma_ops = &pci32_dma_ops;
EXPORT_SYMBOL(dma_ops);

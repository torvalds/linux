#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <asm/dma-mapping.h>

static dma_addr_t pci32_map_single(struct device *dev, phys_addr_t ptr,
				   size_t size, int direction)
{
	WARN_ON(size == 0);
	flush_write_buffers();
	return ptr;
}

static int pci32_dma_map_sg(struct device *dev, struct scatterlist *sglist,
			    int nents, int direction)
{
	struct scatterlist *sg;
	int i;

	WARN_ON(nents == 0 || sglist[0].length == 0);

	for_each_sg(sglist, sg, nents, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);
		sg->dma_length = sg->length;
	}

	flush_write_buffers();
	return nents;
}

/* Make sure we keep the same behaviour */
static int pci32_map_error(dma_addr_t dma_addr)
{
	return 0;
}

const struct dma_mapping_ops pci32_dma_ops = {
	.map_single = pci32_map_single,
	.unmap_single = NULL,
	.map_sg = pci32_dma_map_sg,
	.unmap_sg = NULL,
	.sync_single_for_cpu = NULL,
	.sync_single_for_device = NULL,
	.sync_single_range_for_cpu = NULL,
	.sync_single_range_for_device = NULL,
	.sync_sg_for_cpu = NULL,
	.sync_sg_for_device = NULL,
	.mapping_error = pci32_map_error,
};

/* this is temporary */
int __init no_iommu_init(void)
{
	dma_ops = &pci32_dma_ops;
	return 0;
}
fs_initcall(no_iommu_init);

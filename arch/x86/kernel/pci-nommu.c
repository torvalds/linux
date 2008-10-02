/* Fallback functions when the main IOMMU code is not compiled in. This
   code is roughly equivalent to i386. */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#include <asm/iommu.h>
#include <asm/processor.h>
#include <asm/dma.h>

static int
check_addr(char *name, struct device *hwdev, dma_addr_t bus, size_t size)
{
	if (hwdev && bus + size > *hwdev->dma_mask) {
		if (*hwdev->dma_mask >= DMA_32BIT_MASK)
			printk(KERN_ERR
			    "nommu_%s: overflow %Lx+%zu of device mask %Lx\n",
				name, (long long)bus, size,
				(long long)*hwdev->dma_mask);
		return 0;
	}
	return 1;
}

static dma_addr_t
nommu_map_single(struct device *hwdev, phys_addr_t paddr, size_t size,
	       int direction)
{
	dma_addr_t bus = paddr;
	WARN_ON(size == 0);
	if (!check_addr("map_single", hwdev, bus, size))
				return bad_dma_address;
	flush_write_buffers();
	return bus;
}


/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scatter-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
static int nommu_map_sg(struct device *hwdev, struct scatterlist *sg,
	       int nents, int direction)
{
	struct scatterlist *s;
	int i;

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		BUG_ON(!sg_page(s));
		s->dma_address = sg_phys(s);
		if (!check_addr("map_sg", hwdev, s->dma_address, s->length))
			return 0;
		s->dma_length = s->length;
	}
	flush_write_buffers();
	return nents;
}

struct dma_mapping_ops nommu_dma_ops = {
	.map_single = nommu_map_single,
	.map_sg = nommu_map_sg,
	.is_phys = 1,
};

void __init no_iommu_init(void)
{
	if (dma_ops)
		return;

	force_iommu = 0; /* no HW IOMMU */
	dma_ops = &nommu_dma_ops;
}

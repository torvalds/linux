/*
 * Dynamic DMA mapping support.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <asm/io.h>

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
int dma_map_sg(struct device *hwdev, struct scatterlist *sg,
	       int nents, int direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);
 	for (i = 0; i < nents; i++ ) {
		struct scatterlist *s = &sg[i];
		BUG_ON(!s->page); 
		s->dma_address = virt_to_bus(page_address(s->page) +s->offset);
		s->dma_length = s->length;
	}
	return nents;
}

EXPORT_SYMBOL(dma_map_sg);

/* Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
		  int nents, int dir)
{
	int i;
	for (i = 0; i < nents; i++) { 
		struct scatterlist *s = &sg[i];
		BUG_ON(s->page == NULL); 
		BUG_ON(s->dma_address == 0); 
		dma_unmap_single(dev, s->dma_address, s->dma_length, dir);
	} 
}

EXPORT_SYMBOL(dma_unmap_sg);

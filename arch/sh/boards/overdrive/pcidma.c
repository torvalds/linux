/* 
 * Copyright (C) 2000 David J. Mckay (david.mckay@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.                            
 *
 * Dynamic DMA mapping support.
 *
 * On the overdrive, we can only DMA from memory behind the PCI bus!
 * this means that all DMA'able memory must come from there. 
 * this restriction will not apply to later boards.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <asm/io.h>

void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
			   dma_addr_t * dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;

        printk("BUG: pci_alloc_consistent() called - not yet supported\n");
	/* We ALWAYS need DMA memory on the overdrive hardware,
	 * due to it's extreme weirdness
	 * Need to flush the cache here as well, since the memory
	 * can still be seen through the cache!
	 */
	gfp |= GFP_DMA;
	ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}
	return ret;
}

void pci_free_consistent(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long) vaddr, get_order(size));
}

/*
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 * Copyright (C) 2003 Paul Mundt (lethal@linux-sh.org)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Dynamic DMA mapping support.
 */
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <asm/io.h>

void *consistent_alloc(struct pci_dev *hwdev, size_t size,
			   dma_addr_t *dma_handle)
{
	void *ret;
	int gfp = GFP_ATOMIC;
        void *vp;

	if (hwdev == NULL || hwdev->dma_mask != 0xffffffff)
		gfp |= GFP_DMA;

	ret = (void *)__get_free_pages(gfp, get_order(size));

	/* now call our friend ioremap_nocache to give us an uncached area */
        vp = ioremap_nocache(virt_to_phys(ret), size);

	if (vp != NULL) {
		memset(vp, 0, size);
		*dma_handle = virt_to_phys(ret);
		dma_cache_wback_inv((unsigned long)ret, size);
	}

	return vp;
}

void consistent_free(struct pci_dev *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	void *alloc;

	alloc = phys_to_virt((unsigned long)dma_handle);
	free_pages((unsigned long)alloc, get_order(size));

	iounmap(vaddr);
}


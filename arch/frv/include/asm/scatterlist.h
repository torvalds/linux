#ifndef _ASM_SCATTERLIST_H
#define _ASM_SCATTERLIST_H

#include <asm/types.h>

/*
 * Drivers must set either ->address or (preferred) page and ->offset
 * to indicate where data must be transferred to/from.
 *
 * Using page is recommended since it handles highmem data as well as
 * low mem. ->address is restricted to data which has a virtual mapping, and
 * it will go away in the future. Updating to page can be automated very
 * easily -- something like
 *
 * sg->address = some_ptr;
 *
 * can be rewritten as
 *
 * sg_set_buf(sg, some_ptr, length);
 *
 * and that's it. There's no excuse for not highmem enabling YOUR driver. /jens
 */
struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long	sg_magic;
#endif
	unsigned long	page_link;
	unsigned int	offset;		/* for highmem, page offset */

	dma_addr_t	dma_address;
	unsigned int	length;
};

/*
 * These macros should be used after a pci_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns, or alternatively stop on the first sg_dma_len(sg) which
 * is 0.
 */
#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)		((sg)->length)

#define ISA_DMA_THRESHOLD (0xffffffffUL)

#endif /* !_ASM_SCATTERLIST_H */

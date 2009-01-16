#ifndef _SPARC_SCATTERLIST_H
#define _SPARC_SCATTERLIST_H

#include <asm/page.h>
#include <asm/types.h>

struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long	sg_magic;
#endif
	unsigned long	page_link;
	unsigned int	offset;

	unsigned int	length;

	dma_addr_t	dma_address;
	__u32		dma_length;
};

#define sg_dma_address(sg)	((sg)->dma_address)
#define sg_dma_len(sg)     	((sg)->dma_length)

#define ISA_DMA_THRESHOLD	(~0UL)

#define ARCH_HAS_SG_CHAIN

#endif /* !(_SPARC_SCATTERLIST_H) */

#ifndef _H8300_SCATTERLIST_H
#define _H8300_SCATTERLIST_H

#include <asm/types.h>

struct scatterlist {
#ifdef CONFIG_DEBUG_SG
	unsigned long	sg_magic;
#endif
	unsigned long	page_link;
	unsigned int	offset;
	dma_addr_t	dma_address;
	unsigned int	length;
};

#define ISA_DMA_THRESHOLD	(0xffffffff)

#endif /* !(_H8300_SCATTERLIST_H) */

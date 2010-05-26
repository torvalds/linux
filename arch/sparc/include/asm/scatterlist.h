#ifndef _SPARC_SCATTERLIST_H
#define _SPARC_SCATTERLIST_H

#define sg_dma_len(sg)     	((sg)->dma_length)

#define ISA_DMA_THRESHOLD	(~0UL)

#include <asm-generic/scatterlist.h>

#endif /* !(_SPARC_SCATTERLIST_H) */

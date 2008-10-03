#ifndef _ASM_SPARC_DMA_MAPPING_H
#define _ASM_SPARC_DMA_MAPPING_H


#ifdef CONFIG_PCI
#include <asm-generic/dma-mapping.h>
#else
#include <asm-generic/dma-mapping-broken.h>
#endif /* PCI */

#endif /* _ASM_SPARC_DMA_MAPPING_H */

#ifndef _ASM_PARISC_SCATTERLIST_H
#define _ASM_PARISC_SCATTERLIST_H

#include <asm/page.h>
#include <asm/types.h>
#include <asm-generic/scatterlist.h>

#define ISA_DMA_THRESHOLD (~0UL)
#define sg_virt_addr(sg) ((unsigned long)sg_virt(sg))

#endif /* _ASM_PARISC_SCATTERLIST_H */

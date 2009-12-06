#ifndef _ASM_X86_SWIOTLB_H
#define _ASM_X86_SWIOTLB_H

#include <linux/swiotlb.h>

#ifdef CONFIG_SWIOTLB
extern int swiotlb;
extern int pci_swiotlb_init(void);
#else
#define swiotlb 0
static inline int pci_swiotlb_init(void)
{
	return 0;
}
#endif

static inline void dma_mark_clean(void *addr, size_t size) {}

#endif /* _ASM_X86_SWIOTLB_H */

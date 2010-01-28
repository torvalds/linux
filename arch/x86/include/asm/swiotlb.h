#ifndef _ASM_X86_SWIOTLB_H
#define _ASM_X86_SWIOTLB_H

#include <linux/swiotlb.h>

#ifdef CONFIG_SWIOTLB
extern int swiotlb;
extern int __init pci_swiotlb_detect(void);
extern void __init pci_swiotlb_init(void);
#else
#define swiotlb 0
static inline int pci_swiotlb_detect(void)
{
	return 0;
}
static inline void pci_swiotlb_init(void)
{
}
#endif

static inline void dma_mark_clean(void *addr, size_t size) {}

#endif /* _ASM_X86_SWIOTLB_H */

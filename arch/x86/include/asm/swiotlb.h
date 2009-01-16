#ifndef _ASM_X86_SWIOTLB_H
#define _ASM_X86_SWIOTLB_H

#include <linux/swiotlb.h>

/* SWIOTLB interface */

extern int swiotlb_force;

#ifdef CONFIG_SWIOTLB
extern int swiotlb;
extern void pci_swiotlb_init(void);
#else
#define swiotlb 0
static inline void pci_swiotlb_init(void)
{
}
#endif

static inline void dma_mark_clean(void *addr, size_t size) {}

#endif /* _ASM_X86_SWIOTLB_H */

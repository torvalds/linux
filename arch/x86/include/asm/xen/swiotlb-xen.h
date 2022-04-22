/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SWIOTLB_XEN_H
#define _ASM_X86_SWIOTLB_XEN_H

#ifdef CONFIG_SWIOTLB_XEN
extern int pci_xen_swiotlb_init_late(void);
#else
static inline int pci_xen_swiotlb_init_late(void) { return -ENXIO; }
#endif

int xen_swiotlb_fixup(void *buf, unsigned long nslabs);
int xen_create_contiguous_region(phys_addr_t pstart, unsigned int order,
				unsigned int address_bits,
				dma_addr_t *dma_handle);
void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order);

#endif /* _ASM_X86_SWIOTLB_XEN_H */

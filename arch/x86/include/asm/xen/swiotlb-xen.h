/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SWIOTLB_XEN_H
#define _ASM_X86_SWIOTLB_XEN_H

int xen_swiotlb_fixup(void *buf, unsigned long nslabs);
int xen_create_contiguous_region(phys_addr_t pstart, unsigned int order,
				unsigned int address_bits,
				dma_addr_t *dma_handle);
void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order);

#endif /* _ASM_X86_SWIOTLB_XEN_H */

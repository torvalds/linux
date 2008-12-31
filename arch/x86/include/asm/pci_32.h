#ifndef _ASM_X86_PCI_32_H
#define _ASM_X86_PCI_32_H


#ifdef __KERNEL__


/* Dynamic DMA mapping stuff.
 * i386 has everything mapped statically.
 */

struct pci_dev;

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

/* pci_unmap_{page,single} is a nop so... */
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)	dma_addr_t ADDR_NAME[0];
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)	unsigned LEN_NAME[0];
#define pci_unmap_addr(PTR, ADDR_NAME)	sizeof((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL) \
	do { break; } while (pci_unmap_addr(PTR, ADDR_NAME))
#define pci_unmap_len(PTR, LEN_NAME)		sizeof((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL) \
	do { break; } while (pci_unmap_len(PTR, LEN_NAME))


#endif /* __KERNEL__ */


#endif /* _ASM_X86_PCI_32_H */

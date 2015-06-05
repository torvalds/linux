#ifndef ASMARM_PCI_H
#define ASMARM_PCI_H

#ifdef __KERNEL__
#include <asm-generic/pci-dma-compat.h>
#include <asm-generic/pci-bridge.h>

#include <asm/mach/pci.h> /* for pci_sys_data */

extern unsigned long pcibios_min_io;
#define PCIBIOS_MIN_IO pcibios_min_io
extern unsigned long pcibios_min_mem;
#define PCIBIOS_MIN_MEM pcibios_min_mem

static inline int pcibios_assign_all_busses(void)
{
	return pci_has_flag(PCI_REASSIGN_ALL_RSRC);
}

#ifdef CONFIG_PCI_DOMAINS
static inline int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}
#endif /* CONFIG_PCI_DOMAINS */

/*
 * The PCI address space does equal the physical memory address space.
 * The networking and block device layers use this boolean for bounce
 * buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (1)

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#endif

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
                               enum pci_mmap_state mmap_state, int write_combine);

static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return channel ? 15 : 14;
}

#endif /* __KERNEL__ */
 
#endif

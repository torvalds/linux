#ifndef __SPARC_PCI_H
#define __SPARC_PCI_H

#ifdef __KERNEL__

#include <linux/dma-mapping.h>

/* Can be used to override the logic in pci_scan_bus for skipping
 * already-configured bus numbers - to be used for buggy BIOSes
 * or architectures with incomplete PCI setup by the loader.
 */
#define pcibios_assign_all_busses()	0

#define PCIBIOS_MIN_IO		0UL
#define PCIBIOS_MIN_MEM		0UL

#define PCI_IRQ_NONE		0xffffffff

static inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/* Dynamic DMA mapping stuff.
 */
#define PCI_DMA_BUS_IS_PHYS	(0)

struct pci_dev;

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#endif

struct device_node;
extern struct device_node *pci_device_to_OF_node(struct pci_dev *pdev);

#endif /* __KERNEL__ */

#ifndef CONFIG_LEON_PCI
/* generic pci stuff */
#include <asm-generic/pci.h>
#else
/*
 * On LEON PCI Memory space is mapped 1:1 with physical address space.
 *
 * I/O space is located at low 64Kbytes in PCI I/O space. The I/O addresses
 * are converted into CPU addresses to virtual addresses that are mapped with
 * MMU to the PCI Host PCI I/O space window which are translated to the low
 * 64Kbytes by the Host controller.
 */

extern void
pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			struct resource *res);

extern void
pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			struct pci_bus_region *region);

static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return PCI_IRQ_NONE;
}
#endif

#endif /* __SPARC_PCI_H */

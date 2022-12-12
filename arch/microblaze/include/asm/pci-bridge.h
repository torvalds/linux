/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_MICROBLAZE_PCI_BRIDGE_H
#define _ASM_MICROBLAZE_PCI_BRIDGE_H
#ifdef __KERNEL__
/*
 */
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/ioport.h>

struct device_node;

#ifdef CONFIG_PCI
extern struct list_head hose_list;
extern int pcibios_vaddr_is_ioport(void __iomem *address);
#else
static inline int pcibios_vaddr_is_ioport(void __iomem *address)
{
	return 0;
}
#endif

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	struct pci_bus *bus;
	struct list_head list_node;

	void __iomem *io_base_virt;

	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource io_resource;
};

#ifdef CONFIG_PCI
static inline int isa_vaddr_is_ioport(void __iomem *address)
{
	/* No specific ISA handling on ppc32 at this stage, it
	 * all goes through PCI
	 */
	return 0;
}
#endif /* CONFIG_PCI */

#endif	/* __KERNEL__ */
#endif	/* _ASM_MICROBLAZE_PCI_BRIDGE_H */

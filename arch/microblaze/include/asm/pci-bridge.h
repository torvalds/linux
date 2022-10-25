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
	struct device_node *dn;
	struct list_head list_node;
	struct device *parent;

	int first_busno;
	int last_busno;

	int self_busno;

	void __iomem *io_base_virt;
	resource_size_t io_base_phys;

	/* Some machines (PReP) have a non 1:1 mapping of
	 * the PCI memory space in the CPU bus space
	 */
	resource_size_t pci_mem_offset;

	struct pci_ops *ops;
	unsigned int __iomem *cfg_addr;
	void __iomem *cfg_data;

	/*
	 * Used for variants of PCI indirect handling and possible quirks:
	 *  SET_CFG_TYPE - used on 4xx or any PHB that does explicit type0/1
	 *  EXT_REG - provides access to PCI-e extended registers
	 *  SURPRESS_PRIMARY_BUS - we suppress the setting of PCI_PRIMARY_BUS
	 *   on Freescale PCI-e controllers since they used the PCI_PRIMARY_BUS
	 *   to determine which bus number to match on when generating type0
	 *   config cycles
	 *  NO_PCIE_LINK - the Freescale PCI-e controllers have issues with
	 *   hanging if we don't have link and try to do config cycles to
	 *   anything but the PHB.  Only allow talking to the PHB if this is
	 *   set.
	 *  BIG_ENDIAN - cfg_addr is a big endian register
	 *  BROKEN_MRM - the 440EPx/GRx chips have an errata that causes hangs
	 *   on the PLB4.  Effectively disable MRM commands by setting this.
	 */
#define INDIRECT_TYPE_SET_CFG_TYPE		0x00000001
#define INDIRECT_TYPE_EXT_REG		0x00000002
#define INDIRECT_TYPE_SURPRESS_PRIMARY_BUS	0x00000004
#define INDIRECT_TYPE_NO_PCIE_LINK		0x00000008
#define INDIRECT_TYPE_BIG_ENDIAN		0x00000010
#define INDIRECT_TYPE_BROKEN_MRM		0x00000020
	u32 indirect_type;

	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource io_resource;
	struct resource mem_resources[3];
	int global_number;	/* PCI domain number */
};

#ifdef CONFIG_PCI
static inline struct pci_controller *pci_bus_to_host(const struct pci_bus *bus)
{
	return bus->sysdata;
}

static inline int isa_vaddr_is_ioport(void __iomem *address)
{
	/* No specific ISA handling on ppc32 at this stage, it
	 * all goes through PCI
	 */
	return 0;
}
#endif /* CONFIG_PCI */

extern void setup_indirect_pci(struct pci_controller *hose,
			       resource_size_t cfg_addr,
			       resource_size_t cfg_data, u32 flags);

#endif	/* __KERNEL__ */
#endif	/* _ASM_MICROBLAZE_PCI_BRIDGE_H */

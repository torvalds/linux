#ifndef _ASM_POWERPC_PCI_BRIDGE_H
#define _ASM_POWERPC_PCI_BRIDGE_H
#ifdef __KERNEL__
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <asm-generic/pci-bridge.h>

struct device_node;

/*
 * Structure of a PCI controller (host bridge)
 */
struct pci_controller {
	struct pci_bus *bus;
	char is_dynamic;
#ifdef CONFIG_PPC64
	int node;
#endif
	struct device_node *dn;
	struct list_head list_node;
	struct device *parent;

	int first_busno;
	int last_busno;
	int self_busno;
	struct resource busn;

	void __iomem *io_base_virt;
#ifdef CONFIG_PPC64
	void *io_base_alloc;
#endif
	resource_size_t io_base_phys;
	resource_size_t pci_io_size;

	/* Some machines have a special region to forward the ISA
	 * "memory" cycles such as VGA memory regions. Left to 0
	 * if unsupported
	 */
	resource_size_t	isa_mem_phys;
	resource_size_t	isa_mem_size;

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
	 *  BROKEN_MRM - the 440EPx/GRx chips have an errata that causes hangs on
	 *   the PLB4.  Effectively disable MRM commands by setting this.
	 *  FSL_CFG_REG_LINK - Freescale controller version in which the PCIe
	 *   link status is in a RC PCIe cfg register (vs being a SoC register)
	 */
#define PPC_INDIRECT_TYPE_SET_CFG_TYPE		0x00000001
#define PPC_INDIRECT_TYPE_EXT_REG		0x00000002
#define PPC_INDIRECT_TYPE_SURPRESS_PRIMARY_BUS	0x00000004
#define PPC_INDIRECT_TYPE_NO_PCIE_LINK		0x00000008
#define PPC_INDIRECT_TYPE_BIG_ENDIAN		0x00000010
#define PPC_INDIRECT_TYPE_BROKEN_MRM		0x00000020
#define PPC_INDIRECT_TYPE_FSL_CFG_REG_LINK	0x00000040
	u32 indirect_type;
	/* Currently, we limit ourselves to 1 IO range and 3 mem
	 * ranges since the common pci_bus structure can't handle more
	 */
	struct resource	io_resource;
	struct resource mem_resources[3];
	resource_size_t mem_offset[3];
	int global_number;		/* PCI domain number */

	resource_size_t dma_window_base_cur;
	resource_size_t dma_window_size;

#ifdef CONFIG_PPC64
	unsigned long buid;
#endif	/* CONFIG_PPC64 */

	void *private_data;
};

/* These are used for config access before all the PCI probing
   has been done. */
extern int early_read_config_byte(struct pci_controller *hose, int bus,
			int dev_fn, int where, u8 *val);
extern int early_read_config_word(struct pci_controller *hose, int bus,
			int dev_fn, int where, u16 *val);
extern int early_read_config_dword(struct pci_controller *hose, int bus,
			int dev_fn, int where, u32 *val);
extern int early_write_config_byte(struct pci_controller *hose, int bus,
			int dev_fn, int where, u8 val);
extern int early_write_config_word(struct pci_controller *hose, int bus,
			int dev_fn, int where, u16 val);
extern int early_write_config_dword(struct pci_controller *hose, int bus,
			int dev_fn, int where, u32 val);

extern int early_find_capability(struct pci_controller *hose, int bus,
				 int dev_fn, int cap);

extern void setup_indirect_pci(struct pci_controller* hose,
			       resource_size_t cfg_addr,
			       resource_size_t cfg_data, u32 flags);

extern int indirect_read_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 *val);

extern int indirect_write_config(struct pci_bus *bus, unsigned int devfn,
				 int offset, int len, u32 val);

static inline struct pci_controller *pci_bus_to_host(const struct pci_bus *bus)
{
	return bus->sysdata;
}

#ifndef CONFIG_PPC64

extern int pci_device_from_OF_node(struct device_node *node,
				   u8 *bus, u8 *devfn);
extern void pci_create_OF_bus_map(void);

static inline int isa_vaddr_is_ioport(void __iomem *address)
{
	/* No specific ISA handling on ppc32 at this stage, it
	 * all goes through PCI
	 */
	return 0;
}

#else	/* CONFIG_PPC64 */

/*
 * PCI stuff, for nodes representing PCI devices, pointed to
 * by device_node->data.
 */
struct iommu_table;

struct pci_dn {
	int	busno;			/* pci bus number */
	int	devfn;			/* pci device and function number */

	struct  pci_controller *phb;	/* for pci devices */
	struct	iommu_table *iommu_table;	/* for phb's or bridges */
	struct	device_node *node;	/* back-pointer to the device_node */

	int	pci_ext_config_space;	/* for pci devices */

	struct	pci_dev *pcidev;	/* back-pointer to the pci device */
#ifdef CONFIG_EEH
	struct eeh_dev *edev;		/* eeh device */
#endif
#define IODA_INVALID_PE		(-1)
#ifdef CONFIG_PPC_POWERNV
	int	pe_number;
#endif
};

/* Get the pointer to a device_node's pci_dn */
#define PCI_DN(dn)	((struct pci_dn *) (dn)->data)

extern struct pci_dn *pci_get_pdn(struct pci_dev *pdev);

extern void * update_dn_pci_info(struct device_node *dn, void *data);

static inline int pci_device_from_OF_node(struct device_node *np,
					  u8 *bus, u8 *devfn)
{
	if (!PCI_DN(np))
		return -ENODEV;
	*bus = PCI_DN(np)->busno;
	*devfn = PCI_DN(np)->devfn;
	return 0;
}

#if defined(CONFIG_EEH)
static inline struct eeh_dev *of_node_to_eeh_dev(struct device_node *dn)
{
	/*
	 * For those OF nodes whose parent isn't PCI bridge, they
	 * don't have PCI_DN actually. So we have to skip them for
	 * any EEH operations.
	 */
	if (!dn || !PCI_DN(dn))
		return NULL;

	return PCI_DN(dn)->edev;
}
#else
#define of_node_to_eeh_dev(x) (NULL)
#endif

/** Find the bus corresponding to the indicated device node */
extern struct pci_bus *pcibios_find_pci_bus(struct device_node *dn);

/** Remove all of the PCI devices under this bus */
extern void pcibios_remove_pci_devices(struct pci_bus *bus);

/** Discover new pci devices under this bus, and add them */
extern void pcibios_add_pci_devices(struct pci_bus *bus);


extern void isa_bridge_find_early(struct pci_controller *hose);

static inline int isa_vaddr_is_ioport(void __iomem *address)
{
	/* Check if address hits the reserved legacy IO range */
	unsigned long ea = (unsigned long)address;
	return ea >= ISA_IO_BASE && ea < ISA_IO_END;
}

extern int pcibios_unmap_io_space(struct pci_bus *bus);
extern int pcibios_map_io_space(struct pci_bus *bus);

#ifdef CONFIG_NUMA
#define PHB_SET_NODE(PHB, NODE)		((PHB)->node = (NODE))
#else
#define PHB_SET_NODE(PHB, NODE)		((PHB)->node = -1)
#endif

#endif	/* CONFIG_PPC64 */

/* Get the PCI host controller for an OF device */
extern struct pci_controller *pci_find_hose_for_OF_device(
			struct device_node* node);

/* Fill up host controller resources from the OF node */
extern void pci_process_bridge_OF_ranges(struct pci_controller *hose,
			struct device_node *dev, int primary);

/* Allocate & free a PCI host bridge structure */
extern struct pci_controller *pcibios_alloc_controller(struct device_node *dev);
extern void pcibios_free_controller(struct pci_controller *phb);

#ifdef CONFIG_PCI
extern int pcibios_vaddr_is_ioport(void __iomem *address);
#else
static inline int pcibios_vaddr_is_ioport(void __iomem *address)
{
	return 0;
}
#endif	/* CONFIG_PCI */

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_PCI_BRIDGE_H */

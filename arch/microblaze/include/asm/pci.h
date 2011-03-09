/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Based on powerpc version
 */

#ifndef __ASM_MICROBLAZE_PCI_H
#define __ASM_MICROBLAZE_PCI_H
#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>

#include <asm/scatterlist.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

struct pci_dev;

/* Values for the `which' argument to sys_pciconfig_iobase syscall.  */
#define IOBASE_BRIDGE_NUMBER	0
#define IOBASE_MEMORY		1
#define IOBASE_IO		2
#define IOBASE_ISA_IO		3
#define IOBASE_ISA_MEM		4

#define pcibios_scan_all_fns(a, b)	0

/*
 * Set this to 1 if you want the kernel to re-assign all PCI
 * bus numbers (don't do that on ppc64 yet !)
 */
#define pcibios_assign_all_busses() \
	(pci_has_flag(PCI_REASSIGN_ALL_BUS))

static inline void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

#ifdef CONFIG_PCI
extern void set_pci_dma_ops(struct dma_map_ops *dma_ops);
extern struct dma_map_ops *get_pci_dma_ops(void);
#else	/* CONFIG_PCI */
#define set_pci_dma_ops(d)
#define get_pci_dma_ops()	NULL
#endif

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#endif

extern int pci_domain_nr(struct pci_bus *bus);

/* Decide whether to display the domain number in /proc */
extern int pci_proc_domain(struct pci_bus *bus);

struct vm_area_struct;
/* Map a range of PCI memory or I/O space for a device into user space */
int pci_mmap_page_range(struct pci_dev *pdev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine);

/* Tell drivers/pci/proc.c that we have pci_mmap_page_range() */
#define HAVE_PCI_MMAP	1

extern int pci_legacy_read(struct pci_bus *bus, loff_t port, u32 *val,
			   size_t count);
extern int pci_legacy_write(struct pci_bus *bus, loff_t port, u32 val,
			   size_t count);
extern int pci_mmap_legacy_page_range(struct pci_bus *bus,
				      struct vm_area_struct *vma,
				      enum pci_mmap_state mmap_state);

#define HAVE_PCI_LEGACY	1

/* The PCI address space does equal the physical memory
 * address space (no IOMMU).  The IDE and SCSI device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (1)

extern void pcibios_resource_to_bus(struct pci_dev *dev,
			struct pci_bus_region *region,
			struct resource *res);

extern void pcibios_bus_to_resource(struct pci_dev *dev,
			struct resource *res,
			struct pci_bus_region *region);

static inline struct resource *pcibios_select_root(struct pci_dev *pdev,
			struct resource *res)
{
	struct resource *root = NULL;

	if (res->flags & IORESOURCE_IO)
		root = &ioport_resource;
	if (res->flags & IORESOURCE_MEM)
		root = &iomem_resource;

	return root;
}

extern void pcibios_claim_one_bus(struct pci_bus *b);

extern void pcibios_finish_adding_to_bus(struct pci_bus *bus);

extern void pcibios_resource_survey(void);

extern struct pci_controller *init_phb_dynamic(struct device_node *dn);
extern int remove_phb_dynamic(struct pci_controller *phb);

extern struct pci_dev *of_create_pci_dev(struct device_node *node,
					struct pci_bus *bus, int devfn);

extern void of_scan_pci_bridge(struct device_node *node,
				struct pci_dev *dev);

extern void of_scan_bus(struct device_node *node, struct pci_bus *bus);
extern void of_rescan_bus(struct device_node *node, struct pci_bus *bus);

extern int pci_read_irq_line(struct pci_dev *dev);

extern int pci_bus_find_capability(struct pci_bus *bus,
						unsigned int devfn, int cap);

struct file;
extern pgprot_t	pci_phys_mem_access_prot(struct file *file,
					 unsigned long pfn,
					 unsigned long size,
					 pgprot_t prot);

#define HAVE_ARCH_PCI_RESOURCE_TO_USER
extern void pci_resource_to_user(const struct pci_dev *dev, int bar,
				 const struct resource *rsrc,
				 resource_size_t *start, resource_size_t *end);

extern void pcibios_setup_bus_devices(struct pci_bus *bus);
extern void pcibios_setup_bus_self(struct pci_bus *bus);

/* This part of code was originaly in xilinx-pci.h */
#ifdef CONFIG_PCI_XILINX
extern void __init xilinx_pci_init(void);
#else
static inline void __init xilinx_pci_init(void) { return; }
#endif

#include <asm-generic/pci-dma-compat.h>

#endif	/* __KERNEL__ */
#endif /* __ASM_MICROBLAZE_PCI_H */

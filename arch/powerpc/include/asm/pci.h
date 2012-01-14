#ifndef __ASM_POWERPC_PCI_H
#define __ASM_POWERPC_PCI_H
#ifdef __KERNEL__

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>

#include <asm/machdep.h>
#include <asm/scatterlist.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#include <asm-generic/pci-dma-compat.h>

/* Return values for ppc_md.pci_probe_mode function */
#define PCI_PROBE_NONE		-1	/* Don't look at this bus at all */
#define PCI_PROBE_NORMAL	0	/* Do normal PCI probing */
#define PCI_PROBE_DEVTREE	1	/* Instantiate from device tree */

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

struct pci_dev;

/* Values for the `which' argument to sys_pciconfig_iobase syscall.  */
#define IOBASE_BRIDGE_NUMBER	0
#define IOBASE_MEMORY		1
#define IOBASE_IO		2
#define IOBASE_ISA_IO		3
#define IOBASE_ISA_MEM		4

/*
 * Set this to 1 if you want the kernel to re-assign all PCI
 * bus numbers (don't do that on ppc64 yet !)
 */
#define pcibios_assign_all_busses() \
	(pci_has_flag(PCI_REASSIGN_ALL_BUS))

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

#define HAVE_ARCH_PCI_GET_LEGACY_IDE_IRQ
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	if (ppc_md.pci_get_legacy_ide_irq)
		return ppc_md.pci_get_legacy_ide_irq(dev, channel);
	return channel ? 15 : 14;
}

#ifdef CONFIG_PCI
extern void set_pci_dma_ops(struct dma_map_ops *dma_ops);
extern struct dma_map_ops *get_pci_dma_ops(void);
#else	/* CONFIG_PCI */
#define set_pci_dma_ops(d)
#define get_pci_dma_ops()	NULL
#endif

#ifdef CONFIG_PPC64

/*
 * We want to avoid touching the cacheline size or MWI bit.
 * pSeries firmware sets the cacheline size (which is not the cpu cacheline
 * size in all cases) and hardware treats MWI the same as memory write.
 */
#define PCI_DISABLE_MWI

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	unsigned long cacheline_size;
	u8 byte;

	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &byte);
	if (byte == 0)
		cacheline_size = 1024;
	else
		cacheline_size = (int) byte * 4;

	*strat = PCI_DMA_BURST_MULTIPLE;
	*strategy_parameter = cacheline_size;
}
#endif

#else /* 32-bit */

#ifdef CONFIG_PCI
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}
#endif
#endif /* CONFIG_PPC64 */

extern int pci_domain_nr(struct pci_bus *bus);

/* Decide whether to display the domain number in /proc */
extern int pci_proc_domain(struct pci_bus *bus);

/* MSI arch hooks */
#define arch_setup_msi_irqs arch_setup_msi_irqs
#define arch_teardown_msi_irqs arch_teardown_msi_irqs
#define arch_msi_check_device arch_msi_check_device

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

#ifdef CONFIG_PPC64

/* The PCI address space does not equal the physical memory address
 * space (we have an IOMMU).  The IDE and SCSI device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(0)

#else /* 32-bit */

/* The PCI address space does equal the physical memory
 * address space (no IOMMU).  The IDE and SCSI device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS     (1)

#endif /* CONFIG_PPC64 */

extern void pcibios_resource_to_bus(struct pci_dev *dev,
			struct pci_bus_region *region,
			struct resource *res);

extern void pcibios_bus_to_resource(struct pci_dev *dev,
			struct resource *res,
			struct pci_bus_region *region);

extern void pcibios_claim_one_bus(struct pci_bus *b);

extern void pcibios_finish_adding_to_bus(struct pci_bus *bus);

extern void pcibios_resource_survey(void);

extern struct pci_controller *init_phb_dynamic(struct device_node *dn);
extern int remove_phb_dynamic(struct pci_controller *phb);

extern struct pci_dev *of_create_pci_dev(struct device_node *node,
					struct pci_bus *bus, int devfn);

extern void of_scan_pci_bridge(struct pci_dev *dev);

extern void of_scan_bus(struct device_node *node, struct pci_bus *bus);
extern void of_rescan_bus(struct device_node *node, struct pci_bus *bus);

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
extern void pcibios_setup_phb_io_space(struct pci_controller *hose);
extern void pcibios_scan_phb(struct pci_controller *hose);

#endif	/* __KERNEL__ */
#endif /* __ASM_POWERPC_PCI_H */

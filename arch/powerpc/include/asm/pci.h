/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __ASM_POWERPC_PCI_H
#define __ASM_POWERPC_PCI_H
#ifdef __KERNEL__

/*
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dma-map-ops.h>
#include <linux/scatterlist.h>

#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

/* Return values for pci_controller_ops.probe_mode function */
#define PCI_PROBE_NONE		-1	/* Don't look at this bus at all */
#define PCI_PROBE_NORMAL	0	/* Do normal PCI probing */
#define PCI_PROBE_DEVTREE	1	/* Instantiate from device tree */

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

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

#define HAVE_ARCH_PCI_GET_LEGACY_IDE_IRQ
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	if (ppc_md.pci_get_legacy_ide_irq)
		return ppc_md.pci_get_legacy_ide_irq(dev, channel);
	return channel ? 15 : 14;
}

#ifdef CONFIG_PCI
extern void set_pci_dma_ops(const struct dma_map_ops *dma_ops);
#else	/* CONFIG_PCI */
#define set_pci_dma_ops(d)
#endif

#ifdef CONFIG_PPC64

/*
 * We want to avoid touching the cacheline size or MWI bit.
 * pSeries firmware sets the cacheline size (which is not the cpu cacheline
 * size in all cases) and hardware treats MWI the same as memory write.
 */
#define PCI_DISABLE_MWI

#endif /* CONFIG_PPC64 */

extern int pci_domain_nr(struct pci_bus *bus);

/* Decide whether to display the domain number in /proc */
extern int pci_proc_domain(struct pci_bus *bus);

struct vm_area_struct;

/* Tell PCI code what kind of PCI resource mappings we support */
#define HAVE_PCI_MMAP			1
#define ARCH_GENERIC_PCI_MMAP_RESOURCE	1
#define arch_can_pci_mmap_io()		1
#define arch_can_pci_mmap_wc()		1

extern int pci_legacy_read(struct pci_bus *bus, loff_t port, u32 *val,
			   size_t count);
extern int pci_legacy_write(struct pci_bus *bus, loff_t port, u32 val,
			   size_t count);
extern int pci_mmap_legacy_page_range(struct pci_bus *bus,
				      struct vm_area_struct *vma,
				      enum pci_mmap_state mmap_state);

#define HAVE_PCI_LEGACY	1

extern void pcibios_claim_one_bus(struct pci_bus *b);

extern void pcibios_finish_adding_to_bus(struct pci_bus *bus);

extern void pcibios_resource_survey(void);

extern struct pci_controller *init_phb_dynamic(struct device_node *dn);
extern int remove_phb_dynamic(struct pci_controller *phb);

extern struct pci_dev *of_create_pci_dev(struct device_node *node,
					struct pci_bus *bus, int devfn);

extern unsigned int pci_parse_of_flags(u32 addr0, int bridge);

extern void of_scan_pci_bridge(struct pci_dev *dev);

extern void of_scan_bus(struct device_node *node, struct pci_bus *bus);
extern void of_rescan_bus(struct device_node *node, struct pci_bus *bus);

struct file;
extern pgprot_t	pci_phys_mem_access_prot(struct file *file,
					 unsigned long pfn,
					 unsigned long size,
					 pgprot_t prot);

extern resource_size_t pcibios_io_space_offset(struct pci_controller *hose);
extern void pcibios_setup_bus_self(struct pci_bus *bus);
extern void pcibios_setup_phb_io_space(struct pci_controller *hose);
extern void pcibios_scan_phb(struct pci_controller *hose);

#endif	/* __KERNEL__ */

#endif /* __ASM_POWERPC_PCI_H */

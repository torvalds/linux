/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ALPHA_PCI_H
#define __ALPHA_PCI_H

#ifdef __KERNEL__

#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <asm/machvec.h>

/*
 * The following structure is used to manage multiple PCI busses.
 */

struct pci_iommu_arena;
struct page;

/* A controller.  Used to manage multiple PCI busses.  */

struct pci_controller {
	struct pci_controller *next;
        struct pci_bus *bus;
	struct resource *io_space;
	struct resource *mem_space;

	/* The following are for reporting to userland.  The invariant is
	   that if we report a BWX-capable dense memory, we do not report
	   a sparse memory at all, even if it exists.  */
	unsigned long sparse_mem_base;
	unsigned long dense_mem_base;
	unsigned long sparse_io_base;
	unsigned long dense_io_base;

	/* This one's for the kernel only.  It's in KSEG somewhere.  */
	unsigned long config_space_base;

	unsigned int index;
	/* For compatibility with current (as of July 2003) pciutils
	   and XFree86. Eventually will be removed. */
	unsigned int need_domain_info;

	struct pci_iommu_arena *sg_pci;
	struct pci_iommu_arena *sg_isa;

	void *sysdata;
};

/* Override the logic in pci_scan_bus for skipping already-configured
   bus numbers.  */

#define pcibios_assign_all_busses()	1

#define PCIBIOS_MIN_IO		alpha_mv.min_io_address
#define PCIBIOS_MIN_MEM		alpha_mv.min_mem_address

/* IOMMU controls.  */

#define pci_domain_nr(bus) ((struct pci_controller *)(bus)->sysdata)->index

static inline int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_controller *hose = bus->sysdata;
	return hose->need_domain_info;
}

#endif /* __KERNEL__ */

/* Values for the `which' argument to sys_pciconfig_iobase.  */
#define IOBASE_HOSE		0
#define IOBASE_SPARSE_MEM	1
#define IOBASE_DENSE_MEM	2
#define IOBASE_SPARSE_IO	3
#define IOBASE_DENSE_IO		4
#define IOBASE_ROOT_BUS		5
#define IOBASE_FROM_HOSE	0x10000

extern struct pci_dev *isa_bridge;

extern int pci_legacy_read(struct pci_bus *bus, loff_t port, u32 *val,
			   size_t count);
extern int pci_legacy_write(struct pci_bus *bus, loff_t port, u32 val,
			    size_t count);
extern int pci_mmap_legacy_page_range(struct pci_bus *bus,
				      struct vm_area_struct *vma,
				      enum pci_mmap_state mmap_state);
extern void pci_adjust_legacy_attr(struct pci_bus *bus,
				   enum pci_mmap_state mmap_type);
#define HAVE_PCI_LEGACY	1

extern int pci_create_resource_files(struct pci_dev *dev);
extern void pci_remove_resource_files(struct pci_dev *dev);

#endif /* __ALPHA_PCI_H */

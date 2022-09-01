/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_PCI_H
#define _ASM_IA64_PCI_H

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/scatterlist.h>

#include <asm/io.h>
#include <asm/hw_irq.h>

struct pci_vector_struct {
	__u16 segment;	/* PCI Segment number */
	__u16 bus;	/* PCI Bus number */
	__u32 pci_id;	/* ACPI split 16 bits device, 16 bits function (see section 6.1.1) */
	__u8 pin;	/* PCI PIN (0 = A, 1 = B, 2 = C, 3 = D) */
	__u32 irq;	/* IRQ assigned */
};

/*
 * Can be used to override the logic in pci_scan_bus for skipping already-configured bus
 * numbers - to be used for buggy BIOSes or architectures with incomplete PCI setup by the
 * loader.
 */
#define pcibios_assign_all_busses()     0

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		0x10000000

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE
#define arch_can_pci_mmap_wc()	1

#define HAVE_PCI_LEGACY
extern int pci_mmap_legacy_page_range(struct pci_bus *bus,
				      struct vm_area_struct *vma,
				      enum pci_mmap_state mmap_state);

char *pci_get_legacy_mem(struct pci_bus *bus);
int pci_legacy_read(struct pci_bus *bus, u16 port, u32 *val, u8 size);
int pci_legacy_write(struct pci_bus *bus, u16 port, u32 val, u8 size);

struct pci_controller {
	struct acpi_device *companion;
	void *iommu;
	int segment;
	int node;		/* nearest node with memory or NUMA_NO_NODE for global allocation */

	void *platform_data;
};


#define PCI_CONTROLLER(busdev) ((struct pci_controller *) busdev->sysdata)
#define pci_domain_nr(busdev)    (PCI_CONTROLLER(busdev)->segment)

extern struct pci_ops pci_root_ops;

static inline int pci_proc_domain(struct pci_bus *bus)
{
	return (pci_domain_nr(bus) != 0);
}

#endif /* _ASM_IA64_PCI_H */

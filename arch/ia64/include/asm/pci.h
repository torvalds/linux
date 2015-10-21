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

void pcibios_config_init(void);

struct pci_dev;

/*
 * PCI_DMA_BUS_IS_PHYS should be set to 1 if there is _necessarily_ a direct
 * correspondence between device bus addresses and CPU physical addresses.
 * Platforms with a hardware I/O MMU _must_ turn this off to suppress the
 * bounce buffer handling code in the block and network device layers.
 * Platforms with separate bus address spaces _must_ turn this off and provide
 * a device DMA mapping implementation that takes care of the necessary
 * address translation.
 *
 * For now, the ia64 platforms which may have separate/multiple bus address
 * spaces all have I/O MMUs which support the merging of physically
 * discontiguous buffers, so we can use that as the sole factor to determine
 * the setting of PCI_DMA_BUS_IS_PHYS.
 */
extern unsigned long ia64_max_iommu_merge_mask;
#define PCI_DMA_BUS_IS_PHYS	(ia64_max_iommu_merge_mask == ~0UL)

#include <asm-generic/pci-dma-compat.h>

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range (struct pci_dev *dev, struct vm_area_struct *vma,
				enum pci_mmap_state mmap_state, int write_combine);
#define HAVE_PCI_LEGACY
extern int pci_mmap_legacy_page_range(struct pci_bus *bus,
				      struct vm_area_struct *vma,
				      enum pci_mmap_state mmap_state);

#define pci_get_legacy_mem platform_pci_get_legacy_mem
#define pci_legacy_read platform_pci_legacy_read
#define pci_legacy_write platform_pci_legacy_write

struct iospace_resource {
	struct list_head list;
	struct resource res;
};

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

#define HAVE_ARCH_PCI_GET_LEGACY_IDE_IRQ
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return channel ? isa_irq_to_vector(15) : isa_irq_to_vector(14);
}

#ifdef CONFIG_INTEL_IOMMU
extern void pci_iommu_alloc(void);
#endif
#endif /* _ASM_IA64_PCI_H */

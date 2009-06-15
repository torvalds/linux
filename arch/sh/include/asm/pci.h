#ifndef __ASM_SH_PCI_H
#define __ASM_SH_PCI_H

#ifdef __KERNEL__

#include <linux/dma-mapping.h>

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#define pcibios_assign_all_busses()	1
#define pcibios_scan_all_fns(a, b)	0

/*
 * A board can define one or more PCI channels that represent built-in (or
 * external) PCI controllers.
 */
struct pci_channel {
	struct pci_channel	*next;

	struct pci_ops		*pci_ops;
	struct resource		*io_resource;
	struct resource		*mem_resource;

	unsigned long		io_offset;
	unsigned long		mem_offset;

	unsigned long		reg_base;

	unsigned long		io_map_base;
};

extern void register_pci_controller(struct pci_channel *hose);

extern unsigned long PCIBIOS_MIN_IO, PCIBIOS_MIN_MEM;

struct pci_dev;

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
	enum pci_mmap_state mmap_state, int write_combine);
extern void pcibios_set_master(struct pci_dev *dev);

static inline void pcibios_penalize_isa_irq(int irq, int active)
{
	/* We don't do dynamic PCI IRQ allocation */
}

/* Dynamic DMA mapping stuff.
 * SuperH has everything mapped statically like x86.
 */

/* The PCI address space does equal the physical memory
 * address space.  The networking and block device layers use
 * this boolean for bounce buffer decisions.
 */
#define PCI_DMA_BUS_IS_PHYS	(1)

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

/* pci_unmap_{single,page} being a nop depends upon the
 * configuration.
 */
#ifdef CONFIG_SH_PCIDMA_NONCOHERENT
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)	\
	dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)		\
	__u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)			\
	((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)		\
	(((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)			\
	((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)		\
	(((PTR)->LEN_NAME) = (VAL))
#else
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)		(0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)	do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)		(0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)	do { } while (0)
#endif

#ifdef CONFIG_PCI
/*
 * None of the SH PCI controllers support MWI, it is always treated as a
 * direct memory write.
 */
#define PCI_DISABLE_MWI

static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	unsigned long cacheline_size;
	u8 byte;

	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &byte);

	if (byte == 0)
		cacheline_size = L1_CACHE_BYTES;
	else
		cacheline_size = byte << 2;

	*strat = PCI_DMA_BURST_MULTIPLE;
	*strategy_parameter = cacheline_size;
}
#endif

#ifdef CONFIG_SUPERH32
/*
 * If we're on an SH7751 or SH7780 PCI controller, PCI memory is mapped
 * at the end of the address space in a special non-translatable area.
 */
#define PCI_MEM_FIXED_START	0xfd000000
#define PCI_MEM_FIXED_END	(PCI_MEM_FIXED_START + 0x01000000)

#define is_pci_memory_fixed_range(s, e)	\
	((s) >= PCI_MEM_FIXED_START && (e) < PCI_MEM_FIXED_END)
#else
#define is_pci_memory_fixed_range(s, e)	(0)
#endif

/* Board-specific fixup routines. */
int pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin);

extern void pcibios_resource_to_bus(struct pci_dev *dev,
	struct pci_bus_region *region, struct resource *res);

extern void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
				    struct pci_bus_region *region);

static inline struct resource *
pcibios_select_root(struct pci_dev *pdev, struct resource *res)
{
	struct resource *root = NULL;

	if (res->flags & IORESOURCE_IO)
		root = &ioport_resource;
	if (res->flags & IORESOURCE_MEM)
		root = &iomem_resource;

	return root;
}

/* Chances are this interrupt is wired PC-style ...  */
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return channel ? 15 : 14;
}

/* generic DMA-mapping stuff */
#include <asm-generic/pci-dma-compat.h>

#endif /* __KERNEL__ */
#endif /* __ASM_SH_PCI_H */


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
	int (*init)(struct pci_channel *chan);
	struct pci_ops *pci_ops;
	struct resource *io_resource;
	struct resource *mem_resource;
	int first_devfn;
	int last_devfn;
	int enabled;
	unsigned long reg_base;
	unsigned long io_base;
};

/*
 * Each board initializes this array and terminates it with a NULL entry.
 */
extern struct pci_channel board_pci_channels[];

/* ugly as hell, but makes drivers/pci/setup-res.c compile and work */
#define __PCI_CHAN(bus)		((struct pci_channel *)bus->sysdata)
#define PCIBIOS_MIN_IO		__PCI_CHAN(bus)->io_resource->start
#define PCIBIOS_MIN_MEM		__PCI_CHAN(bus)->mem_resource->start

struct pci_dev;

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
static inline void pci_dma_burst_advice(struct pci_dev *pdev,
					enum pci_dma_burst_strategy *strat,
					unsigned long *strategy_parameter)
{
	*strat = PCI_DMA_BURST_INFINITY;
	*strategy_parameter = ~0UL;
}

static inline int __is_pci_memory(unsigned long phys_addr, unsigned long size)
{
	struct pci_channel *p;
	struct resource *res;

	for (p = board_pci_channels; p->init; p++) {
		res = p->mem_resource;
		if (p->enabled && (phys_addr >= res->start) &&
		    (phys_addr + size) <= (res->end + 1))
			return 1;
	}
	return 0;
}

static inline void __iomem *__get_pci_io_base(unsigned long port,
					      unsigned long size)
{
	struct pci_channel *p;
	struct resource *res;

	for (p = board_pci_channels; p->init; p++) {
		res = p->io_resource;
		if (p->enabled && (port >= res->start) &&
		    (port + size) <= (res->end + 1))
			return (void __iomem *)(p->io_base + port);
	}
	return NULL;
}
#else
static inline int __is_pci_memory(unsigned long phys_addr, unsigned long size)
{
	return 0;
}
static inline void __iomem *__get_pci_io_base(unsigned long port,
					      unsigned long size)
{
	return NULL;
}
#endif

/* Board-specific fixup routines. */
void pcibios_fixup(void);
int pcibios_init_platform(void);
int pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin);

#ifdef CONFIG_PCI_AUTO
int pciauto_assign_resources(int busno, struct pci_channel *hose);
#endif

#endif /* __KERNEL__ */

/* generic pci stuff */
#include <asm-generic/pci.h>

/* generic DMA-mapping stuff */
#include <asm-generic/pci-dma-compat.h>

#endif /* __ASM_SH_PCI_H */


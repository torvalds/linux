#ifndef __ASM_SH_PCI_H
#define __ASM_SH_PCI_H

#ifdef __KERNEL__

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#define pcibios_assign_all_busses()	1

/*
 * A board can define one or more PCI channels that represent built-in (or
 * external) PCI controllers.
 */
struct pci_channel {
	struct pci_channel	*next;
	struct pci_bus		*bus;

	struct pci_ops		*pci_ops;

	struct resource		*resources;
	unsigned int		nr_resources;

	unsigned long		io_offset;
	unsigned long		mem_offset;

	unsigned long		reg_base;
	unsigned long		io_map_base;

	unsigned int		index;
	unsigned int		need_domain_info;

	/* Optional error handling */
	struct timer_list	err_timer, serr_timer;
	unsigned int		err_irq, serr_irq;
};

/* arch/sh/drivers/pci/pci.c */
extern raw_spinlock_t pci_config_lock;

extern int register_pci_controller(struct pci_channel *hose);
extern void pcibios_report_status(unsigned int status_mask, int warn);

/* arch/sh/drivers/pci/common.c */
extern int early_read_config_byte(struct pci_channel *hose, int top_bus,
				  int bus, int devfn, int offset, u8 *value);
extern int early_read_config_word(struct pci_channel *hose, int top_bus,
				  int bus, int devfn, int offset, u16 *value);
extern int early_read_config_dword(struct pci_channel *hose, int top_bus,
				   int bus, int devfn, int offset, u32 *value);
extern int early_write_config_byte(struct pci_channel *hose, int top_bus,
				   int bus, int devfn, int offset, u8 value);
extern int early_write_config_word(struct pci_channel *hose, int top_bus,
				   int bus, int devfn, int offset, u16 value);
extern int early_write_config_dword(struct pci_channel *hose, int top_bus,
				    int bus, int devfn, int offset, u32 value);
extern void pcibios_enable_timers(struct pci_channel *hose);
extern unsigned int pcibios_handle_status_errors(unsigned long addr,
				 unsigned int status, struct pci_channel *hose);
extern int pci_is_66mhz_capable(struct pci_channel *hose,
				int top_bus, int current_bus);

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
#define PCI_DMA_BUS_IS_PHYS	(dma_ops->is_phys)

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

/* Board-specific fixup routines. */
int pcibios_map_platform_irq(struct pci_dev *dev, u8 slot, u8 pin);

extern void pcibios_resource_to_bus(struct pci_dev *dev,
	struct pci_bus_region *region, struct resource *res);

extern void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
				    struct pci_bus_region *region);

#define pci_domain_nr(bus) ((struct pci_channel *)(bus)->sysdata)->index

static inline int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_channel *hose = bus->sysdata;
	return hose->need_domain_info;
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


/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SH_PCI_H
#define __ASM_SH_PCI_H

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

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

/* Dynamic DMA mapping stuff.
 * SuperH has everything mapped statically like x86.
 */

#ifdef CONFIG_PCI
/*
 * None of the SH PCI controllers support MWI, it is always treated as a
 * direct memory write.
 */
#define PCI_DISABLE_MWI
#endif

/* Board-specific fixup routines. */
int pcibios_map_platform_irq(const struct pci_dev *dev, u8 slot, u8 pin);

#define pci_domain_nr(bus) ((struct pci_channel *)(bus)->sysdata)->index

static inline int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_channel *hose = bus->sysdata;
	return hose->need_domain_info;
}

#endif /* __ASM_SH_PCI_H */

/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Support PCI IO workaround
 *
 * (C) Copyright 2007-2008 TOSHIBA CORPORATION
 */

#ifndef _IO_WORKAROUNDS_H
#define _IO_WORKAROUNDS_H

#ifdef CONFIG_PPC_IO_WORKAROUNDS
#include <linux/io.h>
#include <asm/pci-bridge.h>

/* Bus info */
struct iowa_bus {
	struct pci_controller *phb;
	struct ppc_pci_io *ops;
	void   *private;
};

void iowa_register_bus(struct pci_controller *, struct ppc_pci_io *,
		       int (*)(struct iowa_bus *, void *), void *);
struct iowa_bus *iowa_mem_find_bus(const PCI_IO_ADDR);
struct iowa_bus *iowa_pio_find_bus(unsigned long);

extern struct ppc_pci_io spiderpci_ops;
extern int spiderpci_iowa_init(struct iowa_bus *, void *);

#define SPIDER_PCI_REG_BASE		0xd000
#define SPIDER_PCI_REG_SIZE		0x1000
#define SPIDER_PCI_VCI_CNTL_STAT	0x0110
#define SPIDER_PCI_DUMMY_READ		0x0810
#define SPIDER_PCI_DUMMY_READ_BASE	0x0814

#endif

#if defined(CONFIG_PPC_IO_WORKAROUNDS) && defined(CONFIG_PPC_INDIRECT_MMIO)
extern bool io_workaround_inited;

static inline bool iowa_is_active(void)
{
	return unlikely(io_workaround_inited);
}
#else
static inline bool iowa_is_active(void)
{
	return false;
}
#endif

void __iomem *iowa_ioremap(phys_addr_t addr, unsigned long size,
			   pgprot_t prot, void *caller);

#endif /* _IO_WORKAROUNDS_H */

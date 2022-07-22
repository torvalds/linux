/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_CSKY_PCI_H
#define __ASM_CSKY_PCI_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>

#define PCIBIOS_MIN_IO		0
#define PCIBIOS_MIN_MEM		0

/* C-SKY shim does not initialize PCI bus */
#define pcibios_assign_all_busses() 1

#ifdef CONFIG_PCI
static inline int pci_proc_domain(struct pci_bus *bus)
{
	/* always show the domain in /proc */
	return 1;
}
#endif  /* CONFIG_PCI */

#endif  /* __ASM_CSKY_PCI_H */

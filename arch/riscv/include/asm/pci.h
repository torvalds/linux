/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 SiFive
 */

#ifndef _ASM_RISCV_PCI_H
#define _ASM_RISCV_PCI_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>

#define PCIBIOS_MIN_IO		0
#define PCIBIOS_MIN_MEM		0

/* RISC-V shim does not initialize PCI bus */
#define pcibios_assign_all_busses() 1

#define ARCH_GENERIC_PCI_MMAP_RESOURCE 1

extern int isa_dma_bridge_buggy;

#ifdef CONFIG_PCI
static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	/* no legacy IRQ on risc-v */
	return -ENODEV;
}

static inline int pci_proc_domain(struct pci_bus *bus)
{
	/* always show the domain in /proc */
	return 1;
}

#ifdef	CONFIG_NUMA

static inline int pcibus_to_node(struct pci_bus *bus)
{
	return dev_to_node(&bus->dev);
}
#ifndef cpumask_of_pcibus
#define cpumask_of_pcibus(bus)	(pcibus_to_node(bus) == -1 ?		\
				 cpu_all_mask :				\
				 cpumask_of_node(pcibus_to_node(bus)))
#endif
#endif	/* CONFIG_NUMA */

#endif  /* CONFIG_PCI */

#endif  /* _ASM_RISCV_PCI_H */

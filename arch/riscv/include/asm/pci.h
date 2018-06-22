/*
 * Copyright (C) 2016 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef __ASM_RISCV_PCI_H
#define __ASM_RISCV_PCI_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>

#define PCIBIOS_MIN_IO		0
#define PCIBIOS_MIN_MEM		0

/* RISC-V shim does not initialize PCI bus */
#define pcibios_assign_all_busses() 1

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
#endif  /* CONFIG_PCI */

#endif  /* __ASM_PCI_H */

/*
 * arch/sh/drivers/pci/pci.c
 *
 * Copyright (c) 2002 M. R. Brown  <mrbrown@linux-sh.org>
 * Copyright (c) 2004 - 2006 Paul Mundt  <lethal@linux-sh.org>
 *
 * These functions are collected here to reduce duplication of common
 * code amongst the many platform-specific PCI support code files.
 *
 * These routines require the following board-specific routines:
 * void pcibios_fixup_irqs();
 *
 * See include/asm-sh/pci.h for more information.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/dma-debug.h>
#include <asm/io.h>

static int __init pcibios_init(void)
{
	struct pci_channel *p;
	struct pci_bus *bus;
	int busno;

	/* init channels */
	busno = 0;
	for (p = board_pci_channels; p->init; p++) {
		if (p->init(p) == 0)
			p->enabled = 1;
		else
			pr_err("Unable to init pci channel %d\n", busno);
		busno++;
	}

#ifdef CONFIG_PCI_AUTO
	/* assign resources */
	busno = 0;
	for (p = board_pci_channels; p->init; p++)
		if (p->enabled)
			busno = pciauto_assign_resources(busno, p) + 1;
#endif

	/* scan the buses */
	busno = 0;
	for (p = board_pci_channels; p->init; p++) {
		if (p->enabled) {
			bus = pci_scan_bus(busno, p->pci_ops, p);
			busno = bus->subordinate + 1;
		}
	}

	pci_fixup_irqs(pci_common_swizzle, pcibios_map_platform_irq);

	dma_debug_add_bus(&pci_bus_type);

	return 0;
}
subsys_initcall(pcibios_init);

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void __devinit __weak pcibios_fixup_bus(struct pci_bus *bus)
{
	pci_read_bridge_bases(bus);
}

EXPORT_SYMBOL(board_pci_channels);

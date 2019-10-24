/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Christoph Hellwig (hch@lst.de)
 * Copyright (C) 1999, 2000, 04 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#include <asm/pci/bridge.h>

#ifdef CONFIG_NUMA
int pcibus_to_node(struct pci_bus *bus)
{
	struct bridge_controller *bc = BRIDGE_CONTROLLER(bus);

	return bc->nasid;
}
EXPORT_SYMBOL(pcibus_to_node);
#endif /* CONFIG_NUMA */

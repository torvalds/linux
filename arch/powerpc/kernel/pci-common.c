/*
 * Contains common pci routines for ALL ppc platform
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/firmware.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

/*
 * Return the domain number for this bus.
 */
int pci_domain_nr(struct pci_bus *bus)
{
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return 0;
	else {
		struct pci_controller *hose = pci_bus_to_host(bus);

		return hose->global_number;
	}
}

EXPORT_SYMBOL(pci_domain_nr);

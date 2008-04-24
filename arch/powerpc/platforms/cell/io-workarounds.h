/*
 * Support PCI IO workaround
 *
 * (C) Copyright 2007-2008 TOSHIBA CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _IO_WORKAROUNDS_H
#define _IO_WORKAROUNDS_H

#include <linux/io.h>
#include <asm/pci-bridge.h>

/* Bus info */
struct iowa_bus {
	struct pci_controller *phb;
	struct ppc_pci_io *ops;
	void   *private;
};

void __init io_workaround_init(void);
void __init iowa_register_bus(struct pci_controller *, struct ppc_pci_io *,
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

#endif /* _IO_WORKAROUNDS_H */

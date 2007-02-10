/*
 * pci prototypes for Celleb platform
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
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

#ifndef _CELLEB_PCI_H
#define _CELLEB_PCI_H

#include <linux/pci.h>

#include <asm/pci-bridge.h>
#include <asm/prom.h>

extern int celleb_setup_phb(struct pci_controller *);
extern int celleb_pci_probe_mode(struct pci_bus *);

extern struct pci_ops celleb_epci_ops;
extern int celleb_setup_epci(struct device_node *, struct pci_controller *);

#endif /* _CELLEB_PCI_H */

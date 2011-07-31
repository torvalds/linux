/*
 *				powertv-pci.c
 *
 * Copyright (C) 2009  Cisco Systems, Inc.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
/*
 * Local definitions for the powertv PCI code
 */

#ifndef _POWERTV_PCI_POWERTV_PCI_H_
#define _POWERTV_PCI_POWERTV_PCI_H_
extern int asic_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin);
extern int asic_pcie_init(void);
extern int asic_pcie_init(void);

extern int log_level;
#endif

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright (C) 2013 Red Hat, Inc.
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 */
#ifndef __IOMMU_PCI_H
#define __IOMMU_PCI_H

/* Helper function for swapping pci device reference */
static inline void swap_pci_ref(struct pci_dev **from, struct pci_dev *to)
{
	pci_dev_put(*from);
	*from = to;
}

#endif  /* __IOMMU_PCI_H */

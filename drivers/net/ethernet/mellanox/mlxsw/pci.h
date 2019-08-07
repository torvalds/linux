/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2016-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_PCI_H
#define _MLXSW_PCI_H

#include <linux/pci.h>

#define PCI_DEVICE_ID_MELLANOX_SWITCHX2		0xc738
#define PCI_DEVICE_ID_MELLANOX_SPECTRUM		0xcb84
#define PCI_DEVICE_ID_MELLANOX_SPECTRUM2	0xcf6c
#define PCI_DEVICE_ID_MELLANOX_SPECTRUM3	0xcf70
#define PCI_DEVICE_ID_MELLANOX_SWITCHIB		0xcb20
#define PCI_DEVICE_ID_MELLANOX_SWITCHIB2	0xcf08

#if IS_ENABLED(CONFIG_MLXSW_PCI)

int mlxsw_pci_driver_register(struct pci_driver *pci_driver);
void mlxsw_pci_driver_unregister(struct pci_driver *pci_driver);

#else

static inline int
mlxsw_pci_driver_register(struct pci_driver *pci_driver)
{
	return 0;
}

static inline void
mlxsw_pci_driver_unregister(struct pci_driver *pci_driver)
{
}

#endif

#endif

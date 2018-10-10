/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 CGX driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CGX_H
#define CGX_H

 /* PCI device IDs */
#define	PCI_DEVID_OCTEONTX2_CGX			0xA059

/* PCI BAR nos */
#define PCI_CFG_REG_BAR_NUM			0

extern struct pci_driver cgx_driver;

#endif /* CGX_H */

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
#define	PCI_DEVID_OCTEONTX2_CGX		0xA059

/* PCI BAR nos */
#define PCI_CFG_REG_BAR_NUM		0

#define MAX_CGX				3
#define MAX_LMAC_PER_CGX		4
#define CGX_OFFSET(x)			((x) * MAX_LMAC_PER_CGX)

/* Registers */
#define CGXX_CMRX_RX_ID_MAP		0x060
#define CGXX_CMRX_RX_LMACS		0x128

extern struct pci_driver cgx_driver;

int cgx_get_cgx_cnt(void);
int cgx_get_lmac_cnt(void *cgxd);
void *cgx_get_pdata(int cgx_id);
#endif /* CGX_H */

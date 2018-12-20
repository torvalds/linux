/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef	_QTN_HW_IDS_H_
#define	_QTN_HW_IDS_H_

#include <linux/pci_ids.h>

#define PCIE_VENDOR_ID_QUANTENNA	(0x1bb5)

/* PCIE Device IDs */

#define	PCIE_DEVICE_ID_QTN_PEARL	(0x0008)

#define QTN_REG_SYS_CTRL_CSR		0x14
#define QTN_CHIP_ID_MASK		0xF0
#define QTN_CHIP_ID_TOPAZ		0x40
#define QTN_CHIP_ID_PEARL		0x50
#define QTN_CHIP_ID_PEARL_B		0x60
#define QTN_CHIP_ID_PEARL_C		0x70

/* FW names */

#define QTN_PCI_PEARL_FW_NAME		"qtn/fmac_qsr10g.img"

static inline unsigned int qtnf_chip_id_get(const void __iomem *regs_base)
{
	u32 board_rev = readl(regs_base + QTN_REG_SYS_CTRL_CSR);

	return board_rev & QTN_CHIP_ID_MASK;
}

#endif	/* _QTN_HW_IDS_H_ */

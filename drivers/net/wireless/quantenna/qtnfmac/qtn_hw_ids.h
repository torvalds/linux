/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2015-2016 Quantenna Communications. All rights reserved. */

#ifndef	_QTN_HW_IDS_H_
#define	_QTN_HW_IDS_H_

#include <linux/pci_ids.h>

#define PCIE_VENDOR_ID_QUANTENNA	(0x1bb5)

/* PCIE Device IDs */

#define	PCIE_DEVICE_ID_QSR		(0x0008)

#define QTN_REG_SYS_CTRL_CSR		0x14
#define QTN_CHIP_ID_MASK		0xF0
#define QTN_CHIP_ID_TOPAZ		0x40
#define QTN_CHIP_ID_PEARL		0x50
#define QTN_CHIP_ID_PEARL_B		0x60
#define QTN_CHIP_ID_PEARL_C		0x70

/* FW names */

#define QTN_PCI_PEARL_FW_NAME		"qtn/fmac_qsr10g.img"
#define QTN_PCI_TOPAZ_FW_NAME		"qtn/fmac_qsr1000.img"
#define QTN_PCI_TOPAZ_BOOTLD_NAME	"qtn/uboot_qsr1000.img"

static inline unsigned int qtnf_chip_id_get(const void __iomem *regs_base)
{
	u32 board_rev = readl(regs_base + QTN_REG_SYS_CTRL_CSR);

	return board_rev & QTN_CHIP_ID_MASK;
}

#endif	/* _QTN_HW_IDS_H_ */

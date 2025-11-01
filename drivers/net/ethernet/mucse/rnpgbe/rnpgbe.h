/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_H
#define _RNPGBE_H

enum rnpgbe_boards {
	board_n500,
	board_n210
};

struct mucse_hw {
	void __iomem *hw_addr;
};

struct mucse {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct mucse_hw hw;
};

/* Device IDs */
#define PCI_VENDOR_ID_MUCSE               0x8848
#define RNPGBE_DEVICE_ID_N500_QUAD_PORT   0x8308
#define RNPGBE_DEVICE_ID_N500_DUAL_PORT   0x8318
#define RNPGBE_DEVICE_ID_N210             0x8208
#define RNPGBE_DEVICE_ID_N210L            0x820a
#endif /* _RNPGBE_H */

/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_H
#define _RNPGBE_H

#include <linux/types.h>
#include <linux/mutex.h>

enum rnpgbe_boards {
	board_n500,
	board_n210
};

struct mucse_mbx_info {
	u32 timeout_us;
	u32 delay_us;
	u16 fw_req;
	u16 fw_ack;
	/* lock for only one use mbx */
	struct mutex lock;
	/* fw <--> pf mbx */
	u32 fwpf_shm_base;
	u32 pf2fw_mbx_ctrl;
	u32 fwpf_mbx_mask;
	u32 fwpf_ctrl_base;
};

struct mucse_hw {
	void __iomem *hw_addr;
	struct mucse_mbx_info mbx;
	u8 pfvfnum;
};

struct mucse {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct mucse_hw hw;
};

int rnpgbe_init_hw(struct mucse_hw *hw, int board_type);

/* Device IDs */
#define PCI_VENDOR_ID_MUCSE               0x8848
#define RNPGBE_DEVICE_ID_N500_QUAD_PORT   0x8308
#define RNPGBE_DEVICE_ID_N500_DUAL_PORT   0x8318
#define RNPGBE_DEVICE_ID_N210             0x8208
#define RNPGBE_DEVICE_ID_N210L            0x820a
#endif /* _RNPGBE_H */

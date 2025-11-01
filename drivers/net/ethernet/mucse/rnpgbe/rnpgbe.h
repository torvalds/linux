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

/* Enum for firmware notification modes,
 * more modes (e.g., portup, link_report) will be added in future
 **/
enum {
	mucse_fw_powerup,
};

struct mucse_hw {
	void __iomem *hw_addr;
	struct pci_dev *pdev;
	struct mucse_mbx_info mbx;
	int port;
	u8 pfvfnum;
};

struct mucse_stats {
	u64 tx_dropped;
};

struct mucse {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct mucse_hw hw;
	struct mucse_stats stats;
};

int rnpgbe_get_permanent_mac(struct mucse_hw *hw, u8 *perm_addr);
int rnpgbe_reset_hw(struct mucse_hw *hw);
int rnpgbe_send_notify(struct mucse_hw *hw,
		       bool enable,
		       int mode);
int rnpgbe_init_hw(struct mucse_hw *hw, int board_type);

/* Device IDs */
#define PCI_VENDOR_ID_MUCSE               0x8848
#define RNPGBE_DEVICE_ID_N500_QUAD_PORT   0x8308
#define RNPGBE_DEVICE_ID_N500_DUAL_PORT   0x8318
#define RNPGBE_DEVICE_ID_N210             0x8208
#define RNPGBE_DEVICE_ID_N210L            0x820a

#define mucse_hw_wr32(hw, reg, val) \
	writel((val), (hw)->hw_addr + (reg))
#endif /* _RNPGBE_H */

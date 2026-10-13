/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_H
#define _RNPGBE_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/if.h>
#include <linux/workqueue.h>

#include "rnpgbe_hw.h"

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
	void __iomem *ring_msix_base;
	struct pci_dev *pdev;
	struct mucse_mbx_info mbx;
	int port;
	u8 pfvfnum;
};

struct mucse_ring {
	struct mucse_ring *next;
	struct mucse_q_vector *q_vector;
	void __iomem *ring_addr;
	void __iomem *irq_mask;
	void __iomem *trig;
	u8 queue_index;
	/* hw ring idx */
	u8 rnpgbe_queue_idx;
} ____cacheline_internodealigned_in_smp;

struct mucse_ring_container {
	struct mucse_ring *ring;
	u16 count;
};

struct mucse_q_vector {
	struct mucse *mucse;
	/* hardware interrupt vector number */
	int hw_vector;
	struct mucse_ring_container rx, tx;
	struct napi_struct napi;
	char name[IFNAMSIZ + 18];
	/* for dynamic allocation of rings associated with this q_vector */
	struct mucse_ring ring[] ____cacheline_internodealigned_in_smp;
};

struct mucse_stats {
	u64 tx_dropped;
};

#define MAX_Q_VECTORS 8

struct mucse {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct mucse_hw hw;
	struct mucse_stats stats;
	struct mucse_ring *tx_ring[RNPGBE_MAX_QUEUES]
		____cacheline_aligned_in_smp;
	struct mucse_ring *rx_ring[RNPGBE_MAX_QUEUES]
		____cacheline_aligned_in_smp;
	struct mucse_q_vector *q_vector[MAX_Q_VECTORS];
	int num_tx_queues;
	int num_q_vectors;
	int num_rx_queues;
	char mbx_name[32];
	bool mbx_irq_requested;
	struct work_struct mbx_work;
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
#define mucse_hw_rd32(hw, reg) \
	readl((hw)->hw_addr + (reg))
#endif /* _RNPGBE_H */

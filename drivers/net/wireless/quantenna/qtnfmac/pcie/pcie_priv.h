/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018 Quantenna Communications, Inc. All rights reserved. */

#ifndef _QTN_FMAC_PCIE_H_
#define _QTN_FMAC_PCIE_H_

#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

#include "shm_ipc.h"

#define SKB_BUF_SIZE		2048

#define QTN_FW_DL_TIMEOUT_MS	3000
#define QTN_FW_QLINK_TIMEOUT_MS	30000
#define QTN_EP_RESET_WAIT_MS	1000

struct qtnf_pcie_bus_priv {
	struct pci_dev *pdev;

	spinlock_t tx_reclaim_lock;
	spinlock_t tx_lock;
	int mps;

	struct workqueue_struct *workqueue;
	struct tasklet_struct reclaim_tq;

	void __iomem *sysctl_bar;
	void __iomem *epmem_bar;
	void __iomem *dmareg_bar;

	struct qtnf_shm_ipc shm_ipc_ep_in;
	struct qtnf_shm_ipc shm_ipc_ep_out;

	u16 tx_bd_num;
	u16 rx_bd_num;

	struct sk_buff **tx_skb;
	struct sk_buff **rx_skb;

	u32 rx_bd_w_index;
	u32 rx_bd_r_index;

	u32 tx_bd_w_index;
	u32 tx_bd_r_index;

	/* diagnostics stats */
	u32 pcie_irq_count;
	u32 tx_full_count;
	u32 tx_done_count;
	u32 tx_reclaim_done;
	u32 tx_reclaim_req;

	u8 msi_enabled;
	u8 tx_stopped;
};

static inline void qtnf_non_posted_write(u32 val, void __iomem *basereg)
{
	writel(val, basereg);

	/* flush posted write */
	readl(basereg);
}

#endif /* _QTN_FMAC_PCIE_H_ */

/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021-2021 Hisilicon Limited.

#ifndef __HCLGE_COMM_TQP_STATS_H
#define __HCLGE_COMM_TQP_STATS_H
#include <linux/types.h>
#include <linux/etherdevice.h>
#include "hnae3.h"

/* each tqp has TX & RX two queues */
#define HCLGE_COMM_QUEUE_PAIR_SIZE 2

/* TQP stats */
struct hclge_comm_tqp_stats {
	/* query_tqp_tx_queue_statistics ,opcode id:  0x0B03 */
	u64 rcb_tx_ring_pktnum_rcd; /* 32bit */
	/* query_tqp_rx_queue_statistics ,opcode id:  0x0B13 */
	u64 rcb_rx_ring_pktnum_rcd; /* 32bit */
};

struct hclge_comm_tqp {
	/* copy of device pointer from pci_dev,
	 * used when perform DMA mapping
	 */
	struct device *dev;
	struct hnae3_queue q;
	struct hclge_comm_tqp_stats tqp_stats;
	u16 index;	/* Global index in a NIC controller */

	bool alloced;
};

u64 *hclge_comm_tqps_get_stats(struct hnae3_handle *handle, u64 *data);
int hclge_comm_tqps_get_sset_count(struct hnae3_handle *handle);
void hclge_comm_tqps_get_strings(struct hnae3_handle *handle, u8 **data);
void hclge_comm_reset_tqp_stats(struct hnae3_handle *handle);
int hclge_comm_tqps_update_stats(struct hnae3_handle *handle,
				 struct hclge_comm_hw *hw);
#endif

/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_TX_H_
#define _HINIC3_TX_H_

#include <linux/bitops.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <net/checksum.h>

#define VXLAN_OFFLOAD_PORT_LE            cpu_to_be16(4789)
#define TCP_HDR_DATA_OFF_UNIT_SHIFT      2
#define TRANSPORT_OFFSET(l4_hdr, skb)    ((l4_hdr) - (skb)->data)

#define HINIC3_COMPACT_WQEE_SKB_MAX_LEN  16383
#define HINIC3_TX_POLL_WEIGHT		 64
#define HINIC3_DEFAULT_STOP_THRS	 6
#define HINIC3_DEFAULT_START_THRS	 24

enum sq_wqe_data_format {
	SQ_NORMAL_WQE = 0,
};

enum sq_wqe_ec_type {
	SQ_WQE_COMPACT_TYPE  = 0,
	SQ_WQE_EXTENDED_TYPE = 1,
};

enum sq_wqe_tasksect_len_type {
	SQ_WQE_TASKSECT_46BITS  = 0,
	SQ_WQE_TASKSECT_16BYTES = 1,
};

enum hinic3_tx_offload_type {
	HINIC3_TX_OFFLOAD_TSO     = BIT(0),
	HINIC3_TX_OFFLOAD_CSUM    = BIT(1),
	HINIC3_TX_OFFLOAD_VLAN    = BIT(2),
	HINIC3_TX_OFFLOAD_INVALID = BIT(3),
	HINIC3_TX_OFFLOAD_ESP     = BIT(4),
};

#define SQ_CTRL_BUFDESC_NUM_MASK   GENMASK(26, 19)
#define SQ_CTRL_TASKSECT_LEN_MASK  BIT(27)
#define SQ_CTRL_DATA_FORMAT_MASK   BIT(28)
#define SQ_CTRL_EXTENDED_MASK      BIT(30)
#define SQ_CTRL_OWNER_MASK         BIT(31)
#define SQ_CTRL_SET(val, member) \
	FIELD_PREP(SQ_CTRL_##member##_MASK, val)

#define SQ_CTRL_QUEUE_INFO_PLDOFF_MASK  GENMASK(9, 2)
#define SQ_CTRL_QUEUE_INFO_UFO_MASK     BIT(10)
#define SQ_CTRL_QUEUE_INFO_TSO_MASK     BIT(11)
#define SQ_CTRL_QUEUE_INFO_MSS_MASK     GENMASK(26, 13)
#define SQ_CTRL_QUEUE_INFO_UC_MASK      BIT(28)

#define SQ_CTRL_QUEUE_INFO_SET(val, member) \
	FIELD_PREP(SQ_CTRL_QUEUE_INFO_##member##_MASK, val)
#define SQ_CTRL_QUEUE_INFO_GET(val, member) \
	FIELD_GET(SQ_CTRL_QUEUE_INFO_##member##_MASK, le32_to_cpu(val))

#define SQ_CTRL_MAX_PLDOFF  221

#define SQ_TASK_INFO0_TUNNEL_FLAG_MASK  BIT(19)
#define SQ_TASK_INFO0_INNER_L4_EN_MASK  BIT(24)
#define SQ_TASK_INFO0_INNER_L3_EN_MASK  BIT(25)
#define SQ_TASK_INFO0_OUT_L4_EN_MASK    BIT(27)
#define SQ_TASK_INFO0_OUT_L3_EN_MASK    BIT(28)
#define SQ_TASK_INFO0_SET(val, member) \
	FIELD_PREP(SQ_TASK_INFO0_##member##_MASK, val)

#define SQ_TASK_INFO3_VLAN_TAG_MASK        GENMASK(15, 0)
#define SQ_TASK_INFO3_VLAN_TPID_MASK       GENMASK(18, 16)
#define SQ_TASK_INFO3_VLAN_TAG_VALID_MASK  BIT(19)
#define SQ_TASK_INFO3_SET(val, member) \
	FIELD_PREP(SQ_TASK_INFO3_##member##_MASK, val)

struct hinic3_sq_wqe_desc {
	__le32 ctrl_len;
	__le32 queue_info;
	__le32 hi_addr;
	__le32 lo_addr;
};

struct hinic3_sq_task {
	__le32 pkt_info0;
	__le32 ip_identify;
	__le32 rsvd;
	__le32 vlan_offload;
};

struct hinic3_sq_wqe_combo {
	struct hinic3_sq_wqe_desc *ctrl_bd0;
	struct hinic3_sq_task     *task;
	struct hinic3_sq_bufdesc  *bds_head;
	struct hinic3_sq_bufdesc  *bds_sec2;
	u16                       first_bds_num;
	u32                       wqe_type;
	u32                       task_type;
};

struct hinic3_dma_info {
	dma_addr_t dma;
	u32        len;
};

struct hinic3_tx_info {
	struct sk_buff         *skb;
	u16                    wqebb_cnt;
	struct hinic3_dma_info *dma_info;
};

struct hinic3_txq {
	struct net_device       *netdev;
	struct device           *dev;

	u16                     q_id;
	u16                     tx_stop_thrs;
	u16                     tx_start_thrs;
	u32                     q_mask;
	u32                     q_depth;

	struct hinic3_tx_info   *tx_info;
	struct hinic3_io_queue  *sq;
} ____cacheline_aligned;

struct hinic3_dyna_txq_res {
	struct hinic3_tx_info  *tx_info;
	struct hinic3_dma_info *bds;
};

int hinic3_alloc_txqs(struct net_device *netdev);
void hinic3_free_txqs(struct net_device *netdev);

int hinic3_alloc_txqs_res(struct net_device *netdev, u16 num_sq,
			  u32 sq_depth, struct hinic3_dyna_txq_res *txqs_res);
void hinic3_free_txqs_res(struct net_device *netdev, u16 num_sq,
			  u32 sq_depth, struct hinic3_dyna_txq_res *txqs_res);
int hinic3_configure_txqs(struct net_device *netdev, u16 num_sq,
			  u32 sq_depth, struct hinic3_dyna_txq_res *txqs_res);

netdev_tx_t hinic3_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
bool hinic3_tx_poll(struct hinic3_txq *txq, int budget);
void hinic3_flush_txqs(struct net_device *netdev);

#endif

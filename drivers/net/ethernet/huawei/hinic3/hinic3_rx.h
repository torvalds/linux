/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved. */

#ifndef _HINIC3_RX_H_
#define _HINIC3_RX_H_

#include <linux/bitfield.h>
#include <linux/netdevice.h>

#define RQ_CQE_OFFOLAD_TYPE_PKT_TYPE_MASK           GENMASK(4, 0)
#define RQ_CQE_OFFOLAD_TYPE_IP_TYPE_MASK            GENMASK(6, 5)
#define RQ_CQE_OFFOLAD_TYPE_TUNNEL_PKT_FORMAT_MASK  GENMASK(11, 8)
#define RQ_CQE_OFFOLAD_TYPE_VLAN_EN_MASK            BIT(21)
#define RQ_CQE_OFFOLAD_TYPE_GET(val, member) \
	FIELD_GET(RQ_CQE_OFFOLAD_TYPE_##member##_MASK, val)

#define RQ_CQE_SGE_VLAN_MASK  GENMASK(15, 0)
#define RQ_CQE_SGE_LEN_MASK   GENMASK(31, 16)
#define RQ_CQE_SGE_GET(val, member) \
	FIELD_GET(RQ_CQE_SGE_##member##_MASK, val)

#define RQ_CQE_STATUS_CSUM_ERR_MASK  GENMASK(15, 0)
#define RQ_CQE_STATUS_NUM_LRO_MASK   GENMASK(23, 16)
#define RQ_CQE_STATUS_RXDONE_MASK    BIT(31)
#define RQ_CQE_STATUS_GET(val, member) \
	FIELD_GET(RQ_CQE_STATUS_##member##_MASK, val)

/* RX Completion information that is provided by HW for a specific RX WQE */
struct hinic3_rq_cqe {
	__le32 status;
	__le32 vlan_len;
	__le32 offload_type;
	__le32 rsvd3;
	__le32 rsvd4;
	__le32 rsvd5;
	__le32 rsvd6;
	__le32 pkt_info;
};

struct hinic3_rq_wqe {
	__le32 buf_hi_addr;
	__le32 buf_lo_addr;
	__le32 cqe_hi_addr;
	__le32 cqe_lo_addr;
};

struct hinic3_rx_info {
	struct page      *page;
	u32              page_offset;
};

struct hinic3_rxq {
	struct net_device       *netdev;

	u16                     q_id;
	u32                     q_depth;
	u32                     q_mask;

	u16                     buf_len;
	u32                     buf_len_shift;

	u32                     cons_idx;
	u32                     delta;

	u32                     irq_id;
	u16                     msix_entry_idx;

	/* cqe_arr and rx_info are arrays of rq_depth elements. Each element is
	 * statically associated (by index) to a specific rq_wqe.
	 */
	struct hinic3_rq_cqe   *cqe_arr;
	struct hinic3_rx_info  *rx_info;
	struct page_pool       *page_pool;

	struct hinic3_io_queue *rq;

	struct hinic3_irq_cfg  *irq_cfg;
	u16                    next_to_alloc;
	u16                    next_to_update;
	struct device          *dev; /* device for DMA mapping */

	dma_addr_t             cqe_start_paddr;
} ____cacheline_aligned;

struct hinic3_dyna_rxq_res {
	u16                   next_to_alloc;
	struct hinic3_rx_info *rx_info;
	dma_addr_t            cqe_start_paddr;
	void                  *cqe_start_vaddr;
	struct page_pool      *page_pool;
};

int hinic3_alloc_rxqs(struct net_device *netdev);
void hinic3_free_rxqs(struct net_device *netdev);

int hinic3_alloc_rxqs_res(struct net_device *netdev, u16 num_rq,
			  u32 rq_depth, struct hinic3_dyna_rxq_res *rxqs_res);
void hinic3_free_rxqs_res(struct net_device *netdev, u16 num_rq,
			  u32 rq_depth, struct hinic3_dyna_rxq_res *rxqs_res);
int hinic3_configure_rxqs(struct net_device *netdev, u16 num_rq,
			  u32 rq_depth, struct hinic3_dyna_rxq_res *rxqs_res);
int hinic3_rx_poll(struct hinic3_rxq *rxq, int budget);

#endif

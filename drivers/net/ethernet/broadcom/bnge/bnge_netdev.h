/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_NETDEV_H_
#define _BNGE_NETDEV_H_

#include <linux/bnxt/hsi.h>

struct tx_bd {
	__le32 tx_bd_len_flags_type;
	#define TX_BD_TYPE					(0x3f << 0)
	#define TX_BD_TYPE_SHORT_TX_BD				(0x00 << 0)
	#define TX_BD_TYPE_LONG_TX_BD				(0x10 << 0)
	#define TX_BD_FLAGS_PACKET_END				(1 << 6)
	#define TX_BD_FLAGS_NO_CMPL				(1 << 7)
	#define TX_BD_FLAGS_BD_CNT				(0x1f << 8)
	#define TX_BD_FLAGS_BD_CNT_SHIFT			8
	#define TX_BD_FLAGS_LHINT				(3 << 13)
	#define TX_BD_FLAGS_LHINT_SHIFT				13
	#define TX_BD_FLAGS_LHINT_512_AND_SMALLER		(0 << 13)
	#define TX_BD_FLAGS_LHINT_512_TO_1023			(1 << 13)
	#define TX_BD_FLAGS_LHINT_1024_TO_2047			(2 << 13)
	#define TX_BD_FLAGS_LHINT_2048_AND_LARGER		(3 << 13)
	#define TX_BD_FLAGS_COAL_NOW				(1 << 15)
	#define TX_BD_LEN					(0xffff << 16)
	#define TX_BD_LEN_SHIFT					16
	u32 tx_bd_opaque;
	__le64 tx_bd_haddr;
} __packed;

struct rx_bd {
	__le32 rx_bd_len_flags_type;
	#define RX_BD_TYPE					(0x3f << 0)
	#define RX_BD_TYPE_RX_PACKET_BD				0x4
	#define RX_BD_TYPE_RX_BUFFER_BD				0x5
	#define RX_BD_TYPE_RX_AGG_BD				0x6
	#define RX_BD_TYPE_16B_BD_SIZE				(0 << 4)
	#define RX_BD_TYPE_32B_BD_SIZE				(1 << 4)
	#define RX_BD_TYPE_48B_BD_SIZE				(2 << 4)
	#define RX_BD_TYPE_64B_BD_SIZE				(3 << 4)
	#define RX_BD_FLAGS_SOP					(1 << 6)
	#define RX_BD_FLAGS_EOP					(1 << 7)
	#define RX_BD_FLAGS_BUFFERS				(3 << 8)
	#define RX_BD_FLAGS_1_BUFFER_PACKET			(0 << 8)
	#define RX_BD_FLAGS_2_BUFFER_PACKET			(1 << 8)
	#define RX_BD_FLAGS_3_BUFFER_PACKET			(2 << 8)
	#define RX_BD_FLAGS_4_BUFFER_PACKET			(3 << 8)
	#define RX_BD_LEN					(0xffff << 16)
	#define RX_BD_LEN_SHIFT					16
	u32 rx_bd_opaque;
	__le64 rx_bd_haddr;
};

struct tx_cmp {
	__le32 tx_cmp_flags_type;
	#define CMP_TYPE					(0x3f << 0)
	#define CMP_TYPE_TX_L2_CMP				0
	#define CMP_TYPE_TX_L2_COAL_CMP				2
	#define CMP_TYPE_TX_L2_PKT_TS_CMP			4
	#define CMP_TYPE_RX_L2_CMP				17
	#define CMP_TYPE_RX_AGG_CMP				18
	#define CMP_TYPE_RX_L2_TPA_START_CMP			19
	#define CMP_TYPE_RX_L2_TPA_END_CMP			21
	#define CMP_TYPE_RX_TPA_AGG_CMP				22
	#define CMP_TYPE_RX_L2_V3_CMP				23
	#define CMP_TYPE_RX_L2_TPA_START_V3_CMP			25
	#define CMP_TYPE_STATUS_CMP				32
	#define CMP_TYPE_REMOTE_DRIVER_REQ			34
	#define CMP_TYPE_REMOTE_DRIVER_RESP			36
	#define CMP_TYPE_ERROR_STATUS				48
	#define CMPL_BASE_TYPE_STAT_EJECT			0x1aUL
	#define CMPL_BASE_TYPE_HWRM_DONE			0x20UL
	#define CMPL_BASE_TYPE_HWRM_FWD_REQ			0x22UL
	#define CMPL_BASE_TYPE_HWRM_FWD_RESP			0x24UL
	#define CMPL_BASE_TYPE_HWRM_ASYNC_EVENT			0x2eUL
	#define TX_CMP_FLAGS_ERROR				(1 << 6)
	#define TX_CMP_FLAGS_PUSH				(1 << 7)
	u32 tx_cmp_opaque;
	__le32 tx_cmp_errors_v;
	#define TX_CMP_V					(1 << 0)
	#define TX_CMP_ERRORS_BUFFER_ERROR			(7 << 1)
	#define TX_CMP_ERRORS_BUFFER_ERROR_NO_ERROR		0
	#define TX_CMP_ERRORS_BUFFER_ERROR_BAD_FORMAT		2
	#define TX_CMP_ERRORS_BUFFER_ERROR_INVALID_STAG		4
	#define TX_CMP_ERRORS_BUFFER_ERROR_STAG_BOUNDS		5
	#define TX_CMP_ERRORS_ZERO_LENGTH_PKT			(1 << 4)
	#define TX_CMP_ERRORS_EXCESSIVE_BD_LEN			(1 << 5)
	#define TX_CMP_ERRORS_DMA_ERROR				(1 << 6)
	#define TX_CMP_ERRORS_HINT_TOO_SHORT			(1 << 7)
	__le32 sq_cons_idx;
	#define TX_CMP_SQ_CONS_IDX_MASK				0x00ffffff
};

struct bnge_sw_tx_bd {
	struct sk_buff		*skb;
	DEFINE_DMA_UNMAP_ADDR(mapping);
	DEFINE_DMA_UNMAP_LEN(len);
	struct page		*page;
	u8			is_ts_pkt;
	u8			is_push;
	u8			action;
	unsigned short		nr_frags;
	union {
		u16		rx_prod;
		u16		txts_prod;
	};
};

struct bnge_sw_rx_bd {
	void			*data;
	u8			*data_ptr;
	dma_addr_t		mapping;
};

struct bnge_sw_rx_agg_bd {
	struct page		*page;
	unsigned int		offset;
	dma_addr_t		mapping;
};

#define BNGE_RX_COPY_THRESH     256

#define BNGE_HW_FEATURE_VLAN_ALL_RX	\
		(NETIF_F_HW_VLAN_CTAG_RX | NETIF_F_HW_VLAN_STAG_RX)
#define BNGE_HW_FEATURE_VLAN_ALL_TX	\
		(NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_STAG_TX)

enum {
	BNGE_NET_EN_GRO		= BIT(0),
	BNGE_NET_EN_LRO		= BIT(1),
	BNGE_NET_EN_JUMBO	= BIT(2),
};

#define BNGE_NET_EN_TPA		(BNGE_NET_EN_GRO | BNGE_NET_EN_LRO)

struct bnge_net {
	struct bnge_dev		*bd;
	struct net_device	*netdev;

	u32			priv_flags;

	u32			rx_ring_size;
	u32			rx_buf_size;
	u32			rx_buf_use_size; /* usable size */
	u32			rx_agg_ring_size;
	u32			rx_copy_thresh;
	u32			rx_ring_mask;
	u32			rx_agg_ring_mask;
	u16			rx_nr_pages;
	u16			rx_agg_nr_pages;

	u32			tx_ring_size;
	u32			tx_ring_mask;
	u16			tx_nr_pages;

	/* NQs and Completion rings */
	u32			cp_ring_size;
	u32			cp_ring_mask;
	u32			cp_bit;
	u16			cp_nr_pages;

#define BNGE_L2_FLTR_HASH_SIZE	32
#define BNGE_L2_FLTR_HASH_MASK	(BNGE_L2_FLTR_HASH_SIZE - 1)
	struct hlist_head	l2_fltr_hash_tbl[BNGE_L2_FLTR_HASH_SIZE];
	u32			hash_seed;
	u64			toeplitz_prefix;
};

#define BNGE_DEFAULT_RX_RING_SIZE	511
#define BNGE_DEFAULT_TX_RING_SIZE	511

int bnge_netdev_alloc(struct bnge_dev *bd, int max_irqs);
void bnge_netdev_free(struct bnge_dev *bd);
void bnge_set_ring_params(struct bnge_dev *bd);

#if (BNGE_PAGE_SHIFT == 16)
#define MAX_RX_PAGES_AGG_ENA	1
#define MAX_RX_PAGES		4
#define MAX_RX_AGG_PAGES	4
#define MAX_TX_PAGES		1
#define MAX_CP_PAGES		16
#else
#define MAX_RX_PAGES_AGG_ENA	8
#define MAX_RX_PAGES		32
#define MAX_RX_AGG_PAGES	32
#define MAX_TX_PAGES		8
#define MAX_CP_PAGES		128
#endif

#define BNGE_RX_PAGE_SIZE		(1 << BNGE_RX_PAGE_SHIFT)

#define RX_DESC_CNT			(BNGE_PAGE_SIZE / sizeof(struct rx_bd))
#define TX_DESC_CNT			(BNGE_PAGE_SIZE / sizeof(struct tx_bd))
#define CP_DESC_CNT			(BNGE_PAGE_SIZE / sizeof(struct tx_cmp))
#define SW_RXBD_RING_SIZE		(sizeof(struct bnge_sw_rx_bd) * RX_DESC_CNT)
#define HW_RXBD_RING_SIZE		(sizeof(struct rx_bd) * RX_DESC_CNT)
#define SW_RXBD_AGG_RING_SIZE		(sizeof(struct bnge_sw_rx_agg_bd) * RX_DESC_CNT)
#define SW_TXBD_RING_SIZE		(sizeof(struct bnge_sw_tx_bd) * TX_DESC_CNT)
#define HW_TXBD_RING_SIZE		(sizeof(struct tx_bd) * TX_DESC_CNT)
#define HW_CMPD_RING_SIZE		(sizeof(struct tx_cmp) * CP_DESC_CNT)
#define BNGE_MAX_RX_DESC_CNT		(RX_DESC_CNT * MAX_RX_PAGES - 1)
#define BNGE_MAX_RX_DESC_CNT_JUM_ENA	(RX_DESC_CNT * MAX_RX_PAGES_AGG_ENA - 1)
#define BNGE_MAX_RX_JUM_DESC_CNT	(RX_DESC_CNT * MAX_RX_AGG_PAGES - 1)
#define BNGE_MAX_TX_DESC_CNT		(TX_DESC_CNT * MAX_TX_PAGES - 1)

#endif /* _BNGE_NETDEV_H_ */

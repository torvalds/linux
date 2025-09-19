/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Broadcom */

#ifndef _BNGE_NETDEV_H_
#define _BNGE_NETDEV_H_

#include <linux/bnxt/hsi.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/refcount.h>
#include "bnge_db.h"

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
	netmem_ref		netmem;
	unsigned int		offset;
	dma_addr_t		mapping;
};

#define HWRM_RING_ALLOC_TX	0x1
#define HWRM_RING_ALLOC_RX	0x2
#define HWRM_RING_ALLOC_AGG	0x4
#define HWRM_RING_ALLOC_CMPL	0x8
#define HWRM_RING_ALLOC_NQ	0x10

struct bnge_ring_grp_info {
	u16	fw_stats_ctx;
	u16	fw_grp_id;
	u16	rx_fw_ring_id;
	u16	agg_fw_ring_id;
	u16	nq_fw_ring_id;
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

/* Minimum TX BDs for a TX packet with MAX_SKB_FRAGS + 1. We need one extra
 * BD because the first TX BD is always a long BD.
 */
#define BNGE_MIN_TX_DESC_CNT	(MAX_SKB_FRAGS + 2)

#define RX_RING(bn, x)	(((x) & (bn)->rx_ring_mask) >> (BNGE_PAGE_SHIFT - 4))
#define RX_AGG_RING(bn, x)	(((x) & (bn)->rx_agg_ring_mask) >>	\
				 (BNGE_PAGE_SHIFT - 4))
#define RX_IDX(x)	((x) & (RX_DESC_CNT - 1))

#define TX_RING(bn, x)	(((x) & (bn)->tx_ring_mask) >> (BNGE_PAGE_SHIFT - 4))
#define TX_IDX(x)	((x) & (TX_DESC_CNT - 1))

#define CP_RING(x)	(((x) & ~(CP_DESC_CNT - 1)) >> (BNGE_PAGE_SHIFT - 4))
#define CP_IDX(x)	((x) & (CP_DESC_CNT - 1))

#define RING_RX(bn, idx)	((idx) & (bn)->rx_ring_mask)
#define NEXT_RX(idx)		((idx) + 1)

#define RING_RX_AGG(bn, idx)	((idx) & (bn)->rx_agg_ring_mask)
#define NEXT_RX_AGG(idx)	((idx) + 1)

#define BNGE_NQ_HDL_TYPE_SHIFT	24
#define BNGE_NQ_HDL_TYPE_RX	0x00
#define BNGE_NQ_HDL_TYPE_TX	0x01

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

	struct bnge_napi		**bnapi;

	struct bnge_rx_ring_info	*rx_ring;
	struct bnge_tx_ring_info	*tx_ring;

	u16				*tx_ring_map;
	enum dma_data_direction		rx_dir;

	/* grp_info indexed by napi/nq index */
	struct bnge_ring_grp_info	*grp_info;
	struct bnge_vnic_info		*vnic_info;
	int				nr_vnics;
	int				total_irqs;

	u32			tx_wake_thresh;
	u16			rx_offset;
	u16			rx_dma_offset;

	u8			rss_hash_key[HW_HASH_KEY_SIZE];
	u8			rss_hash_key_valid:1;
	u8			rss_hash_key_updated:1;
	int			rsscos_nr_ctxs;
	u32			stats_coal_ticks;
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

#define BNGE_MAX_TXR_PER_NAPI	8

#define bnge_for_each_napi_tx(iter, bnapi, txr)		\
	for (iter = 0, txr = (bnapi)->tx_ring[0]; txr;	\
	     txr = (iter < BNGE_MAX_TXR_PER_NAPI - 1) ?	\
	     (bnapi)->tx_ring[++iter] : NULL)

#define BNGE_SET_NQ_HDL(cpr)						\
	(((cpr)->cp_ring_type << BNGE_NQ_HDL_TYPE_SHIFT) | (cpr)->cp_idx)

struct bnge_stats_mem {
	u64		*sw_stats;
	u64		*hw_masks;
	void		*hw_stats;
	dma_addr_t	hw_stats_map;
	int		len;
};

struct bnge_cp_ring_info {
	struct bnge_napi	*bnapi;
	dma_addr_t		*desc_mapping;
	struct tx_cmp		**desc_ring;
	struct bnge_ring_struct	ring_struct;
	u8			cp_ring_type;
	u8			cp_idx;
	u32			cp_raw_cons;
	struct bnge_db_info	cp_db;
};

struct bnge_nq_ring_info {
	struct bnge_napi	*bnapi;
	dma_addr_t		*desc_mapping;
	struct nqe_cn		**desc_ring;
	struct bnge_ring_struct	ring_struct;
	u32			nq_raw_cons;
	struct bnge_db_info	nq_db;

	struct bnge_stats_mem	stats;
	u32			hw_stats_ctx_id;

	int				cp_ring_count;
	struct bnge_cp_ring_info	*cp_ring_arr;
};

struct bnge_rx_ring_info {
	struct bnge_napi	*bnapi;
	struct bnge_cp_ring_info	*rx_cpr;
	u16			rx_prod;
	u16			rx_agg_prod;
	u16			rx_sw_agg_prod;
	u16			rx_next_cons;
	struct bnge_db_info	rx_db;
	struct bnge_db_info	rx_agg_db;

	struct rx_bd		*rx_desc_ring[MAX_RX_PAGES];
	struct bnge_sw_rx_bd	*rx_buf_ring;

	struct rx_bd			*rx_agg_desc_ring[MAX_RX_AGG_PAGES];
	struct bnge_sw_rx_agg_bd	*rx_agg_buf_ring;

	unsigned long		*rx_agg_bmap;
	u16			rx_agg_bmap_size;

	dma_addr_t		rx_desc_mapping[MAX_RX_PAGES];
	dma_addr_t		rx_agg_desc_mapping[MAX_RX_AGG_PAGES];

	struct bnge_ring_struct	rx_ring_struct;
	struct bnge_ring_struct	rx_agg_ring_struct;
	struct page_pool	*page_pool;
	struct page_pool	*head_pool;
	bool			need_head_pool;
};

struct bnge_tx_ring_info {
	struct bnge_napi	*bnapi;
	struct bnge_cp_ring_info	*tx_cpr;
	u16			tx_prod;
	u16			tx_cons;
	u16			tx_hw_cons;
	u16			txq_index;
	u8			tx_napi_idx;
	u8			kick_pending;
	struct bnge_db_info	tx_db;

	struct tx_bd		*tx_desc_ring[MAX_TX_PAGES];
	struct bnge_sw_tx_bd	*tx_buf_ring;

	dma_addr_t		tx_desc_mapping[MAX_TX_PAGES];

	u32			dev_state;
#define BNGE_DEV_STATE_CLOSING	0x1

	struct bnge_ring_struct	tx_ring_struct;
};

struct bnge_napi {
	struct napi_struct		napi;
	struct bnge_net			*bn;
	int				index;

	struct bnge_nq_ring_info	nq_ring;
	struct bnge_rx_ring_info	*rx_ring;
	struct bnge_tx_ring_info	*tx_ring[BNGE_MAX_TXR_PER_NAPI];
};

#define INVALID_STATS_CTX_ID	-1
#define BNGE_VNIC_DEFAULT	0
#define BNGE_MAX_UC_ADDRS	4

struct bnge_vnic_info {
	u16		fw_vnic_id;
#define BNGE_MAX_CTX_PER_VNIC	8
	u16		fw_rss_cos_lb_ctx[BNGE_MAX_CTX_PER_VNIC];
	u16		mru;
	/* index 0 always dev_addr */
	struct bnge_l2_filter *l2_filters[BNGE_MAX_UC_ADDRS];
	u16		uc_filter_count;
	u8		*uc_list;
	dma_addr_t	rss_table_dma_addr;
	__le16		*rss_table;
	dma_addr_t	rss_hash_key_dma_addr;
	u64		*rss_hash_key;
	int		rss_table_size;
#define BNGE_RSS_TABLE_ENTRIES		64
#define BNGE_RSS_TABLE_SIZE		(BNGE_RSS_TABLE_ENTRIES * 4)
#define BNGE_RSS_TABLE_MAX_TBL		8
#define BNGE_MAX_RSS_TABLE_SIZE			\
	(BNGE_RSS_TABLE_SIZE * BNGE_RSS_TABLE_MAX_TBL)
	u32		rx_mask;

	u8		*mc_list;
	int		mc_list_size;
	int		mc_list_count;
	dma_addr_t	mc_list_mapping;
#define BNGE_MAX_MC_ADDRS	16

	u32		flags;
#define BNGE_VNIC_RSS_FLAG	1
#define BNGE_VNIC_MCAST_FLAG	4
#define BNGE_VNIC_UCAST_FLAG	8
	u32		vnic_id;
};

struct bnge_filter_base {
	struct hlist_node	hash;
	struct list_head	list;
	__le64			filter_id;
	u8			type;
#define BNGE_FLTR_TYPE_L2	2
	u8			flags;
	u16			rxq;
	u16			fw_vnic_id;
	u16			vf_idx;
	unsigned long		state;
#define BNGE_FLTR_VALID		0
#define BNGE_FLTR_FW_DELETED	2

	struct rcu_head         rcu;
};

struct bnge_l2_key {
	union {
		struct {
			u8	dst_mac_addr[ETH_ALEN];
			u16	vlan;
		};
		u32	filter_key;
	};
};

#define BNGE_L2_KEY_SIZE	(sizeof(struct bnge_l2_key) / 4)
struct bnge_l2_filter {
	/* base filter must be the first member */
	struct bnge_filter_base	base;
	struct bnge_l2_key	l2_key;
	refcount_t		refcnt;
};

u16 bnge_cp_ring_for_rx(struct bnge_rx_ring_info *rxr);
u16 bnge_cp_ring_for_tx(struct bnge_tx_ring_info *txr);
void bnge_fill_hw_rss_tbl(struct bnge_net *bn, struct bnge_vnic_info *vnic);
#endif /* _BNGE_NETDEV_H_ */

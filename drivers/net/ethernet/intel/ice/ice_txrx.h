/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TXRX_H_
#define _ICE_TXRX_H_

#define ICE_DFLT_IRQ_WORK	256
#define ICE_RXBUF_2048		2048
#define ICE_MAX_CHAINED_RX_BUFS	5
#define ICE_MAX_BUF_TXD		8
#define ICE_MIN_TX_LEN		17

/* The size limit for a transmit buffer in a descriptor is (16K - 1).
 * In order to align with the read requests we will align the value to
 * the nearest 4K which represents our maximum read request size.
 */
#define ICE_MAX_READ_REQ_SIZE	4096
#define ICE_MAX_DATA_PER_TXD	(16 * 1024 - 1)
#define ICE_MAX_DATA_PER_TXD_ALIGNED \
	(~(ICE_MAX_READ_REQ_SIZE - 1) & ICE_MAX_DATA_PER_TXD)

#define ICE_RX_BUF_WRITE	16	/* Must be power of 2 */
#define ICE_MAX_TXQ_PER_TXQG	128

/* We are assuming that the cache line is always 64 Bytes here for ice.
 * In order to make sure that is a correct assumption there is a check in probe
 * to print a warning if the read from GLPCI_CNF2 tells us that the cache line
 * size is 128 bytes. We do it this way because we do not want to read the
 * GLPCI_CNF2 register or a variable containing the value on every pass through
 * the Tx path.
 */
#define ICE_CACHE_LINE_BYTES		64
#define ICE_DESCS_PER_CACHE_LINE	(ICE_CACHE_LINE_BYTES / \
					 sizeof(struct ice_tx_desc))
#define ICE_DESCS_FOR_CTX_DESC		1
#define ICE_DESCS_FOR_SKB_DATA_PTR	1
/* Tx descriptors needed, worst case */
#define DESC_NEEDED (MAX_SKB_FRAGS + ICE_DESCS_FOR_CTX_DESC + \
		     ICE_DESCS_PER_CACHE_LINE + ICE_DESCS_FOR_SKB_DATA_PTR)
#define ICE_DESC_UNUSED(R)	\
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

#define ICE_TX_FLAGS_TSO	BIT(0)
#define ICE_TX_FLAGS_HW_VLAN	BIT(1)
#define ICE_TX_FLAGS_SW_VLAN	BIT(2)
#define ICE_TX_FLAGS_VLAN_M	0xffff0000
#define ICE_TX_FLAGS_VLAN_S	16

#define ICE_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

struct ice_tx_buf {
	struct ice_tx_desc *next_to_watch;
	struct sk_buff *skb;
	unsigned int bytecount;
	unsigned short gso_segs;
	u32 tx_flags;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
};

struct ice_tx_offload_params {
	u8 header_len;
	u32 td_cmd;
	u32 td_offset;
	u32 td_l2tag1;
	u16 cd_l2tag2;
	u32 cd_tunnel_params;
	u64 cd_qw1;
	struct ice_ring *tx_ring;
};

struct ice_rx_buf {
	struct sk_buff *skb;
	dma_addr_t dma;
	struct page *page;
	unsigned int page_offset;
	u16 pagecnt_bias;
};

struct ice_q_stats {
	u64 pkts;
	u64 bytes;
};

struct ice_txq_stats {
	u64 restart_q;
	u64 tx_busy;
	u64 tx_linearize;
	int prev_pkt; /* negative if no pending Tx descriptors */
};

struct ice_rxq_stats {
	u64 non_eop_descs;
	u64 alloc_page_failed;
	u64 alloc_buf_failed;
	u64 page_reuse_count;
};

/* this enum matches hardware bits and is meant to be used by DYN_CTLN
 * registers and QINT registers or more generally anywhere in the manual
 * mentioning ITR_INDX, ITR_NONE cannot be used as an index 'n' into any
 * register but instead is a special value meaning "don't update" ITR0/1/2.
 */
enum ice_dyn_idx_t {
	ICE_IDX_ITR0 = 0,
	ICE_IDX_ITR1 = 1,
	ICE_IDX_ITR2 = 2,
	ICE_ITR_NONE = 3	/* ITR_NONE must not be used as an index */
};

/* Header split modes defined by DTYPE field of Rx RLAN context */
enum ice_rx_dtype {
	ICE_RX_DTYPE_NO_SPLIT		= 0,
	ICE_RX_DTYPE_HEADER_SPLIT	= 1,
	ICE_RX_DTYPE_SPLIT_ALWAYS	= 2,
};

/* indices into GLINT_ITR registers */
#define ICE_RX_ITR	ICE_IDX_ITR0
#define ICE_TX_ITR	ICE_IDX_ITR1
#define ICE_ITR_8K	124
#define ICE_ITR_20K	50
#define ICE_ITR_MAX	8160
#define ICE_DFLT_TX_ITR	(ICE_ITR_20K | ICE_ITR_DYNAMIC)
#define ICE_DFLT_RX_ITR	(ICE_ITR_20K | ICE_ITR_DYNAMIC)
#define ICE_ITR_DYNAMIC	0x8000  /* used as flag for itr_setting */
#define ITR_IS_DYNAMIC(setting) (!!((setting) & ICE_ITR_DYNAMIC))
#define ITR_TO_REG(setting)	((setting) & ~ICE_ITR_DYNAMIC)
#define ICE_ITR_GRAN_S		1	/* ITR granularity is always 2us */
#define ICE_ITR_GRAN_US		BIT(ICE_ITR_GRAN_S)
#define ICE_ITR_MASK		0x1FFE	/* ITR register value alignment mask */
#define ITR_REG_ALIGN(setting)	__ALIGN_MASK(setting, ~ICE_ITR_MASK)

#define ICE_ITR_ADAPTIVE_MIN_INC	0x0002
#define ICE_ITR_ADAPTIVE_MIN_USECS	0x0002
#define ICE_ITR_ADAPTIVE_MAX_USECS	0x00FA
#define ICE_ITR_ADAPTIVE_LATENCY	0x8000
#define ICE_ITR_ADAPTIVE_BULK		0x0000

#define ICE_DFLT_INTRL	0

/* Legacy or Advanced Mode Queue */
#define ICE_TX_ADVANCED	0
#define ICE_TX_LEGACY	1

/* descriptor ring, associated with a VSI */
struct ice_ring {
	struct ice_ring *next;		/* pointer to next ring in q_vector */
	void *desc;			/* Descriptor ring memory */
	struct device *dev;		/* Used for DMA mapping */
	struct net_device *netdev;	/* netdev ring maps to */
	struct ice_vsi *vsi;		/* Backreference to associated VSI */
	struct ice_q_vector *q_vector;	/* Backreference to associated vector */
	u8 __iomem *tail;
	union {
		struct ice_tx_buf *tx_buf;
		struct ice_rx_buf *rx_buf;
	};
	u16 q_index;			/* Queue number of ring */
	u32 txq_teid;			/* Added Tx queue TEID */

	u16 count;			/* Number of descriptors */
	u16 reg_idx;			/* HW register index of the ring */

	/* used in interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;

	u8 ring_active;			/* is ring online or not */

	/* stats structs */
	struct ice_q_stats	stats;
	struct u64_stats_sync syncp;
	union {
		struct ice_txq_stats tx_stats;
		struct ice_rxq_stats rx_stats;
	};

	unsigned int size;		/* length of descriptor ring in bytes */
	dma_addr_t dma;			/* physical address of ring */
	struct rcu_head rcu;		/* to avoid race on free */
	u16 next_to_alloc;
} ____cacheline_internodealigned_in_smp;

struct ice_ring_container {
	/* head of linked-list of rings */
	struct ice_ring *ring;
	unsigned long next_update;	/* jiffies value of next queue update */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_pkts;	/* total packets processed this int */
	u16 itr_idx;		/* index in the interrupt vector */
	u16 target_itr;		/* value in usecs divided by the hw->itr_gran */
	u16 current_itr;	/* value in usecs divided by the hw->itr_gran */
	/* high bit set means dynamic ITR, rest is used to store user
	 * readable ITR value in usecs and must be converted before programming
	 * to a register.
	 */
	u16 itr_setting;
};

/* iterator for handling rings in ring container */
#define ice_for_each_ring(pos, head) \
	for (pos = (head).ring; pos; pos = pos->next)

bool ice_alloc_rx_bufs(struct ice_ring *rxr, u16 cleaned_count);
netdev_tx_t ice_start_xmit(struct sk_buff *skb, struct net_device *netdev);
void ice_clean_tx_ring(struct ice_ring *tx_ring);
void ice_clean_rx_ring(struct ice_ring *rx_ring);
int ice_setup_tx_ring(struct ice_ring *tx_ring);
int ice_setup_rx_ring(struct ice_ring *rx_ring);
void ice_free_tx_ring(struct ice_ring *tx_ring);
void ice_free_rx_ring(struct ice_ring *rx_ring);
int ice_napi_poll(struct napi_struct *napi, int budget);

#endif /* _ICE_TXRX_H_ */

/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TXRX_H_
#define _ICE_TXRX_H_

#include "ice_type.h"

#define ICE_DFLT_IRQ_WORK	256
#define ICE_RXBUF_3072		3072
#define ICE_RXBUF_2048		2048
#define ICE_RXBUF_1664		1664
#define ICE_RXBUF_1536		1536
#define ICE_MAX_CHAINED_RX_BUFS	5
#define ICE_MAX_BUF_TXD		8
#define ICE_MIN_TX_LEN		17
#define ICE_MAX_FRAME_LEGACY_RX 8320

/* The size limit for a transmit buffer in a descriptor is (16K - 1).
 * In order to align with the read requests we will align the value to
 * the nearest 4K which represents our maximum read request size.
 */
#define ICE_MAX_READ_REQ_SIZE	4096
#define ICE_MAX_DATA_PER_TXD	(16 * 1024 - 1)
#define ICE_MAX_DATA_PER_TXD_ALIGNED \
	(~(ICE_MAX_READ_REQ_SIZE - 1) & ICE_MAX_DATA_PER_TXD)

#define ICE_MAX_TXQ_PER_TXQG	128

/* Attempt to maximize the headroom available for incoming frames. We use a 2K
 * buffer for MTUs <= 1500 and need 1536/1534 to store the data for the frame.
 * This leaves us with 512 bytes of room.  From that we need to deduct the
 * space needed for the shared info and the padding needed to IP align the
 * frame.
 *
 * Note: For cache line sizes 256 or larger this value is going to end
 *	 up negative.  In these cases we should fall back to the legacy
 *	 receive path.
 */
#if (PAGE_SIZE < 8192)
#define ICE_2K_TOO_SMALL_WITH_PADDING \
	((unsigned int)(NET_SKB_PAD + ICE_RXBUF_1536) > \
			SKB_WITH_OVERHEAD(ICE_RXBUF_2048))

/**
 * ice_compute_pad - compute the padding
 * @rx_buf_len: buffer length
 *
 * Figure out the size of half page based on given buffer length and
 * then subtract the skb_shared_info followed by subtraction of the
 * actual buffer length; this in turn results in the actual space that
 * is left for padding usage
 */
static inline int ice_compute_pad(int rx_buf_len)
{
	int half_page_size;

	half_page_size = ALIGN(rx_buf_len, PAGE_SIZE / 2);
	return SKB_WITH_OVERHEAD(half_page_size) - rx_buf_len;
}

/**
 * ice_skb_pad - determine the padding that we can supply
 *
 * Figure out the right Rx buffer size and based on that calculate the
 * padding
 */
static inline int ice_skb_pad(void)
{
	int rx_buf_len;

	/* If a 2K buffer cannot handle a standard Ethernet frame then
	 * optimize padding for a 3K buffer instead of a 1.5K buffer.
	 *
	 * For a 3K buffer we need to add enough padding to allow for
	 * tailroom due to NET_IP_ALIGN possibly shifting us out of
	 * cache-line alignment.
	 */
	if (ICE_2K_TOO_SMALL_WITH_PADDING)
		rx_buf_len = ICE_RXBUF_3072 + SKB_DATA_ALIGN(NET_IP_ALIGN);
	else
		rx_buf_len = ICE_RXBUF_1536;

	/* if needed make room for NET_IP_ALIGN */
	rx_buf_len -= NET_IP_ALIGN;

	return ice_compute_pad(rx_buf_len);
}

#define ICE_SKB_PAD ice_skb_pad()
#else
#define ICE_2K_TOO_SMALL_WITH_PADDING false
#define ICE_SKB_PAD (NET_SKB_PAD + NET_IP_ALIGN)
#endif

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
	(u16)((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	      (R)->next_to_clean - (R)->next_to_use - 1)

#define ICE_RX_DESC_UNUSED(R)	\
	((((R)->first_desc > (R)->next_to_use) ? 0 : (R)->count) + \
	      (R)->first_desc - (R)->next_to_use - 1)

#define ICE_RING_QUARTER(R) ((R)->count >> 2)

#define ICE_TX_FLAGS_TSO	BIT(0)
#define ICE_TX_FLAGS_HW_VLAN	BIT(1)
#define ICE_TX_FLAGS_SW_VLAN	BIT(2)
/* Free, was ICE_TX_FLAGS_DUMMY_PKT */
#define ICE_TX_FLAGS_TSYN	BIT(4)
#define ICE_TX_FLAGS_IPV4	BIT(5)
#define ICE_TX_FLAGS_IPV6	BIT(6)
#define ICE_TX_FLAGS_TUNNEL	BIT(7)
#define ICE_TX_FLAGS_HW_OUTER_SINGLE_VLAN	BIT(8)

#define ICE_XDP_PASS		0
#define ICE_XDP_CONSUMED	BIT(0)
#define ICE_XDP_TX		BIT(1)
#define ICE_XDP_REDIR		BIT(2)
#define ICE_XDP_EXIT		BIT(3)
#define ICE_SKB_CONSUMED	ICE_XDP_CONSUMED

#define ICE_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

#define ICE_ETH_PKT_HDR_PAD	(ETH_HLEN + ETH_FCS_LEN + (VLAN_HLEN * 2))

#define ICE_TXD_LAST_DESC_CMD (ICE_TX_DESC_CMD_EOP | ICE_TX_DESC_CMD_RS)

/**
 * enum ice_tx_buf_type - type of &ice_tx_buf to act on Tx completion
 * @ICE_TX_BUF_EMPTY: unused OR XSk frame, no action required
 * @ICE_TX_BUF_DUMMY: dummy Flow Director packet, unmap and kfree()
 * @ICE_TX_BUF_FRAG: mapped skb OR &xdp_buff frag, only unmap DMA
 * @ICE_TX_BUF_SKB: &sk_buff, unmap and consume_skb(), update stats
 * @ICE_TX_BUF_XDP_TX: &xdp_buff, unmap and page_frag_free(), stats
 * @ICE_TX_BUF_XDP_XMIT: &xdp_frame, unmap and xdp_return_frame(), stats
 * @ICE_TX_BUF_XSK_TX: &xdp_buff on XSk queue, xsk_buff_free(), stats
 */
enum ice_tx_buf_type {
	ICE_TX_BUF_EMPTY	= 0U,
	ICE_TX_BUF_DUMMY,
	ICE_TX_BUF_FRAG,
	ICE_TX_BUF_SKB,
	ICE_TX_BUF_XDP_TX,
	ICE_TX_BUF_XDP_XMIT,
	ICE_TX_BUF_XSK_TX,
};

struct ice_tx_buf {
	union {
		struct ice_tx_desc *next_to_watch;
		u32 rs_idx;
	};
	union {
		void *raw_buf;		/* used for XDP_TX and FDir rules */
		struct sk_buff *skb;	/* used for .ndo_start_xmit() */
		struct xdp_frame *xdpf;	/* used for .ndo_xdp_xmit() */
		struct xdp_buff *xdp;	/* used for XDP_TX ZC */
	};
	unsigned int bytecount;
	union {
		unsigned int gso_segs;
		unsigned int nr_frags;	/* used for mbuf XDP */
	};
	u32 tx_flags:12;
	u32 type:4;			/* &ice_tx_buf_type */
	u32 vid:16;
	DEFINE_DMA_UNMAP_LEN(len);
	DEFINE_DMA_UNMAP_ADDR(dma);
};

struct ice_tx_offload_params {
	u64 cd_qw1;
	struct ice_tx_ring *tx_ring;
	u32 td_cmd;
	u32 td_offset;
	u32 td_l2tag1;
	u32 cd_tunnel_params;
	u16 cd_l2tag2;
	u8 header_len;
};

struct ice_rx_buf {
	dma_addr_t dma;
	struct page *page;
	unsigned int page_offset;
	unsigned int pgcnt;
	unsigned int act;
	unsigned int pagecnt_bias;
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
};

struct ice_ring_stats {
	struct rcu_head rcu;	/* to avoid race on free */
	struct ice_q_stats stats;
	struct u64_stats_sync syncp;
	union {
		struct ice_txq_stats tx_stats;
		struct ice_rxq_stats rx_stats;
	};
};

enum ice_ring_state_t {
	ICE_TX_XPS_INIT_DONE,
	ICE_TX_NBITS,
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

struct ice_pkt_ctx {
	u64 cached_phctime;
	__be16 vlan_proto;
};

struct ice_xdp_buff {
	struct xdp_buff xdp_buff;
	const union ice_32b_rx_flex_desc *eop_desc;
	const struct ice_pkt_ctx *pkt_ctx;
};

/* Required for compatibility with xdp_buffs from xsk_pool */
static_assert(offsetof(struct ice_xdp_buff, xdp_buff) == 0);

/* indices into GLINT_ITR registers */
#define ICE_RX_ITR	ICE_IDX_ITR0
#define ICE_TX_ITR	ICE_IDX_ITR1
#define ICE_ITR_8K	124
#define ICE_ITR_20K	50
#define ICE_ITR_MAX	8160 /* 0x1FE0 */
#define ICE_DFLT_TX_ITR	ICE_ITR_20K
#define ICE_DFLT_RX_ITR	ICE_ITR_20K
enum ice_dynamic_itr {
	ITR_STATIC = 0,
	ITR_DYNAMIC = 1
};

#define ITR_IS_DYNAMIC(rc) ((rc)->itr_mode == ITR_DYNAMIC)
#define ICE_ITR_GRAN_S		1	/* ITR granularity is always 2us */
#define ICE_ITR_GRAN_US		BIT(ICE_ITR_GRAN_S)
#define ICE_ITR_MASK		0x1FFE	/* ITR register value alignment mask */
#define ITR_REG_ALIGN(setting)	((setting) & ICE_ITR_MASK)

#define ICE_DFLT_INTRL	0
#define ICE_MAX_INTRL	236

#define ICE_IN_WB_ON_ITR_MODE	255
/* Sets WB_ON_ITR and assumes INTENA bit is already cleared, which allows
 * setting the MSK_M bit to tell hardware to ignore the INTENA_M bit. Also,
 * set the write-back latency to the usecs passed in.
 */
#define ICE_GLINT_DYN_CTL_WB_ON_ITR(usecs, itr_idx)	\
	((((usecs) << (GLINT_DYN_CTL_INTERVAL_S - ICE_ITR_GRAN_S)) & \
	  GLINT_DYN_CTL_INTERVAL_M) | \
	 (((itr_idx) << GLINT_DYN_CTL_ITR_INDX_S) & \
	  GLINT_DYN_CTL_ITR_INDX_M) | GLINT_DYN_CTL_INTENA_MSK_M | \
	 GLINT_DYN_CTL_WB_ON_ITR_M)

/* Legacy or Advanced Mode Queue */
#define ICE_TX_ADVANCED	0
#define ICE_TX_LEGACY	1

/* descriptor ring, associated with a VSI */
struct ice_rx_ring {
	/* CL1 - 1st cacheline starts here */
	void *desc;			/* Descriptor ring memory */
	struct device *dev;		/* Used for DMA mapping */
	struct net_device *netdev;	/* netdev ring maps to */
	struct ice_vsi *vsi;		/* Backreference to associated VSI */
	struct ice_q_vector *q_vector;	/* Backreference to associated vector */
	u8 __iomem *tail;
	u16 q_index;			/* Queue number of ring */

	u16 count;			/* Number of descriptors */
	u16 reg_idx;			/* HW register index of the ring */
	u16 next_to_alloc;

	union {
		struct ice_rx_buf *rx_buf;
		struct xdp_buff **xdp_buf;
	};
	/* CL2 - 2nd cacheline starts here */
	union {
		struct ice_xdp_buff xdp_ext;
		struct xdp_buff xdp;
	};
	/* CL3 - 3rd cacheline starts here */
	union {
		struct ice_pkt_ctx pkt_ctx;
		struct {
			u64 cached_phctime;
			__be16 vlan_proto;
		};
	};
	struct bpf_prog *xdp_prog;
	u16 rx_offset;

	/* used in interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;
	u16 first_desc;

	/* stats structs */
	struct ice_ring_stats *ring_stats;

	struct rcu_head rcu;		/* to avoid race on free */
	/* CL4 - 4th cacheline starts here */
	struct ice_channel *ch;
	struct ice_tx_ring *xdp_ring;
	struct ice_rx_ring *next;	/* pointer to next ring in q_vector */
	struct xsk_buff_pool *xsk_pool;
	u32 nr_frags;
	u16 max_frame;
	u16 rx_buf_len;
	dma_addr_t dma;			/* physical address of ring */
	u8 dcb_tc;			/* Traffic class of ring */
	u8 ptp_rx;
#define ICE_RX_FLAGS_RING_BUILD_SKB	BIT(1)
#define ICE_RX_FLAGS_CRC_STRIP_DIS	BIT(2)
#define ICE_RX_FLAGS_MULTIDEV		BIT(3)
	u8 flags;
	/* CL5 - 5th cacheline starts here */
	struct xdp_rxq_info xdp_rxq;
} ____cacheline_internodealigned_in_smp;

struct ice_tx_ring {
	/* CL1 - 1st cacheline starts here */
	struct ice_tx_ring *next;	/* pointer to next ring in q_vector */
	void *desc;			/* Descriptor ring memory */
	struct device *dev;		/* Used for DMA mapping */
	u8 __iomem *tail;
	struct ice_tx_buf *tx_buf;
	struct ice_q_vector *q_vector;	/* Backreference to associated vector */
	struct net_device *netdev;	/* netdev ring maps to */
	struct ice_vsi *vsi;		/* Backreference to associated VSI */
	/* CL2 - 2nd cacheline starts here */
	dma_addr_t dma;			/* physical address of ring */
	struct xsk_buff_pool *xsk_pool;
	u16 next_to_use;
	u16 next_to_clean;
	u16 q_handle;			/* Queue handle per TC */
	u16 reg_idx;			/* HW register index of the ring */
	u16 count;			/* Number of descriptors */
	u16 q_index;			/* Queue number of ring */
	u16 xdp_tx_active;
	/* stats structs */
	struct ice_ring_stats *ring_stats;
	/* CL3 - 3rd cacheline starts here */
	struct rcu_head rcu;		/* to avoid race on free */
	DECLARE_BITMAP(xps_state, ICE_TX_NBITS);	/* XPS Config State */
	struct ice_channel *ch;
	struct ice_ptp_tx *tx_tstamps;
	spinlock_t tx_lock;
	u32 txq_teid;			/* Added Tx queue TEID */
	/* CL4 - 4th cacheline starts here */
#define ICE_TX_FLAGS_RING_XDP		BIT(0)
#define ICE_TX_FLAGS_RING_VLAN_L2TAG1	BIT(1)
#define ICE_TX_FLAGS_RING_VLAN_L2TAG2	BIT(2)
	u8 flags;
	u8 dcb_tc;			/* Traffic class of ring */
	u16 quanta_prof_id;
} ____cacheline_internodealigned_in_smp;

static inline bool ice_ring_uses_build_skb(struct ice_rx_ring *ring)
{
	return !!(ring->flags & ICE_RX_FLAGS_RING_BUILD_SKB);
}

static inline void ice_set_ring_build_skb_ena(struct ice_rx_ring *ring)
{
	ring->flags |= ICE_RX_FLAGS_RING_BUILD_SKB;
}

static inline void ice_clear_ring_build_skb_ena(struct ice_rx_ring *ring)
{
	ring->flags &= ~ICE_RX_FLAGS_RING_BUILD_SKB;
}

static inline bool ice_ring_ch_enabled(struct ice_tx_ring *ring)
{
	return !!ring->ch;
}

static inline bool ice_ring_is_xdp(struct ice_tx_ring *ring)
{
	return !!(ring->flags & ICE_TX_FLAGS_RING_XDP);
}

enum ice_container_type {
	ICE_RX_CONTAINER,
	ICE_TX_CONTAINER,
};

struct ice_ring_container {
	/* head of linked-list of rings */
	union {
		struct ice_rx_ring *rx_ring;
		struct ice_tx_ring *tx_ring;
	};
	struct dim dim;		/* data for net_dim algorithm */
	u16 itr_idx;		/* index in the interrupt vector */
	/* this matches the maximum number of ITR bits, but in usec
	 * values, so it is shifted left one bit (bit zero is ignored)
	 */
	union {
		struct {
			u16 itr_setting:13;
			u16 itr_reserved:2;
			u16 itr_mode:1;
		};
		u16 itr_settings;
	};
	enum ice_container_type type;
};

struct ice_coalesce_stored {
	u16 itr_tx;
	u16 itr_rx;
	u8 intrl;
	u8 tx_valid;
	u8 rx_valid;
};

/* iterator for handling rings in ring container */
#define ice_for_each_rx_ring(pos, head) \
	for (pos = (head).rx_ring; pos; pos = pos->next)

#define ice_for_each_tx_ring(pos, head) \
	for (pos = (head).tx_ring; pos; pos = pos->next)

static inline unsigned int ice_rx_pg_order(struct ice_rx_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring->rx_buf_len > (PAGE_SIZE / 2))
		return 1;
#endif
	return 0;
}

#define ice_rx_pg_size(_ring) (PAGE_SIZE << ice_rx_pg_order(_ring))

union ice_32b_rx_flex_desc;

bool ice_alloc_rx_bufs(struct ice_rx_ring *rxr, unsigned int cleaned_count);
netdev_tx_t ice_start_xmit(struct sk_buff *skb, struct net_device *netdev);
u16
ice_select_queue(struct net_device *dev, struct sk_buff *skb,
		 struct net_device *sb_dev);
void ice_clean_tx_ring(struct ice_tx_ring *tx_ring);
void ice_clean_rx_ring(struct ice_rx_ring *rx_ring);
int ice_setup_tx_ring(struct ice_tx_ring *tx_ring);
int ice_setup_rx_ring(struct ice_rx_ring *rx_ring);
void ice_free_tx_ring(struct ice_tx_ring *tx_ring);
void ice_free_rx_ring(struct ice_rx_ring *rx_ring);
int ice_napi_poll(struct napi_struct *napi, int budget);
int
ice_prgm_fdir_fltr(struct ice_vsi *vsi, struct ice_fltr_desc *fdir_desc,
		   u8 *raw_packet);
int ice_clean_rx_irq(struct ice_rx_ring *rx_ring, int budget);
void ice_clean_ctrl_tx_irq(struct ice_tx_ring *tx_ring);
#endif /* _ICE_TXRX_H_ */

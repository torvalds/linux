/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_TXRX_H_
#define _ICE_TXRX_H_

#include "ice_type.h"

#define ICE_DFLT_IRQ_WORK	256
#define ICE_RXBUF_3072		3072
#define ICE_RXBUF_2048		2048
#define ICE_RXBUF_1536		1536
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
((NET_SKB_PAD + ICE_RXBUF_1536) > SKB_WITH_OVERHEAD(ICE_RXBUF_2048))

/**
 * ice_compute_pad - compute the padding
 * rx_buf_len: buffer length
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
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

#define ICE_TX_FLAGS_TSO	BIT(0)
#define ICE_TX_FLAGS_HW_VLAN	BIT(1)
#define ICE_TX_FLAGS_SW_VLAN	BIT(2)
#define ICE_TX_FLAGS_VLAN_M	0xffff0000
#define ICE_TX_FLAGS_VLAN_PR_M	0xe0000000
#define ICE_TX_FLAGS_VLAN_PR_S	29
#define ICE_TX_FLAGS_VLAN_S	16

#define ICE_XDP_PASS		0
#define ICE_XDP_CONSUMED	BIT(0)
#define ICE_XDP_TX		BIT(1)
#define ICE_XDP_REDIR		BIT(2)

#define ICE_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

#define ICE_ETH_PKT_HDR_PAD	(ETH_HLEN + ETH_FCS_LEN + (VLAN_HLEN * 2))

#define ICE_TXD_LAST_DESC_CMD (ICE_TX_DESC_CMD_EOP | ICE_TX_DESC_CMD_RS)

struct ice_tx_buf {
	struct ice_tx_desc *next_to_watch;
	union {
		struct sk_buff *skb;
		void *raw_buf; /* used for XDP */
	};
	unsigned int bytecount;
	unsigned short gso_segs;
	u32 tx_flags;
	DEFINE_DMA_UNMAP_LEN(len);
	DEFINE_DMA_UNMAP_ADDR(dma);
};

struct ice_tx_offload_params {
	u64 cd_qw1;
	struct ice_ring *tx_ring;
	u32 td_cmd;
	u32 td_offset;
	u32 td_l2tag1;
	u32 cd_tunnel_params;
	u16 cd_l2tag2;
	u8 header_len;
};

struct ice_rx_buf {
	struct sk_buff *skb;
	dma_addr_t dma;
	union {
		struct {
			struct page *page;
			unsigned int page_offset;
			u16 pagecnt_bias;
		};
		struct {
			void *addr;
			u64 handle;
		};
	};
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
#define ICE_MAX_INTRL	236

#define ICE_WB_ON_ITR_USECS	2
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
struct ice_ring {
	/* CL1 - 1st cacheline starts here */
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
	/* CL2 - 2nd cacheline starts here */
	u16 q_index;			/* Queue number of ring */
	u16 q_handle;			/* Queue handle per TC */

	u8 ring_active:1;		/* is ring online or not */

	u16 count;			/* Number of descriptors */
	u16 reg_idx;			/* HW register index of the ring */

	/* used in interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;
	u16 next_to_alloc;

	/* stats structs */
	struct ice_q_stats	stats;
	struct u64_stats_sync syncp;
	union {
		struct ice_txq_stats tx_stats;
		struct ice_rxq_stats rx_stats;
	};

	struct rcu_head rcu;		/* to avoid race on free */
	struct bpf_prog *xdp_prog;
	struct xdp_umem *xsk_umem;
	struct zero_copy_allocator zca;
	/* CL3 - 3rd cacheline starts here */
	struct xdp_rxq_info xdp_rxq;
	/* CLX - the below items are only accessed infrequently and should be
	 * in their own cache line if possible
	 */
#define ICE_TX_FLAGS_RING_XDP		BIT(0)
#define ICE_RX_FLAGS_RING_BUILD_SKB	BIT(1)
	u8 flags;
	dma_addr_t dma;			/* physical address of ring */
	unsigned int size;		/* length of descriptor ring in bytes */
	u32 txq_teid;			/* Added Tx queue TEID */
	u16 rx_buf_len;
	u8 dcb_tc;			/* Traffic class of ring */
} ____cacheline_internodealigned_in_smp;

static inline bool ice_ring_uses_build_skb(struct ice_ring *ring)
{
	return !!(ring->flags & ICE_RX_FLAGS_RING_BUILD_SKB);
}

static inline void ice_set_ring_build_skb_ena(struct ice_ring *ring)
{
	ring->flags |= ICE_RX_FLAGS_RING_BUILD_SKB;
}

static inline void ice_clear_ring_build_skb_ena(struct ice_ring *ring)
{
	ring->flags &= ~ICE_RX_FLAGS_RING_BUILD_SKB;
}

static inline bool ice_ring_is_xdp(struct ice_ring *ring)
{
	return !!(ring->flags & ICE_TX_FLAGS_RING_XDP);
}

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

struct ice_coalesce_stored {
	u16 itr_tx;
	u16 itr_rx;
	u8 intrl;
};

/* iterator for handling rings in ring container */
#define ice_for_each_ring(pos, head) \
	for (pos = (head).ring; pos; pos = pos->next)

static inline unsigned int ice_rx_pg_order(struct ice_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring->rx_buf_len > (PAGE_SIZE / 2))
		return 1;
#endif
	return 0;
}

#define ice_rx_pg_size(_ring) (PAGE_SIZE << ice_rx_pg_order(_ring))

union ice_32b_rx_flex_desc;

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

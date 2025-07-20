/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _IAVF_TXRX_H_
#define _IAVF_TXRX_H_

/* Interrupt Throttling and Rate Limiting Goodies */
#define IAVF_DEFAULT_IRQ_WORK      256

/* The datasheet for the X710 and XL710 indicate that the maximum value for
 * the ITR is 8160usec which is then called out as 0xFF0 with a 2usec
 * resolution. 8160 is 0x1FE0 when written out in hex. So instead of storing
 * the register value which is divided by 2 lets use the actual values and
 * avoid an excessive amount of translation.
 */
#define IAVF_ITR_DYNAMIC	0x8000	/* use top bit as a flag */
#define IAVF_ITR_MASK		0x1FFE	/* mask for ITR register value */
#define IAVF_ITR_100K		    10	/* all values below must be even */
#define IAVF_ITR_50K		    20
#define IAVF_ITR_20K		    50
#define IAVF_ITR_18K		    60
#define IAVF_ITR_8K		   122
#define IAVF_MAX_ITR		  8160	/* maximum value as per datasheet */
#define ITR_TO_REG(setting) ((setting) & ~IAVF_ITR_DYNAMIC)
#define ITR_REG_ALIGN(setting) __ALIGN_MASK(setting, ~IAVF_ITR_MASK)
#define ITR_IS_DYNAMIC(setting) (!!((setting) & IAVF_ITR_DYNAMIC))

#define IAVF_ITR_RX_DEF		(IAVF_ITR_20K | IAVF_ITR_DYNAMIC)
#define IAVF_ITR_TX_DEF		(IAVF_ITR_20K | IAVF_ITR_DYNAMIC)

/* 0x40 is the enable bit for interrupt rate limiting, and must be set if
 * the value of the rate limit is non-zero
 */
#define INTRL_ENA                  BIT(6)
#define IAVF_MAX_INTRL             0x3B    /* reg uses 4 usec resolution */
#define INTRL_REG_TO_USEC(intrl) ((intrl & ~INTRL_ENA) << 2)
#define INTRL_USEC_TO_REG(set) ((set) ? ((set) >> 2) | INTRL_ENA : 0)
#define IAVF_INTRL_8K              125     /* 8000 ints/sec */
#define IAVF_INTRL_62K             16      /* 62500 ints/sec */
#define IAVF_INTRL_83K             12      /* 83333 ints/sec */

#define IAVF_QUEUE_END_OF_LIST 0x7FF

/* this enum matches hardware bits and is meant to be used by DYN_CTLN
 * registers and QINT registers or more generally anywhere in the manual
 * mentioning ITR_INDX, ITR_NONE cannot be used as an index 'n' into any
 * register but instead is a special value meaning "don't update" ITR0/1/2.
 */
enum iavf_dyn_idx_t {
	IAVF_IDX_ITR0 = 0,
	IAVF_IDX_ITR1 = 1,
	IAVF_IDX_ITR2 = 2,
	IAVF_ITR_NONE = 3	/* ITR_NONE must not be used as an index */
};

/* these are indexes into ITRN registers */
#define IAVF_RX_ITR    IAVF_IDX_ITR0
#define IAVF_TX_ITR    IAVF_IDX_ITR1
#define IAVF_PE_ITR    IAVF_IDX_ITR2

/* Supported RSS offloads */
#define IAVF_DEFAULT_RSS_HENA ( \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV4_UDP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV4_SCTP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV4_TCP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV4_OTHER) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_FRAG_IPV4) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV6_UDP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV6_TCP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV6_SCTP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV6_OTHER) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_FRAG_IPV6) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_L2_PAYLOAD))

#define IAVF_DEFAULT_RSS_HENA_EXPANDED (IAVF_DEFAULT_RSS_HENA | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_IPV6_TCP_SYN_NO_ACK) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP) | \
	BIT_ULL(IAVF_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP))

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IAVF_RX_INCREMENT(r, i) \
	do {					\
		(i)++;				\
		if ((i) == (r)->count)		\
			i = 0;			\
		r->next_to_clean = i;		\
	} while (0)

#define IAVF_RX_NEXT_DESC(r, i, n)		\
	do {					\
		(i)++;				\
		if ((i) == (r)->count)		\
			i = 0;			\
		(n) = IAVF_RX_DESC((r), (i));	\
	} while (0)

#define IAVF_RX_NEXT_DESC_PREFETCH(r, i, n)		\
	do {						\
		IAVF_RX_NEXT_DESC((r), (i), (n));	\
		prefetch((n));				\
	} while (0)

#define IAVF_MAX_BUFFER_TXD	8
#define IAVF_MIN_TX_LEN		17

/* The size limit for a transmit buffer in a descriptor is (16K - 1).
 * In order to align with the read requests we will align the value to
 * the nearest 4K which represents our maximum read request size.
 */
#define IAVF_MAX_READ_REQ_SIZE		4096
#define IAVF_MAX_DATA_PER_TXD		(16 * 1024 - 1)
#define IAVF_MAX_DATA_PER_TXD_ALIGNED \
	(IAVF_MAX_DATA_PER_TXD & ~(IAVF_MAX_READ_REQ_SIZE - 1))

/**
 * iavf_txd_use_count  - estimate the number of descriptors needed for Tx
 * @size: transmit request size in bytes
 *
 * Due to hardware alignment restrictions (4K alignment), we need to
 * assume that we can have no more than 12K of data per descriptor, even
 * though each descriptor can take up to 16K - 1 bytes of aligned memory.
 * Thus, we need to divide by 12K. But division is slow! Instead,
 * we decompose the operation into shifts and one relatively cheap
 * multiply operation.
 *
 * To divide by 12K, we first divide by 4K, then divide by 3:
 *     To divide by 4K, shift right by 12 bits
 *     To divide by 3, multiply by 85, then divide by 256
 *     (Divide by 256 is done by shifting right by 8 bits)
 * Finally, we add one to round up. Because 256 isn't an exact multiple of
 * 3, we'll underestimate near each multiple of 12K. This is actually more
 * accurate as we have 4K - 1 of wiggle room that we can fit into the last
 * segment.  For our purposes this is accurate out to 1M which is orders of
 * magnitude greater than our largest possible GSO size.
 *
 * This would then be implemented as:
 *     return (((size >> 12) * 85) >> 8) + 1;
 *
 * Since multiplication and division are commutative, we can reorder
 * operations into:
 *     return ((size * 85) >> 20) + 1;
 */
static inline unsigned int iavf_txd_use_count(unsigned int size)
{
	return ((size * 85) >> 20) + 1;
}

/* Tx Descriptors needed, worst case */
#define DESC_NEEDED (MAX_SKB_FRAGS + 6)
#define IAVF_MIN_DESC_PENDING	4

#define IAVF_TX_FLAGS_HW_VLAN			BIT(1)
#define IAVF_TX_FLAGS_SW_VLAN			BIT(2)
#define IAVF_TX_FLAGS_TSO			BIT(3)
#define IAVF_TX_FLAGS_IPV4			BIT(4)
#define IAVF_TX_FLAGS_IPV6			BIT(5)
#define IAVF_TX_FLAGS_FCCRC			BIT(6)
#define IAVF_TX_FLAGS_FSO			BIT(7)
#define IAVF_TX_FLAGS_FD_SB			BIT(9)
#define IAVF_TX_FLAGS_VXLAN_TUNNEL		BIT(10)
#define IAVF_TX_FLAGS_HW_OUTER_SINGLE_VLAN	BIT(11)
#define IAVF_TX_FLAGS_VLAN_MASK			0xffff0000
#define IAVF_TX_FLAGS_VLAN_PRIO_MASK		0xe0000000
#define IAVF_TX_FLAGS_VLAN_PRIO_SHIFT		29
#define IAVF_TX_FLAGS_VLAN_SHIFT		16

struct iavf_tx_buffer {
	struct iavf_tx_desc *next_to_watch;
	union {
		struct sk_buff *skb;
		void *raw_buf;
	};
	unsigned int bytecount;
	unsigned short gso_segs;

	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct iavf_queue_stats {
	u64 packets;
	u64 bytes;
};

struct iavf_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
	u64 tx_linearize;
	u64 tx_force_wb;
	u64 tx_lost_interrupt;
};

struct iavf_rx_queue_stats {
	u64 non_eop_descs;
	u64 alloc_page_failed;
	u64 alloc_buff_failed;
};

/* some useful defines for virtchannel interface, which
 * is the only remaining user of header split
 */
#define IAVF_RX_DTYPE_NO_SPLIT      0
#define IAVF_RX_DTYPE_HEADER_SPLIT  1
#define IAVF_RX_DTYPE_SPLIT_ALWAYS  2
#define IAVF_RX_SPLIT_L2      0x1
#define IAVF_RX_SPLIT_IP      0x2
#define IAVF_RX_SPLIT_TCP_UDP 0x4
#define IAVF_RX_SPLIT_SCTP    0x8

/* struct that defines a descriptor ring, associated with a VSI */
struct iavf_ring {
	struct iavf_ring *next;		/* pointer to next ring in q_vector */
	void *desc;			/* Descriptor ring memory */
	union {
		struct page_pool *pp;	/* Used on Rx for buffer management */
		struct device *dev;	/* Used on Tx for DMA mapping */
	};
	struct net_device *netdev;	/* netdev ring maps to */
	union {
		struct libeth_fqe *rx_fqes;
		struct iavf_tx_buffer *tx_bi;
	};
	u8 __iomem *tail;
	u32 truesize;

	u16 queue_index;		/* Queue number of ring */

	/* high bit set means dynamic, use accessors routines to read/write.
	 * hardware only supports 2us resolution for the ITR registers.
	 * these values always store the USER setting, and must be converted
	 * before programming to a register.
	 */
	u16 itr_setting;

	u16 count;			/* Number of descriptors */

	/* used in interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;

	u16 rxdid;		/* Rx descriptor format */

	u16 flags;
#define IAVF_TXR_FLAGS_WB_ON_ITR		BIT(0)
#define IAVF_TXR_FLAGS_ARM_WB			BIT(1)
/* BIT(2) is free */
#define IAVF_TXRX_FLAGS_VLAN_TAG_LOC_L2TAG1	BIT(3)
#define IAVF_TXR_FLAGS_VLAN_TAG_LOC_L2TAG2	BIT(4)
#define IAVF_RXR_FLAGS_VLAN_TAG_LOC_L2TAG2_2	BIT(5)
#define IAVF_TXRX_FLAGS_HW_TSTAMP		BIT(6)

	/* stats structs */
	struct iavf_queue_stats	stats;
	struct u64_stats_sync syncp;
	union {
		struct iavf_tx_queue_stats tx_stats;
		struct iavf_rx_queue_stats rx_stats;
	};

	int prev_pkt_ctr;		/* For Tx stall detection */
	unsigned int size;		/* length of descriptor ring in bytes */
	dma_addr_t dma;			/* physical address of ring */

	struct iavf_vsi *vsi;		/* Backreference to associated VSI */
	struct iavf_q_vector *q_vector;	/* Backreference to associated vector */

	struct rcu_head rcu;		/* to avoid race on free */
	struct sk_buff *skb;		/* When iavf_clean_rx_ring_irq() must
					 * return before it sees the EOP for
					 * the current packet, we save that skb
					 * here and resume receiving this
					 * packet the next time
					 * iavf_clean_rx_ring_irq() is called
					 * for this ring.
					 */

	struct iavf_ptp *ptp;

	u32 rx_buf_len;
	struct net_shaper q_shaper;
	bool q_shaper_update;
} ____cacheline_internodealigned_in_smp;

#define IAVF_ITR_ADAPTIVE_MIN_INC	0x0002
#define IAVF_ITR_ADAPTIVE_MIN_USECS	0x0002
#define IAVF_ITR_ADAPTIVE_MAX_USECS	0x007e
#define IAVF_ITR_ADAPTIVE_LATENCY	0x8000
#define IAVF_ITR_ADAPTIVE_BULK		0x0000
#define ITR_IS_BULK(x) (!((x) & IAVF_ITR_ADAPTIVE_LATENCY))

struct iavf_ring_container {
	struct iavf_ring *ring;		/* pointer to linked list of ring(s) */
	unsigned long next_update;	/* jiffies value of next update */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u16 count;
	u16 target_itr;			/* target ITR setting for ring(s) */
	u16 current_itr;		/* current ITR setting for ring(s) */
};

/* iterator for handling rings in ring container */
#define iavf_for_each_ring(pos, head) \
	for (pos = (head).ring; pos != NULL; pos = pos->next)

bool iavf_alloc_rx_buffers(struct iavf_ring *rxr, u16 cleaned_count);
netdev_tx_t iavf_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
int iavf_setup_tx_descriptors(struct iavf_ring *tx_ring);
int iavf_setup_rx_descriptors(struct iavf_ring *rx_ring);
void iavf_free_tx_resources(struct iavf_ring *tx_ring);
void iavf_free_rx_resources(struct iavf_ring *rx_ring);
int iavf_napi_poll(struct napi_struct *napi, int budget);
void iavf_detect_recover_hung(struct iavf_vsi *vsi);
int __iavf_maybe_stop_tx(struct iavf_ring *tx_ring, int size);
bool __iavf_chk_linearize(struct sk_buff *skb);

/**
 * iavf_xmit_descriptor_count - calculate number of Tx descriptors needed
 * @skb:     send buffer
 *
 * Returns number of data descriptors needed for this skb. Returns 0 to indicate
 * there is not enough descriptors available in this ring since we need at least
 * one descriptor.
 **/
static inline int iavf_xmit_descriptor_count(struct sk_buff *skb)
{
	const skb_frag_t *frag = &skb_shinfo(skb)->frags[0];
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	int count = 0, size = skb_headlen(skb);

	for (;;) {
		count += iavf_txd_use_count(size);

		if (!nr_frags--)
			break;

		size = skb_frag_size(frag++);
	}

	return count;
}

/**
 * iavf_maybe_stop_tx - 1st level check for Tx stop conditions
 * @tx_ring: the ring to be checked
 * @size:    the size buffer we want to assure is available
 *
 * Returns 0 if stop is not needed
 **/
static inline int iavf_maybe_stop_tx(struct iavf_ring *tx_ring, int size)
{
	if (likely(IAVF_DESC_UNUSED(tx_ring) >= size))
		return 0;
	return __iavf_maybe_stop_tx(tx_ring, size);
}

/**
 * iavf_chk_linearize - Check if there are more than 8 fragments per packet
 * @skb:      send buffer
 * @count:    number of buffers used
 *
 * Note: Our HW can't scatter-gather more than 8 fragments to build
 * a packet on the wire and so we need to figure out the cases where we
 * need to linearize the skb.
 **/
static inline bool iavf_chk_linearize(struct sk_buff *skb, int count)
{
	/* Both TSO and single send will work if count is less than 8 */
	if (likely(count < IAVF_MAX_BUFFER_TXD))
		return false;

	if (skb_is_gso(skb))
		return __iavf_chk_linearize(skb);

	/* we can support up to 8 data buffers for a single send */
	return count != IAVF_MAX_BUFFER_TXD;
}
/**
 * txring_txq - helper to convert from a ring to a queue
 * @ring: Tx ring to find the netdev equivalent of
 **/
static inline struct netdev_queue *txring_txq(const struct iavf_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->queue_index);
}
#endif /* _IAVF_TXRX_H_ */

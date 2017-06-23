/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_TXRX_H_
#define _I40E_TXRX_H_

/* Interrupt Throttling and Rate Limiting Goodies */

#define I40E_MAX_ITR               0x0FF0  /* reg uses 2 usec resolution */
#define I40E_MIN_ITR               0x0001  /* reg uses 2 usec resolution */
#define I40E_ITR_100K              0x0005
#define I40E_ITR_50K               0x000A
#define I40E_ITR_20K               0x0019
#define I40E_ITR_18K               0x001B
#define I40E_ITR_8K                0x003E
#define I40E_ITR_4K                0x007A
#define I40E_MAX_INTRL             0x3B    /* reg uses 4 usec resolution */
#define I40E_ITR_RX_DEF            I40E_ITR_20K
#define I40E_ITR_TX_DEF            I40E_ITR_20K
#define I40E_ITR_DYNAMIC           0x8000  /* use top bit as a flag */
#define I40E_MIN_INT_RATE          250     /* ~= 1000000 / (I40E_MAX_ITR * 2) */
#define I40E_MAX_INT_RATE          500000  /* == 1000000 / (I40E_MIN_ITR * 2) */
#define I40E_DEFAULT_IRQ_WORK      256
#define ITR_TO_REG(setting) ((setting & ~I40E_ITR_DYNAMIC) >> 1)
#define ITR_IS_DYNAMIC(setting) (!!(setting & I40E_ITR_DYNAMIC))
#define ITR_REG_TO_USEC(itr_reg) (itr_reg << 1)
/* 0x40 is the enable bit for interrupt rate limiting, and must be set if
 * the value of the rate limit is non-zero
 */
#define INTRL_ENA                  BIT(6)
#define INTRL_REG_TO_USEC(intrl) ((intrl & ~INTRL_ENA) << 2)
#define INTRL_USEC_TO_REG(set) ((set) ? ((set) >> 2) | INTRL_ENA : 0)
#define I40E_INTRL_8K              125     /* 8000 ints/sec */
#define I40E_INTRL_62K             16      /* 62500 ints/sec */
#define I40E_INTRL_83K             12      /* 83333 ints/sec */

#define I40E_QUEUE_END_OF_LIST 0x7FF

/* this enum matches hardware bits and is meant to be used by DYN_CTLN
 * registers and QINT registers or more generally anywhere in the manual
 * mentioning ITR_INDX, ITR_NONE cannot be used as an index 'n' into any
 * register but instead is a special value meaning "don't update" ITR0/1/2.
 */
enum i40e_dyn_idx_t {
	I40E_IDX_ITR0 = 0,
	I40E_IDX_ITR1 = 1,
	I40E_IDX_ITR2 = 2,
	I40E_ITR_NONE = 3	/* ITR_NONE must not be used as an index */
};

/* these are indexes into ITRN registers */
#define I40E_RX_ITR    I40E_IDX_ITR0
#define I40E_TX_ITR    I40E_IDX_ITR1
#define I40E_PE_ITR    I40E_IDX_ITR2

/* Supported RSS offloads */
#define I40E_DEFAULT_RSS_HENA ( \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_SCTP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_OTHER) | \
	BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV4) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_TCP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_SCTP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_OTHER) | \
	BIT_ULL(I40E_FILTER_PCTYPE_FRAG_IPV6) | \
	BIT_ULL(I40E_FILTER_PCTYPE_L2_PAYLOAD))

#define I40E_DEFAULT_RSS_HENA_EXPANDED (I40E_DEFAULT_RSS_HENA | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_IPV6_TCP_SYN_NO_ACK) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP) | \
	BIT_ULL(I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP))

#define i40e_pf_get_default_rss_hena(pf) \
	(((pf)->flags & I40E_FLAG_MULTIPLE_TCP_UDP_RSS_PCTYPE) ? \
	  I40E_DEFAULT_RSS_HENA_EXPANDED : I40E_DEFAULT_RSS_HENA)

/* Supported Rx Buffer Sizes (a multiple of 128) */
#define I40E_RXBUFFER_256   256
#define I40E_RXBUFFER_1536  1536  /* 128B aligned standard Ethernet frame */
#define I40E_RXBUFFER_2048  2048
#define I40E_RXBUFFER_3072  3072  /* Used for large frames w/ padding */
#define I40E_MAX_RXBUFFER   9728  /* largest size for single descriptor */

/* NOTE: netdev_alloc_skb reserves up to 64 bytes, NET_IP_ALIGN means we
 * reserve 2 more, and skb_shared_info adds an additional 384 bytes more,
 * this adds up to 512 bytes of extra data meaning the smallest allocation
 * we could have is 1K.
 * i.e. RXBUFFER_256 --> 960 byte skb (size-1024 slab)
 * i.e. RXBUFFER_512 --> 1216 byte skb (size-2048 slab)
 */
#define I40E_RX_HDR_SIZE I40E_RXBUFFER_256
#define I40E_PACKET_HDR_PAD (ETH_HLEN + ETH_FCS_LEN + (VLAN_HLEN * 2))
#define i40e_rx_desc i40e_32byte_rx_desc

#define I40E_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

/* Attempt to maximize the headroom available for incoming frames.  We
 * use a 2K buffer for receives and need 1536/1534 to store the data for
 * the frame.  This leaves us with 512 bytes of room.  From that we need
 * to deduct the space needed for the shared info and the padding needed
 * to IP align the frame.
 *
 * Note: For cache line sizes 256 or larger this value is going to end
 *	 up negative.  In these cases we should fall back to the legacy
 *	 receive path.
 */
#if (PAGE_SIZE < 8192)
#define I40E_2K_TOO_SMALL_WITH_PADDING \
((NET_SKB_PAD + I40E_RXBUFFER_1536) > SKB_WITH_OVERHEAD(I40E_RXBUFFER_2048))

static inline int i40e_compute_pad(int rx_buf_len)
{
	int page_size, pad_size;

	page_size = ALIGN(rx_buf_len, PAGE_SIZE / 2);
	pad_size = SKB_WITH_OVERHEAD(page_size) - rx_buf_len;

	return pad_size;
}

static inline int i40e_skb_pad(void)
{
	int rx_buf_len;

	/* If a 2K buffer cannot handle a standard Ethernet frame then
	 * optimize padding for a 3K buffer instead of a 1.5K buffer.
	 *
	 * For a 3K buffer we need to add enough padding to allow for
	 * tailroom due to NET_IP_ALIGN possibly shifting us out of
	 * cache-line alignment.
	 */
	if (I40E_2K_TOO_SMALL_WITH_PADDING)
		rx_buf_len = I40E_RXBUFFER_3072 + SKB_DATA_ALIGN(NET_IP_ALIGN);
	else
		rx_buf_len = I40E_RXBUFFER_1536;

	/* if needed make room for NET_IP_ALIGN */
	rx_buf_len -= NET_IP_ALIGN;

	return i40e_compute_pad(rx_buf_len);
}

#define I40E_SKB_PAD i40e_skb_pad()
#else
#define I40E_2K_TOO_SMALL_WITH_PADDING false
#define I40E_SKB_PAD (NET_SKB_PAD + NET_IP_ALIGN)
#endif

/**
 * i40e_test_staterr - tests bits in Rx descriptor status and error fields
 * @rx_desc: pointer to receive descriptor (in le64 format)
 * @stat_err_bits: value to mask
 *
 * This function does some fast chicanery in order to return the
 * value of the mask which is really only used for boolean tests.
 * The status_error_len doesn't need to be shifted because it begins
 * at offset zero.
 */
static inline bool i40e_test_staterr(union i40e_rx_desc *rx_desc,
				     const u64 stat_err_bits)
{
	return !!(rx_desc->wb.qword1.status_error_len &
		  cpu_to_le64(stat_err_bits));
}

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define I40E_RX_BUFFER_WRITE	16	/* Must be power of 2 */
#define I40E_RX_INCREMENT(r, i) \
	do {					\
		(i)++;				\
		if ((i) == (r)->count)		\
			i = 0;			\
		r->next_to_clean = i;		\
	} while (0)

#define I40E_RX_NEXT_DESC(r, i, n)		\
	do {					\
		(i)++;				\
		if ((i) == (r)->count)		\
			i = 0;			\
		(n) = I40E_RX_DESC((r), (i));	\
	} while (0)

#define I40E_RX_NEXT_DESC_PREFETCH(r, i, n)		\
	do {						\
		I40E_RX_NEXT_DESC((r), (i), (n));	\
		prefetch((n));				\
	} while (0)

#define I40E_MAX_BUFFER_TXD	8
#define I40E_MIN_TX_LEN		17

/* The size limit for a transmit buffer in a descriptor is (16K - 1).
 * In order to align with the read requests we will align the value to
 * the nearest 4K which represents our maximum read request size.
 */
#define I40E_MAX_READ_REQ_SIZE		4096
#define I40E_MAX_DATA_PER_TXD		(16 * 1024 - 1)
#define I40E_MAX_DATA_PER_TXD_ALIGNED \
	(I40E_MAX_DATA_PER_TXD & ~(I40E_MAX_READ_REQ_SIZE - 1))

/**
 * i40e_txd_use_count  - estimate the number of descriptors needed for Tx
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
static inline unsigned int i40e_txd_use_count(unsigned int size)
{
	return ((size * 85) >> 20) + 1;
}

/* Tx Descriptors needed, worst case */
#define DESC_NEEDED (MAX_SKB_FRAGS + 4)
#define I40E_MIN_DESC_PENDING	4

#define I40E_TX_FLAGS_HW_VLAN		BIT(1)
#define I40E_TX_FLAGS_SW_VLAN		BIT(2)
#define I40E_TX_FLAGS_TSO		BIT(3)
#define I40E_TX_FLAGS_IPV4		BIT(4)
#define I40E_TX_FLAGS_IPV6		BIT(5)
#define I40E_TX_FLAGS_FCCRC		BIT(6)
#define I40E_TX_FLAGS_FSO		BIT(7)
#define I40E_TX_FLAGS_FD_SB		BIT(9)
#define I40E_TX_FLAGS_VXLAN_TUNNEL	BIT(10)
#define I40E_TX_FLAGS_VLAN_MASK		0xffff0000
#define I40E_TX_FLAGS_VLAN_PRIO_MASK	0xe0000000
#define I40E_TX_FLAGS_VLAN_PRIO_SHIFT	29
#define I40E_TX_FLAGS_VLAN_SHIFT	16

struct i40e_tx_buffer {
	struct i40e_tx_desc *next_to_watch;
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

struct i40e_rx_buffer {
	dma_addr_t dma;
	struct page *page;
#if (BITS_PER_LONG > 32) || (PAGE_SIZE >= 65536)
	__u32 page_offset;
#else
	__u16 page_offset;
#endif
	__u16 pagecnt_bias;
};

struct i40e_queue_stats {
	u64 packets;
	u64 bytes;
};

struct i40e_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
	u64 tx_linearize;
	u64 tx_force_wb;
	u64 tx_lost_interrupt;
};

struct i40e_rx_queue_stats {
	u64 non_eop_descs;
	u64 alloc_page_failed;
	u64 alloc_buff_failed;
	u64 page_reuse_count;
	u64 realloc_count;
};

enum i40e_ring_state_t {
	__I40E_TX_FDIR_INIT_DONE,
	__I40E_TX_XPS_INIT_DONE,
};

/* some useful defines for virtchannel interface, which
 * is the only remaining user of header split
 */
#define I40E_RX_DTYPE_NO_SPLIT      0
#define I40E_RX_DTYPE_HEADER_SPLIT  1
#define I40E_RX_DTYPE_SPLIT_ALWAYS  2
#define I40E_RX_SPLIT_L2      0x1
#define I40E_RX_SPLIT_IP      0x2
#define I40E_RX_SPLIT_TCP_UDP 0x4
#define I40E_RX_SPLIT_SCTP    0x8

/* struct that defines a descriptor ring, associated with a VSI */
struct i40e_ring {
	struct i40e_ring *next;		/* pointer to next ring in q_vector */
	void *desc;			/* Descriptor ring memory */
	struct device *dev;		/* Used for DMA mapping */
	struct net_device *netdev;	/* netdev ring maps to */
	union {
		struct i40e_tx_buffer *tx_bi;
		struct i40e_rx_buffer *rx_bi;
	};
	unsigned long state;
	u16 queue_index;		/* Queue number of ring */
	u8 dcb_tc;			/* Traffic class of ring */
	u8 __iomem *tail;

	/* high bit set means dynamic, use accessors routines to read/write.
	 * hardware only supports 2us resolution for the ITR registers.
	 * these values always store the USER setting, and must be converted
	 * before programming to a register.
	 */
	u16 rx_itr_setting;
	u16 tx_itr_setting;

	u16 count;			/* Number of descriptors */
	u16 reg_idx;			/* HW register index of the ring */
	u16 rx_buf_len;

	/* used in interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;

	u8 atr_sample_rate;
	u8 atr_count;

	bool ring_active;		/* is ring online or not */
	bool arm_wb;		/* do something to arm write back */
	u8 packet_stride;

	u16 flags;
#define I40E_TXR_FLAGS_WB_ON_ITR		BIT(0)
#define I40E_RXR_FLAGS_BUILD_SKB_ENABLED	BIT(1)

	/* stats structs */
	struct i40e_queue_stats	stats;
	struct u64_stats_sync syncp;
	union {
		struct i40e_tx_queue_stats tx_stats;
		struct i40e_rx_queue_stats rx_stats;
	};

	unsigned int size;		/* length of descriptor ring in bytes */
	dma_addr_t dma;			/* physical address of ring */

	struct i40e_vsi *vsi;		/* Backreference to associated VSI */
	struct i40e_q_vector *q_vector;	/* Backreference to associated vector */

	struct rcu_head rcu;		/* to avoid race on free */
	u16 next_to_alloc;
	struct sk_buff *skb;		/* When i40evf_clean_rx_ring_irq() must
					 * return before it sees the EOP for
					 * the current packet, we save that skb
					 * here and resume receiving this
					 * packet the next time
					 * i40evf_clean_rx_ring_irq() is called
					 * for this ring.
					 */
} ____cacheline_internodealigned_in_smp;

static inline bool ring_uses_build_skb(struct i40e_ring *ring)
{
	return !!(ring->flags & I40E_RXR_FLAGS_BUILD_SKB_ENABLED);
}

static inline void set_ring_build_skb_enabled(struct i40e_ring *ring)
{
	ring->flags |= I40E_RXR_FLAGS_BUILD_SKB_ENABLED;
}

static inline void clear_ring_build_skb_enabled(struct i40e_ring *ring)
{
	ring->flags &= ~I40E_RXR_FLAGS_BUILD_SKB_ENABLED;
}

enum i40e_latency_range {
	I40E_LOWEST_LATENCY = 0,
	I40E_LOW_LATENCY = 1,
	I40E_BULK_LATENCY = 2,
	I40E_ULTRA_LATENCY = 3,
};

struct i40e_ring_container {
	/* array of pointers to rings */
	struct i40e_ring *ring;
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u16 count;
	enum i40e_latency_range latency_range;
	u16 itr;
};

/* iterator for handling rings in ring container */
#define i40e_for_each_ring(pos, head) \
	for (pos = (head).ring; pos != NULL; pos = pos->next)

static inline unsigned int i40e_rx_pg_order(struct i40e_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring->rx_buf_len > (PAGE_SIZE / 2))
		return 1;
#endif
	return 0;
}

#define i40e_rx_pg_size(_ring) (PAGE_SIZE << i40e_rx_pg_order(_ring))

bool i40evf_alloc_rx_buffers(struct i40e_ring *rxr, u16 cleaned_count);
netdev_tx_t i40evf_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
void i40evf_clean_tx_ring(struct i40e_ring *tx_ring);
void i40evf_clean_rx_ring(struct i40e_ring *rx_ring);
int i40evf_setup_tx_descriptors(struct i40e_ring *tx_ring);
int i40evf_setup_rx_descriptors(struct i40e_ring *rx_ring);
void i40evf_free_tx_resources(struct i40e_ring *tx_ring);
void i40evf_free_rx_resources(struct i40e_ring *rx_ring);
int i40evf_napi_poll(struct napi_struct *napi, int budget);
void i40evf_force_wb(struct i40e_vsi *vsi, struct i40e_q_vector *q_vector);
u32 i40evf_get_tx_pending(struct i40e_ring *ring, bool in_sw);
int __i40evf_maybe_stop_tx(struct i40e_ring *tx_ring, int size);
bool __i40evf_chk_linearize(struct sk_buff *skb);

/**
 * i40e_xmit_descriptor_count - calculate number of Tx descriptors needed
 * @skb:     send buffer
 * @tx_ring: ring to send buffer on
 *
 * Returns number of data descriptors needed for this skb. Returns 0 to indicate
 * there is not enough descriptors available in this ring since we need at least
 * one descriptor.
 **/
static inline int i40e_xmit_descriptor_count(struct sk_buff *skb)
{
	const struct skb_frag_struct *frag = &skb_shinfo(skb)->frags[0];
	unsigned int nr_frags = skb_shinfo(skb)->nr_frags;
	int count = 0, size = skb_headlen(skb);

	for (;;) {
		count += i40e_txd_use_count(size);

		if (!nr_frags--)
			break;

		size = skb_frag_size(frag++);
	}

	return count;
}

/**
 * i40e_maybe_stop_tx - 1st level check for Tx stop conditions
 * @tx_ring: the ring to be checked
 * @size:    the size buffer we want to assure is available
 *
 * Returns 0 if stop is not needed
 **/
static inline int i40e_maybe_stop_tx(struct i40e_ring *tx_ring, int size)
{
	if (likely(I40E_DESC_UNUSED(tx_ring) >= size))
		return 0;
	return __i40evf_maybe_stop_tx(tx_ring, size);
}

/**
 * i40e_chk_linearize - Check if there are more than 8 fragments per packet
 * @skb:      send buffer
 * @count:    number of buffers used
 *
 * Note: Our HW can't scatter-gather more than 8 fragments to build
 * a packet on the wire and so we need to figure out the cases where we
 * need to linearize the skb.
 **/
static inline bool i40e_chk_linearize(struct sk_buff *skb, int count)
{
	/* Both TSO and single send will work if count is less than 8 */
	if (likely(count < I40E_MAX_BUFFER_TXD))
		return false;

	if (skb_is_gso(skb))
		return __i40evf_chk_linearize(skb);

	/* we can support up to 8 data buffers for a single send */
	return count != I40E_MAX_BUFFER_TXD;
}
/**
 * @ring: Tx ring to find the netdev equivalent of
 **/
static inline struct netdev_queue *txring_txq(const struct i40e_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->queue_index);
}
#endif /* _I40E_TXRX_H_ */

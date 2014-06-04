/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
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
#define I40E_MIN_ITR               0x0004  /* reg uses 2 usec resolution */
#define I40E_MAX_IRATE             0x03F
#define I40E_MIN_IRATE             0x001
#define I40E_IRATE_USEC_RESOLUTION 4
#define I40E_ITR_100K              0x0005
#define I40E_ITR_20K               0x0019
#define I40E_ITR_8K                0x003E
#define I40E_ITR_4K                0x007A
#define I40E_ITR_RX_DEF            I40E_ITR_8K
#define I40E_ITR_TX_DEF            I40E_ITR_4K
#define I40E_ITR_DYNAMIC           0x8000  /* use top bit as a flag */
#define I40E_MIN_INT_RATE          250     /* ~= 1000000 / (I40E_MAX_ITR * 2) */
#define I40E_MAX_INT_RATE          500000  /* == 1000000 / (I40E_MIN_ITR * 2) */
#define I40E_DEFAULT_IRQ_WORK      256
#define ITR_TO_REG(setting) ((setting & ~I40E_ITR_DYNAMIC) >> 1)
#define ITR_IS_DYNAMIC(setting) (!!(setting & I40E_ITR_DYNAMIC))
#define ITR_REG_TO_USEC(itr_reg) (itr_reg << 1)

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
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_UDP) | \
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_SCTP) | \
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_TCP) | \
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV4_OTHER) | \
	((u64)1 << I40E_FILTER_PCTYPE_FRAG_IPV4) | \
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_UDP) | \
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_TCP) | \
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_SCTP) | \
	((u64)1 << I40E_FILTER_PCTYPE_NONF_IPV6_OTHER) | \
	((u64)1 << I40E_FILTER_PCTYPE_FRAG_IPV6) | \
	((u64)1 << I40E_FILTER_PCTYPE_L2_PAYLOAD))

/* Supported Rx Buffer Sizes */
#define I40E_RXBUFFER_512   512    /* Used for packet split */
#define I40E_RXBUFFER_2048  2048
#define I40E_RXBUFFER_3072  3072   /* For FCoE MTU of 2158 */
#define I40E_RXBUFFER_4096  4096
#define I40E_RXBUFFER_8192  8192
#define I40E_MAX_RXBUFFER   9728  /* largest size for single descriptor */

/* NOTE: netdev_alloc_skb reserves up to 64 bytes, NET_IP_ALIGN means we
 * reserve 2 more, and skb_shared_info adds an additional 384 bytes more,
 * this adds up to 512 bytes of extra data meaning the smallest allocation
 * we could have is 1K.
 * i.e. RXBUFFER_512 --> size-1024 slab
 */
#define I40E_RX_HDR_SIZE  I40E_RXBUFFER_512

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define I40E_RX_BUFFER_WRITE	16	/* Must be power of 2 */
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

#define i40e_rx_desc i40e_32byte_rx_desc

#define I40E_MIN_TX_LEN		17
#define I40E_MAX_DATA_PER_TXD	8192

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S) DIV_ROUND_UP((S), I40E_MAX_DATA_PER_TXD)
#define DESC_NEEDED (MAX_SKB_FRAGS + 4)

#define I40E_TX_FLAGS_CSUM		(u32)(1)
#define I40E_TX_FLAGS_HW_VLAN		(u32)(1 << 1)
#define I40E_TX_FLAGS_SW_VLAN		(u32)(1 << 2)
#define I40E_TX_FLAGS_TSO		(u32)(1 << 3)
#define I40E_TX_FLAGS_IPV4		(u32)(1 << 4)
#define I40E_TX_FLAGS_IPV6		(u32)(1 << 5)
#define I40E_TX_FLAGS_FCCRC		(u32)(1 << 6)
#define I40E_TX_FLAGS_FSO		(u32)(1 << 7)
#define I40E_TX_FLAGS_TSYN		(u32)(1 << 8)
#define I40E_TX_FLAGS_FD_SB		(u32)(1 << 9)
#define I40E_TX_FLAGS_VLAN_MASK		0xffff0000
#define I40E_TX_FLAGS_VLAN_PRIO_MASK	0xe0000000
#define I40E_TX_FLAGS_VLAN_PRIO_SHIFT	29
#define I40E_TX_FLAGS_VLAN_SHIFT	16

struct i40e_tx_buffer {
	struct i40e_tx_desc *next_to_watch;
	unsigned long time_stamp;
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
	struct sk_buff *skb;
	dma_addr_t dma;
	struct page *page;
	dma_addr_t page_dma;
	unsigned int page_offset;
};

struct i40e_queue_stats {
	u64 packets;
	u64 bytes;
};

struct i40e_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
};

struct i40e_rx_queue_stats {
	u64 non_eop_descs;
	u64 alloc_page_failed;
	u64 alloc_buff_failed;
};

enum i40e_ring_state_t {
	__I40E_TX_FDIR_INIT_DONE,
	__I40E_TX_XPS_INIT_DONE,
	__I40E_TX_DETECT_HANG,
	__I40E_HANG_CHECK_ARMED,
	__I40E_RX_PS_ENABLED,
	__I40E_RX_16BYTE_DESC_ENABLED,
};

#define ring_is_ps_enabled(ring) \
	test_bit(__I40E_RX_PS_ENABLED, &(ring)->state)
#define set_ring_ps_enabled(ring) \
	set_bit(__I40E_RX_PS_ENABLED, &(ring)->state)
#define clear_ring_ps_enabled(ring) \
	clear_bit(__I40E_RX_PS_ENABLED, &(ring)->state)
#define check_for_tx_hang(ring) \
	test_bit(__I40E_TX_DETECT_HANG, &(ring)->state)
#define set_check_for_tx_hang(ring) \
	set_bit(__I40E_TX_DETECT_HANG, &(ring)->state)
#define clear_check_for_tx_hang(ring) \
	clear_bit(__I40E_TX_DETECT_HANG, &(ring)->state)
#define ring_is_16byte_desc_enabled(ring) \
	test_bit(__I40E_RX_16BYTE_DESC_ENABLED, &(ring)->state)
#define set_ring_16byte_desc_enabled(ring) \
	set_bit(__I40E_RX_16BYTE_DESC_ENABLED, &(ring)->state)
#define clear_ring_16byte_desc_enabled(ring) \
	clear_bit(__I40E_RX_16BYTE_DESC_ENABLED, &(ring)->state)

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

	u16 count;			/* Number of descriptors */
	u16 reg_idx;			/* HW register index of the ring */
	u16 rx_hdr_len;
	u16 rx_buf_len;
	u8  dtype;
#define I40E_RX_DTYPE_NO_SPLIT      0
#define I40E_RX_DTYPE_SPLIT_ALWAYS  1
#define I40E_RX_DTYPE_HEADER_SPLIT  2
	u8  hsplit;
#define I40E_RX_SPLIT_L2      0x1
#define I40E_RX_SPLIT_IP      0x2
#define I40E_RX_SPLIT_TCP_UDP 0x4
#define I40E_RX_SPLIT_SCTP    0x8

	/* used in interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;

	u8 atr_sample_rate;
	u8 atr_count;

	unsigned long last_rx_timestamp;

	bool ring_active;		/* is ring online or not */

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
} ____cacheline_internodealigned_in_smp;

enum i40e_latency_range {
	I40E_LOWEST_LATENCY = 0,
	I40E_LOW_LATENCY = 1,
	I40E_BULK_LATENCY = 2,
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

void i40e_alloc_rx_buffers(struct i40e_ring *rxr, u16 cleaned_count);
netdev_tx_t i40e_lan_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
void i40e_clean_tx_ring(struct i40e_ring *tx_ring);
void i40e_clean_rx_ring(struct i40e_ring *rx_ring);
int i40e_setup_tx_descriptors(struct i40e_ring *tx_ring);
int i40e_setup_rx_descriptors(struct i40e_ring *rx_ring);
void i40e_free_tx_resources(struct i40e_ring *tx_ring);
void i40e_free_rx_resources(struct i40e_ring *rx_ring);
int i40e_napi_poll(struct napi_struct *napi, int budget);
#endif /* _I40E_TXRX_H_ */

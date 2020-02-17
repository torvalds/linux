/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_H_
#define _IGC_H_

#include <linux/kobject.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/ethtool.h>
#include <linux/sctp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>
#include <linux/net_tstamp.h>

#include "igc_hw.h"

/* forward declaration */
void igc_set_ethtool_ops(struct net_device *);

struct igc_adapter;
struct igc_ring;

void igc_up(struct igc_adapter *adapter);
void igc_down(struct igc_adapter *adapter);
int igc_setup_tx_resources(struct igc_ring *ring);
int igc_setup_rx_resources(struct igc_ring *ring);
void igc_free_tx_resources(struct igc_ring *ring);
void igc_free_rx_resources(struct igc_ring *ring);
unsigned int igc_get_max_rss_queues(struct igc_adapter *adapter);
void igc_set_flag_queue_pairs(struct igc_adapter *adapter,
			      const u32 max_rss_queues);
int igc_reinit_queues(struct igc_adapter *adapter);
void igc_write_rss_indir_tbl(struct igc_adapter *adapter);
bool igc_has_link(struct igc_adapter *adapter);
void igc_reset(struct igc_adapter *adapter);
int igc_set_spd_dplx(struct igc_adapter *adapter, u32 spd, u8 dplx);
int igc_add_mac_steering_filter(struct igc_adapter *adapter,
				const u8 *addr, u8 queue, u8 flags);
int igc_del_mac_steering_filter(struct igc_adapter *adapter,
				const u8 *addr, u8 queue, u8 flags);
void igc_update_stats(struct igc_adapter *adapter);

extern char igc_driver_name[];
extern char igc_driver_version[];

#define IGC_REGS_LEN			740
#define IGC_RETA_SIZE			128

/* flags controlling PTP/1588 function */
#define IGC_PTP_ENABLED		BIT(0)

/* Interrupt defines */
#define IGC_START_ITR			648 /* ~6000 ints/sec */
#define IGC_FLAG_HAS_MSI		BIT(0)
#define IGC_FLAG_QUEUE_PAIRS		BIT(3)
#define IGC_FLAG_DMAC			BIT(4)
#define IGC_FLAG_PTP			BIT(8)
#define IGC_FLAG_NEED_LINK_UPDATE	BIT(9)
#define IGC_FLAG_MEDIA_RESET		BIT(10)
#define IGC_FLAG_MAS_ENABLE		BIT(12)
#define IGC_FLAG_HAS_MSIX		BIT(13)
#define IGC_FLAG_VLAN_PROMISC		BIT(15)
#define IGC_FLAG_RX_LEGACY		BIT(16)

#define IGC_FLAG_RSS_FIELD_IPV4_UDP	BIT(6)
#define IGC_FLAG_RSS_FIELD_IPV6_UDP	BIT(7)

#define IGC_MRQC_ENABLE_RSS_MQ		0x00000002
#define IGC_MRQC_RSS_FIELD_IPV4_UDP	0x00400000
#define IGC_MRQC_RSS_FIELD_IPV6_UDP	0x00800000

#define IGC_START_ITR			648 /* ~6000 ints/sec */
#define IGC_4K_ITR			980
#define IGC_20K_ITR			196
#define IGC_70K_ITR			56

#define IGC_DEFAULT_ITR		3 /* dynamic */
#define IGC_MAX_ITR_USECS	10000
#define IGC_MIN_ITR_USECS	10
#define NON_Q_VECTORS		1
#define MAX_MSIX_ENTRIES	10

/* TX/RX descriptor defines */
#define IGC_DEFAULT_TXD		256
#define IGC_DEFAULT_TX_WORK	128
#define IGC_MIN_TXD		80
#define IGC_MAX_TXD		4096

#define IGC_DEFAULT_RXD		256
#define IGC_MIN_RXD		80
#define IGC_MAX_RXD		4096

/* Transmit and receive queues */
#define IGC_MAX_RX_QUEUES		4
#define IGC_MAX_TX_QUEUES		4

#define MAX_Q_VECTORS			8
#define MAX_STD_JUMBO_FRAME_SIZE	9216

/* Supported Rx Buffer Sizes */
#define IGC_RXBUFFER_256		256
#define IGC_RXBUFFER_2048		2048
#define IGC_RXBUFFER_3072		3072

#define AUTO_ALL_MODES		0
#define IGC_RX_HDR_LEN			IGC_RXBUFFER_256

/* Transmit and receive latency (for PTP timestamps) */
/* FIXME: These values were estimated using the ones that i210 has as
 * basis, they seem to provide good numbers with ptp4l/phc2sys, but we
 * need to confirm them.
 */
#define IGC_I225_TX_LATENCY_10		9542
#define IGC_I225_TX_LATENCY_100		1024
#define IGC_I225_TX_LATENCY_1000	178
#define IGC_I225_TX_LATENCY_2500	64
#define IGC_I225_RX_LATENCY_10		20662
#define IGC_I225_RX_LATENCY_100		2213
#define IGC_I225_RX_LATENCY_1000	448
#define IGC_I225_RX_LATENCY_2500	160

/* RX and TX descriptor control thresholds.
 * PTHRESH - MAC will consider prefetch if it has fewer than this number of
 *           descriptors available in its onboard memory.
 *           Setting this to 0 disables RX descriptor prefetch.
 * HTHRESH - MAC will only prefetch if there are at least this many descriptors
 *           available in host memory.
 *           If PTHRESH is 0, this should also be 0.
 * WTHRESH - RX descriptor writeback threshold - MAC will delay writing back
 *           descriptors until either it has this many to write back, or the
 *           ITR timer expires.
 */
#define IGC_RX_PTHRESH			8
#define IGC_RX_HTHRESH			8
#define IGC_TX_PTHRESH			8
#define IGC_TX_HTHRESH			1
#define IGC_RX_WTHRESH			4
#define IGC_TX_WTHRESH			16

#define IGC_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

#define IGC_TS_HDR_LEN			16

#define IGC_SKB_PAD			(NET_SKB_PAD + NET_IP_ALIGN)

#if (PAGE_SIZE < 8192)
#define IGC_MAX_FRAME_BUILD_SKB \
	(SKB_WITH_OVERHEAD(IGC_RXBUFFER_2048) - IGC_SKB_PAD - IGC_TS_HDR_LEN)
#else
#define IGC_MAX_FRAME_BUILD_SKB (IGC_RXBUFFER_2048 - IGC_TS_HDR_LEN)
#endif

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IGC_RX_BUFFER_WRITE	16 /* Must be power of 2 */

/* VLAN info */
#define IGC_TX_FLAGS_VLAN_MASK	0xffff0000

/* igc_test_staterr - tests bits within Rx descriptor status and error fields */
static inline __le32 igc_test_staterr(union igc_adv_rx_desc *rx_desc,
				      const u32 stat_err_bits)
{
	return rx_desc->wb.upper.status_error & cpu_to_le32(stat_err_bits);
}

enum igc_state_t {
	__IGC_TESTING,
	__IGC_RESETTING,
	__IGC_DOWN,
	__IGC_PTP_TX_IN_PROGRESS,
};

enum igc_tx_flags {
	/* cmd_type flags */
	IGC_TX_FLAGS_VLAN	= 0x01,
	IGC_TX_FLAGS_TSO	= 0x02,
	IGC_TX_FLAGS_TSTAMP	= 0x04,

	/* olinfo flags */
	IGC_TX_FLAGS_IPV4	= 0x10,
	IGC_TX_FLAGS_CSUM	= 0x20,
};

enum igc_boards {
	board_base,
};

/* The largest size we can write to the descriptor is 65535.  In order to
 * maintain a power of two alignment we have to limit ourselves to 32K.
 */
#define IGC_MAX_TXD_PWR		15
#define IGC_MAX_DATA_PER_TXD	BIT(IGC_MAX_TXD_PWR)

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S)	DIV_ROUND_UP((S), IGC_MAX_DATA_PER_TXD)
#define DESC_NEEDED	(MAX_SKB_FRAGS + 4)

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer
 */
struct igc_tx_buffer {
	union igc_adv_tx_desc *next_to_watch;
	unsigned long time_stamp;
	struct sk_buff *skb;
	unsigned int bytecount;
	u16 gso_segs;
	__be16 protocol;

	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct igc_rx_buffer {
	dma_addr_t dma;
	struct page *page;
#if (BITS_PER_LONG > 32) || (PAGE_SIZE >= 65536)
	__u32 page_offset;
#else
	__u16 page_offset;
#endif
	__u16 pagecnt_bias;
};

struct igc_tx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 restart_queue;
	u64 restart_queue2;
};

struct igc_rx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 drops;
	u64 csum_err;
	u64 alloc_failed;
};

struct igc_rx_packet_stats {
	u64 ipv4_packets;      /* IPv4 headers processed */
	u64 ipv4e_packets;     /* IPv4E headers with extensions processed */
	u64 ipv6_packets;      /* IPv6 headers processed */
	u64 ipv6e_packets;     /* IPv6E headers with extensions processed */
	u64 tcp_packets;       /* TCP headers processed */
	u64 udp_packets;       /* UDP headers processed */
	u64 sctp_packets;      /* SCTP headers processed */
	u64 nfs_packets;       /* NFS headers processe */
	u64 other_packets;
};

struct igc_ring_container {
	struct igc_ring *ring;          /* pointer to linked list of rings */
	unsigned int total_bytes;       /* total bytes processed this int */
	unsigned int total_packets;     /* total packets processed this int */
	u16 work_limit;                 /* total work allowed per interrupt */
	u8 count;                       /* total number of rings in vector */
	u8 itr;                         /* current ITR setting for ring */
};

struct igc_ring {
	struct igc_q_vector *q_vector;  /* backlink to q_vector */
	struct net_device *netdev;      /* back pointer to net_device */
	struct device *dev;             /* device for dma mapping */
	union {                         /* array of buffer info structs */
		struct igc_tx_buffer *tx_buffer_info;
		struct igc_rx_buffer *rx_buffer_info;
	};
	void *desc;                     /* descriptor ring memory */
	unsigned long flags;            /* ring specific flags */
	void __iomem *tail;             /* pointer to ring tail register */
	dma_addr_t dma;                 /* phys address of the ring */
	unsigned int size;              /* length of desc. ring in bytes */

	u16 count;                      /* number of desc. in the ring */
	u8 queue_index;                 /* logical index of the ring*/
	u8 reg_idx;                     /* physical index of the ring */
	bool launchtime_enable;		/* true if LaunchTime is enabled */

	/* everything past this point are written often */
	u16 next_to_clean;
	u16 next_to_use;
	u16 next_to_alloc;

	union {
		/* TX */
		struct {
			struct igc_tx_queue_stats tx_stats;
			struct u64_stats_sync tx_syncp;
			struct u64_stats_sync tx_syncp2;
		};
		/* RX */
		struct {
			struct igc_rx_queue_stats rx_stats;
			struct igc_rx_packet_stats pkt_stats;
			struct u64_stats_sync rx_syncp;
			struct sk_buff *skb;
		};
	};
} ____cacheline_internodealigned_in_smp;

struct igc_q_vector {
	struct igc_adapter *adapter;    /* backlink */
	void __iomem *itr_register;
	u32 eims_value;                 /* EIMS mask value */

	u16 itr_val;
	u8 set_itr;

	struct igc_ring_container rx, tx;

	struct napi_struct napi;

	struct rcu_head rcu;    /* to avoid race with update stats on free */
	char name[IFNAMSIZ + 9];
	struct net_device poll_dev;

	/* for dynamic allocation of rings associated with this q_vector */
	struct igc_ring ring[0] ____cacheline_internodealigned_in_smp;
};

#define MAX_ETYPE_FILTER		(4 - 1)

enum igc_filter_match_flags {
	IGC_FILTER_FLAG_ETHER_TYPE =	0x1,
	IGC_FILTER_FLAG_VLAN_TCI   =	0x2,
	IGC_FILTER_FLAG_SRC_MAC_ADDR =	0x4,
	IGC_FILTER_FLAG_DST_MAC_ADDR =	0x8,
};

/* RX network flow classification data structure */
struct igc_nfc_input {
	/* Byte layout in order, all values with MSB first:
	 * match_flags - 1 byte
	 * etype - 2 bytes
	 * vlan_tci - 2 bytes
	 */
	u8 match_flags;
	__be16 etype;
	__be16 vlan_tci;
	u8 src_addr[ETH_ALEN];
	u8 dst_addr[ETH_ALEN];
};

struct igc_nfc_filter {
	struct hlist_node nfc_node;
	struct igc_nfc_input filter;
	unsigned long cookie;
	u16 etype_reg_index;
	u16 sw_idx;
	u16 action;
};

struct igc_mac_addr {
	u8 addr[ETH_ALEN];
	u8 queue;
	u8 state; /* bitmask */
};

#define IGC_MAC_STATE_DEFAULT		0x1
#define IGC_MAC_STATE_IN_USE		0x2
#define IGC_MAC_STATE_SRC_ADDR		0x4
#define IGC_MAC_STATE_QUEUE_STEERING	0x8

#define IGC_MAX_RXNFC_FILTERS		16

/* Board specific private data structure */
struct igc_adapter {
	struct net_device *netdev;

	unsigned long state;
	unsigned int flags;
	unsigned int num_q_vectors;

	struct msix_entry *msix_entries;

	/* TX */
	u16 tx_work_limit;
	u32 tx_timeout_count;
	int num_tx_queues;
	struct igc_ring *tx_ring[IGC_MAX_TX_QUEUES];

	/* RX */
	int num_rx_queues;
	struct igc_ring *rx_ring[IGC_MAX_RX_QUEUES];

	struct timer_list watchdog_timer;
	struct timer_list dma_err_timer;
	struct timer_list phy_info_timer;

	u32 wol;
	u32 en_mng_pt;
	u16 link_speed;
	u16 link_duplex;

	u8 port_num;

	u8 __iomem *io_addr;
	/* Interrupt Throttle Rate */
	u32 rx_itr_setting;
	u32 tx_itr_setting;

	struct work_struct reset_task;
	struct work_struct watchdog_task;
	struct work_struct dma_err_task;
	bool fc_autoneg;

	u8 tx_timeout_factor;

	int msg_enable;
	u32 max_frame_size;
	u32 min_frame_size;

	/* OS defined structs */
	struct pci_dev *pdev;
	/* lock for statistics */
	spinlock_t stats64_lock;
	struct rtnl_link_stats64 stats64;

	/* structs defined in igc_hw.h */
	struct igc_hw hw;
	struct igc_hw_stats stats;

	struct igc_q_vector *q_vector[MAX_Q_VECTORS];
	u32 eims_enable_mask;
	u32 eims_other;

	u16 tx_ring_count;
	u16 rx_ring_count;

	u32 tx_hwtstamp_timeouts;
	u32 tx_hwtstamp_skipped;
	u32 rx_hwtstamp_cleared;

	u32 rss_queues;
	u32 rss_indir_tbl_init;

	/* RX network flow classification support */
	struct hlist_head nfc_filter_list;
	struct hlist_head cls_flower_list;
	unsigned int nfc_filter_count;

	/* lock for RX network flow classification filter */
	spinlock_t nfc_lock;
	bool etype_bitmap[MAX_ETYPE_FILTER];

	struct igc_mac_addr *mac_table;

	u8 rss_indir_tbl[IGC_RETA_SIZE];

	unsigned long link_check_timeout;
	struct igc_info ei;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_caps;
	struct work_struct ptp_tx_work;
	struct sk_buff *ptp_tx_skb;
	struct hwtstamp_config tstamp_config;
	unsigned long ptp_tx_start;
	unsigned long last_rx_ptp_check;
	unsigned long last_rx_timestamp;
	unsigned int ptp_flags;
	/* System time value lock */
	spinlock_t tmreg_lock;
	struct cyclecounter cc;
	struct timecounter tc;
};

/* igc_desc_unused - calculate if we have unused descriptors */
static inline u16 igc_desc_unused(const struct igc_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

static inline s32 igc_get_phy_info(struct igc_hw *hw)
{
	if (hw->phy.ops.get_phy_info)
		return hw->phy.ops.get_phy_info(hw);

	return 0;
}

static inline s32 igc_reset_phy(struct igc_hw *hw)
{
	if (hw->phy.ops.reset)
		return hw->phy.ops.reset(hw);

	return 0;
}

static inline struct netdev_queue *txring_txq(const struct igc_ring *tx_ring)
{
	return netdev_get_tx_queue(tx_ring->netdev, tx_ring->queue_index);
}

enum igc_ring_flags_t {
	IGC_RING_FLAG_RX_3K_BUFFER,
	IGC_RING_FLAG_RX_BUILD_SKB_ENABLED,
	IGC_RING_FLAG_RX_SCTP_CSUM,
	IGC_RING_FLAG_RX_LB_VLAN_BSWAP,
	IGC_RING_FLAG_TX_CTX_IDX,
	IGC_RING_FLAG_TX_DETECT_HANG
};

#define ring_uses_large_buffer(ring) \
	test_bit(IGC_RING_FLAG_RX_3K_BUFFER, &(ring)->flags)

#define ring_uses_build_skb(ring) \
	test_bit(IGC_RING_FLAG_RX_BUILD_SKB_ENABLED, &(ring)->flags)

static inline unsigned int igc_rx_bufsz(struct igc_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring_uses_large_buffer(ring))
		return IGC_RXBUFFER_3072;

	if (ring_uses_build_skb(ring))
		return IGC_MAX_FRAME_BUILD_SKB + IGC_TS_HDR_LEN;
#endif
	return IGC_RXBUFFER_2048;
}

static inline unsigned int igc_rx_pg_order(struct igc_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring_uses_large_buffer(ring))
		return 1;
#endif
	return 0;
}

static inline s32 igc_read_phy_reg(struct igc_hw *hw, u32 offset, u16 *data)
{
	if (hw->phy.ops.read_reg)
		return hw->phy.ops.read_reg(hw, offset, data);

	return 0;
}

/* forward declaration */
void igc_reinit_locked(struct igc_adapter *);
int igc_add_filter(struct igc_adapter *adapter,
		   struct igc_nfc_filter *input);
int igc_erase_filter(struct igc_adapter *adapter,
		     struct igc_nfc_filter *input);

void igc_ptp_init(struct igc_adapter *adapter);
void igc_ptp_reset(struct igc_adapter *adapter);
void igc_ptp_stop(struct igc_adapter *adapter);
void igc_ptp_rx_rgtstamp(struct igc_q_vector *q_vector, struct sk_buff *skb);
void igc_ptp_rx_pktstamp(struct igc_q_vector *q_vector, void *va,
			 struct sk_buff *skb);
int igc_ptp_set_ts_config(struct net_device *netdev, struct ifreq *ifr);
int igc_ptp_get_ts_config(struct net_device *netdev, struct ifreq *ifr);
void igc_ptp_tx_hang(struct igc_adapter *adapter);

#define igc_rx_pg_size(_ring) (PAGE_SIZE << igc_rx_pg_order(_ring))

#define IGC_TXD_DCMD	(IGC_ADVTXD_DCMD_EOP | IGC_ADVTXD_DCMD_RS)

#define IGC_RX_DESC(R, i)       \
	(&(((union igc_adv_rx_desc *)((R)->desc))[i]))
#define IGC_TX_DESC(R, i)       \
	(&(((union igc_adv_tx_desc *)((R)->desc))[i]))
#define IGC_TX_CTXTDESC(R, i)   \
	(&(((struct igc_adv_tx_context_desc *)((R)->desc))[i]))

#endif /* _IGC_H_ */

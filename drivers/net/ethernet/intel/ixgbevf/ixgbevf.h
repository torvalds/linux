/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2018 Intel Corporation. */

#ifndef _IXGBEVF_H_
#define _IXGBEVF_H_

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/u64_stats_sync.h>
#include <net/xdp.h>

#include "vf.h"
#include "ipsec.h"

#define IXGBE_MAX_TXD_PWR	14
#define IXGBE_MAX_DATA_PER_TXD	BIT(IXGBE_MAX_TXD_PWR)

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S) DIV_ROUND_UP((S), IXGBE_MAX_DATA_PER_TXD)
#define DESC_NEEDED (MAX_SKB_FRAGS + 4)

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer
 */
struct ixgbevf_tx_buffer {
	union ixgbe_adv_tx_desc *next_to_watch;
	unsigned long time_stamp;
	union {
		struct sk_buff *skb;
		/* XDP uses address ptr on irq_clean */
		void *data;
	};
	unsigned int bytecount;
	unsigned short gso_segs;
	__be16 protocol;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct ixgbevf_rx_buffer {
	dma_addr_t dma;
	struct page *page;
#if (BITS_PER_LONG > 32) || (PAGE_SIZE >= 65536)
	__u32 page_offset;
#else
	__u16 page_offset;
#endif
	__u16 pagecnt_bias;
};

struct ixgbevf_stats {
	u64 packets;
	u64 bytes;
};

struct ixgbevf_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
};

struct ixgbevf_rx_queue_stats {
	u64 alloc_rx_page_failed;
	u64 alloc_rx_buff_failed;
	u64 alloc_rx_page;
	u64 csum_err;
};

enum ixgbevf_ring_state_t {
	__IXGBEVF_RX_3K_BUFFER,
	__IXGBEVF_RX_BUILD_SKB_ENABLED,
	__IXGBEVF_TX_DETECT_HANG,
	__IXGBEVF_HANG_CHECK_ARMED,
	__IXGBEVF_TX_XDP_RING,
	__IXGBEVF_TX_XDP_RING_PRIMED,
};

#define ring_is_xdp(ring) \
		test_bit(__IXGBEVF_TX_XDP_RING, &(ring)->state)
#define set_ring_xdp(ring) \
		set_bit(__IXGBEVF_TX_XDP_RING, &(ring)->state)
#define clear_ring_xdp(ring) \
		clear_bit(__IXGBEVF_TX_XDP_RING, &(ring)->state)

struct ixgbevf_ring {
	struct ixgbevf_ring *next;
	struct ixgbevf_q_vector *q_vector;	/* backpointer to q_vector */
	struct net_device *netdev;
	struct bpf_prog *xdp_prog;
	struct device *dev;
	void *desc;			/* descriptor ring memory */
	dma_addr_t dma;			/* phys. address of descriptor ring */
	unsigned int size;		/* length in bytes */
	u16 count;			/* amount of descriptors */
	u16 next_to_use;
	u16 next_to_clean;
	u16 next_to_alloc;

	union {
		struct ixgbevf_tx_buffer *tx_buffer_info;
		struct ixgbevf_rx_buffer *rx_buffer_info;
	};
	unsigned long state;
	struct ixgbevf_stats stats;
	struct u64_stats_sync syncp;
	union {
		struct ixgbevf_tx_queue_stats tx_stats;
		struct ixgbevf_rx_queue_stats rx_stats;
	};
	struct xdp_rxq_info xdp_rxq;
	u64 hw_csum_rx_error;
	u8 __iomem *tail;
	struct sk_buff *skb;

	/* holds the special value that gets the hardware register offset
	 * associated with this ring, which is different for DCB and RSS modes
	 */
	u16 reg_idx;
	int queue_index; /* needed for multiqueue queue management */
} ____cacheline_internodealigned_in_smp;

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBEVF_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define MAX_RX_QUEUES IXGBE_VF_MAX_RX_QUEUES
#define MAX_TX_QUEUES IXGBE_VF_MAX_TX_QUEUES
#define MAX_XDP_QUEUES IXGBE_VF_MAX_TX_QUEUES
#define IXGBEVF_MAX_RSS_QUEUES		2
#define IXGBEVF_82599_RETA_SIZE		128	/* 128 entries */
#define IXGBEVF_X550_VFRETA_SIZE	64	/* 64 entries */
#define IXGBEVF_RSS_HASH_KEY_SIZE	40
#define IXGBEVF_VFRSSRK_REGS		10	/* 10 registers for RSS key */

#define IXGBEVF_DEFAULT_TXD	1024
#define IXGBEVF_DEFAULT_RXD	512
#define IXGBEVF_MAX_TXD		4096
#define IXGBEVF_MIN_TXD		64
#define IXGBEVF_MAX_RXD		4096
#define IXGBEVF_MIN_RXD		64

/* Supported Rx Buffer Sizes */
#define IXGBEVF_RXBUFFER_256	256    /* Used for packet split */
#define IXGBEVF_RXBUFFER_2048	2048
#define IXGBEVF_RXBUFFER_3072	3072

#define IXGBEVF_RX_HDR_SIZE	IXGBEVF_RXBUFFER_256

#define MAXIMUM_ETHERNET_VLAN_SIZE (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)

#define IXGBEVF_SKB_PAD		(NET_SKB_PAD + NET_IP_ALIGN)
#if (PAGE_SIZE < 8192)
#define IXGBEVF_MAX_FRAME_BUILD_SKB \
	(SKB_WITH_OVERHEAD(IXGBEVF_RXBUFFER_2048) - IXGBEVF_SKB_PAD)
#else
#define IXGBEVF_MAX_FRAME_BUILD_SKB	IXGBEVF_RXBUFFER_2048
#endif

#define IXGBE_TX_FLAGS_CSUM		BIT(0)
#define IXGBE_TX_FLAGS_VLAN		BIT(1)
#define IXGBE_TX_FLAGS_TSO		BIT(2)
#define IXGBE_TX_FLAGS_IPV4		BIT(3)
#define IXGBE_TX_FLAGS_IPSEC		BIT(4)
#define IXGBE_TX_FLAGS_VLAN_MASK	0xffff0000
#define IXGBE_TX_FLAGS_VLAN_PRIO_MASK	0x0000e000
#define IXGBE_TX_FLAGS_VLAN_SHIFT	16

#define ring_uses_large_buffer(ring) \
	test_bit(__IXGBEVF_RX_3K_BUFFER, &(ring)->state)
#define set_ring_uses_large_buffer(ring) \
	set_bit(__IXGBEVF_RX_3K_BUFFER, &(ring)->state)
#define clear_ring_uses_large_buffer(ring) \
	clear_bit(__IXGBEVF_RX_3K_BUFFER, &(ring)->state)

#define ring_uses_build_skb(ring) \
	test_bit(__IXGBEVF_RX_BUILD_SKB_ENABLED, &(ring)->state)
#define set_ring_build_skb_enabled(ring) \
	set_bit(__IXGBEVF_RX_BUILD_SKB_ENABLED, &(ring)->state)
#define clear_ring_build_skb_enabled(ring) \
	clear_bit(__IXGBEVF_RX_BUILD_SKB_ENABLED, &(ring)->state)

static inline unsigned int ixgbevf_rx_bufsz(struct ixgbevf_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring_uses_large_buffer(ring))
		return IXGBEVF_RXBUFFER_3072;

	if (ring_uses_build_skb(ring))
		return IXGBEVF_MAX_FRAME_BUILD_SKB;
#endif
	return IXGBEVF_RXBUFFER_2048;
}

static inline unsigned int ixgbevf_rx_pg_order(struct ixgbevf_ring *ring)
{
#if (PAGE_SIZE < 8192)
	if (ring_uses_large_buffer(ring))
		return 1;
#endif
	return 0;
}

#define ixgbevf_rx_pg_size(_ring) (PAGE_SIZE << ixgbevf_rx_pg_order(_ring))

#define check_for_tx_hang(ring) \
	test_bit(__IXGBEVF_TX_DETECT_HANG, &(ring)->state)
#define set_check_for_tx_hang(ring) \
	set_bit(__IXGBEVF_TX_DETECT_HANG, &(ring)->state)
#define clear_check_for_tx_hang(ring) \
	clear_bit(__IXGBEVF_TX_DETECT_HANG, &(ring)->state)

struct ixgbevf_ring_container {
	struct ixgbevf_ring *ring;	/* pointer to linked list of rings */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u8 count;			/* total number of rings in vector */
	u8 itr;				/* current ITR setting for ring */
};

/* iterator for handling rings in ring container */
#define ixgbevf_for_each_ring(pos, head) \
	for (pos = (head).ring; pos != NULL; pos = pos->next)

/* MAX_MSIX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ixgbevf_q_vector {
	struct ixgbevf_adapter *adapter;
	/* index of q_vector within array, also used for finding the bit in
	 * EICR and friends that represents the vector for this ring
	 */
	u16 v_idx;
	u16 itr; /* Interrupt throttle rate written to EITR */
	struct napi_struct napi;
	struct ixgbevf_ring_container rx, tx;
	struct rcu_head rcu;    /* to avoid race with update stats on free */
	char name[IFNAMSIZ + 9];

	/* for dynamic allocation of rings associated with this q_vector */
	struct ixgbevf_ring ring[0] ____cacheline_internodealigned_in_smp;
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned int state;
#define IXGBEVF_QV_STATE_IDLE		0
#define IXGBEVF_QV_STATE_NAPI		1    /* NAPI owns this QV */
#define IXGBEVF_QV_STATE_POLL		2    /* poll owns this QV */
#define IXGBEVF_QV_STATE_DISABLED	4    /* QV is disabled */
#define IXGBEVF_QV_OWNED	(IXGBEVF_QV_STATE_NAPI | IXGBEVF_QV_STATE_POLL)
#define IXGBEVF_QV_LOCKED	(IXGBEVF_QV_OWNED | IXGBEVF_QV_STATE_DISABLED)
#define IXGBEVF_QV_STATE_NAPI_YIELD	8    /* NAPI yielded this QV */
#define IXGBEVF_QV_STATE_POLL_YIELD	16   /* poll yielded this QV */
#define IXGBEVF_QV_YIELD	(IXGBEVF_QV_STATE_NAPI_YIELD | \
				 IXGBEVF_QV_STATE_POLL_YIELD)
#define IXGBEVF_QV_USER_PEND	(IXGBEVF_QV_STATE_POLL | \
				 IXGBEVF_QV_STATE_POLL_YIELD)
	spinlock_t lock;
#endif /* CONFIG_NET_RX_BUSY_POLL */
};

/* microsecond values for various ITR rates shifted by 2 to fit itr register
 * with the first 3 bits reserved 0
 */
#define IXGBE_MIN_RSC_ITR	24
#define IXGBE_100K_ITR		40
#define IXGBE_20K_ITR		200
#define IXGBE_12K_ITR		336

/* Helper macros to switch between ints/sec and what the register uses.
 * And yes, it's the same math going both ways.  The lowest value
 * supported by all of the ixgbe hardware is 8.
 */
#define EITR_INTS_PER_SEC_TO_REG(_eitr) \
	((_eitr) ? (1000000000 / ((_eitr) * 256)) : 8)
#define EITR_REG_TO_INTS_PER_SEC EITR_INTS_PER_SEC_TO_REG

/* ixgbevf_test_staterr - tests bits in Rx descriptor status and error fields */
static inline __le32 ixgbevf_test_staterr(union ixgbe_adv_rx_desc *rx_desc,
					  const u32 stat_err_bits)
{
	return rx_desc->wb.upper.status_error & cpu_to_le32(stat_err_bits);
}

static inline u16 ixgbevf_desc_unused(struct ixgbevf_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

static inline void ixgbevf_write_tail(struct ixgbevf_ring *ring, u32 value)
{
	writel(value, ring->tail);
}

#define IXGBEVF_RX_DESC(R, i)	\
	(&(((union ixgbe_adv_rx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_DESC(R, i)	\
	(&(((union ixgbe_adv_tx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_CTXTDESC(R, i)	\
	(&(((struct ixgbe_adv_tx_context_desc *)((R)->desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE	9728 /* Maximum Supported Size 9.5KB */

#define OTHER_VECTOR	1
#define NON_Q_VECTORS	(OTHER_VECTOR)

#define MAX_MSIX_Q_VECTORS	2

#define MIN_MSIX_Q_VECTORS	1
#define MIN_MSIX_COUNT		(MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

#define IXGBEVF_RX_DMA_ATTR \
	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

/* board specific private data structure */
struct ixgbevf_adapter {
	/* this field must be first, see ixgbevf_process_skb_fields */
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	struct ixgbevf_q_vector *q_vector[MAX_MSIX_Q_VECTORS];

	/* Interrupt Throttle Rate */
	u16 rx_itr_setting;
	u16 tx_itr_setting;

	/* interrupt masks */
	u32 eims_enable_mask;
	u32 eims_other;

	/* XDP */
	int num_xdp_queues;
	struct ixgbevf_ring *xdp_ring[MAX_XDP_QUEUES];

	/* TX */
	int num_tx_queues;
	struct ixgbevf_ring *tx_ring[MAX_TX_QUEUES]; /* One per active queue */
	u64 restart_queue;
	u32 tx_timeout_count;
	u64 tx_ipsec;

	/* RX */
	int num_rx_queues;
	struct ixgbevf_ring *rx_ring[MAX_TX_QUEUES]; /* One per active queue */
	u64 hw_csum_rx_error;
	u64 hw_rx_no_dma_resources;
	int num_msix_vectors;
	u64 alloc_rx_page_failed;
	u64 alloc_rx_buff_failed;
	u64 alloc_rx_page;
	u64 rx_ipsec;

	struct msix_entry *msix_entries;

	/* OS defined structs */
	struct net_device *netdev;
	struct bpf_prog *xdp_prog;
	struct pci_dev *pdev;

	/* structs defined in ixgbe_vf.h */
	struct ixgbe_hw hw;
	u16 msg_enable;
	/* Interrupt Throttle Rate */
	u32 eitr_param;

	struct ixgbevf_hw_stats stats;

	unsigned long state;
	u64 tx_busy;
	unsigned int tx_ring_count;
	unsigned int xdp_ring_count;
	unsigned int rx_ring_count;

	u8 __iomem *io_addr; /* Mainly for iounmap use */
	u32 link_speed;
	bool link_up;

	struct timer_list service_timer;
	struct work_struct service_task;

	spinlock_t mbx_lock;
	unsigned long last_reset;

	u32 *rss_key;
	u8 rss_indir_tbl[IXGBEVF_X550_VFRETA_SIZE];
	u32 flags;
	bool link_state;

#define IXGBEVF_FLAGS_LEGACY_RX		BIT(1)

#ifdef CONFIG_XFRM
	struct ixgbevf_ipsec *ipsec;
#endif /* CONFIG_XFRM */
};

enum ixbgevf_state_t {
	__IXGBEVF_TESTING,
	__IXGBEVF_RESETTING,
	__IXGBEVF_DOWN,
	__IXGBEVF_DISABLED,
	__IXGBEVF_REMOVING,
	__IXGBEVF_SERVICE_SCHED,
	__IXGBEVF_SERVICE_INITED,
	__IXGBEVF_RESET_REQUESTED,
	__IXGBEVF_QUEUE_RESET_REQUESTED,
};

enum ixgbevf_boards {
	board_82599_vf,
	board_82599_vf_hv,
	board_X540_vf,
	board_X540_vf_hv,
	board_X550_vf,
	board_X550_vf_hv,
	board_X550EM_x_vf,
	board_X550EM_x_vf_hv,
	board_x550em_a_vf,
};

enum ixgbevf_xcast_modes {
	IXGBEVF_XCAST_MODE_NONE = 0,
	IXGBEVF_XCAST_MODE_MULTI,
	IXGBEVF_XCAST_MODE_ALLMULTI,
	IXGBEVF_XCAST_MODE_PROMISC,
};

extern const struct ixgbevf_info ixgbevf_82599_vf_info;
extern const struct ixgbevf_info ixgbevf_X540_vf_info;
extern const struct ixgbevf_info ixgbevf_X550_vf_info;
extern const struct ixgbevf_info ixgbevf_X550EM_x_vf_info;
extern const struct ixgbe_mbx_operations ixgbevf_mbx_ops;
extern const struct ixgbe_mbx_operations ixgbevf_mbx_ops_legacy;
extern const struct ixgbevf_info ixgbevf_x550em_a_vf_info;

extern const struct ixgbevf_info ixgbevf_82599_vf_hv_info;
extern const struct ixgbevf_info ixgbevf_X540_vf_hv_info;
extern const struct ixgbevf_info ixgbevf_X550_vf_hv_info;
extern const struct ixgbevf_info ixgbevf_X550EM_x_vf_hv_info;
extern const struct ixgbe_mbx_operations ixgbevf_hv_mbx_ops;

/* needed by ethtool.c */
extern const char ixgbevf_driver_name[];

int ixgbevf_open(struct net_device *netdev);
int ixgbevf_close(struct net_device *netdev);
void ixgbevf_up(struct ixgbevf_adapter *adapter);
void ixgbevf_down(struct ixgbevf_adapter *adapter);
void ixgbevf_reinit_locked(struct ixgbevf_adapter *adapter);
void ixgbevf_reset(struct ixgbevf_adapter *adapter);
void ixgbevf_set_ethtool_ops(struct net_device *netdev);
int ixgbevf_setup_rx_resources(struct ixgbevf_adapter *adapter,
			       struct ixgbevf_ring *rx_ring);
int ixgbevf_setup_tx_resources(struct ixgbevf_ring *);
void ixgbevf_free_rx_resources(struct ixgbevf_ring *);
void ixgbevf_free_tx_resources(struct ixgbevf_ring *);
void ixgbevf_update_stats(struct ixgbevf_adapter *adapter);
int ethtool_ioctl(struct ifreq *ifr);

extern void ixgbevf_write_eitr(struct ixgbevf_q_vector *q_vector);

#ifdef CONFIG_IXGBEVF_IPSEC
void ixgbevf_init_ipsec_offload(struct ixgbevf_adapter *adapter);
void ixgbevf_stop_ipsec_offload(struct ixgbevf_adapter *adapter);
void ixgbevf_ipsec_restore(struct ixgbevf_adapter *adapter);
void ixgbevf_ipsec_rx(struct ixgbevf_ring *rx_ring,
		      union ixgbe_adv_rx_desc *rx_desc,
		      struct sk_buff *skb);
int ixgbevf_ipsec_tx(struct ixgbevf_ring *tx_ring,
		     struct ixgbevf_tx_buffer *first,
		     struct ixgbevf_ipsec_tx_data *itd);
#else
static inline void ixgbevf_init_ipsec_offload(struct ixgbevf_adapter *adapter)
{ }
static inline void ixgbevf_stop_ipsec_offload(struct ixgbevf_adapter *adapter)
{ }
static inline void ixgbevf_ipsec_restore(struct ixgbevf_adapter *adapter) { }
static inline void ixgbevf_ipsec_rx(struct ixgbevf_ring *rx_ring,
				    union ixgbe_adv_rx_desc *rx_desc,
				    struct sk_buff *skb) { }
static inline int ixgbevf_ipsec_tx(struct ixgbevf_ring *tx_ring,
				   struct ixgbevf_tx_buffer *first,
				   struct ixgbevf_ipsec_tx_data *itd)
{ return 0; }
#endif /* CONFIG_IXGBEVF_IPSEC */

#define ixgbevf_hw_to_netdev(hw) \
	(((struct ixgbevf_adapter *)(hw)->back)->netdev)

#define hw_dbg(hw, format, arg...) \
	netdev_dbg(ixgbevf_hw_to_netdev(hw), format, ## arg)

s32 ixgbevf_poll_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size);
s32 ixgbevf_write_mbx(struct ixgbe_hw *hw, u32 *msg, u16 size);

#endif /* _IXGBEVF_H_ */

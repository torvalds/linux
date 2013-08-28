/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2013 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _IXGBE_H_
#define _IXGBE_H_

#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/cpumask.h>
#include <linux/aer.h>
#include <linux/if_vlan.h>
#include <linux/jiffies.h>

#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>

#include "ixgbe_type.h"
#include "ixgbe_common.h"
#include "ixgbe_dcb.h"
#if defined(CONFIG_FCOE) || defined(CONFIG_FCOE_MODULE)
#define IXGBE_FCOE
#include "ixgbe_fcoe.h"
#endif /* CONFIG_FCOE or CONFIG_FCOE_MODULE */
#ifdef CONFIG_IXGBE_DCA
#include <linux/dca.h>
#endif

#include <net/busy_poll.h>

#ifdef CONFIG_NET_LL_RX_POLL
#define LL_EXTENDED_STATS
#endif
/* common prefix used by pr_<> macros */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* TX/RX descriptor defines */
#define IXGBE_DEFAULT_TXD		    512
#define IXGBE_DEFAULT_TX_WORK		    256
#define IXGBE_MAX_TXD			   4096
#define IXGBE_MIN_TXD			     64

#define IXGBE_DEFAULT_RXD		    512
#define IXGBE_MAX_RXD			   4096
#define IXGBE_MIN_RXD			     64

/* flow control */
#define IXGBE_MIN_FCRTL			   0x40
#define IXGBE_MAX_FCRTL			0x7FF80
#define IXGBE_MIN_FCRTH			  0x600
#define IXGBE_MAX_FCRTH			0x7FFF0
#define IXGBE_DEFAULT_FCPAUSE		 0xFFFF
#define IXGBE_MIN_FCPAUSE		      0
#define IXGBE_MAX_FCPAUSE		 0xFFFF

/* Supported Rx Buffer Sizes */
#define IXGBE_RXBUFFER_256    256  /* Used for skb receive header */
#define IXGBE_RXBUFFER_2K    2048
#define IXGBE_RXBUFFER_3K    3072
#define IXGBE_RXBUFFER_4K    4096
#define IXGBE_MAX_RXBUFFER  16384  /* largest size for a single descriptor */

/*
 * NOTE: netdev_alloc_skb reserves up to 64 bytes, NET_IP_ALIGN means we
 * reserve 64 more, and skb_shared_info adds an additional 320 bytes more,
 * this adds up to 448 bytes of extra data.
 *
 * Since netdev_alloc_skb now allocates a page fragment we can use a value
 * of 256 and the resultant skb will have a truesize of 960 or less.
 */
#define IXGBE_RX_HDR_SIZE IXGBE_RXBUFFER_256

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBE_RX_BUFFER_WRITE	16	/* Must be power of 2 */

enum ixgbe_tx_flags {
	/* cmd_type flags */
	IXGBE_TX_FLAGS_HW_VLAN	= 0x01,
	IXGBE_TX_FLAGS_TSO	= 0x02,
	IXGBE_TX_FLAGS_TSTAMP	= 0x04,

	/* olinfo flags */
	IXGBE_TX_FLAGS_CC	= 0x08,
	IXGBE_TX_FLAGS_IPV4	= 0x10,
	IXGBE_TX_FLAGS_CSUM	= 0x20,

	/* software defined flags */
	IXGBE_TX_FLAGS_SW_VLAN	= 0x40,
	IXGBE_TX_FLAGS_FCOE	= 0x80,
};

/* VLAN info */
#define IXGBE_TX_FLAGS_VLAN_MASK	0xffff0000
#define IXGBE_TX_FLAGS_VLAN_PRIO_MASK	0xe0000000
#define IXGBE_TX_FLAGS_VLAN_PRIO_SHIFT  29
#define IXGBE_TX_FLAGS_VLAN_SHIFT	16

#define IXGBE_MAX_VF_MC_ENTRIES         30
#define IXGBE_MAX_VF_FUNCTIONS          64
#define IXGBE_MAX_VFTA_ENTRIES          128
#define MAX_EMULATION_MAC_ADDRS         16
#define IXGBE_MAX_PF_MACVLANS           15
#define VMDQ_P(p)   ((p) + adapter->ring_feature[RING_F_VMDQ].offset)
#define IXGBE_82599_VF_DEVICE_ID        0x10ED
#define IXGBE_X540_VF_DEVICE_ID         0x1515

struct vf_data_storage {
	unsigned char vf_mac_addresses[ETH_ALEN];
	u16 vf_mc_hashes[IXGBE_MAX_VF_MC_ENTRIES];
	u16 num_vf_mc_hashes;
	u16 default_vf_vlan_id;
	u16 vlans_enabled;
	bool clear_to_send;
	bool pf_set_mac;
	u16 pf_vlan; /* When set, guest VLAN config not allowed. */
	u16 pf_qos;
	u16 tx_rate;
	u16 vlan_count;
	u8 spoofchk_enabled;
	unsigned int vf_api;
};

struct vf_macvlans {
	struct list_head l;
	int vf;
	int rar_entry;
	bool free;
	bool is_macvlan;
	u8 vf_macvlan[ETH_ALEN];
};

#define IXGBE_MAX_TXD_PWR	14
#define IXGBE_MAX_DATA_PER_TXD	(1 << IXGBE_MAX_TXD_PWR)

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S) DIV_ROUND_UP((S), IXGBE_MAX_DATA_PER_TXD)
#define DESC_NEEDED (MAX_SKB_FRAGS + 4)

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct ixgbe_tx_buffer {
	union ixgbe_adv_tx_desc *next_to_watch;
	unsigned long time_stamp;
	struct sk_buff *skb;
	unsigned int bytecount;
	unsigned short gso_segs;
	__be16 protocol;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct ixgbe_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	struct page *page;
	unsigned int page_offset;
};

struct ixgbe_queue_stats {
	u64 packets;
	u64 bytes;
#ifdef LL_EXTENDED_STATS
	u64 yields;
	u64 misses;
	u64 cleaned;
#endif  /* LL_EXTENDED_STATS */
};

struct ixgbe_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 tx_done_old;
};

struct ixgbe_rx_queue_stats {
	u64 rsc_count;
	u64 rsc_flush;
	u64 non_eop_descs;
	u64 alloc_rx_page_failed;
	u64 alloc_rx_buff_failed;
	u64 csum_err;
};

enum ixgbe_ring_state_t {
	__IXGBE_TX_FDIR_INIT_DONE,
	__IXGBE_TX_XPS_INIT_DONE,
	__IXGBE_TX_DETECT_HANG,
	__IXGBE_HANG_CHECK_ARMED,
	__IXGBE_RX_RSC_ENABLED,
	__IXGBE_RX_CSUM_UDP_ZERO_ERR,
	__IXGBE_RX_FCOE,
};

#define check_for_tx_hang(ring) \
	test_bit(__IXGBE_TX_DETECT_HANG, &(ring)->state)
#define set_check_for_tx_hang(ring) \
	set_bit(__IXGBE_TX_DETECT_HANG, &(ring)->state)
#define clear_check_for_tx_hang(ring) \
	clear_bit(__IXGBE_TX_DETECT_HANG, &(ring)->state)
#define ring_is_rsc_enabled(ring) \
	test_bit(__IXGBE_RX_RSC_ENABLED, &(ring)->state)
#define set_ring_rsc_enabled(ring) \
	set_bit(__IXGBE_RX_RSC_ENABLED, &(ring)->state)
#define clear_ring_rsc_enabled(ring) \
	clear_bit(__IXGBE_RX_RSC_ENABLED, &(ring)->state)
struct ixgbe_ring {
	struct ixgbe_ring *next;	/* pointer to next ring in q_vector */
	struct ixgbe_q_vector *q_vector; /* backpointer to host q_vector */
	struct net_device *netdev;	/* netdev ring belongs to */
	struct device *dev;		/* device for DMA mapping */
	void *desc;			/* descriptor ring memory */
	union {
		struct ixgbe_tx_buffer *tx_buffer_info;
		struct ixgbe_rx_buffer *rx_buffer_info;
	};
	unsigned long last_rx_timestamp;
	unsigned long state;
	u8 __iomem *tail;
	dma_addr_t dma;			/* phys. address of descriptor ring */
	unsigned int size;		/* length in bytes */

	u16 count;			/* amount of descriptors */

	u8 queue_index; /* needed for multiqueue queue management */
	u8 reg_idx;			/* holds the special value that gets
					 * the hardware register offset
					 * associated with this ring, which is
					 * different for DCB and RSS modes
					 */
	u16 next_to_use;
	u16 next_to_clean;

	union {
		u16 next_to_alloc;
		struct {
			u8 atr_sample_rate;
			u8 atr_count;
		};
	};

	u8 dcb_tc;
	struct ixgbe_queue_stats stats;
	struct u64_stats_sync syncp;
	union {
		struct ixgbe_tx_queue_stats tx_stats;
		struct ixgbe_rx_queue_stats rx_stats;
	};
} ____cacheline_internodealigned_in_smp;

enum ixgbe_ring_f_enum {
	RING_F_NONE = 0,
	RING_F_VMDQ,  /* SR-IOV uses the same ring feature */
	RING_F_RSS,
	RING_F_FDIR,
#ifdef IXGBE_FCOE
	RING_F_FCOE,
#endif /* IXGBE_FCOE */

	RING_F_ARRAY_SIZE      /* must be last in enum set */
};

#define IXGBE_MAX_RSS_INDICES  16
#define IXGBE_MAX_VMDQ_INDICES 64
#define IXGBE_MAX_FDIR_INDICES 63	/* based on q_vector limit */
#define IXGBE_MAX_FCOE_INDICES  8
#define MAX_RX_QUEUES (IXGBE_MAX_FDIR_INDICES + 1)
#define MAX_TX_QUEUES (IXGBE_MAX_FDIR_INDICES + 1)
struct ixgbe_ring_feature {
	u16 limit;	/* upper limit on feature indices */
	u16 indices;	/* current value of indices */
	u16 mask;	/* Mask used for feature to ring mapping */
	u16 offset;	/* offset to start of feature */
} ____cacheline_internodealigned_in_smp;

#define IXGBE_82599_VMDQ_8Q_MASK 0x78
#define IXGBE_82599_VMDQ_4Q_MASK 0x7C
#define IXGBE_82599_VMDQ_2Q_MASK 0x7E

/*
 * FCoE requires that all Rx buffers be over 2200 bytes in length.  Since
 * this is twice the size of a half page we need to double the page order
 * for FCoE enabled Rx queues.
 */
static inline unsigned int ixgbe_rx_bufsz(struct ixgbe_ring *ring)
{
#ifdef IXGBE_FCOE
	if (test_bit(__IXGBE_RX_FCOE, &ring->state))
		return (PAGE_SIZE < 8192) ? IXGBE_RXBUFFER_4K :
					    IXGBE_RXBUFFER_3K;
#endif
	return IXGBE_RXBUFFER_2K;
}

static inline unsigned int ixgbe_rx_pg_order(struct ixgbe_ring *ring)
{
#ifdef IXGBE_FCOE
	if (test_bit(__IXGBE_RX_FCOE, &ring->state))
		return (PAGE_SIZE < 8192) ? 1 : 0;
#endif
	return 0;
}
#define ixgbe_rx_pg_size(_ring) (PAGE_SIZE << ixgbe_rx_pg_order(_ring))

struct ixgbe_ring_container {
	struct ixgbe_ring *ring;	/* pointer to linked list of rings */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u16 work_limit;			/* total work allowed per interrupt */
	u8 count;			/* total number of rings in vector */
	u8 itr;				/* current ITR setting for ring */
};

/* iterator for handling rings in ring container */
#define ixgbe_for_each_ring(pos, head) \
	for (pos = (head).ring; pos != NULL; pos = pos->next)

#define MAX_RX_PACKET_BUFFERS ((adapter->flags & IXGBE_FLAG_DCB_ENABLED) \
                              ? 8 : 1)
#define MAX_TX_PACKET_BUFFERS MAX_RX_PACKET_BUFFERS

/* MAX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ixgbe_q_vector {
	struct ixgbe_adapter *adapter;
#ifdef CONFIG_IXGBE_DCA
	int cpu;	    /* CPU for DCA */
#endif
	u16 v_idx;		/* index of q_vector within array, also used for
				 * finding the bit in EICR and friends that
				 * represents the vector for this ring */
	u16 itr;		/* Interrupt throttle rate written to EITR */
	struct ixgbe_ring_container rx, tx;

	struct napi_struct napi;
	cpumask_t affinity_mask;
	int numa_node;
	struct rcu_head rcu;	/* to avoid race with update stats on free */
	char name[IFNAMSIZ + 9];

#ifdef CONFIG_NET_LL_RX_POLL
	unsigned int state;
#define IXGBE_QV_STATE_IDLE        0
#define IXGBE_QV_STATE_NAPI	   1    /* NAPI owns this QV */
#define IXGBE_QV_STATE_POLL	   2    /* poll owns this QV */
#define IXGBE_QV_LOCKED (IXGBE_QV_STATE_NAPI | IXGBE_QV_STATE_POLL)
#define IXGBE_QV_STATE_NAPI_YIELD  4    /* NAPI yielded this QV */
#define IXGBE_QV_STATE_POLL_YIELD  8    /* poll yielded this QV */
#define IXGBE_QV_YIELD (IXGBE_QV_STATE_NAPI_YIELD | IXGBE_QV_STATE_POLL_YIELD)
#define IXGBE_QV_USER_PEND (IXGBE_QV_STATE_POLL | IXGBE_QV_STATE_POLL_YIELD)
	spinlock_t lock;
#endif  /* CONFIG_NET_LL_RX_POLL */

	/* for dynamic allocation of rings associated with this q_vector */
	struct ixgbe_ring ring[0] ____cacheline_internodealigned_in_smp;
};
#ifdef CONFIG_NET_LL_RX_POLL
static inline void ixgbe_qv_init_lock(struct ixgbe_q_vector *q_vector)
{

	spin_lock_init(&q_vector->lock);
	q_vector->state = IXGBE_QV_STATE_IDLE;
}

/* called from the device poll routine to get ownership of a q_vector */
static inline bool ixgbe_qv_lock_napi(struct ixgbe_q_vector *q_vector)
{
	int rc = true;
	spin_lock(&q_vector->lock);
	if (q_vector->state & IXGBE_QV_LOCKED) {
		WARN_ON(q_vector->state & IXGBE_QV_STATE_NAPI);
		q_vector->state |= IXGBE_QV_STATE_NAPI_YIELD;
		rc = false;
#ifdef LL_EXTENDED_STATS
		q_vector->tx.ring->stats.yields++;
#endif
	} else
		/* we don't care if someone yielded */
		q_vector->state = IXGBE_QV_STATE_NAPI;
	spin_unlock(&q_vector->lock);
	return rc;
}

/* returns true is someone tried to get the qv while napi had it */
static inline bool ixgbe_qv_unlock_napi(struct ixgbe_q_vector *q_vector)
{
	int rc = false;
	spin_lock(&q_vector->lock);
	WARN_ON(q_vector->state & (IXGBE_QV_STATE_POLL |
			       IXGBE_QV_STATE_NAPI_YIELD));

	if (q_vector->state & IXGBE_QV_STATE_POLL_YIELD)
		rc = true;
	q_vector->state = IXGBE_QV_STATE_IDLE;
	spin_unlock(&q_vector->lock);
	return rc;
}

/* called from ixgbe_low_latency_poll() */
static inline bool ixgbe_qv_lock_poll(struct ixgbe_q_vector *q_vector)
{
	int rc = true;
	spin_lock_bh(&q_vector->lock);
	if ((q_vector->state & IXGBE_QV_LOCKED)) {
		q_vector->state |= IXGBE_QV_STATE_POLL_YIELD;
		rc = false;
#ifdef LL_EXTENDED_STATS
		q_vector->rx.ring->stats.yields++;
#endif
	} else
		/* preserve yield marks */
		q_vector->state |= IXGBE_QV_STATE_POLL;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* returns true if someone tried to get the qv while it was locked */
static inline bool ixgbe_qv_unlock_poll(struct ixgbe_q_vector *q_vector)
{
	int rc = false;
	spin_lock_bh(&q_vector->lock);
	WARN_ON(q_vector->state & (IXGBE_QV_STATE_NAPI));

	if (q_vector->state & IXGBE_QV_STATE_POLL_YIELD)
		rc = true;
	q_vector->state = IXGBE_QV_STATE_IDLE;
	spin_unlock_bh(&q_vector->lock);
	return rc;
}

/* true if a socket is polling, even if it did not get the lock */
static inline bool ixgbe_qv_ll_polling(struct ixgbe_q_vector *q_vector)
{
	WARN_ON(!(q_vector->state & IXGBE_QV_LOCKED));
	return q_vector->state & IXGBE_QV_USER_PEND;
}
#else /* CONFIG_NET_LL_RX_POLL */
static inline void ixgbe_qv_init_lock(struct ixgbe_q_vector *q_vector)
{
}

static inline bool ixgbe_qv_lock_napi(struct ixgbe_q_vector *q_vector)
{
	return true;
}

static inline bool ixgbe_qv_unlock_napi(struct ixgbe_q_vector *q_vector)
{
	return false;
}

static inline bool ixgbe_qv_lock_poll(struct ixgbe_q_vector *q_vector)
{
	return false;
}

static inline bool ixgbe_qv_unlock_poll(struct ixgbe_q_vector *q_vector)
{
	return false;
}

static inline bool ixgbe_qv_ll_polling(struct ixgbe_q_vector *q_vector)
{
	return false;
}
#endif /* CONFIG_NET_LL_RX_POLL */

#ifdef CONFIG_IXGBE_HWMON

#define IXGBE_HWMON_TYPE_LOC		0
#define IXGBE_HWMON_TYPE_TEMP		1
#define IXGBE_HWMON_TYPE_CAUTION	2
#define IXGBE_HWMON_TYPE_MAX		3

struct hwmon_attr {
	struct device_attribute dev_attr;
	struct ixgbe_hw *hw;
	struct ixgbe_thermal_diode_data *sensor;
	char name[12];
};

struct hwmon_buff {
	struct device *device;
	struct hwmon_attr *hwmon_list;
	unsigned int n_hwmon;
};
#endif /* CONFIG_IXGBE_HWMON */

/*
 * microsecond values for various ITR rates shifted by 2 to fit itr register
 * with the first 3 bits reserved 0
 */
#define IXGBE_MIN_RSC_ITR	24
#define IXGBE_100K_ITR		40
#define IXGBE_20K_ITR		200
#define IXGBE_10K_ITR		400
#define IXGBE_8K_ITR		500

/* ixgbe_test_staterr - tests bits in Rx descriptor status and error fields */
static inline __le32 ixgbe_test_staterr(union ixgbe_adv_rx_desc *rx_desc,
					const u32 stat_err_bits)
{
	return rx_desc->wb.upper.status_error & cpu_to_le32(stat_err_bits);
}

static inline u16 ixgbe_desc_unused(struct ixgbe_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

#define IXGBE_RX_DESC(R, i)	    \
	(&(((union ixgbe_adv_rx_desc *)((R)->desc))[i]))
#define IXGBE_TX_DESC(R, i)	    \
	(&(((union ixgbe_adv_tx_desc *)((R)->desc))[i]))
#define IXGBE_TX_CTXTDESC(R, i)	    \
	(&(((struct ixgbe_adv_tx_context_desc *)((R)->desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE	9728 /* Maximum Supported Size 9.5KB */
#ifdef IXGBE_FCOE
/* Use 3K as the baby jumbo frame size for FCoE */
#define IXGBE_FCOE_JUMBO_FRAME_SIZE       3072
#endif /* IXGBE_FCOE */

#define OTHER_VECTOR 1
#define NON_Q_VECTORS (OTHER_VECTOR)

#define MAX_MSIX_VECTORS_82599 64
#define MAX_Q_VECTORS_82599 64
#define MAX_MSIX_VECTORS_82598 18
#define MAX_Q_VECTORS_82598 16

#define MAX_Q_VECTORS MAX_Q_VECTORS_82599
#define MAX_MSIX_COUNT MAX_MSIX_VECTORS_82599

#define MIN_MSIX_Q_VECTORS 1
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

/* default to trying for four seconds */
#define IXGBE_TRY_LINK_TIMEOUT (4 * HZ)

/* board specific private data structure */
struct ixgbe_adapter {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	unsigned long state;

	/* Some features need tri-state capability,
	 * thus the additional *_CAPABLE flags.
	 */
	u32 flags;
#define IXGBE_FLAG_MSI_CAPABLE                  (u32)(1 << 0)
#define IXGBE_FLAG_MSI_ENABLED                  (u32)(1 << 1)
#define IXGBE_FLAG_MSIX_CAPABLE                 (u32)(1 << 2)
#define IXGBE_FLAG_MSIX_ENABLED                 (u32)(1 << 3)
#define IXGBE_FLAG_RX_1BUF_CAPABLE              (u32)(1 << 4)
#define IXGBE_FLAG_RX_PS_CAPABLE                (u32)(1 << 5)
#define IXGBE_FLAG_RX_PS_ENABLED                (u32)(1 << 6)
#define IXGBE_FLAG_IN_NETPOLL                   (u32)(1 << 7)
#define IXGBE_FLAG_DCA_ENABLED                  (u32)(1 << 8)
#define IXGBE_FLAG_DCA_CAPABLE                  (u32)(1 << 9)
#define IXGBE_FLAG_IMIR_ENABLED                 (u32)(1 << 10)
#define IXGBE_FLAG_MQ_CAPABLE                   (u32)(1 << 11)
#define IXGBE_FLAG_DCB_ENABLED                  (u32)(1 << 12)
#define IXGBE_FLAG_VMDQ_CAPABLE                 (u32)(1 << 13)
#define IXGBE_FLAG_VMDQ_ENABLED                 (u32)(1 << 14)
#define IXGBE_FLAG_FAN_FAIL_CAPABLE             (u32)(1 << 15)
#define IXGBE_FLAG_NEED_LINK_UPDATE             (u32)(1 << 16)
#define IXGBE_FLAG_NEED_LINK_CONFIG             (u32)(1 << 17)
#define IXGBE_FLAG_FDIR_HASH_CAPABLE            (u32)(1 << 18)
#define IXGBE_FLAG_FDIR_PERFECT_CAPABLE         (u32)(1 << 19)
#define IXGBE_FLAG_FCOE_CAPABLE                 (u32)(1 << 20)
#define IXGBE_FLAG_FCOE_ENABLED                 (u32)(1 << 21)
#define IXGBE_FLAG_SRIOV_CAPABLE                (u32)(1 << 22)
#define IXGBE_FLAG_SRIOV_ENABLED                (u32)(1 << 23)

	u32 flags2;
#define IXGBE_FLAG2_RSC_CAPABLE                 (u32)(1 << 0)
#define IXGBE_FLAG2_RSC_ENABLED                 (u32)(1 << 1)
#define IXGBE_FLAG2_TEMP_SENSOR_CAPABLE         (u32)(1 << 2)
#define IXGBE_FLAG2_TEMP_SENSOR_EVENT           (u32)(1 << 3)
#define IXGBE_FLAG2_SEARCH_FOR_SFP              (u32)(1 << 4)
#define IXGBE_FLAG2_SFP_NEEDS_RESET             (u32)(1 << 5)
#define IXGBE_FLAG2_RESET_REQUESTED             (u32)(1 << 6)
#define IXGBE_FLAG2_FDIR_REQUIRES_REINIT        (u32)(1 << 7)
#define IXGBE_FLAG2_RSS_FIELD_IPV4_UDP		(u32)(1 << 8)
#define IXGBE_FLAG2_RSS_FIELD_IPV6_UDP		(u32)(1 << 9)
#define IXGBE_FLAG2_PTP_ENABLED			(u32)(1 << 10)
#define IXGBE_FLAG2_PTP_PPS_ENABLED		(u32)(1 << 11)
#define IXGBE_FLAG2_BRIDGE_MODE_VEB		(u32)(1 << 12)

	/* Tx fast path data */
	int num_tx_queues;
	u16 tx_itr_setting;
	u16 tx_work_limit;

	/* Rx fast path data */
	int num_rx_queues;
	u16 rx_itr_setting;

	/* TX */
	struct ixgbe_ring *tx_ring[MAX_TX_QUEUES] ____cacheline_aligned_in_smp;

	u64 restart_queue;
	u64 lsc_int;
	u32 tx_timeout_count;

	/* RX */
	struct ixgbe_ring *rx_ring[MAX_RX_QUEUES];
	int num_rx_pools;		/* == num_rx_queues in 82598 */
	int num_rx_queues_per_pool;	/* 1 if 82598, can be many if 82599 */
	u64 hw_csum_rx_error;
	u64 hw_rx_no_dma_resources;
	u64 rsc_total_count;
	u64 rsc_total_flush;
	u64 non_eop_descs;
	u32 alloc_rx_page_failed;
	u32 alloc_rx_buff_failed;

	struct ixgbe_q_vector *q_vector[MAX_Q_VECTORS];

	/* DCB parameters */
	struct ieee_pfc *ixgbe_ieee_pfc;
	struct ieee_ets *ixgbe_ieee_ets;
	struct ixgbe_dcb_config dcb_cfg;
	struct ixgbe_dcb_config temp_dcb_cfg;
	u8 dcb_set_bitmap;
	u8 dcbx_cap;
	enum ixgbe_fc_mode last_lfc_mode;

	int num_q_vectors;	/* current number of q_vectors for device */
	int max_q_vectors;	/* true count of q_vectors for device */
	struct ixgbe_ring_feature ring_feature[RING_F_ARRAY_SIZE];
	struct msix_entry *msix_entries;

	u32 test_icr;
	struct ixgbe_ring test_tx_ring;
	struct ixgbe_ring test_rx_ring;

	/* structs defined in ixgbe_hw.h */
	struct ixgbe_hw hw;
	u16 msg_enable;
	struct ixgbe_hw_stats stats;

	u64 tx_busy;
	unsigned int tx_ring_count;
	unsigned int rx_ring_count;

	u32 link_speed;
	bool link_up;
	unsigned long link_check_timeout;

	struct timer_list service_timer;
	struct work_struct service_task;

	struct hlist_head fdir_filter_list;
	unsigned long fdir_overflow; /* number of times ATR was backed off */
	union ixgbe_atr_input fdir_mask;
	int fdir_filter_count;
	u32 fdir_pballoc;
	u32 atr_sample_rate;
	spinlock_t fdir_perfect_lock;

#ifdef IXGBE_FCOE
	struct ixgbe_fcoe fcoe;
#endif /* IXGBE_FCOE */
	u32 wol;

	u16 bd_number;

	u16 eeprom_verh;
	u16 eeprom_verl;
	u16 eeprom_cap;

	u32 interrupt_event;
	u32 led_reg;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_caps;
	struct work_struct ptp_tx_work;
	struct sk_buff *ptp_tx_skb;
	unsigned long ptp_tx_start;
	unsigned long last_overflow_check;
	unsigned long last_rx_ptp_check;
	spinlock_t tmreg_lock;
	struct cyclecounter cc;
	struct timecounter tc;
	u32 base_incval;

	/* SR-IOV */
	DECLARE_BITMAP(active_vfs, IXGBE_MAX_VF_FUNCTIONS);
	unsigned int num_vfs;
	struct vf_data_storage *vfinfo;
	int vf_rate_link_speed;
	struct vf_macvlans vf_mvs;
	struct vf_macvlans *mv_list;

	u32 timer_event_accumulator;
	u32 vferr_refcount;
	struct kobject *info_kobj;
#ifdef CONFIG_IXGBE_HWMON
	struct hwmon_buff ixgbe_hwmon_buff;
#endif /* CONFIG_IXGBE_HWMON */
#ifdef CONFIG_DEBUG_FS
	struct dentry *ixgbe_dbg_adapter;
#endif /*CONFIG_DEBUG_FS*/

	u8 default_up;
};

struct ixgbe_fdir_filter {
	struct hlist_node fdir_node;
	union ixgbe_atr_input filter;
	u16 sw_idx;
	u16 action;
};

enum ixgbe_state_t {
	__IXGBE_TESTING,
	__IXGBE_RESETTING,
	__IXGBE_DOWN,
	__IXGBE_SERVICE_SCHED,
	__IXGBE_IN_SFP_INIT,
	__IXGBE_READ_I2C,
};

struct ixgbe_cb {
	union {				/* Union defining head/tail partner */
		struct sk_buff *head;
		struct sk_buff *tail;
	};
	dma_addr_t dma;
	u16 append_cnt;
	bool page_released;
};
#define IXGBE_CB(skb) ((struct ixgbe_cb *)(skb)->cb)

enum ixgbe_boards {
	board_82598,
	board_82599,
	board_X540,
};

extern struct ixgbe_info ixgbe_82598_info;
extern struct ixgbe_info ixgbe_82599_info;
extern struct ixgbe_info ixgbe_X540_info;
#ifdef CONFIG_IXGBE_DCB
extern const struct dcbnl_rtnl_ops dcbnl_ops;
#endif

extern char ixgbe_driver_name[];
extern const char ixgbe_driver_version[];
#ifdef IXGBE_FCOE
extern char ixgbe_default_device_descr[];
#endif /* IXGBE_FCOE */

extern void ixgbe_up(struct ixgbe_adapter *adapter);
extern void ixgbe_down(struct ixgbe_adapter *adapter);
extern void ixgbe_reinit_locked(struct ixgbe_adapter *adapter);
extern void ixgbe_reset(struct ixgbe_adapter *adapter);
extern void ixgbe_set_ethtool_ops(struct net_device *netdev);
extern int ixgbe_setup_rx_resources(struct ixgbe_ring *);
extern int ixgbe_setup_tx_resources(struct ixgbe_ring *);
extern void ixgbe_free_rx_resources(struct ixgbe_ring *);
extern void ixgbe_free_tx_resources(struct ixgbe_ring *);
extern void ixgbe_configure_rx_ring(struct ixgbe_adapter *,struct ixgbe_ring *);
extern void ixgbe_configure_tx_ring(struct ixgbe_adapter *,struct ixgbe_ring *);
extern void ixgbe_disable_rx_queue(struct ixgbe_adapter *adapter,
				   struct ixgbe_ring *);
extern void ixgbe_update_stats(struct ixgbe_adapter *adapter);
extern int ixgbe_init_interrupt_scheme(struct ixgbe_adapter *adapter);
extern int ixgbe_wol_supported(struct ixgbe_adapter *adapter, u16 device_id,
			       u16 subdevice_id);
extern void ixgbe_clear_interrupt_scheme(struct ixgbe_adapter *adapter);
extern netdev_tx_t ixgbe_xmit_frame_ring(struct sk_buff *,
					 struct ixgbe_adapter *,
					 struct ixgbe_ring *);
extern void ixgbe_unmap_and_free_tx_resource(struct ixgbe_ring *,
                                             struct ixgbe_tx_buffer *);
extern void ixgbe_alloc_rx_buffers(struct ixgbe_ring *, u16);
extern void ixgbe_write_eitr(struct ixgbe_q_vector *);
extern int ixgbe_poll(struct napi_struct *napi, int budget);
extern int ethtool_ioctl(struct ifreq *ifr);
extern s32 ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw);
extern s32 ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, u32 fdirctrl);
extern s32 ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, u32 fdirctrl);
extern s32 ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
						 union ixgbe_atr_hash_dword input,
						 union ixgbe_atr_hash_dword common,
                                                 u8 queue);
extern s32 ixgbe_fdir_set_input_mask_82599(struct ixgbe_hw *hw,
					   union ixgbe_atr_input *input_mask);
extern s32 ixgbe_fdir_write_perfect_filter_82599(struct ixgbe_hw *hw,
						 union ixgbe_atr_input *input,
						 u16 soft_id, u8 queue);
extern s32 ixgbe_fdir_erase_perfect_filter_82599(struct ixgbe_hw *hw,
						 union ixgbe_atr_input *input,
						 u16 soft_id);
extern void ixgbe_atr_compute_perfect_hash_82599(union ixgbe_atr_input *input,
						 union ixgbe_atr_input *mask);
extern bool ixgbe_verify_lesm_fw_enabled_82599(struct ixgbe_hw *hw);
extern void ixgbe_set_rx_mode(struct net_device *netdev);
#ifdef CONFIG_IXGBE_DCB
extern void ixgbe_set_rx_drop_en(struct ixgbe_adapter *adapter);
#endif
extern int ixgbe_setup_tc(struct net_device *dev, u8 tc);
extern void ixgbe_tx_ctxtdesc(struct ixgbe_ring *, u32, u32, u32, u32);
extern void ixgbe_do_reset(struct net_device *netdev);
#ifdef CONFIG_IXGBE_HWMON
extern void ixgbe_sysfs_exit(struct ixgbe_adapter *adapter);
extern int ixgbe_sysfs_init(struct ixgbe_adapter *adapter);
#endif /* CONFIG_IXGBE_HWMON */
#ifdef IXGBE_FCOE
extern void ixgbe_configure_fcoe(struct ixgbe_adapter *adapter);
extern int ixgbe_fso(struct ixgbe_ring *tx_ring,
		     struct ixgbe_tx_buffer *first,
		     u8 *hdr_len);
extern int ixgbe_fcoe_ddp(struct ixgbe_adapter *adapter,
			  union ixgbe_adv_rx_desc *rx_desc,
			  struct sk_buff *skb);
extern int ixgbe_fcoe_ddp_get(struct net_device *netdev, u16 xid,
                              struct scatterlist *sgl, unsigned int sgc);
extern int ixgbe_fcoe_ddp_target(struct net_device *netdev, u16 xid,
				 struct scatterlist *sgl, unsigned int sgc);
extern int ixgbe_fcoe_ddp_put(struct net_device *netdev, u16 xid);
extern int ixgbe_setup_fcoe_ddp_resources(struct ixgbe_adapter *adapter);
extern void ixgbe_free_fcoe_ddp_resources(struct ixgbe_adapter *adapter);
extern int ixgbe_fcoe_enable(struct net_device *netdev);
extern int ixgbe_fcoe_disable(struct net_device *netdev);
#ifdef CONFIG_IXGBE_DCB
extern u8 ixgbe_fcoe_getapp(struct ixgbe_adapter *adapter);
extern u8 ixgbe_fcoe_setapp(struct ixgbe_adapter *adapter, u8 up);
#endif /* CONFIG_IXGBE_DCB */
extern int ixgbe_fcoe_get_wwn(struct net_device *netdev, u64 *wwn, int type);
extern int ixgbe_fcoe_get_hbainfo(struct net_device *netdev,
				  struct netdev_fcoe_hbainfo *info);
extern u8 ixgbe_fcoe_get_tc(struct ixgbe_adapter *adapter);
#endif /* IXGBE_FCOE */
#ifdef CONFIG_DEBUG_FS
extern void ixgbe_dbg_adapter_init(struct ixgbe_adapter *adapter);
extern void ixgbe_dbg_adapter_exit(struct ixgbe_adapter *adapter);
extern void ixgbe_dbg_init(void);
extern void ixgbe_dbg_exit(void);
#else
static inline void ixgbe_dbg_adapter_init(struct ixgbe_adapter *adapter) {}
static inline void ixgbe_dbg_adapter_exit(struct ixgbe_adapter *adapter) {}
static inline void ixgbe_dbg_init(void) {}
static inline void ixgbe_dbg_exit(void) {}
#endif /* CONFIG_DEBUG_FS */
static inline struct netdev_queue *txring_txq(const struct ixgbe_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->queue_index);
}

extern void ixgbe_ptp_init(struct ixgbe_adapter *adapter);
extern void ixgbe_ptp_stop(struct ixgbe_adapter *adapter);
extern void ixgbe_ptp_overflow_check(struct ixgbe_adapter *adapter);
extern void ixgbe_ptp_rx_hang(struct ixgbe_adapter *adapter);
extern void __ixgbe_ptp_rx_hwtstamp(struct ixgbe_q_vector *q_vector,
				    struct sk_buff *skb);
static inline void ixgbe_ptp_rx_hwtstamp(struct ixgbe_ring *rx_ring,
					 union ixgbe_adv_rx_desc *rx_desc,
					 struct sk_buff *skb)
{
	if (unlikely(!ixgbe_test_staterr(rx_desc, IXGBE_RXDADV_STAT_TS)))
		return;

	__ixgbe_ptp_rx_hwtstamp(rx_ring->q_vector, skb);

	/*
	 * Update the last_rx_timestamp timer in order to enable watchdog check
	 * for error case of latched timestamp on a dropped packet.
	 */
	rx_ring->last_rx_timestamp = jiffies;
}

extern int ixgbe_ptp_hwtstamp_ioctl(struct ixgbe_adapter *adapter,
				    struct ifreq *ifr, int cmd);
extern void ixgbe_ptp_start_cyclecounter(struct ixgbe_adapter *adapter);
extern void ixgbe_ptp_reset(struct ixgbe_adapter *adapter);
extern void ixgbe_ptp_check_pps_event(struct ixgbe_adapter *adapter, u32 eicr);
#ifdef CONFIG_PCI_IOV
void ixgbe_sriov_reinit(struct ixgbe_adapter *adapter);
#endif

#endif /* _IXGBE_H_ */

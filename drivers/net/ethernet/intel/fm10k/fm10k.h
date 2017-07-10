/* Intel(R) Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2017 Intel Corporation.
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
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _FM10K_H_
#define _FM10K_H_

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/cpumask.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>

#include "fm10k_pf.h"
#include "fm10k_vf.h"

#define FM10K_MAX_JUMBO_FRAME_SIZE	15342	/* Maximum supported size 15K */

#define MAX_QUEUES	FM10K_MAX_QUEUES_PF

#define FM10K_MIN_RXD		 128
#define FM10K_MAX_RXD		4096
#define FM10K_DEFAULT_RXD	 256

#define FM10K_MIN_TXD		 128
#define FM10K_MAX_TXD		4096
#define FM10K_DEFAULT_TXD	 256
#define FM10K_DEFAULT_TX_WORK	 256

#define FM10K_RXBUFFER_256	  256
#define FM10K_RX_HDR_LEN	FM10K_RXBUFFER_256
#define FM10K_RXBUFFER_2048	 2048
#define FM10K_RX_BUFSZ		FM10K_RXBUFFER_2048

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define FM10K_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define FM10K_MAX_STATIONS	63
struct fm10k_l2_accel {
	int size;
	u16 count;
	u16 dglort;
	struct rcu_head rcu;
	struct net_device *macvlan[0];
};

enum fm10k_ring_state_t {
	__FM10K_TX_DETECT_HANG,
	__FM10K_HANG_CHECK_ARMED,
	__FM10K_TX_XPS_INIT_DONE,
	/* This must be last and is used to calculate BITMAP size */
	__FM10K_TX_STATE_SIZE__,
};

#define check_for_tx_hang(ring) \
	test_bit(__FM10K_TX_DETECT_HANG, (ring)->state)
#define set_check_for_tx_hang(ring) \
	set_bit(__FM10K_TX_DETECT_HANG, (ring)->state)
#define clear_check_for_tx_hang(ring) \
	clear_bit(__FM10K_TX_DETECT_HANG, (ring)->state)

struct fm10k_tx_buffer {
	struct fm10k_tx_desc *next_to_watch;
	struct sk_buff *skb;
	unsigned int bytecount;
	u16 gso_segs;
	u16 tx_flags;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
};

struct fm10k_rx_buffer {
	dma_addr_t dma;
	struct page *page;
	u32 page_offset;
};

struct fm10k_queue_stats {
	u64 packets;
	u64 bytes;
};

struct fm10k_tx_queue_stats {
	u64 restart_queue;
	u64 csum_err;
	u64 tx_busy;
	u64 tx_done_old;
	u64 csum_good;
};

struct fm10k_rx_queue_stats {
	u64 alloc_failed;
	u64 csum_err;
	u64 errors;
	u64 csum_good;
	u64 switch_errors;
	u64 drops;
	u64 pp_errors;
	u64 link_errors;
	u64 length_errors;
};

struct fm10k_ring {
	struct fm10k_q_vector *q_vector;/* backpointer to host q_vector */
	struct net_device *netdev;	/* netdev ring belongs to */
	struct device *dev;		/* device for DMA mapping */
	struct fm10k_l2_accel __rcu *l2_accel;	/* L2 acceleration list */
	void *desc;			/* descriptor ring memory */
	union {
		struct fm10k_tx_buffer *tx_buffer;
		struct fm10k_rx_buffer *rx_buffer;
	};
	u32 __iomem *tail;
	DECLARE_BITMAP(state, __FM10K_TX_STATE_SIZE__);
	dma_addr_t dma;			/* phys. address of descriptor ring */
	unsigned int size;		/* length in bytes */

	u8 queue_index;			/* needed for queue management */
	u8 reg_idx;			/* holds the special value that gets
					 * the hardware register offset
					 * associated with this ring, which is
					 * different for DCB and RSS modes
					 */
	u8 qos_pc;			/* priority class of queue */
	u16 vid;			/* default VLAN ID of queue */
	u16 count;			/* amount of descriptors */

	u16 next_to_alloc;
	u16 next_to_use;
	u16 next_to_clean;

	struct fm10k_queue_stats stats;
	struct u64_stats_sync syncp;
	union {
		/* Tx */
		struct fm10k_tx_queue_stats tx_stats;
		/* Rx */
		struct {
			struct fm10k_rx_queue_stats rx_stats;
			struct sk_buff *skb;
		};
	};
} ____cacheline_internodealigned_in_smp;

struct fm10k_ring_container {
	struct fm10k_ring *ring;	/* pointer to linked list of rings */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u16 work_limit;			/* total work allowed per interrupt */
	u16 itr;			/* interrupt throttle rate value */
	u8 itr_scale;			/* ITR adjustment based on PCI speed */
	u8 count;			/* total number of rings in vector */
};

#define FM10K_ITR_MAX		0x0FFF	/* maximum value for ITR */
#define FM10K_ITR_10K		100	/* 100us */
#define FM10K_ITR_20K		50	/* 50us */
#define FM10K_ITR_40K		25	/* 25us */
#define FM10K_ITR_ADAPTIVE	0x8000	/* adaptive interrupt moderation flag */

#define ITR_IS_ADAPTIVE(itr) (!!(itr & FM10K_ITR_ADAPTIVE))

#define FM10K_TX_ITR_DEFAULT	FM10K_ITR_40K
#define FM10K_RX_ITR_DEFAULT	FM10K_ITR_20K
#define FM10K_ITR_ENABLE	(FM10K_ITR_AUTOMASK | FM10K_ITR_MASK_CLEAR)

static inline struct netdev_queue *txring_txq(const struct fm10k_ring *ring)
{
	return &ring->netdev->_tx[ring->queue_index];
}

/* iterator for handling rings in ring container */
#define fm10k_for_each_ring(pos, head) \
	for (pos = &(head).ring[(head).count]; (--pos) >= (head).ring;)

#define MAX_Q_VECTORS 256
#define MIN_Q_VECTORS	1
enum fm10k_non_q_vectors {
	FM10K_MBX_VECTOR,
#define NON_Q_VECTORS_VF NON_Q_VECTORS_PF
	NON_Q_VECTORS_PF
};

#define NON_Q_VECTORS(hw)	(((hw)->mac.type == fm10k_mac_pf) ? \
						NON_Q_VECTORS_PF : \
						NON_Q_VECTORS_VF)
#define MIN_MSIX_COUNT(hw)	(MIN_Q_VECTORS + NON_Q_VECTORS(hw))

struct fm10k_q_vector {
	struct fm10k_intfc *interface;
	u32 __iomem *itr;	/* pointer to ITR register for this vector */
	u16 v_idx;		/* index of q_vector within interface array */
	struct fm10k_ring_container rx, tx;

	struct napi_struct napi;
	cpumask_t affinity_mask;
	char name[IFNAMSIZ + 9];

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_q_vector;
#endif /* CONFIG_DEBUG_FS */
	struct rcu_head rcu;	/* to avoid race with update stats on free */

	/* for dynamic allocation of rings associated with this q_vector */
	struct fm10k_ring ring[0] ____cacheline_internodealigned_in_smp;
};

enum fm10k_ring_f_enum {
	RING_F_RSS,
	RING_F_QOS,
	RING_F_ARRAY_SIZE  /* must be last in enum set */
};

struct fm10k_ring_feature {
	u16 limit;	/* upper limit on feature indices */
	u16 indices;	/* current value of indices */
	u16 mask;	/* Mask used for feature to ring mapping */
	u16 offset;	/* offset to start of feature */
};

struct fm10k_iov_data {
	unsigned int		num_vfs;
	unsigned int		next_vf_mbx;
	struct rcu_head		rcu;
	struct fm10k_vf_info	vf_info[0];
};

struct fm10k_udp_port {
	struct list_head	list;
	sa_family_t		sa_family;
	__be16			port;
};

/* one work queue for entire driver */
extern struct workqueue_struct *fm10k_workqueue;

/* The following enumeration contains flags which indicate or enable modified
 * driver behaviors. To avoid race conditions, the flags are stored in
 * a BITMAP in the fm10k_intfc structure. The BITMAP should be accessed using
 * atomic *_bit() operations.
 */
enum fm10k_flags_t {
	FM10K_FLAG_RESET_REQUESTED,
	FM10K_FLAG_RSS_FIELD_IPV4_UDP,
	FM10K_FLAG_RSS_FIELD_IPV6_UDP,
	FM10K_FLAG_SWPRI_CONFIG,
	/* __FM10K_FLAGS_SIZE__ is used to calculate the size of
	 * interface->flags and must be the last value in this
	 * enumeration.
	 */
	__FM10K_FLAGS_SIZE__
};

enum fm10k_state_t {
	__FM10K_RESETTING,
	__FM10K_RESET_DETACHED,
	__FM10K_RESET_SUSPENDED,
	__FM10K_DOWN,
	__FM10K_SERVICE_SCHED,
	__FM10K_SERVICE_REQUEST,
	__FM10K_SERVICE_DISABLE,
	__FM10K_LINK_DOWN,
	__FM10K_UPDATING_STATS,
	/* This value must be last and determines the BITMAP size */
	__FM10K_STATE_SIZE__,
};

struct fm10k_intfc {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct net_device *netdev;
	struct fm10k_l2_accel *l2_accel; /* pointer to L2 acceleration list */
	struct pci_dev *pdev;
	DECLARE_BITMAP(state, __FM10K_STATE_SIZE__);

	/* Access flag values using atomic *_bit() operations */
	DECLARE_BITMAP(flags, __FM10K_FLAGS_SIZE__);

	int xcast_mode;

	/* Tx fast path data */
	int num_tx_queues;
	u16 tx_itr;

	/* Rx fast path data */
	int num_rx_queues;
	u16 rx_itr;

	/* TX */
	struct fm10k_ring *tx_ring[MAX_QUEUES] ____cacheline_aligned_in_smp;

	u64 restart_queue;
	u64 tx_busy;
	u64 tx_csum_errors;
	u64 alloc_failed;
	u64 rx_csum_errors;

	u64 tx_bytes_nic;
	u64 tx_packets_nic;
	u64 rx_bytes_nic;
	u64 rx_packets_nic;
	u64 rx_drops_nic;
	u64 rx_overrun_pf;
	u64 rx_overrun_vf;

	/* Debug Statistics */
	u64 hw_sm_mbx_full;
	u64 hw_csum_tx_good;
	u64 hw_csum_rx_good;
	u64 rx_switch_errors;
	u64 rx_drops;
	u64 rx_pp_errors;
	u64 rx_link_errors;
	u64 rx_length_errors;

	u32 tx_timeout_count;

	/* RX */
	struct fm10k_ring *rx_ring[MAX_QUEUES];

	/* Queueing vectors */
	struct fm10k_q_vector *q_vector[MAX_Q_VECTORS];
	struct msix_entry *msix_entries;
	int num_q_vectors;	/* current number of q_vectors for device */
	struct fm10k_ring_feature ring_feature[RING_F_ARRAY_SIZE];

	/* SR-IOV information management structure */
	struct fm10k_iov_data *iov_data;

	struct fm10k_hw_stats stats;
	struct fm10k_hw hw;
	/* Mailbox lock */
	spinlock_t mbx_lock;
	u32 __iomem *uc_addr;
	u32 __iomem *sw_addr;
	u16 msg_enable;
	u16 tx_ring_count;
	u16 rx_ring_count;
	struct timer_list service_timer;
	struct work_struct service_task;
	unsigned long next_stats_update;
	unsigned long next_tx_hang_check;
	unsigned long last_reset;
	unsigned long link_down_event;
	bool host_ready;
	bool lport_map_failed;

	u32 reta[FM10K_RETA_SIZE];
	u32 rssrk[FM10K_RSSRK_SIZE];

	/* UDP encapsulation port tracking information */
	struct list_head vxlan_port;
	struct list_head geneve_port;

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_intfc;
#endif /* CONFIG_DEBUG_FS */

#ifdef CONFIG_DCB
	u8 pfc_en;
#endif
	u8 rx_pause;

	/* GLORT resources in use by PF */
	u16 glort;
	u16 glort_count;

	/* VLAN ID for updating multicast/unicast lists */
	u16 vid;
};

static inline void fm10k_mbx_lock(struct fm10k_intfc *interface)
{
	spin_lock(&interface->mbx_lock);
}

static inline void fm10k_mbx_unlock(struct fm10k_intfc *interface)
{
	spin_unlock(&interface->mbx_lock);
}

static inline int fm10k_mbx_trylock(struct fm10k_intfc *interface)
{
	return spin_trylock(&interface->mbx_lock);
}

/* fm10k_test_staterr - test bits in Rx descriptor status and error fields */
static inline __le32 fm10k_test_staterr(union fm10k_rx_desc *rx_desc,
					const u32 stat_err_bits)
{
	return rx_desc->d.staterr & cpu_to_le32(stat_err_bits);
}

/* fm10k_desc_unused - calculate if we have unused descriptors */
static inline u16 fm10k_desc_unused(struct fm10k_ring *ring)
{
	s16 unused = ring->next_to_clean - ring->next_to_use - 1;

	return likely(unused < 0) ? unused + ring->count : unused;
}

#define FM10K_TX_DESC(R, i)	\
	(&(((struct fm10k_tx_desc *)((R)->desc))[i]))
#define FM10K_RX_DESC(R, i)	\
	 (&(((union fm10k_rx_desc *)((R)->desc))[i]))

#define FM10K_MAX_TXD_PWR	14
#define FM10K_MAX_DATA_PER_TXD	(1u << FM10K_MAX_TXD_PWR)

/* Tx Descriptors needed, worst case */
#define TXD_USE_COUNT(S)	DIV_ROUND_UP((S), FM10K_MAX_DATA_PER_TXD)
#define DESC_NEEDED	(MAX_SKB_FRAGS + 4)

enum fm10k_tx_flags {
	/* Tx offload flags */
	FM10K_TX_FLAGS_CSUM	= 0x01,
};

/* This structure is stored as little endian values as that is the native
 * format of the Rx descriptor.  The ordering of these fields is reversed
 * from the actual ftag header to allow for a single bswap to take care
 * of placing all of the values in network order
 */
union fm10k_ftag_info {
	__le64 ftag;
	struct {
		/* dglort and sglort combined into a single 32bit desc read */
		__le32 glort;
		/* upper 16 bits of VLAN are reserved 0 for swpri_type_user */
		__le32 vlan;
	} d;
	struct {
		__le16 dglort;
		__le16 sglort;
		__le16 vlan;
		__le16 swpri_type_user;
	} w;
};

struct fm10k_cb {
	union {
		__le64 tstamp;
		unsigned long ts_tx_timeout;
	};
	union fm10k_ftag_info fi;
};

#define FM10K_CB(skb) ((struct fm10k_cb *)(skb)->cb)

/* main */
extern char fm10k_driver_name[];
extern const char fm10k_driver_version[];
int fm10k_init_queueing_scheme(struct fm10k_intfc *interface);
void fm10k_clear_queueing_scheme(struct fm10k_intfc *interface);
__be16 fm10k_tx_encap_offload(struct sk_buff *skb);
netdev_tx_t fm10k_xmit_frame_ring(struct sk_buff *skb,
				  struct fm10k_ring *tx_ring);
void fm10k_tx_timeout_reset(struct fm10k_intfc *interface);
u64 fm10k_get_tx_pending(struct fm10k_ring *ring, bool in_sw);
bool fm10k_check_tx_hang(struct fm10k_ring *tx_ring);
void fm10k_alloc_rx_buffers(struct fm10k_ring *rx_ring, u16 cleaned_count);

/* PCI */
void fm10k_mbx_free_irq(struct fm10k_intfc *);
int fm10k_mbx_request_irq(struct fm10k_intfc *);
void fm10k_qv_free_irq(struct fm10k_intfc *interface);
int fm10k_qv_request_irq(struct fm10k_intfc *interface);
int fm10k_register_pci_driver(void);
void fm10k_unregister_pci_driver(void);
void fm10k_up(struct fm10k_intfc *interface);
void fm10k_down(struct fm10k_intfc *interface);
void fm10k_update_stats(struct fm10k_intfc *interface);
void fm10k_service_event_schedule(struct fm10k_intfc *interface);
void fm10k_update_rx_drop_en(struct fm10k_intfc *interface);
#ifdef CONFIG_NET_POLL_CONTROLLER
void fm10k_netpoll(struct net_device *netdev);
#endif

/* Netdev */
struct net_device *fm10k_alloc_netdev(const struct fm10k_info *info);
int fm10k_setup_rx_resources(struct fm10k_ring *);
int fm10k_setup_tx_resources(struct fm10k_ring *);
void fm10k_free_rx_resources(struct fm10k_ring *);
void fm10k_free_tx_resources(struct fm10k_ring *);
void fm10k_clean_all_rx_rings(struct fm10k_intfc *);
void fm10k_clean_all_tx_rings(struct fm10k_intfc *);
void fm10k_unmap_and_free_tx_resource(struct fm10k_ring *,
				      struct fm10k_tx_buffer *);
void fm10k_restore_rx_state(struct fm10k_intfc *);
void fm10k_reset_rx_state(struct fm10k_intfc *);
int fm10k_setup_tc(struct net_device *dev, u8 tc);
int fm10k_open(struct net_device *netdev);
int fm10k_close(struct net_device *netdev);

/* Ethtool */
void fm10k_set_ethtool_ops(struct net_device *dev);
void fm10k_write_reta(struct fm10k_intfc *interface, const u32 *indir);

/* IOV */
s32 fm10k_iov_event(struct fm10k_intfc *interface);
s32 fm10k_iov_mbx(struct fm10k_intfc *interface);
void fm10k_iov_suspend(struct pci_dev *pdev);
int fm10k_iov_resume(struct pci_dev *pdev);
void fm10k_iov_disable(struct pci_dev *pdev);
int fm10k_iov_configure(struct pci_dev *pdev, int num_vfs);
s32 fm10k_iov_update_pvid(struct fm10k_intfc *interface, u16 glort, u16 pvid);
int fm10k_ndo_set_vf_mac(struct net_device *netdev, int vf_idx, u8 *mac);
int fm10k_ndo_set_vf_vlan(struct net_device *netdev,
			  int vf_idx, u16 vid, u8 qos, __be16 vlan_proto);
int fm10k_ndo_set_vf_bw(struct net_device *netdev, int vf_idx, int rate,
			int unused);
int fm10k_ndo_get_vf_config(struct net_device *netdev,
			    int vf_idx, struct ifla_vf_info *ivi);

/* DebugFS */
#ifdef CONFIG_DEBUG_FS
void fm10k_dbg_q_vector_init(struct fm10k_q_vector *q_vector);
void fm10k_dbg_q_vector_exit(struct fm10k_q_vector *q_vector);
void fm10k_dbg_intfc_init(struct fm10k_intfc *interface);
void fm10k_dbg_intfc_exit(struct fm10k_intfc *interface);
void fm10k_dbg_init(void);
void fm10k_dbg_exit(void);
#else
static inline void fm10k_dbg_q_vector_init(struct fm10k_q_vector *q_vector) {}
static inline void fm10k_dbg_q_vector_exit(struct fm10k_q_vector *q_vector) {}
static inline void fm10k_dbg_intfc_init(struct fm10k_intfc *interface) {}
static inline void fm10k_dbg_intfc_exit(struct fm10k_intfc *interface) {}
static inline void fm10k_dbg_init(void) {}
static inline void fm10k_dbg_exit(void) {}
#endif /* CONFIG_DEBUG_FS */

/* DCB */
#ifdef CONFIG_DCB
void fm10k_dcbnl_set_ops(struct net_device *dev);
#else
static inline void fm10k_dcbnl_set_ops(struct net_device *dev) {}
#endif
#endif /* _FM10K_H_ */

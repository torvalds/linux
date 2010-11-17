/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2010 Intel Corporation.

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

/* common prefix used by pr_<> macros */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

/* TX/RX descriptor defines */
#define IXGBE_DEFAULT_TXD		    512
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
#define IXGBE_RXBUFFER_512   512    /* Used for packet split */
#define IXGBE_RXBUFFER_2048  2048
#define IXGBE_RXBUFFER_4096  4096
#define IXGBE_RXBUFFER_8192  8192
#define IXGBE_MAX_RXBUFFER   16384  /* largest size for a single descriptor */

/*
 * NOTE: netdev_alloc_skb reserves up to 64 bytes, NET_IP_ALIGN mans we
 * reserve 2 more, and skb_shared_info adds an additional 384 bytes more,
 * this adds up to 512 bytes of extra data meaning the smallest allocation
 * we could have is 1K.
 * i.e. RXBUFFER_512 --> size-1024 slab
 */
#define IXGBE_RX_HDR_SIZE IXGBE_RXBUFFER_512

#define MAXIMUM_ETHERNET_VLAN_SIZE (ETH_FRAME_LEN + ETH_FCS_LEN + VLAN_HLEN)

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBE_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define IXGBE_TX_FLAGS_CSUM		(u32)(1)
#define IXGBE_TX_FLAGS_VLAN		(u32)(1 << 1)
#define IXGBE_TX_FLAGS_TSO		(u32)(1 << 2)
#define IXGBE_TX_FLAGS_IPV4		(u32)(1 << 3)
#define IXGBE_TX_FLAGS_FCOE		(u32)(1 << 4)
#define IXGBE_TX_FLAGS_FSO		(u32)(1 << 5)
#define IXGBE_TX_FLAGS_VLAN_MASK	0xffff0000
#define IXGBE_TX_FLAGS_VLAN_PRIO_MASK   0x0000e000
#define IXGBE_TX_FLAGS_VLAN_SHIFT	16

#define IXGBE_MAX_RSC_INT_RATE          162760

#define IXGBE_MAX_VF_MC_ENTRIES         30
#define IXGBE_MAX_VF_FUNCTIONS          64
#define IXGBE_MAX_VFTA_ENTRIES          128
#define MAX_EMULATION_MAC_ADDRS         16
#define VMDQ_P(p)   ((p) + adapter->num_vfs)

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
};

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct ixgbe_tx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	unsigned long time_stamp;
	u16 length;
	u16 next_to_watch;
	unsigned int bytecount;
	u16 gso_segs;
	u8 mapped_as_page;
};

struct ixgbe_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	struct page *page;
	dma_addr_t page_dma;
	unsigned int page_offset;
};

struct ixgbe_queue_stats {
	u64 packets;
	u64 bytes;
};

struct ixgbe_tx_queue_stats {
	u64 restart_queue;
	u64 tx_busy;
	u64 completed;
	u64 tx_done_old;
};

struct ixgbe_rx_queue_stats {
	u64 rsc_count;
	u64 rsc_flush;
	u64 non_eop_descs;
	u64 alloc_rx_page_failed;
	u64 alloc_rx_buff_failed;
};

enum ixbge_ring_state_t {
	__IXGBE_TX_FDIR_INIT_DONE,
	__IXGBE_TX_DETECT_HANG,
	__IXGBE_HANG_CHECK_ARMED,
	__IXGBE_RX_PS_ENABLED,
	__IXGBE_RX_RSC_ENABLED,
};

#define ring_is_ps_enabled(ring) \
	test_bit(__IXGBE_RX_PS_ENABLED, &(ring)->state)
#define set_ring_ps_enabled(ring) \
	set_bit(__IXGBE_RX_PS_ENABLED, &(ring)->state)
#define clear_ring_ps_enabled(ring) \
	clear_bit(__IXGBE_RX_PS_ENABLED, &(ring)->state)
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
	void *desc;			/* descriptor ring memory */
	struct device *dev;             /* device for DMA mapping */
	struct net_device *netdev;      /* netdev ring belongs to */
	union {
		struct ixgbe_tx_buffer *tx_buffer_info;
		struct ixgbe_rx_buffer *rx_buffer_info;
	};
	unsigned long state;
	u8 atr_sample_rate;
	u8 atr_count;
	u16 count;			/* amount of descriptors */
	u16 rx_buf_len;
	u16 next_to_use;
	u16 next_to_clean;

	u8 queue_index; /* needed for multiqueue queue management */
	u8 reg_idx;			/* holds the special value that gets
					 * the hardware register offset
					 * associated with this ring, which is
					 * different for DCB and RSS modes
					 */

	u16 work_limit;			/* max work per interrupt */

	u8 __iomem *tail;

	unsigned int total_bytes;
	unsigned int total_packets;

	struct ixgbe_queue_stats stats;
	struct u64_stats_sync syncp;
	union {
		struct ixgbe_tx_queue_stats tx_stats;
		struct ixgbe_rx_queue_stats rx_stats;
	};
	int numa_node;
	unsigned int size;		/* length in bytes */
	dma_addr_t dma;			/* phys. address of descriptor ring */
	struct rcu_head rcu;
	struct ixgbe_q_vector *q_vector; /* back-pointer to host q_vector */
} ____cacheline_internodealigned_in_smp;

enum ixgbe_ring_f_enum {
	RING_F_NONE = 0,
	RING_F_DCB,
	RING_F_VMDQ,  /* SR-IOV uses the same ring feature */
	RING_F_RSS,
	RING_F_FDIR,
#ifdef IXGBE_FCOE
	RING_F_FCOE,
#endif /* IXGBE_FCOE */

	RING_F_ARRAY_SIZE      /* must be last in enum set */
};

#define IXGBE_MAX_DCB_INDICES   8
#define IXGBE_MAX_RSS_INDICES  16
#define IXGBE_MAX_VMDQ_INDICES 64
#define IXGBE_MAX_FDIR_INDICES 64
#ifdef IXGBE_FCOE
#define IXGBE_MAX_FCOE_INDICES  8
#define MAX_RX_QUEUES (IXGBE_MAX_FDIR_INDICES + IXGBE_MAX_FCOE_INDICES)
#define MAX_TX_QUEUES (IXGBE_MAX_FDIR_INDICES + IXGBE_MAX_FCOE_INDICES)
#else
#define MAX_RX_QUEUES IXGBE_MAX_FDIR_INDICES
#define MAX_TX_QUEUES IXGBE_MAX_FDIR_INDICES
#endif /* IXGBE_FCOE */
struct ixgbe_ring_feature {
	int indices;
	int mask;
} ____cacheline_internodealigned_in_smp;


#define MAX_RX_PACKET_BUFFERS ((adapter->flags & IXGBE_FLAG_DCB_ENABLED) \
                              ? 8 : 1)
#define MAX_TX_PACKET_BUFFERS MAX_RX_PACKET_BUFFERS

/* MAX_MSIX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ixgbe_q_vector {
	struct ixgbe_adapter *adapter;
	unsigned int v_idx; /* index of q_vector within array, also used for
	                     * finding the bit in EICR and friends that
	                     * represents the vector for this ring */
#ifdef CONFIG_IXGBE_DCA
	int cpu;	    /* CPU for DCA */
#endif
	struct napi_struct napi;
	DECLARE_BITMAP(rxr_idx, MAX_RX_QUEUES); /* Rx ring indices */
	DECLARE_BITMAP(txr_idx, MAX_TX_QUEUES); /* Tx ring indices */
	u8 rxr_count;     /* Rx ring count assigned to this vector */
	u8 txr_count;     /* Tx ring count assigned to this vector */
	u8 tx_itr;
	u8 rx_itr;
	u32 eitr;
	cpumask_var_t affinity_mask;
	char name[IFNAMSIZ + 9];
};

/* Helper macros to switch between ints/sec and what the register uses.
 * And yes, it's the same math going both ways.  The lowest value
 * supported by all of the ixgbe hardware is 8.
 */
#define EITR_INTS_PER_SEC_TO_REG(_eitr) \
	((_eitr) ? (1000000000 / ((_eitr) * 256)) : 8)
#define EITR_REG_TO_INTS_PER_SEC EITR_INTS_PER_SEC_TO_REG

#define IXGBE_DESC_UNUSED(R) \
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

#define IXGBE_RX_DESC_ADV(R, i)	    \
	(&(((union ixgbe_adv_rx_desc *)((R)->desc))[i]))
#define IXGBE_TX_DESC_ADV(R, i)	    \
	(&(((union ixgbe_adv_tx_desc *)((R)->desc))[i]))
#define IXGBE_TX_CTXTDESC_ADV(R, i)	    \
	(&(((struct ixgbe_adv_tx_context_desc *)((R)->desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE        16128
#ifdef IXGBE_FCOE
/* Use 3K as the baby jumbo frame size for FCoE */
#define IXGBE_FCOE_JUMBO_FRAME_SIZE       3072
#endif /* IXGBE_FCOE */

#define OTHER_VECTOR 1
#define NON_Q_VECTORS (OTHER_VECTOR)

#define MAX_MSIX_VECTORS_82599 64
#define MAX_MSIX_Q_VECTORS_82599 64
#define MAX_MSIX_VECTORS_82598 18
#define MAX_MSIX_Q_VECTORS_82598 16

#define MAX_MSIX_Q_VECTORS MAX_MSIX_Q_VECTORS_82599
#define MAX_MSIX_COUNT MAX_MSIX_VECTORS_82599

#define MIN_MSIX_Q_VECTORS 2
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

/* board specific private data structure */
struct ixgbe_adapter {
	struct timer_list watchdog_timer;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u16 bd_number;
	struct work_struct reset_task;
	struct ixgbe_q_vector *q_vector[MAX_MSIX_Q_VECTORS];
	struct ixgbe_dcb_config dcb_cfg;
	struct ixgbe_dcb_config temp_dcb_cfg;
	u8 dcb_set_bitmap;
	enum ixgbe_fc_mode last_lfc_mode;

	/* Interrupt Throttle Rate */
	u32 rx_itr_setting;
	u32 tx_itr_setting;
	u16 eitr_low;
	u16 eitr_high;

	/* TX */
	struct ixgbe_ring *tx_ring[MAX_TX_QUEUES] ____cacheline_aligned_in_smp;
	int num_tx_queues;
	u32 tx_timeout_count;
	bool detect_tx_hung;

	u64 restart_queue;
	u64 lsc_int;

	/* RX */
	struct ixgbe_ring *rx_ring[MAX_RX_QUEUES] ____cacheline_aligned_in_smp;
	int num_rx_queues;
	int num_rx_pools;		/* == num_rx_queues in 82598 */
	int num_rx_queues_per_pool;	/* 1 if 82598, can be many if 82599 */
	u64 hw_csum_rx_error;
	u64 hw_rx_no_dma_resources;
	u64 non_eop_descs;
	int num_msix_vectors;
	int max_msix_q_vectors;         /* true count of q_vectors for device */
	struct ixgbe_ring_feature ring_feature[RING_F_ARRAY_SIZE];
	struct msix_entry *msix_entries;

	u32 alloc_rx_page_failed;
	u32 alloc_rx_buff_failed;

	/* Some features need tri-state capability,
	 * thus the additional *_CAPABLE flags.
	 */
	u32 flags;
#define IXGBE_FLAG_RX_CSUM_ENABLED              (u32)(1)
#define IXGBE_FLAG_MSI_CAPABLE                  (u32)(1 << 1)
#define IXGBE_FLAG_MSI_ENABLED                  (u32)(1 << 2)
#define IXGBE_FLAG_MSIX_CAPABLE                 (u32)(1 << 3)
#define IXGBE_FLAG_MSIX_ENABLED                 (u32)(1 << 4)
#define IXGBE_FLAG_RX_1BUF_CAPABLE              (u32)(1 << 6)
#define IXGBE_FLAG_RX_PS_CAPABLE                (u32)(1 << 7)
#define IXGBE_FLAG_RX_PS_ENABLED                (u32)(1 << 8)
#define IXGBE_FLAG_IN_NETPOLL                   (u32)(1 << 9)
#define IXGBE_FLAG_DCA_ENABLED                  (u32)(1 << 10)
#define IXGBE_FLAG_DCA_CAPABLE                  (u32)(1 << 11)
#define IXGBE_FLAG_IMIR_ENABLED                 (u32)(1 << 12)
#define IXGBE_FLAG_MQ_CAPABLE                   (u32)(1 << 13)
#define IXGBE_FLAG_DCB_ENABLED                  (u32)(1 << 14)
#define IXGBE_FLAG_RSS_ENABLED                  (u32)(1 << 16)
#define IXGBE_FLAG_RSS_CAPABLE                  (u32)(1 << 17)
#define IXGBE_FLAG_VMDQ_CAPABLE                 (u32)(1 << 18)
#define IXGBE_FLAG_VMDQ_ENABLED                 (u32)(1 << 19)
#define IXGBE_FLAG_FAN_FAIL_CAPABLE             (u32)(1 << 20)
#define IXGBE_FLAG_NEED_LINK_UPDATE             (u32)(1 << 22)
#define IXGBE_FLAG_IN_SFP_LINK_TASK             (u32)(1 << 23)
#define IXGBE_FLAG_IN_SFP_MOD_TASK              (u32)(1 << 24)
#define IXGBE_FLAG_FDIR_HASH_CAPABLE            (u32)(1 << 25)
#define IXGBE_FLAG_FDIR_PERFECT_CAPABLE         (u32)(1 << 26)
#define IXGBE_FLAG_FCOE_CAPABLE                 (u32)(1 << 27)
#define IXGBE_FLAG_FCOE_ENABLED                 (u32)(1 << 28)
#define IXGBE_FLAG_SRIOV_CAPABLE                (u32)(1 << 29)
#define IXGBE_FLAG_SRIOV_ENABLED                (u32)(1 << 30)

	u32 flags2;
#define IXGBE_FLAG2_RSC_CAPABLE                 (u32)(1)
#define IXGBE_FLAG2_RSC_ENABLED                 (u32)(1 << 1)
#define IXGBE_FLAG2_TEMP_SENSOR_CAPABLE         (u32)(1 << 2)
/* default to trying for four seconds */
#define IXGBE_TRY_LINK_TIMEOUT (4 * HZ)

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	u32 test_icr;
	struct ixgbe_ring test_tx_ring;
	struct ixgbe_ring test_rx_ring;

	/* structs defined in ixgbe_hw.h */
	struct ixgbe_hw hw;
	u16 msg_enable;
	struct ixgbe_hw_stats stats;

	/* Interrupt Throttle Rate */
	u32 rx_eitr_param;
	u32 tx_eitr_param;

	unsigned long state;
	u64 tx_busy;
	unsigned int tx_ring_count;
	unsigned int rx_ring_count;

	u32 link_speed;
	bool link_up;
	unsigned long link_check_timeout;

	struct work_struct watchdog_task;
	struct work_struct sfp_task;
	struct timer_list sfp_timer;
	struct work_struct multispeed_fiber_task;
	struct work_struct sfp_config_module_task;
	u32 fdir_pballoc;
	u32 atr_sample_rate;
	spinlock_t fdir_perfect_lock;
	struct work_struct fdir_reinit_task;
#ifdef IXGBE_FCOE
	struct ixgbe_fcoe fcoe;
#endif /* IXGBE_FCOE */
	u64 rsc_total_count;
	u64 rsc_total_flush;
	u32 wol;
	u16 eeprom_version;

	int node;
	struct work_struct check_overtemp_task;
	u32 interrupt_event;
	char lsc_int_name[IFNAMSIZ + 9];

	/* SR-IOV */
	DECLARE_BITMAP(active_vfs, IXGBE_MAX_VF_FUNCTIONS);
	unsigned int num_vfs;
	struct vf_data_storage *vfinfo;
};

enum ixbge_state_t {
	__IXGBE_TESTING,
	__IXGBE_RESETTING,
	__IXGBE_DOWN,
	__IXGBE_SFP_MODULE_NOT_FOUND
};

struct ixgbe_rsc_cb {
	dma_addr_t dma;
	u16 skb_cnt;
	bool delay_unmap;
};
#define IXGBE_RSC_CB(skb) ((struct ixgbe_rsc_cb *)(skb)->cb)

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
extern int ixgbe_copy_dcb_cfg(struct ixgbe_dcb_config *src_dcb_cfg,
                              struct ixgbe_dcb_config *dst_dcb_cfg,
                              int tc_max);
#endif

extern char ixgbe_driver_name[];
extern const char ixgbe_driver_version[];

extern int ixgbe_up(struct ixgbe_adapter *adapter);
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
extern void ixgbe_update_stats(struct ixgbe_adapter *adapter);
extern int ixgbe_init_interrupt_scheme(struct ixgbe_adapter *adapter);
extern void ixgbe_clear_interrupt_scheme(struct ixgbe_adapter *adapter);
extern netdev_tx_t ixgbe_xmit_frame_ring(struct sk_buff *,
					 struct ixgbe_adapter *,
					 struct ixgbe_ring *);
extern void ixgbe_unmap_and_free_tx_resource(struct ixgbe_ring *,
                                             struct ixgbe_tx_buffer *);
extern void ixgbe_alloc_rx_buffers(struct ixgbe_ring *, u16);
extern void ixgbe_write_eitr(struct ixgbe_q_vector *);
extern int ethtool_ioctl(struct ifreq *ifr);
extern u8 ixgbe_dcb_txq_to_tc(struct ixgbe_adapter *adapter, u8 index);
extern s32 ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw);
extern s32 ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, u32 pballoc);
extern s32 ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, u32 pballoc);
extern s32 ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
                                                 struct ixgbe_atr_input *input,
                                                 u8 queue);
extern s32 ixgbe_fdir_add_perfect_filter_82599(struct ixgbe_hw *hw,
                                      struct ixgbe_atr_input *input,
                                      struct ixgbe_atr_input_masks *input_masks,
                                      u16 soft_id, u8 queue);
extern s32 ixgbe_atr_set_vlan_id_82599(struct ixgbe_atr_input *input,
                                       u16 vlan_id);
extern s32 ixgbe_atr_set_src_ipv4_82599(struct ixgbe_atr_input *input,
                                        u32 src_addr);
extern s32 ixgbe_atr_set_dst_ipv4_82599(struct ixgbe_atr_input *input,
                                        u32 dst_addr);
extern s32 ixgbe_atr_set_src_port_82599(struct ixgbe_atr_input *input,
                                        u16 src_port);
extern s32 ixgbe_atr_set_dst_port_82599(struct ixgbe_atr_input *input,
                                        u16 dst_port);
extern s32 ixgbe_atr_set_flex_byte_82599(struct ixgbe_atr_input *input,
                                         u16 flex_byte);
extern s32 ixgbe_atr_set_l4type_82599(struct ixgbe_atr_input *input,
                                      u8 l4type);
extern void ixgbe_configure_rscctl(struct ixgbe_adapter *adapter,
                                   struct ixgbe_ring *ring);
extern void ixgbe_clear_rscctl(struct ixgbe_adapter *adapter,
                               struct ixgbe_ring *ring);
extern void ixgbe_set_rx_mode(struct net_device *netdev);
#ifdef IXGBE_FCOE
extern void ixgbe_configure_fcoe(struct ixgbe_adapter *adapter);
extern int ixgbe_fso(struct ixgbe_adapter *adapter,
                     struct ixgbe_ring *tx_ring, struct sk_buff *skb,
                     u32 tx_flags, u8 *hdr_len);
extern void ixgbe_cleanup_fcoe(struct ixgbe_adapter *adapter);
extern int ixgbe_fcoe_ddp(struct ixgbe_adapter *adapter,
                          union ixgbe_adv_rx_desc *rx_desc,
                          struct sk_buff *skb);
extern int ixgbe_fcoe_ddp_get(struct net_device *netdev, u16 xid,
                              struct scatterlist *sgl, unsigned int sgc);
extern int ixgbe_fcoe_ddp_put(struct net_device *netdev, u16 xid);
extern int ixgbe_fcoe_enable(struct net_device *netdev);
extern int ixgbe_fcoe_disable(struct net_device *netdev);
#ifdef CONFIG_IXGBE_DCB
extern u8 ixgbe_fcoe_getapp(struct ixgbe_adapter *adapter);
extern u8 ixgbe_fcoe_setapp(struct ixgbe_adapter *adapter, u8 up);
#endif /* CONFIG_IXGBE_DCB */
extern int ixgbe_fcoe_get_wwn(struct net_device *netdev, u64 *wwn, int type);
#endif /* IXGBE_FCOE */

#endif /* _IXGBE_H_ */

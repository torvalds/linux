/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2012 Intel Corporation.

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


/* Linux PRO/1000 Ethernet Driver main header file */

#ifndef _IGB_H_
#define _IGB_H_

#include "e1000_mac.h"
#include "e1000_82575.h"

#include <linux/clocksource.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/bitops.h>
#include <linux/if_vlan.h>

struct igb_adapter;

/* Interrupt defines */
#define IGB_START_ITR                    648 /* ~6000 ints/sec */
#define IGB_4K_ITR                       980
#define IGB_20K_ITR                      196
#define IGB_70K_ITR                       56

/* TX/RX descriptor defines */
#define IGB_DEFAULT_TXD                  256
#define IGB_DEFAULT_TX_WORK		 128
#define IGB_MIN_TXD                       80
#define IGB_MAX_TXD                     4096

#define IGB_DEFAULT_RXD                  256
#define IGB_MIN_RXD                       80
#define IGB_MAX_RXD                     4096

#define IGB_DEFAULT_ITR                    3 /* dynamic */
#define IGB_MAX_ITR_USECS              10000
#define IGB_MIN_ITR_USECS                 10
#define NON_Q_VECTORS                      1
#define MAX_Q_VECTORS                      8

/* Transmit and receive queues */
#define IGB_MAX_RX_QUEUES                  8
#define IGB_MAX_RX_QUEUES_82575            4
#define IGB_MAX_RX_QUEUES_I211             2
#define IGB_MAX_TX_QUEUES                  8
#define IGB_MAX_VF_MC_ENTRIES              30
#define IGB_MAX_VF_FUNCTIONS               8
#define IGB_MAX_VFTA_ENTRIES               128
#define IGB_82576_VF_DEV_ID                0x10CA
#define IGB_I350_VF_DEV_ID                 0x1520

/* NVM version defines */
#define IGB_MAJOR_MASK			0xF000
#define IGB_MINOR_MASK			0x0FF0
#define IGB_BUILD_MASK			0x000F
#define IGB_COMB_VER_MASK		0x00FF
#define IGB_MAJOR_SHIFT			12
#define IGB_MINOR_SHIFT			4
#define IGB_COMB_VER_SHFT		8
#define IGB_NVM_VER_INVALID		0xFFFF
#define IGB_ETRACK_SHIFT		16
#define NVM_ETRACK_WORD			0x0042
#define NVM_COMB_VER_OFF		0x0083
#define NVM_COMB_VER_PTR		0x003d

struct vf_data_storage {
	unsigned char vf_mac_addresses[ETH_ALEN];
	u16 vf_mc_hashes[IGB_MAX_VF_MC_ENTRIES];
	u16 num_vf_mc_hashes;
	u16 vlans_enabled;
	u32 flags;
	unsigned long last_nack;
	u16 pf_vlan; /* When set, guest VLAN config not allowed. */
	u16 pf_qos;
	u16 tx_rate;
};

#define IGB_VF_FLAG_CTS            0x00000001 /* VF is clear to send data */
#define IGB_VF_FLAG_UNI_PROMISC    0x00000002 /* VF has unicast promisc */
#define IGB_VF_FLAG_MULTI_PROMISC  0x00000004 /* VF has multicast promisc */
#define IGB_VF_FLAG_PF_SET_MAC     0x00000008 /* PF has set MAC address */

/* RX descriptor control thresholds.
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
#define IGB_RX_PTHRESH                     8
#define IGB_RX_HTHRESH                     8
#define IGB_TX_PTHRESH                     8
#define IGB_TX_HTHRESH                     1
#define IGB_RX_WTHRESH                     ((hw->mac.type == e1000_82576 && \
					     adapter->msix_entries) ? 1 : 4)
#define IGB_TX_WTHRESH                     ((hw->mac.type == e1000_82576 && \
					     adapter->msix_entries) ? 1 : 16)

/* this is the size past which hardware will drop packets when setting LPE=0 */
#define MAXIMUM_ETHERNET_VLAN_SIZE 1522

/* Supported Rx Buffer Sizes */
#define IGB_RXBUFFER_256	256
#define IGB_RXBUFFER_2048	2048
#define IGB_RX_HDR_LEN		IGB_RXBUFFER_256
#define IGB_RX_BUFSZ		IGB_RXBUFFER_2048

/* How many Tx Descriptors do we need to call netif_wake_queue ? */
#define IGB_TX_QUEUE_WAKE	16
/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IGB_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define AUTO_ALL_MODES            0
#define IGB_EEPROM_APME         0x0400

#ifndef IGB_MASTER_SLAVE
/* Switch to override PHY master/slave setting */
#define IGB_MASTER_SLAVE	e1000_ms_hw_default
#endif

#define IGB_MNG_VLAN_NONE -1

enum igb_tx_flags {
	/* cmd_type flags */
	IGB_TX_FLAGS_VLAN	= 0x01,
	IGB_TX_FLAGS_TSO	= 0x02,
	IGB_TX_FLAGS_TSTAMP	= 0x04,

	/* olinfo flags */
	IGB_TX_FLAGS_IPV4	= 0x10,
	IGB_TX_FLAGS_CSUM	= 0x20,
};

/* VLAN info */
#define IGB_TX_FLAGS_VLAN_MASK		0xffff0000
#define IGB_TX_FLAGS_VLAN_SHIFT	16

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct igb_tx_buffer {
	union e1000_adv_tx_desc *next_to_watch;
	unsigned long time_stamp;
	struct sk_buff *skb;
	unsigned int bytecount;
	u16 gso_segs;
	__be16 protocol;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	u32 tx_flags;
};

struct igb_rx_buffer {
	dma_addr_t dma;
	struct page *page;
	unsigned int page_offset;
};

struct igb_tx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 restart_queue;
	u64 restart_queue2;
};

struct igb_rx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 drops;
	u64 csum_err;
	u64 alloc_failed;
};

struct igb_ring_container {
	struct igb_ring *ring;		/* pointer to linked list of rings */
	unsigned int total_bytes;	/* total bytes processed this int */
	unsigned int total_packets;	/* total packets processed this int */
	u16 work_limit;			/* total work allowed per interrupt */
	u8 count;			/* total number of rings in vector */
	u8 itr;				/* current ITR setting for ring */
};

struct igb_ring {
	struct igb_q_vector *q_vector;	/* backlink to q_vector */
	struct net_device *netdev;	/* back pointer to net_device */
	struct device *dev;		/* device pointer for dma mapping */
	union {				/* array of buffer info structs */
		struct igb_tx_buffer *tx_buffer_info;
		struct igb_rx_buffer *rx_buffer_info;
	};
	void *desc;			/* descriptor ring memory */
	unsigned long flags;		/* ring specific flags */
	void __iomem *tail;		/* pointer to ring tail register */
	dma_addr_t dma;			/* phys address of the ring */
	unsigned int  size;		/* length of desc. ring in bytes */

	u16 count;			/* number of desc. in the ring */
	u8 queue_index;			/* logical index of the ring*/
	u8 reg_idx;			/* physical index of the ring */

	/* everything past this point are written often */
	u16 next_to_clean;
	u16 next_to_use;
	u16 next_to_alloc;

	union {
		/* TX */
		struct {
			struct igb_tx_queue_stats tx_stats;
			struct u64_stats_sync tx_syncp;
			struct u64_stats_sync tx_syncp2;
		};
		/* RX */
		struct {
			struct sk_buff *skb;
			struct igb_rx_queue_stats rx_stats;
			struct u64_stats_sync rx_syncp;
		};
	};
} ____cacheline_internodealigned_in_smp;

struct igb_q_vector {
	struct igb_adapter *adapter;	/* backlink */
	int cpu;			/* CPU for DCA */
	u32 eims_value;			/* EIMS mask value */

	u16 itr_val;
	u8 set_itr;
	void __iomem *itr_register;

	struct igb_ring_container rx, tx;

	struct napi_struct napi;
	struct rcu_head rcu;	/* to avoid race with update stats on free */
	char name[IFNAMSIZ + 9];

	/* for dynamic allocation of rings associated with this q_vector */
	struct igb_ring ring[0] ____cacheline_internodealigned_in_smp;
};

enum e1000_ring_flags_t {
	IGB_RING_FLAG_RX_SCTP_CSUM,
	IGB_RING_FLAG_RX_LB_VLAN_BSWAP,
	IGB_RING_FLAG_TX_CTX_IDX,
	IGB_RING_FLAG_TX_DETECT_HANG
};

#define IGB_TXD_DCMD (E1000_ADVTXD_DCMD_EOP | E1000_ADVTXD_DCMD_RS)

#define IGB_RX_DESC(R, i)	    \
	(&(((union e1000_adv_rx_desc *)((R)->desc))[i]))
#define IGB_TX_DESC(R, i)	    \
	(&(((union e1000_adv_tx_desc *)((R)->desc))[i]))
#define IGB_TX_CTXTDESC(R, i)	    \
	(&(((struct e1000_adv_tx_context_desc *)((R)->desc))[i]))

/* igb_test_staterr - tests bits within Rx descriptor status and error fields */
static inline __le32 igb_test_staterr(union e1000_adv_rx_desc *rx_desc,
				      const u32 stat_err_bits)
{
	return rx_desc->wb.upper.status_error & cpu_to_le32(stat_err_bits);
}

/* igb_desc_unused - calculate if we have unused descriptors */
static inline int igb_desc_unused(struct igb_ring *ring)
{
	if (ring->next_to_clean > ring->next_to_use)
		return ring->next_to_clean - ring->next_to_use - 1;

	return ring->count + ring->next_to_clean - ring->next_to_use - 1;
}

/* board specific private data structure */
struct igb_adapter {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	struct net_device *netdev;

	unsigned long state;
	unsigned int flags;

	unsigned int num_q_vectors;
	struct msix_entry *msix_entries;

	/* Interrupt Throttle Rate */
	u32 rx_itr_setting;
	u32 tx_itr_setting;
	u16 tx_itr;
	u16 rx_itr;

	/* TX */
	u16 tx_work_limit;
	u32 tx_timeout_count;
	int num_tx_queues;
	struct igb_ring *tx_ring[16];

	/* RX */
	int num_rx_queues;
	struct igb_ring *rx_ring[16];

	u32 max_frame_size;
	u32 min_frame_size;

	struct timer_list watchdog_timer;
	struct timer_list phy_info_timer;

	u16 mng_vlan_id;
	u32 bd_number;
	u32 wol;
	u32 en_mng_pt;
	u16 link_speed;
	u16 link_duplex;

	struct work_struct reset_task;
	struct work_struct watchdog_task;
	bool fc_autoneg;
	u8  tx_timeout_factor;
	struct timer_list blink_timer;
	unsigned long led_status;

	/* OS defined structs */
	struct pci_dev *pdev;

	spinlock_t stats64_lock;
	struct rtnl_link_stats64 stats64;

	/* structs defined in e1000_hw.h */
	struct e1000_hw hw;
	struct e1000_hw_stats stats;
	struct e1000_phy_info phy_info;
	struct e1000_phy_stats phy_stats;

	u32 test_icr;
	struct igb_ring test_tx_ring;
	struct igb_ring test_rx_ring;

	int msg_enable;

	struct igb_q_vector *q_vector[MAX_Q_VECTORS];
	u32 eims_enable_mask;
	u32 eims_other;

	/* to not mess up cache alignment, always add to the bottom */
	u32 eeprom_wol;

	u16 tx_ring_count;
	u16 rx_ring_count;
	unsigned int vfs_allocated_count;
	struct vf_data_storage *vf_data;
	int vf_rate_link_speed;
	u32 rss_queues;
	u32 wvbr;
	u32 *shadow_vfta;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_caps;
	struct delayed_work ptp_overflow_work;
	struct work_struct ptp_tx_work;
	struct sk_buff *ptp_tx_skb;
	spinlock_t tmreg_lock;
	struct cyclecounter cc;
	struct timecounter tc;

	char fw_version[32];
};

#define IGB_FLAG_HAS_MSI		(1 << 0)
#define IGB_FLAG_DCA_ENABLED		(1 << 1)
#define IGB_FLAG_QUAD_PORT_A		(1 << 2)
#define IGB_FLAG_QUEUE_PAIRS		(1 << 3)
#define IGB_FLAG_DMAC			(1 << 4)
#define IGB_FLAG_PTP			(1 << 5)
#define IGB_FLAG_RSS_FIELD_IPV4_UDP	(1 << 6)
#define IGB_FLAG_RSS_FIELD_IPV6_UDP	(1 << 7)

/* DMA Coalescing defines */
#define IGB_MIN_TXPBSIZE           20408
#define IGB_TX_BUF_4096            4096
#define IGB_DMCTLX_DCFLUSH_DIS     0x80000000  /* Disable DMA Coal Flush */

#define IGB_82576_TSYNC_SHIFT 19
#define IGB_TS_HDR_LEN        16
enum e1000_state_t {
	__IGB_TESTING,
	__IGB_RESETTING,
	__IGB_DOWN
};

enum igb_boards {
	board_82575,
};

extern char igb_driver_name[];
extern char igb_driver_version[];

extern int igb_up(struct igb_adapter *);
extern void igb_down(struct igb_adapter *);
extern void igb_reinit_locked(struct igb_adapter *);
extern void igb_reset(struct igb_adapter *);
extern int igb_set_spd_dplx(struct igb_adapter *, u32, u8);
extern int igb_setup_tx_resources(struct igb_ring *);
extern int igb_setup_rx_resources(struct igb_ring *);
extern void igb_free_tx_resources(struct igb_ring *);
extern void igb_free_rx_resources(struct igb_ring *);
extern void igb_configure_tx_ring(struct igb_adapter *, struct igb_ring *);
extern void igb_configure_rx_ring(struct igb_adapter *, struct igb_ring *);
extern void igb_setup_tctl(struct igb_adapter *);
extern void igb_setup_rctl(struct igb_adapter *);
extern netdev_tx_t igb_xmit_frame_ring(struct sk_buff *, struct igb_ring *);
extern void igb_unmap_and_free_tx_resource(struct igb_ring *,
					   struct igb_tx_buffer *);
extern void igb_alloc_rx_buffers(struct igb_ring *, u16);
extern void igb_update_stats(struct igb_adapter *, struct rtnl_link_stats64 *);
extern bool igb_has_link(struct igb_adapter *adapter);
extern void igb_set_ethtool_ops(struct net_device *);
extern void igb_power_up_link(struct igb_adapter *);
extern void igb_set_fw_version(struct igb_adapter *);
extern void igb_ptp_init(struct igb_adapter *adapter);
extern void igb_ptp_stop(struct igb_adapter *adapter);
extern void igb_ptp_reset(struct igb_adapter *adapter);
extern void igb_ptp_tx_work(struct work_struct *work);
extern void igb_ptp_tx_hwtstamp(struct igb_adapter *adapter);
extern void igb_ptp_rx_rgtstamp(struct igb_q_vector *q_vector,
				struct sk_buff *skb);
extern void igb_ptp_rx_pktstamp(struct igb_q_vector *q_vector,
				unsigned char *va,
				struct sk_buff *skb);
static inline void igb_ptp_rx_hwtstamp(struct igb_q_vector *q_vector,
				       union e1000_adv_rx_desc *rx_desc,
				       struct sk_buff *skb)
{
	if (igb_test_staterr(rx_desc, E1000_RXDADV_STAT_TS) &&
	    !igb_test_staterr(rx_desc, E1000_RXDADV_STAT_TSIP))
		igb_ptp_rx_rgtstamp(q_vector, skb);
}

extern int igb_ptp_hwtstamp_ioctl(struct net_device *netdev,
				  struct ifreq *ifr, int cmd);

static inline s32 igb_reset_phy(struct e1000_hw *hw)
{
	if (hw->phy.ops.reset)
		return hw->phy.ops.reset(hw);

	return 0;
}

static inline s32 igb_read_phy_reg(struct e1000_hw *hw, u32 offset, u16 *data)
{
	if (hw->phy.ops.read_reg)
		return hw->phy.ops.read_reg(hw, offset, data);

	return 0;
}

static inline s32 igb_write_phy_reg(struct e1000_hw *hw, u32 offset, u16 data)
{
	if (hw->phy.ops.write_reg)
		return hw->phy.ops.write_reg(hw, offset, data);

	return 0;
}

static inline s32 igb_get_phy_info(struct e1000_hw *hw)
{
	if (hw->phy.ops.get_phy_info)
		return hw->phy.ops.get_phy_info(hw);

	return 0;
}

static inline struct netdev_queue *txring_txq(const struct igb_ring *tx_ring)
{
	return netdev_get_tx_queue(tx_ring->netdev, tx_ring->queue_index);
}

#endif /* _IGB_H_ */

/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2009 Intel Corporation.

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
#include <linux/timecompare.h>
#include <linux/net_tstamp.h>

struct igb_adapter;

/* ((1000000000ns / (6000ints/s * 1024ns)) << 2 = 648 */
#define IGB_START_ITR 648

/* TX/RX descriptor defines */
#define IGB_DEFAULT_TXD                  256
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
#define IGB_MAX_RX_QUEUES                  (adapter->vfs_allocated_count ? 2 : \
                                           (hw->mac.type > e1000_82575 ? 8 : 4))
#define IGB_ABS_MAX_TX_QUEUES              8
#define IGB_MAX_TX_QUEUES                  IGB_MAX_RX_QUEUES

#define IGB_MAX_VF_MC_ENTRIES              30
#define IGB_MAX_VF_FUNCTIONS               8
#define IGB_MAX_VFTA_ENTRIES               128

struct vf_data_storage {
	unsigned char vf_mac_addresses[ETH_ALEN];
	u16 vf_mc_hashes[IGB_MAX_VF_MC_ENTRIES];
	u16 num_vf_mc_hashes;
	u16 vlans_enabled;
	u32 flags;
	unsigned long last_nack;
	u16 pf_vlan; /* When set, guest VLAN config not allowed. */
	u16 pf_qos;
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
#define IGB_RX_WTHRESH                     1
#define IGB_TX_PTHRESH                     8
#define IGB_TX_HTHRESH                     1
#define IGB_TX_WTHRESH                     ((hw->mac.type == e1000_82576 && \
                                             adapter->msix_entries) ? 1 : 16)

/* this is the size past which hardware will drop packets when setting LPE=0 */
#define MAXIMUM_ETHERNET_VLAN_SIZE 1522

/* Supported Rx Buffer Sizes */
#define IGB_RXBUFFER_64    64     /* Used for packet split */
#define IGB_RXBUFFER_128   128    /* Used for packet split */
#define IGB_RXBUFFER_1024  1024
#define IGB_RXBUFFER_2048  2048
#define IGB_RXBUFFER_16384 16384

#define MAX_STD_JUMBO_FRAME_SIZE 9234

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

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct igb_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	union {
		/* TX */
		struct {
			unsigned long time_stamp;
			u16 length;
			u16 next_to_watch;
			unsigned int bytecount;
			u16 gso_segs;
			u8 tx_flags;
			u8 mapped_as_page;
		};
		/* RX */
		struct {
			struct page *page;
			dma_addr_t page_dma;
			u16 page_offset;
		};
	};
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

struct igb_q_vector {
	struct igb_adapter *adapter; /* backlink */
	struct igb_ring *rx_ring;
	struct igb_ring *tx_ring;
	struct napi_struct napi;

	u32 eims_value;
	u16 cpu;

	u16 itr_val;
	u8 set_itr;
	void __iomem *itr_register;

	char name[IFNAMSIZ + 9];
};

struct igb_ring {
	struct igb_q_vector *q_vector; /* backlink to q_vector */
	struct net_device *netdev;     /* back pointer to net_device */
	struct device *dev;            /* device pointer for dma mapping */
	dma_addr_t dma;                /* phys address of the ring */
	void *desc;                    /* descriptor ring memory */
	unsigned int size;             /* length of desc. ring in bytes */
	u16 count;                     /* number of desc. in the ring */
	u16 next_to_use;
	u16 next_to_clean;
	u8 queue_index;
	u8 reg_idx;
	void __iomem *head;
	void __iomem *tail;
	struct igb_buffer *buffer_info; /* array of buffer info structs */

	unsigned int total_bytes;
	unsigned int total_packets;

	u32 flags;

	union {
		/* TX */
		struct {
			struct igb_tx_queue_stats tx_stats;
			struct u64_stats_sync tx_syncp;
			struct u64_stats_sync tx_syncp2;
			bool detect_tx_hung;
		};
		/* RX */
		struct {
			struct igb_rx_queue_stats rx_stats;
			struct u64_stats_sync rx_syncp;
			u32 rx_buffer_len;
		};
	};
};

#define IGB_RING_FLAG_RX_CSUM        0x00000001 /* RX CSUM enabled */
#define IGB_RING_FLAG_RX_SCTP_CSUM   0x00000002 /* SCTP CSUM offload enabled */

#define IGB_RING_FLAG_TX_CTX_IDX     0x00000001 /* HW requires context index */

#define IGB_ADVTXD_DCMD (E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS)

#define E1000_RX_DESC_ADV(R, i)	    \
	(&(((union e1000_adv_rx_desc *)((R).desc))[i]))
#define E1000_TX_DESC_ADV(R, i)	    \
	(&(((union e1000_adv_tx_desc *)((R).desc))[i]))
#define E1000_TX_CTXTDESC_ADV(R, i)	    \
	(&(((struct e1000_adv_tx_context_desc *)((R).desc))[i]))

/* igb_desc_unused - calculate if we have unused descriptors */
static inline int igb_desc_unused(struct igb_ring *ring)
{
	if (ring->next_to_clean > ring->next_to_use)
		return ring->next_to_clean - ring->next_to_use - 1;

	return ring->count + ring->next_to_clean - ring->next_to_use - 1;
}

/* board specific private data structure */
struct igb_adapter {
	struct timer_list watchdog_timer;
	struct timer_list phy_info_timer;
	struct vlan_group *vlgrp;
	u16 mng_vlan_id;
	u32 bd_number;
	u32 wol;
	u32 en_mng_pt;
	u16 link_speed;
	u16 link_duplex;

	/* Interrupt Throttle Rate */
	u32 rx_itr_setting;
	u32 tx_itr_setting;
	u16 tx_itr;
	u16 rx_itr;

	struct work_struct reset_task;
	struct work_struct watchdog_task;
	bool fc_autoneg;
	u8  tx_timeout_factor;
	struct timer_list blink_timer;
	unsigned long led_status;

	/* TX */
	struct igb_ring *tx_ring[16];
	u32 tx_timeout_count;

	/* RX */
	struct igb_ring *rx_ring[16];
	int num_tx_queues;
	int num_rx_queues;

	u32 max_frame_size;
	u32 min_frame_size;

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct cyclecounter cycles;
	struct timecounter clock;
	struct timecompare compare;
	struct hwtstamp_config hwtstamp_config;

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

	unsigned int num_q_vectors;
	struct igb_q_vector *q_vector[MAX_Q_VECTORS];
	struct msix_entry *msix_entries;
	u32 eims_enable_mask;
	u32 eims_other;

	/* to not mess up cache alignment, always add to the bottom */
	unsigned long state;
	unsigned int flags;
	u32 eeprom_wol;

	struct igb_ring *multi_tx_table[IGB_ABS_MAX_TX_QUEUES];
	u16 tx_ring_count;
	u16 rx_ring_count;
	unsigned int vfs_allocated_count;
	struct vf_data_storage *vf_data;
	u32 rss_queues;
};

#define IGB_FLAG_HAS_MSI           (1 << 0)
#define IGB_FLAG_DCA_ENABLED       (1 << 1)
#define IGB_FLAG_QUAD_PORT_A       (1 << 2)
#define IGB_FLAG_QUEUE_PAIRS       (1 << 3)

#define IGB_82576_TSYNC_SHIFT 19
#define IGB_82580_TSYNC_SHIFT 24
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
extern int igb_set_spd_dplx(struct igb_adapter *, u16);
extern int igb_setup_tx_resources(struct igb_ring *);
extern int igb_setup_rx_resources(struct igb_ring *);
extern void igb_free_tx_resources(struct igb_ring *);
extern void igb_free_rx_resources(struct igb_ring *);
extern void igb_configure_tx_ring(struct igb_adapter *, struct igb_ring *);
extern void igb_configure_rx_ring(struct igb_adapter *, struct igb_ring *);
extern void igb_setup_tctl(struct igb_adapter *);
extern void igb_setup_rctl(struct igb_adapter *);
extern netdev_tx_t igb_xmit_frame_ring_adv(struct sk_buff *, struct igb_ring *);
extern void igb_unmap_and_free_tx_resource(struct igb_ring *,
					   struct igb_buffer *);
extern void igb_alloc_rx_buffers_adv(struct igb_ring *, int);
extern void igb_update_stats(struct igb_adapter *, struct rtnl_link_stats64 *);
extern bool igb_has_link(struct igb_adapter *adapter);
extern void igb_set_ethtool_ops(struct net_device *);
extern void igb_power_up_link(struct igb_adapter *);

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

#endif /* _IGB_H_ */

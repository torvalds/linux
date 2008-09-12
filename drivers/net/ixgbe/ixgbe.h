/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2007 Intel Corporation.

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
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _IXGBE_H_
#define _IXGBE_H_

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/inet_lro.h>

#include "ixgbe_type.h"
#include "ixgbe_common.h"

#if defined(CONFIG_DCA) || defined(CONFIG_DCA_MODULE)
#include <linux/dca.h>
#endif

#define PFX "ixgbe: "
#define DPRINTK(nlevel, klevel, fmt, args...) \
	((void)((NETIF_MSG_##nlevel & adapter->msg_enable) && \
	printk(KERN_##klevel PFX "%s: %s: " fmt, adapter->netdev->name, \
		__FUNCTION__ , ## args)))

/* TX/RX descriptor defines */
#define IXGBE_DEFAULT_TXD		   1024
#define IXGBE_MAX_TXD			   4096
#define IXGBE_MIN_TXD			     64

#define IXGBE_DEFAULT_RXD		   1024
#define IXGBE_MAX_RXD			   4096
#define IXGBE_MIN_RXD			     64

/* flow control */
#define IXGBE_DEFAULT_FCRTL		0x10000
#define IXGBE_MIN_FCRTL			   0x40
#define IXGBE_MAX_FCRTL			0x7FF80
#define IXGBE_DEFAULT_FCRTH		0x20000
#define IXGBE_MIN_FCRTH			  0x600
#define IXGBE_MAX_FCRTH			0x7FFF0
#define IXGBE_DEFAULT_FCPAUSE		 0xFFFF
#define IXGBE_MIN_FCPAUSE		      0
#define IXGBE_MAX_FCPAUSE		 0xFFFF

/* Supported Rx Buffer Sizes */
#define IXGBE_RXBUFFER_64    64     /* Used for packet split */
#define IXGBE_RXBUFFER_128   128    /* Used for packet split */
#define IXGBE_RXBUFFER_256   256    /* Used for packet split */
#define IXGBE_RXBUFFER_2048  2048

#define IXGBE_RX_HDR_SIZE IXGBE_RXBUFFER_256

#define MAXIMUM_ETHERNET_VLAN_SIZE (ETH_FRAME_LEN + ETH_FCS_LEN + VLAN_HLEN)

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBE_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define IXGBE_TX_FLAGS_CSUM		(u32)(1)
#define IXGBE_TX_FLAGS_VLAN		(u32)(1 << 1)
#define IXGBE_TX_FLAGS_TSO		(u32)(1 << 2)
#define IXGBE_TX_FLAGS_IPV4		(u32)(1 << 3)
#define IXGBE_TX_FLAGS_VLAN_MASK	0xffff0000
#define IXGBE_TX_FLAGS_VLAN_SHIFT	16

#define IXGBE_MAX_LRO_DESCRIPTORS       8
#define IXGBE_MAX_LRO_AGGREGATE         32

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct ixgbe_tx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	unsigned long time_stamp;
	u16 length;
	u16 next_to_watch;
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

struct ixgbe_ring {
	void *desc;			/* descriptor ring memory */
	dma_addr_t dma;			/* phys. address of descriptor ring */
	unsigned int size;		/* length in bytes */
	unsigned int count;		/* amount of descriptors */
	unsigned int next_to_use;
	unsigned int next_to_clean;

	int queue_index; /* needed for multiqueue queue management */
	union {
		struct ixgbe_tx_buffer *tx_buffer_info;
		struct ixgbe_rx_buffer *rx_buffer_info;
	};

	u16 head;
	u16 tail;

	unsigned int total_bytes;
	unsigned int total_packets;

	u16 reg_idx; /* holds the special value that gets the hardware register
		      * offset associated with this ring, which is different
		      * for DCE and RSS modes */

#if defined(CONFIG_DCA) || defined(CONFIG_DCA_MODULE)
	/* cpu for tx queue */
	int cpu;
#endif
	struct net_lro_mgr lro_mgr;
	bool lro_used;
	struct ixgbe_queue_stats stats;
	u16 v_idx; /* maps directly to the index for this ring in the hardware
	           * vector array, can also be used for finding the bit in EICR
	           * and friends that represents the vector for this ring */


	u16 work_limit;                /* max work per interrupt */
	u16 rx_buf_len;
};

#define RING_F_VMDQ 1
#define RING_F_RSS  2
#define IXGBE_MAX_RSS_INDICES  16
#define IXGBE_MAX_VMDQ_INDICES 16
struct ixgbe_ring_feature {
	int indices;
	int mask;
};

#define MAX_RX_QUEUES 64
#define MAX_TX_QUEUES 32

/* MAX_MSIX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ixgbe_q_vector {
	struct ixgbe_adapter *adapter;
	struct napi_struct napi;
	DECLARE_BITMAP(rxr_idx, MAX_RX_QUEUES); /* Rx ring indices */
	DECLARE_BITMAP(txr_idx, MAX_TX_QUEUES); /* Tx ring indices */
	u8 rxr_count;     /* Rx ring count assigned to this vector */
	u8 txr_count;     /* Tx ring count assigned to this vector */
	u8 tx_itr;
	u8 rx_itr;
	u32 eitr;
};

/* Helper macros to switch between ints/sec and what the register uses.
 * And yes, it's the same math going both ways.
 */
#define EITR_INTS_PER_SEC_TO_REG(_eitr) \
	((_eitr) ? (1000000000 / ((_eitr) * 256)) : 0)
#define EITR_REG_TO_INTS_PER_SEC EITR_INTS_PER_SEC_TO_REG

#define IXGBE_DESC_UNUSED(R) \
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

#define IXGBE_RX_DESC_ADV(R, i)	    \
	(&(((union ixgbe_adv_rx_desc *)((R).desc))[i]))
#define IXGBE_TX_DESC_ADV(R, i)	    \
	(&(((union ixgbe_adv_tx_desc *)((R).desc))[i]))
#define IXGBE_TX_CTXTDESC_ADV(R, i)	    \
	(&(((struct ixgbe_adv_tx_context_desc *)((R).desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE        16128

#define OTHER_VECTOR 1
#define NON_Q_VECTORS (OTHER_VECTOR)

#define MAX_MSIX_Q_VECTORS 16
#define MIN_MSIX_Q_VECTORS 2
#define MAX_MSIX_COUNT (MAX_MSIX_Q_VECTORS + NON_Q_VECTORS)
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

/* board specific private data structure */
struct ixgbe_adapter {
	struct timer_list watchdog_timer;
	struct vlan_group *vlgrp;
	u16 bd_number;
	struct work_struct reset_task;
	struct ixgbe_q_vector q_vector[MAX_MSIX_Q_VECTORS];
	char name[MAX_MSIX_COUNT][IFNAMSIZ + 5];

	/* Interrupt Throttle Rate */
	u32 itr_setting;
	u16 eitr_low;
	u16 eitr_high;

	/* TX */
	struct ixgbe_ring *tx_ring;	/* One per active queue */
	int num_tx_queues;
	u64 restart_queue;
	u64 hw_csum_tx_good;
	u64 lsc_int;
	u64 hw_tso_ctxt;
	u64 hw_tso6_ctxt;
	u32 tx_timeout_count;
	bool detect_tx_hung;

	/* RX */
	struct ixgbe_ring *rx_ring;	/* One per active queue */
	int num_rx_queues;
	u64 hw_csum_rx_error;
	u64 hw_csum_rx_good;
	u64 non_eop_descs;
	int num_msix_vectors;
	struct ixgbe_ring_feature ring_feature[3];
	struct msix_entry *msix_entries;

	u64 rx_hdr_split;
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
#define IXGBE_FLAG_RSS_ENABLED                  (u32)(1 << 16)
#define IXGBE_FLAG_RSS_CAPABLE                  (u32)(1 << 17)
#define IXGBE_FLAG_VMDQ_CAPABLE                 (u32)(1 << 18)
#define IXGBE_FLAG_VMDQ_ENABLED                 (u32)(1 << 19)
#define IXGBE_FLAG_NEED_LINK_UPDATE             (u32)(1 << 22)
#define IXGBE_FLAG_IN_WATCHDOG_TASK             (u32)(1 << 23)

/* default to trying for four seconds */
#define IXGBE_TRY_LINK_TIMEOUT (4 * HZ)

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats net_stats;

	/* structs defined in ixgbe_hw.h */
	struct ixgbe_hw hw;
	u16 msg_enable;
	struct ixgbe_hw_stats stats;

	/* Interrupt Throttle Rate */
	u32 eitr_param;

	unsigned long state;
	u64 tx_busy;
	u64 lro_aggregated;
	u64 lro_flushed;
	u64 lro_no_desc;
	unsigned int tx_ring_count;
	unsigned int rx_ring_count;

	u32 link_speed;
	bool link_up;
	unsigned long link_check_timeout;

	struct work_struct watchdog_task;
};

enum ixbge_state_t {
	__IXGBE_TESTING,
	__IXGBE_RESETTING,
	__IXGBE_DOWN
};

enum ixgbe_boards {
	board_82598,
};

extern struct ixgbe_info ixgbe_82598_info;

extern char ixgbe_driver_name[];
extern const char ixgbe_driver_version[];

extern int ixgbe_up(struct ixgbe_adapter *adapter);
extern void ixgbe_down(struct ixgbe_adapter *adapter);
extern void ixgbe_reinit_locked(struct ixgbe_adapter *adapter);
extern void ixgbe_reset(struct ixgbe_adapter *adapter);
extern void ixgbe_update_stats(struct ixgbe_adapter *adapter);
extern void ixgbe_set_ethtool_ops(struct net_device *netdev);
extern int ixgbe_setup_rx_resources(struct ixgbe_adapter *adapter,
				    struct ixgbe_ring *rxdr);
extern int ixgbe_setup_tx_resources(struct ixgbe_adapter *adapter,
				    struct ixgbe_ring *txdr);
extern void ixgbe_free_rx_resources(struct ixgbe_adapter *adapter,
                                    struct ixgbe_ring *rxdr);
extern void ixgbe_free_tx_resources(struct ixgbe_adapter *adapter,
                                    struct ixgbe_ring *txdr);

#endif /* _IXGBE_H_ */

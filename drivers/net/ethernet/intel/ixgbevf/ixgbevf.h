/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2012 Intel Corporation.

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

#ifndef _IXGBEVF_H_
#define _IXGBEVF_H_

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/u64_stats_sync.h>

#include "vf.h"

/* wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer */
struct ixgbevf_tx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	unsigned long time_stamp;
	u16 length;
	u16 next_to_watch;
	u16 mapped_as_page;
};

struct ixgbevf_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	struct page *page;
	dma_addr_t page_dma;
	unsigned int page_offset;
};

struct ixgbevf_ring {
	struct ixgbevf_adapter *adapter;  /* backlink */
	void *desc;			/* descriptor ring memory */
	dma_addr_t dma;			/* phys. address of descriptor ring */
	unsigned int size;		/* length in bytes */
	unsigned int count;		/* amount of descriptors */
	unsigned int next_to_use;
	unsigned int next_to_clean;

	int queue_index; /* needed for multiqueue queue management */
	union {
		struct ixgbevf_tx_buffer *tx_buffer_info;
		struct ixgbevf_rx_buffer *rx_buffer_info;
	};

	u64			total_bytes;
	u64			total_packets;
	struct u64_stats_sync	syncp;

	u16 head;
	u16 tail;

	u16 reg_idx; /* holds the special value that gets the hardware register
		      * offset associated with this ring, which is different
		      * for DCB and RSS modes */

#if defined(CONFIG_DCA) || defined(CONFIG_DCA_MODULE)
	/* cpu for tx queue */
	int cpu;
#endif

	u64 v_idx; /* maps directly to the index for this ring in the hardware
		    * vector array, can also be used for finding the bit in EICR
		    * and friends that represents the vector for this ring */

	u16 work_limit;                /* max work per interrupt */
	u16 rx_buf_len;
};

enum ixgbevf_ring_f_enum {
	RING_F_NONE = 0,
	RING_F_ARRAY_SIZE      /* must be last in enum set */
};

struct ixgbevf_ring_feature {
	int indices;
	int mask;
};

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBEVF_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define MAX_RX_QUEUES 1
#define MAX_TX_QUEUES 1

#define IXGBEVF_DEFAULT_TXD   1024
#define IXGBEVF_DEFAULT_RXD   512
#define IXGBEVF_MAX_TXD       4096
#define IXGBEVF_MIN_TXD       64
#define IXGBEVF_MAX_RXD       4096
#define IXGBEVF_MIN_RXD       64

/* Supported Rx Buffer Sizes */
#define IXGBEVF_RXBUFFER_64    64     /* Used for packet split */
#define IXGBEVF_RXBUFFER_128   128    /* Used for packet split */
#define IXGBEVF_RXBUFFER_256   256    /* Used for packet split */
#define IXGBEVF_RXBUFFER_2048  2048
#define IXGBEVF_MAX_RXBUFFER   16384  /* largest size for single descriptor */

#define IXGBEVF_RX_HDR_SIZE IXGBEVF_RXBUFFER_256

#define MAXIMUM_ETHERNET_VLAN_SIZE (VLAN_ETH_FRAME_LEN + ETH_FCS_LEN)

#define IXGBE_TX_FLAGS_CSUM		(u32)(1)
#define IXGBE_TX_FLAGS_VLAN		(u32)(1 << 1)
#define IXGBE_TX_FLAGS_TSO		(u32)(1 << 2)
#define IXGBE_TX_FLAGS_IPV4		(u32)(1 << 3)
#define IXGBE_TX_FLAGS_FCOE		(u32)(1 << 4)
#define IXGBE_TX_FLAGS_FSO		(u32)(1 << 5)
#define IXGBE_TX_FLAGS_VLAN_MASK	0xffff0000
#define IXGBE_TX_FLAGS_VLAN_PRIO_MASK	0x0000e000
#define IXGBE_TX_FLAGS_VLAN_SHIFT	16

/* MAX_MSIX_Q_VECTORS of these are allocated,
 * but we only use one per queue-specific vector.
 */
struct ixgbevf_q_vector {
	struct ixgbevf_adapter *adapter;
	struct napi_struct napi;
	DECLARE_BITMAP(rxr_idx, MAX_RX_QUEUES); /* Rx ring indices */
	DECLARE_BITMAP(txr_idx, MAX_TX_QUEUES); /* Tx ring indices */
	u8 rxr_count;     /* Rx ring count assigned to this vector */
	u8 txr_count;     /* Tx ring count assigned to this vector */
	u8 tx_itr;
	u8 rx_itr;
	u32 eitr;
	int v_idx;	  /* vector index in list */
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
	(&(((union ixgbe_adv_rx_desc *)((R).desc))[i]))
#define IXGBE_TX_DESC_ADV(R, i)	    \
	(&(((union ixgbe_adv_tx_desc *)((R).desc))[i]))
#define IXGBE_TX_CTXTDESC_ADV(R, i)	    \
	(&(((struct ixgbe_adv_tx_context_desc *)((R).desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE        16128

#define OTHER_VECTOR 1
#define NON_Q_VECTORS (OTHER_VECTOR)

#define MAX_MSIX_Q_VECTORS 2
#define MAX_MSIX_COUNT 2

#define MIN_MSIX_Q_VECTORS 2
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

/* board specific private data structure */
struct ixgbevf_adapter {
	struct timer_list watchdog_timer;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u16 bd_number;
	struct work_struct reset_task;
	struct ixgbevf_q_vector *q_vector[MAX_MSIX_Q_VECTORS];
	char name[MAX_MSIX_COUNT][IFNAMSIZ + 9];

	/* Interrupt Throttle Rate */
	u32 itr_setting;
	u16 eitr_low;
	u16 eitr_high;

	/* TX */
	struct ixgbevf_ring *tx_ring;	/* One per active queue */
	int num_tx_queues;
	u64 restart_queue;
	u64 hw_csum_tx_good;
	u64 lsc_int;
	u64 hw_tso_ctxt;
	u64 hw_tso6_ctxt;
	u32 tx_timeout_count;

	/* RX */
	struct ixgbevf_ring *rx_ring;	/* One per active queue */
	int num_rx_queues;
	int num_rx_pools;               /* == num_rx_queues in 82598 */
	int num_rx_queues_per_pool;	/* 1 if 82598, can be many if 82599 */
	u64 hw_csum_rx_error;
	u64 hw_rx_no_dma_resources;
	u64 hw_csum_rx_good;
	u64 non_eop_descs;
	int num_msix_vectors;
	int max_msix_q_vectors;         /* true count of q_vectors for device */
	struct ixgbevf_ring_feature ring_feature[RING_F_ARRAY_SIZE];
	struct msix_entry *msix_entries;

	u64 rx_hdr_split;
	u32 alloc_rx_page_failed;
	u32 alloc_rx_buff_failed;

	/* Some features need tri-state capability,
	 * thus the additional *_CAPABLE flags.
	 */
	u32 flags;
#define IXGBE_FLAG_RX_CSUM_ENABLED              (u32)(1)
#define IXGBE_FLAG_RX_1BUF_CAPABLE              (u32)(1 << 1)
#define IXGBE_FLAG_RX_PS_CAPABLE                (u32)(1 << 2)
#define IXGBE_FLAG_RX_PS_ENABLED                (u32)(1 << 3)
#define IXGBE_FLAG_IN_NETPOLL                   (u32)(1 << 4)
#define IXGBE_FLAG_IMIR_ENABLED                 (u32)(1 << 5)
#define IXGBE_FLAG_MQ_CAPABLE                   (u32)(1 << 6)
#define IXGBE_FLAG_NEED_LINK_UPDATE             (u32)(1 << 7)
#define IXGBE_FLAG_IN_WATCHDOG_TASK             (u32)(1 << 8)
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	/* structs defined in ixgbe_vf.h */
	struct ixgbe_hw hw;
	u16 msg_enable;
	struct ixgbevf_hw_stats stats;
	u64 zero_base;
	/* Interrupt Throttle Rate */
	u32 eitr_param;

	unsigned long state;
	u32 *config_space;
	u64 tx_busy;
	unsigned int tx_ring_count;
	unsigned int rx_ring_count;

	u32 link_speed;
	bool link_up;
	unsigned long link_check_timeout;

	struct work_struct watchdog_task;
	bool netdev_registered;
	bool dev_closed;
};

enum ixbgevf_state_t {
	__IXGBEVF_TESTING,
	__IXGBEVF_RESETTING,
	__IXGBEVF_DOWN
};

enum ixgbevf_boards {
	board_82599_vf,
	board_X540_vf,
};

extern const struct ixgbevf_info ixgbevf_82599_vf_info;
extern const struct ixgbevf_info ixgbevf_X540_vf_info;
extern const struct ixgbe_mbx_operations ixgbevf_mbx_ops;

/* needed by ethtool.c */
extern const char ixgbevf_driver_name[];
extern const char ixgbevf_driver_version[];

extern int ixgbevf_up(struct ixgbevf_adapter *adapter);
extern void ixgbevf_down(struct ixgbevf_adapter *adapter);
extern void ixgbevf_reinit_locked(struct ixgbevf_adapter *adapter);
extern void ixgbevf_reset(struct ixgbevf_adapter *adapter);
extern void ixgbevf_set_ethtool_ops(struct net_device *netdev);
extern int ixgbevf_setup_rx_resources(struct ixgbevf_adapter *,
				      struct ixgbevf_ring *);
extern int ixgbevf_setup_tx_resources(struct ixgbevf_adapter *,
				      struct ixgbevf_ring *);
extern void ixgbevf_free_rx_resources(struct ixgbevf_adapter *,
				      struct ixgbevf_ring *);
extern void ixgbevf_free_tx_resources(struct ixgbevf_adapter *,
				      struct ixgbevf_ring *);
extern void ixgbevf_update_stats(struct ixgbevf_adapter *adapter);

#ifdef ETHTOOL_OPS_COMPAT
extern int ethtool_ioctl(struct ifreq *ifr);

#endif
extern void ixgbe_napi_add_all(struct ixgbevf_adapter *adapter);
extern void ixgbe_napi_del_all(struct ixgbevf_adapter *adapter);

#ifdef DEBUG
extern char *ixgbevf_get_hw_dev_name(struct ixgbe_hw *hw);
#define hw_dbg(hw, format, arg...) \
	printk(KERN_DEBUG "%s: " format, ixgbevf_get_hw_dev_name(hw), ##arg)
#else
#define hw_dbg(hw, format, arg...) do {} while (0)
#endif

#endif /* _IXGBEVF_H_ */

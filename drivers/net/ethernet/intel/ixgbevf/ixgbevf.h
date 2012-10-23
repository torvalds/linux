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
};

struct ixgbevf_ring {
	struct ixgbevf_ring *next;
	struct net_device *netdev;
	struct device *dev;
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

	u16 rx_buf_len;
};

/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IXGBEVF_RX_BUFFER_WRITE	16	/* Must be power of 2 */

#define MAX_RX_QUEUES IXGBE_VF_MAX_RX_QUEUES
#define MAX_TX_QUEUES IXGBE_VF_MAX_TX_QUEUES

#define IXGBEVF_DEFAULT_TXD   1024
#define IXGBEVF_DEFAULT_RXD   512
#define IXGBEVF_MAX_TXD       4096
#define IXGBEVF_MIN_TXD       64
#define IXGBEVF_MAX_RXD       4096
#define IXGBEVF_MIN_RXD       64

/* Supported Rx Buffer Sizes */
#define IXGBEVF_RXBUFFER_256   256    /* Used for packet split */
#define IXGBEVF_RXBUFFER_3K    3072
#define IXGBEVF_RXBUFFER_7K    7168
#define IXGBEVF_RXBUFFER_15K   15360
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
	u16 v_idx;		/* index of q_vector within array, also used for
				 * finding the bit in EICR and friends that
				 * represents the vector for this ring */
	u16 itr;		/* Interrupt throttle rate written to EITR */
	struct napi_struct napi;
	struct ixgbevf_ring_container rx, tx;
	char name[IFNAMSIZ + 9];
};

/*
 * microsecond values for various ITR rates shifted by 2 to fit itr register
 * with the first 3 bits reserved 0
 */
#define IXGBE_MIN_RSC_ITR	24
#define IXGBE_100K_ITR		40
#define IXGBE_20K_ITR		200
#define IXGBE_10K_ITR		400
#define IXGBE_8K_ITR		500

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

#define IXGBEVF_RX_DESC(R, i)	    \
	(&(((union ixgbe_adv_rx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_DESC(R, i)	    \
	(&(((union ixgbe_adv_tx_desc *)((R)->desc))[i]))
#define IXGBEVF_TX_CTXTDESC(R, i)	    \
	(&(((struct ixgbe_adv_tx_context_desc *)((R)->desc))[i]))

#define IXGBE_MAX_JUMBO_FRAME_SIZE	9728 /* Maximum Supported Size 9.5KB */

#define OTHER_VECTOR 1
#define NON_Q_VECTORS (OTHER_VECTOR)

#define MAX_MSIX_Q_VECTORS 2

#define MIN_MSIX_Q_VECTORS 1
#define MIN_MSIX_COUNT (MIN_MSIX_Q_VECTORS + NON_Q_VECTORS)

/* board specific private data structure */
struct ixgbevf_adapter {
	struct timer_list watchdog_timer;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u16 bd_number;
	struct work_struct reset_task;
	struct ixgbevf_q_vector *q_vector[MAX_MSIX_Q_VECTORS];

	/* Interrupt Throttle Rate */
	u16 rx_itr_setting;
	u16 tx_itr_setting;

	/* interrupt masks */
	u32 eims_enable_mask;
	u32 eims_other;

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
	u64 hw_csum_rx_error;
	u64 hw_rx_no_dma_resources;
	u64 hw_csum_rx_good;
	u64 non_eop_descs;
	int num_msix_vectors;
	struct msix_entry *msix_entries;

	u32 alloc_rx_page_failed;
	u32 alloc_rx_buff_failed;

	/* Some features need tri-state capability,
	 * thus the additional *_CAPABLE flags.
	 */
	u32 flags;
#define IXGBE_FLAG_IN_WATCHDOG_TASK             (u32)(1)

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
	u64 tx_busy;
	unsigned int tx_ring_count;
	unsigned int rx_ring_count;

	u32 link_speed;
	bool link_up;

	struct work_struct watchdog_task;

	spinlock_t mbx_lock;
};

enum ixbgevf_state_t {
	__IXGBEVF_TESTING,
	__IXGBEVF_RESETTING,
	__IXGBEVF_DOWN
};

struct ixgbevf_cb {
	struct sk_buff *prev;
};
#define IXGBE_CB(skb) ((struct ixgbevf_cb *)(skb)->cb)

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

extern void ixgbevf_up(struct ixgbevf_adapter *adapter);
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
extern int ethtool_ioctl(struct ifreq *ifr);

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

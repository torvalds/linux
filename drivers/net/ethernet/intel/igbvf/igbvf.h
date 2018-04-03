/* SPDX-License-Identifier: GPL-2.0 */
/*******************************************************************************

  Intel(R) 82576 Virtual Function Linux driver
  Copyright(c) 2009 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, see <http://www.gnu.org/licenses/>.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* Linux PRO/1000 Ethernet Driver main header file */

#ifndef _IGBVF_H_
#define _IGBVF_H_

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>

#include "vf.h"

/* Forward declarations */
struct igbvf_info;
struct igbvf_adapter;

/* Interrupt defines */
#define IGBVF_START_ITR		488 /* ~8000 ints/sec */
#define IGBVF_4K_ITR		980
#define IGBVF_20K_ITR		196
#define IGBVF_70K_ITR		56

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

/* Interrupt modes, as used by the IntMode parameter */
#define IGBVF_INT_MODE_LEGACY	0
#define IGBVF_INT_MODE_MSI	1
#define IGBVF_INT_MODE_MSIX	2

/* Tx/Rx descriptor defines */
#define IGBVF_DEFAULT_TXD	256
#define IGBVF_MAX_TXD		4096
#define IGBVF_MIN_TXD		80

#define IGBVF_DEFAULT_RXD	256
#define IGBVF_MAX_RXD		4096
#define IGBVF_MIN_RXD		80

#define IGBVF_MIN_ITR_USECS	10 /* 100000 irq/sec */
#define IGBVF_MAX_ITR_USECS	10000 /* 100    irq/sec */

/* RX descriptor control thresholds.
 * PTHRESH - MAC will consider prefetch if it has fewer than this number of
 *	   descriptors available in its onboard memory.
 *	   Setting this to 0 disables RX descriptor prefetch.
 * HTHRESH - MAC will only prefetch if there are at least this many descriptors
 *	   available in host memory.
 *	   If PTHRESH is 0, this should also be 0.
 * WTHRESH - RX descriptor writeback threshold - MAC will delay writing back
 *	   descriptors until either it has this many to write back, or the
 *	   ITR timer expires.
 */
#define IGBVF_RX_PTHRESH	16
#define IGBVF_RX_HTHRESH	8
#define IGBVF_RX_WTHRESH	1

/* this is the size past which hardware will drop packets when setting LPE=0 */
#define MAXIMUM_ETHERNET_VLAN_SIZE	1522

#define IGBVF_FC_PAUSE_TIME	0x0680 /* 858 usec */

/* How many Tx Descriptors do we need to call netif_wake_queue ? */
#define IGBVF_TX_QUEUE_WAKE	32
/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define IGBVF_RX_BUFFER_WRITE	16 /* Must be power of 2 */

#define AUTO_ALL_MODES		0
#define IGBVF_EEPROM_APME	0x0400

#define IGBVF_MNG_VLAN_NONE	(-1)

#define IGBVF_MAX_MAC_FILTERS	3

/* Number of packet split data buffers (not including the header buffer) */
#define PS_PAGE_BUFFERS		(MAX_PS_BUFFERS - 1)

enum igbvf_boards {
	board_vf,
	board_i350_vf,
};

struct igbvf_queue_stats {
	u64 packets;
	u64 bytes;
};

/* wrappers around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer
 */
struct igbvf_buffer {
	dma_addr_t dma;
	struct sk_buff *skb;
	union {
		/* Tx */
		struct {
			unsigned long time_stamp;
			union e1000_adv_tx_desc *next_to_watch;
			u16 length;
			u16 mapped_as_page;
		};
		/* Rx */
		struct {
			struct page *page;
			u64 page_dma;
			unsigned int page_offset;
		};
	};
};

union igbvf_desc {
	union e1000_adv_rx_desc rx_desc;
	union e1000_adv_tx_desc tx_desc;
	struct e1000_adv_tx_context_desc tx_context_desc;
};

struct igbvf_ring {
	struct igbvf_adapter *adapter;  /* backlink */
	union igbvf_desc *desc;	/* pointer to ring memory  */
	dma_addr_t dma;		/* phys address of ring    */
	unsigned int size;	/* length of ring in bytes */
	unsigned int count;	/* number of desc. in ring */

	u16 next_to_use;
	u16 next_to_clean;

	u16 head;
	u16 tail;

	/* array of buffer information structs */
	struct igbvf_buffer *buffer_info;
	struct napi_struct napi;

	char name[IFNAMSIZ + 5];
	u32 eims_value;
	u32 itr_val;
	enum latency_range itr_range;
	u16 itr_register;
	int set_itr;

	struct sk_buff *rx_skb_top;

	struct igbvf_queue_stats stats;
};

/* board specific private data structure */
struct igbvf_adapter {
	struct timer_list watchdog_timer;
	struct timer_list blink_timer;

	struct work_struct reset_task;
	struct work_struct watchdog_task;

	const struct igbvf_info *ei;

	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	u32 bd_number;
	u32 rx_buffer_len;
	u32 polling_interval;
	u16 mng_vlan_id;
	u16 link_speed;
	u16 link_duplex;

	spinlock_t tx_queue_lock; /* prevent concurrent tail updates */

	/* track device up/down/testing state */
	unsigned long state;

	/* Interrupt Throttle Rate */
	u32 requested_itr; /* ints/sec or adaptive */
	u32 current_itr; /* Actual ITR register value, not ints/sec */

	/* Tx */
	struct igbvf_ring *tx_ring /* One per active queue */
	____cacheline_aligned_in_smp;

	unsigned int restart_queue;
	u32 txd_cmd;

	u32 tx_int_delay;
	u32 tx_abs_int_delay;

	unsigned int total_tx_bytes;
	unsigned int total_tx_packets;
	unsigned int total_rx_bytes;
	unsigned int total_rx_packets;

	/* Tx stats */
	u32 tx_timeout_count;
	u32 tx_fifo_head;
	u32 tx_head_addr;
	u32 tx_fifo_size;
	u32 tx_dma_failed;

	/* Rx */
	struct igbvf_ring *rx_ring;

	u32 rx_int_delay;
	u32 rx_abs_int_delay;

	/* Rx stats */
	u64 hw_csum_err;
	u64 hw_csum_good;
	u64 rx_hdr_split;
	u32 alloc_rx_buff_failed;
	u32 rx_dma_failed;

	unsigned int rx_ps_hdr_size;
	u32 max_frame_size;
	u32 min_frame_size;

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
	spinlock_t stats_lock; /* prevent concurrent stats updates */

	/* structs defined in e1000_hw.h */
	struct e1000_hw hw;

	/* The VF counters don't clear on read so we have to get a base
	 * count on driver start up and always subtract that base on
	 * on the first update, thus the flag..
	 */
	struct e1000_vf_stats stats;
	u64 zero_base;

	struct igbvf_ring test_tx_ring;
	struct igbvf_ring test_rx_ring;
	u32 test_icr;

	u32 msg_enable;
	struct msix_entry *msix_entries;
	int int_mode;
	u32 eims_enable_mask;
	u32 eims_other;
	u32 int_counter0;
	u32 int_counter1;

	u32 eeprom_wol;
	u32 wol;
	u32 pba;

	bool fc_autoneg;

	unsigned long led_status;

	unsigned int flags;
	unsigned long last_reset;
};

struct igbvf_info {
	enum e1000_mac_type	mac;
	unsigned int		flags;
	u32			pba;
	void			(*init_ops)(struct e1000_hw *);
	s32			(*get_variants)(struct igbvf_adapter *);
};

/* hardware capability, feature, and workaround flags */
#define IGBVF_FLAG_RX_CSUM_DISABLED	BIT(0)
#define IGBVF_FLAG_RX_LB_VLAN_BSWAP	BIT(1)
#define IGBVF_RX_DESC_ADV(R, i)     \
	(&((((R).desc))[i].rx_desc))
#define IGBVF_TX_DESC_ADV(R, i)     \
	(&((((R).desc))[i].tx_desc))
#define IGBVF_TX_CTXTDESC_ADV(R, i) \
	(&((((R).desc))[i].tx_context_desc))

enum igbvf_state_t {
	__IGBVF_TESTING,
	__IGBVF_RESETTING,
	__IGBVF_DOWN
};

extern char igbvf_driver_name[];
extern const char igbvf_driver_version[];

void igbvf_check_options(struct igbvf_adapter *);
void igbvf_set_ethtool_ops(struct net_device *);

int igbvf_up(struct igbvf_adapter *);
void igbvf_down(struct igbvf_adapter *);
void igbvf_reinit_locked(struct igbvf_adapter *);
int igbvf_setup_rx_resources(struct igbvf_adapter *, struct igbvf_ring *);
int igbvf_setup_tx_resources(struct igbvf_adapter *, struct igbvf_ring *);
void igbvf_free_rx_resources(struct igbvf_ring *);
void igbvf_free_tx_resources(struct igbvf_ring *);
void igbvf_update_stats(struct igbvf_adapter *);

extern unsigned int copybreak;

#endif /* _IGBVF_H_ */

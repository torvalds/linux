/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_H_
#define _IGC_H_

#include <linux/kobject.h>

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>

#include <linux/ethtool.h>

#include <linux/sctp.h>

#define IGC_ERR(args...) pr_err("igc: " args)

#define PFX "igc: "

#include <linux/timecounter.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>

#include "igc_hw.h"

/* main */
extern char igc_driver_name[];
extern char igc_driver_version[];

/* Interrupt defines */
#define IGC_START_ITR			648 /* ~6000 ints/sec */
#define IGC_FLAG_HAS_MSI		BIT(0)
#define IGC_FLAG_QUEUE_PAIRS		BIT(4)
#define IGC_FLAG_HAS_MSIX		BIT(13)

#define IGC_START_ITR			648 /* ~6000 ints/sec */
#define IGC_4K_ITR			980
#define IGC_20K_ITR			196
#define IGC_70K_ITR			56

/* Transmit and receive queues */
#define IGC_MAX_RX_QUEUES		4
#define IGC_MAX_TX_QUEUES		4

#define MAX_Q_VECTORS			8
#define MAX_STD_JUMBO_FRAME_SIZE	9216

enum igc_state_t {
	__IGC_TESTING,
	__IGC_RESETTING,
	__IGC_DOWN,
	__IGC_PTP_TX_IN_PROGRESS,
};

struct igc_tx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 restart_queue;
};

struct igc_rx_queue_stats {
	u64 packets;
	u64 bytes;
	u64 drops;
	u64 csum_err;
	u64 alloc_failed;
};

struct igc_rx_packet_stats {
	u64 ipv4_packets;      /* IPv4 headers processed */
	u64 ipv4e_packets;     /* IPv4E headers with extensions processed */
	u64 ipv6_packets;      /* IPv6 headers processed */
	u64 ipv6e_packets;     /* IPv6E headers with extensions processed */
	u64 tcp_packets;       /* TCP headers processed */
	u64 udp_packets;       /* UDP headers processed */
	u64 sctp_packets;      /* SCTP headers processed */
	u64 nfs_packets;       /* NFS headers processe */
	u64 other_packets;
};

struct igc_ring_container {
	struct igc_ring *ring;          /* pointer to linked list of rings */
	unsigned int total_bytes;       /* total bytes processed this int */
	unsigned int total_packets;     /* total packets processed this int */
	u16 work_limit;                 /* total work allowed per interrupt */
	u8 count;                       /* total number of rings in vector */
	u8 itr;                         /* current ITR setting for ring */
};

struct igc_ring {
	struct igc_q_vector *q_vector;  /* backlink to q_vector */
	struct net_device *netdev;      /* back pointer to net_device */
	struct device *dev;             /* device for dma mapping */
	union {                         /* array of buffer info structs */
		struct igc_tx_buffer *tx_buffer_info;
		struct igc_rx_buffer *rx_buffer_info;
	};
	void *desc;                     /* descriptor ring memory */
	unsigned long flags;            /* ring specific flags */
	void __iomem *tail;             /* pointer to ring tail register */
	dma_addr_t dma;                 /* phys address of the ring */
	unsigned int size;              /* length of desc. ring in bytes */

	u16 count;                      /* number of desc. in the ring */
	u8 queue_index;                 /* logical index of the ring*/
	u8 reg_idx;                     /* physical index of the ring */

	/* everything past this point are written often */
	u16 next_to_clean;
	u16 next_to_use;
	u16 next_to_alloc;

	union {
		/* TX */
		struct {
			struct igc_tx_queue_stats tx_stats;
		};
		/* RX */
		struct {
			struct igc_rx_queue_stats rx_stats;
			struct igc_rx_packet_stats pkt_stats;
			struct sk_buff *skb;
		};
	};
} ____cacheline_internodealigned_in_smp;

struct igc_q_vector {
	struct igc_adapter *adapter;    /* backlink */
	void __iomem *itr_register;
	u32 eims_value;                 /* EIMS mask value */

	u16 itr_val;
	u8 set_itr;

	struct igc_ring_container rx, tx;

	struct napi_struct napi;

	struct rcu_head rcu;    /* to avoid race with update stats on free */
	char name[IFNAMSIZ + 9];
	struct net_device poll_dev;

	/* for dynamic allocation of rings associated with this q_vector */
	struct igc_ring ring[0] ____cacheline_internodealigned_in_smp;
};

struct igc_mac_addr {
	u8 addr[ETH_ALEN];
	u8 queue;
	u8 state; /* bitmask */
};

#define IGC_MAC_STATE_DEFAULT	0x1
#define IGC_MAC_STATE_MODIFIED	0x2
#define IGC_MAC_STATE_IN_USE	0x4

/* Board specific private data structure */
struct igc_adapter {
	struct net_device *netdev;

	unsigned long state;
	unsigned int flags;
	unsigned int num_q_vectors;

	struct msix_entry *msix_entries;

	/* TX */
	u16 tx_work_limit;
	int num_tx_queues;
	struct igc_ring *tx_ring[IGC_MAX_TX_QUEUES];

	/* RX */
	int num_rx_queues;
	struct igc_ring *rx_ring[IGC_MAX_RX_QUEUES];

	struct timer_list watchdog_timer;
	struct timer_list dma_err_timer;
	struct timer_list phy_info_timer;

	u16 link_speed;
	u16 link_duplex;

	u8 port_num;

	u8 __iomem *io_addr;
	/* Interrupt Throttle Rate */
	u32 rx_itr_setting;
	u32 tx_itr_setting;

	struct work_struct reset_task;
	struct work_struct watchdog_task;
	struct work_struct dma_err_task;

	int msg_enable;
	u32 max_frame_size;

	/* OS defined structs */
	struct pci_dev *pdev;

	/* structs defined in igc_hw.h */
	struct igc_hw hw;
	struct igc_hw_stats stats;

	struct igc_q_vector *q_vector[MAX_Q_VECTORS];
	u32 eims_enable_mask;
	u32 eims_other;

	u16 tx_ring_count;
	u16 rx_ring_count;

	u32 rss_queues;

	struct igc_mac_addr *mac_table;
};

#endif /* _IGC_H_ */

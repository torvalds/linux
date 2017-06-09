/*
 * Copyright 2015 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ENA_H
#define ENA_H

#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

#include "ena_com.h"
#include "ena_eth_com.h"

#define DRV_MODULE_VER_MAJOR	1
#define DRV_MODULE_VER_MINOR	1
#define DRV_MODULE_VER_SUBMINOR 2

#define DRV_MODULE_NAME		"ena"
#ifndef DRV_MODULE_VERSION
#define DRV_MODULE_VERSION \
	__stringify(DRV_MODULE_VER_MAJOR) "."	\
	__stringify(DRV_MODULE_VER_MINOR) "."	\
	__stringify(DRV_MODULE_VER_SUBMINOR)
#endif

#define DEVICE_NAME	"Elastic Network Adapter (ENA)"

/* 1 for AENQ + ADMIN */
#define ENA_MAX_MSIX_VEC(io_queues)	(1 + (io_queues))

#define ENA_REG_BAR			0
#define ENA_MEM_BAR			2
#define ENA_BAR_MASK (BIT(ENA_REG_BAR) | BIT(ENA_MEM_BAR))

#define ENA_DEFAULT_RING_SIZE	(1024)

#define ENA_TX_WAKEUP_THRESH		(MAX_SKB_FRAGS + 2)
#define ENA_DEFAULT_RX_COPYBREAK	(128 - NET_IP_ALIGN)

/* limit the buffer size to 600 bytes to handle MTU changes from very
 * small to very large, in which case the number of buffers per packet
 * could exceed ENA_PKT_MAX_BUFS
 */
#define ENA_DEFAULT_MIN_RX_BUFF_ALLOC_SIZE 600

#define ENA_MIN_MTU		128

#define ENA_NAME_MAX_LEN	20
#define ENA_IRQNAME_SIZE	40

#define ENA_PKT_MAX_BUFS	19

#define ENA_RX_RSS_TABLE_LOG_SIZE  7
#define ENA_RX_RSS_TABLE_SIZE	(1 << ENA_RX_RSS_TABLE_LOG_SIZE)

#define ENA_HASH_KEY_SIZE	40

/* The number of tx packet completions that will be handled each NAPI poll
 * cycle is ring_size / ENA_TX_POLL_BUDGET_DIVIDER.
 */
#define ENA_TX_POLL_BUDGET_DIVIDER	4

/* Refill Rx queue when number of available descriptors is below
 * QUEUE_SIZE / ENA_RX_REFILL_THRESH_DIVIDER
 */
#define ENA_RX_REFILL_THRESH_DIVIDER	8

/* Number of queues to check for missing queues per timer service */
#define ENA_MONITORED_TX_QUEUES	4
/* Max timeout packets before device reset */
#define MAX_NUM_OF_TIMEOUTED_PACKETS 128

#define ENA_TX_RING_IDX_NEXT(idx, ring_size) (((idx) + 1) & ((ring_size) - 1))

#define ENA_RX_RING_IDX_NEXT(idx, ring_size) (((idx) + 1) & ((ring_size) - 1))
#define ENA_RX_RING_IDX_ADD(idx, n, ring_size) \
	(((idx) + (n)) & ((ring_size) - 1))

#define ENA_IO_TXQ_IDX(q)	(2 * (q))
#define ENA_IO_RXQ_IDX(q)	(2 * (q) + 1)

#define ENA_MGMNT_IRQ_IDX		0
#define ENA_IO_IRQ_FIRST_IDX		1
#define ENA_IO_IRQ_IDX(q)		(ENA_IO_IRQ_FIRST_IDX + (q))

/* ENA device should send keep alive msg every 1 sec.
 * We wait for 6 sec just to be on the safe side.
 */
#define ENA_DEVICE_KALIVE_TIMEOUT	(6 * HZ)

#define ENA_MMIO_DISABLE_REG_READ	BIT(0)

struct ena_irq {
	irq_handler_t handler;
	void *data;
	int cpu;
	u32 vector;
	cpumask_t affinity_hint_mask;
	char name[ENA_IRQNAME_SIZE];
};

struct ena_napi {
	struct napi_struct napi ____cacheline_aligned;
	struct ena_ring *tx_ring;
	struct ena_ring *rx_ring;
	u32 qid;
};

struct ena_tx_buffer {
	struct sk_buff *skb;
	/* num of ena desc for this specific skb
	 * (includes data desc and metadata desc)
	 */
	u32 tx_descs;
	/* num of buffers used by this skb */
	u32 num_of_bufs;
	/* Save the last jiffies to detect missing tx packets */
	unsigned long last_jiffies;
	struct ena_com_buf bufs[ENA_PKT_MAX_BUFS];
} ____cacheline_aligned;

struct ena_rx_buffer {
	struct sk_buff *skb;
	struct page *page;
	u32 page_offset;
	struct ena_com_buf ena_buf;
} ____cacheline_aligned;

struct ena_stats_tx {
	u64 cnt;
	u64 bytes;
	u64 queue_stop;
	u64 prepare_ctx_err;
	u64 queue_wakeup;
	u64 dma_mapping_err;
	u64 linearize;
	u64 linearize_failed;
	u64 napi_comp;
	u64 tx_poll;
	u64 doorbells;
	u64 missing_tx_comp;
	u64 bad_req_id;
};

struct ena_stats_rx {
	u64 cnt;
	u64 bytes;
	u64 refil_partial;
	u64 bad_csum;
	u64 page_alloc_fail;
	u64 skb_alloc_fail;
	u64 dma_mapping_err;
	u64 bad_desc_num;
	u64 rx_copybreak_pkt;
};

struct ena_ring {
	/* Holds the empty requests for TX out of order completions */
	u16 *free_tx_ids;
	union {
		struct ena_tx_buffer *tx_buffer_info;
		struct ena_rx_buffer *rx_buffer_info;
	};

	/* cache ptr to avoid using the adapter */
	struct device *dev;
	struct pci_dev *pdev;
	struct napi_struct *napi;
	struct net_device *netdev;
	struct ena_com_dev *ena_dev;
	struct ena_adapter *adapter;
	struct ena_com_io_cq *ena_com_io_cq;
	struct ena_com_io_sq *ena_com_io_sq;

	u16 next_to_use;
	u16 next_to_clean;
	u16 rx_copybreak;
	u16 qid;
	u16 mtu;
	u16 sgl_size;

	/* The maximum header length the device can handle */
	u8 tx_max_header_size;

	/* cpu for TPH */
	int cpu;
	 /* number of tx/rx_buffer_info's entries */
	int ring_size;

	enum ena_admin_placement_policy_type tx_mem_queue_type;

	struct ena_com_rx_buf_info ena_bufs[ENA_PKT_MAX_BUFS];
	u32  smoothed_interval;
	u32  per_napi_packets;
	u32  per_napi_bytes;
	enum ena_intr_moder_level moder_tbl_idx;
	struct u64_stats_sync syncp;
	union {
		struct ena_stats_tx tx_stats;
		struct ena_stats_rx rx_stats;
	};
} ____cacheline_aligned;

struct ena_stats_dev {
	u64 tx_timeout;
	u64 io_suspend;
	u64 io_resume;
	u64 wd_expired;
	u64 interface_up;
	u64 interface_down;
	u64 admin_q_pause;
	u64 rx_drops;
};

enum ena_flags_t {
	ENA_FLAG_DEVICE_RUNNING,
	ENA_FLAG_DEV_UP,
	ENA_FLAG_LINK_UP,
	ENA_FLAG_TRIGGER_RESET
};

/* adapter specific private data structure */
struct ena_adapter {
	struct ena_com_dev *ena_dev;
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	/* rx packets that shorter that this len will be copied to the skb
	 * header
	 */
	u32 rx_copybreak;
	u32 max_mtu;

	int num_queues;

	int msix_vecs;

	u32 tx_usecs, rx_usecs; /* interrupt moderation */
	u32 tx_frames, rx_frames; /* interrupt moderation */

	u32 tx_ring_size;
	u32 rx_ring_size;

	u32 msg_enable;

	u16 max_tx_sgl_size;
	u16 max_rx_sgl_size;

	u8 mac_addr[ETH_ALEN];

	char name[ENA_NAME_MAX_LEN];

	unsigned long flags;
	/* TX */
	struct ena_ring tx_ring[ENA_MAX_NUM_IO_QUEUES]
		____cacheline_aligned_in_smp;

	/* RX */
	struct ena_ring rx_ring[ENA_MAX_NUM_IO_QUEUES]
		____cacheline_aligned_in_smp;

	struct ena_napi ena_napi[ENA_MAX_NUM_IO_QUEUES];

	struct ena_irq irq_tbl[ENA_MAX_MSIX_VEC(ENA_MAX_NUM_IO_QUEUES)];

	/* timer service */
	struct work_struct reset_task;
	struct work_struct suspend_io_task;
	struct work_struct resume_io_task;
	struct timer_list timer_service;

	bool wd_state;
	unsigned long last_keep_alive_jiffies;

	struct u64_stats_sync syncp;
	struct ena_stats_dev dev_stats;

	/* last queue index that was checked for uncompleted tx packets */
	u32 last_monitored_tx_qid;
};

void ena_set_ethtool_ops(struct net_device *netdev);

void ena_dump_stats_to_dmesg(struct ena_adapter *adapter);

void ena_dump_stats_to_buf(struct ena_adapter *adapter, u8 *buf);

int ena_get_sset_count(struct net_device *netdev, int sset);

#endif /* !(ENA_H) */

/* SPDX-License-Identifier: (GPL-2.0 OR MIT)
 * Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#ifndef _GVE_H_
#define _GVE_H_

#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/u64_stats_sync.h>

#include "gve_desc.h"
#include "gve_desc_dqo.h"

#ifndef PCI_VENDOR_ID_GOOGLE
#define PCI_VENDOR_ID_GOOGLE	0x1ae0
#endif

#define PCI_DEV_ID_GVNIC	0x0042

#define GVE_REGISTER_BAR	0
#define GVE_DOORBELL_BAR	2

/* Driver can alloc up to 2 segments for the header and 2 for the payload. */
#define GVE_TX_MAX_IOVEC	4
/* 1 for management, 1 for rx, 1 for tx */
#define GVE_MIN_MSIX 3

/* Numbers of gve tx/rx stats in stats report. */
#define GVE_TX_STATS_REPORT_NUM	5
#define GVE_RX_STATS_REPORT_NUM	2

/* Interval to schedule a stats report update, 20000ms. */
#define GVE_STATS_REPORT_TIMER_PERIOD	20000

/* Numbers of NIC tx/rx stats in stats report. */
#define NIC_TX_STATS_REPORT_NUM	0
#define NIC_RX_STATS_REPORT_NUM	4

#define GVE_DATA_SLOT_ADDR_PAGE_MASK (~(PAGE_SIZE - 1))

/* PTYPEs are always 10 bits. */
#define GVE_NUM_PTYPES	1024

#define GVE_RX_BUFFER_SIZE_DQO 2048

/* Each slot in the desc ring has a 1:1 mapping to a slot in the data ring */
struct gve_rx_desc_queue {
	struct gve_rx_desc *desc_ring; /* the descriptor ring */
	dma_addr_t bus; /* the bus for the desc_ring */
	u8 seqno; /* the next expected seqno for this desc*/
};

/* The page info for a single slot in the RX data queue */
struct gve_rx_slot_page_info {
	struct page *page;
	void *page_address;
	u32 page_offset; /* offset to write to in page */
	int pagecnt_bias; /* expected pagecnt if only the driver has a ref */
	u8 can_flip;
};

/* A list of pages registered with the device during setup and used by a queue
 * as buffers
 */
struct gve_queue_page_list {
	u32 id; /* unique id */
	u32 num_entries;
	struct page **pages; /* list of num_entries pages */
	dma_addr_t *page_buses; /* the dma addrs of the pages */
};

/* Each slot in the data ring has a 1:1 mapping to a slot in the desc ring */
struct gve_rx_data_queue {
	union gve_rx_data_slot *data_ring; /* read by NIC */
	dma_addr_t data_bus; /* dma mapping of the slots */
	struct gve_rx_slot_page_info *page_info; /* page info of the buffers */
	struct gve_queue_page_list *qpl; /* qpl assigned to this queue */
	u8 raw_addressing; /* use raw_addressing? */
};

struct gve_priv;

/* RX buffer queue for posting buffers to HW.
 * Each RX (completion) queue has a corresponding buffer queue.
 */
struct gve_rx_buf_queue_dqo {
	struct gve_rx_desc_dqo *desc_ring;
	dma_addr_t bus;
	u32 head; /* Pointer to start cleaning buffers at. */
	u32 tail; /* Last posted buffer index + 1 */
	u32 mask; /* Mask for indices to the size of the ring */
};

/* RX completion queue to receive packets from HW. */
struct gve_rx_compl_queue_dqo {
	struct gve_rx_compl_desc_dqo *desc_ring;
	dma_addr_t bus;

	/* Number of slots which did not have a buffer posted yet. We should not
	 * post more buffers than the queue size to avoid HW overrunning the
	 * queue.
	 */
	int num_free_slots;

	/* HW uses a "generation bit" to notify SW of new descriptors. When a
	 * descriptor's generation bit is different from the current generation,
	 * that descriptor is ready to be consumed by SW.
	 */
	u8 cur_gen_bit;

	/* Pointer into desc_ring where the next completion descriptor will be
	 * received.
	 */
	u32 head;
	u32 mask; /* Mask for indices to the size of the ring */
};

/* Stores state for tracking buffers posted to HW */
struct gve_rx_buf_state_dqo {
	/* The page posted to HW. */
	struct gve_rx_slot_page_info page_info;

	/* The DMA address corresponding to `page_info`. */
	dma_addr_t addr;

	/* Last offset into the page when it only had a single reference, at
	 * which point every other offset is free to be reused.
	 */
	u32 last_single_ref_offset;

	/* Linked list index to next element in the list, or -1 if none */
	s16 next;
};

/* `head` and `tail` are indices into an array, or -1 if empty. */
struct gve_index_list {
	s16 head;
	s16 tail;
};

/* Contains datapath state used to represent an RX queue. */
struct gve_rx_ring {
	struct gve_priv *gve;
	union {
		/* GQI fields */
		struct {
			struct gve_rx_desc_queue desc;
			struct gve_rx_data_queue data;

			/* threshold for posting new buffs and descs */
			u32 db_threshold;
		};

		/* DQO fields. */
		struct {
			struct gve_rx_buf_queue_dqo bufq;
			struct gve_rx_compl_queue_dqo complq;

			struct gve_rx_buf_state_dqo *buf_states;
			u16 num_buf_states;

			/* Linked list of gve_rx_buf_state_dqo. Index into
			 * buf_states, or -1 if empty.
			 */
			s16 free_buf_states;

			/* Linked list of gve_rx_buf_state_dqo. Indexes into
			 * buf_states, or -1 if empty.
			 *
			 * This list contains buf_states which are pointing to
			 * valid buffers.
			 *
			 * We use a FIFO here in order to increase the
			 * probability that buffers can be reused by increasing
			 * the time between usages.
			 */
			struct gve_index_list recycled_buf_states;

			/* Linked list of gve_rx_buf_state_dqo. Indexes into
			 * buf_states, or -1 if empty.
			 *
			 * This list contains buf_states which have buffers
			 * which cannot be reused yet.
			 */
			struct gve_index_list used_buf_states;
		} dqo;
	};

	u64 rbytes; /* free-running bytes received */
	u64 rpackets; /* free-running packets received */
	u32 cnt; /* free-running total number of completed packets */
	u32 fill_cnt; /* free-running total number of descs and buffs posted */
	u32 mask; /* masks the cnt and fill_cnt to the size of the ring */
	u64 rx_copybreak_pkt; /* free-running count of copybreak packets */
	u64 rx_copied_pkt; /* free-running total number of copied packets */
	u64 rx_skb_alloc_fail; /* free-running count of skb alloc fails */
	u64 rx_buf_alloc_fail; /* free-running count of buffer alloc fails */
	u64 rx_desc_err_dropped_pkt; /* free-running count of packets dropped by descriptor error */
	u32 q_num; /* queue index */
	u32 ntfy_id; /* notification block index */
	struct gve_queue_resources *q_resources; /* head and tail pointer idx */
	dma_addr_t q_resources_bus; /* dma address for the queue resources */
	struct u64_stats_sync statss; /* sync stats for 32bit archs */

	/* head and tail of skb chain for the current packet or NULL if none */
	struct sk_buff *skb_head;
	struct sk_buff *skb_tail;
};

/* A TX desc ring entry */
union gve_tx_desc {
	struct gve_tx_pkt_desc pkt; /* first desc for a packet */
	struct gve_tx_seg_desc seg; /* subsequent descs for a packet */
};

/* Tracks the memory in the fifo occupied by a segment of a packet */
struct gve_tx_iovec {
	u32 iov_offset; /* offset into this segment */
	u32 iov_len; /* length */
	u32 iov_padding; /* padding associated with this segment */
};

/* Tracks the memory in the fifo occupied by the skb. Mapped 1:1 to a desc
 * ring entry but only used for a pkt_desc not a seg_desc
 */
struct gve_tx_buffer_state {
	struct sk_buff *skb; /* skb for this pkt */
	union {
		struct gve_tx_iovec iov[GVE_TX_MAX_IOVEC]; /* segments of this pkt */
		struct {
			DEFINE_DMA_UNMAP_ADDR(dma);
			DEFINE_DMA_UNMAP_LEN(len);
		};
	};
};

/* A TX buffer - each queue has one */
struct gve_tx_fifo {
	void *base; /* address of base of FIFO */
	u32 size; /* total size */
	atomic_t available; /* how much space is still available */
	u32 head; /* offset to write at */
	struct gve_queue_page_list *qpl; /* QPL mapped into this FIFO */
};

/* TX descriptor for DQO format */
union gve_tx_desc_dqo {
	struct gve_tx_pkt_desc_dqo pkt;
	struct gve_tx_tso_context_desc_dqo tso_ctx;
	struct gve_tx_general_context_desc_dqo general_ctx;
};

enum gve_packet_state {
	/* Packet is in free list, available to be allocated.
	 * This should always be zero since state is not explicitly initialized.
	 */
	GVE_PACKET_STATE_UNALLOCATED,
	/* Packet is expecting a regular data completion or miss completion */
	GVE_PACKET_STATE_PENDING_DATA_COMPL,
	/* Packet has received a miss completion and is expecting a
	 * re-injection completion.
	 */
	GVE_PACKET_STATE_PENDING_REINJECT_COMPL,
	/* No valid completion received within the specified timeout. */
	GVE_PACKET_STATE_TIMED_OUT_COMPL,
};

struct gve_tx_pending_packet_dqo {
	struct sk_buff *skb; /* skb for this packet */

	/* 0th element corresponds to the linear portion of `skb`, should be
	 * unmapped with `dma_unmap_single`.
	 *
	 * All others correspond to `skb`'s frags and should be unmapped with
	 * `dma_unmap_page`.
	 */
	DEFINE_DMA_UNMAP_ADDR(dma[MAX_SKB_FRAGS + 1]);
	DEFINE_DMA_UNMAP_LEN(len[MAX_SKB_FRAGS + 1]);
	u16 num_bufs;

	/* Linked list index to next element in the list, or -1 if none */
	s16 next;

	/* Linked list index to prev element in the list, or -1 if none.
	 * Used for tracking either outstanding miss completions or prematurely
	 * freed packets.
	 */
	s16 prev;

	/* Identifies the current state of the packet as defined in
	 * `enum gve_packet_state`.
	 */
	u8 state;

	/* If packet is an outstanding miss completion, then the packet is
	 * freed if the corresponding re-injection completion is not received
	 * before kernel jiffies exceeds timeout_jiffies.
	 */
	unsigned long timeout_jiffies;
};

/* Contains datapath state used to represent a TX queue. */
struct gve_tx_ring {
	/* Cacheline 0 -- Accessed & dirtied during transmit */
	union {
		/* GQI fields */
		struct {
			struct gve_tx_fifo tx_fifo;
			u32 req; /* driver tracked head pointer */
			u32 done; /* driver tracked tail pointer */
		};

		/* DQO fields. */
		struct {
			/* Linked list of gve_tx_pending_packet_dqo. Index into
			 * pending_packets, or -1 if empty.
			 *
			 * This is a consumer list owned by the TX path. When it
			 * runs out, the producer list is stolen from the
			 * completion handling path
			 * (dqo_compl.free_pending_packets).
			 */
			s16 free_pending_packets;

			/* Cached value of `dqo_compl.hw_tx_head` */
			u32 head;
			u32 tail; /* Last posted buffer index + 1 */

			/* Index of the last descriptor with "report event" bit
			 * set.
			 */
			u32 last_re_idx;
		} dqo_tx;
	};

	/* Cacheline 1 -- Accessed & dirtied during gve_clean_tx_done */
	union {
		/* GQI fields */
		struct {
			/* Spinlock for when cleanup in progress */
			spinlock_t clean_lock;
		};

		/* DQO fields. */
		struct {
			u32 head; /* Last read on compl_desc */

			/* Tracks the current gen bit of compl_q */
			u8 cur_gen_bit;

			/* Linked list of gve_tx_pending_packet_dqo. Index into
			 * pending_packets, or -1 if empty.
			 *
			 * This is the producer list, owned by the completion
			 * handling path. When the consumer list
			 * (dqo_tx.free_pending_packets) is runs out, this list
			 * will be stolen.
			 */
			atomic_t free_pending_packets;

			/* Last TX ring index fetched by HW */
			atomic_t hw_tx_head;

			/* List to track pending packets which received a miss
			 * completion but not a corresponding reinjection.
			 */
			struct gve_index_list miss_completions;

			/* List to track pending packets that were completed
			 * before receiving a valid completion because they
			 * reached a specified timeout.
			 */
			struct gve_index_list timed_out_completions;
		} dqo_compl;
	} ____cacheline_aligned;
	u64 pkt_done; /* free-running - total packets completed */
	u64 bytes_done; /* free-running - total bytes completed */
	u64 dropped_pkt; /* free-running - total packets dropped */
	u64 dma_mapping_error; /* count of dma mapping errors */

	/* Cacheline 2 -- Read-mostly fields */
	union {
		/* GQI fields */
		struct {
			union gve_tx_desc *desc;

			/* Maps 1:1 to a desc */
			struct gve_tx_buffer_state *info;
		};

		/* DQO fields. */
		struct {
			union gve_tx_desc_dqo *tx_ring;
			struct gve_tx_compl_desc *compl_ring;

			struct gve_tx_pending_packet_dqo *pending_packets;
			s16 num_pending_packets;

			u32 complq_mask; /* complq size is complq_mask + 1 */
		} dqo;
	} ____cacheline_aligned;
	struct netdev_queue *netdev_txq;
	struct gve_queue_resources *q_resources; /* head and tail pointer idx */
	struct device *dev;
	u32 mask; /* masks req and done down to queue size */
	u8 raw_addressing; /* use raw_addressing? */

	/* Slow-path fields */
	u32 q_num ____cacheline_aligned; /* queue idx */
	u32 stop_queue; /* count of queue stops */
	u32 wake_queue; /* count of queue wakes */
	u32 ntfy_id; /* notification block index */
	dma_addr_t bus; /* dma address of the descr ring */
	dma_addr_t q_resources_bus; /* dma address of the queue resources */
	dma_addr_t complq_bus_dqo; /* dma address of the dqo.compl_ring */
	struct u64_stats_sync statss; /* sync stats for 32bit archs */
} ____cacheline_aligned;

/* Wraps the info for one irq including the napi struct and the queues
 * associated with that irq.
 */
struct gve_notify_block {
	__be32 irq_db_index; /* idx into Bar2 - set by device, must be 1st */
	char name[IFNAMSIZ + 16]; /* name registered with the kernel */
	struct napi_struct napi; /* kernel napi struct for this block */
	struct gve_priv *priv;
	struct gve_tx_ring *tx; /* tx rings on this block */
	struct gve_rx_ring *rx; /* rx rings on this block */
} ____cacheline_aligned;

/* Tracks allowed and current queue settings */
struct gve_queue_config {
	u16 max_queues;
	u16 num_queues; /* current */
};

/* Tracks the available and used qpl IDs */
struct gve_qpl_config {
	u32 qpl_map_size; /* map memory size */
	unsigned long *qpl_id_map; /* bitmap of used qpl ids */
};

struct gve_options_dqo_rda {
	u16 tx_comp_ring_entries; /* number of tx_comp descriptors */
	u16 rx_buff_ring_entries; /* number of rx_buff descriptors */
};

struct gve_ptype {
	u8 l3_type;  /* `gve_l3_type` in gve_adminq.h */
	u8 l4_type;  /* `gve_l4_type` in gve_adminq.h */
};

struct gve_ptype_lut {
	struct gve_ptype ptypes[GVE_NUM_PTYPES];
};

/* GVE_QUEUE_FORMAT_UNSPECIFIED must be zero since 0 is the default value
 * when the entire configure_device_resources command is zeroed out and the
 * queue_format is not specified.
 */
enum gve_queue_format {
	GVE_QUEUE_FORMAT_UNSPECIFIED	= 0x0,
	GVE_GQI_RDA_FORMAT		= 0x1,
	GVE_GQI_QPL_FORMAT		= 0x2,
	GVE_DQO_RDA_FORMAT		= 0x3,
};

struct gve_priv {
	struct net_device *dev;
	struct gve_tx_ring *tx; /* array of tx_cfg.num_queues */
	struct gve_rx_ring *rx; /* array of rx_cfg.num_queues */
	struct gve_queue_page_list *qpls; /* array of num qpls */
	struct gve_notify_block *ntfy_blocks; /* array of num_ntfy_blks */
	dma_addr_t ntfy_block_bus;
	struct msix_entry *msix_vectors; /* array of num_ntfy_blks + 1 */
	char mgmt_msix_name[IFNAMSIZ + 16];
	u32 mgmt_msix_idx;
	__be32 *counter_array; /* array of num_event_counters */
	dma_addr_t counter_array_bus;

	u16 num_event_counters;
	u16 tx_desc_cnt; /* num desc per ring */
	u16 rx_desc_cnt; /* num desc per ring */
	u16 tx_pages_per_qpl; /* tx buffer length */
	u16 rx_data_slot_cnt; /* rx buffer length */
	u64 max_registered_pages;
	u64 num_registered_pages; /* num pages registered with NIC */
	u32 rx_copybreak; /* copy packets smaller than this */
	u16 default_num_queues; /* default num queues to set up */

	struct gve_queue_config tx_cfg;
	struct gve_queue_config rx_cfg;
	struct gve_qpl_config qpl_cfg; /* map used QPL ids */
	u32 num_ntfy_blks; /* spilt between TX and RX so must be even */

	struct gve_registers __iomem *reg_bar0; /* see gve_register.h */
	__be32 __iomem *db_bar2; /* "array" of doorbells */
	u32 msg_enable;	/* level for netif* netdev print macros	*/
	struct pci_dev *pdev;

	/* metrics */
	u32 tx_timeo_cnt;

	/* Admin queue - see gve_adminq.h*/
	union gve_adminq_command *adminq;
	dma_addr_t adminq_bus_addr;
	u32 adminq_mask; /* masks prod_cnt to adminq size */
	u32 adminq_prod_cnt; /* free-running count of AQ cmds executed */
	u32 adminq_cmd_fail; /* free-running count of AQ cmds failed */
	u32 adminq_timeouts; /* free-running count of AQ cmds timeouts */
	/* free-running count of per AQ cmd executed */
	u32 adminq_describe_device_cnt;
	u32 adminq_cfg_device_resources_cnt;
	u32 adminq_register_page_list_cnt;
	u32 adminq_unregister_page_list_cnt;
	u32 adminq_create_tx_queue_cnt;
	u32 adminq_create_rx_queue_cnt;
	u32 adminq_destroy_tx_queue_cnt;
	u32 adminq_destroy_rx_queue_cnt;
	u32 adminq_dcfg_device_resources_cnt;
	u32 adminq_set_driver_parameter_cnt;
	u32 adminq_report_stats_cnt;
	u32 adminq_report_link_speed_cnt;
	u32 adminq_get_ptype_map_cnt;

	/* Global stats */
	u32 interface_up_cnt; /* count of times interface turned up since last reset */
	u32 interface_down_cnt; /* count of times interface turned down since last reset */
	u32 reset_cnt; /* count of reset */
	u32 page_alloc_fail; /* count of page alloc fails */
	u32 dma_mapping_error; /* count of dma mapping errors */
	u32 stats_report_trigger_cnt; /* count of device-requested stats-reports since last reset */
	struct workqueue_struct *gve_wq;
	struct work_struct service_task;
	struct work_struct stats_report_task;
	unsigned long service_task_flags;
	unsigned long state_flags;

	struct gve_stats_report *stats_report;
	u64 stats_report_len;
	dma_addr_t stats_report_bus; /* dma address for the stats report */
	unsigned long ethtool_flags;

	unsigned long stats_report_timer_period;
	struct timer_list stats_report_timer;

	/* Gvnic device link speed from hypervisor. */
	u64 link_speed;

	struct gve_options_dqo_rda options_dqo_rda;
	struct gve_ptype_lut *ptype_lut_dqo;

	/* Must be a power of two. */
	int data_buffer_size_dqo;

	enum gve_queue_format queue_format;
};

enum gve_service_task_flags_bit {
	GVE_PRIV_FLAGS_DO_RESET			= 1,
	GVE_PRIV_FLAGS_RESET_IN_PROGRESS	= 2,
	GVE_PRIV_FLAGS_PROBE_IN_PROGRESS	= 3,
	GVE_PRIV_FLAGS_DO_REPORT_STATS = 4,
};

enum gve_state_flags_bit {
	GVE_PRIV_FLAGS_ADMIN_QUEUE_OK		= 1,
	GVE_PRIV_FLAGS_DEVICE_RESOURCES_OK	= 2,
	GVE_PRIV_FLAGS_DEVICE_RINGS_OK		= 3,
	GVE_PRIV_FLAGS_NAPI_ENABLED		= 4,
};

enum gve_ethtool_flags_bit {
	GVE_PRIV_FLAGS_REPORT_STATS		= 0,
};

static inline bool gve_get_do_reset(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_DO_RESET, &priv->service_task_flags);
}

static inline void gve_set_do_reset(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_DO_RESET, &priv->service_task_flags);
}

static inline void gve_clear_do_reset(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_DO_RESET, &priv->service_task_flags);
}

static inline bool gve_get_reset_in_progress(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_RESET_IN_PROGRESS,
			&priv->service_task_flags);
}

static inline void gve_set_reset_in_progress(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_RESET_IN_PROGRESS, &priv->service_task_flags);
}

static inline void gve_clear_reset_in_progress(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_RESET_IN_PROGRESS, &priv->service_task_flags);
}

static inline bool gve_get_probe_in_progress(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_PROBE_IN_PROGRESS,
			&priv->service_task_flags);
}

static inline void gve_set_probe_in_progress(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_PROBE_IN_PROGRESS, &priv->service_task_flags);
}

static inline void gve_clear_probe_in_progress(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_PROBE_IN_PROGRESS, &priv->service_task_flags);
}

static inline bool gve_get_do_report_stats(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_DO_REPORT_STATS,
			&priv->service_task_flags);
}

static inline void gve_set_do_report_stats(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_DO_REPORT_STATS, &priv->service_task_flags);
}

static inline void gve_clear_do_report_stats(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_DO_REPORT_STATS, &priv->service_task_flags);
}

static inline bool gve_get_admin_queue_ok(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_ADMIN_QUEUE_OK, &priv->state_flags);
}

static inline void gve_set_admin_queue_ok(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_ADMIN_QUEUE_OK, &priv->state_flags);
}

static inline void gve_clear_admin_queue_ok(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_ADMIN_QUEUE_OK, &priv->state_flags);
}

static inline bool gve_get_device_resources_ok(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_DEVICE_RESOURCES_OK, &priv->state_flags);
}

static inline void gve_set_device_resources_ok(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_DEVICE_RESOURCES_OK, &priv->state_flags);
}

static inline void gve_clear_device_resources_ok(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_DEVICE_RESOURCES_OK, &priv->state_flags);
}

static inline bool gve_get_device_rings_ok(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_DEVICE_RINGS_OK, &priv->state_flags);
}

static inline void gve_set_device_rings_ok(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_DEVICE_RINGS_OK, &priv->state_flags);
}

static inline void gve_clear_device_rings_ok(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_DEVICE_RINGS_OK, &priv->state_flags);
}

static inline bool gve_get_napi_enabled(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_NAPI_ENABLED, &priv->state_flags);
}

static inline void gve_set_napi_enabled(struct gve_priv *priv)
{
	set_bit(GVE_PRIV_FLAGS_NAPI_ENABLED, &priv->state_flags);
}

static inline void gve_clear_napi_enabled(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_NAPI_ENABLED, &priv->state_flags);
}

static inline bool gve_get_report_stats(struct gve_priv *priv)
{
	return test_bit(GVE_PRIV_FLAGS_REPORT_STATS, &priv->ethtool_flags);
}

static inline void gve_clear_report_stats(struct gve_priv *priv)
{
	clear_bit(GVE_PRIV_FLAGS_REPORT_STATS, &priv->ethtool_flags);
}

/* Returns the address of the ntfy_blocks irq doorbell
 */
static inline __be32 __iomem *gve_irq_doorbell(struct gve_priv *priv,
					       struct gve_notify_block *block)
{
	return &priv->db_bar2[be32_to_cpu(block->irq_db_index)];
}

/* Returns the index into ntfy_blocks of the given tx ring's block
 */
static inline u32 gve_tx_idx_to_ntfy(struct gve_priv *priv, u32 queue_idx)
{
	return queue_idx;
}

/* Returns the index into ntfy_blocks of the given rx ring's block
 */
static inline u32 gve_rx_idx_to_ntfy(struct gve_priv *priv, u32 queue_idx)
{
	return (priv->num_ntfy_blks / 2) + queue_idx;
}

/* Returns the number of tx queue page lists
 */
static inline u32 gve_num_tx_qpls(struct gve_priv *priv)
{
	if (priv->queue_format != GVE_GQI_QPL_FORMAT)
		return 0;

	return priv->tx_cfg.num_queues;
}

/* Returns the number of rx queue page lists
 */
static inline u32 gve_num_rx_qpls(struct gve_priv *priv)
{
	if (priv->queue_format != GVE_GQI_QPL_FORMAT)
		return 0;

	return priv->rx_cfg.num_queues;
}

/* Returns a pointer to the next available tx qpl in the list of qpls
 */
static inline
struct gve_queue_page_list *gve_assign_tx_qpl(struct gve_priv *priv)
{
	int id = find_first_zero_bit(priv->qpl_cfg.qpl_id_map,
				     priv->qpl_cfg.qpl_map_size);

	/* we are out of tx qpls */
	if (id >= gve_num_tx_qpls(priv))
		return NULL;

	set_bit(id, priv->qpl_cfg.qpl_id_map);
	return &priv->qpls[id];
}

/* Returns a pointer to the next available rx qpl in the list of qpls
 */
static inline
struct gve_queue_page_list *gve_assign_rx_qpl(struct gve_priv *priv)
{
	int id = find_next_zero_bit(priv->qpl_cfg.qpl_id_map,
				    priv->qpl_cfg.qpl_map_size,
				    gve_num_tx_qpls(priv));

	/* we are out of rx qpls */
	if (id == gve_num_tx_qpls(priv) + gve_num_rx_qpls(priv))
		return NULL;

	set_bit(id, priv->qpl_cfg.qpl_id_map);
	return &priv->qpls[id];
}

/* Unassigns the qpl with the given id
 */
static inline void gve_unassign_qpl(struct gve_priv *priv, int id)
{
	clear_bit(id, priv->qpl_cfg.qpl_id_map);
}

/* Returns the correct dma direction for tx and rx qpls
 */
static inline enum dma_data_direction gve_qpl_dma_dir(struct gve_priv *priv,
						      int id)
{
	if (id < gve_num_tx_qpls(priv))
		return DMA_TO_DEVICE;
	else
		return DMA_FROM_DEVICE;
}

static inline bool gve_is_gqi(struct gve_priv *priv)
{
	return priv->queue_format == GVE_GQI_RDA_FORMAT ||
		priv->queue_format == GVE_GQI_QPL_FORMAT;
}

/* buffers */
int gve_alloc_page(struct gve_priv *priv, struct device *dev,
		   struct page **page, dma_addr_t *dma,
		   enum dma_data_direction);
void gve_free_page(struct device *dev, struct page *page, dma_addr_t dma,
		   enum dma_data_direction);
/* tx handling */
netdev_tx_t gve_tx(struct sk_buff *skb, struct net_device *dev);
bool gve_tx_poll(struct gve_notify_block *block, int budget);
int gve_tx_alloc_rings(struct gve_priv *priv);
void gve_tx_free_rings_gqi(struct gve_priv *priv);
u32 gve_tx_load_event_counter(struct gve_priv *priv,
			      struct gve_tx_ring *tx);
bool gve_tx_clean_pending(struct gve_priv *priv, struct gve_tx_ring *tx);
/* rx handling */
void gve_rx_write_doorbell(struct gve_priv *priv, struct gve_rx_ring *rx);
int gve_rx_poll(struct gve_notify_block *block, int budget);
bool gve_rx_work_pending(struct gve_rx_ring *rx);
int gve_rx_alloc_rings(struct gve_priv *priv);
void gve_rx_free_rings_gqi(struct gve_priv *priv);
/* Reset */
void gve_schedule_reset(struct gve_priv *priv);
int gve_reset(struct gve_priv *priv, bool attempt_teardown);
int gve_adjust_queues(struct gve_priv *priv,
		      struct gve_queue_config new_rx_config,
		      struct gve_queue_config new_tx_config);
/* report stats handling */
void gve_handle_report_stats(struct gve_priv *priv);
/* exported by ethtool.c */
extern const struct ethtool_ops gve_ethtool_ops;
/* needed by ethtool */
extern const char gve_version_str[];
#endif /* _GVE_H_ */

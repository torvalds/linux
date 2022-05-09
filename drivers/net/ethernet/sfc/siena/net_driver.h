/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2013 Solarflare Communications Inc.
 */

/* Common definitions for all Efx net driver code */

#ifndef EFX_NET_DRIVER_H
#define EFX_NET_DRIVER_H

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/timer.h>
#include <linux/mdio.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/vmalloc.h>
#include <linux/mtd/mtd.h>
#include <net/busy_poll.h>
#include <net/xdp.h>

#include "enum.h"
#include "bitfield.h"
#include "filter.h"

/**************************************************************************
 *
 * Build definitions
 *
 **************************************************************************/

#ifdef DEBUG
#define EFX_WARN_ON_ONCE_PARANOID(x) WARN_ON_ONCE(x)
#define EFX_WARN_ON_PARANOID(x) WARN_ON(x)
#else
#define EFX_WARN_ON_ONCE_PARANOID(x) do {} while (0)
#define EFX_WARN_ON_PARANOID(x) do {} while (0)
#endif

/**************************************************************************
 *
 * Efx data structures
 *
 **************************************************************************/

#define EFX_MAX_CHANNELS 32U
#define EFX_MAX_RX_QUEUES EFX_MAX_CHANNELS
#define EFX_EXTRA_CHANNEL_IOV	0
#define EFX_EXTRA_CHANNEL_PTP	1
#define EFX_MAX_EXTRA_CHANNELS	2U

/* Checksum generation is a per-queue option in hardware, so each
 * queue visible to the networking core is backed by two hardware TX
 * queues. */
#define EFX_MAX_TX_TC		2
#define EFX_MAX_CORE_TX_QUEUES	(EFX_MAX_TX_TC * EFX_MAX_CHANNELS)
#define EFX_TXQ_TYPE_OUTER_CSUM	1	/* Outer checksum offload */
#define EFX_TXQ_TYPE_INNER_CSUM	2	/* Inner checksum offload */
#define EFX_TXQ_TYPE_HIGHPRI	4	/* High-priority (for TC) */
#define EFX_TXQ_TYPES		8
/* HIGHPRI is Siena-only, and INNER_CSUM is EF10, so no need for both */
#define EFX_MAX_TXQ_PER_CHANNEL	4
#define EFX_MAX_TX_QUEUES	(EFX_MAX_TXQ_PER_CHANNEL * EFX_MAX_CHANNELS)

/* Maximum possible MTU the driver supports */
#define EFX_MAX_MTU (9 * 1024)

/* Minimum MTU, from RFC791 (IP) */
#define EFX_MIN_MTU 68

/* Maximum total header length for TSOv2 */
#define EFX_TSO2_MAX_HDRLEN	208

/* Size of an RX scatter buffer.  Small enough to pack 2 into a 4K page,
 * and should be a multiple of the cache line size.
 */
#define EFX_RX_USR_BUF_SIZE	(2048 - 256)

/* If possible, we should ensure cache line alignment at start and end
 * of every buffer.  Otherwise, we just need to ensure 4-byte
 * alignment of the network header.
 */
#if NET_IP_ALIGN == 0
#define EFX_RX_BUF_ALIGNMENT	L1_CACHE_BYTES
#else
#define EFX_RX_BUF_ALIGNMENT	4
#endif

/* Non-standard XDP_PACKET_HEADROOM and tailroom to satisfy XDP_REDIRECT and
 * still fit two standard MTU size packets into a single 4K page.
 */
#define EFX_XDP_HEADROOM	128
#define EFX_XDP_TAILROOM	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))

/* Forward declare Precision Time Protocol (PTP) support structure. */
struct efx_ptp_data;
struct hwtstamp_config;

struct efx_self_tests;

/**
 * struct efx_buffer - A general-purpose DMA buffer
 * @addr: host base address of the buffer
 * @dma_addr: DMA base address of the buffer
 * @len: Buffer length, in bytes
 *
 * The NIC uses these buffers for its interrupt status registers and
 * MAC stats dumps.
 */
struct efx_buffer {
	void *addr;
	dma_addr_t dma_addr;
	unsigned int len;
};

/**
 * struct efx_special_buffer - DMA buffer entered into buffer table
 * @buf: Standard &struct efx_buffer
 * @index: Buffer index within controller;s buffer table
 * @entries: Number of buffer table entries
 *
 * The NIC has a buffer table that maps buffers of size %EFX_BUF_SIZE.
 * Event and descriptor rings are addressed via one or more buffer
 * table entries (and so can be physically non-contiguous, although we
 * currently do not take advantage of that).  On Falcon and Siena we
 * have to take care of allocating and initialising the entries
 * ourselves.  On later hardware this is managed by the firmware and
 * @index and @entries are left as 0.
 */
struct efx_special_buffer {
	struct efx_buffer buf;
	unsigned int index;
	unsigned int entries;
};

/**
 * struct efx_tx_buffer - buffer state for a TX descriptor
 * @skb: When @flags & %EFX_TX_BUF_SKB, the associated socket buffer to be
 *	freed when descriptor completes
 * @xdpf: When @flags & %EFX_TX_BUF_XDP, the XDP frame information; its @data
 *	member is the associated buffer to drop a page reference on.
 * @option: When @flags & %EFX_TX_BUF_OPTION, an EF10-specific option
 *	descriptor.
 * @dma_addr: DMA address of the fragment.
 * @flags: Flags for allocation and DMA mapping type
 * @len: Length of this fragment.
 *	This field is zero when the queue slot is empty.
 * @unmap_len: Length of this fragment to unmap
 * @dma_offset: Offset of @dma_addr from the address of the backing DMA mapping.
 * Only valid if @unmap_len != 0.
 */
struct efx_tx_buffer {
	union {
		const struct sk_buff *skb;
		struct xdp_frame *xdpf;
	};
	union {
		efx_qword_t option;    /* EF10 */
		dma_addr_t dma_addr;
	};
	unsigned short flags;
	unsigned short len;
	unsigned short unmap_len;
	unsigned short dma_offset;
};
#define EFX_TX_BUF_CONT		1	/* not last descriptor of packet */
#define EFX_TX_BUF_SKB		2	/* buffer is last part of skb */
#define EFX_TX_BUF_MAP_SINGLE	8	/* buffer was mapped with dma_map_single() */
#define EFX_TX_BUF_OPTION	0x10	/* empty buffer for option descriptor */
#define EFX_TX_BUF_XDP		0x20	/* buffer was sent with XDP */
#define EFX_TX_BUF_TSO_V3	0x40	/* empty buffer for a TSO_V3 descriptor */

/**
 * struct efx_tx_queue - An Efx TX queue
 *
 * This is a ring buffer of TX fragments.
 * Since the TX completion path always executes on the same
 * CPU and the xmit path can operate on different CPUs,
 * performance is increased by ensuring that the completion
 * path and the xmit path operate on different cache lines.
 * This is particularly important if the xmit path is always
 * executing on one CPU which is different from the completion
 * path.  There is also a cache line for members which are
 * read but not written on the fast path.
 *
 * @efx: The associated Efx NIC
 * @queue: DMA queue number
 * @label: Label for TX completion events.
 *	Is our index within @channel->tx_queue array.
 * @type: configuration type of this TX queue.  A bitmask of %EFX_TXQ_TYPE_* flags.
 * @tso_version: Version of TSO in use for this queue.
 * @tso_encap: Is encapsulated TSO supported? Supported in TSOv2 on 8000 series.
 * @channel: The associated channel
 * @core_txq: The networking core TX queue structure
 * @buffer: The software buffer ring
 * @cb_page: Array of pages of copy buffers.  Carved up according to
 *	%EFX_TX_CB_ORDER into %EFX_TX_CB_SIZE-sized chunks.
 * @txd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @piobuf: PIO buffer region for this TX queue (shared with its partner).
 * @piobuf_offset: Buffer offset to be specified in PIO descriptors
 * @initialised: Has hardware queue been initialised?
 * @timestamping: Is timestamping enabled for this channel?
 * @xdp_tx: Is this an XDP tx queue?
 * @read_count: Current read pointer.
 *	This is the number of buffers that have been removed from both rings.
 * @old_write_count: The value of @write_count when last checked.
 *	This is here for performance reasons.  The xmit path will
 *	only get the up-to-date value of @write_count if this
 *	variable indicates that the queue is empty.  This is to
 *	avoid cache-line ping-pong between the xmit path and the
 *	completion path.
 * @merge_events: Number of TX merged completion events
 * @completed_timestamp_major: Top part of the most recent tx timestamp.
 * @completed_timestamp_minor: Low part of the most recent tx timestamp.
 * @insert_count: Current insert pointer
 *	This is the number of buffers that have been added to the
 *	software ring.
 * @write_count: Current write pointer
 *	This is the number of buffers that have been added to the
 *	hardware ring.
 * @packet_write_count: Completable write pointer
 *	This is the write pointer of the last packet written.
 *	Normally this will equal @write_count, but as option descriptors
 *	don't produce completion events, they won't update this.
 *	Filled in iff @efx->type->option_descriptors; only used for PIO.
 *	Thus, this is written and used on EF10, and neither on farch.
 * @old_read_count: The value of read_count when last checked.
 *	This is here for performance reasons.  The xmit path will
 *	only get the up-to-date value of read_count if this
 *	variable indicates that the queue is full.  This is to
 *	avoid cache-line ping-pong between the xmit path and the
 *	completion path.
 * @tso_bursts: Number of times TSO xmit invoked by kernel
 * @tso_long_headers: Number of packets with headers too long for standard
 *	blocks
 * @tso_packets: Number of packets via the TSO xmit path
 * @tso_fallbacks: Number of times TSO fallback used
 * @pushes: Number of times the TX push feature has been used
 * @pio_packets: Number of times the TX PIO feature has been used
 * @xmit_pending: Are any packets waiting to be pushed to the NIC
 * @cb_packets: Number of times the TX copybreak feature has been used
 * @notify_count: Count of notified descriptors to the NIC
 * @empty_read_count: If the completion path has seen the queue as empty
 *	and the transmission path has not yet checked this, the value of
 *	@read_count bitwise-added to %EFX_EMPTY_COUNT_VALID; otherwise 0.
 */
struct efx_tx_queue {
	/* Members which don't change on the fast path */
	struct efx_nic *efx ____cacheline_aligned_in_smp;
	unsigned int queue;
	unsigned int label;
	unsigned int type;
	unsigned int tso_version;
	bool tso_encap;
	struct efx_channel *channel;
	struct netdev_queue *core_txq;
	struct efx_tx_buffer *buffer;
	struct efx_buffer *cb_page;
	struct efx_special_buffer txd;
	unsigned int ptr_mask;
	void __iomem *piobuf;
	unsigned int piobuf_offset;
	bool initialised;
	bool timestamping;
	bool xdp_tx;

	/* Members used mainly on the completion path */
	unsigned int read_count ____cacheline_aligned_in_smp;
	unsigned int old_write_count;
	unsigned int merge_events;
	unsigned int bytes_compl;
	unsigned int pkts_compl;
	u32 completed_timestamp_major;
	u32 completed_timestamp_minor;

	/* Members used only on the xmit path */
	unsigned int insert_count ____cacheline_aligned_in_smp;
	unsigned int write_count;
	unsigned int packet_write_count;
	unsigned int old_read_count;
	unsigned int tso_bursts;
	unsigned int tso_long_headers;
	unsigned int tso_packets;
	unsigned int tso_fallbacks;
	unsigned int pushes;
	unsigned int pio_packets;
	bool xmit_pending;
	unsigned int cb_packets;
	unsigned int notify_count;
	/* Statistics to supplement MAC stats */
	unsigned long tx_packets;

	/* Members shared between paths and sometimes updated */
	unsigned int empty_read_count ____cacheline_aligned_in_smp;
#define EFX_EMPTY_COUNT_VALID 0x80000000
	atomic_t flush_outstanding;
};

#define EFX_TX_CB_ORDER	7
#define EFX_TX_CB_SIZE	(1 << EFX_TX_CB_ORDER) - NET_IP_ALIGN

/**
 * struct efx_rx_buffer - An Efx RX data buffer
 * @dma_addr: DMA base address of the buffer
 * @page: The associated page buffer.
 *	Will be %NULL if the buffer slot is currently free.
 * @page_offset: If pending: offset in @page of DMA base address.
 *	If completed: offset in @page of Ethernet header.
 * @len: If pending: length for DMA descriptor.
 *	If completed: received length, excluding hash prefix.
 * @flags: Flags for buffer and packet state.  These are only set on the
 *	first buffer of a scattered packet.
 */
struct efx_rx_buffer {
	dma_addr_t dma_addr;
	struct page *page;
	u16 page_offset;
	u16 len;
	u16 flags;
};
#define EFX_RX_BUF_LAST_IN_PAGE	0x0001
#define EFX_RX_PKT_CSUMMED	0x0002
#define EFX_RX_PKT_DISCARD	0x0004
#define EFX_RX_PKT_TCP		0x0040
#define EFX_RX_PKT_PREFIX_LEN	0x0080	/* length is in prefix only */
#define EFX_RX_PKT_CSUM_LEVEL	0x0200

/**
 * struct efx_rx_page_state - Page-based rx buffer state
 *
 * Inserted at the start of every page allocated for receive buffers.
 * Used to facilitate sharing dma mappings between recycled rx buffers
 * and those passed up to the kernel.
 *
 * @dma_addr: The dma address of this page.
 */
struct efx_rx_page_state {
	dma_addr_t dma_addr;

	unsigned int __pad[] ____cacheline_aligned;
};

/**
 * struct efx_rx_queue - An Efx RX queue
 * @efx: The associated Efx NIC
 * @core_index:  Index of network core RX queue.  Will be >= 0 iff this
 *	is associated with a real RX queue.
 * @buffer: The software buffer ring
 * @rxd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @refill_enabled: Enable refill whenever fill level is low
 * @flush_pending: Set when a RX flush is pending. Has the same lifetime as
 *	@rxq_flush_pending.
 * @added_count: Number of buffers added to the receive queue.
 * @notified_count: Number of buffers given to NIC (<= @added_count).
 * @removed_count: Number of buffers removed from the receive queue.
 * @scatter_n: Used by NIC specific receive code.
 * @scatter_len: Used by NIC specific receive code.
 * @page_ring: The ring to store DMA mapped pages for reuse.
 * @page_add: Counter to calculate the write pointer for the recycle ring.
 * @page_remove: Counter to calculate the read pointer for the recycle ring.
 * @page_recycle_count: The number of pages that have been recycled.
 * @page_recycle_failed: The number of pages that couldn't be recycled because
 *      the kernel still held a reference to them.
 * @page_recycle_full: The number of pages that were released because the
 *      recycle ring was full.
 * @page_ptr_mask: The number of pages in the RX recycle ring minus 1.
 * @max_fill: RX descriptor maximum fill level (<= ring size)
 * @fast_fill_trigger: RX descriptor fill level that will trigger a fast fill
 *	(<= @max_fill)
 * @min_fill: RX descriptor minimum non-zero fill level.
 *	This records the minimum fill level observed when a ring
 *	refill was triggered.
 * @recycle_count: RX buffer recycle counter.
 * @slow_fill: Timer used to defer efx_nic_generate_fill_event().
 * @xdp_rxq_info: XDP specific RX queue information.
 * @xdp_rxq_info_valid: Is xdp_rxq_info valid data?.
 */
struct efx_rx_queue {
	struct efx_nic *efx;
	int core_index;
	struct efx_rx_buffer *buffer;
	struct efx_special_buffer rxd;
	unsigned int ptr_mask;
	bool refill_enabled;
	bool flush_pending;

	unsigned int added_count;
	unsigned int notified_count;
	unsigned int removed_count;
	unsigned int scatter_n;
	unsigned int scatter_len;
	struct page **page_ring;
	unsigned int page_add;
	unsigned int page_remove;
	unsigned int page_recycle_count;
	unsigned int page_recycle_failed;
	unsigned int page_recycle_full;
	unsigned int page_ptr_mask;
	unsigned int max_fill;
	unsigned int fast_fill_trigger;
	unsigned int min_fill;
	unsigned int min_overfill;
	unsigned int recycle_count;
	struct timer_list slow_fill;
	unsigned int slow_fill_count;
	/* Statistics to supplement MAC stats */
	unsigned long rx_packets;
	struct xdp_rxq_info xdp_rxq_info;
	bool xdp_rxq_info_valid;
};

enum efx_sync_events_state {
	SYNC_EVENTS_DISABLED = 0,
	SYNC_EVENTS_QUIESCENT,
	SYNC_EVENTS_REQUESTED,
	SYNC_EVENTS_VALID,
};

/**
 * struct efx_channel - An Efx channel
 *
 * A channel comprises an event queue, at least one TX queue, at least
 * one RX queue, and an associated tasklet for processing the event
 * queue.
 *
 * @efx: Associated Efx NIC
 * @channel: Channel instance number
 * @type: Channel type definition
 * @eventq_init: Event queue initialised flag
 * @enabled: Channel enabled indicator
 * @irq: IRQ number (MSI and MSI-X only)
 * @irq_moderation_us: IRQ moderation value (in microseconds)
 * @napi_dev: Net device used with NAPI
 * @napi_str: NAPI control structure
 * @state: state for NAPI vs busy polling
 * @state_lock: lock protecting @state
 * @eventq: Event queue buffer
 * @eventq_mask: Event queue pointer mask
 * @eventq_read_ptr: Event queue read pointer
 * @event_test_cpu: Last CPU to handle interrupt or test event for this channel
 * @irq_count: Number of IRQs since last adaptive moderation decision
 * @irq_mod_score: IRQ moderation score
 * @rfs_filter_count: number of accelerated RFS filters currently in place;
 *	equals the count of @rps_flow_id slots filled
 * @rfs_last_expiry: value of jiffies last time some accelerated RFS filters
 *	were checked for expiry
 * @rfs_expire_index: next accelerated RFS filter ID to check for expiry
 * @n_rfs_succeeded: number of successful accelerated RFS filter insertions
 * @n_rfs_failed: number of failed accelerated RFS filter insertions
 * @filter_work: Work item for efx_filter_rfs_expire()
 * @rps_flow_id: Flow IDs of filters allocated for accelerated RFS,
 *      indexed by filter ID
 * @n_rx_tobe_disc: Count of RX_TOBE_DISC errors
 * @n_rx_ip_hdr_chksum_err: Count of RX IP header checksum errors
 * @n_rx_tcp_udp_chksum_err: Count of RX TCP and UDP checksum errors
 * @n_rx_mcast_mismatch: Count of unmatched multicast frames
 * @n_rx_frm_trunc: Count of RX_FRM_TRUNC errors
 * @n_rx_overlength: Count of RX_OVERLENGTH errors
 * @n_skbuff_leaks: Count of skbuffs leaked due to RX overrun
 * @n_rx_nodesc_trunc: Number of RX packets truncated and then dropped due to
 *	lack of descriptors
 * @n_rx_merge_events: Number of RX merged completion events
 * @n_rx_merge_packets: Number of RX packets completed by merged events
 * @n_rx_xdp_drops: Count of RX packets intentionally dropped due to XDP
 * @n_rx_xdp_bad_drops: Count of RX packets dropped due to XDP errors
 * @n_rx_xdp_tx: Count of RX packets retransmitted due to XDP
 * @n_rx_xdp_redirect: Count of RX packets redirected to a different NIC by XDP
 * @rx_pkt_n_frags: Number of fragments in next packet to be delivered by
 *	__efx_siena_rx_packet(), or zero if there is none
 * @rx_pkt_index: Ring index of first buffer for next packet to be delivered
 *	by __efx_siena_rx_packet(), if @rx_pkt_n_frags != 0
 * @rx_list: list of SKBs from current RX, awaiting processing
 * @rx_queue: RX queue for this channel
 * @tx_queue: TX queues for this channel
 * @tx_queue_by_type: pointers into @tx_queue, or %NULL, indexed by txq type
 * @sync_events_state: Current state of sync events on this channel
 * @sync_timestamp_major: Major part of the last ptp sync event
 * @sync_timestamp_minor: Minor part of the last ptp sync event
 */
struct efx_channel {
	struct efx_nic *efx;
	int channel;
	const struct efx_channel_type *type;
	bool eventq_init;
	bool enabled;
	int irq;
	unsigned int irq_moderation_us;
	struct net_device *napi_dev;
	struct napi_struct napi_str;
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned long busy_poll_state;
#endif
	struct efx_special_buffer eventq;
	unsigned int eventq_mask;
	unsigned int eventq_read_ptr;
	int event_test_cpu;

	unsigned int irq_count;
	unsigned int irq_mod_score;
#ifdef CONFIG_RFS_ACCEL
	unsigned int rfs_filter_count;
	unsigned int rfs_last_expiry;
	unsigned int rfs_expire_index;
	unsigned int n_rfs_succeeded;
	unsigned int n_rfs_failed;
	struct delayed_work filter_work;
#define RPS_FLOW_ID_INVALID 0xFFFFFFFF
	u32 *rps_flow_id;
#endif

	unsigned int n_rx_tobe_disc;
	unsigned int n_rx_ip_hdr_chksum_err;
	unsigned int n_rx_tcp_udp_chksum_err;
	unsigned int n_rx_outer_ip_hdr_chksum_err;
	unsigned int n_rx_outer_tcp_udp_chksum_err;
	unsigned int n_rx_inner_ip_hdr_chksum_err;
	unsigned int n_rx_inner_tcp_udp_chksum_err;
	unsigned int n_rx_eth_crc_err;
	unsigned int n_rx_mcast_mismatch;
	unsigned int n_rx_frm_trunc;
	unsigned int n_rx_overlength;
	unsigned int n_skbuff_leaks;
	unsigned int n_rx_nodesc_trunc;
	unsigned int n_rx_merge_events;
	unsigned int n_rx_merge_packets;
	unsigned int n_rx_xdp_drops;
	unsigned int n_rx_xdp_bad_drops;
	unsigned int n_rx_xdp_tx;
	unsigned int n_rx_xdp_redirect;

	unsigned int rx_pkt_n_frags;
	unsigned int rx_pkt_index;

	struct list_head *rx_list;

	struct efx_rx_queue rx_queue;
	struct efx_tx_queue tx_queue[EFX_MAX_TXQ_PER_CHANNEL];
	struct efx_tx_queue *tx_queue_by_type[EFX_TXQ_TYPES];

	enum efx_sync_events_state sync_events_state;
	u32 sync_timestamp_major;
	u32 sync_timestamp_minor;
};

/**
 * struct efx_msi_context - Context for each MSI
 * @efx: The associated NIC
 * @index: Index of the channel/IRQ
 * @name: Name of the channel/IRQ
 *
 * Unlike &struct efx_channel, this is never reallocated and is always
 * safe for the IRQ handler to access.
 */
struct efx_msi_context {
	struct efx_nic *efx;
	unsigned int index;
	char name[IFNAMSIZ + 6];
};

/**
 * struct efx_channel_type - distinguishes traffic and extra channels
 * @handle_no_channel: Handle failure to allocate an extra channel
 * @pre_probe: Set up extra state prior to initialisation
 * @post_remove: Tear down extra state after finalisation, if allocated.
 *	May be called on channels that have not been probed.
 * @get_name: Generate the channel's name (used for its IRQ handler)
 * @copy: Copy the channel state prior to reallocation.  May be %NULL if
 *	reallocation is not supported.
 * @receive_skb: Handle an skb ready to be passed to netif_receive_skb()
 * @want_txqs: Determine whether this channel should have TX queues
 *	created.  If %NULL, TX queues are not created.
 * @keep_eventq: Flag for whether event queue should be kept initialised
 *	while the device is stopped
 * @want_pio: Flag for whether PIO buffers should be linked to this
 *	channel's TX queues.
 */
struct efx_channel_type {
	void (*handle_no_channel)(struct efx_nic *);
	int (*pre_probe)(struct efx_channel *);
	void (*post_remove)(struct efx_channel *);
	void (*get_name)(struct efx_channel *, char *buf, size_t len);
	struct efx_channel *(*copy)(const struct efx_channel *);
	bool (*receive_skb)(struct efx_channel *, struct sk_buff *);
	bool (*want_txqs)(struct efx_channel *);
	bool keep_eventq;
	bool want_pio;
};

enum efx_led_mode {
	EFX_LED_OFF	= 0,
	EFX_LED_ON	= 1,
	EFX_LED_DEFAULT	= 2
};

#define STRING_TABLE_LOOKUP(val, member) \
	((val) < member ## _max) ? member ## _names[val] : "(invalid)"

extern const char *const efx_loopback_mode_names[];
extern const unsigned int efx_loopback_mode_max;
#define LOOPBACK_MODE(efx) \
	STRING_TABLE_LOOKUP((efx)->loopback_mode, efx_loopback_mode)

enum efx_int_mode {
	/* Be careful if altering to correct macro below */
	EFX_INT_MODE_MSIX = 0,
	EFX_INT_MODE_MSI = 1,
	EFX_INT_MODE_LEGACY = 2,
	EFX_INT_MODE_MAX	/* Insert any new items before this */
};
#define EFX_INT_MODE_USE_MSI(x) (((x)->interrupt_mode) <= EFX_INT_MODE_MSI)

enum nic_state {
	STATE_UNINIT = 0,	/* device being probed/removed or is frozen */
	STATE_READY = 1,	/* hardware ready and netdev registered */
	STATE_DISABLED = 2,	/* device disabled due to hardware errors */
	STATE_RECOVERY = 3,	/* device recovering from PCI error */
};

/* Forward declaration */
struct efx_nic;

/* Pseudo bit-mask flow control field */
#define EFX_FC_RX	FLOW_CTRL_RX
#define EFX_FC_TX	FLOW_CTRL_TX
#define EFX_FC_AUTO	4

/**
 * struct efx_link_state - Current state of the link
 * @up: Link is up
 * @fd: Link is full-duplex
 * @fc: Actual flow control flags
 * @speed: Link speed (Mbps)
 */
struct efx_link_state {
	bool up;
	bool fd;
	u8 fc;
	unsigned int speed;
};

static inline bool efx_link_state_equal(const struct efx_link_state *left,
					const struct efx_link_state *right)
{
	return left->up == right->up && left->fd == right->fd &&
		left->fc == right->fc && left->speed == right->speed;
}

/**
 * enum efx_phy_mode - PHY operating mode flags
 * @PHY_MODE_NORMAL: on and should pass traffic
 * @PHY_MODE_TX_DISABLED: on with TX disabled
 * @PHY_MODE_LOW_POWER: set to low power through MDIO
 * @PHY_MODE_OFF: switched off through external control
 * @PHY_MODE_SPECIAL: on but will not pass traffic
 */
enum efx_phy_mode {
	PHY_MODE_NORMAL		= 0,
	PHY_MODE_TX_DISABLED	= 1,
	PHY_MODE_LOW_POWER	= 2,
	PHY_MODE_OFF		= 4,
	PHY_MODE_SPECIAL	= 8,
};

static inline bool efx_phy_mode_disabled(enum efx_phy_mode mode)
{
	return !!(mode & ~PHY_MODE_TX_DISABLED);
}

/**
 * struct efx_hw_stat_desc - Description of a hardware statistic
 * @name: Name of the statistic as visible through ethtool, or %NULL if
 *	it should not be exposed
 * @dma_width: Width in bits (0 for non-DMA statistics)
 * @offset: Offset within stats (ignored for non-DMA statistics)
 */
struct efx_hw_stat_desc {
	const char *name;
	u16 dma_width;
	u16 offset;
};

/* Number of bits used in a multicast filter hash address */
#define EFX_MCAST_HASH_BITS 8

/* Number of (single-bit) entries in a multicast filter hash */
#define EFX_MCAST_HASH_ENTRIES (1 << EFX_MCAST_HASH_BITS)

/* An Efx multicast filter hash */
union efx_multicast_hash {
	u8 byte[EFX_MCAST_HASH_ENTRIES / 8];
	efx_oword_t oword[EFX_MCAST_HASH_ENTRIES / sizeof(efx_oword_t) / 8];
};

struct vfdi_status;

/* The reserved RSS context value */
#define EFX_MCDI_RSS_CONTEXT_INVALID	0xffffffff
/**
 * struct efx_rss_context - A user-defined RSS context for filtering
 * @list: node of linked list on which this struct is stored
 * @context_id: the RSS_CONTEXT_ID returned by MC firmware, or
 *	%EFX_MCDI_RSS_CONTEXT_INVALID if this context is not present on the NIC.
 *	For Siena, 0 if RSS is active, else %EFX_MCDI_RSS_CONTEXT_INVALID.
 * @user_id: the rss_context ID exposed to userspace over ethtool.
 * @rx_hash_udp_4tuple: UDP 4-tuple hashing enabled
 * @rx_hash_key: Toeplitz hash key for this RSS context
 * @indir_table: Indirection table for this RSS context
 */
struct efx_rss_context {
	struct list_head list;
	u32 context_id;
	u32 user_id;
	bool rx_hash_udp_4tuple;
	u8 rx_hash_key[40];
	u32 rx_indir_table[128];
};

#ifdef CONFIG_RFS_ACCEL
/* Order of these is important, since filter_id >= %EFX_ARFS_FILTER_ID_PENDING
 * is used to test if filter does or will exist.
 */
#define EFX_ARFS_FILTER_ID_PENDING	-1
#define EFX_ARFS_FILTER_ID_ERROR	-2
#define EFX_ARFS_FILTER_ID_REMOVING	-3
/**
 * struct efx_arfs_rule - record of an ARFS filter and its IDs
 * @node: linkage into hash table
 * @spec: details of the filter (used as key for hash table).  Use efx->type to
 *	determine which member to use.
 * @rxq_index: channel to which the filter will steer traffic.
 * @arfs_id: filter ID which was returned to ARFS
 * @filter_id: index in software filter table.  May be
 *	%EFX_ARFS_FILTER_ID_PENDING if filter was not inserted yet,
 *	%EFX_ARFS_FILTER_ID_ERROR if filter insertion failed, or
 *	%EFX_ARFS_FILTER_ID_REMOVING if expiry is currently removing the filter.
 */
struct efx_arfs_rule {
	struct hlist_node node;
	struct efx_filter_spec spec;
	u16 rxq_index;
	u16 arfs_id;
	s32 filter_id;
};

/* Size chosen so that the table is one page (4kB) */
#define EFX_ARFS_HASH_TABLE_SIZE	512

/**
 * struct efx_async_filter_insertion - Request to asynchronously insert a filter
 * @net_dev: Reference to the netdevice
 * @spec: The filter to insert
 * @work: Workitem for this request
 * @rxq_index: Identifies the channel for which this request was made
 * @flow_id: Identifies the kernel-side flow for which this request was made
 */
struct efx_async_filter_insertion {
	struct net_device *net_dev;
	struct efx_filter_spec spec;
	struct work_struct work;
	u16 rxq_index;
	u32 flow_id;
};

/* Maximum number of ARFS workitems that may be in flight on an efx_nic */
#define EFX_RPS_MAX_IN_FLIGHT	8
#endif /* CONFIG_RFS_ACCEL */

enum efx_xdp_tx_queues_mode {
	EFX_XDP_TX_QUEUES_DEDICATED,	/* one queue per core, locking not needed */
	EFX_XDP_TX_QUEUES_SHARED,	/* each queue used by more than 1 core */
	EFX_XDP_TX_QUEUES_BORROWED	/* queues borrowed from net stack */
};

/**
 * struct efx_nic - an Efx NIC
 * @name: Device name (net device name or bus id before net device registered)
 * @pci_dev: The PCI device
 * @node: List node for maintaning primary/secondary function lists
 * @primary: &struct efx_nic instance for the primary function of this
 *	controller.  May be the same structure, and may be %NULL if no
 *	primary function is bound.  Serialised by rtnl_lock.
 * @secondary_list: List of &struct efx_nic instances for the secondary PCI
 *	functions of the controller, if this is for the primary function.
 *	Serialised by rtnl_lock.
 * @type: Controller type attributes
 * @legacy_irq: IRQ number
 * @workqueue: Workqueue for port reconfigures and the HW monitor.
 *	Work items do not hold and must not acquire RTNL.
 * @workqueue_name: Name of workqueue
 * @reset_work: Scheduled reset workitem
 * @membase_phys: Memory BAR value as physical address
 * @membase: Memory BAR value
 * @vi_stride: step between per-VI registers / memory regions
 * @interrupt_mode: Interrupt mode
 * @timer_quantum_ns: Interrupt timer quantum, in nanoseconds
 * @timer_max_ns: Interrupt timer maximum value, in nanoseconds
 * @irq_rx_adaptive: Adaptive IRQ moderation enabled for RX event queues
 * @irqs_hooked: Channel interrupts are hooked
 * @irq_rx_mod_step_us: Step size for IRQ moderation for RX event queues
 * @irq_rx_moderation_us: IRQ moderation time for RX event queues
 * @msg_enable: Log message enable flags
 * @state: Device state number (%STATE_*). Serialised by the rtnl_lock.
 * @reset_pending: Bitmask for pending resets
 * @tx_queue: TX DMA queues
 * @rx_queue: RX DMA queues
 * @channel: Channels
 * @msi_context: Context for each MSI
 * @extra_channel_types: Types of extra (non-traffic) channels that
 *	should be allocated for this NIC
 * @xdp_tx_queue_count: Number of entries in %xdp_tx_queues.
 * @xdp_tx_queues: Array of pointers to tx queues used for XDP transmit.
 * @xdp_txq_queues_mode: XDP TX queues sharing strategy.
 * @rxq_entries: Size of receive queues requested by user.
 * @txq_entries: Size of transmit queues requested by user.
 * @txq_stop_thresh: TX queue fill level at or above which we stop it.
 * @txq_wake_thresh: TX queue fill level at or below which we wake it.
 * @tx_dc_base: Base qword address in SRAM of TX queue descriptor caches
 * @rx_dc_base: Base qword address in SRAM of RX queue descriptor caches
 * @sram_lim_qw: Qword address limit of SRAM
 * @next_buffer_table: First available buffer table id
 * @n_channels: Number of channels in use
 * @n_rx_channels: Number of channels used for RX (= number of RX queues)
 * @n_tx_channels: Number of channels used for TX
 * @n_extra_tx_channels: Number of extra channels with TX queues
 * @tx_queues_per_channel: number of TX queues probed on each channel
 * @n_xdp_channels: Number of channels used for XDP TX
 * @xdp_channel_offset: Offset of zeroth channel used for XPD TX.
 * @xdp_tx_per_channel: Max number of TX queues on an XDP TX channel.
 * @rx_ip_align: RX DMA address offset to have IP header aligned in
 *	in accordance with NET_IP_ALIGN
 * @rx_dma_len: Current maximum RX DMA length
 * @rx_buffer_order: Order (log2) of number of pages for each RX buffer
 * @rx_buffer_truesize: Amortised allocation size of an RX buffer,
 *	for use in sk_buff::truesize
 * @rx_prefix_size: Size of RX prefix before packet data
 * @rx_packet_hash_offset: Offset of RX flow hash from start of packet data
 *	(valid only if @rx_prefix_size != 0; always negative)
 * @rx_packet_len_offset: Offset of RX packet length from start of packet data
 *	(valid only for NICs that set %EFX_RX_PKT_PREFIX_LEN; always negative)
 * @rx_packet_ts_offset: Offset of timestamp from start of packet data
 *	(valid only if channel->sync_timestamps_enabled; always negative)
 * @rx_scatter: Scatter mode enabled for receives
 * @rss_context: Main RSS context.  Its @list member is the head of the list of
 *	RSS contexts created by user requests
 * @rss_lock: Protects custom RSS context software state in @rss_context.list
 * @vport_id: The function's vport ID, only relevant for PFs
 * @int_error_count: Number of internal errors seen recently
 * @int_error_expire: Time at which error count will be expired
 * @must_realloc_vis: Flag: VIs have yet to be reallocated after MC reboot
 * @irq_soft_enabled: Are IRQs soft-enabled? If not, IRQ handler will
 *	acknowledge but do nothing else.
 * @irq_status: Interrupt status buffer
 * @irq_zero_count: Number of legacy IRQs seen with queue flags == 0
 * @irq_level: IRQ level/index for IRQs not triggered by an event queue
 * @selftest_work: Work item for asynchronous self-test
 * @mtd_list: List of MTDs attached to the NIC
 * @nic_data: Hardware dependent state
 * @mcdi: Management-Controller-to-Driver Interface state
 * @mac_lock: MAC access lock. Protects @port_enabled, @phy_mode,
 *	efx_monitor() and efx_siena_reconfigure_port()
 * @port_enabled: Port enabled indicator.
 *	Serialises efx_siena_stop_all(), efx_siena_start_all(),
 *	efx_monitor() and efx_mac_work() with kernel interfaces.
 *	Safe to read under any one of the rtnl_lock, mac_lock, or netif_tx_lock,
 *	but all three must be held to modify it.
 * @port_initialized: Port initialized?
 * @net_dev: Operating system network device. Consider holding the rtnl lock
 * @fixed_features: Features which cannot be turned off
 * @num_mac_stats: Number of MAC stats reported by firmware (MAC_STATS_NUM_STATS
 *	field of %MC_CMD_GET_CAPABILITIES_V4 response, or %MC_CMD_MAC_NSTATS)
 * @stats_buffer: DMA buffer for statistics
 * @phy_type: PHY type
 * @phy_data: PHY private data (including PHY-specific stats)
 * @mdio: PHY MDIO interface
 * @mdio_bus: PHY MDIO bus ID (only used by Siena)
 * @phy_mode: PHY operating mode. Serialised by @mac_lock.
 * @link_advertising: Autonegotiation advertising flags
 * @fec_config: Forward Error Correction configuration flags.  For bit positions
 *	see &enum ethtool_fec_config_bits.
 * @link_state: Current state of the link
 * @n_link_state_changes: Number of times the link has changed state
 * @unicast_filter: Flag for Falcon-arch simple unicast filter.
 *	Protected by @mac_lock.
 * @multicast_hash: Multicast hash table for Falcon-arch.
 *	Protected by @mac_lock.
 * @wanted_fc: Wanted flow control flags
 * @fc_disable: When non-zero flow control is disabled. Typically used to
 *	ensure that network back pressure doesn't delay dma queue flushes.
 *	Serialised by the rtnl lock.
 * @mac_work: Work item for changing MAC promiscuity and multicast hash
 * @loopback_mode: Loopback status
 * @loopback_modes: Supported loopback mode bitmask
 * @loopback_selftest: Offline self-test private state
 * @xdp_prog: Current XDP programme for this interface
 * @filter_sem: Filter table rw_semaphore, protects existence of @filter_state
 * @filter_state: Architecture-dependent filter table state
 * @rps_mutex: Protects RPS state of all channels
 * @rps_slot_map: bitmap of in-flight entries in @rps_slot
 * @rps_slot: array of ARFS insertion requests for efx_filter_rfs_work()
 * @rps_hash_lock: Protects ARFS filter mapping state (@rps_hash_table and
 *	@rps_next_id).
 * @rps_hash_table: Mapping between ARFS filters and their various IDs
 * @rps_next_id: next arfs_id for an ARFS filter
 * @active_queues: Count of RX and TX queues that haven't been flushed and drained.
 * @rxq_flush_pending: Count of number of receive queues that need to be flushed.
 *	Decremented when the efx_flush_rx_queue() is called.
 * @rxq_flush_outstanding: Count of number of RX flushes started but not yet
 *	completed (either success or failure). Not used when MCDI is used to
 *	flush receive queues.
 * @flush_wq: wait queue used by efx_nic_flush_queues() to wait for flush completions.
 * @vf_count: Number of VFs intended to be enabled.
 * @vf_init_count: Number of VFs that have been fully initialised.
 * @vi_scale: log2 number of vnics per VF.
 * @ptp_data: PTP state data
 * @ptp_warned: has this NIC seen and warned about unexpected PTP events?
 * @vpd_sn: Serial number read from VPD
 * @xdp_rxq_info_failed: Have any of the rx queues failed to initialise their
 *      xdp_rxq_info structures?
 * @netdev_notifier: Netdevice notifier.
 * @mem_bar: The BAR that is mapped into membase.
 * @reg_base: Offset from the start of the bar to the function control window.
 * @monitor_work: Hardware monitor workitem
 * @biu_lock: BIU (bus interface unit) lock
 * @last_irq_cpu: Last CPU to handle a possible test interrupt.  This
 *	field is used by efx_test_interrupts() to verify that an
 *	interrupt has occurred.
 * @stats_lock: Statistics update lock. Must be held when calling
 *	efx_nic_type::{update,start,stop}_stats.
 * @n_rx_noskb_drops: Count of RX packets dropped due to failure to allocate an skb
 *
 * This is stored in the private area of the &struct net_device.
 */
struct efx_nic {
	/* The following fields should be written very rarely */

	char name[IFNAMSIZ];
	struct list_head node;
	struct efx_nic *primary;
	struct list_head secondary_list;
	struct pci_dev *pci_dev;
	unsigned int port_num;
	const struct efx_nic_type *type;
	int legacy_irq;
	bool eeh_disabled_legacy_irq;
	struct workqueue_struct *workqueue;
	char workqueue_name[16];
	struct work_struct reset_work;
	resource_size_t membase_phys;
	void __iomem *membase;

	unsigned int vi_stride;

	enum efx_int_mode interrupt_mode;
	unsigned int timer_quantum_ns;
	unsigned int timer_max_ns;
	bool irq_rx_adaptive;
	bool irqs_hooked;
	unsigned int irq_mod_step_us;
	unsigned int irq_rx_moderation_us;
	u32 msg_enable;

	enum nic_state state;
	unsigned long reset_pending;

	struct efx_channel *channel[EFX_MAX_CHANNELS];
	struct efx_msi_context msi_context[EFX_MAX_CHANNELS];
	const struct efx_channel_type *
	extra_channel_type[EFX_MAX_EXTRA_CHANNELS];

	unsigned int xdp_tx_queue_count;
	struct efx_tx_queue **xdp_tx_queues;
	enum efx_xdp_tx_queues_mode xdp_txq_queues_mode;

	unsigned rxq_entries;
	unsigned txq_entries;
	unsigned int txq_stop_thresh;
	unsigned int txq_wake_thresh;

	unsigned tx_dc_base;
	unsigned rx_dc_base;
	unsigned sram_lim_qw;
	unsigned next_buffer_table;

	unsigned int max_channels;
	unsigned int max_vis;
	unsigned int max_tx_channels;
	unsigned n_channels;
	unsigned n_rx_channels;
	unsigned rss_spread;
	unsigned tx_channel_offset;
	unsigned n_tx_channels;
	unsigned n_extra_tx_channels;
	unsigned int tx_queues_per_channel;
	unsigned int n_xdp_channels;
	unsigned int xdp_channel_offset;
	unsigned int xdp_tx_per_channel;
	unsigned int rx_ip_align;
	unsigned int rx_dma_len;
	unsigned int rx_buffer_order;
	unsigned int rx_buffer_truesize;
	unsigned int rx_page_buf_step;
	unsigned int rx_bufs_per_page;
	unsigned int rx_pages_per_batch;
	unsigned int rx_prefix_size;
	int rx_packet_hash_offset;
	int rx_packet_len_offset;
	int rx_packet_ts_offset;
	bool rx_scatter;
	struct efx_rss_context rss_context;
	struct mutex rss_lock;
	u32 vport_id;

	unsigned int_error_count;
	unsigned long int_error_expire;

	bool must_realloc_vis;
	bool irq_soft_enabled;
	struct efx_buffer irq_status;
	unsigned irq_zero_count;
	unsigned irq_level;
	struct delayed_work selftest_work;

#ifdef CONFIG_SFC_MTD
	struct list_head mtd_list;
#endif

	void *nic_data;
	struct efx_mcdi_data *mcdi;

	struct mutex mac_lock;
	struct work_struct mac_work;
	bool port_enabled;

	bool mc_bist_for_other_fn;
	bool port_initialized;
	struct net_device *net_dev;

	netdev_features_t fixed_features;

	u16 num_mac_stats;
	struct efx_buffer stats_buffer;
	u64 rx_nodesc_drops_total;
	u64 rx_nodesc_drops_while_down;
	bool rx_nodesc_drops_prev_state;

	unsigned int phy_type;
	void *phy_data;
	struct mdio_if_info mdio;
	unsigned int mdio_bus;
	enum efx_phy_mode phy_mode;

	__ETHTOOL_DECLARE_LINK_MODE_MASK(link_advertising);
	u32 fec_config;
	struct efx_link_state link_state;
	unsigned int n_link_state_changes;

	bool unicast_filter;
	union efx_multicast_hash multicast_hash;
	u8 wanted_fc;
	unsigned fc_disable;

	atomic_t rx_reset;
	enum efx_loopback_mode loopback_mode;
	u64 loopback_modes;

	void *loopback_selftest;
	/* We access loopback_selftest immediately before running XDP,
	 * so we want them next to each other.
	 */
	struct bpf_prog __rcu *xdp_prog;

	struct rw_semaphore filter_sem;
	void *filter_state;
#ifdef CONFIG_RFS_ACCEL
	struct mutex rps_mutex;
	unsigned long rps_slot_map;
	struct efx_async_filter_insertion rps_slot[EFX_RPS_MAX_IN_FLIGHT];
	spinlock_t rps_hash_lock;
	struct hlist_head *rps_hash_table;
	u32 rps_next_id;
#endif

	atomic_t active_queues;
	atomic_t rxq_flush_pending;
	atomic_t rxq_flush_outstanding;
	wait_queue_head_t flush_wq;

#ifdef CONFIG_SFC_SRIOV
	unsigned vf_count;
	unsigned vf_init_count;
	unsigned vi_scale;
#endif

	struct efx_ptp_data *ptp_data;
	bool ptp_warned;

	char *vpd_sn;
	bool xdp_rxq_info_failed;

	struct notifier_block netdev_notifier;

	unsigned int mem_bar;
	u32 reg_base;

	/* The following fields may be written more often */

	struct delayed_work monitor_work ____cacheline_aligned_in_smp;
	spinlock_t biu_lock;
	int last_irq_cpu;
	spinlock_t stats_lock;
	atomic_t n_rx_noskb_drops;
};

static inline int efx_dev_registered(struct efx_nic *efx)
{
	return efx->net_dev->reg_state == NETREG_REGISTERED;
}

static inline unsigned int efx_port_num(struct efx_nic *efx)
{
	return efx->port_num;
}

struct efx_mtd_partition {
	struct list_head node;
	struct mtd_info mtd;
	const char *dev_type_name;
	const char *type_name;
	char name[IFNAMSIZ + 20];
};

struct efx_udp_tunnel {
#define TUNNEL_ENCAP_UDP_PORT_ENTRY_INVALID	0xffff
	u16 type; /* TUNNEL_ENCAP_UDP_PORT_ENTRY_foo, see mcdi_pcol.h */
	__be16 port;
};

/**
 * struct efx_nic_type - Efx device type definition
 * @mem_bar: Get the memory BAR
 * @mem_map_size: Get memory BAR mapped size
 * @probe: Probe the controller
 * @remove: Free resources allocated by probe()
 * @init: Initialise the controller
 * @dimension_resources: Dimension controller resources (buffer table,
 *	and VIs once the available interrupt resources are clear)
 * @fini: Shut down the controller
 * @monitor: Periodic function for polling link state and hardware monitor
 * @map_reset_reason: Map ethtool reset reason to a reset method
 * @map_reset_flags: Map ethtool reset flags to a reset method, if possible
 * @reset: Reset the controller hardware and possibly the PHY.  This will
 *	be called while the controller is uninitialised.
 * @probe_port: Probe the MAC and PHY
 * @remove_port: Free resources allocated by probe_port()
 * @handle_global_event: Handle a "global" event (may be %NULL)
 * @fini_dmaq: Flush and finalise DMA queues (RX and TX queues)
 * @prepare_flush: Prepare the hardware for flushing the DMA queues
 *	(for Falcon architecture)
 * @finish_flush: Clean up after flushing the DMA queues (for Falcon
 *	architecture)
 * @prepare_flr: Prepare for an FLR
 * @finish_flr: Clean up after an FLR
 * @describe_stats: Describe statistics for ethtool
 * @update_stats: Update statistics not provided by event handling.
 *	Either argument may be %NULL.
 * @update_stats_atomic: Update statistics while in atomic context, if that
 *	is more limiting than @update_stats.  Otherwise, leave %NULL and
 *	driver core will call @update_stats.
 * @start_stats: Start the regular fetching of statistics
 * @pull_stats: Pull stats from the NIC and wait until they arrive.
 * @stop_stats: Stop the regular fetching of statistics
 * @push_irq_moderation: Apply interrupt moderation value
 * @reconfigure_port: Push loopback/power/txdis changes to the MAC and PHY
 * @prepare_enable_fc_tx: Prepare MAC to enable pause frame TX (may be %NULL)
 * @reconfigure_mac: Push MAC address, MTU, flow control and filter settings
 *	to the hardware.  Serialised by the mac_lock.
 * @check_mac_fault: Check MAC fault state. True if fault present.
 * @get_wol: Get WoL configuration from driver state
 * @set_wol: Push WoL configuration to the NIC
 * @resume_wol: Synchronise WoL state between driver and MC (e.g. after resume)
 * @get_fec_stats: Get standard FEC statistics.
 * @test_chip: Test registers.  May use efx_farch_test_registers(), and is
 *	expected to reset the NIC.
 * @test_nvram: Test validity of NVRAM contents
 * @mcdi_request: Send an MCDI request with the given header and SDU.
 *	The SDU length may be any value from 0 up to the protocol-
 *	defined maximum, but its buffer will be padded to a multiple
 *	of 4 bytes.
 * @mcdi_poll_response: Test whether an MCDI response is available.
 * @mcdi_read_response: Read the MCDI response PDU.  The offset will
 *	be a multiple of 4.  The length may not be, but the buffer
 *	will be padded so it is safe to round up.
 * @mcdi_poll_reboot: Test whether the MCDI has rebooted.  If so,
 *	return an appropriate error code for aborting any current
 *	request; otherwise return 0.
 * @irq_enable_master: Enable IRQs on the NIC.  Each event queue must
 *	be separately enabled after this.
 * @irq_test_generate: Generate a test IRQ
 * @irq_disable_non_ev: Disable non-event IRQs on the NIC.  Each event
 *	queue must be separately disabled before this.
 * @irq_handle_msi: Handle MSI for a channel.  The @dev_id argument is
 *	a pointer to the &struct efx_msi_context for the channel.
 * @irq_handle_legacy: Handle legacy interrupt.  The @dev_id argument
 *	is a pointer to the &struct efx_nic.
 * @tx_probe: Allocate resources for TX queue (and select TXQ type)
 * @tx_init: Initialise TX queue on the NIC
 * @tx_remove: Free resources for TX queue
 * @tx_write: Write TX descriptors and doorbell
 * @tx_enqueue: Add an SKB to TX queue
 * @rx_push_rss_config: Write RSS hash key and indirection table to the NIC
 * @rx_pull_rss_config: Read RSS hash key and indirection table back from the NIC
 * @rx_push_rss_context_config: Write RSS hash key and indirection table for
 *	user RSS context to the NIC
 * @rx_pull_rss_context_config: Read RSS hash key and indirection table for user
 *	RSS context back from the NIC
 * @rx_probe: Allocate resources for RX queue
 * @rx_init: Initialise RX queue on the NIC
 * @rx_remove: Free resources for RX queue
 * @rx_write: Write RX descriptors and doorbell
 * @rx_defer_refill: Generate a refill reminder event
 * @rx_packet: Receive the queued RX buffer on a channel
 * @rx_buf_hash_valid: Determine whether the RX prefix contains a valid hash
 * @ev_probe: Allocate resources for event queue
 * @ev_init: Initialise event queue on the NIC
 * @ev_fini: Deinitialise event queue on the NIC
 * @ev_remove: Free resources for event queue
 * @ev_process: Process events for a queue, up to the given NAPI quota
 * @ev_read_ack: Acknowledge read events on a queue, rearming its IRQ
 * @ev_test_generate: Generate a test event
 * @filter_table_probe: Probe filter capabilities and set up filter software state
 * @filter_table_restore: Restore filters removed from hardware
 * @filter_table_remove: Remove filters from hardware and tear down software state
 * @filter_update_rx_scatter: Update filters after change to rx scatter setting
 * @filter_insert: add or replace a filter
 * @filter_remove_safe: remove a filter by ID, carefully
 * @filter_get_safe: retrieve a filter by ID, carefully
 * @filter_clear_rx: Remove all RX filters whose priority is less than or
 *	equal to the given priority and is not %EFX_FILTER_PRI_AUTO
 * @filter_count_rx_used: Get the number of filters in use at a given priority
 * @filter_get_rx_id_limit: Get maximum value of a filter id, plus 1
 * @filter_get_rx_ids: Get list of RX filters at a given priority
 * @filter_rfs_expire_one: Consider expiring a filter inserted for RFS.
 *	This must check whether the specified table entry is used by RFS
 *	and that rps_may_expire_flow() returns true for it.
 * @mtd_probe: Probe and add MTD partitions associated with this net device,
 *	 using efx_siena_mtd_add()
 * @mtd_rename: Set an MTD partition name using the net device name
 * @mtd_read: Read from an MTD partition
 * @mtd_erase: Erase part of an MTD partition
 * @mtd_write: Write to an MTD partition
 * @mtd_sync: Wait for write-back to complete on MTD partition.  This
 *	also notifies the driver that a writer has finished using this
 *	partition.
 * @ptp_write_host_time: Send host time to MC as part of sync protocol
 * @ptp_set_ts_sync_events: Enable or disable sync events for inline RX
 *	timestamping, possibly only temporarily for the purposes of a reset.
 * @ptp_set_ts_config: Set hardware timestamp configuration.  The flags
 *	and tx_type will already have been validated but this operation
 *	must validate and update rx_filter.
 * @get_phys_port_id: Get the underlying physical port id.
 * @set_mac_address: Set the MAC address of the device
 * @tso_versions: Returns mask of firmware-assisted TSO versions supported.
 *	If %NULL, then device does not support any TSO version.
 * @udp_tnl_push_ports: Push the list of UDP tunnel ports to the NIC if required.
 * @udp_tnl_has_port: Check if a port has been added as UDP tunnel
 * @print_additional_fwver: Dump NIC-specific additional FW version info
 * @sensor_event: Handle a sensor event from MCDI
 * @rx_recycle_ring_size: Size of the RX recycle ring
 * @revision: Hardware architecture revision
 * @txd_ptr_tbl_base: TX descriptor ring base address
 * @rxd_ptr_tbl_base: RX descriptor ring base address
 * @buf_tbl_base: Buffer table base address
 * @evq_ptr_tbl_base: Event queue pointer table base address
 * @evq_rptr_tbl_base: Event queue read-pointer table base address
 * @max_dma_mask: Maximum possible DMA mask
 * @rx_prefix_size: Size of RX prefix before packet data
 * @rx_hash_offset: Offset of RX flow hash within prefix
 * @rx_ts_offset: Offset of timestamp within prefix
 * @rx_buffer_padding: Size of padding at end of RX packet
 * @can_rx_scatter: NIC is able to scatter packets to multiple buffers
 * @always_rx_scatter: NIC will always scatter packets to multiple buffers
 * @option_descriptors: NIC supports TX option descriptors
 * @min_interrupt_mode: Lowest capability interrupt mode supported
 *	from &enum efx_int_mode.
 * @timer_period_max: Maximum period of interrupt timer (in ticks)
 * @offload_features: net_device feature flags for protocol offload
 *	features implemented in hardware
 * @mcdi_max_ver: Maximum MCDI version supported
 * @hwtstamp_filters: Mask of hardware timestamp filter types supported
 */
struct efx_nic_type {
	bool is_vf;
	unsigned int (*mem_bar)(struct efx_nic *efx);
	unsigned int (*mem_map_size)(struct efx_nic *efx);
	int (*probe)(struct efx_nic *efx);
	void (*remove)(struct efx_nic *efx);
	int (*init)(struct efx_nic *efx);
	int (*dimension_resources)(struct efx_nic *efx);
	void (*fini)(struct efx_nic *efx);
	void (*monitor)(struct efx_nic *efx);
	enum reset_type (*map_reset_reason)(enum reset_type reason);
	int (*map_reset_flags)(u32 *flags);
	int (*reset)(struct efx_nic *efx, enum reset_type method);
	int (*probe_port)(struct efx_nic *efx);
	void (*remove_port)(struct efx_nic *efx);
	bool (*handle_global_event)(struct efx_channel *channel, efx_qword_t *);
	int (*fini_dmaq)(struct efx_nic *efx);
	void (*prepare_flush)(struct efx_nic *efx);
	void (*finish_flush)(struct efx_nic *efx);
	void (*prepare_flr)(struct efx_nic *efx);
	void (*finish_flr)(struct efx_nic *efx);
	size_t (*describe_stats)(struct efx_nic *efx, u8 *names);
	size_t (*update_stats)(struct efx_nic *efx, u64 *full_stats,
			       struct rtnl_link_stats64 *core_stats);
	size_t (*update_stats_atomic)(struct efx_nic *efx, u64 *full_stats,
				      struct rtnl_link_stats64 *core_stats);
	void (*start_stats)(struct efx_nic *efx);
	void (*pull_stats)(struct efx_nic *efx);
	void (*stop_stats)(struct efx_nic *efx);
	void (*push_irq_moderation)(struct efx_channel *channel);
	int (*reconfigure_port)(struct efx_nic *efx);
	void (*prepare_enable_fc_tx)(struct efx_nic *efx);
	int (*reconfigure_mac)(struct efx_nic *efx, bool mtu_only);
	bool (*check_mac_fault)(struct efx_nic *efx);
	void (*get_wol)(struct efx_nic *efx, struct ethtool_wolinfo *wol);
	int (*set_wol)(struct efx_nic *efx, u32 type);
	void (*resume_wol)(struct efx_nic *efx);
	void (*get_fec_stats)(struct efx_nic *efx,
			      struct ethtool_fec_stats *fec_stats);
	unsigned int (*check_caps)(const struct efx_nic *efx,
				   u8 flag,
				   u32 offset);
	int (*test_chip)(struct efx_nic *efx, struct efx_self_tests *tests);
	int (*test_nvram)(struct efx_nic *efx);
	void (*mcdi_request)(struct efx_nic *efx,
			     const efx_dword_t *hdr, size_t hdr_len,
			     const efx_dword_t *sdu, size_t sdu_len);
	bool (*mcdi_poll_response)(struct efx_nic *efx);
	void (*mcdi_read_response)(struct efx_nic *efx, efx_dword_t *pdu,
				   size_t pdu_offset, size_t pdu_len);
	int (*mcdi_poll_reboot)(struct efx_nic *efx);
	void (*mcdi_reboot_detected)(struct efx_nic *efx);
	void (*irq_enable_master)(struct efx_nic *efx);
	int (*irq_test_generate)(struct efx_nic *efx);
	void (*irq_disable_non_ev)(struct efx_nic *efx);
	irqreturn_t (*irq_handle_msi)(int irq, void *dev_id);
	irqreturn_t (*irq_handle_legacy)(int irq, void *dev_id);
	int (*tx_probe)(struct efx_tx_queue *tx_queue);
	void (*tx_init)(struct efx_tx_queue *tx_queue);
	void (*tx_remove)(struct efx_tx_queue *tx_queue);
	void (*tx_write)(struct efx_tx_queue *tx_queue);
	netdev_tx_t (*tx_enqueue)(struct efx_tx_queue *tx_queue, struct sk_buff *skb);
	unsigned int (*tx_limit_len)(struct efx_tx_queue *tx_queue,
				     dma_addr_t dma_addr, unsigned int len);
	int (*rx_push_rss_config)(struct efx_nic *efx, bool user,
				  const u32 *rx_indir_table, const u8 *key);
	int (*rx_pull_rss_config)(struct efx_nic *efx);
	int (*rx_push_rss_context_config)(struct efx_nic *efx,
					  struct efx_rss_context *ctx,
					  const u32 *rx_indir_table,
					  const u8 *key);
	int (*rx_pull_rss_context_config)(struct efx_nic *efx,
					  struct efx_rss_context *ctx);
	void (*rx_restore_rss_contexts)(struct efx_nic *efx);
	int (*rx_probe)(struct efx_rx_queue *rx_queue);
	void (*rx_init)(struct efx_rx_queue *rx_queue);
	void (*rx_remove)(struct efx_rx_queue *rx_queue);
	void (*rx_write)(struct efx_rx_queue *rx_queue);
	void (*rx_defer_refill)(struct efx_rx_queue *rx_queue);
	void (*rx_packet)(struct efx_channel *channel);
	bool (*rx_buf_hash_valid)(const u8 *prefix);
	int (*ev_probe)(struct efx_channel *channel);
	int (*ev_init)(struct efx_channel *channel);
	void (*ev_fini)(struct efx_channel *channel);
	void (*ev_remove)(struct efx_channel *channel);
	int (*ev_process)(struct efx_channel *channel, int quota);
	void (*ev_read_ack)(struct efx_channel *channel);
	void (*ev_test_generate)(struct efx_channel *channel);
	int (*filter_table_probe)(struct efx_nic *efx);
	void (*filter_table_restore)(struct efx_nic *efx);
	void (*filter_table_remove)(struct efx_nic *efx);
	void (*filter_update_rx_scatter)(struct efx_nic *efx);
	s32 (*filter_insert)(struct efx_nic *efx,
			     struct efx_filter_spec *spec, bool replace);
	int (*filter_remove_safe)(struct efx_nic *efx,
				  enum efx_filter_priority priority,
				  u32 filter_id);
	int (*filter_get_safe)(struct efx_nic *efx,
			       enum efx_filter_priority priority,
			       u32 filter_id, struct efx_filter_spec *);
	int (*filter_clear_rx)(struct efx_nic *efx,
			       enum efx_filter_priority priority);
	u32 (*filter_count_rx_used)(struct efx_nic *efx,
				    enum efx_filter_priority priority);
	u32 (*filter_get_rx_id_limit)(struct efx_nic *efx);
	s32 (*filter_get_rx_ids)(struct efx_nic *efx,
				 enum efx_filter_priority priority,
				 u32 *buf, u32 size);
#ifdef CONFIG_RFS_ACCEL
	bool (*filter_rfs_expire_one)(struct efx_nic *efx, u32 flow_id,
				      unsigned int index);
#endif
#ifdef CONFIG_SFC_MTD
	int (*mtd_probe)(struct efx_nic *efx);
	void (*mtd_rename)(struct efx_mtd_partition *part);
	int (*mtd_read)(struct mtd_info *mtd, loff_t start, size_t len,
			size_t *retlen, u8 *buffer);
	int (*mtd_erase)(struct mtd_info *mtd, loff_t start, size_t len);
	int (*mtd_write)(struct mtd_info *mtd, loff_t start, size_t len,
			 size_t *retlen, const u8 *buffer);
	int (*mtd_sync)(struct mtd_info *mtd);
#endif
	void (*ptp_write_host_time)(struct efx_nic *efx, u32 host_time);
	int (*ptp_set_ts_sync_events)(struct efx_nic *efx, bool en, bool temp);
	int (*ptp_set_ts_config)(struct efx_nic *efx,
				 struct hwtstamp_config *init);
	int (*sriov_configure)(struct efx_nic *efx, int num_vfs);
	int (*vlan_rx_add_vid)(struct efx_nic *efx, __be16 proto, u16 vid);
	int (*vlan_rx_kill_vid)(struct efx_nic *efx, __be16 proto, u16 vid);
	int (*get_phys_port_id)(struct efx_nic *efx,
				struct netdev_phys_item_id *ppid);
	int (*sriov_init)(struct efx_nic *efx);
	void (*sriov_fini)(struct efx_nic *efx);
	bool (*sriov_wanted)(struct efx_nic *efx);
	void (*sriov_reset)(struct efx_nic *efx);
	void (*sriov_flr)(struct efx_nic *efx, unsigned vf_i);
	int (*sriov_set_vf_mac)(struct efx_nic *efx, int vf_i, const u8 *mac);
	int (*sriov_set_vf_vlan)(struct efx_nic *efx, int vf_i, u16 vlan,
				 u8 qos);
	int (*sriov_set_vf_spoofchk)(struct efx_nic *efx, int vf_i,
				     bool spoofchk);
	int (*sriov_get_vf_config)(struct efx_nic *efx, int vf_i,
				   struct ifla_vf_info *ivi);
	int (*sriov_set_vf_link_state)(struct efx_nic *efx, int vf_i,
				       int link_state);
	int (*vswitching_probe)(struct efx_nic *efx);
	int (*vswitching_restore)(struct efx_nic *efx);
	void (*vswitching_remove)(struct efx_nic *efx);
	int (*get_mac_address)(struct efx_nic *efx, unsigned char *perm_addr);
	int (*set_mac_address)(struct efx_nic *efx);
	u32 (*tso_versions)(struct efx_nic *efx);
	int (*udp_tnl_push_ports)(struct efx_nic *efx);
	bool (*udp_tnl_has_port)(struct efx_nic *efx, __be16 port);
	size_t (*print_additional_fwver)(struct efx_nic *efx, char *buf,
					 size_t len);
	void (*sensor_event)(struct efx_nic *efx, efx_qword_t *ev);
	unsigned int (*rx_recycle_ring_size)(const struct efx_nic *efx);

	int revision;
	unsigned int txd_ptr_tbl_base;
	unsigned int rxd_ptr_tbl_base;
	unsigned int buf_tbl_base;
	unsigned int evq_ptr_tbl_base;
	unsigned int evq_rptr_tbl_base;
	u64 max_dma_mask;
	unsigned int rx_prefix_size;
	unsigned int rx_hash_offset;
	unsigned int rx_ts_offset;
	unsigned int rx_buffer_padding;
	bool can_rx_scatter;
	bool always_rx_scatter;
	bool option_descriptors;
	unsigned int min_interrupt_mode;
	unsigned int timer_period_max;
	netdev_features_t offload_features;
	int mcdi_max_ver;
	unsigned int max_rx_ip_filters;
	u32 hwtstamp_filters;
	unsigned int rx_hash_key_size;
};

/**************************************************************************
 *
 * Prototypes and inline functions
 *
 *************************************************************************/

static inline struct efx_channel *
efx_get_channel(struct efx_nic *efx, unsigned index)
{
	EFX_WARN_ON_ONCE_PARANOID(index >= efx->n_channels);
	return efx->channel[index];
}

/* Iterate over all used channels */
#define efx_for_each_channel(_channel, _efx)				\
	for (_channel = (_efx)->channel[0];				\
	     _channel;							\
	     _channel = (_channel->channel + 1 < (_efx)->n_channels) ?	\
		     (_efx)->channel[_channel->channel + 1] : NULL)

/* Iterate over all used channels in reverse */
#define efx_for_each_channel_rev(_channel, _efx)			\
	for (_channel = (_efx)->channel[(_efx)->n_channels - 1];	\
	     _channel;							\
	     _channel = _channel->channel ?				\
		     (_efx)->channel[_channel->channel - 1] : NULL)

static inline struct efx_channel *
efx_get_tx_channel(struct efx_nic *efx, unsigned int index)
{
	EFX_WARN_ON_ONCE_PARANOID(index >= efx->n_tx_channels);
	return efx->channel[efx->tx_channel_offset + index];
}

static inline struct efx_channel *
efx_get_xdp_channel(struct efx_nic *efx, unsigned int index)
{
	EFX_WARN_ON_ONCE_PARANOID(index >= efx->n_xdp_channels);
	return efx->channel[efx->xdp_channel_offset + index];
}

static inline bool efx_channel_is_xdp_tx(struct efx_channel *channel)
{
	return channel->channel - channel->efx->xdp_channel_offset <
	       channel->efx->n_xdp_channels;
}

static inline bool efx_channel_has_tx_queues(struct efx_channel *channel)
{
	return true;
}

static inline unsigned int efx_channel_num_tx_queues(struct efx_channel *channel)
{
	if (efx_channel_is_xdp_tx(channel))
		return channel->efx->xdp_tx_per_channel;
	return channel->efx->tx_queues_per_channel;
}

static inline struct efx_tx_queue *
efx_channel_get_tx_queue(struct efx_channel *channel, unsigned int type)
{
	EFX_WARN_ON_ONCE_PARANOID(type >= EFX_TXQ_TYPES);
	return channel->tx_queue_by_type[type];
}

static inline struct efx_tx_queue *
efx_get_tx_queue(struct efx_nic *efx, unsigned int index, unsigned int type)
{
	struct efx_channel *channel = efx_get_tx_channel(efx, index);

	return efx_channel_get_tx_queue(channel, type);
}

/* Iterate over all TX queues belonging to a channel */
#define efx_for_each_channel_tx_queue(_tx_queue, _channel)		\
	if (!efx_channel_has_tx_queues(_channel))			\
		;							\
	else								\
		for (_tx_queue = (_channel)->tx_queue;			\
		     _tx_queue < (_channel)->tx_queue +			\
				 efx_channel_num_tx_queues(_channel);		\
		     _tx_queue++)

static inline bool efx_channel_has_rx_queue(struct efx_channel *channel)
{
	return channel->rx_queue.core_index >= 0;
}

static inline struct efx_rx_queue *
efx_channel_get_rx_queue(struct efx_channel *channel)
{
	EFX_WARN_ON_ONCE_PARANOID(!efx_channel_has_rx_queue(channel));
	return &channel->rx_queue;
}

/* Iterate over all RX queues belonging to a channel */
#define efx_for_each_channel_rx_queue(_rx_queue, _channel)		\
	if (!efx_channel_has_rx_queue(_channel))			\
		;							\
	else								\
		for (_rx_queue = &(_channel)->rx_queue;			\
		     _rx_queue;						\
		     _rx_queue = NULL)

static inline struct efx_channel *
efx_rx_queue_channel(struct efx_rx_queue *rx_queue)
{
	return container_of(rx_queue, struct efx_channel, rx_queue);
}

static inline int efx_rx_queue_index(struct efx_rx_queue *rx_queue)
{
	return efx_rx_queue_channel(rx_queue)->channel;
}

/* Returns a pointer to the specified receive buffer in the RX
 * descriptor queue.
 */
static inline struct efx_rx_buffer *efx_rx_buffer(struct efx_rx_queue *rx_queue,
						  unsigned int index)
{
	return &rx_queue->buffer[index];
}

static inline struct efx_rx_buffer *
efx_rx_buf_next(struct efx_rx_queue *rx_queue, struct efx_rx_buffer *rx_buf)
{
	if (unlikely(rx_buf == efx_rx_buffer(rx_queue, rx_queue->ptr_mask)))
		return efx_rx_buffer(rx_queue, 0);
	else
		return rx_buf + 1;
}

/**
 * EFX_MAX_FRAME_LEN - calculate maximum frame length
 *
 * This calculates the maximum frame length that will be used for a
 * given MTU.  The frame length will be equal to the MTU plus a
 * constant amount of header space and padding.  This is the quantity
 * that the net driver will program into the MAC as the maximum frame
 * length.
 *
 * The 10G MAC requires 8-byte alignment on the frame
 * length, so we round up to the nearest 8.
 *
 * Re-clocking by the XGXS on RX can reduce an IPG to 32 bits (half an
 * XGMII cycle).  If the frame length reaches the maximum value in the
 * same cycle, the XMAC can miss the IPG altogether.  We work around
 * this by adding a further 16 bytes.
 */
#define EFX_FRAME_PAD	16
#define EFX_MAX_FRAME_LEN(mtu) \
	(ALIGN(((mtu) + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN + EFX_FRAME_PAD), 8))

static inline bool efx_xmit_with_hwtstamp(struct sk_buff *skb)
{
	return skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP;
}
static inline void efx_xmit_hwtstamp_pending(struct sk_buff *skb)
{
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
}

/* Get the max fill level of the TX queues on this channel */
static inline unsigned int
efx_channel_tx_fill_level(struct efx_channel *channel)
{
	struct efx_tx_queue *tx_queue;
	unsigned int fill_level = 0;

	efx_for_each_channel_tx_queue(tx_queue, channel)
		fill_level = max(fill_level,
				 tx_queue->insert_count - tx_queue->read_count);

	return fill_level;
}

/* Conservative approximation of efx_channel_tx_fill_level using cached value */
static inline unsigned int
efx_channel_tx_old_fill_level(struct efx_channel *channel)
{
	struct efx_tx_queue *tx_queue;
	unsigned int fill_level = 0;

	efx_for_each_channel_tx_queue(tx_queue, channel)
		fill_level = max(fill_level,
				 tx_queue->insert_count - tx_queue->old_read_count);

	return fill_level;
}

/* Get all supported features.
 * If a feature is not fixed, it is present in hw_features.
 * If a feature is fixed, it does not present in hw_features, but
 * always in features.
 */
static inline netdev_features_t efx_supported_features(const struct efx_nic *efx)
{
	const struct net_device *net_dev = efx->net_dev;

	return net_dev->features | net_dev->hw_features;
}

/* Get the current TX queue insert index. */
static inline unsigned int
efx_tx_queue_get_insert_index(const struct efx_tx_queue *tx_queue)
{
	return tx_queue->insert_count & tx_queue->ptr_mask;
}

/* Get a TX buffer. */
static inline struct efx_tx_buffer *
__efx_tx_queue_get_insert_buffer(const struct efx_tx_queue *tx_queue)
{
	return &tx_queue->buffer[efx_tx_queue_get_insert_index(tx_queue)];
}

/* Get a TX buffer, checking it's not currently in use. */
static inline struct efx_tx_buffer *
efx_tx_queue_get_insert_buffer(const struct efx_tx_queue *tx_queue)
{
	struct efx_tx_buffer *buffer =
		__efx_tx_queue_get_insert_buffer(tx_queue);

	EFX_WARN_ON_ONCE_PARANOID(buffer->len);
	EFX_WARN_ON_ONCE_PARANOID(buffer->flags);
	EFX_WARN_ON_ONCE_PARANOID(buffer->unmap_len);

	return buffer;
}

#endif /* EFX_NET_DRIVER_H */

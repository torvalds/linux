/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
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
#include <linux/vmalloc.h>
#include <linux/i2c.h>

#include "enum.h"
#include "bitfield.h"

/**************************************************************************
 *
 * Build definitions
 *
 **************************************************************************/

#define EFX_DRIVER_VERSION	"3.2"

#ifdef DEBUG
#define EFX_BUG_ON_PARANOID(x) BUG_ON(x)
#define EFX_WARN_ON_PARANOID(x) WARN_ON(x)
#else
#define EFX_BUG_ON_PARANOID(x) do {} while (0)
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
#define EFX_TXQ_TYPE_OFFLOAD	1	/* flag */
#define EFX_TXQ_TYPE_HIGHPRI	2	/* flag */
#define EFX_TXQ_TYPES		4
#define EFX_MAX_TX_QUEUES	(EFX_TXQ_TYPES * EFX_MAX_CHANNELS)

/* Maximum possible MTU the driver supports */
#define EFX_MAX_MTU (9 * 1024)

/* Size of an RX scatter buffer.  Small enough to pack 2 into a 4K page. */
#define EFX_RX_USR_BUF_SIZE 1824

/* Forward declare Precision Time Protocol (PTP) support structure. */
struct efx_ptp_data;

struct efx_self_tests;

/**
 * struct efx_special_buffer - An Efx special buffer
 * @addr: CPU base address of the buffer
 * @dma_addr: DMA base address of the buffer
 * @len: Buffer length, in bytes
 * @index: Buffer index within controller;s buffer table
 * @entries: Number of buffer table entries
 *
 * Special buffers are used for the event queues and the TX and RX
 * descriptor queues for each channel.  They are *not* used for the
 * actual transmit and receive buffers.
 */
struct efx_special_buffer {
	void *addr;
	dma_addr_t dma_addr;
	unsigned int len;
	unsigned int index;
	unsigned int entries;
};

/**
 * struct efx_tx_buffer - buffer state for a TX descriptor
 * @skb: When @flags & %EFX_TX_BUF_SKB, the associated socket buffer to be
 *	freed when descriptor completes
 * @heap_buf: When @flags & %EFX_TX_BUF_HEAP, the associated heap buffer to be
 *	freed when descriptor completes.
 * @dma_addr: DMA address of the fragment.
 * @flags: Flags for allocation and DMA mapping type
 * @len: Length of this fragment.
 *	This field is zero when the queue slot is empty.
 * @unmap_len: Length of this fragment to unmap
 */
struct efx_tx_buffer {
	union {
		const struct sk_buff *skb;
		void *heap_buf;
	};
	dma_addr_t dma_addr;
	unsigned short flags;
	unsigned short len;
	unsigned short unmap_len;
};
#define EFX_TX_BUF_CONT		1	/* not last descriptor of packet */
#define EFX_TX_BUF_SKB		2	/* buffer is last part of skb */
#define EFX_TX_BUF_HEAP		4	/* buffer was allocated with kmalloc() */
#define EFX_TX_BUF_MAP_SINGLE	8	/* buffer was mapped with dma_map_single() */

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
 * @channel: The associated channel
 * @core_txq: The networking core TX queue structure
 * @buffer: The software buffer ring
 * @tsoh_page: Array of pages of TSO header buffers
 * @txd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @initialised: Has hardware queue been initialised?
 * @read_count: Current read pointer.
 *	This is the number of buffers that have been removed from both rings.
 * @old_write_count: The value of @write_count when last checked.
 *	This is here for performance reasons.  The xmit path will
 *	only get the up-to-date value of @write_count if this
 *	variable indicates that the queue is empty.  This is to
 *	avoid cache-line ping-pong between the xmit path and the
 *	completion path.
 * @insert_count: Current insert pointer
 *	This is the number of buffers that have been added to the
 *	software ring.
 * @write_count: Current write pointer
 *	This is the number of buffers that have been added to the
 *	hardware ring.
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
 * @pushes: Number of times the TX push feature has been used
 * @empty_read_count: If the completion path has seen the queue as empty
 *	and the transmission path has not yet checked this, the value of
 *	@read_count bitwise-added to %EFX_EMPTY_COUNT_VALID; otherwise 0.
 */
struct efx_tx_queue {
	/* Members which don't change on the fast path */
	struct efx_nic *efx ____cacheline_aligned_in_smp;
	unsigned queue;
	struct efx_channel *channel;
	struct netdev_queue *core_txq;
	struct efx_tx_buffer *buffer;
	struct efx_buffer *tsoh_page;
	struct efx_special_buffer txd;
	unsigned int ptr_mask;
	bool initialised;

	/* Members used mainly on the completion path */
	unsigned int read_count ____cacheline_aligned_in_smp;
	unsigned int old_write_count;

	/* Members used only on the xmit path */
	unsigned int insert_count ____cacheline_aligned_in_smp;
	unsigned int write_count;
	unsigned int old_read_count;
	unsigned int tso_bursts;
	unsigned int tso_long_headers;
	unsigned int tso_packets;
	unsigned int pushes;

	/* Members shared between paths and sometimes updated */
	unsigned int empty_read_count ____cacheline_aligned_in_smp;
#define EFX_EMPTY_COUNT_VALID 0x80000000
	atomic_t flush_outstanding;
};

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
#define EFX_RX_PKT_CSUMMED	0x0002
#define EFX_RX_PKT_DISCARD	0x0004

/**
 * struct efx_rx_page_state - Page-based rx buffer state
 *
 * Inserted at the start of every page allocated for receive buffers.
 * Used to facilitate sharing dma mappings between recycled rx buffers
 * and those passed up to the kernel.
 *
 * @refcnt: Number of struct efx_rx_buffer's referencing this page.
 *	When refcnt falls to zero, the page is unmapped for dma
 * @dma_addr: The dma address of this page.
 */
struct efx_rx_page_state {
	unsigned refcnt;
	dma_addr_t dma_addr;

	unsigned int __pad[0] ____cacheline_aligned;
};

/**
 * struct efx_rx_queue - An Efx RX queue
 * @efx: The associated Efx NIC
 * @core_index:  Index of network core RX queue.  Will be >= 0 iff this
 *	is associated with a real RX queue.
 * @buffer: The software buffer ring
 * @rxd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @enabled: Receive queue enabled indicator.
 * @flush_pending: Set when a RX flush is pending. Has the same lifetime as
 *	@rxq_flush_pending.
 * @added_count: Number of buffers added to the receive queue.
 * @notified_count: Number of buffers given to NIC (<= @added_count).
 * @removed_count: Number of buffers removed from the receive queue.
 * @scatter_n: Number of buffers used by current packet
 * @max_fill: RX descriptor maximum fill level (<= ring size)
 * @fast_fill_trigger: RX descriptor fill level that will trigger a fast fill
 *	(<= @max_fill)
 * @min_fill: RX descriptor minimum non-zero fill level.
 *	This records the minimum fill level observed when a ring
 *	refill was triggered.
 * @slow_fill: Timer used to defer efx_nic_generate_fill_event().
 */
struct efx_rx_queue {
	struct efx_nic *efx;
	int core_index;
	struct efx_rx_buffer *buffer;
	struct efx_special_buffer rxd;
	unsigned int ptr_mask;
	bool enabled;
	bool flush_pending;

	unsigned int added_count;
	unsigned int notified_count;
	unsigned int removed_count;
	unsigned int scatter_n;
	unsigned int max_fill;
	unsigned int fast_fill_trigger;
	unsigned int min_fill;
	unsigned int min_overfill;
	struct timer_list slow_fill;
	unsigned int slow_fill_count;
};

/**
 * struct efx_buffer - An Efx general-purpose buffer
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


enum efx_rx_alloc_method {
	RX_ALLOC_METHOD_AUTO = 0,
	RX_ALLOC_METHOD_SKB = 1,
	RX_ALLOC_METHOD_PAGE = 2,
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
 * @enabled: Channel enabled indicator
 * @irq: IRQ number (MSI and MSI-X only)
 * @irq_moderation: IRQ moderation value (in hardware ticks)
 * @napi_dev: Net device used with NAPI
 * @napi_str: NAPI control structure
 * @work_pending: Is work pending via NAPI?
 * @eventq: Event queue buffer
 * @eventq_mask: Event queue pointer mask
 * @eventq_read_ptr: Event queue read pointer
 * @event_test_cpu: Last CPU to handle interrupt or test event for this channel
 * @irq_count: Number of IRQs since last adaptive moderation decision
 * @irq_mod_score: IRQ moderation score
 * @n_rx_tobe_disc: Count of RX_TOBE_DISC errors
 * @n_rx_ip_hdr_chksum_err: Count of RX IP header checksum errors
 * @n_rx_tcp_udp_chksum_err: Count of RX TCP and UDP checksum errors
 * @n_rx_mcast_mismatch: Count of unmatched multicast frames
 * @n_rx_frm_trunc: Count of RX_FRM_TRUNC errors
 * @n_rx_overlength: Count of RX_OVERLENGTH errors
 * @n_skbuff_leaks: Count of skbuffs leaked due to RX overrun
 * @n_rx_nodesc_trunc: Number of RX packets truncated and then dropped due to
 *	lack of descriptors
 * @rx_pkt_n_frags: Number of fragments in next packet to be delivered by
 *	__efx_rx_packet(), or zero if there is none
 * @rx_pkt_index: Ring index of first buffer for next packet to be delivered
 *	by __efx_rx_packet(), if @rx_pkt_n_frags != 0
 * @rx_queue: RX queue for this channel
 * @tx_queue: TX queues for this channel
 */
struct efx_channel {
	struct efx_nic *efx;
	int channel;
	const struct efx_channel_type *type;
	bool enabled;
	int irq;
	unsigned int irq_moderation;
	struct net_device *napi_dev;
	struct napi_struct napi_str;
	bool work_pending;
	struct efx_special_buffer eventq;
	unsigned int eventq_mask;
	unsigned int eventq_read_ptr;
	int event_test_cpu;

	unsigned int irq_count;
	unsigned int irq_mod_score;
#ifdef CONFIG_RFS_ACCEL
	unsigned int rfs_filters_added;
#endif

	unsigned n_rx_tobe_disc;
	unsigned n_rx_ip_hdr_chksum_err;
	unsigned n_rx_tcp_udp_chksum_err;
	unsigned n_rx_mcast_mismatch;
	unsigned n_rx_frm_trunc;
	unsigned n_rx_overlength;
	unsigned n_skbuff_leaks;
	unsigned int n_rx_nodesc_trunc;

	unsigned int rx_pkt_n_frags;
	unsigned int rx_pkt_index;

	struct efx_rx_queue rx_queue;
	struct efx_tx_queue tx_queue[EFX_TXQ_TYPES];
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
 * @keep_eventq: Flag for whether event queue should be kept initialised
 *	while the device is stopped
 */
struct efx_channel_type {
	void (*handle_no_channel)(struct efx_nic *);
	int (*pre_probe)(struct efx_channel *);
	void (*post_remove)(struct efx_channel *);
	void (*get_name)(struct efx_channel *, char *buf, size_t len);
	struct efx_channel *(*copy)(const struct efx_channel *);
	bool (*receive_skb)(struct efx_channel *, struct sk_buff *);
	bool keep_eventq;
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

extern const char *const efx_reset_type_names[];
extern const unsigned int efx_reset_type_max;
#define RESET_TYPE(type) \
	STRING_TABLE_LOOKUP(type, efx_reset_type)

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

/*
 * Alignment of page-allocated RX buffers
 *
 * Controls the number of bytes inserted at the start of an RX buffer.
 * This is the equivalent of NET_IP_ALIGN [which controls the alignment
 * of the skb->head for hardware DMA].
 */
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
#define EFX_PAGE_IP_ALIGN 0
#else
#define EFX_PAGE_IP_ALIGN NET_IP_ALIGN
#endif

/*
 * Alignment of the skb->head which wraps a page-allocated RX buffer
 *
 * The skb allocated to wrap an rx_buffer can have this alignment. Since
 * the data is memcpy'd from the rx_buf, it does not need to be equal to
 * EFX_PAGE_IP_ALIGN.
 */
#define EFX_PAGE_SKB_ALIGN 2

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
 * struct efx_phy_operations - Efx PHY operations table
 * @probe: Probe PHY and initialise efx->mdio.mode_support, efx->mdio.mmds,
 *	efx->loopback_modes.
 * @init: Initialise PHY
 * @fini: Shut down PHY
 * @reconfigure: Reconfigure PHY (e.g. for new link parameters)
 * @poll: Update @link_state and report whether it changed.
 *	Serialised by the mac_lock.
 * @get_settings: Get ethtool settings. Serialised by the mac_lock.
 * @set_settings: Set ethtool settings. Serialised by the mac_lock.
 * @set_npage_adv: Set abilities advertised in (Extended) Next Page
 *	(only needed where AN bit is set in mmds)
 * @test_alive: Test that PHY is 'alive' (online)
 * @test_name: Get the name of a PHY-specific test/result
 * @run_tests: Run tests and record results as appropriate (offline).
 *	Flags are the ethtool tests flags.
 */
struct efx_phy_operations {
	int (*probe) (struct efx_nic *efx);
	int (*init) (struct efx_nic *efx);
	void (*fini) (struct efx_nic *efx);
	void (*remove) (struct efx_nic *efx);
	int (*reconfigure) (struct efx_nic *efx);
	bool (*poll) (struct efx_nic *efx);
	void (*get_settings) (struct efx_nic *efx,
			      struct ethtool_cmd *ecmd);
	int (*set_settings) (struct efx_nic *efx,
			     struct ethtool_cmd *ecmd);
	void (*set_npage_adv) (struct efx_nic *efx, u32);
	int (*test_alive) (struct efx_nic *efx);
	const char *(*test_name) (struct efx_nic *efx, unsigned int index);
	int (*run_tests) (struct efx_nic *efx, int *results, unsigned flags);
	int (*get_module_eeprom) (struct efx_nic *efx,
			       struct ethtool_eeprom *ee,
			       u8 *data);
	int (*get_module_info) (struct efx_nic *efx,
				struct ethtool_modinfo *modinfo);
};

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

/*
 * Efx extended statistics
 *
 * Not all statistics are provided by all supported MACs.  The purpose
 * is this structure is to contain the raw statistics provided by each
 * MAC.
 */
struct efx_mac_stats {
	u64 tx_bytes;
	u64 tx_good_bytes;
	u64 tx_bad_bytes;
	u64 tx_packets;
	u64 tx_bad;
	u64 tx_pause;
	u64 tx_control;
	u64 tx_unicast;
	u64 tx_multicast;
	u64 tx_broadcast;
	u64 tx_lt64;
	u64 tx_64;
	u64 tx_65_to_127;
	u64 tx_128_to_255;
	u64 tx_256_to_511;
	u64 tx_512_to_1023;
	u64 tx_1024_to_15xx;
	u64 tx_15xx_to_jumbo;
	u64 tx_gtjumbo;
	u64 tx_collision;
	u64 tx_single_collision;
	u64 tx_multiple_collision;
	u64 tx_excessive_collision;
	u64 tx_deferred;
	u64 tx_late_collision;
	u64 tx_excessive_deferred;
	u64 tx_non_tcpudp;
	u64 tx_mac_src_error;
	u64 tx_ip_src_error;
	u64 rx_bytes;
	u64 rx_good_bytes;
	u64 rx_bad_bytes;
	u64 rx_packets;
	u64 rx_good;
	u64 rx_bad;
	u64 rx_pause;
	u64 rx_control;
	u64 rx_unicast;
	u64 rx_multicast;
	u64 rx_broadcast;
	u64 rx_lt64;
	u64 rx_64;
	u64 rx_65_to_127;
	u64 rx_128_to_255;
	u64 rx_256_to_511;
	u64 rx_512_to_1023;
	u64 rx_1024_to_15xx;
	u64 rx_15xx_to_jumbo;
	u64 rx_gtjumbo;
	u64 rx_bad_lt64;
	u64 rx_bad_64_to_15xx;
	u64 rx_bad_15xx_to_jumbo;
	u64 rx_bad_gtjumbo;
	u64 rx_overflow;
	u64 rx_missed;
	u64 rx_false_carrier;
	u64 rx_symbol_error;
	u64 rx_align_error;
	u64 rx_length_error;
	u64 rx_internal_error;
	u64 rx_good_lt64;
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

struct efx_filter_state;
struct efx_vf;
struct vfdi_status;

/**
 * struct efx_nic - an Efx NIC
 * @name: Device name (net device name or bus id before net device registered)
 * @pci_dev: The PCI device
 * @type: Controller type attributes
 * @legacy_irq: IRQ number
 * @legacy_irq_enabled: Are IRQs enabled on NIC (INT_EN_KER register)?
 * @workqueue: Workqueue for port reconfigures and the HW monitor.
 *	Work items do not hold and must not acquire RTNL.
 * @workqueue_name: Name of workqueue
 * @reset_work: Scheduled reset workitem
 * @membase_phys: Memory BAR value as physical address
 * @membase: Memory BAR value
 * @interrupt_mode: Interrupt mode
 * @timer_quantum_ns: Interrupt timer quantum, in nanoseconds
 * @irq_rx_adaptive: Adaptive IRQ moderation enabled for RX event queues
 * @irq_rx_moderation: IRQ moderation time for RX event queues
 * @msg_enable: Log message enable flags
 * @state: Device state number (%STATE_*). Serialised by the rtnl_lock.
 * @reset_pending: Bitmask for pending resets
 * @tx_queue: TX DMA queues
 * @rx_queue: RX DMA queues
 * @channel: Channels
 * @channel_name: Names for channels and their IRQs
 * @extra_channel_types: Types of extra (non-traffic) channels that
 *	should be allocated for this NIC
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
 * @rx_dma_len: Current maximum RX DMA length
 * @rx_buffer_order: Order (log2) of number of pages for each RX buffer
 * @rx_buffer_truesize: Amortised allocation size of an RX buffer,
 *	for use in sk_buff::truesize
 * @rx_hash_key: Toeplitz hash key for RSS
 * @rx_indir_table: Indirection table for RSS
 * @rx_scatter: Scatter mode enabled for receives
 * @int_error_count: Number of internal errors seen recently
 * @int_error_expire: Time at which error count will be expired
 * @irq_status: Interrupt status buffer
 * @irq_zero_count: Number of legacy IRQs seen with queue flags == 0
 * @irq_level: IRQ level/index for IRQs not triggered by an event queue
 * @selftest_work: Work item for asynchronous self-test
 * @mtd_list: List of MTDs attached to the NIC
 * @nic_data: Hardware dependent state
 * @mac_lock: MAC access lock. Protects @port_enabled, @phy_mode,
 *	efx_monitor() and efx_reconfigure_port()
 * @port_enabled: Port enabled indicator.
 *	Serialises efx_stop_all(), efx_start_all(), efx_monitor() and
 *	efx_mac_work() with kernel interfaces. Safe to read under any
 *	one of the rtnl_lock, mac_lock, or netif_tx_lock, but all three must
 *	be held to modify it.
 * @port_initialized: Port initialized?
 * @net_dev: Operating system network device. Consider holding the rtnl lock
 * @stats_buffer: DMA buffer for statistics
 * @phy_type: PHY type
 * @phy_op: PHY interface
 * @phy_data: PHY private data (including PHY-specific stats)
 * @mdio: PHY MDIO interface
 * @mdio_bus: PHY MDIO bus ID (only used by Siena)
 * @phy_mode: PHY operating mode. Serialised by @mac_lock.
 * @link_advertising: Autonegotiation advertising flags
 * @link_state: Current state of the link
 * @n_link_state_changes: Number of times the link has changed state
 * @promiscuous: Promiscuous flag. Protected by netif_tx_lock.
 * @multicast_hash: Multicast hash table
 * @wanted_fc: Wanted flow control flags
 * @fc_disable: When non-zero flow control is disabled. Typically used to
 *	ensure that network back pressure doesn't delay dma queue flushes.
 *	Serialised by the rtnl lock.
 * @mac_work: Work item for changing MAC promiscuity and multicast hash
 * @loopback_mode: Loopback status
 * @loopback_modes: Supported loopback mode bitmask
 * @loopback_selftest: Offline self-test private state
 * @drain_pending: Count of RX and TX queues that haven't been flushed and drained.
 * @rxq_flush_pending: Count of number of receive queues that need to be flushed.
 *	Decremented when the efx_flush_rx_queue() is called.
 * @rxq_flush_outstanding: Count of number of RX flushes started but not yet
 *	completed (either success or failure). Not used when MCDI is used to
 *	flush receive queues.
 * @flush_wq: wait queue used by efx_nic_flush_queues() to wait for flush completions.
 * @vf: Array of &struct efx_vf objects.
 * @vf_count: Number of VFs intended to be enabled.
 * @vf_init_count: Number of VFs that have been fully initialised.
 * @vi_scale: log2 number of vnics per VF.
 * @vf_buftbl_base: The zeroth buffer table index used to back VF queues.
 * @vfdi_status: Common VFDI status page to be dmad to VF address space.
 * @local_addr_list: List of local addresses. Protected by %local_lock.
 * @local_page_list: List of DMA addressable pages used to broadcast
 *	%local_addr_list. Protected by %local_lock.
 * @local_lock: Mutex protecting %local_addr_list and %local_page_list.
 * @peer_work: Work item to broadcast peer addresses to VMs.
 * @ptp_data: PTP state data
 * @monitor_work: Hardware monitor workitem
 * @biu_lock: BIU (bus interface unit) lock
 * @last_irq_cpu: Last CPU to handle a possible test interrupt.  This
 *	field is used by efx_test_interrupts() to verify that an
 *	interrupt has occurred.
 * @n_rx_nodesc_drop_cnt: RX no descriptor drop count
 * @mac_stats: MAC statistics. These include all statistics the MACs
 *	can provide.  Generic code converts these into a standard
 *	&struct net_device_stats.
 * @stats_lock: Statistics update lock. Serialises statistics fetches
 *	and access to @mac_stats.
 *
 * This is stored in the private area of the &struct net_device.
 */
struct efx_nic {
	/* The following fields should be written very rarely */

	char name[IFNAMSIZ];
	struct pci_dev *pci_dev;
	const struct efx_nic_type *type;
	int legacy_irq;
	bool legacy_irq_enabled;
	struct workqueue_struct *workqueue;
	char workqueue_name[16];
	struct work_struct reset_work;
	resource_size_t membase_phys;
	void __iomem *membase;

	enum efx_int_mode interrupt_mode;
	unsigned int timer_quantum_ns;
	bool irq_rx_adaptive;
	unsigned int irq_rx_moderation;
	u32 msg_enable;

	enum nic_state state;
	unsigned long reset_pending;

	struct efx_channel *channel[EFX_MAX_CHANNELS];
	char channel_name[EFX_MAX_CHANNELS][IFNAMSIZ + 6];
	const struct efx_channel_type *
	extra_channel_type[EFX_MAX_EXTRA_CHANNELS];

	unsigned rxq_entries;
	unsigned txq_entries;
	unsigned int txq_stop_thresh;
	unsigned int txq_wake_thresh;

	unsigned tx_dc_base;
	unsigned rx_dc_base;
	unsigned sram_lim_qw;
	unsigned next_buffer_table;
	unsigned n_channels;
	unsigned n_rx_channels;
	unsigned rss_spread;
	unsigned tx_channel_offset;
	unsigned n_tx_channels;
	unsigned int rx_dma_len;
	unsigned int rx_buffer_order;
	unsigned int rx_buffer_truesize;
	u8 rx_hash_key[40];
	u32 rx_indir_table[128];
	bool rx_scatter;

	unsigned int_error_count;
	unsigned long int_error_expire;

	struct efx_buffer irq_status;
	unsigned irq_zero_count;
	unsigned irq_level;
	struct delayed_work selftest_work;

#ifdef CONFIG_SFC_MTD
	struct list_head mtd_list;
#endif

	void *nic_data;

	struct mutex mac_lock;
	struct work_struct mac_work;
	bool port_enabled;

	bool port_initialized;
	struct net_device *net_dev;

	struct efx_buffer stats_buffer;

	unsigned int phy_type;
	const struct efx_phy_operations *phy_op;
	void *phy_data;
	struct mdio_if_info mdio;
	unsigned int mdio_bus;
	enum efx_phy_mode phy_mode;

	u32 link_advertising;
	struct efx_link_state link_state;
	unsigned int n_link_state_changes;

	bool promiscuous;
	union efx_multicast_hash multicast_hash;
	u8 wanted_fc;
	unsigned fc_disable;

	atomic_t rx_reset;
	enum efx_loopback_mode loopback_mode;
	u64 loopback_modes;

	void *loopback_selftest;

	struct efx_filter_state *filter_state;

	atomic_t drain_pending;
	atomic_t rxq_flush_pending;
	atomic_t rxq_flush_outstanding;
	wait_queue_head_t flush_wq;

#ifdef CONFIG_SFC_SRIOV
	struct efx_channel *vfdi_channel;
	struct efx_vf *vf;
	unsigned vf_count;
	unsigned vf_init_count;
	unsigned vi_scale;
	unsigned vf_buftbl_base;
	struct efx_buffer vfdi_status;
	struct list_head local_addr_list;
	struct list_head local_page_list;
	struct mutex local_lock;
	struct work_struct peer_work;
#endif

	struct efx_ptp_data *ptp_data;

	/* The following fields may be written more often */

	struct delayed_work monitor_work ____cacheline_aligned_in_smp;
	spinlock_t biu_lock;
	int last_irq_cpu;
	unsigned n_rx_nodesc_drop_cnt;
	struct efx_mac_stats mac_stats;
	spinlock_t stats_lock;
};

static inline int efx_dev_registered(struct efx_nic *efx)
{
	return efx->net_dev->reg_state == NETREG_REGISTERED;
}

static inline unsigned int efx_port_num(struct efx_nic *efx)
{
	return efx->net_dev->dev_id;
}

/**
 * struct efx_nic_type - Efx device type definition
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
 * @prepare_flush: Prepare the hardware for flushing the DMA queues
 * @finish_flush: Clean up after flushing the DMA queues
 * @update_stats: Update statistics not provided by event handling
 * @start_stats: Start the regular fetching of statistics
 * @stop_stats: Stop the regular fetching of statistics
 * @set_id_led: Set state of identifying LED or revert to automatic function
 * @push_irq_moderation: Apply interrupt moderation value
 * @reconfigure_port: Push loopback/power/txdis changes to the MAC and PHY
 * @reconfigure_mac: Push MAC address, MTU, flow control and filter settings
 *	to the hardware.  Serialised by the mac_lock.
 * @check_mac_fault: Check MAC fault state. True if fault present.
 * @get_wol: Get WoL configuration from driver state
 * @set_wol: Push WoL configuration to the NIC
 * @resume_wol: Synchronise WoL state between driver and MC (e.g. after resume)
 * @test_chip: Test registers.  Should use efx_nic_test_registers(), and is
 *	expected to reset the NIC.
 * @test_nvram: Test validity of NVRAM contents
 * @revision: Hardware architecture revision
 * @mem_map_size: Memory BAR mapped size
 * @txd_ptr_tbl_base: TX descriptor ring base address
 * @rxd_ptr_tbl_base: RX descriptor ring base address
 * @buf_tbl_base: Buffer table base address
 * @evq_ptr_tbl_base: Event queue pointer table base address
 * @evq_rptr_tbl_base: Event queue read-pointer table base address
 * @max_dma_mask: Maximum possible DMA mask
 * @rx_buffer_hash_size: Size of hash at start of RX packet
 * @rx_buffer_padding: Size of padding at end of RX packet
 * @can_rx_scatter: NIC is able to scatter packet to multiple buffers
 * @max_interrupt_mode: Highest capability interrupt mode supported
 *	from &enum efx_init_mode.
 * @phys_addr_channels: Number of channels with physically addressed
 *	descriptors
 * @timer_period_max: Maximum period of interrupt timer (in ticks)
 * @offload_features: net_device feature flags for protocol offload
 *	features implemented in hardware
 */
struct efx_nic_type {
	int (*probe)(struct efx_nic *efx);
	void (*remove)(struct efx_nic *efx);
	int (*init)(struct efx_nic *efx);
	void (*dimension_resources)(struct efx_nic *efx);
	void (*fini)(struct efx_nic *efx);
	void (*monitor)(struct efx_nic *efx);
	enum reset_type (*map_reset_reason)(enum reset_type reason);
	int (*map_reset_flags)(u32 *flags);
	int (*reset)(struct efx_nic *efx, enum reset_type method);
	int (*probe_port)(struct efx_nic *efx);
	void (*remove_port)(struct efx_nic *efx);
	bool (*handle_global_event)(struct efx_channel *channel, efx_qword_t *);
	void (*prepare_flush)(struct efx_nic *efx);
	void (*finish_flush)(struct efx_nic *efx);
	void (*update_stats)(struct efx_nic *efx);
	void (*start_stats)(struct efx_nic *efx);
	void (*stop_stats)(struct efx_nic *efx);
	void (*set_id_led)(struct efx_nic *efx, enum efx_led_mode mode);
	void (*push_irq_moderation)(struct efx_channel *channel);
	int (*reconfigure_port)(struct efx_nic *efx);
	int (*reconfigure_mac)(struct efx_nic *efx);
	bool (*check_mac_fault)(struct efx_nic *efx);
	void (*get_wol)(struct efx_nic *efx, struct ethtool_wolinfo *wol);
	int (*set_wol)(struct efx_nic *efx, u32 type);
	void (*resume_wol)(struct efx_nic *efx);
	int (*test_chip)(struct efx_nic *efx, struct efx_self_tests *tests);
	int (*test_nvram)(struct efx_nic *efx);

	int revision;
	unsigned int mem_map_size;
	unsigned int txd_ptr_tbl_base;
	unsigned int rxd_ptr_tbl_base;
	unsigned int buf_tbl_base;
	unsigned int evq_ptr_tbl_base;
	unsigned int evq_rptr_tbl_base;
	u64 max_dma_mask;
	unsigned int rx_buffer_hash_size;
	unsigned int rx_buffer_padding;
	bool can_rx_scatter;
	unsigned int max_interrupt_mode;
	unsigned int phys_addr_channels;
	unsigned int timer_period_max;
	netdev_features_t offload_features;
};

/**************************************************************************
 *
 * Prototypes and inline functions
 *
 *************************************************************************/

static inline struct efx_channel *
efx_get_channel(struct efx_nic *efx, unsigned index)
{
	EFX_BUG_ON_PARANOID(index >= efx->n_channels);
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

static inline struct efx_tx_queue *
efx_get_tx_queue(struct efx_nic *efx, unsigned index, unsigned type)
{
	EFX_BUG_ON_PARANOID(index >= efx->n_tx_channels ||
			    type >= EFX_TXQ_TYPES);
	return &efx->channel[efx->tx_channel_offset + index]->tx_queue[type];
}

static inline bool efx_channel_has_tx_queues(struct efx_channel *channel)
{
	return channel->channel - channel->efx->tx_channel_offset <
		channel->efx->n_tx_channels;
}

static inline struct efx_tx_queue *
efx_channel_get_tx_queue(struct efx_channel *channel, unsigned type)
{
	EFX_BUG_ON_PARANOID(!efx_channel_has_tx_queues(channel) ||
			    type >= EFX_TXQ_TYPES);
	return &channel->tx_queue[type];
}

static inline bool efx_tx_queue_used(struct efx_tx_queue *tx_queue)
{
	return !(tx_queue->efx->net_dev->num_tc < 2 &&
		 tx_queue->queue & EFX_TXQ_TYPE_HIGHPRI);
}

/* Iterate over all TX queues belonging to a channel */
#define efx_for_each_channel_tx_queue(_tx_queue, _channel)		\
	if (!efx_channel_has_tx_queues(_channel))			\
		;							\
	else								\
		for (_tx_queue = (_channel)->tx_queue;			\
		     _tx_queue < (_channel)->tx_queue + EFX_TXQ_TYPES && \
			     efx_tx_queue_used(_tx_queue);		\
		     _tx_queue++)

/* Iterate over all possible TX queues belonging to a channel */
#define efx_for_each_possible_channel_tx_queue(_tx_queue, _channel)	\
	if (!efx_channel_has_tx_queues(_channel))			\
		;							\
	else								\
		for (_tx_queue = (_channel)->tx_queue;			\
		     _tx_queue < (_channel)->tx_queue + EFX_TXQ_TYPES;	\
		     _tx_queue++)

static inline bool efx_channel_has_rx_queue(struct efx_channel *channel)
{
	return channel->rx_queue.core_index >= 0;
}

static inline struct efx_rx_queue *
efx_channel_get_rx_queue(struct efx_channel *channel)
{
	EFX_BUG_ON_PARANOID(!efx_channel_has_rx_queue(channel));
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
#define EFX_MAX_FRAME_LEN(mtu) \
	((((mtu) + ETH_HLEN + VLAN_HLEN + 4/* FCS */ + 7) & ~7) + 16)

static inline bool efx_xmit_with_hwtstamp(struct sk_buff *skb)
{
	return skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP;
}
static inline void efx_xmit_hwtstamp_pending(struct sk_buff *skb)
{
	skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
}

#endif /* EFX_NET_DRIVER_H */

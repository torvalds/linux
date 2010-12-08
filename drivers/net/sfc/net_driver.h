/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/* Common definitions for all Efx net driver code */

#ifndef EFX_NET_DRIVER_H
#define EFX_NET_DRIVER_H

#if defined(EFX_ENABLE_DEBUG) && !defined(DEBUG)
#define DEBUG
#endif

#include <linux/version.h>
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
#include <linux/vmalloc.h>
#include <linux/i2c.h>

#include "enum.h"
#include "bitfield.h"

/**************************************************************************
 *
 * Build definitions
 *
 **************************************************************************/

#define EFX_DRIVER_VERSION	"3.0"

#ifdef EFX_ENABLE_DEBUG
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

#define EFX_MAX_CHANNELS 32
#define EFX_MAX_RX_QUEUES EFX_MAX_CHANNELS

/* Checksum generation is a per-queue option in hardware, so each
 * queue visible to the networking core is backed by two hardware TX
 * queues. */
#define EFX_MAX_CORE_TX_QUEUES	EFX_MAX_CHANNELS
#define EFX_TXQ_TYPE_OFFLOAD	1
#define EFX_TXQ_TYPES		2
#define EFX_MAX_TX_QUEUES	(EFX_TXQ_TYPES * EFX_MAX_CORE_TX_QUEUES)

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
	int index;
	int entries;
};

enum efx_flush_state {
	FLUSH_NONE,
	FLUSH_PENDING,
	FLUSH_FAILED,
	FLUSH_DONE,
};

/**
 * struct efx_tx_buffer - An Efx TX buffer
 * @skb: The associated socket buffer.
 *	Set only on the final fragment of a packet; %NULL for all other
 *	fragments.  When this fragment completes, then we can free this
 *	skb.
 * @tsoh: The associated TSO header structure, or %NULL if this
 *	buffer is not a TSO header.
 * @dma_addr: DMA address of the fragment.
 * @len: Length of this fragment.
 *	This field is zero when the queue slot is empty.
 * @continuation: True if this fragment is not the end of a packet.
 * @unmap_single: True if pci_unmap_single should be used.
 * @unmap_len: Length of this fragment to unmap
 */
struct efx_tx_buffer {
	const struct sk_buff *skb;
	struct efx_tso_header *tsoh;
	dma_addr_t dma_addr;
	unsigned short len;
	bool continuation;
	bool unmap_single;
	unsigned short unmap_len;
};

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
 * @buffer: The software buffer ring
 * @txd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @flushed: Used when handling queue flushing
 * @read_count: Current read pointer.
 *	This is the number of buffers that have been removed from both rings.
 * @stopped: Stopped count.
 *	Set if this TX queue is currently stopping its port.
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
 * @tso_headers_free: A list of TSO headers allocated for this TX queue
 *	that are not in use, and so available for new TSO sends. The list
 *	is protected by the TX queue lock.
 * @tso_bursts: Number of times TSO xmit invoked by kernel
 * @tso_long_headers: Number of packets with headers too long for standard
 *	blocks
 * @tso_packets: Number of packets via the TSO xmit path
 */
struct efx_tx_queue {
	/* Members which don't change on the fast path */
	struct efx_nic *efx ____cacheline_aligned_in_smp;
	unsigned queue;
	struct efx_channel *channel;
	struct efx_nic *nic;
	struct efx_tx_buffer *buffer;
	struct efx_special_buffer txd;
	unsigned int ptr_mask;
	enum efx_flush_state flushed;

	/* Members used mainly on the completion path */
	unsigned int read_count ____cacheline_aligned_in_smp;
	int stopped;

	/* Members used only on the xmit path */
	unsigned int insert_count ____cacheline_aligned_in_smp;
	unsigned int write_count;
	unsigned int old_read_count;
	struct efx_tso_header *tso_headers_free;
	unsigned int tso_bursts;
	unsigned int tso_long_headers;
	unsigned int tso_packets;
};

/**
 * struct efx_rx_buffer - An Efx RX data buffer
 * @dma_addr: DMA base address of the buffer
 * @skb: The associated socket buffer, if any.
 *	If both this and page are %NULL, the buffer slot is currently free.
 * @page: The associated page buffer, if any.
 *	If both this and skb are %NULL, the buffer slot is currently free.
 * @data: Pointer to ethernet header
 * @len: Buffer length, in bytes.
 */
struct efx_rx_buffer {
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	struct page *page;
	char *data;
	unsigned int len;
};

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
 * @buffer: The software buffer ring
 * @rxd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @added_count: Number of buffers added to the receive queue.
 * @notified_count: Number of buffers given to NIC (<= @added_count).
 * @removed_count: Number of buffers removed from the receive queue.
 * @max_fill: RX descriptor maximum fill level (<= ring size)
 * @fast_fill_trigger: RX descriptor fill level that will trigger a fast fill
 *	(<= @max_fill)
 * @fast_fill_limit: The level to which a fast fill will fill
 *	(@fast_fill_trigger <= @fast_fill_limit <= @max_fill)
 * @min_fill: RX descriptor minimum non-zero fill level.
 *	This records the minimum fill level observed when a ring
 *	refill was triggered.
 * @alloc_page_count: RX allocation strategy counter.
 * @alloc_skb_count: RX allocation strategy counter.
 * @slow_fill: Timer used to defer efx_nic_generate_fill_event().
 * @flushed: Use when handling queue flushing
 */
struct efx_rx_queue {
	struct efx_nic *efx;
	struct efx_rx_buffer *buffer;
	struct efx_special_buffer rxd;
	unsigned int ptr_mask;

	int added_count;
	int notified_count;
	int removed_count;
	unsigned int max_fill;
	unsigned int fast_fill_trigger;
	unsigned int fast_fill_limit;
	unsigned int min_fill;
	unsigned int min_overfill;
	unsigned int alloc_page_count;
	unsigned int alloc_skb_count;
	struct timer_list slow_fill;
	unsigned int slow_fill_count;

	enum efx_flush_state flushed;
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
 * @enabled: Channel enabled indicator
 * @irq: IRQ number (MSI and MSI-X only)
 * @irq_moderation: IRQ moderation value (in hardware ticks)
 * @napi_dev: Net device used with NAPI
 * @napi_str: NAPI control structure
 * @reset_work: Scheduled reset work thread
 * @work_pending: Is work pending via NAPI?
 * @eventq: Event queue buffer
 * @eventq_mask: Event queue pointer mask
 * @eventq_read_ptr: Event queue read pointer
 * @last_eventq_read_ptr: Last event queue read pointer value.
 * @magic_count: Event queue test event count
 * @irq_count: Number of IRQs since last adaptive moderation decision
 * @irq_mod_score: IRQ moderation score
 * @rx_alloc_level: Watermark based heuristic counter for pushing descriptors
 *	and diagnostic counters
 * @rx_alloc_push_pages: RX allocation method currently in use for pushing
 *	descriptors
 * @n_rx_tobe_disc: Count of RX_TOBE_DISC errors
 * @n_rx_ip_hdr_chksum_err: Count of RX IP header checksum errors
 * @n_rx_tcp_udp_chksum_err: Count of RX TCP and UDP checksum errors
 * @n_rx_mcast_mismatch: Count of unmatched multicast frames
 * @n_rx_frm_trunc: Count of RX_FRM_TRUNC errors
 * @n_rx_overlength: Count of RX_OVERLENGTH errors
 * @n_skbuff_leaks: Count of skbuffs leaked due to RX overrun
 * @rx_queue: RX queue for this channel
 * @tx_stop_count: Core TX queue stop count
 * @tx_stop_lock: Core TX queue stop lock
 * @tx_queue: TX queues for this channel
 */
struct efx_channel {
	struct efx_nic *efx;
	int channel;
	bool enabled;
	int irq;
	unsigned int irq_moderation;
	struct net_device *napi_dev;
	struct napi_struct napi_str;
	bool work_pending;
	struct efx_special_buffer eventq;
	unsigned int eventq_mask;
	unsigned int eventq_read_ptr;
	unsigned int last_eventq_read_ptr;
	unsigned int magic_count;

	unsigned int irq_count;
	unsigned int irq_mod_score;

	int rx_alloc_level;
	int rx_alloc_push_pages;

	unsigned n_rx_tobe_disc;
	unsigned n_rx_ip_hdr_chksum_err;
	unsigned n_rx_tcp_udp_chksum_err;
	unsigned n_rx_mcast_mismatch;
	unsigned n_rx_frm_trunc;
	unsigned n_rx_overlength;
	unsigned n_skbuff_leaks;

	/* Used to pipeline received packets in order to optimise memory
	 * access with prefetches.
	 */
	struct efx_rx_buffer *rx_pkt;
	bool rx_pkt_csummed;

	struct efx_rx_queue rx_queue;

	atomic_t tx_stop_count;
	spinlock_t tx_stop_lock;

	struct efx_tx_queue tx_queue[2];
};

enum efx_led_mode {
	EFX_LED_OFF	= 0,
	EFX_LED_ON	= 1,
	EFX_LED_DEFAULT	= 2
};

#define STRING_TABLE_LOOKUP(val, member) \
	((val) < member ## _max) ? member ## _names[val] : "(invalid)"

extern const char *efx_loopback_mode_names[];
extern const unsigned int efx_loopback_mode_max;
#define LOOPBACK_MODE(efx) \
	STRING_TABLE_LOOKUP((efx)->loopback_mode, efx_loopback_mode)

extern const char *efx_reset_type_names[];
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
	STATE_INIT = 0,
	STATE_RUNNING = 1,
	STATE_FINI = 2,
	STATE_DISABLED = 3,
	STATE_MAX,
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
enum efx_fc_type {
	EFX_FC_RX = FLOW_CTRL_RX,
	EFX_FC_TX = FLOW_CTRL_TX,
	EFX_FC_AUTO = 4,
};

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
	enum efx_fc_type fc;
	unsigned int speed;
};

static inline bool efx_link_state_equal(const struct efx_link_state *left,
					const struct efx_link_state *right)
{
	return left->up == right->up && left->fd == right->fd &&
		left->fc == right->fc && left->speed == right->speed;
}

/**
 * struct efx_mac_operations - Efx MAC operations table
 * @reconfigure: Reconfigure MAC. Serialised by the mac_lock
 * @update_stats: Update statistics
 * @check_fault: Check fault state. True if fault present.
 */
struct efx_mac_operations {
	int (*reconfigure) (struct efx_nic *efx);
	void (*update_stats) (struct efx_nic *efx);
	bool (*check_fault)(struct efx_nic *efx);
};

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
};

/**
 * @enum efx_phy_mode - PHY operating mode flags
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
	unsigned long tx_packets;
	unsigned long tx_bad;
	unsigned long tx_pause;
	unsigned long tx_control;
	unsigned long tx_unicast;
	unsigned long tx_multicast;
	unsigned long tx_broadcast;
	unsigned long tx_lt64;
	unsigned long tx_64;
	unsigned long tx_65_to_127;
	unsigned long tx_128_to_255;
	unsigned long tx_256_to_511;
	unsigned long tx_512_to_1023;
	unsigned long tx_1024_to_15xx;
	unsigned long tx_15xx_to_jumbo;
	unsigned long tx_gtjumbo;
	unsigned long tx_collision;
	unsigned long tx_single_collision;
	unsigned long tx_multiple_collision;
	unsigned long tx_excessive_collision;
	unsigned long tx_deferred;
	unsigned long tx_late_collision;
	unsigned long tx_excessive_deferred;
	unsigned long tx_non_tcpudp;
	unsigned long tx_mac_src_error;
	unsigned long tx_ip_src_error;
	u64 rx_bytes;
	u64 rx_good_bytes;
	u64 rx_bad_bytes;
	unsigned long rx_packets;
	unsigned long rx_good;
	unsigned long rx_bad;
	unsigned long rx_pause;
	unsigned long rx_control;
	unsigned long rx_unicast;
	unsigned long rx_multicast;
	unsigned long rx_broadcast;
	unsigned long rx_lt64;
	unsigned long rx_64;
	unsigned long rx_65_to_127;
	unsigned long rx_128_to_255;
	unsigned long rx_256_to_511;
	unsigned long rx_512_to_1023;
	unsigned long rx_1024_to_15xx;
	unsigned long rx_15xx_to_jumbo;
	unsigned long rx_gtjumbo;
	unsigned long rx_bad_lt64;
	unsigned long rx_bad_64_to_15xx;
	unsigned long rx_bad_15xx_to_jumbo;
	unsigned long rx_bad_gtjumbo;
	unsigned long rx_overflow;
	unsigned long rx_missed;
	unsigned long rx_false_carrier;
	unsigned long rx_symbol_error;
	unsigned long rx_align_error;
	unsigned long rx_length_error;
	unsigned long rx_internal_error;
	unsigned long rx_good_lt64;
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
 * @monitor_work: Hardware monitor workitem
 * @membase_phys: Memory BAR value as physical address
 * @membase: Memory BAR value
 * @biu_lock: BIU (bus interface unit) lock
 * @interrupt_mode: Interrupt mode
 * @irq_rx_adaptive: Adaptive IRQ moderation enabled for RX event queues
 * @irq_rx_moderation: IRQ moderation time for RX event queues
 * @msg_enable: Log message enable flags
 * @state: Device state flag. Serialised by the rtnl_lock.
 * @reset_pending: Pending reset method (normally RESET_TYPE_NONE)
 * @tx_queue: TX DMA queues
 * @rx_queue: RX DMA queues
 * @channel: Channels
 * @channel_name: Names for channels and their IRQs
 * @rxq_entries: Size of receive queues requested by user.
 * @txq_entries: Size of transmit queues requested by user.
 * @next_buffer_table: First available buffer table id
 * @n_channels: Number of channels in use
 * @n_rx_channels: Number of channels used for RX (= number of RX queues)
 * @n_tx_channels: Number of channels used for TX
 * @rx_buffer_len: RX buffer length
 * @rx_buffer_order: Order (log2) of number of pages for each RX buffer
 * @rx_hash_key: Toeplitz hash key for RSS
 * @rx_indir_table: Indirection table for RSS
 * @int_error_count: Number of internal errors seen recently
 * @int_error_expire: Time at which error count will be expired
 * @irq_status: Interrupt status buffer
 * @last_irq_cpu: Last CPU to handle interrupt.
 *	This register is written with the SMP processor ID whenever an
 *	interrupt is handled.  It is used by efx_nic_test_interrupt()
 *	to verify that an interrupt has occurred.
 * @irq_zero_count: Number of legacy IRQs seen with queue flags == 0
 * @fatal_irq_level: IRQ level (bit number) used for serious errors
 * @mtd_list: List of MTDs attached to the NIC
 * @n_rx_nodesc_drop_cnt: RX no descriptor drop count
 * @nic_data: Hardware dependant state
 * @mac_lock: MAC access lock. Protects @port_enabled, @phy_mode,
 *	@port_inhibited, efx_monitor() and efx_reconfigure_port()
 * @port_enabled: Port enabled indicator.
 *	Serialises efx_stop_all(), efx_start_all(), efx_monitor() and
 *	efx_mac_work() with kernel interfaces. Safe to read under any
 *	one of the rtnl_lock, mac_lock, or netif_tx_lock, but all three must
 *	be held to modify it.
 * @port_inhibited: If set, the netif_carrier is always off. Hold the mac_lock
 * @port_initialized: Port initialized?
 * @net_dev: Operating system network device. Consider holding the rtnl lock
 * @rx_checksum_enabled: RX checksumming enabled
 * @mac_stats: MAC statistics. These include all statistics the MACs
 *	can provide.  Generic code converts these into a standard
 *	&struct net_device_stats.
 * @stats_buffer: DMA buffer for statistics
 * @stats_lock: Statistics update lock. Serialises statistics fetches
 * @mac_op: MAC interface
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
 * @mac_work: Work item for changing MAC promiscuity and multicast hash
 * @loopback_mode: Loopback status
 * @loopback_modes: Supported loopback mode bitmask
 * @loopback_selftest: Offline self-test private state
 *
 * This is stored in the private area of the &struct net_device.
 */
struct efx_nic {
	char name[IFNAMSIZ];
	struct pci_dev *pci_dev;
	const struct efx_nic_type *type;
	int legacy_irq;
	bool legacy_irq_enabled;
	struct workqueue_struct *workqueue;
	char workqueue_name[16];
	struct work_struct reset_work;
	struct delayed_work monitor_work;
	resource_size_t membase_phys;
	void __iomem *membase;
	spinlock_t biu_lock;
	enum efx_int_mode interrupt_mode;
	bool irq_rx_adaptive;
	unsigned int irq_rx_moderation;
	u32 msg_enable;

	enum nic_state state;
	enum reset_type reset_pending;

	struct efx_channel *channel[EFX_MAX_CHANNELS];
	char channel_name[EFX_MAX_CHANNELS][IFNAMSIZ + 6];

	unsigned rxq_entries;
	unsigned txq_entries;
	unsigned next_buffer_table;
	unsigned n_channels;
	unsigned n_rx_channels;
	unsigned n_tx_channels;
	unsigned int rx_buffer_len;
	unsigned int rx_buffer_order;
	u8 rx_hash_key[40];
	u32 rx_indir_table[128];

	unsigned int_error_count;
	unsigned long int_error_expire;

	struct efx_buffer irq_status;
	volatile signed int last_irq_cpu;
	unsigned irq_zero_count;
	unsigned fatal_irq_level;

#ifdef CONFIG_SFC_MTD
	struct list_head mtd_list;
#endif

	unsigned n_rx_nodesc_drop_cnt;

	void *nic_data;

	struct mutex mac_lock;
	struct work_struct mac_work;
	bool port_enabled;
	bool port_inhibited;

	bool port_initialized;
	struct net_device *net_dev;
	bool rx_checksum_enabled;

	struct efx_mac_stats mac_stats;
	struct efx_buffer stats_buffer;
	spinlock_t stats_lock;

	struct efx_mac_operations *mac_op;

	unsigned int phy_type;
	struct efx_phy_operations *phy_op;
	void *phy_data;
	struct mdio_if_info mdio;
	unsigned int mdio_bus;
	enum efx_phy_mode phy_mode;

	u32 link_advertising;
	struct efx_link_state link_state;
	unsigned int n_link_state_changes;

	bool promiscuous;
	union efx_multicast_hash multicast_hash;
	enum efx_fc_type wanted_fc;

	atomic_t rx_reset;
	enum efx_loopback_mode loopback_mode;
	u64 loopback_modes;

	void *loopback_selftest;

	struct efx_filter_state *filter_state;
};

static inline int efx_dev_registered(struct efx_nic *efx)
{
	return efx->net_dev->reg_state == NETREG_REGISTERED;
}

/* Net device name, for inclusion in log messages if it has been registered.
 * Use efx->name not efx->net_dev->name so that races with (un)registration
 * are harmless.
 */
static inline const char *efx_dev_name(struct efx_nic *efx)
{
	return efx_dev_registered(efx) ? efx->name : "";
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
 * @fini: Shut down the controller
 * @monitor: Periodic function for polling link state and hardware monitor
 * @reset: Reset the controller hardware and possibly the PHY.  This will
 *	be called while the controller is uninitialised.
 * @probe_port: Probe the MAC and PHY
 * @remove_port: Free resources allocated by probe_port()
 * @handle_global_event: Handle a "global" event (may be %NULL)
 * @prepare_flush: Prepare the hardware for flushing the DMA queues
 * @update_stats: Update statistics not provided by event handling
 * @start_stats: Start the regular fetching of statistics
 * @stop_stats: Stop the regular fetching of statistics
 * @set_id_led: Set state of identifying LED or revert to automatic function
 * @push_irq_moderation: Apply interrupt moderation value
 * @push_multicast_hash: Apply multicast hash table
 * @reconfigure_port: Push loopback/power/txdis changes to the MAC and PHY
 * @get_wol: Get WoL configuration from driver state
 * @set_wol: Push WoL configuration to the NIC
 * @resume_wol: Synchronise WoL state between driver and MC (e.g. after resume)
 * @test_registers: Test read/write functionality of control registers
 * @test_nvram: Test validity of NVRAM contents
 * @default_mac_ops: efx_mac_operations to set at startup
 * @revision: Hardware architecture revision
 * @mem_map_size: Memory BAR mapped size
 * @txd_ptr_tbl_base: TX descriptor ring base address
 * @rxd_ptr_tbl_base: RX descriptor ring base address
 * @buf_tbl_base: Buffer table base address
 * @evq_ptr_tbl_base: Event queue pointer table base address
 * @evq_rptr_tbl_base: Event queue read-pointer table base address
 * @max_dma_mask: Maximum possible DMA mask
 * @rx_buffer_hash_size: Size of hash at start of RX buffer
 * @rx_buffer_padding: Size of padding at end of RX buffer
 * @max_interrupt_mode: Highest capability interrupt mode supported
 *	from &enum efx_init_mode.
 * @phys_addr_channels: Number of channels with physically addressed
 *	descriptors
 * @tx_dc_base: Base address in SRAM of TX queue descriptor caches
 * @rx_dc_base: Base address in SRAM of RX queue descriptor caches
 * @offload_features: net_device feature flags for protocol offload
 *	features implemented in hardware
 * @reset_world_flags: Flags for additional components covered by
 *	reset method RESET_TYPE_WORLD
 */
struct efx_nic_type {
	int (*probe)(struct efx_nic *efx);
	void (*remove)(struct efx_nic *efx);
	int (*init)(struct efx_nic *efx);
	void (*fini)(struct efx_nic *efx);
	void (*monitor)(struct efx_nic *efx);
	int (*reset)(struct efx_nic *efx, enum reset_type method);
	int (*probe_port)(struct efx_nic *efx);
	void (*remove_port)(struct efx_nic *efx);
	bool (*handle_global_event)(struct efx_channel *channel, efx_qword_t *);
	void (*prepare_flush)(struct efx_nic *efx);
	void (*update_stats)(struct efx_nic *efx);
	void (*start_stats)(struct efx_nic *efx);
	void (*stop_stats)(struct efx_nic *efx);
	void (*set_id_led)(struct efx_nic *efx, enum efx_led_mode mode);
	void (*push_irq_moderation)(struct efx_channel *channel);
	void (*push_multicast_hash)(struct efx_nic *efx);
	int (*reconfigure_port)(struct efx_nic *efx);
	void (*get_wol)(struct efx_nic *efx, struct ethtool_wolinfo *wol);
	int (*set_wol)(struct efx_nic *efx, u32 type);
	void (*resume_wol)(struct efx_nic *efx);
	int (*test_registers)(struct efx_nic *efx);
	int (*test_nvram)(struct efx_nic *efx);
	struct efx_mac_operations *default_mac_ops;

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
	unsigned int max_interrupt_mode;
	unsigned int phys_addr_channels;
	unsigned int tx_dc_base;
	unsigned int rx_dc_base;
	unsigned long offload_features;
	u32 reset_world_flags;
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

extern struct efx_tx_queue *
efx_get_tx_queue(struct efx_nic *efx, unsigned index, unsigned type);

static inline struct efx_tx_queue *
efx_channel_get_tx_queue(struct efx_channel *channel, unsigned type)
{
	struct efx_tx_queue *tx_queue = channel->tx_queue;
	EFX_BUG_ON_PARANOID(type >= EFX_TXQ_TYPES);
	return tx_queue->channel ? tx_queue + type : NULL;
}

/* Iterate over all TX queues belonging to a channel */
#define efx_for_each_channel_tx_queue(_tx_queue, _channel)		\
	for (_tx_queue = efx_channel_get_tx_queue(channel, 0);		\
	     _tx_queue && _tx_queue < (_channel)->tx_queue + EFX_TXQ_TYPES; \
	     _tx_queue++)

static inline struct efx_rx_queue *
efx_get_rx_queue(struct efx_nic *efx, unsigned index)
{
	EFX_BUG_ON_PARANOID(index >= efx->n_rx_channels);
	return &efx->channel[index]->rx_queue;
}

static inline struct efx_rx_queue *
efx_channel_get_rx_queue(struct efx_channel *channel)
{
	return channel->channel < channel->efx->n_rx_channels ?
		&channel->rx_queue : NULL;
}

/* Iterate over all RX queues belonging to a channel */
#define efx_for_each_channel_rx_queue(_rx_queue, _channel)		\
	for (_rx_queue = efx_channel_get_rx_queue(channel);		\
	     _rx_queue;							\
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

/* Set bit in a little-endian bitfield */
static inline void set_bit_le(unsigned nr, unsigned char *addr)
{
	addr[nr / 8] |= (1 << (nr % 8));
}

/* Clear bit in a little-endian bitfield */
static inline void clear_bit_le(unsigned nr, unsigned char *addr)
{
	addr[nr / 8] &= ~(1 << (nr % 8));
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


#endif /* EFX_NET_DRIVER_H */

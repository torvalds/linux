/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

/* Common definitions for all Efx net driver code */

#ifndef EFX_NET_DRIVER_H
#define EFX_NET_DRIVER_H

#include <linux/version.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#include <linux/timer.h>
#include <linux/mii.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/workqueue.h>
#include <linux/i2c.h>

#include "enum.h"
#include "bitfield.h"

/**************************************************************************
 *
 * Build definitions
 *
 **************************************************************************/
#ifndef EFX_DRIVER_NAME
#define EFX_DRIVER_NAME	"sfc"
#endif
#define EFX_DRIVER_VERSION	"2.3"

#ifdef EFX_ENABLE_DEBUG
#define EFX_BUG_ON_PARANOID(x) BUG_ON(x)
#define EFX_WARN_ON_PARANOID(x) WARN_ON(x)
#else
#define EFX_BUG_ON_PARANOID(x) do {} while (0)
#define EFX_WARN_ON_PARANOID(x) do {} while (0)
#endif

/* Un-rate-limited logging */
#define EFX_ERR(efx, fmt, args...) \
dev_err(&((efx)->pci_dev->dev), "ERR: %s " fmt, efx_dev_name(efx), ##args)

#define EFX_INFO(efx, fmt, args...) \
dev_info(&((efx)->pci_dev->dev), "INFO: %s " fmt, efx_dev_name(efx), ##args)

#ifdef EFX_ENABLE_DEBUG
#define EFX_LOG(efx, fmt, args...) \
dev_info(&((efx)->pci_dev->dev), "DBG: %s " fmt, efx_dev_name(efx), ##args)
#else
#define EFX_LOG(efx, fmt, args...) \
dev_dbg(&((efx)->pci_dev->dev), "DBG: %s " fmt, efx_dev_name(efx), ##args)
#endif

#define EFX_TRACE(efx, fmt, args...) do {} while (0)

#define EFX_REGDUMP(efx, fmt, args...) do {} while (0)

/* Rate-limited logging */
#define EFX_ERR_RL(efx, fmt, args...) \
do {if (net_ratelimit()) EFX_ERR(efx, fmt, ##args); } while (0)

#define EFX_INFO_RL(efx, fmt, args...) \
do {if (net_ratelimit()) EFX_INFO(efx, fmt, ##args); } while (0)

#define EFX_LOG_RL(efx, fmt, args...) \
do {if (net_ratelimit()) EFX_LOG(efx, fmt, ##args); } while (0)

/**************************************************************************
 *
 * Efx data structures
 *
 **************************************************************************/

#define EFX_MAX_CHANNELS 32
#define EFX_MAX_RX_QUEUES EFX_MAX_CHANNELS

#define EFX_TX_QUEUE_OFFLOAD_CSUM	0
#define EFX_TX_QUEUE_NO_CSUM		1
#define EFX_TX_QUEUE_COUNT		2

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
 *
 * Note that for Falcon, TX and RX descriptor queues live in host memory.
 * Allocation and freeing procedures must take this into account.
 */
struct efx_special_buffer {
	void *addr;
	dma_addr_t dma_addr;
	unsigned int len;
	int index;
	int entries;
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
	int queue;
	struct efx_channel *channel;
	struct efx_nic *nic;
	struct efx_tx_buffer *buffer;
	struct efx_special_buffer txd;
	bool flushed;

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
 * @unmap_addr: DMA address to unmap
 */
struct efx_rx_buffer {
	dma_addr_t dma_addr;
	struct sk_buff *skb;
	struct page *page;
	char *data;
	unsigned int len;
	dma_addr_t unmap_addr;
};

/**
 * struct efx_rx_queue - An Efx RX queue
 * @efx: The associated Efx NIC
 * @queue: DMA queue number
 * @channel: The associated channel
 * @buffer: The software buffer ring
 * @rxd: The hardware descriptor ring
 * @added_count: Number of buffers added to the receive queue.
 * @notified_count: Number of buffers given to NIC (<= @added_count).
 * @removed_count: Number of buffers removed from the receive queue.
 * @add_lock: Receive queue descriptor add spin lock.
 *	This lock must be held in order to add buffers to the RX
 *	descriptor ring (rxd and buffer) and to update added_count (but
 *	not removed_count).
 * @max_fill: RX descriptor maximum fill level (<= ring size)
 * @fast_fill_trigger: RX descriptor fill level that will trigger a fast fill
 *	(<= @max_fill)
 * @fast_fill_limit: The level to which a fast fill will fill
 *	(@fast_fill_trigger <= @fast_fill_limit <= @max_fill)
 * @min_fill: RX descriptor minimum non-zero fill level.
 *	This records the minimum fill level observed when a ring
 *	refill was triggered.
 * @min_overfill: RX descriptor minimum overflow fill level.
 *	This records the minimum fill level at which RX queue
 *	overflow was observed.  It should never be set.
 * @alloc_page_count: RX allocation strategy counter.
 * @alloc_skb_count: RX allocation strategy counter.
 * @work: Descriptor push work thread
 * @buf_page: Page for next RX buffer.
 *	We can use a single page for multiple RX buffers. This tracks
 *	the remaining space in the allocation.
 * @buf_dma_addr: Page's DMA address.
 * @buf_data: Page's host address.
 * @flushed: Use when handling queue flushing
 */
struct efx_rx_queue {
	struct efx_nic *efx;
	int queue;
	struct efx_channel *channel;
	struct efx_rx_buffer *buffer;
	struct efx_special_buffer rxd;

	int added_count;
	int notified_count;
	int removed_count;
	spinlock_t add_lock;
	unsigned int max_fill;
	unsigned int fast_fill_trigger;
	unsigned int fast_fill_limit;
	unsigned int min_fill;
	unsigned int min_overfill;
	unsigned int alloc_page_count;
	unsigned int alloc_skb_count;
	struct delayed_work work;
	unsigned int slow_fill_count;

	struct page *buf_page;
	dma_addr_t buf_dma_addr;
	char *buf_data;
	bool flushed;
};

/**
 * struct efx_buffer - An Efx general-purpose buffer
 * @addr: host base address of the buffer
 * @dma_addr: DMA base address of the buffer
 * @len: Buffer length, in bytes
 *
 * Falcon uses these buffers for its interrupt status registers and
 * MAC stats dumps.
 */
struct efx_buffer {
	void *addr;
	dma_addr_t dma_addr;
	unsigned int len;
};


/* Flags for channel->used_flags */
#define EFX_USED_BY_RX 1
#define EFX_USED_BY_TX 2
#define EFX_USED_BY_RX_TX (EFX_USED_BY_RX | EFX_USED_BY_TX)

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
 * @name: Name for channel and IRQ
 * @used_flags: Channel is used by net driver
 * @enabled: Channel enabled indicator
 * @irq: IRQ number (MSI and MSI-X only)
 * @irq_moderation: IRQ moderation value (in us)
 * @napi_dev: Net device used with NAPI
 * @napi_str: NAPI control structure
 * @reset_work: Scheduled reset work thread
 * @work_pending: Is work pending via NAPI?
 * @eventq: Event queue buffer
 * @eventq_read_ptr: Event queue read pointer
 * @last_eventq_read_ptr: Last event queue read pointer value.
 * @eventq_magic: Event queue magic value for driver-generated test events
 * @irq_count: Number of IRQs since last adaptive moderation decision
 * @irq_mod_score: IRQ moderation score
 * @rx_alloc_level: Watermark based heuristic counter for pushing descriptors
 *	and diagnostic counters
 * @rx_alloc_push_pages: RX allocation method currently in use for pushing
 *	descriptors
 * @n_rx_tobe_disc: Count of RX_TOBE_DISC errors
 * @n_rx_ip_frag_err: Count of RX IP fragment errors
 * @n_rx_ip_hdr_chksum_err: Count of RX IP header checksum errors
 * @n_rx_tcp_udp_chksum_err: Count of RX TCP and UDP checksum errors
 * @n_rx_frm_trunc: Count of RX_FRM_TRUNC errors
 * @n_rx_overlength: Count of RX_OVERLENGTH errors
 * @n_skbuff_leaks: Count of skbuffs leaked due to RX overrun
 */
struct efx_channel {
	struct efx_nic *efx;
	int channel;
	char name[IFNAMSIZ + 6];
	int used_flags;
	bool enabled;
	int irq;
	unsigned int irq_moderation;
	struct net_device *napi_dev;
	struct napi_struct napi_str;
	bool work_pending;
	struct efx_special_buffer eventq;
	unsigned int eventq_read_ptr;
	unsigned int last_eventq_read_ptr;
	unsigned int eventq_magic;

	unsigned int irq_count;
	unsigned int irq_mod_score;

	int rx_alloc_level;
	int rx_alloc_push_pages;

	unsigned n_rx_tobe_disc;
	unsigned n_rx_ip_frag_err;
	unsigned n_rx_ip_hdr_chksum_err;
	unsigned n_rx_tcp_udp_chksum_err;
	unsigned n_rx_frm_trunc;
	unsigned n_rx_overlength;
	unsigned n_skbuff_leaks;

	/* Used to pipeline received packets in order to optimise memory
	 * access with prefetches.
	 */
	struct efx_rx_buffer *rx_pkt;
	bool rx_pkt_csummed;

};

/**
 * struct efx_blinker - S/W LED blinking context
 * @state: Current state - on or off
 * @resubmit: Timer resubmission flag
 * @timer: Control timer for blinking
 */
struct efx_blinker {
	bool state;
	bool resubmit;
	struct timer_list timer;
};


/**
 * struct efx_board - board information
 * @type: Board model type
 * @major: Major rev. ('A', 'B' ...)
 * @minor: Minor rev. (0, 1, ...)
 * @init: Initialisation function
 * @init_leds: Sets up board LEDs. May be called repeatedly.
 * @set_id_led: Turns the identification LED on or off
 * @blink: Starts/stops blinking
 * @monitor: Board-specific health check function
 * @fini: Cleanup function
 * @blinker: used to blink LEDs in software
 * @hwmon_client: I2C client for hardware monitor
 * @ioexp_client: I2C client for power/port control
 */
struct efx_board {
	int type;
	int major;
	int minor;
	int (*init) (struct efx_nic *nic);
	/* As the LEDs are typically attached to the PHY, LEDs
	 * have a separate init callback that happens later than
	 * board init. */
	void (*init_leds)(struct efx_nic *efx);
	void (*set_id_led) (struct efx_nic *efx, bool state);
	int (*monitor) (struct efx_nic *nic);
	void (*blink) (struct efx_nic *efx, bool start);
	void (*fini) (struct efx_nic *nic);
	struct efx_blinker blinker;
	struct i2c_client *hwmon_client, *ioexp_client;
};

#define STRING_TABLE_LOOKUP(val, member)	\
	member ## _names[val]

enum efx_int_mode {
	/* Be careful if altering to correct macro below */
	EFX_INT_MODE_MSIX = 0,
	EFX_INT_MODE_MSI = 1,
	EFX_INT_MODE_LEGACY = 2,
	EFX_INT_MODE_MAX	/* Insert any new items before this */
};
#define EFX_INT_MODE_USE_MSI(x) (((x)->interrupt_mode) <= EFX_INT_MODE_MSI)

enum phy_type {
	PHY_TYPE_NONE = 0,
	PHY_TYPE_TXC43128 = 1,
	PHY_TYPE_88E1111 = 2,
	PHY_TYPE_SFX7101 = 3,
	PHY_TYPE_QT2022C2 = 4,
	PHY_TYPE_PM8358 = 6,
	PHY_TYPE_SFT9001A = 8,
	PHY_TYPE_QT2025C = 9,
	PHY_TYPE_SFT9001B = 10,
	PHY_TYPE_MAX	/* Insert any new items before this */
};

#define PHY_ADDR_INVALID 0xff

#define EFX_IS10G(efx) ((efx)->link_speed == 10000)

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
	EFX_FC_RX = 1,
	EFX_FC_TX = 2,
	EFX_FC_AUTO = 4,
};

/* Supported MAC bit-mask */
enum efx_mac_type {
	EFX_GMAC = 1,
	EFX_XMAC = 2,
};

static inline unsigned int efx_fc_advertise(enum efx_fc_type wanted_fc)
{
	unsigned int adv = 0;
	if (wanted_fc & EFX_FC_RX)
		adv = ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
	if (wanted_fc & EFX_FC_TX)
		adv ^= ADVERTISE_PAUSE_ASYM;
	return adv;
}

static inline enum efx_fc_type efx_fc_resolve(enum efx_fc_type wanted_fc,
					      unsigned int lpa)
{
	unsigned int adv = efx_fc_advertise(wanted_fc);

	if (!(wanted_fc & EFX_FC_AUTO))
		return wanted_fc;

	if (adv & lpa & ADVERTISE_PAUSE_CAP)
		return EFX_FC_RX | EFX_FC_TX;
	if (adv & lpa & ADVERTISE_PAUSE_ASYM) {
		if (adv & ADVERTISE_PAUSE_CAP)
			return EFX_FC_RX;
		if (lpa & ADVERTISE_PAUSE_CAP)
			return EFX_FC_TX;
	}
	return 0;
}

/**
 * struct efx_mac_operations - Efx MAC operations table
 * @reconfigure: Reconfigure MAC. Serialised by the mac_lock
 * @update_stats: Update statistics
 * @irq: Hardware MAC event callback. Serialised by the mac_lock
 * @poll: Poll for hardware state. Serialised by the mac_lock
 */
struct efx_mac_operations {
	void (*reconfigure) (struct efx_nic *efx);
	void (*update_stats) (struct efx_nic *efx);
	void (*irq) (struct efx_nic *efx);
	void (*poll) (struct efx_nic *efx);
};

/**
 * struct efx_phy_operations - Efx PHY operations table
 * @init: Initialise PHY
 * @fini: Shut down PHY
 * @reconfigure: Reconfigure PHY (e.g. for new link parameters)
 * @clear_interrupt: Clear down interrupt
 * @blink: Blink LEDs
 * @poll: Poll for hardware state. Serialised by the mac_lock.
 * @get_settings: Get ethtool settings. Serialised by the mac_lock.
 * @set_settings: Set ethtool settings. Serialised by the mac_lock.
 * @set_npage_adv: Set abilities advertised in (Extended) Next Page
 *	(only needed where AN bit is set in mmds)
 * @num_tests: Number of PHY-specific tests/results
 * @test_names: Names of the tests/results
 * @run_tests: Run tests and record results as appropriate.
 *	Flags are the ethtool tests flags.
 * @mmds: MMD presence mask
 * @loopbacks: Supported loopback modes mask
 */
struct efx_phy_operations {
	enum efx_mac_type macs;
	int (*init) (struct efx_nic *efx);
	void (*fini) (struct efx_nic *efx);
	void (*reconfigure) (struct efx_nic *efx);
	void (*clear_interrupt) (struct efx_nic *efx);
	void (*poll) (struct efx_nic *efx);
	void (*get_settings) (struct efx_nic *efx,
			      struct ethtool_cmd *ecmd);
	int (*set_settings) (struct efx_nic *efx,
			     struct ethtool_cmd *ecmd);
	void (*set_npage_adv) (struct efx_nic *efx, u32);
	u32 num_tests;
	const char *const *test_names;
	int (*run_tests) (struct efx_nic *efx, int *results, unsigned flags);
	int mmds;
	unsigned loopbacks;
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

/**
 * struct efx_nic - an Efx NIC
 * @name: Device name (net device name or bus id before net device registered)
 * @pci_dev: The PCI device
 * @type: Controller type attributes
 * @legacy_irq: IRQ number
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
 * @i2c_adap: I2C adapter
 * @board_info: Board-level information
 * @state: Device state flag. Serialised by the rtnl_lock.
 * @reset_pending: Pending reset method (normally RESET_TYPE_NONE)
 * @tx_queue: TX DMA queues
 * @rx_queue: RX DMA queues
 * @channel: Channels
 * @n_rx_queues: Number of RX queues
 * @n_channels: Number of channels in use
 * @rx_buffer_len: RX buffer length
 * @rx_buffer_order: Order (log2) of number of pages for each RX buffer
 * @irq_status: Interrupt status buffer
 * @last_irq_cpu: Last CPU to handle interrupt.
 *	This register is written with the SMP processor ID whenever an
 *	interrupt is handled.  It is used by falcon_test_interrupt()
 *	to verify that an interrupt has occurred.
 * @spi_flash: SPI flash device
 *	This field will be %NULL if no flash device is present.
 * @spi_eeprom: SPI EEPROM device
 *	This field will be %NULL if no EEPROM device is present.
 * @spi_lock: SPI bus lock
 * @n_rx_nodesc_drop_cnt: RX no descriptor drop count
 * @nic_data: Hardware dependant state
 * @mac_lock: MAC access lock. Protects @port_enabled, @phy_mode,
 *	@port_inhibited, efx_monitor() and efx_reconfigure_port()
 * @port_enabled: Port enabled indicator.
 *	Serialises efx_stop_all(), efx_start_all(), efx_monitor(),
 *	efx_phy_work(), and efx_mac_work() with kernel interfaces. Safe to read
 *	under any one of the rtnl_lock, mac_lock, or netif_tx_lock, but all
 *	three must be held to modify it.
 * @port_inhibited: If set, the netif_carrier is always off. Hold the mac_lock
 * @port_initialized: Port initialized?
 * @net_dev: Operating system network device. Consider holding the rtnl lock
 * @rx_checksum_enabled: RX checksumming enabled
 * @netif_stop_count: Port stop count
 * @netif_stop_lock: Port stop lock
 * @mac_stats: MAC statistics. These include all statistics the MACs
 *	can provide.  Generic code converts these into a standard
 *	&struct net_device_stats.
 * @stats_buffer: DMA buffer for statistics
 * @stats_lock: Statistics update lock. Serialises statistics fetches
 * @stats_disable_count: Nest count for disabling statistics fetches
 * @mac_op: MAC interface
 * @mac_address: Permanent MAC address
 * @phy_type: PHY type
 * @phy_lock: PHY access lock
 * @phy_op: PHY interface
 * @phy_data: PHY private data (including PHY-specific stats)
 * @mii: PHY interface
 * @phy_mode: PHY operating mode. Serialised by @mac_lock.
 * @mac_up: MAC link state
 * @link_up: Link status
 * @link_fd: Link is full duplex
 * @link_fc: Actualy flow control flags
 * @link_speed: Link speed (Mbps)
 * @n_link_state_changes: Number of times the link has changed state
 * @promiscuous: Promiscuous flag. Protected by netif_tx_lock.
 * @multicast_hash: Multicast hash table
 * @wanted_fc: Wanted flow control flags
 * @phy_work: work item for dealing with PHY events
 * @mac_work: work item for dealing with MAC events
 * @loopback_mode: Loopback status
 * @loopback_modes: Supported loopback mode bitmask
 * @loopback_selftest: Offline self-test private state
 *
 * The @priv field of the corresponding &struct net_device points to
 * this.
 */
struct efx_nic {
	char name[IFNAMSIZ];
	struct pci_dev *pci_dev;
	const struct efx_nic_type *type;
	int legacy_irq;
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

	struct i2c_adapter i2c_adap;
	struct efx_board board_info;

	enum nic_state state;
	enum reset_type reset_pending;

	struct efx_tx_queue tx_queue[EFX_TX_QUEUE_COUNT];
	struct efx_rx_queue rx_queue[EFX_MAX_RX_QUEUES];
	struct efx_channel channel[EFX_MAX_CHANNELS];

	int n_rx_queues;
	int n_channels;
	unsigned int rx_buffer_len;
	unsigned int rx_buffer_order;

	struct efx_buffer irq_status;
	volatile signed int last_irq_cpu;

	struct efx_spi_device *spi_flash;
	struct efx_spi_device *spi_eeprom;
	struct mutex spi_lock;

	unsigned n_rx_nodesc_drop_cnt;

	struct falcon_nic_data *nic_data;

	struct mutex mac_lock;
	struct work_struct mac_work;
	bool port_enabled;
	bool port_inhibited;

	bool port_initialized;
	struct net_device *net_dev;
	bool rx_checksum_enabled;

	atomic_t netif_stop_count;
	spinlock_t netif_stop_lock;

	struct efx_mac_stats mac_stats;
	struct efx_buffer stats_buffer;
	spinlock_t stats_lock;
	unsigned int stats_disable_count;

	struct efx_mac_operations *mac_op;
	unsigned char mac_address[ETH_ALEN];

	enum phy_type phy_type;
	spinlock_t phy_lock;
	struct work_struct phy_work;
	struct efx_phy_operations *phy_op;
	void *phy_data;
	struct mii_if_info mii;
	enum efx_phy_mode phy_mode;

	bool mac_up;
	bool link_up;
	bool link_fd;
	enum efx_fc_type link_fc;
	unsigned int link_speed;
	unsigned int n_link_state_changes;

	bool promiscuous;
	union efx_multicast_hash multicast_hash;
	enum efx_fc_type wanted_fc;

	atomic_t rx_reset;
	enum efx_loopback_mode loopback_mode;
	unsigned int loopback_modes;

	void *loopback_selftest;
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

/**
 * struct efx_nic_type - Efx device type definition
 * @mem_bar: Memory BAR number
 * @mem_map_size: Memory BAR mapped size
 * @txd_ptr_tbl_base: TX descriptor ring base address
 * @rxd_ptr_tbl_base: RX descriptor ring base address
 * @buf_tbl_base: Buffer table base address
 * @evq_ptr_tbl_base: Event queue pointer table base address
 * @evq_rptr_tbl_base: Event queue read-pointer table base address
 * @txd_ring_mask: TX descriptor ring size - 1 (must be a power of two - 1)
 * @rxd_ring_mask: RX descriptor ring size - 1 (must be a power of two - 1)
 * @evq_size: Event queue size (must be a power of two)
 * @max_dma_mask: Maximum possible DMA mask
 * @tx_dma_mask: TX DMA mask
 * @bug5391_mask: Address mask for bug 5391 workaround
 * @rx_xoff_thresh: RX FIFO XOFF watermark (bytes)
 * @rx_xon_thresh: RX FIFO XON watermark (bytes)
 * @rx_buffer_padding: Padding added to each RX buffer
 * @max_interrupt_mode: Highest capability interrupt mode supported
 *	from &enum efx_init_mode.
 * @phys_addr_channels: Number of channels with physically addressed
 *	descriptors
 */
struct efx_nic_type {
	unsigned int mem_bar;
	unsigned int mem_map_size;
	unsigned int txd_ptr_tbl_base;
	unsigned int rxd_ptr_tbl_base;
	unsigned int buf_tbl_base;
	unsigned int evq_ptr_tbl_base;
	unsigned int evq_rptr_tbl_base;

	unsigned int txd_ring_mask;
	unsigned int rxd_ring_mask;
	unsigned int evq_size;
	u64 max_dma_mask;
	unsigned int tx_dma_mask;
	unsigned bug5391_mask;

	int rx_xoff_thresh;
	int rx_xon_thresh;
	unsigned int rx_buffer_padding;
	unsigned int max_interrupt_mode;
	unsigned int phys_addr_channels;
};

/**************************************************************************
 *
 * Prototypes and inline functions
 *
 *************************************************************************/

/* Iterate over all used channels */
#define efx_for_each_channel(_channel, _efx)				\
	for (_channel = &_efx->channel[0];				\
	     _channel < &_efx->channel[EFX_MAX_CHANNELS];		\
	     _channel++)						\
		if (!_channel->used_flags)				\
			continue;					\
		else

/* Iterate over all used TX queues */
#define efx_for_each_tx_queue(_tx_queue, _efx)				\
	for (_tx_queue = &_efx->tx_queue[0];				\
	     _tx_queue < &_efx->tx_queue[EFX_TX_QUEUE_COUNT];		\
	     _tx_queue++)

/* Iterate over all TX queues belonging to a channel */
#define efx_for_each_channel_tx_queue(_tx_queue, _channel)		\
	for (_tx_queue = &_channel->efx->tx_queue[0];			\
	     _tx_queue < &_channel->efx->tx_queue[EFX_TX_QUEUE_COUNT];	\
	     _tx_queue++)						\
		if (_tx_queue->channel != _channel)			\
			continue;					\
		else

/* Iterate over all used RX queues */
#define efx_for_each_rx_queue(_rx_queue, _efx)				\
	for (_rx_queue = &_efx->rx_queue[0];				\
	     _rx_queue < &_efx->rx_queue[_efx->n_rx_queues];		\
	     _rx_queue++)

/* Iterate over all RX queues belonging to a channel */
#define efx_for_each_channel_rx_queue(_rx_queue, _channel)		\
	for (_rx_queue = &_channel->efx->rx_queue[_channel->channel];	\
	     _rx_queue;							\
	     _rx_queue = NULL)						\
		if (_rx_queue->channel != _channel)			\
			continue;					\
		else

/* Returns a pointer to the specified receive buffer in the RX
 * descriptor queue.
 */
static inline struct efx_rx_buffer *efx_rx_buffer(struct efx_rx_queue *rx_queue,
						  unsigned int index)
{
	return (&rx_queue->buffer[index]);
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
 * The 10G MAC used in Falcon requires 8-byte alignment on the frame
 * length, so we round up to the nearest 8.
 */
#define EFX_MAX_FRAME_LEN(mtu) \
	((((mtu) + ETH_HLEN + VLAN_HLEN + 4/* FCS */) + 7) & ~7)


#endif /* EFX_NET_DRIVER_H */

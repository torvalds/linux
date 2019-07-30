/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2005-2013 Solarflare Communications Inc.
 */

/* Common definitions for all Efx net driver code */

#ifndef EF4_NET_DRIVER_H
#define EF4_NET_DRIVER_H

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
#include <linux/i2c.h>
#include <linux/mtd/mtd.h>
#include <net/busy_poll.h>

#include "enum.h"
#include "bitfield.h"
#include "filter.h"

/**************************************************************************
 *
 * Build definitions
 *
 **************************************************************************/

#define EF4_DRIVER_VERSION	"4.1"

#ifdef DEBUG
#define EF4_BUG_ON_PARANOID(x) BUG_ON(x)
#define EF4_WARN_ON_PARANOID(x) WARN_ON(x)
#else
#define EF4_BUG_ON_PARANOID(x) do {} while (0)
#define EF4_WARN_ON_PARANOID(x) do {} while (0)
#endif

/**************************************************************************
 *
 * Efx data structures
 *
 **************************************************************************/

#define EF4_MAX_CHANNELS 32U
#define EF4_MAX_RX_QUEUES EF4_MAX_CHANNELS
#define EF4_EXTRA_CHANNEL_IOV	0
#define EF4_EXTRA_CHANNEL_PTP	1
#define EF4_MAX_EXTRA_CHANNELS	2U

/* Checksum generation is a per-queue option in hardware, so each
 * queue visible to the networking core is backed by two hardware TX
 * queues. */
#define EF4_MAX_TX_TC		2
#define EF4_MAX_CORE_TX_QUEUES	(EF4_MAX_TX_TC * EF4_MAX_CHANNELS)
#define EF4_TXQ_TYPE_OFFLOAD	1	/* flag */
#define EF4_TXQ_TYPE_HIGHPRI	2	/* flag */
#define EF4_TXQ_TYPES		4
#define EF4_MAX_TX_QUEUES	(EF4_TXQ_TYPES * EF4_MAX_CHANNELS)

/* Maximum possible MTU the driver supports */
#define EF4_MAX_MTU (9 * 1024)

/* Minimum MTU, from RFC791 (IP) */
#define EF4_MIN_MTU 68

/* Size of an RX scatter buffer.  Small enough to pack 2 into a 4K page,
 * and should be a multiple of the cache line size.
 */
#define EF4_RX_USR_BUF_SIZE	(2048 - 256)

/* If possible, we should ensure cache line alignment at start and end
 * of every buffer.  Otherwise, we just need to ensure 4-byte
 * alignment of the network header.
 */
#if NET_IP_ALIGN == 0
#define EF4_RX_BUF_ALIGNMENT	L1_CACHE_BYTES
#else
#define EF4_RX_BUF_ALIGNMENT	4
#endif

struct ef4_self_tests;

/**
 * struct ef4_buffer - A general-purpose DMA buffer
 * @addr: host base address of the buffer
 * @dma_addr: DMA base address of the buffer
 * @len: Buffer length, in bytes
 *
 * The NIC uses these buffers for its interrupt status registers and
 * MAC stats dumps.
 */
struct ef4_buffer {
	void *addr;
	dma_addr_t dma_addr;
	unsigned int len;
};

/**
 * struct ef4_special_buffer - DMA buffer entered into buffer table
 * @buf: Standard &struct ef4_buffer
 * @index: Buffer index within controller;s buffer table
 * @entries: Number of buffer table entries
 *
 * The NIC has a buffer table that maps buffers of size %EF4_BUF_SIZE.
 * Event and descriptor rings are addressed via one or more buffer
 * table entries (and so can be physically non-contiguous, although we
 * currently do not take advantage of that).  On Falcon and Siena we
 * have to take care of allocating and initialising the entries
 * ourselves.  On later hardware this is managed by the firmware and
 * @index and @entries are left as 0.
 */
struct ef4_special_buffer {
	struct ef4_buffer buf;
	unsigned int index;
	unsigned int entries;
};

/**
 * struct ef4_tx_buffer - buffer state for a TX descriptor
 * @skb: When @flags & %EF4_TX_BUF_SKB, the associated socket buffer to be
 *	freed when descriptor completes
 * @option: When @flags & %EF4_TX_BUF_OPTION, a NIC-specific option descriptor.
 * @dma_addr: DMA address of the fragment.
 * @flags: Flags for allocation and DMA mapping type
 * @len: Length of this fragment.
 *	This field is zero when the queue slot is empty.
 * @unmap_len: Length of this fragment to unmap
 * @dma_offset: Offset of @dma_addr from the address of the backing DMA mapping.
 * Only valid if @unmap_len != 0.
 */
struct ef4_tx_buffer {
	const struct sk_buff *skb;
	union {
		ef4_qword_t option;
		dma_addr_t dma_addr;
	};
	unsigned short flags;
	unsigned short len;
	unsigned short unmap_len;
	unsigned short dma_offset;
};
#define EF4_TX_BUF_CONT		1	/* not last descriptor of packet */
#define EF4_TX_BUF_SKB		2	/* buffer is last part of skb */
#define EF4_TX_BUF_MAP_SINGLE	8	/* buffer was mapped with dma_map_single() */
#define EF4_TX_BUF_OPTION	0x10	/* empty buffer for option descriptor */

/**
 * struct ef4_tx_queue - An Efx TX queue
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
 * @cb_page: Array of pages of copy buffers.  Carved up according to
 *	%EF4_TX_CB_ORDER into %EF4_TX_CB_SIZE-sized chunks.
 * @txd: The hardware descriptor ring
 * @ptr_mask: The size of the ring minus 1.
 * @initialised: Has hardware queue been initialised?
 * @tx_min_size: Minimum transmit size for this queue. Depends on HW.
 * @read_count: Current read pointer.
 *	This is the number of buffers that have been removed from both rings.
 * @old_write_count: The value of @write_count when last checked.
 *	This is here for performance reasons.  The xmit path will
 *	only get the up-to-date value of @write_count if this
 *	variable indicates that the queue is empty.  This is to
 *	avoid cache-line ping-pong between the xmit path and the
 *	completion path.
 * @merge_events: Number of TX merged completion events
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
 * @pushes: Number of times the TX push feature has been used
 * @xmit_more_available: Are any packets waiting to be pushed to the NIC
 * @cb_packets: Number of times the TX copybreak feature has been used
 * @empty_read_count: If the completion path has seen the queue as empty
 *	and the transmission path has not yet checked this, the value of
 *	@read_count bitwise-added to %EF4_EMPTY_COUNT_VALID; otherwise 0.
 */
struct ef4_tx_queue {
	/* Members which don't change on the fast path */
	struct ef4_nic *efx ____cacheline_aligned_in_smp;
	unsigned queue;
	struct ef4_channel *channel;
	struct netdev_queue *core_txq;
	struct ef4_tx_buffer *buffer;
	struct ef4_buffer *cb_page;
	struct ef4_special_buffer txd;
	unsigned int ptr_mask;
	bool initialised;
	unsigned int tx_min_size;

	/* Function pointers used in the fast path. */
	int (*handle_tso)(struct ef4_tx_queue*, struct sk_buff*, bool *);

	/* Members used mainly on the completion path */
	unsigned int read_count ____cacheline_aligned_in_smp;
	unsigned int old_write_count;
	unsigned int merge_events;
	unsigned int bytes_compl;
	unsigned int pkts_compl;

	/* Members used only on the xmit path */
	unsigned int insert_count ____cacheline_aligned_in_smp;
	unsigned int write_count;
	unsigned int old_read_count;
	unsigned int pushes;
	bool xmit_more_available;
	unsigned int cb_packets;
	/* Statistics to supplement MAC stats */
	unsigned long tx_packets;

	/* Members shared between paths and sometimes updated */
	unsigned int empty_read_count ____cacheline_aligned_in_smp;
#define EF4_EMPTY_COUNT_VALID 0x80000000
	atomic_t flush_outstanding;
};

#define EF4_TX_CB_ORDER	7
#define EF4_TX_CB_SIZE	(1 << EF4_TX_CB_ORDER) - NET_IP_ALIGN

/**
 * struct ef4_rx_buffer - An Efx RX data buffer
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
struct ef4_rx_buffer {
	dma_addr_t dma_addr;
	struct page *page;
	u16 page_offset;
	u16 len;
	u16 flags;
};
#define EF4_RX_BUF_LAST_IN_PAGE	0x0001
#define EF4_RX_PKT_CSUMMED	0x0002
#define EF4_RX_PKT_DISCARD	0x0004
#define EF4_RX_PKT_TCP		0x0040
#define EF4_RX_PKT_PREFIX_LEN	0x0080	/* length is in prefix only */

/**
 * struct ef4_rx_page_state - Page-based rx buffer state
 *
 * Inserted at the start of every page allocated for receive buffers.
 * Used to facilitate sharing dma mappings between recycled rx buffers
 * and those passed up to the kernel.
 *
 * @dma_addr: The dma address of this page.
 */
struct ef4_rx_page_state {
	dma_addr_t dma_addr;

	unsigned int __pad[0] ____cacheline_aligned;
};

/**
 * struct ef4_rx_queue - An Efx RX queue
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
 * @slow_fill: Timer used to defer ef4_nic_generate_fill_event().
 */
struct ef4_rx_queue {
	struct ef4_nic *efx;
	int core_index;
	struct ef4_rx_buffer *buffer;
	struct ef4_special_buffer rxd;
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
};

/**
 * struct ef4_channel - An Efx channel
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
 * @rx_pkt_n_frags: Number of fragments in next packet to be delivered by
 *	__ef4_rx_packet(), or zero if there is none
 * @rx_pkt_index: Ring index of first buffer for next packet to be delivered
 *	by __ef4_rx_packet(), if @rx_pkt_n_frags != 0
 * @rx_queue: RX queue for this channel
 * @tx_queue: TX queues for this channel
 */
struct ef4_channel {
	struct ef4_nic *efx;
	int channel;
	const struct ef4_channel_type *type;
	bool eventq_init;
	bool enabled;
	int irq;
	unsigned int irq_moderation_us;
	struct net_device *napi_dev;
	struct napi_struct napi_str;
#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned long busy_poll_state;
#endif
	struct ef4_special_buffer eventq;
	unsigned int eventq_mask;
	unsigned int eventq_read_ptr;
	int event_test_cpu;

	unsigned int irq_count;
	unsigned int irq_mod_score;
#ifdef CONFIG_RFS_ACCEL
	unsigned int rfs_filters_added;
#define RPS_FLOW_ID_INVALID 0xFFFFFFFF
	u32 *rps_flow_id;
#endif

	unsigned n_rx_tobe_disc;
	unsigned n_rx_ip_hdr_chksum_err;
	unsigned n_rx_tcp_udp_chksum_err;
	unsigned n_rx_mcast_mismatch;
	unsigned n_rx_frm_trunc;
	unsigned n_rx_overlength;
	unsigned n_skbuff_leaks;
	unsigned int n_rx_nodesc_trunc;
	unsigned int n_rx_merge_events;
	unsigned int n_rx_merge_packets;

	unsigned int rx_pkt_n_frags;
	unsigned int rx_pkt_index;

	struct ef4_rx_queue rx_queue;
	struct ef4_tx_queue tx_queue[EF4_TXQ_TYPES];
};

/**
 * struct ef4_msi_context - Context for each MSI
 * @efx: The associated NIC
 * @index: Index of the channel/IRQ
 * @name: Name of the channel/IRQ
 *
 * Unlike &struct ef4_channel, this is never reallocated and is always
 * safe for the IRQ handler to access.
 */
struct ef4_msi_context {
	struct ef4_nic *efx;
	unsigned int index;
	char name[IFNAMSIZ + 6];
};

/**
 * struct ef4_channel_type - distinguishes traffic and extra channels
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
struct ef4_channel_type {
	void (*handle_no_channel)(struct ef4_nic *);
	int (*pre_probe)(struct ef4_channel *);
	void (*post_remove)(struct ef4_channel *);
	void (*get_name)(struct ef4_channel *, char *buf, size_t len);
	struct ef4_channel *(*copy)(const struct ef4_channel *);
	bool (*receive_skb)(struct ef4_channel *, struct sk_buff *);
	bool keep_eventq;
};

enum ef4_led_mode {
	EF4_LED_OFF	= 0,
	EF4_LED_ON	= 1,
	EF4_LED_DEFAULT	= 2
};

#define STRING_TABLE_LOOKUP(val, member) \
	((val) < member ## _max) ? member ## _names[val] : "(invalid)"

extern const char *const ef4_loopback_mode_names[];
extern const unsigned int ef4_loopback_mode_max;
#define LOOPBACK_MODE(efx) \
	STRING_TABLE_LOOKUP((efx)->loopback_mode, ef4_loopback_mode)

extern const char *const ef4_reset_type_names[];
extern const unsigned int ef4_reset_type_max;
#define RESET_TYPE(type) \
	STRING_TABLE_LOOKUP(type, ef4_reset_type)

enum ef4_int_mode {
	/* Be careful if altering to correct macro below */
	EF4_INT_MODE_MSIX = 0,
	EF4_INT_MODE_MSI = 1,
	EF4_INT_MODE_LEGACY = 2,
	EF4_INT_MODE_MAX	/* Insert any new items before this */
};
#define EF4_INT_MODE_USE_MSI(x) (((x)->interrupt_mode) <= EF4_INT_MODE_MSI)

enum nic_state {
	STATE_UNINIT = 0,	/* device being probed/removed or is frozen */
	STATE_READY = 1,	/* hardware ready and netdev registered */
	STATE_DISABLED = 2,	/* device disabled due to hardware errors */
	STATE_RECOVERY = 3,	/* device recovering from PCI error */
};

/* Forward declaration */
struct ef4_nic;

/* Pseudo bit-mask flow control field */
#define EF4_FC_RX	FLOW_CTRL_RX
#define EF4_FC_TX	FLOW_CTRL_TX
#define EF4_FC_AUTO	4

/**
 * struct ef4_link_state - Current state of the link
 * @up: Link is up
 * @fd: Link is full-duplex
 * @fc: Actual flow control flags
 * @speed: Link speed (Mbps)
 */
struct ef4_link_state {
	bool up;
	bool fd;
	u8 fc;
	unsigned int speed;
};

static inline bool ef4_link_state_equal(const struct ef4_link_state *left,
					const struct ef4_link_state *right)
{
	return left->up == right->up && left->fd == right->fd &&
		left->fc == right->fc && left->speed == right->speed;
}

/**
 * struct ef4_phy_operations - Efx PHY operations table
 * @probe: Probe PHY and initialise efx->mdio.mode_support, efx->mdio.mmds,
 *	efx->loopback_modes.
 * @init: Initialise PHY
 * @fini: Shut down PHY
 * @reconfigure: Reconfigure PHY (e.g. for new link parameters)
 * @poll: Update @link_state and report whether it changed.
 *	Serialised by the mac_lock.
 * @get_link_ksettings: Get ethtool settings. Serialised by the mac_lock.
 * @set_link_ksettings: Set ethtool settings. Serialised by the mac_lock.
 * @set_npage_adv: Set abilities advertised in (Extended) Next Page
 *	(only needed where AN bit is set in mmds)
 * @test_alive: Test that PHY is 'alive' (online)
 * @test_name: Get the name of a PHY-specific test/result
 * @run_tests: Run tests and record results as appropriate (offline).
 *	Flags are the ethtool tests flags.
 */
struct ef4_phy_operations {
	int (*probe) (struct ef4_nic *efx);
	int (*init) (struct ef4_nic *efx);
	void (*fini) (struct ef4_nic *efx);
	void (*remove) (struct ef4_nic *efx);
	int (*reconfigure) (struct ef4_nic *efx);
	bool (*poll) (struct ef4_nic *efx);
	void (*get_link_ksettings)(struct ef4_nic *efx,
				   struct ethtool_link_ksettings *cmd);
	int (*set_link_ksettings)(struct ef4_nic *efx,
				  const struct ethtool_link_ksettings *cmd);
	void (*set_npage_adv) (struct ef4_nic *efx, u32);
	int (*test_alive) (struct ef4_nic *efx);
	const char *(*test_name) (struct ef4_nic *efx, unsigned int index);
	int (*run_tests) (struct ef4_nic *efx, int *results, unsigned flags);
	int (*get_module_eeprom) (struct ef4_nic *efx,
			       struct ethtool_eeprom *ee,
			       u8 *data);
	int (*get_module_info) (struct ef4_nic *efx,
				struct ethtool_modinfo *modinfo);
};

/**
 * enum ef4_phy_mode - PHY operating mode flags
 * @PHY_MODE_NORMAL: on and should pass traffic
 * @PHY_MODE_TX_DISABLED: on with TX disabled
 * @PHY_MODE_LOW_POWER: set to low power through MDIO
 * @PHY_MODE_OFF: switched off through external control
 * @PHY_MODE_SPECIAL: on but will not pass traffic
 */
enum ef4_phy_mode {
	PHY_MODE_NORMAL		= 0,
	PHY_MODE_TX_DISABLED	= 1,
	PHY_MODE_LOW_POWER	= 2,
	PHY_MODE_OFF		= 4,
	PHY_MODE_SPECIAL	= 8,
};

static inline bool ef4_phy_mode_disabled(enum ef4_phy_mode mode)
{
	return !!(mode & ~PHY_MODE_TX_DISABLED);
}

/**
 * struct ef4_hw_stat_desc - Description of a hardware statistic
 * @name: Name of the statistic as visible through ethtool, or %NULL if
 *	it should not be exposed
 * @dma_width: Width in bits (0 for non-DMA statistics)
 * @offset: Offset within stats (ignored for non-DMA statistics)
 */
struct ef4_hw_stat_desc {
	const char *name;
	u16 dma_width;
	u16 offset;
};

/* Number of bits used in a multicast filter hash address */
#define EF4_MCAST_HASH_BITS 8

/* Number of (single-bit) entries in a multicast filter hash */
#define EF4_MCAST_HASH_ENTRIES (1 << EF4_MCAST_HASH_BITS)

/* An Efx multicast filter hash */
union ef4_multicast_hash {
	u8 byte[EF4_MCAST_HASH_ENTRIES / 8];
	ef4_oword_t oword[EF4_MCAST_HASH_ENTRIES / sizeof(ef4_oword_t) / 8];
};

/**
 * struct ef4_nic - an Efx NIC
 * @name: Device name (net device name or bus id before net device registered)
 * @pci_dev: The PCI device
 * @node: List node for maintaning primary/secondary function lists
 * @primary: &struct ef4_nic instance for the primary function of this
 *	controller.  May be the same structure, and may be %NULL if no
 *	primary function is bound.  Serialised by rtnl_lock.
 * @secondary_list: List of &struct ef4_nic instances for the secondary PCI
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
 * @interrupt_mode: Interrupt mode
 * @timer_quantum_ns: Interrupt timer quantum, in nanoseconds
 * @timer_max_ns: Interrupt timer maximum value, in nanoseconds
 * @irq_rx_adaptive: Adaptive IRQ moderation enabled for RX event queues
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
 *	(valid only for NICs that set %EF4_RX_PKT_PREFIX_LEN; always negative)
 * @rx_packet_ts_offset: Offset of timestamp from start of packet data
 *	(valid only if channel->sync_timestamps_enabled; always negative)
 * @rx_hash_key: Toeplitz hash key for RSS
 * @rx_indir_table: Indirection table for RSS
 * @rx_scatter: Scatter mode enabled for receives
 * @int_error_count: Number of internal errors seen recently
 * @int_error_expire: Time at which error count will be expired
 * @irq_soft_enabled: Are IRQs soft-enabled? If not, IRQ handler will
 *	acknowledge but do nothing else.
 * @irq_status: Interrupt status buffer
 * @irq_zero_count: Number of legacy IRQs seen with queue flags == 0
 * @irq_level: IRQ level/index for IRQs not triggered by an event queue
 * @selftest_work: Work item for asynchronous self-test
 * @mtd_list: List of MTDs attached to the NIC
 * @nic_data: Hardware dependent state
 * @mac_lock: MAC access lock. Protects @port_enabled, @phy_mode,
 *	ef4_monitor() and ef4_reconfigure_port()
 * @port_enabled: Port enabled indicator.
 *	Serialises ef4_stop_all(), ef4_start_all(), ef4_monitor() and
 *	ef4_mac_work() with kernel interfaces. Safe to read under any
 *	one of the rtnl_lock, mac_lock, or netif_tx_lock, but all three must
 *	be held to modify it.
 * @port_initialized: Port initialized?
 * @net_dev: Operating system network device. Consider holding the rtnl lock
 * @fixed_features: Features which cannot be turned off
 * @stats_buffer: DMA buffer for statistics
 * @phy_type: PHY type
 * @phy_op: PHY interface
 * @phy_data: PHY private data (including PHY-specific stats)
 * @mdio: PHY MDIO interface
 * @phy_mode: PHY operating mode. Serialised by @mac_lock.
 * @link_advertising: Autonegotiation advertising flags
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
 * @filter_sem: Filter table rw_semaphore, for freeing the table
 * @filter_lock: Filter table lock, for mere content changes
 * @filter_state: Architecture-dependent filter table state
 * @rps_expire_channel: Next channel to check for expiry
 * @rps_expire_index: Next index to check for expiry in
 *	@rps_expire_channel's @rps_flow_id
 * @active_queues: Count of RX and TX queues that haven't been flushed and drained.
 * @rxq_flush_pending: Count of number of receive queues that need to be flushed.
 *	Decremented when the ef4_flush_rx_queue() is called.
 * @rxq_flush_outstanding: Count of number of RX flushes started but not yet
 *	completed (either success or failure). Not used when MCDI is used to
 *	flush receive queues.
 * @flush_wq: wait queue used by ef4_nic_flush_queues() to wait for flush completions.
 * @vpd_sn: Serial number read from VPD
 * @monitor_work: Hardware monitor workitem
 * @biu_lock: BIU (bus interface unit) lock
 * @last_irq_cpu: Last CPU to handle a possible test interrupt.  This
 *	field is used by ef4_test_interrupts() to verify that an
 *	interrupt has occurred.
 * @stats_lock: Statistics update lock. Must be held when calling
 *	ef4_nic_type::{update,start,stop}_stats.
 * @n_rx_noskb_drops: Count of RX packets dropped due to failure to allocate an skb
 *
 * This is stored in the private area of the &struct net_device.
 */
struct ef4_nic {
	/* The following fields should be written very rarely */

	char name[IFNAMSIZ];
	struct list_head node;
	struct ef4_nic *primary;
	struct list_head secondary_list;
	struct pci_dev *pci_dev;
	unsigned int port_num;
	const struct ef4_nic_type *type;
	int legacy_irq;
	bool eeh_disabled_legacy_irq;
	struct workqueue_struct *workqueue;
	char workqueue_name[16];
	struct work_struct reset_work;
	resource_size_t membase_phys;
	void __iomem *membase;

	enum ef4_int_mode interrupt_mode;
	unsigned int timer_quantum_ns;
	unsigned int timer_max_ns;
	bool irq_rx_adaptive;
	unsigned int irq_mod_step_us;
	unsigned int irq_rx_moderation_us;
	u32 msg_enable;

	enum nic_state state;
	unsigned long reset_pending;

	struct ef4_channel *channel[EF4_MAX_CHANNELS];
	struct ef4_msi_context msi_context[EF4_MAX_CHANNELS];
	const struct ef4_channel_type *
	extra_channel_type[EF4_MAX_EXTRA_CHANNELS];

	unsigned rxq_entries;
	unsigned txq_entries;
	unsigned int txq_stop_thresh;
	unsigned int txq_wake_thresh;

	unsigned tx_dc_base;
	unsigned rx_dc_base;
	unsigned sram_lim_qw;
	unsigned next_buffer_table;

	unsigned int max_channels;
	unsigned int max_tx_channels;
	unsigned n_channels;
	unsigned n_rx_channels;
	unsigned rss_spread;
	unsigned tx_channel_offset;
	unsigned n_tx_channels;
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
	u8 rx_hash_key[40];
	u32 rx_indir_table[128];
	bool rx_scatter;

	unsigned int_error_count;
	unsigned long int_error_expire;

	bool irq_soft_enabled;
	struct ef4_buffer irq_status;
	unsigned irq_zero_count;
	unsigned irq_level;
	struct delayed_work selftest_work;

#ifdef CONFIG_SFC_FALCON_MTD
	struct list_head mtd_list;
#endif

	void *nic_data;

	struct mutex mac_lock;
	struct work_struct mac_work;
	bool port_enabled;

	bool mc_bist_for_other_fn;
	bool port_initialized;
	struct net_device *net_dev;

	netdev_features_t fixed_features;

	struct ef4_buffer stats_buffer;
	u64 rx_nodesc_drops_total;
	u64 rx_nodesc_drops_while_down;
	bool rx_nodesc_drops_prev_state;

	unsigned int phy_type;
	const struct ef4_phy_operations *phy_op;
	void *phy_data;
	struct mdio_if_info mdio;
	enum ef4_phy_mode phy_mode;

	u32 link_advertising;
	struct ef4_link_state link_state;
	unsigned int n_link_state_changes;

	bool unicast_filter;
	union ef4_multicast_hash multicast_hash;
	u8 wanted_fc;
	unsigned fc_disable;

	atomic_t rx_reset;
	enum ef4_loopback_mode loopback_mode;
	u64 loopback_modes;

	void *loopback_selftest;

	struct rw_semaphore filter_sem;
	spinlock_t filter_lock;
	void *filter_state;
#ifdef CONFIG_RFS_ACCEL
	unsigned int rps_expire_channel;
	unsigned int rps_expire_index;
#endif

	atomic_t active_queues;
	atomic_t rxq_flush_pending;
	atomic_t rxq_flush_outstanding;
	wait_queue_head_t flush_wq;

	char *vpd_sn;

	/* The following fields may be written more often */

	struct delayed_work monitor_work ____cacheline_aligned_in_smp;
	spinlock_t biu_lock;
	int last_irq_cpu;
	spinlock_t stats_lock;
	atomic_t n_rx_noskb_drops;
};

static inline int ef4_dev_registered(struct ef4_nic *efx)
{
	return efx->net_dev->reg_state == NETREG_REGISTERED;
}

static inline unsigned int ef4_port_num(struct ef4_nic *efx)
{
	return efx->port_num;
}

struct ef4_mtd_partition {
	struct list_head node;
	struct mtd_info mtd;
	const char *dev_type_name;
	const char *type_name;
	char name[IFNAMSIZ + 20];
};

/**
 * struct ef4_nic_type - Efx device type definition
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
 * @start_stats: Start the regular fetching of statistics
 * @pull_stats: Pull stats from the NIC and wait until they arrive.
 * @stop_stats: Stop the regular fetching of statistics
 * @set_id_led: Set state of identifying LED or revert to automatic function
 * @push_irq_moderation: Apply interrupt moderation value
 * @reconfigure_port: Push loopback/power/txdis changes to the MAC and PHY
 * @prepare_enable_fc_tx: Prepare MAC to enable pause frame TX (may be %NULL)
 * @reconfigure_mac: Push MAC address, MTU, flow control and filter settings
 *	to the hardware.  Serialised by the mac_lock.
 * @check_mac_fault: Check MAC fault state. True if fault present.
 * @get_wol: Get WoL configuration from driver state
 * @set_wol: Push WoL configuration to the NIC
 * @resume_wol: Synchronise WoL state between driver and MC (e.g. after resume)
 * @test_chip: Test registers.  May use ef4_farch_test_registers(), and is
 *	expected to reset the NIC.
 * @test_nvram: Test validity of NVRAM contents
 * @irq_enable_master: Enable IRQs on the NIC.  Each event queue must
 *	be separately enabled after this.
 * @irq_test_generate: Generate a test IRQ
 * @irq_disable_non_ev: Disable non-event IRQs on the NIC.  Each event
 *	queue must be separately disabled before this.
 * @irq_handle_msi: Handle MSI for a channel.  The @dev_id argument is
 *	a pointer to the &struct ef4_msi_context for the channel.
 * @irq_handle_legacy: Handle legacy interrupt.  The @dev_id argument
 *	is a pointer to the &struct ef4_nic.
 * @tx_probe: Allocate resources for TX queue
 * @tx_init: Initialise TX queue on the NIC
 * @tx_remove: Free resources for TX queue
 * @tx_write: Write TX descriptors and doorbell
 * @rx_push_rss_config: Write RSS hash key and indirection table to the NIC
 * @rx_probe: Allocate resources for RX queue
 * @rx_init: Initialise RX queue on the NIC
 * @rx_remove: Free resources for RX queue
 * @rx_write: Write RX descriptors and doorbell
 * @rx_defer_refill: Generate a refill reminder event
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
 *	equal to the given priority and is not %EF4_FILTER_PRI_AUTO
 * @filter_count_rx_used: Get the number of filters in use at a given priority
 * @filter_get_rx_id_limit: Get maximum value of a filter id, plus 1
 * @filter_get_rx_ids: Get list of RX filters at a given priority
 * @filter_rfs_insert: Add or replace a filter for RFS.  This must be
 *	atomic.  The hardware change may be asynchronous but should
 *	not be delayed for long.  It may fail if this can't be done
 *	atomically.
 * @filter_rfs_expire_one: Consider expiring a filter inserted for RFS.
 *	This must check whether the specified table entry is used by RFS
 *	and that rps_may_expire_flow() returns true for it.
 * @mtd_probe: Probe and add MTD partitions associated with this net device,
 *	 using ef4_mtd_add()
 * @mtd_rename: Set an MTD partition name using the net device name
 * @mtd_read: Read from an MTD partition
 * @mtd_erase: Erase part of an MTD partition
 * @mtd_write: Write to an MTD partition
 * @mtd_sync: Wait for write-back to complete on MTD partition.  This
 *	also notifies the driver that a writer has finished using this
 *	partition.
 * @set_mac_address: Set the MAC address of the device
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
 * @max_interrupt_mode: Highest capability interrupt mode supported
 *	from &enum ef4_init_mode.
 * @timer_period_max: Maximum period of interrupt timer (in ticks)
 * @offload_features: net_device feature flags for protocol offload
 *	features implemented in hardware
 */
struct ef4_nic_type {
	unsigned int mem_bar;
	unsigned int (*mem_map_size)(struct ef4_nic *efx);
	int (*probe)(struct ef4_nic *efx);
	void (*remove)(struct ef4_nic *efx);
	int (*init)(struct ef4_nic *efx);
	int (*dimension_resources)(struct ef4_nic *efx);
	void (*fini)(struct ef4_nic *efx);
	void (*monitor)(struct ef4_nic *efx);
	enum reset_type (*map_reset_reason)(enum reset_type reason);
	int (*map_reset_flags)(u32 *flags);
	int (*reset)(struct ef4_nic *efx, enum reset_type method);
	int (*probe_port)(struct ef4_nic *efx);
	void (*remove_port)(struct ef4_nic *efx);
	bool (*handle_global_event)(struct ef4_channel *channel, ef4_qword_t *);
	int (*fini_dmaq)(struct ef4_nic *efx);
	void (*prepare_flush)(struct ef4_nic *efx);
	void (*finish_flush)(struct ef4_nic *efx);
	void (*prepare_flr)(struct ef4_nic *efx);
	void (*finish_flr)(struct ef4_nic *efx);
	size_t (*describe_stats)(struct ef4_nic *efx, u8 *names);
	size_t (*update_stats)(struct ef4_nic *efx, u64 *full_stats,
			       struct rtnl_link_stats64 *core_stats);
	void (*start_stats)(struct ef4_nic *efx);
	void (*pull_stats)(struct ef4_nic *efx);
	void (*stop_stats)(struct ef4_nic *efx);
	void (*set_id_led)(struct ef4_nic *efx, enum ef4_led_mode mode);
	void (*push_irq_moderation)(struct ef4_channel *channel);
	int (*reconfigure_port)(struct ef4_nic *efx);
	void (*prepare_enable_fc_tx)(struct ef4_nic *efx);
	int (*reconfigure_mac)(struct ef4_nic *efx);
	bool (*check_mac_fault)(struct ef4_nic *efx);
	void (*get_wol)(struct ef4_nic *efx, struct ethtool_wolinfo *wol);
	int (*set_wol)(struct ef4_nic *efx, u32 type);
	void (*resume_wol)(struct ef4_nic *efx);
	int (*test_chip)(struct ef4_nic *efx, struct ef4_self_tests *tests);
	int (*test_nvram)(struct ef4_nic *efx);
	void (*irq_enable_master)(struct ef4_nic *efx);
	int (*irq_test_generate)(struct ef4_nic *efx);
	void (*irq_disable_non_ev)(struct ef4_nic *efx);
	irqreturn_t (*irq_handle_msi)(int irq, void *dev_id);
	irqreturn_t (*irq_handle_legacy)(int irq, void *dev_id);
	int (*tx_probe)(struct ef4_tx_queue *tx_queue);
	void (*tx_init)(struct ef4_tx_queue *tx_queue);
	void (*tx_remove)(struct ef4_tx_queue *tx_queue);
	void (*tx_write)(struct ef4_tx_queue *tx_queue);
	unsigned int (*tx_limit_len)(struct ef4_tx_queue *tx_queue,
				     dma_addr_t dma_addr, unsigned int len);
	int (*rx_push_rss_config)(struct ef4_nic *efx, bool user,
				  const u32 *rx_indir_table);
	int (*rx_probe)(struct ef4_rx_queue *rx_queue);
	void (*rx_init)(struct ef4_rx_queue *rx_queue);
	void (*rx_remove)(struct ef4_rx_queue *rx_queue);
	void (*rx_write)(struct ef4_rx_queue *rx_queue);
	void (*rx_defer_refill)(struct ef4_rx_queue *rx_queue);
	int (*ev_probe)(struct ef4_channel *channel);
	int (*ev_init)(struct ef4_channel *channel);
	void (*ev_fini)(struct ef4_channel *channel);
	void (*ev_remove)(struct ef4_channel *channel);
	int (*ev_process)(struct ef4_channel *channel, int quota);
	void (*ev_read_ack)(struct ef4_channel *channel);
	void (*ev_test_generate)(struct ef4_channel *channel);
	int (*filter_table_probe)(struct ef4_nic *efx);
	void (*filter_table_restore)(struct ef4_nic *efx);
	void (*filter_table_remove)(struct ef4_nic *efx);
	void (*filter_update_rx_scatter)(struct ef4_nic *efx);
	s32 (*filter_insert)(struct ef4_nic *efx,
			     struct ef4_filter_spec *spec, bool replace);
	int (*filter_remove_safe)(struct ef4_nic *efx,
				  enum ef4_filter_priority priority,
				  u32 filter_id);
	int (*filter_get_safe)(struct ef4_nic *efx,
			       enum ef4_filter_priority priority,
			       u32 filter_id, struct ef4_filter_spec *);
	int (*filter_clear_rx)(struct ef4_nic *efx,
			       enum ef4_filter_priority priority);
	u32 (*filter_count_rx_used)(struct ef4_nic *efx,
				    enum ef4_filter_priority priority);
	u32 (*filter_get_rx_id_limit)(struct ef4_nic *efx);
	s32 (*filter_get_rx_ids)(struct ef4_nic *efx,
				 enum ef4_filter_priority priority,
				 u32 *buf, u32 size);
#ifdef CONFIG_RFS_ACCEL
	s32 (*filter_rfs_insert)(struct ef4_nic *efx,
				 struct ef4_filter_spec *spec);
	bool (*filter_rfs_expire_one)(struct ef4_nic *efx, u32 flow_id,
				      unsigned int index);
#endif
#ifdef CONFIG_SFC_FALCON_MTD
	int (*mtd_probe)(struct ef4_nic *efx);
	void (*mtd_rename)(struct ef4_mtd_partition *part);
	int (*mtd_read)(struct mtd_info *mtd, loff_t start, size_t len,
			size_t *retlen, u8 *buffer);
	int (*mtd_erase)(struct mtd_info *mtd, loff_t start, size_t len);
	int (*mtd_write)(struct mtd_info *mtd, loff_t start, size_t len,
			 size_t *retlen, const u8 *buffer);
	int (*mtd_sync)(struct mtd_info *mtd);
#endif
	int (*get_mac_address)(struct ef4_nic *efx, unsigned char *perm_addr);
	int (*set_mac_address)(struct ef4_nic *efx);

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
	unsigned int max_interrupt_mode;
	unsigned int timer_period_max;
	netdev_features_t offload_features;
	unsigned int max_rx_ip_filters;
};

/**************************************************************************
 *
 * Prototypes and inline functions
 *
 *************************************************************************/

static inline struct ef4_channel *
ef4_get_channel(struct ef4_nic *efx, unsigned index)
{
	EF4_BUG_ON_PARANOID(index >= efx->n_channels);
	return efx->channel[index];
}

/* Iterate over all used channels */
#define ef4_for_each_channel(_channel, _efx)				\
	for (_channel = (_efx)->channel[0];				\
	     _channel;							\
	     _channel = (_channel->channel + 1 < (_efx)->n_channels) ?	\
		     (_efx)->channel[_channel->channel + 1] : NULL)

/* Iterate over all used channels in reverse */
#define ef4_for_each_channel_rev(_channel, _efx)			\
	for (_channel = (_efx)->channel[(_efx)->n_channels - 1];	\
	     _channel;							\
	     _channel = _channel->channel ?				\
		     (_efx)->channel[_channel->channel - 1] : NULL)

static inline struct ef4_tx_queue *
ef4_get_tx_queue(struct ef4_nic *efx, unsigned index, unsigned type)
{
	EF4_BUG_ON_PARANOID(index >= efx->n_tx_channels ||
			    type >= EF4_TXQ_TYPES);
	return &efx->channel[efx->tx_channel_offset + index]->tx_queue[type];
}

static inline bool ef4_channel_has_tx_queues(struct ef4_channel *channel)
{
	return channel->channel - channel->efx->tx_channel_offset <
		channel->efx->n_tx_channels;
}

static inline struct ef4_tx_queue *
ef4_channel_get_tx_queue(struct ef4_channel *channel, unsigned type)
{
	EF4_BUG_ON_PARANOID(!ef4_channel_has_tx_queues(channel) ||
			    type >= EF4_TXQ_TYPES);
	return &channel->tx_queue[type];
}

static inline bool ef4_tx_queue_used(struct ef4_tx_queue *tx_queue)
{
	return !(tx_queue->efx->net_dev->num_tc < 2 &&
		 tx_queue->queue & EF4_TXQ_TYPE_HIGHPRI);
}

/* Iterate over all TX queues belonging to a channel */
#define ef4_for_each_channel_tx_queue(_tx_queue, _channel)		\
	if (!ef4_channel_has_tx_queues(_channel))			\
		;							\
	else								\
		for (_tx_queue = (_channel)->tx_queue;			\
		     _tx_queue < (_channel)->tx_queue + EF4_TXQ_TYPES && \
			     ef4_tx_queue_used(_tx_queue);		\
		     _tx_queue++)

/* Iterate over all possible TX queues belonging to a channel */
#define ef4_for_each_possible_channel_tx_queue(_tx_queue, _channel)	\
	if (!ef4_channel_has_tx_queues(_channel))			\
		;							\
	else								\
		for (_tx_queue = (_channel)->tx_queue;			\
		     _tx_queue < (_channel)->tx_queue + EF4_TXQ_TYPES;	\
		     _tx_queue++)

static inline bool ef4_channel_has_rx_queue(struct ef4_channel *channel)
{
	return channel->rx_queue.core_index >= 0;
}

static inline struct ef4_rx_queue *
ef4_channel_get_rx_queue(struct ef4_channel *channel)
{
	EF4_BUG_ON_PARANOID(!ef4_channel_has_rx_queue(channel));
	return &channel->rx_queue;
}

/* Iterate over all RX queues belonging to a channel */
#define ef4_for_each_channel_rx_queue(_rx_queue, _channel)		\
	if (!ef4_channel_has_rx_queue(_channel))			\
		;							\
	else								\
		for (_rx_queue = &(_channel)->rx_queue;			\
		     _rx_queue;						\
		     _rx_queue = NULL)

static inline struct ef4_channel *
ef4_rx_queue_channel(struct ef4_rx_queue *rx_queue)
{
	return container_of(rx_queue, struct ef4_channel, rx_queue);
}

static inline int ef4_rx_queue_index(struct ef4_rx_queue *rx_queue)
{
	return ef4_rx_queue_channel(rx_queue)->channel;
}

/* Returns a pointer to the specified receive buffer in the RX
 * descriptor queue.
 */
static inline struct ef4_rx_buffer *ef4_rx_buffer(struct ef4_rx_queue *rx_queue,
						  unsigned int index)
{
	return &rx_queue->buffer[index];
}

/**
 * EF4_MAX_FRAME_LEN - calculate maximum frame length
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
#define EF4_FRAME_PAD	16
#define EF4_MAX_FRAME_LEN(mtu) \
	(ALIGN(((mtu) + ETH_HLEN + VLAN_HLEN + ETH_FCS_LEN + EF4_FRAME_PAD), 8))

/* Get all supported features.
 * If a feature is not fixed, it is present in hw_features.
 * If a feature is fixed, it does not present in hw_features, but
 * always in features.
 */
static inline netdev_features_t ef4_supported_features(const struct ef4_nic *efx)
{
	const struct net_device *net_dev = efx->net_dev;

	return net_dev->features | net_dev->hw_features;
}

/* Get the current TX queue insert index. */
static inline unsigned int
ef4_tx_queue_get_insert_index(const struct ef4_tx_queue *tx_queue)
{
	return tx_queue->insert_count & tx_queue->ptr_mask;
}

/* Get a TX buffer. */
static inline struct ef4_tx_buffer *
__ef4_tx_queue_get_insert_buffer(const struct ef4_tx_queue *tx_queue)
{
	return &tx_queue->buffer[ef4_tx_queue_get_insert_index(tx_queue)];
}

/* Get a TX buffer, checking it's not currently in use. */
static inline struct ef4_tx_buffer *
ef4_tx_queue_get_insert_buffer(const struct ef4_tx_queue *tx_queue)
{
	struct ef4_tx_buffer *buffer =
		__ef4_tx_queue_get_insert_buffer(tx_queue);

	EF4_BUG_ON_PARANOID(buffer->len);
	EF4_BUG_ON_PARANOID(buffer->flags);
	EF4_BUG_ON_PARANOID(buffer->unmap_len);

	return buffer;
}

#endif /* EF4_NET_DRIVER_H */

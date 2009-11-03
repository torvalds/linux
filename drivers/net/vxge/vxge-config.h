/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-config.h: Driver for Neterion Inc's X3100 Series 10GbE PCIe I/O
 *                Virtualized Server Adapter.
 * Copyright(c) 2002-2009 Neterion Inc.
 ******************************************************************************/
#ifndef VXGE_CONFIG_H
#define VXGE_CONFIG_H
#include <linux/list.h>

#ifndef VXGE_CACHE_LINE_SIZE
#define VXGE_CACHE_LINE_SIZE 128
#endif

#define vxge_os_vaprintf(level, mask, fmt, ...) { \
	char buff[255]; \
		snprintf(buff, 255, fmt, __VA_ARGS__); \
		printk(buff); \
		printk("\n"); \
}

#ifndef VXGE_ALIGN
#define VXGE_ALIGN(adrs, size) \
	(((size) - (((u64)adrs) & ((size)-1))) & ((size)-1))
#endif

#define VXGE_HW_MIN_MTU				68
#define VXGE_HW_MAX_MTU				9600
#define VXGE_HW_DEFAULT_MTU			1500

#ifdef VXGE_DEBUG_ASSERT

/**
 * vxge_assert
 * @test: C-condition to check
 * @fmt: printf like format string
 *
 * This function implements traditional assert. By default assertions
 * are enabled. It can be disabled by undefining VXGE_DEBUG_ASSERT macro in
 * compilation
 * time.
 */
#define vxge_assert(test) { \
	if (!(test)) \
		vxge_os_bug("bad cond: "#test" at %s:%d\n", \
				__FILE__, __LINE__); }
#else
#define vxge_assert(test)
#endif /* end of VXGE_DEBUG_ASSERT */

/**
 * enum enum vxge_debug_level
 * @VXGE_NONE: debug disabled
 * @VXGE_ERR: all errors going to be logged out
 * @VXGE_TRACE: all errors plus all kind of verbose tracing print outs
 *                 going to be logged out. Very noisy.
 *
 * This enumeration going to be used to switch between different
 * debug levels during runtime if DEBUG macro defined during
 * compilation. If DEBUG macro not defined than code will be
 * compiled out.
 */
enum vxge_debug_level {
	VXGE_NONE   = 0,
	VXGE_TRACE  = 1,
	VXGE_ERR    = 2
};

#define NULL_VPID					0xFFFFFFFF
#ifdef CONFIG_VXGE_DEBUG_TRACE_ALL
#define VXGE_DEBUG_MODULE_MASK  0xffffffff
#define VXGE_DEBUG_TRACE_MASK   0xffffffff
#define VXGE_DEBUG_ERR_MASK     0xffffffff
#define VXGE_DEBUG_MASK         0x000001ff
#else
#define VXGE_DEBUG_MODULE_MASK  0x20000000
#define VXGE_DEBUG_TRACE_MASK   0x20000000
#define VXGE_DEBUG_ERR_MASK     0x20000000
#define VXGE_DEBUG_MASK         0x00000001
#endif

/*
 * @VXGE_COMPONENT_LL: do debug for vxge link layer module
 * @VXGE_COMPONENT_ALL: activate debug for all modules with no exceptions
 *
 * This enumeration going to be used to distinguish modules
 * or libraries during compilation and runtime.  Makefile must declare
 * VXGE_DEBUG_MODULE_MASK macro and set it to proper value.
 */
#define	VXGE_COMPONENT_LL				0x20000000
#define	VXGE_COMPONENT_ALL				0xffffffff

#define VXGE_HW_BASE_INF	100
#define VXGE_HW_BASE_ERR	200
#define VXGE_HW_BASE_BADCFG	300

enum vxge_hw_status {
	VXGE_HW_OK				  = 0,
	VXGE_HW_FAIL				  = 1,
	VXGE_HW_PENDING				  = 2,
	VXGE_HW_COMPLETIONS_REMAIN		  = 3,

	VXGE_HW_INF_NO_MORE_COMPLETED_DESCRIPTORS = VXGE_HW_BASE_INF + 1,
	VXGE_HW_INF_OUT_OF_DESCRIPTORS		  = VXGE_HW_BASE_INF + 2,

	VXGE_HW_ERR_INVALID_HANDLE		  = VXGE_HW_BASE_ERR + 1,
	VXGE_HW_ERR_OUT_OF_MEMORY		  = VXGE_HW_BASE_ERR + 2,
	VXGE_HW_ERR_VPATH_NOT_AVAILABLE	  	  = VXGE_HW_BASE_ERR + 3,
	VXGE_HW_ERR_VPATH_NOT_OPEN		  = VXGE_HW_BASE_ERR + 4,
	VXGE_HW_ERR_WRONG_IRQ			  = VXGE_HW_BASE_ERR + 5,
	VXGE_HW_ERR_SWAPPER_CTRL		  = VXGE_HW_BASE_ERR + 6,
	VXGE_HW_ERR_INVALID_MTU_SIZE		  = VXGE_HW_BASE_ERR + 7,
	VXGE_HW_ERR_INVALID_INDEX		  = VXGE_HW_BASE_ERR + 8,
	VXGE_HW_ERR_INVALID_TYPE		  = VXGE_HW_BASE_ERR + 9,
	VXGE_HW_ERR_INVALID_OFFSET		  = VXGE_HW_BASE_ERR + 10,
	VXGE_HW_ERR_INVALID_DEVICE		  = VXGE_HW_BASE_ERR + 11,
	VXGE_HW_ERR_VERSION_CONFLICT		  = VXGE_HW_BASE_ERR + 12,
	VXGE_HW_ERR_INVALID_PCI_INFO		  = VXGE_HW_BASE_ERR + 13,
	VXGE_HW_ERR_INVALID_TCODE 		  = VXGE_HW_BASE_ERR + 14,
	VXGE_HW_ERR_INVALID_BLOCK_SIZE		  = VXGE_HW_BASE_ERR + 15,
	VXGE_HW_ERR_INVALID_STATE		  = VXGE_HW_BASE_ERR + 16,
	VXGE_HW_ERR_PRIVILAGED_OPEARATION	  = VXGE_HW_BASE_ERR + 17,
	VXGE_HW_ERR_INVALID_PORT 		  = VXGE_HW_BASE_ERR + 18,
	VXGE_HW_ERR_FIFO		 	  = VXGE_HW_BASE_ERR + 19,
	VXGE_HW_ERR_VPATH			  = VXGE_HW_BASE_ERR + 20,
	VXGE_HW_ERR_CRITICAL			  = VXGE_HW_BASE_ERR + 21,
	VXGE_HW_ERR_SLOT_FREEZE 		  = VXGE_HW_BASE_ERR + 22,

	VXGE_HW_BADCFG_RING_INDICATE_MAX_PKTS	  = VXGE_HW_BASE_BADCFG + 1,
	VXGE_HW_BADCFG_FIFO_BLOCKS		  = VXGE_HW_BASE_BADCFG + 2,
	VXGE_HW_BADCFG_VPATH_MTU		  = VXGE_HW_BASE_BADCFG + 3,
	VXGE_HW_BADCFG_VPATH_RPA_STRIP_VLAN_TAG	  = VXGE_HW_BASE_BADCFG + 4,
	VXGE_HW_BADCFG_VPATH_MIN_BANDWIDTH	  = VXGE_HW_BASE_BADCFG + 5,
	VXGE_HW_BADCFG_INTR_MODE		  = VXGE_HW_BASE_BADCFG + 6,
	VXGE_HW_BADCFG_RTS_MAC_EN		  = VXGE_HW_BASE_BADCFG + 7,

	VXGE_HW_EOF_TRACE_BUF			  = -1
};

/**
 * enum enum vxge_hw_device_link_state - Link state enumeration.
 * @VXGE_HW_LINK_NONE: Invalid link state.
 * @VXGE_HW_LINK_DOWN: Link is down.
 * @VXGE_HW_LINK_UP: Link is up.
 *
 */
enum vxge_hw_device_link_state {
	VXGE_HW_LINK_NONE,
	VXGE_HW_LINK_DOWN,
	VXGE_HW_LINK_UP
};

/**
 * struct vxge_hw_device_date - Date Format
 * @day: Day
 * @month: Month
 * @year: Year
 * @date: Date in string format
 *
 * Structure for returning date
 */

#define VXGE_HW_FW_STRLEN	32
struct vxge_hw_device_date {
	u32     day;
	u32     month;
	u32     year;
	char    date[VXGE_HW_FW_STRLEN];
};

struct vxge_hw_device_version {
	u32     major;
	u32     minor;
	u32     build;
	char    version[VXGE_HW_FW_STRLEN];
};

u64
__vxge_hw_vpath_pci_func_mode_get(
	u32 vp_id,
	struct vxge_hw_vpath_reg __iomem *vpath_reg);

/**
 * struct vxge_hw_fifo_config - Configuration of fifo.
 * @enable: Is this fifo to be commissioned
 * @fifo_blocks: Numbers of TxDL (that is, lists of Tx descriptors)
 * 		blocks per queue.
 * @max_frags: Max number of Tx buffers per TxDL (that is, per single
 *             transmit operation).
 *             No more than 256 transmit buffers can be specified.
 * @memblock_size: Fifo descriptors are allocated in blocks of @mem_block_size
 *             bytes. Setting @memblock_size to page size ensures
 *             by-page allocation of descriptors. 128K bytes is the
 *             maximum supported block size.
 * @alignment_size: per Tx fragment DMA-able memory used to align transmit data
 *             (e.g., to align on a cache line).
 * @intr: Boolean. Use 1 to generate interrupt for each completed TxDL.
 *             Use 0 otherwise.
 * @no_snoop_bits: If non-zero, specifies no-snoop PCI operation,
 *             which generally improves latency of the host bridge operation
 *             (see PCI specification). For valid values please refer
 *             to struct vxge_hw_fifo_config{} in the driver sources.
 * Configuration of all Titan fifos.
 * Note: Valid (min, max) range for each attribute is specified in the body of
 * the struct vxge_hw_fifo_config{} structure.
 */
struct vxge_hw_fifo_config {
	u32				enable;
#define VXGE_HW_FIFO_ENABLE				1
#define VXGE_HW_FIFO_DISABLE				0

	u32				fifo_blocks;
#define VXGE_HW_MIN_FIFO_BLOCKS				2
#define VXGE_HW_MAX_FIFO_BLOCKS				128

	u32				max_frags;
#define VXGE_HW_MIN_FIFO_FRAGS				1
#define VXGE_HW_MAX_FIFO_FRAGS				256

	u32				memblock_size;
#define VXGE_HW_MIN_FIFO_MEMBLOCK_SIZE			VXGE_HW_BLOCK_SIZE
#define VXGE_HW_MAX_FIFO_MEMBLOCK_SIZE			131072
#define VXGE_HW_DEF_FIFO_MEMBLOCK_SIZE			8096

	u32		                alignment_size;
#define VXGE_HW_MIN_FIFO_ALIGNMENT_SIZE		0
#define VXGE_HW_MAX_FIFO_ALIGNMENT_SIZE		65536
#define VXGE_HW_DEF_FIFO_ALIGNMENT_SIZE		VXGE_CACHE_LINE_SIZE

	u32		                intr;
#define VXGE_HW_FIFO_QUEUE_INTR_ENABLE			1
#define VXGE_HW_FIFO_QUEUE_INTR_DISABLE			0
#define VXGE_HW_FIFO_QUEUE_INTR_DEFAULT			0

	u32				no_snoop_bits;
#define VXGE_HW_FIFO_NO_SNOOP_DISABLED			0
#define VXGE_HW_FIFO_NO_SNOOP_TXD			1
#define VXGE_HW_FIFO_NO_SNOOP_FRM			2
#define VXGE_HW_FIFO_NO_SNOOP_ALL			3
#define VXGE_HW_FIFO_NO_SNOOP_DEFAULT			0

};
/**
 * struct vxge_hw_ring_config - Ring configurations.
 * @enable: Is this ring to be commissioned
 * @ring_blocks: Numbers of RxD blocks in the ring
 * @buffer_mode: Receive buffer mode (1, 2, 3, or 5); for details please refer
 *             to Titan User Guide.
 * @scatter_mode: Titan supports two receive scatter modes: A and B.
 *             For details please refer to Titan User Guide.
 * @rx_timer_val: The number of 32ns periods that would be counted between two
 *             timer interrupts.
 * @greedy_return: If Set it forces the device to return absolutely all RxD
 *             that are consumed and still on board when a timer interrupt
 *             triggers. If Clear, then if the device has already returned
 *             RxD before current timer interrupt trigerred and after the
 *             previous timer interrupt triggered, then the device is not
 *             forced to returned the rest of the consumed RxD that it has
 *             on board which account for a byte count less than the one
 *             programmed into PRC_CFG6.RXD_CRXDT field
 * @rx_timer_ci: TBD
 * @backoff_interval_us: Time (in microseconds), after which Titan
 *             tries to download RxDs posted by the host.
 *             Note that the "backoff" does not happen if host posts receive
 *             descriptors in the timely fashion.
 * Ring configuration.
 */
struct vxge_hw_ring_config {
	u32				enable;
#define VXGE_HW_RING_ENABLE					1
#define VXGE_HW_RING_DISABLE					0
#define VXGE_HW_RING_DEFAULT					1

	u32				ring_blocks;
#define VXGE_HW_MIN_RING_BLOCKS				1
#define VXGE_HW_MAX_RING_BLOCKS				128
#define VXGE_HW_DEF_RING_BLOCKS				2

	u32				buffer_mode;
#define VXGE_HW_RING_RXD_BUFFER_MODE_1				1
#define VXGE_HW_RING_RXD_BUFFER_MODE_3				3
#define VXGE_HW_RING_RXD_BUFFER_MODE_5				5
#define VXGE_HW_RING_RXD_BUFFER_MODE_DEFAULT			1

	u32				scatter_mode;
#define VXGE_HW_RING_SCATTER_MODE_A				0
#define VXGE_HW_RING_SCATTER_MODE_B				1
#define VXGE_HW_RING_SCATTER_MODE_C				2
#define VXGE_HW_RING_SCATTER_MODE_USE_FLASH_DEFAULT		0xffffffff

	u64				rxds_limit;
#define VXGE_HW_DEF_RING_RXDS_LIMIT				44
};

/**
 * struct vxge_hw_vp_config - Configuration of virtual path
 * @vp_id: Virtual Path Id
 * @min_bandwidth: Minimum Guaranteed bandwidth
 * @ring: See struct vxge_hw_ring_config{}.
 * @fifo: See struct vxge_hw_fifo_config{}.
 * @tti: Configuration of interrupt associated with Transmit.
 *             see struct vxge_hw_tim_intr_config();
 * @rti: Configuration of interrupt associated with Receive.
 *              see struct vxge_hw_tim_intr_config();
 * @mtu: mtu size used on this port.
 * @rpa_strip_vlan_tag: Strip VLAN Tag enable/disable. Instructs the device to
 *             remove the VLAN tag from all received tagged frames that are not
 *             replicated at the internal L2 switch.
 *             0 - Do not strip the VLAN tag.
 *             1 - Strip the VLAN tag. Regardless of this setting, VLAN tags are
 *                 always placed into the RxDMA descriptor.
 *
 * This structure is used by the driver to pass the configuration parameters to
 * configure Virtual Path.
 */
struct vxge_hw_vp_config {
	u32				vp_id;

#define	VXGE_HW_VPATH_PRIORITY_MIN			0
#define	VXGE_HW_VPATH_PRIORITY_MAX			16
#define	VXGE_HW_VPATH_PRIORITY_DEFAULT			0

	u32				min_bandwidth;
#define	VXGE_HW_VPATH_BANDWIDTH_MIN			0
#define	VXGE_HW_VPATH_BANDWIDTH_MAX			100
#define	VXGE_HW_VPATH_BANDWIDTH_DEFAULT			0

	struct vxge_hw_ring_config		ring;
	struct vxge_hw_fifo_config		fifo;
	struct vxge_hw_tim_intr_config	tti;
	struct vxge_hw_tim_intr_config	rti;

	u32				mtu;
#define VXGE_HW_VPATH_MIN_INITIAL_MTU			VXGE_HW_MIN_MTU
#define VXGE_HW_VPATH_MAX_INITIAL_MTU			VXGE_HW_MAX_MTU
#define VXGE_HW_VPATH_USE_FLASH_DEFAULT_INITIAL_MTU	0xffffffff

	u32				rpa_strip_vlan_tag;
#define VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_ENABLE			1
#define VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_DISABLE		0
#define VXGE_HW_VPATH_RPA_STRIP_VLAN_TAG_USE_FLASH_DEFAULT	0xffffffff

};
/**
 * struct vxge_hw_device_config - Device configuration.
 * @dma_blockpool_initial: Initial size of DMA Pool
 * @dma_blockpool_max: Maximum blocks in DMA pool
 * @intr_mode: Line, or MSI-X interrupt.
 *
 * @rth_en: Enable Receive Traffic Hashing(RTH) using IT(Indirection Table).
 * @rth_it_type: RTH IT table programming type
 * @rts_mac_en: Enable Receive Traffic Steering using MAC destination address
 * @vp_config: Configuration for virtual paths
 * @device_poll_millis: Specify the interval (in mulliseconds)
 * 			to wait for register reads
 *
 * Titan configuration.
 * Contains per-device configuration parameters, including:
 * - stats sampling interval, etc.
 *
 * In addition, struct vxge_hw_device_config{} includes "subordinate"
 * configurations, including:
 * - fifos and rings;
 * - MAC (done at firmware level).
 *
 * See Titan User Guide for more details.
 * Note: Valid (min, max) range for each attribute is specified in the body of
 * the struct vxge_hw_device_config{} structure. Please refer to the
 * corresponding include file.
 * See also: struct vxge_hw_tim_intr_config{}.
 */
struct vxge_hw_device_config {
	u32				dma_blockpool_initial;
	u32				dma_blockpool_max;
#define VXGE_HW_MIN_DMA_BLOCK_POOL_SIZE			0
#define VXGE_HW_INITIAL_DMA_BLOCK_POOL_SIZE		0
#define VXGE_HW_INCR_DMA_BLOCK_POOL_SIZE		4
#define VXGE_HW_MAX_DMA_BLOCK_POOL_SIZE			4096

#define        VXGE_HW_MAX_PAYLOAD_SIZE_512		2

	u32				intr_mode;
#define VXGE_HW_INTR_MODE_IRQLINE			0
#define VXGE_HW_INTR_MODE_MSIX				1
#define VXGE_HW_INTR_MODE_MSIX_ONE_SHOT			2

#define VXGE_HW_INTR_MODE_DEF				0

	u32				rth_en;
#define VXGE_HW_RTH_DISABLE				0
#define VXGE_HW_RTH_ENABLE				1
#define VXGE_HW_RTH_DEFAULT				0

	u32				rth_it_type;
#define VXGE_HW_RTH_IT_TYPE_SOLO_IT			0
#define VXGE_HW_RTH_IT_TYPE_MULTI_IT			1
#define VXGE_HW_RTH_IT_TYPE_DEFAULT			0

	u32				rts_mac_en;
#define VXGE_HW_RTS_MAC_DISABLE			0
#define VXGE_HW_RTS_MAC_ENABLE			1
#define VXGE_HW_RTS_MAC_DEFAULT			0

	struct vxge_hw_vp_config	vp_config[VXGE_HW_MAX_VIRTUAL_PATHS];

	u32				device_poll_millis;
#define VXGE_HW_MIN_DEVICE_POLL_MILLIS			1
#define VXGE_HW_MAX_DEVICE_POLL_MILLIS			100000
#define VXGE_HW_DEF_DEVICE_POLL_MILLIS			1000

};

/**
 * function vxge_uld_link_up_f - Link-Up callback provided by driver.
 * @devh: HW device handle.
 * Link-up notification callback provided by the driver.
 * This is one of the per-driver callbacks, see struct vxge_hw_uld_cbs{}.
 *
 * See also: struct vxge_hw_uld_cbs{}, vxge_uld_link_down_f{},
 * vxge_hw_driver_initialize().
 */

/**
 * function vxge_uld_link_down_f - Link-Down callback provided by
 * driver.
 * @devh: HW device handle.
 *
 * Link-Down notification callback provided by the driver.
 * This is one of the per-driver callbacks, see struct vxge_hw_uld_cbs{}.
 *
 * See also: struct vxge_hw_uld_cbs{}, vxge_uld_link_up_f{},
 * vxge_hw_driver_initialize().
 */

/**
 * function vxge_uld_crit_err_f - Critical Error notification callback.
 * @devh: HW device handle.
 * (typically - at HW device iinitialization time).
 * @type: Enumerated hw error, e.g.: double ECC.
 * @serr_data: Titan status.
 * @ext_data: Extended data. The contents depends on the @type.
 *
 * Link-Down notification callback provided by the driver.
 * This is one of the per-driver callbacks, see struct vxge_hw_uld_cbs{}.
 *
 * See also: struct vxge_hw_uld_cbs{}, enum vxge_hw_event{},
 * vxge_hw_driver_initialize().
 */

/**
 * struct vxge_hw_uld_cbs - driver "slow-path" callbacks.
 * @link_up: See vxge_uld_link_up_f{}.
 * @link_down: See vxge_uld_link_down_f{}.
 * @crit_err: See vxge_uld_crit_err_f{}.
 *
 * Driver slow-path (per-driver) callbacks.
 * Implemented by driver and provided to HW via
 * vxge_hw_driver_initialize().
 * Note that these callbacks are not mandatory: HW will not invoke
 * a callback if NULL is specified.
 *
 * See also: vxge_hw_driver_initialize().
 */
struct vxge_hw_uld_cbs {

	void (*link_up)(struct __vxge_hw_device *devh);
	void (*link_down)(struct __vxge_hw_device *devh);
	void (*crit_err)(struct __vxge_hw_device *devh,
			enum vxge_hw_event type, u64 ext_data);
};

/*
 * struct __vxge_hw_blockpool_entry - Block private data structure
 * @item: List header used to link.
 * @length: Length of the block
 * @memblock: Virtual address block
 * @dma_addr: DMA Address of the block.
 * @dma_handle: DMA handle of the block.
 * @acc_handle: DMA acc handle
 *
 * Block is allocated with a header to put the blocks into list.
 *
 */
struct __vxge_hw_blockpool_entry {
	struct list_head	item;
	u32			length;
	void			*memblock;
	dma_addr_t		dma_addr;
	struct pci_dev 		*dma_handle;
	struct pci_dev 		*acc_handle;
};

/*
 * struct __vxge_hw_blockpool - Block Pool
 * @hldev: HW device
 * @block_size: size of each block.
 * @Pool_size: Number of blocks in the pool
 * @pool_max: Maximum number of blocks above which to free additional blocks
 * @req_out: Number of block requests with OS out standing
 * @free_block_list: List of free blocks
 *
 * Block pool contains the DMA blocks preallocated.
 *
 */
struct __vxge_hw_blockpool {
	struct __vxge_hw_device *hldev;
	u32				block_size;
	u32				pool_size;
	u32				pool_max;
	u32				req_out;
	struct list_head		free_block_list;
	struct list_head		free_entry_list;
};

/*
 * enum enum __vxge_hw_channel_type - Enumerated channel types.
 * @VXGE_HW_CHANNEL_TYPE_UNKNOWN: Unknown channel.
 * @VXGE_HW_CHANNEL_TYPE_FIFO: fifo.
 * @VXGE_HW_CHANNEL_TYPE_RING: ring.
 * @VXGE_HW_CHANNEL_TYPE_MAX: Maximum number of HW-supported
 * (and recognized) channel types. Currently: 2.
 *
 * Enumerated channel types. Currently there are only two link-layer
 * channels - Titan fifo and Titan ring. In the future the list will grow.
 */
enum __vxge_hw_channel_type {
	VXGE_HW_CHANNEL_TYPE_UNKNOWN			= 0,
	VXGE_HW_CHANNEL_TYPE_FIFO			= 1,
	VXGE_HW_CHANNEL_TYPE_RING			= 2,
	VXGE_HW_CHANNEL_TYPE_MAX			= 3
};

/*
 * struct __vxge_hw_channel
 * @item: List item; used to maintain a list of open channels.
 * @type: Channel type. See enum vxge_hw_channel_type{}.
 * @devh: Device handle. HW device object that contains _this_ channel.
 * @vph: Virtual path handle. Virtual Path Object that contains _this_ channel.
 * @length: Channel length. Currently allocated number of descriptors.
 *          The channel length "grows" when more descriptors get allocated.
 *          See _hw_mempool_grow.
 * @reserve_arr: Reserve array. Contains descriptors that can be reserved
 *               by driver for the subsequent send or receive operation.
 *               See vxge_hw_fifo_txdl_reserve(),
 *               vxge_hw_ring_rxd_reserve().
 * @reserve_ptr: Current pointer in the resrve array
 * @reserve_top: Reserve top gives the maximum number of dtrs available in
 *          reserve array.
 * @work_arr: Work array. Contains descriptors posted to the channel.
 *            Note that at any point in time @work_arr contains 3 types of
 *            descriptors:
 *            1) posted but not yet consumed by Titan device;
 *            2) consumed but not yet completed;
 *            3) completed but not yet freed
 *            (via vxge_hw_fifo_txdl_free() or vxge_hw_ring_rxd_free())
 * @post_index: Post index. At any point in time points on the
 *              position in the channel, which'll contain next to-be-posted
 *              descriptor.
 * @compl_index: Completion index. At any point in time points on the
 *               position in the channel, which will contain next
 *               to-be-completed descriptor.
 * @free_arr: Free array. Contains completed descriptors that were freed
 *            (i.e., handed over back to HW) by driver.
 *            See vxge_hw_fifo_txdl_free(), vxge_hw_ring_rxd_free().
 * @free_ptr: current pointer in free array
 * @per_dtr_space: Per-descriptor space (in bytes) that channel user can utilize
 *                 to store per-operation control information.
 * @stats: Pointer to common statistics
 * @userdata: Per-channel opaque (void*) user-defined context, which may be
 *            driver object, ULP connection, etc.
 *            Once channel is open, @userdata is passed back to user via
 *            vxge_hw_channel_callback_f.
 *
 * HW channel object.
 *
 * See also: enum vxge_hw_channel_type{}, enum vxge_hw_channel_flag
 */
struct __vxge_hw_channel {
	struct list_head		item;
	enum __vxge_hw_channel_type	type;
	struct __vxge_hw_device 	*devh;
	struct __vxge_hw_vpath_handle 	*vph;
	u32			length;
	u32			vp_id;
	void		**reserve_arr;
	u32			reserve_ptr;
	u32			reserve_top;
	void		**work_arr;
	u32			post_index ____cacheline_aligned;
	u32			compl_index ____cacheline_aligned;
	void		**free_arr;
	u32			free_ptr;
	void		**orig_arr;
	u32			per_dtr_space;
	void		*userdata;
	struct vxge_hw_common_reg	__iomem *common_reg;
	u32			first_vp_id;
	struct vxge_hw_vpath_stats_sw_common_info *stats;

} ____cacheline_aligned;

/*
 * struct __vxge_hw_virtualpath - Virtual Path
 *
 * @vp_id: Virtual path id
 * @vp_open: This flag specifies if vxge_hw_vp_open is called from LL Driver
 * @hldev: Hal device
 * @vp_config: Virtual Path Config
 * @vp_reg: VPATH Register map address in BAR0
 * @vpmgmt_reg: VPATH_MGMT register map address
 * @max_mtu: Max mtu that can be supported
 * @vsport_number: vsport attached to this vpath
 * @max_kdfc_db: Maximum kernel mode doorbells
 * @max_nofl_db: Maximum non offload doorbells
 * @tx_intr_num: Interrupt Number associated with the TX

 * @ringh: Ring Queue
 * @fifoh: FIFO Queue
 * @vpath_handles: Virtual Path handles list
 * @stats_block: Memory for DMAing stats
 * @stats: Vpath statistics
 *
 * Virtual path structure to encapsulate the data related to a virtual path.
 * Virtual paths are allocated by the HW upon getting configuration from the
 * driver and inserted into the list of virtual paths.
 */
struct __vxge_hw_virtualpath {
	u32				vp_id;

	u32				vp_open;
#define VXGE_HW_VP_NOT_OPEN	0
#define	VXGE_HW_VP_OPEN		1

	struct __vxge_hw_device		*hldev;
	struct vxge_hw_vp_config	*vp_config;
	struct vxge_hw_vpath_reg	__iomem *vp_reg;
	struct vxge_hw_vpmgmt_reg	__iomem *vpmgmt_reg;
	struct __vxge_hw_non_offload_db_wrapper	__iomem *nofl_db;

	u32				max_mtu;
	u32				vsport_number;
	u32				max_kdfc_db;
	u32				max_nofl_db;

	struct __vxge_hw_ring *____cacheline_aligned ringh;
	struct __vxge_hw_fifo *____cacheline_aligned fifoh;
	struct list_head		vpath_handles;
	struct __vxge_hw_blockpool_entry		*stats_block;
	struct vxge_hw_vpath_stats_hw_info	*hw_stats;
	struct vxge_hw_vpath_stats_hw_info	*hw_stats_sav;
	struct vxge_hw_vpath_stats_sw_info	*sw_stats;
};

/*
 * struct __vxge_hw_vpath_handle - List item to store callback information
 * @item: List head to keep the item in linked list
 * @vpath: Virtual path to which this item belongs
 *
 * This structure is used to store the callback information.
 */
struct __vxge_hw_vpath_handle{
	struct list_head	item;
	struct __vxge_hw_virtualpath	*vpath;
};

/*
 * struct __vxge_hw_device
 *
 * HW device object.
 */
/**
 * struct __vxge_hw_device  - Hal device object
 * @magic: Magic Number
 * @device_id: PCI Device Id of the adapter
 * @major_revision: PCI Device major revision
 * @minor_revision: PCI Device minor revision
 * @bar0: BAR0 virtual address.
 * @pdev: Physical device handle
 * @config: Confguration passed by the LL driver at initialization
 * @link_state: Link state
 *
 * HW device object. Represents Titan adapter
 */
struct __vxge_hw_device {
	u32				magic;
#define VXGE_HW_DEVICE_MAGIC		0x12345678
#define VXGE_HW_DEVICE_DEAD		0xDEADDEAD
	u16				device_id;
	u8				major_revision;
	u8				minor_revision;
	void __iomem			*bar0;
	struct pci_dev			*pdev;
	struct net_device		*ndev;
	struct vxge_hw_device_config	config;
	enum vxge_hw_device_link_state	link_state;

	struct vxge_hw_uld_cbs		uld_callbacks;

	u32				host_type;
	u32				func_id;
	u32				access_rights;
#define VXGE_HW_DEVICE_ACCESS_RIGHT_VPATH      0x1
#define VXGE_HW_DEVICE_ACCESS_RIGHT_SRPCIM     0x2
#define VXGE_HW_DEVICE_ACCESS_RIGHT_MRPCIM     0x4
	struct vxge_hw_legacy_reg	__iomem *legacy_reg;
	struct vxge_hw_toc_reg		__iomem *toc_reg;
	struct vxge_hw_common_reg	__iomem *common_reg;
	struct vxge_hw_mrpcim_reg	__iomem *mrpcim_reg;
	struct vxge_hw_srpcim_reg	__iomem *srpcim_reg \
					[VXGE_HW_TITAN_SRPCIM_REG_SPACES];
	struct vxge_hw_vpmgmt_reg	__iomem *vpmgmt_reg \
					[VXGE_HW_TITAN_VPMGMT_REG_SPACES];
	struct vxge_hw_vpath_reg	__iomem *vpath_reg \
					[VXGE_HW_TITAN_VPATH_REG_SPACES];
	u8				__iomem *kdfc;
	u8				__iomem *usdc;
	struct __vxge_hw_virtualpath	virtual_paths \
					[VXGE_HW_MAX_VIRTUAL_PATHS];
	u64				vpath_assignments;
	u64				vpaths_deployed;
	u32				first_vp_id;
	u64				tim_int_mask0[4];
	u32				tim_int_mask1[4];

	struct __vxge_hw_blockpool	block_pool;
	struct vxge_hw_device_stats	stats;
	u32				debug_module_mask;
	u32				debug_level;
	u32				level_err;
	u32				level_trace;
};

#define VXGE_HW_INFO_LEN	64
/**
 * struct vxge_hw_device_hw_info - Device information
 * @host_type: Host Type
 * @func_id: Function Id
 * @vpath_mask: vpath bit mask
 * @fw_version: Firmware version
 * @fw_date: Firmware Date
 * @flash_version: Firmware version
 * @flash_date: Firmware Date
 * @mac_addrs: Mac addresses for each vpath
 * @mac_addr_masks: Mac address masks for each vpath
 *
 * Returns the vpath mask that has the bits set for each vpath allocated
 * for the driver and the first mac address for each vpath
 */
struct vxge_hw_device_hw_info {
	u32		host_type;
#define VXGE_HW_NO_MR_NO_SR_NORMAL_FUNCTION			0
#define VXGE_HW_MR_NO_SR_VH0_BASE_FUNCTION			1
#define VXGE_HW_NO_MR_SR_VH0_FUNCTION0				2
#define VXGE_HW_NO_MR_SR_VH0_VIRTUAL_FUNCTION			3
#define VXGE_HW_MR_SR_VH0_INVALID_CONFIG			4
#define VXGE_HW_SR_VH_FUNCTION0					5
#define VXGE_HW_SR_VH_VIRTUAL_FUNCTION				6
#define VXGE_HW_VH_NORMAL_FUNCTION				7
	u64		function_mode;
#define VXGE_HW_FUNCTION_MODE_MULTI_FUNCTION			0
#define VXGE_HW_FUNCTION_MODE_SINGLE_FUNCTION			1
#define VXGE_HW_FUNCTION_MODE_SRIOV				2
#define VXGE_HW_FUNCTION_MODE_MRIOV				3
	u32		func_id;
	u64		vpath_mask;
	struct vxge_hw_device_version fw_version;
	struct vxge_hw_device_date    fw_date;
	struct vxge_hw_device_version flash_version;
	struct vxge_hw_device_date    flash_date;
	u8		serial_number[VXGE_HW_INFO_LEN];
	u8		part_number[VXGE_HW_INFO_LEN];
	u8		product_desc[VXGE_HW_INFO_LEN];
	u8 (mac_addrs)[VXGE_HW_MAX_VIRTUAL_PATHS][ETH_ALEN];
	u8 (mac_addr_masks)[VXGE_HW_MAX_VIRTUAL_PATHS][ETH_ALEN];
};

/**
 * struct vxge_hw_device_attr - Device memory spaces.
 * @bar0: BAR0 virtual address.
 * @pdev: PCI device object.
 *
 * Device memory spaces. Includes configuration, BAR0 etc. per device
 * mapped memories. Also, includes a pointer to OS-specific PCI device object.
 */
struct vxge_hw_device_attr {
	void __iomem		*bar0;
	struct pci_dev 		*pdev;
	struct vxge_hw_uld_cbs	uld_callbacks;
};

#define VXGE_HW_DEVICE_LINK_STATE_SET(hldev, ls)	(hldev->link_state = ls)

#define VXGE_HW_DEVICE_TIM_INT_MASK_SET(m0, m1, i) {	\
	if (i < 16) {				\
		m0[0] |= vxge_vBIT(0x8, (i*4), 4);	\
		m0[1] |= vxge_vBIT(0x4, (i*4), 4);	\
	}			       		\
	else {					\
		m1[0] = 0x80000000;		\
		m1[1] = 0x40000000;		\
	}					\
}

#define VXGE_HW_DEVICE_TIM_INT_MASK_RESET(m0, m1, i) {	\
	if (i < 16) {					\
		m0[0] &= ~vxge_vBIT(0x8, (i*4), 4);		\
		m0[1] &= ~vxge_vBIT(0x4, (i*4), 4);		\
	}						\
	else {						\
		m1[0] = 0;				\
		m1[1] = 0;				\
	}						\
}

#define VXGE_HW_DEVICE_STATS_PIO_READ(loc, offset) {		\
	status = vxge_hw_mrpcim_stats_access(hldev, \
				VXGE_HW_STATS_OP_READ, \
				loc, \
				offset, \
				&val64);			\
								\
	if (status != VXGE_HW_OK)				\
		return status;						\
}

#define VXGE_HW_VPATH_STATS_PIO_READ(offset) {				\
	status = __vxge_hw_vpath_stats_access(vpath, \
			VXGE_HW_STATS_OP_READ, \
			offset, \
			&val64);					\
	if (status != VXGE_HW_OK)					\
		return status;						\
}

/*
 * struct __vxge_hw_ring - Ring channel.
 * @channel: Channel "base" of this ring, the common part of all HW
 *           channels.
 * @mempool: Memory pool, the pool from which descriptors get allocated.
 *           (See vxge_hw_mm.h).
 * @config: Ring configuration, part of device configuration
 *          (see struct vxge_hw_device_config{}).
 * @ring_length: Length of the ring
 * @buffer_mode: 1, 3, or 5. The value specifies a receive buffer mode,
 *          as per Titan User Guide.
 * @rxd_size: RxD sizes for 1-, 3- or 5- buffer modes. As per Titan spec,
 *            1-buffer mode descriptor is 32 byte long, etc.
 * @rxd_priv_size: Per RxD size reserved (by HW) for driver to keep
 *                 per-descriptor data (e.g., DMA handle for Solaris)
 * @per_rxd_space: Per rxd space requested by driver
 * @rxds_per_block: Number of descriptors per hardware-defined RxD
 *                  block. Depends on the (1-, 3-, 5-) buffer mode.
 * @rxdblock_priv_size: Reserved at the end of each RxD block. HW internal
 *                      usage. Not to confuse with @rxd_priv_size.
 * @cmpl_cnt: Completion counter. Is reset to zero upon entering the ISR.
 * @callback: Channel completion callback. HW invokes the callback when there
 *            are new completions on that channel. In many implementations
 *            the @callback executes in the hw interrupt context.
 * @rxd_init: Channel's descriptor-initialize callback.
 *            See vxge_hw_ring_rxd_init_f{}.
 *            If not NULL, HW invokes the callback when opening
 *            the ring.
 * @rxd_term: Channel's descriptor-terminate callback. If not NULL,
 *          HW invokes the callback when closing the corresponding channel.
 *          See also vxge_hw_channel_rxd_term_f{}.
 * @stats: Statistics for ring
 * Ring channel.
 *
 * Note: The structure is cache line aligned to better utilize
 *       CPU cache performance.
 */
struct __vxge_hw_ring {
	struct __vxge_hw_channel		channel;
	struct vxge_hw_mempool			*mempool;
	struct vxge_hw_vpath_reg		__iomem	*vp_reg;
	struct vxge_hw_common_reg		__iomem	*common_reg;
	u32					ring_length;
	u32					buffer_mode;
	u32					rxd_size;
	u32					rxd_priv_size;
	u32					per_rxd_space;
	u32					rxds_per_block;
	u32					rxdblock_priv_size;
	u32					cmpl_cnt;
	u32					vp_id;
	u32					doorbell_cnt;
	u32					total_db_cnt;
	u64					rxds_limit;

	enum vxge_hw_status (*callback)(
			struct __vxge_hw_ring *ringh,
			void *rxdh,
			u8 t_code,
			void *userdata);

	enum vxge_hw_status (*rxd_init)(
			void *rxdh,
			void *userdata);

	void (*rxd_term)(
			void *rxdh,
			enum vxge_hw_rxd_state state,
			void *userdata);

	struct vxge_hw_vpath_stats_sw_ring_info *stats	____cacheline_aligned;
	struct vxge_hw_ring_config		*config;
} ____cacheline_aligned;

/**
 * enum enum vxge_hw_txdl_state - Descriptor (TXDL) state.
 * @VXGE_HW_TXDL_STATE_NONE: Invalid state.
 * @VXGE_HW_TXDL_STATE_AVAIL: Descriptor is available for reservation.
 * @VXGE_HW_TXDL_STATE_POSTED: Descriptor is posted for processing by the
 * device.
 * @VXGE_HW_TXDL_STATE_FREED: Descriptor is free and can be reused for
 * filling-in and posting later.
 *
 * Titan/HW descriptor states.
 *
 */
enum vxge_hw_txdl_state {
	VXGE_HW_TXDL_STATE_NONE	= 0,
	VXGE_HW_TXDL_STATE_AVAIL	= 1,
	VXGE_HW_TXDL_STATE_POSTED	= 2,
	VXGE_HW_TXDL_STATE_FREED	= 3
};
/*
 * struct __vxge_hw_fifo - Fifo.
 * @channel: Channel "base" of this fifo, the common part of all HW
 *             channels.
 * @mempool: Memory pool, from which descriptors get allocated.
 * @config: Fifo configuration, part of device configuration
 *             (see struct vxge_hw_device_config{}).
 * @interrupt_type: Interrupt type to be used
 * @no_snoop_bits: See struct vxge_hw_fifo_config{}.
 * @txdl_per_memblock: Number of TxDLs (TxD lists) per memblock.
 *             on TxDL please refer to Titan UG.
 * @txdl_size: Configured TxDL size (i.e., number of TxDs in a list), plus
 *             per-TxDL HW private space (struct __vxge_hw_fifo_txdl_priv).
 * @priv_size: Per-Tx descriptor space reserved for driver
 *             usage.
 * @per_txdl_space: Per txdl private space for the driver
 * @callback: Fifo completion callback. HW invokes the callback when there
 *             are new completions on that fifo. In many implementations
 *             the @callback executes in the hw interrupt context.
 * @txdl_term: Fifo's descriptor-terminate callback. If not NULL,
 *             HW invokes the callback when closing the corresponding fifo.
 *             See also vxge_hw_fifo_txdl_term_f{}.
 * @stats: Statistics of this fifo
 *
 * Fifo channel.
 * Note: The structure is cache line aligned.
 */
struct __vxge_hw_fifo {
	struct __vxge_hw_channel		channel;
	struct vxge_hw_mempool			*mempool;
	struct vxge_hw_fifo_config		*config;
	struct vxge_hw_vpath_reg		__iomem *vp_reg;
	struct __vxge_hw_non_offload_db_wrapper	__iomem *nofl_db;
	u64					interrupt_type;
	u32					no_snoop_bits;
	u32					txdl_per_memblock;
	u32					txdl_size;
	u32					priv_size;
	u32					per_txdl_space;
	u32					vp_id;
	u32					tx_intr_num;

	enum vxge_hw_status (*callback)(
			struct __vxge_hw_fifo *fifo_handle,
			void *txdlh,
			enum vxge_hw_fifo_tcode t_code,
			void *userdata,
			struct sk_buff ***skb_ptr,
			int nr_skb,
			int *more);

	void (*txdl_term)(
			void *txdlh,
			enum vxge_hw_txdl_state state,
			void *userdata);

	struct vxge_hw_vpath_stats_sw_fifo_info *stats ____cacheline_aligned;
} ____cacheline_aligned;

/*
 * struct __vxge_hw_fifo_txdl_priv - Transmit descriptor HW-private data.
 * @dma_addr: DMA (mapped) address of _this_ descriptor.
 * @dma_handle: DMA handle used to map the descriptor onto device.
 * @dma_offset: Descriptor's offset in the memory block. HW allocates
 *	 descriptors in memory blocks (see struct vxge_hw_fifo_config{})
 *             Each memblock is a contiguous block of DMA-able memory.
 * @frags: Total number of fragments (that is, contiguous data buffers)
 * carried by this TxDL.
 * @align_vaddr_start: Aligned virtual address start
 * @align_vaddr: Virtual address of the per-TxDL area in memory used for
 *             alignement. Used to place one or more mis-aligned fragments
 * @align_dma_addr: DMA address translated from the @align_vaddr.
 * @align_dma_handle: DMA handle that corresponds to @align_dma_addr.
 * @align_dma_acch: DMA access handle corresponds to @align_dma_addr.
 * @align_dma_offset: The current offset into the @align_vaddr area.
 * Grows while filling the descriptor, gets reset.
 * @align_used_frags: Number of fragments used.
 * @alloc_frags: Total number of fragments allocated.
 * @unused: TODO
 * @next_txdl_priv: (TODO).
 * @first_txdp: (TODO).
 * @linked_txdl_priv: Pointer to any linked TxDL for creating contiguous
 *             TxDL list.
 * @txdlh: Corresponding txdlh to this TxDL.
 * @memblock: Pointer to the TxDL memory block or memory page.
 *             on the next send operation.
 * @dma_object: DMA address and handle of the memory block that contains
 *             the descriptor. This member is used only in the "checked"
 *             version of the HW (to enforce certain assertions);
 *             otherwise it gets compiled out.
 * @allocated: True if the descriptor is reserved, 0 otherwise. Internal usage.
 *
 * Per-transmit decsriptor HW-private data. HW uses the space to keep DMA
 * information associated with the descriptor. Note that driver can ask HW
 * to allocate additional per-descriptor space for its own (driver-specific)
 * purposes.
 *
 * See also: struct vxge_hw_ring_rxd_priv{}.
 */
struct __vxge_hw_fifo_txdl_priv {
	dma_addr_t		dma_addr;
	struct pci_dev	*dma_handle;
	ptrdiff_t		dma_offset;
	u32				frags;
	u8				*align_vaddr_start;
	u8				*align_vaddr;
	dma_addr_t		align_dma_addr;
	struct pci_dev 	*align_dma_handle;
	struct pci_dev	*align_dma_acch;
	ptrdiff_t		align_dma_offset;
	u32				align_used_frags;
	u32				alloc_frags;
	u32				unused;
	struct __vxge_hw_fifo_txdl_priv	*next_txdl_priv;
	struct vxge_hw_fifo_txd		*first_txdp;
	void			*memblock;
};

/*
 * struct __vxge_hw_non_offload_db_wrapper - Non-offload Doorbell Wrapper
 * @control_0: Bits 0 to 7 - Doorbell type.
 *             Bits 8 to 31 - Reserved.
 *             Bits 32 to 39 - The highest TxD in this TxDL.
 *             Bits 40 to 47 - Reserved.
	*	       Bits 48 to 55 - Reserved.
 *             Bits 56 to 63 - No snoop flags.
 * @txdl_ptr:  The starting location of the TxDL in host memory.
 *
 * Created by the host and written to the adapter via PIO to a Kernel Doorbell
 * FIFO. All non-offload doorbell wrapper fields must be written by the host as
 * part of a doorbell write. Consumed by the adapter but is not written by the
 * adapter.
 */
struct __vxge_hw_non_offload_db_wrapper {
	u64		control_0;
#define	VXGE_HW_NODBW_GET_TYPE(ctrl0)			vxge_bVALn(ctrl0, 0, 8)
#define VXGE_HW_NODBW_TYPE(val) vxge_vBIT(val, 0, 8)
#define	VXGE_HW_NODBW_TYPE_NODBW				0

#define	VXGE_HW_NODBW_GET_LAST_TXD_NUMBER(ctrl0)	vxge_bVALn(ctrl0, 32, 8)
#define VXGE_HW_NODBW_LAST_TXD_NUMBER(val) vxge_vBIT(val, 32, 8)

#define	VXGE_HW_NODBW_GET_NO_SNOOP(ctrl0)		vxge_bVALn(ctrl0, 56, 8)
#define VXGE_HW_NODBW_LIST_NO_SNOOP(val) vxge_vBIT(val, 56, 8)
#define	VXGE_HW_NODBW_LIST_NO_SNOOP_TXD_READ_TXD0_WRITE		0x2
#define	VXGE_HW_NODBW_LIST_NO_SNOOP_TX_FRAME_DATA_READ		0x1

	u64		txdl_ptr;
};

/*
 * TX Descriptor
 */

/**
 * struct vxge_hw_fifo_txd - Transmit Descriptor
 * @control_0: Bits 0 to 6 - Reserved.
 *             Bit 7 - List Ownership. This field should be initialized
 *             to '1' by the driver before the transmit list pointer is
 *             written to the adapter. This field will be set to '0' by the
 *             adapter once it has completed transmitting the frame or frames in
 *             the list. Note - This field is only valid in TxD0. Additionally,
 *             for multi-list sequences, the driver should not release any
 *             buffers until the ownership of the last list in the multi-list
 *             sequence has been returned to the host.
 *             Bits 8 to 11 - Reserved
 *             Bits 12 to 15 - Transfer_Code. This field is only valid in
 *             TxD0. It is used to describe the status of the transmit data
 *             buffer transfer. This field is always overwritten by the
 *             adapter, so this field may be initialized to any value.
 *             Bits 16 to 17 - Host steering. This field allows the host to
 *             override the selection of the physical transmit port.
 *             Attention:
 *             Normal sounds as if learned from the switch rather than from
 *             the aggregation algorythms.
 *             00: Normal. Use Destination/MAC Address
 *             lookup to determine the transmit port.
 *             01: Send on physical Port1.
 *             10: Send on physical Port0.
	*	       11: Send on both ports.
 *             Bits 18 to 21 - Reserved
 *             Bits 22 to 23 - Gather_Code. This field is set by the host and
 *             is used to describe how individual buffers comprise a frame.
 *             10: First descriptor of a frame.
 *             00: Middle of a multi-descriptor frame.
 *             01: Last descriptor of a frame.
 *             11: First and last descriptor of a frame (the entire frame
 *             resides in a single buffer).
 *             For multi-descriptor frames, the only valid gather code sequence
 *             is {10, [00], 01}. In other words, the descriptors must be placed
 *             in the list in the correct order.
 *             Bits 24 to 27 - Reserved
 *             Bits 28 to 29 - LSO_Frm_Encap. LSO Frame Encapsulation
 *             definition. Only valid in TxD0. This field allows the host to
 *             indicate the Ethernet encapsulation of an outbound LSO packet.
 *             00 - classic mode (best guess)
 *             01 - LLC
 *             10 - SNAP
 *             11 - DIX
 *             If "classic mode" is selected, the adapter will attempt to
 *             decode the frame's Ethernet encapsulation by examining the L/T
 *             field as follows:
 *             <= 0x05DC LLC/SNAP encoding; must examine DSAP/SSAP to determine
 *             if packet is IPv4 or IPv6.
 *             0x8870 Jumbo-SNAP encoding.
 *             0x0800 IPv4 DIX encoding
 *             0x86DD IPv6 DIX encoding
 *             others illegal encapsulation
 *             Bits 30 - LSO_ Flag. Large Send Offload (LSO) flag.
 *             Set to 1 to perform segmentation offload for TCP/UDP.
 *             This field is valid only in TxD0.
 *             Bits 31 to 33 - Reserved.
 *             Bits 34 to 47 - LSO_MSS. TCP/UDP LSO Maximum Segment Size
 *             This field is meaningful only when LSO_Control is non-zero.
 *             When LSO_Control is set to TCP_LSO, the single (possibly large)
 *             TCP segment described by this TxDL will be sent as a series of
 *             TCP segments each of which contains no more than LSO_MSS
 *             payload bytes.
 *             When LSO_Control is set to UDP_LSO, the single (possibly large)
 *             UDP datagram described by this TxDL will be sent as a series of
 *             UDP datagrams each of which contains no more than LSO_MSS
 *             payload bytes.
 *             All outgoing frames from this TxDL will have LSO_MSS bytes of UDP
 *             or TCP payload, with the exception of the last, which will have
 *             <= LSO_MSS bytes of payload.
 *             Bits 48 to 63 - Buffer_Size. Number of valid bytes in the
 *             buffer to be read by the adapter. This field is written by the
 *             host. A value of 0 is illegal.
 *	       Bits 32 to 63 - This value is written by the adapter upon
 *	       completion of a UDP or TCP LSO operation and indicates the number
 *             of UDP or TCP payload bytes that were transmitted. 0x0000 will be
 *             returned for any non-LSO operation.
 * @control_1: Bits 0 to 4 - Reserved.
 *             Bit 5 - Tx_CKO_IPv4 Set to a '1' to enable IPv4 header checksum
 *             offload. This field is only valid in the first TxD of a frame.
 *             Bit 6 - Tx_CKO_TCP Set to a '1' to enable TCP checksum offload.
 *             This field is only valid in the first TxD of a frame (the TxD's
 *             gather code must be 10 or 11). The driver should only set this
 *             bit if it can guarantee that TCP is present.
 *             Bit 7 - Tx_CKO_UDP Set to a '1' to enable UDP checksum offload.
 *             This field is only valid in the first TxD of a frame (the TxD's
 *             gather code must be 10 or 11). The driver should only set this
 *             bit if it can guarantee that UDP is present.
 *             Bits 8 to 14 - Reserved.
 *             Bit 15 - Tx_VLAN_Enable VLAN tag insertion flag. Set to a '1' to
 *             instruct the adapter to insert the VLAN tag specified by the
 *             Tx_VLAN_Tag field. This field is only valid in the first TxD of
 *             a frame.
 *             Bits 16 to 31 - Tx_VLAN_Tag. Variable portion of the VLAN tag
 *             to be inserted into the frame by the adapter (the first two bytes
 *             of a VLAN tag are always 0x8100). This field is only valid if the
 *             Tx_VLAN_Enable field is set to '1'.
 *             Bits 32 to 33 - Reserved.
 *             Bits 34 to 39 - Tx_Int_Number. Indicates which Tx interrupt
 *             number the frame associated with. This field is written by the
 *             host. It is only valid in the first TxD of a frame.
 *             Bits 40 to 42 - Reserved.
 *             Bit 43 - Set to 1 to exclude the frame from bandwidth metering
 *             functions. This field is valid only in the first TxD
 *             of a frame.
 *             Bits 44 to 45 - Reserved.
 *             Bit 46 - Tx_Int_Per_List Set to a '1' to instruct the adapter to
 *             generate an interrupt as soon as all of the frames in the list
 *             have been transmitted. In order to have per-frame interrupts,
 *             the driver should place a maximum of one frame per list. This
 *             field is only valid in the first TxD of a frame.
 *             Bit 47 - Tx_Int_Utilization Set to a '1' to instruct the adapter
 *             to count the frame toward the utilization interrupt specified in
 *             the Tx_Int_Number field. This field is only valid in the first
 *             TxD of a frame.
 *             Bits 48 to 63 - Reserved.
 * @buffer_pointer: Buffer start address.
 * @host_control: Host_Control.Opaque 64bit data stored by driver inside the
 *            Titan descriptor prior to posting the latter on the fifo
 *            via vxge_hw_fifo_txdl_post().The %host_control is returned as is
 *            to the driver with each completed descriptor.
 *
 * Transmit descriptor (TxD).Fifo descriptor contains configured number
 * (list) of TxDs. * For more details please refer to Titan User Guide,
 * Section 5.4.2 "Transmit Descriptor (TxD) Format".
 */
struct vxge_hw_fifo_txd {
	u64 control_0;
#define VXGE_HW_FIFO_TXD_LIST_OWN_ADAPTER		vxge_mBIT(7)

#define VXGE_HW_FIFO_TXD_T_CODE_GET(ctrl0)		vxge_bVALn(ctrl0, 12, 4)
#define VXGE_HW_FIFO_TXD_T_CODE(val) 			vxge_vBIT(val, 12, 4)
#define VXGE_HW_FIFO_TXD_T_CODE_UNUSED		VXGE_HW_FIFO_T_CODE_UNUSED


#define VXGE_HW_FIFO_TXD_GATHER_CODE(val) 		vxge_vBIT(val, 22, 2)
#define VXGE_HW_FIFO_TXD_GATHER_CODE_FIRST	VXGE_HW_FIFO_GATHER_CODE_FIRST
#define VXGE_HW_FIFO_TXD_GATHER_CODE_LAST	VXGE_HW_FIFO_GATHER_CODE_LAST


#define VXGE_HW_FIFO_TXD_LSO_EN				vxge_mBIT(30)

#define VXGE_HW_FIFO_TXD_LSO_MSS(val) 			vxge_vBIT(val, 34, 14)

#define VXGE_HW_FIFO_TXD_BUFFER_SIZE(val) 		vxge_vBIT(val, 48, 16)

	u64 control_1;
#define VXGE_HW_FIFO_TXD_TX_CKO_IPV4_EN			vxge_mBIT(5)
#define VXGE_HW_FIFO_TXD_TX_CKO_TCP_EN			vxge_mBIT(6)
#define VXGE_HW_FIFO_TXD_TX_CKO_UDP_EN			vxge_mBIT(7)
#define VXGE_HW_FIFO_TXD_VLAN_ENABLE			vxge_mBIT(15)

#define VXGE_HW_FIFO_TXD_VLAN_TAG(val) 			vxge_vBIT(val, 16, 16)

#define VXGE_HW_FIFO_TXD_INT_NUMBER(val) 		vxge_vBIT(val, 34, 6)

#define VXGE_HW_FIFO_TXD_INT_TYPE_PER_LIST		vxge_mBIT(46)
#define VXGE_HW_FIFO_TXD_INT_TYPE_UTILZ			vxge_mBIT(47)

	u64 buffer_pointer;

	u64 host_control;
};

/**
 * struct vxge_hw_ring_rxd_1 - One buffer mode RxD for ring
 * @host_control: This field is exclusively for host use and is "readonly"
 *             from the adapter's perspective.
 * @control_0:Bits 0 to 6 - RTH_Bucket get
 *	      Bit 7 - Own Descriptor ownership bit. This bit is set to 1
 *            by the host, and is set to 0 by the adapter.
 *	      0 - Host owns RxD and buffer.
 *	      1 - The adapter owns RxD and buffer.
 *	      Bit 8 - Fast_Path_Eligible When set, indicates that the
 *            received frame meets all of the criteria for fast path processing.
 *            The required criteria are as follows:
 *            !SYN &
 *            (Transfer_Code == "Transfer OK") &
 *            (!Is_IP_Fragment) &
 *            ((Is_IPv4 & computed_L3_checksum == 0xFFFF) |
 *            (Is_IPv6)) &
 *            ((Is_TCP & computed_L4_checksum == 0xFFFF) |
 *            (Is_UDP & (computed_L4_checksum == 0xFFFF |
 *            computed _L4_checksum == 0x0000)))
 *            (same meaning for all RxD buffer modes)
 *	      Bit 9 - L3 Checksum Correct
 *	      Bit 10 - L4 Checksum Correct
 *	      Bit 11 - Reserved
 *	      Bit 12 to 15 - This field is written by the adapter. It is
 *            used to report the status of the frame transfer to the host.
 *	      0x0 - Transfer OK
 *	      0x4 - RDA Failure During Transfer
 *	      0x5 - Unparseable Packet, such as unknown IPv6 header.
 *	      0x6 - Frame integrity error (FCS or ECC).
 *	      0x7 - Buffer Size Error. The provided buffer(s) were not
 *                  appropriately sized and data loss occurred.
 *	      0x8 - Internal ECC Error. RxD corrupted.
 *	      0x9 - IPv4 Checksum error
 *	      0xA - TCP/UDP Checksum error
 *	      0xF - Unknown Error or Multiple Error. Indicates an
 *               unknown problem or that more than one of transfer codes is set.
 *	      Bit 16 - SYN The adapter sets this field to indicate that
 *                the incoming frame contained a TCP segment with its SYN bit
 *	          set and its ACK bit NOT set. (same meaning for all RxD buffer
 *                modes)
 *	      Bit 17 - Is ICMP
 *	      Bit 18 - RTH_SPDM_HIT Set to 1 if there was a match in the
 *                Socket Pair Direct Match Table and the frame was steered based
 *                on SPDM.
 *	      Bit 19 - RTH_IT_HIT Set to 1 if there was a match in the
 *            Indirection Table and the frame was steered based on hash
 *            indirection.
 *	      Bit 20 to 23 - RTH_HASH_TYPE Indicates the function (hash
 *	          type) that was used to calculate the hash.
 *	      Bit 19 - IS_VLAN Set to '1' if the frame was/is VLAN
 *	          tagged.
 *	      Bit 25 to 26 - ETHER_ENCAP Reflects the Ethernet encapsulation
 *                of the received frame.
 *	      0x0 - Ethernet DIX
 *	      0x1 - LLC
 *	      0x2 - SNAP (includes Jumbo-SNAP)
 *	      0x3 - IPX
 *	      Bit 27 - IS_IPV4 Set to '1' if the frame contains an IPv4	packet.
 *	      Bit 28 - IS_IPV6 Set to '1' if the frame contains an IPv6 packet.
 *	      Bit 29 - IS_IP_FRAG Set to '1' if the frame contains a fragmented
 *            IP packet.
 *	      Bit 30 - IS_TCP Set to '1' if the frame contains a TCP segment.
 *	      Bit 31 - IS_UDP Set to '1' if the frame contains a UDP message.
 *	      Bit 32 to 47 - L3_Checksum[0:15] The IPv4 checksum value 	that
 *            arrived with the frame. If the resulting computed IPv4 header
 *            checksum for the frame did not produce the expected 0xFFFF value,
 *            then the transfer code would be set to 0x9.
 *	      Bit 48 to 63 - L4_Checksum[0:15] The TCP/UDP checksum value that
 *            arrived with the frame. If the resulting computed TCP/UDP checksum
 *            for the frame did not produce the expected 0xFFFF value, then the
 *            transfer code would be set to 0xA.
 * @control_1:Bits 0 to 1 - Reserved
 *            Bits 2 to 15 - Buffer0_Size.This field is set by the host and
 *            eventually overwritten by the adapter. The host writes the
 *            available buffer size in bytes when it passes the descriptor to
 *            the adapter. When a frame is delivered the host, the adapter
 *            populates this field with the number of bytes written into the
 *            buffer. The largest supported buffer is 16, 383 bytes.
 *	      Bit 16 to 47 - RTH Hash Value 32-bit RTH hash value. Only valid if
 *	      RTH_HASH_TYPE (Control_0, bits 20:23) is nonzero.
 *	      Bit 48 to 63 - VLAN_Tag[0:15] The contents of the variable portion
 *            of the VLAN tag, if one was detected by the adapter. This field is
 *            populated even if VLAN-tag stripping is enabled.
 * @buffer0_ptr: Pointer to buffer. This field is populated by the driver.
 *
 * One buffer mode RxD for ring structure
 */
struct vxge_hw_ring_rxd_1 {
	u64 host_control;
	u64 control_0;
#define VXGE_HW_RING_RXD_RTH_BUCKET_GET(ctrl0)		vxge_bVALn(ctrl0, 0, 7)

#define VXGE_HW_RING_RXD_LIST_OWN_ADAPTER		vxge_mBIT(7)

#define VXGE_HW_RING_RXD_FAST_PATH_ELIGIBLE_GET(ctrl0)	vxge_bVALn(ctrl0, 8, 1)

#define VXGE_HW_RING_RXD_L3_CKSUM_CORRECT_GET(ctrl0)	vxge_bVALn(ctrl0, 9, 1)

#define VXGE_HW_RING_RXD_L4_CKSUM_CORRECT_GET(ctrl0)	vxge_bVALn(ctrl0, 10, 1)

#define VXGE_HW_RING_RXD_T_CODE_GET(ctrl0)		vxge_bVALn(ctrl0, 12, 4)
#define VXGE_HW_RING_RXD_T_CODE(val) 			vxge_vBIT(val, 12, 4)

#define VXGE_HW_RING_RXD_T_CODE_UNUSED		VXGE_HW_RING_T_CODE_UNUSED

#define VXGE_HW_RING_RXD_SYN_GET(ctrl0)		vxge_bVALn(ctrl0, 16, 1)

#define VXGE_HW_RING_RXD_IS_ICMP_GET(ctrl0)		vxge_bVALn(ctrl0, 17, 1)

#define VXGE_HW_RING_RXD_RTH_SPDM_HIT_GET(ctrl0)	vxge_bVALn(ctrl0, 18, 1)

#define VXGE_HW_RING_RXD_RTH_IT_HIT_GET(ctrl0)		vxge_bVALn(ctrl0, 19, 1)

#define VXGE_HW_RING_RXD_RTH_HASH_TYPE_GET(ctrl0)	vxge_bVALn(ctrl0, 20, 4)

#define VXGE_HW_RING_RXD_IS_VLAN_GET(ctrl0)		vxge_bVALn(ctrl0, 24, 1)

#define VXGE_HW_RING_RXD_ETHER_ENCAP_GET(ctrl0)		vxge_bVALn(ctrl0, 25, 2)

#define VXGE_HW_RING_RXD_FRAME_PROTO_GET(ctrl0)		vxge_bVALn(ctrl0, 27, 5)

#define VXGE_HW_RING_RXD_L3_CKSUM_GET(ctrl0)	vxge_bVALn(ctrl0, 32, 16)

#define VXGE_HW_RING_RXD_L4_CKSUM_GET(ctrl0)	vxge_bVALn(ctrl0, 48, 16)

	u64 control_1;

#define VXGE_HW_RING_RXD_1_BUFFER0_SIZE_GET(ctrl1)	vxge_bVALn(ctrl1, 2, 14)
#define VXGE_HW_RING_RXD_1_BUFFER0_SIZE(val) vxge_vBIT(val, 2, 14)
#define VXGE_HW_RING_RXD_1_BUFFER0_SIZE_MASK		vxge_vBIT(0x3FFF, 2, 14)

#define VXGE_HW_RING_RXD_1_RTH_HASH_VAL_GET(ctrl1)    vxge_bVALn(ctrl1, 16, 32)

#define VXGE_HW_RING_RXD_VLAN_TAG_GET(ctrl1)	vxge_bVALn(ctrl1, 48, 16)

	u64 buffer0_ptr;
};

enum vxge_hw_rth_algoritms {
	RTH_ALG_JENKINS = 0,
	RTH_ALG_MS_RSS	= 1,
	RTH_ALG_CRC32C	= 2
};

/**
 * struct vxge_hw_rth_hash_types - RTH hash types.
 * @hash_type_tcpipv4_en: Enables RTH field type HashTypeTcpIPv4
 * @hash_type_ipv4_en: Enables RTH field type HashTypeIPv4
 * @hash_type_tcpipv6_en: Enables RTH field type HashTypeTcpIPv6
 * @hash_type_ipv6_en: Enables RTH field type HashTypeIPv6
 * @hash_type_tcpipv6ex_en: Enables RTH field type HashTypeTcpIPv6Ex
 * @hash_type_ipv6ex_en: Enables RTH field type HashTypeIPv6Ex
 *
 * Used to pass RTH hash types to rts_rts_set.
 *
 * See also: vxge_hw_vpath_rts_rth_set(), vxge_hw_vpath_rts_rth_get().
 */
struct vxge_hw_rth_hash_types {
	u8 hash_type_tcpipv4_en;
	u8 hash_type_ipv4_en;
	u8 hash_type_tcpipv6_en;
	u8 hash_type_ipv6_en;
	u8 hash_type_tcpipv6ex_en;
	u8 hash_type_ipv6ex_en;
};

u32
vxge_hw_device_debug_mask_get(struct __vxge_hw_device *devh);

void vxge_hw_device_debug_set(
	struct __vxge_hw_device *devh,
	enum vxge_debug_level level,
	u32 mask);

u32
vxge_hw_device_error_level_get(struct __vxge_hw_device *devh);

u32
vxge_hw_device_trace_level_get(struct __vxge_hw_device *devh);

u32
vxge_hw_device_debug_mask_get(struct __vxge_hw_device *devh);

/**
 * vxge_hw_ring_rxd_size_get	- Get the size of ring descriptor.
 * @buf_mode: Buffer mode (1, 3 or 5)
 *
 * This function returns the size of RxD for given buffer mode
 */
static inline u32 vxge_hw_ring_rxd_size_get(u32 buf_mode)
{
	return sizeof(struct vxge_hw_ring_rxd_1);
}

/**
 * vxge_hw_ring_rxds_per_block_get - Get the number of rxds per block.
 * @buf_mode: Buffer mode (1 buffer mode only)
 *
 * This function returns the number of RxD for RxD block for given buffer mode
 */
static inline u32 vxge_hw_ring_rxds_per_block_get(u32 buf_mode)
{
	return (u32)((VXGE_HW_BLOCK_SIZE-16) /
		sizeof(struct vxge_hw_ring_rxd_1));
}

/**
 * vxge_hw_ring_rxd_1b_set - Prepare 1-buffer-mode descriptor.
 * @rxdh: Descriptor handle.
 * @dma_pointer: DMA address of	a single receive buffer	this descriptor
 * should carry. Note that by the time vxge_hw_ring_rxd_1b_set is called,
 * the receive buffer should be already mapped to the device
 * @size: Size of the receive @dma_pointer buffer.
 *
 * Prepare 1-buffer-mode Rx	descriptor for posting
 * (via	vxge_hw_ring_rxd_post()).
 *
 * This	inline helper-function does not	return any parameters and always
 * succeeds.
 *
 */
static inline
void vxge_hw_ring_rxd_1b_set(
	void *rxdh,
	dma_addr_t dma_pointer,
	u32 size)
{
	struct vxge_hw_ring_rxd_1 *rxdp = (struct vxge_hw_ring_rxd_1 *)rxdh;
	rxdp->buffer0_ptr = dma_pointer;
	rxdp->control_1	&= ~VXGE_HW_RING_RXD_1_BUFFER0_SIZE_MASK;
	rxdp->control_1	|= VXGE_HW_RING_RXD_1_BUFFER0_SIZE(size);
}

/**
 * vxge_hw_ring_rxd_1b_get - Get data from the completed 1-buf
 * descriptor.
 * @vpath_handle: Virtual Path handle.
 * @rxdh: Descriptor handle.
 * @dma_pointer: DMA address of	a single receive buffer	this descriptor
 * carries. Returned by HW.
 * @pkt_length:	Length (in bytes) of the data in the buffer pointed by
 *
 * Retrieve protocol data from the completed 1-buffer-mode Rx descriptor.
 * This	inline helper-function uses completed descriptor to populate receive
 * buffer pointer and other "out" parameters. The function always succeeds.
 *
 */
static inline
void vxge_hw_ring_rxd_1b_get(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh,
	u32 *pkt_length)
{
	struct vxge_hw_ring_rxd_1 *rxdp = (struct vxge_hw_ring_rxd_1 *)rxdh;

	*pkt_length =
		(u32)VXGE_HW_RING_RXD_1_BUFFER0_SIZE_GET(rxdp->control_1);
}

/**
 * vxge_hw_ring_rxd_1b_info_get - Get extended information associated with
 * a completed receive descriptor for 1b mode.
 * @vpath_handle: Virtual Path handle.
 * @rxdh: Descriptor handle.
 * @rxd_info: Descriptor information
 *
 * Retrieve extended information associated with a completed receive descriptor.
 *
 */
static inline
void vxge_hw_ring_rxd_1b_info_get(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh,
	struct vxge_hw_ring_rxd_info *rxd_info)
{

	struct vxge_hw_ring_rxd_1 *rxdp = (struct vxge_hw_ring_rxd_1 *)rxdh;
	rxd_info->syn_flag =
		(u32)VXGE_HW_RING_RXD_SYN_GET(rxdp->control_0);
	rxd_info->is_icmp =
		(u32)VXGE_HW_RING_RXD_IS_ICMP_GET(rxdp->control_0);
	rxd_info->fast_path_eligible =
		(u32)VXGE_HW_RING_RXD_FAST_PATH_ELIGIBLE_GET(rxdp->control_0);
	rxd_info->l3_cksum_valid =
		(u32)VXGE_HW_RING_RXD_L3_CKSUM_CORRECT_GET(rxdp->control_0);
	rxd_info->l3_cksum =
		(u32)VXGE_HW_RING_RXD_L3_CKSUM_GET(rxdp->control_0);
	rxd_info->l4_cksum_valid =
		(u32)VXGE_HW_RING_RXD_L4_CKSUM_CORRECT_GET(rxdp->control_0);
	rxd_info->l4_cksum =
		(u32)VXGE_HW_RING_RXD_L4_CKSUM_GET(rxdp->control_0);
	rxd_info->frame =
		(u32)VXGE_HW_RING_RXD_ETHER_ENCAP_GET(rxdp->control_0);
	rxd_info->proto =
		(u32)VXGE_HW_RING_RXD_FRAME_PROTO_GET(rxdp->control_0);
	rxd_info->is_vlan =
		(u32)VXGE_HW_RING_RXD_IS_VLAN_GET(rxdp->control_0);
	rxd_info->vlan =
		(u32)VXGE_HW_RING_RXD_VLAN_TAG_GET(rxdp->control_1);
	rxd_info->rth_bucket =
		(u32)VXGE_HW_RING_RXD_RTH_BUCKET_GET(rxdp->control_0);
	rxd_info->rth_it_hit =
		(u32)VXGE_HW_RING_RXD_RTH_IT_HIT_GET(rxdp->control_0);
	rxd_info->rth_spdm_hit =
		(u32)VXGE_HW_RING_RXD_RTH_SPDM_HIT_GET(rxdp->control_0);
	rxd_info->rth_hash_type =
		(u32)VXGE_HW_RING_RXD_RTH_HASH_TYPE_GET(rxdp->control_0);
	rxd_info->rth_value =
		(u32)VXGE_HW_RING_RXD_1_RTH_HASH_VAL_GET(rxdp->control_1);
}

/**
 * vxge_hw_ring_rxd_private_get - Get driver private per-descriptor data
 *                      of 1b mode 3b mode ring.
 * @rxdh: Descriptor handle.
 *
 * Returns: private driver	info associated	with the descriptor.
 * driver requests	per-descriptor space via vxge_hw_ring_attr.
 *
 */
static inline void *vxge_hw_ring_rxd_private_get(void *rxdh)
{
	struct vxge_hw_ring_rxd_1 *rxdp = (struct vxge_hw_ring_rxd_1 *)rxdh;
	return (void *)(size_t)rxdp->host_control;
}

/**
 * vxge_hw_fifo_txdl_cksum_set_bits - Offload checksum.
 * @txdlh: Descriptor handle.
 * @cksum_bits: Specifies which checksums are to be offloaded: IPv4,
 *              and/or TCP and/or UDP.
 *
 * Ask Titan to calculate IPv4 & transport checksums for _this_ transmit
 * descriptor.
 * This API is part of the preparation of the transmit descriptor for posting
 * (via vxge_hw_fifo_txdl_post()). The related "preparation" APIs include
 * vxge_hw_fifo_txdl_mss_set(), vxge_hw_fifo_txdl_buffer_set_aligned(),
 * and vxge_hw_fifo_txdl_buffer_set().
 * All these APIs fill in the fields of the fifo descriptor,
 * in accordance with the Titan specification.
 *
 */
static inline void vxge_hw_fifo_txdl_cksum_set_bits(void *txdlh, u64 cksum_bits)
{
	struct vxge_hw_fifo_txd *txdp = (struct vxge_hw_fifo_txd *)txdlh;
	txdp->control_1 |= cksum_bits;
}

/**
 * vxge_hw_fifo_txdl_mss_set - Set MSS.
 * @txdlh: Descriptor handle.
 * @mss: MSS size for _this_ TCP connection. Passed by TCP stack down to the
 *       driver, which in turn inserts the MSS into the @txdlh.
 *
 * This API is part of the preparation of the transmit descriptor for posting
 * (via vxge_hw_fifo_txdl_post()). The related "preparation" APIs include
 * vxge_hw_fifo_txdl_buffer_set(), vxge_hw_fifo_txdl_buffer_set_aligned(),
 * and vxge_hw_fifo_txdl_cksum_set_bits().
 * All these APIs fill in the fields of the fifo descriptor,
 * in accordance with the Titan specification.
 *
 */
static inline void vxge_hw_fifo_txdl_mss_set(void *txdlh, int mss)
{
	struct vxge_hw_fifo_txd *txdp = (struct vxge_hw_fifo_txd *)txdlh;

	txdp->control_0 |= VXGE_HW_FIFO_TXD_LSO_EN;
	txdp->control_0 |= VXGE_HW_FIFO_TXD_LSO_MSS(mss);
}

/**
 * vxge_hw_fifo_txdl_vlan_set - Set VLAN tag.
 * @txdlh: Descriptor handle.
 * @vlan_tag: 16bit VLAN tag.
 *
 * Insert VLAN tag into specified transmit descriptor.
 * The actual insertion of the tag into outgoing frame is done by the hardware.
 */
static inline void vxge_hw_fifo_txdl_vlan_set(void *txdlh, u16 vlan_tag)
{
	struct vxge_hw_fifo_txd *txdp = (struct vxge_hw_fifo_txd *)txdlh;

	txdp->control_1 |= VXGE_HW_FIFO_TXD_VLAN_ENABLE;
	txdp->control_1 |= VXGE_HW_FIFO_TXD_VLAN_TAG(vlan_tag);
}

/**
 * vxge_hw_fifo_txdl_private_get - Retrieve per-descriptor private data.
 * @txdlh: Descriptor handle.
 *
 * Retrieve per-descriptor private data.
 * Note that driver requests per-descriptor space via
 * struct vxge_hw_fifo_attr passed to
 * vxge_hw_vpath_open().
 *
 * Returns: private driver data associated with the descriptor.
 */
static inline void *vxge_hw_fifo_txdl_private_get(void *txdlh)
{
	struct vxge_hw_fifo_txd *txdp  = (struct vxge_hw_fifo_txd *)txdlh;

	return (void *)(size_t)txdp->host_control;
}

/**
 * struct vxge_hw_ring_attr - Ring open "template".
 * @callback: Ring completion callback. HW invokes the callback when there
 *            are new completions on that ring. In many implementations
 *            the @callback executes in the hw interrupt context.
 * @rxd_init: Ring's descriptor-initialize callback.
 *            See vxge_hw_ring_rxd_init_f{}.
 *            If not NULL, HW invokes the callback when opening
 *            the ring.
 * @rxd_term: Ring's descriptor-terminate callback. If not NULL,
 *          HW invokes the callback when closing the corresponding ring.
 *          See also vxge_hw_ring_rxd_term_f{}.
 * @userdata: User-defined "context" of _that_ ring. Passed back to the
 *            user as one of the @callback, @rxd_init, and @rxd_term arguments.
 * @per_rxd_space: If specified (i.e., greater than zero): extra space
 *              reserved by HW per each receive descriptor.
 *              Can be used to store
 *              and retrieve on completion, information specific
 *              to the driver.
 *
 * Ring open "template". User fills the structure with ring
 * attributes and passes it to vxge_hw_vpath_open().
 */
struct vxge_hw_ring_attr {
	enum vxge_hw_status (*callback)(
			struct __vxge_hw_ring *ringh,
			void *rxdh,
			u8 t_code,
			void *userdata);

	enum vxge_hw_status (*rxd_init)(
			void *rxdh,
			void *userdata);

	void (*rxd_term)(
			void *rxdh,
			enum vxge_hw_rxd_state state,
			void *userdata);

	void		*userdata;
	u32		per_rxd_space;
};

/**
 * function vxge_hw_fifo_callback_f - FIFO callback.
 * @vpath_handle: Virtual path whose Fifo "containing" 1 or more completed
 *             descriptors.
 * @txdlh: First completed descriptor.
 * @txdl_priv: Pointer to per txdl space allocated
 * @t_code: Transfer code, as per Titan User Guide.
 *          Returned by HW.
 * @host_control: Opaque 64bit data stored by driver inside the Titan
 *            descriptor prior to posting the latter on the fifo
 *            via vxge_hw_fifo_txdl_post(). The @host_control is returned
 *            as is to the driver with each completed descriptor.
 * @userdata: Opaque per-fifo data specified at fifo open
 *            time, via vxge_hw_vpath_open().
 *
 * Fifo completion callback (type declaration). A single per-fifo
 * callback is specified at fifo open time, via
 * vxge_hw_vpath_open(). Typically gets called as part of the processing
 * of the Interrupt Service Routine.
 *
 * Fifo callback gets called by HW if, and only if, there is at least
 * one new completion on a given fifo. Upon processing the first @txdlh driver
 * is _supposed_ to continue consuming completions using:
 *    - vxge_hw_fifo_txdl_next_completed()
 *
 * Note that failure to process new completions in a timely fashion
 * leads to VXGE_HW_INF_OUT_OF_DESCRIPTORS condition.
 *
 * Non-zero @t_code means failure to process transmit descriptor.
 *
 * In the "transmit" case the failure could happen, for instance, when the
 * link is down, in which case Titan completes the descriptor because it
 * is not able to send the data out.
 *
 * For details please refer to Titan User Guide.
 *
 * See also: vxge_hw_fifo_txdl_next_completed(), vxge_hw_fifo_txdl_term_f{}.
 */
/**
 * function vxge_hw_fifo_txdl_term_f - Terminate descriptor callback.
 * @txdlh: First completed descriptor.
 * @txdl_priv: Pointer to per txdl space allocated
 * @state: One of the enum vxge_hw_txdl_state{} enumerated states.
 * @userdata: Per-fifo user data (a.k.a. context) specified at
 * fifo open time, via vxge_hw_vpath_open().
 *
 * Terminate descriptor callback. Unless NULL is specified in the
 * struct vxge_hw_fifo_attr{} structure passed to vxge_hw_vpath_open()),
 * HW invokes the callback as part of closing fifo, prior to
 * de-allocating the ring and associated data structures
 * (including descriptors).
 * driver should utilize the callback to (for instance) unmap
 * and free DMA data buffers associated with the posted (state =
 * VXGE_HW_TXDL_STATE_POSTED) descriptors,
 * as well as other relevant cleanup functions.
 *
 * See also: struct vxge_hw_fifo_attr{}
 */
/**
 * struct vxge_hw_fifo_attr - Fifo open "template".
 * @callback: Fifo completion callback. HW invokes the callback when there
 *            are new completions on that fifo. In many implementations
 *            the @callback executes in the hw interrupt context.
 * @txdl_term: Fifo's descriptor-terminate callback. If not NULL,
 *          HW invokes the callback when closing the corresponding fifo.
 *          See also vxge_hw_fifo_txdl_term_f{}.
 * @userdata: User-defined "context" of _that_ fifo. Passed back to the
 *            user as one of the @callback, and @txdl_term arguments.
 * @per_txdl_space: If specified (i.e., greater than zero): extra space
 *              reserved by HW per each transmit descriptor. Can be used to
 *              store, and retrieve on completion, information specific
 *              to the driver.
 *
 * Fifo open "template". User fills the structure with fifo
 * attributes and passes it to vxge_hw_vpath_open().
 */
struct vxge_hw_fifo_attr {

	enum vxge_hw_status (*callback)(
			struct __vxge_hw_fifo *fifo_handle,
			void *txdlh,
			enum vxge_hw_fifo_tcode t_code,
			void *userdata,
			struct sk_buff ***skb_ptr,
			int nr_skb, int *more);

	void (*txdl_term)(
			void *txdlh,
			enum vxge_hw_txdl_state state,
			void *userdata);

	void		*userdata;
	u32		per_txdl_space;
};

/**
 * struct vxge_hw_vpath_attr - Attributes of virtual path
 * @vp_id: Identifier of Virtual Path
 * @ring_attr: Attributes of ring for non-offload receive
 * @fifo_attr: Attributes of fifo for non-offload transmit
 *
 * Attributes of virtual path.  This structure is passed as parameter
 * to the vxge_hw_vpath_open() routine to set the attributes of ring and fifo.
 */
struct vxge_hw_vpath_attr {
	u32				vp_id;
	struct vxge_hw_ring_attr	ring_attr;
	struct vxge_hw_fifo_attr	fifo_attr;
};

enum vxge_hw_status
__vxge_hw_blockpool_create(struct __vxge_hw_device *hldev,
			struct __vxge_hw_blockpool  *blockpool,
			u32 pool_size,
			u32 pool_max);

void
__vxge_hw_blockpool_destroy(struct __vxge_hw_blockpool  *blockpool);

struct __vxge_hw_blockpool_entry *
__vxge_hw_blockpool_block_allocate(struct __vxge_hw_device *hldev,
			u32 size);

void
__vxge_hw_blockpool_block_free(struct __vxge_hw_device *hldev,
			struct __vxge_hw_blockpool_entry *entry);

void *
__vxge_hw_blockpool_malloc(struct __vxge_hw_device *hldev,
			u32 size,
			struct vxge_hw_mempool_dma *dma_object);

void
__vxge_hw_blockpool_free(struct __vxge_hw_device *hldev,
			void *memblock,
			u32 size,
			struct vxge_hw_mempool_dma *dma_object);

enum vxge_hw_status
__vxge_hw_device_fifo_config_check(struct vxge_hw_fifo_config *fifo_config);

enum vxge_hw_status
__vxge_hw_device_config_check(struct vxge_hw_device_config *new_config);

enum vxge_hw_status
vxge_hw_mgmt_device_config(struct __vxge_hw_device *devh,
		struct vxge_hw_device_config	*dev_config, int size);

enum vxge_hw_status __devinit vxge_hw_device_hw_info_get(
	void __iomem *bar0,
	struct vxge_hw_device_hw_info *hw_info);

enum vxge_hw_status
__vxge_hw_vpath_fw_ver_get(
	u32	vp_id,
	struct vxge_hw_vpath_reg __iomem *vpath_reg,
	struct vxge_hw_device_hw_info *hw_info);

enum vxge_hw_status
__vxge_hw_vpath_card_info_get(
	u32 vp_id,
	struct vxge_hw_vpath_reg __iomem *vpath_reg,
	struct vxge_hw_device_hw_info *hw_info);

enum vxge_hw_status __devinit vxge_hw_device_config_default_get(
	struct vxge_hw_device_config *device_config);

/**
 * vxge_hw_device_link_state_get - Get link state.
 * @devh: HW device handle.
 *
 * Get link state.
 * Returns: link state.
 */
static inline
enum vxge_hw_device_link_state vxge_hw_device_link_state_get(
	struct __vxge_hw_device *devh)
{
	return devh->link_state;
}

void vxge_hw_device_terminate(struct __vxge_hw_device *devh);

const u8 *
vxge_hw_device_serial_number_get(struct __vxge_hw_device *devh);

u16 vxge_hw_device_link_width_get(struct __vxge_hw_device *devh);

const u8 *
vxge_hw_device_product_name_get(struct __vxge_hw_device *devh);

enum vxge_hw_status __devinit vxge_hw_device_initialize(
	struct __vxge_hw_device **devh,
	struct vxge_hw_device_attr *attr,
	struct vxge_hw_device_config *device_config);

enum vxge_hw_status vxge_hw_device_getpause_data(
	 struct __vxge_hw_device *devh,
	 u32 port,
	 u32 *tx,
	 u32 *rx);

enum vxge_hw_status vxge_hw_device_setpause_data(
	struct __vxge_hw_device *devh,
	u32 port,
	u32 tx,
	u32 rx);

static inline void *vxge_os_dma_malloc(struct pci_dev *pdev,
			unsigned long size,
			struct pci_dev **p_dmah,
			struct pci_dev **p_dma_acch)
{
	gfp_t flags;
	void *vaddr;
	unsigned long misaligned = 0;
	*p_dma_acch = *p_dmah = NULL;

	if (in_interrupt())
		flags = GFP_ATOMIC | GFP_DMA;
	else
		flags = GFP_KERNEL | GFP_DMA;

	size += VXGE_CACHE_LINE_SIZE;

	vaddr = kmalloc((size), flags);
	if (vaddr == NULL)
		return vaddr;
	misaligned = (unsigned long)VXGE_ALIGN(*((u64 *)&vaddr),
				VXGE_CACHE_LINE_SIZE);
	*(unsigned long *)p_dma_acch = misaligned;
	vaddr = (void *)((u8 *)vaddr + misaligned);
	return vaddr;
}

extern void vxge_hw_blockpool_block_add(
			struct __vxge_hw_device *devh,
			void *block_addr,
			u32 length,
			struct pci_dev *dma_h,
			struct pci_dev *acc_handle);

static inline void vxge_os_dma_malloc_async(struct pci_dev *pdev, void *devh,
					unsigned long size)
{
	gfp_t flags;
	void *vaddr;

	if (in_interrupt())
		flags = GFP_ATOMIC | GFP_DMA;
	else
		flags = GFP_KERNEL | GFP_DMA;

	vaddr = kmalloc((size), flags);

	vxge_hw_blockpool_block_add(devh, vaddr, size, pdev, pdev);
}

static inline void vxge_os_dma_free(struct pci_dev *pdev, const void *vaddr,
			struct pci_dev **p_dma_acch)
{
	unsigned long misaligned = *(unsigned long *)p_dma_acch;
	u8 *tmp = (u8 *)vaddr;
	tmp -= misaligned;
	kfree((void *)tmp);
}

/*
 * __vxge_hw_mempool_item_priv - will return pointer on per item private space
 */
static inline void*
__vxge_hw_mempool_item_priv(
	struct vxge_hw_mempool *mempool,
	u32 memblock_idx,
	void *item,
	u32 *memblock_item_idx)
{
	ptrdiff_t offset;
	void *memblock = mempool->memblocks_arr[memblock_idx];


	offset = (u32)((u8 *)item - (u8 *)memblock);
	vxge_assert(offset >= 0 && (u32)offset < mempool->memblock_size);

	(*memblock_item_idx) = (u32) offset / mempool->item_size;
	vxge_assert((*memblock_item_idx) < mempool->items_per_memblock);

	return (u8 *)mempool->memblocks_priv_arr[memblock_idx] +
			    (*memblock_item_idx) * mempool->items_priv_size;
}

enum vxge_hw_status
__vxge_hw_mempool_grow(
	struct vxge_hw_mempool *mempool,
	u32 num_allocate,
	u32 *num_allocated);

struct vxge_hw_mempool*
__vxge_hw_mempool_create(
	struct __vxge_hw_device *devh,
	u32 memblock_size,
	u32 item_size,
	u32 private_size,
	u32 items_initial,
	u32 items_max,
	struct vxge_hw_mempool_cbs *mp_callback,
	void *userdata);

struct __vxge_hw_channel*
__vxge_hw_channel_allocate(struct __vxge_hw_vpath_handle *vph,
			enum __vxge_hw_channel_type type, u32 length,
			u32 per_dtr_space, void *userdata);

void
__vxge_hw_channel_free(
	struct __vxge_hw_channel *channel);

enum vxge_hw_status
__vxge_hw_channel_initialize(
	struct __vxge_hw_channel *channel);

enum vxge_hw_status
__vxge_hw_channel_reset(
	struct __vxge_hw_channel *channel);

/*
 * __vxge_hw_fifo_txdl_priv - Return the max fragments allocated
 * for the fifo.
 * @fifo: Fifo
 * @txdp: Poniter to a TxD
 */
static inline struct __vxge_hw_fifo_txdl_priv *
__vxge_hw_fifo_txdl_priv(
	struct __vxge_hw_fifo *fifo,
	struct vxge_hw_fifo_txd *txdp)
{
	return (struct __vxge_hw_fifo_txdl_priv *)
			(((char *)((ulong)txdp->host_control)) +
				fifo->per_txdl_space);
}

enum vxge_hw_status vxge_hw_vpath_open(
	struct __vxge_hw_device *devh,
	struct vxge_hw_vpath_attr *attr,
	struct __vxge_hw_vpath_handle **vpath_handle);

enum vxge_hw_status
__vxge_hw_device_vpath_reset_in_prog_check(u64 __iomem *vpath_rst_in_prog);

enum vxge_hw_status vxge_hw_vpath_close(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status
vxge_hw_vpath_reset(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status
vxge_hw_vpath_recover_from_reset(
	struct __vxge_hw_vpath_handle *vpath_handle);

void
vxge_hw_vpath_enable(struct __vxge_hw_vpath_handle *vp);

enum vxge_hw_status
vxge_hw_vpath_check_leak(struct __vxge_hw_ring *ringh);

enum vxge_hw_status vxge_hw_vpath_mtu_set(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u32 new_mtu);

enum vxge_hw_status vxge_hw_vpath_stats_enable(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status
__vxge_hw_vpath_stats_access(
	struct __vxge_hw_virtualpath	*vpath,
	u32			operation,
	u32			offset,
	u64			*stat);

enum vxge_hw_status
__vxge_hw_vpath_xmac_tx_stats_get(
	struct __vxge_hw_virtualpath	*vpath,
	struct vxge_hw_xmac_vpath_tx_stats *vpath_tx_stats);

enum vxge_hw_status
__vxge_hw_vpath_xmac_rx_stats_get(
	struct __vxge_hw_virtualpath	*vpath,
	struct vxge_hw_xmac_vpath_rx_stats *vpath_rx_stats);

enum vxge_hw_status
__vxge_hw_vpath_stats_get(
	struct __vxge_hw_virtualpath *vpath,
	struct vxge_hw_vpath_stats_hw_info *hw_stats);

void
vxge_hw_vpath_rx_doorbell_init(struct __vxge_hw_vpath_handle *vp);

enum vxge_hw_status
__vxge_hw_device_vpath_config_check(struct vxge_hw_vp_config *vp_config);

void
__vxge_hw_device_pci_e_init(struct __vxge_hw_device *hldev);

enum vxge_hw_status
__vxge_hw_legacy_swapper_set(struct vxge_hw_legacy_reg __iomem *legacy_reg);

enum vxge_hw_status
__vxge_hw_vpath_swapper_set(struct vxge_hw_vpath_reg __iomem *vpath_reg);

enum vxge_hw_status
__vxge_hw_kdfc_swapper_set(struct vxge_hw_legacy_reg __iomem *legacy_reg,
	struct vxge_hw_vpath_reg __iomem *vpath_reg);

enum vxge_hw_status
__vxge_hw_device_register_poll(
	void __iomem	*reg,
	u64 mask, u32 max_millis);

#ifndef readq
static inline u64 readq(void __iomem *addr)
{
	u64 ret = 0;
	ret = readl(addr + 4);
	ret <<= 32;
	ret |= readl(addr);

	return ret;
}
#endif

#ifndef writeq
static inline void writeq(u64 val, void __iomem *addr)
{
	writel((u32) (val), addr);
	writel((u32) (val >> 32), (addr + 4));
}
#endif

static inline void __vxge_hw_pio_mem_write32_upper(u32 val, void __iomem *addr)
{
	writel(val, addr + 4);
}

static inline void __vxge_hw_pio_mem_write32_lower(u32 val, void __iomem *addr)
{
	writel(val, addr);
}

static inline enum vxge_hw_status
__vxge_hw_pio_mem_write64(u64 val64, void __iomem *addr,
			  u64 mask, u32 max_millis)
{
	enum vxge_hw_status status = VXGE_HW_OK;

	__vxge_hw_pio_mem_write32_lower((u32)vxge_bVALn(val64, 32, 32), addr);
	wmb();
	__vxge_hw_pio_mem_write32_upper((u32)vxge_bVALn(val64, 0, 32), addr);
	wmb();

	status = __vxge_hw_device_register_poll(addr, mask, max_millis);
	return status;
}

struct vxge_hw_toc_reg __iomem *
__vxge_hw_device_toc_get(void __iomem *bar0);

enum vxge_hw_status
__vxge_hw_device_reg_addr_get(struct __vxge_hw_device *hldev);

void
__vxge_hw_device_id_get(struct __vxge_hw_device *hldev);

void
__vxge_hw_device_host_info_get(struct __vxge_hw_device *hldev);

enum vxge_hw_status
vxge_hw_device_flick_link_led(struct __vxge_hw_device *devh, u64 on_off);

enum vxge_hw_status
__vxge_hw_device_initialize(struct __vxge_hw_device *hldev);

enum vxge_hw_status
__vxge_hw_vpath_pci_read(
	struct __vxge_hw_virtualpath	*vpath,
	u32			phy_func_0,
	u32			offset,
	u32			*val);

enum vxge_hw_status
__vxge_hw_vpath_addr_get(
	u32 vp_id,
	struct vxge_hw_vpath_reg __iomem *vpath_reg,
	u8 (macaddr)[ETH_ALEN],
	u8 (macaddr_mask)[ETH_ALEN]);

u32
__vxge_hw_vpath_func_id_get(
	u32 vp_id, struct vxge_hw_vpmgmt_reg __iomem *vpmgmt_reg);

enum vxge_hw_status
__vxge_hw_vpath_reset_check(struct __vxge_hw_virtualpath *vpath);

/**
 * vxge_debug
 * @level: level of debug verbosity.
 * @mask: mask for the debug
 * @buf: Circular buffer for tracing
 * @fmt: printf like format string
 *
 * Provides logging facilities. Can be customized on per-module
 * basis or/and with debug levels. Input parameters, except
 * module and level, are the same as posix printf. This function
 * may be compiled out if DEBUG macro was never defined.
 * See also: enum vxge_debug_level{}.
 */

#define vxge_trace_aux(level, mask, fmt, ...) \
{\
		vxge_os_vaprintf(level, mask, fmt, __VA_ARGS__);\
}

#define vxge_debug(module, level, mask, fmt, ...) { \
if ((level >= VXGE_TRACE && ((module & VXGE_DEBUG_TRACE_MASK) == module)) || \
	(level >= VXGE_ERR && ((module & VXGE_DEBUG_ERR_MASK) == module))) {\
	if ((mask & VXGE_DEBUG_MASK) == mask)\
		vxge_trace_aux(level, mask, fmt, __VA_ARGS__); \
} \
}

#if (VXGE_COMPONENT_LL & VXGE_DEBUG_MODULE_MASK)
#define vxge_debug_ll(level, mask, fmt, ...) \
{\
	vxge_debug(VXGE_COMPONENT_LL, level, mask, fmt, __VA_ARGS__);\
}

#else
#define vxge_debug_ll(level, mask, fmt, ...)
#endif

enum vxge_hw_status vxge_hw_vpath_rts_rth_itable_set(
			struct __vxge_hw_vpath_handle **vpath_handles,
			u32 vpath_count,
			u8 *mtable,
			u8 *itable,
			u32 itable_size);

enum vxge_hw_status vxge_hw_vpath_rts_rth_set(
	struct __vxge_hw_vpath_handle *vpath_handle,
	enum vxge_hw_rth_algoritms algorithm,
	struct vxge_hw_rth_hash_types *hash_type,
	u16 bucket_size);

#endif

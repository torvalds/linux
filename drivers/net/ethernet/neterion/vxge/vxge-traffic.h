/******************************************************************************
 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 *
 * vxge-traffic.h: Driver for Exar Corp's X3100 Series 10GbE PCIe I/O
 *                 Virtualized Server Adapter.
 * Copyright(c) 2002-2010 Exar Corp.
 ******************************************************************************/
#ifndef VXGE_TRAFFIC_H
#define VXGE_TRAFFIC_H

#include "vxge-reg.h"
#include "vxge-version.h"

#define VXGE_HW_DTR_MAX_T_CODE		16
#define VXGE_HW_ALL_FOXES		0xFFFFFFFFFFFFFFFFULL
#define VXGE_HW_INTR_MASK_ALL		0xFFFFFFFFFFFFFFFFULL
#define	VXGE_HW_MAX_VIRTUAL_PATHS	17

#define VXGE_HW_MAC_MAX_MAC_PORT_ID	2

#define VXGE_HW_DEFAULT_32		0xffffffff
/* frames sizes */
#define VXGE_HW_HEADER_802_2_SIZE	3
#define VXGE_HW_HEADER_SNAP_SIZE	5
#define VXGE_HW_HEADER_VLAN_SIZE	4
#define VXGE_HW_MAC_HEADER_MAX_SIZE \
			(ETH_HLEN + \
			VXGE_HW_HEADER_802_2_SIZE + \
			VXGE_HW_HEADER_VLAN_SIZE + \
			VXGE_HW_HEADER_SNAP_SIZE)

/* 32bit alignments */
#define VXGE_HW_HEADER_ETHERNET_II_802_3_ALIGN		2
#define VXGE_HW_HEADER_802_2_SNAP_ALIGN			2
#define VXGE_HW_HEADER_802_2_ALIGN			3
#define VXGE_HW_HEADER_SNAP_ALIGN			1

#define VXGE_HW_L3_CKSUM_OK				0xFFFF
#define VXGE_HW_L4_CKSUM_OK				0xFFFF

/* Forward declarations */
struct __vxge_hw_device;
struct __vxge_hw_vpath_handle;
struct vxge_hw_vp_config;
struct __vxge_hw_virtualpath;
struct __vxge_hw_channel;
struct __vxge_hw_fifo;
struct __vxge_hw_ring;
struct vxge_hw_ring_attr;
struct vxge_hw_mempool;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/*VXGE_HW_STATUS_H*/

#define VXGE_HW_EVENT_BASE			0
#define VXGE_LL_EVENT_BASE			100

/**
 * enum vxge_hw_event- Enumerates slow-path HW events.
 * @VXGE_HW_EVENT_UNKNOWN: Unknown (and invalid) event.
 * @VXGE_HW_EVENT_SERR: Serious vpath hardware error event.
 * @VXGE_HW_EVENT_ECCERR: vpath ECC error event.
 * @VXGE_HW_EVENT_VPATH_ERR: Error local to the respective vpath
 * @VXGE_HW_EVENT_FIFO_ERR: FIFO Doorbell fifo error.
 * @VXGE_HW_EVENT_SRPCIM_SERR: srpcim hardware error event.
 * @VXGE_HW_EVENT_MRPCIM_SERR: mrpcim hardware error event.
 * @VXGE_HW_EVENT_MRPCIM_ECCERR: mrpcim ecc error event.
 * @VXGE_HW_EVENT_RESET_START: Privileged entity is starting device reset
 * @VXGE_HW_EVENT_RESET_COMPLETE: Device reset has been completed
 * @VXGE_HW_EVENT_SLOT_FREEZE: Slot-freeze event. Driver tries to distinguish
 * slot-freeze from the rest critical events (e.g. ECC) when it is
 * impossible to PIO read "through" the bus, i.e. when getting all-foxes.
 *
 * enum vxge_hw_event enumerates slow-path HW eventis.
 *
 * See also: struct vxge_hw_uld_cbs{}, vxge_uld_link_up_f{},
 * vxge_uld_link_down_f{}.
 */
enum vxge_hw_event {
	VXGE_HW_EVENT_UNKNOWN		= 0,
	/* HW events */
	VXGE_HW_EVENT_RESET_START	= VXGE_HW_EVENT_BASE + 1,
	VXGE_HW_EVENT_RESET_COMPLETE	= VXGE_HW_EVENT_BASE + 2,
	VXGE_HW_EVENT_LINK_DOWN		= VXGE_HW_EVENT_BASE + 3,
	VXGE_HW_EVENT_LINK_UP		= VXGE_HW_EVENT_BASE + 4,
	VXGE_HW_EVENT_ALARM_CLEARED	= VXGE_HW_EVENT_BASE + 5,
	VXGE_HW_EVENT_ECCERR		= VXGE_HW_EVENT_BASE + 6,
	VXGE_HW_EVENT_MRPCIM_ECCERR	= VXGE_HW_EVENT_BASE + 7,
	VXGE_HW_EVENT_FIFO_ERR		= VXGE_HW_EVENT_BASE + 8,
	VXGE_HW_EVENT_VPATH_ERR		= VXGE_HW_EVENT_BASE + 9,
	VXGE_HW_EVENT_CRITICAL_ERR	= VXGE_HW_EVENT_BASE + 10,
	VXGE_HW_EVENT_SERR		= VXGE_HW_EVENT_BASE + 11,
	VXGE_HW_EVENT_SRPCIM_SERR	= VXGE_HW_EVENT_BASE + 12,
	VXGE_HW_EVENT_MRPCIM_SERR	= VXGE_HW_EVENT_BASE + 13,
	VXGE_HW_EVENT_SLOT_FREEZE	= VXGE_HW_EVENT_BASE + 14,
};

#define VXGE_HW_SET_LEVEL(a, b) (((a) > (b)) ? (a) : (b))

/*
 * struct vxge_hw_mempool_dma - Represents DMA objects passed to the
	caller.
 */
struct vxge_hw_mempool_dma {
	dma_addr_t			addr;
	struct pci_dev *handle;
	struct pci_dev *acc_handle;
};

/*
 * vxge_hw_mempool_item_f  - Mempool item alloc/free callback
 * @mempoolh: Memory pool handle.
 * @memblock: Address of memory block
 * @memblock_index: Index of memory block
 * @item: Item that gets allocated or freed.
 * @index: Item's index in the memory pool.
 * @is_last: True, if this item is the last one in the pool; false - otherwise.
 * userdata: Per-pool user context.
 *
 * Memory pool allocation/deallocation callback.
 */

/*
 * struct vxge_hw_mempool - Memory pool.
 */
struct vxge_hw_mempool {

	void (*item_func_alloc)(
	struct vxge_hw_mempool *mempoolh,
	u32			memblock_index,
	struct vxge_hw_mempool_dma	*dma_object,
	u32			index,
	u32			is_last);

	void		*userdata;
	void		**memblocks_arr;
	void		**memblocks_priv_arr;
	struct vxge_hw_mempool_dma	*memblocks_dma_arr;
	struct __vxge_hw_device *devh;
	u32			memblock_size;
	u32			memblocks_max;
	u32			memblocks_allocated;
	u32			item_size;
	u32			items_max;
	u32			items_initial;
	u32			items_current;
	u32			items_per_memblock;
	void		**items_arr;
	u32			items_priv_size;
};

#define	VXGE_HW_MAX_INTR_PER_VP				4
#define	VXGE_HW_VPATH_INTR_TX				0
#define	VXGE_HW_VPATH_INTR_RX				1
#define	VXGE_HW_VPATH_INTR_EINTA			2
#define	VXGE_HW_VPATH_INTR_BMAP				3

#define VXGE_HW_BLOCK_SIZE				4096

/**
 * struct vxge_hw_tim_intr_config - Titan Tim interrupt configuration.
 * @intr_enable: Set to 1, if interrupt is enabled.
 * @btimer_val: Boundary Timer Initialization value in units of 272 ns.
 * @timer_ac_en: Timer Automatic Cancel. 1 : Automatic Canceling Enable: when
 *             asserted, other interrupt-generating entities will cancel the
 *             scheduled timer interrupt.
 * @timer_ci_en: Timer Continuous Interrupt. 1 : Continuous Interrupting Enable:
 *             When asserted, an interrupt will be generated every time the
 *             boundary timer expires, even if no traffic has been transmitted
 *             on this interrupt.
 * @timer_ri_en: Timer Consecutive (Re-) Interrupt 1 : Consecutive
 *             (Re-) Interrupt Enable: When asserted, an interrupt will be
 *             generated the next time the timer expires, even if no traffic has
 *             been transmitted on this interrupt. (This will only happen once
 *             each time that this value is written to the TIM.) This bit is
 *             cleared by H/W at the end of the current-timer-interval when
 *             the interrupt is triggered.
 * @rtimer_val: Restriction Timer Initialization value in units of 272 ns.
 * @util_sel: Utilization Selector. Selects which of the workload approximations
 *             to use (e.g. legacy Tx utilization, Tx/Rx utilization, host
 *             specified utilization etc.), selects one of
 *             the 17 host configured values.
 *             0-Virtual Path 0
 *             1-Virtual Path 1
 *             ...
 *             16-Virtual Path 17
 *             17-Legacy Tx network utilization, provided by TPA
 *             18-Legacy Rx network utilization, provided by FAU
 *             19-Average of legacy Rx and Tx utilization calculated from link
 *                utilization values.
 *             20-31-Invalid configurations
 *             32-Host utilization for Virtual Path 0
 *             33-Host utilization for Virtual Path 1
 *             ...
 *             48-Host utilization for Virtual Path 17
 *             49-Legacy Tx network utilization, provided by TPA
 *             50-Legacy Rx network utilization, provided by FAU
 *             51-Average of legacy Rx and Tx utilization calculated from
 *                link utilization values.
 *             52-63-Invalid configurations
 * @ltimer_val: Latency Timer Initialization Value in units of 272 ns.
 * @txd_cnt_en: TxD Return Event Count Enable. This configuration bit when set
 *             to 1 enables counting of TxD0 returns (signalled by PCC's),
 *             towards utilization event count values.
 * @urange_a: Defines the upper limit (in percent) for this utilization range
 *             to be active. This range is considered active
 *             if 0 = UTIL = URNG_A
 *             and the UEC_A field (below) is non-zero.
 * @uec_a: Utilization Event Count A. If this range is active, the adapter will
 *             wait until UEC_A events have occurred on the interrupt before
 *             generating an interrupt.
 * @urange_b: Link utilization range B.
 * @uec_b: Utilization Event Count B.
 * @urange_c: Link utilization range C.
 * @uec_c: Utilization Event Count C.
 * @urange_d: Link utilization range D.
 * @uec_d: Utilization Event Count D.
 * Traffic Interrupt Controller Module interrupt configuration.
 */
struct vxge_hw_tim_intr_config {

	u32				intr_enable;
#define VXGE_HW_TIM_INTR_ENABLE				1
#define VXGE_HW_TIM_INTR_DISABLE				0
#define VXGE_HW_TIM_INTR_DEFAULT				0

	u32				btimer_val;
#define VXGE_HW_MIN_TIM_BTIMER_VAL				0
#define VXGE_HW_MAX_TIM_BTIMER_VAL				67108864
#define VXGE_HW_USE_FLASH_DEFAULT				(~0)

	u32				timer_ac_en;
#define VXGE_HW_TIM_TIMER_AC_ENABLE				1
#define VXGE_HW_TIM_TIMER_AC_DISABLE				0

	u32				timer_ci_en;
#define VXGE_HW_TIM_TIMER_CI_ENABLE				1
#define VXGE_HW_TIM_TIMER_CI_DISABLE				0

	u32				timer_ri_en;
#define VXGE_HW_TIM_TIMER_RI_ENABLE				1
#define VXGE_HW_TIM_TIMER_RI_DISABLE				0

	u32				rtimer_val;
#define VXGE_HW_MIN_TIM_RTIMER_VAL				0
#define VXGE_HW_MAX_TIM_RTIMER_VAL				67108864

	u32				util_sel;
#define VXGE_HW_TIM_UTIL_SEL_LEGACY_TX_NET_UTIL		17
#define VXGE_HW_TIM_UTIL_SEL_LEGACY_RX_NET_UTIL		18
#define VXGE_HW_TIM_UTIL_SEL_LEGACY_TX_RX_AVE_NET_UTIL		19
#define VXGE_HW_TIM_UTIL_SEL_PER_VPATH				63

	u32				ltimer_val;
#define VXGE_HW_MIN_TIM_LTIMER_VAL				0
#define VXGE_HW_MAX_TIM_LTIMER_VAL				67108864

	/* Line utilization interrupts */
	u32				urange_a;
#define VXGE_HW_MIN_TIM_URANGE_A				0
#define VXGE_HW_MAX_TIM_URANGE_A				100

	u32				uec_a;
#define VXGE_HW_MIN_TIM_UEC_A					0
#define VXGE_HW_MAX_TIM_UEC_A					65535

	u32				urange_b;
#define VXGE_HW_MIN_TIM_URANGE_B				0
#define VXGE_HW_MAX_TIM_URANGE_B				100

	u32				uec_b;
#define VXGE_HW_MIN_TIM_UEC_B					0
#define VXGE_HW_MAX_TIM_UEC_B					65535

	u32				urange_c;
#define VXGE_HW_MIN_TIM_URANGE_C				0
#define VXGE_HW_MAX_TIM_URANGE_C				100

	u32				uec_c;
#define VXGE_HW_MIN_TIM_UEC_C					0
#define VXGE_HW_MAX_TIM_UEC_C					65535

	u32				uec_d;
#define VXGE_HW_MIN_TIM_UEC_D					0
#define VXGE_HW_MAX_TIM_UEC_D					65535
};

#define	VXGE_HW_STATS_OP_READ					0
#define	VXGE_HW_STATS_OP_CLEAR_STAT				1
#define	VXGE_HW_STATS_OP_CLEAR_ALL_VPATH_STATS			2
#define	VXGE_HW_STATS_OP_CLEAR_ALL_STATS_OF_LOC			2
#define	VXGE_HW_STATS_OP_CLEAR_ALL_STATS			3

#define	VXGE_HW_STATS_LOC_AGGR					17
#define VXGE_HW_STATS_AGGRn_OFFSET				0x00720

#define VXGE_HW_STATS_VPATH_TX_OFFSET				0x0
#define VXGE_HW_STATS_VPATH_RX_OFFSET				0x00090

#define	VXGE_HW_STATS_VPATH_PROG_EVENT_VNUM0_OFFSET	   (0x001d0 >> 3)
#define	VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM0(bits) \
						vxge_bVALn(bits, 0, 32)

#define	VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM1(bits) \
						vxge_bVALn(bits, 32, 32)

#define	VXGE_HW_STATS_VPATH_PROG_EVENT_VNUM2_OFFSET	   (0x001d8 >> 3)
#define	VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM2(bits) \
						vxge_bVALn(bits, 0, 32)

#define	VXGE_HW_STATS_GET_VPATH_PROG_EVENT_VNUM3(bits) \
						vxge_bVALn(bits, 32, 32)

/**
 * struct vxge_hw_xmac_aggr_stats - Per-Aggregator XMAC Statistics
 *
 * @tx_frms: Count of data frames transmitted on this Aggregator on all
 *             its Aggregation ports. Does not include LACPDUs or Marker PDUs.
 *             However, does include frames discarded by the Distribution
 *             function.
 * @tx_data_octets: Count of data and padding octets of frames transmitted
 *             on this Aggregator on all its Aggregation ports. Does not include
 *             octets of LACPDUs or Marker PDUs. However, does include octets of
 *             frames discarded by the Distribution function.
 * @tx_mcast_frms: Count of data frames transmitted (to a group destination
 *             address other than the broadcast address) on this Aggregator on
 *             all its Aggregation ports. Does not include LACPDUs or Marker
 *             PDUs. However, does include frames discarded by the Distribution
 *             function.
 * @tx_bcast_frms: Count of broadcast data frames transmitted on this Aggregator
 *             on all its Aggregation ports. Does not include LACPDUs or Marker
 *             PDUs. However, does include frames discarded by the Distribution
 *             function.
 * @tx_discarded_frms: Count of data frames to be transmitted on this Aggregator
 *             that are discarded by the Distribution function. This occurs when
 *             conversation are allocated to different ports and have to be
 *             flushed on old ports
 * @tx_errored_frms: Count of data frames transmitted on this Aggregator that
 *             experience transmission errors on its Aggregation ports.
 * @rx_frms: Count of data frames received on this Aggregator on all its
 *             Aggregation ports. Does not include LACPDUs or Marker PDUs.
 *             Also, does not include frames discarded by the Collection
 *             function.
 * @rx_data_octets: Count of data and padding octets of frames received on this
 *             Aggregator on all its Aggregation ports. Does not include octets
 *             of LACPDUs or Marker PDUs. Also, does not include
 *             octets of frames
 *             discarded by the Collection function.
 * @rx_mcast_frms: Count of data frames received (from a group destination
 *             address other than the broadcast address) on this Aggregator on
 *             all its Aggregation ports. Does not include LACPDUs or Marker
 *             PDUs. Also, does not include frames discarded by the Collection
 *             function.
 * @rx_bcast_frms: Count of broadcast data frames received on this Aggregator on
 *             all its Aggregation ports. Does not include LACPDUs or Marker
 *             PDUs. Also, does not include frames discarded by the Collection
 *             function.
 * @rx_discarded_frms: Count of data frames received on this Aggregator that are
 *             discarded by the Collection function because the Collection
 *             function was disabled on the port which the frames are received.
 * @rx_errored_frms: Count of data frames received on this Aggregator that are
 *             discarded by its Aggregation ports, or are discarded by the
 *             Collection function of the Aggregator, or that are discarded by
 *             the Aggregator due to detection of an illegal Slow Protocols PDU.
 * @rx_unknown_slow_proto_frms: Count of data frames received on this Aggregator
 *             that are discarded by its Aggregation ports due to detection of
 *             an unknown Slow Protocols PDU.
 *
 * Per aggregator XMAC RX statistics.
 */
struct vxge_hw_xmac_aggr_stats {
/*0x000*/		u64	tx_frms;
/*0x008*/		u64	tx_data_octets;
/*0x010*/		u64	tx_mcast_frms;
/*0x018*/		u64	tx_bcast_frms;
/*0x020*/		u64	tx_discarded_frms;
/*0x028*/		u64	tx_errored_frms;
/*0x030*/		u64	rx_frms;
/*0x038*/		u64	rx_data_octets;
/*0x040*/		u64	rx_mcast_frms;
/*0x048*/		u64	rx_bcast_frms;
/*0x050*/		u64	rx_discarded_frms;
/*0x058*/		u64	rx_errored_frms;
/*0x060*/		u64	rx_unknown_slow_proto_frms;
} __packed;

/**
 * struct vxge_hw_xmac_port_stats - XMAC Port Statistics
 *
 * @tx_ttl_frms: Count of successfully transmitted MAC frames
 * @tx_ttl_octets: Count of total octets of transmitted frames, not including
 *            framing characters (i.e. less framing bits). To determine the
 *            total octets of transmitted frames, including framing characters,
 *            multiply PORTn_TX_TTL_FRMS by 8 and add it to this stat (unless
 *            otherwise configured, this stat only counts frames that have
 *            8 bytes of preamble for each frame). This stat can be configured
 *            (see XMAC_STATS_GLOBAL_CFG.TTL_FRMS_HANDLING) to count everything
 *            including the preamble octets.
 * @tx_data_octets: Count of data and padding octets of successfully transmitted
 *            frames.
 * @tx_mcast_frms: Count of successfully transmitted frames to a group address
 *            other than the broadcast address.
 * @tx_bcast_frms: Count of successfully transmitted frames to the broadcast
 *            group address.
 * @tx_ucast_frms: Count of transmitted frames containing a unicast address.
 *            Includes discarded frames that are not sent to the network.
 * @tx_tagged_frms: Count of transmitted frames containing a VLAN tag.
 * @tx_vld_ip: Count of transmitted IP datagrams that are passed to the network.
 * @tx_vld_ip_octets: Count of total octets of transmitted IP datagrams that
 *            are passed to the network.
 * @tx_icmp: Count of transmitted ICMP messages. Includes messages not sent
 *            due to problems within ICMP.
 * @tx_tcp: Count of transmitted TCP segments. Does not include segments
 *            containing retransmitted octets.
 * @tx_rst_tcp: Count of transmitted TCP segments containing the RST flag.
 * @tx_udp: Count of transmitted UDP datagrams.
 * @tx_parse_error: Increments when the TPA is unable to parse a packet. This
 *            generally occurs when a packet is corrupt somehow, including
 *            packets that have IP version mismatches, invalid Layer 2 control
 *            fields, etc. L3/L4 checksums are not offloaded, but the packet
 *            is still be transmitted.
 * @tx_unknown_protocol: Increments when the TPA encounters an unknown
 *            protocol, such as a new IPv6 extension header, or an unsupported
 *            Routing Type. The packet still has a checksum calculated but it
 *            may be incorrect.
 * @tx_pause_ctrl_frms: Count of MAC PAUSE control frames that are transmitted.
 *            Since, the only control frames supported by this device are
 *            PAUSE frames, this register is a count of all transmitted MAC
 *            control frames.
 * @tx_marker_pdu_frms: Count of Marker PDUs transmitted
 * on this Aggregation port.
 * @tx_lacpdu_frms: Count of LACPDUs transmitted on this Aggregation port.
 * @tx_drop_ip: Count of transmitted IP datagrams that could not be passed to
 *            the network. Increments because of:
 *            1) An internal processing error
 *            (such as an uncorrectable ECC error). 2) A frame parsing error
 *            during IP checksum calculation.
 * @tx_marker_resp_pdu_frms: Count of Marker Response PDUs transmitted on this
 *            Aggregation port.
 * @tx_xgmii_char2_match: Maintains a count of the number of transmitted XGMII
 *            characters that match a pattern that is programmable through
 *            register XMAC_STATS_TX_XGMII_CHAR_PORTn. By default, the pattern
 *            is set to /T/ (i.e. the terminate character), thus the statistic
 *            tracks the number of transmitted Terminate characters.
 * @tx_xgmii_char1_match: Maintains a count of the number of transmitted XGMII
 *            characters that match a pattern that is programmable through
 *            register XMAC_STATS_TX_XGMII_CHAR_PORTn. By default, the pattern
 *            is set to /S/ (i.e. the start character),
 *            thus the statistic tracks
 *            the number of transmitted Start characters.
 * @tx_xgmii_column2_match: Maintains a count of the number of transmitted XGMII
 *            columns that match a pattern that is programmable through register
 *            XMAC_STATS_TX_XGMII_COLUMN2_PORTn. By default, the pattern is set
 *            to 4 x /E/ (i.e. a column containing all error characters), thus
 *            the statistic tracks the number of Error columns transmitted at
 *            any time. If XMAC_STATS_TX_XGMII_BEHAV_COLUMN2_PORTn.NEAR_COL1 is
 *            set to 1, then this stat increments when COLUMN2 is found within
 *            'n' clocks after COLUMN1. Here, 'n' is defined by
 *            XMAC_STATS_TX_XGMII_BEHAV_COLUMN2_PORTn.NUM_COL (if 'n' is set
 *            to 0, then it means to search anywhere for COLUMN2).
 * @tx_xgmii_column1_match: Maintains a count of the number of transmitted XGMII
 *            columns that match a pattern that is programmable through register
 *            XMAC_STATS_TX_XGMII_COLUMN1_PORTn. By default, the pattern is set
 *            to 4 x /I/ (i.e. a column containing all idle characters),
 *            thus the statistic tracks the number of transmitted Idle columns.
 * @tx_any_err_frms: Count of transmitted frames containing any error that
 *            prevents them from being passed to the network. Increments if
 *            there is an ECC while reading the frame out of the transmit
 *            buffer. Also increments if the transmit protocol assist (TPA)
 *            block determines that the frame should not be sent.
 * @tx_drop_frms: Count of frames that could not be sent for no other reason
 *            than internal MAC processing. Increments once whenever the
 *            transmit buffer is flushed (due to an ECC error on a memory
 *            descriptor).
 * @rx_ttl_frms: Count of total received MAC frames, including frames received
 *            with frame-too-long, FCS, or length errors. This stat can be
 *            configured (see XMAC_STATS_GLOBAL_CFG.TTL_FRMS_HANDLING) to count
 *            everything, even "frames" as small one byte of preamble.
 * @rx_vld_frms: Count of successfully received MAC frames. Does not include
 *            frames received with frame-too-long, FCS, or length errors.
 * @rx_offload_frms: Count of offloaded received frames that are passed to
 *            the host.
 * @rx_ttl_octets: Count of total octets of received frames, not including
 *            framing characters (i.e. less framing bits). To determine the
 *            total octets of received frames, including framing characters,
 *            multiply PORTn_RX_TTL_FRMS by 8 and add it to this stat (unless
 *            otherwise configured, this stat only counts frames that have 8
 *            bytes of preamble for each frame). This stat can be configured
 *            (see XMAC_STATS_GLOBAL_CFG.TTL_FRMS_HANDLING) to count everything,
 *            even the preamble octets of "frames" as small one byte of preamble
 * @rx_data_octets: Count of data and padding octets of successfully received
 *            frames. Does not include frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_offload_octets: Count of total octets, not including framing
 *            characters, of offloaded received frames that are passed
 *            to the host.
 * @rx_vld_mcast_frms: Count of successfully received MAC frames containing a
 *	      nonbroadcast group address. Does not include frames received
 *            with frame-too-long, FCS, or length errors.
 * @rx_vld_bcast_frms: Count of successfully received MAC frames containing
 *            the broadcast group address. Does not include frames received
 *            with frame-too-long, FCS, or length errors.
 * @rx_accepted_ucast_frms: Count of successfully received frames containing
 *            a unicast address. Only includes frames that are passed to
 *            the system.
 * @rx_accepted_nucast_frms: Count of successfully received frames containing
 *            a non-unicast (broadcast or multicast) address. Only includes
 *            frames that are passed to the system. Could include, for instance,
 *            non-unicast frames that contain FCS errors if the MAC_ERROR_CFG
 *            register is set to pass FCS-errored frames to the host.
 * @rx_tagged_frms: Count of received frames containing a VLAN tag.
 * @rx_long_frms: Count of received frames that are longer than RX_MAX_PYLD_LEN
 *            + 18 bytes (+ 22 bytes if VLAN-tagged).
 * @rx_usized_frms: Count of received frames of length (including FCS, but not
 *            framing bits) less than 64 octets, that are otherwise well-formed.
 *            In other words, counts runts.
 * @rx_osized_frms: Count of received frames of length (including FCS, but not
 *            framing bits) more than 1518 octets, that are otherwise
 *            well-formed. Note: If register XMAC_STATS_GLOBAL_CFG.VLAN_HANDLING
 *            is set to 1, then "more than 1518 octets" becomes "more than 1518
 *            (1522 if VLAN-tagged) octets".
 * @rx_frag_frms: Count of received frames of length (including FCS, but not
 *            framing bits) less than 64 octets that had bad FCS. In other
 *            words, counts fragments.
 * @rx_jabber_frms: Count of received frames of length (including FCS, but not
 *            framing bits) more than 1518 octets that had bad FCS. In other
 *            words, counts jabbers. Note: If register
 *            XMAC_STATS_GLOBAL_CFG.VLAN_HANDLING is set to 1, then "more than
 *            1518 octets" becomes "more than 1518 (1522 if VLAN-tagged)
 *            octets".
 * @rx_ttl_64_frms: Count of total received MAC frames with length (including
 *            FCS, but not framing bits) of exactly 64 octets. Includes frames
 *            received with frame-too-long, FCS, or length errors.
 * @rx_ttl_65_127_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 65 and 127
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_128_255_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 128 and 255
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_256_511_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 256 and 511
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_512_1023_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 512 and 1023
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_1024_1518_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 1024 and 1518
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_1519_4095_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 1519 and 4095
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_4096_8191_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 4096 and 8191
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_8192_max_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 8192 and
 *            RX_MAX_PYLD_LEN+18 octets inclusive. Includes frames received
 *            with frame-too-long, FCS, or length errors.
 * @rx_ttl_gt_max_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) exceeding
 *            RX_MAX_PYLD_LEN+18 (+22 bytes if VLAN-tagged) octets inclusive.
 *            Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ip: Count of received IP datagrams. Includes errored IP datagrams.
 * @rx_accepted_ip: Count of received IP datagrams that
 * 		are passed to the system.
 * @rx_ip_octets: Count of number of octets in received IP datagrams. Includes
 *            errored IP datagrams.
 * @rx_err_ip: 	Count of received IP datagrams containing errors. For example,
 *            bad IP checksum.
 * @rx_icmp: Count of received ICMP messages. Includes errored ICMP messages.
 * @rx_tcp: Count of received TCP segments. Includes errored TCP segments.
 *            Note: This stat contains a count of all received TCP segments,
 *            regardless of whether or not they pertain to an established
 *            connection.
 * @rx_udp: Count of received UDP datagrams.
 * @rx_err_tcp: Count of received TCP segments containing errors. For example,
 *            bad TCP checksum.
 * @rx_pause_count: Count of number of pause quanta that the MAC has been in
 *            the paused state. Recall, one pause quantum equates to 512
 *            bit times.
 * @rx_pause_ctrl_frms: Count of received MAC PAUSE control frames.
 * @rx_unsup_ctrl_frms: Count of received MAC control frames that do not
 *            contain the PAUSE opcode. The sum of RX_PAUSE_CTRL_FRMS and
 *            this register is a count of all received MAC control frames.
 *            Note: This stat may be configured to count all layer 2 errors
 *            (i.e. length errors and FCS errors).
 * @rx_fcs_err_frms: Count of received MAC frames that do not pass FCS. Does
 *            not include frames received with frame-too-long or
 *            frame-too-short error.
 * @rx_in_rng_len_err_frms: Count of received frames with a length/type field
 *            value between 46 (42 for VLAN-tagged frames) and 1500 (also 1500
 *            for VLAN-tagged frames), inclusive, that does not match the
 *            number of data octets (including pad) received. Also contains
 *            a count of received frames with a length/type field less than
 *            46 (42 for VLAN-tagged frames) and the number of data octets
 *            (including pad) received is greater than 46 (42 for VLAN-tagged
 *            frames).
 * @rx_out_rng_len_err_frms:  Count of received frames with length/type field
 *            between 1501 and 1535 decimal, inclusive.
 * @rx_drop_frms: Count of received frames that could not be passed to the host.
 *            See PORTn_RX_L2_MGMT_DISCARD, PORTn_RX_RPA_DISCARD,
 *            PORTn_RX_TRASH_DISCARD, PORTn_RX_RTS_DISCARD, PORTn_RX_RED_DISCARD
 *            for a list of reasons. Because the RMAC drops one frame at a time,
 *            this stat also indicates the number of drop events.
 * @rx_discarded_frms: Count of received frames containing
 * 		any error that prevents
 *            them from being passed to the system. See PORTn_RX_FCS_DISCARD,
 *            PORTn_RX_LEN_DISCARD, and PORTn_RX_SWITCH_DISCARD for a list of
 *            reasons.
 * @rx_drop_ip: Count of received IP datagrams that could not be passed to the
 *            host. See PORTn_RX_DROP_FRMS for a list of reasons.
 * @rx_drop_udp: Count of received UDP datagrams that are not delivered to the
 *            host. See PORTn_RX_DROP_FRMS for a list of reasons.
 * @rx_marker_pdu_frms: Count of valid Marker PDUs received on this Aggregation
 *            port.
 * @rx_lacpdu_frms: Count of valid LACPDUs received on this Aggregation port.
 * @rx_unknown_pdu_frms: Count of received frames (on this Aggregation port)
 *            that carry the Slow Protocols EtherType, but contain an unknown
 *            PDU. Or frames that contain the Slow Protocols group MAC address,
 *            but do not carry the Slow Protocols EtherType.
 * @rx_marker_resp_pdu_frms: Count of valid Marker Response PDUs received on
 *            this Aggregation port.
 * @rx_fcs_discard: Count of received frames that are discarded because the
 *            FCS check failed.
 * @rx_illegal_pdu_frms: Count of received frames (on this Aggregation port)
 *            that carry the Slow Protocols EtherType, but contain a badly
 *            formed PDU. Or frames that carry the Slow Protocols EtherType,
 *            but contain an illegal value of Protocol Subtype.
 * @rx_switch_discard: Count of received frames that are discarded by the
 *            internal switch because they did not have an entry in the
 *            Filtering Database. This includes frames that had an invalid
 *            destination MAC address or VLAN ID. It also includes frames are
 *            discarded because they did not satisfy the length requirements
 *            of the target VPATH.
 * @rx_len_discard: Count of received frames that are discarded because of an
 *            invalid frame length (includes fragments, oversized frames and
 *            mismatch between frame length and length/type field). This stat
 *            can be configured
 *            (see XMAC_STATS_GLOBAL_CFG.LEN_DISCARD_HANDLING).
 * @rx_rpa_discard: Count of received frames that were discarded because the
 *            receive protocol assist (RPA) discovered and error in the frame
 *            or was unable to parse the frame.
 * @rx_l2_mgmt_discard: Count of Layer 2 management frames (eg. pause frames,
 *            Link Aggregation Control Protocol (LACP) frames, etc.) that are
 *            discarded.
 * @rx_rts_discard: Count of received frames that are discarded by the receive
 *            traffic steering (RTS) logic. Includes those frame discarded
 *            because the SSC response contradicted the switch table, because
 *            the SSC timed out, or because the target queue could not fit the
 *            frame.
 * @rx_trash_discard: Count of received frames that are discarded because
 *            receive traffic steering (RTS) steered the frame to the trash
 *            queue.
 * @rx_buff_full_discard: Count of received frames that are discarded because
 *            internal buffers are full. Includes frames discarded because the
 *            RTS logic is waiting for an SSC lookup that has no timeout bound.
 *            Also, includes frames that are dropped because the MAC2FAU buffer
 *            is nearly full -- this can happen if the external receive buffer
 *            is full and the receive path is backing up.
 * @rx_red_discard: Count of received frames that are discarded because of RED
 *            (Random Early Discard).
 * @rx_xgmii_ctrl_err_cnt: Maintains a count of unexpected or misplaced control
 *            characters occurring between times of normal data transmission
 *            (i.e. not included in RX_XGMII_DATA_ERR_CNT). This counter is
 *            incremented when either -
 *            1) The Reconciliation Sublayer (RS) is expecting one control
 *               character and gets another (i.e. is expecting a Start
 *               character, but gets another control character).
 *            2) Start control character is not in lane 0
 *            Only increments the count by one for each XGMII column.
 * @rx_xgmii_data_err_cnt: Maintains a count of unexpected control characters
 *            during normal data transmission. If the Reconciliation Sublayer
 *            (RS) receives a control character, other than a terminate control
 *            character, during receipt of data octets then this register is
 *            incremented. Also increments if the start frame delimiter is not
 *            found in the correct location. Only increments the count by one
 *            for each XGMII column.
 * @rx_xgmii_char1_match: Maintains a count of the number of XGMII characters
 *            that match a pattern that is programmable through register
 *            XMAC_STATS_RX_XGMII_CHAR_PORTn. By default, the pattern is set
 *            to /E/ (i.e. the error character), thus the statistic tracks the
 *            number of Error characters received at any time.
 * @rx_xgmii_err_sym: Count of the number of symbol errors in the received
 *            XGMII data (i.e. PHY indicates "Receive Error" on the XGMII).
 *            Only includes symbol errors that are observed between the XGMII
 *            Start Frame Delimiter and End Frame Delimiter, inclusive. And
 *            only increments the count by one for each frame.
 * @rx_xgmii_column1_match: Maintains a count of the number of XGMII columns
 *            that match a pattern that is programmable through register
 *            XMAC_STATS_RX_XGMII_COLUMN1_PORTn. By default, the pattern is set
 *            to 4 x /E/ (i.e. a column containing all error characters), thus
 *            the statistic tracks the number of Error columns received at any
 *            time.
 * @rx_xgmii_char2_match: Maintains a count of the number of XGMII characters
 *            that match a pattern that is programmable through register
 *            XMAC_STATS_RX_XGMII_CHAR_PORTn. By default, the pattern is set
 *            to /E/ (i.e. the error character), thus the statistic tracks the
 *            number of Error characters received at any time.
 * @rx_local_fault: Maintains a count of the number of times that link
 *            transitioned from "up" to "down" due to a local fault.
 * @rx_xgmii_column2_match: Maintains a count of the number of XGMII columns
 *            that match a pattern that is programmable through register
 *            XMAC_STATS_RX_XGMII_COLUMN2_PORTn. By default, the pattern is set
 *            to 4 x /E/ (i.e. a column containing all error characters), thus
 *            the statistic tracks the number of Error columns received at any
 *            time. If XMAC_STATS_RX_XGMII_BEHAV_COLUMN2_PORTn.NEAR_COL1 is set
 *            to 1, then this stat increments when COLUMN2 is found within 'n'
 *            clocks after COLUMN1. Here, 'n' is defined by
 *            XMAC_STATS_RX_XGMII_BEHAV_COLUMN2_PORTn.NUM_COL (if 'n' is set to
 *            0, then it means to search anywhere for COLUMN2).
 * @rx_jettison: Count of received frames that are jettisoned because internal
 *            buffers are full.
 * @rx_remote_fault: Maintains a count of the number of times that link
 *            transitioned from "up" to "down" due to a remote fault.
 *
 * XMAC Port Statistics.
 */
struct vxge_hw_xmac_port_stats {
/*0x000*/		u64	tx_ttl_frms;
/*0x008*/		u64	tx_ttl_octets;
/*0x010*/		u64	tx_data_octets;
/*0x018*/		u64	tx_mcast_frms;
/*0x020*/		u64	tx_bcast_frms;
/*0x028*/		u64	tx_ucast_frms;
/*0x030*/		u64	tx_tagged_frms;
/*0x038*/		u64	tx_vld_ip;
/*0x040*/		u64	tx_vld_ip_octets;
/*0x048*/		u64	tx_icmp;
/*0x050*/		u64	tx_tcp;
/*0x058*/		u64	tx_rst_tcp;
/*0x060*/		u64	tx_udp;
/*0x068*/		u32	tx_parse_error;
/*0x06c*/		u32	tx_unknown_protocol;
/*0x070*/		u64	tx_pause_ctrl_frms;
/*0x078*/		u32	tx_marker_pdu_frms;
/*0x07c*/		u32	tx_lacpdu_frms;
/*0x080*/		u32	tx_drop_ip;
/*0x084*/		u32	tx_marker_resp_pdu_frms;
/*0x088*/		u32	tx_xgmii_char2_match;
/*0x08c*/		u32	tx_xgmii_char1_match;
/*0x090*/		u32	tx_xgmii_column2_match;
/*0x094*/		u32	tx_xgmii_column1_match;
/*0x098*/		u32	unused1;
/*0x09c*/		u16	tx_any_err_frms;
/*0x09e*/		u16	tx_drop_frms;
/*0x0a0*/		u64	rx_ttl_frms;
/*0x0a8*/		u64	rx_vld_frms;
/*0x0b0*/		u64	rx_offload_frms;
/*0x0b8*/		u64	rx_ttl_octets;
/*0x0c0*/		u64	rx_data_octets;
/*0x0c8*/		u64	rx_offload_octets;
/*0x0d0*/		u64	rx_vld_mcast_frms;
/*0x0d8*/		u64	rx_vld_bcast_frms;
/*0x0e0*/		u64	rx_accepted_ucast_frms;
/*0x0e8*/		u64	rx_accepted_nucast_frms;
/*0x0f0*/		u64	rx_tagged_frms;
/*0x0f8*/		u64	rx_long_frms;
/*0x100*/		u64	rx_usized_frms;
/*0x108*/		u64	rx_osized_frms;
/*0x110*/		u64	rx_frag_frms;
/*0x118*/		u64	rx_jabber_frms;
/*0x120*/		u64	rx_ttl_64_frms;
/*0x128*/		u64	rx_ttl_65_127_frms;
/*0x130*/		u64	rx_ttl_128_255_frms;
/*0x138*/		u64	rx_ttl_256_511_frms;
/*0x140*/		u64	rx_ttl_512_1023_frms;
/*0x148*/		u64	rx_ttl_1024_1518_frms;
/*0x150*/		u64	rx_ttl_1519_4095_frms;
/*0x158*/		u64	rx_ttl_4096_8191_frms;
/*0x160*/		u64	rx_ttl_8192_max_frms;
/*0x168*/		u64	rx_ttl_gt_max_frms;
/*0x170*/		u64	rx_ip;
/*0x178*/		u64	rx_accepted_ip;
/*0x180*/		u64	rx_ip_octets;
/*0x188*/		u64	rx_err_ip;
/*0x190*/		u64	rx_icmp;
/*0x198*/		u64	rx_tcp;
/*0x1a0*/		u64	rx_udp;
/*0x1a8*/		u64	rx_err_tcp;
/*0x1b0*/		u64	rx_pause_count;
/*0x1b8*/		u64	rx_pause_ctrl_frms;
/*0x1c0*/		u64	rx_unsup_ctrl_frms;
/*0x1c8*/		u64	rx_fcs_err_frms;
/*0x1d0*/		u64	rx_in_rng_len_err_frms;
/*0x1d8*/		u64	rx_out_rng_len_err_frms;
/*0x1e0*/		u64	rx_drop_frms;
/*0x1e8*/		u64	rx_discarded_frms;
/*0x1f0*/		u64	rx_drop_ip;
/*0x1f8*/		u64	rx_drop_udp;
/*0x200*/		u32	rx_marker_pdu_frms;
/*0x204*/		u32	rx_lacpdu_frms;
/*0x208*/		u32	rx_unknown_pdu_frms;
/*0x20c*/		u32	rx_marker_resp_pdu_frms;
/*0x210*/		u32	rx_fcs_discard;
/*0x214*/		u32	rx_illegal_pdu_frms;
/*0x218*/		u32	rx_switch_discard;
/*0x21c*/		u32	rx_len_discard;
/*0x220*/		u32	rx_rpa_discard;
/*0x224*/		u32	rx_l2_mgmt_discard;
/*0x228*/		u32	rx_rts_discard;
/*0x22c*/		u32	rx_trash_discard;
/*0x230*/		u32	rx_buff_full_discard;
/*0x234*/		u32	rx_red_discard;
/*0x238*/		u32	rx_xgmii_ctrl_err_cnt;
/*0x23c*/		u32	rx_xgmii_data_err_cnt;
/*0x240*/		u32	rx_xgmii_char1_match;
/*0x244*/		u32	rx_xgmii_err_sym;
/*0x248*/		u32	rx_xgmii_column1_match;
/*0x24c*/		u32	rx_xgmii_char2_match;
/*0x250*/		u32	rx_local_fault;
/*0x254*/		u32	rx_xgmii_column2_match;
/*0x258*/		u32	rx_jettison;
/*0x25c*/		u32	rx_remote_fault;
} __packed;

/**
 * struct vxge_hw_xmac_vpath_tx_stats - XMAC Vpath Tx Statistics
 *
 * @tx_ttl_eth_frms: Count of successfully transmitted MAC frames.
 * @tx_ttl_eth_octets: Count of total octets of transmitted frames,
 *             not including framing characters (i.e. less framing bits).
 *             To determine the total octets of transmitted frames, including
 *             framing characters, multiply TX_TTL_ETH_FRMS by 8 and add it to
 *             this stat (the device always prepends 8 bytes of preamble for
 *             each frame)
 * @tx_data_octets: Count of data and padding octets of successfully transmitted
 *             frames.
 * @tx_mcast_frms: Count of successfully transmitted frames to a group address
 *             other than the broadcast address.
 * @tx_bcast_frms: Count of successfully transmitted frames to the broadcast
 *             group address.
 * @tx_ucast_frms: Count of transmitted frames containing a unicast address.
 *             Includes discarded frames that are not sent to the network.
 * @tx_tagged_frms: Count of transmitted frames containing a VLAN tag.
 * @tx_vld_ip: Count of transmitted IP datagrams that are passed to the network.
 * @tx_vld_ip_octets: Count of total octets of transmitted IP datagrams that
 *            are passed to the network.
 * @tx_icmp: Count of transmitted ICMP messages. Includes messages not sent due
 *            to problems within ICMP.
 * @tx_tcp: Count of transmitted TCP segments. Does not include segments
 *            containing retransmitted octets.
 * @tx_rst_tcp: Count of transmitted TCP segments containing the RST flag.
 * @tx_udp: Count of transmitted UDP datagrams.
 * @tx_unknown_protocol: Increments when the TPA encounters an unknown protocol,
 *            such as a new IPv6 extension header, or an unsupported Routing
 *            Type. The packet still has a checksum calculated but it may be
 *            incorrect.
 * @tx_lost_ip: Count of transmitted IP datagrams that could not be passed
 *            to the network. Increments because of: 1) An internal processing
 *            error (such as an uncorrectable ECC error). 2) A frame parsing
 *            error during IP checksum calculation.
 * @tx_parse_error: Increments when the TPA is unable to parse a packet. This
 *            generally occurs when a packet is corrupt somehow, including
 *            packets that have IP version mismatches, invalid Layer 2 control
 *            fields, etc. L3/L4 checksums are not offloaded, but the packet
 *            is still be transmitted.
 * @tx_tcp_offload: For frames belonging to offloaded sessions only, a count
 *            of transmitted TCP segments. Does not include segments containing
 *            retransmitted octets.
 * @tx_retx_tcp_offload: For frames belonging to offloaded sessions only, the
 *            total number of segments retransmitted. Retransmitted segments
 *            that are sourced by the host are counted by the host.
 * @tx_lost_ip_offload: For frames belonging to offloaded sessions only, a count
 *            of transmitted IP datagrams that could not be passed to the
 *            network.
 *
 * XMAC Vpath TX Statistics.
 */
struct vxge_hw_xmac_vpath_tx_stats {
	u64	tx_ttl_eth_frms;
	u64	tx_ttl_eth_octets;
	u64	tx_data_octets;
	u64	tx_mcast_frms;
	u64	tx_bcast_frms;
	u64	tx_ucast_frms;
	u64	tx_tagged_frms;
	u64	tx_vld_ip;
	u64	tx_vld_ip_octets;
	u64	tx_icmp;
	u64	tx_tcp;
	u64	tx_rst_tcp;
	u64	tx_udp;
	u32	tx_unknown_protocol;
	u32	tx_lost_ip;
	u32	unused1;
	u32	tx_parse_error;
	u64	tx_tcp_offload;
	u64	tx_retx_tcp_offload;
	u64	tx_lost_ip_offload;
} __packed;

/**
 * struct vxge_hw_xmac_vpath_rx_stats - XMAC Vpath RX Statistics
 *
 * @rx_ttl_eth_frms: Count of successfully received MAC frames.
 * @rx_vld_frms: Count of successfully received MAC frames. Does not include
 *            frames received with frame-too-long, FCS, or length errors.
 * @rx_offload_frms: Count of offloaded received frames that are passed to
 *            the host.
 * @rx_ttl_eth_octets: Count of total octets of received frames, not including
 *            framing characters (i.e. less framing bits). Only counts octets
 *            of frames that are at least 14 bytes (18 bytes for VLAN-tagged)
 *            before FCS. To determine the total octets of received frames,
 *            including framing characters, multiply RX_TTL_ETH_FRMS by 8 and
 *            add it to this stat (the stat RX_TTL_ETH_FRMS only counts frames
 *            that have the required 8 bytes of preamble).
 * @rx_data_octets: Count of data and padding octets of successfully received
 *            frames. Does not include frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_offload_octets: Count of total octets, not including framing characters,
 *            of offloaded received frames that are passed to the host.
 * @rx_vld_mcast_frms: Count of successfully received MAC frames containing a
 *            nonbroadcast group address. Does not include frames received with
 *            frame-too-long, FCS, or length errors.
 * @rx_vld_bcast_frms: Count of successfully received MAC frames containing the
 *            broadcast group address. Does not include frames received with
 *            frame-too-long, FCS, or length errors.
 * @rx_accepted_ucast_frms: Count of successfully received frames containing
 *            a unicast address. Only includes frames that are passed to the
 *            system.
 * @rx_accepted_nucast_frms: Count of successfully received frames containing
 *            a non-unicast (broadcast or multicast) address. Only includes
 *            frames that are passed to the system. Could include, for instance,
 *            non-unicast frames that contain FCS errors if the MAC_ERROR_CFG
 *            register is set to pass FCS-errored frames to the host.
 * @rx_tagged_frms: Count of received frames containing a VLAN tag.
 * @rx_long_frms: Count of received frames that are longer than RX_MAX_PYLD_LEN
 *            + 18 bytes (+ 22 bytes if VLAN-tagged).
 * @rx_usized_frms: Count of received frames of length (including FCS, but not
 *            framing bits) less than 64 octets, that are otherwise well-formed.
 *            In other words, counts runts.
 * @rx_osized_frms: Count of received frames of length (including FCS, but not
 *            framing bits) more than 1518 octets, that are otherwise
 *            well-formed.
 * @rx_frag_frms: Count of received frames of length (including FCS, but not
 *            framing bits) less than 64 octets that had bad FCS.
 *            In other words, counts fragments.
 * @rx_jabber_frms: Count of received frames of length (including FCS, but not
 *            framing bits) more than 1518 octets that had bad FCS. In other
 *            words, counts jabbers.
 * @rx_ttl_64_frms: Count of total received MAC frames with length (including
 *            FCS, but not framing bits) of exactly 64 octets. Includes frames
 *            received with frame-too-long, FCS, or length errors.
 * @rx_ttl_65_127_frms: Count of total received MAC frames
 * 		with length (including
 *            FCS, but not framing bits) of between 65 and 127 octets inclusive.
 *            Includes frames received with frame-too-long, FCS,
 *            or length errors.
 * @rx_ttl_128_255_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits)
 *            of between 128 and 255 octets
 *            inclusive. Includes frames received with frame-too-long, FCS,
 *            or length errors.
 * @rx_ttl_256_511_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits)
 *            of between 256 and 511 octets
 *            inclusive. Includes frames received with frame-too-long, FCS, or
 *            length errors.
 * @rx_ttl_512_1023_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 512 and 1023
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_1024_1518_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 1024 and 1518
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_1519_4095_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 1519 and 4095
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_4096_8191_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 4096 and 8191
 *            octets inclusive. Includes frames received with frame-too-long,
 *            FCS, or length errors.
 * @rx_ttl_8192_max_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) of between 8192 and
 *            RX_MAX_PYLD_LEN+18 octets inclusive. Includes frames received
 *            with frame-too-long, FCS, or length errors.
 * @rx_ttl_gt_max_frms: Count of total received MAC frames with length
 *            (including FCS, but not framing bits) exceeding RX_MAX_PYLD_LEN+18
 *            (+22 bytes if VLAN-tagged) octets inclusive. Includes frames
 *            received with frame-too-long, FCS, or length errors.
 * @rx_ip: Count of received IP datagrams. Includes errored IP datagrams.
 * @rx_accepted_ip: Count of received IP datagrams that
 * 		are passed to the system.
 * @rx_ip_octets: Count of number of octets in received IP datagrams.
 *            Includes errored IP datagrams.
 * @rx_err_ip: Count of received IP datagrams containing errors. For example,
 *            bad IP checksum.
 * @rx_icmp: Count of received ICMP messages. Includes errored ICMP messages.
 * @rx_tcp: Count of received TCP segments. Includes errored TCP segments.
 *             Note: This stat contains a count of all received TCP segments,
 *             regardless of whether or not they pertain to an established
 *             connection.
 * @rx_udp: Count of received UDP datagrams.
 * @rx_err_tcp: Count of received TCP segments containing errors. For example,
 *             bad TCP checksum.
 * @rx_lost_frms: Count of received frames that could not be passed to the host.
 *             See RX_QUEUE_FULL_DISCARD and RX_RED_DISCARD
 *             for a list of reasons.
 * @rx_lost_ip: Count of received IP datagrams that could not be passed to
 *             the host. See RX_LOST_FRMS for a list of reasons.
 * @rx_lost_ip_offload: For frames belonging to offloaded sessions only, a count
 *             of received IP datagrams that could not be passed to the host.
 *             See RX_LOST_FRMS for a list of reasons.
 * @rx_various_discard: Count of received frames that are discarded because
 *             the target receive queue is full.
 * @rx_sleep_discard: Count of received frames that are discarded because the
 *            target VPATH is asleep (a Wake-on-LAN magic packet can be used
 *            to awaken the VPATH).
 * @rx_red_discard: Count of received frames that are discarded because of RED
 *            (Random Early Discard).
 * @rx_queue_full_discard: Count of received frames that are discarded because
 *             the target receive queue is full.
 * @rx_mpa_ok_frms: Count of received frames that pass the MPA checks.
 *
 * XMAC Vpath RX Statistics.
 */
struct vxge_hw_xmac_vpath_rx_stats {
	u64	rx_ttl_eth_frms;
	u64	rx_vld_frms;
	u64	rx_offload_frms;
	u64	rx_ttl_eth_octets;
	u64	rx_data_octets;
	u64	rx_offload_octets;
	u64	rx_vld_mcast_frms;
	u64	rx_vld_bcast_frms;
	u64	rx_accepted_ucast_frms;
	u64	rx_accepted_nucast_frms;
	u64	rx_tagged_frms;
	u64	rx_long_frms;
	u64	rx_usized_frms;
	u64	rx_osized_frms;
	u64	rx_frag_frms;
	u64	rx_jabber_frms;
	u64	rx_ttl_64_frms;
	u64	rx_ttl_65_127_frms;
	u64	rx_ttl_128_255_frms;
	u64	rx_ttl_256_511_frms;
	u64	rx_ttl_512_1023_frms;
	u64	rx_ttl_1024_1518_frms;
	u64	rx_ttl_1519_4095_frms;
	u64	rx_ttl_4096_8191_frms;
	u64	rx_ttl_8192_max_frms;
	u64	rx_ttl_gt_max_frms;
	u64	rx_ip;
	u64	rx_accepted_ip;
	u64	rx_ip_octets;
	u64	rx_err_ip;
	u64	rx_icmp;
	u64	rx_tcp;
	u64	rx_udp;
	u64	rx_err_tcp;
	u64	rx_lost_frms;
	u64	rx_lost_ip;
	u64	rx_lost_ip_offload;
	u16	rx_various_discard;
	u16	rx_sleep_discard;
	u16	rx_red_discard;
	u16	rx_queue_full_discard;
	u64	rx_mpa_ok_frms;
} __packed;

/**
 * struct vxge_hw_xmac_stats - XMAC Statistics
 *
 * @aggr_stats: Statistics on aggregate port(port 0, port 1)
 * @port_stats: Staticstics on ports(wire 0, wire 1, lag)
 * @vpath_tx_stats: Per vpath XMAC TX stats
 * @vpath_rx_stats: Per vpath XMAC RX stats
 *
 * XMAC Statistics.
 */
struct vxge_hw_xmac_stats {
	struct vxge_hw_xmac_aggr_stats
				aggr_stats[VXGE_HW_MAC_MAX_MAC_PORT_ID];
	struct vxge_hw_xmac_port_stats
				port_stats[VXGE_HW_MAC_MAX_MAC_PORT_ID+1];
	struct vxge_hw_xmac_vpath_tx_stats
				vpath_tx_stats[VXGE_HW_MAX_VIRTUAL_PATHS];
	struct vxge_hw_xmac_vpath_rx_stats
				vpath_rx_stats[VXGE_HW_MAX_VIRTUAL_PATHS];
};

/**
 * struct vxge_hw_vpath_stats_hw_info - Titan vpath hardware statistics.
 * @ini_num_mwr_sent: The number of PCI memory writes initiated by the PIC block
 *             for the given VPATH
 * @ini_num_mrd_sent: The number of PCI memory reads initiated by the PIC block
 * @ini_num_cpl_rcvd: The number of PCI read completions received by the
 *             PIC block
 * @ini_num_mwr_byte_sent: The number of PCI memory write bytes sent by the PIC
 *             block to the host
 * @ini_num_cpl_byte_rcvd: The number of PCI read completion bytes received by
 *             the PIC block
 * @wrcrdtarb_xoff: TBD
 * @rdcrdtarb_xoff: TBD
 * @vpath_genstats_count0: TBD
 * @vpath_genstats_count1: TBD
 * @vpath_genstats_count2: TBD
 * @vpath_genstats_count3: TBD
 * @vpath_genstats_count4: TBD
 * @vpath_gennstats_count5: TBD
 * @tx_stats: Transmit stats
 * @rx_stats: Receive stats
 * @prog_event_vnum1: Programmable statistic. Increments when internal logic
 *             detects a certain event. See register
 *             XMAC_STATS_CFG.EVENT_VNUM1_CFG for more information.
 * @prog_event_vnum0: Programmable statistic. Increments when internal logic
 *             detects a certain event. See register
 *             XMAC_STATS_CFG.EVENT_VNUM0_CFG for more information.
 * @prog_event_vnum3: Programmable statistic. Increments when internal logic
 *             detects a certain event. See register
 *             XMAC_STATS_CFG.EVENT_VNUM3_CFG for more information.
 * @prog_event_vnum2: Programmable statistic. Increments when internal logic
 *             detects a certain event. See register
 *             XMAC_STATS_CFG.EVENT_VNUM2_CFG for more information.
 * @rx_multi_cast_frame_discard: TBD
 * @rx_frm_transferred: TBD
 * @rxd_returned: TBD
 * @rx_mpa_len_fail_frms: Count of received frames
 * 		that fail the MPA length check
 * @rx_mpa_mrk_fail_frms: Count of received frames
 * 		that fail the MPA marker check
 * @rx_mpa_crc_fail_frms: Count of received frames that fail the MPA CRC check
 * @rx_permitted_frms: Count of frames that pass through the FAU and on to the
 *             frame buffer (and subsequently to the host).
 * @rx_vp_reset_discarded_frms: Count of receive frames that are discarded
 *             because the VPATH is in reset
 * @rx_wol_frms: Count of received "magic packet" frames. Stat increments
 *             whenever the received frame matches the VPATH's Wake-on-LAN
 *             signature(s) CRC.
 * @tx_vp_reset_discarded_frms: Count of transmit frames that are discarded
 *             because the VPATH is in reset. Includes frames that are discarded
 *             because the current VPIN does not match that VPIN of the frame
 *
 * Titan vpath hardware statistics.
 */
struct vxge_hw_vpath_stats_hw_info {
/*0x000*/	u32 ini_num_mwr_sent;
/*0x004*/	u32 unused1;
/*0x008*/	u32 ini_num_mrd_sent;
/*0x00c*/	u32 unused2;
/*0x010*/	u32 ini_num_cpl_rcvd;
/*0x014*/	u32 unused3;
/*0x018*/	u64 ini_num_mwr_byte_sent;
/*0x020*/	u64 ini_num_cpl_byte_rcvd;
/*0x028*/	u32 wrcrdtarb_xoff;
/*0x02c*/	u32 unused4;
/*0x030*/	u32 rdcrdtarb_xoff;
/*0x034*/	u32 unused5;
/*0x038*/	u32 vpath_genstats_count0;
/*0x03c*/	u32 vpath_genstats_count1;
/*0x040*/	u32 vpath_genstats_count2;
/*0x044*/	u32 vpath_genstats_count3;
/*0x048*/	u32 vpath_genstats_count4;
/*0x04c*/	u32 unused6;
/*0x050*/	u32 vpath_genstats_count5;
/*0x054*/	u32 unused7;
/*0x058*/	struct vxge_hw_xmac_vpath_tx_stats tx_stats;
/*0x0e8*/	struct vxge_hw_xmac_vpath_rx_stats rx_stats;
/*0x220*/	u64 unused9;
/*0x228*/	u32 prog_event_vnum1;
/*0x22c*/	u32 prog_event_vnum0;
/*0x230*/	u32 prog_event_vnum3;
/*0x234*/	u32 prog_event_vnum2;
/*0x238*/	u16 rx_multi_cast_frame_discard;
/*0x23a*/	u8 unused10[6];
/*0x240*/	u32 rx_frm_transferred;
/*0x244*/	u32 unused11;
/*0x248*/	u16 rxd_returned;
/*0x24a*/	u8 unused12[6];
/*0x252*/	u16 rx_mpa_len_fail_frms;
/*0x254*/	u16 rx_mpa_mrk_fail_frms;
/*0x256*/	u16 rx_mpa_crc_fail_frms;
/*0x258*/	u16 rx_permitted_frms;
/*0x25c*/	u64 rx_vp_reset_discarded_frms;
/*0x25e*/	u64 rx_wol_frms;
/*0x260*/	u64 tx_vp_reset_discarded_frms;
} __packed;


/**
 * struct vxge_hw_device_stats_mrpcim_info - Titan mrpcim hardware statistics.
 * @pic.ini_rd_drop  	 0x0000  	 4  	 Number of DMA reads initiated
 *  by the adapter that were discarded because the VPATH is out of service
 * @pic.ini_wr_drop 	0x0004 	4 	Number of DMA writes initiated by the
 *  adapter that were discared because the VPATH is out of service
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane0] 	0x0008 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane1] 	0x0010 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane2] 	0x0018 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane3] 	0x0020 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane4] 	0x0028 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane5] 	0x0030 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane6] 	0x0038 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane7] 	0x0040 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane8] 	0x0048 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane9] 	0x0050 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane10] 	0x0058 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane11] 	0x0060 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane12] 	0x0068 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane13] 	0x0070 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane14] 	0x0078 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane15] 	0x0080 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_ph_crdt_depleted[vplane16] 	0x0088 	4 	Number of times
 *  the posted header credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane0] 	0x0090 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane1] 	0x0098 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane2] 	0x00a0 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane3] 	0x00a8 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane4] 	0x00b0 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane5] 	0x00b8 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane6] 	0x00c0 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane7] 	0x00c8 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane8] 	0x00d0 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane9] 	0x00d8 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane10] 	0x00e0 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane11] 	0x00e8 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane12] 	0x00f0 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane13] 	0x00f8 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane14] 	0x0100 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane15] 	0x0108 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.wrcrdtarb_pd_crdt_depleted[vplane16] 	0x0110 	4 	Number of times
 *  the posted data credits for upstream PCI writes were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane0] 	0x0118 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane1] 	0x0120 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane2] 	0x0128 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane3] 	0x0130 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane4] 	0x0138 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane5] 	0x0140 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane6] 	0x0148 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane7] 	0x0150 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane8] 	0x0158 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane9] 	0x0160 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane10] 	0x0168 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane11] 	0x0170 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane12] 	0x0178 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane13] 	0x0180 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane14] 	0x0188 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane15] 	0x0190 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.rdcrdtarb_nph_crdt_depleted[vplane16] 	0x0198 	4 	Number of times
 *  the non-posted header credits for upstream PCI reads were depleted
 * @pic.ini_rd_vpin_drop 	0x01a0 	4 	Number of DMA reads initiated by
 *  the adapter that were discarded because the VPATH instance number does
 *  not match
 * @pic.ini_wr_vpin_drop 	0x01a4 	4 	Number of DMA writes initiated
 *  by the adapter that were discarded because the VPATH instance number
 *  does not match
 * @pic.genstats_count0 	0x01a8 	4 	Configurable statistic #1. Refer
 *  to the GENSTATS0_CFG for information on configuring this statistic
 * @pic.genstats_count1 	0x01ac 	4 	Configurable statistic #2. Refer
 *  to the GENSTATS1_CFG for information on configuring this statistic
 * @pic.genstats_count2 	0x01b0 	4 	Configurable statistic #3. Refer
 *  to the GENSTATS2_CFG for information on configuring this statistic
 * @pic.genstats_count3 	0x01b4 	4 	Configurable statistic #4. Refer
 *  to the GENSTATS3_CFG for information on configuring this statistic
 * @pic.genstats_count4 	0x01b8 	4 	Configurable statistic #5. Refer
 *  to the GENSTATS4_CFG for information on configuring this statistic
 * @pic.genstats_count5 	0x01c0 	4 	Configurable statistic #6. Refer
 *  to the GENSTATS5_CFG for information on configuring this statistic
 * @pci.rstdrop_cpl 	0x01c8 	4
 * @pci.rstdrop_msg 	0x01cc 	4
 * @pci.rstdrop_client1 	0x01d0 	4
 * @pci.rstdrop_client0 	0x01d4 	4
 * @pci.rstdrop_client2 	0x01d8 	4
 * @pci.depl_cplh[vplane0] 	0x01e2 	2 	Number of times completion
 *  header credits were depleted
 * @pci.depl_nph[vplane0] 	0x01e4 	2 	Number of times non posted
 *  header credits were depleted
 * @pci.depl_ph[vplane0] 	0x01e6 	2 	Number of times the posted
 *  header credits were depleted
 * @pci.depl_cplh[vplane1] 	0x01ea 	2
 * @pci.depl_nph[vplane1] 	0x01ec 	2
 * @pci.depl_ph[vplane1] 	0x01ee 	2
 * @pci.depl_cplh[vplane2] 	0x01f2 	2
 * @pci.depl_nph[vplane2] 	0x01f4 	2
 * @pci.depl_ph[vplane2] 	0x01f6 	2
 * @pci.depl_cplh[vplane3] 	0x01fa 	2
 * @pci.depl_nph[vplane3] 	0x01fc 	2
 * @pci.depl_ph[vplane3] 	0x01fe 	2
 * @pci.depl_cplh[vplane4] 	0x0202 	2
 * @pci.depl_nph[vplane4] 	0x0204 	2
 * @pci.depl_ph[vplane4] 	0x0206 	2
 * @pci.depl_cplh[vplane5] 	0x020a 	2
 * @pci.depl_nph[vplane5] 	0x020c 	2
 * @pci.depl_ph[vplane5] 	0x020e 	2
 * @pci.depl_cplh[vplane6] 	0x0212 	2
 * @pci.depl_nph[vplane6] 	0x0214 	2
 * @pci.depl_ph[vplane6] 	0x0216 	2
 * @pci.depl_cplh[vplane7] 	0x021a 	2
 * @pci.depl_nph[vplane7] 	0x021c 	2
 * @pci.depl_ph[vplane7] 	0x021e 	2
 * @pci.depl_cplh[vplane8] 	0x0222 	2
 * @pci.depl_nph[vplane8] 	0x0224 	2
 * @pci.depl_ph[vplane8] 	0x0226 	2
 * @pci.depl_cplh[vplane9] 	0x022a 	2
 * @pci.depl_nph[vplane9] 	0x022c 	2
 * @pci.depl_ph[vplane9] 	0x022e 	2
 * @pci.depl_cplh[vplane10] 	0x0232 	2
 * @pci.depl_nph[vplane10] 	0x0234 	2
 * @pci.depl_ph[vplane10] 	0x0236 	2
 * @pci.depl_cplh[vplane11] 	0x023a 	2
 * @pci.depl_nph[vplane11] 	0x023c 	2
 * @pci.depl_ph[vplane11] 	0x023e 	2
 * @pci.depl_cplh[vplane12] 	0x0242 	2
 * @pci.depl_nph[vplane12] 	0x0244 	2
 * @pci.depl_ph[vplane12] 	0x0246 	2
 * @pci.depl_cplh[vplane13] 	0x024a 	2
 * @pci.depl_nph[vplane13] 	0x024c 	2
 * @pci.depl_ph[vplane13] 	0x024e 	2
 * @pci.depl_cplh[vplane14] 	0x0252 	2
 * @pci.depl_nph[vplane14] 	0x0254 	2
 * @pci.depl_ph[vplane14] 	0x0256 	2
 * @pci.depl_cplh[vplane15] 	0x025a 	2
 * @pci.depl_nph[vplane15] 	0x025c 	2
 * @pci.depl_ph[vplane15] 	0x025e 	2
 * @pci.depl_cplh[vplane16] 	0x0262 	2
 * @pci.depl_nph[vplane16] 	0x0264 	2
 * @pci.depl_ph[vplane16] 	0x0266 	2
 * @pci.depl_cpld[vplane0] 	0x026a 	2 	Number of times completion data
 *  credits were depleted
 * @pci.depl_npd[vplane0] 	0x026c 	2 	Number of times non posted data
 *  credits were depleted
 * @pci.depl_pd[vplane0] 	0x026e 	2 	Number of times the posted data
 *  credits were depleted
 * @pci.depl_cpld[vplane1] 	0x0272 	2
 * @pci.depl_npd[vplane1] 	0x0274 	2
 * @pci.depl_pd[vplane1] 	0x0276 	2
 * @pci.depl_cpld[vplane2] 	0x027a 	2
 * @pci.depl_npd[vplane2] 	0x027c 	2
 * @pci.depl_pd[vplane2] 	0x027e 	2
 * @pci.depl_cpld[vplane3] 	0x0282 	2
 * @pci.depl_npd[vplane3] 	0x0284 	2
 * @pci.depl_pd[vplane3] 	0x0286 	2
 * @pci.depl_cpld[vplane4] 	0x028a 	2
 * @pci.depl_npd[vplane4] 	0x028c 	2
 * @pci.depl_pd[vplane4] 	0x028e 	2
 * @pci.depl_cpld[vplane5] 	0x0292 	2
 * @pci.depl_npd[vplane5] 	0x0294 	2
 * @pci.depl_pd[vplane5] 	0x0296 	2
 * @pci.depl_cpld[vplane6] 	0x029a 	2
 * @pci.depl_npd[vplane6] 	0x029c 	2
 * @pci.depl_pd[vplane6] 	0x029e 	2
 * @pci.depl_cpld[vplane7] 	0x02a2 	2
 * @pci.depl_npd[vplane7] 	0x02a4 	2
 * @pci.depl_pd[vplane7] 	0x02a6 	2
 * @pci.depl_cpld[vplane8] 	0x02aa 	2
 * @pci.depl_npd[vplane8] 	0x02ac 	2
 * @pci.depl_pd[vplane8] 	0x02ae 	2
 * @pci.depl_cpld[vplane9] 	0x02b2 	2
 * @pci.depl_npd[vplane9] 	0x02b4 	2
 * @pci.depl_pd[vplane9] 	0x02b6 	2
 * @pci.depl_cpld[vplane10] 	0x02ba 	2
 * @pci.depl_npd[vplane10] 	0x02bc 	2
 * @pci.depl_pd[vplane10] 	0x02be 	2
 * @pci.depl_cpld[vplane11] 	0x02c2 	2
 * @pci.depl_npd[vplane11] 	0x02c4 	2
 * @pci.depl_pd[vplane11] 	0x02c6 	2
 * @pci.depl_cpld[vplane12] 	0x02ca 	2
 * @pci.depl_npd[vplane12] 	0x02cc 	2
 * @pci.depl_pd[vplane12] 	0x02ce 	2
 * @pci.depl_cpld[vplane13] 	0x02d2 	2
 * @pci.depl_npd[vplane13] 	0x02d4 	2
 * @pci.depl_pd[vplane13] 	0x02d6 	2
 * @pci.depl_cpld[vplane14] 	0x02da 	2
 * @pci.depl_npd[vplane14] 	0x02dc 	2
 * @pci.depl_pd[vplane14] 	0x02de 	2
 * @pci.depl_cpld[vplane15] 	0x02e2 	2
 * @pci.depl_npd[vplane15] 	0x02e4 	2
 * @pci.depl_pd[vplane15] 	0x02e6 	2
 * @pci.depl_cpld[vplane16] 	0x02ea 	2
 * @pci.depl_npd[vplane16] 	0x02ec 	2
 * @pci.depl_pd[vplane16] 	0x02ee 	2
 * @xgmac_port[3];
 * @xgmac_aggr[2];
 * @xgmac.global_prog_event_gnum0 	0x0ae0 	8 	Programmable statistic.
 *  Increments when internal logic detects a certain event. See register
 *  XMAC_STATS_GLOBAL_CFG.EVENT_GNUM0_CFG for more information.
 * @xgmac.global_prog_event_gnum1 	0x0ae8 	8 	Programmable statistic.
 *  Increments when internal logic detects a certain event. See register
 *  XMAC_STATS_GLOBAL_CFG.EVENT_GNUM1_CFG for more information.
 * @xgmac.orp_lro_events 	0x0af8 	8
 * @xgmac.orp_bs_events 	0x0b00 	8
 * @xgmac.orp_iwarp_events 	0x0b08 	8
 * @xgmac.tx_permitted_frms 	0x0b14 	4
 * @xgmac.port2_tx_any_frms 	0x0b1d 	1
 * @xgmac.port1_tx_any_frms 	0x0b1e 	1
 * @xgmac.port0_tx_any_frms 	0x0b1f 	1
 * @xgmac.port2_rx_any_frms 	0x0b25 	1
 * @xgmac.port1_rx_any_frms 	0x0b26 	1
 * @xgmac.port0_rx_any_frms 	0x0b27 	1
 *
 * Titan mrpcim hardware statistics.
 */
struct vxge_hw_device_stats_mrpcim_info {
/*0x0000*/	u32	pic_ini_rd_drop;
/*0x0004*/	u32	pic_ini_wr_drop;
/*0x0008*/	struct {
	/*0x0000*/	u32	pic_wrcrdtarb_ph_crdt_depleted;
	/*0x0004*/	u32	unused1;
		} pic_wrcrdtarb_ph_crdt_depleted_vplane[17];
/*0x0090*/	struct {
	/*0x0000*/	u32	pic_wrcrdtarb_pd_crdt_depleted;
	/*0x0004*/	u32	unused2;
		} pic_wrcrdtarb_pd_crdt_depleted_vplane[17];
/*0x0118*/	struct {
	/*0x0000*/	u32	pic_rdcrdtarb_nph_crdt_depleted;
	/*0x0004*/	u32	unused3;
		} pic_rdcrdtarb_nph_crdt_depleted_vplane[17];
/*0x01a0*/	u32	pic_ini_rd_vpin_drop;
/*0x01a4*/	u32	pic_ini_wr_vpin_drop;
/*0x01a8*/	u32	pic_genstats_count0;
/*0x01ac*/	u32	pic_genstats_count1;
/*0x01b0*/	u32	pic_genstats_count2;
/*0x01b4*/	u32	pic_genstats_count3;
/*0x01b8*/	u32	pic_genstats_count4;
/*0x01bc*/	u32	unused4;
/*0x01c0*/	u32	pic_genstats_count5;
/*0x01c4*/	u32	unused5;
/*0x01c8*/	u32	pci_rstdrop_cpl;
/*0x01cc*/	u32	pci_rstdrop_msg;
/*0x01d0*/	u32	pci_rstdrop_client1;
/*0x01d4*/	u32	pci_rstdrop_client0;
/*0x01d8*/	u32	pci_rstdrop_client2;
/*0x01dc*/	u32	unused6;
/*0x01e0*/	struct {
	/*0x0000*/	u16	unused7;
	/*0x0002*/	u16	pci_depl_cplh;
	/*0x0004*/	u16	pci_depl_nph;
	/*0x0006*/	u16	pci_depl_ph;
		} pci_depl_h_vplane[17];
/*0x0268*/	struct {
	/*0x0000*/	u16	unused8;
	/*0x0002*/	u16	pci_depl_cpld;
	/*0x0004*/	u16	pci_depl_npd;
	/*0x0006*/	u16	pci_depl_pd;
		} pci_depl_d_vplane[17];
/*0x02f0*/	struct vxge_hw_xmac_port_stats xgmac_port[3];
/*0x0a10*/	struct vxge_hw_xmac_aggr_stats xgmac_aggr[2];
/*0x0ae0*/	u64	xgmac_global_prog_event_gnum0;
/*0x0ae8*/	u64	xgmac_global_prog_event_gnum1;
/*0x0af0*/	u64	unused7;
/*0x0af8*/	u64	unused8;
/*0x0b00*/	u64	unused9;
/*0x0b08*/	u64	unused10;
/*0x0b10*/	u32	unused11;
/*0x0b14*/	u32	xgmac_tx_permitted_frms;
/*0x0b18*/	u32	unused12;
/*0x0b1c*/	u8	unused13;
/*0x0b1d*/	u8	xgmac_port2_tx_any_frms;
/*0x0b1e*/	u8	xgmac_port1_tx_any_frms;
/*0x0b1f*/	u8	xgmac_port0_tx_any_frms;
/*0x0b20*/	u32	unused14;
/*0x0b24*/	u8	unused15;
/*0x0b25*/	u8	xgmac_port2_rx_any_frms;
/*0x0b26*/	u8	xgmac_port1_rx_any_frms;
/*0x0b27*/	u8	xgmac_port0_rx_any_frms;
} __packed;

/**
 * struct vxge_hw_device_stats_hw_info - Titan hardware statistics.
 * @vpath_info: VPath statistics
 * @vpath_info_sav: Vpath statistics saved
 *
 * Titan hardware statistics.
 */
struct vxge_hw_device_stats_hw_info {
	struct vxge_hw_vpath_stats_hw_info
		*vpath_info[VXGE_HW_MAX_VIRTUAL_PATHS];
	struct vxge_hw_vpath_stats_hw_info
		vpath_info_sav[VXGE_HW_MAX_VIRTUAL_PATHS];
};

/**
 * struct vxge_hw_vpath_stats_sw_common_info - HW common
 * statistics for queues.
 * @full_cnt: Number of times the queue was full
 * @usage_cnt: usage count.
 * @usage_max: Maximum usage
 * @reserve_free_swaps_cnt: Reserve/free swap counter. Internal usage.
 * @total_compl_cnt: Total descriptor completion count.
 *
 * Hw queue counters
 * See also: struct vxge_hw_vpath_stats_sw_fifo_info{},
 * struct vxge_hw_vpath_stats_sw_ring_info{},
 */
struct vxge_hw_vpath_stats_sw_common_info {
	u32	full_cnt;
	u32	usage_cnt;
	u32	usage_max;
	u32	reserve_free_swaps_cnt;
	u32 total_compl_cnt;
};

/**
 * struct vxge_hw_vpath_stats_sw_fifo_info - HW fifo statistics
 * @common_stats: Common counters for all queues
 * @total_posts: Total number of postings on the queue.
 * @total_buffers: Total number of buffers posted.
 * @txd_t_code_err_cnt: Array of transmit transfer codes. The position
 * (index) in this array reflects the transfer code type, for instance
 * 0xA - "loss of link".
 * Value txd_t_code_err_cnt[i] reflects the
 * number of times the corresponding transfer code was encountered.
 *
 * HW fifo counters
 * See also: struct vxge_hw_vpath_stats_sw_common_info{},
 * struct vxge_hw_vpath_stats_sw_ring_info{},
 */
struct vxge_hw_vpath_stats_sw_fifo_info {
	struct vxge_hw_vpath_stats_sw_common_info common_stats;
	u32 total_posts;
	u32 total_buffers;
	u32 txd_t_code_err_cnt[VXGE_HW_DTR_MAX_T_CODE];
};

/**
 * struct vxge_hw_vpath_stats_sw_ring_info - HW ring statistics
 * @common_stats: Common counters for all queues
 * @rxd_t_code_err_cnt: Array of receive transfer codes. The position
 *             (index) in this array reflects the transfer code type,
 *             for instance
 *             0x7 - for "invalid receive buffer size", or 0x8 - for ECC.
 *             Value rxd_t_code_err_cnt[i] reflects the
 *             number of times the corresponding transfer code was encountered.
 *
 * HW ring counters
 * See also: struct vxge_hw_vpath_stats_sw_common_info{},
 * struct vxge_hw_vpath_stats_sw_fifo_info{},
 */
struct vxge_hw_vpath_stats_sw_ring_info {
	struct vxge_hw_vpath_stats_sw_common_info common_stats;
	u32 rxd_t_code_err_cnt[VXGE_HW_DTR_MAX_T_CODE];

};

/**
 * struct vxge_hw_vpath_stats_sw_err - HW vpath error statistics
 * @unknown_alarms:
 * @network_sustained_fault:
 * @network_sustained_ok:
 * @kdfcctl_fifo0_overwrite:
 * @kdfcctl_fifo0_poison:
 * @kdfcctl_fifo0_dma_error:
 * @dblgen_fifo0_overflow:
 * @statsb_pif_chain_error:
 * @statsb_drop_timeout:
 * @target_illegal_access:
 * @ini_serr_det:
 * @prc_ring_bumps:
 * @prc_rxdcm_sc_err:
 * @prc_rxdcm_sc_abort:
 * @prc_quanta_size_err:
 *
 * HW vpath error statistics
 */
struct vxge_hw_vpath_stats_sw_err {
	u32	unknown_alarms;
	u32	network_sustained_fault;
	u32	network_sustained_ok;
	u32	kdfcctl_fifo0_overwrite;
	u32	kdfcctl_fifo0_poison;
	u32	kdfcctl_fifo0_dma_error;
	u32	dblgen_fifo0_overflow;
	u32	statsb_pif_chain_error;
	u32	statsb_drop_timeout;
	u32	target_illegal_access;
	u32	ini_serr_det;
	u32	prc_ring_bumps;
	u32	prc_rxdcm_sc_err;
	u32	prc_rxdcm_sc_abort;
	u32	prc_quanta_size_err;
};

/**
 * struct vxge_hw_vpath_stats_sw_info - HW vpath sw statistics
 * @soft_reset_cnt: Number of times soft reset is done on this vpath.
 * @error_stats: error counters for the vpath
 * @ring_stats: counters for ring belonging to the vpath
 * @fifo_stats: counters for fifo belonging to the vpath
 *
 * HW vpath sw statistics
 * See also: struct vxge_hw_device_info{} }.
 */
struct vxge_hw_vpath_stats_sw_info {
	u32    soft_reset_cnt;
	struct vxge_hw_vpath_stats_sw_err	error_stats;
	struct vxge_hw_vpath_stats_sw_ring_info	ring_stats;
	struct vxge_hw_vpath_stats_sw_fifo_info	fifo_stats;
};

/**
 * struct vxge_hw_device_stats_sw_info - HW own per-device statistics.
 *
 * @not_traffic_intr_cnt: Number of times the host was interrupted
 *                        without new completions.
 *                        "Non-traffic interrupt counter".
 * @traffic_intr_cnt: Number of traffic interrupts for the device.
 * @total_intr_cnt: Total number of traffic interrupts for the device.
 *                  @total_intr_cnt == @traffic_intr_cnt +
 *                              @not_traffic_intr_cnt
 * @soft_reset_cnt: Number of times soft reset is done on this device.
 * @vpath_info: please see struct vxge_hw_vpath_stats_sw_info{}
 * HW per-device statistics.
 */
struct vxge_hw_device_stats_sw_info {
	u32	not_traffic_intr_cnt;
	u32	traffic_intr_cnt;
	u32	total_intr_cnt;
	u32	soft_reset_cnt;
	struct vxge_hw_vpath_stats_sw_info
		vpath_info[VXGE_HW_MAX_VIRTUAL_PATHS];
};

/**
 * struct vxge_hw_device_stats_sw_err - HW device error statistics.
 * @vpath_alarms: Number of vpath alarms
 *
 * HW Device error stats
 */
struct vxge_hw_device_stats_sw_err {
	u32     vpath_alarms;
};

/**
 * struct vxge_hw_device_stats - Contains HW per-device statistics,
 * including hw.
 * @devh: HW device handle.
 * @dma_addr: DMA address of the %hw_info. Given to device to fill-in the stats.
 * @hw_info_dmah: DMA handle used to map hw statistics onto the device memory
 *                space.
 * @hw_info_dma_acch: One more DMA handle used subsequently to free the
 *                    DMA object. Note that this and the previous handle have
 *                    physical meaning for Solaris; on Windows and Linux the
 *                    corresponding value will be simply pointer to PCI device.
 *
 * @hw_dev_info_stats: Titan statistics maintained by the hardware.
 * @sw_dev_info_stats: HW's "soft" device informational statistics, e.g. number
 *                     of completions per interrupt.
 * @sw_dev_err_stats: HW's "soft" device error statistics.
 *
 * Structure-container of HW per-device statistics. Note that per-channel
 * statistics are kept in separate structures under HW's fifo and ring
 * channels.
 */
struct vxge_hw_device_stats {
	/* handles */
	struct __vxge_hw_device *devh;

	/* HW device hardware statistics */
	struct vxge_hw_device_stats_hw_info	hw_dev_info_stats;

	/* HW device "soft" stats */
	struct vxge_hw_device_stats_sw_err   sw_dev_err_stats;
	struct vxge_hw_device_stats_sw_info  sw_dev_info_stats;

};

enum vxge_hw_status vxge_hw_device_hw_stats_enable(
			struct __vxge_hw_device *devh);

enum vxge_hw_status vxge_hw_device_stats_get(
			struct __vxge_hw_device *devh,
			struct vxge_hw_device_stats_hw_info *hw_stats);

enum vxge_hw_status vxge_hw_driver_stats_get(
			struct __vxge_hw_device *devh,
			struct vxge_hw_device_stats_sw_info *sw_stats);

enum vxge_hw_status vxge_hw_mrpcim_stats_enable(struct __vxge_hw_device *devh);

enum vxge_hw_status vxge_hw_mrpcim_stats_disable(struct __vxge_hw_device *devh);

enum vxge_hw_status
vxge_hw_mrpcim_stats_access(
	struct __vxge_hw_device *devh,
	u32 operation,
	u32 location,
	u32 offset,
	u64 *stat);

enum vxge_hw_status
vxge_hw_device_xmac_stats_get(struct __vxge_hw_device *devh,
			      struct vxge_hw_xmac_stats *xmac_stats);

/**
 * enum enum vxge_hw_mgmt_reg_type - Register types.
 *
 * @vxge_hw_mgmt_reg_type_legacy: Legacy registers
 * @vxge_hw_mgmt_reg_type_toc: TOC Registers
 * @vxge_hw_mgmt_reg_type_common: Common Registers
 * @vxge_hw_mgmt_reg_type_mrpcim: mrpcim registers
 * @vxge_hw_mgmt_reg_type_srpcim: srpcim registers
 * @vxge_hw_mgmt_reg_type_vpmgmt: vpath management registers
 * @vxge_hw_mgmt_reg_type_vpath: vpath registers
 *
 * Register type enumaration
 */
enum vxge_hw_mgmt_reg_type {
	vxge_hw_mgmt_reg_type_legacy = 0,
	vxge_hw_mgmt_reg_type_toc = 1,
	vxge_hw_mgmt_reg_type_common = 2,
	vxge_hw_mgmt_reg_type_mrpcim = 3,
	vxge_hw_mgmt_reg_type_srpcim = 4,
	vxge_hw_mgmt_reg_type_vpmgmt = 5,
	vxge_hw_mgmt_reg_type_vpath = 6
};

enum vxge_hw_status
vxge_hw_mgmt_reg_read(struct __vxge_hw_device *devh,
		      enum vxge_hw_mgmt_reg_type type,
		      u32 index,
		      u32 offset,
		      u64 *value);

enum vxge_hw_status
vxge_hw_mgmt_reg_write(struct __vxge_hw_device *devh,
		      enum vxge_hw_mgmt_reg_type type,
		      u32 index,
		      u32 offset,
		      u64 value);

/**
 * enum enum vxge_hw_rxd_state - Descriptor (RXD) state.
 * @VXGE_HW_RXD_STATE_NONE: Invalid state.
 * @VXGE_HW_RXD_STATE_AVAIL: Descriptor is available for reservation.
 * @VXGE_HW_RXD_STATE_POSTED: Descriptor is posted for processing by the
 * device.
 * @VXGE_HW_RXD_STATE_FREED: Descriptor is free and can be reused for
 * filling-in and posting later.
 *
 * Titan/HW descriptor states.
 *
 */
enum vxge_hw_rxd_state {
	VXGE_HW_RXD_STATE_NONE		= 0,
	VXGE_HW_RXD_STATE_AVAIL		= 1,
	VXGE_HW_RXD_STATE_POSTED	= 2,
	VXGE_HW_RXD_STATE_FREED		= 3
};

/**
 * struct vxge_hw_ring_rxd_info - Extended information associated with a
 * completed ring descriptor.
 * @syn_flag: SYN flag
 * @is_icmp: Is ICMP
 * @fast_path_eligible: Fast Path Eligible flag
 * @l3_cksum: in L3 checksum is valid
 * @l3_cksum: Result of IP checksum check (by Titan hardware).
 *            This field containing VXGE_HW_L3_CKSUM_OK would mean that
 *            the checksum is correct, otherwise - the datagram is
 *            corrupted.
 * @l4_cksum: in L4 checksum is valid
 * @l4_cksum: Result of TCP/UDP checksum check (by Titan hardware).
 *            This field containing VXGE_HW_L4_CKSUM_OK would mean that
 *            the checksum is correct. Otherwise - the packet is
 *            corrupted.
 * @frame: Zero or more of enum vxge_hw_frame_type flags.
 * 		See enum vxge_hw_frame_type{}.
 * @proto: zero or more of enum vxge_hw_frame_proto flags.  Reporting bits for
 *            various higher-layer protocols, including (but note restricted to)
 *            TCP and UDP. See enum vxge_hw_frame_proto{}.
 * @is_vlan: If vlan tag is valid
 * @vlan: VLAN tag extracted from the received frame.
 * @rth_bucket: RTH bucket
 * @rth_it_hit: Set, If RTH hash value calculated by the Titan hardware
 *             has a matching entry in the Indirection table.
 * @rth_spdm_hit: Set, If RTH hash value calculated by the Titan hardware
 *             has a matching entry in the Socket Pair Direct Match table.
 * @rth_hash_type: RTH hash code of the function used to calculate the hash.
 * @rth_value: Receive Traffic Hashing(RTH) hash value. Produced by Titan
 *             hardware if RTH is enabled.
 */
struct vxge_hw_ring_rxd_info {
	u32	syn_flag;
	u32	is_icmp;
	u32	fast_path_eligible;
	u32	l3_cksum_valid;
	u32	l3_cksum;
	u32	l4_cksum_valid;
	u32	l4_cksum;
	u32	frame;
	u32	proto;
	u32	is_vlan;
	u32	vlan;
	u32	rth_bucket;
	u32	rth_it_hit;
	u32	rth_spdm_hit;
	u32	rth_hash_type;
	u32	rth_value;
};
/**
 * enum vxge_hw_ring_tcode - Transfer codes returned by adapter
 * @VXGE_HW_RING_T_CODE_OK: Transfer ok.
 * @VXGE_HW_RING_T_CODE_L3_CKSUM_MISMATCH: Layer 3 checksum presentation
 *		configuration mismatch.
 * @VXGE_HW_RING_T_CODE_L4_CKSUM_MISMATCH: Layer 4 checksum presentation
 *		configuration mismatch.
 * @VXGE_HW_RING_T_CODE_L3_L4_CKSUM_MISMATCH: Layer 3 and Layer 4 checksum
 *		presentation configuration mismatch.
 * @VXGE_HW_RING_T_CODE_L3_PKT_ERR: Layer 3 error unparseable packet,
 *		such as unknown IPv6 header.
 * @VXGE_HW_RING_T_CODE_L2_FRM_ERR: Layer 2 error frame integrity
 *		error, such as FCS or ECC).
 * @VXGE_HW_RING_T_CODE_BUF_SIZE_ERR: Buffer size error the RxD buffer(
 *		s) were not appropriately sized and data loss occurred.
 * @VXGE_HW_RING_T_CODE_INT_ECC_ERR: Internal ECC error RxD corrupted.
 * @VXGE_HW_RING_T_CODE_BENIGN_OVFLOW: Benign overflow the contents of
 *		Segment1 exceeded the capacity of Buffer1 and the remainder
 *		was placed in Buffer2. Segment2 now starts in Buffer3.
 *		No data loss or errors occurred.
 * @VXGE_HW_RING_T_CODE_ZERO_LEN_BUFF: Buffer size 0 one of the RxDs
 *		assigned buffers has a size of 0 bytes.
 * @VXGE_HW_RING_T_CODE_FRM_DROP: Frame dropped either due to
 *		VPath Reset or because of a VPIN mismatch.
 * @VXGE_HW_RING_T_CODE_UNUSED: Unused
 * @VXGE_HW_RING_T_CODE_MULTI_ERR: Multiple errors more than one
 *		transfer code condition occurred.
 *
 * Transfer codes returned by adapter.
 */
enum vxge_hw_ring_tcode {
	VXGE_HW_RING_T_CODE_OK				= 0x0,
	VXGE_HW_RING_T_CODE_L3_CKSUM_MISMATCH		= 0x1,
	VXGE_HW_RING_T_CODE_L4_CKSUM_MISMATCH		= 0x2,
	VXGE_HW_RING_T_CODE_L3_L4_CKSUM_MISMATCH	= 0x3,
	VXGE_HW_RING_T_CODE_L3_PKT_ERR			= 0x5,
	VXGE_HW_RING_T_CODE_L2_FRM_ERR			= 0x6,
	VXGE_HW_RING_T_CODE_BUF_SIZE_ERR		= 0x7,
	VXGE_HW_RING_T_CODE_INT_ECC_ERR			= 0x8,
	VXGE_HW_RING_T_CODE_BENIGN_OVFLOW		= 0x9,
	VXGE_HW_RING_T_CODE_ZERO_LEN_BUFF		= 0xA,
	VXGE_HW_RING_T_CODE_FRM_DROP			= 0xC,
	VXGE_HW_RING_T_CODE_UNUSED			= 0xE,
	VXGE_HW_RING_T_CODE_MULTI_ERR			= 0xF
};

enum vxge_hw_status vxge_hw_ring_rxd_reserve(
	struct __vxge_hw_ring *ring_handle,
	void **rxdh);

void
vxge_hw_ring_rxd_pre_post(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh);

void
vxge_hw_ring_rxd_post_post(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh);

void
vxge_hw_ring_rxd_post_post_wmb(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh);

void vxge_hw_ring_rxd_post(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh);

enum vxge_hw_status vxge_hw_ring_rxd_next_completed(
	struct __vxge_hw_ring *ring_handle,
	void **rxdh,
	u8 *t_code);

enum vxge_hw_status vxge_hw_ring_handle_tcode(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh,
	u8 t_code);

void vxge_hw_ring_rxd_free(
	struct __vxge_hw_ring *ring_handle,
	void *rxdh);

/**
 * enum enum vxge_hw_frame_proto - Higher-layer ethernet protocols.
 * @VXGE_HW_FRAME_PROTO_VLAN_TAGGED: VLAN.
 * @VXGE_HW_FRAME_PROTO_IPV4: IPv4.
 * @VXGE_HW_FRAME_PROTO_IPV6: IPv6.
 * @VXGE_HW_FRAME_PROTO_IP_FRAG: IP fragmented.
 * @VXGE_HW_FRAME_PROTO_TCP: TCP.
 * @VXGE_HW_FRAME_PROTO_UDP: UDP.
 * @VXGE_HW_FRAME_PROTO_TCP_OR_UDP: TCP or UDP.
 *
 * Higher layer ethernet protocols and options.
 */
enum vxge_hw_frame_proto {
	VXGE_HW_FRAME_PROTO_VLAN_TAGGED = 0x80,
	VXGE_HW_FRAME_PROTO_IPV4		= 0x10,
	VXGE_HW_FRAME_PROTO_IPV6		= 0x08,
	VXGE_HW_FRAME_PROTO_IP_FRAG		= 0x04,
	VXGE_HW_FRAME_PROTO_TCP			= 0x02,
	VXGE_HW_FRAME_PROTO_UDP			= 0x01,
	VXGE_HW_FRAME_PROTO_TCP_OR_UDP	= (VXGE_HW_FRAME_PROTO_TCP | \
						   VXGE_HW_FRAME_PROTO_UDP)
};

/**
 * enum enum vxge_hw_fifo_gather_code - Gather codes used in fifo TxD
 * @VXGE_HW_FIFO_GATHER_CODE_FIRST: First TxDL
 * @VXGE_HW_FIFO_GATHER_CODE_MIDDLE: Middle TxDL
 * @VXGE_HW_FIFO_GATHER_CODE_LAST: Last TxDL
 * @VXGE_HW_FIFO_GATHER_CODE_FIRST_LAST: First and Last TxDL.
 *
 * These gather codes are used to indicate the position of a TxD in a TxD list
 */
enum vxge_hw_fifo_gather_code {
	VXGE_HW_FIFO_GATHER_CODE_FIRST		= 0x2,
	VXGE_HW_FIFO_GATHER_CODE_MIDDLE		= 0x0,
	VXGE_HW_FIFO_GATHER_CODE_LAST		= 0x1,
	VXGE_HW_FIFO_GATHER_CODE_FIRST_LAST	= 0x3
};

/**
 * enum enum vxge_hw_fifo_tcode - tcodes used in fifo
 * @VXGE_HW_FIFO_T_CODE_OK: Transfer OK
 * @VXGE_HW_FIFO_T_CODE_PCI_READ_CORRUPT: PCI read transaction (either TxD or
 *             frame data) returned with corrupt data.
 * @VXGE_HW_FIFO_T_CODE_PCI_READ_FAIL:PCI read transaction was returned
 *             with no data.
 * @VXGE_HW_FIFO_T_CODE_INVALID_MSS: The host attempted to send either a
 *             frame or LSO MSS that was too long (>9800B).
 * @VXGE_HW_FIFO_T_CODE_LSO_ERROR: Error detected during TCP/UDP Large Send
	*	       Offload operation, due to improper header template,
	*	       unsupported protocol, etc.
 * @VXGE_HW_FIFO_T_CODE_UNUSED: Unused
 * @VXGE_HW_FIFO_T_CODE_MULTI_ERROR: Set to 1 by the adapter if multiple
 *             data buffer transfer errors are encountered (see below).
 *             Otherwise it is set to 0.
 *
 * These tcodes are returned in various API for TxD status
 */
enum vxge_hw_fifo_tcode {
	VXGE_HW_FIFO_T_CODE_OK			= 0x0,
	VXGE_HW_FIFO_T_CODE_PCI_READ_CORRUPT	= 0x1,
	VXGE_HW_FIFO_T_CODE_PCI_READ_FAIL	= 0x2,
	VXGE_HW_FIFO_T_CODE_INVALID_MSS		= 0x3,
	VXGE_HW_FIFO_T_CODE_LSO_ERROR		= 0x4,
	VXGE_HW_FIFO_T_CODE_UNUSED		= 0x7,
	VXGE_HW_FIFO_T_CODE_MULTI_ERROR		= 0x8
};

enum vxge_hw_status vxge_hw_fifo_txdl_reserve(
	struct __vxge_hw_fifo *fifoh,
	void **txdlh,
	void **txdl_priv);

void vxge_hw_fifo_txdl_buffer_set(
			struct __vxge_hw_fifo *fifo_handle,
			void *txdlh,
			u32 frag_idx,
			dma_addr_t dma_pointer,
			u32 size);

void vxge_hw_fifo_txdl_post(
			struct __vxge_hw_fifo *fifo_handle,
			void *txdlh);

u32 vxge_hw_fifo_free_txdl_count_get(
			struct __vxge_hw_fifo *fifo_handle);

enum vxge_hw_status vxge_hw_fifo_txdl_next_completed(
	struct __vxge_hw_fifo *fifoh,
	void **txdlh,
	enum vxge_hw_fifo_tcode *t_code);

enum vxge_hw_status vxge_hw_fifo_handle_tcode(
	struct __vxge_hw_fifo *fifoh,
	void *txdlh,
	enum vxge_hw_fifo_tcode t_code);

void vxge_hw_fifo_txdl_free(
	struct __vxge_hw_fifo *fifoh,
	void *txdlh);

/*
 * Device
 */

#define VXGE_HW_RING_NEXT_BLOCK_POINTER_OFFSET	(VXGE_HW_BLOCK_SIZE-8)
#define VXGE_HW_RING_MEMBLOCK_IDX_OFFSET		(VXGE_HW_BLOCK_SIZE-16)

/*
 * struct __vxge_hw_ring_rxd_priv - Receive descriptor HW-private data.
 * @dma_addr: DMA (mapped) address of _this_ descriptor.
 * @dma_handle: DMA handle used to map the descriptor onto device.
 * @dma_offset: Descriptor's offset in the memory block. HW allocates
 *              descriptors in memory blocks of %VXGE_HW_BLOCK_SIZE
 *              bytes. Each memblock is contiguous DMA-able memory. Each
 *              memblock contains 1 or more 4KB RxD blocks visible to the
 *              Titan hardware.
 * @dma_object: DMA address and handle of the memory block that contains
 *              the descriptor. This member is used only in the "checked"
 *              version of the HW (to enforce certain assertions);
 *              otherwise it gets compiled out.
 * @allocated: True if the descriptor is reserved, 0 otherwise. Internal usage.
 *
 * Per-receive decsriptor HW-private data. HW uses the space to keep DMA
 * information associated with the descriptor. Note that driver can ask HW
 * to allocate additional per-descriptor space for its own (driver-specific)
 * purposes.
 */
struct __vxge_hw_ring_rxd_priv {
	dma_addr_t	dma_addr;
	struct pci_dev *dma_handle;
	ptrdiff_t	dma_offset;
#ifdef VXGE_DEBUG_ASSERT
	struct vxge_hw_mempool_dma	*dma_object;
#endif
};

struct vxge_hw_mempool_cbs {
	void (*item_func_alloc)(
			struct vxge_hw_mempool *mempoolh,
			u32			memblock_index,
			struct vxge_hw_mempool_dma	*dma_object,
			u32			index,
			u32			is_last);
};

#define VXGE_HW_VIRTUAL_PATH_HANDLE(vpath)				\
		((struct __vxge_hw_vpath_handle *)(vpath)->vpath_handles.next)

enum vxge_hw_status
__vxge_hw_vpath_rts_table_get(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u32			action,
	u32			rts_table,
	u32			offset,
	u64			*data1,
	u64			*data2);

enum vxge_hw_status
__vxge_hw_vpath_rts_table_set(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u32			action,
	u32			rts_table,
	u32			offset,
	u64			data1,
	u64			data2);

enum vxge_hw_status
__vxge_hw_vpath_enable(
	struct __vxge_hw_device *devh,
	u32			vp_id);

void vxge_hw_device_intr_enable(
	struct __vxge_hw_device *devh);

u32 vxge_hw_device_set_intr_type(struct __vxge_hw_device *devh, u32 intr_mode);

void vxge_hw_device_intr_disable(
	struct __vxge_hw_device *devh);

void vxge_hw_device_mask_all(
	struct __vxge_hw_device *devh);

void vxge_hw_device_unmask_all(
	struct __vxge_hw_device *devh);

enum vxge_hw_status vxge_hw_device_begin_irq(
	struct __vxge_hw_device *devh,
	u32 skip_alarms,
	u64 *reason);

void vxge_hw_device_clear_tx_rx(
	struct __vxge_hw_device *devh);

/*
 *  Virtual Paths
 */

void vxge_hw_vpath_dynamic_rti_rtimer_set(struct __vxge_hw_ring *ring);

void vxge_hw_vpath_dynamic_tti_rtimer_set(struct __vxge_hw_fifo *fifo);

u32 vxge_hw_vpath_id(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_vpath_mac_addr_add_mode {
	VXGE_HW_VPATH_MAC_ADDR_ADD_DUPLICATE = 0,
	VXGE_HW_VPATH_MAC_ADDR_DISCARD_DUPLICATE = 1,
	VXGE_HW_VPATH_MAC_ADDR_REPLACE_DUPLICATE = 2
};

enum vxge_hw_status
vxge_hw_vpath_mac_addr_add(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u8 *macaddr,
	u8 *macaddr_mask,
	enum vxge_hw_vpath_mac_addr_add_mode duplicate_mode);

enum vxge_hw_status
vxge_hw_vpath_mac_addr_get(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u8 *macaddr,
	u8 *macaddr_mask);

enum vxge_hw_status
vxge_hw_vpath_mac_addr_get_next(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u8 *macaddr,
	u8 *macaddr_mask);

enum vxge_hw_status
vxge_hw_vpath_mac_addr_delete(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u8 *macaddr,
	u8 *macaddr_mask);

enum vxge_hw_status
vxge_hw_vpath_vid_add(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u64			vid);

enum vxge_hw_status
vxge_hw_vpath_vid_delete(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u64			vid);

enum vxge_hw_status
vxge_hw_vpath_etype_add(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u64			etype);

enum vxge_hw_status
vxge_hw_vpath_etype_get(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u64			*etype);

enum vxge_hw_status
vxge_hw_vpath_etype_get_next(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u64			*etype);

enum vxge_hw_status
vxge_hw_vpath_etype_delete(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u64			etype);

enum vxge_hw_status vxge_hw_vpath_promisc_enable(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status vxge_hw_vpath_promisc_disable(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status vxge_hw_vpath_bcast_enable(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status vxge_hw_vpath_mcast_enable(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status vxge_hw_vpath_mcast_disable(
	struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status vxge_hw_vpath_poll_rx(
	struct __vxge_hw_ring *ringh);

enum vxge_hw_status vxge_hw_vpath_poll_tx(
	struct __vxge_hw_fifo *fifoh,
	struct sk_buff ***skb_ptr, int nr_skb, int *more);

enum vxge_hw_status vxge_hw_vpath_alarm_process(
	struct __vxge_hw_vpath_handle *vpath_handle,
	u32 skip_alarms);

void
vxge_hw_vpath_msix_set(struct __vxge_hw_vpath_handle *vpath_handle,
		       int *tim_msix_id, int alarm_msix_id);

void
vxge_hw_vpath_msix_mask(struct __vxge_hw_vpath_handle *vpath_handle,
			int msix_id);

void vxge_hw_vpath_msix_clear(struct __vxge_hw_vpath_handle *vp, int msix_id);

void vxge_hw_device_flush_io(struct __vxge_hw_device *devh);

void
vxge_hw_vpath_msix_unmask(struct __vxge_hw_vpath_handle *vpath_handle,
			  int msix_id);

enum vxge_hw_status vxge_hw_vpath_intr_enable(
				struct __vxge_hw_vpath_handle *vpath_handle);

enum vxge_hw_status vxge_hw_vpath_intr_disable(
				struct __vxge_hw_vpath_handle *vpath_handle);

void vxge_hw_vpath_inta_mask_tx_rx(
	struct __vxge_hw_vpath_handle *vpath_handle);

void vxge_hw_vpath_inta_unmask_tx_rx(
	struct __vxge_hw_vpath_handle *vpath_handle);

void
vxge_hw_channel_msix_mask(struct __vxge_hw_channel *channelh, int msix_id);

void
vxge_hw_channel_msix_unmask(struct __vxge_hw_channel *channelh, int msix_id);

void
vxge_hw_channel_msix_clear(struct __vxge_hw_channel *channelh, int msix_id);

void
vxge_hw_channel_dtr_try_complete(struct __vxge_hw_channel *channel,
				 void **dtrh);

void
vxge_hw_channel_dtr_complete(struct __vxge_hw_channel *channel);

void
vxge_hw_channel_dtr_free(struct __vxge_hw_channel *channel, void *dtrh);

int
vxge_hw_channel_dtr_count(struct __vxge_hw_channel *channel);

void vxge_hw_vpath_tti_ci_set(struct __vxge_hw_fifo *fifo);

void vxge_hw_vpath_dynamic_rti_ci_set(struct __vxge_hw_ring *ring);

#endif

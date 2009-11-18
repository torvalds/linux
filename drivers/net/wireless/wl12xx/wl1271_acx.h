/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL1271_ACX_H__
#define __WL1271_ACX_H__

#include "wl1271.h"
#include "wl1271_cmd.h"

/*************************************************************************

    Host Interrupt Register (WiLink -> Host)

**************************************************************************/
/* HW Initiated interrupt Watchdog timer expiration */
#define WL1271_ACX_INTR_WATCHDOG           BIT(0)
/* Init sequence is done (masked interrupt, detection through polling only ) */
#define WL1271_ACX_INTR_INIT_COMPLETE      BIT(1)
/* Event was entered to Event MBOX #A*/
#define WL1271_ACX_INTR_EVENT_A            BIT(2)
/* Event was entered to Event MBOX #B*/
#define WL1271_ACX_INTR_EVENT_B            BIT(3)
/* Command processing completion*/
#define WL1271_ACX_INTR_CMD_COMPLETE       BIT(4)
/* Signaling the host on HW wakeup */
#define WL1271_ACX_INTR_HW_AVAILABLE       BIT(5)
/* The MISC bit is used for aggregation of RX, TxComplete and TX rate update */
#define WL1271_ACX_INTR_DATA               BIT(6)
/* Trace meassge on MBOX #A */
#define WL1271_ACX_INTR_TRACE_A            BIT(7)
/* Trace meassge on MBOX #B */
#define WL1271_ACX_INTR_TRACE_B            BIT(8)

#define WL1271_ACX_INTR_ALL		   0xFFFFFFFF
#define WL1271_ACX_ALL_EVENTS_VECTOR       (WL1271_ACX_INTR_WATCHDOG      | \
					    WL1271_ACX_INTR_INIT_COMPLETE | \
					    WL1271_ACX_INTR_EVENT_A       | \
					    WL1271_ACX_INTR_EVENT_B       | \
					    WL1271_ACX_INTR_CMD_COMPLETE  | \
					    WL1271_ACX_INTR_HW_AVAILABLE  | \
					    WL1271_ACX_INTR_DATA)

#define WL1271_INTR_MASK                   (WL1271_ACX_INTR_EVENT_A |	\
					    WL1271_ACX_INTR_EVENT_B | \
					    WL1271_ACX_INTR_DATA)

/* Target's information element */
struct acx_header {
	struct wl1271_cmd_header cmd;

	/* acx (or information element) header */
	u16 id;

	/* payload length (not including headers */
	u16 len;
};

struct acx_error_counter {
	struct acx_header header;

	/* The number of PLCP errors since the last time this */
	/* information element was interrogated. This field is */
	/* automatically cleared when it is interrogated.*/
	u32 PLCP_error;

	/* The number of FCS errors since the last time this */
	/* information element was interrogated. This field is */
	/* automatically cleared when it is interrogated.*/
	u32 FCS_error;

	/* The number of MPDUs without PLCP header errors received*/
	/* since the last time this information element was interrogated. */
	/* This field is automatically cleared when it is interrogated.*/
	u32 valid_frame;

	/* the number of missed sequence numbers in the squentially */
	/* values of frames seq numbers */
	u32 seq_num_miss;
} __attribute__ ((packed));

struct acx_revision {
	struct acx_header header;

	/*
	 * The WiLink firmware version, an ASCII string x.x.x.x,
	 * that uniquely identifies the current firmware.
	 * The left most digit is incremented each time a
	 * significant change is made to the firmware, such as
	 * code redesign or new platform support.
	 * The second digit is incremented when major enhancements
	 * are added or major fixes are made.
	 * The third digit is incremented for each GA release.
	 * The fourth digit is incremented for each build.
	 * The first two digits identify a firmware release version,
	 * in other words, a unique set of features.
	 * The first three digits identify a GA release.
	 */
	char fw_version[20];

	/*
	 * This 4 byte field specifies the WiLink hardware version.
	 * bits 0  - 15: Reserved.
	 * bits 16 - 23: Version ID - The WiLink version ID
	 *              (1 = first spin, 2 = second spin, and so on).
	 * bits 24 - 31: Chip ID - The WiLink chip ID.
	 */
	u32 hw_version;
} __attribute__ ((packed));

enum wl1271_psm_mode {
	/* Active mode */
	WL1271_PSM_CAM = 0,

	/* Power save mode */
	WL1271_PSM_PS = 1,

	/* Extreme low power */
	WL1271_PSM_ELP = 2,
};

struct acx_sleep_auth {
	struct acx_header header;

	/* The sleep level authorization of the device. */
	/* 0 - Always active*/
	/* 1 - Power down mode: light / fast sleep*/
	/* 2 - ELP mode: Deep / Max sleep*/
	u8  sleep_auth;
	u8  padding[3];
} __attribute__ ((packed));

enum {
	HOSTIF_PCI_MASTER_HOST_INDIRECT,
	HOSTIF_PCI_MASTER_HOST_DIRECT,
	HOSTIF_SLAVE,
	HOSTIF_PKT_RING,
	HOSTIF_DONTCARE = 0xFF
};

#define DEFAULT_UCAST_PRIORITY          0
#define DEFAULT_RX_Q_PRIORITY           0
#define DEFAULT_NUM_STATIONS            1
#define DEFAULT_RXQ_PRIORITY            0 /* low 0 .. 15 high  */
#define DEFAULT_RXQ_TYPE                0x07    /* All frames, Data/Ctrl/Mgmt */
#define TRACE_BUFFER_MAX_SIZE           256

#define  DP_RX_PACKET_RING_CHUNK_SIZE 1600
#define  DP_TX_PACKET_RING_CHUNK_SIZE 1600
#define  DP_RX_PACKET_RING_CHUNK_NUM 2
#define  DP_TX_PACKET_RING_CHUNK_NUM 2
#define  DP_TX_COMPLETE_TIME_OUT 20
#define  FW_TX_CMPLT_BLOCK_SIZE 16

#define TX_MSDU_LIFETIME_MIN       0
#define TX_MSDU_LIFETIME_MAX       3000
#define TX_MSDU_LIFETIME_DEF       512
#define RX_MSDU_LIFETIME_MIN       0
#define RX_MSDU_LIFETIME_MAX       0xFFFFFFFF
#define RX_MSDU_LIFETIME_DEF       512000

struct acx_rx_msdu_lifetime {
	struct acx_header header;

	/*
	 * The maximum amount of time, in TU, before the
	 * firmware discards the MSDU.
	 */
	u32 lifetime;
} __attribute__ ((packed));

/*
 * RX Config Options Table
 * Bit		Definition
 * ===		==========
 * 31:14		Reserved
 * 13		Copy RX Status - when set, write three receive status words
 *		to top of rx'd MPDUs.
 *		When cleared, do not write three status words (added rev 1.5)
 * 12		Reserved
 * 11		RX Complete upon FCS error - when set, give rx complete
 *		interrupt for FCS errors, after the rx filtering, e.g. unicast
 *		frames not to us with FCS error will not generate an interrupt.
 * 10		SSID Filter Enable - When set, the WiLink discards all beacon,
 *	        probe request, and probe response frames with an SSID that does
 *		not match the SSID specified by the host in the START/JOIN
 *		command.
 *		When clear, the WiLink receives frames with any SSID.
 * 9		Broadcast Filter Enable - When set, the WiLink discards all
 *		broadcast frames. When clear, the WiLink receives all received
 *		broadcast frames.
 * 8:6		Reserved
 * 5		BSSID Filter Enable - When set, the WiLink discards any frames
 *		with a BSSID that does not match the BSSID specified by the
 *		host.
 *		When clear, the WiLink receives frames from any BSSID.
 * 4		MAC Addr Filter - When set, the WiLink discards any frames
 *		with a destination address that does not match the MAC address
 *		of the adaptor.
 *		When clear, the WiLink receives frames destined to any MAC
 *		address.
 * 3		Promiscuous - When set, the WiLink receives all valid frames
 *		(i.e., all frames that pass the FCS check).
 *		When clear, only frames that pass the other filters specified
 *		are received.
 * 2		FCS - When set, the WiLink includes the FCS with the received
 *		frame.
 *		When cleared, the FCS is discarded.
 * 1		PLCP header - When set, write all data from baseband to frame
 *		buffer including PHY header.
 * 0		Reserved - Always equal to 0.
 *
 * RX Filter Options Table
 * Bit		Definition
 * ===		==========
 * 31:12		Reserved - Always equal to 0.
 * 11		Association - When set, the WiLink receives all association
 *		related frames (association request/response, reassocation
 *		request/response, and disassociation). When clear, these frames
 *		are discarded.
 * 10		Auth/De auth - When set, the WiLink receives all authentication
 *		and de-authentication frames. When clear, these frames are
 *		discarded.
 * 9		Beacon - When set, the WiLink receives all beacon frames.
 *		When clear, these frames are discarded.
 * 8		Contention Free - When set, the WiLink receives all contention
 *		free frames.
 *		When clear, these frames are discarded.
 * 7		Control - When set, the WiLink receives all control frames.
 *		When clear, these frames are discarded.
 * 6		Data - When set, the WiLink receives all data frames.
 *		When clear, these frames are discarded.
 * 5		FCS Error - When set, the WiLink receives frames that have FCS
 *		errors.
 *		When clear, these frames are discarded.
 * 4		Management - When set, the WiLink receives all management
 *		frames.
 *		When clear, these frames are discarded.
 * 3		Probe Request - When set, the WiLink receives all probe request
 *		frames.
 *		When clear, these frames are discarded.
 * 2		Probe Response - When set, the WiLink receives all probe
 *		response frames.
 *		When clear, these frames are discarded.
 * 1		RTS/CTS/ACK - When set, the WiLink receives all RTS, CTS and ACK
 *		frames.
 *		When clear, these frames are discarded.
 * 0		Rsvd Type/Sub Type - When set, the WiLink receives all frames
 *		that have reserved frame types and sub types as defined by the
 *		802.11 specification.
 *		When clear, these frames are discarded.
 */
struct acx_rx_config {
	struct acx_header header;

	u32 config_options;
	u32 filter_options;
} __attribute__ ((packed));

struct acx_packet_detection {
	struct acx_header header;

	u32 threshold;
} __attribute__ ((packed));


enum acx_slot_type {
	SLOT_TIME_LONG = 0,
	SLOT_TIME_SHORT = 1,
	DEFAULT_SLOT_TIME = SLOT_TIME_SHORT,
	MAX_SLOT_TIMES = 0xFF
};

#define STATION_WONE_INDEX 0

struct acx_slot {
	struct acx_header header;

	u8 wone_index; /* Reserved */
	u8 slot_time;
	u8 reserved[6];
} __attribute__ ((packed));


#define ADDRESS_GROUP_MAX	(8)
#define ADDRESS_GROUP_MAX_LEN	(ETH_ALEN * ADDRESS_GROUP_MAX)

struct acx_dot11_grp_addr_tbl {
	struct acx_header header;

	u8 enabled;
	u8 num_groups;
	u8 pad[2];
	u8 mac_table[ADDRESS_GROUP_MAX_LEN];
} __attribute__ ((packed));


#define  RX_TIMEOUT_PS_POLL_MIN    0
#define  RX_TIMEOUT_PS_POLL_MAX    (200000)
#define  RX_TIMEOUT_PS_POLL_DEF    (15)
#define  RX_TIMEOUT_UPSD_MIN       0
#define  RX_TIMEOUT_UPSD_MAX       (200000)
#define  RX_TIMEOUT_UPSD_DEF       (15)

struct acx_rx_timeout {
	struct acx_header header;

	/*
	 * The longest time the STA will wait to receive
	 * traffic from the AP after a PS-poll has been
	 * transmitted.
	 */
	u16 ps_poll_timeout;

	/*
	 * The longest time the STA will wait to receive
	 * traffic from the AP after a frame has been sent
	 * from an UPSD enabled queue.
	 */
	u16 upsd_timeout;
} __attribute__ ((packed));

#define RTS_THRESHOLD_MIN              0
#define RTS_THRESHOLD_MAX              4096
#define RTS_THRESHOLD_DEF              2347

struct acx_rts_threshold {
	struct acx_header header;

	u16 threshold;
	u8 pad[2];
} __attribute__ ((packed));

struct acx_beacon_filter_option {
	struct acx_header header;

	u8 enable;

	/*
	 * The number of beacons without the unicast TIM
	 * bit set that the firmware buffers before
	 * signaling the host about ready frames.
	 * When set to 0 and the filter is enabled, beacons
	 * without the unicast TIM bit set are dropped.
	 */
	u8 max_num_beacons;
	u8 pad[2];
} __attribute__ ((packed));

/*
 * ACXBeaconFilterEntry (not 221)
 * Byte Offset     Size (Bytes)    Definition
 * ===========     ============    ==========
 * 0				1               IE identifier
 * 1               1               Treatment bit mask
 *
 * ACXBeaconFilterEntry (221)
 * Byte Offset     Size (Bytes)    Definition
 * ===========     ============    ==========
 * 0               1               IE identifier
 * 1               1               Treatment bit mask
 * 2               3               OUI
 * 5               1               Type
 * 6               2               Version
 *
 *
 * Treatment bit mask - The information element handling:
 * bit 0 - The information element is compared and transferred
 * in case of change.
 * bit 1 - The information element is transferred to the host
 * with each appearance or disappearance.
 * Note that both bits can be set at the same time.
 */
#define	BEACON_FILTER_TABLE_MAX_IE_NUM		       (32)
#define BEACON_FILTER_TABLE_MAX_VENDOR_SPECIFIC_IE_NUM (6)
#define BEACON_FILTER_TABLE_IE_ENTRY_SIZE	       (2)
#define BEACON_FILTER_TABLE_EXTRA_VENDOR_SPECIFIC_IE_SIZE (6)
#define BEACON_FILTER_TABLE_MAX_SIZE ((BEACON_FILTER_TABLE_MAX_IE_NUM * \
			    BEACON_FILTER_TABLE_IE_ENTRY_SIZE) + \
			   (BEACON_FILTER_TABLE_MAX_VENDOR_SPECIFIC_IE_NUM * \
			    BEACON_FILTER_TABLE_EXTRA_VENDOR_SPECIFIC_IE_SIZE))

struct acx_beacon_filter_ie_table {
	struct acx_header header;

	u8 num_ie;
	u8 table[BEACON_FILTER_TABLE_MAX_SIZE];
	u8 pad[3];
} __attribute__ ((packed));

enum {
	SG_ENABLE = 0,
	SG_DISABLE,
	SG_SENSE_NO_ACTIVITY,
	SG_SENSE_ACTIVE
};

struct acx_bt_wlan_coex {
	struct acx_header header;

	/*
	 * 0 -> PTA enabled
	 * 1 -> PTA disabled
	 * 2 -> sense no active mode, i.e.
	 *      an interrupt is sent upon
	 *      BT activity.
	 * 3 -> PTA is switched on in response
	 *      to the interrupt sending.
	 */
	u8 enable;
	u8 pad[3];
} __attribute__ ((packed));

#define PTA_ANTENNA_TYPE_DEF		  (0)
#define PTA_BT_HP_MAXTIME_DEF		  (2000)
#define PTA_WLAN_HP_MAX_TIME_DEF	  (5000)
#define PTA_SENSE_DISABLE_TIMER_DEF	  (1350)
#define PTA_PROTECTIVE_RX_TIME_DEF	  (1500)
#define PTA_PROTECTIVE_TX_TIME_DEF	  (1500)
#define PTA_TIMEOUT_NEXT_BT_LP_PACKET_DEF (3000)
#define PTA_SIGNALING_TYPE_DEF		  (1)
#define PTA_AFH_LEVERAGE_ON_DEF		  (0)
#define PTA_NUMBER_QUIET_CYCLE_DEF	  (0)
#define PTA_MAX_NUM_CTS_DEF		  (3)
#define PTA_NUMBER_OF_WLAN_PACKETS_DEF	  (2)
#define PTA_NUMBER_OF_BT_PACKETS_DEF	  (2)
#define PTA_PROTECTIVE_RX_TIME_FAST_DEF	  (1500)
#define PTA_PROTECTIVE_TX_TIME_FAST_DEF	  (3000)
#define PTA_CYCLE_TIME_FAST_DEF		  (8700)
#define PTA_RX_FOR_AVALANCHE_DEF	  (5)
#define PTA_ELP_HP_DEF			  (0)
#define PTA_ANTI_STARVE_PERIOD_DEF	  (500)
#define PTA_ANTI_STARVE_NUM_CYCLE_DEF	  (4)
#define PTA_ALLOW_PA_SD_DEF		  (1)
#define PTA_TIME_BEFORE_BEACON_DEF	  (6300)
#define PTA_HPDM_MAX_TIME_DEF		  (1600)
#define PTA_TIME_OUT_NEXT_WLAN_DEF	  (2550)
#define PTA_AUTO_MODE_NO_CTS_DEF	  (0)
#define PTA_BT_HP_RESPECTED_DEF		  (3)
#define PTA_WLAN_RX_MIN_RATE_DEF	  (24)
#define PTA_ACK_MODE_DEF		  (1)

struct acx_bt_wlan_coex_param {
	struct acx_header header;

	/*
	 * The minimum rate of a received WLAN packet in the STA,
	 * during protective mode, of which a new BT-HP request
	 * during this Rx will always be respected and gain the antenna.
	 */
	u32 min_rate;

	/* Max time the BT HP will be respected. */
	u16 bt_hp_max_time;

	/* Max time the WLAN HP will be respected. */
	u16 wlan_hp_max_time;

	/*
	 * The time between the last BT activity
	 * and the moment when the sense mode returns
	 * to SENSE_INACTIVE.
	 */
	u16 sense_disable_timer;

	/* Time before the next BT HP instance */
	u16 rx_time_bt_hp;
	u16 tx_time_bt_hp;

	/* range: 10-20000    default: 1500 */
	u16 rx_time_bt_hp_fast;
	u16 tx_time_bt_hp_fast;

	/* range: 2000-65535  default: 8700 */
	u16 wlan_cycle_fast;

	/* range: 0 - 15000 (Msec) default: 1000 */
	u16 bt_anti_starvation_period;

	/* range 400-10000(Usec) default: 3000 */
	u16 next_bt_lp_packet;

	/* Deafult: worst case for BT DH5 traffic */
	u16 wake_up_beacon;

	/* range: 0-50000(Usec) default: 1050 */
	u16 hp_dm_max_guard_time;

	/*
	 * This is to prevent both BT & WLAN antenna
	 * starvation.
	 * Range: 100-50000(Usec) default:2550
	 */
	u16 next_wlan_packet;

	/* 0 -> shared antenna */
	u8 antenna_type;

	/*
	 * 0 -> TI legacy
	 * 1 -> Palau
	 */
	u8 signal_type;

	/*
	 * BT AFH status
	 * 0 -> no AFH
	 * 1 -> from dedicated GPIO
	 * 2 -> AFH on (from host)
	 */
	u8 afh_leverage_on;

	/*
	 * The number of cycles during which no
	 * TX will be sent after 1 cycle of RX
	 * transaction in protective mode
	 */
	u8 quiet_cycle_num;

	/*
	 * The maximum number of CTSs that will
	 * be sent for receiving RX packet in
	 * protective mode
	 */
	u8 max_cts;

	/*
	 * The number of WLAN packets
	 * transferred in common mode before
	 * switching to BT.
	 */
	u8 wlan_packets_num;

	/*
	 * The number of BT packets
	 * transferred in common mode before
	 * switching to WLAN.
	 */
	u8 bt_packets_num;

	/* range: 1-255  default: 5 */
	u8 missed_rx_avalanche;

	/* range: 0-1    default: 1 */
	u8 wlan_elp_hp;

	/* range: 0 - 15  default: 4 */
	u8 bt_anti_starvation_cycles;

	u8 ack_mode_dual_ant;

	/*
	 * Allow PA_SD assertion/de-assertion
	 * during enabled BT activity.
	 */
	u8 pa_sd_enable;

	/*
	 * Enable/Disable PTA in auto mode:
	 * Support Both Active & P.S modes
	 */
	u8 pta_auto_mode_enable;

	/* range: 0 - 20  default: 1 */
	u8 bt_hp_respected_num;
} __attribute__ ((packed));

#define CCA_THRSH_ENABLE_ENERGY_D       0x140A
#define CCA_THRSH_DISABLE_ENERGY_D      0xFFEF

struct acx_energy_detection {
	struct acx_header header;

	/* The RX Clear Channel Assessment threshold in the PHY */
	u16 rx_cca_threshold;
	u8 tx_energy_detection;
	u8 pad;
} __attribute__ ((packed));

#define BCN_RX_TIMEOUT_DEF_VALUE        10000
#define BROADCAST_RX_TIMEOUT_DEF_VALUE  20000
#define RX_BROADCAST_IN_PS_DEF_VALUE    1
#define CONSECUTIVE_PS_POLL_FAILURE_DEF 4

struct acx_beacon_broadcast {
	struct acx_header header;

	u16 beacon_rx_timeout;
	u16 broadcast_timeout;

	/* Enables receiving of broadcast packets in PS mode */
	u8 rx_broadcast_in_ps;

	/* Consecutive PS Poll failures before updating the host */
	u8 ps_poll_threshold;
	u8 pad[2];
} __attribute__ ((packed));

struct acx_event_mask {
	struct acx_header header;

	u32 event_mask;
	u32 high_event_mask; /* Unused */
} __attribute__ ((packed));

#define CFG_RX_FCS		BIT(2)
#define CFG_RX_ALL_GOOD		BIT(3)
#define CFG_UNI_FILTER_EN	BIT(4)
#define CFG_BSSID_FILTER_EN	BIT(5)
#define CFG_MC_FILTER_EN	BIT(6)
#define CFG_MC_ADDR0_EN		BIT(7)
#define CFG_MC_ADDR1_EN		BIT(8)
#define CFG_BC_REJECT_EN	BIT(9)
#define CFG_SSID_FILTER_EN	BIT(10)
#define CFG_RX_INT_FCS_ERROR	BIT(11)
#define CFG_RX_INT_ENCRYPTED	BIT(12)
#define CFG_RX_WR_RX_STATUS	BIT(13)
#define CFG_RX_FILTER_NULTI	BIT(14)
#define CFG_RX_RESERVE		BIT(15)
#define CFG_RX_TIMESTAMP_TSF	BIT(16)

#define CFG_RX_RSV_EN		BIT(0)
#define CFG_RX_RCTS_ACK		BIT(1)
#define CFG_RX_PRSP_EN		BIT(2)
#define CFG_RX_PREQ_EN		BIT(3)
#define CFG_RX_MGMT_EN		BIT(4)
#define CFG_RX_FCS_ERROR	BIT(5)
#define CFG_RX_DATA_EN		BIT(6)
#define CFG_RX_CTL_EN		BIT(7)
#define CFG_RX_CF_EN		BIT(8)
#define CFG_RX_BCN_EN		BIT(9)
#define CFG_RX_AUTH_EN		BIT(10)
#define CFG_RX_ASSOC_EN		BIT(11)

#define SCAN_PASSIVE		BIT(0)
#define SCAN_5GHZ_BAND		BIT(1)
#define SCAN_TRIGGERED		BIT(2)
#define SCAN_PRIORITY_HIGH	BIT(3)

struct acx_feature_config {
	struct acx_header header;

	u32 options;
	u32 data_flow_options;
} __attribute__ ((packed));

struct acx_current_tx_power {
	struct acx_header header;

	u8  current_tx_power;
	u8  padding[3];
} __attribute__ ((packed));

enum acx_wake_up_event {
	WAKE_UP_EVENT_BEACON_BITMAP	= 0x01, /* Wake on every Beacon*/
	WAKE_UP_EVENT_DTIM_BITMAP	= 0x02,	/* Wake on every DTIM*/
	WAKE_UP_EVENT_N_DTIM_BITMAP	= 0x04, /* Wake on every Nth DTIM */
	WAKE_UP_EVENT_N_BEACONS_BITMAP	= 0x08, /* Wake on every Nth Beacon */
	WAKE_UP_EVENT_BITS_MASK		= 0x0F
};

struct acx_wake_up_condition {
	struct acx_header header;

	u8 wake_up_event; /* Only one bit can be set */
	u8 listen_interval;
	u8 pad[2];
} __attribute__ ((packed));

struct acx_aid {
	struct acx_header header;

	/*
	 * To be set when associated with an AP.
	 */
	u16 aid;
	u8 pad[2];
} __attribute__ ((packed));

enum acx_preamble_type {
	ACX_PREAMBLE_LONG = 0,
	ACX_PREAMBLE_SHORT = 1
};

struct acx_preamble {
	struct acx_header header;

	/*
	 * When set, the WiLink transmits the frames with a short preamble and
	 * when cleared, the WiLink transmits the frames with a long preamble.
	 */
	u8 preamble;
	u8 padding[3];
} __attribute__ ((packed));

enum acx_ctsprotect_type {
	CTSPROTECT_DISABLE = 0,
	CTSPROTECT_ENABLE = 1
};

struct acx_ctsprotect {
	struct acx_header header;
	u8 ctsprotect;
	u8 padding[3];
} __attribute__ ((packed));

struct acx_tx_statistics {
	u32 internal_desc_overflow;
}  __attribute__ ((packed));

struct acx_rx_statistics {
	u32 out_of_mem;
	u32 hdr_overflow;
	u32 hw_stuck;
	u32 dropped;
	u32 fcs_err;
	u32 xfr_hint_trig;
	u32 path_reset;
	u32 reset_counter;
} __attribute__ ((packed));

struct acx_dma_statistics {
	u32 rx_requested;
	u32 rx_errors;
	u32 tx_requested;
	u32 tx_errors;
}  __attribute__ ((packed));

struct acx_isr_statistics {
	/* host command complete */
	u32 cmd_cmplt;

	/* fiqisr() */
	u32 fiqs;

	/* (INT_STS_ND & INT_TRIG_RX_HEADER) */
	u32 rx_headers;

	/* (INT_STS_ND & INT_TRIG_RX_CMPLT) */
	u32 rx_completes;

	/* (INT_STS_ND & INT_TRIG_NO_RX_BUF) */
	u32 rx_mem_overflow;

	/* (INT_STS_ND & INT_TRIG_S_RX_RDY) */
	u32 rx_rdys;

	/* irqisr() */
	u32 irqs;

	/* (INT_STS_ND & INT_TRIG_TX_PROC) */
	u32 tx_procs;

	/* (INT_STS_ND & INT_TRIG_DECRYPT_DONE) */
	u32 decrypt_done;

	/* (INT_STS_ND & INT_TRIG_DMA0) */
	u32 dma0_done;

	/* (INT_STS_ND & INT_TRIG_DMA1) */
	u32 dma1_done;

	/* (INT_STS_ND & INT_TRIG_TX_EXC_CMPLT) */
	u32 tx_exch_complete;

	/* (INT_STS_ND & INT_TRIG_COMMAND) */
	u32 commands;

	/* (INT_STS_ND & INT_TRIG_RX_PROC) */
	u32 rx_procs;

	/* (INT_STS_ND & INT_TRIG_PM_802) */
	u32 hw_pm_mode_changes;

	/* (INT_STS_ND & INT_TRIG_ACKNOWLEDGE) */
	u32 host_acknowledges;

	/* (INT_STS_ND & INT_TRIG_PM_PCI) */
	u32 pci_pm;

	/* (INT_STS_ND & INT_TRIG_ACM_WAKEUP) */
	u32 wakeups;

	/* (INT_STS_ND & INT_TRIG_LOW_RSSI) */
	u32 low_rssi;
} __attribute__ ((packed));

struct acx_wep_statistics {
	/* WEP address keys configured */
	u32 addr_key_count;

	/* default keys configured */
	u32 default_key_count;

	u32 reserved;

	/* number of times that WEP key not found on lookup */
	u32 key_not_found;

	/* number of times that WEP key decryption failed */
	u32 decrypt_fail;

	/* WEP packets decrypted */
	u32 packets;

	/* WEP decrypt interrupts */
	u32 interrupt;
} __attribute__ ((packed));

#define ACX_MISSED_BEACONS_SPREAD 10

struct acx_pwr_statistics {
	/* the amount of enters into power save mode (both PD & ELP) */
	u32 ps_enter;

	/* the amount of enters into ELP mode */
	u32 elp_enter;

	/* the amount of missing beacon interrupts to the host */
	u32 missing_bcns;

	/* the amount of wake on host-access times */
	u32 wake_on_host;

	/* the amount of wake on timer-expire */
	u32 wake_on_timer_exp;

	/* the number of packets that were transmitted with PS bit set */
	u32 tx_with_ps;

	/* the number of packets that were transmitted with PS bit clear */
	u32 tx_without_ps;

	/* the number of received beacons */
	u32 rcvd_beacons;

	/* the number of entering into PowerOn (power save off) */
	u32 power_save_off;

	/* the number of entries into power save mode */
	u16 enable_ps;

	/*
	 * the number of exits from power save, not including failed PS
	 * transitions
	 */
	u16 disable_ps;

	/*
	 * the number of times the TSF counter was adjusted because
	 * of drift
	 */
	u32 fix_tsf_ps;

	/* Gives statistics about the spread continuous missed beacons.
	 * The 16 LSB are dedicated for the PS mode.
	 * The 16 MSB are dedicated for the PS mode.
	 * cont_miss_bcns_spread[0] - single missed beacon.
	 * cont_miss_bcns_spread[1] - two continuous missed beacons.
	 * cont_miss_bcns_spread[2] - three continuous missed beacons.
	 * ...
	 * cont_miss_bcns_spread[9] - ten and more continuous missed beacons.
	*/
	u32 cont_miss_bcns_spread[ACX_MISSED_BEACONS_SPREAD];

	/* the number of beacons in awake mode */
	u32 rcvd_awake_beacons;
} __attribute__ ((packed));

struct acx_mic_statistics {
	u32 rx_pkts;
	u32 calc_failure;
} __attribute__ ((packed));

struct acx_aes_statistics {
	u32 encrypt_fail;
	u32 decrypt_fail;
	u32 encrypt_packets;
	u32 decrypt_packets;
	u32 encrypt_interrupt;
	u32 decrypt_interrupt;
} __attribute__ ((packed));

struct acx_event_statistics {
	u32 heart_beat;
	u32 calibration;
	u32 rx_mismatch;
	u32 rx_mem_empty;
	u32 rx_pool;
	u32 oom_late;
	u32 phy_transmit_error;
	u32 tx_stuck;
} __attribute__ ((packed));

struct acx_ps_statistics {
	u32 pspoll_timeouts;
	u32 upsd_timeouts;
	u32 upsd_max_sptime;
	u32 upsd_max_apturn;
	u32 pspoll_max_apturn;
	u32 pspoll_utilization;
	u32 upsd_utilization;
} __attribute__ ((packed));

struct acx_rxpipe_statistics {
	u32 rx_prep_beacon_drop;
	u32 descr_host_int_trig_rx_data;
	u32 beacon_buffer_thres_host_int_trig_rx_data;
	u32 missed_beacon_host_int_trig_rx_data;
	u32 tx_xfr_host_int_trig_rx_data;
} __attribute__ ((packed));

struct acx_statistics {
	struct acx_header header;

	struct acx_tx_statistics tx;
	struct acx_rx_statistics rx;
	struct acx_dma_statistics dma;
	struct acx_isr_statistics isr;
	struct acx_wep_statistics wep;
	struct acx_pwr_statistics pwr;
	struct acx_aes_statistics aes;
	struct acx_mic_statistics mic;
	struct acx_event_statistics event;
	struct acx_ps_statistics ps;
	struct acx_rxpipe_statistics rxpipe;
} __attribute__ ((packed));

#define ACX_MAX_RATE_CLASSES       8
#define ACX_RATE_MASK_UNSPECIFIED  0
#define ACX_RATE_MASK_ALL          0x1eff
#define ACX_RATE_RETRY_LIMIT       10

struct acx_rate_class {
	u32 enabled_rates;
	u8 short_retry_limit;
	u8 long_retry_limit;
	u8 aflags;
	u8 reserved;
};

struct acx_rate_policy {
	struct acx_header header;

	u32 rate_class_cnt;
	struct acx_rate_class rate_class[ACX_MAX_RATE_CLASSES];
} __attribute__ ((packed));

#define WL1271_ACX_AC_COUNT 4

struct acx_ac_cfg {
	struct acx_header header;
	u8 ac;
	u8 cw_min;
	u16 cw_max;
	u8 aifsn;
	u8 reserved;
	u16 tx_op_limit;
} __attribute__ ((packed));

enum wl1271_acx_ac {
	WL1271_ACX_AC_BE = 0,
	WL1271_ACX_AC_BK = 1,
	WL1271_ACX_AC_VI = 2,
	WL1271_ACX_AC_VO = 3,
	WL1271_ACX_AC_CTS2SELF = 4,
	WL1271_ACX_AC_ANY_TID = 0x1F,
	WL1271_ACX_AC_INVALID = 0xFF,
};

enum wl1271_acx_ps_scheme {
	WL1271_ACX_PS_SCHEME_LEGACY = 0,
	WL1271_ACX_PS_SCHEME_UPSD_TRIGGER = 1,
	WL1271_ACX_PS_SCHEME_LEGACY_PSPOLL = 2,
	WL1271_ACX_PS_SCHEME_SAPSD = 3,
};

enum wl1271_acx_ack_policy {
	WL1271_ACX_ACK_POLICY_LEGACY = 0,
	WL1271_ACX_ACK_POLICY_NO_ACK = 1,
	WL1271_ACX_ACK_POLICY_BLOCK = 2,
};

#define WL1271_ACX_TID_COUNT 7

struct acx_tid_config {
	struct acx_header header;
	u8 queue_id;
	u8 channel_type;
	u8 tsid;
	u8 ps_scheme;
	u8 ack_policy;
	u8 padding[3];
	u32 apsd_conf[2];
} __attribute__ ((packed));

struct acx_frag_threshold {
	struct acx_header header;
	u16 frag_threshold;
	u8 padding[2];
} __attribute__ ((packed));

#define WL1271_ACX_TX_COMPL_TIMEOUT   5
#define WL1271_ACX_TX_COMPL_THRESHOLD 5

struct acx_tx_config_options {
	struct acx_header header;
	u16 tx_compl_timeout;     /* msec */
	u16 tx_compl_threshold;   /* number of packets */
} __attribute__ ((packed));

#define ACX_RX_MEM_BLOCKS     64
#define ACX_TX_MIN_MEM_BLOCKS 64
#define ACX_TX_DESCRIPTORS    32
#define ACX_NUM_SSID_PROFILES 1

struct wl1271_acx_config_memory {
	struct acx_header header;

	u8 rx_mem_block_num;
	u8 tx_min_mem_block_num;
	u8 num_stations;
	u8 num_ssid_profiles;
	u32 total_tx_descriptors;
} __attribute__ ((packed));

struct wl1271_acx_mem_map {
	struct acx_header header;

	void *code_start;
	void *code_end;

	void *wep_defkey_start;
	void *wep_defkey_end;

	void *sta_table_start;
	void *sta_table_end;

	void *packet_template_start;
	void *packet_template_end;

	/* Address of the TX result interface (control block) */
	u32 tx_result;
	u32 tx_result_queue_start;

	void *queue_memory_start;
	void *queue_memory_end;

	u32 packet_memory_pool_start;
	u32 packet_memory_pool_end;

	void *debug_buffer1_start;
	void *debug_buffer1_end;

	void *debug_buffer2_start;
	void *debug_buffer2_end;

	/* Number of blocks FW allocated for TX packets */
	u32 num_tx_mem_blocks;

	/* Number of blocks FW allocated for RX packets */
	u32 num_rx_mem_blocks;

	/* the following 4 fields are valid in SLAVE mode only */
	u8 *tx_cbuf;
	u8 *rx_cbuf;
	void *rx_ctrl;
	void *tx_ctrl;
} __attribute__ ((packed));

enum wl1271_acx_rx_queue_type {
	RX_QUEUE_TYPE_RX_LOW_PRIORITY,    /* All except the high priority */
	RX_QUEUE_TYPE_RX_HIGH_PRIORITY,   /* Management and voice packets */
	RX_QUEUE_TYPE_NUM,
	RX_QUEUE_TYPE_MAX = USHORT_MAX
};

#define WL1271_RX_INTR_THRESHOLD_DEF  0       /* no pacing, send interrupt on
					       * every event */
#define WL1271_RX_INTR_THRESHOLD_MIN  0
#define WL1271_RX_INTR_THRESHOLD_MAX  15

#define WL1271_RX_INTR_TIMEOUT_DEF    5
#define WL1271_RX_INTR_TIMEOUT_MIN    1
#define WL1271_RX_INTR_TIMEOUT_MAX    100

struct wl1271_acx_rx_config_opt {
	struct acx_header header;

	u16 mblk_threshold;
	u16 threshold;
	u16 timeout;
	u8 queue_type;
	u8 reserved;
} __attribute__ ((packed));

enum {
	ACX_WAKE_UP_CONDITIONS      = 0x0002,
	ACX_MEM_CFG                 = 0x0003,
	ACX_SLOT                    = 0x0004,
	ACX_AC_CFG                  = 0x0007,
	ACX_MEM_MAP                 = 0x0008,
	ACX_AID                     = 0x000A,
	/* ACX_FW_REV is missing in the ref driver, but seems to work */
	ACX_FW_REV                  = 0x000D,
	ACX_MEDIUM_USAGE            = 0x000F,
	ACX_RX_CFG                  = 0x0010,
	ACX_TX_QUEUE_CFG            = 0x0011, /* FIXME: only used by wl1251 */
	ACX_STATISTICS              = 0x0013, /* Debug API */
	ACX_PWR_CONSUMPTION_STATISTICS = 0x0014,
	ACX_FEATURE_CFG             = 0x0015,
	ACX_TID_CFG                 = 0x001A,
	ACX_PS_RX_STREAMING         = 0x001B,
	ACX_BEACON_FILTER_OPT       = 0x001F,
	ACX_NOISE_HIST              = 0x0021,
	ACX_HDK_VERSION             = 0x0022, /* ??? */
	ACX_PD_THRESHOLD            = 0x0023,
	ACX_TX_CONFIG_OPT           = 0x0024,
	ACX_CCA_THRESHOLD           = 0x0025,
	ACX_EVENT_MBOX_MASK         = 0x0026,
	ACX_CONN_MONIT_PARAMS       = 0x002D,
	ACX_CONS_TX_FAILURE         = 0x002F,
	ACX_BCN_DTIM_OPTIONS        = 0x0031,
	ACX_SG_ENABLE               = 0x0032,
	ACX_SG_CFG                  = 0x0033,
	ACX_BEACON_FILTER_TABLE     = 0x0038,
	ACX_ARP_IP_FILTER           = 0x0039,
	ACX_ROAMING_STATISTICS_TBL  = 0x003B,
	ACX_RATE_POLICY             = 0x003D,
	ACX_CTS_PROTECTION          = 0x003E,
	ACX_SLEEP_AUTH              = 0x003F,
	ACX_PREAMBLE_TYPE	    = 0x0040,
	ACX_ERROR_CNT               = 0x0041,
	ACX_IBSS_FILTER		    = 0x0044,
	ACX_SERVICE_PERIOD_TIMEOUT  = 0x0045,
	ACX_TSF_INFO                = 0x0046,
	ACX_CONFIG_PS_WMM           = 0x0049,
	ACX_ENABLE_RX_DATA_FILTER   = 0x004A,
	ACX_SET_RX_DATA_FILTER      = 0x004B,
	ACX_GET_DATA_FILTER_STATISTICS = 0x004C,
	ACX_RX_CONFIG_OPT           = 0x004E,
	ACX_FRAG_CFG                = 0x004F,
	ACX_BET_ENABLE              = 0x0050,
	ACX_RSSI_SNR_TRIGGER        = 0x0051,
	ACX_RSSI_SNR_WEIGHTS        = 0x0051,
	ACX_KEEP_ALIVE_MODE         = 0x0052,
	ACX_SET_KEEP_ALIVE_CONFIG   = 0x0054,
	ACX_BA_SESSION_RESPONDER_POLICY = 0x0055,
	ACX_BA_SESSION_INITIATOR_POLICY = 0x0056,
	ACX_PEER_HT_CAP             = 0x0057,
	ACX_HT_BSS_OPERATION        = 0x0058,
	ACX_COEX_ACTIVITY           = 0x0059,
	DOT11_RX_MSDU_LIFE_TIME     = 0x1004,
	DOT11_CUR_TX_PWR            = 0x100D,
	DOT11_RX_DOT11_MODE         = 0x1012,
	DOT11_RTS_THRESHOLD         = 0x1013,
	DOT11_GROUP_ADDRESS_TBL     = 0x1014,

	MAX_DOT11_IE = DOT11_GROUP_ADDRESS_TBL,

	MAX_IE = 0xFFFF
};


int wl1271_acx_wake_up_conditions(struct wl1271 *wl, u8 wake_up_event,
				  u8 listen_interval);
int wl1271_acx_sleep_auth(struct wl1271 *wl, u8 sleep_auth);
int wl1271_acx_fw_version(struct wl1271 *wl, char *buf, size_t len);
int wl1271_acx_tx_power(struct wl1271 *wl, int power);
int wl1271_acx_feature_cfg(struct wl1271 *wl);
int wl1271_acx_mem_map(struct wl1271 *wl,
		       struct acx_header *mem_map, size_t len);
int wl1271_acx_rx_msdu_life_time(struct wl1271 *wl, u32 life_time);
int wl1271_acx_rx_config(struct wl1271 *wl, u32 config, u32 filter);
int wl1271_acx_pd_threshold(struct wl1271 *wl);
int wl1271_acx_slot(struct wl1271 *wl, enum acx_slot_type slot_time);
int wl1271_acx_group_address_tbl(struct wl1271 *wl);
int wl1271_acx_service_period_timeout(struct wl1271 *wl);
int wl1271_acx_rts_threshold(struct wl1271 *wl, u16 rts_threshold);
int wl1271_acx_beacon_filter_opt(struct wl1271 *wl);
int wl1271_acx_beacon_filter_table(struct wl1271 *wl);
int wl1271_acx_sg_enable(struct wl1271 *wl);
int wl1271_acx_sg_cfg(struct wl1271 *wl);
int wl1271_acx_cca_threshold(struct wl1271 *wl);
int wl1271_acx_bcn_dtim_options(struct wl1271 *wl);
int wl1271_acx_aid(struct wl1271 *wl, u16 aid);
int wl1271_acx_event_mbox_mask(struct wl1271 *wl, u32 event_mask);
int wl1271_acx_set_preamble(struct wl1271 *wl, enum acx_preamble_type preamble);
int wl1271_acx_cts_protect(struct wl1271 *wl,
			    enum acx_ctsprotect_type ctsprotect);
int wl1271_acx_statistics(struct wl1271 *wl, struct acx_statistics *stats);
int wl1271_acx_rate_policies(struct wl1271 *wl);
int wl1271_acx_ac_cfg(struct wl1271 *wl);
int wl1271_acx_tid_cfg(struct wl1271 *wl);
int wl1271_acx_frag_threshold(struct wl1271 *wl);
int wl1271_acx_tx_config_options(struct wl1271 *wl);
int wl1271_acx_mem_cfg(struct wl1271 *wl);
int wl1271_acx_init_mem_config(struct wl1271 *wl);
int wl1271_acx_init_rx_interrupt(struct wl1271 *wl);

#endif /* __WL1271_ACX_H__ */

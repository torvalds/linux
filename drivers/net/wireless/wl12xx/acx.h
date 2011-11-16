/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2010 Nokia Corporation
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

#ifndef __ACX_H__
#define __ACX_H__

#include "wl12xx.h"
#include "cmd.h"

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
/* Trace message on MBOX #A */
#define WL1271_ACX_INTR_TRACE_A            BIT(7)
/* Trace message on MBOX #B */
#define WL1271_ACX_INTR_TRACE_B            BIT(8)

#define WL1271_ACX_INTR_ALL		   0xFFFFFFFF
#define WL1271_ACX_ALL_EVENTS_VECTOR       (WL1271_ACX_INTR_WATCHDOG      | \
					    WL1271_ACX_INTR_INIT_COMPLETE | \
					    WL1271_ACX_INTR_EVENT_A       | \
					    WL1271_ACX_INTR_EVENT_B       | \
					    WL1271_ACX_INTR_CMD_COMPLETE  | \
					    WL1271_ACX_INTR_HW_AVAILABLE  | \
					    WL1271_ACX_INTR_DATA)

#define WL1271_INTR_MASK                   (WL1271_ACX_INTR_WATCHDOG     | \
					    WL1271_ACX_INTR_EVENT_A      | \
					    WL1271_ACX_INTR_EVENT_B      | \
					    WL1271_ACX_INTR_HW_AVAILABLE | \
					    WL1271_ACX_INTR_DATA)

/* Target's information element */
struct acx_header {
	struct wl1271_cmd_header cmd;

	/* acx (or information element) header */
	__le16 id;

	/* payload length (not including headers */
	__le16 len;
} __packed;

struct acx_error_counter {
	struct acx_header header;

	/* The number of PLCP errors since the last time this */
	/* information element was interrogated. This field is */
	/* automatically cleared when it is interrogated.*/
	__le32 PLCP_error;

	/* The number of FCS errors since the last time this */
	/* information element was interrogated. This field is */
	/* automatically cleared when it is interrogated.*/
	__le32 FCS_error;

	/* The number of MPDUs without PLCP header errors received*/
	/* since the last time this information element was interrogated. */
	/* This field is automatically cleared when it is interrogated.*/
	__le32 valid_frame;

	/* the number of missed sequence numbers in the squentially */
	/* values of frames seq numbers */
	__le32 seq_num_miss;
} __packed;

enum wl12xx_role {
	WL1271_ROLE_STA = 0,
	WL1271_ROLE_IBSS,
	WL1271_ROLE_AP,
	WL1271_ROLE_DEVICE,
	WL1271_ROLE_P2P_CL,
	WL1271_ROLE_P2P_GO,

	WL12XX_INVALID_ROLE_TYPE = 0xff
};

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
} __packed;

enum {
	HOSTIF_PCI_MASTER_HOST_INDIRECT,
	HOSTIF_PCI_MASTER_HOST_DIRECT,
	HOSTIF_SLAVE,
	HOSTIF_PKT_RING,
	HOSTIF_DONTCARE = 0xFF
};

#define DEFAULT_UCAST_PRIORITY          0
#define DEFAULT_RX_Q_PRIORITY           0
#define DEFAULT_RXQ_PRIORITY            0 /* low 0 .. 15 high  */
#define DEFAULT_RXQ_TYPE                0x07    /* All frames, Data/Ctrl/Mgmt */
#define TRACE_BUFFER_MAX_SIZE           256

#define  DP_RX_PACKET_RING_CHUNK_SIZE 1600
#define  DP_TX_PACKET_RING_CHUNK_SIZE 1600
#define  DP_RX_PACKET_RING_CHUNK_NUM 2
#define  DP_TX_PACKET_RING_CHUNK_NUM 2
#define  DP_TX_COMPLETE_TIME_OUT 20

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
	__le32 lifetime;
} __packed;

struct acx_packet_detection {
	struct acx_header header;

	__le32 threshold;
} __packed;


enum acx_slot_type {
	SLOT_TIME_LONG = 0,
	SLOT_TIME_SHORT = 1,
	DEFAULT_SLOT_TIME = SLOT_TIME_SHORT,
	MAX_SLOT_TIMES = 0xFF
};

#define STATION_WONE_INDEX 0

struct acx_slot {
	struct acx_header header;

	u8 role_id;
	u8 wone_index; /* Reserved */
	u8 slot_time;
	u8 reserved[5];
} __packed;


#define ACX_MC_ADDRESS_GROUP_MAX	(8)
#define ADDRESS_GROUP_MAX_LEN	        (ETH_ALEN * ACX_MC_ADDRESS_GROUP_MAX)

struct acx_dot11_grp_addr_tbl {
	struct acx_header header;

	u8 role_id;
	u8 enabled;
	u8 num_groups;
	u8 pad[1];
	u8 mac_table[ADDRESS_GROUP_MAX_LEN];
} __packed;

struct acx_rx_timeout {
	struct acx_header header;

	u8 role_id;
	u8 reserved;
	__le16 ps_poll_timeout;
	__le16 upsd_timeout;
	u8 padding[2];
} __packed;

struct acx_rts_threshold {
	struct acx_header header;

	u8 role_id;
	u8 reserved;
	__le16 threshold;
} __packed;

struct acx_beacon_filter_option {
	struct acx_header header;

	u8 role_id;
	u8 enable;
	/*
	 * The number of beacons without the unicast TIM
	 * bit set that the firmware buffers before
	 * signaling the host about ready frames.
	 * When set to 0 and the filter is enabled, beacons
	 * without the unicast TIM bit set are dropped.
	 */
	u8 max_num_beacons;
	u8 pad[1];
} __packed;

/*
 * ACXBeaconFilterEntry (not 221)
 * Byte Offset     Size (Bytes)    Definition
 * ===========     ============    ==========
 * 0               1               IE identifier
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

	u8 role_id;
	u8 num_ie;
	u8 pad[2];
	u8 table[BEACON_FILTER_TABLE_MAX_SIZE];
} __packed;

struct acx_conn_monit_params {
       struct acx_header header;

	   u8 role_id;
	   u8 padding[3];
       __le32 synch_fail_thold; /* number of beacons missed */
       __le32 bss_lose_timeout; /* number of TU's from synch fail */
} __packed;

struct acx_bt_wlan_coex {
	struct acx_header header;

	u8 enable;
	u8 pad[3];
} __packed;

struct acx_bt_wlan_coex_param {
	struct acx_header header;

	__le32 params[CONF_SG_PARAMS_MAX];
	u8 param_idx;
	u8 padding[3];
} __packed;

struct acx_dco_itrim_params {
	struct acx_header header;

	u8 enable;
	u8 padding[3];
	__le32 timeout;
} __packed;

struct acx_energy_detection {
	struct acx_header header;

	/* The RX Clear Channel Assessment threshold in the PHY */
	__le16 rx_cca_threshold;
	u8 tx_energy_detection;
	u8 pad;
} __packed;

struct acx_beacon_broadcast {
	struct acx_header header;

	u8 role_id;
	/* Enables receiving of broadcast packets in PS mode */
	u8 rx_broadcast_in_ps;

	__le16 beacon_rx_timeout;
	__le16 broadcast_timeout;

	/* Consecutive PS Poll failures before updating the host */
	u8 ps_poll_threshold;
	u8 pad[1];
} __packed;

struct acx_event_mask {
	struct acx_header header;

	__le32 event_mask;
	__le32 high_event_mask; /* Unused */
} __packed;

#define SCAN_PASSIVE		BIT(0)
#define SCAN_5GHZ_BAND		BIT(1)
#define SCAN_TRIGGERED		BIT(2)
#define SCAN_PRIORITY_HIGH	BIT(3)

/* When set, disable HW encryption */
#define DF_ENCRYPTION_DISABLE      0x01
#define DF_SNIFF_MODE_ENABLE       0x80

struct acx_feature_config {
	struct acx_header header;

	u8 role_id;
	u8 padding[3];
	__le32 options;
	__le32 data_flow_options;
} __packed;

struct acx_current_tx_power {
	struct acx_header header;

	u8  role_id;
	u8  current_tx_power;
	u8  padding[2];
} __packed;

struct acx_wake_up_condition {
	struct acx_header header;

	u8 role_id;
	u8 wake_up_event; /* Only one bit can be set */
	u8 listen_interval;
	u8 pad[1];
} __packed;

struct acx_aid {
	struct acx_header header;

	/*
	 * To be set when associated with an AP.
	 */
	u8 role_id;
	u8 reserved;
	__le16 aid;
} __packed;

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
	u8 role_id;
	u8 preamble;
	u8 padding[2];
} __packed;

enum acx_ctsprotect_type {
	CTSPROTECT_DISABLE = 0,
	CTSPROTECT_ENABLE = 1
};

struct acx_ctsprotect {
	struct acx_header header;
	u8 role_id;
	u8 ctsprotect;
	u8 padding[2];
} __packed;

struct acx_tx_statistics {
	__le32 internal_desc_overflow;
}  __packed;

struct acx_rx_statistics {
	__le32 out_of_mem;
	__le32 hdr_overflow;
	__le32 hw_stuck;
	__le32 dropped;
	__le32 fcs_err;
	__le32 xfr_hint_trig;
	__le32 path_reset;
	__le32 reset_counter;
} __packed;

struct acx_dma_statistics {
	__le32 rx_requested;
	__le32 rx_errors;
	__le32 tx_requested;
	__le32 tx_errors;
}  __packed;

struct acx_isr_statistics {
	/* host command complete */
	__le32 cmd_cmplt;

	/* fiqisr() */
	__le32 fiqs;

	/* (INT_STS_ND & INT_TRIG_RX_HEADER) */
	__le32 rx_headers;

	/* (INT_STS_ND & INT_TRIG_RX_CMPLT) */
	__le32 rx_completes;

	/* (INT_STS_ND & INT_TRIG_NO_RX_BUF) */
	__le32 rx_mem_overflow;

	/* (INT_STS_ND & INT_TRIG_S_RX_RDY) */
	__le32 rx_rdys;

	/* irqisr() */
	__le32 irqs;

	/* (INT_STS_ND & INT_TRIG_TX_PROC) */
	__le32 tx_procs;

	/* (INT_STS_ND & INT_TRIG_DECRYPT_DONE) */
	__le32 decrypt_done;

	/* (INT_STS_ND & INT_TRIG_DMA0) */
	__le32 dma0_done;

	/* (INT_STS_ND & INT_TRIG_DMA1) */
	__le32 dma1_done;

	/* (INT_STS_ND & INT_TRIG_TX_EXC_CMPLT) */
	__le32 tx_exch_complete;

	/* (INT_STS_ND & INT_TRIG_COMMAND) */
	__le32 commands;

	/* (INT_STS_ND & INT_TRIG_RX_PROC) */
	__le32 rx_procs;

	/* (INT_STS_ND & INT_TRIG_PM_802) */
	__le32 hw_pm_mode_changes;

	/* (INT_STS_ND & INT_TRIG_ACKNOWLEDGE) */
	__le32 host_acknowledges;

	/* (INT_STS_ND & INT_TRIG_PM_PCI) */
	__le32 pci_pm;

	/* (INT_STS_ND & INT_TRIG_ACM_WAKEUP) */
	__le32 wakeups;

	/* (INT_STS_ND & INT_TRIG_LOW_RSSI) */
	__le32 low_rssi;
} __packed;

struct acx_wep_statistics {
	/* WEP address keys configured */
	__le32 addr_key_count;

	/* default keys configured */
	__le32 default_key_count;

	__le32 reserved;

	/* number of times that WEP key not found on lookup */
	__le32 key_not_found;

	/* number of times that WEP key decryption failed */
	__le32 decrypt_fail;

	/* WEP packets decrypted */
	__le32 packets;

	/* WEP decrypt interrupts */
	__le32 interrupt;
} __packed;

#define ACX_MISSED_BEACONS_SPREAD 10

struct acx_pwr_statistics {
	/* the amount of enters into power save mode (both PD & ELP) */
	__le32 ps_enter;

	/* the amount of enters into ELP mode */
	__le32 elp_enter;

	/* the amount of missing beacon interrupts to the host */
	__le32 missing_bcns;

	/* the amount of wake on host-access times */
	__le32 wake_on_host;

	/* the amount of wake on timer-expire */
	__le32 wake_on_timer_exp;

	/* the number of packets that were transmitted with PS bit set */
	__le32 tx_with_ps;

	/* the number of packets that were transmitted with PS bit clear */
	__le32 tx_without_ps;

	/* the number of received beacons */
	__le32 rcvd_beacons;

	/* the number of entering into PowerOn (power save off) */
	__le32 power_save_off;

	/* the number of entries into power save mode */
	__le16 enable_ps;

	/*
	 * the number of exits from power save, not including failed PS
	 * transitions
	 */
	__le16 disable_ps;

	/*
	 * the number of times the TSF counter was adjusted because
	 * of drift
	 */
	__le32 fix_tsf_ps;

	/* Gives statistics about the spread continuous missed beacons.
	 * The 16 LSB are dedicated for the PS mode.
	 * The 16 MSB are dedicated for the PS mode.
	 * cont_miss_bcns_spread[0] - single missed beacon.
	 * cont_miss_bcns_spread[1] - two continuous missed beacons.
	 * cont_miss_bcns_spread[2] - three continuous missed beacons.
	 * ...
	 * cont_miss_bcns_spread[9] - ten and more continuous missed beacons.
	*/
	__le32 cont_miss_bcns_spread[ACX_MISSED_BEACONS_SPREAD];

	/* the number of beacons in awake mode */
	__le32 rcvd_awake_beacons;
} __packed;

struct acx_mic_statistics {
	__le32 rx_pkts;
	__le32 calc_failure;
} __packed;

struct acx_aes_statistics {
	__le32 encrypt_fail;
	__le32 decrypt_fail;
	__le32 encrypt_packets;
	__le32 decrypt_packets;
	__le32 encrypt_interrupt;
	__le32 decrypt_interrupt;
} __packed;

struct acx_event_statistics {
	__le32 heart_beat;
	__le32 calibration;
	__le32 rx_mismatch;
	__le32 rx_mem_empty;
	__le32 rx_pool;
	__le32 oom_late;
	__le32 phy_transmit_error;
	__le32 tx_stuck;
} __packed;

struct acx_ps_statistics {
	__le32 pspoll_timeouts;
	__le32 upsd_timeouts;
	__le32 upsd_max_sptime;
	__le32 upsd_max_apturn;
	__le32 pspoll_max_apturn;
	__le32 pspoll_utilization;
	__le32 upsd_utilization;
} __packed;

struct acx_rxpipe_statistics {
	__le32 rx_prep_beacon_drop;
	__le32 descr_host_int_trig_rx_data;
	__le32 beacon_buffer_thres_host_int_trig_rx_data;
	__le32 missed_beacon_host_int_trig_rx_data;
	__le32 tx_xfr_host_int_trig_rx_data;
} __packed;

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
} __packed;

struct acx_rate_class {
	__le32 enabled_rates;
	u8 short_retry_limit;
	u8 long_retry_limit;
	u8 aflags;
	u8 reserved;
};

#define ACX_TX_BASIC_RATE      0
#define ACX_TX_AP_FULL_RATE    1
#define ACX_TX_BASIC_RATE_P2P  2
#define ACX_TX_AP_MODE_MGMT_RATE 4
#define ACX_TX_AP_MODE_BCST_RATE 5
struct acx_rate_policy {
	struct acx_header header;

	__le32 rate_policy_idx;
	struct acx_rate_class rate_policy;
} __packed;

struct acx_ac_cfg {
	struct acx_header header;
	u8 role_id;
	u8 ac;
	u8 aifsn;
	u8 cw_min;
	__le16 cw_max;
	__le16 tx_op_limit;
} __packed;

struct acx_tid_config {
	struct acx_header header;
	u8 role_id;
	u8 queue_id;
	u8 channel_type;
	u8 tsid;
	u8 ps_scheme;
	u8 ack_policy;
	u8 padding[2];
	__le32 apsd_conf[2];
} __packed;

struct acx_frag_threshold {
	struct acx_header header;
	__le16 frag_threshold;
	u8 padding[2];
} __packed;

struct acx_tx_config_options {
	struct acx_header header;
	__le16 tx_compl_timeout;     /* msec */
	__le16 tx_compl_threshold;   /* number of packets */
} __packed;

struct wl12xx_acx_config_memory {
	struct acx_header header;

	u8 rx_mem_block_num;
	u8 tx_min_mem_block_num;
	u8 num_stations;
	u8 num_ssid_profiles;
	__le32 total_tx_descriptors;
	u8 dyn_mem_enable;
	u8 tx_free_req;
	u8 rx_free_req;
	u8 tx_min;
	u8 fwlog_blocks;
	u8 padding[3];
} __packed;

struct wl1271_acx_mem_map {
	struct acx_header header;

	__le32 code_start;
	__le32 code_end;

	__le32 wep_defkey_start;
	__le32 wep_defkey_end;

	__le32 sta_table_start;
	__le32 sta_table_end;

	__le32 packet_template_start;
	__le32 packet_template_end;

	/* Address of the TX result interface (control block) */
	__le32 tx_result;
	__le32 tx_result_queue_start;

	__le32 queue_memory_start;
	__le32 queue_memory_end;

	__le32 packet_memory_pool_start;
	__le32 packet_memory_pool_end;

	__le32 debug_buffer1_start;
	__le32 debug_buffer1_end;

	__le32 debug_buffer2_start;
	__le32 debug_buffer2_end;

	/* Number of blocks FW allocated for TX packets */
	__le32 num_tx_mem_blocks;

	/* Number of blocks FW allocated for RX packets */
	__le32 num_rx_mem_blocks;

	/* the following 4 fields are valid in SLAVE mode only */
	u8 *tx_cbuf;
	u8 *rx_cbuf;
	__le32 rx_ctrl;
	__le32 tx_ctrl;
} __packed;

struct wl1271_acx_rx_config_opt {
	struct acx_header header;

	__le16 mblk_threshold;
	__le16 threshold;
	__le16 timeout;
	u8 queue_type;
	u8 reserved;
} __packed;


struct wl1271_acx_bet_enable {
	struct acx_header header;

	u8 role_id;
	u8 enable;
	u8 max_consecutive;
	u8 padding[1];
} __packed;

#define ACX_IPV4_VERSION 4
#define ACX_IPV6_VERSION 6
#define ACX_IPV4_ADDR_SIZE 4

/* bitmap of enabled arp_filter features */
#define ACX_ARP_FILTER_ARP_FILTERING	BIT(0)
#define ACX_ARP_FILTER_AUTO_ARP		BIT(1)

struct wl1271_acx_arp_filter {
	struct acx_header header;
	u8 role_id;
	u8 version;         /* ACX_IPV4_VERSION, ACX_IPV6_VERSION */
	u8 enable;          /* bitmap of enabled ARP filtering features */
	u8 padding[1];
	u8 address[16];     /* The configured device IP address - all ARP
			       requests directed to this IP address will pass
			       through. For IPv4, the first four bytes are
			       used. */
} __packed;

struct wl1271_acx_pm_config {
	struct acx_header header;

	__le32 host_clk_settling_time;
	u8 host_fast_wakeup_support;
	u8 padding[3];
} __packed;

struct wl1271_acx_keep_alive_mode {
	struct acx_header header;

	u8 role_id;
	u8 enabled;
	u8 padding[2];
} __packed;

enum {
	ACX_KEEP_ALIVE_NO_TX = 0,
	ACX_KEEP_ALIVE_PERIOD_ONLY
};

enum {
	ACX_KEEP_ALIVE_TPL_INVALID = 0,
	ACX_KEEP_ALIVE_TPL_VALID
};

struct wl1271_acx_keep_alive_config {
	struct acx_header header;

	u8 role_id;
	u8 index;
	u8 tpl_validation;
	u8 trigger;
	__le32 period;
} __packed;

#define HOST_IF_CFG_RX_FIFO_ENABLE     BIT(0)
#define HOST_IF_CFG_TX_EXTRA_BLKS_SWAP BIT(1)
#define HOST_IF_CFG_TX_PAD_TO_SDIO_BLK BIT(3)

struct wl1271_acx_host_config_bitmap {
	struct acx_header header;

	__le32 host_cfg_bitmap;
} __packed;

enum {
	WL1271_ACX_TRIG_TYPE_LEVEL = 0,
	WL1271_ACX_TRIG_TYPE_EDGE,
};

enum {
	WL1271_ACX_TRIG_DIR_LOW = 0,
	WL1271_ACX_TRIG_DIR_HIGH,
	WL1271_ACX_TRIG_DIR_BIDIR,
};

enum {
	WL1271_ACX_TRIG_ENABLE = 1,
	WL1271_ACX_TRIG_DISABLE,
};

enum {
	WL1271_ACX_TRIG_METRIC_RSSI_BEACON = 0,
	WL1271_ACX_TRIG_METRIC_RSSI_DATA,
	WL1271_ACX_TRIG_METRIC_SNR_BEACON,
	WL1271_ACX_TRIG_METRIC_SNR_DATA,
};

enum {
	WL1271_ACX_TRIG_IDX_RSSI = 0,
	WL1271_ACX_TRIG_COUNT = 8,
};

struct wl1271_acx_rssi_snr_trigger {
	struct acx_header header;

	u8 role_id;
	u8 metric;
	u8 type;
	u8 dir;
	__le16 threshold;
	__le16 pacing; /* 0 - 60000 ms */
	u8 hysteresis;
	u8 index;
	u8 enable;
	u8 padding[1];
};

struct wl1271_acx_rssi_snr_avg_weights {
	struct acx_header header;

	u8 role_id;
	u8 padding[3];
	u8 rssi_beacon;
	u8 rssi_data;
	u8 snr_beacon;
	u8 snr_data;
};


/* special capability bit (not employed by the 802.11n spec) */
#define WL12XX_HT_CAP_HT_OPERATION BIT(16)

/*
 * ACX_PEER_HT_CAP
 * Configure HT capabilities - declare the capabilities of the peer
 * we are connected to.
 */
struct wl1271_acx_ht_capabilities {
	struct acx_header header;

	/* bitmask of capability bits supported by the peer */
	__le32 ht_capabilites;

	/* Indicates to which link these capabilities apply. */
	u8 hlid;

	/*
	 * This the maximum A-MPDU length supported by the AP. The FW may not
	 * exceed this length when sending A-MPDUs
	 */
	u8 ampdu_max_length;

	/* This is the minimal spacing required when sending A-MPDUs to the AP*/
	u8 ampdu_min_spacing;

	u8 padding;
} __packed;

/*
 * ACX_HT_BSS_OPERATION
 * Configure HT capabilities - AP rules for behavior in the BSS.
 */
struct wl1271_acx_ht_information {
	struct acx_header header;

	u8 role_id;

	/* Values: 0 - RIFS not allowed, 1 - RIFS allowed */
	u8 rifs_mode;

	/* Values: 0 - 3 like in spec */
	u8 ht_protection;

	/* Values: 0 - GF protection not required, 1 - GF protection required */
	u8 gf_protection;

	/*Values: 0 - TX Burst limit not required, 1 - TX Burst Limit required*/
	u8 ht_tx_burst_limit;

	/*
	 * Values: 0 - Dual CTS protection not required,
	 *         1 - Dual CTS Protection required
	 * Note: When this value is set to 1 FW will protect all TXOP with RTS
	 * frame and will not use CTS-to-self regardless of the value of the
	 * ACX_CTS_PROTECTION information element
	 */
	u8 dual_cts_protection;

	u8 padding[2];
} __packed;

#define RX_BA_MAX_SESSIONS 2

struct wl1271_acx_ba_initiator_policy {
	struct acx_header header;

	/* Specifies role Id, Range 0-7, 0xFF means ANY role. */
	u8 role_id;

	/*
	 * Per TID setting for allowing TX BA. Set a bit to 1 to allow
	 * TX BA sessions for the corresponding TID.
	 */
	u8 tid_bitmap;

	/* Windows size in number of packets */
	u8 win_size;

	u8 padding1[1];

	/* As initiator inactivity timeout in time units(TU) of 1024us */
	u16 inactivity_timeout;

	u8 padding[2];
} __packed;

struct wl1271_acx_ba_receiver_setup {
	struct acx_header header;

	/* Specifies link id, range 0-31 */
	u8 hlid;

	u8 tid;

	u8 enable;

	/* Windows size in number of packets */
	u8 win_size;

	/* BA session starting sequence number.  RANGE 0-FFF */
	u16 ssn;

	u8 padding[2];
} __packed;

struct wl1271_acx_fw_tsf_information {
	struct acx_header header;

	__le32 current_tsf_high;
	__le32 current_tsf_low;
	__le32 last_bttt_high;
	__le32 last_tbtt_low;
	u8 last_dtim_count;
	u8 padding[3];
} __packed;

struct wl1271_acx_ps_rx_streaming {
	struct acx_header header;

	u8 role_id;
	u8 tid;
	u8 enable;

	/* interval between triggers (10-100 msec) */
	u8 period;

	/* timeout before first trigger (0-200 msec) */
	u8 timeout;
	u8 padding[3];
} __packed;

struct wl1271_acx_ap_max_tx_retry {
	struct acx_header header;

	u8 role_id;
	u8 padding_1;

	/*
	 * the number of frames transmission failures before
	 * issuing the aging event.
	 */
	__le16 max_tx_retry;
} __packed;

struct wl1271_acx_config_ps {
	struct acx_header header;

	u8 exit_retries;
	u8 enter_retries;
	u8 padding[2];
	__le32 null_data_rate;
} __packed;

struct wl1271_acx_inconnection_sta {
	struct acx_header header;

	u8 addr[ETH_ALEN];
	u8 padding1[2];
} __packed;

/*
 * ACX_FM_COEX_CFG
 * set the FM co-existence parameters.
 */
struct wl1271_acx_fm_coex {
	struct acx_header header;
	/* enable(1) / disable(0) the FM Coex feature */
	u8 enable;
	/*
	 * Swallow period used in COEX PLL swallowing mechanism.
	 * 0xFF = use FW default
	 */
	u8 swallow_period;
	/*
	 * The N divider used in COEX PLL swallowing mechanism for Fref of
	 * 38.4/19.2 Mhz. 0xFF = use FW default
	 */
	u8 n_divider_fref_set_1;
	/*
	 * The N divider used in COEX PLL swallowing mechanism for Fref of
	 * 26/52 Mhz. 0xFF = use FW default
	 */
	u8 n_divider_fref_set_2;
	/*
	 * The M divider used in COEX PLL swallowing mechanism for Fref of
	 * 38.4/19.2 Mhz. 0xFFFF = use FW default
	 */
	__le16 m_divider_fref_set_1;
	/*
	 * The M divider used in COEX PLL swallowing mechanism for Fref of
	 * 26/52 Mhz. 0xFFFF = use FW default
	 */
	__le16 m_divider_fref_set_2;
	/*
	 * The time duration in uSec required for COEX PLL to stabilize.
	 * 0xFFFFFFFF = use FW default
	 */
	__le32 coex_pll_stabilization_time;
	/*
	 * The time duration in uSec required for LDO to stabilize.
	 * 0xFFFFFFFF = use FW default
	 */
	__le16 ldo_stabilization_time;
	/*
	 * The disturbed frequency band margin around the disturbed frequency
	 * center (single sided).
	 * For example, if 2 is configured, the following channels will be
	 * considered disturbed channel:
	 *   80 +- 0.1 MHz, 91 +- 0.1 MHz, 98 +- 0.1 MHz, 102 +- 0.1 MH
	 * 0xFF = use FW default
	 */
	u8 fm_disturbed_band_margin;
	/*
	 * The swallow clock difference of the swallowing mechanism.
	 * 0xFF = use FW default
	 */
	u8 swallow_clk_diff;
} __packed;

#define ACX_RATE_MGMT_ALL_PARAMS 0xff
struct wl12xx_acx_set_rate_mgmt_params {
	struct acx_header header;

	u8 index; /* 0xff to configure all params */
	u8 padding1;
	__le16 rate_retry_score;
	__le16 per_add;
	__le16 per_th1;
	__le16 per_th2;
	__le16 max_per;
	u8 inverse_curiosity_factor;
	u8 tx_fail_low_th;
	u8 tx_fail_high_th;
	u8 per_alpha_shift;
	u8 per_add_shift;
	u8 per_beta1_shift;
	u8 per_beta2_shift;
	u8 rate_check_up;
	u8 rate_check_down;
	u8 rate_retry_policy[ACX_RATE_MGMT_NUM_OF_RATES];
	u8 padding2[2];
} __packed;

struct wl12xx_acx_config_hangover {
	struct acx_header header;

	__le32 recover_time;
	u8 hangover_period;
	u8 dynamic_mode;
	u8 early_termination_mode;
	u8 max_period;
	u8 min_period;
	u8 increase_delta;
	u8 decrease_delta;
	u8 quiet_time;
	u8 increase_time;
	u8 window_size;
	u8 padding[2];
} __packed;

enum {
	ACX_WAKE_UP_CONDITIONS      = 0x0002,
	ACX_MEM_CFG                 = 0x0003,
	ACX_SLOT                    = 0x0004,
	ACX_AC_CFG                  = 0x0007,
	ACX_MEM_MAP                 = 0x0008,
	ACX_AID                     = 0x000A,
	ACX_MEDIUM_USAGE            = 0x000F,
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
	ACX_BCN_DTIM_OPTIONS        = 0x0031,
	ACX_SG_ENABLE               = 0x0032,
	ACX_SG_CFG                  = 0x0033,
	ACX_FM_COEX_CFG             = 0x0034,
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
	ACX_RSSI_SNR_WEIGHTS        = 0x0052,
	ACX_KEEP_ALIVE_MODE         = 0x0053,
	ACX_SET_KEEP_ALIVE_CONFIG   = 0x0054,
	ACX_BA_SESSION_INIT_POLICY  = 0x0055,
	ACX_BA_SESSION_RX_SETUP     = 0x0056,
	ACX_PEER_HT_CAP             = 0x0057,
	ACX_HT_BSS_OPERATION        = 0x0058,
	ACX_COEX_ACTIVITY           = 0x0059,
	ACX_BURST_MODE              = 0x005C,
	ACX_SET_RATE_MGMT_PARAMS    = 0x005D,
	ACX_SET_RATE_ADAPT_PARAMS   = 0x0060,
	ACX_SET_DCO_ITRIM_PARAMS    = 0x0061,
	ACX_GEN_FW_CMD              = 0x0070,
	ACX_HOST_IF_CFG_BITMAP      = 0x0071,
	ACX_MAX_TX_FAILURE          = 0x0072,
	ACX_UPDATE_INCONNECTION_STA_LIST = 0x0073,
	DOT11_RX_MSDU_LIFE_TIME     = 0x1004,
	DOT11_CUR_TX_PWR            = 0x100D,
	DOT11_RX_DOT11_MODE         = 0x1012,
	DOT11_RTS_THRESHOLD         = 0x1013,
	DOT11_GROUP_ADDRESS_TBL     = 0x1014,
	ACX_PM_CONFIG               = 0x1016,
	ACX_CONFIG_PS               = 0x1017,
	ACX_CONFIG_HANGOVER         = 0x1018,
};


int wl1271_acx_wake_up_conditions(struct wl1271 *wl);
int wl1271_acx_sleep_auth(struct wl1271 *wl, u8 sleep_auth);
int wl1271_acx_tx_power(struct wl1271 *wl, int power);
int wl1271_acx_feature_cfg(struct wl1271 *wl);
int wl1271_acx_mem_map(struct wl1271 *wl,
		       struct acx_header *mem_map, size_t len);
int wl1271_acx_rx_msdu_life_time(struct wl1271 *wl);
int wl1271_acx_pd_threshold(struct wl1271 *wl);
int wl1271_acx_slot(struct wl1271 *wl, enum acx_slot_type slot_time);
int wl1271_acx_group_address_tbl(struct wl1271 *wl, bool enable,
				 void *mc_list, u32 mc_list_len);
int wl1271_acx_service_period_timeout(struct wl1271 *wl);
int wl1271_acx_rts_threshold(struct wl1271 *wl, u32 rts_threshold);
int wl1271_acx_dco_itrim_params(struct wl1271 *wl);
int wl1271_acx_beacon_filter_opt(struct wl1271 *wl, bool enable_filter);
int wl1271_acx_beacon_filter_table(struct wl1271 *wl);
int wl1271_acx_conn_monit_params(struct wl1271 *wl, bool enable);
int wl1271_acx_sg_enable(struct wl1271 *wl, bool enable);
int wl12xx_acx_sg_cfg(struct wl1271 *wl);
int wl1271_acx_cca_threshold(struct wl1271 *wl);
int wl1271_acx_bcn_dtim_options(struct wl1271 *wl);
int wl1271_acx_aid(struct wl1271 *wl, u16 aid);
int wl1271_acx_event_mbox_mask(struct wl1271 *wl, u32 event_mask);
int wl1271_acx_set_preamble(struct wl1271 *wl, enum acx_preamble_type preamble);
int wl1271_acx_cts_protect(struct wl1271 *wl,
			   enum acx_ctsprotect_type ctsprotect);
int wl1271_acx_statistics(struct wl1271 *wl, struct acx_statistics *stats);
int wl1271_acx_sta_rate_policies(struct wl1271 *wl);
int wl1271_acx_ap_rate_policy(struct wl1271 *wl, struct conf_tx_rate_class *c,
		      u8 idx);
int wl1271_acx_ac_cfg(struct wl1271 *wl, u8 ac, u8 cw_min, u16 cw_max,
		      u8 aifsn, u16 txop);
int wl1271_acx_tid_cfg(struct wl1271 *wl, u8 queue_id, u8 channel_type,
		       u8 tsid, u8 ps_scheme, u8 ack_policy,
		       u32 apsd_conf0, u32 apsd_conf1);
int wl1271_acx_frag_threshold(struct wl1271 *wl, u32 frag_threshold);
int wl1271_acx_tx_config_options(struct wl1271 *wl);
int wl12xx_acx_mem_cfg(struct wl1271 *wl);
int wl1271_acx_init_mem_config(struct wl1271 *wl);
int wl1271_acx_host_if_cfg_bitmap(struct wl1271 *wl, u32 host_cfg_bitmap);
int wl1271_acx_init_rx_interrupt(struct wl1271 *wl);
int wl1271_acx_smart_reflex(struct wl1271 *wl);
int wl1271_acx_bet_enable(struct wl1271 *wl, bool enable);
int wl1271_acx_arp_ip_filter(struct wl1271 *wl, u8 enable, __be32 address);
int wl1271_acx_pm_config(struct wl1271 *wl);
int wl1271_acx_keep_alive_mode(struct wl1271 *wl, bool enable);
int wl1271_acx_keep_alive_config(struct wl1271 *wl, u8 index, u8 tpl_valid);
int wl1271_acx_rssi_snr_trigger(struct wl1271 *wl, bool enable,
				s16 thold, u8 hyst);
int wl1271_acx_rssi_snr_avg_weights(struct wl1271 *wl);
int wl1271_acx_set_ht_capabilities(struct wl1271 *wl,
				    struct ieee80211_sta_ht_cap *ht_cap,
				    bool allow_ht_operation, u8 hlid);
int wl1271_acx_set_ht_information(struct wl1271 *wl,
				   u16 ht_operation_mode);
int wl12xx_acx_set_ba_initiator_policy(struct wl1271 *wl);
int wl12xx_acx_set_ba_receiver_session(struct wl1271 *wl, u8 tid_index,
				       u16 ssn, bool enable, u8 peer_hlid);
int wl1271_acx_tsf_info(struct wl1271 *wl, u64 *mactime);
int wl1271_acx_ps_rx_streaming(struct wl1271 *wl, bool enable);
int wl1271_acx_ap_max_tx_retry(struct wl1271 *wl);
int wl1271_acx_config_ps(struct wl1271 *wl);
int wl1271_acx_set_inconnection_sta(struct wl1271 *wl, u8 *addr);
int wl1271_acx_fm_coex(struct wl1271 *wl);
int wl12xx_acx_set_rate_mgmt_params(struct wl1271 *wl);
int wl12xx_acx_config_hangover(struct wl1271 *wl);

#endif /* __WL1271_ACX_H__ */

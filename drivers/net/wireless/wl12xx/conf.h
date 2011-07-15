/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef __CONF_H__
#define __CONF_H__

enum {
	CONF_HW_BIT_RATE_1MBPS   = BIT(0),
	CONF_HW_BIT_RATE_2MBPS   = BIT(1),
	CONF_HW_BIT_RATE_5_5MBPS = BIT(2),
	CONF_HW_BIT_RATE_6MBPS   = BIT(3),
	CONF_HW_BIT_RATE_9MBPS   = BIT(4),
	CONF_HW_BIT_RATE_11MBPS  = BIT(5),
	CONF_HW_BIT_RATE_12MBPS  = BIT(6),
	CONF_HW_BIT_RATE_18MBPS  = BIT(7),
	CONF_HW_BIT_RATE_22MBPS  = BIT(8),
	CONF_HW_BIT_RATE_24MBPS  = BIT(9),
	CONF_HW_BIT_RATE_36MBPS  = BIT(10),
	CONF_HW_BIT_RATE_48MBPS  = BIT(11),
	CONF_HW_BIT_RATE_54MBPS  = BIT(12),
	CONF_HW_BIT_RATE_MCS_0   = BIT(13),
	CONF_HW_BIT_RATE_MCS_1   = BIT(14),
	CONF_HW_BIT_RATE_MCS_2   = BIT(15),
	CONF_HW_BIT_RATE_MCS_3   = BIT(16),
	CONF_HW_BIT_RATE_MCS_4   = BIT(17),
	CONF_HW_BIT_RATE_MCS_5   = BIT(18),
	CONF_HW_BIT_RATE_MCS_6   = BIT(19),
	CONF_HW_BIT_RATE_MCS_7   = BIT(20)
};

enum {
	CONF_HW_RATE_INDEX_1MBPS   = 0,
	CONF_HW_RATE_INDEX_2MBPS   = 1,
	CONF_HW_RATE_INDEX_5_5MBPS = 2,
	CONF_HW_RATE_INDEX_6MBPS   = 3,
	CONF_HW_RATE_INDEX_9MBPS   = 4,
	CONF_HW_RATE_INDEX_11MBPS  = 5,
	CONF_HW_RATE_INDEX_12MBPS  = 6,
	CONF_HW_RATE_INDEX_18MBPS  = 7,
	CONF_HW_RATE_INDEX_22MBPS  = 8,
	CONF_HW_RATE_INDEX_24MBPS  = 9,
	CONF_HW_RATE_INDEX_36MBPS  = 10,
	CONF_HW_RATE_INDEX_48MBPS  = 11,
	CONF_HW_RATE_INDEX_54MBPS  = 12,
	CONF_HW_RATE_INDEX_MAX     = CONF_HW_RATE_INDEX_54MBPS,
};

enum {
	CONF_HW_RXTX_RATE_MCS7 = 0,
	CONF_HW_RXTX_RATE_MCS6,
	CONF_HW_RXTX_RATE_MCS5,
	CONF_HW_RXTX_RATE_MCS4,
	CONF_HW_RXTX_RATE_MCS3,
	CONF_HW_RXTX_RATE_MCS2,
	CONF_HW_RXTX_RATE_MCS1,
	CONF_HW_RXTX_RATE_MCS0,
	CONF_HW_RXTX_RATE_54,
	CONF_HW_RXTX_RATE_48,
	CONF_HW_RXTX_RATE_36,
	CONF_HW_RXTX_RATE_24,
	CONF_HW_RXTX_RATE_22,
	CONF_HW_RXTX_RATE_18,
	CONF_HW_RXTX_RATE_12,
	CONF_HW_RXTX_RATE_11,
	CONF_HW_RXTX_RATE_9,
	CONF_HW_RXTX_RATE_6,
	CONF_HW_RXTX_RATE_5_5,
	CONF_HW_RXTX_RATE_2,
	CONF_HW_RXTX_RATE_1,
	CONF_HW_RXTX_RATE_MAX,
	CONF_HW_RXTX_RATE_UNSUPPORTED = 0xff
};

enum {
	CONF_SG_DISABLE = 0,
	CONF_SG_PROTECTIVE,
	CONF_SG_OPPORTUNISTIC
};

enum {
	/*
	 * PER threshold in PPM of the BT voice
	 *
	 * Range: 0 - 10000000
	 */
	CONF_SG_BT_PER_THRESHOLD = 0,

	/*
	 * Number of consequent RX_ACTIVE activities to override BT voice
	 * frames to ensure WLAN connection
	 *
	 * Range: 0 - 100
	 */
	CONF_SG_HV3_MAX_OVERRIDE,

	/*
	 * Defines the PER threshold of the BT voice
	 *
	 * Range: 0 - 65000
	 */
	CONF_SG_BT_NFS_SAMPLE_INTERVAL,

	/*
	 * Defines the load ratio of BT
	 *
	 * Range: 0 - 100 (%)
	 */
	CONF_SG_BT_LOAD_RATIO,

	/*
	 * Defines whether the SG will force WLAN host to enter/exit PSM
	 *
	 * Range: 1 - SG can force, 0 - host handles PSM
	 */
	CONF_SG_AUTO_PS_MODE,

	/*
	 * Compensation percentage of probe requests when scan initiated
	 * during BT voice/ACL link.
	 *
	 * Range: 0 - 255 (%)
	 */
	CONF_SG_AUTO_SCAN_PROBE_REQ,

	/*
	 * Compensation percentage of probe requests when active scan initiated
	 * during BT voice
	 *
	 * Range: 0 - 255 (%)
	 */
	CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_HV3,

	/*
	 * Defines antenna configuration (single/dual antenna)
	 *
	 * Range: 0 - single antenna, 1 - dual antenna
	 */
	CONF_SG_ANTENNA_CONFIGURATION,

	/*
	 * The threshold (percent) of max consequtive beacon misses before
	 * increasing priority of beacon reception.
	 *
	 * Range: 0 - 100 (%)
	 */
	CONF_SG_BEACON_MISS_PERCENT,

	/*
	 * The rate threshold below which receiving a data frame from the AP
	 * will increase the priority of the data frame above BT traffic.
	 *
	 * Range: 0,2, 5(=5.5), 6, 9, 11, 12, 18, 24, 36, 48, 54
	 */
	CONF_SG_RATE_ADAPT_THRESH,

	/*
	 * Not used currently.
	 *
	 * Range: 0
	 */
	CONF_SG_RATE_ADAPT_SNR,

	/*
	 * Configure the min and max time BT gains the antenna
	 * in WLAN PSM / BT master basic rate
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_BT_ACL_MASTER_MIN_BR,
	CONF_SG_WLAN_PS_BT_ACL_MASTER_MAX_BR,

	/*
	 * The time after it expires no new WLAN trigger frame is trasmitted
	 * in WLAN PSM / BT master basic rate
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_MAX_BT_ACL_MASTER_BR,

	/*
	 * Configure the min and max time BT gains the antenna
	 * in WLAN PSM / BT slave basic rate
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_BT_ACL_SLAVE_MIN_BR,
	CONF_SG_WLAN_PS_BT_ACL_SLAVE_MAX_BR,

	/*
	 * The time after it expires no new WLAN trigger frame is trasmitted
	 * in WLAN PSM / BT slave basic rate
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_MAX_BT_ACL_SLAVE_BR,

	/*
	 * Configure the min and max time BT gains the antenna
	 * in WLAN PSM / BT master EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_BT_ACL_MASTER_MIN_EDR,
	CONF_SG_WLAN_PS_BT_ACL_MASTER_MAX_EDR,

	/*
	 * The time after it expires no new WLAN trigger frame is trasmitted
	 * in WLAN PSM / BT master EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_MAX_BT_ACL_MASTER_EDR,

	/*
	 * Configure the min and max time BT gains the antenna
	 * in WLAN PSM / BT slave EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_BT_ACL_SLAVE_MIN_EDR,
	CONF_SG_WLAN_PS_BT_ACL_SLAVE_MAX_EDR,

	/*
	 * The time after it expires no new WLAN trigger frame is trasmitted
	 * in WLAN PSM / BT slave EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_PS_MAX_BT_ACL_SLAVE_EDR,

	/*
	 * RX guard time before the beginning of a new BT voice frame during
	 * which no new WLAN trigger frame is transmitted.
	 *
	 * Range: 0 - 100000 (us)
	 */
	CONF_SG_RXT,

	/*
	 * TX guard time before the beginning of a new BT voice frame during
	 * which no new WLAN frame is transmitted.
	 *
	 * Range: 0 - 100000 (us)
	 */

	CONF_SG_TXT,

	/*
	 * Enable adaptive RXT/TXT algorithm. If disabled, the host values
	 * will be utilized.
	 *
	 * Range: 0 - disable, 1 - enable
	 */
	CONF_SG_ADAPTIVE_RXT_TXT,

	/*
	 * The used WLAN legacy service period during active BT ACL link
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_PS_POLL_TIMEOUT,

	/*
	 * The used WLAN UPSD service period during active BT ACL link
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_UPSD_TIMEOUT,

	/*
	 * Configure the min and max time BT gains the antenna
	 * in WLAN Active / BT master EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_ACTIVE_BT_ACL_MASTER_MIN_EDR,
	CONF_SG_WLAN_ACTIVE_BT_ACL_MASTER_MAX_EDR,

	/*
	 * The maximum time WLAN can gain the antenna for
	 * in WLAN Active / BT master EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_MASTER_EDR,

	/*
	 * Configure the min and max time BT gains the antenna
	 * in WLAN Active / BT slave EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_ACTIVE_BT_ACL_SLAVE_MIN_EDR,
	CONF_SG_WLAN_ACTIVE_BT_ACL_SLAVE_MAX_EDR,

	/*
	 * The maximum time WLAN can gain the antenna for
	 * in WLAN Active / BT slave EDR
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_SLAVE_EDR,

	/*
	 * Configure the min and max time BT gains the antenna
	 * in WLAN Active / BT basic rate
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_ACTIVE_BT_ACL_MIN_BR,
	CONF_SG_WLAN_ACTIVE_BT_ACL_MAX_BR,

	/*
	 * The maximum time WLAN can gain the antenna for
	 * in WLAN Active / BT basic rate
	 *
	 * Range: 0 - 255 (ms)
	 */
	CONF_SG_WLAN_ACTIVE_MAX_BT_ACL_BR,

	/*
	 * Compensation percentage of WLAN passive scan window if initiated
	 * during BT voice
	 *
	 * Range: 0 - 1000 (%)
	 */
	CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_HV3,

	/*
	 * Compensation percentage of WLAN passive scan window if initiated
	 * during BT A2DP
	 *
	 * Range: 0 - 1000 (%)
	 */
	CONF_SG_PASSIVE_SCAN_DURATION_FACTOR_A2DP,

	/*
	 * Fixed time ensured for BT traffic to gain the antenna during WLAN
	 * passive scan.
	 *
	 * Range: 0 - 1000 ms
	 */
	CONF_SG_PASSIVE_SCAN_A2DP_BT_TIME,

	/*
	 * Fixed time ensured for WLAN traffic to gain the antenna during WLAN
	 * passive scan.
	 *
	 * Range: 0 - 1000 ms
	 */
	CONF_SG_PASSIVE_SCAN_A2DP_WLAN_TIME,

	/*
	 * Number of consequent BT voice frames not interrupted by WLAN
	 *
	 * Range: 0 - 100
	 */
	CONF_SG_HV3_MAX_SERVED,

	/*
	 * Protection time of the DHCP procedure.
	 *
	 * Range: 0 - 100000 (ms)
	 */
	CONF_SG_DHCP_TIME,

	/*
	 * Compensation percentage of WLAN active scan window if initiated
	 * during BT A2DP
	 *
	 * Range: 0 - 1000 (%)
	 */
	CONF_SG_ACTIVE_SCAN_DURATION_FACTOR_A2DP,
	CONF_SG_TEMP_PARAM_1,
	CONF_SG_TEMP_PARAM_2,
	CONF_SG_TEMP_PARAM_3,
	CONF_SG_TEMP_PARAM_4,
	CONF_SG_TEMP_PARAM_5,

	/*
	 * AP beacon miss
	 *
	 * Range: 0 - 255
	 */
	CONF_SG_AP_BEACON_MISS_TX,

	/*
	 * AP RX window length
	 *
	 * Range: 0 - 50
	 */
	CONF_SG_RX_WINDOW_LENGTH,

	/*
	 * AP connection protection time
	 *
	 * Range: 0 - 5000
	 */
	CONF_SG_AP_CONNECTION_PROTECTION_TIME,

	CONF_SG_TEMP_PARAM_6,
	CONF_SG_TEMP_PARAM_7,
	CONF_SG_TEMP_PARAM_8,
	CONF_SG_TEMP_PARAM_9,
	CONF_SG_TEMP_PARAM_10,

	CONF_SG_STA_PARAMS_MAX = CONF_SG_TEMP_PARAM_5 + 1,
	CONF_SG_AP_PARAMS_MAX = CONF_SG_TEMP_PARAM_10 + 1,

	CONF_SG_PARAMS_ALL = 0xff
};

struct conf_sg_settings {
	u32 sta_params[CONF_SG_STA_PARAMS_MAX];
	u32 ap_params[CONF_SG_AP_PARAMS_MAX];
	u8 state;
};

enum conf_rx_queue_type {
	CONF_RX_QUEUE_TYPE_LOW_PRIORITY,  /* All except the high priority */
	CONF_RX_QUEUE_TYPE_HIGH_PRIORITY, /* Management and voice packets */
};

struct conf_rx_settings {
	/*
	 * The maximum amount of time, in TU, before the
	 * firmware discards the MSDU.
	 *
	 * Range: 0 - 0xFFFFFFFF
	 */
	u32 rx_msdu_life_time;

	/*
	 * Packet detection threshold in the PHY.
	 *
	 * FIXME: details unknown.
	 */
	u32 packet_detection_threshold;

	/*
	 * The longest time the STA will wait to receive traffic from the AP
	 * after a PS-poll has been transmitted.
	 *
	 * Range: 0 - 200000
	 */
	u16 ps_poll_timeout;
	/*
	 * The longest time the STA will wait to receive traffic from the AP
	 * after a frame has been sent from an UPSD enabled queue.
	 *
	 * Range: 0 - 200000
	 */
	u16 upsd_timeout;

	/*
	 * The number of octets in an MPDU, below which an RTS/CTS
	 * handshake is not performed.
	 *
	 * Range: 0 - 4096
	 */
	u16 rts_threshold;

	/*
	 * The RX Clear Channel Assessment threshold in the PHY
	 * (the energy threshold).
	 *
	 * Range: ENABLE_ENERGY_D  == 0x140A
	 *        DISABLE_ENERGY_D == 0xFFEF
	 */
	u16 rx_cca_threshold;

	/*
	 * Occupied Rx mem-blocks number which requires interrupting the host
	 * (0 = no buffering, 0xffff = disabled).
	 *
	 * Range: u16
	 */
	u16 irq_blk_threshold;

	/*
	 * Rx packets number which requires interrupting the host
	 * (0 = no buffering).
	 *
	 * Range: u16
	 */
	u16 irq_pkt_threshold;

	/*
	 * Max time in msec the FW may delay RX-Complete interrupt.
	 *
	 * Range: 1 - 100
	 */
	u16 irq_timeout;

	/*
	 * The RX queue type.
	 *
	 * Range: RX_QUEUE_TYPE_RX_LOW_PRIORITY, RX_QUEUE_TYPE_RX_HIGH_PRIORITY,
	 */
	u8 queue_type;
};

#define CONF_TX_MAX_RATE_CLASSES       8

#define CONF_TX_RATE_MASK_UNSPECIFIED  0
#define CONF_TX_RATE_MASK_BASIC        (CONF_HW_BIT_RATE_1MBPS | \
					CONF_HW_BIT_RATE_2MBPS)
#define CONF_TX_RATE_RETRY_LIMIT       10

/*
 * Rates supported for data packets when operating as AP. Note the absence
 * of the 22Mbps rate. There is a FW limitation on 12 rates so we must drop
 * one. The rate dropped is not mandatory under any operating mode.
 */
#define CONF_TX_AP_ENABLED_RATES       (CONF_HW_BIT_RATE_1MBPS | \
	CONF_HW_BIT_RATE_2MBPS | CONF_HW_BIT_RATE_5_5MBPS |      \
	CONF_HW_BIT_RATE_6MBPS | CONF_HW_BIT_RATE_9MBPS |        \
	CONF_HW_BIT_RATE_11MBPS | CONF_HW_BIT_RATE_12MBPS |      \
	CONF_HW_BIT_RATE_18MBPS | CONF_HW_BIT_RATE_24MBPS |      \
	CONF_HW_BIT_RATE_36MBPS | CONF_HW_BIT_RATE_48MBPS |      \
	CONF_HW_BIT_RATE_54MBPS)

#define CONF_TX_OFDM_RATES (CONF_HW_BIT_RATE_6MBPS |             \
	CONF_HW_BIT_RATE_12MBPS | CONF_HW_BIT_RATE_24MBPS |      \
	CONF_HW_BIT_RATE_36MBPS | CONF_HW_BIT_RATE_48MBPS |      \
	CONF_HW_BIT_RATE_54MBPS)


/*
 * Default rates for management traffic when operating in AP mode. This
 * should be configured according to the basic rate set of the AP
 */
#define CONF_TX_AP_DEFAULT_MGMT_RATES  (CONF_HW_BIT_RATE_1MBPS | \
	CONF_HW_BIT_RATE_2MBPS | CONF_HW_BIT_RATE_5_5MBPS)

/*
 * Default rates for working as IBSS. use 11b rates
 */
#define CONF_TX_IBSS_DEFAULT_RATES  (CONF_HW_BIT_RATE_1MBPS |       \
		CONF_HW_BIT_RATE_2MBPS | CONF_HW_BIT_RATE_5_5MBPS | \
		CONF_HW_BIT_RATE_11MBPS);

struct conf_tx_rate_class {

	/*
	 * The rates enabled for this rate class.
	 *
	 * Range: CONF_HW_BIT_RATE_* bit mask
	 */
	u32 enabled_rates;

	/*
	 * The dot11 short retry limit used for TX retries.
	 *
	 * Range: u8
	 */
	u8 short_retry_limit;

	/*
	 * The dot11 long retry limit used for TX retries.
	 *
	 * Range: u8
	 */
	u8 long_retry_limit;

	/*
	 * Flags controlling the attributes of TX transmission.
	 *
	 * Range: bit 0: Truncate - when set, FW attempts to send a frame stop
	 *               when the total valid per-rate attempts have
	 *               been exhausted; otherwise transmissions
	 *               will continue at the lowest available rate
	 *               until the appropriate one of the
	 *               short_retry_limit, long_retry_limit,
	 *               dot11_max_transmit_msdu_life_time, or
	 *               max_tx_life_time, is exhausted.
	 *            1: Preamble Override - indicates if the preamble type
	 *               should be used in TX.
	 *            2: Preamble Type - the type of the preamble to be used by
	 *               the policy (0 - long preamble, 1 - short preamble.
	 */
	u8 aflags;
};

#define CONF_TX_MAX_AC_COUNT 4

/* Slot number setting to start transmission at PIFS interval */
#define CONF_TX_AIFS_PIFS 1
/* Slot number setting to start transmission at DIFS interval normal
 * DCF access */
#define CONF_TX_AIFS_DIFS 2


enum conf_tx_ac {
	CONF_TX_AC_BE = 0,         /* best effort / legacy */
	CONF_TX_AC_BK = 1,         /* background */
	CONF_TX_AC_VI = 2,         /* video */
	CONF_TX_AC_VO = 3,         /* voice */
	CONF_TX_AC_CTS2SELF = 4,   /* fictitious AC, follows AC_VO */
	CONF_TX_AC_ANY_TID = 0x1f
};

struct conf_tx_ac_category {
	/*
	 * The AC class identifier.
	 *
	 * Range: enum conf_tx_ac
	 */
	u8 ac;

	/*
	 * The contention window minimum size (in slots) for the access
	 * class.
	 *
	 * Range: u8
	 */
	u8 cw_min;

	/*
	 * The contention window maximum size (in slots) for the access
	 * class.
	 *
	 * Range: u8
	 */
	u16 cw_max;

	/*
	 * The AIF value (in slots) for the access class.
	 *
	 * Range: u8
	 */
	u8 aifsn;

	/*
	 * The TX Op Limit (in microseconds) for the access class.
	 *
	 * Range: u16
	 */
	u16 tx_op_limit;
};

#define CONF_TX_MAX_TID_COUNT 8

enum {
	CONF_CHANNEL_TYPE_DCF = 0,   /* DC/LEGACY*/
	CONF_CHANNEL_TYPE_EDCF = 1,  /* EDCA*/
	CONF_CHANNEL_TYPE_HCCA = 2,  /* HCCA*/
};

enum {
	CONF_PS_SCHEME_LEGACY = 0,
	CONF_PS_SCHEME_UPSD_TRIGGER = 1,
	CONF_PS_SCHEME_LEGACY_PSPOLL = 2,
	CONF_PS_SCHEME_SAPSD = 3,
};

enum {
	CONF_ACK_POLICY_LEGACY = 0,
	CONF_ACK_POLICY_NO_ACK = 1,
	CONF_ACK_POLICY_BLOCK = 2,
};


struct conf_tx_tid {
	u8 queue_id;
	u8 channel_type;
	u8 tsid;
	u8 ps_scheme;
	u8 ack_policy;
	u32 apsd_conf[2];
};

struct conf_tx_settings {
	/*
	 * The TX ED value for TELEC Enable/Disable.
	 *
	 * Range: 0, 1
	 */
	u8 tx_energy_detection;

	/*
	 * Configuration for rate classes for TX (currently only one
	 * rate class supported). Used in non-AP mode.
	 */
	struct conf_tx_rate_class sta_rc_conf;

	/*
	 * Configuration for access categories for TX rate control.
	 */
	u8 ac_conf_count;
	struct conf_tx_ac_category ac_conf[CONF_TX_MAX_AC_COUNT];

	/*
	 * AP-mode - allow this number of TX retries to a station before an
	 * event is triggered from FW.
	 * In AP-mode the hlids of unreachable stations are given in the
	 * "sta_tx_retry_exceeded" member in the event mailbox.
	 */
	u8 max_tx_retries;

	/*
	 * AP-mode - after this number of seconds a connected station is
	 * considered inactive.
	 */
	u16 ap_aging_period;

	/*
	 * Configuration for TID parameters.
	 */
	u8 tid_conf_count;
	struct conf_tx_tid tid_conf[CONF_TX_MAX_TID_COUNT];

	/*
	 * The TX fragmentation threshold.
	 *
	 * Range: u16
	 */
	u16 frag_threshold;

	/*
	 * Max time in msec the FW may delay frame TX-Complete interrupt.
	 *
	 * Range: u16
	 */
	u16 tx_compl_timeout;

	/*
	 * Completed TX packet count which requires to issue the TX-Complete
	 * interrupt.
	 *
	 * Range: u16
	 */
	u16 tx_compl_threshold;

	/*
	 * The rate used for control messages and scanning on the 2.4GHz band
	 *
	 * Range: CONF_HW_BIT_RATE_* bit mask
	 */
	u32 basic_rate;

	/*
	 * The rate used for control messages and scanning on the 5GHz band
	 *
	 * Range: CONF_HW_BIT_RATE_* bit mask
	 */
	u32 basic_rate_5;

	/*
	 * TX retry limits for templates
	 */
	u8 tmpl_short_retry_limit;
	u8 tmpl_long_retry_limit;
};

enum {
	CONF_WAKE_UP_EVENT_BEACON    = 0x01, /* Wake on every Beacon*/
	CONF_WAKE_UP_EVENT_DTIM      = 0x02, /* Wake on every DTIM*/
	CONF_WAKE_UP_EVENT_N_DTIM    = 0x04, /* Wake every Nth DTIM */
	CONF_WAKE_UP_EVENT_N_BEACONS = 0x08, /* Wake every Nth beacon */
	CONF_WAKE_UP_EVENT_BITS_MASK = 0x0F
};

#define CONF_MAX_BCN_FILT_IE_COUNT 32

#define CONF_BCN_RULE_PASS_ON_CHANGE         BIT(0)
#define CONF_BCN_RULE_PASS_ON_APPEARANCE     BIT(1)

#define CONF_BCN_IE_OUI_LEN    3
#define CONF_BCN_IE_VER_LEN    2

struct conf_bcn_filt_rule {
	/*
	 * IE number to which to associate a rule.
	 *
	 * Range: u8
	 */
	u8 ie;

	/*
	 * Rule to associate with the specific ie.
	 *
	 * Range: CONF_BCN_RULE_PASS_ON_*
	 */
	u8 rule;

	/*
	 * OUI for the vendor specifie IE (221)
	 */
	u8 oui[CONF_BCN_IE_OUI_LEN];

	/*
	 * Type for the vendor specifie IE (221)
	 */
	u8 type;

	/*
	 * Version for the vendor specifie IE (221)
	 */
	u8 version[CONF_BCN_IE_VER_LEN];
};

#define CONF_MAX_RSSI_SNR_TRIGGERS 8

enum {
	CONF_TRIG_METRIC_RSSI_BEACON = 0,
	CONF_TRIG_METRIC_RSSI_DATA,
	CONF_TRIG_METRIC_SNR_BEACON,
	CONF_TRIG_METRIC_SNR_DATA
};

enum {
	CONF_TRIG_EVENT_TYPE_LEVEL = 0,
	CONF_TRIG_EVENT_TYPE_EDGE
};

enum {
	CONF_TRIG_EVENT_DIR_LOW = 0,
	CONF_TRIG_EVENT_DIR_HIGH,
	CONF_TRIG_EVENT_DIR_BIDIR
};

struct conf_sig_weights {

	/*
	 * RSSI from beacons average weight.
	 *
	 * Range: u8
	 */
	u8 rssi_bcn_avg_weight;

	/*
	 * RSSI from data average weight.
	 *
	 * Range: u8
	 */
	u8 rssi_pkt_avg_weight;

	/*
	 * SNR from beacons average weight.
	 *
	 * Range: u8
	 */
	u8 snr_bcn_avg_weight;

	/*
	 * SNR from data average weight.
	 *
	 * Range: u8
	 */
	u8 snr_pkt_avg_weight;
};

enum conf_bcn_filt_mode {
	CONF_BCN_FILT_MODE_DISABLED = 0,
	CONF_BCN_FILT_MODE_ENABLED = 1
};

enum conf_bet_mode {
	CONF_BET_MODE_DISABLE = 0,
	CONF_BET_MODE_ENABLE = 1,
};

struct conf_conn_settings {
	/*
	 * Firmware wakeup conditions configuration. The host may set only
	 * one bit.
	 *
	 * Range: CONF_WAKE_UP_EVENT_*
	 */
	u8 wake_up_event;

	/*
	 * Listen interval for beacons or Dtims.
	 *
	 * Range: 0 for beacon and Dtim wakeup
	 *        1-10 for x Dtims
	 *        1-255 for x beacons
	 */
	u8 listen_interval;

	/*
	 * Enable or disable the beacon filtering.
	 *
	 * Range: CONF_BCN_FILT_MODE_*
	 */
	enum conf_bcn_filt_mode bcn_filt_mode;

	/*
	 * Configure Beacon filter pass-thru rules.
	 */
	u8 bcn_filt_ie_count;
	struct conf_bcn_filt_rule bcn_filt_ie[CONF_MAX_BCN_FILT_IE_COUNT];

	/*
	 * The number of consequtive beacons to lose, before the firmware
	 * becomes out of synch.
	 *
	 * Range: u32
	 */
	u32 synch_fail_thold;

	/*
	 * After out-of-synch, the number of TU's to wait without a further
	 * received beacon (or probe response) before issuing the BSS_EVENT_LOSE
	 * event.
	 *
	 * Range: u32
	 */
	u32 bss_lose_timeout;

	/*
	 * Beacon receive timeout.
	 *
	 * Range: u32
	 */
	u32 beacon_rx_timeout;

	/*
	 * Broadcast receive timeout.
	 *
	 * Range: u32
	 */
	u32 broadcast_timeout;

	/*
	 * Enable/disable reception of broadcast packets in power save mode
	 *
	 * Range: 1 - enable, 0 - disable
	 */
	u8 rx_broadcast_in_ps;

	/*
	 * Consequtive PS Poll failures before sending event to driver
	 *
	 * Range: u8
	 */
	u8 ps_poll_threshold;

	/*
	 * PS Poll failure recovery ACTIVE period length
	 *
	 * Range: u32 (ms)
	 */
	u32 ps_poll_recovery_period;

	/*
	 * Configuration of signal average weights.
	 */
	struct conf_sig_weights sig_weights;

	/*
	 * Specifies if beacon early termination procedure is enabled or
	 * disabled.
	 *
	 * Range: CONF_BET_MODE_*
	 */
	u8 bet_enable;

	/*
	 * Specifies the maximum number of consecutive beacons that may be
	 * early terminated. After this number is reached at least one full
	 * beacon must be correctly received in FW before beacon ET
	 * resumes.
	 *
	 * Range 0 - 255
	 */
	u8 bet_max_consecutive;

	/*
	 * Specifies the maximum number of times to try PSM entry if it fails
	 * (if sending the appropriate null-func message fails.)
	 *
	 * Range 0 - 255
	 */
	u8 psm_entry_retries;

	/*
	 * Specifies the maximum number of times to try PSM exit if it fails
	 * (if sending the appropriate null-func message fails.)
	 *
	 * Range 0 - 255
	 */
	u8 psm_exit_retries;

	/*
	 * Specifies the maximum number of times to try transmit the PSM entry
	 * null-func frame for each PSM entry attempt
	 *
	 * Range 0 - 255
	 */
	u8 psm_entry_nullfunc_retries;

	/*
	 * Specifies the time to linger in active mode after successfully
	 * transmitting the PSM entry null-func frame.
	 *
	 * Range 0 - 255 TU's
	 */
	u8 psm_entry_hangover_period;

	/*
	 *
	 * Specifies the interval of the connection keep-alive null-func
	 * frame in ms.
	 *
	 * Range: 1000 - 3600000
	 */
	u32 keep_alive_interval;

	/*
	 * Maximum listen interval supported by the driver in units of beacons.
	 *
	 * Range: u16
	 */
	u8 max_listen_interval;
};

enum {
	CONF_REF_CLK_19_2_E,
	CONF_REF_CLK_26_E,
	CONF_REF_CLK_38_4_E,
	CONF_REF_CLK_52_E,
	CONF_REF_CLK_38_4_M_XTAL,
	CONF_REF_CLK_26_M_XTAL,
};

enum single_dual_band_enum {
	CONF_SINGLE_BAND,
	CONF_DUAL_BAND
};

#define CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE 15
#define CONF_NUMBER_OF_SUB_BANDS_5  7
#define CONF_NUMBER_OF_RATE_GROUPS  6
#define CONF_NUMBER_OF_CHANNELS_2_4 14
#define CONF_NUMBER_OF_CHANNELS_5   35

struct conf_itrim_settings {
	/* enable dco itrim */
	u8 enable;

	/* moderation timeout in microsecs from the last TX */
	u32 timeout;
};

struct conf_pm_config_settings {
	/*
	 * Host clock settling time
	 *
	 * Range: 0 - 30000 us
	 */
	u32 host_clk_settling_time;

	/*
	 * Host fast wakeup support
	 *
	 * Range: true, false
	 */
	bool host_fast_wakeup_support;
};

struct conf_roam_trigger_settings {
	/*
	 * The minimum interval between two trigger events.
	 *
	 * Range: 0 - 60000 ms
	 */
	u16 trigger_pacing;

	/*
	 * The weight for rssi/beacon average calculation
	 *
	 * Range: 0 - 255
	 */
	u8 avg_weight_rssi_beacon;

	/*
	 * The weight for rssi/data frame average calculation
	 *
	 * Range: 0 - 255
	 */
	u8 avg_weight_rssi_data;

	/*
	 * The weight for snr/beacon average calculation
	 *
	 * Range: 0 - 255
	 */
	u8 avg_weight_snr_beacon;

	/*
	 * The weight for snr/data frame average calculation
	 *
	 * Range: 0 - 255
	 */
	u8 avg_weight_snr_data;
};

struct conf_scan_settings {
	/*
	 * The minimum time to wait on each channel for active scans
	 *
	 * Range: u32 tu/1000
	 */
	u32 min_dwell_time_active;

	/*
	 * The maximum time to wait on each channel for active scans
	 *
	 * Range: u32 tu/1000
	 */
	u32 max_dwell_time_active;

	/*
	 * The minimum time to wait on each channel for passive scans
	 *
	 * Range: u32 tu/1000
	 */
	u32 min_dwell_time_passive;

	/*
	 * The maximum time to wait on each channel for passive scans
	 *
	 * Range: u32 tu/1000
	 */
	u32 max_dwell_time_passive;

	/*
	 * Number of probe requests to transmit on each active scan channel
	 *
	 * Range: u8
	 */
	u16 num_probe_reqs;

};

struct conf_sched_scan_settings {
	/* minimum time to wait on the channel for active scans (in TUs) */
	u16 min_dwell_time_active;

	/* maximum time to wait on the channel for active scans (in TUs) */
	u16 max_dwell_time_active;

	/* time to wait on the channel for passive scans (in TUs) */
	u32 dwell_time_passive;

	/* time to wait on the channel for DFS scans (in TUs) */
	u32 dwell_time_dfs;

	/* number of probe requests to send on each channel in active scans */
	u8 num_probe_reqs;

	/* RSSI threshold to be used for filtering */
	s8 rssi_threshold;

	/* SNR threshold to be used for filtering */
	s8 snr_threshold;
};

/* these are number of channels on the band divided by two, rounded up */
#define CONF_TX_PWR_COMPENSATION_LEN_2 7
#define CONF_TX_PWR_COMPENSATION_LEN_5 18

struct conf_rf_settings {
	/*
	 * Per channel power compensation for 2.4GHz
	 *
	 * Range: s8
	 */
	u8 tx_per_channel_power_compensation_2[CONF_TX_PWR_COMPENSATION_LEN_2];

	/*
	 * Per channel power compensation for 5GHz
	 *
	 * Range: s8
	 */
	u8 tx_per_channel_power_compensation_5[CONF_TX_PWR_COMPENSATION_LEN_5];
};

struct conf_ht_setting {
	u16 tx_ba_win_size;
	u16 inactivity_timeout;
};

struct conf_memory_settings {
	/* Number of stations supported in IBSS mode */
	u8 num_stations;

	/* Number of ssid profiles used in IBSS mode */
	u8 ssid_profiles;

	/* Number of memory buffers allocated to rx pool */
	u8 rx_block_num;

	/* Minimum number of blocks allocated to tx pool */
	u8 tx_min_block_num;

	/* Disable/Enable dynamic memory */
	u8 dynamic_memory;

	/*
	 * Minimum required free tx memory blocks in order to assure optimum
	 * performance
	 *
	 * Range: 0-120
	 */
	u8 min_req_tx_blocks;

	/*
	 * Minimum required free rx memory blocks in order to assure optimum
	 * performance
	 *
	 * Range: 0-120
	 */
	u8 min_req_rx_blocks;

	/*
	 * Minimum number of mem blocks (free+used) guaranteed for TX
	 *
	 * Range: 0-120
	 */
	u8 tx_min;
};

struct conf_fm_coex {
	u8 enable;
	u8 swallow_period;
	u8 n_divider_fref_set_1;
	u8 n_divider_fref_set_2;
	u16 m_divider_fref_set_1;
	u16 m_divider_fref_set_2;
	u32 coex_pll_stabilization_time;
	u16 ldo_stabilization_time;
	u8 fm_disturbed_band_margin;
	u8 swallow_clk_diff;
};

struct conf_rx_streaming_settings {
	/*
	 * RX Streaming duration (in msec) from last tx/rx
	 *
	 * Range: u32
	 */
	u32 duration;

	/*
	 * Bitmap of tids to be polled during RX streaming.
	 * (Note: it doesn't look like it really matters)
	 *
	 * Range: 0x1-0xff
	 */
	u8 queues;

	/*
	 * RX Streaming interval.
	 * (Note:this value is also used as the rx streaming timeout)
	 * Range: 0 (disabled), 10 - 100
	 */
	u8 interval;

	/*
	 * enable rx streaming also when there is no coex activity
	 */
	u8 always;
};

struct conf_fwlog {
	/* Continuous or on-demand */
	u8 mode;

	/*
	 * Number of memory blocks dedicated for the FW logger
	 *
	 * Range: 1-3, or 0 to disable the FW logger
	 */
	u8 mem_blocks;

	/* Minimum log level threshold */
	u8 severity;

	/* Include/exclude timestamps from the log messages */
	u8 timestamp;

	/* See enum wl1271_fwlogger_output */
	u8 output;

	/* Regulates the frequency of log messages */
	u8 threshold;
};

struct conf_drv_settings {
	struct conf_sg_settings sg;
	struct conf_rx_settings rx;
	struct conf_tx_settings tx;
	struct conf_conn_settings conn;
	struct conf_itrim_settings itrim;
	struct conf_pm_config_settings pm_config;
	struct conf_roam_trigger_settings roam_trigger;
	struct conf_scan_settings scan;
	struct conf_sched_scan_settings sched_scan;
	struct conf_rf_settings rf;
	struct conf_ht_setting ht;
	struct conf_memory_settings mem_wl127x;
	struct conf_memory_settings mem_wl128x;
	struct conf_fm_coex fm_coex;
	struct conf_rx_streaming_settings rx_streaming;
	struct conf_fwlog fwlog;
	u8 hci_io_ds;
};

#endif

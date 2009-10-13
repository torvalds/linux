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

#ifndef __WL1271_CONF_H__
#define __WL1271_CONF_H__

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

struct conf_sg_settings {
	/*
	 * Defines the PER threshold in PPM of the BT voice of which reaching
	 * this value will trigger raising the priority of the BT voice by
	 * the BT IP until next NFS sample interval time as defined in
	 * nfs_sample_interval.
	 *
	 * Unit: PER value in PPM (parts per million)
	 * #Error_packets / #Total_packets

	 * Range: u32
	 */
	u32 per_threshold;

	/*
	 * This value is an absolute time in micro-seconds to limit the
	 * maximum scan duration compensation while in SG
	 */
	u32 max_scan_compensation_time;

	/* Defines the PER threshold of the BT voice of which reaching this
	 * value will trigger raising the priority of the BT voice until next
	 * NFS sample interval time as defined in sample_interval.
	 *
	 * Unit: msec
	 * Range: 1-65000
	 */
	u16 nfs_sample_interval;

	/*
	 * Defines the load ratio for the BT.
	 * The WLAN ratio is: 100 - load_ratio
	 *
	 * Unit: Percent
	 * Range: 0-100
	 */
	u8 load_ratio;

	/*
	 * true - Co-ex is allowed to enter/exit P.S automatically and
	 *        transparently to the host
	 *
	 * false - Co-ex is disallowed to enter/exit P.S and will trigger an
	 *         event to the host to notify for the need to enter/exit P.S
	 *         due to BT change state
	 *
	 */
	u8 auto_ps_mode;

	/*
	 * This parameter defines the compensation percentage of num of probe
	 * requests in case scan is initiated during BT voice/BT ACL
	 * guaranteed link.
	 *
	 * Unit: Percent
	 * Range: 0-255 (0 - No compensation)
	 */
	u8 probe_req_compensation;

	/*
	 * This parameter defines the compensation percentage of scan window
	 * size in case scan is initiated during BT voice/BT ACL Guaranteed
	 * link.
	 *
	 * Unit: Percent
	 * Range: 0-255 (0 - No compensation)
	 */
	u8 scan_window_compensation;

	/*
	 * Defines the antenna configuration.
	 *
	 * Range: 0 - Single Antenna; 1 - Dual Antenna
	 */
	u8 antenna_config;

	/*
	 * The percent out of the Max consecutive beacon miss roaming trigger
	 * which is the threshold for raising the priority of beacon
	 * reception.
	 *
	 * Range: 1-100
	 * N = MaxConsecutiveBeaconMiss
	 * P = coexMaxConsecutiveBeaconMissPrecent
	 * Threshold = MIN( N-1, round(N * P / 100))
	 */
	u8 beacon_miss_threshold;

	/*
	 * The RX rate threshold below which rate adaptation is assumed to be
	 * occurring at the AP which will raise priority for ACTIVE_RX and RX
	 * SP.
	 *
	 * Range: HW_BIT_RATE_*
	 */
	u32 rate_adaptation_threshold;

	/*
	 * The SNR above which the RX rate threshold indicating AP rate
	 * adaptation is valid
	 *
	 * Range: -128 - 127
	 */
	s8 rate_adaptation_snr;
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
#define CONF_TX_RATE_MASK_ALL          0x1eff
#define CONF_TX_RATE_RETRY_LIMIT       10

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
	CONF_TX_AC_CTS2SELF = 4,   /* fictious AC, follows AC_VO */
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

#define CONF_TX_MAX_TID_COUNT 7

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
	 * rate class supported.)
	 */
	struct conf_tx_rate_class rc_conf;

	/*
	 * Configuration for access categories for TX rate control.
	 */
	u8 ac_conf_count;
	struct conf_tx_ac_category ac_conf[CONF_TX_MAX_AC_COUNT];

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


struct conf_sig_trigger {
	/*
	 * The RSSI / SNR threshold value.
	 *
	 * FIXME: what is the range?
	 */
	s16 threshold;

	/*
	 * Minimum delay between two trigger events for this trigger in ms.
	 *
	 * Range: 0 - 60000
	 */
	u16 pacing;

	/*
	 * The measurement data source for this trigger.
	 *
	 * Range: CONF_TRIG_METRIC_*
	 */
	u8 metric;

	/*
	 * The trigger type of this trigger.
	 *
	 * Range: CONF_TRIG_EVENT_TYPE_*
	 */
	u8 type;

	/*
	 * The direction of the trigger.
	 *
	 * Range: CONF_TRIG_EVENT_DIR_*
	 */
	u8 direction;

	/*
	 * Hysteresis range of the trigger around the threshold (in dB)
	 *
	 * Range: u8
	 */
	u8 hysteresis;

	/*
	 * Index of the trigger rule.
	 *
	 * Range: 0 - CONF_MAX_RSSI_SNR_TRIGGERS-1
	 */
	u8 index;

	/*
	 * Enable / disable this rule (to use for clearing rules.)
	 *
	 * Range: 1 - Enabled, 2 - Not enabled
	 */
	u8 enable;
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
	 * Configuration of signal (rssi/snr) triggers.
	 */
	u8 sig_trigger_count;
	struct conf_sig_trigger sig_trigger[CONF_MAX_RSSI_SNR_TRIGGERS];

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
};

#define CONF_SR_ERR_TBL_MAX_VALUES   14

struct conf_mart_reflex_err_table {
	/*
	 * Length of the error table values table.
	 *
	 * Range: 0 - CONF_SR_ERR_TBL_MAX_VALUES
	 */
	u8 len;

	/*
	 * Smart Reflex error table upper limit.
	 *
	 * Range: s8
	 */
	s8 upper_limit;

	/*
	 * Smart Reflex error table values.
	 *
	 * Range: s8
	 */
	s8 values[CONF_SR_ERR_TBL_MAX_VALUES];
};

enum {
	CONF_REF_CLK_19_2_E,
	CONF_REF_CLK_26_E,
	CONF_REF_CLK_38_4_E,
	CONF_REF_CLK_52_E
};

enum single_dual_band_enum {
	CONF_SINGLE_BAND,
	CONF_DUAL_BAND
};

struct conf_general_parms {
	/*
	 * RF Reference Clock type / speed
	 *
	 * Range: CONF_REF_CLK_*
	 */
	u8 ref_clk;

	/*
	 * Settling time of the reference clock after boot.
	 *
	 * Range: u8
	 */
	u8 settling_time;

	/*
	 * Flag defining whether clock is valid on wakeup.
	 *
	 * Range: 0 - not valid on wakeup, 1 - valid on wakeup
	 */
	u8 clk_valid_on_wakeup;

	/*
	 * DC-to-DC mode.
	 *
	 * Range: Unknown
	 */
	u8 dc2dcmode;

	/*
	 * Flag defining whether used as single or dual-band.
	 *
	 * Range: CONF_SINGLE_BAND, CONF_DUAL_BAND
	 */
	u8 single_dual_band;

	/*
	 * TX bip fem autodetect flag.
	 *
	 * Range: Unknown
	 */
	u8 tx_bip_fem_autodetect;

	/*
	 * TX bip gem manufacturer.
	 *
	 * Range: Unknown
	 */
	u8 tx_bip_fem_manufacturer;

	/*
	 * Settings flags.
	 *
	 * Range: Unknown
	 */
	u8 settings;
};

#define CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE 15
#define CONF_NUMBER_OF_SUB_BANDS_5  7
#define CONF_NUMBER_OF_RATE_GROUPS  6
#define CONF_NUMBER_OF_CHANNELS_2_4 14
#define CONF_NUMBER_OF_CHANNELS_5   35

struct conf_radio_parms {
	/*
	 * Static radio parameters for 2.4GHz
	 *
	 * Range: unknown
	 */
	u8 rx_trace_loss;
	u8 tx_trace_loss;
	s8 rx_rssi_and_proc_compens[CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE];

	/*
	 * Static radio parameters for 5GHz
	 *
	 * Range: unknown
	 */
	u8 rx_trace_loss_5[CONF_NUMBER_OF_SUB_BANDS_5];
	u8 tx_trace_loss_5[CONF_NUMBER_OF_SUB_BANDS_5];
	s8 rx_rssi_and_proc_compens_5[CONF_RSSI_AND_PROCESS_COMPENSATION_SIZE];

	/*
	 * Dynamic radio parameters for 2.4GHz
	 *
	 * Range: unknown
	 */
	s16 tx_ref_pd_voltage;
	s8  tx_ref_power;
	s8  tx_offset_db;

	s8  tx_rate_limits_normal[CONF_NUMBER_OF_RATE_GROUPS];
	s8  tx_rate_limits_degraded[CONF_NUMBER_OF_RATE_GROUPS];

	s8  tx_channel_limits_11b[CONF_NUMBER_OF_CHANNELS_2_4];
	s8  tx_channel_limits_ofdm[CONF_NUMBER_OF_CHANNELS_2_4];
	s8  tx_pdv_rate_offsets[CONF_NUMBER_OF_RATE_GROUPS];

	u8  tx_ibias[CONF_NUMBER_OF_RATE_GROUPS];
	u8  rx_fem_insertion_loss;

	/*
	 * Dynamic radio parameters for 5GHz
	 *
	 * Range: unknown
	 */
	s16 tx_ref_pd_voltage_5[CONF_NUMBER_OF_SUB_BANDS_5];
	s8  tx_ref_power_5[CONF_NUMBER_OF_SUB_BANDS_5];
	s8  tx_offset_db_5[CONF_NUMBER_OF_SUB_BANDS_5];

	s8  tx_rate_limits_normal_5[CONF_NUMBER_OF_RATE_GROUPS];
	s8  tx_rate_limits_degraded_5[CONF_NUMBER_OF_RATE_GROUPS];

	s8  tx_channel_limits_ofdm_5[CONF_NUMBER_OF_CHANNELS_5];
	s8  tx_pdv_rate_offsets_5[CONF_NUMBER_OF_RATE_GROUPS];

	/* FIXME: this is inconsistent with the types for 2.4GHz */
	s8  tx_ibias_5[CONF_NUMBER_OF_RATE_GROUPS];
	s8  rx_fem_insertion_loss_5[CONF_NUMBER_OF_SUB_BANDS_5];
};

#define CONF_SR_ERR_TBL_COUNT        3

struct conf_init_settings {
	/*
	 * Configure Smart Reflex error table values.
	 */
	struct conf_mart_reflex_err_table sr_err_tbl[CONF_SR_ERR_TBL_COUNT];

	/*
	 * Smart Reflex enable flag.
	 *
	 * Range: 1 - Smart Reflex enabled, 0 - Smart Reflex disabled
	 */
	u8 sr_enable;

	/*
	 * Configure general parameters.
	 */
	struct conf_general_parms genparam;

	/*
	 * Configure radio parameters.
	 */
	struct conf_radio_parms radioparam;

};

struct conf_drv_settings {
	struct conf_sg_settings sg;
	struct conf_rx_settings rx;
	struct conf_tx_settings tx;
	struct conf_conn_settings conn;
	struct conf_init_settings init;
};

#endif

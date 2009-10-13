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

struct conf_drv_settings {
	struct conf_sg_settings sg;
	struct conf_rx_settings rx;
	struct conf_tx_settings tx;
};

#endif

/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU Geeral Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#ifndef __iwl_commands_h__
#define __iwl_commands_h__

enum {
	REPLY_ALIVE = 0x1,
	REPLY_ERROR = 0x2,

	/* RXON and QOS commands */
	REPLY_RXON = 0x10,
	REPLY_RXON_ASSOC = 0x11,
	REPLY_QOS_PARAM = 0x13,
	REPLY_RXON_TIMING = 0x14,

	/* Multi-Station support */
	REPLY_ADD_STA = 0x18,
	REPLY_REMOVE_STA = 0x19,	/* not used */
	REPLY_REMOVE_ALL_STA = 0x1a,	/* not used */

	/* RX, TX, LEDs */
#if IWL == 3945
	REPLY_3945_RX = 0x1b,		/* 3945 only */
#endif
	REPLY_TX = 0x1c,
	REPLY_RATE_SCALE = 0x47,	/* 3945 only */
	REPLY_LEDS_CMD = 0x48,
	REPLY_TX_LINK_QUALITY_CMD = 0x4e, /* 4965 only */

	/* 802.11h related */
	RADAR_NOTIFICATION = 0x70,	/* not used */
	REPLY_QUIET_CMD = 0x71,		/* not used */
	REPLY_CHANNEL_SWITCH = 0x72,
	CHANNEL_SWITCH_NOTIFICATION = 0x73,
	REPLY_SPECTRUM_MEASUREMENT_CMD = 0x74,
	SPECTRUM_MEASURE_NOTIFICATION = 0x75,

	/* Power Management */
	POWER_TABLE_CMD = 0x77,
	PM_SLEEP_NOTIFICATION = 0x7A,
	PM_DEBUG_STATISTIC_NOTIFIC = 0x7B,

	/* Scan commands and notifications */
	REPLY_SCAN_CMD = 0x80,
	REPLY_SCAN_ABORT_CMD = 0x81,
	SCAN_START_NOTIFICATION = 0x82,
	SCAN_RESULTS_NOTIFICATION = 0x83,
	SCAN_COMPLETE_NOTIFICATION = 0x84,

	/* IBSS/AP commands */
	BEACON_NOTIFICATION = 0x90,
	REPLY_TX_BEACON = 0x91,
	WHO_IS_AWAKE_NOTIFICATION = 0x94,	/* not used */

	/* Miscellaneous commands */
	QUIET_NOTIFICATION = 0x96,		/* not used */
	REPLY_TX_PWR_TABLE_CMD = 0x97,
	MEASURE_ABORT_NOTIFICATION = 0x99,	/* not used */

	/* BT config command */
	REPLY_BT_CONFIG = 0x9b,

	/* 4965 Statistics */
	REPLY_STATISTICS_CMD = 0x9c,
	STATISTICS_NOTIFICATION = 0x9d,

	/* RF-KILL commands and notifications */
	REPLY_CARD_STATE_CMD = 0xa0,
	CARD_STATE_NOTIFICATION = 0xa1,

	/* Missed beacons notification */
	MISSED_BEACONS_NOTIFICATION = 0xa2,

#if IWL == 4965
	REPLY_CT_KILL_CONFIG_CMD = 0xa4,
	SENSITIVITY_CMD = 0xa8,
	REPLY_PHY_CALIBRATION_CMD = 0xb0,
	REPLY_RX_PHY_CMD = 0xc0,
	REPLY_RX_MPDU_CMD = 0xc1,
	REPLY_4965_RX = 0xc3,
	REPLY_COMPRESSED_BA = 0xc5,
#endif
	REPLY_MAX = 0xff
};

/******************************************************************************
 * (0)
 * Header
 *
 *****************************************************************************/

#define IWL_CMD_FAILED_MSK 0x40

struct iwl_cmd_header {
	u8 cmd;
	u8 flags;
	/* We have 15 LSB to use as we please (MSB indicates
	 * a frame Rx'd from the HW).  We encode the following
	 * information into the sequence field:
	 *
	 *  0:7    index in fifo
	 *  8:13   fifo selection
	 * 14:14   bit indicating if this packet references the 'extra'
	 *         storage at the end of the memory queue
	 * 15:15   (Rx indication)
	 *
	 */
	__le16 sequence;

	/* command data follows immediately */
	u8 data[0];
} __attribute__ ((packed));

/******************************************************************************
 * (0a)
 * Alive and Error Commands & Responses:
 *
 *****************************************************************************/

#define UCODE_VALID_OK	__constant_cpu_to_le32(0x1)
#define INITIALIZE_SUBTYPE    (9)

/*
 * REPLY_ALIVE = 0x1 (response only, not a command)
 */
struct iwl_alive_resp {
	u8 ucode_minor;
	u8 ucode_major;
	__le16 reserved1;
	u8 sw_rev[8];
	u8 ver_type;
	u8 ver_subtype;
	__le16 reserved2;
	__le32 log_event_table_ptr;
	__le32 error_event_table_ptr;
	__le32 timestamp;
	__le32 is_valid;
} __attribute__ ((packed));

struct iwl_init_alive_resp {
	u8 ucode_minor;
	u8 ucode_major;
	__le16 reserved1;
	u8 sw_rev[8];
	u8 ver_type;
	u8 ver_subtype;
	__le16 reserved2;
	__le32 log_event_table_ptr;
	__le32 error_event_table_ptr;
	__le32 timestamp;
	__le32 is_valid;

#if IWL == 4965
	/* calibration values from "initialize" uCode */
	__le32 voltage;		/* signed */
	__le32 therm_r1[2];	/* signed 1st for normal, 2nd for FAT channel */
	__le32 therm_r2[2];	/* signed */
	__le32 therm_r3[2];	/* signed */
	__le32 therm_r4[2];	/* signed */
	__le32 tx_atten[5][2];	/* signed MIMO gain comp, 5 freq groups,
				 * 2 Tx chains */
#endif
} __attribute__ ((packed));

union tsf {
	u8 byte[8];
	__le16 word[4];
	__le32 dw[2];
};

/*
 * REPLY_ERROR = 0x2 (response only, not a command)
 */
struct iwl_error_resp {
	__le32 error_type;
	u8 cmd_id;
	u8 reserved1;
	__le16 bad_cmd_seq_num;
#if IWL == 3945
	__le16 reserved2;
#endif
	__le32 error_info;
	union tsf timestamp;
} __attribute__ ((packed));

/******************************************************************************
 * (1)
 * RXON Commands & Responses:
 *
 *****************************************************************************/

/*
 * Rx config defines & structure
 */
/* rx_config device types  */
enum {
	RXON_DEV_TYPE_AP = 1,
	RXON_DEV_TYPE_ESS = 3,
	RXON_DEV_TYPE_IBSS = 4,
	RXON_DEV_TYPE_SNIFFER = 6,
};

/* rx_config flags */
/* band & modulation selection */
#define RXON_FLG_BAND_24G_MSK           __constant_cpu_to_le32(1 << 0)
#define RXON_FLG_CCK_MSK                __constant_cpu_to_le32(1 << 1)
/* auto detection enable */
#define RXON_FLG_AUTO_DETECT_MSK        __constant_cpu_to_le32(1 << 2)
/* TGg protection when tx */
#define RXON_FLG_TGG_PROTECT_MSK        __constant_cpu_to_le32(1 << 3)
/* cck short slot & preamble */
#define RXON_FLG_SHORT_SLOT_MSK          __constant_cpu_to_le32(1 << 4)
#define RXON_FLG_SHORT_PREAMBLE_MSK     __constant_cpu_to_le32(1 << 5)
/* antenna selection */
#define RXON_FLG_DIS_DIV_MSK            __constant_cpu_to_le32(1 << 7)
#define RXON_FLG_ANT_SEL_MSK            __constant_cpu_to_le32(0x0f00)
#define RXON_FLG_ANT_A_MSK              __constant_cpu_to_le32(1 << 8)
#define RXON_FLG_ANT_B_MSK              __constant_cpu_to_le32(1 << 9)
/* radar detection enable */
#define RXON_FLG_RADAR_DETECT_MSK       __constant_cpu_to_le32(1 << 12)
#define RXON_FLG_TGJ_NARROW_BAND_MSK    __constant_cpu_to_le32(1 << 13)
/* rx response to host with 8-byte TSF
* (according to ON_AIR deassertion) */
#define RXON_FLG_TSF2HOST_MSK           __constant_cpu_to_le32(1 << 15)

/* rx_config filter flags */
/* accept all data frames */
#define RXON_FILTER_PROMISC_MSK         __constant_cpu_to_le32(1 << 0)
/* pass control & management to host */
#define RXON_FILTER_CTL2HOST_MSK        __constant_cpu_to_le32(1 << 1)
/* accept multi-cast */
#define RXON_FILTER_ACCEPT_GRP_MSK      __constant_cpu_to_le32(1 << 2)
/* don't decrypt uni-cast frames */
#define RXON_FILTER_DIS_DECRYPT_MSK     __constant_cpu_to_le32(1 << 3)
/* don't decrypt multi-cast frames */
#define RXON_FILTER_DIS_GRP_DECRYPT_MSK __constant_cpu_to_le32(1 << 4)
/* STA is associated */
#define RXON_FILTER_ASSOC_MSK           __constant_cpu_to_le32(1 << 5)
/* transfer to host non bssid beacons in associated state */
#define RXON_FILTER_BCON_AWARE_MSK      __constant_cpu_to_le32(1 << 6)

/*
 * REPLY_RXON = 0x10 (command, has simple generic response)
 */
struct iwl_rxon_cmd {
	u8 node_addr[6];
	__le16 reserved1;
	u8 bssid_addr[6];
	__le16 reserved2;
	u8 wlap_bssid_addr[6];
	__le16 reserved3;
	u8 dev_type;
	u8 air_propagation;
#if IWL == 3945
	__le16 reserved4;
#elif IWL == 4965
	__le16 rx_chain;
#endif
	u8 ofdm_basic_rates;
	u8 cck_basic_rates;
	__le16 assoc_id;
	__le32 flags;
	__le32 filter_flags;
	__le16 channel;
#if IWL == 3945
	__le16 reserved5;
#elif IWL == 4965
	u8 ofdm_ht_single_stream_basic_rates;
	u8 ofdm_ht_dual_stream_basic_rates;
#endif
} __attribute__ ((packed));

/*
 * REPLY_RXON_ASSOC = 0x11 (command, has simple generic response)
 */
struct iwl_rxon_assoc_cmd {
	__le32 flags;
	__le32 filter_flags;
	u8 ofdm_basic_rates;
	u8 cck_basic_rates;
#if IWL == 4965
	u8 ofdm_ht_single_stream_basic_rates;
	u8 ofdm_ht_dual_stream_basic_rates;
	__le16 rx_chain_select_flags;
#endif
	__le16 reserved;
} __attribute__ ((packed));

/*
 * REPLY_RXON_TIMING = 0x14 (command, has simple generic response)
 */
struct iwl_rxon_time_cmd {
	union tsf timestamp;
	__le16 beacon_interval;
	__le16 atim_window;
	__le32 beacon_init_val;
	__le16 listen_interval;
	__le16 reserved;
} __attribute__ ((packed));

struct iwl_tx_power {
	u8 tx_gain;		/* gain for analog radio */
	u8 dsp_atten;		/* gain for DSP */
} __attribute__ ((packed));

#if IWL == 3945
struct iwl_power_per_rate {
	u8 rate;		/* plcp */
	struct iwl_tx_power tpc;
	u8 reserved;
} __attribute__ ((packed));

#elif IWL == 4965
#define POWER_TABLE_NUM_ENTRIES			33
#define POWER_TABLE_NUM_HT_OFDM_ENTRIES		32
#define POWER_TABLE_CCK_ENTRY			32
struct tx_power_dual_stream {
	__le32 dw;
} __attribute__ ((packed));

struct iwl_tx_power_db {
	struct tx_power_dual_stream power_tbl[POWER_TABLE_NUM_ENTRIES];
} __attribute__ ((packed));
#endif

/*
 * REPLY_CHANNEL_SWITCH = 0x72 (command, has simple generic response)
 */
struct iwl_channel_switch_cmd {
	u8 band;
	u8 expect_beacon;
	__le16 channel;
	__le32 rxon_flags;
	__le32 rxon_filter_flags;
	__le32 switch_time;
#if IWL == 3945
	struct iwl_power_per_rate power[IWL_MAX_RATES];
#elif IWL == 4965
	struct iwl_tx_power_db tx_power;
#endif
} __attribute__ ((packed));

/*
 * CHANNEL_SWITCH_NOTIFICATION = 0x73 (notification only, not a command)
 */
struct iwl_csa_notification {
	__le16 band;
	__le16 channel;
	__le32 status;		/* 0 - OK, 1 - fail */
} __attribute__ ((packed));

/******************************************************************************
 * (2)
 * Quality-of-Service (QOS) Commands & Responses:
 *
 *****************************************************************************/
struct iwl_ac_qos {
	__le16 cw_min;
	__le16 cw_max;
	u8 aifsn;
	u8 reserved1;
	__le16 edca_txop;
} __attribute__ ((packed));

/* QoS flags defines */
#define QOS_PARAM_FLG_UPDATE_EDCA_MSK	__constant_cpu_to_le32(0x01)
#define QOS_PARAM_FLG_TGN_MSK		__constant_cpu_to_le32(0x02)
#define QOS_PARAM_FLG_TXOP_TYPE_MSK	__constant_cpu_to_le32(0x10)

/*
 *  TXFIFO Queue number defines
 */
/* number of Access categories (AC) (EDCA), queues 0..3 */
#define AC_NUM                4

/*
 * REPLY_QOS_PARAM = 0x13 (command, has simple generic response)
 */
struct iwl_qosparam_cmd {
	__le32 qos_flags;
	struct iwl_ac_qos ac[AC_NUM];
} __attribute__ ((packed));

/******************************************************************************
 * (3)
 * Add/Modify Stations Commands & Responses:
 *
 *****************************************************************************/
/*
 * Multi station support
 */
#define	IWL_AP_ID		0
#define IWL_MULTICAST_ID	1
#define	IWL_STA_ID		2

#define	IWL3945_BROADCAST_ID	24
#define IWL3945_STATION_COUNT	25

#define IWL4965_BROADCAST_ID	31
#define	IWL4965_STATION_COUNT	32

#define	IWL_STATION_COUNT	32 	/* MAX(3945,4965)*/
#define	IWL_INVALID_STATION 	255

#if IWL == 3945
#define STA_FLG_TX_RATE_MSK		__constant_cpu_to_le32(1<<2);
#endif
#define STA_FLG_PWR_SAVE_MSK		__constant_cpu_to_le32(1<<8);

#define STA_CONTROL_MODIFY_MSK		0x01

/* key flags __le16*/
#define STA_KEY_FLG_ENCRYPT_MSK	__constant_cpu_to_le16(0x7)
#define STA_KEY_FLG_NO_ENC	__constant_cpu_to_le16(0x0)
#define STA_KEY_FLG_WEP		__constant_cpu_to_le16(0x1)
#define STA_KEY_FLG_CCMP	__constant_cpu_to_le16(0x2)
#define STA_KEY_FLG_TKIP	__constant_cpu_to_le16(0x3)

#define STA_KEY_FLG_KEYID_POS	8
#define STA_KEY_FLG_INVALID 	__constant_cpu_to_le16(0x0800)

/* modify flags  */
#define	STA_MODIFY_KEY_MASK		0x01
#define	STA_MODIFY_TID_DISABLE_TX	0x02
#define	STA_MODIFY_TX_RATE_MSK		0x04
#define STA_MODIFY_ADDBA_TID_MSK	0x08
#define STA_MODIFY_DELBA_TID_MSK	0x10
#define BUILD_RAxTID(sta_id, tid)	(((sta_id) << 4) + (tid))

/*
 * Antenna masks:
 * bit14:15 01 B inactive, A active
 *          10 B active, A inactive
 *          11 Both active
 */
#define RATE_MCS_ANT_A_POS	14
#define RATE_MCS_ANT_B_POS	15
#define RATE_MCS_ANT_A_MSK	0x4000
#define RATE_MCS_ANT_B_MSK	0x8000
#define RATE_MCS_ANT_AB_MSK	0xc000

struct iwl_keyinfo {
	__le16 key_flags;
	u8 tkip_rx_tsc_byte2;	/* TSC[2] for key mix ph1 detection */
	u8 reserved1;
	__le16 tkip_rx_ttak[5];	/* 10-byte unicast TKIP TTAK */
	__le16 reserved2;
	u8 key[16];		/* 16-byte unicast decryption key */
} __attribute__ ((packed));

struct sta_id_modify {
	u8 addr[ETH_ALEN];
	__le16 reserved1;
	u8 sta_id;
	u8 modify_mask;
	__le16 reserved2;
} __attribute__ ((packed));

/*
 * REPLY_ADD_STA = 0x18 (command)
 */
struct iwl_addsta_cmd {
	u8 mode;
	u8 reserved[3];
	struct sta_id_modify sta;
	struct iwl_keyinfo key;
	__le32 station_flags;
	__le32 station_flags_msk;
	__le16 tid_disable_tx;
#if IWL == 3945
	__le16 rate_n_flags;
#else
	__le16	reserved1;
#endif
	u8 add_immediate_ba_tid;
	u8 remove_immediate_ba_tid;
	__le16 add_immediate_ba_ssn;
#if IWL == 4965
	__le32 reserved2;
#endif
} __attribute__ ((packed));

/*
 * REPLY_ADD_STA = 0x18 (response)
 */
struct iwl_add_sta_resp {
	u8 status;
} __attribute__ ((packed));

#define ADD_STA_SUCCESS_MSK              0x1

/******************************************************************************
 * (4)
 * Rx Responses:
 *
 *****************************************************************************/

struct iwl_rx_frame_stats {
	u8 phy_count;
	u8 id;
	u8 rssi;
	u8 agc;
	__le16 sig_avg;
	__le16 noise_diff;
	u8 payload[0];
} __attribute__ ((packed));

struct iwl_rx_frame_hdr {
	__le16 channel;
	__le16 phy_flags;
	u8 reserved1;
	u8 rate;
	__le16 len;
	u8 payload[0];
} __attribute__ ((packed));

#define	RX_RES_STATUS_NO_CRC32_ERROR	__constant_cpu_to_le32(1 << 0)
#define	RX_RES_STATUS_NO_RXE_OVERFLOW	__constant_cpu_to_le32(1 << 1)

#define	RX_RES_PHY_FLAGS_BAND_24_MSK	__constant_cpu_to_le16(1 << 0)
#define	RX_RES_PHY_FLAGS_MOD_CCK_MSK		__constant_cpu_to_le16(1 << 1)
#define	RX_RES_PHY_FLAGS_SHORT_PREAMBLE_MSK	__constant_cpu_to_le16(1 << 2)
#define	RX_RES_PHY_FLAGS_NARROW_BAND_MSK	__constant_cpu_to_le16(1 << 3)
#define	RX_RES_PHY_FLAGS_ANTENNA_MSK		__constant_cpu_to_le16(0xf0)

#define	RX_RES_STATUS_SEC_TYPE_MSK	(0x7 << 8)
#define	RX_RES_STATUS_SEC_TYPE_NONE	(0x0 << 8)
#define	RX_RES_STATUS_SEC_TYPE_WEP	(0x1 << 8)
#define	RX_RES_STATUS_SEC_TYPE_CCMP	(0x2 << 8)
#define	RX_RES_STATUS_SEC_TYPE_TKIP	(0x3 << 8)

#define	RX_RES_STATUS_DECRYPT_TYPE_MSK	(0x3 << 11)
#define	RX_RES_STATUS_NOT_DECRYPT	(0x0 << 11)
#define	RX_RES_STATUS_DECRYPT_OK	(0x3 << 11)
#define	RX_RES_STATUS_BAD_ICV_MIC	(0x1 << 11)
#define	RX_RES_STATUS_BAD_KEY_TTAK	(0x2 << 11)

struct iwl_rx_frame_end {
	__le32 status;
	__le64 timestamp;
	__le32 beacon_timestamp;
} __attribute__ ((packed));

/*
 * REPLY_3945_RX = 0x1b (response only, not a command)
 *
 * NOTE:  DO NOT dereference from casts to this structure
 * It is provided only for calculating minimum data set size.
 * The actual offsets of the hdr and end are dynamic based on
 * stats.phy_count
 */
struct iwl_rx_frame {
	struct iwl_rx_frame_stats stats;
	struct iwl_rx_frame_hdr hdr;
	struct iwl_rx_frame_end end;
} __attribute__ ((packed));

/* Fixed (non-configurable) rx data from phy */
#define RX_PHY_FLAGS_ANTENNAE_OFFSET		(4)
#define RX_PHY_FLAGS_ANTENNAE_MASK		(0x70)
#define IWL_AGC_DB_MASK 	(0x3f80)	/* MASK(7,13) */
#define IWL_AGC_DB_POS		(7)
struct iwl4965_rx_non_cfg_phy {
	__le16 ant_selection;	/* ant A bit 4, ant B bit 5, ant C bit 6 */
	__le16 agc_info;	/* agc code 0:6, agc dB 7:13, reserved 14:15 */
	u8 rssi_info[6];	/* we use even entries, 0/2/4 for A/B/C rssi */
	u8 pad[0];
} __attribute__ ((packed));

/*
 * REPLY_4965_RX = 0xc3 (response only, not a command)
 * Used only for legacy (non 11n) frames.
 */
#define RX_RES_PHY_CNT 14
struct iwl4965_rx_phy_res {
	u8 non_cfg_phy_cnt;     /* non configurable DSP phy data byte count */
	u8 cfg_phy_cnt;		/* configurable DSP phy data byte count */
	u8 stat_id;		/* configurable DSP phy data set ID */
	u8 reserved1;
	__le64 timestamp;	/* TSF at on air rise */
	__le32 beacon_time_stamp; /* beacon at on-air rise */
	__le16 phy_flags;	/* general phy flags: band, modulation, ... */
	__le16 channel;		/* channel number */
	__le16 non_cfg_phy[RX_RES_PHY_CNT];	/* upto 14 phy entries */
	__le32 reserved2;
	__le32 rate_n_flags;
	__le16 byte_count;		/* frame's byte-count */
	__le16 reserved3;
} __attribute__ ((packed));

struct iwl4965_rx_mpdu_res_start {
	__le16 byte_count;
	__le16 reserved;
} __attribute__ ((packed));


/******************************************************************************
 * (5)
 * Tx Commands & Responses:
 *
 *****************************************************************************/

/* Tx flags */
#define TX_CMD_FLG_RTS_MSK __constant_cpu_to_le32(1 << 1)
#define TX_CMD_FLG_CTS_MSK __constant_cpu_to_le32(1 << 2)
#define TX_CMD_FLG_ACK_MSK __constant_cpu_to_le32(1 << 3)
#define TX_CMD_FLG_STA_RATE_MSK __constant_cpu_to_le32(1 << 4)
#define TX_CMD_FLG_IMM_BA_RSP_MASK  __constant_cpu_to_le32(1 << 6)
#define TX_CMD_FLG_FULL_TXOP_PROT_MSK __constant_cpu_to_le32(1 << 7)
#define TX_CMD_FLG_ANT_SEL_MSK __constant_cpu_to_le32(0xf00)
#define TX_CMD_FLG_ANT_A_MSK __constant_cpu_to_le32(1 << 8)
#define TX_CMD_FLG_ANT_B_MSK __constant_cpu_to_le32(1 << 9)

/* ucode ignores BT priority for this frame */
#define TX_CMD_FLG_BT_DIS_MSK __constant_cpu_to_le32(1 << 12)

/* ucode overrides sequence control */
#define TX_CMD_FLG_SEQ_CTL_MSK __constant_cpu_to_le32(1 << 13)

/* signal that this frame is non-last MPDU */
#define TX_CMD_FLG_MORE_FRAG_MSK __constant_cpu_to_le32(1 << 14)

/* calculate TSF in outgoing frame */
#define TX_CMD_FLG_TSF_MSK __constant_cpu_to_le32(1 << 16)

/* activate TX calibration. */
#define TX_CMD_FLG_CALIB_MSK __constant_cpu_to_le32(1 << 17)

/* signals that 2 bytes pad was inserted
   after the MAC header */
#define TX_CMD_FLG_MH_PAD_MSK __constant_cpu_to_le32(1 << 20)

/* HCCA-AP - disable duration overwriting. */
#define TX_CMD_FLG_DUR_MSK __constant_cpu_to_le32(1 << 25)

/*
 * TX command security control
 */
#define TX_CMD_SEC_WEP  	0x01
#define TX_CMD_SEC_CCM  	0x02
#define TX_CMD_SEC_TKIP		0x03
#define TX_CMD_SEC_MSK		0x03
#define TX_CMD_SEC_SHIFT	6
#define TX_CMD_SEC_KEY128	0x08

/*
 * TX command Frame life time
 */

struct iwl_dram_scratch {
	u8 try_cnt;
	u8 bt_kill_cnt;
	__le16 reserved;
} __attribute__ ((packed));

/*
 * REPLY_TX = 0x1c (command)
 */
struct iwl_tx_cmd {
	__le16 len;
	__le16 next_frame_len;
	__le32 tx_flags;
#if IWL == 3945
	u8 rate;
	u8 sta_id;
	u8 tid_tspec;
#elif IWL == 4965
	struct iwl_dram_scratch scratch;
	__le32 rate_n_flags;
	u8 sta_id;
#endif
	u8 sec_ctl;
#if IWL == 4965
	u8 initial_rate_index;
	u8 reserved;
#endif
	u8 key[16];
#if IWL == 3945
	union {
		u8 byte[8];
		__le16 word[4];
		__le32 dw[2];
	} tkip_mic;
	__le32 next_frame_info;
#elif IWL == 4965
	__le16 next_frame_flags;
	__le16 reserved2;
#endif
	union {
		__le32 life_time;
		__le32 attempt;
	} stop_time;
#if IWL == 3945
	u8 supp_rates[2];
#elif IWL == 4965
	__le32 dram_lsb_ptr;
	u8 dram_msb_ptr;
#endif
	u8 rts_retry_limit;	/*byte 50 */
	u8 data_retry_limit;	/*byte 51 */
#if IWL == 4965
	u8 tid_tspec;
#endif
	union {
		__le16 pm_frame_timeout;
		__le16 attempt_duration;
	} timeout;
	__le16 driver_txop;
	u8 payload[0];
	struct ieee80211_hdr hdr[0];
} __attribute__ ((packed));

/* TX command response is sent after *all* transmission attempts.
 *
 * NOTES:
 *
 * TX_STATUS_FAIL_NEXT_FRAG
 *
 * If the fragment flag in the MAC header for the frame being transmitted
 * is set and there is insufficient time to transmit the next frame, the
 * TX status will be returned with 'TX_STATUS_FAIL_NEXT_FRAG'.
 *
 * TX_STATUS_FIFO_UNDERRUN
 *
 * Indicates the host did not provide bytes to the FIFO fast enough while
 * a TX was in progress.
 *
 * TX_STATUS_FAIL_MGMNT_ABORT
 *
 * This status is only possible if the ABORT ON MGMT RX parameter was
 * set to true with the TX command.
 *
 * If the MSB of the status parameter is set then an abort sequence is
 * required.  This sequence consists of the host activating the TX Abort
 * control line, and then waiting for the TX Abort command response.  This
 * indicates that a the device is no longer in a transmit state, and that the
 * command FIFO has been cleared.  The host must then deactivate the TX Abort
 * control line.  Receiving is still allowed in this case.
 */
enum {
	TX_STATUS_SUCCESS = 0x01,
	TX_STATUS_DIRECT_DONE = 0x02,
	TX_STATUS_FAIL_SHORT_LIMIT = 0x82,
	TX_STATUS_FAIL_LONG_LIMIT = 0x83,
	TX_STATUS_FAIL_FIFO_UNDERRUN = 0x84,
	TX_STATUS_FAIL_MGMNT_ABORT = 0x85,
	TX_STATUS_FAIL_NEXT_FRAG = 0x86,
	TX_STATUS_FAIL_LIFE_EXPIRE = 0x87,
	TX_STATUS_FAIL_DEST_PS = 0x88,
	TX_STATUS_FAIL_ABORTED = 0x89,
	TX_STATUS_FAIL_BT_RETRY = 0x8a,
	TX_STATUS_FAIL_STA_INVALID = 0x8b,
	TX_STATUS_FAIL_FRAG_DROPPED = 0x8c,
	TX_STATUS_FAIL_TID_DISABLE = 0x8d,
	TX_STATUS_FAIL_FRAME_FLUSHED = 0x8e,
	TX_STATUS_FAIL_INSUFFICIENT_CF_POLL = 0x8f,
	TX_STATUS_FAIL_TX_LOCKED = 0x90,
	TX_STATUS_FAIL_NO_BEACON_ON_RADAR = 0x91,
};

#define	TX_PACKET_MODE_REGULAR		0x0000
#define	TX_PACKET_MODE_BURST_SEQ	0x0100
#define	TX_PACKET_MODE_BURST_FIRST	0x0200

enum {
	TX_POWER_PA_NOT_ACTIVE = 0x0,
};

enum {
	TX_STATUS_MSK = 0x000000ff,	/* bits 0:7 */
	TX_STATUS_DELAY_MSK = 0x00000040,
	TX_STATUS_ABORT_MSK = 0x00000080,
	TX_PACKET_MODE_MSK = 0x0000ff00,	/* bits 8:15 */
	TX_FIFO_NUMBER_MSK = 0x00070000,	/* bits 16:18 */
	TX_RESERVED = 0x00780000,	/* bits 19:22 */
	TX_POWER_PA_DETECT_MSK = 0x7f800000,	/* bits 23:30 */
	TX_ABORT_REQUIRED_MSK = 0x80000000,	/* bits 31:31 */
};

/* *******************************
 * TX aggregation state
 ******************************* */

enum {
	AGG_TX_STATE_TRANSMITTED = 0x00,
	AGG_TX_STATE_UNDERRUN_MSK = 0x01,
	AGG_TX_STATE_BT_PRIO_MSK = 0x02,
	AGG_TX_STATE_FEW_BYTES_MSK = 0x04,
	AGG_TX_STATE_ABORT_MSK = 0x08,
	AGG_TX_STATE_LAST_SENT_TTL_MSK = 0x10,
	AGG_TX_STATE_LAST_SENT_TRY_CNT_MSK = 0x20,
	AGG_TX_STATE_LAST_SENT_BT_KILL_MSK = 0x40,
	AGG_TX_STATE_SCD_QUERY_MSK = 0x80,
	AGG_TX_STATE_TEST_BAD_CRC32_MSK = 0x100,
	AGG_TX_STATE_RESPONSE_MSK = 0x1ff,
	AGG_TX_STATE_DUMP_TX_MSK = 0x200,
	AGG_TX_STATE_DELAY_TX_MSK = 0x400
};

#define AGG_TX_STATE_LAST_SENT_MSK \
(AGG_TX_STATE_LAST_SENT_TTL_MSK | \
 AGG_TX_STATE_LAST_SENT_TRY_CNT_MSK | \
 AGG_TX_STATE_LAST_SENT_BT_KILL_MSK)

#define AGG_TX_STATE_TRY_CNT_POS 12
#define AGG_TX_STATE_TRY_CNT_MSK 0xf000

#define AGG_TX_STATE_SEQ_NUM_POS 16
#define AGG_TX_STATE_SEQ_NUM_MSK 0xffff0000

/*
 * REPLY_TX = 0x1c (response)
 */
#if IWL == 4965
struct iwl_tx_resp {
	u8 frame_count;		/* 1 no aggregation, >1 aggregation */
	u8 bt_kill_count;
	u8 failure_rts;
	u8 failure_frame;
	__le32 rate_n_flags;
	__le16 wireless_media_time;
	__le16 reserved;
	__le32 pa_power1;
	__le32 pa_power2;
	__le32 status;	/* TX status (for aggregation status of 1st frame) */
} __attribute__ ((packed));

#elif IWL == 3945
struct iwl_tx_resp {
	u8 failure_rts;
	u8 failure_frame;
	u8 bt_kill_count;
	u8 rate;
	__le32 wireless_media_time;
	__le32 status;	/* TX status (for aggregation status of 1st frame) */
} __attribute__ ((packed));
#endif

/*
 * REPLY_COMPRESSED_BA = 0xc5 (response only, not a command)
 */
struct iwl_compressed_ba_resp {
	__le32 sta_addr_lo32;
	__le16 sta_addr_hi16;
	__le16 reserved;
	u8 sta_id;
	u8 tid;
	__le16 ba_seq_ctl;
	__le32 ba_bitmap0;
	__le32 ba_bitmap1;
	__le16 scd_flow;
	__le16 scd_ssn;
} __attribute__ ((packed));

/*
 * REPLY_TX_PWR_TABLE_CMD = 0x97 (command, has simple generic response)
 */
struct iwl_txpowertable_cmd {
	u8 band;		/* 0: 5 GHz, 1: 2.4 GHz */
	u8 reserved;
	__le16 channel;
#if IWL == 3945
	struct iwl_power_per_rate power[IWL_MAX_RATES];
#elif IWL == 4965
	struct iwl_tx_power_db tx_power;
#endif
} __attribute__ ((packed));

#if IWL == 3945
struct iwl_rate_scaling_info {
	__le16 rate_n_flags;
	u8 try_cnt;
	u8 next_rate_index;
} __attribute__ ((packed));

/**
 * struct iwl_rate_scaling_cmd - Rate Scaling Command & Response
 *
 * REPLY_RATE_SCALE = 0x47 (command, has simple generic response)
 *
 * NOTE: The table of rates passed to the uCode via the
 * RATE_SCALE command sets up the corresponding order of
 * rates used for all related commands, including rate
 * masks, etc.
 *
 * For example, if you set 9MB (PLCP 0x0f) as the first
 * rate in the rate table, the bit mask for that rate
 * when passed through ofdm_basic_rates on the REPLY_RXON
 * command would be bit 0 (1<<0)
 */
struct iwl_rate_scaling_cmd {
	u8 table_id;
	u8 reserved[3];
	struct iwl_rate_scaling_info table[IWL_MAX_RATES];
} __attribute__ ((packed));

#elif IWL == 4965

/*RS_NEW_API: only TLC_RTS remains and moved to bit 0 */
#define  LINK_QUAL_FLAGS_SET_STA_TLC_RTS_MSK	(1<<0)

#define  LINK_QUAL_AC_NUM AC_NUM
#define  LINK_QUAL_MAX_RETRY_NUM 16

#define  LINK_QUAL_ANT_A_MSK (1<<0)
#define  LINK_QUAL_ANT_B_MSK (1<<1)
#define  LINK_QUAL_ANT_MSK   (LINK_QUAL_ANT_A_MSK|LINK_QUAL_ANT_B_MSK)

struct iwl_link_qual_general_params {
	u8 flags;
	u8 mimo_delimiter;
	u8 single_stream_ant_msk;
	u8 dual_stream_ant_msk;
	u8 start_rate_index[LINK_QUAL_AC_NUM];
} __attribute__ ((packed));

struct iwl_link_qual_agg_params {
	__le16 agg_time_limit;
	u8 agg_dis_start_th;
	u8 agg_frame_cnt_limit;
	__le32 reserved;
} __attribute__ ((packed));

/*
 * REPLY_TX_LINK_QUALITY_CMD = 0x4e (command, has simple generic response)
 */
struct iwl_link_quality_cmd {
	u8 sta_id;
	u8 reserved1;
	__le16 control;
	struct iwl_link_qual_general_params general_params;
	struct iwl_link_qual_agg_params agg_params;
	struct {
		__le32 rate_n_flags;
	} rs_table[LINK_QUAL_MAX_RETRY_NUM];
	__le32 reserved2;
} __attribute__ ((packed));
#endif

/*
 * REPLY_BT_CONFIG = 0x9b (command, has simple generic response)
 */
struct iwl_bt_cmd {
	u8 flags;
	u8 lead_time;
	u8 max_kill;
	u8 reserved;
	__le32 kill_ack_mask;
	__le32 kill_cts_mask;
} __attribute__ ((packed));

/******************************************************************************
 * (6)
 * Spectrum Management (802.11h) Commands, Responses, Notifications:
 *
 *****************************************************************************/

/*
 * Spectrum Management
 */
#define MEASUREMENT_FILTER_FLAG (RXON_FILTER_PROMISC_MSK         | \
				 RXON_FILTER_CTL2HOST_MSK        | \
				 RXON_FILTER_ACCEPT_GRP_MSK      | \
				 RXON_FILTER_DIS_DECRYPT_MSK     | \
				 RXON_FILTER_DIS_GRP_DECRYPT_MSK | \
				 RXON_FILTER_ASSOC_MSK           | \
				 RXON_FILTER_BCON_AWARE_MSK)

struct iwl_measure_channel {
	__le32 duration;	/* measurement duration in extended beacon
				 * format */
	u8 channel;		/* channel to measure */
	u8 type;		/* see enum iwl_measure_type */
	__le16 reserved;
} __attribute__ ((packed));

/*
 * REPLY_SPECTRUM_MEASUREMENT_CMD = 0x74 (command)
 */
struct iwl_spectrum_cmd {
	__le16 len;		/* number of bytes starting from token */
	u8 token;		/* token id */
	u8 id;			/* measurement id -- 0 or 1 */
	u8 origin;		/* 0 = TGh, 1 = other, 2 = TGk */
	u8 periodic;		/* 1 = periodic */
	__le16 path_loss_timeout;
	__le32 start_time;	/* start time in extended beacon format */
	__le32 reserved2;
	__le32 flags;		/* rxon flags */
	__le32 filter_flags;	/* rxon filter flags */
	__le16 channel_count;	/* minimum 1, maximum 10 */
	__le16 reserved3;
	struct iwl_measure_channel channels[10];
} __attribute__ ((packed));

/*
 * REPLY_SPECTRUM_MEASUREMENT_CMD = 0x74 (response)
 */
struct iwl_spectrum_resp {
	u8 token;
	u8 id;			/* id of the prior command replaced, or 0xff */
	__le16 status;		/* 0 - command will be handled
				 * 1 - cannot handle (conflicts with another
				 *     measurement) */
} __attribute__ ((packed));

enum iwl_measurement_state {
	IWL_MEASUREMENT_START = 0,
	IWL_MEASUREMENT_STOP = 1,
};

enum iwl_measurement_status {
	IWL_MEASUREMENT_OK = 0,
	IWL_MEASUREMENT_CONCURRENT = 1,
	IWL_MEASUREMENT_CSA_CONFLICT = 2,
	IWL_MEASUREMENT_TGH_CONFLICT = 3,
	/* 4-5 reserved */
	IWL_MEASUREMENT_STOPPED = 6,
	IWL_MEASUREMENT_TIMEOUT = 7,
	IWL_MEASUREMENT_PERIODIC_FAILED = 8,
};

#define NUM_ELEMENTS_IN_HISTOGRAM 8

struct iwl_measurement_histogram {
	__le32 ofdm[NUM_ELEMENTS_IN_HISTOGRAM];	/* in 0.8usec counts */
	__le32 cck[NUM_ELEMENTS_IN_HISTOGRAM];	/* in 1usec counts */
} __attribute__ ((packed));

/* clear channel availability counters */
struct iwl_measurement_cca_counters {
	__le32 ofdm;
	__le32 cck;
} __attribute__ ((packed));

enum iwl_measure_type {
	IWL_MEASURE_BASIC = (1 << 0),
	IWL_MEASURE_CHANNEL_LOAD = (1 << 1),
	IWL_MEASURE_HISTOGRAM_RPI = (1 << 2),
	IWL_MEASURE_HISTOGRAM_NOISE = (1 << 3),
	IWL_MEASURE_FRAME = (1 << 4),
	/* bits 5:6 are reserved */
	IWL_MEASURE_IDLE = (1 << 7),
};

/*
 * SPECTRUM_MEASURE_NOTIFICATION = 0x75 (notification only, not a command)
 */
struct iwl_spectrum_notification {
	u8 id;			/* measurement id -- 0 or 1 */
	u8 token;
	u8 channel_index;	/* index in measurement channel list */
	u8 state;		/* 0 - start, 1 - stop */
	__le32 start_time;	/* lower 32-bits of TSF */
	u8 band;		/* 0 - 5.2GHz, 1 - 2.4GHz */
	u8 channel;
	u8 type;		/* see enum iwl_measurement_type */
	u8 reserved1;
	/* NOTE:  cca_ofdm, cca_cck, basic_type, and histogram are only only
	 * valid if applicable for measurement type requested. */
	__le32 cca_ofdm;	/* cca fraction time in 40Mhz clock periods */
	__le32 cca_cck;		/* cca fraction time in 44Mhz clock periods */
	__le32 cca_time;	/* channel load time in usecs */
	u8 basic_type;		/* 0 - bss, 1 - ofdm preamble, 2 -
				 * unidentified */
	u8 reserved2[3];
	struct iwl_measurement_histogram histogram;
	__le32 stop_time;	/* lower 32-bits of TSF */
	__le32 status;		/* see iwl_measurement_status */
} __attribute__ ((packed));

/******************************************************************************
 * (7)
 * Power Management Commands, Responses, Notifications:
 *
 *****************************************************************************/

/**
 * struct iwl_powertable_cmd - Power Table Command
 * @flags: See below:
 *
 * POWER_TABLE_CMD = 0x77 (command, has simple generic response)
 *
 * PM allow:
 *   bit 0 - '0' Driver not allow power management
 *           '1' Driver allow PM (use rest of parameters)
 * uCode send sleep notifications:
 *   bit 1 - '0' Don't send sleep notification
 *           '1' send sleep notification (SEND_PM_NOTIFICATION)
 * Sleep over DTIM
 *   bit 2 - '0' PM have to walk up every DTIM
 *           '1' PM could sleep over DTIM till listen Interval.
 * PCI power managed
 *   bit 3 - '0' (PCI_LINK_CTRL & 0x1)
 *           '1' !(PCI_LINK_CTRL & 0x1)
 * Force sleep Modes
 *   bit 31/30- '00' use both mac/xtal sleeps
 *              '01' force Mac sleep
 *              '10' force xtal sleep
 *              '11' Illegal set
 *
 * NOTE: if sleep_interval[SLEEP_INTRVL_TABLE_SIZE-1] > DTIM period then
 * ucode assume sleep over DTIM is allowed and we don't need to wakeup
 * for every DTIM.
 */
#define IWL_POWER_VEC_SIZE 5


#if IWL == 3945

#define IWL_POWER_DRIVER_ALLOW_SLEEP_MSK	__constant_cpu_to_le32(1<<0)
#define IWL_POWER_SLEEP_OVER_DTIM_MSK		__constant_cpu_to_le32(1<<2)
#define IWL_POWER_PCI_PM_MSK			__constant_cpu_to_le32(1<<3)
struct iwl_powertable_cmd {
	__le32 flags;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 sleep_interval[IWL_POWER_VEC_SIZE];
} __attribute__((packed));

#elif IWL == 4965

#define IWL_POWER_DRIVER_ALLOW_SLEEP_MSK	__constant_cpu_to_le16(1<<0)
#define IWL_POWER_SLEEP_OVER_DTIM_MSK		__constant_cpu_to_le16(1<<2)
#define IWL_POWER_PCI_PM_MSK			__constant_cpu_to_le16(1<<3)

struct iwl_powertable_cmd {
	__le16 flags;
	u8 keep_alive_seconds;
	u8 debug_flags;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 sleep_interval[IWL_POWER_VEC_SIZE];
	__le32 keep_alive_beacons;
} __attribute__ ((packed));
#endif

/*
 * PM_SLEEP_NOTIFICATION = 0x7A (notification only, not a command)
 * 3945 and 4965 identical.
 */
struct iwl_sleep_notification {
	u8 pm_sleep_mode;
	u8 pm_wakeup_src;
	__le16 reserved;
	__le32 sleep_time;
	__le32 tsf_low;
	__le32 bcon_timer;
} __attribute__ ((packed));

/* Sleep states.  3945 and 4965 identical. */
enum {
	IWL_PM_NO_SLEEP = 0,
	IWL_PM_SLP_MAC = 1,
	IWL_PM_SLP_FULL_MAC_UNASSOCIATE = 2,
	IWL_PM_SLP_FULL_MAC_CARD_STATE = 3,
	IWL_PM_SLP_PHY = 4,
	IWL_PM_SLP_REPENT = 5,
	IWL_PM_WAKEUP_BY_TIMER = 6,
	IWL_PM_WAKEUP_BY_DRIVER = 7,
	IWL_PM_WAKEUP_BY_RFKILL = 8,
	/* 3 reserved */
	IWL_PM_NUM_OF_MODES = 12,
};

/*
 * REPLY_CARD_STATE_CMD = 0xa0 (command, has simple generic response)
 */
#define CARD_STATE_CMD_DISABLE 0x00	/* Put card to sleep */
#define CARD_STATE_CMD_ENABLE  0x01	/* Wake up card */
#define CARD_STATE_CMD_HALT    0x02	/* Power down permanently */
struct iwl_card_state_cmd {
	__le32 status;		/* CARD_STATE_CMD_* request new power state */
} __attribute__ ((packed));

/*
 * CARD_STATE_NOTIFICATION = 0xa1 (notification only, not a command)
 */
struct iwl_card_state_notif {
	__le32 flags;
} __attribute__ ((packed));

#define HW_CARD_DISABLED   0x01
#define SW_CARD_DISABLED   0x02
#define RF_CARD_DISABLED   0x04
#define RXON_CARD_DISABLED 0x10

struct iwl_ct_kill_config {
	__le32   reserved;
	__le32   critical_temperature_M;
	__le32   critical_temperature_R;
}  __attribute__ ((packed));

/******************************************************************************
 * (8)
 * Scan Commands, Responses, Notifications:
 *
 *****************************************************************************/

struct iwl_scan_channel {
	/* type is defined as:
	 * 0:0 active (0 - passive)
	 * 1:4 SSID direct
	 *     If 1 is set then corresponding SSID IE is transmitted in probe
	 * 5:7 reserved
	 */
	u8 type;
	u8 channel;
	struct iwl_tx_power tpc;
	__le16 active_dwell;
	__le16 passive_dwell;
} __attribute__ ((packed));

struct iwl_ssid_ie {
	u8 id;
	u8 len;
	u8 ssid[32];
} __attribute__ ((packed));

#define PROBE_OPTION_MAX        0x4
#define TX_CMD_LIFE_TIME_INFINITE	__constant_cpu_to_le32(0xFFFFFFFF)
#define IWL_GOOD_CRC_TH		__constant_cpu_to_le16(1)
#define IWL_MAX_SCAN_SIZE 1024

/*
 * REPLY_SCAN_CMD = 0x80 (command)
 */
struct iwl_scan_cmd {
	__le16 len;
	u8 reserved0;
	u8 channel_count;
	__le16 quiet_time;     /* dwell only this long on quiet chnl
				* (active scan) */
	__le16 quiet_plcp_th;  /* quiet chnl is < this # pkts (typ. 1) */
	__le16 good_CRC_th;    /* passive -> active promotion threshold */
#if IWL == 3945
	__le16 reserved1;
#elif IWL == 4965
	__le16 rx_chain;
#endif
	__le32 max_out_time;   /* max usec to be out of associated (service)
				* chnl */
	__le32 suspend_time;   /* pause scan this long when returning to svc
				* chnl.
				* 3945 -- 31:24 # beacons, 19:0 additional usec,
				* 4965 -- 31:22 # beacons, 21:0 additional usec.
				*/
	__le32 flags;
	__le32 filter_flags;

	struct iwl_tx_cmd tx_cmd;
	struct iwl_ssid_ie direct_scan[PROBE_OPTION_MAX];

	u8 data[0];
	/*
	 * The channels start after the probe request payload and are of type:
	 *
	 * struct iwl_scan_channel channels[0];
	 *
	 * NOTE:  Only one band of channels can be scanned per pass.  You
	 * can not mix 2.4GHz channels and 5.2GHz channels and must
	 * request a scan multiple times (not concurrently)
	 *
	 */
} __attribute__ ((packed));

/* Can abort will notify by complete notification with abort status. */
#define CAN_ABORT_STATUS	__constant_cpu_to_le32(0x1)
/* complete notification statuses */
#define ABORT_STATUS            0x2

/*
 * REPLY_SCAN_CMD = 0x80 (response)
 */
struct iwl_scanreq_notification {
	__le32 status;		/* 1: okay, 2: cannot fulfill request */
} __attribute__ ((packed));

/*
 * SCAN_START_NOTIFICATION = 0x82 (notification only, not a command)
 */
struct iwl_scanstart_notification {
	__le32 tsf_low;
	__le32 tsf_high;
	__le32 beacon_timer;
	u8 channel;
	u8 band;
	u8 reserved[2];
	__le32 status;
} __attribute__ ((packed));

#define  SCAN_OWNER_STATUS 0x1;
#define  MEASURE_OWNER_STATUS 0x2;

#define NUMBER_OF_STATISTICS 1	/* first __le32 is good CRC */
/*
 * SCAN_RESULTS_NOTIFICATION = 0x83 (notification only, not a command)
 */
struct iwl_scanresults_notification {
	u8 channel;
	u8 band;
	u8 reserved[2];
	__le32 tsf_low;
	__le32 tsf_high;
	__le32 statistics[NUMBER_OF_STATISTICS];
} __attribute__ ((packed));

/*
 * SCAN_COMPLETE_NOTIFICATION = 0x84 (notification only, not a command)
 */
struct iwl_scancomplete_notification {
	u8 scanned_channels;
	u8 status;
	u8 reserved;
	u8 last_channel;
	__le32 tsf_low;
	__le32 tsf_high;
} __attribute__ ((packed));


/******************************************************************************
 * (9)
 * IBSS/AP Commands and Notifications:
 *
 *****************************************************************************/

/*
 * BEACON_NOTIFICATION = 0x90 (notification only, not a command)
 */
struct iwl_beacon_notif {
	struct iwl_tx_resp beacon_notify_hdr;
	__le32 low_tsf;
	__le32 high_tsf;
	__le32 ibss_mgr_status;
} __attribute__ ((packed));

/*
 * REPLY_TX_BEACON = 0x91 (command, has simple generic response)
 */
struct iwl_tx_beacon_cmd {
	struct iwl_tx_cmd tx;
	__le16 tim_idx;
	u8 tim_size;
	u8 reserved1;
	struct ieee80211_hdr frame[0];	/* beacon frame */
} __attribute__ ((packed));

/******************************************************************************
 * (10)
 * Statistics Commands and Notifications:
 *
 *****************************************************************************/

#define IWL_TEMP_CONVERT 260

#define SUP_RATE_11A_MAX_NUM_CHANNELS  8
#define SUP_RATE_11B_MAX_NUM_CHANNELS  4
#define SUP_RATE_11G_MAX_NUM_CHANNELS  12

/* Used for passing to driver number of successes and failures per rate */
struct rate_histogram {
	union {
		__le32 a[SUP_RATE_11A_MAX_NUM_CHANNELS];
		__le32 b[SUP_RATE_11B_MAX_NUM_CHANNELS];
		__le32 g[SUP_RATE_11G_MAX_NUM_CHANNELS];
	} success;
	union {
		__le32 a[SUP_RATE_11A_MAX_NUM_CHANNELS];
		__le32 b[SUP_RATE_11B_MAX_NUM_CHANNELS];
		__le32 g[SUP_RATE_11G_MAX_NUM_CHANNELS];
	} failed;
} __attribute__ ((packed));

/* statistics command response */

struct statistics_rx_phy {
	__le32 ina_cnt;
	__le32 fina_cnt;
	__le32 plcp_err;
	__le32 crc32_err;
	__le32 overrun_err;
	__le32 early_overrun_err;
	__le32 crc32_good;
	__le32 false_alarm_cnt;
	__le32 fina_sync_err_cnt;
	__le32 sfd_timeout;
	__le32 fina_timeout;
	__le32 unresponded_rts;
	__le32 rxe_frame_limit_overrun;
	__le32 sent_ack_cnt;
	__le32 sent_cts_cnt;
#if IWL == 4965
	__le32 sent_ba_rsp_cnt;
	__le32 dsp_self_kill;
	__le32 mh_format_err;
	__le32 re_acq_main_rssi_sum;
	__le32 reserved3;
#endif
} __attribute__ ((packed));

#if IWL == 4965
struct statistics_rx_ht_phy {
	__le32 plcp_err;
	__le32 overrun_err;
	__le32 early_overrun_err;
	__le32 crc32_good;
	__le32 crc32_err;
	__le32 mh_format_err;
	__le32 agg_crc32_good;
	__le32 agg_mpdu_cnt;
	__le32 agg_cnt;
	__le32 reserved2;
} __attribute__ ((packed));
#endif

struct statistics_rx_non_phy {
	__le32 bogus_cts;	/* CTS received when not expecting CTS */
	__le32 bogus_ack;	/* ACK received when not expecting ACK */
	__le32 non_bssid_frames;	/* number of frames with BSSID that
					 * doesn't belong to the STA BSSID */
	__le32 filtered_frames;	/* count frames that were dumped in the
				 * filtering process */
	__le32 non_channel_beacons;	/* beacons with our bss id but not on
					 * our serving channel */
#if IWL == 4965
	__le32 channel_beacons;	/* beacons with our bss id and in our
				 * serving channel */
	__le32 num_missed_bcon;	/* number of missed beacons */
	__le32 adc_rx_saturation_time;	/* count in 0.8us units the time the
					 * ADC was in saturation */
	__le32 ina_detection_search_time;/* total time (in 0.8us) searched
					  * for INA */
	__le32 beacon_silence_rssi_a;	/* RSSI silence after beacon frame */
	__le32 beacon_silence_rssi_b;	/* RSSI silence after beacon frame */
	__le32 beacon_silence_rssi_c;	/* RSSI silence after beacon frame */
	__le32 interference_data_flag;	/* flag for interference data
					 * availability. 1 when data is
					 * available. */
	__le32 channel_load;	/* counts RX Enable time */
	__le32 dsp_false_alarms;	/* DSP false alarm (both OFDM
					 * and CCK) counter */
	__le32 beacon_rssi_a;
	__le32 beacon_rssi_b;
	__le32 beacon_rssi_c;
	__le32 beacon_energy_a;
	__le32 beacon_energy_b;
	__le32 beacon_energy_c;
#endif
} __attribute__ ((packed));

struct statistics_rx {
	struct statistics_rx_phy ofdm;
	struct statistics_rx_phy cck;
	struct statistics_rx_non_phy general;
#if IWL == 4965
	struct statistics_rx_ht_phy ofdm_ht;
#endif
} __attribute__ ((packed));

#if IWL == 4965
struct statistics_tx_non_phy_agg {
	__le32 ba_timeout;
	__le32 ba_reschedule_frames;
	__le32 scd_query_agg_frame_cnt;
	__le32 scd_query_no_agg;
	__le32 scd_query_agg;
	__le32 scd_query_mismatch;
	__le32 frame_not_ready;
	__le32 underrun;
	__le32 bt_prio_kill;
	__le32 rx_ba_rsp_cnt;
	__le32 reserved2;
	__le32 reserved3;
} __attribute__ ((packed));
#endif

struct statistics_tx {
	__le32 preamble_cnt;
	__le32 rx_detected_cnt;
	__le32 bt_prio_defer_cnt;
	__le32 bt_prio_kill_cnt;
	__le32 few_bytes_cnt;
	__le32 cts_timeout;
	__le32 ack_timeout;
	__le32 expected_ack_cnt;
	__le32 actual_ack_cnt;
#if IWL == 4965
	__le32 dump_msdu_cnt;
	__le32 burst_abort_next_frame_mismatch_cnt;
	__le32 burst_abort_missing_next_frame_cnt;
	__le32 cts_timeout_collision;
	__le32 ack_or_ba_timeout_collision;
	struct statistics_tx_non_phy_agg agg;
#endif
} __attribute__ ((packed));

struct statistics_dbg {
	__le32 burst_check;
	__le32 burst_count;
	__le32 reserved[4];
} __attribute__ ((packed));

struct statistics_div {
	__le32 tx_on_a;
	__le32 tx_on_b;
	__le32 exec_time;
	__le32 probe_time;
#if IWL == 4965
	__le32 reserved1;
	__le32 reserved2;
#endif
} __attribute__ ((packed));

struct statistics_general {
	__le32 temperature;
#if IWL == 4965
	__le32 temperature_m;
#endif
	struct statistics_dbg dbg;
	__le32 sleep_time;
	__le32 slots_out;
	__le32 slots_idle;
	__le32 ttl_timestamp;
	struct statistics_div div;
#if IWL == 4965
	__le32 rx_enable_counter;
	__le32 reserved1;
	__le32 reserved2;
	__le32 reserved3;
#endif
} __attribute__ ((packed));

/*
 * REPLY_STATISTICS_CMD = 0x9c,
 * 3945 and 4965 identical.
 *
 * This command triggers an immediate response containing uCode statistics.
 * The response is in the same format as STATISTICS_NOTIFICATION 0x9d, below.
 *
 * If the CLEAR_STATS configuration flag is set, uCode will clear its
 * internal copy of the statistics (counters) after issuing the response.
 * This flag does not affect STATISTICS_NOTIFICATIONs after beacons (see below).
 *
 * If the DISABLE_NOTIF configuration flag is set, uCode will not issue
 * STATISTICS_NOTIFICATIONs after received beacons (see below).  This flag
 * does not affect the response to the REPLY_STATISTICS_CMD 0x9c itself.
 */
#define IWL_STATS_CONF_CLEAR_STATS __constant_cpu_to_le32(0x1)	/* see above */
#define IWL_STATS_CONF_DISABLE_NOTIF __constant_cpu_to_le32(0x2)/* see above */
struct iwl_statistics_cmd {
	__le32 configuration_flags;	/* IWL_STATS_CONF_* */
} __attribute__ ((packed));

/*
 * STATISTICS_NOTIFICATION = 0x9d (notification only, not a command)
 *
 * By default, uCode issues this notification after receiving a beacon
 * while associated.  To disable this behavior, set DISABLE_NOTIF flag in the
 * REPLY_STATISTICS_CMD 0x9c, above.
 *
 * Statistics counters continue to increment beacon after beacon, but are
 * cleared when changing channels or when driver issues REPLY_STATISTICS_CMD
 * 0x9c with CLEAR_STATS bit set (see above).
 *
 * uCode also issues this notification during scans.  uCode clears statistics
 * appropriately so that each notification contains statistics for only the
 * one channel that has just been scanned.
 */
#define STATISTICS_REPLY_FLG_BAND_24G_MSK         __constant_cpu_to_le32(0x2)
#define STATISTICS_REPLY_FLG_FAT_MODE_MSK         __constant_cpu_to_le32(0x8)
struct iwl_notif_statistics {
	__le32 flag;
	struct statistics_rx rx;
	struct statistics_tx tx;
	struct statistics_general general;
} __attribute__ ((packed));


/*
 * MISSED_BEACONS_NOTIFICATION = 0xa2 (notification only, not a command)
 */
/* if ucode missed CONSECUTIVE_MISSED_BCONS_TH beacons in a row,
 * then this notification will be sent. */
#define CONSECUTIVE_MISSED_BCONS_TH 20

struct iwl_missed_beacon_notif {
	__le32 consequtive_missed_beacons;
	__le32 total_missed_becons;
	__le32 num_expected_beacons;
	__le32 num_recvd_beacons;
} __attribute__ ((packed));

/******************************************************************************
 * (11)
 * Rx Calibration Commands:
 *
 *****************************************************************************/

#define PHY_CALIBRATE_DIFF_GAIN_CMD (7)
#define HD_TABLE_SIZE  (11)

struct iwl_sensitivity_cmd {
	__le16 control;
	__le16 table[HD_TABLE_SIZE];
} __attribute__ ((packed));

struct iwl_calibration_cmd {
	u8 opCode;
	u8 flags;
	__le16 reserved;
	s8 diff_gain_a;
	s8 diff_gain_b;
	s8 diff_gain_c;
	u8 reserved1;
} __attribute__ ((packed));

/******************************************************************************
 * (12)
 * Miscellaneous Commands:
 *
 *****************************************************************************/

/*
 * LEDs Command & Response
 * REPLY_LEDS_CMD = 0x48 (command, has simple generic response)
 *
 * For each of 3 possible LEDs (Activity/Link/Tech, selected by "id" field),
 * this command turns it on or off, or sets up a periodic blinking cycle.
 */
struct iwl_led_cmd {
	__le32 interval;	/* "interval" in uSec */
	u8 id;			/* 1: Activity, 2: Link, 3: Tech */
	u8 off;			/* # intervals off while blinking;
				 * "0", with >0 "on" value, turns LED on */
	u8 on;			/* # intervals on while blinking;
				 * "0", regardless of "off", turns LED off */
	u8 reserved;
} __attribute__ ((packed));

/******************************************************************************
 * (13)
 * Union of all expected notifications/responses:
 *
 *****************************************************************************/

struct iwl_rx_packet {
	__le32 len;
	struct iwl_cmd_header hdr;
	union {
		struct iwl_alive_resp alive_frame;
		struct iwl_rx_frame rx_frame;
		struct iwl_tx_resp tx_resp;
		struct iwl_spectrum_notification spectrum_notif;
		struct iwl_csa_notification csa_notif;
		struct iwl_error_resp err_resp;
		struct iwl_card_state_notif card_state_notif;
		struct iwl_beacon_notif beacon_status;
		struct iwl_add_sta_resp add_sta;
		struct iwl_sleep_notification sleep_notif;
		struct iwl_spectrum_resp spectrum;
		struct iwl_notif_statistics stats;
#if IWL == 4965
		struct iwl_compressed_ba_resp compressed_ba;
		struct iwl_missed_beacon_notif missed_beacon;
#endif
		__le32 status;
		u8 raw[0];
	} u;
} __attribute__ ((packed));

#define IWL_RX_FRAME_SIZE        (4 + sizeof(struct iwl_rx_frame))

#endif				/* __iwl_commands_h__ */

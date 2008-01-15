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
 * it under the terms of version 2 of the GNU General Public License as
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
/*
 * Please use this file (iwl-3945-commands.h) only for uCode API definitions.
 * Please use iwl-3945-hw.h for hardware-related definitions.
 * Please use iwl-3945.h for driver implementation definitions.
 */

#ifndef __iwl_3945_commands_h__
#define __iwl_3945_commands_h__

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
	REPLY_3945_RX = 0x1b,		/* 3945 only */
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

	/* Bluetooth device coexistance config command */
	REPLY_BT_CONFIG = 0x9b,

	/* Statistics */
	REPLY_STATISTICS_CMD = 0x9c,
	STATISTICS_NOTIFICATION = 0x9d,

	/* RF-KILL commands and notifications */
	REPLY_CARD_STATE_CMD = 0xa0,
	CARD_STATE_NOTIFICATION = 0xa1,

	/* Missed beacons notification */
	MISSED_BEACONS_NOTIFICATION = 0xa2,

	REPLY_MAX = 0xff
};

/******************************************************************************
 * (0)
 * Commonly used structures and definitions:
 * Command header, txpower
 *
 *****************************************************************************/

/* iwl3945_cmd_header flags value */
#define IWL_CMD_FAILED_MSK 0x40

/**
 * struct iwl3945_cmd_header
 *
 * This header format appears in the beginning of each command sent from the
 * driver, and each response/notification received from uCode.
 */
struct iwl3945_cmd_header {
	u8 cmd;		/* Command ID:  REPLY_RXON, etc. */
	u8 flags;	/* IWL_CMD_* */
	/*
	 * The driver sets up the sequence number to values of its chosing.
	 * uCode does not use this value, but passes it back to the driver
	 * when sending the response to each driver-originated command, so
	 * the driver can match the response to the command.  Since the values
	 * don't get used by uCode, the driver may set up an arbitrary format.
	 *
	 * There is one exception:  uCode sets bit 15 when it originates
	 * the response/notification, i.e. when the response/notification
	 * is not a direct response to a command sent by the driver.  For
	 * example, uCode issues REPLY_3945_RX when it sends a received frame
	 * to the driver; it is not a direct response to any driver command.
	 *
	 * The Linux driver uses the following format:
	 *
	 *  0:7    index/position within Tx queue
	 *  8:13   Tx queue selection
	 * 14:14   driver sets this to indicate command is in the 'huge'
	 *         storage at the end of the command buffers, i.e. scan cmd
	 * 15:15   uCode sets this in uCode-originated response/notification
	 */
	__le16 sequence;

	/* command or response/notification data follows immediately */
	u8 data[0];
} __attribute__ ((packed));

/**
 * struct iwl3945_tx_power
 *
 * Used in REPLY_TX_PWR_TABLE_CMD, REPLY_SCAN_CMD, REPLY_CHANNEL_SWITCH
 *
 * Each entry contains two values:
 * 1)  DSP gain (or sometimes called DSP attenuation).  This is a fine-grained
 *     linear value that multiplies the output of the digital signal processor,
 *     before being sent to the analog radio.
 * 2)  Radio gain.  This sets the analog gain of the radio Tx path.
 *     It is a coarser setting, and behaves in a logarithmic (dB) fashion.
 *
 * Driver obtains values from struct iwl3945_tx_power power_gain_table[][].
 */
struct iwl3945_tx_power {
	u8 tx_gain;		/* gain for analog radio */
	u8 dsp_atten;		/* gain for DSP */
} __attribute__ ((packed));

/**
 * struct iwl3945_power_per_rate
 *
 * Used in REPLY_TX_PWR_TABLE_CMD, REPLY_CHANNEL_SWITCH
 */
struct iwl3945_power_per_rate {
	u8 rate;		/* plcp */
	struct iwl3945_tx_power tpc;
	u8 reserved;
} __attribute__ ((packed));

/******************************************************************************
 * (0a)
 * Alive and Error Commands & Responses:
 *
 *****************************************************************************/

#define UCODE_VALID_OK	__constant_cpu_to_le32(0x1)
#define INITIALIZE_SUBTYPE    (9)

/*
 * ("Initialize") REPLY_ALIVE = 0x1 (response only, not a command)
 *
 * uCode issues this "initialize alive" notification once the initialization
 * uCode image has completed its work, and is ready to load the runtime image.
 * This is the *first* "alive" notification that the driver will receive after
 * rebooting uCode; the "initialize" alive is indicated by subtype field == 9.
 *
 * See comments documenting "BSM" (bootstrap state machine).
 */
struct iwl3945_init_alive_resp {
	u8 ucode_minor;
	u8 ucode_major;
	__le16 reserved1;
	u8 sw_rev[8];
	u8 ver_type;
	u8 ver_subtype;			/* "9" for initialize alive */
	__le16 reserved2;
	__le32 log_event_table_ptr;
	__le32 error_event_table_ptr;
	__le32 timestamp;
	__le32 is_valid;
} __attribute__ ((packed));


/**
 * REPLY_ALIVE = 0x1 (response only, not a command)
 *
 * uCode issues this "alive" notification once the runtime image is ready
 * to receive commands from the driver.  This is the *second* "alive"
 * notification that the driver will receive after rebooting uCode;
 * this "alive" is indicated by subtype field != 9.
 *
 * See comments documenting "BSM" (bootstrap state machine).
 *
 * This response includes two pointers to structures within the device's
 * data SRAM (access via HBUS_TARG_MEM_* regs) that are useful for debugging:
 *
 * 1)  log_event_table_ptr indicates base of the event log.  This traces
 *     a 256-entry history of uCode execution within a circular buffer.
 *
 * 2)  error_event_table_ptr indicates base of the error log.  This contains
 *     information about any uCode error that occurs.
 *
 * The Linux driver can print both logs to the system log when a uCode error
 * occurs.
 */
struct iwl3945_alive_resp {
	u8 ucode_minor;
	u8 ucode_major;
	__le16 reserved1;
	u8 sw_rev[8];
	u8 ver_type;
	u8 ver_subtype;			/* not "9" for runtime alive */
	__le16 reserved2;
	__le32 log_event_table_ptr;	/* SRAM address for event log */
	__le32 error_event_table_ptr;	/* SRAM address for error log */
	__le32 timestamp;
	__le32 is_valid;
} __attribute__ ((packed));

union tsf {
	u8 byte[8];
	__le16 word[4];
	__le32 dw[2];
};

/*
 * REPLY_ERROR = 0x2 (response only, not a command)
 */
struct iwl3945_error_resp {
	__le32 error_type;
	u8 cmd_id;
	u8 reserved1;
	__le16 bad_cmd_seq_num;
	__le16 reserved2;
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

/**
 * REPLY_RXON = 0x10 (command, has simple generic response)
 *
 * RXON tunes the radio tuner to a service channel, and sets up a number
 * of parameters that are used primarily for Rx, but also for Tx operations.
 *
 * NOTE:  When tuning to a new channel, driver must set the
 *        RXON_FILTER_ASSOC_MSK to 0.  This will clear station-dependent
 *        info within the device, including the station tables, tx retry
 *        rate tables, and txpower tables.  Driver must build a new station
 *        table and txpower table before transmitting anything on the RXON
 *        channel.
 *
 * NOTE:  All RXONs wipe clean the internal txpower table.  Driver must
 *        issue a new REPLY_TX_PWR_TABLE_CMD after each REPLY_RXON (0x10),
 *        regardless of whether RXON_FILTER_ASSOC_MSK is set.
 */
struct iwl3945_rxon_cmd {
	u8 node_addr[6];
	__le16 reserved1;
	u8 bssid_addr[6];
	__le16 reserved2;
	u8 wlap_bssid_addr[6];
	__le16 reserved3;
	u8 dev_type;
	u8 air_propagation;
	__le16 reserved4;
	u8 ofdm_basic_rates;
	u8 cck_basic_rates;
	__le16 assoc_id;
	__le32 flags;
	__le32 filter_flags;
	__le16 channel;
	__le16 reserved5;
} __attribute__ ((packed));

/*
 * REPLY_RXON_ASSOC = 0x11 (command, has simple generic response)
 */
struct iwl3945_rxon_assoc_cmd {
	__le32 flags;
	__le32 filter_flags;
	u8 ofdm_basic_rates;
	u8 cck_basic_rates;
	__le16 reserved;
} __attribute__ ((packed));

/*
 * REPLY_RXON_TIMING = 0x14 (command, has simple generic response)
 */
struct iwl3945_rxon_time_cmd {
	union tsf timestamp;
	__le16 beacon_interval;
	__le16 atim_window;
	__le32 beacon_init_val;
	__le16 listen_interval;
	__le16 reserved;
} __attribute__ ((packed));

/*
 * REPLY_CHANNEL_SWITCH = 0x72 (command, has simple generic response)
 */
struct iwl3945_channel_switch_cmd {
	u8 band;
	u8 expect_beacon;
	__le16 channel;
	__le32 rxon_flags;
	__le32 rxon_filter_flags;
	__le32 switch_time;
	struct iwl3945_power_per_rate power[IWL_MAX_RATES];
} __attribute__ ((packed));

/*
 * CHANNEL_SWITCH_NOTIFICATION = 0x73 (notification only, not a command)
 */
struct iwl3945_csa_notification {
	__le16 band;
	__le16 channel;
	__le32 status;		/* 0 - OK, 1 - fail */
} __attribute__ ((packed));

/******************************************************************************
 * (2)
 * Quality-of-Service (QOS) Commands & Responses:
 *
 *****************************************************************************/

/**
 * struct iwl_ac_qos -- QOS timing params for REPLY_QOS_PARAM
 * One for each of 4 EDCA access categories in struct iwl_qosparam_cmd
 *
 * @cw_min: Contention window, start value in numbers of slots.
 *          Should be a power-of-2, minus 1.  Device's default is 0x0f.
 * @cw_max: Contention window, max value in numbers of slots.
 *          Should be a power-of-2, minus 1.  Device's default is 0x3f.
 * @aifsn:  Number of slots in Arbitration Interframe Space (before
 *          performing random backoff timing prior to Tx).  Device default 1.
 * @edca_txop:  Length of Tx opportunity, in uSecs.  Device default is 0.
 *
 * Device will automatically increase contention window by (2*CW) + 1 for each
 * transmission retry.  Device uses cw_max as a bit mask, ANDed with new CW
 * value, to cap the CW value.
 */
struct iwl3945_ac_qos {
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

/* Number of Access Categories (AC) (EDCA), queues 0..3 */
#define AC_NUM                4

/*
 * REPLY_QOS_PARAM = 0x13 (command, has simple generic response)
 *
 * This command sets up timings for each of the 4 prioritized EDCA Tx FIFOs
 * 0: Background, 1: Best Effort, 2: Video, 3: Voice.
 */
struct iwl3945_qosparam_cmd {
	__le32 qos_flags;
	struct iwl3945_ac_qos ac[AC_NUM];
} __attribute__ ((packed));

/******************************************************************************
 * (3)
 * Add/Modify Stations Commands & Responses:
 *
 *****************************************************************************/
/*
 * Multi station support
 */

/* Special, dedicated locations within device's station table */
#define	IWL_AP_ID		0
#define IWL_MULTICAST_ID	1
#define	IWL_STA_ID		2
#define	IWL3945_BROADCAST_ID	24
#define IWL3945_STATION_COUNT	25

#define	IWL_STATION_COUNT	32 	/* MAX(3945,4965)*/
#define	IWL_INVALID_STATION 	255

#define STA_FLG_TX_RATE_MSK		__constant_cpu_to_le32(1<<2);
#define STA_FLG_PWR_SAVE_MSK		__constant_cpu_to_le32(1<<8);

/* Use in mode field.  1: modify existing entry, 0: add new station entry */
#define STA_CONTROL_MODIFY_MSK		0x01

/* key flags __le16*/
#define STA_KEY_FLG_ENCRYPT_MSK	__constant_cpu_to_le16(0x7)
#define STA_KEY_FLG_NO_ENC	__constant_cpu_to_le16(0x0)
#define STA_KEY_FLG_WEP		__constant_cpu_to_le16(0x1)
#define STA_KEY_FLG_CCMP	__constant_cpu_to_le16(0x2)
#define STA_KEY_FLG_TKIP	__constant_cpu_to_le16(0x3)

#define STA_KEY_FLG_KEYID_POS	8
#define STA_KEY_FLG_INVALID 	__constant_cpu_to_le16(0x0800)

/* Flags indicate whether to modify vs. don't change various station params */
#define	STA_MODIFY_KEY_MASK		0x01
#define	STA_MODIFY_TID_DISABLE_TX	0x02
#define	STA_MODIFY_TX_RATE_MSK		0x04

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

struct iwl3945_keyinfo {
	__le16 key_flags;
	u8 tkip_rx_tsc_byte2;	/* TSC[2] for key mix ph1 detection */
	u8 reserved1;
	__le16 tkip_rx_ttak[5];	/* 10-byte unicast TKIP TTAK */
	__le16 reserved2;
	u8 key[16];		/* 16-byte unicast decryption key */
} __attribute__ ((packed));

/**
 * struct sta_id_modify
 * @addr[ETH_ALEN]: station's MAC address
 * @sta_id: index of station in uCode's station table
 * @modify_mask: STA_MODIFY_*, 1: modify, 0: don't change
 *
 * Driver selects unused table index when adding new station,
 * or the index to a pre-existing station entry when modifying that station.
 * Some indexes have special purposes (IWL_AP_ID, index 0, is for AP).
 *
 * modify_mask flags select which parameters to modify vs. leave alone.
 */
struct sta_id_modify {
	u8 addr[ETH_ALEN];
	__le16 reserved1;
	u8 sta_id;
	u8 modify_mask;
	__le16 reserved2;
} __attribute__ ((packed));

/*
 * REPLY_ADD_STA = 0x18 (command)
 *
 * The device contains an internal table of per-station information,
 * with info on security keys, aggregation parameters, and Tx rates for
 * initial Tx attempt and any retries (4965 uses REPLY_TX_LINK_QUALITY_CMD,
 * 3945 uses REPLY_RATE_SCALE to set up rate tables).
 *
 * REPLY_ADD_STA sets up the table entry for one station, either creating
 * a new entry, or modifying a pre-existing one.
 *
 * NOTE:  RXON command (without "associated" bit set) wipes the station table
 *        clean.  Moving into RF_KILL state does this also.  Driver must set up
 *        new station table before transmitting anything on the RXON channel
 *        (except active scans or active measurements; those commands carry
 *        their own txpower/rate setup data).
 *
 *        When getting started on a new channel, driver must set up the
 *        IWL_BROADCAST_ID entry (last entry in the table).  For a client
 *        station in a BSS, once an AP is selected, driver sets up the AP STA
 *        in the IWL_AP_ID entry (1st entry in the table).  BROADCAST and AP
 *        are all that are needed for a BSS client station.  If the device is
 *        used as AP, or in an IBSS network, driver must set up station table
 *        entries for all STAs in network, starting with index IWL_STA_ID.
 */
struct iwl3945_addsta_cmd {
	u8 mode;		/* 1: modify existing, 0: add new station */
	u8 reserved[3];
	struct sta_id_modify sta;
	struct iwl3945_keyinfo key;
	__le32 station_flags;		/* STA_FLG_* */
	__le32 station_flags_msk;	/* STA_FLG_* */

	/* bit field to disable (1) or enable (0) Tx for Traffic ID (TID)
	 * corresponding to bit (e.g. bit 5 controls TID 5).
	 * Set modify_mask bit STA_MODIFY_TID_DISABLE_TX to use this field. */
	__le16 tid_disable_tx;

	__le16 rate_n_flags;

	/* TID for which to add block-ack support.
	 * Set modify_mask bit STA_MODIFY_ADDBA_TID_MSK to use this field. */
	u8 add_immediate_ba_tid;

	/* TID for which to remove block-ack support.
	 * Set modify_mask bit STA_MODIFY_DELBA_TID_MSK to use this field. */
	u8 remove_immediate_ba_tid;

	/* Starting Sequence Number for added block-ack support.
	 * Set modify_mask bit STA_MODIFY_ADDBA_TID_MSK to use this field. */
	__le16 add_immediate_ba_ssn;
} __attribute__ ((packed));

#define ADD_STA_SUCCESS_MSK		0x1
#define ADD_STA_NO_ROOM_IN_TABLE	0x2
#define ADD_STA_NO_BLOCK_ACK_RESOURCE	0x4
/*
 * REPLY_ADD_STA = 0x18 (response)
 */
struct iwl3945_add_sta_resp {
	u8 status;	/* ADD_STA_* */
} __attribute__ ((packed));


/******************************************************************************
 * (4)
 * Rx Responses:
 *
 *****************************************************************************/

struct iwl3945_rx_frame_stats {
	u8 phy_count;
	u8 id;
	u8 rssi;
	u8 agc;
	__le16 sig_avg;
	__le16 noise_diff;
	u8 payload[0];
} __attribute__ ((packed));

struct iwl3945_rx_frame_hdr {
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

struct iwl3945_rx_frame_end {
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
struct iwl3945_rx_frame {
	struct iwl3945_rx_frame_stats stats;
	struct iwl3945_rx_frame_hdr hdr;
	struct iwl3945_rx_frame_end end;
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
 * Driver must place each REPLY_TX command into one of the prioritized Tx
 * queues in host DRAM, shared between driver and device.  When the device's
 * Tx scheduler and uCode are preparing to transmit, the device pulls the
 * Tx command over the PCI bus via one of the device's Tx DMA channels,
 * to fill an internal FIFO from which data will be transmitted.
 *
 * uCode handles all timing and protocol related to control frames
 * (RTS/CTS/ACK), based on flags in the Tx command.
 *
 * uCode handles retrying Tx when an ACK is expected but not received.
 * This includes trying lower data rates than the one requested in the Tx
 * command, as set up by the REPLY_RATE_SCALE (for 3945) or
 * REPLY_TX_LINK_QUALITY_CMD (4965).
 *
 * Driver sets up transmit power for various rates via REPLY_TX_PWR_TABLE_CMD.
 * This command must be executed after every RXON command, before Tx can occur.
 *****************************************************************************/

/* REPLY_TX Tx flags field */

/* 1: Use Request-To-Send protocol before this frame.
 * Mutually exclusive vs. TX_CMD_FLG_CTS_MSK. */
#define TX_CMD_FLG_RTS_MSK __constant_cpu_to_le32(1 << 1)

/* 1: Transmit Clear-To-Send to self before this frame.
 * Driver should set this for AUTH/DEAUTH/ASSOC-REQ/REASSOC mgmnt frames.
 * Mutually exclusive vs. TX_CMD_FLG_RTS_MSK. */
#define TX_CMD_FLG_CTS_MSK __constant_cpu_to_le32(1 << 2)

/* 1: Expect ACK from receiving station
 * 0: Don't expect ACK (MAC header's duration field s/b 0)
 * Set this for unicast frames, but not broadcast/multicast. */
#define TX_CMD_FLG_ACK_MSK __constant_cpu_to_le32(1 << 3)

/* 1: Use rate scale table (see REPLY_TX_LINK_QUALITY_CMD).
 *    Tx command's initial_rate_index indicates first rate to try;
 *    uCode walks through table for additional Tx attempts.
 * 0: Use Tx rate/MCS from Tx command's rate_n_flags field.
 *    This rate will be used for all Tx attempts; it will not be scaled. */
#define TX_CMD_FLG_STA_RATE_MSK __constant_cpu_to_le32(1 << 4)

/* 1: Expect immediate block-ack.
 * Set when Txing a block-ack request frame.  Also set TX_CMD_FLG_ACK_MSK. */
#define TX_CMD_FLG_IMM_BA_RSP_MASK  __constant_cpu_to_le32(1 << 6)

/* 1: Frame requires full Tx-Op protection.
 * Set this if either RTS or CTS Tx Flag gets set. */
#define TX_CMD_FLG_FULL_TXOP_PROT_MSK __constant_cpu_to_le32(1 << 7)

/* Tx antenna selection field; used only for 3945, reserved (0) for 4965.
 * Set field to "0" to allow 3945 uCode to select antenna (normal usage). */
#define TX_CMD_FLG_ANT_SEL_MSK __constant_cpu_to_le32(0xf00)
#define TX_CMD_FLG_ANT_A_MSK __constant_cpu_to_le32(1 << 8)
#define TX_CMD_FLG_ANT_B_MSK __constant_cpu_to_le32(1 << 9)

/* 1: Ignore Bluetooth priority for this frame.
 * 0: Delay Tx until Bluetooth device is done (normal usage). */
#define TX_CMD_FLG_BT_DIS_MSK __constant_cpu_to_le32(1 << 12)

/* 1: uCode overrides sequence control field in MAC header.
 * 0: Driver provides sequence control field in MAC header.
 * Set this for management frames, non-QOS data frames, non-unicast frames,
 * and also in Tx command embedded in REPLY_SCAN_CMD for active scans. */
#define TX_CMD_FLG_SEQ_CTL_MSK __constant_cpu_to_le32(1 << 13)

/* 1: This frame is non-last MPDU; more fragments are coming.
 * 0: Last fragment, or not using fragmentation. */
#define TX_CMD_FLG_MORE_FRAG_MSK __constant_cpu_to_le32(1 << 14)

/* 1: uCode calculates and inserts Timestamp Function (TSF) in outgoing frame.
 * 0: No TSF required in outgoing frame.
 * Set this for transmitting beacons and probe responses. */
#define TX_CMD_FLG_TSF_MSK __constant_cpu_to_le32(1 << 16)

/* 1: Driver inserted 2 bytes pad after the MAC header, for (required) dword
 *    alignment of frame's payload data field.
 * 0: No pad
 * Set this for MAC headers with 26 or 30 bytes, i.e. those with QOS or ADDR4
 * field (but not both).  Driver must align frame data (i.e. data following
 * MAC header) to DWORD boundary. */
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
 * REPLY_TX = 0x1c (command)
 */
struct iwl3945_tx_cmd {
	/*
	 * MPDU byte count:
	 * MAC header (24/26/30/32 bytes) + 2 bytes pad if 26/30 header size,
	 * + 8 byte IV for CCM or TKIP (not used for WEP)
	 * + Data payload
	 * + 8-byte MIC (not used for CCM/WEP)
	 * NOTE:  Does not include Tx command bytes, post-MAC pad bytes,
	 *        MIC (CCM) 8 bytes, ICV (WEP/TKIP/CKIP) 4 bytes, CRC 4 bytes.i
	 * Range: 14-2342 bytes.
	 */
	__le16 len;

	/*
	 * MPDU or MSDU byte count for next frame.
	 * Used for fragmentation and bursting, but not 11n aggregation.
	 * Same as "len", but for next frame.  Set to 0 if not applicable.
	 */
	__le16 next_frame_len;

	__le32 tx_flags;	/* TX_CMD_FLG_* */

	u8 rate;

	/* Index of recipient station in uCode's station table */
	u8 sta_id;
	u8 tid_tspec;
	u8 sec_ctl;
	u8 key[16];
	union {
		u8 byte[8];
		__le16 word[4];
		__le32 dw[2];
	} tkip_mic;
	__le32 next_frame_info;
	union {
		__le32 life_time;
		__le32 attempt;
	} stop_time;
	u8 supp_rates[2];
	u8 rts_retry_limit;	/*byte 50 */
	u8 data_retry_limit;	/*byte 51 */
	union {
		__le16 pm_frame_timeout;
		__le16 attempt_duration;
	} timeout;

	/*
	 * Duration of EDCA burst Tx Opportunity, in 32-usec units.
	 * Set this if txop time is not specified by HCCA protocol (e.g. by AP).
	 */
	__le16 driver_txop;

	/*
	 * MAC header goes here, followed by 2 bytes padding if MAC header
	 * length is 26 or 30 bytes, followed by payload data
	 */
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

/*
 * REPLY_TX = 0x1c (response)
 */
struct iwl3945_tx_resp {
	u8 failure_rts;
	u8 failure_frame;
	u8 bt_kill_count;
	u8 rate;
	__le32 wireless_media_time;
	__le32 status;		/* TX status */
} __attribute__ ((packed));

/*
 * REPLY_TX_PWR_TABLE_CMD = 0x97 (command, has simple generic response)
 */
struct iwl3945_txpowertable_cmd {
	u8 band;		/* 0: 5 GHz, 1: 2.4 GHz */
	u8 reserved;
	__le16 channel;
	struct iwl3945_power_per_rate power[IWL_MAX_RATES];
} __attribute__ ((packed));

struct iwl3945_rate_scaling_info {
	__le16 rate_n_flags;
	u8 try_cnt;
	u8 next_rate_index;
} __attribute__ ((packed));

/**
 * struct iwl3945_rate_scaling_cmd - Rate Scaling Command & Response
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
struct iwl3945_rate_scaling_cmd {
	u8 table_id;
	u8 reserved[3];
	struct iwl3945_rate_scaling_info table[IWL_MAX_RATES];
} __attribute__ ((packed));

/*
 * REPLY_BT_CONFIG = 0x9b (command, has simple generic response)
 *
 * 3945 and 4965 support hardware handshake with Bluetooth device on
 * same platform.  Bluetooth device alerts wireless device when it will Tx;
 * wireless device can delay or kill its own Tx to accomodate.
 */
struct iwl3945_bt_cmd {
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

struct iwl3945_measure_channel {
	__le32 duration;	/* measurement duration in extended beacon
				 * format */
	u8 channel;		/* channel to measure */
	u8 type;		/* see enum iwl3945_measure_type */
	__le16 reserved;
} __attribute__ ((packed));

/*
 * REPLY_SPECTRUM_MEASUREMENT_CMD = 0x74 (command)
 */
struct iwl3945_spectrum_cmd {
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
	struct iwl3945_measure_channel channels[10];
} __attribute__ ((packed));

/*
 * REPLY_SPECTRUM_MEASUREMENT_CMD = 0x74 (response)
 */
struct iwl3945_spectrum_resp {
	u8 token;
	u8 id;			/* id of the prior command replaced, or 0xff */
	__le16 status;		/* 0 - command will be handled
				 * 1 - cannot handle (conflicts with another
				 *     measurement) */
} __attribute__ ((packed));

enum iwl3945_measurement_state {
	IWL_MEASUREMENT_START = 0,
	IWL_MEASUREMENT_STOP = 1,
};

enum iwl3945_measurement_status {
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

struct iwl3945_measurement_histogram {
	__le32 ofdm[NUM_ELEMENTS_IN_HISTOGRAM];	/* in 0.8usec counts */
	__le32 cck[NUM_ELEMENTS_IN_HISTOGRAM];	/* in 1usec counts */
} __attribute__ ((packed));

/* clear channel availability counters */
struct iwl3945_measurement_cca_counters {
	__le32 ofdm;
	__le32 cck;
} __attribute__ ((packed));

enum iwl3945_measure_type {
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
struct iwl3945_spectrum_notification {
	u8 id;			/* measurement id -- 0 or 1 */
	u8 token;
	u8 channel_index;	/* index in measurement channel list */
	u8 state;		/* 0 - start, 1 - stop */
	__le32 start_time;	/* lower 32-bits of TSF */
	u8 band;		/* 0 - 5.2GHz, 1 - 2.4GHz */
	u8 channel;
	u8 type;		/* see enum iwl3945_measurement_type */
	u8 reserved1;
	/* NOTE:  cca_ofdm, cca_cck, basic_type, and histogram are only only
	 * valid if applicable for measurement type requested. */
	__le32 cca_ofdm;	/* cca fraction time in 40Mhz clock periods */
	__le32 cca_cck;		/* cca fraction time in 44Mhz clock periods */
	__le32 cca_time;	/* channel load time in usecs */
	u8 basic_type;		/* 0 - bss, 1 - ofdm preamble, 2 -
				 * unidentified */
	u8 reserved2[3];
	struct iwl3945_measurement_histogram histogram;
	__le32 stop_time;	/* lower 32-bits of TSF */
	__le32 status;		/* see iwl3945_measurement_status */
} __attribute__ ((packed));

/******************************************************************************
 * (7)
 * Power Management Commands, Responses, Notifications:
 *
 *****************************************************************************/

/**
 * struct iwl3945_powertable_cmd - Power Table Command
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

#define IWL_POWER_DRIVER_ALLOW_SLEEP_MSK	__constant_cpu_to_le32(1<<0)
#define IWL_POWER_SLEEP_OVER_DTIM_MSK		__constant_cpu_to_le32(1<<2)
#define IWL_POWER_PCI_PM_MSK			__constant_cpu_to_le32(1<<3)
struct iwl3945_powertable_cmd {
	__le32 flags;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 sleep_interval[IWL_POWER_VEC_SIZE];
} __attribute__((packed));

/*
 * PM_SLEEP_NOTIFICATION = 0x7A (notification only, not a command)
 * 3945 and 4965 identical.
 */
struct iwl3945_sleep_notification {
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
struct iwl3945_card_state_cmd {
	__le32 status;		/* CARD_STATE_CMD_* request new power state */
} __attribute__ ((packed));

/*
 * CARD_STATE_NOTIFICATION = 0xa1 (notification only, not a command)
 */
struct iwl3945_card_state_notif {
	__le32 flags;
} __attribute__ ((packed));

#define HW_CARD_DISABLED   0x01
#define SW_CARD_DISABLED   0x02
#define RF_CARD_DISABLED   0x04
#define RXON_CARD_DISABLED 0x10

struct iwl3945_ct_kill_config {
	__le32   reserved;
	__le32   critical_temperature_M;
	__le32   critical_temperature_R;
}  __attribute__ ((packed));

/******************************************************************************
 * (8)
 * Scan Commands, Responses, Notifications:
 *
 *****************************************************************************/

/**
 * struct iwl3945_scan_channel - entry in REPLY_SCAN_CMD channel table
 *
 * One for each channel in the scan list.
 * Each channel can independently select:
 * 1)  SSID for directed active scans
 * 2)  Txpower setting (for rate specified within Tx command)
 * 3)  How long to stay on-channel (behavior may be modified by quiet_time,
 *     quiet_plcp_th, good_CRC_th)
 *
 * To avoid uCode errors, make sure the following are true (see comments
 * under struct iwl3945_scan_cmd about max_out_time and quiet_time):
 * 1)  If using passive_dwell (i.e. passive_dwell != 0):
 *     active_dwell <= passive_dwell (< max_out_time if max_out_time != 0)
 * 2)  quiet_time <= active_dwell
 * 3)  If restricting off-channel time (i.e. max_out_time !=0):
 *     passive_dwell < max_out_time
 *     active_dwell < max_out_time
 */
struct iwl3945_scan_channel {
	/*
	 * type is defined as:
	 * 0:0 1 = active, 0 = passive
	 * 1:4 SSID direct bit map; if a bit is set, then corresponding
	 *     SSID IE is transmitted in probe request.
	 * 5:7 reserved
	 */
	u8 type;
	u8 channel;	/* band is selected by iwl3945_scan_cmd "flags" field */
	struct iwl3945_tx_power tpc;
	__le16 active_dwell;	/* in 1024-uSec TU (time units), typ 5-50 */
	__le16 passive_dwell;	/* in 1024-uSec TU (time units), typ 20-500 */
} __attribute__ ((packed));

/**
 * struct iwl3945_ssid_ie - directed scan network information element
 *
 * Up to 4 of these may appear in REPLY_SCAN_CMD, selected by "type" field
 * in struct iwl3945_scan_channel; each channel may select different ssids from
 * among the 4 entries.  SSID IEs get transmitted in reverse order of entry.
 */
struct iwl3945_ssid_ie {
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
 *
 * The hardware scan command is very powerful; the driver can set it up to
 * maintain (relatively) normal network traffic while doing a scan in the
 * background.  The max_out_time and suspend_time control the ratio of how
 * long the device stays on an associated network channel ("service channel")
 * vs. how long it's away from the service channel, tuned to other channels
 * for scanning.
 *
 * max_out_time is the max time off-channel (in usec), and suspend_time
 * is how long (in "extended beacon" format) that the scan is "suspended"
 * after returning to the service channel.  That is, suspend_time is the
 * time that we stay on the service channel, doing normal work, between
 * scan segments.  The driver may set these parameters differently to support
 * scanning when associated vs. not associated, and light vs. heavy traffic
 * loads when associated.
 *
 * After receiving this command, the device's scan engine does the following;
 *
 * 1)  Sends SCAN_START notification to driver
 * 2)  Checks to see if it has time to do scan for one channel
 * 3)  Sends NULL packet, with power-save (PS) bit set to 1,
 *     to tell AP that we're going off-channel
 * 4)  Tunes to first channel in scan list, does active or passive scan
 * 5)  Sends SCAN_RESULT notification to driver
 * 6)  Checks to see if it has time to do scan on *next* channel in list
 * 7)  Repeats 4-6 until it no longer has time to scan the next channel
 *     before max_out_time expires
 * 8)  Returns to service channel
 * 9)  Sends NULL packet with PS=0 to tell AP that we're back
 * 10) Stays on service channel until suspend_time expires
 * 11) Repeats entire process 2-10 until list is complete
 * 12) Sends SCAN_COMPLETE notification
 *
 * For fast, efficient scans, the scan command also has support for staying on
 * a channel for just a short time, if doing active scanning and getting no
 * responses to the transmitted probe request.  This time is controlled by
 * quiet_time, and the number of received packets below which a channel is
 * considered "quiet" is controlled by quiet_plcp_threshold.
 *
 * For active scanning on channels that have regulatory restrictions against
 * blindly transmitting, the scan can listen before transmitting, to make sure
 * that there is already legitimate activity on the channel.  If enough
 * packets are cleanly received on the channel (controlled by good_CRC_th,
 * typical value 1), the scan engine starts transmitting probe requests.
 *
 * Driver must use separate scan commands for 2.4 vs. 5 GHz bands.
 *
 * To avoid uCode errors, see timing restrictions described under
 * struct iwl3945_scan_channel.
 */
struct iwl3945_scan_cmd {
	__le16 len;
	u8 reserved0;
	u8 channel_count;	/* # channels in channel list */
	__le16 quiet_time;	/* dwell only this # millisecs on quiet channel
				 * (only for active scan) */
	__le16 quiet_plcp_th;	/* quiet chnl is < this # pkts (typ. 1) */
	__le16 good_CRC_th;	/* passive -> active promotion threshold */
	__le16 reserved1;
	__le32 max_out_time;	/* max usec to be away from associated (service)
				 * channel */
	__le32 suspend_time;	/* pause scan this long (in "extended beacon
				 * format") when returning to service channel:
				 * 3945; 31:24 # beacons, 19:0 additional usec,
				 * 4965; 31:22 # beacons, 21:0 additional usec.
				 */
	__le32 flags;		/* RXON_FLG_* */
	__le32 filter_flags;	/* RXON_FILTER_* */

	/* For active scans (set to all-0s for passive scans).
	 * Does not include payload.  Must specify Tx rate; no rate scaling. */
	struct iwl3945_tx_cmd tx_cmd;

	/* For directed active scans (set to all-0s otherwise) */
	struct iwl3945_ssid_ie direct_scan[PROBE_OPTION_MAX];

	/*
	 * Probe request frame, followed by channel list.
	 *
	 * Size of probe request frame is specified by byte count in tx_cmd.
	 * Channel list follows immediately after probe request frame.
	 * Number of channels in list is specified by channel_count.
	 * Each channel in list is of type:
	 *
	 * struct iwl3945_scan_channel channels[0];
	 *
	 * NOTE:  Only one band of channels can be scanned per pass.  You
	 * must not mix 2.4GHz channels and 5.2GHz channels, and you must wait
	 * for one scan to complete (i.e. receive SCAN_COMPLETE_NOTIFICATION)
	 * before requesting another scan.
	 */
	u8 data[0];
} __attribute__ ((packed));

/* Can abort will notify by complete notification with abort status. */
#define CAN_ABORT_STATUS	__constant_cpu_to_le32(0x1)
/* complete notification statuses */
#define ABORT_STATUS            0x2

/*
 * REPLY_SCAN_CMD = 0x80 (response)
 */
struct iwl3945_scanreq_notification {
	__le32 status;		/* 1: okay, 2: cannot fulfill request */
} __attribute__ ((packed));

/*
 * SCAN_START_NOTIFICATION = 0x82 (notification only, not a command)
 */
struct iwl3945_scanstart_notification {
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
struct iwl3945_scanresults_notification {
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
struct iwl3945_scancomplete_notification {
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
struct iwl3945_beacon_notif {
	struct iwl3945_tx_resp beacon_notify_hdr;
	__le32 low_tsf;
	__le32 high_tsf;
	__le32 ibss_mgr_status;
} __attribute__ ((packed));

/*
 * REPLY_TX_BEACON = 0x91 (command, has simple generic response)
 */
struct iwl3945_tx_beacon_cmd {
	struct iwl3945_tx_cmd tx;
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
} __attribute__ ((packed));

struct statistics_rx_non_phy {
	__le32 bogus_cts;	/* CTS received when not expecting CTS */
	__le32 bogus_ack;	/* ACK received when not expecting ACK */
	__le32 non_bssid_frames;	/* number of frames with BSSID that
					 * doesn't belong to the STA BSSID */
	__le32 filtered_frames;	/* count frames that were dumped in the
				 * filtering process */
	__le32 non_channel_beacons;	/* beacons with our bss id but not on
					 * our serving channel */
} __attribute__ ((packed));

struct statistics_rx {
	struct statistics_rx_phy ofdm;
	struct statistics_rx_phy cck;
	struct statistics_rx_non_phy general;
} __attribute__ ((packed));

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
} __attribute__ ((packed));

struct statistics_general {
	__le32 temperature;
	struct statistics_dbg dbg;
	__le32 sleep_time;
	__le32 slots_out;
	__le32 slots_idle;
	__le32 ttl_timestamp;
	struct statistics_div div;
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
struct iwl3945_statistics_cmd {
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
struct iwl3945_notif_statistics {
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

struct iwl3945_missed_beacon_notif {
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

struct iwl3945_sensitivity_cmd {
	__le16 control;
	__le16 table[HD_TABLE_SIZE];
} __attribute__ ((packed));

struct iwl3945_calibration_cmd {
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
struct iwl3945_led_cmd {
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

struct iwl3945_rx_packet {
	__le32 len;
	struct iwl3945_cmd_header hdr;
	union {
		struct iwl3945_alive_resp alive_frame;
		struct iwl3945_rx_frame rx_frame;
		struct iwl3945_tx_resp tx_resp;
		struct iwl3945_spectrum_notification spectrum_notif;
		struct iwl3945_csa_notification csa_notif;
		struct iwl3945_error_resp err_resp;
		struct iwl3945_card_state_notif card_state_notif;
		struct iwl3945_beacon_notif beacon_status;
		struct iwl3945_add_sta_resp add_sta;
		struct iwl3945_sleep_notification sleep_notif;
		struct iwl3945_spectrum_resp spectrum;
		struct iwl3945_notif_statistics stats;
		__le32 status;
		u8 raw[0];
	} u;
} __attribute__ ((packed));

#define IWL_RX_FRAME_SIZE        (4 + sizeof(struct iwl3945_rx_frame))

#endif				/* __iwl3945_3945_commands_h__ */

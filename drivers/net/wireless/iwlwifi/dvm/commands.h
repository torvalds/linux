/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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
 * Please use this file (commands.h) only for uCode API definitions.
 * Please use iwl-xxxx-hw.h for hardware-related definitions.
 * Please use dev.h for driver implementation definitions.
 */

#ifndef __iwl_commands_h__
#define __iwl_commands_h__

#include <linux/ieee80211.h>
#include <linux/types.h>


enum {
	REPLY_ALIVE = 0x1,
	REPLY_ERROR = 0x2,
	REPLY_ECHO = 0x3,		/* test command */

	/* RXON and QOS commands */
	REPLY_RXON = 0x10,
	REPLY_RXON_ASSOC = 0x11,
	REPLY_QOS_PARAM = 0x13,
	REPLY_RXON_TIMING = 0x14,

	/* Multi-Station support */
	REPLY_ADD_STA = 0x18,
	REPLY_REMOVE_STA = 0x19,
	REPLY_REMOVE_ALL_STA = 0x1a,	/* not used */
	REPLY_TXFIFO_FLUSH = 0x1e,

	/* Security */
	REPLY_WEPKEY = 0x20,

	/* RX, TX, LEDs */
	REPLY_TX = 0x1c,
	REPLY_LEDS_CMD = 0x48,
	REPLY_TX_LINK_QUALITY_CMD = 0x4e,

	/* WiMAX coexistence */
	COEX_PRIORITY_TABLE_CMD = 0x5a,
	COEX_MEDIUM_NOTIFICATION = 0x5b,
	COEX_EVENT_CMD = 0x5c,

	/* Calibration */
	TEMPERATURE_NOTIFICATION = 0x62,
	CALIBRATION_CFG_CMD = 0x65,
	CALIBRATION_RES_NOTIFICATION = 0x66,
	CALIBRATION_COMPLETE_NOTIFICATION = 0x67,

	/* 802.11h related */
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
	REPLY_TX_POWER_DBM_CMD = 0x95,
	QUIET_NOTIFICATION = 0x96,		/* not used */
	REPLY_TX_PWR_TABLE_CMD = 0x97,
	REPLY_TX_POWER_DBM_CMD_V1 = 0x98,	/* old version of API */
	TX_ANT_CONFIGURATION_CMD = 0x98,
	MEASURE_ABORT_NOTIFICATION = 0x99,	/* not used */

	/* Bluetooth device coexistence config command */
	REPLY_BT_CONFIG = 0x9b,

	/* Statistics */
	REPLY_STATISTICS_CMD = 0x9c,
	STATISTICS_NOTIFICATION = 0x9d,

	/* RF-KILL commands and notifications */
	REPLY_CARD_STATE_CMD = 0xa0,
	CARD_STATE_NOTIFICATION = 0xa1,

	/* Missed beacons notification */
	MISSED_BEACONS_NOTIFICATION = 0xa2,

	REPLY_CT_KILL_CONFIG_CMD = 0xa4,
	SENSITIVITY_CMD = 0xa8,
	REPLY_PHY_CALIBRATION_CMD = 0xb0,
	REPLY_RX_PHY_CMD = 0xc0,
	REPLY_RX_MPDU_CMD = 0xc1,
	REPLY_RX = 0xc3,
	REPLY_COMPRESSED_BA = 0xc5,

	/* BT Coex */
	REPLY_BT_COEX_PRIO_TABLE = 0xcc,
	REPLY_BT_COEX_PROT_ENV = 0xcd,
	REPLY_BT_COEX_PROFILE_NOTIF = 0xce,

	/* PAN commands */
	REPLY_WIPAN_PARAMS = 0xb2,
	REPLY_WIPAN_RXON = 0xb3,	/* use REPLY_RXON structure */
	REPLY_WIPAN_RXON_TIMING = 0xb4,	/* use REPLY_RXON_TIMING structure */
	REPLY_WIPAN_RXON_ASSOC = 0xb6,	/* use REPLY_RXON_ASSOC structure */
	REPLY_WIPAN_QOS_PARAM = 0xb7,	/* use REPLY_QOS_PARAM structure */
	REPLY_WIPAN_WEPKEY = 0xb8,	/* use REPLY_WEPKEY structure */
	REPLY_WIPAN_P2P_CHANNEL_SWITCH = 0xb9,
	REPLY_WIPAN_NOA_NOTIFICATION = 0xbc,
	REPLY_WIPAN_DEACTIVATION_COMPLETE = 0xbd,

	REPLY_WOWLAN_PATTERNS = 0xe0,
	REPLY_WOWLAN_WAKEUP_FILTER = 0xe1,
	REPLY_WOWLAN_TSC_RSC_PARAMS = 0xe2,
	REPLY_WOWLAN_TKIP_PARAMS = 0xe3,
	REPLY_WOWLAN_KEK_KCK_MATERIAL = 0xe4,
	REPLY_WOWLAN_GET_STATUS = 0xe5,
	REPLY_D3_CONFIG = 0xd3,

	REPLY_MAX = 0xff
};

/*
 * Minimum number of queues. MAX_NUM is defined in hw specific files.
 * Set the minimum to accommodate
 *  - 4 standard TX queues
 *  - the command queue
 *  - 4 PAN TX queues
 *  - the PAN multicast queue, and
 *  - the AUX (TX during scan dwell) queue.
 */
#define IWL_MIN_NUM_QUEUES	11

/*
 * Command queue depends on iPAN support.
 */
#define IWL_DEFAULT_CMD_QUEUE_NUM	4
#define IWL_IPAN_CMD_QUEUE_NUM		9

#define IWL_TX_FIFO_BK		0	/* shared */
#define IWL_TX_FIFO_BE		1
#define IWL_TX_FIFO_VI		2	/* shared */
#define IWL_TX_FIFO_VO		3
#define IWL_TX_FIFO_BK_IPAN	IWL_TX_FIFO_BK
#define IWL_TX_FIFO_BE_IPAN	4
#define IWL_TX_FIFO_VI_IPAN	IWL_TX_FIFO_VI
#define IWL_TX_FIFO_VO_IPAN	5
/* re-uses the VO FIFO, uCode will properly flush/schedule */
#define IWL_TX_FIFO_AUX		5
#define IWL_TX_FIFO_UNUSED	255

#define IWLAGN_CMD_FIFO_NUM	7

/*
 * This queue number is required for proper operation
 * because the ucode will stop/start the scheduler as
 * required.
 */
#define IWL_IPAN_MCAST_QUEUE	8

/******************************************************************************
 * (0)
 * Commonly used structures and definitions:
 * Command header, rate_n_flags, txpower
 *
 *****************************************************************************/

/**
 * iwlagn rate_n_flags bit fields
 *
 * rate_n_flags format is used in following iwlagn commands:
 *  REPLY_RX (response only)
 *  REPLY_RX_MPDU (response only)
 *  REPLY_TX (both command and response)
 *  REPLY_TX_LINK_QUALITY_CMD
 *
 * High-throughput (HT) rate format for bits 7:0 (bit 8 must be "1"):
 *  2-0:  0)   6 Mbps
 *        1)  12 Mbps
 *        2)  18 Mbps
 *        3)  24 Mbps
 *        4)  36 Mbps
 *        5)  48 Mbps
 *        6)  54 Mbps
 *        7)  60 Mbps
 *
 *  4-3:  0)  Single stream (SISO)
 *        1)  Dual stream (MIMO)
 *        2)  Triple stream (MIMO)
 *
 *    5:  Value of 0x20 in bits 7:0 indicates 6 Mbps HT40 duplicate data
 *
 * Legacy OFDM rate format for bits 7:0 (bit 8 must be "0", bit 9 "0"):
 *  3-0:  0xD)   6 Mbps
 *        0xF)   9 Mbps
 *        0x5)  12 Mbps
 *        0x7)  18 Mbps
 *        0x9)  24 Mbps
 *        0xB)  36 Mbps
 *        0x1)  48 Mbps
 *        0x3)  54 Mbps
 *
 * Legacy CCK rate format for bits 7:0 (bit 8 must be "0", bit 9 "1"):
 *  6-0:   10)  1 Mbps
 *         20)  2 Mbps
 *         55)  5.5 Mbps
 *        110)  11 Mbps
 */
#define RATE_MCS_CODE_MSK 0x7
#define RATE_MCS_SPATIAL_POS 3
#define RATE_MCS_SPATIAL_MSK 0x18
#define RATE_MCS_HT_DUP_POS 5
#define RATE_MCS_HT_DUP_MSK 0x20
/* Both legacy and HT use bits 7:0 as the CCK/OFDM rate or HT MCS */
#define RATE_MCS_RATE_MSK 0xff

/* Bit 8: (1) HT format, (0) legacy format in bits 7:0 */
#define RATE_MCS_FLAGS_POS 8
#define RATE_MCS_HT_POS 8
#define RATE_MCS_HT_MSK 0x100

/* Bit 9: (1) CCK, (0) OFDM.  HT (bit 8) must be "0" for this bit to be valid */
#define RATE_MCS_CCK_POS 9
#define RATE_MCS_CCK_MSK 0x200

/* Bit 10: (1) Use Green Field preamble */
#define RATE_MCS_GF_POS 10
#define RATE_MCS_GF_MSK 0x400

/* Bit 11: (1) Use 40Mhz HT40 chnl width, (0) use 20 MHz legacy chnl width */
#define RATE_MCS_HT40_POS 11
#define RATE_MCS_HT40_MSK 0x800

/* Bit 12: (1) Duplicate data on both 20MHz chnls. HT40 (bit 11) must be set. */
#define RATE_MCS_DUP_POS 12
#define RATE_MCS_DUP_MSK 0x1000

/* Bit 13: (1) Short guard interval (0.4 usec), (0) normal GI (0.8 usec) */
#define RATE_MCS_SGI_POS 13
#define RATE_MCS_SGI_MSK 0x2000

/**
 * rate_n_flags Tx antenna masks
 * 4965 has 2 transmitters
 * 5100 has 1 transmitter B
 * 5150 has 1 transmitter A
 * 5300 has 3 transmitters
 * 5350 has 3 transmitters
 * bit14:16
 */
#define RATE_MCS_ANT_POS	14
#define RATE_MCS_ANT_A_MSK	0x04000
#define RATE_MCS_ANT_B_MSK	0x08000
#define RATE_MCS_ANT_C_MSK	0x10000
#define RATE_MCS_ANT_AB_MSK	(RATE_MCS_ANT_A_MSK | RATE_MCS_ANT_B_MSK)
#define RATE_MCS_ANT_ABC_MSK	(RATE_MCS_ANT_AB_MSK | RATE_MCS_ANT_C_MSK)
#define RATE_ANT_NUM 3

#define POWER_TABLE_NUM_ENTRIES			33
#define POWER_TABLE_NUM_HT_OFDM_ENTRIES		32
#define POWER_TABLE_CCK_ENTRY			32

#define IWL_PWR_NUM_HT_OFDM_ENTRIES		24
#define IWL_PWR_CCK_ENTRIES			2

/**
 * struct tx_power_dual_stream
 *
 * Table entries in REPLY_TX_PWR_TABLE_CMD, REPLY_CHANNEL_SWITCH
 *
 * Same format as iwl_tx_power_dual_stream, but __le32
 */
struct tx_power_dual_stream {
	__le32 dw;
} __packed;

/**
 * Command REPLY_TX_POWER_DBM_CMD = 0x98
 * struct iwlagn_tx_power_dbm_cmd
 */
#define IWLAGN_TX_POWER_AUTO 0x7f
#define IWLAGN_TX_POWER_NO_CLOSED (0x1 << 6)

struct iwlagn_tx_power_dbm_cmd {
	s8 global_lmt; /*in half-dBm (e.g. 30 = 15 dBm) */
	u8 flags;
	s8 srv_chan_lmt; /*in half-dBm (e.g. 30 = 15 dBm) */
	u8 reserved;
} __packed;

/**
 * Command TX_ANT_CONFIGURATION_CMD = 0x98
 * This command is used to configure valid Tx antenna.
 * By default uCode concludes the valid antenna according to the radio flavor.
 * This command enables the driver to override/modify this conclusion.
 */
struct iwl_tx_ant_config_cmd {
	__le32 valid;
} __packed;

/******************************************************************************
 * (0a)
 * Alive and Error Commands & Responses:
 *
 *****************************************************************************/

#define UCODE_VALID_OK	cpu_to_le32(0x1)

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
 *     Its header format is:
 *
 *	__le32 log_size;     log capacity (in number of entries)
 *	__le32 type;         (1) timestamp with each entry, (0) no timestamp
 *	__le32 wraps;        # times uCode has wrapped to top of circular buffer
 *      __le32 write_index;  next circular buffer entry that uCode would fill
 *
 *     The header is followed by the circular buffer of log entries.  Entries
 *     with timestamps have the following format:
 *
 *	__le32 event_id;     range 0 - 1500
 *	__le32 timestamp;    low 32 bits of TSF (of network, if associated)
 *	__le32 data;         event_id-specific data value
 *
 *     Entries without timestamps contain only event_id and data.
 *
 *
 * 2)  error_event_table_ptr indicates base of the error log.  This contains
 *     information about any uCode error that occurs.  For agn, the format
 *     of the error log is defined by struct iwl_error_event_table.
 *
 * The Linux driver can print both logs to the system log when a uCode error
 * occurs.
 */

/*
 * Note: This structure is read from the device with IO accesses,
 * and the reading already does the endian conversion. As it is
 * read with u32-sized accesses, any members with a different size
 * need to be ordered correctly though!
 */
struct iwl_error_event_table {
	u32 valid;		/* (nonzero) valid, (0) log is empty */
	u32 error_id;		/* type of error */
	u32 pc;			/* program counter */
	u32 blink1;		/* branch link */
	u32 blink2;		/* branch link */
	u32 ilink1;		/* interrupt link */
	u32 ilink2;		/* interrupt link */
	u32 data1;		/* error-specific data */
	u32 data2;		/* error-specific data */
	u32 line;		/* source code line of error */
	u32 bcon_time;		/* beacon timer */
	u32 tsf_low;		/* network timestamp function timer */
	u32 tsf_hi;		/* network timestamp function timer */
	u32 gp1;		/* GP1 timer register */
	u32 gp2;		/* GP2 timer register */
	u32 gp3;		/* GP3 timer register */
	u32 ucode_ver;		/* uCode version */
	u32 hw_ver;		/* HW Silicon version */
	u32 brd_ver;		/* HW board version */
	u32 log_pc;		/* log program counter */
	u32 frame_ptr;		/* frame pointer */
	u32 stack_ptr;		/* stack pointer */
	u32 hcmd;		/* last host command header */
	u32 isr0;		/* isr status register LMPM_NIC_ISR0:
				 * rxtx_flag */
	u32 isr1;		/* isr status register LMPM_NIC_ISR1:
				 * host_flag */
	u32 isr2;		/* isr status register LMPM_NIC_ISR2:
				 * enc_flag */
	u32 isr3;		/* isr status register LMPM_NIC_ISR3:
				 * time_flag */
	u32 isr4;		/* isr status register LMPM_NIC_ISR4:
				 * wico interrupt */
	u32 isr_pref;		/* isr status register LMPM_NIC_PREF_STAT */
	u32 wait_event;		/* wait event() caller address */
	u32 l2p_control;	/* L2pControlField */
	u32 l2p_duration;	/* L2pDurationField */
	u32 l2p_mhvalid;	/* L2pMhValidBits */
	u32 l2p_addr_match;	/* L2pAddrMatchStat */
	u32 lmpm_pmg_sel;	/* indicate which clocks are turned on
				 * (LMPM_PMG_SEL) */
	u32 u_timestamp;	/* indicate when the date and time of the
				 * compilation */
	u32 flow_handler;	/* FH read/write pointers, RX credit */
} __packed;

struct iwl_alive_resp {
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
} __packed;

/*
 * REPLY_ERROR = 0x2 (response only, not a command)
 */
struct iwl_error_resp {
	__le32 error_type;
	u8 cmd_id;
	u8 reserved1;
	__le16 bad_cmd_seq_num;
	__le32 error_info;
	__le64 timestamp;
} __packed;

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
	RXON_DEV_TYPE_CP = 7,
	RXON_DEV_TYPE_2STA = 8,
	RXON_DEV_TYPE_P2P = 9,
};


#define RXON_RX_CHAIN_DRIVER_FORCE_MSK		cpu_to_le16(0x1 << 0)
#define RXON_RX_CHAIN_DRIVER_FORCE_POS		(0)
#define RXON_RX_CHAIN_VALID_MSK			cpu_to_le16(0x7 << 1)
#define RXON_RX_CHAIN_VALID_POS			(1)
#define RXON_RX_CHAIN_FORCE_SEL_MSK		cpu_to_le16(0x7 << 4)
#define RXON_RX_CHAIN_FORCE_SEL_POS		(4)
#define RXON_RX_CHAIN_FORCE_MIMO_SEL_MSK	cpu_to_le16(0x7 << 7)
#define RXON_RX_CHAIN_FORCE_MIMO_SEL_POS	(7)
#define RXON_RX_CHAIN_CNT_MSK			cpu_to_le16(0x3 << 10)
#define RXON_RX_CHAIN_CNT_POS			(10)
#define RXON_RX_CHAIN_MIMO_CNT_MSK		cpu_to_le16(0x3 << 12)
#define RXON_RX_CHAIN_MIMO_CNT_POS		(12)
#define RXON_RX_CHAIN_MIMO_FORCE_MSK		cpu_to_le16(0x1 << 14)
#define RXON_RX_CHAIN_MIMO_FORCE_POS		(14)

/* rx_config flags */
/* band & modulation selection */
#define RXON_FLG_BAND_24G_MSK           cpu_to_le32(1 << 0)
#define RXON_FLG_CCK_MSK                cpu_to_le32(1 << 1)
/* auto detection enable */
#define RXON_FLG_AUTO_DETECT_MSK        cpu_to_le32(1 << 2)
/* TGg protection when tx */
#define RXON_FLG_TGG_PROTECT_MSK        cpu_to_le32(1 << 3)
/* cck short slot & preamble */
#define RXON_FLG_SHORT_SLOT_MSK          cpu_to_le32(1 << 4)
#define RXON_FLG_SHORT_PREAMBLE_MSK     cpu_to_le32(1 << 5)
/* antenna selection */
#define RXON_FLG_DIS_DIV_MSK            cpu_to_le32(1 << 7)
#define RXON_FLG_ANT_SEL_MSK            cpu_to_le32(0x0f00)
#define RXON_FLG_ANT_A_MSK              cpu_to_le32(1 << 8)
#define RXON_FLG_ANT_B_MSK              cpu_to_le32(1 << 9)
/* radar detection enable */
#define RXON_FLG_RADAR_DETECT_MSK       cpu_to_le32(1 << 12)
#define RXON_FLG_TGJ_NARROW_BAND_MSK    cpu_to_le32(1 << 13)
/* rx response to host with 8-byte TSF
* (according to ON_AIR deassertion) */
#define RXON_FLG_TSF2HOST_MSK           cpu_to_le32(1 << 15)


/* HT flags */
#define RXON_FLG_CTRL_CHANNEL_LOC_POS		(22)
#define RXON_FLG_CTRL_CHANNEL_LOC_HI_MSK	cpu_to_le32(0x1 << 22)

#define RXON_FLG_HT_OPERATING_MODE_POS		(23)

#define RXON_FLG_HT_PROT_MSK			cpu_to_le32(0x1 << 23)
#define RXON_FLG_HT40_PROT_MSK			cpu_to_le32(0x2 << 23)

#define RXON_FLG_CHANNEL_MODE_POS		(25)
#define RXON_FLG_CHANNEL_MODE_MSK		cpu_to_le32(0x3 << 25)

/* channel mode */
enum {
	CHANNEL_MODE_LEGACY = 0,
	CHANNEL_MODE_PURE_40 = 1,
	CHANNEL_MODE_MIXED = 2,
	CHANNEL_MODE_RESERVED = 3,
};
#define RXON_FLG_CHANNEL_MODE_LEGACY	cpu_to_le32(CHANNEL_MODE_LEGACY << RXON_FLG_CHANNEL_MODE_POS)
#define RXON_FLG_CHANNEL_MODE_PURE_40	cpu_to_le32(CHANNEL_MODE_PURE_40 << RXON_FLG_CHANNEL_MODE_POS)
#define RXON_FLG_CHANNEL_MODE_MIXED	cpu_to_le32(CHANNEL_MODE_MIXED << RXON_FLG_CHANNEL_MODE_POS)

/* CTS to self (if spec allows) flag */
#define RXON_FLG_SELF_CTS_EN			cpu_to_le32(0x1<<30)

/* rx_config filter flags */
/* accept all data frames */
#define RXON_FILTER_PROMISC_MSK         cpu_to_le32(1 << 0)
/* pass control & management to host */
#define RXON_FILTER_CTL2HOST_MSK        cpu_to_le32(1 << 1)
/* accept multi-cast */
#define RXON_FILTER_ACCEPT_GRP_MSK      cpu_to_le32(1 << 2)
/* don't decrypt uni-cast frames */
#define RXON_FILTER_DIS_DECRYPT_MSK     cpu_to_le32(1 << 3)
/* don't decrypt multi-cast frames */
#define RXON_FILTER_DIS_GRP_DECRYPT_MSK cpu_to_le32(1 << 4)
/* STA is associated */
#define RXON_FILTER_ASSOC_MSK           cpu_to_le32(1 << 5)
/* transfer to host non bssid beacons in associated state */
#define RXON_FILTER_BCON_AWARE_MSK      cpu_to_le32(1 << 6)

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

struct iwl_rxon_cmd {
	u8 node_addr[6];
	__le16 reserved1;
	u8 bssid_addr[6];
	__le16 reserved2;
	u8 wlap_bssid_addr[6];
	__le16 reserved3;
	u8 dev_type;
	u8 air_propagation;
	__le16 rx_chain;
	u8 ofdm_basic_rates;
	u8 cck_basic_rates;
	__le16 assoc_id;
	__le32 flags;
	__le32 filter_flags;
	__le16 channel;
	u8 ofdm_ht_single_stream_basic_rates;
	u8 ofdm_ht_dual_stream_basic_rates;
	u8 ofdm_ht_triple_stream_basic_rates;
	u8 reserved5;
	__le16 acquisition_data;
	__le16 reserved6;
} __packed;

/*
 * REPLY_RXON_ASSOC = 0x11 (command, has simple generic response)
 */
struct iwl_rxon_assoc_cmd {
	__le32 flags;
	__le32 filter_flags;
	u8 ofdm_basic_rates;
	u8 cck_basic_rates;
	__le16 reserved1;
	u8 ofdm_ht_single_stream_basic_rates;
	u8 ofdm_ht_dual_stream_basic_rates;
	u8 ofdm_ht_triple_stream_basic_rates;
	u8 reserved2;
	__le16 rx_chain_select_flags;
	__le16 acquisition_data;
	__le32 reserved3;
} __packed;

#define IWL_CONN_MAX_LISTEN_INTERVAL	10
#define IWL_MAX_UCODE_BEACON_INTERVAL	4 /* 4096 */

/*
 * REPLY_RXON_TIMING = 0x14 (command, has simple generic response)
 */
struct iwl_rxon_time_cmd {
	__le64 timestamp;
	__le16 beacon_interval;
	__le16 atim_window;
	__le32 beacon_init_val;
	__le16 listen_interval;
	u8 dtim_period;
	u8 delta_cp_bss_tbtts;
} __packed;

/*
 * REPLY_CHANNEL_SWITCH = 0x72 (command, has simple generic response)
 */
/**
 * struct iwl5000_channel_switch_cmd
 * @band: 0- 5.2GHz, 1- 2.4GHz
 * @expect_beacon: 0- resume transmits after channel switch
 *		   1- wait for beacon to resume transmits
 * @channel: new channel number
 * @rxon_flags: Rx on flags
 * @rxon_filter_flags: filtering parameters
 * @switch_time: switch time in extended beacon format
 * @reserved: reserved bytes
 */
struct iwl5000_channel_switch_cmd {
	u8 band;
	u8 expect_beacon;
	__le16 channel;
	__le32 rxon_flags;
	__le32 rxon_filter_flags;
	__le32 switch_time;
	__le32 reserved[2][IWL_PWR_NUM_HT_OFDM_ENTRIES + IWL_PWR_CCK_ENTRIES];
} __packed;

/**
 * struct iwl6000_channel_switch_cmd
 * @band: 0- 5.2GHz, 1- 2.4GHz
 * @expect_beacon: 0- resume transmits after channel switch
 *		   1- wait for beacon to resume transmits
 * @channel: new channel number
 * @rxon_flags: Rx on flags
 * @rxon_filter_flags: filtering parameters
 * @switch_time: switch time in extended beacon format
 * @reserved: reserved bytes
 */
struct iwl6000_channel_switch_cmd {
	u8 band;
	u8 expect_beacon;
	__le16 channel;
	__le32 rxon_flags;
	__le32 rxon_filter_flags;
	__le32 switch_time;
	__le32 reserved[3][IWL_PWR_NUM_HT_OFDM_ENTRIES + IWL_PWR_CCK_ENTRIES];
} __packed;

/*
 * CHANNEL_SWITCH_NOTIFICATION = 0x73 (notification only, not a command)
 */
struct iwl_csa_notification {
	__le16 band;
	__le16 channel;
	__le32 status;		/* 0 - OK, 1 - fail */
} __packed;

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
struct iwl_ac_qos {
	__le16 cw_min;
	__le16 cw_max;
	u8 aifsn;
	u8 reserved1;
	__le16 edca_txop;
} __packed;

/* QoS flags defines */
#define QOS_PARAM_FLG_UPDATE_EDCA_MSK	cpu_to_le32(0x01)
#define QOS_PARAM_FLG_TGN_MSK		cpu_to_le32(0x02)
#define QOS_PARAM_FLG_TXOP_TYPE_MSK	cpu_to_le32(0x10)

/* Number of Access Categories (AC) (EDCA), queues 0..3 */
#define AC_NUM                4

/*
 * REPLY_QOS_PARAM = 0x13 (command, has simple generic response)
 *
 * This command sets up timings for each of the 4 prioritized EDCA Tx FIFOs
 * 0: Background, 1: Best Effort, 2: Video, 3: Voice.
 */
struct iwl_qosparam_cmd {
	__le32 qos_flags;
	struct iwl_ac_qos ac[AC_NUM];
} __packed;

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
#define	IWL_AP_ID_PAN		1
#define	IWL_STA_ID		2
#define IWLAGN_PAN_BCAST_ID	14
#define IWLAGN_BROADCAST_ID	15
#define	IWLAGN_STATION_COUNT	16

#define IWL_TID_NON_QOS IWL_MAX_TID_COUNT

#define STA_FLG_TX_RATE_MSK		cpu_to_le32(1 << 2)
#define STA_FLG_PWR_SAVE_MSK		cpu_to_le32(1 << 8)
#define STA_FLG_PAN_STATION		cpu_to_le32(1 << 13)
#define STA_FLG_RTS_MIMO_PROT_MSK	cpu_to_le32(1 << 17)
#define STA_FLG_AGG_MPDU_8US_MSK	cpu_to_le32(1 << 18)
#define STA_FLG_MAX_AGG_SIZE_POS	(19)
#define STA_FLG_MAX_AGG_SIZE_MSK	cpu_to_le32(3 << 19)
#define STA_FLG_HT40_EN_MSK		cpu_to_le32(1 << 21)
#define STA_FLG_MIMO_DIS_MSK		cpu_to_le32(1 << 22)
#define STA_FLG_AGG_MPDU_DENSITY_POS	(23)
#define STA_FLG_AGG_MPDU_DENSITY_MSK	cpu_to_le32(7 << 23)

/* Use in mode field.  1: modify existing entry, 0: add new station entry */
#define STA_CONTROL_MODIFY_MSK		0x01

/* key flags __le16*/
#define STA_KEY_FLG_ENCRYPT_MSK	cpu_to_le16(0x0007)
#define STA_KEY_FLG_NO_ENC	cpu_to_le16(0x0000)
#define STA_KEY_FLG_WEP		cpu_to_le16(0x0001)
#define STA_KEY_FLG_CCMP	cpu_to_le16(0x0002)
#define STA_KEY_FLG_TKIP	cpu_to_le16(0x0003)

#define STA_KEY_FLG_KEYID_POS	8
#define STA_KEY_FLG_INVALID 	cpu_to_le16(0x0800)
/* wep key is either from global key (0) or from station info array (1) */
#define STA_KEY_FLG_MAP_KEY_MSK	cpu_to_le16(0x0008)

/* wep key in STA: 5-bytes (0) or 13-bytes (1) */
#define STA_KEY_FLG_KEY_SIZE_MSK     cpu_to_le16(0x1000)
#define STA_KEY_MULTICAST_MSK        cpu_to_le16(0x4000)
#define STA_KEY_MAX_NUM		8
#define STA_KEY_MAX_NUM_PAN	16
/* must not match WEP_INVALID_OFFSET */
#define IWLAGN_HW_KEY_DEFAULT	0xfe

/* Flags indicate whether to modify vs. don't change various station params */
#define	STA_MODIFY_KEY_MASK		0x01
#define	STA_MODIFY_TID_DISABLE_TX	0x02
#define	STA_MODIFY_TX_RATE_MSK		0x04
#define STA_MODIFY_ADDBA_TID_MSK	0x08
#define STA_MODIFY_DELBA_TID_MSK	0x10
#define STA_MODIFY_SLEEP_TX_COUNT_MSK	0x20

/* agn */
struct iwl_keyinfo {
	__le16 key_flags;
	u8 tkip_rx_tsc_byte2;	/* TSC[2] for key mix ph1 detection */
	u8 reserved1;
	__le16 tkip_rx_ttak[5];	/* 10-byte unicast TKIP TTAK */
	u8 key_offset;
	u8 reserved2;
	u8 key[16];		/* 16-byte unicast decryption key */
	__le64 tx_secur_seq_cnt;
	__le64 hw_tkip_mic_rx_key;
	__le64 hw_tkip_mic_tx_key;
} __packed;

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
} __packed;

/*
 * REPLY_ADD_STA = 0x18 (command)
 *
 * The device contains an internal table of per-station information,
 * with info on security keys, aggregation parameters, and Tx rates for
 * initial Tx attempt and any retries (agn devices uses
 * REPLY_TX_LINK_QUALITY_CMD,
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

struct iwl_addsta_cmd {
	u8 mode;		/* 1: modify existing, 0: add new station */
	u8 reserved[3];
	struct sta_id_modify sta;
	struct iwl_keyinfo key;
	__le32 station_flags;		/* STA_FLG_* */
	__le32 station_flags_msk;	/* STA_FLG_* */

	/* bit field to disable (1) or enable (0) Tx for Traffic ID (TID)
	 * corresponding to bit (e.g. bit 5 controls TID 5).
	 * Set modify_mask bit STA_MODIFY_TID_DISABLE_TX to use this field. */
	__le16 tid_disable_tx;
	__le16 legacy_reserved;

	/* TID for which to add block-ack support.
	 * Set modify_mask bit STA_MODIFY_ADDBA_TID_MSK to use this field. */
	u8 add_immediate_ba_tid;

	/* TID for which to remove block-ack support.
	 * Set modify_mask bit STA_MODIFY_DELBA_TID_MSK to use this field. */
	u8 remove_immediate_ba_tid;

	/* Starting Sequence Number for added block-ack support.
	 * Set modify_mask bit STA_MODIFY_ADDBA_TID_MSK to use this field. */
	__le16 add_immediate_ba_ssn;

	/*
	 * Number of packets OK to transmit to station even though
	 * it is asleep -- used to synchronise PS-poll and u-APSD
	 * responses while ucode keeps track of STA sleep state.
	 */
	__le16 sleep_tx_count;

	__le16 reserved2;
} __packed;


#define ADD_STA_SUCCESS_MSK		0x1
#define ADD_STA_NO_ROOM_IN_TABLE	0x2
#define ADD_STA_NO_BLOCK_ACK_RESOURCE	0x4
#define ADD_STA_MODIFY_NON_EXIST_STA	0x8
/*
 * REPLY_ADD_STA = 0x18 (response)
 */
struct iwl_add_sta_resp {
	u8 status;	/* ADD_STA_* */
} __packed;

#define REM_STA_SUCCESS_MSK              0x1
/*
 *  REPLY_REM_STA = 0x19 (response)
 */
struct iwl_rem_sta_resp {
	u8 status;
} __packed;

/*
 *  REPLY_REM_STA = 0x19 (command)
 */
struct iwl_rem_sta_cmd {
	u8 num_sta;     /* number of removed stations */
	u8 reserved[3];
	u8 addr[ETH_ALEN]; /* MAC addr of the first station */
	u8 reserved2[2];
} __packed;


/* WiFi queues mask */
#define IWL_SCD_BK_MSK			BIT(0)
#define IWL_SCD_BE_MSK			BIT(1)
#define IWL_SCD_VI_MSK			BIT(2)
#define IWL_SCD_VO_MSK			BIT(3)
#define IWL_SCD_MGMT_MSK		BIT(3)

/* PAN queues mask */
#define IWL_PAN_SCD_BK_MSK		BIT(4)
#define IWL_PAN_SCD_BE_MSK		BIT(5)
#define IWL_PAN_SCD_VI_MSK		BIT(6)
#define IWL_PAN_SCD_VO_MSK		BIT(7)
#define IWL_PAN_SCD_MGMT_MSK		BIT(7)
#define IWL_PAN_SCD_MULTICAST_MSK	BIT(8)

#define IWL_AGG_TX_QUEUE_MSK		0xffc00

#define IWL_DROP_ALL			BIT(1)

/*
 * REPLY_TXFIFO_FLUSH = 0x1e(command and response)
 *
 * When using full FIFO flush this command checks the scheduler HW block WR/RD
 * pointers to check if all the frames were transferred by DMA into the
 * relevant TX FIFO queue. Only when the DMA is finished and the queue is
 * empty the command can finish.
 * This command is used to flush the TXFIFO from transmit commands, it may
 * operate on single or multiple queues, the command queue can't be flushed by
 * this command. The command response is returned when all the queue flush
 * operations are done. Each TX command flushed return response with the FLUSH
 * status set in the TX response status. When FIFO flush operation is used,
 * the flush operation ends when both the scheduler DMA done and TXFIFO empty
 * are set.
 *
 * @queue_control: bit mask for which queues to flush
 * @flush_control: flush controls
 *	0: Dump single MSDU
 *	1: Dump multiple MSDU according to PS, INVALID STA, TTL, TID disable.
 *	2: Dump all FIFO
 */
struct iwl_txfifo_flush_cmd_v3 {
	__le32 queue_control;
	__le16 flush_control;
	__le16 reserved;
} __packed;

struct iwl_txfifo_flush_cmd_v2 {
	__le16 queue_control;
	__le16 flush_control;
} __packed;

/*
 * REPLY_WEP_KEY = 0x20
 */
struct iwl_wep_key {
	u8 key_index;
	u8 key_offset;
	u8 reserved1[2];
	u8 key_size;
	u8 reserved2[3];
	u8 key[16];
} __packed;

struct iwl_wep_cmd {
	u8 num_keys;
	u8 global_key_type;
	u8 flags;
	u8 reserved;
	struct iwl_wep_key key[0];
} __packed;

#define WEP_KEY_WEP_TYPE 1
#define WEP_KEYS_MAX 4
#define WEP_INVALID_OFFSET 0xff
#define WEP_KEY_LEN_64 5
#define WEP_KEY_LEN_128 13

/******************************************************************************
 * (4)
 * Rx Responses:
 *
 *****************************************************************************/

#define RX_RES_STATUS_NO_CRC32_ERROR	cpu_to_le32(1 << 0)
#define RX_RES_STATUS_NO_RXE_OVERFLOW	cpu_to_le32(1 << 1)

#define RX_RES_PHY_FLAGS_BAND_24_MSK	cpu_to_le16(1 << 0)
#define RX_RES_PHY_FLAGS_MOD_CCK_MSK		cpu_to_le16(1 << 1)
#define RX_RES_PHY_FLAGS_SHORT_PREAMBLE_MSK	cpu_to_le16(1 << 2)
#define RX_RES_PHY_FLAGS_NARROW_BAND_MSK	cpu_to_le16(1 << 3)
#define RX_RES_PHY_FLAGS_ANTENNA_MSK		0x70
#define RX_RES_PHY_FLAGS_ANTENNA_POS		4
#define RX_RES_PHY_FLAGS_AGG_MSK		cpu_to_le16(1 << 7)

#define RX_RES_STATUS_SEC_TYPE_MSK	(0x7 << 8)
#define RX_RES_STATUS_SEC_TYPE_NONE	(0x0 << 8)
#define RX_RES_STATUS_SEC_TYPE_WEP	(0x1 << 8)
#define RX_RES_STATUS_SEC_TYPE_CCMP	(0x2 << 8)
#define RX_RES_STATUS_SEC_TYPE_TKIP	(0x3 << 8)
#define	RX_RES_STATUS_SEC_TYPE_ERR	(0x7 << 8)

#define RX_RES_STATUS_STATION_FOUND	(1<<6)
#define RX_RES_STATUS_NO_STATION_INFO_MISMATCH	(1<<7)

#define RX_RES_STATUS_DECRYPT_TYPE_MSK	(0x3 << 11)
#define RX_RES_STATUS_NOT_DECRYPT	(0x0 << 11)
#define RX_RES_STATUS_DECRYPT_OK	(0x3 << 11)
#define RX_RES_STATUS_BAD_ICV_MIC	(0x1 << 11)
#define RX_RES_STATUS_BAD_KEY_TTAK	(0x2 << 11)

#define RX_MPDU_RES_STATUS_ICV_OK	(0x20)
#define RX_MPDU_RES_STATUS_MIC_OK	(0x40)
#define RX_MPDU_RES_STATUS_TTAK_OK	(1 << 7)
#define RX_MPDU_RES_STATUS_DEC_DONE_MSK	(0x800)


#define IWLAGN_RX_RES_PHY_CNT 8
#define IWLAGN_RX_RES_AGC_IDX     1
#define IWLAGN_RX_RES_RSSI_AB_IDX 2
#define IWLAGN_RX_RES_RSSI_C_IDX  3
#define IWLAGN_OFDM_AGC_MSK 0xfe00
#define IWLAGN_OFDM_AGC_BIT_POS 9
#define IWLAGN_OFDM_RSSI_INBAND_A_BITMSK 0x00ff
#define IWLAGN_OFDM_RSSI_ALLBAND_A_BITMSK 0xff00
#define IWLAGN_OFDM_RSSI_A_BIT_POS 0
#define IWLAGN_OFDM_RSSI_INBAND_B_BITMSK 0xff0000
#define IWLAGN_OFDM_RSSI_ALLBAND_B_BITMSK 0xff000000
#define IWLAGN_OFDM_RSSI_B_BIT_POS 16
#define IWLAGN_OFDM_RSSI_INBAND_C_BITMSK 0x00ff
#define IWLAGN_OFDM_RSSI_ALLBAND_C_BITMSK 0xff00
#define IWLAGN_OFDM_RSSI_C_BIT_POS 0

struct iwlagn_non_cfg_phy {
	__le32 non_cfg_phy[IWLAGN_RX_RES_PHY_CNT];  /* up to 8 phy entries */
} __packed;


/*
 * REPLY_RX = 0xc3 (response only, not a command)
 * Used only for legacy (non 11n) frames.
 */
struct iwl_rx_phy_res {
	u8 non_cfg_phy_cnt;     /* non configurable DSP phy data byte count */
	u8 cfg_phy_cnt;		/* configurable DSP phy data byte count */
	u8 stat_id;		/* configurable DSP phy data set ID */
	u8 reserved1;
	__le64 timestamp;	/* TSF at on air rise */
	__le32 beacon_time_stamp; /* beacon at on-air rise */
	__le16 phy_flags;	/* general phy flags: band, modulation, ... */
	__le16 channel;		/* channel number */
	u8 non_cfg_phy_buf[32]; /* for various implementations of non_cfg_phy */
	__le32 rate_n_flags;	/* RATE_MCS_* */
	__le16 byte_count;	/* frame's byte-count */
	__le16 frame_time;	/* frame's time on the air */
} __packed;

struct iwl_rx_mpdu_res_start {
	__le16 byte_count;
	__le16 reserved;
} __packed;


/******************************************************************************
 * (5)
 * Tx Commands & Responses:
 *
 * Driver must place each REPLY_TX command into one of the prioritized Tx
 * queues in host DRAM, shared between driver and device (see comments for
 * SCD registers and Tx/Rx Queues).  When the device's Tx scheduler and uCode
 * are preparing to transmit, the device pulls the Tx command over the PCI
 * bus via one of the device's Tx DMA channels, to fill an internal FIFO
 * from which data will be transmitted.
 *
 * uCode handles all timing and protocol related to control frames
 * (RTS/CTS/ACK), based on flags in the Tx command.  uCode and Tx scheduler
 * handle reception of block-acks; uCode updates the host driver via
 * REPLY_COMPRESSED_BA.
 *
 * uCode handles retrying Tx when an ACK is expected but not received.
 * This includes trying lower data rates than the one requested in the Tx
 * command, as set up by the REPLY_TX_LINK_QUALITY_CMD (agn).
 *
 * Driver sets up transmit power for various rates via REPLY_TX_PWR_TABLE_CMD.
 * This command must be executed after every RXON command, before Tx can occur.
 *****************************************************************************/

/* REPLY_TX Tx flags field */

/*
 * 1: Use RTS/CTS protocol or CTS-to-self if spec allows it
 * before this frame. if CTS-to-self required check
 * RXON_FLG_SELF_CTS_EN status.
 */
#define TX_CMD_FLG_PROT_REQUIRE_MSK cpu_to_le32(1 << 0)

/* 1: Expect ACK from receiving station
 * 0: Don't expect ACK (MAC header's duration field s/b 0)
 * Set this for unicast frames, but not broadcast/multicast. */
#define TX_CMD_FLG_ACK_MSK cpu_to_le32(1 << 3)

/* For agn devices:
 * 1: Use rate scale table (see REPLY_TX_LINK_QUALITY_CMD).
 *    Tx command's initial_rate_index indicates first rate to try;
 *    uCode walks through table for additional Tx attempts.
 * 0: Use Tx rate/MCS from Tx command's rate_n_flags field.
 *    This rate will be used for all Tx attempts; it will not be scaled. */
#define TX_CMD_FLG_STA_RATE_MSK cpu_to_le32(1 << 4)

/* 1: Expect immediate block-ack.
 * Set when Txing a block-ack request frame.  Also set TX_CMD_FLG_ACK_MSK. */
#define TX_CMD_FLG_IMM_BA_RSP_MASK  cpu_to_le32(1 << 6)

/* Tx antenna selection field; reserved (0) for agn devices. */
#define TX_CMD_FLG_ANT_SEL_MSK cpu_to_le32(0xf00)

/* 1: Ignore Bluetooth priority for this frame.
 * 0: Delay Tx until Bluetooth device is done (normal usage). */
#define TX_CMD_FLG_IGNORE_BT cpu_to_le32(1 << 12)

/* 1: uCode overrides sequence control field in MAC header.
 * 0: Driver provides sequence control field in MAC header.
 * Set this for management frames, non-QOS data frames, non-unicast frames,
 * and also in Tx command embedded in REPLY_SCAN_CMD for active scans. */
#define TX_CMD_FLG_SEQ_CTL_MSK cpu_to_le32(1 << 13)

/* 1: This frame is non-last MPDU; more fragments are coming.
 * 0: Last fragment, or not using fragmentation. */
#define TX_CMD_FLG_MORE_FRAG_MSK cpu_to_le32(1 << 14)

/* 1: uCode calculates and inserts Timestamp Function (TSF) in outgoing frame.
 * 0: No TSF required in outgoing frame.
 * Set this for transmitting beacons and probe responses. */
#define TX_CMD_FLG_TSF_MSK cpu_to_le32(1 << 16)

/* 1: Driver inserted 2 bytes pad after the MAC header, for (required) dword
 *    alignment of frame's payload data field.
 * 0: No pad
 * Set this for MAC headers with 26 or 30 bytes, i.e. those with QOS or ADDR4
 * field (but not both).  Driver must align frame data (i.e. data following
 * MAC header) to DWORD boundary. */
#define TX_CMD_FLG_MH_PAD_MSK cpu_to_le32(1 << 20)

/* accelerate aggregation support
 * 0 - no CCMP encryption; 1 - CCMP encryption */
#define TX_CMD_FLG_AGG_CCMP_MSK cpu_to_le32(1 << 22)

/* HCCA-AP - disable duration overwriting. */
#define TX_CMD_FLG_DUR_MSK cpu_to_le32(1 << 25)


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

/*
 * 4965 uCode updates these Tx attempt count values in host DRAM.
 * Used for managing Tx retries when expecting block-acks.
 * Driver should set these fields to 0.
 */
struct iwl_dram_scratch {
	u8 try_cnt;		/* Tx attempts */
	u8 bt_kill_cnt;		/* Tx attempts blocked by Bluetooth device */
	__le16 reserved;
} __packed;

struct iwl_tx_cmd {
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

	/* uCode may modify this field of the Tx command (in host DRAM!).
	 * Driver must also set dram_lsb_ptr and dram_msb_ptr in this cmd. */
	struct iwl_dram_scratch scratch;

	/* Rate for *all* Tx attempts, if TX_CMD_FLG_STA_RATE_MSK is cleared. */
	__le32 rate_n_flags;	/* RATE_MCS_* */

	/* Index of destination station in uCode's station table */
	u8 sta_id;

	/* Type of security encryption:  CCM or TKIP */
	u8 sec_ctl;		/* TX_CMD_SEC_* */

	/*
	 * Index into rate table (see REPLY_TX_LINK_QUALITY_CMD) for initial
	 * Tx attempt, if TX_CMD_FLG_STA_RATE_MSK is set.  Normally "0" for
	 * data frames, this field may be used to selectively reduce initial
	 * rate (via non-0 value) for special frames (e.g. management), while
	 * still supporting rate scaling for all frames.
	 */
	u8 initial_rate_index;
	u8 reserved;
	u8 key[16];
	__le16 next_frame_flags;
	__le16 reserved2;
	union {
		__le32 life_time;
		__le32 attempt;
	} stop_time;

	/* Host DRAM physical address pointer to "scratch" in this command.
	 * Must be dword aligned.  "0" in dram_lsb_ptr disables usage. */
	__le32 dram_lsb_ptr;
	u8 dram_msb_ptr;

	u8 rts_retry_limit;	/*byte 50 */
	u8 data_retry_limit;	/*byte 51 */
	u8 tid_tspec;
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
} __packed;

/*
 * TX command response is sent after *agn* transmission attempts.
 *
 * both postpone and abort status are expected behavior from uCode. there is
 * no special operation required from driver; except for RFKILL_FLUSH,
 * which required tx flush host command to flush all the tx frames in queues
 */
enum {
	TX_STATUS_SUCCESS = 0x01,
	TX_STATUS_DIRECT_DONE = 0x02,
	/* postpone TX */
	TX_STATUS_POSTPONE_DELAY = 0x40,
	TX_STATUS_POSTPONE_FEW_BYTES = 0x41,
	TX_STATUS_POSTPONE_BT_PRIO = 0x42,
	TX_STATUS_POSTPONE_QUIET_PERIOD = 0x43,
	TX_STATUS_POSTPONE_CALC_TTAK = 0x44,
	/* abort TX */
	TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY = 0x81,
	TX_STATUS_FAIL_SHORT_LIMIT = 0x82,
	TX_STATUS_FAIL_LONG_LIMIT = 0x83,
	TX_STATUS_FAIL_FIFO_UNDERRUN = 0x84,
	TX_STATUS_FAIL_DRAIN_FLOW = 0x85,
	TX_STATUS_FAIL_RFKILL_FLUSH = 0x86,
	TX_STATUS_FAIL_LIFE_EXPIRE = 0x87,
	TX_STATUS_FAIL_DEST_PS = 0x88,
	TX_STATUS_FAIL_HOST_ABORTED = 0x89,
	TX_STATUS_FAIL_BT_RETRY = 0x8a,
	TX_STATUS_FAIL_STA_INVALID = 0x8b,
	TX_STATUS_FAIL_FRAG_DROPPED = 0x8c,
	TX_STATUS_FAIL_TID_DISABLE = 0x8d,
	TX_STATUS_FAIL_FIFO_FLUSHED = 0x8e,
	TX_STATUS_FAIL_INSUFFICIENT_CF_POLL = 0x8f,
	TX_STATUS_FAIL_PASSIVE_NO_RX = 0x90,
	TX_STATUS_FAIL_NO_BEACON_ON_RADAR = 0x91,
};

#define	TX_PACKET_MODE_REGULAR		0x0000
#define	TX_PACKET_MODE_BURST_SEQ	0x0100
#define	TX_PACKET_MODE_BURST_FIRST	0x0200

enum {
	TX_POWER_PA_NOT_ACTIVE = 0x0,
};

enum {
	TX_STATUS_MSK = 0x000000ff,		/* bits 0:7 */
	TX_STATUS_DELAY_MSK = 0x00000040,
	TX_STATUS_ABORT_MSK = 0x00000080,
	TX_PACKET_MODE_MSK = 0x0000ff00,	/* bits 8:15 */
	TX_FIFO_NUMBER_MSK = 0x00070000,	/* bits 16:18 */
	TX_RESERVED = 0x00780000,		/* bits 19:22 */
	TX_POWER_PA_DETECT_MSK = 0x7f800000,	/* bits 23:30 */
	TX_ABORT_REQUIRED_MSK = 0x80000000,	/* bits 31:31 */
};

/* *******************************
 * TX aggregation status
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

#define AGG_TX_STATUS_MSK	0x00000fff	/* bits 0:11 */
#define AGG_TX_TRY_MSK		0x0000f000	/* bits 12:15 */
#define AGG_TX_TRY_POS		12

#define AGG_TX_STATE_LAST_SENT_MSK  (AGG_TX_STATE_LAST_SENT_TTL_MSK | \
				     AGG_TX_STATE_LAST_SENT_TRY_CNT_MSK | \
				     AGG_TX_STATE_LAST_SENT_BT_KILL_MSK)

/* # tx attempts for first frame in aggregation */
#define AGG_TX_STATE_TRY_CNT_POS 12
#define AGG_TX_STATE_TRY_CNT_MSK 0xf000

/* Command ID and sequence number of Tx command for this frame */
#define AGG_TX_STATE_SEQ_NUM_POS 16
#define AGG_TX_STATE_SEQ_NUM_MSK 0xffff0000

/*
 * REPLY_TX = 0x1c (response)
 *
 * This response may be in one of two slightly different formats, indicated
 * by the frame_count field:
 *
 * 1)  No aggregation (frame_count == 1).  This reports Tx results for
 *     a single frame.  Multiple attempts, at various bit rates, may have
 *     been made for this frame.
 *
 * 2)  Aggregation (frame_count > 1).  This reports Tx results for
 *     2 or more frames that used block-acknowledge.  All frames were
 *     transmitted at same rate.  Rate scaling may have been used if first
 *     frame in this new agg block failed in previous agg block(s).
 *
 *     Note that, for aggregation, ACK (block-ack) status is not delivered here;
 *     block-ack has not been received by the time the agn device records
 *     this status.
 *     This status relates to reasons the tx might have been blocked or aborted
 *     within the sending station (this agn device), rather than whether it was
 *     received successfully by the destination station.
 */
struct agg_tx_status {
	__le16 status;
	__le16 sequence;
} __packed;

/*
 * definitions for initial rate index field
 * bits [3:0] initial rate index
 * bits [6:4] rate table color, used for the initial rate
 * bit-7 invalid rate indication
 *   i.e. rate was not chosen from rate table
 *   or rate table color was changed during frame retries
 * refer tlc rate info
 */

#define IWL50_TX_RES_INIT_RATE_INDEX_POS	0
#define IWL50_TX_RES_INIT_RATE_INDEX_MSK	0x0f
#define IWL50_TX_RES_RATE_TABLE_COLOR_POS	4
#define IWL50_TX_RES_RATE_TABLE_COLOR_MSK	0x70
#define IWL50_TX_RES_INV_RATE_INDEX_MSK	0x80

/* refer to ra_tid */
#define IWLAGN_TX_RES_TID_POS	0
#define IWLAGN_TX_RES_TID_MSK	0x0f
#define IWLAGN_TX_RES_RA_POS	4
#define IWLAGN_TX_RES_RA_MSK	0xf0

struct iwlagn_tx_resp {
	u8 frame_count;		/* 1 no aggregation, >1 aggregation */
	u8 bt_kill_count;	/* # blocked by bluetooth (unused for agg) */
	u8 failure_rts;		/* # failures due to unsuccessful RTS */
	u8 failure_frame;	/* # failures due to no ACK (unused for agg) */

	/* For non-agg:  Rate at which frame was successful.
	 * For agg:  Rate at which all frames were transmitted. */
	__le32 rate_n_flags;	/* RATE_MCS_*  */

	/* For non-agg:  RTS + CTS + frame tx attempts time + ACK.
	 * For agg:  RTS + CTS + aggregation tx time + block-ack time. */
	__le16 wireless_media_time;	/* uSecs */

	u8 pa_status;		/* RF power amplifier measurement (not used) */
	u8 pa_integ_res_a[3];
	u8 pa_integ_res_b[3];
	u8 pa_integ_res_C[3];

	__le32 tfd_info;
	__le16 seq_ctl;
	__le16 byte_cnt;
	u8 tlc_info;
	u8 ra_tid;		/* tid (0:3), sta_id (4:7) */
	__le16 frame_ctrl;
	/*
	 * For non-agg:  frame status TX_STATUS_*
	 * For agg:  status of 1st frame, AGG_TX_STATE_*; other frame status
	 *           fields follow this one, up to frame_count.
	 *           Bit fields:
	 *           11- 0:  AGG_TX_STATE_* status code
	 *           15-12:  Retry count for 1st frame in aggregation (retries
	 *                   occur if tx failed for this frame when it was a
	 *                   member of a previous aggregation block).  If rate
	 *                   scaling is used, retry count indicates the rate
	 *                   table entry used for all frames in the new agg.
	 *           31-16:  Sequence # for this frame's Tx cmd (not SSN!)
	 */
	struct agg_tx_status status;	/* TX status (in aggregation -
					 * status of 1st frame) */
} __packed;
/*
 * REPLY_COMPRESSED_BA = 0xc5 (response only, not a command)
 *
 * Reports Block-Acknowledge from recipient station
 */
struct iwl_compressed_ba_resp {
	__le32 sta_addr_lo32;
	__le16 sta_addr_hi16;
	__le16 reserved;

	/* Index of recipient (BA-sending) station in uCode's station table */
	u8 sta_id;
	u8 tid;
	__le16 seq_ctl;
	__le64 bitmap;
	__le16 scd_flow;
	__le16 scd_ssn;
	u8 txed;	/* number of frames sent */
	u8 txed_2_done; /* number of frames acked */
	__le16 reserved1;
} __packed;

/*
 * REPLY_TX_PWR_TABLE_CMD = 0x97 (command, has simple generic response)
 *
 */

/*RS_NEW_API: only TLC_RTS remains and moved to bit 0 */
#define  LINK_QUAL_FLAGS_SET_STA_TLC_RTS_MSK	(1 << 0)

/* # of EDCA prioritized tx fifos */
#define  LINK_QUAL_AC_NUM AC_NUM

/* # entries in rate scale table to support Tx retries */
#define  LINK_QUAL_MAX_RETRY_NUM 16

/* Tx antenna selection values */
#define  LINK_QUAL_ANT_A_MSK (1 << 0)
#define  LINK_QUAL_ANT_B_MSK (1 << 1)
#define  LINK_QUAL_ANT_MSK   (LINK_QUAL_ANT_A_MSK|LINK_QUAL_ANT_B_MSK)


/**
 * struct iwl_link_qual_general_params
 *
 * Used in REPLY_TX_LINK_QUALITY_CMD
 */
struct iwl_link_qual_general_params {
	u8 flags;

	/* No entries at or above this (driver chosen) index contain MIMO */
	u8 mimo_delimiter;

	/* Best single antenna to use for single stream (legacy, SISO). */
	u8 single_stream_ant_msk;	/* LINK_QUAL_ANT_* */

	/* Best antennas to use for MIMO (unused for 4965, assumes both). */
	u8 dual_stream_ant_msk;		/* LINK_QUAL_ANT_* */

	/*
	 * If driver needs to use different initial rates for different
	 * EDCA QOS access categories (as implemented by tx fifos 0-3),
	 * this table will set that up, by indicating the indexes in the
	 * rs_table[LINK_QUAL_MAX_RETRY_NUM] rate table at which to start.
	 * Otherwise, driver should set all entries to 0.
	 *
	 * Entry usage:
	 * 0 = Background, 1 = Best Effort (normal), 2 = Video, 3 = Voice
	 * TX FIFOs above 3 use same value (typically 0) as TX FIFO 3.
	 */
	u8 start_rate_index[LINK_QUAL_AC_NUM];
} __packed;

#define LINK_QUAL_AGG_TIME_LIMIT_DEF	(4000) /* 4 milliseconds */
#define LINK_QUAL_AGG_TIME_LIMIT_MAX	(8000)
#define LINK_QUAL_AGG_TIME_LIMIT_MIN	(100)

#define LINK_QUAL_AGG_DISABLE_START_DEF	(3)
#define LINK_QUAL_AGG_DISABLE_START_MAX	(255)
#define LINK_QUAL_AGG_DISABLE_START_MIN	(0)

#define LINK_QUAL_AGG_FRAME_LIMIT_DEF	(63)
#define LINK_QUAL_AGG_FRAME_LIMIT_MAX	(63)
#define LINK_QUAL_AGG_FRAME_LIMIT_MIN	(0)

/**
 * struct iwl_link_qual_agg_params
 *
 * Used in REPLY_TX_LINK_QUALITY_CMD
 */
struct iwl_link_qual_agg_params {

	/*
	 *Maximum number of uSec in aggregation.
	 * default set to 4000 (4 milliseconds) if not configured in .cfg
	 */
	__le16 agg_time_limit;

	/*
	 * Number of Tx retries allowed for a frame, before that frame will
	 * no longer be considered for the start of an aggregation sequence
	 * (scheduler will then try to tx it as single frame).
	 * Driver should set this to 3.
	 */
	u8 agg_dis_start_th;

	/*
	 * Maximum number of frames in aggregation.
	 * 0 = no limit (default).  1 = no aggregation.
	 * Other values = max # frames in aggregation.
	 */
	u8 agg_frame_cnt_limit;

	__le32 reserved;
} __packed;

/*
 * REPLY_TX_LINK_QUALITY_CMD = 0x4e (command, has simple generic response)
 *
 * For agn devices
 *
 * Each station in the agn device's internal station table has its own table
 * of 16
 * Tx rates and modulation modes (e.g. legacy/SISO/MIMO) for retrying Tx when
 * an ACK is not received.  This command replaces the entire table for
 * one station.
 *
 * NOTE:  Station must already be in agn device's station table.
 *	  Use REPLY_ADD_STA.
 *
 * The rate scaling procedures described below work well.  Of course, other
 * procedures are possible, and may work better for particular environments.
 *
 *
 * FILLING THE RATE TABLE
 *
 * Given a particular initial rate and mode, as determined by the rate
 * scaling algorithm described below, the Linux driver uses the following
 * formula to fill the rs_table[LINK_QUAL_MAX_RETRY_NUM] rate table in the
 * Link Quality command:
 *
 *
 * 1)  If using High-throughput (HT) (SISO or MIMO) initial rate:
 *     a) Use this same initial rate for first 3 entries.
 *     b) Find next lower available rate using same mode (SISO or MIMO),
 *        use for next 3 entries.  If no lower rate available, switch to
 *        legacy mode (no HT40 channel, no MIMO, no short guard interval).
 *     c) If using MIMO, set command's mimo_delimiter to number of entries
 *        using MIMO (3 or 6).
 *     d) After trying 2 HT rates, switch to legacy mode (no HT40 channel,
 *        no MIMO, no short guard interval), at the next lower bit rate
 *        (e.g. if second HT bit rate was 54, try 48 legacy), and follow
 *        legacy procedure for remaining table entries.
 *
 * 2)  If using legacy initial rate:
 *     a) Use the initial rate for only one entry.
 *     b) For each following entry, reduce the rate to next lower available
 *        rate, until reaching the lowest available rate.
 *     c) When reducing rate, also switch antenna selection.
 *     d) Once lowest available rate is reached, repeat this rate until
 *        rate table is filled (16 entries), switching antenna each entry.
 *
 *
 * ACCUMULATING HISTORY
 *
 * The rate scaling algorithm for agn devices, as implemented in Linux driver,
 * uses two sets of frame Tx success history:  One for the current/active
 * modulation mode, and one for a speculative/search mode that is being
 * attempted. If the speculative mode turns out to be more effective (i.e.
 * actual transfer rate is better), then the driver continues to use the
 * speculative mode as the new current active mode.
 *
 * Each history set contains, separately for each possible rate, data for a
 * sliding window of the 62 most recent tx attempts at that rate.  The data
 * includes a shifting bitmap of success(1)/failure(0), and sums of successful
 * and attempted frames, from which the driver can additionally calculate a
 * success ratio (success / attempted) and number of failures
 * (attempted - success), and control the size of the window (attempted).
 * The driver uses the bit map to remove successes from the success sum, as
 * the oldest tx attempts fall out of the window.
 *
 * When the agn device makes multiple tx attempts for a given frame, each
 * attempt might be at a different rate, and have different modulation
 * characteristics (e.g. antenna, fat channel, short guard interval), as set
 * up in the rate scaling table in the Link Quality command.  The driver must
 * determine which rate table entry was used for each tx attempt, to determine
 * which rate-specific history to update, and record only those attempts that
 * match the modulation characteristics of the history set.
 *
 * When using block-ack (aggregation), all frames are transmitted at the same
 * rate, since there is no per-attempt acknowledgment from the destination
 * station.  The Tx response struct iwl_tx_resp indicates the Tx rate in
 * rate_n_flags field.  After receiving a block-ack, the driver can update
 * history for the entire block all at once.
 *
 *
 * FINDING BEST STARTING RATE:
 *
 * When working with a selected initial modulation mode (see below), the
 * driver attempts to find a best initial rate.  The initial rate is the
 * first entry in the Link Quality command's rate table.
 *
 * 1)  Calculate actual throughput (success ratio * expected throughput, see
 *     table below) for current initial rate.  Do this only if enough frames
 *     have been attempted to make the value meaningful:  at least 6 failed
 *     tx attempts, or at least 8 successes.  If not enough, don't try rate
 *     scaling yet.
 *
 * 2)  Find available rates adjacent to current initial rate.  Available means:
 *     a)  supported by hardware &&
 *     b)  supported by association &&
 *     c)  within any constraints selected by user
 *
 * 3)  Gather measured throughputs for adjacent rates.  These might not have
 *     enough history to calculate a throughput.  That's okay, we might try
 *     using one of them anyway!
 *
 * 4)  Try decreasing rate if, for current rate:
 *     a)  success ratio is < 15% ||
 *     b)  lower adjacent rate has better measured throughput ||
 *     c)  higher adjacent rate has worse throughput, and lower is unmeasured
 *
 *     As a sanity check, if decrease was determined above, leave rate
 *     unchanged if:
 *     a)  lower rate unavailable
 *     b)  success ratio at current rate > 85% (very good)
 *     c)  current measured throughput is better than expected throughput
 *         of lower rate (under perfect 100% tx conditions, see table below)
 *
 * 5)  Try increasing rate if, for current rate:
 *     a)  success ratio is < 15% ||
 *     b)  both adjacent rates' throughputs are unmeasured (try it!) ||
 *     b)  higher adjacent rate has better measured throughput ||
 *     c)  lower adjacent rate has worse throughput, and higher is unmeasured
 *
 *     As a sanity check, if increase was determined above, leave rate
 *     unchanged if:
 *     a)  success ratio at current rate < 70%.  This is not particularly
 *         good performance; higher rate is sure to have poorer success.
 *
 * 6)  Re-evaluate the rate after each tx frame.  If working with block-
 *     acknowledge, history and statistics may be calculated for the entire
 *     block (including prior history that fits within the history windows),
 *     before re-evaluation.
 *
 * FINDING BEST STARTING MODULATION MODE:
 *
 * After working with a modulation mode for a "while" (and doing rate scaling),
 * the driver searches for a new initial mode in an attempt to improve
 * throughput.  The "while" is measured by numbers of attempted frames:
 *
 * For legacy mode, search for new mode after:
 *   480 successful frames, or 160 failed frames
 * For high-throughput modes (SISO or MIMO), search for new mode after:
 *   4500 successful frames, or 400 failed frames
 *
 * Mode switch possibilities are (3 for each mode):
 *
 * For legacy:
 *   Change antenna, try SISO (if HT association), try MIMO (if HT association)
 * For SISO:
 *   Change antenna, try MIMO, try shortened guard interval (SGI)
 * For MIMO:
 *   Try SISO antenna A, SISO antenna B, try shortened guard interval (SGI)
 *
 * When trying a new mode, use the same bit rate as the old/current mode when
 * trying antenna switches and shortened guard interval.  When switching to
 * SISO from MIMO or legacy, or to MIMO from SISO or legacy, use a rate
 * for which the expected throughput (under perfect conditions) is about the
 * same or slightly better than the actual measured throughput delivered by
 * the old/current mode.
 *
 * Actual throughput can be estimated by multiplying the expected throughput
 * by the success ratio (successful / attempted tx frames).  Frame size is
 * not considered in this calculation; it assumes that frame size will average
 * out to be fairly consistent over several samples.  The following are
 * metric values for expected throughput assuming 100% success ratio.
 * Only G band has support for CCK rates:
 *
 *           RATE:  1    2    5   11    6   9   12   18   24   36   48   54   60
 *
 *              G:  7   13   35   58   40  57   72   98  121  154  177  186  186
 *              A:  0    0    0    0   40  57   72   98  121  154  177  186  186
 *     SISO 20MHz:  0    0    0    0   42  42   76  102  124  159  183  193  202
 * SGI SISO 20MHz:  0    0    0    0   46  46   82  110  132  168  192  202  211
 *     MIMO 20MHz:  0    0    0    0   74  74  123  155  179  214  236  244  251
 * SGI MIMO 20MHz:  0    0    0    0   81  81  131  164  188  222  243  251  257
 *     SISO 40MHz:  0    0    0    0   77  77  127  160  184  220  242  250  257
 * SGI SISO 40MHz:  0    0    0    0   83  83  135  169  193  229  250  257  264
 *     MIMO 40MHz:  0    0    0    0  123 123  182  214  235  264  279  285  289
 * SGI MIMO 40MHz:  0    0    0    0  131 131  191  222  242  270  284  289  293
 *
 * After the new mode has been tried for a short while (minimum of 6 failed
 * frames or 8 successful frames), compare success ratio and actual throughput
 * estimate of the new mode with the old.  If either is better with the new
 * mode, continue to use the new mode.
 *
 * Continue comparing modes until all 3 possibilities have been tried.
 * If moving from legacy to HT, try all 3 possibilities from the new HT
 * mode.  After trying all 3, a best mode is found.  Continue to use this mode
 * for the longer "while" described above (e.g. 480 successful frames for
 * legacy), and then repeat the search process.
 *
 */
struct iwl_link_quality_cmd {

	/* Index of destination/recipient station in uCode's station table */
	u8 sta_id;
	u8 reserved1;
	__le16 control;		/* not used */
	struct iwl_link_qual_general_params general_params;
	struct iwl_link_qual_agg_params agg_params;

	/*
	 * Rate info; when using rate-scaling, Tx command's initial_rate_index
	 * specifies 1st Tx rate attempted, via index into this table.
	 * agn devices works its way through table when retrying Tx.
	 */
	struct {
		__le32 rate_n_flags;	/* RATE_MCS_*, IWL_RATE_* */
	} rs_table[LINK_QUAL_MAX_RETRY_NUM];
	__le32 reserved2;
} __packed;

/*
 * BT configuration enable flags:
 *   bit 0 - 1: BT channel announcement enabled
 *           0: disable
 *   bit 1 - 1: priority of BT device enabled
 *           0: disable
 *   bit 2 - 1: BT 2 wire support enabled
 *           0: disable
 */
#define BT_COEX_DISABLE (0x0)
#define BT_ENABLE_CHANNEL_ANNOUNCE BIT(0)
#define BT_ENABLE_PRIORITY	   BIT(1)
#define BT_ENABLE_2_WIRE	   BIT(2)

#define BT_COEX_DISABLE (0x0)
#define BT_COEX_ENABLE  (BT_ENABLE_CHANNEL_ANNOUNCE | BT_ENABLE_PRIORITY)

#define BT_LEAD_TIME_MIN (0x0)
#define BT_LEAD_TIME_DEF (0x1E)
#define BT_LEAD_TIME_MAX (0xFF)

#define BT_MAX_KILL_MIN (0x1)
#define BT_MAX_KILL_DEF (0x5)
#define BT_MAX_KILL_MAX (0xFF)

#define BT_DURATION_LIMIT_DEF	625
#define BT_DURATION_LIMIT_MAX	1250
#define BT_DURATION_LIMIT_MIN	625

#define BT_ON_THRESHOLD_DEF	4
#define BT_ON_THRESHOLD_MAX	1000
#define BT_ON_THRESHOLD_MIN	1

#define BT_FRAG_THRESHOLD_DEF	0
#define BT_FRAG_THRESHOLD_MAX	0
#define BT_FRAG_THRESHOLD_MIN	0

#define BT_AGG_THRESHOLD_DEF	1200
#define BT_AGG_THRESHOLD_MAX	8000
#define BT_AGG_THRESHOLD_MIN	400

/*
 * REPLY_BT_CONFIG = 0x9b (command, has simple generic response)
 *
 * agn devices support hardware handshake with Bluetooth device on
 * same platform.  Bluetooth device alerts wireless device when it will Tx;
 * wireless device can delay or kill its own Tx to accommodate.
 */
struct iwl_bt_cmd {
	u8 flags;
	u8 lead_time;
	u8 max_kill;
	u8 reserved;
	__le32 kill_ack_mask;
	__le32 kill_cts_mask;
} __packed;

#define IWLAGN_BT_FLAG_CHANNEL_INHIBITION	BIT(0)

#define IWLAGN_BT_FLAG_COEX_MODE_MASK		(BIT(3)|BIT(4)|BIT(5))
#define IWLAGN_BT_FLAG_COEX_MODE_SHIFT		3
#define IWLAGN_BT_FLAG_COEX_MODE_DISABLED	0
#define IWLAGN_BT_FLAG_COEX_MODE_LEGACY_2W	1
#define IWLAGN_BT_FLAG_COEX_MODE_3W		2
#define IWLAGN_BT_FLAG_COEX_MODE_4W		3

#define IWLAGN_BT_FLAG_UCODE_DEFAULT		BIT(6)
/* Disable Sync PSPoll on SCO/eSCO */
#define IWLAGN_BT_FLAG_SYNC_2_BT_DISABLE	BIT(7)

#define IWLAGN_BT_PSP_MIN_RSSI_THRESHOLD	-75 /* dBm */
#define IWLAGN_BT_PSP_MAX_RSSI_THRESHOLD	-65 /* dBm */

#define IWLAGN_BT_PRIO_BOOST_MAX	0xFF
#define IWLAGN_BT_PRIO_BOOST_MIN	0x00
#define IWLAGN_BT_PRIO_BOOST_DEFAULT	0xF0
#define IWLAGN_BT_PRIO_BOOST_DEFAULT32	0xF0F0F0F0

#define IWLAGN_BT_MAX_KILL_DEFAULT	5

#define IWLAGN_BT3_T7_DEFAULT		1

enum iwl_bt_kill_idx {
	IWL_BT_KILL_DEFAULT = 0,
	IWL_BT_KILL_OVERRIDE = 1,
	IWL_BT_KILL_REDUCE = 2,
};

#define IWLAGN_BT_KILL_ACK_MASK_DEFAULT	cpu_to_le32(0xffff0000)
#define IWLAGN_BT_KILL_CTS_MASK_DEFAULT	cpu_to_le32(0xffff0000)
#define IWLAGN_BT_KILL_ACK_CTS_MASK_SCO	cpu_to_le32(0xffffffff)
#define IWLAGN_BT_KILL_ACK_CTS_MASK_REDUCE	cpu_to_le32(0)

#define IWLAGN_BT3_PRIO_SAMPLE_DEFAULT	2

#define IWLAGN_BT3_T2_DEFAULT		0xc

#define IWLAGN_BT_VALID_ENABLE_FLAGS	cpu_to_le16(BIT(0))
#define IWLAGN_BT_VALID_BOOST		cpu_to_le16(BIT(1))
#define IWLAGN_BT_VALID_MAX_KILL	cpu_to_le16(BIT(2))
#define IWLAGN_BT_VALID_3W_TIMERS	cpu_to_le16(BIT(3))
#define IWLAGN_BT_VALID_KILL_ACK_MASK	cpu_to_le16(BIT(4))
#define IWLAGN_BT_VALID_KILL_CTS_MASK	cpu_to_le16(BIT(5))
#define IWLAGN_BT_VALID_REDUCED_TX_PWR	cpu_to_le16(BIT(6))
#define IWLAGN_BT_VALID_3W_LUT		cpu_to_le16(BIT(7))

#define IWLAGN_BT_ALL_VALID_MSK		(IWLAGN_BT_VALID_ENABLE_FLAGS | \
					IWLAGN_BT_VALID_BOOST | \
					IWLAGN_BT_VALID_MAX_KILL | \
					IWLAGN_BT_VALID_3W_TIMERS | \
					IWLAGN_BT_VALID_KILL_ACK_MASK | \
					IWLAGN_BT_VALID_KILL_CTS_MASK | \
					IWLAGN_BT_VALID_REDUCED_TX_PWR | \
					IWLAGN_BT_VALID_3W_LUT)

#define IWLAGN_BT_REDUCED_TX_PWR	BIT(0)

#define IWLAGN_BT_DECISION_LUT_SIZE	12

struct iwl_basic_bt_cmd {
	u8 flags;
	u8 ledtime; /* unused */
	u8 max_kill;
	u8 bt3_timer_t7_value;
	__le32 kill_ack_mask;
	__le32 kill_cts_mask;
	u8 bt3_prio_sample_time;
	u8 bt3_timer_t2_value;
	__le16 bt4_reaction_time; /* unused */
	__le32 bt3_lookup_table[IWLAGN_BT_DECISION_LUT_SIZE];
	/*
	 * bit 0: use reduced tx power for control frame
	 * bit 1 - 7: reserved
	 */
	u8 reduce_txpower;
	u8 reserved;
	__le16 valid;
};

struct iwl_bt_cmd_v1 {
	struct iwl_basic_bt_cmd basic;
	u8 prio_boost;
	/*
	 * set IWLAGN_BT_VALID_BOOST to "1" in "valid" bitmask
	 * if configure the following patterns
	 */
	u8 tx_prio_boost;	/* SW boost of WiFi tx priority */
	__le16 rx_prio_boost;	/* SW boost of WiFi rx priority */
};

struct iwl_bt_cmd_v2 {
	struct iwl_basic_bt_cmd basic;
	__le32 prio_boost;
	/*
	 * set IWLAGN_BT_VALID_BOOST to "1" in "valid" bitmask
	 * if configure the following patterns
	 */
	u8 reserved;
	u8 tx_prio_boost;	/* SW boost of WiFi tx priority */
	__le16 rx_prio_boost;	/* SW boost of WiFi rx priority */
};

#define IWLAGN_BT_SCO_ACTIVE	cpu_to_le32(BIT(0))

struct iwlagn_bt_sco_cmd {
	__le32 flags;
};

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
} __packed;

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
} __packed;

/*
 * REPLY_SPECTRUM_MEASUREMENT_CMD = 0x74 (response)
 */
struct iwl_spectrum_resp {
	u8 token;
	u8 id;			/* id of the prior command replaced, or 0xff */
	__le16 status;		/* 0 - command will be handled
				 * 1 - cannot handle (conflicts with another
				 *     measurement) */
} __packed;

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
} __packed;

/* clear channel availability counters */
struct iwl_measurement_cca_counters {
	__le32 ofdm;
	__le32 cck;
} __packed;

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
} __packed;

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
 *
 * uCode send sleep notifications:
 *   bit 1 - '0' Don't send sleep notification
 *           '1' send sleep notification (SEND_PM_NOTIFICATION)
 *
 * Sleep over DTIM
 *   bit 2 - '0' PM have to walk up every DTIM
 *           '1' PM could sleep over DTIM till listen Interval.
 *
 * PCI power managed
 *   bit 3 - '0' (PCI_CFG_LINK_CTRL & 0x1)
 *           '1' !(PCI_CFG_LINK_CTRL & 0x1)
 *
 * Fast PD
 *   bit 4 - '1' Put radio to sleep when receiving frame for others
 *
 * Force sleep Modes
 *   bit 31/30- '00' use both mac/xtal sleeps
 *              '01' force Mac sleep
 *              '10' force xtal sleep
 *              '11' Illegal set
 *
 * NOTE: if sleep_interval[SLEEP_INTRVL_TABLE_SIZE-1] > DTIM period then
 * ucode assume sleep over DTIM is allowed and we don't need to wake up
 * for every DTIM.
 */
#define IWL_POWER_VEC_SIZE 5

#define IWL_POWER_DRIVER_ALLOW_SLEEP_MSK	cpu_to_le16(BIT(0))
#define IWL_POWER_POWER_SAVE_ENA_MSK		cpu_to_le16(BIT(0))
#define IWL_POWER_POWER_MANAGEMENT_ENA_MSK	cpu_to_le16(BIT(1))
#define IWL_POWER_SLEEP_OVER_DTIM_MSK		cpu_to_le16(BIT(2))
#define IWL_POWER_PCI_PM_MSK			cpu_to_le16(BIT(3))
#define IWL_POWER_FAST_PD			cpu_to_le16(BIT(4))
#define IWL_POWER_BEACON_FILTERING		cpu_to_le16(BIT(5))
#define IWL_POWER_SHADOW_REG_ENA		cpu_to_le16(BIT(6))
#define IWL_POWER_CT_KILL_SET			cpu_to_le16(BIT(7))
#define IWL_POWER_BT_SCO_ENA			cpu_to_le16(BIT(8))
#define IWL_POWER_ADVANCE_PM_ENA_MSK		cpu_to_le16(BIT(9))

struct iwl_powertable_cmd {
	__le16 flags;
	u8 keep_alive_seconds;
	u8 debug_flags;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 sleep_interval[IWL_POWER_VEC_SIZE];
	__le32 keep_alive_beacons;
} __packed;

/*
 * PM_SLEEP_NOTIFICATION = 0x7A (notification only, not a command)
 * all devices identical.
 */
struct iwl_sleep_notification {
	u8 pm_sleep_mode;
	u8 pm_wakeup_src;
	__le16 reserved;
	__le32 sleep_time;
	__le32 tsf_low;
	__le32 bcon_timer;
} __packed;

/* Sleep states.  all devices identical. */
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
} __packed;

/*
 * CARD_STATE_NOTIFICATION = 0xa1 (notification only, not a command)
 */
struct iwl_card_state_notif {
	__le32 flags;
} __packed;

#define HW_CARD_DISABLED   0x01
#define SW_CARD_DISABLED   0x02
#define CT_CARD_DISABLED   0x04
#define RXON_CARD_DISABLED 0x10

struct iwl_ct_kill_config {
	__le32   reserved;
	__le32   critical_temperature_M;
	__le32   critical_temperature_R;
}  __packed;

/* 1000, and 6x00 */
struct iwl_ct_kill_throttling_config {
	__le32   critical_temperature_exit;
	__le32   reserved;
	__le32   critical_temperature_enter;
}  __packed;

/******************************************************************************
 * (8)
 * Scan Commands, Responses, Notifications:
 *
 *****************************************************************************/

#define SCAN_CHANNEL_TYPE_PASSIVE cpu_to_le32(0)
#define SCAN_CHANNEL_TYPE_ACTIVE  cpu_to_le32(1)

/**
 * struct iwl_scan_channel - entry in REPLY_SCAN_CMD channel table
 *
 * One for each channel in the scan list.
 * Each channel can independently select:
 * 1)  SSID for directed active scans
 * 2)  Txpower setting (for rate specified within Tx command)
 * 3)  How long to stay on-channel (behavior may be modified by quiet_time,
 *     quiet_plcp_th, good_CRC_th)
 *
 * To avoid uCode errors, make sure the following are true (see comments
 * under struct iwl_scan_cmd about max_out_time and quiet_time):
 * 1)  If using passive_dwell (i.e. passive_dwell != 0):
 *     active_dwell <= passive_dwell (< max_out_time if max_out_time != 0)
 * 2)  quiet_time <= active_dwell
 * 3)  If restricting off-channel time (i.e. max_out_time !=0):
 *     passive_dwell < max_out_time
 *     active_dwell < max_out_time
 */

struct iwl_scan_channel {
	/*
	 * type is defined as:
	 * 0:0 1 = active, 0 = passive
	 * 1:20 SSID direct bit map; if a bit is set, then corresponding
	 *     SSID IE is transmitted in probe request.
	 * 21:31 reserved
	 */
	__le32 type;
	__le16 channel;	/* band is selected by iwl_scan_cmd "flags" field */
	u8 tx_gain;		/* gain for analog radio */
	u8 dsp_atten;		/* gain for DSP */
	__le16 active_dwell;	/* in 1024-uSec TU (time units), typ 5-50 */
	__le16 passive_dwell;	/* in 1024-uSec TU (time units), typ 20-500 */
} __packed;

/* set number of direct probes __le32 type */
#define IWL_SCAN_PROBE_MASK(n) 	cpu_to_le32((BIT(n) | (BIT(n) - BIT(1))))

/**
 * struct iwl_ssid_ie - directed scan network information element
 *
 * Up to 20 of these may appear in REPLY_SCAN_CMD,
 * selected by "type" bit field in struct iwl_scan_channel;
 * each channel may select different ssids from among the 20 entries.
 * SSID IEs get transmitted in reverse order of entry.
 */
struct iwl_ssid_ie {
	u8 id;
	u8 len;
	u8 ssid[32];
} __packed;

#define PROBE_OPTION_MAX		20
#define TX_CMD_LIFE_TIME_INFINITE	cpu_to_le32(0xFFFFFFFF)
#define IWL_GOOD_CRC_TH_DISABLED	0
#define IWL_GOOD_CRC_TH_DEFAULT		cpu_to_le16(1)
#define IWL_GOOD_CRC_TH_NEVER		cpu_to_le16(0xffff)
#define IWL_MAX_CMD_SIZE 4096

/*
 * REPLY_SCAN_CMD = 0x80 (command)
 *
 * The hardware scan command is very powerful; the driver can set it up to
 * maintain (relatively) normal network traffic while doing a scan in the
 * background.  The max_out_time and suspend_time control the ratio of how
 * long the device stays on an associated network channel ("service channel")
 * vs. how long it's away from the service channel, i.e. tuned to other channels
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
 * struct iwl_scan_channel.
 */

enum iwl_scan_flags {
	/* BIT(0) currently unused */
	IWL_SCAN_FLAGS_ACTION_FRAME_TX	= BIT(1),
	/* bits 2-7 reserved */
};

struct iwl_scan_cmd {
	__le16 len;
	u8 scan_flags;		/* scan flags: see enum iwl_scan_flags */
	u8 channel_count;	/* # channels in channel list */
	__le16 quiet_time;	/* dwell only this # millisecs on quiet channel
				 * (only for active scan) */
	__le16 quiet_plcp_th;	/* quiet chnl is < this # pkts (typ. 1) */
	__le16 good_CRC_th;	/* passive -> active promotion threshold */
	__le16 rx_chain;	/* RXON_RX_CHAIN_* */
	__le32 max_out_time;	/* max usec to be away from associated (service)
				 * channel */
	__le32 suspend_time;	/* pause scan this long (in "extended beacon
				 * format") when returning to service chnl:
				 */
	__le32 flags;		/* RXON_FLG_* */
	__le32 filter_flags;	/* RXON_FILTER_* */

	/* For active scans (set to all-0s for passive scans).
	 * Does not include payload.  Must specify Tx rate; no rate scaling. */
	struct iwl_tx_cmd tx_cmd;

	/* For directed active scans (set to all-0s otherwise) */
	struct iwl_ssid_ie direct_scan[PROBE_OPTION_MAX];

	/*
	 * Probe request frame, followed by channel list.
	 *
	 * Size of probe request frame is specified by byte count in tx_cmd.
	 * Channel list follows immediately after probe request frame.
	 * Number of channels in list is specified by channel_count.
	 * Each channel in list is of type:
	 *
	 * struct iwl_scan_channel channels[0];
	 *
	 * NOTE:  Only one band of channels can be scanned per pass.  You
	 * must not mix 2.4GHz channels and 5.2GHz channels, and you must wait
	 * for one scan to complete (i.e. receive SCAN_COMPLETE_NOTIFICATION)
	 * before requesting another scan.
	 */
	u8 data[0];
} __packed;

/* Can abort will notify by complete notification with abort status. */
#define CAN_ABORT_STATUS	cpu_to_le32(0x1)
/* complete notification statuses */
#define ABORT_STATUS            0x2

/*
 * REPLY_SCAN_CMD = 0x80 (response)
 */
struct iwl_scanreq_notification {
	__le32 status;		/* 1: okay, 2: cannot fulfill request */
} __packed;

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
} __packed;

#define  SCAN_OWNER_STATUS 0x1
#define  MEASURE_OWNER_STATUS 0x2

#define IWL_PROBE_STATUS_OK		0
#define IWL_PROBE_STATUS_TX_FAILED	BIT(0)
/* error statuses combined with TX_FAILED */
#define IWL_PROBE_STATUS_FAIL_TTL	BIT(1)
#define IWL_PROBE_STATUS_FAIL_BT	BIT(2)

#define NUMBER_OF_STATISTICS 1	/* first __le32 is good CRC */
/*
 * SCAN_RESULTS_NOTIFICATION = 0x83 (notification only, not a command)
 */
struct iwl_scanresults_notification {
	u8 channel;
	u8 band;
	u8 probe_status;
	u8 num_probe_not_sent; /* not enough time to send */
	__le32 tsf_low;
	__le32 tsf_high;
	__le32 statistics[NUMBER_OF_STATISTICS];
} __packed;

/*
 * SCAN_COMPLETE_NOTIFICATION = 0x84 (notification only, not a command)
 */
struct iwl_scancomplete_notification {
	u8 scanned_channels;
	u8 status;
	u8 bt_status;	/* BT On/Off status */
	u8 last_channel;
	__le32 tsf_low;
	__le32 tsf_high;
} __packed;


/******************************************************************************
 * (9)
 * IBSS/AP Commands and Notifications:
 *
 *****************************************************************************/

enum iwl_ibss_manager {
	IWL_NOT_IBSS_MANAGER = 0,
	IWL_IBSS_MANAGER = 1,
};

/*
 * BEACON_NOTIFICATION = 0x90 (notification only, not a command)
 */

struct iwlagn_beacon_notif {
	struct iwlagn_tx_resp beacon_notify_hdr;
	__le32 low_tsf;
	__le32 high_tsf;
	__le32 ibss_mgr_status;
} __packed;

/*
 * REPLY_TX_BEACON = 0x91 (command, has simple generic response)
 */

struct iwl_tx_beacon_cmd {
	struct iwl_tx_cmd tx;
	__le16 tim_idx;
	u8 tim_size;
	u8 reserved1;
	struct ieee80211_hdr frame[0];	/* beacon frame */
} __packed;

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
} __packed;

/* statistics command response */

struct statistics_dbg {
	__le32 burst_check;
	__le32 burst_count;
	__le32 wait_for_silence_timeout_cnt;
	__le32 reserved[3];
} __packed;

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
	__le32 sent_ba_rsp_cnt;
	__le32 dsp_self_kill;
	__le32 mh_format_err;
	__le32 re_acq_main_rssi_sum;
	__le32 reserved3;
} __packed;

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
	__le32 unsupport_mcs;
} __packed;

#define INTERFERENCE_DATA_AVAILABLE      cpu_to_le32(1)

struct statistics_rx_non_phy {
	__le32 bogus_cts;	/* CTS received when not expecting CTS */
	__le32 bogus_ack;	/* ACK received when not expecting ACK */
	__le32 non_bssid_frames;	/* number of frames with BSSID that
					 * doesn't belong to the STA BSSID */
	__le32 filtered_frames;	/* count frames that were dumped in the
				 * filtering process */
	__le32 non_channel_beacons;	/* beacons with our bss id but not on
					 * our serving channel */
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
	__le32 channel_load;		/* counts RX Enable time in uSec */
	__le32 dsp_false_alarms;	/* DSP false alarm (both OFDM
					 * and CCK) counter */
	__le32 beacon_rssi_a;
	__le32 beacon_rssi_b;
	__le32 beacon_rssi_c;
	__le32 beacon_energy_a;
	__le32 beacon_energy_b;
	__le32 beacon_energy_c;
} __packed;

struct statistics_rx_non_phy_bt {
	struct statistics_rx_non_phy common;
	/* additional stats for bt */
	__le32 num_bt_kills;
	__le32 reserved[2];
} __packed;

struct statistics_rx {
	struct statistics_rx_phy ofdm;
	struct statistics_rx_phy cck;
	struct statistics_rx_non_phy general;
	struct statistics_rx_ht_phy ofdm_ht;
} __packed;

struct statistics_rx_bt {
	struct statistics_rx_phy ofdm;
	struct statistics_rx_phy cck;
	struct statistics_rx_non_phy_bt general;
	struct statistics_rx_ht_phy ofdm_ht;
} __packed;

/**
 * struct statistics_tx_power - current tx power
 *
 * @ant_a: current tx power on chain a in 1/2 dB step
 * @ant_b: current tx power on chain b in 1/2 dB step
 * @ant_c: current tx power on chain c in 1/2 dB step
 */
struct statistics_tx_power {
	u8 ant_a;
	u8 ant_b;
	u8 ant_c;
	u8 reserved;
} __packed;

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
} __packed;

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
	__le32 dump_msdu_cnt;
	__le32 burst_abort_next_frame_mismatch_cnt;
	__le32 burst_abort_missing_next_frame_cnt;
	__le32 cts_timeout_collision;
	__le32 ack_or_ba_timeout_collision;
	struct statistics_tx_non_phy_agg agg;
	/*
	 * "tx_power" are optional parameters provided by uCode,
	 * 6000 series is the only device provide the information,
	 * Those are reserved fields for all the other devices
	 */
	struct statistics_tx_power tx_power;
	__le32 reserved1;
} __packed;


struct statistics_div {
	__le32 tx_on_a;
	__le32 tx_on_b;
	__le32 exec_time;
	__le32 probe_time;
	__le32 reserved1;
	__le32 reserved2;
} __packed;

struct statistics_general_common {
	__le32 temperature;   /* radio temperature */
	__le32 temperature_m; /* radio voltage */
	struct statistics_dbg dbg;
	__le32 sleep_time;
	__le32 slots_out;
	__le32 slots_idle;
	__le32 ttl_timestamp;
	struct statistics_div div;
	__le32 rx_enable_counter;
	/*
	 * num_of_sos_states:
	 *  count the number of times we have to re-tune
	 *  in order to get out of bad PHY status
	 */
	__le32 num_of_sos_states;
} __packed;

struct statistics_bt_activity {
	/* Tx statistics */
	__le32 hi_priority_tx_req_cnt;
	__le32 hi_priority_tx_denied_cnt;
	__le32 lo_priority_tx_req_cnt;
	__le32 lo_priority_tx_denied_cnt;
	/* Rx statistics */
	__le32 hi_priority_rx_req_cnt;
	__le32 hi_priority_rx_denied_cnt;
	__le32 lo_priority_rx_req_cnt;
	__le32 lo_priority_rx_denied_cnt;
} __packed;

struct statistics_general {
	struct statistics_general_common common;
	__le32 reserved2;
	__le32 reserved3;
} __packed;

struct statistics_general_bt {
	struct statistics_general_common common;
	struct statistics_bt_activity activity;
	__le32 reserved2;
	__le32 reserved3;
} __packed;

#define UCODE_STATISTICS_CLEAR_MSK		(0x1 << 0)
#define UCODE_STATISTICS_FREQUENCY_MSK		(0x1 << 1)
#define UCODE_STATISTICS_NARROW_BAND_MSK	(0x1 << 2)

/*
 * REPLY_STATISTICS_CMD = 0x9c,
 * all devices identical.
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
#define IWL_STATS_CONF_CLEAR_STATS cpu_to_le32(0x1)	/* see above */
#define IWL_STATS_CONF_DISABLE_NOTIF cpu_to_le32(0x2)/* see above */
struct iwl_statistics_cmd {
	__le32 configuration_flags;	/* IWL_STATS_CONF_* */
} __packed;

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
#define STATISTICS_REPLY_FLG_BAND_24G_MSK         cpu_to_le32(0x2)
#define STATISTICS_REPLY_FLG_HT40_MODE_MSK        cpu_to_le32(0x8)

struct iwl_notif_statistics {
	__le32 flag;
	struct statistics_rx rx;
	struct statistics_tx tx;
	struct statistics_general general;
} __packed;

struct iwl_bt_notif_statistics {
	__le32 flag;
	struct statistics_rx_bt rx;
	struct statistics_tx tx;
	struct statistics_general_bt general;
} __packed;

/*
 * MISSED_BEACONS_NOTIFICATION = 0xa2 (notification only, not a command)
 *
 * uCode send MISSED_BEACONS_NOTIFICATION to driver when detect beacon missed
 * in regardless of how many missed beacons, which mean when driver receive the
 * notification, inside the command, it can find all the beacons information
 * which include number of total missed beacons, number of consecutive missed
 * beacons, number of beacons received and number of beacons expected to
 * receive.
 *
 * If uCode detected consecutive_missed_beacons > 5, it will reset the radio
 * in order to bring the radio/PHY back to working state; which has no relation
 * to when driver will perform sensitivity calibration.
 *
 * Driver should set it own missed_beacon_threshold to decide when to perform
 * sensitivity calibration based on number of consecutive missed beacons in
 * order to improve overall performance, especially in noisy environment.
 *
 */

#define IWL_MISSED_BEACON_THRESHOLD_MIN	(1)
#define IWL_MISSED_BEACON_THRESHOLD_DEF	(5)
#define IWL_MISSED_BEACON_THRESHOLD_MAX	IWL_MISSED_BEACON_THRESHOLD_DEF

struct iwl_missed_beacon_notif {
	__le32 consecutive_missed_beacons;
	__le32 total_missed_becons;
	__le32 num_expected_beacons;
	__le32 num_recvd_beacons;
} __packed;


/******************************************************************************
 * (11)
 * Rx Calibration Commands:
 *
 * With the uCode used for open source drivers, most Tx calibration (except
 * for Tx Power) and most Rx calibration is done by uCode during the
 * "initialize" phase of uCode boot.  Driver must calibrate only:
 *
 * 1)  Tx power (depends on temperature), described elsewhere
 * 2)  Receiver gain balance (optimize MIMO, and detect disconnected antennas)
 * 3)  Receiver sensitivity (to optimize signal detection)
 *
 *****************************************************************************/

/**
 * SENSITIVITY_CMD = 0xa8 (command, has simple generic response)
 *
 * This command sets up the Rx signal detector for a sensitivity level that
 * is high enough to lock onto all signals within the associated network,
 * but low enough to ignore signals that are below a certain threshold, so as
 * not to have too many "false alarms".  False alarms are signals that the
 * Rx DSP tries to lock onto, but then discards after determining that they
 * are noise.
 *
 * The optimum number of false alarms is between 5 and 50 per 200 TUs
 * (200 * 1024 uSecs, i.e. 204.8 milliseconds) of actual Rx time (i.e.
 * time listening, not transmitting).  Driver must adjust sensitivity so that
 * the ratio of actual false alarms to actual Rx time falls within this range.
 *
 * While associated, uCode delivers STATISTICS_NOTIFICATIONs after each
 * received beacon.  These provide information to the driver to analyze the
 * sensitivity.  Don't analyze statistics that come in from scanning, or any
 * other non-associated-network source.  Pertinent statistics include:
 *
 * From "general" statistics (struct statistics_rx_non_phy):
 *
 * (beacon_energy_[abc] & 0x0FF00) >> 8 (unsigned, higher value is lower level)
 *   Measure of energy of desired signal.  Used for establishing a level
 *   below which the device does not detect signals.
 *
 * (beacon_silence_rssi_[abc] & 0x0FF00) >> 8 (unsigned, units in dB)
 *   Measure of background noise in silent period after beacon.
 *
 * channel_load
 *   uSecs of actual Rx time during beacon period (varies according to
 *   how much time was spent transmitting).
 *
 * From "cck" and "ofdm" statistics (struct statistics_rx_phy), separately:
 *
 * false_alarm_cnt
 *   Signal locks abandoned early (before phy-level header).
 *
 * plcp_err
 *   Signal locks abandoned late (during phy-level header).
 *
 * NOTE:  Both false_alarm_cnt and plcp_err increment monotonically from
 *        beacon to beacon, i.e. each value is an accumulation of all errors
 *        before and including the latest beacon.  Values will wrap around to 0
 *        after counting up to 2^32 - 1.  Driver must differentiate vs.
 *        previous beacon's values to determine # false alarms in the current
 *        beacon period.
 *
 * Total number of false alarms = false_alarms + plcp_errs
 *
 * For OFDM, adjust the following table entries in struct iwl_sensitivity_cmd
 * (notice that the start points for OFDM are at or close to settings for
 * maximum sensitivity):
 *
 *                                             START  /  MIN  /  MAX
 *   HD_AUTO_CORR32_X1_TH_ADD_MIN_INDEX          90   /   85  /  120
 *   HD_AUTO_CORR32_X1_TH_ADD_MIN_MRC_INDEX     170   /  170  /  210
 *   HD_AUTO_CORR32_X4_TH_ADD_MIN_INDEX         105   /  105  /  140
 *   HD_AUTO_CORR32_X4_TH_ADD_MIN_MRC_INDEX     220   /  220  /  270
 *
 *   If actual rate of OFDM false alarms (+ plcp_errors) is too high
 *   (greater than 50 for each 204.8 msecs listening), reduce sensitivity
 *   by *adding* 1 to all 4 of the table entries above, up to the max for
 *   each entry.  Conversely, if false alarm rate is too low (less than 5
 *   for each 204.8 msecs listening), *subtract* 1 from each entry to
 *   increase sensitivity.
 *
 * For CCK sensitivity, keep track of the following:
 *
 *   1).  20-beacon history of maximum background noise, indicated by
 *        (beacon_silence_rssi_[abc] & 0x0FF00), units in dB, across the
 *        3 receivers.  For any given beacon, the "silence reference" is
 *        the maximum of last 60 samples (20 beacons * 3 receivers).
 *
 *   2).  10-beacon history of strongest signal level, as indicated
 *        by (beacon_energy_[abc] & 0x0FF00) >> 8, across the 3 receivers,
 *        i.e. the strength of the signal through the best receiver at the
 *        moment.  These measurements are "upside down", with lower values
 *        for stronger signals, so max energy will be *minimum* value.
 *
 *        Then for any given beacon, the driver must determine the *weakest*
 *        of the strongest signals; this is the minimum level that needs to be
 *        successfully detected, when using the best receiver at the moment.
 *        "Max cck energy" is the maximum (higher value means lower energy!)
 *        of the last 10 minima.  Once this is determined, driver must add
 *        a little margin by adding "6" to it.
 *
 *   3).  Number of consecutive beacon periods with too few false alarms.
 *        Reset this to 0 at the first beacon period that falls within the
 *        "good" range (5 to 50 false alarms per 204.8 milliseconds rx).
 *
 * Then, adjust the following CCK table entries in struct iwl_sensitivity_cmd
 * (notice that the start points for CCK are at maximum sensitivity):
 *
 *                                             START  /  MIN  /  MAX
 *   HD_AUTO_CORR40_X4_TH_ADD_MIN_INDEX         125   /  125  /  200
 *   HD_AUTO_CORR40_X4_TH_ADD_MIN_MRC_INDEX     200   /  200  /  400
 *   HD_MIN_ENERGY_CCK_DET_INDEX                100   /    0  /  100
 *
 *   If actual rate of CCK false alarms (+ plcp_errors) is too high
 *   (greater than 50 for each 204.8 msecs listening), method for reducing
 *   sensitivity is:
 *
 *   1)  *Add* 3 to value in HD_AUTO_CORR40_X4_TH_ADD_MIN_MRC_INDEX,
 *       up to max 400.
 *
 *   2)  If current value in HD_AUTO_CORR40_X4_TH_ADD_MIN_INDEX is < 160,
 *       sensitivity has been reduced a significant amount; bring it up to
 *       a moderate 161.  Otherwise, *add* 3, up to max 200.
 *
 *   3)  a)  If current value in HD_AUTO_CORR40_X4_TH_ADD_MIN_INDEX is > 160,
 *       sensitivity has been reduced only a moderate or small amount;
 *       *subtract* 2 from value in HD_MIN_ENERGY_CCK_DET_INDEX,
 *       down to min 0.  Otherwise (if gain has been significantly reduced),
 *       don't change the HD_MIN_ENERGY_CCK_DET_INDEX value.
 *
 *       b)  Save a snapshot of the "silence reference".
 *
 *   If actual rate of CCK false alarms (+ plcp_errors) is too low
 *   (less than 5 for each 204.8 msecs listening), method for increasing
 *   sensitivity is used only if:
 *
 *   1a)  Previous beacon did not have too many false alarms
 *   1b)  AND difference between previous "silence reference" and current
 *        "silence reference" (prev - current) is 2 or more,
 *   OR 2)  100 or more consecutive beacon periods have had rate of
 *          less than 5 false alarms per 204.8 milliseconds rx time.
 *
 *   Method for increasing sensitivity:
 *
 *   1)  *Subtract* 3 from value in HD_AUTO_CORR40_X4_TH_ADD_MIN_INDEX,
 *       down to min 125.
 *
 *   2)  *Subtract* 3 from value in HD_AUTO_CORR40_X4_TH_ADD_MIN_MRC_INDEX,
 *       down to min 200.
 *
 *   3)  *Add* 2 to value in HD_MIN_ENERGY_CCK_DET_INDEX, up to max 100.
 *
 *   If actual rate of CCK false alarms (+ plcp_errors) is within good range
 *   (between 5 and 50 for each 204.8 msecs listening):
 *
 *   1)  Save a snapshot of the silence reference.
 *
 *   2)  If previous beacon had too many CCK false alarms (+ plcp_errors),
 *       give some extra margin to energy threshold by *subtracting* 8
 *       from value in HD_MIN_ENERGY_CCK_DET_INDEX.
 *
 *   For all cases (too few, too many, good range), make sure that the CCK
 *   detection threshold (energy) is below the energy level for robust
 *   detection over the past 10 beacon periods, the "Max cck energy".
 *   Lower values mean higher energy; this means making sure that the value
 *   in HD_MIN_ENERGY_CCK_DET_INDEX is at or *above* "Max cck energy".
 *
 */

/*
 * Table entries in SENSITIVITY_CMD (struct iwl_sensitivity_cmd)
 */
#define HD_TABLE_SIZE  (11)	/* number of entries */
#define HD_MIN_ENERGY_CCK_DET_INDEX                 (0)	/* table indexes */
#define HD_MIN_ENERGY_OFDM_DET_INDEX                (1)
#define HD_AUTO_CORR32_X1_TH_ADD_MIN_INDEX          (2)
#define HD_AUTO_CORR32_X1_TH_ADD_MIN_MRC_INDEX      (3)
#define HD_AUTO_CORR40_X4_TH_ADD_MIN_MRC_INDEX      (4)
#define HD_AUTO_CORR32_X4_TH_ADD_MIN_INDEX          (5)
#define HD_AUTO_CORR32_X4_TH_ADD_MIN_MRC_INDEX      (6)
#define HD_BARKER_CORR_TH_ADD_MIN_INDEX             (7)
#define HD_BARKER_CORR_TH_ADD_MIN_MRC_INDEX         (8)
#define HD_AUTO_CORR40_X4_TH_ADD_MIN_INDEX          (9)
#define HD_OFDM_ENERGY_TH_IN_INDEX                  (10)

/*
 * Additional table entries in enhance SENSITIVITY_CMD
 */
#define HD_INA_NON_SQUARE_DET_OFDM_INDEX		(11)
#define HD_INA_NON_SQUARE_DET_CCK_INDEX			(12)
#define HD_CORR_11_INSTEAD_OF_CORR_9_EN_INDEX		(13)
#define HD_OFDM_NON_SQUARE_DET_SLOPE_MRC_INDEX		(14)
#define HD_OFDM_NON_SQUARE_DET_INTERCEPT_MRC_INDEX	(15)
#define HD_OFDM_NON_SQUARE_DET_SLOPE_INDEX		(16)
#define HD_OFDM_NON_SQUARE_DET_INTERCEPT_INDEX		(17)
#define HD_CCK_NON_SQUARE_DET_SLOPE_MRC_INDEX		(18)
#define HD_CCK_NON_SQUARE_DET_INTERCEPT_MRC_INDEX	(19)
#define HD_CCK_NON_SQUARE_DET_SLOPE_INDEX		(20)
#define HD_CCK_NON_SQUARE_DET_INTERCEPT_INDEX		(21)
#define HD_RESERVED					(22)

/* number of entries for enhanced tbl */
#define ENHANCE_HD_TABLE_SIZE  (23)

/* number of additional entries for enhanced tbl */
#define ENHANCE_HD_TABLE_ENTRIES  (ENHANCE_HD_TABLE_SIZE - HD_TABLE_SIZE)

#define HD_INA_NON_SQUARE_DET_OFDM_DATA_V1		cpu_to_le16(0)
#define HD_INA_NON_SQUARE_DET_CCK_DATA_V1		cpu_to_le16(0)
#define HD_CORR_11_INSTEAD_OF_CORR_9_EN_DATA_V1		cpu_to_le16(0)
#define HD_OFDM_NON_SQUARE_DET_SLOPE_MRC_DATA_V1	cpu_to_le16(668)
#define HD_OFDM_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V1	cpu_to_le16(4)
#define HD_OFDM_NON_SQUARE_DET_SLOPE_DATA_V1		cpu_to_le16(486)
#define HD_OFDM_NON_SQUARE_DET_INTERCEPT_DATA_V1	cpu_to_le16(37)
#define HD_CCK_NON_SQUARE_DET_SLOPE_MRC_DATA_V1		cpu_to_le16(853)
#define HD_CCK_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V1	cpu_to_le16(4)
#define HD_CCK_NON_SQUARE_DET_SLOPE_DATA_V1		cpu_to_le16(476)
#define HD_CCK_NON_SQUARE_DET_INTERCEPT_DATA_V1		cpu_to_le16(99)

#define HD_INA_NON_SQUARE_DET_OFDM_DATA_V2		cpu_to_le16(1)
#define HD_INA_NON_SQUARE_DET_CCK_DATA_V2		cpu_to_le16(1)
#define HD_CORR_11_INSTEAD_OF_CORR_9_EN_DATA_V2		cpu_to_le16(1)
#define HD_OFDM_NON_SQUARE_DET_SLOPE_MRC_DATA_V2	cpu_to_le16(600)
#define HD_OFDM_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V2	cpu_to_le16(40)
#define HD_OFDM_NON_SQUARE_DET_SLOPE_DATA_V2		cpu_to_le16(486)
#define HD_OFDM_NON_SQUARE_DET_INTERCEPT_DATA_V2	cpu_to_le16(45)
#define HD_CCK_NON_SQUARE_DET_SLOPE_MRC_DATA_V2		cpu_to_le16(853)
#define HD_CCK_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V2	cpu_to_le16(60)
#define HD_CCK_NON_SQUARE_DET_SLOPE_DATA_V2		cpu_to_le16(476)
#define HD_CCK_NON_SQUARE_DET_INTERCEPT_DATA_V2		cpu_to_le16(99)


/* Control field in struct iwl_sensitivity_cmd */
#define SENSITIVITY_CMD_CONTROL_DEFAULT_TABLE	cpu_to_le16(0)
#define SENSITIVITY_CMD_CONTROL_WORK_TABLE	cpu_to_le16(1)

/**
 * struct iwl_sensitivity_cmd
 * @control:  (1) updates working table, (0) updates default table
 * @table:  energy threshold values, use HD_* as index into table
 *
 * Always use "1" in "control" to update uCode's working table and DSP.
 */
struct iwl_sensitivity_cmd {
	__le16 control;			/* always use "1" */
	__le16 table[HD_TABLE_SIZE];	/* use HD_* as index */
} __packed;

/*
 *
 */
struct iwl_enhance_sensitivity_cmd {
	__le16 control;			/* always use "1" */
	__le16 enhance_table[ENHANCE_HD_TABLE_SIZE];	/* use HD_* as index */
} __packed;


/**
 * REPLY_PHY_CALIBRATION_CMD = 0xb0 (command, has simple generic response)
 *
 * This command sets the relative gains of agn device's 3 radio receiver chains.
 *
 * After the first association, driver should accumulate signal and noise
 * statistics from the STATISTICS_NOTIFICATIONs that follow the first 20
 * beacons from the associated network (don't collect statistics that come
 * in from scanning, or any other non-network source).
 *
 * DISCONNECTED ANTENNA:
 *
 * Driver should determine which antennas are actually connected, by comparing
 * average beacon signal levels for the 3 Rx chains.  Accumulate (add) the
 * following values over 20 beacons, one accumulator for each of the chains
 * a/b/c, from struct statistics_rx_non_phy:
 *
 * beacon_rssi_[abc] & 0x0FF (unsigned, units in dB)
 *
 * Find the strongest signal from among a/b/c.  Compare the other two to the
 * strongest.  If any signal is more than 15 dB (times 20, unless you
 * divide the accumulated values by 20) below the strongest, the driver
 * considers that antenna to be disconnected, and should not try to use that
 * antenna/chain for Rx or Tx.  If both A and B seem to be disconnected,
 * driver should declare the stronger one as connected, and attempt to use it
 * (A and B are the only 2 Tx chains!).
 *
 *
 * RX BALANCE:
 *
 * Driver should balance the 3 receivers (but just the ones that are connected
 * to antennas, see above) for gain, by comparing the average signal levels
 * detected during the silence after each beacon (background noise).
 * Accumulate (add) the following values over 20 beacons, one accumulator for
 * each of the chains a/b/c, from struct statistics_rx_non_phy:
 *
 * beacon_silence_rssi_[abc] & 0x0FF (unsigned, units in dB)
 *
 * Find the weakest background noise level from among a/b/c.  This Rx chain
 * will be the reference, with 0 gain adjustment.  Attenuate other channels by
 * finding noise difference:
 *
 * (accum_noise[i] - accum_noise[reference]) / 30
 *
 * The "30" adjusts the dB in the 20 accumulated samples to units of 1.5 dB.
 * For use in diff_gain_[abc] fields of struct iwl_calibration_cmd, the
 * driver should limit the difference results to a range of 0-3 (0-4.5 dB),
 * and set bit 2 to indicate "reduce gain".  The value for the reference
 * (weakest) chain should be "0".
 *
 * diff_gain_[abc] bit fields:
 *   2: (1) reduce gain, (0) increase gain
 * 1-0: amount of gain, units of 1.5 dB
 */

/* Phy calibration command for series */
enum {
	IWL_PHY_CALIBRATE_DC_CMD		= 8,
	IWL_PHY_CALIBRATE_LO_CMD		= 9,
	IWL_PHY_CALIBRATE_TX_IQ_CMD		= 11,
	IWL_PHY_CALIBRATE_CRYSTAL_FRQ_CMD	= 15,
	IWL_PHY_CALIBRATE_BASE_BAND_CMD		= 16,
	IWL_PHY_CALIBRATE_TX_IQ_PERD_CMD	= 17,
	IWL_PHY_CALIBRATE_TEMP_OFFSET_CMD	= 18,
};

/* This enum defines the bitmap of various calibrations to enable in both
 * init ucode and runtime ucode through CALIBRATION_CFG_CMD.
 */
enum iwl_ucode_calib_cfg {
	IWL_CALIB_CFG_RX_BB_IDX			= BIT(0),
	IWL_CALIB_CFG_DC_IDX			= BIT(1),
	IWL_CALIB_CFG_LO_IDX			= BIT(2),
	IWL_CALIB_CFG_TX_IQ_IDX			= BIT(3),
	IWL_CALIB_CFG_RX_IQ_IDX			= BIT(4),
	IWL_CALIB_CFG_NOISE_IDX			= BIT(5),
	IWL_CALIB_CFG_CRYSTAL_IDX		= BIT(6),
	IWL_CALIB_CFG_TEMPERATURE_IDX		= BIT(7),
	IWL_CALIB_CFG_PAPD_IDX			= BIT(8),
	IWL_CALIB_CFG_SENSITIVITY_IDX		= BIT(9),
	IWL_CALIB_CFG_TX_PWR_IDX		= BIT(10),
};

#define IWL_CALIB_INIT_CFG_ALL	cpu_to_le32(IWL_CALIB_CFG_RX_BB_IDX |	\
					IWL_CALIB_CFG_DC_IDX |		\
					IWL_CALIB_CFG_LO_IDX |		\
					IWL_CALIB_CFG_TX_IQ_IDX |	\
					IWL_CALIB_CFG_RX_IQ_IDX |	\
					IWL_CALIB_CFG_CRYSTAL_IDX)

#define IWL_CALIB_RT_CFG_ALL	cpu_to_le32(IWL_CALIB_CFG_RX_BB_IDX |	\
					IWL_CALIB_CFG_DC_IDX |		\
					IWL_CALIB_CFG_LO_IDX |		\
					IWL_CALIB_CFG_TX_IQ_IDX |	\
					IWL_CALIB_CFG_RX_IQ_IDX |	\
					IWL_CALIB_CFG_TEMPERATURE_IDX |	\
					IWL_CALIB_CFG_PAPD_IDX |	\
					IWL_CALIB_CFG_TX_PWR_IDX |	\
					IWL_CALIB_CFG_CRYSTAL_IDX)

#define IWL_CALIB_CFG_FLAG_SEND_COMPLETE_NTFY_MSK	cpu_to_le32(BIT(0))

struct iwl_calib_cfg_elmnt_s {
	__le32 is_enable;
	__le32 start;
	__le32 send_res;
	__le32 apply_res;
	__le32 reserved;
} __packed;

struct iwl_calib_cfg_status_s {
	struct iwl_calib_cfg_elmnt_s once;
	struct iwl_calib_cfg_elmnt_s perd;
	__le32 flags;
} __packed;

struct iwl_calib_cfg_cmd {
	struct iwl_calib_cfg_status_s ucd_calib_cfg;
	struct iwl_calib_cfg_status_s drv_calib_cfg;
	__le32 reserved1;
} __packed;

struct iwl_calib_hdr {
	u8 op_code;
	u8 first_group;
	u8 groups_num;
	u8 data_valid;
} __packed;

struct iwl_calib_cmd {
	struct iwl_calib_hdr hdr;
	u8 data[0];
} __packed;

struct iwl_calib_xtal_freq_cmd {
	struct iwl_calib_hdr hdr;
	u8 cap_pin1;
	u8 cap_pin2;
	u8 pad[2];
} __packed;

#define DEFAULT_RADIO_SENSOR_OFFSET    cpu_to_le16(2700)
struct iwl_calib_temperature_offset_cmd {
	struct iwl_calib_hdr hdr;
	__le16 radio_sensor_offset;
	__le16 reserved;
} __packed;

struct iwl_calib_temperature_offset_v2_cmd {
	struct iwl_calib_hdr hdr;
	__le16 radio_sensor_offset_high;
	__le16 radio_sensor_offset_low;
	__le16 burntVoltageRef;
	__le16 reserved;
} __packed;

/* IWL_PHY_CALIBRATE_CHAIN_NOISE_RESET_CMD */
struct iwl_calib_chain_noise_reset_cmd {
	struct iwl_calib_hdr hdr;
	u8 data[0];
};

/* IWL_PHY_CALIBRATE_CHAIN_NOISE_GAIN_CMD */
struct iwl_calib_chain_noise_gain_cmd {
	struct iwl_calib_hdr hdr;
	u8 delta_gain_1;
	u8 delta_gain_2;
	u8 pad[2];
} __packed;

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
} __packed;

/*
 * station priority table entries
 * also used as potential "events" value for both
 * COEX_MEDIUM_NOTIFICATION and COEX_EVENT_CMD
 */

/*
 * COEX events entry flag masks
 * RP - Requested Priority
 * WP - Win Medium Priority: priority assigned when the contention has been won
 */
#define COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG        (0x1)
#define COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG        (0x2)
#define COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_FLG  (0x4)

#define COEX_CU_UNASSOC_IDLE_RP               4
#define COEX_CU_UNASSOC_MANUAL_SCAN_RP        4
#define COEX_CU_UNASSOC_AUTO_SCAN_RP          4
#define COEX_CU_CALIBRATION_RP                4
#define COEX_CU_PERIODIC_CALIBRATION_RP       4
#define COEX_CU_CONNECTION_ESTAB_RP           4
#define COEX_CU_ASSOCIATED_IDLE_RP            4
#define COEX_CU_ASSOC_MANUAL_SCAN_RP          4
#define COEX_CU_ASSOC_AUTO_SCAN_RP            4
#define COEX_CU_ASSOC_ACTIVE_LEVEL_RP         4
#define COEX_CU_RF_ON_RP                      6
#define COEX_CU_RF_OFF_RP                     4
#define COEX_CU_STAND_ALONE_DEBUG_RP          6
#define COEX_CU_IPAN_ASSOC_LEVEL_RP           4
#define COEX_CU_RSRVD1_RP                     4
#define COEX_CU_RSRVD2_RP                     4

#define COEX_CU_UNASSOC_IDLE_WP               3
#define COEX_CU_UNASSOC_MANUAL_SCAN_WP        3
#define COEX_CU_UNASSOC_AUTO_SCAN_WP          3
#define COEX_CU_CALIBRATION_WP                3
#define COEX_CU_PERIODIC_CALIBRATION_WP       3
#define COEX_CU_CONNECTION_ESTAB_WP           3
#define COEX_CU_ASSOCIATED_IDLE_WP            3
#define COEX_CU_ASSOC_MANUAL_SCAN_WP          3
#define COEX_CU_ASSOC_AUTO_SCAN_WP            3
#define COEX_CU_ASSOC_ACTIVE_LEVEL_WP         3
#define COEX_CU_RF_ON_WP                      3
#define COEX_CU_RF_OFF_WP                     3
#define COEX_CU_STAND_ALONE_DEBUG_WP          6
#define COEX_CU_IPAN_ASSOC_LEVEL_WP           3
#define COEX_CU_RSRVD1_WP                     3
#define COEX_CU_RSRVD2_WP                     3

#define COEX_UNASSOC_IDLE_FLAGS                     0
#define COEX_UNASSOC_MANUAL_SCAN_FLAGS		\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG)
#define COEX_UNASSOC_AUTO_SCAN_FLAGS		\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG)
#define COEX_CALIBRATION_FLAGS			\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG)
#define COEX_PERIODIC_CALIBRATION_FLAGS             0
/*
 * COEX_CONNECTION_ESTAB:
 * we need DELAY_MEDIUM_FREE_NTFY to let WiMAX disconnect from network.
 */
#define COEX_CONNECTION_ESTAB_FLAGS		\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG |	\
	COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_FLG)
#define COEX_ASSOCIATED_IDLE_FLAGS                  0
#define COEX_ASSOC_MANUAL_SCAN_FLAGS		\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG)
#define COEX_ASSOC_AUTO_SCAN_FLAGS		\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG)
#define COEX_ASSOC_ACTIVE_LEVEL_FLAGS               0
#define COEX_RF_ON_FLAGS                            0
#define COEX_RF_OFF_FLAGS                           0
#define COEX_STAND_ALONE_DEBUG_FLAGS		\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG)
#define COEX_IPAN_ASSOC_LEVEL_FLAGS		\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG |	\
	 COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_FLG)
#define COEX_RSRVD1_FLAGS                           0
#define COEX_RSRVD2_FLAGS                           0
/*
 * COEX_CU_RF_ON is the event wrapping all radio ownership.
 * We need DELAY_MEDIUM_FREE_NTFY to let WiMAX disconnect from network.
 */
#define COEX_CU_RF_ON_FLAGS			\
	(COEX_EVT_FLAG_MEDIUM_FREE_NTFY_FLG |	\
	 COEX_EVT_FLAG_MEDIUM_ACTV_NTFY_FLG |	\
	 COEX_EVT_FLAG_DELAY_MEDIUM_FREE_NTFY_FLG)


enum {
	/* un-association part */
	COEX_UNASSOC_IDLE		= 0,
	COEX_UNASSOC_MANUAL_SCAN	= 1,
	COEX_UNASSOC_AUTO_SCAN		= 2,
	/* calibration */
	COEX_CALIBRATION		= 3,
	COEX_PERIODIC_CALIBRATION	= 4,
	/* connection */
	COEX_CONNECTION_ESTAB		= 5,
	/* association part */
	COEX_ASSOCIATED_IDLE		= 6,
	COEX_ASSOC_MANUAL_SCAN		= 7,
	COEX_ASSOC_AUTO_SCAN		= 8,
	COEX_ASSOC_ACTIVE_LEVEL		= 9,
	/* RF ON/OFF */
	COEX_RF_ON			= 10,
	COEX_RF_OFF			= 11,
	COEX_STAND_ALONE_DEBUG		= 12,
	/* IPAN */
	COEX_IPAN_ASSOC_LEVEL		= 13,
	/* reserved */
	COEX_RSRVD1			= 14,
	COEX_RSRVD2			= 15,
	COEX_NUM_OF_EVENTS		= 16
};

/*
 * Coexistence WIFI/WIMAX  Command
 * COEX_PRIORITY_TABLE_CMD = 0x5a
 *
 */
struct iwl_wimax_coex_event_entry {
	u8 request_prio;
	u8 win_medium_prio;
	u8 reserved;
	u8 flags;
} __packed;

/* COEX flag masks */

/* Station table is valid */
#define COEX_FLAGS_STA_TABLE_VALID_MSK      (0x1)
/* UnMask wake up src at unassociated sleep */
#define COEX_FLAGS_UNASSOC_WA_UNMASK_MSK    (0x4)
/* UnMask wake up src at associated sleep */
#define COEX_FLAGS_ASSOC_WA_UNMASK_MSK      (0x8)
/* Enable CoEx feature. */
#define COEX_FLAGS_COEX_ENABLE_MSK          (0x80)

struct iwl_wimax_coex_cmd {
	u8 flags;
	u8 reserved[3];
	struct iwl_wimax_coex_event_entry sta_prio[COEX_NUM_OF_EVENTS];
} __packed;

/*
 * Coexistence MEDIUM NOTIFICATION
 * COEX_MEDIUM_NOTIFICATION = 0x5b
 *
 * notification from uCode to host to indicate medium changes
 *
 */
/*
 * status field
 * bit 0 - 2: medium status
 * bit 3: medium change indication
 * bit 4 - 31: reserved
 */
/* status option values, (0 - 2 bits) */
#define COEX_MEDIUM_BUSY	(0x0) /* radio belongs to WiMAX */
#define COEX_MEDIUM_ACTIVE	(0x1) /* radio belongs to WiFi */
#define COEX_MEDIUM_PRE_RELEASE	(0x2) /* received radio release */
#define COEX_MEDIUM_MSK		(0x7)

/* send notification status (1 bit) */
#define COEX_MEDIUM_CHANGED	(0x8)
#define COEX_MEDIUM_CHANGED_MSK	(0x8)
#define COEX_MEDIUM_SHIFT	(3)

struct iwl_coex_medium_notification {
	__le32 status;
	__le32 events;
} __packed;

/*
 * Coexistence EVENT  Command
 * COEX_EVENT_CMD = 0x5c
 *
 * send from host to uCode for coex event request.
 */
/* flags options */
#define COEX_EVENT_REQUEST_MSK	(0x1)

struct iwl_coex_event_cmd {
	u8 flags;
	u8 event;
	__le16 reserved;
} __packed;

struct iwl_coex_event_resp {
	__le32 status;
} __packed;


/******************************************************************************
 * Bluetooth Coexistence commands
 *
 *****************************************************************************/

/*
 * BT Status notification
 * REPLY_BT_COEX_PROFILE_NOTIF = 0xce
 */
enum iwl_bt_coex_profile_traffic_load {
	IWL_BT_COEX_TRAFFIC_LOAD_NONE = 	0,
	IWL_BT_COEX_TRAFFIC_LOAD_LOW =		1,
	IWL_BT_COEX_TRAFFIC_LOAD_HIGH = 	2,
	IWL_BT_COEX_TRAFFIC_LOAD_CONTINUOUS =	3,
/*
 * There are no more even though below is a u8, the
 * indication from the BT device only has two bits.
 */
};

#define BT_SESSION_ACTIVITY_1_UART_MSG		0x1
#define BT_SESSION_ACTIVITY_2_UART_MSG		0x2

/* BT UART message - Share Part (BT -> WiFi) */
#define BT_UART_MSG_FRAME1MSGTYPE_POS		(0)
#define BT_UART_MSG_FRAME1MSGTYPE_MSK		\
		(0x7 << BT_UART_MSG_FRAME1MSGTYPE_POS)
#define BT_UART_MSG_FRAME1SSN_POS		(3)
#define BT_UART_MSG_FRAME1SSN_MSK		\
		(0x3 << BT_UART_MSG_FRAME1SSN_POS)
#define BT_UART_MSG_FRAME1UPDATEREQ_POS		(5)
#define BT_UART_MSG_FRAME1UPDATEREQ_MSK		\
		(0x1 << BT_UART_MSG_FRAME1UPDATEREQ_POS)
#define BT_UART_MSG_FRAME1RESERVED_POS		(6)
#define BT_UART_MSG_FRAME1RESERVED_MSK		\
		(0x3 << BT_UART_MSG_FRAME1RESERVED_POS)

#define BT_UART_MSG_FRAME2OPENCONNECTIONS_POS	(0)
#define BT_UART_MSG_FRAME2OPENCONNECTIONS_MSK	\
		(0x3 << BT_UART_MSG_FRAME2OPENCONNECTIONS_POS)
#define BT_UART_MSG_FRAME2TRAFFICLOAD_POS	(2)
#define BT_UART_MSG_FRAME2TRAFFICLOAD_MSK	\
		(0x3 << BT_UART_MSG_FRAME2TRAFFICLOAD_POS)
#define BT_UART_MSG_FRAME2CHLSEQN_POS		(4)
#define BT_UART_MSG_FRAME2CHLSEQN_MSK		\
		(0x1 << BT_UART_MSG_FRAME2CHLSEQN_POS)
#define BT_UART_MSG_FRAME2INBAND_POS		(5)
#define BT_UART_MSG_FRAME2INBAND_MSK		\
		(0x1 << BT_UART_MSG_FRAME2INBAND_POS)
#define BT_UART_MSG_FRAME2RESERVED_POS		(6)
#define BT_UART_MSG_FRAME2RESERVED_MSK		\
		(0x3 << BT_UART_MSG_FRAME2RESERVED_POS)

#define BT_UART_MSG_FRAME3SCOESCO_POS		(0)
#define BT_UART_MSG_FRAME3SCOESCO_MSK		\
		(0x1 << BT_UART_MSG_FRAME3SCOESCO_POS)
#define BT_UART_MSG_FRAME3SNIFF_POS		(1)
#define BT_UART_MSG_FRAME3SNIFF_MSK		\
		(0x1 << BT_UART_MSG_FRAME3SNIFF_POS)
#define BT_UART_MSG_FRAME3A2DP_POS		(2)
#define BT_UART_MSG_FRAME3A2DP_MSK		\
		(0x1 << BT_UART_MSG_FRAME3A2DP_POS)
#define BT_UART_MSG_FRAME3ACL_POS		(3)
#define BT_UART_MSG_FRAME3ACL_MSK		\
		(0x1 << BT_UART_MSG_FRAME3ACL_POS)
#define BT_UART_MSG_FRAME3MASTER_POS		(4)
#define BT_UART_MSG_FRAME3MASTER_MSK		\
		(0x1 << BT_UART_MSG_FRAME3MASTER_POS)
#define BT_UART_MSG_FRAME3OBEX_POS		(5)
#define BT_UART_MSG_FRAME3OBEX_MSK		\
		(0x1 << BT_UART_MSG_FRAME3OBEX_POS)
#define BT_UART_MSG_FRAME3RESERVED_POS		(6)
#define BT_UART_MSG_FRAME3RESERVED_MSK		\
		(0x3 << BT_UART_MSG_FRAME3RESERVED_POS)

#define BT_UART_MSG_FRAME4IDLEDURATION_POS	(0)
#define BT_UART_MSG_FRAME4IDLEDURATION_MSK	\
		(0x3F << BT_UART_MSG_FRAME4IDLEDURATION_POS)
#define BT_UART_MSG_FRAME4RESERVED_POS		(6)
#define BT_UART_MSG_FRAME4RESERVED_MSK		\
		(0x3 << BT_UART_MSG_FRAME4RESERVED_POS)

#define BT_UART_MSG_FRAME5TXACTIVITY_POS	(0)
#define BT_UART_MSG_FRAME5TXACTIVITY_MSK	\
		(0x3 << BT_UART_MSG_FRAME5TXACTIVITY_POS)
#define BT_UART_MSG_FRAME5RXACTIVITY_POS	(2)
#define BT_UART_MSG_FRAME5RXACTIVITY_MSK	\
		(0x3 << BT_UART_MSG_FRAME5RXACTIVITY_POS)
#define BT_UART_MSG_FRAME5ESCORETRANSMIT_POS	(4)
#define BT_UART_MSG_FRAME5ESCORETRANSMIT_MSK	\
		(0x3 << BT_UART_MSG_FRAME5ESCORETRANSMIT_POS)
#define BT_UART_MSG_FRAME5RESERVED_POS		(6)
#define BT_UART_MSG_FRAME5RESERVED_MSK		\
		(0x3 << BT_UART_MSG_FRAME5RESERVED_POS)

#define BT_UART_MSG_FRAME6SNIFFINTERVAL_POS	(0)
#define BT_UART_MSG_FRAME6SNIFFINTERVAL_MSK	\
		(0x1F << BT_UART_MSG_FRAME6SNIFFINTERVAL_POS)
#define BT_UART_MSG_FRAME6DISCOVERABLE_POS	(5)
#define BT_UART_MSG_FRAME6DISCOVERABLE_MSK	\
		(0x1 << BT_UART_MSG_FRAME6DISCOVERABLE_POS)
#define BT_UART_MSG_FRAME6RESERVED_POS		(6)
#define BT_UART_MSG_FRAME6RESERVED_MSK		\
		(0x3 << BT_UART_MSG_FRAME6RESERVED_POS)

#define BT_UART_MSG_FRAME7SNIFFACTIVITY_POS	(0)
#define BT_UART_MSG_FRAME7SNIFFACTIVITY_MSK	\
		(0x7 << BT_UART_MSG_FRAME7SNIFFACTIVITY_POS)
#define BT_UART_MSG_FRAME7PAGE_POS		(3)
#define BT_UART_MSG_FRAME7PAGE_MSK		\
		(0x1 << BT_UART_MSG_FRAME7PAGE_POS)
#define BT_UART_MSG_FRAME7INQUIRY_POS		(4)
#define BT_UART_MSG_FRAME7INQUIRY_MSK		\
		(0x1 << BT_UART_MSG_FRAME7INQUIRY_POS)
#define BT_UART_MSG_FRAME7CONNECTABLE_POS	(5)
#define BT_UART_MSG_FRAME7CONNECTABLE_MSK	\
		(0x1 << BT_UART_MSG_FRAME7CONNECTABLE_POS)
#define BT_UART_MSG_FRAME7RESERVED_POS		(6)
#define BT_UART_MSG_FRAME7RESERVED_MSK		\
		(0x3 << BT_UART_MSG_FRAME7RESERVED_POS)

/* BT Session Activity 2 UART message (BT -> WiFi) */
#define BT_UART_MSG_2_FRAME1RESERVED1_POS	(5)
#define BT_UART_MSG_2_FRAME1RESERVED1_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME1RESERVED1_POS)
#define BT_UART_MSG_2_FRAME1RESERVED2_POS	(6)
#define BT_UART_MSG_2_FRAME1RESERVED2_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME1RESERVED2_POS)

#define BT_UART_MSG_2_FRAME2AGGTRAFFICLOAD_POS	(0)
#define BT_UART_MSG_2_FRAME2AGGTRAFFICLOAD_MSK	\
		(0x3F<<BT_UART_MSG_2_FRAME2AGGTRAFFICLOAD_POS)
#define BT_UART_MSG_2_FRAME2RESERVED_POS	(6)
#define BT_UART_MSG_2_FRAME2RESERVED_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME2RESERVED_POS)

#define BT_UART_MSG_2_FRAME3BRLASTTXPOWER_POS	(0)
#define BT_UART_MSG_2_FRAME3BRLASTTXPOWER_MSK	\
		(0xF<<BT_UART_MSG_2_FRAME3BRLASTTXPOWER_POS)
#define BT_UART_MSG_2_FRAME3INQPAGESRMODE_POS	(4)
#define BT_UART_MSG_2_FRAME3INQPAGESRMODE_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME3INQPAGESRMODE_POS)
#define BT_UART_MSG_2_FRAME3LEMASTER_POS	(5)
#define BT_UART_MSG_2_FRAME3LEMASTER_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME3LEMASTER_POS)
#define BT_UART_MSG_2_FRAME3RESERVED_POS	(6)
#define BT_UART_MSG_2_FRAME3RESERVED_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME3RESERVED_POS)

#define BT_UART_MSG_2_FRAME4LELASTTXPOWER_POS	(0)
#define BT_UART_MSG_2_FRAME4LELASTTXPOWER_MSK	\
		(0xF<<BT_UART_MSG_2_FRAME4LELASTTXPOWER_POS)
#define BT_UART_MSG_2_FRAME4NUMLECONN_POS	(4)
#define BT_UART_MSG_2_FRAME4NUMLECONN_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME4NUMLECONN_POS)
#define BT_UART_MSG_2_FRAME4RESERVED_POS	(6)
#define BT_UART_MSG_2_FRAME4RESERVED_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME4RESERVED_POS)

#define BT_UART_MSG_2_FRAME5BTMINRSSI_POS	(0)
#define BT_UART_MSG_2_FRAME5BTMINRSSI_MSK	\
		(0xF<<BT_UART_MSG_2_FRAME5BTMINRSSI_POS)
#define BT_UART_MSG_2_FRAME5LESCANINITMODE_POS	(4)
#define BT_UART_MSG_2_FRAME5LESCANINITMODE_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME5LESCANINITMODE_POS)
#define BT_UART_MSG_2_FRAME5LEADVERMODE_POS	(5)
#define BT_UART_MSG_2_FRAME5LEADVERMODE_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME5LEADVERMODE_POS)
#define BT_UART_MSG_2_FRAME5RESERVED_POS	(6)
#define BT_UART_MSG_2_FRAME5RESERVED_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME5RESERVED_POS)

#define BT_UART_MSG_2_FRAME6LECONNINTERVAL_POS	(0)
#define BT_UART_MSG_2_FRAME6LECONNINTERVAL_MSK	\
		(0x1F<<BT_UART_MSG_2_FRAME6LECONNINTERVAL_POS)
#define BT_UART_MSG_2_FRAME6RFU_POS		(5)
#define BT_UART_MSG_2_FRAME6RFU_MSK		\
		(0x1<<BT_UART_MSG_2_FRAME6RFU_POS)
#define BT_UART_MSG_2_FRAME6RESERVED_POS	(6)
#define BT_UART_MSG_2_FRAME6RESERVED_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME6RESERVED_POS)

#define BT_UART_MSG_2_FRAME7LECONNSLAVELAT_POS	(0)
#define BT_UART_MSG_2_FRAME7LECONNSLAVELAT_MSK	\
		(0x7<<BT_UART_MSG_2_FRAME7LECONNSLAVELAT_POS)
#define BT_UART_MSG_2_FRAME7LEPROFILE1_POS	(3)
#define BT_UART_MSG_2_FRAME7LEPROFILE1_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME7LEPROFILE1_POS)
#define BT_UART_MSG_2_FRAME7LEPROFILE2_POS	(4)
#define BT_UART_MSG_2_FRAME7LEPROFILE2_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME7LEPROFILE2_POS)
#define BT_UART_MSG_2_FRAME7LEPROFILEOTHER_POS	(5)
#define BT_UART_MSG_2_FRAME7LEPROFILEOTHER_MSK	\
		(0x1<<BT_UART_MSG_2_FRAME7LEPROFILEOTHER_POS)
#define BT_UART_MSG_2_FRAME7RESERVED_POS	(6)
#define BT_UART_MSG_2_FRAME7RESERVED_MSK	\
		(0x3<<BT_UART_MSG_2_FRAME7RESERVED_POS)


#define BT_ENABLE_REDUCED_TXPOWER_THRESHOLD	(-62)
#define BT_DISABLE_REDUCED_TXPOWER_THRESHOLD	(-65)

struct iwl_bt_uart_msg {
	u8 header;
	u8 frame1;
	u8 frame2;
	u8 frame3;
	u8 frame4;
	u8 frame5;
	u8 frame6;
	u8 frame7;
} __packed;

struct iwl_bt_coex_profile_notif {
	struct iwl_bt_uart_msg last_bt_uart_msg;
	u8 bt_status; /* 0 - off, 1 - on */
	u8 bt_traffic_load; /* 0 .. 3? */
	u8 bt_ci_compliance; /* 0 - not complied, 1 - complied */
	u8 reserved;
} __packed;

#define IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_POS	0
#define IWL_BT_COEX_PRIO_TBL_SHARED_ANTENNA_MSK	0x1
#define IWL_BT_COEX_PRIO_TBL_PRIO_POS		1
#define IWL_BT_COEX_PRIO_TBL_PRIO_MASK		0x0e
#define IWL_BT_COEX_PRIO_TBL_RESERVED_POS	4
#define IWL_BT_COEX_PRIO_TBL_RESERVED_MASK	0xf0
#define IWL_BT_COEX_PRIO_TBL_PRIO_SHIFT		1

/*
 * BT Coexistence Priority table
 * REPLY_BT_COEX_PRIO_TABLE = 0xcc
 */
enum bt_coex_prio_table_events {
	BT_COEX_PRIO_TBL_EVT_INIT_CALIB1 = 0,
	BT_COEX_PRIO_TBL_EVT_INIT_CALIB2 = 1,
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_LOW1 = 2,
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_LOW2 = 3, /* DC calib */
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_HIGH1 = 4,
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_HIGH2 = 5,
	BT_COEX_PRIO_TBL_EVT_DTIM = 6,
	BT_COEX_PRIO_TBL_EVT_SCAN52 = 7,
	BT_COEX_PRIO_TBL_EVT_SCAN24 = 8,
	BT_COEX_PRIO_TBL_EVT_RESERVED0 = 9,
	BT_COEX_PRIO_TBL_EVT_RESERVED1 = 10,
	BT_COEX_PRIO_TBL_EVT_RESERVED2 = 11,
	BT_COEX_PRIO_TBL_EVT_RESERVED3 = 12,
	BT_COEX_PRIO_TBL_EVT_RESERVED4 = 13,
	BT_COEX_PRIO_TBL_EVT_RESERVED5 = 14,
	BT_COEX_PRIO_TBL_EVT_RESERVED6 = 15,
	/* BT_COEX_PRIO_TBL_EVT_MAX should always be last */
	BT_COEX_PRIO_TBL_EVT_MAX,
};

enum bt_coex_prio_table_priorities {
	BT_COEX_PRIO_TBL_DISABLED = 0,
	BT_COEX_PRIO_TBL_PRIO_LOW = 1,
	BT_COEX_PRIO_TBL_PRIO_HIGH = 2,
	BT_COEX_PRIO_TBL_PRIO_BYPASS = 3,
	BT_COEX_PRIO_TBL_PRIO_COEX_OFF = 4,
	BT_COEX_PRIO_TBL_PRIO_COEX_ON = 5,
	BT_COEX_PRIO_TBL_PRIO_RSRVD1 = 6,
	BT_COEX_PRIO_TBL_PRIO_RSRVD2 = 7,
	BT_COEX_PRIO_TBL_MAX,
};

struct iwl_bt_coex_prio_table_cmd {
	u8 prio_tbl[BT_COEX_PRIO_TBL_EVT_MAX];
} __packed;

#define IWL_BT_COEX_ENV_CLOSE	0
#define IWL_BT_COEX_ENV_OPEN	1
/*
 * BT Protection Envelope
 * REPLY_BT_COEX_PROT_ENV = 0xcd
 */
struct iwl_bt_coex_prot_env_cmd {
	u8 action; /* 0 = closed, 1 = open */
	u8 type; /* 0 .. 15 */
	u8 reserved[2];
} __packed;

/*
 * REPLY_D3_CONFIG
 */
enum iwlagn_d3_wakeup_filters {
	IWLAGN_D3_WAKEUP_RFKILL		= BIT(0),
	IWLAGN_D3_WAKEUP_SYSASSERT	= BIT(1),
};

struct iwlagn_d3_config_cmd {
	__le32 min_sleep_time;
	__le32 wakeup_flags;
} __packed;

/*
 * REPLY_WOWLAN_PATTERNS
 */
#define IWLAGN_WOWLAN_MIN_PATTERN_LEN	16
#define IWLAGN_WOWLAN_MAX_PATTERN_LEN	128

struct iwlagn_wowlan_pattern {
	u8 mask[IWLAGN_WOWLAN_MAX_PATTERN_LEN / 8];
	u8 pattern[IWLAGN_WOWLAN_MAX_PATTERN_LEN];
	u8 mask_size;
	u8 pattern_size;
	__le16 reserved;
} __packed;

#define IWLAGN_WOWLAN_MAX_PATTERNS	20

struct iwlagn_wowlan_patterns_cmd {
	__le32 n_patterns;
	struct iwlagn_wowlan_pattern patterns[];
} __packed;

/*
 * REPLY_WOWLAN_WAKEUP_FILTER
 */
enum iwlagn_wowlan_wakeup_filters {
	IWLAGN_WOWLAN_WAKEUP_MAGIC_PACKET	= BIT(0),
	IWLAGN_WOWLAN_WAKEUP_PATTERN_MATCH	= BIT(1),
	IWLAGN_WOWLAN_WAKEUP_BEACON_MISS	= BIT(2),
	IWLAGN_WOWLAN_WAKEUP_LINK_CHANGE	= BIT(3),
	IWLAGN_WOWLAN_WAKEUP_GTK_REKEY_FAIL	= BIT(4),
	IWLAGN_WOWLAN_WAKEUP_EAP_IDENT_REQ	= BIT(5),
	IWLAGN_WOWLAN_WAKEUP_4WAY_HANDSHAKE	= BIT(6),
	IWLAGN_WOWLAN_WAKEUP_ALWAYS		= BIT(7),
	IWLAGN_WOWLAN_WAKEUP_ENABLE_NET_DETECT	= BIT(8),
};

struct iwlagn_wowlan_wakeup_filter_cmd {
	__le32 enabled;
	__le16 non_qos_seq;
	__le16 reserved;
	__le16 qos_seq[8];
};

/*
 * REPLY_WOWLAN_TSC_RSC_PARAMS
 */
#define IWLAGN_NUM_RSC	16

struct tkip_sc {
	__le16 iv16;
	__le16 pad;
	__le32 iv32;
} __packed;

struct iwlagn_tkip_rsc_tsc {
	struct tkip_sc unicast_rsc[IWLAGN_NUM_RSC];
	struct tkip_sc multicast_rsc[IWLAGN_NUM_RSC];
	struct tkip_sc tsc;
} __packed;

struct aes_sc {
	__le64 pn;
} __packed;

struct iwlagn_aes_rsc_tsc {
	struct aes_sc unicast_rsc[IWLAGN_NUM_RSC];
	struct aes_sc multicast_rsc[IWLAGN_NUM_RSC];
	struct aes_sc tsc;
} __packed;

union iwlagn_all_tsc_rsc {
	struct iwlagn_tkip_rsc_tsc tkip;
	struct iwlagn_aes_rsc_tsc aes;
};

struct iwlagn_wowlan_rsc_tsc_params_cmd {
	union iwlagn_all_tsc_rsc all_tsc_rsc;
} __packed;

/*
 * REPLY_WOWLAN_TKIP_PARAMS
 */
#define IWLAGN_MIC_KEY_SIZE	8
#define IWLAGN_P1K_SIZE		5
struct iwlagn_mic_keys {
	u8 tx[IWLAGN_MIC_KEY_SIZE];
	u8 rx_unicast[IWLAGN_MIC_KEY_SIZE];
	u8 rx_mcast[IWLAGN_MIC_KEY_SIZE];
} __packed;

struct iwlagn_p1k_cache {
	__le16 p1k[IWLAGN_P1K_SIZE];
} __packed;

#define IWLAGN_NUM_RX_P1K_CACHE	2

struct iwlagn_wowlan_tkip_params_cmd {
	struct iwlagn_mic_keys mic_keys;
	struct iwlagn_p1k_cache tx;
	struct iwlagn_p1k_cache rx_uni[IWLAGN_NUM_RX_P1K_CACHE];
	struct iwlagn_p1k_cache rx_multi[IWLAGN_NUM_RX_P1K_CACHE];
} __packed;

/*
 * REPLY_WOWLAN_KEK_KCK_MATERIAL
 */

#define IWLAGN_KCK_MAX_SIZE	32
#define IWLAGN_KEK_MAX_SIZE	32

struct iwlagn_wowlan_kek_kck_material_cmd {
	u8	kck[IWLAGN_KCK_MAX_SIZE];
	u8	kek[IWLAGN_KEK_MAX_SIZE];
	__le16	kck_len;
	__le16	kek_len;
	__le64	replay_ctr;
} __packed;

#define RF_KILL_INDICATOR_FOR_WOWLAN	0x87

/*
 * REPLY_WOWLAN_GET_STATUS = 0xe5
 */
struct iwlagn_wowlan_status {
	__le64 replay_ctr;
	__le32 rekey_status;
	__le32 wakeup_reason;
	u8 pattern_number;
	u8 reserved1;
	__le16 qos_seq_ctr[8];
	__le16 non_qos_seq_ctr;
	__le16 reserved2;
	union iwlagn_all_tsc_rsc tsc_rsc;
	__le16 reserved3;
} __packed;

/*
 * REPLY_WIPAN_PARAMS = 0xb2 (Commands and Notification)
 */

/*
 * Minimum slot time in TU
 */
#define IWL_MIN_SLOT_TIME	20

/**
 * struct iwl_wipan_slot
 * @width: Time in TU
 * @type:
 *   0 - BSS
 *   1 - PAN
 */
struct iwl_wipan_slot {
	__le16 width;
	u8 type;
	u8 reserved;
} __packed;

#define IWL_WIPAN_PARAMS_FLG_LEAVE_CHANNEL_CTS		BIT(1)	/* reserved */
#define IWL_WIPAN_PARAMS_FLG_LEAVE_CHANNEL_QUIET	BIT(2)	/* reserved */
#define IWL_WIPAN_PARAMS_FLG_SLOTTED_MODE		BIT(3)	/* reserved */
#define IWL_WIPAN_PARAMS_FLG_FILTER_BEACON_NOTIF	BIT(4)
#define IWL_WIPAN_PARAMS_FLG_FULL_SLOTTED_MODE		BIT(5)

/**
 * struct iwl_wipan_params_cmd
 * @flags:
 *   bit0: reserved
 *   bit1: CP leave channel with CTS
 *   bit2: CP leave channel qith Quiet
 *   bit3: slotted mode
 *     1 - work in slotted mode
 *     0 - work in non slotted mode
 *   bit4: filter beacon notification
 *   bit5: full tx slotted mode. if this flag is set,
 *         uCode will perform leaving channel methods in context switch
 *         also when working in same channel mode
 * @num_slots: 1 - 10
 */
struct iwl_wipan_params_cmd {
	__le16 flags;
	u8 reserved;
	u8 num_slots;
	struct iwl_wipan_slot slots[10];
} __packed;

/*
 * REPLY_WIPAN_P2P_CHANNEL_SWITCH = 0xb9
 *
 * TODO: Figure out what this is used for,
 *	 it can only switch between 2.4 GHz
 *	 channels!!
 */

struct iwl_wipan_p2p_channel_switch_cmd {
	__le16 channel;
	__le16 reserved;
};

/*
 * REPLY_WIPAN_NOA_NOTIFICATION = 0xbc
 *
 * This is used by the device to notify us of the
 * NoA schedule it determined so we can forward it
 * to userspace for inclusion in probe responses.
 *
 * In beacons, the NoA schedule is simply appended
 * to the frame we give the device.
 */

struct iwl_wipan_noa_descriptor {
	u8 count;
	__le32 duration;
	__le32 interval;
	__le32 starttime;
} __packed;

struct iwl_wipan_noa_attribute {
	u8 id;
	__le16 length;
	u8 index;
	u8 ct_window;
	struct iwl_wipan_noa_descriptor descr0, descr1;
	u8 reserved;
} __packed;

struct iwl_wipan_noa_notification {
	u32 noa_active;
	struct iwl_wipan_noa_attribute noa_attribute;
} __packed;

#endif				/* __iwl_commands_h__ */

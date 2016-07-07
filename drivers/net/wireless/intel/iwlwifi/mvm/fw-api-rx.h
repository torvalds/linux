/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2016 Intel Deutschland GmbH
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

#ifndef __fw_api_rx_h__
#define __fw_api_rx_h__

/* API for pre-9000 hardware */

#define IWL_RX_INFO_PHY_CNT 8
#define IWL_RX_INFO_ENERGY_ANT_ABC_IDX 1
#define IWL_RX_INFO_ENERGY_ANT_A_MSK 0x000000ff
#define IWL_RX_INFO_ENERGY_ANT_B_MSK 0x0000ff00
#define IWL_RX_INFO_ENERGY_ANT_C_MSK 0x00ff0000
#define IWL_RX_INFO_ENERGY_ANT_A_POS 0
#define IWL_RX_INFO_ENERGY_ANT_B_POS 8
#define IWL_RX_INFO_ENERGY_ANT_C_POS 16

enum iwl_mac_context_info {
	MAC_CONTEXT_INFO_NONE,
	MAC_CONTEXT_INFO_GSCAN,
};

/**
 * struct iwl_rx_phy_info - phy info
 * (REPLY_RX_PHY_CMD = 0xc0)
 * @non_cfg_phy_cnt: non configurable DSP phy data byte count
 * @cfg_phy_cnt: configurable DSP phy data byte count
 * @stat_id: configurable DSP phy data set ID
 * @reserved1:
 * @system_timestamp: GP2  at on air rise
 * @timestamp: TSF at on air rise
 * @beacon_time_stamp: beacon at on-air rise
 * @phy_flags: general phy flags: band, modulation, ...
 * @channel: channel number
 * @non_cfg_phy_buf: for various implementations of non_cfg_phy
 * @rate_n_flags: RATE_MCS_*
 * @byte_count: frame's byte-count
 * @frame_time: frame's time on the air, based on byte count and frame rate
 *	calculation
 * @mac_active_msk: what MACs were active when the frame was received
 * @mac_context_info: additional info on the context in which the frame was
 *	received as defined in &enum iwl_mac_context_info
 *
 * Before each Rx, the device sends this data. It contains PHY information
 * about the reception of the packet.
 */
struct iwl_rx_phy_info {
	u8 non_cfg_phy_cnt;
	u8 cfg_phy_cnt;
	u8 stat_id;
	u8 reserved1;
	__le32 system_timestamp;
	__le64 timestamp;
	__le32 beacon_time_stamp;
	__le16 phy_flags;
	__le16 channel;
	__le32 non_cfg_phy[IWL_RX_INFO_PHY_CNT];
	__le32 rate_n_flags;
	__le32 byte_count;
	u8 mac_active_msk;
	u8 mac_context_info;
	__le16 frame_time;
} __packed;

/*
 * TCP offload Rx assist info
 *
 * bits 0:3 - reserved
 * bits 4:7 - MIC CRC length
 * bits 8:12 - MAC header length
 * bit 13 - Padding indication
 * bit 14 - A-AMSDU indication
 * bit 15 - Offload enabled
 */
enum iwl_csum_rx_assist_info {
	CSUM_RXA_RESERVED_MASK	= 0x000f,
	CSUM_RXA_MICSIZE_MASK	= 0x00f0,
	CSUM_RXA_HEADERLEN_MASK	= 0x1f00,
	CSUM_RXA_PADD		= BIT(13),
	CSUM_RXA_AMSDU		= BIT(14),
	CSUM_RXA_ENA		= BIT(15)
};

/**
 * struct iwl_rx_mpdu_res_start - phy info
 * @assist: see CSUM_RX_ASSIST_ above
 */
struct iwl_rx_mpdu_res_start {
	__le16 byte_count;
	__le16 assist;
} __packed; /* _RX_MPDU_RES_START_API_S_VER_2 */

/**
 * enum iwl_rx_phy_flags - to parse %iwl_rx_phy_info phy_flags
 * @RX_RES_PHY_FLAGS_BAND_24: true if the packet was received on 2.4 band
 * @RX_RES_PHY_FLAGS_MOD_CCK:
 * @RX_RES_PHY_FLAGS_SHORT_PREAMBLE: true if packet's preamble was short
 * @RX_RES_PHY_FLAGS_NARROW_BAND:
 * @RX_RES_PHY_FLAGS_ANTENNA: antenna on which the packet was received
 * @RX_RES_PHY_FLAGS_AGG: set if the packet was part of an A-MPDU
 * @RX_RES_PHY_FLAGS_OFDM_HT: The frame was an HT frame
 * @RX_RES_PHY_FLAGS_OFDM_GF: The frame used GF preamble
 * @RX_RES_PHY_FLAGS_OFDM_VHT: The frame was a VHT frame
 */
enum iwl_rx_phy_flags {
	RX_RES_PHY_FLAGS_BAND_24	= BIT(0),
	RX_RES_PHY_FLAGS_MOD_CCK	= BIT(1),
	RX_RES_PHY_FLAGS_SHORT_PREAMBLE	= BIT(2),
	RX_RES_PHY_FLAGS_NARROW_BAND	= BIT(3),
	RX_RES_PHY_FLAGS_ANTENNA	= (0x7 << 4),
	RX_RES_PHY_FLAGS_ANTENNA_POS	= 4,
	RX_RES_PHY_FLAGS_AGG		= BIT(7),
	RX_RES_PHY_FLAGS_OFDM_HT	= BIT(8),
	RX_RES_PHY_FLAGS_OFDM_GF	= BIT(9),
	RX_RES_PHY_FLAGS_OFDM_VHT	= BIT(10),
};

/**
 * enum iwl_mvm_rx_status - written by fw for each Rx packet
 * @RX_MPDU_RES_STATUS_CRC_OK: CRC is fine
 * @RX_MPDU_RES_STATUS_OVERRUN_OK: there was no RXE overflow
 * @RX_MPDU_RES_STATUS_SRC_STA_FOUND:
 * @RX_MPDU_RES_STATUS_KEY_VALID:
 * @RX_MPDU_RES_STATUS_KEY_PARAM_OK:
 * @RX_MPDU_RES_STATUS_ICV_OK: ICV is fine, if not, the packet is destroyed
 * @RX_MPDU_RES_STATUS_MIC_OK: used for CCM alg only. TKIP MIC is checked
 *	in the driver.
 * @RX_MPDU_RES_STATUS_TTAK_OK: TTAK is fine
 * @RX_MPDU_RES_STATUS_MNG_FRAME_REPLAY_ERR:  valid for alg = CCM_CMAC or
 *	alg = CCM only. Checks replay attack for 11w frames. Relevant only if
 *	%RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME is set.
 * @RX_MPDU_RES_STATUS_SEC_NO_ENC: this frame is not encrypted
 * @RX_MPDU_RES_STATUS_SEC_WEP_ENC: this frame is encrypted using WEP
 * @RX_MPDU_RES_STATUS_SEC_CCM_ENC: this frame is encrypted using CCM
 * @RX_MPDU_RES_STATUS_SEC_TKIP_ENC: this frame is encrypted using TKIP
 * @RX_MPDU_RES_STATUS_SEC_CCM_CMAC_ENC: this frame is encrypted using CCM_CMAC
 * @RX_MPDU_RES_STATUS_SEC_ENC_ERR: this frame couldn't be decrypted
 * @RX_MPDU_RES_STATUS_SEC_ENC_MSK: bitmask of the encryption algorithm
 * @RX_MPDU_RES_STATUS_DEC_DONE: this frame has been successfully decrypted
 * @RX_MPDU_RES_STATUS_PROTECT_FRAME_BIT_CMP:
 * @RX_MPDU_RES_STATUS_EXT_IV_BIT_CMP:
 * @RX_MPDU_RES_STATUS_KEY_ID_CMP_BIT:
 * @RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME: this frame is an 11w management frame
 * @RX_MPDU_RES_STATUS_CSUM_DONE: checksum was done by the hw
 * @RX_MPDU_RES_STATUS_CSUM_OK: checksum found no errors
 * @RX_MPDU_RES_STATUS_HASH_INDEX_MSK:
 * @RX_MPDU_RES_STATUS_STA_ID_MSK:
 * @RX_MPDU_RES_STATUS_RRF_KILL:
 * @RX_MPDU_RES_STATUS_FILTERING_MSK:
 * @RX_MPDU_RES_STATUS2_FILTERING_MSK:
 */
enum iwl_mvm_rx_status {
	RX_MPDU_RES_STATUS_CRC_OK			= BIT(0),
	RX_MPDU_RES_STATUS_OVERRUN_OK			= BIT(1),
	RX_MPDU_RES_STATUS_SRC_STA_FOUND		= BIT(2),
	RX_MPDU_RES_STATUS_KEY_VALID			= BIT(3),
	RX_MPDU_RES_STATUS_KEY_PARAM_OK			= BIT(4),
	RX_MPDU_RES_STATUS_ICV_OK			= BIT(5),
	RX_MPDU_RES_STATUS_MIC_OK			= BIT(6),
	RX_MPDU_RES_STATUS_TTAK_OK			= BIT(7),
	RX_MPDU_RES_STATUS_MNG_FRAME_REPLAY_ERR		= BIT(7),
	RX_MPDU_RES_STATUS_SEC_NO_ENC			= (0 << 8),
	RX_MPDU_RES_STATUS_SEC_WEP_ENC			= (1 << 8),
	RX_MPDU_RES_STATUS_SEC_CCM_ENC			= (2 << 8),
	RX_MPDU_RES_STATUS_SEC_TKIP_ENC			= (3 << 8),
	RX_MPDU_RES_STATUS_SEC_EXT_ENC			= (4 << 8),
	RX_MPDU_RES_STATUS_SEC_CCM_CMAC_ENC		= (6 << 8),
	RX_MPDU_RES_STATUS_SEC_ENC_ERR			= (7 << 8),
	RX_MPDU_RES_STATUS_SEC_ENC_MSK			= (7 << 8),
	RX_MPDU_RES_STATUS_DEC_DONE			= BIT(11),
	RX_MPDU_RES_STATUS_PROTECT_FRAME_BIT_CMP	= BIT(12),
	RX_MPDU_RES_STATUS_EXT_IV_BIT_CMP		= BIT(13),
	RX_MPDU_RES_STATUS_KEY_ID_CMP_BIT		= BIT(14),
	RX_MPDU_RES_STATUS_ROBUST_MNG_FRAME		= BIT(15),
	RX_MPDU_RES_STATUS_CSUM_DONE			= BIT(16),
	RX_MPDU_RES_STATUS_CSUM_OK			= BIT(17),
	RX_MPDU_RES_STATUS_HASH_INDEX_MSK		= (0x3F0000),
	RX_MDPU_RES_STATUS_STA_ID_SHIFT			= 24,
	RX_MPDU_RES_STATUS_STA_ID_MSK			= 0x1f << RX_MDPU_RES_STATUS_STA_ID_SHIFT,
	RX_MPDU_RES_STATUS_RRF_KILL			= BIT(29),
	RX_MPDU_RES_STATUS_FILTERING_MSK		= (0xc00000),
	RX_MPDU_RES_STATUS2_FILTERING_MSK		= (0xc0000000),
};

/* 9000 series API */
enum iwl_rx_mpdu_mac_flags1 {
	IWL_RX_MDPU_MFLG1_ADDRTYPE_MASK		= 0x03,
	IWL_RX_MPDU_MFLG1_MIC_CRC_LEN_MASK	= 0xf0,
	/* shift should be 4, but the length is measured in 2-byte
	 * words, so shifting only by 3 gives a byte result
	 */
	IWL_RX_MPDU_MFLG1_MIC_CRC_LEN_SHIFT	= 3,
};

enum iwl_rx_mpdu_mac_flags2 {
	/* in 2-byte words */
	IWL_RX_MPDU_MFLG2_HDR_LEN_MASK		= 0x1f,
	IWL_RX_MPDU_MFLG2_PAD			= 0x20,
	IWL_RX_MPDU_MFLG2_AMSDU			= 0x40,
};

enum iwl_rx_mpdu_amsdu_info {
	IWL_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK	= 0x7f,
	IWL_RX_MPDU_AMSDU_LAST_SUBFRAME		= 0x80,
};

enum iwl_rx_l3_proto_values {
	IWL_RX_L3_TYPE_NONE,
	IWL_RX_L3_TYPE_IPV4,
	IWL_RX_L3_TYPE_IPV4_FRAG,
	IWL_RX_L3_TYPE_IPV6_FRAG,
	IWL_RX_L3_TYPE_IPV6,
	IWL_RX_L3_TYPE_IPV6_IN_IPV4,
	IWL_RX_L3_TYPE_ARP,
	IWL_RX_L3_TYPE_EAPOL,
};

#define IWL_RX_L3_PROTO_POS 4

enum iwl_rx_l3l4_flags {
	IWL_RX_L3L4_IP_HDR_CSUM_OK		= BIT(0),
	IWL_RX_L3L4_TCP_UDP_CSUM_OK		= BIT(1),
	IWL_RX_L3L4_TCP_FIN_SYN_RST_PSH		= BIT(2),
	IWL_RX_L3L4_TCP_ACK			= BIT(3),
	IWL_RX_L3L4_L3_PROTO_MASK		= 0xf << IWL_RX_L3_PROTO_POS,
	IWL_RX_L3L4_L4_PROTO_MASK		= 0xf << 8,
	IWL_RX_L3L4_RSS_HASH_MASK		= 0xf << 12,
};

enum iwl_rx_mpdu_status {
	IWL_RX_MPDU_STATUS_CRC_OK		= BIT(0),
	IWL_RX_MPDU_STATUS_OVERRUN_OK		= BIT(1),
	IWL_RX_MPDU_STATUS_SRC_STA_FOUND	= BIT(2),
	IWL_RX_MPDU_STATUS_KEY_VALID		= BIT(3),
	IWL_RX_MPDU_STATUS_KEY_ERROR		= BIT(4),
	IWL_RX_MPDU_STATUS_ICV_OK		= BIT(5),
	IWL_RX_MPDU_STATUS_MIC_OK		= BIT(6),
	IWL_RX_MPDU_RES_STATUS_TTAK_OK		= BIT(7),
	IWL_RX_MPDU_STATUS_SEC_MASK		= 0x7 << 8,
	IWL_RX_MPDU_STATUS_SEC_NONE		= 0x0 << 8,
	IWL_RX_MPDU_STATUS_SEC_WEP		= 0x1 << 8,
	IWL_RX_MPDU_STATUS_SEC_CCM		= 0x2 << 8,
	IWL_RX_MPDU_STATUS_SEC_TKIP		= 0x3 << 8,
	IWL_RX_MPDU_STATUS_SEC_EXT_ENC		= 0x4 << 8,
	IWL_RX_MPDU_STATUS_SEC_GCM		= 0x5 << 8,
	IWL_RX_MPDU_STATUS_DECRYPTED		= BIT(11),
	IWL_RX_MPDU_STATUS_WEP_MATCH		= BIT(12),
	IWL_RX_MPDU_STATUS_EXT_IV_MATCH		= BIT(13),
	IWL_RX_MPDU_STATUS_KEY_ID_MATCH		= BIT(14),
	IWL_RX_MPDU_STATUS_KEY_COLOR		= BIT(15),
};

enum iwl_rx_mpdu_hash_filter {
	IWL_RX_MPDU_HF_A1_HASH_MASK		= 0x3f,
	IWL_RX_MPDU_HF_FILTER_STATUS_MASK	= 0xc0,
};

enum iwl_rx_mpdu_sta_id_flags {
	IWL_RX_MPDU_SIF_STA_ID_MASK		= 0x1f,
	IWL_RX_MPDU_SIF_RRF_ABORT		= 0x20,
	IWL_RX_MPDU_SIF_FILTER_STATUS_MASK	= 0xc0,
};

#define IWL_RX_REORDER_DATA_INVALID_BAID 0x7f

enum iwl_rx_mpdu_reorder_data {
	IWL_RX_MPDU_REORDER_NSSN_MASK		= 0x00000fff,
	IWL_RX_MPDU_REORDER_SN_MASK		= 0x00fff000,
	IWL_RX_MPDU_REORDER_SN_SHIFT		= 12,
	IWL_RX_MPDU_REORDER_BAID_MASK		= 0x7f000000,
	IWL_RX_MPDU_REORDER_BAID_SHIFT		= 24,
	IWL_RX_MPDU_REORDER_BA_OLD_SN		= 0x80000000,
};

struct iwl_rx_mpdu_desc {
	/* DW2 */
	__le16 mpdu_len;
	u8 mac_flags1;
	u8 mac_flags2;
	/* DW3 */
	u8 amsdu_info;
	__le16 reserved_for_software;
	u8 mac_phy_idx;
	/* DW4 */
	__le16 raw_csum; /* alledgedly unreliable */
	__le16 l3l4_flags;
	/* DW5 */
	__le16 status;
	u8 hash_filter;
	u8 sta_id_flags;
	/* DW6 */
	__le32 reorder_data;
	/* DW7 */
	__le32 rss_hash;
	/* DW8 */
	__le32 filter_match;
	/* DW9 */
	__le32 rate_n_flags;
	/* DW10 */
	u8 energy_a, energy_b, channel, reserved;
	/* DW11 */
	__le32 gp2_on_air_rise;
	/* DW12 & DW13 */
	__le64 tsf_on_air_rise;
} __packed;

struct iwl_frame_release {
	u8 baid;
	u8 reserved;
	__le16 nssn;
};

enum iwl_rss_hash_func_en {
	IWL_RSS_HASH_TYPE_IPV4_TCP,
	IWL_RSS_HASH_TYPE_IPV4_UDP,
	IWL_RSS_HASH_TYPE_IPV4_PAYLOAD,
	IWL_RSS_HASH_TYPE_IPV6_TCP,
	IWL_RSS_HASH_TYPE_IPV6_UDP,
	IWL_RSS_HASH_TYPE_IPV6_PAYLOAD,
};

#define IWL_RSS_HASH_KEY_CNT 10
#define IWL_RSS_INDIRECTION_TABLE_SIZE 128
#define IWL_RSS_ENABLE 1

/**
 * struct iwl_rss_config_cmd - RSS (Receive Side Scaling) configuration
 *
 * @flags: 1 - enable, 0 - disable
 * @hash_mask: Type of RSS to use. Values are from %iwl_rss_hash_func_en
 * @secret_key: 320 bit input of random key configuration from driver
 * @indirection_table: indirection table
 */
struct iwl_rss_config_cmd {
	__le32 flags;
	u8 hash_mask;
	u8 reserved[3];
	__le32 secret_key[IWL_RSS_HASH_KEY_CNT];
	u8 indirection_table[IWL_RSS_INDIRECTION_TABLE_SIZE];
} __packed; /* RSS_CONFIG_CMD_API_S_VER_1 */

#define IWL_MULTI_QUEUE_SYNC_MSG_MAX_SIZE 128
#define IWL_MULTI_QUEUE_SYNC_SENDER_POS 0
#define IWL_MULTI_QUEUE_SYNC_SENDER_MSK 0xf

/**
 * struct iwl_rxq_sync_cmd - RXQ notification trigger
 *
 * @flags: flags of the notification. bit 0:3 are the sender queue
 * @rxq_mask: rx queues to send the notification on
 * @count: number of bytes in payload, should be DWORD aligned
 * @payload: data to send to rx queues
 */
struct iwl_rxq_sync_cmd {
	__le32 flags;
	__le32 rxq_mask;
	__le32 count;
	u8 payload[];
} __packed; /* MULTI_QUEUE_DRV_SYNC_HDR_CMD_API_S_VER_1 */

/**
 * struct iwl_rxq_sync_notification - Notification triggered by RXQ
 * sync command
 *
 * @count: number of bytes in payload
 * @payload: data to send to rx queues
 */
struct iwl_rxq_sync_notification {
	__le32 count;
	u8 payload[];
} __packed; /* MULTI_QUEUE_DRV_SYNC_HDR_CMD_API_S_VER_1 */

/**
* Internal message identifier
*
* @IWL_MVM_RXQ_EMPTY: empty sync notification
* @IWL_MVM_RXQ_NOTIF_DEL_BA: notify RSS queues of delBA
*/
enum iwl_mvm_rxq_notif_type {
	IWL_MVM_RXQ_EMPTY,
	IWL_MVM_RXQ_NOTIF_DEL_BA,
};

/**
* struct iwl_mvm_internal_rxq_notif - Internal representation of the data sent
* in &iwl_rxq_sync_cmd. Should be DWORD aligned.
* FW is agnostic to the payload, so there are no endianity requirements.
*
* @type: value from &iwl_mvm_rxq_notif_type
* @sync: ctrl path is waiting for all notifications to be received
* @cookie: internal cookie to identify old notifications
* @data: payload
*/
struct iwl_mvm_internal_rxq_notif {
	u16 type;
	u16 sync;
	u32 cookie;
	u8 data[];
} __packed;

#endif /* __fw_api_rx_h__ */

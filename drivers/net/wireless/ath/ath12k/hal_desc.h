/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include "core.h"

#ifndef ATH12K_HAL_DESC_H
#define ATH12K_HAL_DESC_H

#define BUFFER_ADDR_INFO0_ADDR         GENMASK(31, 0)

#define BUFFER_ADDR_INFO1_ADDR         GENMASK(7, 0)
#define BUFFER_ADDR_INFO1_RET_BUF_MGR  GENMASK(11, 8)
#define BUFFER_ADDR_INFO1_SW_COOKIE    GENMASK(31, 12)

struct ath12k_buffer_addr {
	__le32 info0;
	__le32 info1;
} __packed;

/* ath12k_buffer_addr
 *
 * buffer_addr_31_0
 *		Address (lower 32 bits) of the MSDU buffer or MSDU_EXTENSION
 *		descriptor or Link descriptor
 *
 * buffer_addr_39_32
 *		Address (upper 8 bits) of the MSDU buffer or MSDU_EXTENSION
 *		descriptor or Link descriptor
 *
 * return_buffer_manager (RBM)
 *		Consumer: WBM
 *		Producer: SW/FW
 *		Indicates to which buffer manager the buffer or MSDU_EXTENSION
 *		descriptor or link descriptor that is being pointed to shall be
 *		returned after the frame has been processed. It is used by WBM
 *		for routing purposes.
 *
 *		Values are defined in enum %HAL_RX_BUF_RBM_
 *
 * sw_buffer_cookie
 *		Cookie field exclusively used by SW. HW ignores the contents,
 *		accept that it passes the programmed value on to other
 *		descriptors together with the physical address.
 *
 *		Field can be used by SW to for example associate the buffers
 *		physical address with the virtual address.
 *
 *		NOTE1:
 *		The three most significant bits can have a special meaning
 *		 in case this struct is embedded in a TX_MPDU_DETAILS STRUCT,
 *		and field transmit_bw_restriction is set
 *
 *		In case of NON punctured transmission:
 *		Sw_buffer_cookie[19:17] = 3'b000: 20 MHz TX only
 *		Sw_buffer_cookie[19:17] = 3'b001: 40 MHz TX only
 *		Sw_buffer_cookie[19:17] = 3'b010: 80 MHz TX only
 *		Sw_buffer_cookie[19:17] = 3'b011: 160 MHz TX only
 *		Sw_buffer_cookie[19:17] = 3'b101: 240 MHz TX only
 *		Sw_buffer_cookie[19:17] = 3'b100: 320 MHz TX only
 *		Sw_buffer_cookie[19:18] = 2'b11: reserved
 *
 *		In case of punctured transmission:
 *		Sw_buffer_cookie[19:16] = 4'b0000: pattern 0 only
 *		Sw_buffer_cookie[19:16] = 4'b0001: pattern 1 only
 *		Sw_buffer_cookie[19:16] = 4'b0010: pattern 2 only
 *		Sw_buffer_cookie[19:16] = 4'b0011: pattern 3 only
 *		Sw_buffer_cookie[19:16] = 4'b0100: pattern 4 only
 *		Sw_buffer_cookie[19:16] = 4'b0101: pattern 5 only
 *		Sw_buffer_cookie[19:16] = 4'b0110: pattern 6 only
 *		Sw_buffer_cookie[19:16] = 4'b0111: pattern 7 only
 *		Sw_buffer_cookie[19:16] = 4'b1000: pattern 8 only
 *		Sw_buffer_cookie[19:16] = 4'b1001: pattern 9 only
 *		Sw_buffer_cookie[19:16] = 4'b1010: pattern 10 only
 *		Sw_buffer_cookie[19:16] = 4'b1011: pattern 11 only
 *		Sw_buffer_cookie[19:18] = 2'b11: reserved
 *
 *		Note: a punctured transmission is indicated by the presence
 *		 of TLV TX_PUNCTURE_SETUP embedded in the scheduler TLV
 *
 *		Sw_buffer_cookie[20:17]: Tid: The TID field in the QoS control
 *		 field
 *
 *		Sw_buffer_cookie[16]: Mpdu_qos_control_valid: This field
 *		 indicates MPDUs with a QoS control field.
 *
 */

enum hal_tlv_tag {
	HAL_MACTX_CBF_START					= 0 /* 0x0 */,
	HAL_PHYRX_DATA						= 1 /* 0x1 */,
	HAL_PHYRX_CBF_DATA_RESP					= 2 /* 0x2 */,
	HAL_PHYRX_ABORT_REQUEST					= 3 /* 0x3 */,
	HAL_PHYRX_USER_ABORT_NOTIFICATION			= 4 /* 0x4 */,
	HAL_MACTX_DATA_RESP					= 5 /* 0x5 */,
	HAL_MACTX_CBF_DATA					= 6 /* 0x6 */,
	HAL_MACTX_CBF_DONE					= 7 /* 0x7 */,
	HAL_PHYRX_LMR_DATA_RESP					= 8 /* 0x8 */,
	HAL_RXPCU_TO_UCODE_START				= 9 /* 0x9 */,
	HAL_RXPCU_TO_UCODE_DELIMITER_FOR_FULL_MPDU		= 10 /* 0xa */,
	HAL_RXPCU_TO_UCODE_FULL_MPDU_DATA			= 11 /* 0xb */,
	HAL_RXPCU_TO_UCODE_FCS_STATUS				= 12 /* 0xc */,
	HAL_RXPCU_TO_UCODE_MPDU_DELIMITER			= 13 /* 0xd */,
	HAL_RXPCU_TO_UCODE_DELIMITER_FOR_MPDU_HEADER		= 14 /* 0xe */,
	HAL_RXPCU_TO_UCODE_MPDU_HEADER_DATA			= 15 /* 0xf */,
	HAL_RXPCU_TO_UCODE_END					= 16 /* 0x10 */,
	HAL_MACRX_CBF_READ_REQUEST				= 32 /* 0x20 */,
	HAL_MACRX_CBF_DATA_REQUEST				= 33 /* 0x21 */,
	HAL_MACRXXPECT_NDP_RECEPTION				= 34 /* 0x22 */,
	HAL_MACRX_FREEZE_CAPTURE_CHANNEL			= 35 /* 0x23 */,
	HAL_MACRX_NDP_TIMEOUT					= 36 /* 0x24 */,
	HAL_MACRX_ABORT_ACK					= 37 /* 0x25 */,
	HAL_MACRX_REQ_IMPLICIT_FB				= 38 /* 0x26 */,
	HAL_MACRX_CHAIN_MASK					= 39 /* 0x27 */,
	HAL_MACRX_NAP_USER					= 40 /* 0x28 */,
	HAL_MACRX_ABORT_REQUEST					= 41 /* 0x29 */,
	HAL_PHYTX_OTHER_TRANSMIT_INFO16				= 42 /* 0x2a */,
	HAL_PHYTX_ABORT_ACK					= 43 /* 0x2b */,
	HAL_PHYTX_ABORT_REQUEST					= 44 /* 0x2c */,
	HAL_PHYTX_PKT_END					= 45 /* 0x2d */,
	HAL_PHYTX_PPDU_HEADER_INFO_REQUEST			= 46 /* 0x2e */,
	HAL_PHYTX_REQUEST_CTRL_INFO				= 47 /* 0x2f */,
	HAL_PHYTX_DATA_REQUEST					= 48 /* 0x30 */,
	HAL_PHYTX_BF_CV_LOADING_DONE				= 49 /* 0x31 */,
	HAL_PHYTX_NAP_ACK					= 50 /* 0x32 */,
	HAL_PHYTX_NAP_DONE					= 51 /* 0x33 */,
	HAL_PHYTX_OFF_ACK					= 52 /* 0x34 */,
	HAL_PHYTX_ON_ACK					= 53 /* 0x35 */,
	HAL_PHYTX_SYNTH_OFF_ACK					= 54 /* 0x36 */,
	HAL_PHYTX_DEBUG16					= 55 /* 0x37 */,
	HAL_MACTX_ABORT_REQUEST					= 56 /* 0x38 */,
	HAL_MACTX_ABORT_ACK					= 57 /* 0x39 */,
	HAL_MACTX_PKT_END					= 58 /* 0x3a */,
	HAL_MACTX_PRE_PHY_DESC					= 59 /* 0x3b */,
	HAL_MACTX_BF_PARAMS_COMMON				= 60 /* 0x3c */,
	HAL_MACTX_BF_PARAMS_PER_USER				= 61 /* 0x3d */,
	HAL_MACTX_PREFETCH_CV					= 62 /* 0x3e */,
	HAL_MACTX_USER_DESC_COMMON				= 63 /* 0x3f */,
	HAL_MACTX_USER_DESC_PER_USER				= 64 /* 0x40 */,
	HAL_XAMPLE_USER_TLV_16					= 65 /* 0x41 */,
	HAL_XAMPLE_TLV_16					= 66 /* 0x42 */,
	HAL_MACTX_PHY_OFF					= 67 /* 0x43 */,
	HAL_MACTX_PHY_ON					= 68 /* 0x44 */,
	HAL_MACTX_SYNTH_OFF					= 69 /* 0x45 */,
	HAL_MACTXXPECT_CBF_COMMON				= 70 /* 0x46 */,
	HAL_MACTXXPECT_CBF_PER_USER				= 71 /* 0x47 */,
	HAL_MACTX_PHY_DESC					= 72 /* 0x48 */,
	HAL_MACTX_L_SIG_A					= 73 /* 0x49 */,
	HAL_MACTX_L_SIG_B					= 74 /* 0x4a */,
	HAL_MACTX_HT_SIG					= 75 /* 0x4b */,
	HAL_MACTX_VHT_SIG_A					= 76 /* 0x4c */,
	HAL_MACTX_VHT_SIG_B_SU20				= 77 /* 0x4d */,
	HAL_MACTX_VHT_SIG_B_SU40				= 78 /* 0x4e */,
	HAL_MACTX_VHT_SIG_B_SU80				= 79 /* 0x4f */,
	HAL_MACTX_VHT_SIG_B_SU160				= 80 /* 0x50 */,
	HAL_MACTX_VHT_SIG_B_MU20				= 81 /* 0x51 */,
	HAL_MACTX_VHT_SIG_B_MU40				= 82 /* 0x52 */,
	HAL_MACTX_VHT_SIG_B_MU80				= 83 /* 0x53 */,
	HAL_MACTX_VHT_SIG_B_MU160				= 84 /* 0x54 */,
	HAL_MACTX_SERVICE					= 85 /* 0x55 */,
	HAL_MACTX_HE_SIG_A_SU					= 86 /* 0x56 */,
	HAL_MACTX_HE_SIG_A_MU_DL				= 87 /* 0x57 */,
	HAL_MACTX_HE_SIG_A_MU_UL				= 88 /* 0x58 */,
	HAL_MACTX_HE_SIG_B1_MU					= 89 /* 0x59 */,
	HAL_MACTX_HE_SIG_B2_MU					= 90 /* 0x5a */,
	HAL_MACTX_HE_SIG_B2_OFDMA				= 91 /* 0x5b */,
	HAL_MACTX_DELETE_CV					= 92 /* 0x5c */,
	HAL_MACTX_MU_UPLINK_COMMON				= 93 /* 0x5d */,
	HAL_MACTX_MU_UPLINK_USER_SETUP				= 94 /* 0x5e */,
	HAL_MACTX_OTHER_TRANSMIT_INFO				= 95 /* 0x5f */,
	HAL_MACTX_PHY_NAP					= 96 /* 0x60 */,
	HAL_MACTX_DEBUG						= 97 /* 0x61 */,
	HAL_PHYRX_ABORT_ACK					= 98 /* 0x62 */,
	HAL_PHYRX_GENERATED_CBF_DETAILS				= 99 /* 0x63 */,
	HAL_PHYRX_RSSI_LEGACY					= 100 /* 0x64 */,
	HAL_PHYRX_RSSI_HT					= 101 /* 0x65 */,
	HAL_PHYRX_USER_INFO					= 102 /* 0x66 */,
	HAL_PHYRX_PKT_END					= 103 /* 0x67 */,
	HAL_PHYRX_DEBUG						= 104 /* 0x68 */,
	HAL_PHYRX_CBF_TRANSFER_DONE				= 105 /* 0x69 */,
	HAL_PHYRX_CBF_TRANSFER_ABORT				= 106 /* 0x6a */,
	HAL_PHYRX_L_SIG_A					= 107 /* 0x6b */,
	HAL_PHYRX_L_SIG_B					= 108 /* 0x6c */,
	HAL_PHYRX_HT_SIG					= 109 /* 0x6d */,
	HAL_PHYRX_VHT_SIG_A					= 110 /* 0x6e */,
	HAL_PHYRX_VHT_SIG_B_SU20				= 111 /* 0x6f */,
	HAL_PHYRX_VHT_SIG_B_SU40				= 112 /* 0x70 */,
	HAL_PHYRX_VHT_SIG_B_SU80				= 113 /* 0x71 */,
	HAL_PHYRX_VHT_SIG_B_SU160				= 114 /* 0x72 */,
	HAL_PHYRX_VHT_SIG_B_MU20				= 115 /* 0x73 */,
	HAL_PHYRX_VHT_SIG_B_MU40				= 116 /* 0x74 */,
	HAL_PHYRX_VHT_SIG_B_MU80				= 117 /* 0x75 */,
	HAL_PHYRX_VHT_SIG_B_MU160				= 118 /* 0x76 */,
	HAL_PHYRX_HE_SIG_A_SU					= 119 /* 0x77 */,
	HAL_PHYRX_HE_SIG_A_MU_DL				= 120 /* 0x78 */,
	HAL_PHYRX_HE_SIG_A_MU_UL				= 121 /* 0x79 */,
	HAL_PHYRX_HE_SIG_B1_MU					= 122 /* 0x7a */,
	HAL_PHYRX_HE_SIG_B2_MU					= 123 /* 0x7b */,
	HAL_PHYRX_HE_SIG_B2_OFDMA				= 124 /* 0x7c */,
	HAL_PHYRX_OTHER_RECEIVE_INFO				= 125 /* 0x7d */,
	HAL_PHYRX_COMMON_USER_INFO				= 126 /* 0x7e */,
	HAL_PHYRX_DATA_DONE					= 127 /* 0x7f */,
	HAL_COEX_TX_REQ						= 128 /* 0x80 */,
	HAL_DUMMY						= 129 /* 0x81 */,
	HALXAMPLE_TLV_32_NAME					= 130 /* 0x82 */,
	HAL_MPDU_LIMIT						= 131 /* 0x83 */,
	HAL_NA_LENGTH_END					= 132 /* 0x84 */,
	HAL_OLE_BUF_STATUS					= 133 /* 0x85 */,
	HAL_PCU_PPDU_SETUP_DONE					= 134 /* 0x86 */,
	HAL_PCU_PPDU_SETUP_END					= 135 /* 0x87 */,
	HAL_PCU_PPDU_SETUP_INIT					= 136 /* 0x88 */,
	HAL_PCU_PPDU_SETUP_START				= 137 /* 0x89 */,
	HAL_PDG_FES_SETUP					= 138 /* 0x8a */,
	HAL_PDG_RESPONSE					= 139 /* 0x8b */,
	HAL_PDG_TX_REQ						= 140 /* 0x8c */,
	HAL_SCH_WAIT_INSTR					= 141 /* 0x8d */,
	HAL_TQM_FLOWMPTY_STATUS					= 143 /* 0x8f */,
	HAL_TQM_FLOW_NOTMPTY_STATUS				= 144 /* 0x90 */,
	HAL_TQM_GEN_MPDU_LENGTH_LIST				= 145 /* 0x91 */,
	HAL_TQM_GEN_MPDU_LENGTH_LIST_STATUS			= 146 /* 0x92 */,
	HAL_TQM_GEN_MPDUS					= 147 /* 0x93 */,
	HAL_TQM_GEN_MPDUS_STATUS				= 148 /* 0x94 */,
	HAL_TQM_REMOVE_MPDU					= 149 /* 0x95 */,
	HAL_TQM_REMOVE_MPDU_STATUS				= 150 /* 0x96 */,
	HAL_TQM_REMOVE_MSDU					= 151 /* 0x97 */,
	HAL_TQM_REMOVE_MSDU_STATUS				= 152 /* 0x98 */,
	HAL_TQM_UPDATE_TX_MPDU_COUNT				= 153 /* 0x99 */,
	HAL_TQM_WRITE_CMD					= 154 /* 0x9a */,
	HAL_OFDMA_TRIGGER_DETAILS				= 155 /* 0x9b */,
	HAL_TX_DATA						= 156 /* 0x9c */,
	HAL_TX_FES_SETUP					= 157 /* 0x9d */,
	HAL_RX_PACKET						= 158 /* 0x9e */,
	HALXPECTED_RESPONSE					= 159 /* 0x9f */,
	HAL_TX_MPDU_END						= 160 /* 0xa0 */,
	HAL_TX_MPDU_START					= 161 /* 0xa1 */,
	HAL_TX_MSDU_END						= 162 /* 0xa2 */,
	HAL_TX_MSDU_START					= 163 /* 0xa3 */,
	HAL_TX_SW_MODE_SETUP					= 164 /* 0xa4 */,
	HAL_TXPCU_BUFFER_STATUS					= 165 /* 0xa5 */,
	HAL_TXPCU_USER_BUFFER_STATUS				= 166 /* 0xa6 */,
	HAL_DATA_TO_TIME_CONFIG					= 167 /* 0xa7 */,
	HALXAMPLE_USER_TLV_32					= 168 /* 0xa8 */,
	HAL_MPDU_INFO						= 169 /* 0xa9 */,
	HAL_PDG_USER_SETUP					= 170 /* 0xaa */,
	HAL_TX_11AH_SETUP					= 171 /* 0xab */,
	HAL_REO_UPDATE_RX_REO_QUEUE_STATUS			= 172 /* 0xac */,
	HAL_TX_PEER_ENTRY					= 173 /* 0xad */,
	HAL_TX_RAW_OR_NATIVE_FRAME_SETUP			= 174 /* 0xae */,
	HALXAMPLE_USER_TLV_44					= 175 /* 0xaf */,
	HAL_TX_FLUSH						= 176 /* 0xb0 */,
	HAL_TX_FLUSH_REQ					= 177 /* 0xb1 */,
	HAL_TQM_WRITE_CMD_STATUS				= 178 /* 0xb2 */,
	HAL_TQM_GET_MPDU_QUEUE_STATS				= 179 /* 0xb3 */,
	HAL_TQM_GET_MSDU_FLOW_STATS				= 180 /* 0xb4 */,
	HALXAMPLE_USER_CTLV_44					= 181 /* 0xb5 */,
	HAL_TX_FES_STATUS_START					= 182 /* 0xb6 */,
	HAL_TX_FES_STATUS_USER_PPDU				= 183 /* 0xb7 */,
	HAL_TX_FES_STATUS_USER_RESPONSE				= 184 /* 0xb8 */,
	HAL_TX_FES_STATUS_END					= 185 /* 0xb9 */,
	HAL_RX_TRIG_INFO					= 186 /* 0xba */,
	HAL_RXPCU_TX_SETUP_CLEAR				= 187 /* 0xbb */,
	HAL_RX_FRAME_BITMAP_REQ					= 188 /* 0xbc */,
	HAL_RX_FRAME_BITMAP_ACK					= 189 /* 0xbd */,
	HAL_COEX_RX_STATUS					= 190 /* 0xbe */,
	HAL_RX_START_PARAM					= 191 /* 0xbf */,
	HAL_RX_PPDU_START					= 192 /* 0xc0 */,
	HAL_RX_PPDU_END						= 193 /* 0xc1 */,
	HAL_RX_MPDU_START					= 194 /* 0xc2 */,
	HAL_RX_MPDU_END						= 195 /* 0xc3 */,
	HAL_RX_MSDU_START					= 196 /* 0xc4 */,
	HAL_RX_MSDU_END						= 197 /* 0xc5 */,
	HAL_RX_ATTENTION					= 198 /* 0xc6 */,
	HAL_RECEIVED_RESPONSE_INFO				= 199 /* 0xc7 */,
	HAL_RX_PHY_SLEEP					= 200 /* 0xc8 */,
	HAL_RX_HEADER						= 201 /* 0xc9 */,
	HAL_RX_PEER_ENTRY					= 202 /* 0xca */,
	HAL_RX_FLUSH						= 203 /* 0xcb */,
	HAL_RX_RESPONSE_REQUIRED_INFO				= 204 /* 0xcc */,
	HAL_RX_FRAMELESS_BAR_DETAILS				= 205 /* 0xcd */,
	HAL_TQM_GET_MPDU_QUEUE_STATS_STATUS			= 206 /* 0xce */,
	HAL_TQM_GET_MSDU_FLOW_STATS_STATUS			= 207 /* 0xcf */,
	HAL_TX_CBF_INFO						= 208 /* 0xd0 */,
	HAL_PCU_PPDU_SETUP_USER					= 209 /* 0xd1 */,
	HAL_RX_MPDU_PCU_START					= 210 /* 0xd2 */,
	HAL_RX_PM_INFO						= 211 /* 0xd3 */,
	HAL_RX_USER_PPDU_END					= 212 /* 0xd4 */,
	HAL_RX_PRE_PPDU_START					= 213 /* 0xd5 */,
	HAL_RX_PREAMBLE						= 214 /* 0xd6 */,
	HAL_TX_FES_SETUP_COMPLETE				= 215 /* 0xd7 */,
	HAL_TX_LAST_MPDU_FETCHED				= 216 /* 0xd8 */,
	HAL_TXDMA_STOP_REQUEST					= 217 /* 0xd9 */,
	HAL_RXPCU_SETUP						= 218 /* 0xda */,
	HAL_RXPCU_USER_SETUP					= 219 /* 0xdb */,
	HAL_TX_FES_STATUS_ACK_OR_BA				= 220 /* 0xdc */,
	HAL_TQM_ACKED_MPDU					= 221 /* 0xdd */,
	HAL_COEX_TX_RESP					= 222 /* 0xde */,
	HAL_COEX_TX_STATUS					= 223 /* 0xdf */,
	HAL_MACTX_COEX_PHY_CTRL					= 224 /* 0xe0 */,
	HAL_COEX_STATUS_BROADCAST				= 225 /* 0xe1 */,
	HAL_RESPONSE_START_STATUS				= 226 /* 0xe2 */,
	HAL_RESPONSEND_STATUS					= 227 /* 0xe3 */,
	HAL_CRYPTO_STATUS					= 228 /* 0xe4 */,
	HAL_RECEIVED_TRIGGER_INFO				= 229 /* 0xe5 */,
	HAL_COEX_TX_STOP_CTRL					= 230 /* 0xe6 */,
	HAL_RX_PPDU_ACK_REPORT					= 231 /* 0xe7 */,
	HAL_RX_PPDU_NO_ACK_REPORT				= 232 /* 0xe8 */,
	HAL_SCH_COEX_STATUS					= 233 /* 0xe9 */,
	HAL_SCHEDULER_COMMAND_STATUS				= 234 /* 0xea */,
	HAL_SCHEDULER_RX_PPDU_NO_RESPONSE_STATUS		= 235 /* 0xeb */,
	HAL_TX_FES_STATUS_PROT					= 236 /* 0xec */,
	HAL_TX_FES_STATUS_START_PPDU				= 237 /* 0xed */,
	HAL_TX_FES_STATUS_START_PROT				= 238 /* 0xee */,
	HAL_TXPCU_PHYTX_DEBUG32					= 239 /* 0xef */,
	HAL_TXPCU_PHYTX_OTHER_TRANSMIT_INFO32			= 240 /* 0xf0 */,
	HAL_TX_MPDU_COUNT_TRANSFERND				= 241 /* 0xf1 */,
	HAL_WHO_ANCHOR_OFFSET					= 242 /* 0xf2 */,
	HAL_WHO_ANCHOR_VALUE					= 243 /* 0xf3 */,
	HAL_WHO_CCE_INFO					= 244 /* 0xf4 */,
	HAL_WHO_COMMIT						= 245 /* 0xf5 */,
	HAL_WHO_COMMIT_DONE					= 246 /* 0xf6 */,
	HAL_WHO_FLUSH						= 247 /* 0xf7 */,
	HAL_WHO_L2_LLC						= 248 /* 0xf8 */,
	HAL_WHO_L2_PAYLOAD					= 249 /* 0xf9 */,
	HAL_WHO_L3_CHECKSUM					= 250 /* 0xfa */,
	HAL_WHO_L3_INFO						= 251 /* 0xfb */,
	HAL_WHO_L4_CHECKSUM					= 252 /* 0xfc */,
	HAL_WHO_L4_INFO						= 253 /* 0xfd */,
	HAL_WHO_MSDU						= 254 /* 0xfe */,
	HAL_WHO_MSDU_MISC					= 255 /* 0xff */,
	HAL_WHO_PACKET_DATA					= 256 /* 0x100 */,
	HAL_WHO_PACKET_HDR					= 257 /* 0x101 */,
	HAL_WHO_PPDU_END					= 258 /* 0x102 */,
	HAL_WHO_PPDU_START					= 259 /* 0x103 */,
	HAL_WHO_TSO						= 260 /* 0x104 */,
	HAL_WHO_WMAC_HEADER_PV0					= 261 /* 0x105 */,
	HAL_WHO_WMAC_HEADER_PV1					= 262 /* 0x106 */,
	HAL_WHO_WMAC_IV						= 263 /* 0x107 */,
	HAL_MPDU_INFO_END					= 264 /* 0x108 */,
	HAL_MPDU_INFO_BITMAP					= 265 /* 0x109 */,
	HAL_TX_QUEUE_EXTENSION					= 266 /* 0x10a */,
	HAL_SCHEDULER_SELFGEN_RESPONSE_STATUS			= 267 /* 0x10b */,
	HAL_TQM_UPDATE_TX_MPDU_COUNT_STATUS			= 268 /* 0x10c */,
	HAL_TQM_ACKED_MPDU_STATUS				= 269 /* 0x10d */,
	HAL_TQM_ADD_MSDU_STATUS					= 270 /* 0x10e */,
	HAL_TQM_LIST_GEN_DONE					= 271 /* 0x10f */,
	HAL_WHO_TERMINATE					= 272 /* 0x110 */,
	HAL_TX_LAST_MPDU_END					= 273 /* 0x111 */,
	HAL_TX_CV_DATA						= 274 /* 0x112 */,
	HAL_PPDU_TX_END						= 275 /* 0x113 */,
	HAL_PROT_TX_END						= 276 /* 0x114 */,
	HAL_MPDU_INFO_GLOBAL_END				= 277 /* 0x115 */,
	HAL_TQM_SCH_INSTR_GLOBAL_END				= 278 /* 0x116 */,
	HAL_RX_PPDU_END_USER_STATS				= 279 /* 0x117 */,
	HAL_RX_PPDU_END_USER_STATS_EXT				= 280 /* 0x118 */,
	HAL_REO_GET_QUEUE_STATS					= 281 /* 0x119 */,
	HAL_REO_FLUSH_QUEUE					= 282 /* 0x11a */,
	HAL_REO_FLUSH_CACHE					= 283 /* 0x11b */,
	HAL_REO_UNBLOCK_CACHE					= 284 /* 0x11c */,
	HAL_REO_GET_QUEUE_STATS_STATUS				= 285 /* 0x11d */,
	HAL_REO_FLUSH_QUEUE_STATUS				= 286 /* 0x11e */,
	HAL_REO_FLUSH_CACHE_STATUS				= 287 /* 0x11f */,
	HAL_REO_UNBLOCK_CACHE_STATUS				= 288 /* 0x120 */,
	HAL_TQM_FLUSH_CACHE					= 289 /* 0x121 */,
	HAL_TQM_UNBLOCK_CACHE					= 290 /* 0x122 */,
	HAL_TQM_FLUSH_CACHE_STATUS				= 291 /* 0x123 */,
	HAL_TQM_UNBLOCK_CACHE_STATUS				= 292 /* 0x124 */,
	HAL_RX_PPDU_END_STATUS_DONE				= 293 /* 0x125 */,
	HAL_RX_STATUS_BUFFER_DONE				= 294 /* 0x126 */,
	HAL_TX_DATA_SYNC					= 297 /* 0x129 */,
	HAL_PHYRX_CBF_READ_REQUEST_ACK				= 298 /* 0x12a */,
	HAL_TQM_GET_MPDU_HEAD_INFO				= 299 /* 0x12b */,
	HAL_TQM_SYNC_CMD					= 300 /* 0x12c */,
	HAL_TQM_GET_MPDU_HEAD_INFO_STATUS			= 301 /* 0x12d */,
	HAL_TQM_SYNC_CMD_STATUS					= 302 /* 0x12e */,
	HAL_TQM_THRESHOLD_DROP_NOTIFICATION_STATUS		= 303 /* 0x12f */,
	HAL_TQM_DESCRIPTOR_THRESHOLD_REACHED_STATUS		= 304 /* 0x130 */,
	HAL_REO_FLUSH_TIMEOUT_LIST				= 305 /* 0x131 */,
	HAL_REO_FLUSH_TIMEOUT_LIST_STATUS			= 306 /* 0x132 */,
	HAL_REO_DESCRIPTOR_THRESHOLD_REACHED_STATUS		= 307 /* 0x133 */,
	HAL_SCHEDULER_RX_SIFS_RESPONSE_TRIGGER_STATUS		= 308 /* 0x134 */,
	HALXAMPLE_USER_TLV_32_NAME				= 309 /* 0x135 */,
	HAL_RX_PPDU_START_USER_INFO				= 310 /* 0x136 */,
	HAL_RX_RING_MASK					= 311 /* 0x137 */,
	HAL_COEX_MAC_NAP					= 312 /* 0x138 */,
	HAL_RXPCU_PPDU_END_INFO					= 313 /* 0x139 */,
	HAL_WHO_MESH_CONTROL					= 314 /* 0x13a */,
	HAL_PDG_SW_MODE_BW_START				= 315 /* 0x13b */,
	HAL_PDG_SW_MODE_BW_END					= 316 /* 0x13c */,
	HAL_PDG_WAIT_FOR_MAC_REQUEST				= 317 /* 0x13d */,
	HAL_PDG_WAIT_FOR_PHY_REQUEST				= 318 /* 0x13e */,
	HAL_SCHEDULER_END					= 319 /* 0x13f */,
	HAL_RX_PPDU_START_DROPPED				= 320 /* 0x140 */,
	HAL_RX_PPDU_END_DROPPED					= 321 /* 0x141 */,
	HAL_RX_PPDU_END_STATUS_DONE_DROPPED			= 322 /* 0x142 */,
	HAL_RX_MPDU_START_DROPPED				= 323 /* 0x143 */,
	HAL_RX_MSDU_START_DROPPED				= 324 /* 0x144 */,
	HAL_RX_MSDU_END_DROPPED					= 325 /* 0x145 */,
	HAL_RX_MPDU_END_DROPPED					= 326 /* 0x146 */,
	HAL_RX_ATTENTION_DROPPED				= 327 /* 0x147 */,
	HAL_TXPCU_USER_SETUP					= 328 /* 0x148 */,
	HAL_RXPCU_USER_SETUP_EXT				= 329 /* 0x149 */,
	HAL_CMD_PART_0_END					= 330 /* 0x14a */,
	HAL_MACTX_SYNTH_ON					= 331 /* 0x14b */,
	HAL_SCH_CRITICAL_TLV_REFERENCE				= 332 /* 0x14c */,
	HAL_TQM_MPDU_GLOBAL_START				= 333 /* 0x14d */,
	HALXAMPLE_TLV_32					= 334 /* 0x14e */,
	HAL_TQM_UPDATE_TX_MSDU_FLOW				= 335 /* 0x14f */,
	HAL_TQM_UPDATE_TX_MPDU_QUEUE_HEAD			= 336 /* 0x150 */,
	HAL_TQM_UPDATE_TX_MSDU_FLOW_STATUS			= 337 /* 0x151 */,
	HAL_TQM_UPDATE_TX_MPDU_QUEUE_HEAD_STATUS		= 338 /* 0x152 */,
	HAL_REO_UPDATE_RX_REO_QUEUE				= 339 /* 0x153 */,
	HAL_TQM_MPDU_QUEUEMPTY_STATUS				= 340 /* 0x154 */,
	HAL_TQM_2_SCH_MPDU_AVAILABLE				= 341 /* 0x155 */,
	HAL_PDG_TRIG_RESPONSE					= 342 /* 0x156 */,
	HAL_TRIGGER_RESPONSE_TX_DONE				= 343 /* 0x157 */,
	HAL_ABORT_FROM_PHYRX_DETAILS				= 344 /* 0x158 */,
	HAL_SCH_TQM_CMD_WRAPPER					= 345 /* 0x159 */,
	HAL_MPDUS_AVAILABLE					= 346 /* 0x15a */,
	HAL_RECEIVED_RESPONSE_INFO_PART2			= 347 /* 0x15b */,
	HAL_PHYRX_TX_START_TIMING				= 348 /* 0x15c */,
	HAL_TXPCU_PREAMBLE_DONE					= 349 /* 0x15d */,
	HAL_NDP_PREAMBLE_DONE					= 350 /* 0x15e */,
	HAL_SCH_TQM_CMD_WRAPPER_RBO_DROP			= 351 /* 0x15f */,
	HAL_SCH_TQM_CMD_WRAPPER_CONT_DROP			= 352 /* 0x160 */,
	HAL_MACTX_CLEAR_PREV_TX_INFO				= 353 /* 0x161 */,
	HAL_TX_PUNCTURE_SETUP					= 354 /* 0x162 */,
	HAL_R2R_STATUS_END					= 355 /* 0x163 */,
	HAL_MACTX_PREFETCH_CV_COMMON				= 356 /* 0x164 */,
	HAL_END_OF_FLUSH_MARKER					= 357 /* 0x165 */,
	HAL_MACTX_MU_UPLINK_COMMON_PUNC				= 358 /* 0x166 */,
	HAL_MACTX_MU_UPLINK_USER_SETUP_PUNC			= 359 /* 0x167 */,
	HAL_RECEIVED_RESPONSE_USER_7_0				= 360 /* 0x168 */,
	HAL_RECEIVED_RESPONSE_USER_15_8				= 361 /* 0x169 */,
	HAL_RECEIVED_RESPONSE_USER_23_16			= 362 /* 0x16a */,
	HAL_RECEIVED_RESPONSE_USER_31_24			= 363 /* 0x16b */,
	HAL_RECEIVED_RESPONSE_USER_36_32			= 364 /* 0x16c */,
	HAL_TX_LOOPBACK_SETUP					= 365 /* 0x16d */,
	HAL_PHYRX_OTHER_RECEIVE_INFO_RU_DETAILS			= 366 /* 0x16e */,
	HAL_SCH_WAIT_INSTR_TX_PATH				= 367 /* 0x16f */,
	HAL_MACTX_OTHER_TRANSMIT_INFO_TX2TX			= 368 /* 0x170 */,
	HAL_MACTX_OTHER_TRANSMIT_INFOMUPHY_SETUP		= 369 /* 0x171 */,
	HAL_PHYRX_OTHER_RECEIVE_INFOVM_DETAILS			= 370 /* 0x172 */,
	HAL_TX_WUR_DATA						= 371 /* 0x173 */,
	HAL_RX_PPDU_END_START					= 372 /* 0x174 */,
	HAL_RX_PPDU_END_MIDDLE					= 373 /* 0x175 */,
	HAL_RX_PPDU_END_LAST					= 374 /* 0x176 */,
	HAL_MACTX_BACKOFF_BASED_TRANSMISSION			= 375 /* 0x177 */,
	HAL_MACTX_OTHER_TRANSMIT_INFO_DL_OFDMA_TX		= 376 /* 0x178 */,
	HAL_SRP_INFO						= 377 /* 0x179 */,
	HAL_OBSS_SR_INFO					= 378 /* 0x17a */,
	HAL_SCHEDULER_SW_MSG_STATUS				= 379 /* 0x17b */,
	HAL_HWSCH_RXPCU_MAC_INFO_ANNOUNCEMENT			= 380 /* 0x17c */,
	HAL_RXPCU_SETUP_COMPLETE				= 381 /* 0x17d */,
	HAL_SNOOP_PPDU_START					= 382 /* 0x17e */,
	HAL_SNOOP_MPDU_USR_DBG_INFO				= 383 /* 0x17f */,
	HAL_SNOOP_MSDU_USR_DBG_INFO				= 384 /* 0x180 */,
	HAL_SNOOP_MSDU_USR_DATA					= 385 /* 0x181 */,
	HAL_SNOOP_MPDU_USR_STAT_INFO				= 386 /* 0x182 */,
	HAL_SNOOP_PPDU_END					= 387 /* 0x183 */,
	HAL_SNOOP_SPARE						= 388 /* 0x184 */,
	HAL_PHYRX_OTHER_RECEIVE_INFO_MU_RSSI_COMMON		= 390 /* 0x186 */,
	HAL_PHYRX_OTHER_RECEIVE_INFO_MU_RSSI_USER		= 391 /* 0x187 */,
	HAL_MACTX_OTHER_TRANSMIT_INFO_SCH_DETAILS		= 392 /* 0x188 */,
	HAL_PHYRX_OTHER_RECEIVE_INFO_108PVM_DETAILS		= 393 /* 0x189 */,
	HAL_SCH_TLV_WRAPPER					= 394 /* 0x18a */,
	HAL_SCHEDULER_STATUS_WRAPPER				= 395 /* 0x18b */,
	HAL_MPDU_INFO_6X					= 396 /* 0x18c */,
	HAL_MACTX_11AZ_USER_DESC_PER_USER			= 397 /* 0x18d */,
	HAL_MACTX_U_SIGHT_SU_MU					= 398 /* 0x18e */,
	HAL_MACTX_U_SIGHT_TB					= 399 /* 0x18f */,
	HAL_PHYRX_U_SIGHT_SU_MU					= 403 /* 0x193 */,
	HAL_PHYRX_U_SIGHT_TB					= 404 /* 0x194 */,
	HAL_MACRX_LMR_READ_REQUEST				= 408 /* 0x198 */,
	HAL_MACRX_LMR_DATA_REQUEST				= 409 /* 0x199 */,
	HAL_PHYRX_LMR_TRANSFER_DONE				= 410 /* 0x19a */,
	HAL_PHYRX_LMR_TRANSFER_ABORT				= 411 /* 0x19b */,
	HAL_PHYRX_LMR_READ_REQUEST_ACK				= 412 /* 0x19c */,
	HAL_MACRX_SECURE_LTF_SEQ_PTR				= 413 /* 0x19d */,
	HAL_PHYRX_USER_INFO_MU_UL				= 414 /* 0x19e */,
	HAL_MPDU_QUEUE_OVERVIEW					= 415 /* 0x19f */,
	HAL_SCHEDULER_NAV_INFO					= 416 /* 0x1a0 */,
	HAL_LMR_PEER_ENTRY					= 418 /* 0x1a2 */,
	HAL_LMR_MPDU_START					= 419 /* 0x1a3 */,
	HAL_LMR_DATA						= 420 /* 0x1a4 */,
	HAL_LMR_MPDU_END					= 421 /* 0x1a5 */,
	HAL_REO_GET_QUEUE_1K_STATS_STATUS			= 422 /* 0x1a6 */,
	HAL_RX_FRAME_1K_BITMAP_ACK				= 423 /* 0x1a7 */,
	HAL_TX_FES_STATUS_1K_BA					= 424 /* 0x1a8 */,
	HAL_TQM_ACKED_1K_MPDU					= 425 /* 0x1a9 */,
	HAL_MACRX_INBSS_OBSS_IND				= 426 /* 0x1aa */,
	HAL_PHYRX_LOCATION					= 427 /* 0x1ab */,
	HAL_MLO_TX_NOTIFICATION_SU				= 428 /* 0x1ac */,
	HAL_MLO_TX_NOTIFICATION_MU				= 429 /* 0x1ad */,
	HAL_MLO_TX_REQ_SU					= 430 /* 0x1ae */,
	HAL_MLO_TX_REQ_MU					= 431 /* 0x1af */,
	HAL_MLO_TX_RESP						= 432 /* 0x1b0 */,
	HAL_MLO_RX_NOTIFICATION					= 433 /* 0x1b1 */,
	HAL_MLO_BKOFF_TRUNC_REQ					= 434 /* 0x1b2 */,
	HAL_MLO_TBTT_NOTIFICATION				= 435 /* 0x1b3 */,
	HAL_MLO_MESSAGE						= 436 /* 0x1b4 */,
	HAL_MLO_TS_SYNC_MSG					= 437 /* 0x1b5 */,
	HAL_MLO_FES_SETUP					= 438 /* 0x1b6 */,
	HAL_MLO_PDG_FES_SETUP_SU				= 439 /* 0x1b7 */,
	HAL_MLO_PDG_FES_SETUP_MU				= 440 /* 0x1b8 */,
	HAL_MPDU_INFO_1K_BITMAP					= 441 /* 0x1b9 */,
	HAL_MON_BUF_ADDR					= 442 /* 0x1ba */,
	HAL_TX_FRAG_STATE					= 443 /* 0x1bb */,
	HAL_MACTXHT_SIG_USR_OFDMA				= 446 /* 0x1be */,
	HAL_PHYRXHT_SIG_CMN_PUNC				= 448 /* 0x1c0 */,
	HAL_PHYRXHT_SIG_CMN_OFDMA				= 450 /* 0x1c2 */,
	HAL_PHYRXHT_SIG_USR_OFDMA				= 454 /* 0x1c6 */,
	HAL_PHYRX_PKT_END_PART1					= 456 /* 0x1c8 */,
	HAL_MACTXXPECT_NDP_RECEPTION				= 457 /* 0x1c9 */,
	HAL_MACTX_SECURE_LTF_SEQ_PTR				= 458 /* 0x1ca */,
	HAL_MLO_PDG_BKOFF_TRUNC_NOTIFY				= 460 /* 0x1cc */,
	HAL_PHYRX_11AZ_INTEGRITY_DATA				= 461 /* 0x1cd */,
	HAL_PHYTX_LOCATION					= 462 /* 0x1ce */,
	HAL_PHYTX_11AZ_INTEGRITY_DATA				= 463 /* 0x1cf */,
	HAL_MACTXHT_SIG_USR_SU					= 466 /* 0x1d2 */,
	HAL_MACTXHT_SIG_USR_MU_MIMO				= 467 /* 0x1d3 */,
	HAL_PHYRXHT_SIG_USR_SU					= 468 /* 0x1d4 */,
	HAL_PHYRXHT_SIG_USR_MU_MIMO				= 469 /* 0x1d5 */,
	HAL_PHYRX_GENERIC_U_SIG					= 470 /* 0x1d6 */,
	HAL_PHYRX_GENERIC_EHT_SIG				= 471 /* 0x1d7 */,
	HAL_OVERWRITE_RESP_START				= 472 /* 0x1d8 */,
	HAL_OVERWRITE_RESP_PREAMBLE_INFO			= 473 /* 0x1d9 */,
	HAL_OVERWRITE_RESP_FRAME_INFO				= 474 /* 0x1da */,
	HAL_OVERWRITE_RESP_END					= 475 /* 0x1db */,
	HAL_RXPCUARLY_RX_INDICATION				= 476 /* 0x1dc */,
	HAL_MON_DROP						= 477 /* 0x1dd */,
	HAL_MACRX_MU_UPLINK_COMMON_SNIFF			= 478 /* 0x1de */,
	HAL_MACRX_MU_UPLINK_USER_SETUP_SNIFF			= 479 /* 0x1df */,
	HAL_MACRX_MU_UPLINK_USER_SEL_SNIFF			= 480 /* 0x1e0 */,
	HAL_MACRX_MU_UPLINK_FCS_STATUS_SNIFF			= 481 /* 0x1e1 */,
	HAL_MACTX_PREFETCH_CV_DMA				= 482 /* 0x1e2 */,
	HAL_MACTX_PREFETCH_CV_PER_USER				= 483 /* 0x1e3 */,
	HAL_PHYRX_OTHER_RECEIVE_INFO_ALL_SIGB_DETAILS		= 484 /* 0x1e4 */,
	HAL_MACTX_BF_PARAMS_UPDATE_COMMON			= 485 /* 0x1e5 */,
	HAL_MACTX_BF_PARAMS_UPDATE_PER_USER			= 486 /* 0x1e6 */,
	HAL_RANGING_USER_DETAILS				= 487 /* 0x1e7 */,
	HAL_PHYTX_CV_CORR_STATUS				= 488 /* 0x1e8 */,
	HAL_PHYTX_CV_CORR_COMMON				= 489 /* 0x1e9 */,
	HAL_PHYTX_CV_CORR_USER					= 490 /* 0x1ea */,
	HAL_MACTX_CV_CORR_COMMON				= 491 /* 0x1eb */,
	HAL_MACTX_CV_CORR_MAC_INFO_GROUP			= 492 /* 0x1ec */,
	HAL_BW_PUNCTUREVAL_WRAPPER				= 493 /* 0x1ed */,
	HAL_MACTX_RX_NOTIFICATION_FOR_PHY			= 494 /* 0x1ee */,
	HAL_MACTX_TX_NOTIFICATION_FOR_PHY			= 495 /* 0x1ef */,
	HAL_MACTX_MU_UPLINK_COMMON_PER_BW			= 496 /* 0x1f0 */,
	HAL_MACTX_MU_UPLINK_USER_SETUP_PER_BW			= 497 /* 0x1f1 */,
	HAL_RX_PPDU_END_USER_STATS_EXT2				= 498 /* 0x1f2 */,
	HAL_FW2SW_MON						= 499 /* 0x1f3 */,
	HAL_WSI_DIRECT_MESSAGE					= 500 /* 0x1f4 */,
	HAL_MACTXMLSR_PRE_SWITCH				= 501 /* 0x1f5 */,
	HAL_MACTXMLSR_SWITCH					= 502 /* 0x1f6 */,
	HAL_MACTXMLSR_SWITCH_BACK				= 503 /* 0x1f7 */,
	HAL_PHYTXMLSR_SWITCH_ACK				= 504 /* 0x1f8 */,
	HAL_PHYTXMLSR_SWITCH_BACK_ACK				= 505 /* 0x1f9 */,
	HAL_SPARE_REUSE_TAG_0					= 506 /* 0x1fa */,
	HAL_SPARE_REUSE_TAG_1					= 507 /* 0x1fb */,
	HAL_SPARE_REUSE_TAG_2					= 508 /* 0x1fc */,
	HAL_SPARE_REUSE_TAG_3					= 509 /* 0x1fd */,
	/* FIXME: Assign correct value for HAL_TCL_DATA_CMD */
	HAL_TCL_DATA_CMD					= 510,
	HAL_TLV_BASE						= 511 /* 0x1ff */,
};

#define HAL_TLV_HDR_TAG		GENMASK(9, 1)
#define HAL_TLV_HDR_LEN		GENMASK(25, 10)
#define HAL_TLV_USR_ID          GENMASK(31, 26)

#define HAL_TLV_ALIGN	4

struct hal_tlv_hdr {
	__le32 tl;
	u8 value[];
} __packed;

#define HAL_TLV_64_HDR_TAG		GENMASK(9, 1)
#define HAL_TLV_64_HDR_LEN		GENMASK(21, 10)
#define HAL_TLV_64_USR_ID		GENMASK(31, 26)
#define HAL_TLV_64_ALIGN		8

struct hal_tlv_64_hdr {
	__le64 tl;
	u8 value[];
} __packed;

#define RX_MPDU_DESC_INFO0_MSDU_COUNT		GENMASK(7, 0)
#define RX_MPDU_DESC_INFO0_FRAG_FLAG		BIT(8)
#define RX_MPDU_DESC_INFO0_MPDU_RETRY		BIT(9)
#define RX_MPDU_DESC_INFO0_AMPDU_FLAG		BIT(10)
#define RX_MPDU_DESC_INFO0_BAR_FRAME		BIT(11)
#define RX_MPDU_DESC_INFO0_VALID_PN		BIT(12)
#define RX_MPDU_DESC_INFO0_RAW_MPDU		BIT(13)
#define RX_MPDU_DESC_INFO0_MORE_FRAG_FLAG	BIT(14)
#define RX_MPDU_DESC_INFO0_SRC_INFO		GENMASK(26, 15)
#define RX_MPDU_DESC_INFO0_MPDU_QOS_CTRL_VALID	BIT(27)
#define RX_MPDU_DESC_INFO0_TID			GENMASK(31, 28)

/* Peer Metadata classification */

/* Version 0 */
#define RX_MPDU_DESC_META_DATA_V0_PEER_ID	GENMASK(15, 0)
#define RX_MPDU_DESC_META_DATA_V0_VDEV_ID	GENMASK(23, 16)

/* Version 1 */
#define RX_MPDU_DESC_META_DATA_V1_PEER_ID		GENMASK(13, 0)
#define RX_MPDU_DESC_META_DATA_V1_LOGICAL_LINK_ID	GENMASK(15, 14)
#define RX_MPDU_DESC_META_DATA_V1_VDEV_ID		GENMASK(23, 16)
#define RX_MPDU_DESC_META_DATA_V1_LMAC_ID		GENMASK(25, 24)
#define RX_MPDU_DESC_META_DATA_V1_DEVICE_ID		GENMASK(28, 26)

/* Version 1A */
#define RX_MPDU_DESC_META_DATA_V1A_PEER_ID		GENMASK(13, 0)
#define RX_MPDU_DESC_META_DATA_V1A_VDEV_ID		GENMASK(21, 14)
#define RX_MPDU_DESC_META_DATA_V1A_LOGICAL_LINK_ID	GENMASK(25, 22)
#define RX_MPDU_DESC_META_DATA_V1A_DEVICE_ID		GENMASK(28, 26)

/* Version 1B */
#define RX_MPDU_DESC_META_DATA_V1B_PEER_ID	GENMASK(13, 0)
#define RX_MPDU_DESC_META_DATA_V1B_VDEV_ID	GENMASK(21, 14)
#define RX_MPDU_DESC_META_DATA_V1B_HW_LINK_ID	GENMASK(25, 22)
#define RX_MPDU_DESC_META_DATA_V1B_DEVICE_ID	GENMASK(28, 26)

struct rx_mpdu_desc {
	__le32 info0; /* %RX_MPDU_DESC_INFO */
	__le32 peer_meta_data;
} __packed;

/* rx_mpdu_desc
 *		Producer: RXDMA
 *		Consumer: REO/SW/FW
 *
 * msdu_count
 *		The number of MSDUs within the MPDU
 *
 * fragment_flag
 *		When set, this MPDU is a fragment and REO should forward this
 *		fragment MPDU to the REO destination ring without any reorder
 *		checks, pn checks or bitmap update. This implies that REO is
 *		forwarding the pointer to the MSDU link descriptor.
 *
 * mpdu_retry_bit
 *		The retry bit setting from the MPDU header of the received frame
 *
 * ampdu_flag
 *		Indicates the MPDU was received as part of an A-MPDU.
 *
 * bar_frame
 *		Indicates the received frame is a BAR frame. After processing,
 *		this frame shall be pushed to SW or deleted.
 *
 * valid_pn
 *		When not set, REO will not perform a PN sequence number check.
 *
 * raw_mpdu
 *		Field only valid when first_msdu_in_mpdu_flag is set. Indicates
 *		the contents in the MSDU buffer contains a 'RAW' MPDU. This
 *		'RAW' MPDU might be spread out over multiple MSDU buffers.
 *
 * more_fragment_flag
 *		The More Fragment bit setting from the MPDU header of the
 *		received frame
 *
 * src_info
 *		Source (Virtual) device/interface info associated with this peer.
 *		This field gets passed on by REO to PPE in the EDMA descriptor.
 *
 * mpdu_qos_control_valid
 *		When set, the MPDU has a QoS control field
 *
 * tid
 *		Field only valid when mpdu_qos_control_valid is set
 */

enum hal_rx_msdu_desc_reo_dest_ind {
	HAL_RX_MSDU_DESC_REO_DEST_IND_TCL,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW1,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW2,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW3,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW4,
	HAL_RX_MSDU_DESC_REO_DEST_IND_RELEASE,
	HAL_RX_MSDU_DESC_REO_DEST_IND_FW,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW5,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW6,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW7,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW8,
};

#define RX_MSDU_DESC_INFO0_FIRST_MSDU_IN_MPDU	BIT(0)
#define RX_MSDU_DESC_INFO0_LAST_MSDU_IN_MPDU	BIT(1)
#define RX_MSDU_DESC_INFO0_MSDU_CONTINUATION	BIT(2)
#define RX_MSDU_DESC_INFO0_MSDU_LENGTH		GENMASK(16, 3)
#define RX_MSDU_DESC_INFO0_MSDU_DROP		BIT(17)
#define RX_MSDU_DESC_INFO0_VALID_SA		BIT(18)
#define RX_MSDU_DESC_INFO0_VALID_DA		BIT(19)
#define RX_MSDU_DESC_INFO0_DA_MCBC		BIT(20)
#define RX_MSDU_DESC_INFO0_L3_HDR_PAD_MSB	BIT(21)
#define RX_MSDU_DESC_INFO0_TCP_UDP_CHKSUM_FAIL	BIT(22)
#define RX_MSDU_DESC_INFO0_IP_CHKSUM_FAIL	BIT(23)
#define RX_MSDU_DESC_INFO0_FROM_DS		BIT(24)
#define RX_MSDU_DESC_INFO0_TO_DS		BIT(25)
#define RX_MSDU_DESC_INFO0_INTRA_BSS		BIT(26)
#define RX_MSDU_DESC_INFO0_DST_CHIP_ID		GENMASK(28, 27)
#define RX_MSDU_DESC_INFO0_DECAP_FORMAT		GENMASK(30, 29)

#define HAL_RX_MSDU_PKT_LENGTH_GET(val)		\
	(le32_get_bits((val), RX_MSDU_DESC_INFO0_MSDU_LENGTH))

struct rx_msdu_desc {
	__le32 info0;
} __packed;

/* rx_msdu_desc
 *
 * first_msdu_in_mpdu
 *		Indicates first msdu in mpdu.
 *
 * last_msdu_in_mpdu
 *		Indicates last msdu in mpdu. This flag can be true only when
 *		'Msdu_continuation' set to 0. This implies that when an msdu
 *		is spread out over multiple buffers and thus msdu_continuation
 *		is set, only for the very last buffer of the msdu, can the
 *		'last_msdu_in_mpdu' be set.
 *
 *		When both first_msdu_in_mpdu and last_msdu_in_mpdu are set,
 *		the MPDU that this MSDU belongs to only contains a single MSDU.
 *
 * msdu_continuation
 *		When set, this MSDU buffer was not able to hold the entire MSDU.
 *		The next buffer will therefore contain additional information
 *		related to this MSDU.
 *
 * msdu_length
 *		Field is only valid in combination with the 'first_msdu_in_mpdu'
 *		being set. Full MSDU length in bytes after decapsulation. This
 *		field is still valid for MPDU frames without A-MSDU. It still
 *		represents MSDU length after decapsulation Or in case of RAW
 *		MPDUs, it indicates the length of the entire MPDU (without FCS
 *		field).
 *
 * msdu_drop
 *		Indicates that REO shall drop this MSDU and not forward it to
 *		any other ring.
 *
 * valid_sa
 *		Indicates OLE found a valid SA entry for this MSDU.
 *
 * valid_da
 *		When set, OLE found a valid DA entry for this MSDU.
 *
 * da_mcbc
 *		Field Only valid if valid_da is set. Indicates the DA address
 *		is a Multicast or Broadcast address for this MSDU.
 *
 * l3_header_padding_msb
 *		Passed on from 'RX_MSDU_END' TLV (only the MSB is reported as
 *		the LSB is always zero). Number of bytes padded to make sure
 *		that the L3 header will always start of a Dword boundary
 *
 * tcp_udp_checksum_fail
 *		Passed on from 'RX_ATTENTION' TLV
 *		Indicates that the computed checksum did not match the checksum
 *		in the TCP/UDP header.
 *
 * ip_checksum_fail
 *		Passed on from 'RX_ATTENTION' TLV
 *		Indicates that the computed checksum did not match the checksum
 *		in the IP header.
 *
 * from_DS
 *		Set if the 'from DS' bit is set in the frame control.
 *
 * to_DS
 *		Set if the 'to DS' bit is set in the frame control.
 *
 * intra_bss
 *		This packet needs intra-BSS routing by SW as the 'vdev_id'
 *		for the destination is the same as the 'vdev_id' that this
 *		MSDU was got in.
 *
 * dest_chip_id
 *		If intra_bss is set, copied by RXOLE/RXDMA from 'ADDR_SEARCH_ENTRY'
 *		to support intra-BSS routing with multi-chip multi-link operation.
 *		This indicates into which chip's TCL the packet should be queued.
 *
 * decap_format
 *		Indicates the format after decapsulation:
 */

#define RX_MSDU_EXT_DESC_INFO0_REO_DEST_IND	GENMASK(4, 0)
#define RX_MSDU_EXT_DESC_INFO0_SERVICE_CODE	GENMASK(13, 5)
#define RX_MSDU_EXT_DESC_INFO0_PRIORITY_VALID	BIT(14)
#define RX_MSDU_EXT_DESC_INFO0_DATA_OFFSET	GENMASK(26, 15)
#define RX_MSDU_EXT_DESC_INFO0_SRC_LINK_ID	GENMASK(29, 27)

struct rx_msdu_ext_desc {
	__le32 info0;
} __packed;

/* rx_msdu_ext_desc
 *
 * reo_destination_indication
 *		The ID of the REO exit ring where the MSDU frame shall push
 *		after (MPDU level) reordering has finished.
 *
 * service_code
 *		Opaque service code between PPE and Wi-Fi
 *
 * priority_valid
 *
 * data_offset
 *		The offset to Rx packet data within the buffer (including
 *		Rx DMA offset programming and L3 header padding inserted
 *		by Rx OLE).
 *
 * src_link_id
 *		Set to the link ID of the PMAC that received the frame
 */

enum hal_reo_dest_ring_buffer_type {
	HAL_REO_DEST_RING_BUFFER_TYPE_MSDU,
	HAL_REO_DEST_RING_BUFFER_TYPE_LINK_DESC,
};

enum hal_reo_dest_ring_push_reason {
	HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED,
	HAL_REO_DEST_RING_PUSH_REASON_ROUTING_INSTRUCTION,
};

enum hal_reo_dest_ring_error_code {
	HAL_REO_DEST_RING_ERROR_CODE_DESC_ADDR_ZERO,
	HAL_REO_DEST_RING_ERROR_CODE_DESC_INVALID,
	HAL_REO_DEST_RING_ERROR_CODE_AMPDU_IN_NON_BA,
	HAL_REO_DEST_RING_ERROR_CODE_NON_BA_DUPLICATE,
	HAL_REO_DEST_RING_ERROR_CODE_BA_DUPLICATE,
	HAL_REO_DEST_RING_ERROR_CODE_FRAME_2K_JUMP,
	HAL_REO_DEST_RING_ERROR_CODE_BAR_2K_JUMP,
	HAL_REO_DEST_RING_ERROR_CODE_FRAME_OOR,
	HAL_REO_DEST_RING_ERROR_CODE_BAR_OOR,
	HAL_REO_DEST_RING_ERROR_CODE_NO_BA_SESSION,
	HAL_REO_DEST_RING_ERROR_CODE_FRAME_SN_EQUALS_SSN,
	HAL_REO_DEST_RING_ERROR_CODE_PN_CHECK_FAILED,
	HAL_REO_DEST_RING_ERROR_CODE_2K_ERR_FLAG_SET,
	HAL_REO_DEST_RING_ERROR_CODE_PN_ERR_FLAG_SET,
	HAL_REO_DEST_RING_ERROR_CODE_DESC_BLOCKED,
	HAL_REO_DEST_RING_ERROR_CODE_MAX,
};

#define HAL_REO_DEST_RING_INFO0_BUFFER_TYPE		BIT(0)
#define HAL_REO_DEST_RING_INFO0_PUSH_REASON		GENMASK(2, 1)
#define HAL_REO_DEST_RING_INFO0_ERROR_CODE		GENMASK(7, 3)
#define HAL_REO_DEST_RING_INFO0_MSDU_DATA_SIZE		GENMASK(11, 8)
#define HAL_REO_DEST_RING_INFO0_SW_EXCEPTION		BIT(12)
#define HAL_REO_DEST_RING_INFO0_SRC_LINK_ID		GENMASK(15, 13)
#define HAL_REO_DEST_RING_INFO0_SIGNATURE		GENMASK(19, 16)
#define HAL_REO_DEST_RING_INFO0_RING_ID			GENMASK(27, 20)
#define HAL_REO_DEST_RING_INFO0_LOOPING_COUNT		GENMASK(31, 28)

struct hal_reo_dest_ring {
	struct ath12k_buffer_addr buf_addr_info;
	struct rx_mpdu_desc rx_mpdu_info;
	struct rx_msdu_desc rx_msdu_info;
	__le32 buf_va_lo;
	__le32 buf_va_hi;
	__le32 info0; /* %HAL_REO_DEST_RING_INFO0_ */
} __packed;

/* hal_reo_dest_ring
 *
 *		Producer: RXDMA
 *		Consumer: REO/SW/FW
 *
 * buf_addr_info
 *		Details of the physical address of a buffer or MSDU
 *		link descriptor.
 *
 * rx_mpdu_info
 *		General information related to the MPDU that is passed
 *		on from REO entrance ring to the REO destination ring.
 *
 * rx_msdu_info
 *		General information related to the MSDU that is passed
 *		on from RXDMA all the way to the REO destination ring.
 *
 * buf_va_lo
 *		Field only valid if Reo_dest_buffer_type is set to MSDU_buf_address
 *		Lower 32 bits of the 64-bit virtual address corresponding
 *		to Buf_or_link_desc_addr_info
 *
 * buf_va_hi
 *		Address (upper 32 bits) of the REO queue descriptor.
 *		Upper 32 bits of the 64-bit virtual address corresponding
 *		to Buf_or_link_desc_addr_info
 *
 * buffer_type
 *		Indicates the type of address provided in the buf_addr_info.
 *		Values are defined in enum %HAL_REO_DEST_RING_BUFFER_TYPE_.
 *
 * push_reason
 *		Reason for pushing this frame to this exit ring. Values are
 *		defined in enum %HAL_REO_DEST_RING_PUSH_REASON_.
 *
 * error_code
 *		Valid only when 'push_reason' is set. All error codes are
 *		defined in enum %HAL_REO_DEST_RING_ERROR_CODE_.
 *
 * captured_msdu_data_size
 *		The number of following REO_DESTINATION STRUCTs that have
 *		been replaced with msdu_data extracted from the msdu_buffer
 *		and copied into the ring for easy FW/SW access.
 *
 * sw_exception
 *		This field has the same setting as the SW_exception field
 *		in the corresponding REO_entrance_ring descriptor.
 *		When set, the REO entrance descriptor is generated by FW,
 *		and the MPDU was processed in the following way:
 *		- NO re-order function is needed.
 *		- MPDU delinking is determined by the setting of Entrance
 *		  ring field: SW_excection_mpdu_delink
 *		- Destination ring selection is based on the setting of
 *		  the Entrance ring field SW_exception_destination _ring_valid
 *
 * src_link_id
 *		Set to the link ID of the PMAC that received the frame
 *
 * signature
 *		Set to value 0x8 when msdu capture mode is enabled for this ring
 *
 * ring_id
 *		The buffer pointer ring id.
 *		0 - Idle ring
 *		1 - N refers to other rings.
 *
 * looping_count
 *		Indicates the number of times the producer of entries into
 *		this ring has looped around the ring.
 */

#define HAL_REO_TO_PPE_RING_INFO0_DATA_LENGTH	GENMASK(15, 0)
#define HAL_REO_TO_PPE_RING_INFO0_DATA_OFFSET	GENMASK(23, 16)
#define HAL_REO_TO_PPE_RING_INFO0_POOL_ID	GENMASK(28, 24)
#define HAL_REO_TO_PPE_RING_INFO0_PREHEADER	BIT(29)
#define HAL_REO_TO_PPE_RING_INFO0_TSO_EN	BIT(30)
#define HAL_REO_TO_PPE_RING_INFO0_MORE	BIT(31)

struct hal_reo_to_ppe_ring {
	__le32 buffer_addr;
	__le32 info0; /* %HAL_REO_TO_PPE_RING_INFO0_ */
} __packed;

/* hal_reo_to_ppe_ring
 *
 *		Producer: REO
 *		Consumer: PPE
 *
 * buf_addr_info
 *		Details of the physical address of a buffer or MSDU
 *		link descriptor.
 *
 * data_length
 *		Length of valid data in bytes
 *
 * data_offset
 *		Offset to the data from buffer pointer. Can be used to
 *		strip header in the data for tunnel termination etc.
 *
 * pool_id
 *		REO has global configuration register for this field.
 *		It may have several free buffer pools, each
 *		RX-Descriptor ring can fetch free buffer from specific
 *		buffer pool; pool id will indicate which pool the buffer
 *		will be released to; POOL_ID Zero returned to SW
 *
 * preheader
 *		Disabled: 0 (Default)
 *		Enabled: 1
 *
 * tso_en
 *		Disabled: 0 (Default)
 *		Enabled: 1
 *
 * more
 *		More Segments followed
 */

enum hal_reo_entr_rxdma_push_reason {
	HAL_REO_ENTR_RING_RXDMA_PUSH_REASON_ERR_DETECTED,
	HAL_REO_ENTR_RING_RXDMA_PUSH_REASON_ROUTING_INSTRUCTION,
	HAL_REO_ENTR_RING_RXDMA_PUSH_REASON_RX_FLUSH,
};

enum hal_reo_entr_rxdma_ecode {
	HAL_REO_ENTR_RING_RXDMA_ECODE_OVERFLOW_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_MPDU_LEN_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_FCS_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_DECRYPT_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_TKIP_MIC_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_UNECRYPTED_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_MSDU_LEN_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_MSDU_LIMIT_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_WIFI_PARSE_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_AMSDU_PARSE_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_SA_TIMEOUT_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_DA_TIMEOUT_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_FLOW_TIMEOUT_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_FLUSH_REQUEST_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_AMSDU_FRAG_ERR,
	HAL_REO_ENTR_RING_RXDMA_ECODE_MAX,
};

enum hal_rx_reo_dest_ring {
	HAL_RX_REO_DEST_RING_TCL,
	HAL_RX_REO_DEST_RING_SW1,
	HAL_RX_REO_DEST_RING_SW2,
	HAL_RX_REO_DEST_RING_SW3,
	HAL_RX_REO_DEST_RING_SW4,
	HAL_RX_REO_DEST_RING_RELEASE,
	HAL_RX_REO_DEST_RING_FW,
	HAL_RX_REO_DEST_RING_SW5,
	HAL_RX_REO_DEST_RING_SW6,
	HAL_RX_REO_DEST_RING_SW7,
	HAL_RX_REO_DEST_RING_SW8,
};

#define HAL_REO_ENTR_RING_INFO0_QUEUE_ADDR_HI		GENMASK(7, 0)
#define HAL_REO_ENTR_RING_INFO0_MPDU_BYTE_COUNT		GENMASK(21, 8)
#define HAL_REO_ENTR_RING_INFO0_DEST_IND		GENMASK(26, 22)
#define HAL_REO_ENTR_RING_INFO0_FRAMELESS_BAR		BIT(27)

#define HAL_REO_ENTR_RING_INFO1_RXDMA_PUSH_REASON	GENMASK(1, 0)
#define HAL_REO_ENTR_RING_INFO1_RXDMA_ERROR_CODE	GENMASK(6, 2)
#define HAL_REO_ENTR_RING_INFO1_MPDU_FRAG_NUM		GENMASK(10, 7)
#define HAL_REO_ENTR_RING_INFO1_SW_EXCEPTION		BIT(11)
#define HAL_REO_ENTR_RING_INFO1_SW_EXCEPT_MPDU_DELINK	BIT(12)
#define HAL_REO_ENTR_RING_INFO1_SW_EXCEPTION_RING_VLD	BIT(13)
#define HAL_REO_ENTR_RING_INFO1_SW_EXCEPTION_RING	GENMASK(18, 14)
#define HAL_REO_ENTR_RING_INFO1_MPDU_SEQ_NUM		GENMASK(30, 19)

#define HAL_REO_ENTR_RING_INFO2_PHY_PPDU_ID		GENMASK(15, 0)
#define HAL_REO_ENTR_RING_INFO2_SRC_LINK_ID		GENMASK(18, 16)
#define HAL_REO_ENTR_RING_INFO2_RING_ID			GENMASK(27, 20)
#define HAL_REO_ENTR_RING_INFO2_LOOPING_COUNT		GENMASK(31, 28)

struct hal_reo_entrance_ring {
	struct ath12k_buffer_addr buf_addr_info;
	struct rx_mpdu_desc rx_mpdu_info;
	__le32 queue_addr_lo;
	__le32 info0; /* %HAL_REO_ENTR_RING_INFO0_ */
	__le32 info1; /* %HAL_REO_ENTR_RING_INFO1_ */
	__le32 info2; /* %HAL_REO_DEST_RING_INFO2_ */

} __packed;

/* hal_reo_entrance_ring
 *
 *		Producer: RXDMA
 *		Consumer: REO
 *
 * buf_addr_info
 *		Details of the physical address of a buffer or MSDU
 *		link descriptor.
 *
 * rx_mpdu_info
 *		General information related to the MPDU that is passed
 *		on from REO entrance ring to the REO destination ring.
 *
 * queue_addr_lo
 *		Address (lower 32 bits) of the REO queue descriptor.
 *
 * queue_addr_hi
 *		Address (upper 8 bits) of the REO queue descriptor.
 *
 * mpdu_byte_count
 *		An approximation of the number of bytes received in this MPDU.
 *		Used to keeps stats on the amount of data flowing
 *		through a queue.
 *
 * reo_destination_indication
 *		The id of the reo exit ring where the msdu frame shall push
 *		after (MPDU level) reordering has finished. Values are defined
 *		in enum %HAL_RX_MSDU_DESC_REO_DEST_IND_.
 *
 * frameless_bar
 *		Indicates that this REO entrance ring struct contains BAR info
 *		from a multi TID BAR frame. The original multi TID BAR frame
 *		itself contained all the REO info for the first TID, but all
 *		the subsequent TID info and their linkage to the REO descriptors
 *		is passed down as 'frameless' BAR info.
 *
 *		The only fields valid in this descriptor when this bit is set
 *		are queue_addr_lo, queue_addr_hi, mpdu_sequence_number,
 *		bar_frame and peer_meta_data.
 *
 * rxdma_push_reason
 *		Reason for pushing this frame to this exit ring. Values are
 *		defined in enum %HAL_REO_ENTR_RING_RXDMA_PUSH_REASON_.
 *
 * rxdma_error_code
 *		Valid only when 'push_reason' is set. All error codes are
 *		defined in enum %HAL_REO_ENTR_RING_RXDMA_ECODE_.
 *
 * mpdu_fragment_number
 *		Field only valid when Reo_level_mpdu_frame_info.
 *		Rx_mpdu_desc_info_details.Fragment_flag is set.
 *
 * sw_exception
 *		When not set, REO is performing all its default MPDU processing
 *		operations,
 *		When set, this REO entrance descriptor is generated by FW, and
 *		should be processed as an exception. This implies:
 *		NO re-order function is needed.
 *		MPDU delinking is determined by the setting of field
 *		SW_excection_mpdu_delink
 *
 * sw_exception_mpdu_delink
 *		Field only valid when SW_exception is set.
 *		1'b0: REO should NOT delink the MPDU, and thus pass this
 *			MPDU on to the destination ring as is. This implies that
 *			in the REO_DESTINATION_RING struct field
 *			Buf_or_link_desc_addr_info should point to an MSDU link
 *			descriptor
 *		1'b1: REO should perform the normal MPDU delink into MSDU operations.
 *
 * sw_exception_dest_ring
 *		Field only valid when fields SW_exception and SW
 *		exception_destination_ring_valid are set. values are defined
 *		in %HAL_RX_REO_DEST_RING_.
 *
 * mpdu_seq_number
 *		The field can have two different meanings based on the setting
 *		of sub-field Reo level mpdu frame info.
 *		Rx_mpdu_desc_info_details. BAR_frame
 *		'BAR_frame' is NOT set:
 *		The MPDU sequence number of the received frame.
 *		'BAR_frame' is set.
 *		The MPDU Start sequence number from the BAR frame
 *
 * phy_ppdu_id
 *		A PPDU counter value that PHY increments for every PPDU received
 *
 * src_link_id
 *		Set to the link ID of the PMAC that received the frame
 *
 * ring_id
 *		The buffer pointer ring id.
 *		0 - Idle ring
 *		1 - N refers to other rings.
 *
 * looping_count
 *		Indicates the number of times the producer of entries into
 *		this ring has looped around the ring.
 */

#define HAL_REO_CMD_HDR_INFO0_CMD_NUMBER	GENMASK(15, 0)
#define HAL_REO_CMD_HDR_INFO0_STATUS_REQUIRED	BIT(16)

struct hal_reo_cmd_hdr {
	__le32 info0;
} __packed;

#define HAL_REO_GET_QUEUE_STATS_INFO0_QUEUE_ADDR_HI	GENMASK(7, 0)
#define HAL_REO_GET_QUEUE_STATS_INFO0_CLEAR_STATS	BIT(8)

struct hal_reo_get_queue_stats {
	struct hal_reo_cmd_hdr cmd;
	__le32 queue_addr_lo;
	__le32 info0;
	__le32 rsvd0[6];
	__le32 tlv64_pad;
} __packed;

/* hal_reo_get_queue_stats
 *		Producer: SW
 *		Consumer: REO
 *
 * cmd
 *		Details for command execution tracking purposes.
 *
 * queue_addr_lo
 *		Address (lower 32 bits) of the REO queue descriptor.
 *
 * queue_addr_hi
 *		Address (upper 8 bits) of the REO queue descriptor.
 *
 * clear_stats
 *		Clear stats settings. When set, Clear the stats after
 *		generating the status.
 *
 *		Following stats will be cleared.
 *		Timeout_count
 *		Forward_due_to_bar_count
 *		Duplicate_count
 *		Frames_in_order_count
 *		BAR_received_count
 *		MPDU_Frames_processed_count
 *		MSDU_Frames_processed_count
 *		Total_processed_byte_count
 *		Late_receive_MPDU_count
 *		window_jump_2k
 *		Hole_count
 */

#define HAL_REO_FLUSH_QUEUE_INFO0_DESC_ADDR_HI		GENMASK(7, 0)
#define HAL_REO_FLUSH_QUEUE_INFO0_BLOCK_DESC_ADDR	BIT(8)
#define HAL_REO_FLUSH_QUEUE_INFO0_BLOCK_RESRC_IDX	GENMASK(10, 9)

struct hal_reo_flush_queue {
	struct hal_reo_cmd_hdr cmd;
	__le32 desc_addr_lo;
	__le32 info0;
	__le32 rsvd0[6];
} __packed;

#define HAL_REO_FLUSH_CACHE_INFO0_CACHE_ADDR_HI		GENMASK(7, 0)
#define HAL_REO_FLUSH_CACHE_INFO0_FWD_ALL_MPDUS		BIT(8)
#define HAL_REO_FLUSH_CACHE_INFO0_RELEASE_BLOCK_IDX	BIT(9)
#define HAL_REO_FLUSH_CACHE_INFO0_BLOCK_RESRC_IDX	GENMASK(11, 10)
#define HAL_REO_FLUSH_CACHE_INFO0_FLUSH_WO_INVALIDATE	BIT(12)
#define HAL_REO_FLUSH_CACHE_INFO0_BLOCK_CACHE_USAGE	BIT(13)
#define HAL_REO_FLUSH_CACHE_INFO0_FLUSH_ALL		BIT(14)

struct hal_reo_flush_cache {
	struct hal_reo_cmd_hdr cmd;
	__le32 cache_addr_lo;
	__le32 info0;
	__le32 rsvd0[6];
} __packed;

#define HAL_TCL_DATA_CMD_INFO0_CMD_TYPE			BIT(0)
#define HAL_TCL_DATA_CMD_INFO0_DESC_TYPE		BIT(1)
#define HAL_TCL_DATA_CMD_INFO0_BANK_ID			GENMASK(7, 2)
#define HAL_TCL_DATA_CMD_INFO0_TX_NOTIFY_FRAME		GENMASK(10, 8)
#define HAL_TCL_DATA_CMD_INFO0_HDR_LEN_READ_SEL		BIT(11)
#define HAL_TCL_DATA_CMD_INFO0_BUF_TIMESTAMP		GENMASK(30, 12)
#define HAL_TCL_DATA_CMD_INFO0_BUF_TIMESTAMP_VLD	BIT(31)

#define HAL_TCL_DATA_CMD_INFO1_CMD_NUM		GENMASK(31, 16)

#define HAL_TCL_DATA_CMD_INFO2_DATA_LEN		GENMASK(15, 0)
#define HAL_TCL_DATA_CMD_INFO2_IP4_CKSUM_EN	BIT(16)
#define HAL_TCL_DATA_CMD_INFO2_UDP4_CKSUM_EN	BIT(17)
#define HAL_TCL_DATA_CMD_INFO2_UDP6_CKSUM_EN	BIT(18)
#define HAL_TCL_DATA_CMD_INFO2_TCP4_CKSUM_EN	BIT(19)
#define HAL_TCL_DATA_CMD_INFO2_TCP6_CKSUM_EN	BIT(20)
#define HAL_TCL_DATA_CMD_INFO2_TO_FW		BIT(21)
#define HAL_TCL_DATA_CMD_INFO2_PKT_OFFSET	GENMASK(31, 23)

#define HAL_TCL_DATA_CMD_INFO3_TID_OVERWRITE		BIT(0)
#define HAL_TCL_DATA_CMD_INFO3_FLOW_OVERRIDE_EN		BIT(1)
#define HAL_TCL_DATA_CMD_INFO3_CLASSIFY_INFO_SEL	GENMASK(3, 2)
#define HAL_TCL_DATA_CMD_INFO3_TID			GENMASK(7, 4)
#define HAL_TCL_DATA_CMD_INFO3_FLOW_OVERRIDE		BIT(8)
#define HAL_TCL_DATA_CMD_INFO3_PMAC_ID			GENMASK(10, 9)
#define HAL_TCL_DATA_CMD_INFO3_MSDU_COLOR		GENMASK(12, 11)
#define HAL_TCL_DATA_CMD_INFO3_VDEV_ID			GENMASK(31, 24)

#define HAL_TCL_DATA_CMD_INFO4_SEARCH_INDEX		GENMASK(19, 0)
#define HAL_TCL_DATA_CMD_INFO4_CACHE_SET_NUM		GENMASK(23, 20)
#define HAL_TCL_DATA_CMD_INFO4_IDX_LOOKUP_OVERRIDE	BIT(24)

#define HAL_TCL_DATA_CMD_INFO5_RING_ID			GENMASK(27, 20)
#define HAL_TCL_DATA_CMD_INFO5_LOOPING_COUNT		GENMASK(31, 28)

enum hal_encrypt_type {
	HAL_ENCRYPT_TYPE_WEP_40,
	HAL_ENCRYPT_TYPE_WEP_104,
	HAL_ENCRYPT_TYPE_TKIP_NO_MIC,
	HAL_ENCRYPT_TYPE_WEP_128,
	HAL_ENCRYPT_TYPE_TKIP_MIC,
	HAL_ENCRYPT_TYPE_WAPI,
	HAL_ENCRYPT_TYPE_CCMP_128,
	HAL_ENCRYPT_TYPE_OPEN,
	HAL_ENCRYPT_TYPE_CCMP_256,
	HAL_ENCRYPT_TYPE_GCMP_128,
	HAL_ENCRYPT_TYPE_AES_GCMP_256,
	HAL_ENCRYPT_TYPE_WAPI_GCM_SM4,
};

enum hal_tcl_encap_type {
	HAL_TCL_ENCAP_TYPE_RAW,
	HAL_TCL_ENCAP_TYPE_NATIVE_WIFI,
	HAL_TCL_ENCAP_TYPE_ETHERNET,
	HAL_TCL_ENCAP_TYPE_802_3 = 3,
	HAL_TCL_ENCAP_TYPE_MAX
};

enum hal_tcl_desc_type {
	HAL_TCL_DESC_TYPE_BUFFER,
	HAL_TCL_DESC_TYPE_EXT_DESC,
	HAL_TCL_DESC_TYPE_MAX,
};

enum hal_wbm_htt_tx_comp_status {
	HAL_WBM_REL_HTT_TX_COMP_STATUS_OK,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_DROP,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_TTL,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_REINJ,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_INSPECT,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_MEC_NOTIFY,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_VDEVID_MISMATCH,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_MAX,
};

struct hal_tcl_data_cmd {
	struct ath12k_buffer_addr buf_addr_info;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 info4;
	__le32 info5;
} __packed;

/* hal_tcl_data_cmd
 *
 * buf_addr_info
 *		Details of the physical address of a buffer or MSDU
 *		link descriptor.
 *
 * tcl_cmd_type
 *		used to select the type of TCL Command descriptor
 *
 * desc_type
 *		Indicates the type of address provided in the buf_addr_info.
 *		Values are defined in enum %HAL_REO_DEST_RING_BUFFER_TYPE_.
 *
 * bank_id
 *		used to select one of the TCL register banks for fields removed
 *		from 'TCL_DATA_CMD' that do not change often within one virtual
 *		device or a set of virtual devices:
 *
 * tx_notify_frame
 *		TCL copies this value to 'TQM_ENTRANCE_RING' field FW_tx_notify_frame.
 *
 * hdr_length_read_sel
 *		used to select the per 'encap_type' register set for MSDU header
 *		read length
 *
 * buffer_timestamp
 * buffer_timestamp_valid
 *		Frame system entrance timestamp. It shall be filled by first
 *		module (SW, TCL or TQM) that sees the frames first.
 *
 * cmd_num
 *		This number can be used to match against status.
 *
 * data_length
 *		MSDU length in case of direct descriptor. Length of link
 *		extension descriptor in case of Link extension descriptor.
 *
 * *_checksum_en
 *		Enable checksum replacement for ipv4, udp_over_ipv4, ipv6,
 *		udp_over_ipv6, tcp_over_ipv4 and tcp_over_ipv6.
 *
 * to_fw
 *		Forward packet to FW along with classification result. The
 *		packet will not be forward to TQM when this bit is set.
 *		1'b0: Use classification result to forward the packet.
 *		1'b1: Override classification result & forward packet only to fw
 *
 * packet_offset
 *		Packet offset from Metadata in case of direct buffer descriptor.
 *
 * hlos_tid_overwrite
 *
 *		When set, TCL shall ignore the IP DSCP and VLAN PCP
 *		fields and use HLOS_TID as the final TID. Otherwise TCL
 *		shall consider the DSCP and PCP fields as well as HLOS_TID
 *		and choose a final TID based on the configured priority
 *
 * flow_override_enable
 *		TCL uses this to select the flow pointer from the peer table,
 *		which can be overridden by SW for pre-encrypted raw WiFi packets
 *		that cannot be parsed for UDP or for other MLO
 *		0 - FP_PARSE_IP: Use the flow-pointer based on parsing the IPv4
 *				 or IPv6 header.
 *		1 - FP_USE_OVERRIDE: Use the who_classify_info_sel and
 *				     flow_override fields to select the flow-pointer
 *
 * who_classify_info_sel
 *		Field only valid when flow_override_enable is set to FP_USE_OVERRIDE.
 *		This field is used to select  one of the 'WHO_CLASSIFY_INFO's in the
 *		peer table in case more than 2 flows are mapped to a single TID.
 *		0: To choose Flow 0 and 1 of any TID use this value.
 *		1: To choose Flow 2 and 3 of any TID use this value.
 *		2: To choose Flow 4 and 5 of any TID use this value.
 *		3: To choose Flow 6 and 7 of any TID use this value.
 *
 *		If who_classify_info sel is not in sync with the num_tx_classify_info
 *		field from address search, then TCL will set 'who_classify_info_sel'
 *		to 0 use flows 0 and 1.
 *
 * hlos_tid
 *		HLOS MSDU priority
 *		Field is used when HLOS_TID_overwrite is set.
 *
 * flow_override
 *		Field only valid when flow_override_enable is set to FP_USE_OVERRIDE
 *		TCL uses this to select the flow pointer from the peer table,
 *		which can be overridden by SW for pre-encrypted raw WiFi packets
 *		that cannot be parsed for UDP or for other MLO
 *		0 - FP_USE_NON_UDP: Use the non-UDP flow pointer (flow 0)
 *		1 - FP_USE_UDP: Use the UDP flow pointer (flow 1)
 *
 * pmac_id
 *		TCL uses this PMAC_ID in address search, i.e, while
 *		finding matching entry for the packet in AST corresponding
 *		to given PMAC_ID
 *
 *		If PMAC ID is all 1s (=> value 3), it indicates wildcard
 *		match for any PMAC
 *
 * vdev_id
 *		Virtual device ID to check against the address search entry to
 *		avoid security issues from transmitting packets from an incorrect
 *		virtual device
 *
 * search_index
 *		The index that will be used for index based address or
 *		flow search. The field is valid when 'search_type' is  1 or 2.
 *
 * cache_set_num
 *
 *		Cache set number that should be used to cache the index
 *		based search results, for address and flow search. This
 *		value should be equal to LSB four bits of the hash value of
 *		match data, in case of search index points to an entry which
 *		may be used in content based search also. The value can be
 *		anything when the entry pointed by search index will not be
 *		used for content based search.
 *
 * index_loop_override
 *		When set, address search and packet routing is forced to use
 *		'search_index' instead of following the register configuration
 *		selected by Bank_id.
 *
 * ring_id
 *		The buffer pointer ring ID.
 *		0 refers to the IDLE ring
 *		1 - N refers to other rings
 *
 * looping_count
 *
 *		A count value that indicates the number of times the
 *		producer of entries into the Ring has looped around the
 *		ring.
 *
 *		At initialization time, this value is set to 0. On the
 *		first loop, this value is set to 1. After the max value is
 *		reached allowed by the number of bits for this field, the
 *		count value continues with 0 again.
 *
 *		In case SW is the consumer of the ring entries, it can
 *		use this field to figure out up to where the producer of
 *		entries has created new entries. This eliminates the need to
 *		check where the head pointer' of the ring is located once
 *		the SW starts processing an interrupt indicating that new
 *		entries have been put into this ring...
 *
 *		Also note that SW if it wants only needs to look at the
 *		LSB bit of this count value.
 */

#define HAL_TCL_DESC_LEN sizeof(struct hal_tcl_data_cmd)

#define HAL_TX_MSDU_EXT_INFO0_BUF_PTR_LO	GENMASK(31, 0)

#define HAL_TX_MSDU_EXT_INFO1_BUF_PTR_HI	GENMASK(7, 0)
#define HAL_TX_MSDU_EXT_INFO1_EXTN_OVERRIDE	BIT(8)
#define HAL_TX_MSDU_EXT_INFO1_ENCAP_TYPE	GENMASK(10, 9)
#define HAL_TX_MSDU_EXT_INFO1_ENCRYPT_TYPE	GENMASK(14, 11)
#define HAL_TX_MSDU_EXT_INFO1_BUF_LEN		GENMASK(31, 16)

struct hal_tx_msdu_ext_desc {
	__le32 rsvd0[6];
	__le32 info0;
	__le32 info1;
	__le32 rsvd1[10];
};

struct hal_tcl_gse_cmd {
	__le32 ctrl_buf_addr_lo;
	__le32 info0;
	__le32 meta_data[2];
	__le32 rsvd0[2];
	__le32 info1;
} __packed;

/* hal_tcl_gse_cmd
 *
 * ctrl_buf_addr_lo, ctrl_buf_addr_hi
 *		Address of a control buffer containing additional info needed
 *		for this command execution.
 *
 * meta_data
 *		Meta data to be returned in the status descriptor
 */

enum hal_tcl_cache_op_res {
	HAL_TCL_CACHE_OP_RES_DONE,
	HAL_TCL_CACHE_OP_RES_NOT_FOUND,
	HAL_TCL_CACHE_OP_RES_TIMEOUT,
};

struct hal_tcl_status_ring {
	__le32 info0;
	__le32 msdu_byte_count;
	__le32 msdu_timestamp;
	__le32 meta_data[2];
	__le32 info1;
	__le32 rsvd0;
	__le32 info2;
} __packed;

/* hal_tcl_status_ring
 *
 * msdu_cnt
 * msdu_byte_count
 *		MSDU count of Entry and MSDU byte count for entry 1.
 *
 */

#define HAL_CE_SRC_DESC_ADDR_INFO_ADDR_HI	GENMASK(7, 0)
#define HAL_CE_SRC_DESC_ADDR_INFO_HASH_EN	BIT(8)
#define HAL_CE_SRC_DESC_ADDR_INFO_BYTE_SWAP	BIT(9)
#define HAL_CE_SRC_DESC_ADDR_INFO_DEST_SWAP	BIT(10)
#define HAL_CE_SRC_DESC_ADDR_INFO_GATHER	BIT(11)
#define HAL_CE_SRC_DESC_ADDR_INFO_LEN		GENMASK(31, 16)

#define HAL_CE_SRC_DESC_META_INFO_DATA		GENMASK(15, 0)

#define HAL_CE_SRC_DESC_FLAGS_RING_ID		GENMASK(27, 20)
#define HAL_CE_SRC_DESC_FLAGS_LOOP_CNT		HAL_SRNG_DESC_LOOP_CNT

struct hal_ce_srng_src_desc {
	__le32 buffer_addr_low;
	__le32 buffer_addr_info; /* %HAL_CE_SRC_DESC_ADDR_INFO_ */
	__le32 meta_info; /* %HAL_CE_SRC_DESC_META_INFO_ */
	__le32 flags; /* %HAL_CE_SRC_DESC_FLAGS_ */
} __packed;

/* hal_ce_srng_src_desc
 *
 * buffer_addr_lo
 *		LSB 32 bits of the 40 Bit Pointer to the source buffer
 *
 * buffer_addr_hi
 *		MSB 8 bits of the 40 Bit Pointer to the source buffer
 *
 * toeplitz_en
 *		Enable generation of 32-bit Toeplitz-LFSR hash for
 *		data transfer. In case of gather field in first source
 *		ring entry of the gather copy cycle in taken into account.
 *
 * src_swap
 *		Treats source memory organization as big-endian. For
 *		each dword read (4 bytes), the byte 0 is swapped with byte 3
 *		and byte 1 is swapped with byte 2.
 *		In case of gather field in first source ring entry of
 *		the gather copy cycle in taken into account.
 *
 * dest_swap
 *		Treats destination memory organization as big-endian.
 *		For each dword write (4 bytes), the byte 0 is swapped with
 *		byte 3 and byte 1 is swapped with byte 2.
 *		In case of gather field in first source ring entry of
 *		the gather copy cycle in taken into account.
 *
 * gather
 *		Enables gather of multiple copy engine source
 *		descriptors to one destination.
 *
 * ce_res_0
 *		Reserved
 *
 *
 * length
 *		Length of the buffer in units of octets of the current
 *		descriptor
 *
 * fw_metadata
 *		Meta data used by FW.
 *		In case of gather field in first source ring entry of
 *		the gather copy cycle in taken into account.
 *
 * ce_res_1
 *		Reserved
 *
 * ce_res_2
 *		Reserved
 *
 * ring_id
 *		The buffer pointer ring ID.
 *		0 refers to the IDLE ring
 *		1 - N refers to other rings
 *		Helps with debugging when dumping ring contents.
 *
 * looping_count
 *		A count value that indicates the number of times the
 *		producer of entries into the Ring has looped around the
 *		ring.
 *
 *		At initialization time, this value is set to 0. On the
 *		first loop, this value is set to 1. After the max value is
 *		reached allowed by the number of bits for this field, the
 *		count value continues with 0 again.
 *
 *		In case SW is the consumer of the ring entries, it can
 *		use this field to figure out up to where the producer of
 *		entries has created new entries. This eliminates the need to
 *		check where the head pointer' of the ring is located once
 *		the SW starts processing an interrupt indicating that new
 *		entries have been put into this ring...
 *
 *		Also note that SW if it wants only needs to look at the
 *		LSB bit of this count value.
 */

#define HAL_CE_DEST_DESC_ADDR_INFO_ADDR_HI		GENMASK(7, 0)
#define HAL_CE_DEST_DESC_ADDR_INFO_RING_ID		GENMASK(27, 20)
#define HAL_CE_DEST_DESC_ADDR_INFO_LOOP_CNT		HAL_SRNG_DESC_LOOP_CNT

struct hal_ce_srng_dest_desc {
	__le32 buffer_addr_low;
	__le32 buffer_addr_info; /* %HAL_CE_DEST_DESC_ADDR_INFO_ */
} __packed;

/* hal_ce_srng_dest_desc
 *
 * dst_buffer_low
 *		LSB 32 bits of the 40 Bit Pointer to the Destination
 *		buffer
 *
 * dst_buffer_high
 *		MSB 8 bits of the 40 Bit Pointer to the Destination
 *		buffer
 *
 * ce_res_4
 *		Reserved
 *
 * ring_id
 *		The buffer pointer ring ID.
 *		0 refers to the IDLE ring
 *		1 - N refers to other rings
 *		Helps with debugging when dumping ring contents.
 *
 * looping_count
 *		A count value that indicates the number of times the
 *		producer of entries into the Ring has looped around the
 *		ring.
 *
 *		At initialization time, this value is set to 0. On the
 *		first loop, this value is set to 1. After the max value is
 *		reached allowed by the number of bits for this field, the
 *		count value continues with 0 again.
 *
 *		In case SW is the consumer of the ring entries, it can
 *		use this field to figure out up to where the producer of
 *		entries has created new entries. This eliminates the need to
 *		check where the head pointer' of the ring is located once
 *		the SW starts processing an interrupt indicating that new
 *		entries have been put into this ring...
 *
 *		Also note that SW if it wants only needs to look at the
 *		LSB bit of this count value.
 */

#define HAL_CE_DST_STATUS_DESC_FLAGS_HASH_EN		BIT(8)
#define HAL_CE_DST_STATUS_DESC_FLAGS_BYTE_SWAP		BIT(9)
#define HAL_CE_DST_STATUS_DESC_FLAGS_DEST_SWAP		BIT(10)
#define HAL_CE_DST_STATUS_DESC_FLAGS_GATHER		BIT(11)
#define HAL_CE_DST_STATUS_DESC_FLAGS_LEN		GENMASK(31, 16)

#define HAL_CE_DST_STATUS_DESC_META_INFO_DATA		GENMASK(15, 0)
#define HAL_CE_DST_STATUS_DESC_META_INFO_RING_ID	GENMASK(27, 20)
#define HAL_CE_DST_STATUS_DESC_META_INFO_LOOP_CNT	HAL_SRNG_DESC_LOOP_CNT

struct hal_ce_srng_dst_status_desc {
	__le32 flags; /* %HAL_CE_DST_STATUS_DESC_FLAGS_ */
	__le32 toeplitz_hash0;
	__le32 toeplitz_hash1;
	__le32 meta_info; /* HAL_CE_DST_STATUS_DESC_META_INFO_ */
} __packed;

/* hal_ce_srng_dst_status_desc
 *
 * ce_res_5
 *		Reserved
 *
 * toeplitz_en
 *
 * src_swap
 *		Source memory buffer swapped
 *
 * dest_swap
 *		Destination  memory buffer swapped
 *
 * gather
 *		Gather of multiple copy engine source descriptors to one
 *		destination enabled
 *
 * ce_res_6
 *		Reserved
 *
 * length
 *		Sum of all the Lengths of the source descriptor in the
 *		gather chain
 *
 * toeplitz_hash_0
 *		32 LS bits of 64 bit Toeplitz LFSR hash result
 *
 * toeplitz_hash_1
 *		32 MS bits of 64 bit Toeplitz LFSR hash result
 *
 * fw_metadata
 *		Meta data used by FW
 *		In case of gather field in first source ring entry of
 *		the gather copy cycle in taken into account.
 *
 * ce_res_7
 *		Reserved
 *
 * ring_id
 *		The buffer pointer ring ID.
 *		0 refers to the IDLE ring
 *		1 - N refers to other rings
 *		Helps with debugging when dumping ring contents.
 *
 * looping_count
 *		A count value that indicates the number of times the
 *		producer of entries into the Ring has looped around the
 *		ring.
 *
 *		At initialization time, this value is set to 0. On the
 *		first loop, this value is set to 1. After the max value is
 *		reached allowed by the number of bits for this field, the
 *		count value continues with 0 again.
 *
 *		In case SW is the consumer of the ring entries, it can
 *		use this field to figure out up to where the producer of
 *		entries has created new entries. This eliminates the need to
 *		check where the head pointer' of the ring is located once
 *		the SW starts processing an interrupt indicating that new
 *		entries have been put into this ring...
 *
 *		Also note that SW if it wants only needs to look at the
 *			LSB bit of this count value.
 */

#define HAL_TX_RATE_STATS_INFO0_VALID		BIT(0)
#define HAL_TX_RATE_STATS_INFO0_BW		GENMASK(3, 1)
#define HAL_TX_RATE_STATS_INFO0_PKT_TYPE	GENMASK(7, 4)
#define HAL_TX_RATE_STATS_INFO0_STBC		BIT(8)
#define HAL_TX_RATE_STATS_INFO0_LDPC		BIT(9)
#define HAL_TX_RATE_STATS_INFO0_SGI		GENMASK(11, 10)
#define HAL_TX_RATE_STATS_INFO0_MCS		GENMASK(15, 12)
#define HAL_TX_RATE_STATS_INFO0_OFDMA_TX	BIT(16)
#define HAL_TX_RATE_STATS_INFO0_TONES_IN_RU	GENMASK(28, 17)

enum hal_tx_rate_stats_bw {
	HAL_TX_RATE_STATS_BW_20,
	HAL_TX_RATE_STATS_BW_40,
	HAL_TX_RATE_STATS_BW_80,
	HAL_TX_RATE_STATS_BW_160,
};

enum hal_tx_rate_stats_pkt_type {
	HAL_TX_RATE_STATS_PKT_TYPE_11A,
	HAL_TX_RATE_STATS_PKT_TYPE_11B,
	HAL_TX_RATE_STATS_PKT_TYPE_11N,
	HAL_TX_RATE_STATS_PKT_TYPE_11AC,
	HAL_TX_RATE_STATS_PKT_TYPE_11AX,
	HAL_TX_RATE_STATS_PKT_TYPE_11BA,
	HAL_TX_RATE_STATS_PKT_TYPE_11BE,
};

enum hal_tx_rate_stats_sgi {
	HAL_TX_RATE_STATS_SGI_08US,
	HAL_TX_RATE_STATS_SGI_04US,
	HAL_TX_RATE_STATS_SGI_16US,
	HAL_TX_RATE_STATS_SGI_32US,
};

struct hal_tx_rate_stats {
	__le32 info0;
	__le32 tsf;
} __packed;

struct hal_wbm_link_desc {
	struct ath12k_buffer_addr buf_addr_info;
} __packed;

/* hal_wbm_link_desc
 *
 *	Producer: WBM
 *	Consumer: WBM
 *
 * buf_addr_info
 *		Details of the physical address of a buffer or MSDU
 *		link descriptor.
 */

enum hal_wbm_rel_src_module {
	HAL_WBM_REL_SRC_MODULE_TQM,
	HAL_WBM_REL_SRC_MODULE_RXDMA,
	HAL_WBM_REL_SRC_MODULE_REO,
	HAL_WBM_REL_SRC_MODULE_FW,
	HAL_WBM_REL_SRC_MODULE_SW,
};

enum hal_wbm_rel_desc_type {
	HAL_WBM_REL_DESC_TYPE_REL_MSDU,
	HAL_WBM_REL_DESC_TYPE_MSDU_LINK,
	HAL_WBM_REL_DESC_TYPE_MPDU_LINK,
	HAL_WBM_REL_DESC_TYPE_MSDU_EXT,
	HAL_WBM_REL_DESC_TYPE_QUEUE_EXT,
};

/* hal_wbm_rel_desc_type
 *
 * msdu_buffer
 *	The address points to an MSDU buffer
 *
 * msdu_link_descriptor
 *	The address points to an Tx MSDU link descriptor
 *
 * mpdu_link_descriptor
 *	The address points to an MPDU link descriptor
 *
 * msdu_ext_descriptor
 *	The address points to an MSDU extension descriptor
 *
 * queue_ext_descriptor
 *	The address points to an TQM queue extension descriptor. WBM should
 *	treat this is the same way as a link descriptor.
 */

enum hal_wbm_rel_bm_act {
	HAL_WBM_REL_BM_ACT_PUT_IN_IDLE,
	HAL_WBM_REL_BM_ACT_REL_MSDU,
};

/* hal_wbm_rel_bm_act
 *
 * put_in_idle_list
 *	Put the buffer or descriptor back in the idle list. In case of MSDU or
 *	MDPU link descriptor, BM does not need to check to release any
 *	individual MSDU buffers.
 *
 * release_msdu_list
 *	This BM action can only be used in combination with desc_type being
 *	msdu_link_descriptor. Field first_msdu_index points out which MSDU
 *	pointer in the MSDU link descriptor is the first of an MPDU that is
 *	released. BM shall release all the MSDU buffers linked to this first
 *	MSDU buffer pointer. All related MSDU buffer pointer entries shall be
 *	set to value 0, which represents the 'NULL' pointer. When all MSDU
 *	buffer pointers in the MSDU link descriptor are 'NULL', the MSDU link
 *	descriptor itself shall also be released.
 */
#define HAL_WBM_COMPL_RX_INFO0_REL_SRC_MODULE		GENMASK(2, 0)
#define HAL_WBM_COMPL_RX_INFO0_BM_ACTION		GENMASK(5, 3)
#define HAL_WBM_COMPL_RX_INFO0_DESC_TYPE		GENMASK(8, 6)
#define HAL_WBM_COMPL_RX_INFO0_RBM			GENMASK(12, 9)
#define HAL_WBM_COMPL_RX_INFO0_RXDMA_PUSH_REASON	GENMASK(18, 17)
#define HAL_WBM_COMPL_RX_INFO0_RXDMA_ERROR_CODE		GENMASK(23, 19)
#define HAL_WBM_COMPL_RX_INFO0_REO_PUSH_REASON		GENMASK(25, 24)
#define HAL_WBM_COMPL_RX_INFO0_REO_ERROR_CODE		GENMASK(30, 26)
#define HAL_WBM_COMPL_RX_INFO0_WBM_INTERNAL_ERROR	BIT(31)

#define HAL_WBM_COMPL_RX_INFO1_PHY_ADDR_HI		GENMASK(7, 0)
#define HAL_WBM_COMPL_RX_INFO1_SW_COOKIE		GENMASK(27, 8)
#define HAL_WBM_COMPL_RX_INFO1_LOOPING_COUNT		GENMASK(31, 28)

struct hal_wbm_completion_ring_rx {
	__le32 addr_lo;
	__le32 addr_hi;
	__le32 info0;
	struct rx_mpdu_desc rx_mpdu_info;
	struct rx_msdu_desc rx_msdu_info;
	__le32 phy_addr_lo;
	__le32 info1;
} __packed;

#define HAL_WBM_COMPL_TX_INFO0_REL_SRC_MODULE		GENMASK(2, 0)
#define HAL_WBM_COMPL_TX_INFO0_DESC_TYPE		GENMASK(8, 6)
#define HAL_WBM_COMPL_TX_INFO0_RBM			GENMASK(12, 9)
#define HAL_WBM_COMPL_TX_INFO0_TQM_RELEASE_REASON	GENMASK(16, 13)
#define HAL_WBM_COMPL_TX_INFO0_RBM_OVERRIDE_VLD		BIT(17)
#define HAL_WBM_COMPL_TX_INFO0_SW_COOKIE_LO		GENMASK(29, 18)
#define HAL_WBM_COMPL_TX_INFO0_CC_DONE			BIT(30)
#define HAL_WBM_COMPL_TX_INFO0_WBM_INTERNAL_ERROR	BIT(31)

#define HAL_WBM_COMPL_TX_INFO1_TQM_STATUS_NUMBER	GENMASK(23, 0)
#define HAL_WBM_COMPL_TX_INFO1_TRANSMIT_COUNT		GENMASK(30, 24)
#define HAL_WBM_COMPL_TX_INFO1_SW_REL_DETAILS_VALID	BIT(31)

#define HAL_WBM_COMPL_TX_INFO2_ACK_FRAME_RSSI		GENMASK(7, 0)
#define HAL_WBM_COMPL_TX_INFO2_FIRST_MSDU		BIT(8)
#define HAL_WBM_COMPL_TX_INFO2_LAST_MSDU		BIT(9)
#define HAL_WBM_COMPL_TX_INFO2_FW_TX_NOTIF_FRAME	GENMASK(12, 10)
#define HAL_WBM_COMPL_TX_INFO2_BUFFER_TIMESTAMP		GENMASK(31, 13)

#define HAL_WBM_COMPL_TX_INFO3_PEER_ID			GENMASK(15, 0)
#define HAL_WBM_COMPL_TX_INFO3_TID			GENMASK(19, 16)
#define HAL_WBM_COMPL_TX_INFO3_SW_COOKIE_HI		GENMASK(27, 20)
#define HAL_WBM_COMPL_TX_INFO3_LOOPING_COUNT		GENMASK(31, 28)

struct hal_wbm_completion_ring_tx {
	__le32 buf_va_lo;
	__le32 buf_va_hi;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	struct hal_tx_rate_stats rate_stats;
	__le32 info3;
} __packed;

#define HAL_WBM_RELEASE_TX_INFO0_REL_SRC_MODULE		GENMASK(2, 0)
#define HAL_WBM_RELEASE_TX_INFO0_BM_ACTION		GENMASK(5, 3)
#define HAL_WBM_RELEASE_TX_INFO0_DESC_TYPE		GENMASK(8, 6)
#define HAL_WBM_RELEASE_TX_INFO0_FIRST_MSDU_IDX		GENMASK(12, 9)
#define HAL_WBM_RELEASE_TX_INFO0_TQM_RELEASE_REASON	GENMASK(18, 13)
#define HAL_WBM_RELEASE_TX_INFO0_RBM_OVERRIDE_VLD	BIT(17)
#define HAL_WBM_RELEASE_TX_INFO0_SW_BUFFER_COOKIE_11_0	GENMASK(29, 18)
#define HAL_WBM_RELEASE_TX_INFO0_WBM_INTERNAL_ERROR	BIT(31)

#define HAL_WBM_RELEASE_TX_INFO1_TQM_STATUS_NUMBER	GENMASK(23, 0)
#define HAL_WBM_RELEASE_TX_INFO1_TRANSMIT_COUNT		GENMASK(30, 24)
#define HAL_WBM_RELEASE_TX_INFO1_SW_REL_DETAILS_VALID	BIT(31)

#define HAL_WBM_RELEASE_TX_INFO2_ACK_FRAME_RSSI		GENMASK(7, 0)
#define HAL_WBM_RELEASE_TX_INFO2_FIRST_MSDU		BIT(8)
#define HAL_WBM_RELEASE_TX_INFO2_LAST_MSDU		BIT(9)
#define HAL_WBM_RELEASE_TX_INFO2_FW_TX_NOTIF_FRAME	GENMASK(12, 10)
#define HAL_WBM_RELEASE_TX_INFO2_BUFFER_TIMESTAMP	GENMASK(31, 13)

#define HAL_WBM_RELEASE_TX_INFO3_PEER_ID		GENMASK(15, 0)
#define HAL_WBM_RELEASE_TX_INFO3_TID			GENMASK(19, 16)
#define HAL_WBM_RELEASE_TX_INFO3_SW_BUFFER_COOKIE_19_12	GENMASK(27, 20)
#define HAL_WBM_RELEASE_TX_INFO3_LOOPING_COUNT		GENMASK(31, 28)

struct hal_wbm_release_ring_tx {
	struct ath12k_buffer_addr buf_addr_info;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	struct hal_tx_rate_stats rate_stats;
	__le32 info3;
} __packed;

#define HAL_WBM_RELEASE_RX_INFO0_REL_SRC_MODULE		GENMASK(2, 0)
#define HAL_WBM_RELEASE_RX_INFO0_BM_ACTION		GENMASK(5, 3)
#define HAL_WBM_RELEASE_RX_INFO0_DESC_TYPE		GENMASK(8, 6)
#define HAL_WBM_RELEASE_RX_INFO0_FIRST_MSDU_IDX		GENMASK(12, 9)
#define HAL_WBM_RELEASE_RX_INFO0_CC_STATUS		BIT(16)
#define HAL_WBM_RELEASE_RX_INFO0_RXDMA_PUSH_REASON	GENMASK(18, 17)
#define HAL_WBM_RELEASE_RX_INFO0_RXDMA_ERROR_CODE	GENMASK(23, 19)
#define HAL_WBM_RELEASE_RX_INFO0_REO_PUSH_REASON	GENMASK(25, 24)
#define HAL_WBM_RELEASE_RX_INFO0_REO_ERROR_CODE		GENMASK(30, 26)
#define HAL_WBM_RELEASE_RX_INFO0_WBM_INTERNAL_ERROR	BIT(31)

#define HAL_WBM_RELEASE_RX_INFO2_RING_ID		GENMASK(27, 20)
#define HAL_WBM_RELEASE_RX_INFO2_LOOPING_COUNT		GENMASK(31, 28)

struct hal_wbm_release_ring_rx {
	struct ath12k_buffer_addr buf_addr_info;
	__le32 info0;
	struct rx_mpdu_desc rx_mpdu_info;
	struct rx_msdu_desc rx_msdu_info;
	__le32 info1;
	__le32 info2;
} __packed;

#define HAL_WBM_RELEASE_RX_CC_INFO0_RBM			GENMASK(12, 9)
#define HAL_WBM_RELEASE_RX_CC_INFO1_COOKIE		GENMASK(27, 8)
/* Used when hw cc is success */
struct hal_wbm_release_ring_cc_rx {
	__le32 buf_va_lo;
	__le32 buf_va_hi;
	__le32 info0;
	struct rx_mpdu_desc rx_mpdu_info;
	struct rx_msdu_desc rx_msdu_info;
	__le32 buf_pa_lo;
	__le32 info1;
} __packed;

#define HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE		GENMASK(2, 0)
#define HAL_WBM_RELEASE_INFO0_BM_ACTION			GENMASK(5, 3)
#define HAL_WBM_RELEASE_INFO0_DESC_TYPE			GENMASK(8, 6)
#define HAL_WBM_RELEASE_INFO0_RXDMA_PUSH_REASON		GENMASK(18, 17)
#define HAL_WBM_RELEASE_INFO0_RXDMA_ERROR_CODE		GENMASK(23, 19)
#define HAL_WBM_RELEASE_INFO0_REO_PUSH_REASON		GENMASK(25, 24)
#define HAL_WBM_RELEASE_INFO0_REO_ERROR_CODE		GENMASK(30, 26)
#define HAL_WBM_RELEASE_INFO0_WBM_INTERNAL_ERROR	BIT(31)

#define HAL_WBM_RELEASE_INFO3_FIRST_MSDU		BIT(0)
#define HAL_WBM_RELEASE_INFO3_LAST_MSDU			BIT(1)
#define HAL_WBM_RELEASE_INFO3_CONTINUATION		BIT(2)

#define HAL_WBM_RELEASE_INFO5_LOOPING_COUNT		GENMASK(31, 28)
#define HAL_ENCRYPT_TYPE_MAX 12

struct hal_wbm_release_ring {
	struct ath12k_buffer_addr buf_addr_info;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 info4;
	__le32 info5;
} __packed;

/* hal_wbm_release_ring
 *
 *	Producer: SW/TQM/RXDMA/REO/SWITCH
 *	Consumer: WBM/SW/FW
 *
 * HTT tx status is overlaid on wbm_release ring on 4-byte words 2, 3, 4 and 5
 * for software based completions.
 *
 * buf_addr_info
 *	Details of the physical address of the buffer or link descriptor.
 *
 * release_source_module
 *	Indicates which module initiated the release of this buffer/descriptor.
 *	Values are defined in enum %HAL_WBM_REL_SRC_MODULE_.
 *
 * buffer_or_desc_type
 *	Field only valid when WBM is marked as the return_buffer_manager in
 *	the Released_Buffer_address_info. Indicates that type of buffer or
 *	descriptor is being released. Values are in enum %HAL_WBM_REL_DESC_TYPE.
 *
 * wbm_internal_error
 *	Is set when WBM got a buffer pointer but the action was to push it to
 *	the idle link descriptor ring or do link related activity OR
 *	Is set when WBM got a link buffer pointer but the action was to push it
 *	to the buffer descriptor ring.
 *
 * looping_count
 *	A count value that indicates the number of times the
 *	producer of entries into the Buffer Manager Ring has looped
 *	around the ring.
 *
 *	At initialization time, this value is set to 0. On the
 *	first loop, this value is set to 1. After the max value is
 *	reached allowed by the number of bits for this field, the
 *	count value continues with 0 again.
 *
 *	In case SW is the consumer of the ring entries, it can
 *	use this field to figure out up to where the producer of
 *	entries has created new entries. This eliminates the need to
 *	check where the head pointer' of the ring is located once
 *	the SW starts processing an interrupt indicating that new
 *	entries have been put into this ring...
 *
 *	Also note that SW if it wants only needs to look at the
 *	LSB bit of this count value.
 */

/**
 * enum hal_wbm_tqm_rel_reason - TQM release reason code
 * @HAL_WBM_TQM_REL_REASON_FRAME_ACKED: ACK or BACK received for the frame
 * @HAL_WBM_TQM_REL_REASON_CMD_REMOVE_MPDU: Command remove_mpdus initiated by SW
 * @HAL_WBM_TQM_REL_REASON_CMD_REMOVE_TX: Command remove transmitted_mpdus
 *	initiated by sw.
 * @HAL_WBM_TQM_REL_REASON_CMD_REMOVE_NOTX: Command remove untransmitted_mpdus
 *	initiated by sw.
 * @HAL_WBM_TQM_REL_REASON_CMD_REMOVE_AGED_FRAMES: Command remove aged msdus or
 *	mpdus.
 * @HAL_WBM_TQM_REL_REASON_CMD_REMOVE_RESEAON1: Remove command initiated by
 *	fw with fw_reason1.
 * @HAL_WBM_TQM_REL_REASON_CMD_REMOVE_RESEAON2: Remove command initiated by
 *	fw with fw_reason2.
 * @HAL_WBM_TQM_REL_REASON_CMD_REMOVE_RESEAON3: Remove command initiated by
 *	fw with fw_reason3.
 * @HAL_WBM_TQM_REL_REASON_CMD_DISABLE_QUEUE: Remove command initiated by
 *	fw with disable queue.
 * @HAL_WBM_TQM_REL_REASON_CMD_TILL_NONMATCHING: Remove command initiated by
 *	fw to remove all mpdu until 1st non-match.
 * @HAL_WBM_TQM_REL_REASON_DROP_THRESHOLD: Dropped due to drop threshold
 *	criteria
 * @HAL_WBM_TQM_REL_REASON_DROP_LINK_DESC_UNAVAIL: Dropped due to link desc
 *	not available
 * @HAL_WBM_TQM_REL_REASON_DROP_OR_INVALID_MSDU: Dropped due drop bit set or
 *	null flow
 * @HAL_WBM_TQM_REL_REASON_MULTICAST_DROP: Dropped due mcast drop set for VDEV
 * @HAL_WBM_TQM_REL_REASON_VDEV_MISMATCH_DROP: Dropped due to being set with
 *	'TCL_drop_reason'
 */
enum hal_wbm_tqm_rel_reason {
	HAL_WBM_TQM_REL_REASON_FRAME_ACKED,
	HAL_WBM_TQM_REL_REASON_CMD_REMOVE_MPDU,
	HAL_WBM_TQM_REL_REASON_CMD_REMOVE_TX,
	HAL_WBM_TQM_REL_REASON_CMD_REMOVE_NOTX,
	HAL_WBM_TQM_REL_REASON_CMD_REMOVE_AGED_FRAMES,
	HAL_WBM_TQM_REL_REASON_CMD_REMOVE_RESEAON1,
	HAL_WBM_TQM_REL_REASON_CMD_REMOVE_RESEAON2,
	HAL_WBM_TQM_REL_REASON_CMD_REMOVE_RESEAON3,
	HAL_WBM_TQM_REL_REASON_CMD_DISABLE_QUEUE,
	HAL_WBM_TQM_REL_REASON_CMD_TILL_NONMATCHING,
	HAL_WBM_TQM_REL_REASON_DROP_THRESHOLD,
	HAL_WBM_TQM_REL_REASON_DROP_LINK_DESC_UNAVAIL,
	HAL_WBM_TQM_REL_REASON_DROP_OR_INVALID_MSDU,
	HAL_WBM_TQM_REL_REASON_MULTICAST_DROP,
	HAL_WBM_TQM_REL_REASON_VDEV_MISMATCH_DROP,
};

struct hal_wbm_buffer_ring {
	struct ath12k_buffer_addr buf_addr_info;
};

enum hal_mon_end_reason {
	HAL_MON_STATUS_BUFFER_FULL,
	HAL_MON_FLUSH_DETECTED,
	HAL_MON_END_OF_PPDU,
	HAL_MON_PPDU_TRUNCATED,
};

#define HAL_SW_MONITOR_RING_INFO0_RXDMA_PUSH_REASON	GENMASK(1, 0)
#define HAL_SW_MONITOR_RING_INFO0_RXDMA_ERROR_CODE	GENMASK(6, 2)
#define HAL_SW_MONITOR_RING_INFO0_MPDU_FRAGMENT_NUMBER	GENMASK(10, 7)
#define HAL_SW_MONITOR_RING_INFO0_FRAMELESS_BAR		BIT(11)
#define HAL_SW_MONITOR_RING_INFO0_STATUS_BUF_COUNT	GENMASK(15, 12)
#define HAL_SW_MONITOR_RING_INFO0_END_OF_PPDU		BIT(16)

#define HAL_SW_MONITOR_RING_INFO1_PHY_PPDU_ID	GENMASK(15, 0)
#define HAL_SW_MONITOR_RING_INFO1_RING_ID	GENMASK(27, 20)
#define HAL_SW_MONITOR_RING_INFO1_LOOPING_COUNT	GENMASK(31, 28)

struct hal_sw_monitor_ring {
	struct ath12k_buffer_addr buf_addr_info;
	struct rx_mpdu_desc rx_mpdu_info;
	struct ath12k_buffer_addr status_buff_addr_info;
	__le32 info0; /* %HAL_SW_MONITOR_RING_INFO0 */
	__le32 info1; /* %HAL_SW_MONITOR_RING_INFO1 */
} __packed;

/* hal_sw_monitor_ring
 *
 *		Producer: RXDMA
 *		Consumer: REO/SW/FW
 * buf_addr_info
 *              Details of the physical address of a buffer or MSDU
 *              link descriptor.
 *
 * rx_mpdu_info
 *              Details related to the MPDU being pushed to SW, valid
 *              only if end_of_ppdu is set to 0.
 *
 * status_buff_addr_info
 *		Details of the physical address of the first status
 *		buffer used for the PPDU (either the PPDU that included the
 *		MPDU being pushed to SW if end_of_ppdu = 0, or the PPDU
 *		whose end is indicated through end_of_ppdu = 1)
 *
 * rxdma_push_reason
 *		Indicates why RXDMA pushed the frame to this ring
 *
 *		<enum 0 rxdma_error_detected> RXDMA detected an error an
 *		pushed this frame to this queue
 *
 *		<enum 1 rxdma_routing_instruction> RXDMA pushed the
 *		frame to this queue per received routing instructions. No
 *		error within RXDMA was detected
 *
 *		<enum 2 rxdma_rx_flush> RXDMA received an RX_FLUSH. As a
 *		result the MSDU link descriptor might not have the
 *		last_msdu_in_mpdu_flag set, but instead WBM might just see a
 *		NULL pointer in the MSDU link descriptor. This is to be
 *		considered a normal condition for this scenario.
 *
 * rxdma_error_code
 *		Field only valid when rxdma_push_reason is set to
 *		'rxdma_error_detected.'
 *
 *		<enum 0 rxdma_overflow_err>MPDU frame is not complete
 *		due to a FIFO overflow error in RXPCU.
 *
 *		<enum 1 rxdma_mpdu_length_err>MPDU frame is not complete
 *		due to receiving incomplete MPDU from the PHY
 *
 *		<enum 3 rxdma_decrypt_err>CRYPTO reported a decryption
 *		error or CRYPTO received an encrypted frame, but did not get
 *		a valid corresponding key id in the peer entry.
 *
 *		<enum 4 rxdma_tkip_mic_err>CRYPTO reported a TKIP MIC
 *		error
 *
 *		<enum 5 rxdma_unecrypted_err>CRYPTO reported an
 *		unencrypted frame error when encrypted was expected
 *
 *		<enum 6 rxdma_msdu_len_err>RX OLE reported an MSDU
 *		length error
 *
 *		<enum 7 rxdma_msdu_limit_err>RX OLE reported that max
 *		number of MSDUs allowed in an MPDU got exceeded
 *
 *		<enum 8 rxdma_wifi_parse_err>RX OLE reported a parsing
 *		error
 *
 *		<enum 9 rxdma_amsdu_parse_err>RX OLE reported an A-MSDU
 *		parsing error
 *
 *		<enum 10 rxdma_sa_timeout_err>RX OLE reported a timeout
 *		during SA search
 *
 *		<enum 11 rxdma_da_timeout_err>RX OLE reported a timeout
 *		during DA search
 *
 *		<enum 12 rxdma_flow_timeout_err>RX OLE reported a
 *		timeout during flow search
 *
 *		<enum 13 rxdma_flush_request>RXDMA received a flush
 *		request
 *
 *		<enum 14 rxdma_amsdu_fragment_err>Rx PCU reported A-MSDU
 *		present as well as a fragmented MPDU.
 *
 * mpdu_fragment_number
 *		Field only valid when Reo_level_mpdu_frame_info.
 *		Rx_mpdu_desc_info_details.Fragment_flag is set and
 *		end_of_ppdu is set to 0.
 *
 *		The fragment number from the 802.11 header.
 *
 *		Note that the sequence number is embedded in the field:
 *		Reo_level_mpdu_frame_info. Rx_mpdu_desc_info_details.
 *		Mpdu_sequence_number
 *
 * frameless_bar
 *		When set, this SW monitor ring struct contains BAR info
 *		from a multi TID BAR frame. The original multi TID BAR frame
 *		itself contained all the REO info for the first TID, but all
 *		the subsequent TID info and their linkage to the REO
 *		descriptors is passed down as 'frameless' BAR info.
 *
 *		The only fields valid in this descriptor when this bit
 *		is within the
 *
 *		Reo_level_mpdu_frame_info:
 *		   Within Rx_mpdu_desc_info_details:
 *			Mpdu_Sequence_number
 *			BAR_frame
 *			Peer_meta_data
 *			All other fields shall be set to 0.
 *
 * status_buf_count
 *		A count of status buffers used so far for the PPDU
 *		(either the PPDU that included the MPDU being pushed to SW
 *		if end_of_ppdu = 0, or the PPDU whose end is indicated
 *		through end_of_ppdu = 1)
 *
 * end_of_ppdu
 *		Some hw RXDMA can be configured to generate a separate
 *		'SW_MONITOR_RING' descriptor at the end of a PPDU (either
 *		through an 'RX_PPDU_END' TLV or through an 'RX_FLUSH') to
 *		demarcate PPDUs.
 *
 *		For such a descriptor, this bit is set to 1 and fields
 *		Reo_level_mpdu_frame_info, mpdu_fragment_number and
 *		Frameless_bar are all set to 0.
 *
 *		Otherwise this bit is set to 0.
 *
 * phy_ppdu_id
 *		A PPDU counter value that PHY increments for every PPDU
 *		received
 *
 *		The counter value wraps around. Some hw RXDMA can be
 *		configured to copy this from the RX_PPDU_START TLV for every
 *		output descriptor.
 *
 * ring_id
 *		For debugging.
 *		This field is filled in by the SRNG module.
 *		It help to identify the ring that is being looked
 *
 * looping_count
 *		For debugging.
 *		This field is filled in by the SRNG module.
 *
 *		A count value that indicates the number of times the
 *		producer of entries into this Ring has looped around the
 *		ring.
 *		At initialization time, this value is set to 0. On the
 *		first loop, this value is set to 1. After the max value is
 *		reached allowed by the number of bits for this field, the
 *		count value continues with 0 again.
 *
 *		In case SW is the consumer of the ring entries, it can
 *		use this field to figure out up to where the producer of
 *		entries has created new entries. This eliminates the need to
 *		check where the head pointer' of the ring is located once
 *		the SW starts processing an interrupt indicating that new
 *		entries have been put into this ring...
 */

enum hal_desc_owner {
	HAL_DESC_OWNER_WBM,
	HAL_DESC_OWNER_SW,
	HAL_DESC_OWNER_TQM,
	HAL_DESC_OWNER_RXDMA,
	HAL_DESC_OWNER_REO,
	HAL_DESC_OWNER_SWITCH,
};

enum hal_desc_buf_type {
	HAL_DESC_BUF_TYPE_TX_MSDU_LINK,
	HAL_DESC_BUF_TYPE_TX_MPDU_LINK,
	HAL_DESC_BUF_TYPE_TX_MPDU_QUEUE_HEAD,
	HAL_DESC_BUF_TYPE_TX_MPDU_QUEUE_EXT,
	HAL_DESC_BUF_TYPE_TX_FLOW,
	HAL_DESC_BUF_TYPE_TX_BUFFER,
	HAL_DESC_BUF_TYPE_RX_MSDU_LINK,
	HAL_DESC_BUF_TYPE_RX_MPDU_LINK,
	HAL_DESC_BUF_TYPE_RX_REO_QUEUE,
	HAL_DESC_BUF_TYPE_RX_REO_QUEUE_EXT,
	HAL_DESC_BUF_TYPE_RX_BUFFER,
	HAL_DESC_BUF_TYPE_IDLE_LINK,
};

#define HAL_DESC_REO_OWNED		4
#define HAL_DESC_REO_QUEUE_DESC		8
#define HAL_DESC_REO_QUEUE_EXT_DESC	9
#define HAL_DESC_REO_NON_QOS_TID	16

#define HAL_DESC_HDR_INFO0_OWNER	GENMASK(3, 0)
#define HAL_DESC_HDR_INFO0_BUF_TYPE	GENMASK(7, 4)
#define HAL_DESC_HDR_INFO0_DBG_RESERVED	GENMASK(31, 8)

struct hal_desc_header {
	__le32 info0;
} __packed;

struct hal_rx_mpdu_link_ptr {
	struct ath12k_buffer_addr addr_info;
} __packed;

struct hal_rx_msdu_details {
	struct ath12k_buffer_addr buf_addr_info;
	struct rx_msdu_desc rx_msdu_info;
	struct rx_msdu_ext_desc rx_msdu_ext_info;
} __packed;

#define HAL_RX_MSDU_LNK_INFO0_RX_QUEUE_NUMBER		GENMASK(15, 0)
#define HAL_RX_MSDU_LNK_INFO0_FIRST_MSDU_LNK		BIT(16)

struct hal_rx_msdu_link {
	struct hal_desc_header desc_hdr;
	struct ath12k_buffer_addr buf_addr_info;
	__le32 info0;
	__le32 pn[4];
	struct hal_rx_msdu_details msdu_link[6];
} __packed;

struct hal_rx_reo_queue_ext {
	struct hal_desc_header desc_hdr;
	__le32 rsvd;
	struct hal_rx_mpdu_link_ptr mpdu_link[15];
} __packed;

/* hal_rx_reo_queue_ext
 *	Consumer: REO
 *	Producer: REO
 *
 * descriptor_header
 *	Details about which module owns this struct.
 *
 * mpdu_link
 *	Pointer to the next MPDU_link descriptor in the MPDU queue.
 */

enum hal_rx_reo_queue_pn_size {
	HAL_RX_REO_QUEUE_PN_SIZE_24,
	HAL_RX_REO_QUEUE_PN_SIZE_48,
	HAL_RX_REO_QUEUE_PN_SIZE_128,
};

#define HAL_RX_REO_QUEUE_RX_QUEUE_NUMBER		GENMASK(15, 0)

#define HAL_RX_REO_QUEUE_INFO0_VLD			BIT(0)
#define HAL_RX_REO_QUEUE_INFO0_ASSOC_LNK_DESC_COUNTER	GENMASK(2, 1)
#define HAL_RX_REO_QUEUE_INFO0_DIS_DUP_DETECTION	BIT(3)
#define HAL_RX_REO_QUEUE_INFO0_SOFT_REORDER_EN		BIT(4)
#define HAL_RX_REO_QUEUE_INFO0_AC			GENMASK(6, 5)
#define HAL_RX_REO_QUEUE_INFO0_BAR			BIT(7)
#define HAL_RX_REO_QUEUE_INFO0_RETRY			BIT(8)
#define HAL_RX_REO_QUEUE_INFO0_CHECK_2K_MODE		BIT(9)
#define HAL_RX_REO_QUEUE_INFO0_OOR_MODE			BIT(10)
#define HAL_RX_REO_QUEUE_INFO0_BA_WINDOW_SIZE		GENMASK(20, 11)
#define HAL_RX_REO_QUEUE_INFO0_PN_CHECK			BIT(21)
#define HAL_RX_REO_QUEUE_INFO0_EVEN_PN			BIT(22)
#define HAL_RX_REO_QUEUE_INFO0_UNEVEN_PN		BIT(23)
#define HAL_RX_REO_QUEUE_INFO0_PN_HANDLE_ENABLE		BIT(24)
#define HAL_RX_REO_QUEUE_INFO0_PN_SIZE			GENMASK(26, 25)
#define HAL_RX_REO_QUEUE_INFO0_IGNORE_AMPDU_FLG		BIT(27)

#define HAL_RX_REO_QUEUE_INFO1_SVLD			BIT(0)
#define HAL_RX_REO_QUEUE_INFO1_SSN			GENMASK(12, 1)
#define HAL_RX_REO_QUEUE_INFO1_CURRENT_IDX		GENMASK(22, 13)
#define HAL_RX_REO_QUEUE_INFO1_SEQ_2K_ERR		BIT(23)
#define HAL_RX_REO_QUEUE_INFO1_PN_ERR			BIT(24)
#define HAL_RX_REO_QUEUE_INFO1_PN_VALID			BIT(31)

#define HAL_RX_REO_QUEUE_INFO2_MPDU_COUNT		GENMASK(6, 0)
#define HAL_RX_REO_QUEUE_INFO2_MSDU_COUNT		(31, 7)

#define HAL_RX_REO_QUEUE_INFO3_TIMEOUT_COUNT		GENMASK(9, 4)
#define HAL_RX_REO_QUEUE_INFO3_FWD_DUE_TO_BAR_CNT	GENMASK(15, 10)
#define HAL_RX_REO_QUEUE_INFO3_DUPLICATE_COUNT		GENMASK(31, 16)

#define HAL_RX_REO_QUEUE_INFO4_FRAME_IN_ORD_COUNT	GENMASK(23, 0)
#define HAL_RX_REO_QUEUE_INFO4_BAR_RECVD_COUNT		GENMASK(31, 24)

#define HAL_RX_REO_QUEUE_INFO5_LATE_RX_MPDU_COUNT	GENMASK(11, 0)
#define HAL_RX_REO_QUEUE_INFO5_WINDOW_JUMP_2K		GENMASK(15, 12)
#define HAL_RX_REO_QUEUE_INFO5_HOLE_COUNT		GENMASK(31, 16)

struct hal_rx_reo_queue {
	struct hal_desc_header desc_hdr;
	__le32 rx_queue_num;
	__le32 info0;
	__le32 info1;
	__le32 pn[4];
	__le32 last_rx_enqueue_timestamp;
	__le32 last_rx_dequeue_timestamp;
	__le32 next_aging_queue[2];
	__le32 prev_aging_queue[2];
	__le32 rx_bitmap[9];
	__le32 info2;
	__le32 info3;
	__le32 info4;
	__le32 processed_mpdus;
	__le32 processed_msdus;
	__le32 processed_total_bytes;
	__le32 info5;
	__le32 rsvd[2];
	struct hal_rx_reo_queue_ext ext_desc[];
} __packed;

/* hal_rx_reo_queue
 *
 * descriptor_header
 *	Details about which module owns this struct. Note that sub field
 *	Buffer_type shall be set to receive_reo_queue_descriptor.
 *
 * receive_queue_number
 *	Indicates the MPDU queue ID to which this MPDU link descriptor belongs.
 *
 * vld
 *	Valid bit indicating a session is established and the queue descriptor
 *	is valid.
 * associated_link_descriptor_counter
 *	Indicates which of the 3 link descriptor counters shall be incremented
 *	or decremented when link descriptors are added or removed from this
 *	flow queue.
 * disable_duplicate_detection
 *	When set, do not perform any duplicate detection.
 * soft_reorder_enable
 *	When set, REO has been instructed to not perform the actual re-ordering
 *	of frames for this queue, but just to insert the reorder opcodes.
 * ac
 *	Indicates the access category of the queue descriptor.
 * bar
 *	Indicates if BAR has been received.
 * retry
 *	Retry bit is checked if this bit is set.
 * chk_2k_mode
 *	Indicates what type of operation is expected from Reo when the received
 *	frame SN falls within the 2K window.
 * oor_mode
 *	Indicates what type of operation is expected when the received frame
 *	falls within the OOR window.
 * ba_window_size
 *	Indicates the negotiated (window size + 1). Max of 256 bits.
 *
 *	A value 255 means 256 bitmap, 63 means 64 bitmap, 0 (means non-BA
 *	session, with window size of 0). The 3 values here are the main values
 *	validated, but other values should work as well.
 *
 *	A BA window size of 0 (=> one frame entry bitmat), means that there is
 *	no additional rx_reo_queue_ext desc. following rx_reo_queue in memory.
 *	A BA window size of 1 - 105, means that there is 1 rx_reo_queue_ext.
 *	A BA window size of 106 - 210, means that there are 2 rx_reo_queue_ext.
 *	A BA window size of 211 - 256, means that there are 3 rx_reo_queue_ext.
 * pn_check_needed, pn_shall_be_even, pn_shall_be_uneven, pn_handling_enable,
 * pn_size
 *	REO shall perform the PN increment check, even number check, uneven
 *	number check, PN error check and size of the PN field check.
 * ignore_ampdu_flag
 *	REO shall ignore the ampdu_flag on entrance descriptor for this queue.
 *
 * svld
 *	Sequence number in next field is valid one.
 * ssn
 *	 Starting Sequence number of the session.
 * current_index
 *	Points to last forwarded packet
 * seq_2k_error_detected_flag
 *	REO has detected a 2k error jump in the sequence number and from that
 *	moment forward, all new frames are forwarded directly to FW, without
 *	duplicate detect, reordering, etc.
 * pn_error_detected_flag
 *	REO has detected a PN error.
 */

#define HAL_REO_UPD_RX_QUEUE_INFO0_QUEUE_ADDR_HI		GENMASK(7, 0)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_RX_QUEUE_NUM		BIT(8)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_VLD			BIT(9)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_ASSOC_LNK_DESC_CNT	BIT(10)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_DIS_DUP_DETECTION	BIT(11)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SOFT_REORDER_EN		BIT(12)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_AC			BIT(13)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_BAR			BIT(14)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_RETRY			BIT(15)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_CHECK_2K_MODE		BIT(16)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_OOR_MODE			BIT(17)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_BA_WINDOW_SIZE		BIT(18)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_CHECK			BIT(19)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_EVEN_PN			BIT(20)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_UNEVEN_PN		BIT(21)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_HANDLE_ENABLE		BIT(22)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_SIZE			BIT(23)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_IGNORE_AMPDU_FLG		BIT(24)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SVLD			BIT(25)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SSN			BIT(26)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_SEQ_2K_ERR		BIT(27)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_ERR			BIT(28)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN_VALID			BIT(29)
#define HAL_REO_UPD_RX_QUEUE_INFO0_UPD_PN			BIT(30)

#define HAL_REO_UPD_RX_QUEUE_INFO1_RX_QUEUE_NUMBER		GENMASK(15, 0)
#define HAL_REO_UPD_RX_QUEUE_INFO1_VLD				BIT(16)
#define HAL_REO_UPD_RX_QUEUE_INFO1_ASSOC_LNK_DESC_COUNTER	GENMASK(18, 17)
#define HAL_REO_UPD_RX_QUEUE_INFO1_DIS_DUP_DETECTION		BIT(19)
#define HAL_REO_UPD_RX_QUEUE_INFO1_SOFT_REORDER_EN		BIT(20)
#define HAL_REO_UPD_RX_QUEUE_INFO1_AC				GENMASK(22, 21)
#define HAL_REO_UPD_RX_QUEUE_INFO1_BAR				BIT(23)
#define HAL_REO_UPD_RX_QUEUE_INFO1_RETRY			BIT(24)
#define HAL_REO_UPD_RX_QUEUE_INFO1_CHECK_2K_MODE		BIT(25)
#define HAL_REO_UPD_RX_QUEUE_INFO1_OOR_MODE			BIT(26)
#define HAL_REO_UPD_RX_QUEUE_INFO1_PN_CHECK			BIT(27)
#define HAL_REO_UPD_RX_QUEUE_INFO1_EVEN_PN			BIT(28)
#define HAL_REO_UPD_RX_QUEUE_INFO1_UNEVEN_PN			BIT(29)
#define HAL_REO_UPD_RX_QUEUE_INFO1_PN_HANDLE_ENABLE		BIT(30)
#define HAL_REO_UPD_RX_QUEUE_INFO1_IGNORE_AMPDU_FLG		BIT(31)

#define HAL_REO_UPD_RX_QUEUE_INFO2_BA_WINDOW_SIZE		GENMASK(9, 0)
#define HAL_REO_UPD_RX_QUEUE_INFO2_PN_SIZE			GENMASK(11, 10)
#define HAL_REO_UPD_RX_QUEUE_INFO2_SVLD				BIT(12)
#define HAL_REO_UPD_RX_QUEUE_INFO2_SSN				GENMASK(24, 13)
#define HAL_REO_UPD_RX_QUEUE_INFO2_SEQ_2K_ERR			BIT(25)
#define HAL_REO_UPD_RX_QUEUE_INFO2_PN_ERR			BIT(26)
#define HAL_REO_UPD_RX_QUEUE_INFO2_PN_VALID			BIT(27)

struct hal_reo_update_rx_queue {
	struct hal_reo_cmd_hdr cmd;
	__le32 queue_addr_lo;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 pn[4];
} __packed;

struct hal_rx_reo_queue_1k {
	struct hal_desc_header desc_hdr;
	__le32 rx_bitmap_1023_288[23];
	__le32 reserved[8];
} __packed;

#define HAL_REO_UNBLOCK_CACHE_INFO0_UNBLK_CACHE		BIT(0)
#define HAL_REO_UNBLOCK_CACHE_INFO0_RESOURCE_IDX	GENMASK(2, 1)

struct hal_reo_unblock_cache {
	struct hal_reo_cmd_hdr cmd;
	__le32 info0;
	__le32 rsvd[7];
} __packed;

enum hal_reo_exec_status {
	HAL_REO_EXEC_STATUS_SUCCESS,
	HAL_REO_EXEC_STATUS_BLOCKED,
	HAL_REO_EXEC_STATUS_FAILED,
	HAL_REO_EXEC_STATUS_RESOURCE_BLOCKED,
};

#define HAL_REO_STATUS_HDR_INFO0_STATUS_NUM	GENMASK(15, 0)
#define HAL_REO_STATUS_HDR_INFO0_EXEC_TIME	GENMASK(25, 16)
#define HAL_REO_STATUS_HDR_INFO0_EXEC_STATUS	GENMASK(27, 26)

struct hal_reo_status_hdr {
	__le32 info0;
	__le32 timestamp;
} __packed;

/* hal_reo_status_hdr
 *		Producer: REO
 *		Consumer: SW
 *
 * status_num
 *		The value in this field is equal to value of the reo command
 *		number. This field helps to correlate the statuses with the REO
 *		commands.
 *
 * execution_time (in us)
 *		The amount of time REO took to execute the command. Note that
 *		this time does not include the duration of the command waiting
 *		in the command ring, before the execution started.
 *
 * execution_status
 *		Execution status of the command. Values are defined in
 *		enum %HAL_REO_EXEC_STATUS_.
 */
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_SSN		GENMASK(11, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_CUR_IDX		GENMASK(21, 12)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MPDU_COUNT		GENMASK(6, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MSDU_COUNT		GENMASK(31, 7)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_WINDOW_JMP2K	GENMASK(3, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_TIMEOUT_COUNT	GENMASK(9, 4)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_FDTB_COUNT		GENMASK(15, 10)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_DUPLICATE_COUNT	GENMASK(31, 16)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_FIO_COUNT		GENMASK(23, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_BAR_RCVD_CNT	GENMASK(31, 24)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_LATE_RX_MPDU	GENMASK(11, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_HOLE_COUNT		GENMASK(27, 12)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO5_LOOPING_CNT	GENMASK(31, 28)

struct hal_reo_get_queue_stats_status {
	struct hal_reo_status_hdr hdr;
	__le32 info0;
	__le32 pn[4];
	__le32 last_rx_enqueue_timestamp;
	__le32 last_rx_dequeue_timestamp;
	__le32 rx_bitmap[9];
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 num_mpdu_frames;
	__le32 num_msdu_frames;
	__le32 total_bytes;
	__le32 info4;
	__le32 info5;
} __packed;

/* hal_reo_get_queue_stats_status
 *		Producer: REO
 *		Consumer: SW
 *
 * status_hdr
 *		Details that can link this status with the original command. It
 *		also contains info on how long REO took to execute this command.
 *
 * ssn
 *		Starting Sequence number of the session, this changes whenever
 *		window moves (can be filled by SW then maintained by REO).
 *
 * current_index
 *		Points to last forwarded packet.
 *
 * pn
 *		Bits of the PN number.
 *
 * last_rx_enqueue_timestamp
 * last_rx_dequeue_timestamp
 *		Timestamp of arrival of the last MPDU for this queue and
 *		Timestamp of forwarding an MPDU accordingly.
 *
 * rx_bitmap
 *		When a bit is set, the corresponding frame is currently held
 *		in the re-order queue. The bitmap  is Fully managed by HW.
 *
 * current_mpdu_count
 * current_msdu_count
 *		The number of MPDUs and MSDUs in the queue.
 *
 * timeout_count
 *		The number of times REO started forwarding frames even though
 *		there is a hole in the bitmap. Forwarding reason is timeout.
 *
 * forward_due_to_bar_count
 *		The number of times REO started forwarding frames even though
 *		there is a hole in the bitmap. Fwd reason is reception of BAR.
 *
 * duplicate_count
 *		The number of duplicate frames that have been detected.
 *
 * frames_in_order_count
 *		The number of frames that have been received in order (without
 *		a hole that prevented them from being forwarded immediately).
 *
 * bar_received_count
 *		The number of times a BAR frame is received.
 *
 * mpdu_frames_processed_count
 * msdu_frames_processed_count
 *		The total number of MPDU/MSDU frames that have been processed.
 *
 * total_bytes
 *		An approximation of the number of bytes received for this queue.
 *
 * late_receive_mpdu_count
 *		The number of MPDUs received after the window had already moved
 *		on. The 'late' sequence window is defined as
 *		(Window SSN - 256) - (Window SSN - 1).
 *
 * window_jump_2k
 *		The number of times the window moved more than 2K
 *
 * hole_count
 *		The number of times a hole was created in the receive bitmap.
 *
 * looping_count
 *		A count value that indicates the number of times the producer of
 *		entries into this Ring has looped around the ring.
 */

#define HAL_REO_STATUS_LOOP_CNT			GENMASK(31, 28)

#define HAL_REO_FLUSH_QUEUE_INFO0_ERR_DETECTED	BIT(0)
#define HAL_REO_FLUSH_QUEUE_INFO0_RSVD		GENMASK(31, 1)
#define HAL_REO_FLUSH_QUEUE_INFO1_RSVD		GENMASK(27, 0)

struct hal_reo_flush_queue_status {
	struct hal_reo_status_hdr hdr;
	__le32 info0;
	__le32 rsvd0[21];
	__le32 info1;
} __packed;

/* hal_reo_flush_queue_status
 *		Producer: REO
 *		Consumer: SW
 *
 * status_hdr
 *		Details that can link this status with the original command. It
 *		also contains info on how long REO took to execute this command.
 *
 * error_detected
 *		Status of blocking resource
 *
 *		0 - No error has been detected while executing this command
 *		1 - Error detected. The resource to be used for blocking was
 *		    already in use.
 *
 * looping_count
 *		A count value that indicates the number of times the producer of
 *		entries into this Ring has looped around the ring.
 */

#define HAL_REO_FLUSH_CACHE_STATUS_INFO0_IS_ERR			BIT(0)
#define HAL_REO_FLUSH_CACHE_STATUS_INFO0_BLOCK_ERR_CODE		GENMASK(2, 1)
#define HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_STATUS_HIT	BIT(8)
#define HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_DESC_TYPE	GENMASK(11, 9)
#define HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_CLIENT_ID	GENMASK(15, 12)
#define HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_ERR		GENMASK(17, 16)
#define HAL_REO_FLUSH_CACHE_STATUS_INFO0_FLUSH_COUNT		GENMASK(25, 18)

struct hal_reo_flush_cache_status {
	struct hal_reo_status_hdr hdr;
	__le32 info0;
	__le32 rsvd0[21];
	__le32 info1;
} __packed;

/* hal_reo_flush_cache_status
 *		Producer: REO
 *		Consumer: SW
 *
 * status_hdr
 *		Details that can link this status with the original command. It
 *		also contains info on how long REO took to execute this command.
 *
 * error_detected
 *		Status for blocking resource handling
 *
 *		0 - No error has been detected while executing this command
 *		1 - An error in the blocking resource management was detected
 *
 * block_error_details
 *		only valid when error_detected is set
 *
 *		0 - No blocking related errors found
 *		1 - Blocking resource is already in use
 *		2 - Resource requested to be unblocked, was not blocked
 *
 * cache_controller_flush_status_hit
 *		The status that the cache controller returned on executing the
 *		flush command.
 *
 *		0 - miss; 1 - hit
 *
 * cache_controller_flush_status_desc_type
 *		Flush descriptor type
 *
 * cache_controller_flush_status_client_id
 *		Module who made the flush request
 *
 *		In REO, this is always 0
 *
 * cache_controller_flush_status_error
 *		Error condition
 *
 *		0 - No error found
 *		1 - HW interface is still busy
 *		2 - Line currently locked. Used for one line flush command
 *		3 - At least one line is still locked.
 *		    Used for cache flush command.
 *
 * cache_controller_flush_count
 *		The number of lines that were actually flushed out
 *
 * looping_count
 *		A count value that indicates the number of times the producer of
 *		entries into this Ring has looped around the ring.
 */

#define HAL_REO_UNBLOCK_CACHE_STATUS_INFO0_IS_ERR	BIT(0)
#define HAL_REO_UNBLOCK_CACHE_STATUS_INFO0_TYPE		BIT(1)

struct hal_reo_unblock_cache_status {
	struct hal_reo_status_hdr hdr;
	__le32 info0;
	__le32 rsvd0[21];
	__le32 info1;
} __packed;

/* hal_reo_unblock_cache_status
 *		Producer: REO
 *		Consumer: SW
 *
 * status_hdr
 *		Details that can link this status with the original command. It
 *		also contains info on how long REO took to execute this command.
 *
 * error_detected
 *		0 - No error has been detected while executing this command
 *		1 - The blocking resource was not in use, and therefore it could
 *		    not be unblocked.
 *
 * unblock_type
 *		Reference to the type of unblock command
 *		0 - Unblock a blocking resource
 *		1 - The entire cache usage is unblock
 *
 * looping_count
 *		A count value that indicates the number of times the producer of
 *		entries into this Ring has looped around the ring.
 */

#define HAL_REO_FLUSH_TIMEOUT_STATUS_INFO0_IS_ERR		BIT(0)
#define HAL_REO_FLUSH_TIMEOUT_STATUS_INFO0_LIST_EMPTY		BIT(1)

#define HAL_REO_FLUSH_TIMEOUT_STATUS_INFO1_REL_DESC_COUNT	GENMASK(15, 0)
#define HAL_REO_FLUSH_TIMEOUT_STATUS_INFO1_FWD_BUF_COUNT	GENMASK(31, 16)

struct hal_reo_flush_timeout_list_status {
	struct hal_reo_status_hdr hdr;
	__le32 info0;
	__le32 info1;
	__le32 rsvd0[20];
	__le32 info2;
} __packed;

/* hal_reo_flush_timeout_list_status
 *		Producer: REO
 *		Consumer: SW
 *
 * status_hdr
 *		Details that can link this status with the original command. It
 *		also contains info on how long REO took to execute this command.
 *
 * error_detected
 *		0 - No error has been detected while executing this command
 *		1 - Command not properly executed and returned with error
 *
 * timeout_list_empty
 *		When set, REO has depleted the timeout list and all entries are
 *		gone.
 *
 * release_desc_count
 *		Producer: SW; Consumer: REO
 *		The number of link descriptor released
 *
 * forward_buf_count
 *		Producer: SW; Consumer: REO
 *		The number of buffers forwarded to the REO destination rings
 *
 * looping_count
 *		A count value that indicates the number of times the producer of
 *		entries into this Ring has looped around the ring.
 */

#define HAL_REO_DESC_THRESH_STATUS_INFO0_THRESH_INDEX		GENMASK(1, 0)
#define HAL_REO_DESC_THRESH_STATUS_INFO1_LINK_DESC_COUNTER0	GENMASK(23, 0)
#define HAL_REO_DESC_THRESH_STATUS_INFO2_LINK_DESC_COUNTER1	GENMASK(23, 0)
#define HAL_REO_DESC_THRESH_STATUS_INFO3_LINK_DESC_COUNTER2	GENMASK(23, 0)
#define HAL_REO_DESC_THRESH_STATUS_INFO4_LINK_DESC_COUNTER_SUM	GENMASK(25, 0)

struct hal_reo_desc_thresh_reached_status {
	struct hal_reo_status_hdr hdr;
	__le32 info0;
	__le32 info1;
	__le32 info2;
	__le32 info3;
	__le32 info4;
	__le32 rsvd0[17];
	__le32 info5;
} __packed;

/* hal_reo_desc_thresh_reached_status
 *		Producer: REO
 *		Consumer: SW
 *
 * status_hdr
 *		Details that can link this status with the original command. It
 *		also contains info on how long REO took to execute this command.
 *
 * threshold_index
 *		The index of the threshold register whose value got reached
 *
 * link_descriptor_counter0
 * link_descriptor_counter1
 * link_descriptor_counter2
 * link_descriptor_counter_sum
 *		Value of the respective counters at generation of this message
 *
 * looping_count
 *		A count value that indicates the number of times the producer of
 *		entries into this Ring has looped around the ring.
 */

#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_DATA_LENGTH	GENMASK(13, 0)
#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_L4_CSUM_STATUS	BIT(14)
#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_L3_CSUM_STATUS	BIT(15)
#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_PID		GENMASK(27, 24)
#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_QDISC		BIT(28)
#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_MULTICAST	BIT(29)
#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_MORE		BIT(30)
#define HAL_TCL_ENTRANCE_FROM_PPE_RING_INFO0_VALID_TOGGLE	BIT(31)

struct hal_tcl_entrance_from_ppe_ring {
	__le32 buffer_addr;
	__le32 info0;
} __packed;

struct hal_mon_buf_ring {
	__le32 paddr_lo;
	__le32 paddr_hi;
	__le64 cookie;
};

/* hal_mon_buf_ring
 *	Producer : SW
 *	Consumer : Monitor
 *
 * paddr_lo
 *	Lower 32-bit physical address of the buffer pointer from the source ring.
 * paddr_hi
 *	bit range 7-0 : upper 8 bit of the physical address.
 *	bit range 31-8 : reserved.
 * cookie
 *	Consumer: RxMon/TxMon 64 bit cookie of the buffers.
 */

#define HAL_MON_DEST_COOKIE_BUF_ID      GENMASK(17, 0)

#define HAL_MON_DEST_INFO0_END_OFFSET		GENMASK(11, 0)
#define HAL_MON_DEST_INFO0_END_REASON		GENMASK(17, 16)
#define HAL_MON_DEST_INFO0_INITIATOR		BIT(18)
#define HAL_MON_DEST_INFO0_EMPTY_DESC		BIT(19)
#define HAL_MON_DEST_INFO0_RING_ID		GENMASK(27, 20)
#define HAL_MON_DEST_INFO0_LOOPING_COUNT	GENMASK(31, 28)

struct hal_mon_dest_desc {
	__le32 cookie;
	__le32 reserved;
	__le32 ppdu_id;
	__le32 info0;
};

/* hal_mon_dest_ring
 *	Producer : TxMon/RxMon
 *	Consumer : SW
 * cookie
 *	bit 0 -17 buf_id to track the skb's vaddr.
 * ppdu_id
 *	Phy ppdu_id
 * end_offset
 *	The offset into status buffer where DMA ended, ie., offset to the last
 *	TLV + last TLV size.
 * flush_detected
 *	Indicates whether 'tx_flush' or 'rx_flush' occurred.
 * end_of_ppdu
 *	Indicates end of ppdu.
 * pmac_id
 *	Indicates PMAC that received from frame.
 * empty_descriptor
 *	This descriptor is written on flush or end of ppdu or end of status
 *	buffer.
 * ring_id
 *	updated by SRNG.
 * looping_count
 *	updated by SRNG.
 */

#define HAL_TX_MSDU_METADATA_INFO0_ENCRYPT_FLAG		BIT(8)
#define HAL_TX_MSDU_METADATA_INFO0_ENCRYPT_TYPE		GENMASK(16, 15)
#define HAL_TX_MSDU_METADATA_INFO0_HOST_TX_DESC_POOL	BIT(31)

struct hal_tx_msdu_metadata {
	__le32 info0;
	__le32 rsvd0[6];
} __packed;

/* hal_tx_msdu_metadata
 * valid_encrypt_type
 *		if set, encrypt type is valid
 * encrypt_type
 *		0 = NO_ENCRYPT,
 *		1 = ENCRYPT,
 *		2 ~ 3 - Reserved
 * host_tx_desc_pool
 *		If set, Firmware allocates tx_descriptors
 *		in WAL_BUFFERID_TX_HOST_DATA_EXP,instead
 *		of WAL_BUFFERID_TX_TCL_DATA_EXP.
 *		Use cases:
 *		Any time firmware uses TQM-BYPASS for Data
 *		TID, firmware expect host to set this bit.
 */

#endif /* ATH12K_HAL_DESC_H */

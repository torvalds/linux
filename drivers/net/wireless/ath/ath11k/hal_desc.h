/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */
#include "core.h"

#ifndef ATH11K_HAL_DESC_H
#define ATH11K_HAL_DESC_H

#define BUFFER_ADDR_INFO0_ADDR         GENMASK(31, 0)

#define BUFFER_ADDR_INFO1_ADDR         GENMASK(7, 0)
#define BUFFER_ADDR_INFO1_RET_BUF_MGR  GENMASK(10, 8)
#define BUFFER_ADDR_INFO1_SW_COOKIE    GENMASK(31, 11)

struct ath11k_buffer_addr {
	u32 info0;
	u32 info1;
} __packed;

/* ath11k_buffer_addr
 *
 * info0
 *		Address (lower 32 bits) of the msdu buffer or msdu extension
 *		descriptor or Link descriptor
 *
 * addr
 *		Address (upper 8 bits) of the msdu buffer or msdu extension
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
 */

enum hal_tlv_tag {
	HAL_MACTX_CBF_START                    =   0 /* 0x0 */,
	HAL_PHYRX_DATA                         =   1 /* 0x1 */,
	HAL_PHYRX_CBF_DATA_RESP                =   2 /* 0x2 */,
	HAL_PHYRX_ABORT_REQUEST                =   3 /* 0x3 */,
	HAL_PHYRX_USER_ABORT_NOTIFICATION      =   4 /* 0x4 */,
	HAL_MACTX_DATA_RESP                    =   5 /* 0x5 */,
	HAL_MACTX_CBF_DATA                     =   6 /* 0x6 */,
	HAL_MACTX_CBF_DONE                     =   7 /* 0x7 */,
	HAL_MACRX_CBF_READ_REQUEST             =   8 /* 0x8 */,
	HAL_MACRX_CBF_DATA_REQUEST             =   9 /* 0x9 */,
	HAL_MACRX_EXPECT_NDP_RECEPTION         =  10 /* 0xa */,
	HAL_MACRX_FREEZE_CAPTURE_CHANNEL       =  11 /* 0xb */,
	HAL_MACRX_NDP_TIMEOUT                  =  12 /* 0xc */,
	HAL_MACRX_ABORT_ACK                    =  13 /* 0xd */,
	HAL_MACRX_REQ_IMPLICIT_FB              =  14 /* 0xe */,
	HAL_MACRX_CHAIN_MASK                   =  15 /* 0xf */,
	HAL_MACRX_NAP_USER                     =  16 /* 0x10 */,
	HAL_MACRX_ABORT_REQUEST                =  17 /* 0x11 */,
	HAL_PHYTX_OTHER_TRANSMIT_INFO16        =  18 /* 0x12 */,
	HAL_PHYTX_ABORT_ACK                    =  19 /* 0x13 */,
	HAL_PHYTX_ABORT_REQUEST                =  20 /* 0x14 */,
	HAL_PHYTX_PKT_END                      =  21 /* 0x15 */,
	HAL_PHYTX_PPDU_HEADER_INFO_REQUEST     =  22 /* 0x16 */,
	HAL_PHYTX_REQUEST_CTRL_INFO            =  23 /* 0x17 */,
	HAL_PHYTX_DATA_REQUEST                 =  24 /* 0x18 */,
	HAL_PHYTX_BF_CV_LOADING_DONE           =  25 /* 0x19 */,
	HAL_PHYTX_NAP_ACK                      =  26 /* 0x1a */,
	HAL_PHYTX_NAP_DONE                     =  27 /* 0x1b */,
	HAL_PHYTX_OFF_ACK                      =  28 /* 0x1c */,
	HAL_PHYTX_ON_ACK                       =  29 /* 0x1d */,
	HAL_PHYTX_SYNTH_OFF_ACK                =  30 /* 0x1e */,
	HAL_PHYTX_DEBUG16                      =  31 /* 0x1f */,
	HAL_MACTX_ABORT_REQUEST                =  32 /* 0x20 */,
	HAL_MACTX_ABORT_ACK                    =  33 /* 0x21 */,
	HAL_MACTX_PKT_END                      =  34 /* 0x22 */,
	HAL_MACTX_PRE_PHY_DESC                 =  35 /* 0x23 */,
	HAL_MACTX_BF_PARAMS_COMMON             =  36 /* 0x24 */,
	HAL_MACTX_BF_PARAMS_PER_USER           =  37 /* 0x25 */,
	HAL_MACTX_PREFETCH_CV                  =  38 /* 0x26 */,
	HAL_MACTX_USER_DESC_COMMON             =  39 /* 0x27 */,
	HAL_MACTX_USER_DESC_PER_USER           =  40 /* 0x28 */,
	HAL_EXAMPLE_USER_TLV_16                =  41 /* 0x29 */,
	HAL_EXAMPLE_TLV_16                     =  42 /* 0x2a */,
	HAL_MACTX_PHY_OFF                      =  43 /* 0x2b */,
	HAL_MACTX_PHY_ON                       =  44 /* 0x2c */,
	HAL_MACTX_SYNTH_OFF                    =  45 /* 0x2d */,
	HAL_MACTX_EXPECT_CBF_COMMON            =  46 /* 0x2e */,
	HAL_MACTX_EXPECT_CBF_PER_USER          =  47 /* 0x2f */,
	HAL_MACTX_PHY_DESC                     =  48 /* 0x30 */,
	HAL_MACTX_L_SIG_A                      =  49 /* 0x31 */,
	HAL_MACTX_L_SIG_B                      =  50 /* 0x32 */,
	HAL_MACTX_HT_SIG                       =  51 /* 0x33 */,
	HAL_MACTX_VHT_SIG_A                    =  52 /* 0x34 */,
	HAL_MACTX_VHT_SIG_B_SU20               =  53 /* 0x35 */,
	HAL_MACTX_VHT_SIG_B_SU40               =  54 /* 0x36 */,
	HAL_MACTX_VHT_SIG_B_SU80               =  55 /* 0x37 */,
	HAL_MACTX_VHT_SIG_B_SU160              =  56 /* 0x38 */,
	HAL_MACTX_VHT_SIG_B_MU20               =  57 /* 0x39 */,
	HAL_MACTX_VHT_SIG_B_MU40               =  58 /* 0x3a */,
	HAL_MACTX_VHT_SIG_B_MU80               =  59 /* 0x3b */,
	HAL_MACTX_VHT_SIG_B_MU160              =  60 /* 0x3c */,
	HAL_MACTX_SERVICE                      =  61 /* 0x3d */,
	HAL_MACTX_HE_SIG_A_SU                  =  62 /* 0x3e */,
	HAL_MACTX_HE_SIG_A_MU_DL               =  63 /* 0x3f */,
	HAL_MACTX_HE_SIG_A_MU_UL               =  64 /* 0x40 */,
	HAL_MACTX_HE_SIG_B1_MU                 =  65 /* 0x41 */,
	HAL_MACTX_HE_SIG_B2_MU                 =  66 /* 0x42 */,
	HAL_MACTX_HE_SIG_B2_OFDMA              =  67 /* 0x43 */,
	HAL_MACTX_DELETE_CV                    =  68 /* 0x44 */,
	HAL_MACTX_MU_UPLINK_COMMON             =  69 /* 0x45 */,
	HAL_MACTX_MU_UPLINK_USER_SETUP         =  70 /* 0x46 */,
	HAL_MACTX_OTHER_TRANSMIT_INFO          =  71 /* 0x47 */,
	HAL_MACTX_PHY_NAP                      =  72 /* 0x48 */,
	HAL_MACTX_DEBUG                        =  73 /* 0x49 */,
	HAL_PHYRX_ABORT_ACK                    =  74 /* 0x4a */,
	HAL_PHYRX_GENERATED_CBF_DETAILS        =  75 /* 0x4b */,
	HAL_PHYRX_RSSI_LEGACY                  =  76 /* 0x4c */,
	HAL_PHYRX_RSSI_HT                      =  77 /* 0x4d */,
	HAL_PHYRX_USER_INFO                    =  78 /* 0x4e */,
	HAL_PHYRX_PKT_END                      =  79 /* 0x4f */,
	HAL_PHYRX_DEBUG                        =  80 /* 0x50 */,
	HAL_PHYRX_CBF_TRANSFER_DONE            =  81 /* 0x51 */,
	HAL_PHYRX_CBF_TRANSFER_ABORT           =  82 /* 0x52 */,
	HAL_PHYRX_L_SIG_A                      =  83 /* 0x53 */,
	HAL_PHYRX_L_SIG_B                      =  84 /* 0x54 */,
	HAL_PHYRX_HT_SIG                       =  85 /* 0x55 */,
	HAL_PHYRX_VHT_SIG_A                    =  86 /* 0x56 */,
	HAL_PHYRX_VHT_SIG_B_SU20               =  87 /* 0x57 */,
	HAL_PHYRX_VHT_SIG_B_SU40               =  88 /* 0x58 */,
	HAL_PHYRX_VHT_SIG_B_SU80               =  89 /* 0x59 */,
	HAL_PHYRX_VHT_SIG_B_SU160              =  90 /* 0x5a */,
	HAL_PHYRX_VHT_SIG_B_MU20               =  91 /* 0x5b */,
	HAL_PHYRX_VHT_SIG_B_MU40               =  92 /* 0x5c */,
	HAL_PHYRX_VHT_SIG_B_MU80               =  93 /* 0x5d */,
	HAL_PHYRX_VHT_SIG_B_MU160              =  94 /* 0x5e */,
	HAL_PHYRX_HE_SIG_A_SU                  =  95 /* 0x5f */,
	HAL_PHYRX_HE_SIG_A_MU_DL               =  96 /* 0x60 */,
	HAL_PHYRX_HE_SIG_A_MU_UL               =  97 /* 0x61 */,
	HAL_PHYRX_HE_SIG_B1_MU                 =  98 /* 0x62 */,
	HAL_PHYRX_HE_SIG_B2_MU                 =  99 /* 0x63 */,
	HAL_PHYRX_HE_SIG_B2_OFDMA              = 100 /* 0x64 */,
	HAL_PHYRX_OTHER_RECEIVE_INFO           = 101 /* 0x65 */,
	HAL_PHYRX_COMMON_USER_INFO             = 102 /* 0x66 */,
	HAL_PHYRX_DATA_DONE                    = 103 /* 0x67 */,
	HAL_RECEIVE_RSSI_INFO                  = 104 /* 0x68 */,
	HAL_RECEIVE_USER_INFO                  = 105 /* 0x69 */,
	HAL_MIMO_CONTROL_INFO                  = 106 /* 0x6a */,
	HAL_RX_LOCATION_INFO                   = 107 /* 0x6b */,
	HAL_COEX_TX_REQ                        = 108 /* 0x6c */,
	HAL_DUMMY                              = 109 /* 0x6d */,
	HAL_RX_TIMING_OFFSET_INFO              = 110 /* 0x6e */,
	HAL_EXAMPLE_TLV_32_NAME                = 111 /* 0x6f */,
	HAL_MPDU_LIMIT                         = 112 /* 0x70 */,
	HAL_NA_LENGTH_END                      = 113 /* 0x71 */,
	HAL_OLE_BUF_STATUS                     = 114 /* 0x72 */,
	HAL_PCU_PPDU_SETUP_DONE                = 115 /* 0x73 */,
	HAL_PCU_PPDU_SETUP_END                 = 116 /* 0x74 */,
	HAL_PCU_PPDU_SETUP_INIT                = 117 /* 0x75 */,
	HAL_PCU_PPDU_SETUP_START               = 118 /* 0x76 */,
	HAL_PDG_FES_SETUP                      = 119 /* 0x77 */,
	HAL_PDG_RESPONSE                       = 120 /* 0x78 */,
	HAL_PDG_TX_REQ                         = 121 /* 0x79 */,
	HAL_SCH_WAIT_INSTR                     = 122 /* 0x7a */,
	HAL_SCHEDULER_TLV                      = 123 /* 0x7b */,
	HAL_TQM_FLOW_EMPTY_STATUS              = 124 /* 0x7c */,
	HAL_TQM_FLOW_NOT_EMPTY_STATUS          = 125 /* 0x7d */,
	HAL_TQM_GEN_MPDU_LENGTH_LIST           = 126 /* 0x7e */,
	HAL_TQM_GEN_MPDU_LENGTH_LIST_STATUS    = 127 /* 0x7f */,
	HAL_TQM_GEN_MPDUS                      = 128 /* 0x80 */,
	HAL_TQM_GEN_MPDUS_STATUS               = 129 /* 0x81 */,
	HAL_TQM_REMOVE_MPDU                    = 130 /* 0x82 */,
	HAL_TQM_REMOVE_MPDU_STATUS             = 131 /* 0x83 */,
	HAL_TQM_REMOVE_MSDU                    = 132 /* 0x84 */,
	HAL_TQM_REMOVE_MSDU_STATUS             = 133 /* 0x85 */,
	HAL_TQM_UPDATE_TX_MPDU_COUNT           = 134 /* 0x86 */,
	HAL_TQM_WRITE_CMD                      = 135 /* 0x87 */,
	HAL_OFDMA_TRIGGER_DETAILS              = 136 /* 0x88 */,
	HAL_TX_DATA                            = 137 /* 0x89 */,
	HAL_TX_FES_SETUP                       = 138 /* 0x8a */,
	HAL_RX_PACKET                          = 139 /* 0x8b */,
	HAL_EXPECTED_RESPONSE                  = 140 /* 0x8c */,
	HAL_TX_MPDU_END                        = 141 /* 0x8d */,
	HAL_TX_MPDU_START                      = 142 /* 0x8e */,
	HAL_TX_MSDU_END                        = 143 /* 0x8f */,
	HAL_TX_MSDU_START                      = 144 /* 0x90 */,
	HAL_TX_SW_MODE_SETUP                   = 145 /* 0x91 */,
	HAL_TXPCU_BUFFER_STATUS                = 146 /* 0x92 */,
	HAL_TXPCU_USER_BUFFER_STATUS           = 147 /* 0x93 */,
	HAL_DATA_TO_TIME_CONFIG                = 148 /* 0x94 */,
	HAL_EXAMPLE_USER_TLV_32                = 149 /* 0x95 */,
	HAL_MPDU_INFO                          = 150 /* 0x96 */,
	HAL_PDG_USER_SETUP                     = 151 /* 0x97 */,
	HAL_TX_11AH_SETUP                      = 152 /* 0x98 */,
	HAL_REO_UPDATE_RX_REO_QUEUE_STATUS     = 153 /* 0x99 */,
	HAL_TX_PEER_ENTRY                      = 154 /* 0x9a */,
	HAL_TX_RAW_OR_NATIVE_FRAME_SETUP       = 155 /* 0x9b */,
	HAL_EXAMPLE_STRUCT_NAME                = 156 /* 0x9c */,
	HAL_PCU_PPDU_SETUP_END_INFO            = 157 /* 0x9d */,
	HAL_PPDU_RATE_SETTING                  = 158 /* 0x9e */,
	HAL_PROT_RATE_SETTING                  = 159 /* 0x9f */,
	HAL_RX_MPDU_DETAILS                    = 160 /* 0xa0 */,
	HAL_EXAMPLE_USER_TLV_42                = 161 /* 0xa1 */,
	HAL_RX_MSDU_LINK                       = 162 /* 0xa2 */,
	HAL_RX_REO_QUEUE                       = 163 /* 0xa3 */,
	HAL_ADDR_SEARCH_ENTRY                  = 164 /* 0xa4 */,
	HAL_SCHEDULER_CMD                      = 165 /* 0xa5 */,
	HAL_TX_FLUSH                           = 166 /* 0xa6 */,
	HAL_TQM_ENTRANCE_RING                  = 167 /* 0xa7 */,
	HAL_TX_DATA_WORD                       = 168 /* 0xa8 */,
	HAL_TX_MPDU_DETAILS                    = 169 /* 0xa9 */,
	HAL_TX_MPDU_LINK                       = 170 /* 0xaa */,
	HAL_TX_MPDU_LINK_PTR                   = 171 /* 0xab */,
	HAL_TX_MPDU_QUEUE_HEAD                 = 172 /* 0xac */,
	HAL_TX_MPDU_QUEUE_EXT                  = 173 /* 0xad */,
	HAL_TX_MPDU_QUEUE_EXT_PTR              = 174 /* 0xae */,
	HAL_TX_MSDU_DETAILS                    = 175 /* 0xaf */,
	HAL_TX_MSDU_EXTENSION                  = 176 /* 0xb0 */,
	HAL_TX_MSDU_FLOW                       = 177 /* 0xb1 */,
	HAL_TX_MSDU_LINK                       = 178 /* 0xb2 */,
	HAL_TX_MSDU_LINK_ENTRY_PTR             = 179 /* 0xb3 */,
	HAL_RESPONSE_RATE_SETTING              = 180 /* 0xb4 */,
	HAL_TXPCU_BUFFER_BASICS                = 181 /* 0xb5 */,
	HAL_UNIFORM_DESCRIPTOR_HEADER          = 182 /* 0xb6 */,
	HAL_UNIFORM_TQM_CMD_HEADER             = 183 /* 0xb7 */,
	HAL_UNIFORM_TQM_STATUS_HEADER          = 184 /* 0xb8 */,
	HAL_USER_RATE_SETTING                  = 185 /* 0xb9 */,
	HAL_WBM_BUFFER_RING                    = 186 /* 0xba */,
	HAL_WBM_LINK_DESCRIPTOR_RING           = 187 /* 0xbb */,
	HAL_WBM_RELEASE_RING                   = 188 /* 0xbc */,
	HAL_TX_FLUSH_REQ                       = 189 /* 0xbd */,
	HAL_RX_MSDU_DETAILS                    = 190 /* 0xbe */,
	HAL_TQM_WRITE_CMD_STATUS               = 191 /* 0xbf */,
	HAL_TQM_GET_MPDU_QUEUE_STATS           = 192 /* 0xc0 */,
	HAL_TQM_GET_MSDU_FLOW_STATS            = 193 /* 0xc1 */,
	HAL_EXAMPLE_USER_CTLV_32               = 194 /* 0xc2 */,
	HAL_TX_FES_STATUS_START                = 195 /* 0xc3 */,
	HAL_TX_FES_STATUS_USER_PPDU            = 196 /* 0xc4 */,
	HAL_TX_FES_STATUS_USER_RESPONSE        = 197 /* 0xc5 */,
	HAL_TX_FES_STATUS_END                  = 198 /* 0xc6 */,
	HAL_RX_TRIG_INFO                       = 199 /* 0xc7 */,
	HAL_RXPCU_TX_SETUP_CLEAR               = 200 /* 0xc8 */,
	HAL_RX_FRAME_BITMAP_REQ                = 201 /* 0xc9 */,
	HAL_RX_FRAME_BITMAP_ACK                = 202 /* 0xca */,
	HAL_COEX_RX_STATUS                     = 203 /* 0xcb */,
	HAL_RX_START_PARAM                     = 204 /* 0xcc */,
	HAL_RX_PPDU_START                      = 205 /* 0xcd */,
	HAL_RX_PPDU_END                        = 206 /* 0xce */,
	HAL_RX_MPDU_START                      = 207 /* 0xcf */,
	HAL_RX_MPDU_END                        = 208 /* 0xd0 */,
	HAL_RX_MSDU_START                      = 209 /* 0xd1 */,
	HAL_RX_MSDU_END                        = 210 /* 0xd2 */,
	HAL_RX_ATTENTION                       = 211 /* 0xd3 */,
	HAL_RECEIVED_RESPONSE_INFO             = 212 /* 0xd4 */,
	HAL_RX_PHY_SLEEP                       = 213 /* 0xd5 */,
	HAL_RX_HEADER                          = 214 /* 0xd6 */,
	HAL_RX_PEER_ENTRY                      = 215 /* 0xd7 */,
	HAL_RX_FLUSH                           = 216 /* 0xd8 */,
	HAL_RX_RESPONSE_REQUIRED_INFO          = 217 /* 0xd9 */,
	HAL_RX_FRAMELESS_BAR_DETAILS           = 218 /* 0xda */,
	HAL_TQM_GET_MPDU_QUEUE_STATS_STATUS    = 219 /* 0xdb */,
	HAL_TQM_GET_MSDU_FLOW_STATS_STATUS     = 220 /* 0xdc */,
	HAL_TX_CBF_INFO                        = 221 /* 0xdd */,
	HAL_PCU_PPDU_SETUP_USER                = 222 /* 0xde */,
	HAL_RX_MPDU_PCU_START                  = 223 /* 0xdf */,
	HAL_RX_PM_INFO                         = 224 /* 0xe0 */,
	HAL_RX_USER_PPDU_END                   = 225 /* 0xe1 */,
	HAL_RX_PRE_PPDU_START                  = 226 /* 0xe2 */,
	HAL_RX_PREAMBLE                        = 227 /* 0xe3 */,
	HAL_TX_FES_SETUP_COMPLETE              = 228 /* 0xe4 */,
	HAL_TX_LAST_MPDU_FETCHED               = 229 /* 0xe5 */,
	HAL_TXDMA_STOP_REQUEST                 = 230 /* 0xe6 */,
	HAL_RXPCU_SETUP                        = 231 /* 0xe7 */,
	HAL_RXPCU_USER_SETUP                   = 232 /* 0xe8 */,
	HAL_TX_FES_STATUS_ACK_OR_BA            = 233 /* 0xe9 */,
	HAL_TQM_ACKED_MPDU                     = 234 /* 0xea */,
	HAL_COEX_TX_RESP                       = 235 /* 0xeb */,
	HAL_COEX_TX_STATUS                     = 236 /* 0xec */,
	HAL_MACTX_COEX_PHY_CTRL                = 237 /* 0xed */,
	HAL_COEX_STATUS_BROADCAST              = 238 /* 0xee */,
	HAL_RESPONSE_START_STATUS              = 239 /* 0xef */,
	HAL_RESPONSE_END_STATUS                = 240 /* 0xf0 */,
	HAL_CRYPTO_STATUS                      = 241 /* 0xf1 */,
	HAL_RECEIVED_TRIGGER_INFO              = 242 /* 0xf2 */,
	HAL_REO_ENTRANCE_RING                  = 243 /* 0xf3 */,
	HAL_RX_MPDU_LINK                       = 244 /* 0xf4 */,
	HAL_COEX_TX_STOP_CTRL                  = 245 /* 0xf5 */,
	HAL_RX_PPDU_ACK_REPORT                 = 246 /* 0xf6 */,
	HAL_RX_PPDU_NO_ACK_REPORT              = 247 /* 0xf7 */,
	HAL_SCH_COEX_STATUS                    = 248 /* 0xf8 */,
	HAL_SCHEDULER_COMMAND_STATUS           = 249 /* 0xf9 */,
	HAL_SCHEDULER_RX_PPDU_NO_RESPONSE_STATUS = 250 /* 0xfa */,
	HAL_TX_FES_STATUS_PROT                 = 251 /* 0xfb */,
	HAL_TX_FES_STATUS_START_PPDU           = 252 /* 0xfc */,
	HAL_TX_FES_STATUS_START_PROT           = 253 /* 0xfd */,
	HAL_TXPCU_PHYTX_DEBUG32                = 254 /* 0xfe */,
	HAL_TXPCU_PHYTX_OTHER_TRANSMIT_INFO32  = 255 /* 0xff */,
	HAL_TX_MPDU_COUNT_TRANSFER_END         = 256 /* 0x100 */,
	HAL_WHO_ANCHOR_OFFSET                  = 257 /* 0x101 */,
	HAL_WHO_ANCHOR_VALUE                   = 258 /* 0x102 */,
	HAL_WHO_CCE_INFO                       = 259 /* 0x103 */,
	HAL_WHO_COMMIT                         = 260 /* 0x104 */,
	HAL_WHO_COMMIT_DONE                    = 261 /* 0x105 */,
	HAL_WHO_FLUSH                          = 262 /* 0x106 */,
	HAL_WHO_L2_LLC                         = 263 /* 0x107 */,
	HAL_WHO_L2_PAYLOAD                     = 264 /* 0x108 */,
	HAL_WHO_L3_CHECKSUM                    = 265 /* 0x109 */,
	HAL_WHO_L3_INFO                        = 266 /* 0x10a */,
	HAL_WHO_L4_CHECKSUM                    = 267 /* 0x10b */,
	HAL_WHO_L4_INFO                        = 268 /* 0x10c */,
	HAL_WHO_MSDU                           = 269 /* 0x10d */,
	HAL_WHO_MSDU_MISC                      = 270 /* 0x10e */,
	HAL_WHO_PACKET_DATA                    = 271 /* 0x10f */,
	HAL_WHO_PACKET_HDR                     = 272 /* 0x110 */,
	HAL_WHO_PPDU_END                       = 273 /* 0x111 */,
	HAL_WHO_PPDU_START                     = 274 /* 0x112 */,
	HAL_WHO_TSO                            = 275 /* 0x113 */,
	HAL_WHO_WMAC_HEADER_PV0                = 276 /* 0x114 */,
	HAL_WHO_WMAC_HEADER_PV1                = 277 /* 0x115 */,
	HAL_WHO_WMAC_IV                        = 278 /* 0x116 */,
	HAL_MPDU_INFO_END                      = 279 /* 0x117 */,
	HAL_MPDU_INFO_BITMAP                   = 280 /* 0x118 */,
	HAL_TX_QUEUE_EXTENSION                 = 281 /* 0x119 */,
	HAL_RX_PEER_ENTRY_DETAILS              = 282 /* 0x11a */,
	HAL_RX_REO_QUEUE_REFERENCE             = 283 /* 0x11b */,
	HAL_RX_REO_QUEUE_EXT                   = 284 /* 0x11c */,
	HAL_SCHEDULER_SELFGEN_RESPONSE_STATUS  = 285 /* 0x11d */,
	HAL_TQM_UPDATE_TX_MPDU_COUNT_STATUS    = 286 /* 0x11e */,
	HAL_TQM_ACKED_MPDU_STATUS              = 287 /* 0x11f */,
	HAL_TQM_ADD_MSDU_STATUS                = 288 /* 0x120 */,
	HAL_RX_MPDU_LINK_PTR                   = 289 /* 0x121 */,
	HAL_REO_DESTINATION_RING               = 290 /* 0x122 */,
	HAL_TQM_LIST_GEN_DONE                  = 291 /* 0x123 */,
	HAL_WHO_TERMINATE                      = 292 /* 0x124 */,
	HAL_TX_LAST_MPDU_END                   = 293 /* 0x125 */,
	HAL_TX_CV_DATA                         = 294 /* 0x126 */,
	HAL_TCL_ENTRANCE_FROM_PPE_RING         = 295 /* 0x127 */,
	HAL_PPDU_TX_END                        = 296 /* 0x128 */,
	HAL_PROT_TX_END                        = 297 /* 0x129 */,
	HAL_PDG_RESPONSE_RATE_SETTING          = 298 /* 0x12a */,
	HAL_MPDU_INFO_GLOBAL_END               = 299 /* 0x12b */,
	HAL_TQM_SCH_INSTR_GLOBAL_END           = 300 /* 0x12c */,
	HAL_RX_PPDU_END_USER_STATS             = 301 /* 0x12d */,
	HAL_RX_PPDU_END_USER_STATS_EXT         = 302 /* 0x12e */,
	HAL_NO_ACK_REPORT                      = 303 /* 0x12f */,
	HAL_ACK_REPORT                         = 304 /* 0x130 */,
	HAL_UNIFORM_REO_CMD_HEADER             = 305 /* 0x131 */,
	HAL_REO_GET_QUEUE_STATS                = 306 /* 0x132 */,
	HAL_REO_FLUSH_QUEUE                    = 307 /* 0x133 */,
	HAL_REO_FLUSH_CACHE                    = 308 /* 0x134 */,
	HAL_REO_UNBLOCK_CACHE                  = 309 /* 0x135 */,
	HAL_UNIFORM_REO_STATUS_HEADER          = 310 /* 0x136 */,
	HAL_REO_GET_QUEUE_STATS_STATUS         = 311 /* 0x137 */,
	HAL_REO_FLUSH_QUEUE_STATUS             = 312 /* 0x138 */,
	HAL_REO_FLUSH_CACHE_STATUS             = 313 /* 0x139 */,
	HAL_REO_UNBLOCK_CACHE_STATUS           = 314 /* 0x13a */,
	HAL_TQM_FLUSH_CACHE                    = 315 /* 0x13b */,
	HAL_TQM_UNBLOCK_CACHE                  = 316 /* 0x13c */,
	HAL_TQM_FLUSH_CACHE_STATUS             = 317 /* 0x13d */,
	HAL_TQM_UNBLOCK_CACHE_STATUS           = 318 /* 0x13e */,
	HAL_RX_PPDU_END_STATUS_DONE            = 319 /* 0x13f */,
	HAL_RX_STATUS_BUFFER_DONE              = 320 /* 0x140 */,
	HAL_BUFFER_ADDR_INFO                   = 321 /* 0x141 */,
	HAL_RX_MSDU_DESC_INFO                  = 322 /* 0x142 */,
	HAL_RX_MPDU_DESC_INFO                  = 323 /* 0x143 */,
	HAL_TCL_DATA_CMD                       = 324 /* 0x144 */,
	HAL_TCL_GSE_CMD                        = 325 /* 0x145 */,
	HAL_TCL_EXIT_BASE                      = 326 /* 0x146 */,
	HAL_TCL_COMPACT_EXIT_RING              = 327 /* 0x147 */,
	HAL_TCL_REGULAR_EXIT_RING              = 328 /* 0x148 */,
	HAL_TCL_EXTENDED_EXIT_RING             = 329 /* 0x149 */,
	HAL_UPLINK_COMMON_INFO                 = 330 /* 0x14a */,
	HAL_UPLINK_USER_SETUP_INFO             = 331 /* 0x14b */,
	HAL_TX_DATA_SYNC                       = 332 /* 0x14c */,
	HAL_PHYRX_CBF_READ_REQUEST_ACK         = 333 /* 0x14d */,
	HAL_TCL_STATUS_RING                    = 334 /* 0x14e */,
	HAL_TQM_GET_MPDU_HEAD_INFO             = 335 /* 0x14f */,
	HAL_TQM_SYNC_CMD                       = 336 /* 0x150 */,
	HAL_TQM_GET_MPDU_HEAD_INFO_STATUS      = 337 /* 0x151 */,
	HAL_TQM_SYNC_CMD_STATUS                = 338 /* 0x152 */,
	HAL_TQM_THRESHOLD_DROP_NOTIFICATION_STATUS = 339 /* 0x153 */,
	HAL_TQM_DESCRIPTOR_THRESHOLD_REACHED_STATUS = 340 /* 0x154 */,
	HAL_REO_FLUSH_TIMEOUT_LIST             = 341 /* 0x155 */,
	HAL_REO_FLUSH_TIMEOUT_LIST_STATUS      = 342 /* 0x156 */,
	HAL_REO_TO_PPE_RING                    = 343 /* 0x157 */,
	HAL_RX_MPDU_INFO                       = 344 /* 0x158 */,
	HAL_REO_DESCRIPTOR_THRESHOLD_REACHED_STATUS = 345 /* 0x159 */,
	HAL_SCHEDULER_RX_SIFS_RESPONSE_TRIGGER_STATUS = 346 /* 0x15a */,
	HAL_EXAMPLE_USER_TLV_32_NAME           = 347 /* 0x15b */,
	HAL_RX_PPDU_START_USER_INFO            = 348 /* 0x15c */,
	HAL_RX_RXPCU_CLASSIFICATION_OVERVIEW   = 349 /* 0x15d */,
	HAL_RX_RING_MASK                       = 350 /* 0x15e */,
	HAL_WHO_CLASSIFY_INFO                  = 351 /* 0x15f */,
	HAL_TXPT_CLASSIFY_INFO                 = 352 /* 0x160 */,
	HAL_RXPT_CLASSIFY_INFO                 = 353 /* 0x161 */,
	HAL_TX_FLOW_SEARCH_ENTRY               = 354 /* 0x162 */,
	HAL_RX_FLOW_SEARCH_ENTRY               = 355 /* 0x163 */,
	HAL_RECEIVED_TRIGGER_INFO_DETAILS      = 356 /* 0x164 */,
	HAL_COEX_MAC_NAP                       = 357 /* 0x165 */,
	HAL_MACRX_ABORT_REQUEST_INFO           = 358 /* 0x166 */,
	HAL_MACTX_ABORT_REQUEST_INFO           = 359 /* 0x167 */,
	HAL_PHYRX_ABORT_REQUEST_INFO           = 360 /* 0x168 */,
	HAL_PHYTX_ABORT_REQUEST_INFO           = 361 /* 0x169 */,
	HAL_RXPCU_PPDU_END_INFO                = 362 /* 0x16a */,
	HAL_WHO_MESH_CONTROL                   = 363 /* 0x16b */,
	HAL_L_SIG_A_INFO                       = 364 /* 0x16c */,
	HAL_L_SIG_B_INFO                       = 365 /* 0x16d */,
	HAL_HT_SIG_INFO                        = 366 /* 0x16e */,
	HAL_VHT_SIG_A_INFO                     = 367 /* 0x16f */,
	HAL_VHT_SIG_B_SU20_INFO                = 368 /* 0x170 */,
	HAL_VHT_SIG_B_SU40_INFO                = 369 /* 0x171 */,
	HAL_VHT_SIG_B_SU80_INFO                = 370 /* 0x172 */,
	HAL_VHT_SIG_B_SU160_INFO               = 371 /* 0x173 */,
	HAL_VHT_SIG_B_MU20_INFO                = 372 /* 0x174 */,
	HAL_VHT_SIG_B_MU40_INFO                = 373 /* 0x175 */,
	HAL_VHT_SIG_B_MU80_INFO                = 374 /* 0x176 */,
	HAL_VHT_SIG_B_MU160_INFO               = 375 /* 0x177 */,
	HAL_SERVICE_INFO                       = 376 /* 0x178 */,
	HAL_HE_SIG_A_SU_INFO                   = 377 /* 0x179 */,
	HAL_HE_SIG_A_MU_DL_INFO                = 378 /* 0x17a */,
	HAL_HE_SIG_A_MU_UL_INFO                = 379 /* 0x17b */,
	HAL_HE_SIG_B1_MU_INFO                  = 380 /* 0x17c */,
	HAL_HE_SIG_B2_MU_INFO                  = 381 /* 0x17d */,
	HAL_HE_SIG_B2_OFDMA_INFO               = 382 /* 0x17e */,
	HAL_PDG_SW_MODE_BW_START               = 383 /* 0x17f */,
	HAL_PDG_SW_MODE_BW_END                 = 384 /* 0x180 */,
	HAL_PDG_WAIT_FOR_MAC_REQUEST           = 385 /* 0x181 */,
	HAL_PDG_WAIT_FOR_PHY_REQUEST           = 386 /* 0x182 */,
	HAL_SCHEDULER_END                      = 387 /* 0x183 */,
	HAL_PEER_TABLE_ENTRY                   = 388 /* 0x184 */,
	HAL_SW_PEER_INFO                       = 389 /* 0x185 */,
	HAL_RXOLE_CCE_CLASSIFY_INFO            = 390 /* 0x186 */,
	HAL_TCL_CCE_CLASSIFY_INFO              = 391 /* 0x187 */,
	HAL_RXOLE_CCE_INFO                     = 392 /* 0x188 */,
	HAL_TCL_CCE_INFO                       = 393 /* 0x189 */,
	HAL_TCL_CCE_SUPERRULE                  = 394 /* 0x18a */,
	HAL_CCE_RULE                           = 395 /* 0x18b */,
	HAL_RX_PPDU_START_DROPPED              = 396 /* 0x18c */,
	HAL_RX_PPDU_END_DROPPED                = 397 /* 0x18d */,
	HAL_RX_PPDU_END_STATUS_DONE_DROPPED    = 398 /* 0x18e */,
	HAL_RX_MPDU_START_DROPPED              = 399 /* 0x18f */,
	HAL_RX_MSDU_START_DROPPED              = 400 /* 0x190 */,
	HAL_RX_MSDU_END_DROPPED                = 401 /* 0x191 */,
	HAL_RX_MPDU_END_DROPPED                = 402 /* 0x192 */,
	HAL_RX_ATTENTION_DROPPED               = 403 /* 0x193 */,
	HAL_TXPCU_USER_SETUP                   = 404 /* 0x194 */,
	HAL_RXPCU_USER_SETUP_EXT               = 405 /* 0x195 */,
	HAL_CE_SRC_DESC                        = 406 /* 0x196 */,
	HAL_CE_STAT_DESC                       = 407 /* 0x197 */,
	HAL_RXOLE_CCE_SUPERRULE                = 408 /* 0x198 */,
	HAL_TX_RATE_STATS_INFO                 = 409 /* 0x199 */,
	HAL_CMD_PART_0_END                     = 410 /* 0x19a */,
	HAL_MACTX_SYNTH_ON                     = 411 /* 0x19b */,
	HAL_SCH_CRITICAL_TLV_REFERENCE         = 412 /* 0x19c */,
	HAL_TQM_MPDU_GLOBAL_START              = 413 /* 0x19d */,
	HAL_EXAMPLE_TLV_32                     = 414 /* 0x19e */,
	HAL_TQM_UPDATE_TX_MSDU_FLOW            = 415 /* 0x19f */,
	HAL_TQM_UPDATE_TX_MPDU_QUEUE_HEAD      = 416 /* 0x1a0 */,
	HAL_TQM_UPDATE_TX_MSDU_FLOW_STATUS     = 417 /* 0x1a1 */,
	HAL_TQM_UPDATE_TX_MPDU_QUEUE_HEAD_STATUS = 418 /* 0x1a2 */,
	HAL_REO_UPDATE_RX_REO_QUEUE            = 419 /* 0x1a3 */,
	HAL_CE_DST_DESC			       = 420 /* 0x1a4 */,
	HAL_TLV_BASE                           = 511 /* 0x1ff */,
};

#define HAL_TLV_HDR_TAG		GENMASK(9, 1)
#define HAL_TLV_HDR_LEN		GENMASK(25, 10)

#define HAL_TLV_ALIGN	4

struct hal_tlv_hdr {
	u32 tl;
	u8 value[];
} __packed;

#define RX_MPDU_DESC_INFO0_MSDU_COUNT		GENMASK(7, 0)
#define RX_MPDU_DESC_INFO0_SEQ_NUM		GENMASK(19, 8)
#define RX_MPDU_DESC_INFO0_FRAG_FLAG		BIT(20)
#define RX_MPDU_DESC_INFO0_MPDU_RETRY		BIT(21)
#define RX_MPDU_DESC_INFO0_AMPDU_FLAG		BIT(22)
#define RX_MPDU_DESC_INFO0_BAR_FRAME		BIT(23)
#define RX_MPDU_DESC_INFO0_VALID_PN		BIT(24)
#define RX_MPDU_DESC_INFO0_VALID_SA		BIT(25)
#define RX_MPDU_DESC_INFO0_SA_IDX_TIMEOUT	BIT(26)
#define RX_MPDU_DESC_INFO0_VALID_DA		BIT(27)
#define RX_MPDU_DESC_INFO0_DA_MCBC		BIT(28)
#define RX_MPDU_DESC_INFO0_DA_IDX_TIMEOUT	BIT(29)
#define RX_MPDU_DESC_INFO0_RAW_MPDU		BIT(30)

struct rx_mpdu_desc {
	u32 info0; /* %RX_MPDU_DESC_INFO */
	u32 meta_data;
} __packed;

/* rx_mpdu_desc
 *		Producer: RXDMA
 *		Consumer: REO/SW/FW
 *
 * msdu_count
 *		The number of MSDUs within the MPDU
 *
 * mpdu_sequence_number
 *		The field can have two different meanings based on the setting
 *		of field 'bar_frame'. If 'bar_frame' is set, it means the MPDU
 *		start sequence number from the BAR frame otherwise it means
 *		the MPDU sequence number of the received frame.
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
 * valid_sa
 *		Indicates OLE found a valid SA entry for all MSDUs in this MPDU.
 *
 * sa_idx_timeout
 *		Indicates, at least 1 MSDU within the MPDU has an unsuccessful
 *		MAC source address search due to the expiration of search timer.
 *
 * valid_da
 *		When set, OLE found a valid DA entry for all MSDUs in this MPDU.
 *
 * da_mcbc
 *		Field Only valid if valid_da is set. Indicates at least one of
 *		the DA addresses is a Multicast or Broadcast address.
 *
 * da_idx_timeout
 *		Indicates, at least 1 MSDU within the MPDU has an unsuccessful
 *		MAC destination address search due to the expiration of search
 *		timer.
 *
 * raw_mpdu
 *		Field only valid when first_msdu_in_mpdu_flag is set. Indicates
 *		the contents in the MSDU buffer contains a 'RAW' MPDU.
 */

enum hal_rx_msdu_desc_reo_dest_ind {
	HAL_RX_MSDU_DESC_REO_DEST_IND_TCL,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW1,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW2,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW3,
	HAL_RX_MSDU_DESC_REO_DEST_IND_SW4,
	HAL_RX_MSDU_DESC_REO_DEST_IND_RELEASE,
	HAL_RX_MSDU_DESC_REO_DEST_IND_FW,
};

#define RX_MSDU_DESC_INFO0_FIRST_MSDU_IN_MPDU	BIT(0)
#define RX_MSDU_DESC_INFO0_LAST_MSDU_IN_MPDU	BIT(1)
#define RX_MSDU_DESC_INFO0_MSDU_CONTINUATION	BIT(2)
#define RX_MSDU_DESC_INFO0_MSDU_LENGTH		GENMASK(16, 3)
#define RX_MSDU_DESC_INFO0_REO_DEST_IND		GENMASK(21, 17)
#define RX_MSDU_DESC_INFO0_MSDU_DROP		BIT(22)
#define RX_MSDU_DESC_INFO0_VALID_SA		BIT(23)
#define RX_MSDU_DESC_INFO0_SA_IDX_TIMEOUT	BIT(24)
#define RX_MSDU_DESC_INFO0_VALID_DA		BIT(25)
#define RX_MSDU_DESC_INFO0_DA_MCBC		BIT(26)
#define RX_MSDU_DESC_INFO0_DA_IDX_TIMEOUT	BIT(27)

#define HAL_RX_MSDU_PKT_LENGTH_GET(val)		\
	(FIELD_GET(RX_MSDU_DESC_INFO0_MSDU_LENGTH, (val)))

struct rx_msdu_desc {
	u32 info0;
	u32 rsvd0;
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
 *		The next buffer will therefor contain additional information
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
 * reo_destination_indication
 *		The id of the reo exit ring where the msdu frame shall push
 *		after (MPDU level) reordering has finished. Values are defined
 *		in enum %HAL_RX_MSDU_DESC_REO_DEST_IND_.
 *
 * msdu_drop
 *		Indicates that REO shall drop this MSDU and not forward it to
 *		any other ring.
 *
 * valid_sa
 *		Indicates OLE found a valid SA entry for this MSDU.
 *
 * sa_idx_timeout
 *		Indicates, an unsuccessful MAC source address search due to
 *		the expiration of search timer for this MSDU.
 *
 * valid_da
 *		When set, OLE found a valid DA entry for this MSDU.
 *
 * da_mcbc
 *		Field Only valid if valid_da is set. Indicates the DA address
 *		is a Multicast or Broadcast address for this MSDU.
 *
 * da_idx_timeout
 *		Indicates, an unsuccessful MAC destination address search due
 *		to the expiration of search timer fot this MSDU.
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

#define HAL_REO_DEST_RING_INFO0_QUEUE_ADDR_HI		GENMASK(7, 0)
#define HAL_REO_DEST_RING_INFO0_BUFFER_TYPE		BIT(8)
#define HAL_REO_DEST_RING_INFO0_PUSH_REASON		GENMASK(10, 9)
#define HAL_REO_DEST_RING_INFO0_ERROR_CODE		GENMASK(15, 11)
#define HAL_REO_DEST_RING_INFO0_RX_QUEUE_NUM		GENMASK(31, 16)

#define HAL_REO_DEST_RING_INFO1_REORDER_INFO_VALID	BIT(0)
#define HAL_REO_DEST_RING_INFO1_REORDER_OPCODE		GENMASK(4, 1)
#define HAL_REO_DEST_RING_INFO1_REORDER_SLOT_IDX	GENMASK(12, 5)

#define HAL_REO_DEST_RING_INFO2_RING_ID			GENMASK(27, 20)
#define HAL_REO_DEST_RING_INFO2_LOOPING_COUNT		GENMASK(31, 28)

struct hal_reo_dest_ring {
	struct ath11k_buffer_addr buf_addr_info;
	struct rx_mpdu_desc rx_mpdu_info;
	struct rx_msdu_desc rx_msdu_info;
	u32 queue_addr_lo;
	u32 info0; /* %HAL_REO_DEST_RING_INFO0_ */
	u32 info1; /* %HAL_REO_DEST_RING_INFO1_ */
	u32 rsvd0;
	u32 rsvd1;
	u32 rsvd2;
	u32 rsvd3;
	u32 rsvd4;
	u32 rsvd5;
	u32 info2; /* %HAL_REO_DEST_RING_INFO2_ */
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
 * queue_addr_lo
 *		Address (lower 32 bits) of the REO queue descriptor.
 *
 * queue_addr_hi
 *		Address (upper 8 bits) of the REO queue descriptor.
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
 * rx_queue_num
 *		Indicates the REO MPDU reorder queue id from which this frame
 *		originated.
 *
 * reorder_info_valid
 *		When set, REO has been instructed to not perform the actual
 *		re-ordering of frames for this queue, but just to insert
 *		the reorder opcodes.
 *
 * reorder_opcode
 *		Field is valid when 'reorder_info_valid' is set. This field is
 *		always valid for debug purpose as well.
 *
 * reorder_slot_idx
 *		Valid only when 'reorder_info_valid' is set.
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
	HAL_REO_ENTR_RING_RXDMA_ECODE_MAX,
};

#define HAL_REO_ENTR_RING_INFO0_QUEUE_ADDR_HI		GENMASK(7, 0)
#define HAL_REO_ENTR_RING_INFO0_MPDU_BYTE_COUNT		GENMASK(21, 8)
#define HAL_REO_ENTR_RING_INFO0_DEST_IND		GENMASK(26, 22)
#define HAL_REO_ENTR_RING_INFO0_FRAMELESS_BAR		BIT(27)

#define HAL_REO_ENTR_RING_INFO1_RXDMA_PUSH_REASON	GENMASK(1, 0)
#define HAL_REO_ENTR_RING_INFO1_RXDMA_ERROR_CODE	GENMASK(6, 2)

struct hal_reo_entrance_ring {
	struct ath11k_buffer_addr buf_addr_info;
	struct rx_mpdu_desc rx_mpdu_info;
	u32 queue_addr_lo;
	u32 info0; /* %HAL_REO_ENTR_RING_INFO0_ */
	u32 info1; /* %HAL_REO_ENTR_RING_INFO1_ */
	u32 info2; /* %HAL_REO_DEST_RING_INFO2_ */

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
 *		defined in enum %HAL_REO_DEST_RING_PUSH_REASON_.
 *
 * rxdma_error_code
 *		Valid only when 'push_reason' is set. All error codes are
 *		defined in enum %HAL_REO_ENTR_RING_RXDMA_ECODE_.
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
	u32 info0;
} __packed;

#define HAL_REO_GET_QUEUE_STATS_INFO0_QUEUE_ADDR_HI	GENMASK(7, 0)
#define HAL_REO_GET_QUEUE_STATS_INFO0_CLEAR_STATS	BIT(8)

struct hal_reo_get_queue_stats {
	struct hal_reo_cmd_hdr cmd;
	u32 queue_addr_lo;
	u32 info0;
	u32 rsvd0[6];
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
	u32 desc_addr_lo;
	u32 info0;
	u32 rsvd0[6];
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
	u32 cache_addr_lo;
	u32 info0;
	u32 rsvd0[6];
} __packed;

#define HAL_TCL_DATA_CMD_INFO0_DESC_TYPE	BIT(0)
#define HAL_TCL_DATA_CMD_INFO0_EPD		BIT(1)
#define HAL_TCL_DATA_CMD_INFO0_ENCAP_TYPE	GENMASK(3, 2)
#define HAL_TCL_DATA_CMD_INFO0_ENCRYPT_TYPE	GENMASK(7, 4)
#define HAL_TCL_DATA_CMD_INFO0_SRC_BUF_SWAP	BIT(8)
#define HAL_TCL_DATA_CMD_INFO0_LNK_META_SWAP	BIT(9)
#define HAL_TCL_DATA_CMD_INFO0_SEARCH_TYPE	GENMASK(13, 12)
#define HAL_TCL_DATA_CMD_INFO0_ADDR_EN		GENMASK(15, 14)
#define HAL_TCL_DATA_CMD_INFO0_CMD_NUM		GENMASK(31, 16)

#define HAL_TCL_DATA_CMD_INFO1_DATA_LEN		GENMASK(15, 0)
#define HAL_TCL_DATA_CMD_INFO1_IP4_CKSUM_EN	BIT(16)
#define HAL_TCL_DATA_CMD_INFO1_UDP4_CKSUM_EN	BIT(17)
#define HAL_TCL_DATA_CMD_INFO1_UDP6_CKSUM_EN	BIT(18)
#define HAL_TCL_DATA_CMD_INFO1_TCP4_CKSUM_EN	BIT(19)
#define HAL_TCL_DATA_CMD_INFO1_TCP6_CKSUM_EN	BIT(20)
#define HAL_TCL_DATA_CMD_INFO1_TO_FW		BIT(21)
#define HAL_TCL_DATA_CMD_INFO1_PKT_OFFSET	GENMASK(31, 23)

#define HAL_TCL_DATA_CMD_INFO2_BUF_TIMESTAMP		GENMASK(18, 0)
#define HAL_TCL_DATA_CMD_INFO2_BUF_T_VALID		BIT(19)
#define HAL_IPQ8074_TCL_DATA_CMD_INFO2_MESH_ENABLE	BIT(20)
#define HAL_TCL_DATA_CMD_INFO2_TID_OVERWRITE		BIT(21)
#define HAL_TCL_DATA_CMD_INFO2_TID			GENMASK(25, 22)
#define HAL_TCL_DATA_CMD_INFO2_LMAC_ID			GENMASK(27, 26)

#define HAL_TCL_DATA_CMD_INFO3_DSCP_TID_TABLE_IDX	GENMASK(5, 0)
#define HAL_TCL_DATA_CMD_INFO3_SEARCH_INDEX		GENMASK(25, 6)
#define HAL_TCL_DATA_CMD_INFO3_CACHE_SET_NUM		GENMASK(29, 26)
#define HAL_QCN9074_TCL_DATA_CMD_INFO3_MESH_ENABLE	GENMASK(31, 30)

#define HAL_TCL_DATA_CMD_INFO4_RING_ID			GENMASK(27, 20)
#define HAL_TCL_DATA_CMD_INFO4_LOOPING_COUNT		GENMASK(31, 28)

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
};

enum hal_tcl_desc_type {
	HAL_TCL_DESC_TYPE_BUFFER,
	HAL_TCL_DESC_TYPE_EXT_DESC,
};

enum hal_wbm_htt_tx_comp_status {
	HAL_WBM_REL_HTT_TX_COMP_STATUS_OK,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_DROP,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_TTL,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_REINJ,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_INSPECT,
	HAL_WBM_REL_HTT_TX_COMP_STATUS_MEC_NOTIFY,
};

struct hal_tcl_data_cmd {
	struct ath11k_buffer_addr buf_addr_info;
	u32 info0;
	u32 info1;
	u32 info2;
	u32 info3;
	u32 info4;
} __packed;

/* hal_tcl_data_cmd
 *
 * buf_addr_info
 *		Details of the physical address of a buffer or MSDU
 *		link descriptor.
 *
 * desc_type
 *		Indicates the type of address provided in the buf_addr_info.
 *		Values are defined in enum %HAL_REO_DEST_RING_BUFFER_TYPE_.
 *
 * epd
 *		When this bit is set then input packet is an EPD type.
 *
 * encap_type
 *		Indicates the encapsulation that HW will perform. Values are
 *		defined in enum %HAL_TCL_ENCAP_TYPE_.
 *
 * encrypt_type
 *		Field only valid for encap_type: RAW
 *		Values are defined in enum %HAL_ENCRYPT_TYPE_.
 *
 * src_buffer_swap
 *		Treats source memory (packet buffer) organization as big-endian.
 *		1'b0: Source memory is little endian
 *		1'b1: Source memory is big endian
 *
 * link_meta_swap
 *		Treats link descriptor and Metadata as big-endian.
 *		1'b0: memory is little endian
 *		1'b1: memory is big endian
 *
 * search_type
 *		Search type select
 *		0 - Normal search, 1 - Index based address search,
 *		2 - Index based flow search
 *
 * addrx_en
 * addry_en
 *		Address X/Y search enable in ASE correspondingly.
 *		1'b0: Search disable
 *		1'b1: Search Enable
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
 * buffer_timestamp
 * buffer_timestamp_valid
 *		Frame system entrance timestamp. It shall be filled by first
 *		module (SW, TCL or TQM) that sees the frames first.
 *
 * mesh_enable
 *		For raw WiFi frames, this indicates transmission to a mesh STA,
 *		enabling the interpretation of the 'Mesh Control Present' bit
 *		(bit 8) of QoS Control.
 *		For native WiFi frames, this indicates that a 'Mesh Control'
 *		field is present between the header and the LLC.
 *
 * hlos_tid_overwrite
 *
 *		When set, TCL shall ignore the IP DSCP and VLAN PCP
 *		fields and use HLOS_TID as the final TID. Otherwise TCL
 *		shall consider the DSCP and PCP fields as well as HLOS_TID
 *		and choose a final TID based on the configured priority
 *
 * hlos_tid
 *		HLOS MSDU priority
 *		Field is used when HLOS_TID_overwrite is set.
 *
 * lmac_id
 *		TCL uses this LMAC_ID in address search, i.e, while
 *		finding matching entry for the packet in AST corresponding
 *		to given LMAC_ID
 *
 *		If LMAC ID is all 1s (=> value 3), it indicates wildcard
 *		match for any MAC
 *
 * dscp_tid_table_num
 *		DSCP to TID mapping table number that need to be used
 *		for the MSDU.
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

enum hal_tcl_gse_ctrl {
	HAL_TCL_GSE_CTRL_RD_STAT,
	HAL_TCL_GSE_CTRL_SRCH_DIS,
	HAL_TCL_GSE_CTRL_WR_BK_SINGLE,
	HAL_TCL_GSE_CTRL_WR_BK_ALL,
	HAL_TCL_GSE_CTRL_INVAL_SINGLE,
	HAL_TCL_GSE_CTRL_INVAL_ALL,
	HAL_TCL_GSE_CTRL_WR_BK_INVAL_SINGLE,
	HAL_TCL_GSE_CTRL_WR_BK_INVAL_ALL,
	HAL_TCL_GSE_CTRL_CLR_STAT_SINGLE,
};

/* hal_tcl_gse_ctrl
 *
 * rd_stat
 *		Report or Read statistics
 * srch_dis
 *		Search disable. Report only Hash.
 * wr_bk_single
 *		Write Back single entry
 * wr_bk_all
 *		Write Back entire cache entry
 * inval_single
 *		Invalidate single cache entry
 * inval_all
 *		Invalidate entire cache
 * wr_bk_inval_single
 *		Write back and invalidate single entry in cache
 * wr_bk_inval_all
 *		Write back and invalidate entire cache
 * clr_stat_single
 *		Clear statistics for single entry
 */

#define HAL_TCL_GSE_CMD_INFO0_CTRL_BUF_ADDR_HI		GENMASK(7, 0)
#define HAL_TCL_GSE_CMD_INFO0_GSE_CTRL			GENMASK(11, 8)
#define HAL_TCL_GSE_CMD_INFO0_GSE_SEL			BIT(12)
#define HAL_TCL_GSE_CMD_INFO0_STATUS_DEST_RING_ID	BIT(13)
#define HAL_TCL_GSE_CMD_INFO0_SWAP			BIT(14)

#define HAL_TCL_GSE_CMD_INFO1_RING_ID			GENMASK(27, 20)
#define HAL_TCL_GSE_CMD_INFO1_LOOPING_COUNT		GENMASK(31, 28)

struct hal_tcl_gse_cmd {
	u32 ctrl_buf_addr_lo;
	u32 info0;
	u32 meta_data[2];
	u32 rsvd0[2];
	u32 info1;
} __packed;

/* hal_tcl_gse_cmd
 *
 * ctrl_buf_addr_lo, ctrl_buf_addr_hi
 *		Address of a control buffer containing additional info needed
 *		for this command execution.
 *
 * gse_ctrl
 *		GSE control operations. This includes cache operations and table
 *		entry statistics read/clear operation. Values are defined in
 *		enum %HAL_TCL_GSE_CTRL.
 *
 * gse_sel
 *		To select the ASE/FSE to do the operation mention by GSE_ctrl.
 *		0: FSE select 1: ASE select
 *
 * status_destination_ring_id
 *		TCL status ring to which the GSE status needs to be send.
 *
 * swap
 *		Bit to enable byte swapping of contents of buffer.
 *
 * meta_data
 *		Meta data to be returned in the status descriptor
 */

enum hal_tcl_cache_op_res {
	HAL_TCL_CACHE_OP_RES_DONE,
	HAL_TCL_CACHE_OP_RES_NOT_FOUND,
	HAL_TCL_CACHE_OP_RES_TIMEOUT,
};

#define HAL_TCL_STATUS_RING_INFO0_GSE_CTRL		GENMASK(3, 0)
#define HAL_TCL_STATUS_RING_INFO0_GSE_SEL		BIT(4)
#define HAL_TCL_STATUS_RING_INFO0_CACHE_OP_RES		GENMASK(6, 5)
#define HAL_TCL_STATUS_RING_INFO0_MSDU_CNT		GENMASK(31, 8)

#define HAL_TCL_STATUS_RING_INFO1_HASH_IDX		GENMASK(19, 0)

#define HAL_TCL_STATUS_RING_INFO2_RING_ID		GENMASK(27, 20)
#define HAL_TCL_STATUS_RING_INFO2_LOOPING_COUNT		GENMASK(31, 28)

struct hal_tcl_status_ring {
	u32 info0;
	u32 msdu_byte_count;
	u32 msdu_timestamp;
	u32 meta_data[2];
	u32 info1;
	u32 rsvd0;
	u32 info2;
} __packed;

/* hal_tcl_status_ring
 *
 * gse_ctrl
 *		GSE control operations. This includes cache operations and table
 *		entry statistics read/clear operation. Values are defined in
 *		enum %HAL_TCL_GSE_CTRL.
 *
 * gse_sel
 *		To select the ASE/FSE to do the operation mention by GSE_ctrl.
 *		0: FSE select 1: ASE select
 *
 * cache_op_res
 *		Cache operation result. Values are defined in enum
 *		%HAL_TCL_CACHE_OP_RES_.
 *
 * msdu_cnt
 * msdu_byte_count
 *		MSDU count of Entry and MSDU byte count for entry 1.
 *
 * hash_indx
 *		Hash value of the entry in case of search failed or disabled.
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
	u32 buffer_addr_low;
	u32 buffer_addr_info; /* %HAL_CE_SRC_DESC_ADDR_INFO_ */
	u32 meta_info; /* %HAL_CE_SRC_DESC_META_INFO_ */
	u32 flags; /* %HAL_CE_SRC_DESC_FLAGS_ */
} __packed;

/*
 * hal_ce_srng_src_desc
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
	u32 buffer_addr_low;
	u32 buffer_addr_info; /* %HAL_CE_DEST_DESC_ADDR_INFO_ */
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
	u32 flags; /* %HAL_CE_DST_STATUS_DESC_FLAGS_ */
	u32 toeplitz_hash0;
	u32 toeplitz_hash1;
	u32 meta_info; /* HAL_CE_DST_STATUS_DESC_META_INFO_ */
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
#define HAL_TX_RATE_STATS_INFO0_BW		GENMASK(2, 1)
#define HAL_TX_RATE_STATS_INFO0_PKT_TYPE	GENMASK(6, 3)
#define HAL_TX_RATE_STATS_INFO0_STBC		BIT(7)
#define HAL_TX_RATE_STATS_INFO0_LDPC		BIT(8)
#define HAL_TX_RATE_STATS_INFO0_SGI		GENMASK(10, 9)
#define HAL_TX_RATE_STATS_INFO0_MCS		GENMASK(14, 11)
#define HAL_TX_RATE_STATS_INFO0_OFDMA_TX	BIT(15)
#define HAL_TX_RATE_STATS_INFO0_TONES_IN_RU	GENMASK(27, 16)

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
};

enum hal_tx_rate_stats_sgi {
	HAL_TX_RATE_STATS_SGI_08US,
	HAL_TX_RATE_STATS_SGI_04US,
	HAL_TX_RATE_STATS_SGI_16US,
	HAL_TX_RATE_STATS_SGI_32US,
};

struct hal_tx_rate_stats {
	u32 info0;
	u32 tsf;
} __packed;

struct hal_wbm_link_desc {
	struct ath11k_buffer_addr buf_addr_info;
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

#define HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE		GENMASK(2, 0)
#define HAL_WBM_RELEASE_INFO0_BM_ACTION			GENMASK(5, 3)
#define HAL_WBM_RELEASE_INFO0_DESC_TYPE			GENMASK(8, 6)
#define HAL_WBM_RELEASE_INFO0_FIRST_MSDU_IDX		GENMASK(12, 9)
#define HAL_WBM_RELEASE_INFO0_TQM_RELEASE_REASON	GENMASK(16, 13)
#define HAL_WBM_RELEASE_INFO0_RXDMA_PUSH_REASON		GENMASK(18, 17)
#define HAL_WBM_RELEASE_INFO0_RXDMA_ERROR_CODE		GENMASK(23, 19)
#define HAL_WBM_RELEASE_INFO0_REO_PUSH_REASON		GENMASK(25, 24)
#define HAL_WBM_RELEASE_INFO0_REO_ERROR_CODE		GENMASK(30, 26)
#define HAL_WBM_RELEASE_INFO0_WBM_INTERNAL_ERROR	BIT(31)

#define HAL_WBM_RELEASE_INFO1_TQM_STATUS_NUMBER		GENMASK(23, 0)
#define HAL_WBM_RELEASE_INFO1_TRANSMIT_COUNT		GENMASK(30, 24)

#define HAL_WBM_RELEASE_INFO2_ACK_FRAME_RSSI		GENMASK(7, 0)
#define HAL_WBM_RELEASE_INFO2_SW_REL_DETAILS_VALID	BIT(8)
#define HAL_WBM_RELEASE_INFO2_FIRST_MSDU		BIT(9)
#define HAL_WBM_RELEASE_INFO2_LAST_MSDU			BIT(10)
#define HAL_WBM_RELEASE_INFO2_MSDU_IN_AMSDU		BIT(11)
#define HAL_WBM_RELEASE_INFO2_FW_TX_NOTIF_FRAME		BIT(12)
#define HAL_WBM_RELEASE_INFO2_BUFFER_TIMESTAMP		GENMASK(31, 13)

#define HAL_WBM_RELEASE_INFO3_PEER_ID			GENMASK(15, 0)
#define HAL_WBM_RELEASE_INFO3_TID			GENMASK(19, 16)
#define HAL_WBM_RELEASE_INFO3_RING_ID			GENMASK(27, 20)
#define HAL_WBM_RELEASE_INFO3_LOOPING_COUNT		GENMASK(31, 28)

#define HAL_WBM_REL_HTT_TX_COMP_INFO0_STATUS		GENMASK(12, 9)
#define HAL_WBM_REL_HTT_TX_COMP_INFO0_REINJ_REASON	GENMASK(16, 13)
#define HAL_WBM_REL_HTT_TX_COMP_INFO0_EXP_FRAME		BIT(17)

struct hal_wbm_release_ring {
	struct ath11k_buffer_addr buf_addr_info;
	u32 info0;
	u32 info1;
	u32 info2;
	struct hal_tx_rate_stats rate_stats;
	u32 info3;
} __packed;

/* hal_wbm_release_ring
 *
 *	Producer: SW/TQM/RXDMA/REO/SWITCH
 *	Consumer: WBM/SW/FW
 *
 * HTT tx status is overlayed on wbm_release ring on 4-byte words 2, 3, 4 and 5
 * for software based completions.
 *
 * buf_addr_info
 *	Details of the physical address of the buffer or link descriptor.
 *
 * release_source_module
 *	Indicates which module initiated the release of this buffer/descriptor.
 *	Values are defined in enum %HAL_WBM_REL_SRC_MODULE_.
 *
 * bm_action
 *	Field only valid when the field return_buffer_manager in
 *	Released_buff_or_desc_addr_info indicates:
 *		WBM_IDLE_BUF_LIST / WBM_IDLE_DESC_LIST
 *	Values are defined in enum %HAL_WBM_REL_BM_ACT_.
 *
 * buffer_or_desc_type
 *	Field only valid when WBM is marked as the return_buffer_manager in
 *	the Released_Buffer_address_info. Indicates that type of buffer or
 *	descriptor is being released. Values are in enum %HAL_WBM_REL_DESC_TYPE.
 *
 * first_msdu_index
 *	Field only valid for the bm_action release_msdu_list. The index of the
 *	first MSDU in an MSDU link descriptor all belonging to the same MPDU.
 *
 * tqm_release_reason
 *	Field only valid when Release_source_module is set to release_source_TQM
 *	Release reasons are defined in enum %HAL_WBM_TQM_REL_REASON_.
 *
 * rxdma_push_reason
 * reo_push_reason
 *	Indicates why rxdma/reo pushed the frame to this ring and values are
 *	defined in enum %HAL_REO_DEST_RING_PUSH_REASON_.
 *
 * rxdma_error_code
 *	Field only valid when 'rxdma_push_reason' set to 'error_detected'.
 *	Values are defined in enum %HAL_REO_ENTR_RING_RXDMA_ECODE_.
 *
 * reo_error_code
 *	Field only valid when 'reo_push_reason' set to 'error_detected'. Values
 *	are defined in enum %HAL_REO_DEST_RING_ERROR_CODE_.
 *
 * wbm_internal_error
 *	Is set when WBM got a buffer pointer but the action was to push it to
 *	the idle link descriptor ring or do link related activity OR
 *	Is set when WBM got a link buffer pointer but the action was to push it
 *	to the buffer descriptor ring.
 *
 * tqm_status_number
 *	The value in this field is equal to tqm_cmd_number in TQM command. It is
 *	used to correlate the statu with TQM commands. Only valid when
 *	release_source_module is TQM.
 *
 * transmit_count
 *	The number of times the frame has been transmitted, valid only when
 *	release source in TQM.
 *
 * ack_frame_rssi
 *	This field is only valid when the source is TQM. If this frame is
 *	removed as the result of the reception of an ACK or BA, this field
 *	indicates the RSSI of the received ACK or BA frame.
 *
 * sw_release_details_valid
 *	This is set when WMB got a 'release_msdu_list' command from TQM and
 *	return buffer manager is not WMB. WBM will then de-aggregate all MSDUs
 *	and pass them one at a time on to the 'buffer owner'.
 *
 * first_msdu
 *	Field only valid when SW_release_details_valid is set.
 *	When set, this MSDU is the first MSDU pointed to in the
 *	'release_msdu_list' command.
 *
 * last_msdu
 *	Field only valid when SW_release_details_valid is set.
 *	When set, this MSDU is the last MSDU pointed to in the
 *	'release_msdu_list' command.
 *
 * msdu_part_of_amsdu
 *	Field only valid when SW_release_details_valid is set.
 *	When set, this MSDU was part of an A-MSDU in MPDU
 *
 * fw_tx_notify_frame
 *	Field only valid when SW_release_details_valid is set.
 *
 * buffer_timestamp
 *	Field only valid when SW_release_details_valid is set.
 *	This is the Buffer_timestamp field from the
 *	Timestamp in units of 1024 us
 *
 * struct hal_tx_rate_stats rate_stats
 *	Details for command execution tracking purposes.
 *
 * sw_peer_id
 * tid
 *	Field only valid when Release_source_module is set to
 *	release_source_TQM
 *
 *	1) Release of msdu buffer due to drop_frame = 1. Flow is
 *	not fetched and hence sw_peer_id and tid = 0
 *
 *	buffer_or_desc_type = e_num 0
 *	MSDU_rel_buffertqm_release_reason = e_num 1
 *	tqm_rr_rem_cmd_rem
 *
 *	2) Release of msdu buffer due to Flow is not fetched and
 *	hence sw_peer_id and tid = 0
 *
 *	buffer_or_desc_type = e_num 0
 *	MSDU_rel_buffertqm_release_reason = e_num 1
 *	tqm_rr_rem_cmd_rem
 *
 *	3) Release of msdu link due to remove_mpdu or acked_mpdu
 *	command.
 *
 *	buffer_or_desc_type = e_num1
 *	msdu_link_descriptortqm_release_reason can be:e_num 1
 *	tqm_rr_rem_cmd_reme_num 2 tqm_rr_rem_cmd_tx
 *	e_num 3 tqm_rr_rem_cmd_notxe_num 4 tqm_rr_rem_cmd_aged
 *
 *	This field represents the TID from the TX_MSDU_FLOW
 *	descriptor or TX_MPDU_QUEUE descriptor
 *
 * rind_id
 *	For debugging.
 *	This field is filled in by the SRNG module.
 *	It help to identify the ring that is being looked
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
};

struct hal_wbm_buffer_ring {
	struct ath11k_buffer_addr buf_addr_info;
};

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
	u32 info0;
} __packed;

struct hal_rx_mpdu_link_ptr {
	struct ath11k_buffer_addr addr_info;
} __packed;

struct hal_rx_msdu_details {
	struct ath11k_buffer_addr buf_addr_info;
	struct rx_msdu_desc rx_msdu_info;
} __packed;

#define HAL_RX_MSDU_LNK_INFO0_RX_QUEUE_NUMBER		GENMASK(15, 0)
#define HAL_RX_MSDU_LNK_INFO0_FIRST_MSDU_LNK		BIT(16)

struct hal_rx_msdu_link {
	struct hal_desc_header desc_hdr;
	struct ath11k_buffer_addr buf_addr_info;
	u32 info0;
	u32 pn[4];
	struct hal_rx_msdu_details msdu_link[6];
} __packed;

struct hal_rx_reo_queue_ext {
	struct hal_desc_header desc_hdr;
	u32 rsvd;
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
#define HAL_RX_REO_QUEUE_INFO0_BA_WINDOW_SIZE		GENMASK(18, 11)
#define HAL_RX_REO_QUEUE_INFO0_PN_CHECK			BIT(19)
#define HAL_RX_REO_QUEUE_INFO0_EVEN_PN			BIT(20)
#define HAL_RX_REO_QUEUE_INFO0_UNEVEN_PN		BIT(21)
#define HAL_RX_REO_QUEUE_INFO0_PN_HANDLE_ENABLE		BIT(22)
#define HAL_RX_REO_QUEUE_INFO0_PN_SIZE			GENMASK(24, 23)
#define HAL_RX_REO_QUEUE_INFO0_IGNORE_AMPDU_FLG		BIT(25)

#define HAL_RX_REO_QUEUE_INFO1_SVLD			BIT(0)
#define HAL_RX_REO_QUEUE_INFO1_SSN			GENMASK(12, 1)
#define HAL_RX_REO_QUEUE_INFO1_CURRENT_IDX		GENMASK(20, 13)
#define HAL_RX_REO_QUEUE_INFO1_SEQ_2K_ERR		BIT(21)
#define HAL_RX_REO_QUEUE_INFO1_PN_ERR			BIT(22)
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
	u32 rx_queue_num;
	u32 info0;
	u32 info1;
	u32 pn[4];
	u32 last_rx_enqueue_timestamp;
	u32 last_rx_dequeue_timestamp;
	u32 next_aging_queue[2];
	u32 prev_aging_queue[2];
	u32 rx_bitmap[8];
	u32 info2;
	u32 info3;
	u32 info4;
	u32 processed_mpdus;
	u32 processed_msdus;
	u32 processed_total_bytes;
	u32 info5;
	u32 rsvd[3];
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

#define HAL_REO_UPD_RX_QUEUE_INFO2_BA_WINDOW_SIZE		GENMASK(7, 0)
#define HAL_REO_UPD_RX_QUEUE_INFO2_PN_SIZE			GENMASK(9, 8)
#define HAL_REO_UPD_RX_QUEUE_INFO2_SVLD				BIT(10)
#define HAL_REO_UPD_RX_QUEUE_INFO2_SSN				GENMASK(22, 11)
#define HAL_REO_UPD_RX_QUEUE_INFO2_SEQ_2K_ERR			BIT(23)
#define HAL_REO_UPD_RX_QUEUE_INFO2_PN_ERR			BIT(24)
#define HAL_REO_UPD_RX_QUEUE_INFO2_PN_VALID			BIT(25)

struct hal_reo_update_rx_queue {
	struct hal_reo_cmd_hdr cmd;
	u32 queue_addr_lo;
	u32 info0;
	u32 info1;
	u32 info2;
	u32 pn[4];
} __packed;

#define HAL_REO_UNBLOCK_CACHE_INFO0_UNBLK_CACHE		BIT(0)
#define HAL_REO_UNBLOCK_CACHE_INFO0_RESOURCE_IDX	GENMASK(2, 1)

struct hal_reo_unblock_cache {
	struct hal_reo_cmd_hdr cmd;
	u32 info0;
	u32 rsvd[7];
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
	u32 info0;
	u32 timestamp;
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
 *		The amount of time REO took to excecute the command. Note that
 *		this time does not include the duration of the command waiting
 *		in the command ring, before the execution started.
 *
 * execution_status
 *		Execution status of the command. Values are defined in
 *		enum %HAL_REO_EXEC_STATUS_.
 */
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_SSN		GENMASK(11, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO0_CUR_IDX		GENMASK(19, 12)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MPDU_COUNT		GENMASK(6, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO1_MSDU_COUNT		GENMASK(31, 7)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_TIMEOUT_COUNT	GENMASK(9, 4)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_FDTB_COUNT		GENMASK(15, 10)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO2_DUPLICATE_COUNT	GENMASK(31, 16)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_FIO_COUNT		GENMASK(23, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO3_BAR_RCVD_CNT	GENMASK(31, 24)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_LATE_RX_MPDU	GENMASK(11, 0)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_WINDOW_JMP2K	GENMASK(15, 12)
#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO4_HOLE_COUNT		GENMASK(31, 16)

#define HAL_REO_GET_QUEUE_STATS_STATUS_INFO5_LOOPING_CNT	GENMASK(31, 28)

struct hal_reo_get_queue_stats_status {
	struct hal_reo_status_hdr hdr;
	u32 info0;
	u32 pn[4];
	u32 last_rx_enqueue_timestamp;
	u32 last_rx_dequeue_timestamp;
	u32 rx_bitmap[8];
	u32 info1;
	u32 info2;
	u32 info3;
	u32 num_mpdu_frames;
	u32 num_msdu_frames;
	u32 total_bytes;
	u32 info4;
	u32 info5;
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
	u32 info0;
	u32 rsvd0[21];
	u32 info1;
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
	u32 info0;
	u32 rsvd0[21];
	u32 info1;
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
	u32 info0;
	u32 rsvd0[21];
	u32 info1;
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
	u32 info0;
	u32 info1;
	u32 rsvd0[20];
	u32 info2;
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
	u32 info0;
	u32 info1;
	u32 info2;
	u32 info3;
	u32 info4;
	u32 rsvd0[17];
	u32 info5;
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

#endif /* ATH11K_HAL_DESC_H */

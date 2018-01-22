/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HAL_ORIGINALH2CFORMAT_H2C_C2H_NIC_H_
#define _HAL_ORIGINALH2CFORMAT_H2C_C2H_NIC_H_
#define CMD_ID_ORIGINAL_H2C 0X00
#define CMD_ID_H2C2H_LB 0X0
#define CMD_ID_D0_SCAN_OFFLOAD_CTRL 0X06
#define CMD_ID_RSVD_PAGE 0X0
#define CMD_ID_MEDIA_STATUS_RPT 0X01
#define CMD_ID_KEEP_ALIVE 0X03
#define CMD_ID_DISCONNECT_DECISION 0X04
#define CMD_ID_AP_OFFLOAD 0X08
#define CMD_ID_BCN_RSVDPAGE 0X09
#define CMD_ID_PROBE_RSP_RSVDPAGE 0X0A
#define CMD_ID_SET_PWR_MODE 0X00
#define CMD_ID_PS_TUNING_PARA 0X01
#define CMD_ID_PS_TUNING_PARA_II 0X02
#define CMD_ID_PS_LPS_PARA 0X03
#define CMD_ID_P2P_PS_OFFLOAD 0X04
#define CMD_ID_PS_SCAN_EN 0X05
#define CMD_ID_SAP_PS 0X06
#define CMD_ID_INACTIVE_PS 0X07
#define CMD_ID_MACID_CFG 0X00
#define CMD_ID_TXBF 0X01
#define CMD_ID_RSSI_SETTING 0X02
#define CMD_ID_AP_REQ_TXRPT 0X03
#define CMD_ID_INIT_RATE_COLLECTION 0X04
#define CMD_ID_IQK_OFFLOAD 0X05
#define CMD_ID_MACID_CFG_3SS 0X06
#define CMD_ID_RA_PARA_ADJUST 0X07
#define CMD_ID_WWLAN 0X00
#define CMD_ID_REMOTE_WAKE_CTRL 0X01
#define CMD_ID_AOAC_GLOBAL_INFO 0X02
#define CMD_ID_AOAC_RSVD_PAGE 0X03
#define CMD_ID_AOAC_RSVD_PAGE2 0X04
#define CMD_ID_D0_SCAN_OFFLOAD_INFO 0X05
#define CMD_ID_CHANNEL_SWITCH_OFFLOAD 0X07
#define CMD_ID_AOAC_RSVD_PAGE3 0X08
#define CLASS_ORIGINAL_H2C 0X00
#define CLASS_H2C2H_LB 0X07
#define CLASS_D0_SCAN_OFFLOAD_CTRL 0X04
#define CLASS_RSVD_PAGE 0X0
#define CLASS_MEDIA_STATUS_RPT 0X0
#define CLASS_KEEP_ALIVE 0X0
#define CLASS_DISCONNECT_DECISION 0X0
#define CLASS_AP_OFFLOAD 0X0
#define CLASS_BCN_RSVDPAGE 0X0
#define CLASS_PROBE_RSP_RSVDPAGE 0X0
#define CLASS_SET_PWR_MODE 0X01
#define CLASS_PS_TUNING_PARA 0X01
#define CLASS_PS_TUNING_PARA_II 0X01
#define CLASS_PS_LPS_PARA 0X01
#define CLASS_P2P_PS_OFFLOAD 0X01
#define CLASS_PS_SCAN_EN 0X1
#define CLASS_SAP_PS 0X1
#define CLASS_INACTIVE_PS 0X1
#define CLASS_MACID_CFG 0X2
#define CLASS_TXBF 0X2
#define CLASS_RSSI_SETTING 0X2
#define CLASS_AP_REQ_TXRPT 0X2
#define CLASS_INIT_RATE_COLLECTION 0X2
#define CLASS_IQK_OFFLOAD 0X2
#define CLASS_MACID_CFG_3SS 0X2
#define CLASS_RA_PARA_ADJUST 0X02
#define CLASS_WWLAN 0X4
#define CLASS_REMOTE_WAKE_CTRL 0X4
#define CLASS_AOAC_GLOBAL_INFO 0X04
#define CLASS_AOAC_RSVD_PAGE 0X04
#define CLASS_AOAC_RSVD_PAGE2 0X04
#define CLASS_D0_SCAN_OFFLOAD_INFO 0X04
#define CLASS_CHANNEL_SWITCH_OFFLOAD 0X04
#define CLASS_AOAC_RSVD_PAGE3 0X04
#define ORIGINAL_H2C_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define ORIGINAL_H2C_SET_CMD_ID(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define ORIGINAL_H2C_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define ORIGINAL_H2C_SET_CLASS(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define H2C2H_LB_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define H2C2H_LB_SET_CMD_ID(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define H2C2H_LB_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define H2C2H_LB_SET_CLASS(__h2c, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define H2C2H_LB_GET_SEQ(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define H2C2H_LB_SET_SEQ(__h2c, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define H2C2H_LB_GET_PAYLOAD1(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 16)
#define H2C2H_LB_SET_PAYLOAD1(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 16, __value)
#define H2C2H_LB_GET_PAYLOAD2(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 32)
#define H2C2H_LB_SET_PAYLOAD2(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 32, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_CMD_ID(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define D0_SCAN_OFFLOAD_CTRL_SET_CMD_ID(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_CLASS(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define D0_SCAN_OFFLOAD_CTRL_SET_CLASS(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_D0_SCAN_FUN_EN(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_D0_SCAN_FUN_EN(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_RTD3FUN_EN(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_RTD3FUN_EN(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_U3_SCAN_FUN_EN(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_U3_SCAN_FUN_EN(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_NLO_FUN_EN(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 11, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_NLO_FUN_EN(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 11, 1, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_IPS_DEPENDENT(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 12, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_IPS_DEPENDENT(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 12, 1, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_LOC_PROBE_PACKET(__h2c)                       \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 17)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_PROBE_PACKET(__h2c, __value)              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 17, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_LOC_SCAN_INFO(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_SCAN_INFO(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define D0_SCAN_OFFLOAD_CTRL_GET_LOC_SSID_INFO(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_SSID_INFO(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define RSVD_PAGE_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define RSVD_PAGE_SET_CMD_ID(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define RSVD_PAGE_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define RSVD_PAGE_SET_CLASS(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define RSVD_PAGE_GET_LOC_PROBE_RSP(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define RSVD_PAGE_SET_LOC_PROBE_RSP(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define RSVD_PAGE_GET_LOC_PS_POLL(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define RSVD_PAGE_SET_LOC_PS_POLL(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define RSVD_PAGE_GET_LOC_NULL_DATA(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define RSVD_PAGE_SET_LOC_NULL_DATA(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define RSVD_PAGE_GET_LOC_QOS_NULL(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define RSVD_PAGE_SET_LOC_QOS_NULL(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define RSVD_PAGE_GET_LOC_BT_QOS_NULL(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define RSVD_PAGE_SET_LOC_BT_QOS_NULL(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define RSVD_PAGE_GET_LOC_CTS2SELF(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 8)
#define RSVD_PAGE_SET_LOC_CTS2SELF(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 8, __value)
#define RSVD_PAGE_GET_LOC_LTECOEX_QOSNULL(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 24, 8)
#define RSVD_PAGE_SET_LOC_LTECOEX_QOSNULL(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 24, 8, __value)
#define MEDIA_STATUS_RPT_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define MEDIA_STATUS_RPT_SET_CMD_ID(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define MEDIA_STATUS_RPT_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define MEDIA_STATUS_RPT_SET_CLASS(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define MEDIA_STATUS_RPT_GET_OP_MODE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define MEDIA_STATUS_RPT_SET_OP_MODE(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define MEDIA_STATUS_RPT_GET_MACID_IN(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define MEDIA_STATUS_RPT_SET_MACID_IN(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define MEDIA_STATUS_RPT_GET_MACID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define MEDIA_STATUS_RPT_SET_MACID(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define MEDIA_STATUS_RPT_GET_MACID_END(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define MEDIA_STATUS_RPT_SET_MACID_END(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define KEEP_ALIVE_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define KEEP_ALIVE_SET_CMD_ID(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define KEEP_ALIVE_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define KEEP_ALIVE_SET_CLASS(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define KEEP_ALIVE_GET_ENABLE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define KEEP_ALIVE_SET_ENABLE(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define KEEP_ALIVE_GET_ADOPT_USER_SETTING(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define KEEP_ALIVE_SET_ADOPT_USER_SETTING(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define KEEP_ALIVE_GET_PKT_TYPE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define KEEP_ALIVE_SET_PKT_TYPE(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define KEEP_ALIVE_GET_KEEP_ALIVE_CHECK_PERIOD(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define KEEP_ALIVE_SET_KEEP_ALIVE_CHECK_PERIOD(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define DISCONNECT_DECISION_GET_CMD_ID(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define DISCONNECT_DECISION_SET_CMD_ID(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define DISCONNECT_DECISION_GET_CLASS(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define DISCONNECT_DECISION_SET_CLASS(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define DISCONNECT_DECISION_GET_ENABLE(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define DISCONNECT_DECISION_SET_ENABLE(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define DISCONNECT_DECISION_GET_ADOPT_USER_SETTING(__h2c)                      \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define DISCONNECT_DECISION_SET_ADOPT_USER_SETTING(__h2c, __value)             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define DISCONNECT_DECISION_GET_TRY_OK_BCN_FAIL_COUNT_EN(__h2c)                \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define DISCONNECT_DECISION_SET_TRY_OK_BCN_FAIL_COUNT_EN(__h2c, __value)       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define DISCONNECT_DECISION_GET_DISCONNECT_EN(__h2c)                           \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 11, 1)
#define DISCONNECT_DECISION_SET_DISCONNECT_EN(__h2c, __value)                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 11, 1, __value)
#define DISCONNECT_DECISION_GET_DISCON_DECISION_CHECK_PERIOD(__h2c)            \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define DISCONNECT_DECISION_SET_DISCON_DECISION_CHECK_PERIOD(__h2c, __value)   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define DISCONNECT_DECISION_GET_TRY_PKT_NUM(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define DISCONNECT_DECISION_SET_TRY_PKT_NUM(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define DISCONNECT_DECISION_GET_TRY_OK_BCN_FAIL_COUNT_LIMIT(__h2c)             \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define DISCONNECT_DECISION_SET_TRY_OK_BCN_FAIL_COUNT_LIMIT(__h2c, __value)    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define AP_OFFLOAD_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define AP_OFFLOAD_SET_CMD_ID(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define AP_OFFLOAD_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define AP_OFFLOAD_SET_CLASS(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define AP_OFFLOAD_GET_ON(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define AP_OFFLOAD_SET_ON(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define AP_OFFLOAD_GET_CFG_MIFI_PLATFORM(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define AP_OFFLOAD_SET_CFG_MIFI_PLATFORM(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define AP_OFFLOAD_GET_LINKED(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define AP_OFFLOAD_SET_LINKED(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define AP_OFFLOAD_GET_EN_AUTO_WAKE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 11, 1)
#define AP_OFFLOAD_SET_EN_AUTO_WAKE(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 11, 1, __value)
#define AP_OFFLOAD_GET_WAKE_FLAG(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 12, 1)
#define AP_OFFLOAD_SET_WAKE_FLAG(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 12, 1, __value)
#define AP_OFFLOAD_GET_HIDDEN_ROOT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 1)
#define AP_OFFLOAD_SET_HIDDEN_ROOT(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 1, __value)
#define AP_OFFLOAD_GET_HIDDEN_VAP1(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 17, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP1(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 17, 1, __value)
#define AP_OFFLOAD_GET_HIDDEN_VAP2(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 18, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP2(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 18, 1, __value)
#define AP_OFFLOAD_GET_HIDDEN_VAP3(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 19, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP3(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 19, 1, __value)
#define AP_OFFLOAD_GET_HIDDEN_VAP4(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 20, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP4(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 20, 1, __value)
#define AP_OFFLOAD_GET_DENYANY_ROOT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 1)
#define AP_OFFLOAD_SET_DENYANY_ROOT(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 1, __value)
#define AP_OFFLOAD_GET_DENYANY_VAP1(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 25, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP1(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 25, 1, __value)
#define AP_OFFLOAD_GET_DENYANY_VAP2(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 26, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP2(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 26, 1, __value)
#define AP_OFFLOAD_GET_DENYANY_VAP3(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 27, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP3(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 27, 1, __value)
#define AP_OFFLOAD_GET_DENYANY_VAP4(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 28, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP4(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 28, 1, __value)
#define AP_OFFLOAD_GET_WAIT_TBTT_CNT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define AP_OFFLOAD_SET_WAIT_TBTT_CNT(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define AP_OFFLOAD_GET_WAKE_TIMEOUT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define AP_OFFLOAD_SET_WAKE_TIMEOUT(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define AP_OFFLOAD_GET_LEN_IV_PAIR(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 8)
#define AP_OFFLOAD_SET_LEN_IV_PAIR(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 8, __value)
#define AP_OFFLOAD_GET_LEN_IV_GRP(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 24, 8)
#define AP_OFFLOAD_SET_LEN_IV_GRP(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 24, 8, __value)
#define BCN_RSVDPAGE_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define BCN_RSVDPAGE_SET_CMD_ID(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define BCN_RSVDPAGE_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define BCN_RSVDPAGE_SET_CLASS(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define BCN_RSVDPAGE_GET_LOC_ROOT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define BCN_RSVDPAGE_SET_LOC_ROOT(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define BCN_RSVDPAGE_GET_LOC_VAP1(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP1(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define BCN_RSVDPAGE_GET_LOC_VAP2(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP2(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define BCN_RSVDPAGE_GET_LOC_VAP3(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP3(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define BCN_RSVDPAGE_GET_LOC_VAP4(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP4(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define PROBE_RSP_RSVDPAGE_GET_CMD_ID(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define PROBE_RSP_RSVDPAGE_SET_CMD_ID(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define PROBE_RSP_RSVDPAGE_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define PROBE_RSP_RSVDPAGE_SET_CLASS(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_ROOT(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_ROOT(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP1(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP1(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP2(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP2(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP3(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP3(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP4(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP4(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define SET_PWR_MODE_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define SET_PWR_MODE_SET_CMD_ID(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define SET_PWR_MODE_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define SET_PWR_MODE_SET_CLASS(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define SET_PWR_MODE_GET_MODE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 7)
#define SET_PWR_MODE_SET_MODE(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 7, __value)
#define SET_PWR_MODE_GET_CLK_REQUEST(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 15, 1)
#define SET_PWR_MODE_SET_CLK_REQUEST(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 15, 1, __value)
#define SET_PWR_MODE_GET_RLBM(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 4)
#define SET_PWR_MODE_SET_RLBM(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 4, __value)
#define SET_PWR_MODE_GET_SMART_PS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 20, 4)
#define SET_PWR_MODE_SET_SMART_PS(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 20, 4, __value)
#define SET_PWR_MODE_GET_AWAKE_INTERVAL(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define SET_PWR_MODE_SET_AWAKE_INTERVAL(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define SET_PWR_MODE_GET_B_ALL_QUEUE_UAPSD(__h2c)                              \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 1)
#define SET_PWR_MODE_SET_B_ALL_QUEUE_UAPSD(__h2c, __value)                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 1, __value)
#define SET_PWR_MODE_GET_BCN_EARLY_RPT(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 2, 1)
#define SET_PWR_MODE_SET_BCN_EARLY_RPT(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 2, 1, __value)
#define SET_PWR_MODE_GET_PORT_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 5, 3)
#define SET_PWR_MODE_SET_PORT_ID(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 5, 3, __value)
#define SET_PWR_MODE_GET_PWR_STATE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define SET_PWR_MODE_SET_PWR_STATE(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define SET_PWR_MODE_GET_LOW_POWER_RX_BCN(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 1)
#define SET_PWR_MODE_SET_LOW_POWER_RX_BCN(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 1, __value)
#define SET_PWR_MODE_GET_ANT_AUTO_SWITCH(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 17, 1)
#define SET_PWR_MODE_SET_ANT_AUTO_SWITCH(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 17, 1, __value)
#define SET_PWR_MODE_GET_PS_ALLOW_BT_HIGH_PRIORITY(__h2c)                      \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 18, 1)
#define SET_PWR_MODE_SET_PS_ALLOW_BT_HIGH_PRIORITY(__h2c, __value)             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 18, 1, __value)
#define SET_PWR_MODE_GET_PROTECT_BCN(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 19, 1)
#define SET_PWR_MODE_SET_PROTECT_BCN(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 19, 1, __value)
#define SET_PWR_MODE_GET_SILENCE_PERIOD(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 20, 1)
#define SET_PWR_MODE_SET_SILENCE_PERIOD(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 20, 1, __value)
#define SET_PWR_MODE_GET_FAST_BT_CONNECT(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 21, 1)
#define SET_PWR_MODE_SET_FAST_BT_CONNECT(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 21, 1, __value)
#define SET_PWR_MODE_GET_TWO_ANTENNA_EN(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 22, 1)
#define SET_PWR_MODE_SET_TWO_ANTENNA_EN(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 22, 1, __value)
#define SET_PWR_MODE_GET_ADOPT_USER_SETTING(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 24, 1)
#define SET_PWR_MODE_SET_ADOPT_USER_SETTING(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 24, 1, __value)
#define SET_PWR_MODE_GET_DRV_BCN_EARLY_SHIFT(__h2c)                            \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 25, 3)
#define SET_PWR_MODE_SET_DRV_BCN_EARLY_SHIFT(__h2c, __value)                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 25, 3, __value)
#define SET_PWR_MODE_GET_DRV_BCN_EARLY_SHIFT2(__h2c)                           \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 28, 4)
#define SET_PWR_MODE_SET_DRV_BCN_EARLY_SHIFT2(__h2c, __value)                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 28, 4, __value)
#define PS_TUNING_PARA_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define PS_TUNING_PARA_SET_CMD_ID(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define PS_TUNING_PARA_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define PS_TUNING_PARA_SET_CLASS(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define PS_TUNING_PARA_GET_BCN_TO_LIMIT(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 7)
#define PS_TUNING_PARA_SET_BCN_TO_LIMIT(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 7, __value)
#define PS_TUNING_PARA_GET_DTIM_TIME_OUT(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 15, 1)
#define PS_TUNING_PARA_SET_DTIM_TIME_OUT(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 15, 1, __value)
#define PS_TUNING_PARA_GET_PS_TIME_OUT(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 4)
#define PS_TUNING_PARA_SET_PS_TIME_OUT(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 4, __value)
#define PS_TUNING_PARA_GET_ADOPT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define PS_TUNING_PARA_SET_ADOPT(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define PS_TUNING_PARA_II_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define PS_TUNING_PARA_II_SET_CMD_ID(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define PS_TUNING_PARA_II_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define PS_TUNING_PARA_II_SET_CLASS(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define PS_TUNING_PARA_II_GET_BCN_TO_PERIOD(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 7)
#define PS_TUNING_PARA_II_SET_BCN_TO_PERIOD(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 7, __value)
#define PS_TUNING_PARA_II_GET_ADOPT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 15, 1)
#define PS_TUNING_PARA_II_SET_ADOPT(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 15, 1, __value)
#define PS_TUNING_PARA_II_GET_DRV_EARLY_IVL(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define PS_TUNING_PARA_II_SET_DRV_EARLY_IVL(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define PS_LPS_PARA_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define PS_LPS_PARA_SET_CMD_ID(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define PS_LPS_PARA_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define PS_LPS_PARA_SET_CLASS(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define PS_LPS_PARA_GET_LPS_CONTROL(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define PS_LPS_PARA_SET_LPS_CONTROL(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define P2P_PS_OFFLOAD_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define P2P_PS_OFFLOAD_SET_CMD_ID(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define P2P_PS_OFFLOAD_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define P2P_PS_OFFLOAD_SET_CLASS(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define P2P_PS_OFFLOAD_GET_OFFLOAD_EN(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define P2P_PS_OFFLOAD_SET_OFFLOAD_EN(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define P2P_PS_OFFLOAD_GET_ROLE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define P2P_PS_OFFLOAD_SET_ROLE(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define P2P_PS_OFFLOAD_GET_CTWINDOW_EN(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define P2P_PS_OFFLOAD_SET_CTWINDOW_EN(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define P2P_PS_OFFLOAD_GET_NOA0_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 11, 1)
#define P2P_PS_OFFLOAD_SET_NOA0_EN(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 11, 1, __value)
#define P2P_PS_OFFLOAD_GET_NOA1_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 12, 1)
#define P2P_PS_OFFLOAD_SET_NOA1_EN(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 12, 1, __value)
#define P2P_PS_OFFLOAD_GET_ALL_STA_SLEEP(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 13, 1)
#define P2P_PS_OFFLOAD_SET_ALL_STA_SLEEP(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 13, 1, __value)
#define P2P_PS_OFFLOAD_GET_DISCOVERY(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 14, 1)
#define P2P_PS_OFFLOAD_SET_DISCOVERY(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 14, 1, __value)
#define PS_SCAN_EN_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define PS_SCAN_EN_SET_CMD_ID(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define PS_SCAN_EN_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define PS_SCAN_EN_SET_CLASS(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define PS_SCAN_EN_GET_ENABLE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define PS_SCAN_EN_SET_ENABLE(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define SAP_PS_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define SAP_PS_SET_CMD_ID(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define SAP_PS_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define SAP_PS_SET_CLASS(__h2c, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define SAP_PS_GET_ENABLE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define SAP_PS_SET_ENABLE(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define SAP_PS_GET_EN_PS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define SAP_PS_SET_EN_PS(__h2c, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define SAP_PS_GET_EN_LP_RX(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define SAP_PS_SET_EN_LP_RX(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define SAP_PS_GET_MANUAL_32K(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 11, 1)
#define SAP_PS_SET_MANUAL_32K(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 11, 1, __value)
#define SAP_PS_GET_DURATION(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define SAP_PS_SET_DURATION(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define INACTIVE_PS_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define INACTIVE_PS_SET_CMD_ID(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define INACTIVE_PS_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define INACTIVE_PS_SET_CLASS(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define INACTIVE_PS_GET_ENABLE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define INACTIVE_PS_SET_ENABLE(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define INACTIVE_PS_GET_IGNORE_PS_CONDITION(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define INACTIVE_PS_SET_IGNORE_PS_CONDITION(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define INACTIVE_PS_GET_FREQUENCY(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define INACTIVE_PS_SET_FREQUENCY(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define INACTIVE_PS_GET_DURATION(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define INACTIVE_PS_SET_DURATION(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define MACID_CFG_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define MACID_CFG_SET_CMD_ID(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define MACID_CFG_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define MACID_CFG_SET_CLASS(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define MACID_CFG_GET_MAC_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define MACID_CFG_SET_MAC_ID(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define MACID_CFG_GET_RATE_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 5)
#define MACID_CFG_SET_RATE_ID(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 5, __value)
#define MACID_CFG_GET_SGI(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 23, 1)
#define MACID_CFG_SET_SGI(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 23, 1, __value)
#define MACID_CFG_GET_BW(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 2)
#define MACID_CFG_SET_BW(__h2c, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 2, __value)
#define MACID_CFG_GET_LDPC_CAP(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 26, 1)
#define MACID_CFG_SET_LDPC_CAP(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 26, 1, __value)
#define MACID_CFG_GET_NO_UPDATE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 27, 1)
#define MACID_CFG_SET_NO_UPDATE(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 27, 1, __value)
#define MACID_CFG_GET_WHT_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 28, 2)
#define MACID_CFG_SET_WHT_EN(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 28, 2, __value)
#define MACID_CFG_GET_DISPT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 30, 1)
#define MACID_CFG_SET_DISPT(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 30, 1, __value)
#define MACID_CFG_GET_DISRA(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 31, 1)
#define MACID_CFG_SET_DISRA(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 31, 1, __value)
#define MACID_CFG_GET_RATE_MASK7_0(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define MACID_CFG_SET_RATE_MASK7_0(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define MACID_CFG_GET_RATE_MASK15_8(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define MACID_CFG_SET_RATE_MASK15_8(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define MACID_CFG_GET_RATE_MASK23_16(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 8)
#define MACID_CFG_SET_RATE_MASK23_16(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 8, __value)
#define MACID_CFG_GET_RATE_MASK31_24(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 24, 8)
#define MACID_CFG_SET_RATE_MASK31_24(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 24, 8, __value)
#define TXBF_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define TXBF_SET_CMD_ID(__h2c, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define TXBF_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define TXBF_SET_CLASS(__h2c, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define TXBF_GET_NDPA0_HEAD_PAGE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define TXBF_SET_NDPA0_HEAD_PAGE(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define TXBF_GET_NDPA1_HEAD_PAGE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define TXBF_SET_NDPA1_HEAD_PAGE(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define TXBF_GET_PERIOD_0(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define TXBF_SET_PERIOD_0(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define RSSI_SETTING_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define RSSI_SETTING_SET_CMD_ID(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define RSSI_SETTING_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define RSSI_SETTING_SET_CLASS(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define RSSI_SETTING_GET_MAC_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define RSSI_SETTING_SET_MAC_ID(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define RSSI_SETTING_GET_RSSI(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 7)
#define RSSI_SETTING_SET_RSSI(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 7, __value)
#define RSSI_SETTING_GET_RA_INFO(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define RSSI_SETTING_SET_RA_INFO(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define AP_REQ_TXRPT_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define AP_REQ_TXRPT_SET_CMD_ID(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define AP_REQ_TXRPT_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define AP_REQ_TXRPT_SET_CLASS(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define AP_REQ_TXRPT_GET_STA1_MACID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define AP_REQ_TXRPT_SET_STA1_MACID(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define AP_REQ_TXRPT_GET_STA2_MACID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define AP_REQ_TXRPT_SET_STA2_MACID(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define AP_REQ_TXRPT_GET_RTY_OK_TOTAL(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 1)
#define AP_REQ_TXRPT_SET_RTY_OK_TOTAL(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 1, __value)
#define AP_REQ_TXRPT_GET_RTY_CNT_MACID(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 25, 1)
#define AP_REQ_TXRPT_SET_RTY_CNT_MACID(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 25, 1, __value)
#define INIT_RATE_COLLECTION_GET_CMD_ID(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define INIT_RATE_COLLECTION_SET_CMD_ID(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define INIT_RATE_COLLECTION_GET_CLASS(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define INIT_RATE_COLLECTION_SET_CLASS(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define INIT_RATE_COLLECTION_GET_STA1_MACID(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define INIT_RATE_COLLECTION_SET_STA1_MACID(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define INIT_RATE_COLLECTION_GET_STA2_MACID(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define INIT_RATE_COLLECTION_SET_STA2_MACID(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define INIT_RATE_COLLECTION_GET_STA3_MACID(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define INIT_RATE_COLLECTION_SET_STA3_MACID(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define INIT_RATE_COLLECTION_GET_STA4_MACID(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define INIT_RATE_COLLECTION_SET_STA4_MACID(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define INIT_RATE_COLLECTION_GET_STA5_MACID(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define INIT_RATE_COLLECTION_SET_STA5_MACID(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define INIT_RATE_COLLECTION_GET_STA6_MACID(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 8)
#define INIT_RATE_COLLECTION_SET_STA6_MACID(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 8, __value)
#define INIT_RATE_COLLECTION_GET_STA7_MACID(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 24, 8)
#define INIT_RATE_COLLECTION_SET_STA7_MACID(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 24, 8, __value)
#define IQK_OFFLOAD_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define IQK_OFFLOAD_SET_CMD_ID(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define IQK_OFFLOAD_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define IQK_OFFLOAD_SET_CLASS(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define IQK_OFFLOAD_GET_CHANNEL(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define IQK_OFFLOAD_SET_CHANNEL(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define IQK_OFFLOAD_GET_BWBAND(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define IQK_OFFLOAD_SET_BWBAND(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define IQK_OFFLOAD_GET_EXTPALNA(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define IQK_OFFLOAD_SET_EXTPALNA(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define MACID_CFG_3SS_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define MACID_CFG_3SS_SET_CMD_ID(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define MACID_CFG_3SS_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define MACID_CFG_3SS_SET_CLASS(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define MACID_CFG_3SS_GET_MACID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define MACID_CFG_3SS_SET_MACID(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define MACID_CFG_3SS_GET_RATE_MASK_39_32(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define MACID_CFG_3SS_SET_RATE_MASK_39_32(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define MACID_CFG_3SS_GET_RATE_MASK_47_40(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define MACID_CFG_3SS_SET_RATE_MASK_47_40(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define RA_PARA_ADJUST_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define RA_PARA_ADJUST_SET_CMD_ID(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define RA_PARA_ADJUST_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define RA_PARA_ADJUST_SET_CLASS(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define RA_PARA_ADJUST_GET_MAC_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define RA_PARA_ADJUST_SET_MAC_ID(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define RA_PARA_ADJUST_GET_PARAMETER_INDEX(__h2c)                              \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define RA_PARA_ADJUST_SET_PARAMETER_INDEX(__h2c, __value)                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define RA_PARA_ADJUST_GET_RATE_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define RA_PARA_ADJUST_SET_RATE_ID(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define RA_PARA_ADJUST_GET_VALUE_BYTE0(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define RA_PARA_ADJUST_SET_VALUE_BYTE0(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define RA_PARA_ADJUST_GET_VALUE_BYTE1(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define RA_PARA_ADJUST_SET_VALUE_BYTE1(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define RA_PARA_ADJUST_GET_ASK_FW_FOR_FW_PARA(__h2c)                           \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 8)
#define RA_PARA_ADJUST_SET_ASK_FW_FOR_FW_PARA(__h2c, __value)                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 8, __value)
#define WWLAN_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define WWLAN_SET_CMD_ID(__h2c, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define WWLAN_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define WWLAN_SET_CLASS(__h2c, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define WWLAN_GET_FUNC_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define WWLAN_SET_FUNC_EN(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define WWLAN_GET_PATTERM_MAT_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define WWLAN_SET_PATTERM_MAT_EN(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define WWLAN_GET_MAGIC_PKT_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define WWLAN_SET_MAGIC_PKT_EN(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define WWLAN_GET_UNICAST_WAKEUP_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 11, 1)
#define WWLAN_SET_UNICAST_WAKEUP_EN(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 11, 1, __value)
#define WWLAN_GET_ALL_PKT_DROP(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 12, 1)
#define WWLAN_SET_ALL_PKT_DROP(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 12, 1, __value)
#define WWLAN_GET_GPIO_ACTIVE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 13, 1)
#define WWLAN_SET_GPIO_ACTIVE(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 13, 1, __value)
#define WWLAN_GET_REKEY_WAKEUP_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 14, 1)
#define WWLAN_SET_REKEY_WAKEUP_EN(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 14, 1, __value)
#define WWLAN_GET_DEAUTH_WAKEUP_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 15, 1)
#define WWLAN_SET_DEAUTH_WAKEUP_EN(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 15, 1, __value)
#define WWLAN_GET_GPIO_NUM(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 7)
#define WWLAN_SET_GPIO_NUM(__h2c, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 7, __value)
#define WWLAN_GET_DATAPIN_WAKEUP_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 23, 1)
#define WWLAN_SET_DATAPIN_WAKEUP_EN(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 23, 1, __value)
#define WWLAN_GET_GPIO_DURATION(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define WWLAN_SET_GPIO_DURATION(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define WWLAN_GET_GPIO_PLUS_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 1)
#define WWLAN_SET_GPIO_PLUS_EN(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 1, __value)
#define WWLAN_GET_GPIO_PULSE_COUNT(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 1, 7)
#define WWLAN_SET_GPIO_PULSE_COUNT(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 1, 7, __value)
#define WWLAN_GET_DISABLE_UPHY(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 1)
#define WWLAN_SET_DISABLE_UPHY(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 1, __value)
#define WWLAN_GET_HST2DEV_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 9, 1)
#define WWLAN_SET_HST2DEV_EN(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 9, 1, __value)
#define WWLAN_GET_GPIO_DURATION_MS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 10, 1)
#define WWLAN_SET_GPIO_DURATION_MS(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 10, 1, __value)
#define REMOTE_WAKE_CTRL_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define REMOTE_WAKE_CTRL_SET_CMD_ID(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define REMOTE_WAKE_CTRL_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define REMOTE_WAKE_CTRL_SET_CLASS(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define REMOTE_WAKE_CTRL_GET_REMOTE_WAKE_CTRL_EN(__h2c)                        \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 1)
#define REMOTE_WAKE_CTRL_SET_REMOTE_WAKE_CTRL_EN(__h2c, __value)               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 1, __value)
#define REMOTE_WAKE_CTRL_GET_ARP_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 9, 1)
#define REMOTE_WAKE_CTRL_SET_ARP_EN(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 9, 1, __value)
#define REMOTE_WAKE_CTRL_GET_NDP_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 10, 1)
#define REMOTE_WAKE_CTRL_SET_NDP_EN(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 10, 1, __value)
#define REMOTE_WAKE_CTRL_GET_GTK_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 11, 1)
#define REMOTE_WAKE_CTRL_SET_GTK_EN(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 11, 1, __value)
#define REMOTE_WAKE_CTRL_GET_NLO_OFFLOAD_EN(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 12, 1)
#define REMOTE_WAKE_CTRL_SET_NLO_OFFLOAD_EN(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 12, 1, __value)
#define REMOTE_WAKE_CTRL_GET_REAL_WOW_V1_EN(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 13, 1)
#define REMOTE_WAKE_CTRL_SET_REAL_WOW_V1_EN(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 13, 1, __value)
#define REMOTE_WAKE_CTRL_GET_REAL_WOW_V2_EN(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 14, 1)
#define REMOTE_WAKE_CTRL_SET_REAL_WOW_V2_EN(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 14, 1, __value)
#define REMOTE_WAKE_CTRL_GET_FW_UNICAST(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 15, 1)
#define REMOTE_WAKE_CTRL_SET_FW_UNICAST(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 15, 1, __value)
#define REMOTE_WAKE_CTRL_GET_P2P_OFFLOAD_EN(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 1)
#define REMOTE_WAKE_CTRL_SET_P2P_OFFLOAD_EN(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 1, __value)
#define REMOTE_WAKE_CTRL_GET_RUNTIME_PM_EN(__h2c)                              \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 17, 1)
#define REMOTE_WAKE_CTRL_SET_RUNTIME_PM_EN(__h2c, __value)                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 17, 1, __value)
#define REMOTE_WAKE_CTRL_GET_NET_BIOS_DROP_EN(__h2c)                           \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 18, 1)
#define REMOTE_WAKE_CTRL_SET_NET_BIOS_DROP_EN(__h2c, __value)                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 18, 1, __value)
#define REMOTE_WAKE_CTRL_GET_ARP_ACTION(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 1)
#define REMOTE_WAKE_CTRL_SET_ARP_ACTION(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 1, __value)
#define REMOTE_WAKE_CTRL_GET_FW_PARSING_UNTIL_WAKEUP(__h2c)                    \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 28, 1)
#define REMOTE_WAKE_CTRL_SET_FW_PARSING_UNTIL_WAKEUP(__h2c, __value)           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 28, 1, __value)
#define REMOTE_WAKE_CTRL_GET_FW_PARSING_AFTER_WAKEUP(__h2c)                    \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 29, 1)
#define REMOTE_WAKE_CTRL_SET_FW_PARSING_AFTER_WAKEUP(__h2c, __value)           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 29, 1, __value)
#define AOAC_GLOBAL_INFO_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define AOAC_GLOBAL_INFO_SET_CMD_ID(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define AOAC_GLOBAL_INFO_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define AOAC_GLOBAL_INFO_SET_CLASS(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define AOAC_GLOBAL_INFO_GET_PAIR_WISE_ENC_ALG(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define AOAC_GLOBAL_INFO_SET_PAIR_WISE_ENC_ALG(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define AOAC_GLOBAL_INFO_GET_GROUP_ENC_ALG(__h2c)                              \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define AOAC_GLOBAL_INFO_SET_GROUP_ENC_ALG(__h2c, __value)                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define AOAC_RSVD_PAGE_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define AOAC_RSVD_PAGE_SET_CMD_ID(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define AOAC_RSVD_PAGE_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define AOAC_RSVD_PAGE_SET_CLASS(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define AOAC_RSVD_PAGE_GET_LOC_REMOTE_CTRL_INFO(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define AOAC_RSVD_PAGE_SET_LOC_REMOTE_CTRL_INFO(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define AOAC_RSVD_PAGE_GET_LOC_ARP_RESPONSE(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define AOAC_RSVD_PAGE_SET_LOC_ARP_RESPONSE(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define AOAC_RSVD_PAGE_GET_LOC_NEIGHBOR_ADVERTISEMENT(__h2c)                   \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define AOAC_RSVD_PAGE_SET_LOC_NEIGHBOR_ADVERTISEMENT(__h2c, __value)          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define AOAC_RSVD_PAGE_GET_LOC_GTK_RSP(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_RSP(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define AOAC_RSVD_PAGE_GET_LOC_GTK_INFO(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_INFO(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define AOAC_RSVD_PAGE_GET_LOC_GTK_EXT_MEM(__h2c)                              \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 8)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_EXT_MEM(__h2c, __value)                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 8, __value)
#define AOAC_RSVD_PAGE_GET_LOC_NDP_INFO(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 24, 8)
#define AOAC_RSVD_PAGE_SET_LOC_NDP_INFO(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 24, 8, __value)
#define AOAC_RSVD_PAGE2_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define AOAC_RSVD_PAGE2_SET_CMD_ID(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define AOAC_RSVD_PAGE2_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define AOAC_RSVD_PAGE2_SET_CLASS(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define AOAC_RSVD_PAGE2_GET_LOC_ROUTER_SOLICATION(__h2c)                       \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_ROUTER_SOLICATION(__h2c, __value)              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define AOAC_RSVD_PAGE2_GET_LOC_BUBBLE_PACKET(__h2c)                           \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_BUBBLE_PACKET(__h2c, __value)                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define AOAC_RSVD_PAGE2_GET_LOC_TEREDO_INFO(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_TEREDO_INFO(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define AOAC_RSVD_PAGE2_GET_LOC_REALWOW_INFO(__h2c)                            \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_REALWOW_INFO(__h2c, __value)                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 8, __value)
#define AOAC_RSVD_PAGE2_GET_LOC_KEEP_ALIVE_PKT(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 8, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_KEEP_ALIVE_PKT(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 8, 8, __value)
#define AOAC_RSVD_PAGE2_GET_LOC_ACK_PATTERN(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_ACK_PATTERN(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 8, __value)
#define AOAC_RSVD_PAGE2_GET_LOC_WAKEUP_PATTERN(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 24, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_WAKEUP_PATTERN(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 24, 8, __value)
#define D0_SCAN_OFFLOAD_INFO_GET_CMD_ID(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define D0_SCAN_OFFLOAD_INFO_SET_CMD_ID(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define D0_SCAN_OFFLOAD_INFO_GET_CLASS(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define D0_SCAN_OFFLOAD_INFO_SET_CLASS(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define D0_SCAN_OFFLOAD_INFO_GET_LOC_CHANNEL_INFO(__h2c)                       \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define D0_SCAN_OFFLOAD_INFO_SET_LOC_CHANNEL_INFO(__h2c, __value)              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define CHANNEL_SWITCH_OFFLOAD_GET_CMD_ID(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define CHANNEL_SWITCH_OFFLOAD_SET_CMD_ID(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define CHANNEL_SWITCH_OFFLOAD_GET_CLASS(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define CHANNEL_SWITCH_OFFLOAD_SET_CLASS(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define CHANNEL_SWITCH_OFFLOAD_GET_CHANNEL_NUM(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define CHANNEL_SWITCH_OFFLOAD_SET_CHANNEL_NUM(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define CHANNEL_SWITCH_OFFLOAD_GET_EN_RFE(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define CHANNEL_SWITCH_OFFLOAD_SET_EN_RFE(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#define CHANNEL_SWITCH_OFFLOAD_GET_RFE_TYPE(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 24, 8)
#define CHANNEL_SWITCH_OFFLOAD_SET_RFE_TYPE(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 24, 8, __value)
#define AOAC_RSVD_PAGE3_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 5)
#define AOAC_RSVD_PAGE3_SET_CMD_ID(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 5, __value)
#define AOAC_RSVD_PAGE3_GET_CLASS(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 5, 3)
#define AOAC_RSVD_PAGE3_SET_CLASS(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 5, 3, __value)
#define AOAC_RSVD_PAGE3_GET_LOC_NLO_INFO(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define AOAC_RSVD_PAGE3_SET_LOC_NLO_INFO(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define AOAC_RSVD_PAGE3_GET_LOC_AOAC_REPORT(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 8)
#define AOAC_RSVD_PAGE3_SET_LOC_AOAC_REPORT(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 8, __value)
#endif

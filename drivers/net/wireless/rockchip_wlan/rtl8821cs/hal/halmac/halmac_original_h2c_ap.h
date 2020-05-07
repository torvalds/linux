/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _HAL_ORIGINALH2CFORMAT_H2C_C2H_AP_H_
#define _HAL_ORIGINALH2CFORMAT_H2C_C2H_AP_H_
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
#define CMD_ID_SINGLE_CHANNELSWITCH 0X1C
#define CMD_ID_SINGLE_CHANNELSWITCH_V2 0X1D
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
#define CMD_ID_REQ_TXRPT_ACQ 0X12
#define CMD_ID_WWLAN 0X00
#define CMD_ID_REMOTE_WAKE_CTRL 0X01
#define CMD_ID_AOAC_GLOBAL_INFO 0X02
#define CMD_ID_AOAC_RSVD_PAGE 0X03
#define CMD_ID_AOAC_RSVD_PAGE2 0X04
#define CMD_ID_D0_SCAN_OFFLOAD_INFO 0X05
#define CMD_ID_CHANNEL_SWITCH_OFFLOAD 0X07
#define CMD_ID_AOAC_RSVD_PAGE3 0X08
#define CMD_ID_DBG_MSG_CTRL 0X1E
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
#define CLASS_SINGLE_CHANNELSWITCH 0X0
#define CLASS_SINGLE_CHANNELSWITCH_V2 0X0
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
#define CLASS_REQ_TXRPT_ACQ 0X02
#define CLASS_WWLAN 0X4
#define CLASS_REMOTE_WAKE_CTRL 0X4
#define CLASS_AOAC_GLOBAL_INFO 0X04
#define CLASS_AOAC_RSVD_PAGE 0X04
#define CLASS_AOAC_RSVD_PAGE2 0X04
#define CLASS_D0_SCAN_OFFLOAD_INFO 0X04
#define CLASS_CHANNEL_SWITCH_OFFLOAD 0X04
#define CLASS_AOAC_RSVD_PAGE3 0X04
#define CLASS_DBG_MSG_CTRL 0X07
#define ORIGINAL_H2C_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define ORIGINAL_H2C_SET_CMD_ID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define ORIGINAL_H2C_SET_CMD_ID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define ORIGINAL_H2C_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define ORIGINAL_H2C_SET_CLASS(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define ORIGINAL_H2C_SET_CLASS_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define H2C2H_LB_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define H2C2H_LB_SET_CMD_ID(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define H2C2H_LB_SET_CMD_ID_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define H2C2H_LB_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define H2C2H_LB_SET_CLASS(h2c_pkt, value)                                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define H2C2H_LB_SET_CLASS_NO_CLR(h2c_pkt, value)                              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define H2C2H_LB_GET_SEQ(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define H2C2H_LB_SET_SEQ(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define H2C2H_LB_SET_SEQ_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define H2C2H_LB_GET_PAYLOAD1(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 16)
#define H2C2H_LB_SET_PAYLOAD1(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 16, value)
#define H2C2H_LB_SET_PAYLOAD1_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 16, value)
#define H2C2H_LB_GET_PAYLOAD2(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 0, 32)
#define H2C2H_LB_SET_PAYLOAD2(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 32, value)
#define H2C2H_LB_SET_PAYLOAD2_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 32, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_CMD_ID(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define D0_SCAN_OFFLOAD_CTRL_SET_CMD_ID(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_CMD_ID_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_CLASS(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define D0_SCAN_OFFLOAD_CTRL_SET_CLASS(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_CLASS_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_D0_SCAN_FUN_EN(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_D0_SCAN_FUN_EN(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_D0_SCAN_FUN_EN_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_RTD3FUN_EN(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_RTD3FUN_EN(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_RTD3FUN_EN_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_U3_SCAN_FUN_EN(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_U3_SCAN_FUN_EN(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_U3_SCAN_FUN_EN_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_NLO_FUN_EN(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 11, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_NLO_FUN_EN(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 11, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_NLO_FUN_EN_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 11, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_IPS_DEPENDENT(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X00, 12, 1)
#define D0_SCAN_OFFLOAD_CTRL_SET_IPS_DEPENDENT(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 12, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_IPS_DEPENDENT_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 12, 1, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_LOC_PROBE_PACKET(h2c_pkt)                     \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 17)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_PROBE_PACKET(h2c_pkt, value)              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 17, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_PROBE_PACKET_NO_CLR(h2c_pkt, value)       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 17, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_LOC_SCAN_INFO(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_SCAN_INFO(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_SCAN_INFO_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define D0_SCAN_OFFLOAD_CTRL_GET_LOC_SSID_INFO(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_SSID_INFO(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define D0_SCAN_OFFLOAD_CTRL_SET_LOC_SSID_INFO_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define RSVD_PAGE_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define RSVD_PAGE_SET_CMD_ID(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define RSVD_PAGE_SET_CMD_ID_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define RSVD_PAGE_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define RSVD_PAGE_SET_CLASS(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define RSVD_PAGE_SET_CLASS_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define RSVD_PAGE_GET_LOC_PROBE_RSP(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define RSVD_PAGE_SET_LOC_PROBE_RSP(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define RSVD_PAGE_SET_LOC_PROBE_RSP_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define RSVD_PAGE_GET_LOC_PS_POLL(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define RSVD_PAGE_SET_LOC_PS_POLL(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define RSVD_PAGE_SET_LOC_PS_POLL_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define RSVD_PAGE_GET_LOC_NULL_DATA(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define RSVD_PAGE_SET_LOC_NULL_DATA(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define RSVD_PAGE_SET_LOC_NULL_DATA_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define RSVD_PAGE_GET_LOC_QOS_NULL(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define RSVD_PAGE_SET_LOC_QOS_NULL(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define RSVD_PAGE_SET_LOC_QOS_NULL_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define RSVD_PAGE_GET_LOC_BT_QOS_NULL(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define RSVD_PAGE_SET_LOC_BT_QOS_NULL(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define RSVD_PAGE_SET_LOC_BT_QOS_NULL_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define RSVD_PAGE_GET_LOC_CTS2SELF(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define RSVD_PAGE_SET_LOC_CTS2SELF(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define RSVD_PAGE_SET_LOC_CTS2SELF_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define RSVD_PAGE_GET_LOC_LTECOEX_QOSNULL(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X04, 24, 8)
#define RSVD_PAGE_SET_LOC_LTECOEX_QOSNULL(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 24, 8, value)
#define RSVD_PAGE_SET_LOC_LTECOEX_QOSNULL_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 24, 8, value)
#define MEDIA_STATUS_RPT_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define MEDIA_STATUS_RPT_SET_CMD_ID(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define MEDIA_STATUS_RPT_SET_CMD_ID_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define MEDIA_STATUS_RPT_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define MEDIA_STATUS_RPT_SET_CLASS(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define MEDIA_STATUS_RPT_SET_CLASS_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define MEDIA_STATUS_RPT_GET_OP_MODE(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define MEDIA_STATUS_RPT_SET_OP_MODE(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define MEDIA_STATUS_RPT_SET_OP_MODE_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define MEDIA_STATUS_RPT_GET_MACID_IN(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define MEDIA_STATUS_RPT_SET_MACID_IN(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define MEDIA_STATUS_RPT_SET_MACID_IN_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define MEDIA_STATUS_RPT_GET_MACID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define MEDIA_STATUS_RPT_SET_MACID(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define MEDIA_STATUS_RPT_SET_MACID_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define MEDIA_STATUS_RPT_GET_MACID_END(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define MEDIA_STATUS_RPT_SET_MACID_END(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define MEDIA_STATUS_RPT_SET_MACID_END_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define KEEP_ALIVE_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define KEEP_ALIVE_SET_CMD_ID(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define KEEP_ALIVE_SET_CMD_ID_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define KEEP_ALIVE_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define KEEP_ALIVE_SET_CLASS(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define KEEP_ALIVE_SET_CLASS_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define KEEP_ALIVE_GET_ENABLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define KEEP_ALIVE_SET_ENABLE(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define KEEP_ALIVE_SET_ENABLE_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define KEEP_ALIVE_GET_ADOPT_USER_SETTING(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define KEEP_ALIVE_SET_ADOPT_USER_SETTING(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define KEEP_ALIVE_SET_ADOPT_USER_SETTING_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define KEEP_ALIVE_GET_PKT_TYPE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define KEEP_ALIVE_SET_PKT_TYPE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define KEEP_ALIVE_SET_PKT_TYPE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define KEEP_ALIVE_GET_KEEP_ALIVE_CHECK_PERIOD(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define KEEP_ALIVE_SET_KEEP_ALIVE_CHECK_PERIOD(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define KEEP_ALIVE_SET_KEEP_ALIVE_CHECK_PERIOD_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define DISCONNECT_DECISION_GET_CMD_ID(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define DISCONNECT_DECISION_SET_CMD_ID(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define DISCONNECT_DECISION_SET_CMD_ID_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define DISCONNECT_DECISION_GET_CLASS(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define DISCONNECT_DECISION_SET_CLASS(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define DISCONNECT_DECISION_SET_CLASS_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define DISCONNECT_DECISION_GET_ENABLE(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define DISCONNECT_DECISION_SET_ENABLE(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define DISCONNECT_DECISION_SET_ENABLE_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define DISCONNECT_DECISION_GET_ADOPT_USER_SETTING(h2c_pkt)                    \
	GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define DISCONNECT_DECISION_SET_ADOPT_USER_SETTING(h2c_pkt, value)             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define DISCONNECT_DECISION_SET_ADOPT_USER_SETTING_NO_CLR(h2c_pkt, value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define DISCONNECT_DECISION_GET_TRY_OK_BCN_FAIL_COUNT_EN(h2c_pkt)              \
	GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define DISCONNECT_DECISION_SET_TRY_OK_BCN_FAIL_COUNT_EN(h2c_pkt, value)       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define DISCONNECT_DECISION_SET_TRY_OK_BCN_FAIL_COUNT_EN_NO_CLR(h2c_pkt,       \
								value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define DISCONNECT_DECISION_GET_DISCONNECT_EN(h2c_pkt)                         \
	GET_H2C_FIELD(h2c_pkt + 0X00, 11, 1)
#define DISCONNECT_DECISION_SET_DISCONNECT_EN(h2c_pkt, value)                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 11, 1, value)
#define DISCONNECT_DECISION_SET_DISCONNECT_EN_NO_CLR(h2c_pkt, value)           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 11, 1, value)
#define DISCONNECT_DECISION_GET_DISCON_DECISION_CHECK_PERIOD(h2c_pkt)          \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define DISCONNECT_DECISION_SET_DISCON_DECISION_CHECK_PERIOD(h2c_pkt, value)   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define DISCONNECT_DECISION_SET_DISCON_DECISION_CHECK_PERIOD_NO_CLR(h2c_pkt,   \
								    value)     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define DISCONNECT_DECISION_GET_TRY_PKT_NUM(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define DISCONNECT_DECISION_SET_TRY_PKT_NUM(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define DISCONNECT_DECISION_SET_TRY_PKT_NUM_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define DISCONNECT_DECISION_GET_TRY_OK_BCN_FAIL_COUNT_LIMIT(h2c_pkt)           \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define DISCONNECT_DECISION_SET_TRY_OK_BCN_FAIL_COUNT_LIMIT(h2c_pkt, value)    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define DISCONNECT_DECISION_SET_TRY_OK_BCN_FAIL_COUNT_LIMIT_NO_CLR(h2c_pkt,    \
								   value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AP_OFFLOAD_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define AP_OFFLOAD_SET_CMD_ID(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AP_OFFLOAD_SET_CMD_ID_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AP_OFFLOAD_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define AP_OFFLOAD_SET_CLASS(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AP_OFFLOAD_SET_CLASS_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AP_OFFLOAD_GET_ON(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define AP_OFFLOAD_SET_ON(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define AP_OFFLOAD_SET_ON_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define AP_OFFLOAD_GET_CFG_MIFI_PLATFORM(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define AP_OFFLOAD_SET_CFG_MIFI_PLATFORM(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define AP_OFFLOAD_SET_CFG_MIFI_PLATFORM_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define AP_OFFLOAD_GET_LINKED(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define AP_OFFLOAD_SET_LINKED(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define AP_OFFLOAD_SET_LINKED_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define AP_OFFLOAD_GET_EN_AUTO_WAKE(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 11, 1)
#define AP_OFFLOAD_SET_EN_AUTO_WAKE(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 11, 1, value)
#define AP_OFFLOAD_SET_EN_AUTO_WAKE_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 11, 1, value)
#define AP_OFFLOAD_GET_WAKE_FLAG(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 12, 1)
#define AP_OFFLOAD_SET_WAKE_FLAG(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 12, 1, value)
#define AP_OFFLOAD_SET_WAKE_FLAG_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 12, 1, value)
#define AP_OFFLOAD_GET_HIDDEN_ROOT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 1)
#define AP_OFFLOAD_SET_HIDDEN_ROOT(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 1, value)
#define AP_OFFLOAD_SET_HIDDEN_ROOT_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 1, value)
#define AP_OFFLOAD_GET_HIDDEN_VAP1(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 17, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP1(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 17, 1, value)
#define AP_OFFLOAD_SET_HIDDEN_VAP1_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 17, 1, value)
#define AP_OFFLOAD_GET_HIDDEN_VAP2(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 18, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP2(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 18, 1, value)
#define AP_OFFLOAD_SET_HIDDEN_VAP2_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 18, 1, value)
#define AP_OFFLOAD_GET_HIDDEN_VAP3(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 19, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP3(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 19, 1, value)
#define AP_OFFLOAD_SET_HIDDEN_VAP3_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 19, 1, value)
#define AP_OFFLOAD_GET_HIDDEN_VAP4(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 20, 1)
#define AP_OFFLOAD_SET_HIDDEN_VAP4(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 20, 1, value)
#define AP_OFFLOAD_SET_HIDDEN_VAP4_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 20, 1, value)
#define AP_OFFLOAD_GET_DENYANY_ROOT(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 1)
#define AP_OFFLOAD_SET_DENYANY_ROOT(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 1, value)
#define AP_OFFLOAD_SET_DENYANY_ROOT_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 1, value)
#define AP_OFFLOAD_GET_DENYANY_VAP1(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 25, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP1(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 25, 1, value)
#define AP_OFFLOAD_SET_DENYANY_VAP1_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 25, 1, value)
#define AP_OFFLOAD_GET_DENYANY_VAP2(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 26, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP2(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 26, 1, value)
#define AP_OFFLOAD_SET_DENYANY_VAP2_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 26, 1, value)
#define AP_OFFLOAD_GET_DENYANY_VAP3(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 27, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP3(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 27, 1, value)
#define AP_OFFLOAD_SET_DENYANY_VAP3_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 27, 1, value)
#define AP_OFFLOAD_GET_DENYANY_VAP4(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 28, 1)
#define AP_OFFLOAD_SET_DENYANY_VAP4(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 28, 1, value)
#define AP_OFFLOAD_SET_DENYANY_VAP4_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 28, 1, value)
#define AP_OFFLOAD_GET_WAIT_TBTT_CNT(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define AP_OFFLOAD_SET_WAIT_TBTT_CNT(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AP_OFFLOAD_SET_WAIT_TBTT_CNT_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AP_OFFLOAD_GET_WAKE_TIMEOUT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define AP_OFFLOAD_SET_WAKE_TIMEOUT(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define AP_OFFLOAD_SET_WAKE_TIMEOUT_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define AP_OFFLOAD_GET_LEN_IV_PAIR(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define AP_OFFLOAD_SET_LEN_IV_PAIR(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define AP_OFFLOAD_SET_LEN_IV_PAIR_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define AP_OFFLOAD_GET_LEN_IV_GRP(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 24, 8)
#define AP_OFFLOAD_SET_LEN_IV_GRP(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 24, 8, value)
#define AP_OFFLOAD_SET_LEN_IV_GRP_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 24, 8, value)
#define BCN_RSVDPAGE_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define BCN_RSVDPAGE_SET_CMD_ID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define BCN_RSVDPAGE_SET_CMD_ID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define BCN_RSVDPAGE_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define BCN_RSVDPAGE_SET_CLASS(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define BCN_RSVDPAGE_SET_CLASS_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define BCN_RSVDPAGE_GET_LOC_ROOT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define BCN_RSVDPAGE_SET_LOC_ROOT(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define BCN_RSVDPAGE_SET_LOC_ROOT_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define BCN_RSVDPAGE_GET_LOC_VAP1(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP1(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define BCN_RSVDPAGE_SET_LOC_VAP1_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define BCN_RSVDPAGE_GET_LOC_VAP2(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP2(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define BCN_RSVDPAGE_SET_LOC_VAP2_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define BCN_RSVDPAGE_GET_LOC_VAP3(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP3(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define BCN_RSVDPAGE_SET_LOC_VAP3_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define BCN_RSVDPAGE_GET_LOC_VAP4(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define BCN_RSVDPAGE_SET_LOC_VAP4(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define BCN_RSVDPAGE_SET_LOC_VAP4_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define PROBE_RSP_RSVDPAGE_GET_CMD_ID(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define PROBE_RSP_RSVDPAGE_SET_CMD_ID(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PROBE_RSP_RSVDPAGE_SET_CMD_ID_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PROBE_RSP_RSVDPAGE_GET_CLASS(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define PROBE_RSP_RSVDPAGE_SET_CLASS(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PROBE_RSP_RSVDPAGE_SET_CLASS_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_ROOT(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_ROOT(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define PROBE_RSP_RSVDPAGE_SET_LOC_ROOT_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP1(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP1(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP1_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP2(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP2(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP2_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP3(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP3(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP3_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define PROBE_RSP_RSVDPAGE_GET_LOC_VAP4(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP4(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define PROBE_RSP_RSVDPAGE_SET_LOC_VAP4_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define SINGLE_CHANNELSWITCH_GET_CMD_ID(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define SINGLE_CHANNELSWITCH_SET_CMD_ID(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SINGLE_CHANNELSWITCH_SET_CMD_ID_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SINGLE_CHANNELSWITCH_GET_CLASS(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define SINGLE_CHANNELSWITCH_SET_CLASS(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SINGLE_CHANNELSWITCH_SET_CLASS_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SINGLE_CHANNELSWITCH_GET_CHANNEL_NUM(h2c_pkt)                          \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define SINGLE_CHANNELSWITCH_SET_CHANNEL_NUM(h2c_pkt, value)                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define SINGLE_CHANNELSWITCH_SET_CHANNEL_NUM_NO_CLR(h2c_pkt, value)            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define SINGLE_CHANNELSWITCH_GET_BW(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 2)
#define SINGLE_CHANNELSWITCH_SET_BW(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 2, value)
#define SINGLE_CHANNELSWITCH_SET_BW_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 2, value)
#define SINGLE_CHANNELSWITCH_GET_BW40SC(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 18, 3)
#define SINGLE_CHANNELSWITCH_SET_BW40SC(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 18, 3, value)
#define SINGLE_CHANNELSWITCH_SET_BW40SC_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 18, 3, value)
#define SINGLE_CHANNELSWITCH_GET_BW80SC(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 21, 3)
#define SINGLE_CHANNELSWITCH_SET_BW80SC(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 21, 3, value)
#define SINGLE_CHANNELSWITCH_SET_BW80SC_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 21, 3, value)
#define SINGLE_CHANNELSWITCH_GET_RFE_TYPE(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 4)
#define SINGLE_CHANNELSWITCH_SET_RFE_TYPE(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 4, value)
#define SINGLE_CHANNELSWITCH_SET_RFE_TYPE_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 4, value)
#define SINGLE_CHANNELSWITCH_V2_GET_CMD_ID(h2c_pkt)                            \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define SINGLE_CHANNELSWITCH_V2_SET_CMD_ID(h2c_pkt, value)                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SINGLE_CHANNELSWITCH_V2_SET_CMD_ID_NO_CLR(h2c_pkt, value)              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SINGLE_CHANNELSWITCH_V2_GET_CLASS(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define SINGLE_CHANNELSWITCH_V2_SET_CLASS(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SINGLE_CHANNELSWITCH_V2_SET_CLASS_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SINGLE_CHANNELSWITCH_V2_GET_CENTRAL_CH(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define SINGLE_CHANNELSWITCH_V2_SET_CENTRAL_CH(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define SINGLE_CHANNELSWITCH_V2_SET_CENTRAL_CH_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define SINGLE_CHANNELSWITCH_V2_GET_PRIMARY_CH_IDX(h2c_pkt)                    \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 4)
#define SINGLE_CHANNELSWITCH_V2_SET_PRIMARY_CH_IDX(h2c_pkt, value)             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 4, value)
#define SINGLE_CHANNELSWITCH_V2_SET_PRIMARY_CH_IDX_NO_CLR(h2c_pkt, value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 4, value)
#define SINGLE_CHANNELSWITCH_V2_GET_BW(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 20, 4)
#define SINGLE_CHANNELSWITCH_V2_SET_BW(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 20, 4, value)
#define SINGLE_CHANNELSWITCH_V2_SET_BW_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 20, 4, value)
#define SET_PWR_MODE_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define SET_PWR_MODE_SET_CMD_ID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SET_PWR_MODE_SET_CMD_ID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SET_PWR_MODE_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define SET_PWR_MODE_SET_CLASS(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SET_PWR_MODE_SET_CLASS_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SET_PWR_MODE_GET_MODE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 7)
#define SET_PWR_MODE_SET_MODE(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 7, value)
#define SET_PWR_MODE_SET_MODE_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 7, value)
#define SET_PWR_MODE_GET_CLK_REQUEST(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 15, 1)
#define SET_PWR_MODE_SET_CLK_REQUEST(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 15, 1, value)
#define SET_PWR_MODE_SET_CLK_REQUEST_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 15, 1, value)
#define SET_PWR_MODE_GET_RLBM(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 4)
#define SET_PWR_MODE_SET_RLBM(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 4, value)
#define SET_PWR_MODE_SET_RLBM_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 4, value)
#define SET_PWR_MODE_GET_SMART_PS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 20, 4)
#define SET_PWR_MODE_SET_SMART_PS(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 20, 4, value)
#define SET_PWR_MODE_SET_SMART_PS_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 20, 4, value)
#define SET_PWR_MODE_GET_AWAKE_INTERVAL(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define SET_PWR_MODE_SET_AWAKE_INTERVAL(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define SET_PWR_MODE_SET_AWAKE_INTERVAL_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define SET_PWR_MODE_GET_B_ALL_QUEUE_UAPSD(h2c_pkt)                            \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 1)
#define SET_PWR_MODE_SET_B_ALL_QUEUE_UAPSD(h2c_pkt, value)                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 1, value)
#define SET_PWR_MODE_SET_B_ALL_QUEUE_UAPSD_NO_CLR(h2c_pkt, value)              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 1, value)
#define SET_PWR_MODE_GET_BCN_EARLY_RPT(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X04, 2, 1)
#define SET_PWR_MODE_SET_BCN_EARLY_RPT(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 2, 1, value)
#define SET_PWR_MODE_SET_BCN_EARLY_RPT_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 2, 1, value)
#define SET_PWR_MODE_GET_PORT_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 5, 3)
#define SET_PWR_MODE_SET_PORT_ID(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 5, 3, value)
#define SET_PWR_MODE_SET_PORT_ID_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 5, 3, value)
#define SET_PWR_MODE_GET_PWR_STATE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define SET_PWR_MODE_SET_PWR_STATE(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define SET_PWR_MODE_SET_PWR_STATE_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define SET_PWR_MODE_GET_RSVD_NOUSED(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define SET_PWR_MODE_SET_RSVD_NOUSED(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define SET_PWR_MODE_SET_RSVD_NOUSED_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define SET_PWR_MODE_GET_BCN_RECEIVING_TIME(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X04, 24, 5)
#define SET_PWR_MODE_SET_BCN_RECEIVING_TIME(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 24, 5, value)
#define SET_PWR_MODE_SET_BCN_RECEIVING_TIME_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 24, 5, value)
#define SET_PWR_MODE_GET_BCN_LISTEN_INTERVAL(h2c_pkt)                          \
	GET_H2C_FIELD(h2c_pkt + 0X04, 29, 2)
#define SET_PWR_MODE_SET_BCN_LISTEN_INTERVAL(h2c_pkt, value)                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 29, 2, value)
#define SET_PWR_MODE_SET_BCN_LISTEN_INTERVAL_NO_CLR(h2c_pkt, value)            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 29, 2, value)
#define SET_PWR_MODE_GET_ADOPT_BCN_RECEIVING_TIME(h2c_pkt)                     \
	GET_H2C_FIELD(h2c_pkt + 0X04, 31, 1)
#define SET_PWR_MODE_SET_ADOPT_BCN_RECEIVING_TIME(h2c_pkt, value)              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 31, 1, value)
#define SET_PWR_MODE_SET_ADOPT_BCN_RECEIVING_TIME_NO_CLR(h2c_pkt, value)       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 31, 1, value)
#define PS_TUNING_PARA_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define PS_TUNING_PARA_SET_CMD_ID(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_TUNING_PARA_SET_CMD_ID_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_TUNING_PARA_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define PS_TUNING_PARA_SET_CLASS(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_TUNING_PARA_SET_CLASS_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_TUNING_PARA_GET_BCN_TO_LIMIT(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 7)
#define PS_TUNING_PARA_SET_BCN_TO_LIMIT(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 7, value)
#define PS_TUNING_PARA_SET_BCN_TO_LIMIT_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 7, value)
#define PS_TUNING_PARA_GET_DTIM_TIME_OUT(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X00, 15, 1)
#define PS_TUNING_PARA_SET_DTIM_TIME_OUT(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 15, 1, value)
#define PS_TUNING_PARA_SET_DTIM_TIME_OUT_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 15, 1, value)
#define PS_TUNING_PARA_GET_PS_TIME_OUT(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 4)
#define PS_TUNING_PARA_SET_PS_TIME_OUT(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 4, value)
#define PS_TUNING_PARA_SET_PS_TIME_OUT_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 4, value)
#define PS_TUNING_PARA_GET_ADOPT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define PS_TUNING_PARA_SET_ADOPT(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define PS_TUNING_PARA_SET_ADOPT_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define PS_TUNING_PARA_II_GET_CMD_ID(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define PS_TUNING_PARA_II_SET_CMD_ID(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_TUNING_PARA_II_SET_CMD_ID_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_TUNING_PARA_II_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define PS_TUNING_PARA_II_SET_CLASS(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_TUNING_PARA_II_SET_CLASS_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_TUNING_PARA_II_GET_BCN_TO_PERIOD(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 7)
#define PS_TUNING_PARA_II_SET_BCN_TO_PERIOD(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 7, value)
#define PS_TUNING_PARA_II_SET_BCN_TO_PERIOD_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 7, value)
#define PS_TUNING_PARA_II_GET_ADOPT(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 15, 1)
#define PS_TUNING_PARA_II_SET_ADOPT(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 15, 1, value)
#define PS_TUNING_PARA_II_SET_ADOPT_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 15, 1, value)
#define PS_TUNING_PARA_II_GET_DRV_EARLY_IVL(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define PS_TUNING_PARA_II_SET_DRV_EARLY_IVL(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define PS_TUNING_PARA_II_SET_DRV_EARLY_IVL_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define PS_LPS_PARA_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define PS_LPS_PARA_SET_CMD_ID(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_LPS_PARA_SET_CMD_ID_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_LPS_PARA_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define PS_LPS_PARA_SET_CLASS(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_LPS_PARA_SET_CLASS_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_LPS_PARA_GET_LPS_CONTROL(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define PS_LPS_PARA_SET_LPS_CONTROL(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define PS_LPS_PARA_SET_LPS_CONTROL_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define P2P_PS_OFFLOAD_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define P2P_PS_OFFLOAD_SET_CMD_ID(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define P2P_PS_OFFLOAD_SET_CMD_ID_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define P2P_PS_OFFLOAD_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define P2P_PS_OFFLOAD_SET_CLASS(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define P2P_PS_OFFLOAD_SET_CLASS_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define P2P_PS_OFFLOAD_GET_OFFLOAD_EN(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define P2P_PS_OFFLOAD_SET_OFFLOAD_EN(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define P2P_PS_OFFLOAD_SET_OFFLOAD_EN_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define P2P_PS_OFFLOAD_GET_ROLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define P2P_PS_OFFLOAD_SET_ROLE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define P2P_PS_OFFLOAD_SET_ROLE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define P2P_PS_OFFLOAD_GET_CTWINDOW_EN(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define P2P_PS_OFFLOAD_SET_CTWINDOW_EN(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define P2P_PS_OFFLOAD_SET_CTWINDOW_EN_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define P2P_PS_OFFLOAD_GET_NOA0_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 11, 1)
#define P2P_PS_OFFLOAD_SET_NOA0_EN(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 11, 1, value)
#define P2P_PS_OFFLOAD_SET_NOA0_EN_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 11, 1, value)
#define P2P_PS_OFFLOAD_GET_NOA1_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 12, 1)
#define P2P_PS_OFFLOAD_SET_NOA1_EN(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 12, 1, value)
#define P2P_PS_OFFLOAD_SET_NOA1_EN_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 12, 1, value)
#define P2P_PS_OFFLOAD_GET_ALL_STA_SLEEP(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X00, 13, 1)
#define P2P_PS_OFFLOAD_SET_ALL_STA_SLEEP(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 13, 1, value)
#define P2P_PS_OFFLOAD_SET_ALL_STA_SLEEP_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 13, 1, value)
#define P2P_PS_OFFLOAD_GET_DISCOVERY(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 14, 1)
#define P2P_PS_OFFLOAD_SET_DISCOVERY(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 14, 1, value)
#define P2P_PS_OFFLOAD_SET_DISCOVERY_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 14, 1, value)
#define PS_SCAN_EN_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define PS_SCAN_EN_SET_CMD_ID(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_SCAN_EN_SET_CMD_ID_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define PS_SCAN_EN_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define PS_SCAN_EN_SET_CLASS(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_SCAN_EN_SET_CLASS_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define PS_SCAN_EN_GET_ENABLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define PS_SCAN_EN_SET_ENABLE(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define PS_SCAN_EN_SET_ENABLE_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define SAP_PS_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define SAP_PS_SET_CMD_ID(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SAP_PS_SET_CMD_ID_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define SAP_PS_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define SAP_PS_SET_CLASS(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SAP_PS_SET_CLASS_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define SAP_PS_GET_ENABLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define SAP_PS_SET_ENABLE(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define SAP_PS_SET_ENABLE_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define SAP_PS_GET_EN_PS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define SAP_PS_SET_EN_PS(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define SAP_PS_SET_EN_PS_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define SAP_PS_GET_EN_LP_RX(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define SAP_PS_SET_EN_LP_RX(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define SAP_PS_SET_EN_LP_RX_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define SAP_PS_GET_MANUAL_32K(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 11, 1)
#define SAP_PS_SET_MANUAL_32K(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 11, 1, value)
#define SAP_PS_SET_MANUAL_32K_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 11, 1, value)
#define SAP_PS_GET_DURATION(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define SAP_PS_SET_DURATION(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define SAP_PS_SET_DURATION_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define INACTIVE_PS_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define INACTIVE_PS_SET_CMD_ID(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define INACTIVE_PS_SET_CMD_ID_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define INACTIVE_PS_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define INACTIVE_PS_SET_CLASS(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define INACTIVE_PS_SET_CLASS_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define INACTIVE_PS_GET_ENABLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define INACTIVE_PS_SET_ENABLE(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define INACTIVE_PS_SET_ENABLE_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define INACTIVE_PS_GET_IGNORE_PS_CONDITION(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define INACTIVE_PS_SET_IGNORE_PS_CONDITION(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define INACTIVE_PS_SET_IGNORE_PS_CONDITION_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define INACTIVE_PS_GET_FREQUENCY(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define INACTIVE_PS_SET_FREQUENCY(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define INACTIVE_PS_SET_FREQUENCY_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define INACTIVE_PS_GET_DURATION(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define INACTIVE_PS_SET_DURATION(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define INACTIVE_PS_SET_DURATION_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define MACID_CFG_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define MACID_CFG_SET_CMD_ID(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define MACID_CFG_SET_CMD_ID_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define MACID_CFG_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define MACID_CFG_SET_CLASS(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define MACID_CFG_SET_CLASS_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define MACID_CFG_GET_MAC_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define MACID_CFG_SET_MAC_ID(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define MACID_CFG_SET_MAC_ID_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define MACID_CFG_GET_RATE_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 5)
#define MACID_CFG_SET_RATE_ID(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 5, value)
#define MACID_CFG_SET_RATE_ID_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 5, value)
#define MACID_CFG_GET_INIT_RATE_LV(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 21, 2)
#define MACID_CFG_SET_INIT_RATE_LV(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 21, 2, value)
#define MACID_CFG_SET_INIT_RATE_LV_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 21, 2, value)
#define MACID_CFG_GET_SGI(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 23, 1)
#define MACID_CFG_SET_SGI(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 23, 1, value)
#define MACID_CFG_SET_SGI_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 23, 1, value)
#define MACID_CFG_GET_BW(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 2)
#define MACID_CFG_SET_BW(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 2, value)
#define MACID_CFG_SET_BW_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 2, value)
#define MACID_CFG_GET_LDPC_CAP(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 26, 1)
#define MACID_CFG_SET_LDPC_CAP(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 26, 1, value)
#define MACID_CFG_SET_LDPC_CAP_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 26, 1, value)
#define MACID_CFG_GET_NO_UPDATE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 27, 1)
#define MACID_CFG_SET_NO_UPDATE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 27, 1, value)
#define MACID_CFG_SET_NO_UPDATE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 27, 1, value)
#define MACID_CFG_GET_WHT_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 28, 2)
#define MACID_CFG_SET_WHT_EN(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 28, 2, value)
#define MACID_CFG_SET_WHT_EN_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 28, 2, value)
#define MACID_CFG_GET_DISPT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 30, 1)
#define MACID_CFG_SET_DISPT(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 30, 1, value)
#define MACID_CFG_SET_DISPT_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 30, 1, value)
#define MACID_CFG_GET_DISRA(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 31, 1)
#define MACID_CFG_SET_DISRA(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 31, 1, value)
#define MACID_CFG_SET_DISRA_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 31, 1, value)
#define MACID_CFG_GET_RATE_MASK7_0(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define MACID_CFG_SET_RATE_MASK7_0(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define MACID_CFG_SET_RATE_MASK7_0_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define MACID_CFG_GET_RATE_MASK15_8(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define MACID_CFG_SET_RATE_MASK15_8(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define MACID_CFG_SET_RATE_MASK15_8_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define MACID_CFG_GET_RATE_MASK23_16(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define MACID_CFG_SET_RATE_MASK23_16(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define MACID_CFG_SET_RATE_MASK23_16_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define MACID_CFG_GET_RATE_MASK31_24(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X04, 24, 8)
#define MACID_CFG_SET_RATE_MASK31_24(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 24, 8, value)
#define MACID_CFG_SET_RATE_MASK31_24_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 24, 8, value)
#define TXBF_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define TXBF_SET_CMD_ID(h2c_pkt, value)                                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define TXBF_SET_CMD_ID_NO_CLR(h2c_pkt, value)                                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define TXBF_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define TXBF_SET_CLASS(h2c_pkt, value)                                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define TXBF_SET_CLASS_NO_CLR(h2c_pkt, value)                                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define TXBF_GET_NDPA0_HEAD_PAGE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define TXBF_SET_NDPA0_HEAD_PAGE(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define TXBF_SET_NDPA0_HEAD_PAGE_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define TXBF_GET_NDPA1_HEAD_PAGE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define TXBF_SET_NDPA1_HEAD_PAGE(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define TXBF_SET_NDPA1_HEAD_PAGE_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define TXBF_GET_PERIOD_0(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define TXBF_SET_PERIOD_0(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define TXBF_SET_PERIOD_0_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define RSSI_SETTING_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define RSSI_SETTING_SET_CMD_ID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define RSSI_SETTING_SET_CMD_ID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define RSSI_SETTING_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define RSSI_SETTING_SET_CLASS(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define RSSI_SETTING_SET_CLASS_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define RSSI_SETTING_GET_MAC_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define RSSI_SETTING_SET_MAC_ID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define RSSI_SETTING_SET_MAC_ID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define RSSI_SETTING_GET_RSSI(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 7)
#define RSSI_SETTING_SET_RSSI(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 7, value)
#define RSSI_SETTING_SET_RSSI_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 7, value)
#define RSSI_SETTING_GET_RA_INFO(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define RSSI_SETTING_SET_RA_INFO(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define RSSI_SETTING_SET_RA_INFO_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AP_REQ_TXRPT_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define AP_REQ_TXRPT_SET_CMD_ID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AP_REQ_TXRPT_SET_CMD_ID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AP_REQ_TXRPT_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define AP_REQ_TXRPT_SET_CLASS(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AP_REQ_TXRPT_SET_CLASS_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AP_REQ_TXRPT_GET_STA1_MACID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define AP_REQ_TXRPT_SET_STA1_MACID(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AP_REQ_TXRPT_SET_STA1_MACID_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AP_REQ_TXRPT_GET_STA2_MACID(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define AP_REQ_TXRPT_SET_STA2_MACID(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AP_REQ_TXRPT_SET_STA2_MACID_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AP_REQ_TXRPT_GET_RTY_OK_TOTAL(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 1)
#define AP_REQ_TXRPT_SET_RTY_OK_TOTAL(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 1, value)
#define AP_REQ_TXRPT_SET_RTY_OK_TOTAL_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 1, value)
#define AP_REQ_TXRPT_GET_RTY_CNT_MACID(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 25, 1)
#define AP_REQ_TXRPT_SET_RTY_CNT_MACID(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 25, 1, value)
#define AP_REQ_TXRPT_SET_RTY_CNT_MACID_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 25, 1, value)
#define INIT_RATE_COLLECTION_GET_CMD_ID(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define INIT_RATE_COLLECTION_SET_CMD_ID(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define INIT_RATE_COLLECTION_SET_CMD_ID_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define INIT_RATE_COLLECTION_GET_CLASS(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define INIT_RATE_COLLECTION_SET_CLASS(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define INIT_RATE_COLLECTION_SET_CLASS_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define INIT_RATE_COLLECTION_GET_STA1_MACID(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define INIT_RATE_COLLECTION_SET_STA1_MACID(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define INIT_RATE_COLLECTION_SET_STA1_MACID_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define INIT_RATE_COLLECTION_GET_STA2_MACID(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define INIT_RATE_COLLECTION_SET_STA2_MACID(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define INIT_RATE_COLLECTION_SET_STA2_MACID_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define INIT_RATE_COLLECTION_GET_STA3_MACID(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define INIT_RATE_COLLECTION_SET_STA3_MACID(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define INIT_RATE_COLLECTION_SET_STA3_MACID_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define INIT_RATE_COLLECTION_GET_STA4_MACID(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define INIT_RATE_COLLECTION_SET_STA4_MACID(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define INIT_RATE_COLLECTION_SET_STA4_MACID_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define INIT_RATE_COLLECTION_GET_STA5_MACID(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define INIT_RATE_COLLECTION_SET_STA5_MACID(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define INIT_RATE_COLLECTION_SET_STA5_MACID_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define INIT_RATE_COLLECTION_GET_STA6_MACID(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define INIT_RATE_COLLECTION_SET_STA6_MACID(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define INIT_RATE_COLLECTION_SET_STA6_MACID_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define INIT_RATE_COLLECTION_GET_STA7_MACID(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X04, 24, 8)
#define INIT_RATE_COLLECTION_SET_STA7_MACID(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 24, 8, value)
#define INIT_RATE_COLLECTION_SET_STA7_MACID_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 24, 8, value)
#define IQK_OFFLOAD_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define IQK_OFFLOAD_SET_CMD_ID(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define IQK_OFFLOAD_SET_CMD_ID_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define IQK_OFFLOAD_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define IQK_OFFLOAD_SET_CLASS(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define IQK_OFFLOAD_SET_CLASS_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define IQK_OFFLOAD_GET_CHANNEL(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define IQK_OFFLOAD_SET_CHANNEL(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define IQK_OFFLOAD_SET_CHANNEL_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define IQK_OFFLOAD_GET_BWBAND(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define IQK_OFFLOAD_SET_BWBAND(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define IQK_OFFLOAD_SET_BWBAND_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define IQK_OFFLOAD_GET_EXTPALNA(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define IQK_OFFLOAD_SET_EXTPALNA(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define IQK_OFFLOAD_SET_EXTPALNA_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define MACID_CFG_3SS_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define MACID_CFG_3SS_SET_CMD_ID(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define MACID_CFG_3SS_SET_CMD_ID_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define MACID_CFG_3SS_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define MACID_CFG_3SS_SET_CLASS(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define MACID_CFG_3SS_SET_CLASS_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define MACID_CFG_3SS_GET_MACID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define MACID_CFG_3SS_SET_MACID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define MACID_CFG_3SS_SET_MACID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define MACID_CFG_3SS_GET_RATE_MASK_39_32(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define MACID_CFG_3SS_SET_RATE_MASK_39_32(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define MACID_CFG_3SS_SET_RATE_MASK_39_32_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define MACID_CFG_3SS_GET_RATE_MASK_47_40(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define MACID_CFG_3SS_SET_RATE_MASK_47_40(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define MACID_CFG_3SS_SET_RATE_MASK_47_40_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define RA_PARA_ADJUST_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define RA_PARA_ADJUST_SET_CMD_ID(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define RA_PARA_ADJUST_SET_CMD_ID_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define RA_PARA_ADJUST_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define RA_PARA_ADJUST_SET_CLASS(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define RA_PARA_ADJUST_SET_CLASS_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define RA_PARA_ADJUST_GET_MAC_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define RA_PARA_ADJUST_SET_MAC_ID(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define RA_PARA_ADJUST_SET_MAC_ID_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define RA_PARA_ADJUST_GET_PARAMETER_INDEX(h2c_pkt)                            \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define RA_PARA_ADJUST_SET_PARAMETER_INDEX(h2c_pkt, value)                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define RA_PARA_ADJUST_SET_PARAMETER_INDEX_NO_CLR(h2c_pkt, value)              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define RA_PARA_ADJUST_GET_RATE_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define RA_PARA_ADJUST_SET_RATE_ID(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define RA_PARA_ADJUST_SET_RATE_ID_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define RA_PARA_ADJUST_GET_VALUE_BYTE0(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define RA_PARA_ADJUST_SET_VALUE_BYTE0(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define RA_PARA_ADJUST_SET_VALUE_BYTE0_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define RA_PARA_ADJUST_GET_VALUE_BYTE1(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define RA_PARA_ADJUST_SET_VALUE_BYTE1(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define RA_PARA_ADJUST_SET_VALUE_BYTE1_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define RA_PARA_ADJUST_GET_ASK_FW_FOR_FW_PARA(h2c_pkt)                         \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define RA_PARA_ADJUST_SET_ASK_FW_FOR_FW_PARA(h2c_pkt, value)                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define RA_PARA_ADJUST_SET_ASK_FW_FOR_FW_PARA_NO_CLR(h2c_pkt, value)           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define REQ_TXRPT_ACQ_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define REQ_TXRPT_ACQ_SET_CMD_ID(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define REQ_TXRPT_ACQ_SET_CMD_ID_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define REQ_TXRPT_ACQ_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define REQ_TXRPT_ACQ_SET_CLASS(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define REQ_TXRPT_ACQ_SET_CLASS_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define REQ_TXRPT_ACQ_GET_STA1_MACID(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define REQ_TXRPT_ACQ_SET_STA1_MACID(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define REQ_TXRPT_ACQ_SET_STA1_MACID_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define REQ_TXRPT_ACQ_GET_PASS_DROP_SEL(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define REQ_TXRPT_ACQ_SET_PASS_DROP_SEL(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define REQ_TXRPT_ACQ_SET_PASS_DROP_SEL_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define WWLAN_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define WWLAN_SET_CMD_ID(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define WWLAN_SET_CMD_ID_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define WWLAN_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define WWLAN_SET_CLASS(h2c_pkt, value)                                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define WWLAN_SET_CLASS_NO_CLR(h2c_pkt, value)                                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define WWLAN_GET_FUNC_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define WWLAN_SET_FUNC_EN(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define WWLAN_SET_FUNC_EN_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define WWLAN_GET_PATTERM_MAT_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define WWLAN_SET_PATTERM_MAT_EN(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define WWLAN_SET_PATTERM_MAT_EN_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define WWLAN_GET_MAGIC_PKT_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define WWLAN_SET_MAGIC_PKT_EN(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define WWLAN_SET_MAGIC_PKT_EN_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define WWLAN_GET_UNICAST_WAKEUP_EN(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 11, 1)
#define WWLAN_SET_UNICAST_WAKEUP_EN(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 11, 1, value)
#define WWLAN_SET_UNICAST_WAKEUP_EN_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 11, 1, value)
#define WWLAN_GET_ALL_PKT_DROP(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 12, 1)
#define WWLAN_SET_ALL_PKT_DROP(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 12, 1, value)
#define WWLAN_SET_ALL_PKT_DROP_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 12, 1, value)
#define WWLAN_GET_GPIO_ACTIVE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 13, 1)
#define WWLAN_SET_GPIO_ACTIVE(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 13, 1, value)
#define WWLAN_SET_GPIO_ACTIVE_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 13, 1, value)
#define WWLAN_GET_REKEY_WAKEUP_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 14, 1)
#define WWLAN_SET_REKEY_WAKEUP_EN(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 14, 1, value)
#define WWLAN_SET_REKEY_WAKEUP_EN_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 14, 1, value)
#define WWLAN_GET_DEAUTH_WAKEUP_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 15, 1)
#define WWLAN_SET_DEAUTH_WAKEUP_EN(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 15, 1, value)
#define WWLAN_SET_DEAUTH_WAKEUP_EN_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 15, 1, value)
#define WWLAN_GET_GPIO_NUM(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 16, 7)
#define WWLAN_SET_GPIO_NUM(h2c_pkt, value)                                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 7, value)
#define WWLAN_SET_GPIO_NUM_NO_CLR(h2c_pkt, value)                              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 7, value)
#define WWLAN_GET_DATAPIN_WAKEUP_EN(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 23, 1)
#define WWLAN_SET_DATAPIN_WAKEUP_EN(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 23, 1, value)
#define WWLAN_SET_DATAPIN_WAKEUP_EN_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 23, 1, value)
#define WWLAN_GET_GPIO_DURATION(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define WWLAN_SET_GPIO_DURATION(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define WWLAN_SET_GPIO_DURATION_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define WWLAN_GET_GPIO_PLUS_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 0, 1)
#define WWLAN_SET_GPIO_PLUS_EN(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 1, value)
#define WWLAN_SET_GPIO_PLUS_EN_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 1, value)
#define WWLAN_GET_GPIO_PULSE_COUNT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 1, 7)
#define WWLAN_SET_GPIO_PULSE_COUNT(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 1, 7, value)
#define WWLAN_SET_GPIO_PULSE_COUNT_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 1, 7, value)
#define WWLAN_GET_DISABLE_UPHY(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 8, 1)
#define WWLAN_SET_DISABLE_UPHY(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 1, value)
#define WWLAN_SET_DISABLE_UPHY_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 1, value)
#define WWLAN_GET_HST2DEV_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 9, 1)
#define WWLAN_SET_HST2DEV_EN(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 9, 1, value)
#define WWLAN_SET_HST2DEV_EN_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 9, 1, value)
#define WWLAN_GET_GPIO_DURATION_MS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X04, 10, 1)
#define WWLAN_SET_GPIO_DURATION_MS(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 10, 1, value)
#define WWLAN_SET_GPIO_DURATION_MS_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 10, 1, value)
#define REMOTE_WAKE_CTRL_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define REMOTE_WAKE_CTRL_SET_CMD_ID(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define REMOTE_WAKE_CTRL_SET_CMD_ID_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define REMOTE_WAKE_CTRL_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define REMOTE_WAKE_CTRL_SET_CLASS(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define REMOTE_WAKE_CTRL_SET_CLASS_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define REMOTE_WAKE_CTRL_GET_REMOTE_WAKE_CTRL_EN(h2c_pkt)                      \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define REMOTE_WAKE_CTRL_SET_REMOTE_WAKE_CTRL_EN(h2c_pkt, value)               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define REMOTE_WAKE_CTRL_SET_REMOTE_WAKE_CTRL_EN_NO_CLR(h2c_pkt, value)        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define REMOTE_WAKE_CTRL_GET_ARP_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 9, 1)
#define REMOTE_WAKE_CTRL_SET_ARP_EN(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 9, 1, value)
#define REMOTE_WAKE_CTRL_SET_ARP_EN_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 9, 1, value)
#define REMOTE_WAKE_CTRL_GET_NDP_EN(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 10, 1)
#define REMOTE_WAKE_CTRL_SET_NDP_EN(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 10, 1, value)
#define REMOTE_WAKE_CTRL_SET_NDP_EN_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 10, 1, value)
#define REMOTE_WAKE_CTRL_GET_GTK_EN(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X00, 11, 1)
#define REMOTE_WAKE_CTRL_SET_GTK_EN(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 11, 1, value)
#define REMOTE_WAKE_CTRL_SET_GTK_EN_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 11, 1, value)
#define REMOTE_WAKE_CTRL_GET_NLO_OFFLOAD_EN(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 12, 1)
#define REMOTE_WAKE_CTRL_SET_NLO_OFFLOAD_EN(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 12, 1, value)
#define REMOTE_WAKE_CTRL_SET_NLO_OFFLOAD_EN_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 12, 1, value)
#define REMOTE_WAKE_CTRL_GET_REAL_WOW_V1_EN(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 13, 1)
#define REMOTE_WAKE_CTRL_SET_REAL_WOW_V1_EN(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 13, 1, value)
#define REMOTE_WAKE_CTRL_SET_REAL_WOW_V1_EN_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 13, 1, value)
#define REMOTE_WAKE_CTRL_GET_REAL_WOW_V2_EN(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 14, 1)
#define REMOTE_WAKE_CTRL_SET_REAL_WOW_V2_EN(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 14, 1, value)
#define REMOTE_WAKE_CTRL_SET_REAL_WOW_V2_EN_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 14, 1, value)
#define REMOTE_WAKE_CTRL_GET_FW_UNICAST(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 15, 1)
#define REMOTE_WAKE_CTRL_SET_FW_UNICAST(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 15, 1, value)
#define REMOTE_WAKE_CTRL_SET_FW_UNICAST_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 15, 1, value)
#define REMOTE_WAKE_CTRL_GET_P2P_OFFLOAD_EN(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 1)
#define REMOTE_WAKE_CTRL_SET_P2P_OFFLOAD_EN(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 1, value)
#define REMOTE_WAKE_CTRL_SET_P2P_OFFLOAD_EN_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 1, value)
#define REMOTE_WAKE_CTRL_GET_RUNTIME_PM_EN(h2c_pkt)                            \
	GET_H2C_FIELD(h2c_pkt + 0X00, 17, 1)
#define REMOTE_WAKE_CTRL_SET_RUNTIME_PM_EN(h2c_pkt, value)                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 17, 1, value)
#define REMOTE_WAKE_CTRL_SET_RUNTIME_PM_EN_NO_CLR(h2c_pkt, value)              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 17, 1, value)
#define REMOTE_WAKE_CTRL_GET_NET_BIOS_DROP_EN(h2c_pkt)                         \
	GET_H2C_FIELD(h2c_pkt + 0X00, 18, 1)
#define REMOTE_WAKE_CTRL_SET_NET_BIOS_DROP_EN(h2c_pkt, value)                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 18, 1, value)
#define REMOTE_WAKE_CTRL_SET_NET_BIOS_DROP_EN_NO_CLR(h2c_pkt, value)           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 18, 1, value)
#define REMOTE_WAKE_CTRL_GET_ARP_ACTION(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 1)
#define REMOTE_WAKE_CTRL_SET_ARP_ACTION(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 1, value)
#define REMOTE_WAKE_CTRL_SET_ARP_ACTION_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 1, value)
#define REMOTE_WAKE_CTRL_GET_TIM_PARSER_EN(h2c_pkt)                            \
	GET_H2C_FIELD(h2c_pkt + 0X00, 26, 1)
#define REMOTE_WAKE_CTRL_SET_TIM_PARSER_EN(h2c_pkt, value)                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 26, 1, value)
#define REMOTE_WAKE_CTRL_SET_TIM_PARSER_EN_NO_CLR(h2c_pkt, value)              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 26, 1, value)
#define REMOTE_WAKE_CTRL_GET_FW_PARSING_UNTIL_WAKEUP(h2c_pkt)                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 28, 1)
#define REMOTE_WAKE_CTRL_SET_FW_PARSING_UNTIL_WAKEUP(h2c_pkt, value)           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 28, 1, value)
#define REMOTE_WAKE_CTRL_SET_FW_PARSING_UNTIL_WAKEUP_NO_CLR(h2c_pkt, value)    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 28, 1, value)
#define REMOTE_WAKE_CTRL_GET_FW_PARSING_AFTER_WAKEUP(h2c_pkt)                  \
	GET_H2C_FIELD(h2c_pkt + 0X00, 29, 1)
#define REMOTE_WAKE_CTRL_SET_FW_PARSING_AFTER_WAKEUP(h2c_pkt, value)           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 29, 1, value)
#define REMOTE_WAKE_CTRL_SET_FW_PARSING_AFTER_WAKEUP_NO_CLR(h2c_pkt, value)    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 29, 1, value)
#define AOAC_GLOBAL_INFO_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define AOAC_GLOBAL_INFO_SET_CMD_ID(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_GLOBAL_INFO_SET_CMD_ID_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_GLOBAL_INFO_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define AOAC_GLOBAL_INFO_SET_CLASS(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_GLOBAL_INFO_SET_CLASS_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_GLOBAL_INFO_GET_PAIR_WISE_ENC_ALG(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define AOAC_GLOBAL_INFO_SET_PAIR_WISE_ENC_ALG(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_GLOBAL_INFO_SET_PAIR_WISE_ENC_ALG_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_GLOBAL_INFO_GET_GROUP_ENC_ALG(h2c_pkt)                            \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define AOAC_GLOBAL_INFO_SET_GROUP_ENC_ALG(h2c_pkt, value)                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AOAC_GLOBAL_INFO_SET_GROUP_ENC_ALG_NO_CLR(h2c_pkt, value)              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AOAC_RSVD_PAGE_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define AOAC_RSVD_PAGE_SET_CMD_ID(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_RSVD_PAGE_SET_CMD_ID_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_RSVD_PAGE_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define AOAC_RSVD_PAGE_SET_CLASS(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_RSVD_PAGE_SET_CLASS_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_RSVD_PAGE_GET_LOC_REMOTE_CTRL_INFO(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define AOAC_RSVD_PAGE_SET_LOC_REMOTE_CTRL_INFO(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_RSVD_PAGE_SET_LOC_REMOTE_CTRL_INFO_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_RSVD_PAGE_GET_LOC_ARP_RESPONSE(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define AOAC_RSVD_PAGE_SET_LOC_ARP_RESPONSE(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AOAC_RSVD_PAGE_SET_LOC_ARP_RESPONSE_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AOAC_RSVD_PAGE_GET_LOC_NEIGHBOR_ADVERTISEMENT(h2c_pkt)                 \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define AOAC_RSVD_PAGE_SET_LOC_NEIGHBOR_ADVERTISEMENT(h2c_pkt, value)          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define AOAC_RSVD_PAGE_SET_LOC_NEIGHBOR_ADVERTISEMENT_NO_CLR(h2c_pkt, value)   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define AOAC_RSVD_PAGE_GET_LOC_GTK_RSP(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_RSP(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_RSP_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AOAC_RSVD_PAGE_GET_LOC_GTK_INFO(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_INFO(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_INFO_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define AOAC_RSVD_PAGE_GET_LOC_GTK_EXT_MEM(h2c_pkt)                            \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_EXT_MEM(h2c_pkt, value)                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define AOAC_RSVD_PAGE_SET_LOC_GTK_EXT_MEM_NO_CLR(h2c_pkt, value)              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define AOAC_RSVD_PAGE_GET_LOC_NDP_INFO(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X04, 24, 8)
#define AOAC_RSVD_PAGE_SET_LOC_NDP_INFO(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 24, 8, value)
#define AOAC_RSVD_PAGE_SET_LOC_NDP_INFO_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 24, 8, value)
#define AOAC_RSVD_PAGE2_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define AOAC_RSVD_PAGE2_SET_CMD_ID(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_RSVD_PAGE2_SET_CMD_ID_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_RSVD_PAGE2_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define AOAC_RSVD_PAGE2_SET_CLASS(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_RSVD_PAGE2_SET_CLASS_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_RSVD_PAGE2_GET_LOC_ROUTER_SOLICATION(h2c_pkt)                     \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_ROUTER_SOLICATION(h2c_pkt, value)              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_RSVD_PAGE2_SET_LOC_ROUTER_SOLICATION_NO_CLR(h2c_pkt, value)       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_RSVD_PAGE2_GET_LOC_BUBBLE_PACKET(h2c_pkt)                         \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_BUBBLE_PACKET(h2c_pkt, value)                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AOAC_RSVD_PAGE2_SET_LOC_BUBBLE_PACKET_NO_CLR(h2c_pkt, value)           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AOAC_RSVD_PAGE2_GET_LOC_TEREDO_INFO(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_TEREDO_INFO(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define AOAC_RSVD_PAGE2_SET_LOC_TEREDO_INFO_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define AOAC_RSVD_PAGE2_GET_LOC_REALWOW_INFO(h2c_pkt)                          \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_REALWOW_INFO(h2c_pkt, value)                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AOAC_RSVD_PAGE2_SET_LOC_REALWOW_INFO_NO_CLR(h2c_pkt, value)            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 8, value)
#define AOAC_RSVD_PAGE2_GET_LOC_KEEP_ALIVE_PKT(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X04, 8, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_KEEP_ALIVE_PKT(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 8, 8, value)
#define AOAC_RSVD_PAGE2_SET_LOC_KEEP_ALIVE_PKT_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 8, 8, value)
#define AOAC_RSVD_PAGE2_GET_LOC_ACK_PATTERN(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_ACK_PATTERN(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 8, value)
#define AOAC_RSVD_PAGE2_SET_LOC_ACK_PATTERN_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 8, value)
#define AOAC_RSVD_PAGE2_GET_LOC_WAKEUP_PATTERN(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X04, 24, 8)
#define AOAC_RSVD_PAGE2_SET_LOC_WAKEUP_PATTERN(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 24, 8, value)
#define AOAC_RSVD_PAGE2_SET_LOC_WAKEUP_PATTERN_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 24, 8, value)
#define D0_SCAN_OFFLOAD_INFO_GET_CMD_ID(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define D0_SCAN_OFFLOAD_INFO_SET_CMD_ID(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define D0_SCAN_OFFLOAD_INFO_SET_CMD_ID_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define D0_SCAN_OFFLOAD_INFO_GET_CLASS(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define D0_SCAN_OFFLOAD_INFO_SET_CLASS(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define D0_SCAN_OFFLOAD_INFO_SET_CLASS_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define D0_SCAN_OFFLOAD_INFO_GET_LOC_CHANNEL_INFO(h2c_pkt)                     \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define D0_SCAN_OFFLOAD_INFO_SET_LOC_CHANNEL_INFO(h2c_pkt, value)              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define D0_SCAN_OFFLOAD_INFO_SET_LOC_CHANNEL_INFO_NO_CLR(h2c_pkt, value)       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define CHANNEL_SWITCH_OFFLOAD_GET_CMD_ID(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define CHANNEL_SWITCH_OFFLOAD_SET_CMD_ID(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define CHANNEL_SWITCH_OFFLOAD_SET_CMD_ID_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define CHANNEL_SWITCH_OFFLOAD_GET_CLASS(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define CHANNEL_SWITCH_OFFLOAD_SET_CLASS(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define CHANNEL_SWITCH_OFFLOAD_SET_CLASS_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define CHANNEL_SWITCH_OFFLOAD_GET_CHANNEL_NUM(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define CHANNEL_SWITCH_OFFLOAD_SET_CHANNEL_NUM(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define CHANNEL_SWITCH_OFFLOAD_SET_CHANNEL_NUM_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define CHANNEL_SWITCH_OFFLOAD_GET_EN_RFE(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define CHANNEL_SWITCH_OFFLOAD_SET_EN_RFE(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define CHANNEL_SWITCH_OFFLOAD_SET_EN_RFE_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define CHANNEL_SWITCH_OFFLOAD_GET_RFE_TYPE(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 24, 8)
#define CHANNEL_SWITCH_OFFLOAD_SET_RFE_TYPE(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 24, 8, value)
#define CHANNEL_SWITCH_OFFLOAD_SET_RFE_TYPE_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 24, 8, value)
#define AOAC_RSVD_PAGE3_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define AOAC_RSVD_PAGE3_SET_CMD_ID(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_RSVD_PAGE3_SET_CMD_ID_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define AOAC_RSVD_PAGE3_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define AOAC_RSVD_PAGE3_SET_CLASS(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_RSVD_PAGE3_SET_CLASS_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define AOAC_RSVD_PAGE3_GET_LOC_NLO_INFO(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define AOAC_RSVD_PAGE3_SET_LOC_NLO_INFO(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_RSVD_PAGE3_SET_LOC_NLO_INFO_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define AOAC_RSVD_PAGE3_GET_LOC_AOAC_REPORT(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 8)
#define AOAC_RSVD_PAGE3_SET_LOC_AOAC_REPORT(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 8, value)
#define AOAC_RSVD_PAGE3_SET_LOC_AOAC_REPORT_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 8, value)
#define DBG_MSG_CTRL_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 5)
#define DBG_MSG_CTRL_SET_CMD_ID(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 5, value)
#define DBG_MSG_CTRL_SET_CMD_ID_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 5, value)
#define DBG_MSG_CTRL_GET_CLASS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 5, 3)
#define DBG_MSG_CTRL_SET_CLASS(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 5, 3, value)
#define DBG_MSG_CTRL_SET_CLASS_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 5, 3, value)
#define DBG_MSG_CTRL_GET_FUN_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 1)
#define DBG_MSG_CTRL_SET_FUN_EN(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 1, value)
#define DBG_MSG_CTRL_SET_FUN_EN_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 1, value)
#define DBG_MSG_CTRL_GET_MODE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 12, 4)
#define DBG_MSG_CTRL_SET_MODE(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 12, 4, value)
#define DBG_MSG_CTRL_SET_MODE_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 12, 4, value)
#endif

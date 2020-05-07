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

#ifndef _HAL_FWOFFLOADC2HFORMAT_H2C_C2H_NIC_H_
#define _HAL_FWOFFLOADC2HFORMAT_H2C_C2H_NIC_H_
#define C2H_SUB_CMD_ID_C2H_DBG 0X00
#define C2H_SUB_CMD_ID_BT_COEX_INFO 0X02
#define C2H_SUB_CMD_ID_SCAN_STATUS_RPT 0X03
#define C2H_SUB_CMD_ID_H2C_ACK_HDR 0X01
#define C2H_SUB_CMD_ID_CFG_PARAM_ACK 0X01
#define C2H_SUB_CMD_ID_CH_SWITCH_ACK 0X01
#define C2H_SUB_CMD_ID_BT_COEX_ACK 0X01
#define C2H_SUB_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK 0X01
#define C2H_SUB_CMD_ID_UPDATE_PKT_ACK 0X01
#define C2H_SUB_CMD_ID_SEND_SCAN_PKT_ACK 0X01
#define C2H_SUB_CMD_ID_DROP_SCAN_PKT_ACK 0X01
#define C2H_SUB_CMD_ID_UPDATE_DATAPACK_ACK 0X01
#define C2H_SUB_CMD_ID_RUN_DATAPACK_ACK 0X01
#define C2H_SUB_CMD_ID_IQK_ACK 0X01
#define C2H_SUB_CMD_ID_PWR_TRK_ACK 0X01
#define C2H_SUB_CMD_ID_PSD_ACK 0X01
#define C2H_SUB_CMD_ID_FW_MEM_DUMP_ACK 0X01
#define C2H_SUB_CMD_ID_ACT_SCHEDULE_REQ_ACK 0X1
#define C2H_SUB_CMD_ID_NAN_FUNC_CTRL_ACK 0X1
#define C2H_SUB_CMD_ID_PSD_DATA 0X04
#define C2H_SUB_CMD_ID_EFUSE_DATA 0X05
#define C2H_SUB_CMD_ID_IQK_DATA 0X06
#define C2H_SUB_CMD_ID_C2H_PKT_FTM_DBG 0X07
#define C2H_SUB_CMD_ID_C2H_PKT_FTM_2_DBG 0X08
#define C2H_SUB_CMD_ID_C2H_PKT_FTM_3_DBG 0X09
#define C2H_SUB_CMD_ID_C2H_PKT_FTM_4_DBG 0X0A
#define C2H_SUB_CMD_ID_FTMACKRPT_HDL_DBG 0X0B
#define C2H_SUB_CMD_ID_FTMC2H_RPT 0X0C
#define C2H_SUB_CMD_ID_DRVFTMC2H_RPT 0X0D
#define C2H_SUB_CMD_ID_C2H_PKT_FTM_5_DBG 0X0E
#define C2H_SUB_CMD_ID_CCX_RPT 0X0F
#define C2H_SUB_CMD_ID_C2H_PKT_NAN_RPT 0X10
#define C2H_SUB_CMD_ID_C2H_PKT_ATM_RPT 0X11
#define C2H_SUB_CMD_ID_C2H_PKT_SCC_CSA_RPT 0X1A
#define C2H_SUB_CMD_ID_C2H_PKT_FW_STATUS_NOTIFY 0X1B
#define C2H_SUB_CMD_ID_C2H_PKT_FTMSESSION_END 0X1C
#define C2H_SUB_CMD_ID_C2H_PKT_DETECT_THERMAL 0X1D
#define C2H_SUB_CMD_ID_FW_DBG_MSG 0XFF
#define C2H_SUB_CMD_ID_FW_SNDING_ACK 0X01
#define C2H_SUB_CMD_ID_FW_FWCTRL_RPT 0X1F
#define C2H_SUB_CMD_ID_H2C_LOOPBACK_ACK 0X20
#define C2H_SUB_CMD_ID_FWCMD_LOOPBACK_ACK 0X21
#define C2H_SUB_CMD_ID_SCAN_CH_NOTIFY 0X22
#define C2H_SUB_CMD_ID_FW_TBTT_RPT 0X23
#define C2H_SUB_CMD_ID_BCN_OFFLOAD 0X24
#define H2C_SUB_CMD_ID_CFG_PARAM_ACK SUB_CMD_ID_CFG_PARAM
#define H2C_SUB_CMD_ID_CH_SWITCH_ACK SUB_CMD_ID_CH_SWITCH
#define H2C_SUB_CMD_ID_BT_COEX_ACK SUB_CMD_ID_BT_COEX
#define H2C_SUB_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK SUB_CMD_ID_DUMP_PHYSICAL_EFUSE
#define H2C_SUB_CMD_ID_UPDATE_PKT_ACK SUB_CMD_ID_UPDATE_PKT
#define H2C_SUB_CMD_ID_SEND_SCAN_PKT_ACK SUB_CMD_ID_SEND_SCAN_PKT
#define H2C_SUB_CMD_ID_DROP_SCAN_PKT_ACK SUB_CMD_ID_DROP_SCAN_PKT
#define H2C_SUB_CMD_ID_UPDATE_DATAPACK_ACK SUB_CMD_ID_UPDATE_DATAPACK
#define H2C_SUB_CMD_ID_RUN_DATAPACK_ACK SUB_CMD_ID_RUN_DATAPACK
#define H2C_SUB_CMD_ID_IQK_ACK SUB_CMD_ID_IQK
#define H2C_SUB_CMD_ID_PWR_TRK_ACK SUB_CMD_ID_PWR_TRK
#define H2C_SUB_CMD_ID_PSD_ACK SUB_CMD_ID_PSD
#define H2C_SUB_CMD_ID_FW_MEM_DUMP_ACK SUB_CMD_ID_FW_MEM_DUMP
#define H2C_SUB_CMD_ID_ACT_SCHEDULE_REQ_ACK SUB_CMD_ID_ACT_SCHEDULE_REQ
#define H2C_SUB_CMD_ID_NAN_FUNC_CTRL_ACK SUB_CMD_ID_NAN_FUNC_CTRL
#define H2C_SUB_CMD_ID_CCX_RPT SUB_CMD_ID_CCX_RPT
#define H2C_SUB_CMD_ID_FW_DBG_MSG SUB_CMD_ID_FW_DBG_MSG
#define H2C_SUB_CMD_ID_FW_SNDING_ACK SUB_CMD_ID_FW_SNDING
#define H2C_SUB_CMD_ID_FW_FWCTRL_RPT SUB_CMD_ID_FW_FWCTRL_RPT
#define H2C_SUB_CMD_ID_H2C_LOOPBACK_ACK SUB_CMD_ID_H2C_LOOPBACK
#define H2C_SUB_CMD_ID_FWCMD_LOOPBACK_ACK SUB_CMD_ID_FWCMD_LOOPBACK
#define H2C_CMD_ID_CFG_PARAM_ACK 0XFF
#define H2C_CMD_ID_CH_SWITCH_ACK 0XFF
#define H2C_CMD_ID_BT_COEX_ACK 0XFF
#define H2C_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK 0XFF
#define H2C_CMD_ID_UPDATE_PKT_ACK 0XFF
#define H2C_CMD_ID_SEND_SCAN_PKT_ACK 0XFF
#define H2C_CMD_ID_DROP_SCAN_PKT_ACK 0XFF
#define H2C_CMD_ID_UPDATE_DATAPACK_ACK 0XFF
#define H2C_CMD_ID_RUN_DATAPACK_ACK 0XFF
#define H2C_CMD_ID_IQK_ACK 0XFF
#define H2C_CMD_ID_PWR_TRK_ACK 0XFF
#define H2C_CMD_ID_PSD_ACK 0XFF
#define H2C_CMD_ID_FW_MEM_DUMP_ACK 0XFF
#define H2C_CMD_ID_ACT_SCHEDULE_REQ_ACK 0XFF
#define H2C_CMD_ID_NAN_FUNC_CTRL_ACK 0XFF
#define H2C_CMD_ID_CCX_RPT 0XFF
#define H2C_CMD_ID_FW_DBG_MSG 0XFF
#define H2C_CMD_ID_FW_SNDING_ACK 0XFF
#define H2C_CMD_ID_FW_FWCTRL_RPT 0XFF
#define H2C_CMD_ID_H2C_LOOPBACK_ACK 0XFF
#define H2C_CMD_ID_FWCMD_LOOPBACK_ACK 0XFF
#define C2H_HDR_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_HDR_SET_CMD_ID(c2h_pkt, value)                                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_HDR_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_HDR_SET_SEQ(c2h_pkt, value)                                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_HDR_GET_C2H_SUB_CMD_ID(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define C2H_HDR_SET_C2H_SUB_CMD_ID(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define C2H_HDR_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define C2H_HDR_SET_LEN(c2h_pkt, value)                                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define C2H_DBG_GET_DBG_MSG(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define C2H_DBG_SET_DBG_MSG(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define BT_COEX_INFO_GET_DATA_START(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define BT_COEX_INFO_SET_DATA_START(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define SCAN_STATUS_RPT_GET_H2C_RETURN_CODE(c2h_pkt)                           \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define SCAN_STATUS_RPT_SET_H2C_RETURN_CODE(c2h_pkt, value)                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define SCAN_STATUS_RPT_GET_H2C_SEQ(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 16)
#define SCAN_STATUS_RPT_SET_H2C_SEQ(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 16, value)
#define SCAN_STATUS_RPT_GET_TSF_0(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 8)
#define SCAN_STATUS_RPT_SET_TSF_0(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 8, value)
#define SCAN_STATUS_RPT_GET_TSF_1(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 8, 8)
#define SCAN_STATUS_RPT_SET_TSF_1(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 8, 8, value)
#define SCAN_STATUS_RPT_GET_TSF_2(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 16, 8)
#define SCAN_STATUS_RPT_SET_TSF_2(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 16, 8, value)
#define SCAN_STATUS_RPT_GET_TSF_3(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 24, 8)
#define SCAN_STATUS_RPT_SET_TSF_3(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 24, 8, value)
#define SCAN_STATUS_RPT_GET_TSF_4(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 0, 8)
#define SCAN_STATUS_RPT_SET_TSF_4(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 0, 8, value)
#define SCAN_STATUS_RPT_GET_TSF_5(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 8, 8)
#define SCAN_STATUS_RPT_SET_TSF_5(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 8, 8, value)
#define SCAN_STATUS_RPT_GET_TSF_6(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 16, 8)
#define SCAN_STATUS_RPT_SET_TSF_6(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 16, 8, value)
#define SCAN_STATUS_RPT_GET_TSF_7(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 24, 8)
#define SCAN_STATUS_RPT_SET_TSF_7(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 24, 8, value)
#define H2C_ACK_HDR_GET_H2C_RETURN_CODE(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define H2C_ACK_HDR_SET_H2C_RETURN_CODE(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define H2C_ACK_HDR_GET_H2C_CMD_ID(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define H2C_ACK_HDR_SET_H2C_CMD_ID(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define H2C_ACK_HDR_GET_H2C_SUB_CMD_ID(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 16)
#define H2C_ACK_HDR_SET_H2C_SUB_CMD_ID(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 16, value)
#define H2C_ACK_HDR_GET_H2C_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 16)
#define H2C_ACK_HDR_SET_H2C_SEQ(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 16, value)
#define CFG_PARAM_ACK_GET_OFFSET_ACCUMULATION(c2h_pkt)                         \
	LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 0, 32)
#define CFG_PARAM_ACK_SET_OFFSET_ACCUMULATION(c2h_pkt, value)                  \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 0, 32, value)
#define CFG_PARAM_ACK_GET_VALUE_ACCUMULATION(c2h_pkt)                          \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X10, 0, 32)
#define CFG_PARAM_ACK_SET_VALUE_ACCUMULATION(c2h_pkt, value)                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X10, 0, 32, value)
#define CH_SWITCH_ACK_GET_TSF_0(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 0, 8)
#define CH_SWITCH_ACK_SET_TSF_0(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 0, 8, value)
#define CH_SWITCH_ACK_GET_TSF_1(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 8, 8)
#define CH_SWITCH_ACK_SET_TSF_1(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 8, 8, value)
#define CH_SWITCH_ACK_GET_TSF_2(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define CH_SWITCH_ACK_SET_TSF_2(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define CH_SWITCH_ACK_GET_TSF_3(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define CH_SWITCH_ACK_SET_TSF_3(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define CH_SWITCH_ACK_GET_TSF_4(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X10, 0, 8)
#define CH_SWITCH_ACK_SET_TSF_4(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X10, 0, 8, value)
#define CH_SWITCH_ACK_GET_TSF_5(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X10, 8, 8)
#define CH_SWITCH_ACK_SET_TSF_5(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X10, 8, 8, value)
#define CH_SWITCH_ACK_GET_TSF_6(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X10, 16, 8)
#define CH_SWITCH_ACK_SET_TSF_6(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X10, 16, 8, value)
#define CH_SWITCH_ACK_GET_TSF_7(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X10, 24, 8)
#define CH_SWITCH_ACK_SET_TSF_7(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X10, 24, 8, value)
#define BT_COEX_ACK_GET_DATA_START(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 0, 8)
#define BT_COEX_ACK_SET_DATA_START(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 0, 8, value)
#define PSD_DATA_GET_SEGMENT_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 7)
#define PSD_DATA_SET_SEGMENT_ID(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 7, value)
#define PSD_DATA_GET_END_SEGMENT(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 7, 1)
#define PSD_DATA_SET_END_SEGMENT(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 7, 1, value)
#define PSD_DATA_GET_SEGMENT_SIZE(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define PSD_DATA_SET_SEGMENT_SIZE(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define PSD_DATA_GET_TOTAL_SIZE(c2h_pkt)                                       \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 16)
#define PSD_DATA_SET_TOTAL_SIZE(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 16, value)
#define PSD_DATA_GET_H2C_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 0, 16)
#define PSD_DATA_SET_H2C_SEQ(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 0, 16, value)
#define PSD_DATA_GET_DATA_START(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 16, 8)
#define PSD_DATA_SET_DATA_START(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 16, 8, value)
#define EFUSE_DATA_GET_SEGMENT_ID(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 7)
#define EFUSE_DATA_SET_SEGMENT_ID(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 7, value)
#define EFUSE_DATA_GET_END_SEGMENT(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 7, 1)
#define EFUSE_DATA_SET_END_SEGMENT(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 7, 1, value)
#define EFUSE_DATA_GET_SEGMENT_SIZE(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define EFUSE_DATA_SET_SEGMENT_SIZE(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define EFUSE_DATA_GET_TOTAL_SIZE(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 16)
#define EFUSE_DATA_SET_TOTAL_SIZE(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 16, value)
#define EFUSE_DATA_GET_H2C_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 0, 16)
#define EFUSE_DATA_SET_H2C_SEQ(c2h_pkt, value)                                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 0, 16, value)
#define EFUSE_DATA_GET_DATA_START(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 16, 8)
#define EFUSE_DATA_SET_DATA_START(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 16, 8, value)
#define IQK_DATA_GET_SEGMENT_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 7)
#define IQK_DATA_SET_SEGMENT_ID(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 7, value)
#define IQK_DATA_GET_END_SEGMENT(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 7, 1)
#define IQK_DATA_SET_END_SEGMENT(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 7, 1, value)
#define IQK_DATA_GET_SEGMENT_SIZE(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define IQK_DATA_SET_SEGMENT_SIZE(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define IQK_DATA_GET_TOTAL_SIZE(c2h_pkt)                                       \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 16)
#define IQK_DATA_SET_TOTAL_SIZE(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 16, value)
#define IQK_DATA_GET_H2C_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 0, 16)
#define IQK_DATA_SET_H2C_SEQ(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 0, 16, value)
#define IQK_DATA_GET_DATA_START(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 16, 8)
#define IQK_DATA_SET_DATA_START(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 16, 8, value)
#define CCX_RPT_GET_POLLUTED(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X4, 0, 1)
#define CCX_RPT_SET_POLLUTED(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X4, 0, 1, value)
#define CCX_RPT_GET_RPT_SEL(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X4, 5, 3)
#define CCX_RPT_SET_RPT_SEL(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X4, 5, 3, value)
#define CCX_RPT_GET_QSEL(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X4, 8, 5)
#define CCX_RPT_SET_QSEL(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X4, 8, 5, value)
#define CCX_RPT_GET_MISSED_RPT_NUM(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X4, 13, 3)
#define CCX_RPT_SET_MISSED_RPT_NUM(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X4, 13, 3, value)
#define CCX_RPT_GET_MACID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X4, 16, 7)
#define CCX_RPT_SET_MACID(c2h_pkt, value)                                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X4, 16, 7, value)
#define CCX_RPT_GET_INITIAL_DATA_RATE(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X4, 24, 7)
#define CCX_RPT_SET_INITIAL_DATA_RATE(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X4, 24, 7, value)
#define CCX_RPT_GET_INITIAL_SGI(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X4, 31, 1)
#define CCX_RPT_SET_INITIAL_SGI(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X4, 31, 1, value)
#define CCX_RPT_GET_QUEUE_TIME(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 0, 16)
#define CCX_RPT_SET_QUEUE_TIME(c2h_pkt, value)                                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 0, 16, value)
#define CCX_RPT_GET_SW_DEFINE_BYTE0(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 16, 8)
#define CCX_RPT_SET_SW_DEFINE_BYTE0(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 16, 8, value)
#define CCX_RPT_GET_RTS_RETRY_COUNT(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 24, 4)
#define CCX_RPT_SET_RTS_RETRY_COUNT(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 24, 4, value)
#define CCX_RPT_GET_BMC(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 29, 1)
#define CCX_RPT_SET_BMC(c2h_pkt, value)                                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 29, 1, value)
#define CCX_RPT_GET_TX_STATE(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 30, 2)
#define CCX_RPT_SET_TX_STATE(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 30, 2, value)
#define CCX_RPT_GET_DATA_RETRY_COUNT(c2h_pkt)                                  \
	LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 0, 6)
#define CCX_RPT_SET_DATA_RETRY_COUNT(c2h_pkt, value)                           \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 0, 6, value)
#define CCX_RPT_GET_FINAL_DATA_RATE(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 8, 7)
#define CCX_RPT_SET_FINAL_DATA_RATE(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 8, 7, value)
#define CCX_RPT_GET_FINAL_SGI(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 15, 1)
#define CCX_RPT_SET_FINAL_SGI(c2h_pkt, value)                                  \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 15, 1, value)
#define CCX_RPT_GET_RF_CH_NUM(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 16, 10)
#define CCX_RPT_SET_RF_CH_NUM(c2h_pkt, value)                                  \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 16, 10, value)
#define CCX_RPT_GET_SC(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 26, 4)
#define CCX_RPT_SET_SC(c2h_pkt, value)                                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 26, 4, value)
#define CCX_RPT_GET_BW(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0XC, 30, 2)
#define CCX_RPT_SET_BW(c2h_pkt, value)                                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0XC, 30, 2, value)
#define C2H_PKT_FW_STATUS_NOTIFY_GET_STATUS_CODE(c2h_pkt)                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 32)
#define C2H_PKT_FW_STATUS_NOTIFY_SET_STATUS_CODE(c2h_pkt, value)               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 32, value)
#define C2H_PKT_DETECT_THERMAL_GET_THERMAL_VALUE(c2h_pkt)                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 32)
#define C2H_PKT_DETECT_THERMAL_SET_THERMAL_VALUE(c2h_pkt, value)               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 32, value)
#define FW_DBG_MSG_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define FW_DBG_MSG_SET_CMD_ID(c2h_pkt, value)                                  \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define FW_DBG_MSG_GET_C2H_SUB_CMD_ID(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define FW_DBG_MSG_SET_C2H_SUB_CMD_ID(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define FW_DBG_MSG_GET_FULL(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 1)
#define FW_DBG_MSG_SET_FULL(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 1, value)
#define FW_DBG_MSG_GET_OWN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 31, 1)
#define FW_DBG_MSG_SET_OWN(c2h_pkt, value)                                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 31, 1, value)
#define FW_FWCTRL_RPT_GET_EVT_TYPE(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define FW_FWCTRL_RPT_SET_EVT_TYPE(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define FW_FWCTRL_RPT_GET_LENGTH(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define FW_FWCTRL_RPT_SET_LENGTH(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define FW_FWCTRL_RPT_GET_SEQ_NUM(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define FW_FWCTRL_RPT_SET_SEQ_NUM(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define FW_FWCTRL_RPT_GET_IS_ACK(c2h_pkt)                                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 1)
#define FW_FWCTRL_RPT_SET_IS_ACK(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 1, value)
#define FW_FWCTRL_RPT_GET_MORE_CONTENT(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 25, 1)
#define FW_FWCTRL_RPT_SET_MORE_CONTENT(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 25, 1, value)
#define FW_FWCTRL_RPT_GET_CONTENT_IDX(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 26, 6)
#define FW_FWCTRL_RPT_SET_CONTENT_IDX(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 26, 6, value)
#define FW_FWCTRL_RPT_GET_CLASS_ID(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define FW_FWCTRL_RPT_SET_CLASS_ID(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define FW_FWCTRL_RPT_GET_CONTENT(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 16)
#define FW_FWCTRL_RPT_SET_CONTENT(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 16, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_0(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_0(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_1(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_1(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_2(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_2(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_3(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 24, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_3(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 24, 8, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_4(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 0, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_4(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 0, 8, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_5(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 8, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_5(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 8, 8, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_6(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 16, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_6(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 16, 8, value)
#define H2C_LOOPBACK_ACK_GET_H2C_BYTE_7(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 24, 8)
#define H2C_LOOPBACK_ACK_SET_H2C_BYTE_7(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 24, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_0(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_0(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_1(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_1(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_2(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_2(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_3(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 24, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_3(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 24, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_4(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 0, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_4(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 0, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_5(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 8, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_5(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 8, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_6(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 16, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_6(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 16, 8, value)
#define FWCMD_LOOPBACK_ACK_GET_H2C_BYTE_7(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X8, 24, 8)
#define FWCMD_LOOPBACK_ACK_SET_H2C_BYTE_7(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X8, 24, 8, value)
#define SCAN_CH_NOTIFY_GET_CH_NUM(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define SCAN_CH_NOTIFY_SET_CH_NUM(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define SCAN_CH_NOTIFY_GET_NOTIFY_ID(c2h_pkt)                                  \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define SCAN_CH_NOTIFY_SET_NOTIFY_ID(c2h_pkt, value)                           \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define SCAN_CH_NOTIFY_GET_STATUS(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define SCAN_CH_NOTIFY_SET_STATUS(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_0(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 8)
#define SCAN_CH_NOTIFY_SET_TSF_0(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_1(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 8, 8)
#define SCAN_CH_NOTIFY_SET_TSF_1(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 8, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_2(c2h_pkt)                                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 16, 8)
#define SCAN_CH_NOTIFY_SET_TSF_2(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 16, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_3(c2h_pkt)                                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 24, 8)
#define SCAN_CH_NOTIFY_SET_TSF_3(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 24, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_4(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 0, 8)
#define SCAN_CH_NOTIFY_SET_TSF_4(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 0, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_5(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 8, 8)
#define SCAN_CH_NOTIFY_SET_TSF_5(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 8, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_6(c2h_pkt)                                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define SCAN_CH_NOTIFY_SET_TSF_6(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define SCAN_CH_NOTIFY_GET_TSF_7(c2h_pkt)                                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define SCAN_CH_NOTIFY_SET_TSF_7(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define FW_TBTT_RPT_GET_PORT_NUMBER(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define FW_TBTT_RPT_SET_PORT_NUMBER(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define BCN_OFFLOAD_GET_SUPPORT_VER(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define BCN_OFFLOAD_SET_SUPPORT_VER(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define BCN_OFFLOAD_GET_STATUS(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define BCN_OFFLOAD_SET_STATUS(c2h_pkt, value)                                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#endif

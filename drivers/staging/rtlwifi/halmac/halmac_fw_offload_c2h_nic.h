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
#ifndef _HAL_FWOFFLOADC2HFORMAT_H2C_C2H_NIC_H_
#define _HAL_FWOFFLOADC2HFORMAT_H2C_C2H_NIC_H_
#define C2H_SUB_CMD_ID_C2H_DBG 0X00
#define C2H_SUB_CMD_ID_BT_COEX_INFO 0X02
#define C2H_SUB_CMD_ID_SCAN_STATUS_RPT 0X03
#define C2H_SUB_CMD_ID_H2C_ACK_HDR 0X01
#define C2H_SUB_CMD_ID_CFG_PARAMETER_ACK 0X01
#define C2H_SUB_CMD_ID_BT_COEX_ACK 0X01
#define C2H_SUB_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK 0X01
#define C2H_SUB_CMD_ID_UPDATE_PACKET_ACK 0X01
#define C2H_SUB_CMD_ID_UPDATE_DATAPACK_ACK 0X01
#define C2H_SUB_CMD_ID_RUN_DATAPACK_ACK 0X01
#define C2H_SUB_CMD_ID_CHANNEL_SWITCH_ACK 0X01
#define C2H_SUB_CMD_ID_IQK_ACK 0X01
#define C2H_SUB_CMD_ID_POWER_TRACKING_ACK 0X01
#define C2H_SUB_CMD_ID_PSD_ACK 0X01
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
#define H2C_SUB_CMD_ID_CFG_PARAMETER_ACK SUB_CMD_ID_CFG_PARAMETER
#define H2C_SUB_CMD_ID_BT_COEX_ACK SUB_CMD_ID_BT_COEX
#define H2C_SUB_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK SUB_CMD_ID_DUMP_PHYSICAL_EFUSE
#define H2C_SUB_CMD_ID_UPDATE_PACKET_ACK SUB_CMD_ID_UPDATE_PACKET
#define H2C_SUB_CMD_ID_UPDATE_DATAPACK_ACK SUB_CMD_ID_UPDATE_DATAPACK
#define H2C_SUB_CMD_ID_RUN_DATAPACK_ACK SUB_CMD_ID_RUN_DATAPACK
#define H2C_SUB_CMD_ID_CHANNEL_SWITCH_ACK SUB_CMD_ID_CHANNEL_SWITCH
#define H2C_SUB_CMD_ID_IQK_ACK SUB_CMD_ID_IQK
#define H2C_SUB_CMD_ID_POWER_TRACKING_ACK SUB_CMD_ID_POWER_TRACKING
#define H2C_SUB_CMD_ID_PSD_ACK SUB_CMD_ID_PSD
#define H2C_SUB_CMD_ID_CCX_RPT SUB_CMD_ID_CCX_RPT
#define H2C_CMD_ID_CFG_PARAMETER_ACK 0XFF
#define H2C_CMD_ID_BT_COEX_ACK 0XFF
#define H2C_CMD_ID_DUMP_PHYSICAL_EFUSE_ACK 0XFF
#define H2C_CMD_ID_UPDATE_PACKET_ACK 0XFF
#define H2C_CMD_ID_UPDATE_DATAPACK_ACK 0XFF
#define H2C_CMD_ID_RUN_DATAPACK_ACK 0XFF
#define H2C_CMD_ID_CHANNEL_SWITCH_ACK 0XFF
#define H2C_CMD_ID_IQK_ACK 0XFF
#define H2C_CMD_ID_POWER_TRACKING_ACK 0XFF
#define H2C_CMD_ID_PSD_ACK 0XFF
#define H2C_CMD_ID_CCX_RPT 0XFF
#define C2H_HDR_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_HDR_SET_CMD_ID(__c2h, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_HDR_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_HDR_SET_SEQ(__c2h, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_HDR_GET_C2H_SUB_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 8)
#define C2H_HDR_SET_C2H_SUB_CMD_ID(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 8, __value)
#define C2H_HDR_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 24, 8)
#define C2H_HDR_SET_LEN(__c2h, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 24, 8, __value)
#define C2H_DBG_GET_DBG_MSG(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define C2H_DBG_SET_DBG_MSG(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define BT_COEX_INFO_GET_DATA_START(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define BT_COEX_INFO_SET_DATA_START(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define SCAN_STATUS_RPT_GET_H2C_RETURN_CODE(__c2h)                             \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define SCAN_STATUS_RPT_SET_H2C_RETURN_CODE(__c2h, __value)                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define SCAN_STATUS_RPT_GET_H2C_SEQ(__c2h)                                     \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 16)
#define SCAN_STATUS_RPT_SET_H2C_SEQ(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 16, __value)
#define H2C_ACK_HDR_GET_H2C_RETURN_CODE(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define H2C_ACK_HDR_SET_H2C_RETURN_CODE(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define H2C_ACK_HDR_GET_H2C_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define H2C_ACK_HDR_SET_H2C_CMD_ID(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define H2C_ACK_HDR_GET_H2C_SUB_CMD_ID(__c2h)                                  \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 16)
#define H2C_ACK_HDR_SET_H2C_SUB_CMD_ID(__c2h, __value)                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 16, __value)
#define H2C_ACK_HDR_GET_H2C_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X08, 0, 16)
#define H2C_ACK_HDR_SET_H2C_SEQ(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 0, 16, __value)
#define CFG_PARAMETER_ACK_GET_OFFSET_ACCUMULATION(__c2h)                       \
	LE_BITS_TO_4BYTE(__c2h + 0XC, 0, 32)
#define CFG_PARAMETER_ACK_SET_OFFSET_ACCUMULATION(__c2h, __value)              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0XC, 0, 32, __value)
#define CFG_PARAMETER_ACK_GET_VALUE_ACCUMULATION(__c2h)                        \
	LE_BITS_TO_4BYTE(__c2h + 0X10, 0, 32)
#define CFG_PARAMETER_ACK_SET_VALUE_ACCUMULATION(__c2h, __value)               \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X10, 0, 32, __value)
#define BT_COEX_ACK_GET_DATA_START(__c2h) LE_BITS_TO_4BYTE(__c2h + 0XC, 0, 8)
#define BT_COEX_ACK_SET_DATA_START(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0XC, 0, 8, __value)
#define PSD_DATA_GET_SEGMENT_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 7)
#define PSD_DATA_SET_SEGMENT_ID(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 7, __value)
#define PSD_DATA_GET_END_SEGMENT(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 7, 1)
#define PSD_DATA_SET_END_SEGMENT(__c2h, __value)                               \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 7, 1, __value)
#define PSD_DATA_GET_SEGMENT_SIZE(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define PSD_DATA_SET_SEGMENT_SIZE(__c2h, __value)                              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define PSD_DATA_GET_TOTAL_SIZE(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 16)
#define PSD_DATA_SET_TOTAL_SIZE(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 16, __value)
#define PSD_DATA_GET_H2C_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X8, 0, 16)
#define PSD_DATA_SET_H2C_SEQ(__c2h, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X8, 0, 16, __value)
#define PSD_DATA_GET_DATA_START(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X8, 16, 8)
#define PSD_DATA_SET_DATA_START(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X8, 16, 8, __value)
#define EFUSE_DATA_GET_SEGMENT_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 7)
#define EFUSE_DATA_SET_SEGMENT_ID(__c2h, __value)                              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 7, __value)
#define EFUSE_DATA_GET_END_SEGMENT(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 7, 1)
#define EFUSE_DATA_SET_END_SEGMENT(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 7, 1, __value)
#define EFUSE_DATA_GET_SEGMENT_SIZE(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define EFUSE_DATA_SET_SEGMENT_SIZE(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define EFUSE_DATA_GET_TOTAL_SIZE(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 16)
#define EFUSE_DATA_SET_TOTAL_SIZE(__c2h, __value)                              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 16, __value)
#define EFUSE_DATA_GET_H2C_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X8, 0, 16)
#define EFUSE_DATA_SET_H2C_SEQ(__c2h, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X8, 0, 16, __value)
#define EFUSE_DATA_GET_DATA_START(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X8, 16, 8)
#define EFUSE_DATA_SET_DATA_START(__c2h, __value)                              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X8, 16, 8, __value)
#define IQK_DATA_GET_SEGMENT_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 7)
#define IQK_DATA_SET_SEGMENT_ID(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 7, __value)
#define IQK_DATA_GET_END_SEGMENT(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 7, 1)
#define IQK_DATA_SET_END_SEGMENT(__c2h, __value)                               \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 7, 1, __value)
#define IQK_DATA_GET_SEGMENT_SIZE(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define IQK_DATA_SET_SEGMENT_SIZE(__c2h, __value)                              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define IQK_DATA_GET_TOTAL_SIZE(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 16)
#define IQK_DATA_SET_TOTAL_SIZE(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 16, __value)
#define IQK_DATA_GET_H2C_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X8, 0, 16)
#define IQK_DATA_SET_H2C_SEQ(__c2h, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X8, 0, 16, __value)
#define IQK_DATA_GET_DATA_START(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X8, 16, 8)
#define IQK_DATA_SET_DATA_START(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X8, 16, 8, __value)
#define CCX_RPT_GET_CCX_RPT(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X4, 0, 129)
#define CCX_RPT_SET_CCX_RPT(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X4, 0, 129, __value)
#endif

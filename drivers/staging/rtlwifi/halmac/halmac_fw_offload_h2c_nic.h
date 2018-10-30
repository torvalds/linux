/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HAL_FWOFFLOADH2CFORMAT_H2C_C2H_NIC_H_
#define _HAL_FWOFFLOADH2CFORMAT_H2C_C2H_NIC_H_
#define CMD_ID_FW_OFFLOAD_H2C 0XFF
#define CMD_ID_CHANNEL_SWITCH 0XFF
#define CMD_ID_DUMP_PHYSICAL_EFUSE 0XFF
#define CMD_ID_UPDATE_BEACON_PARSING_INFO 0XFF
#define CMD_ID_CFG_PARAMETER 0XFF
#define CMD_ID_UPDATE_DATAPACK 0XFF
#define CMD_ID_RUN_DATAPACK 0XFF
#define CMD_ID_DOWNLOAD_FLASH 0XFF
#define CMD_ID_UPDATE_PACKET 0XFF
#define CMD_ID_GENERAL_INFO 0XFF
#define CMD_ID_IQK 0XFF
#define CMD_ID_POWER_TRACKING 0XFF
#define CMD_ID_PSD 0XFF
#define CMD_ID_P2PPS 0XFF
#define CMD_ID_BT_COEX 0XFF
#define CMD_ID_NAN_CTRL 0XFF
#define CMD_ID_NAN_CHANNEL_PLAN_0 0XFF
#define CMD_ID_NAN_CHANNEL_PLAN_1 0XFF
#define CATEGORY_H2C_CMD_HEADER 0X00
#define CATEGORY_FW_OFFLOAD_H2C 0X01
#define CATEGORY_CHANNEL_SWITCH 0X01
#define CATEGORY_DUMP_PHYSICAL_EFUSE 0X01
#define CATEGORY_UPDATE_BEACON_PARSING_INFO 0X01
#define CATEGORY_CFG_PARAMETER 0X01
#define CATEGORY_UPDATE_DATAPACK 0X01
#define CATEGORY_RUN_DATAPACK 0X01
#define CATEGORY_DOWNLOAD_FLASH 0X01
#define CATEGORY_UPDATE_PACKET 0X01
#define CATEGORY_GENERAL_INFO 0X01
#define CATEGORY_IQK 0X01
#define CATEGORY_POWER_TRACKING 0X01
#define CATEGORY_PSD 0X01
#define CATEGORY_P2PPS 0X01
#define CATEGORY_BT_COEX 0X01
#define CATEGORY_NAN_CTRL 0X01
#define CATEGORY_NAN_CHANNEL_PLAN_0 0X01
#define CATEGORY_NAN_CHANNEL_PLAN_1 0X01
#define SUB_CMD_ID_CHANNEL_SWITCH 0X02
#define SUB_CMD_ID_DUMP_PHYSICAL_EFUSE 0X03
#define SUB_CMD_ID_UPDATE_BEACON_PARSING_INFO 0X05
#define SUB_CMD_ID_CFG_PARAMETER 0X08
#define SUB_CMD_ID_UPDATE_DATAPACK 0X09
#define SUB_CMD_ID_RUN_DATAPACK 0X0A
#define SUB_CMD_ID_DOWNLOAD_FLASH 0X0B
#define SUB_CMD_ID_UPDATE_PACKET 0X0C
#define SUB_CMD_ID_GENERAL_INFO 0X0D
#define SUB_CMD_ID_IQK 0X0E
#define SUB_CMD_ID_POWER_TRACKING 0X0F
#define SUB_CMD_ID_PSD 0X10
#define SUB_CMD_ID_P2PPS 0X24
#define SUB_CMD_ID_BT_COEX 0X60
#define SUB_CMD_ID_NAN_CTRL 0XB2
#define SUB_CMD_ID_NAN_CHANNEL_PLAN_0 0XB4
#define SUB_CMD_ID_NAN_CHANNEL_PLAN_1 0XB5
#define H2C_CMD_HEADER_GET_CATEGORY(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 7)
#define H2C_CMD_HEADER_SET_CATEGORY(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 7, __value)
#define H2C_CMD_HEADER_GET_ACK(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 7, 1)
#define H2C_CMD_HEADER_SET_ACK(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 7, 1, __value)
#define H2C_CMD_HEADER_GET_TOTAL_LEN(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 16)
#define H2C_CMD_HEADER_SET_TOTAL_LEN(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 16, __value)
#define H2C_CMD_HEADER_GET_SEQ_NUM(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 16)
#define H2C_CMD_HEADER_SET_SEQ_NUM(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 16, __value)
#define FW_OFFLOAD_H2C_GET_CATEGORY(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 0, 7)
#define FW_OFFLOAD_H2C_SET_CATEGORY(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 0, 7, __value)
#define FW_OFFLOAD_H2C_GET_ACK(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 7, 1)
#define FW_OFFLOAD_H2C_SET_ACK(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 7, 1, __value)
#define FW_OFFLOAD_H2C_GET_CMD_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X00, 8, 8)
#define FW_OFFLOAD_H2C_SET_CMD_ID(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 8, 8, __value)
#define FW_OFFLOAD_H2C_GET_SUB_CMD_ID(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X00, 16, 16)
#define FW_OFFLOAD_H2C_SET_SUB_CMD_ID(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X00, 16, 16, __value)
#define FW_OFFLOAD_H2C_GET_TOTAL_LEN(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X04, 0, 16)
#define FW_OFFLOAD_H2C_SET_TOTAL_LEN(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 0, 16, __value)
#define FW_OFFLOAD_H2C_GET_SEQ_NUM(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X04, 16, 16)
#define FW_OFFLOAD_H2C_SET_SEQ_NUM(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X04, 16, 16, __value)
#define CHANNEL_SWITCH_GET_SWITCH_START(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 1)
#define CHANNEL_SWITCH_SET_SWITCH_START(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 1, __value)
#define CHANNEL_SWITCH_GET_DEST_CH_EN(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 1, 1)
#define CHANNEL_SWITCH_SET_DEST_CH_EN(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 1, 1, __value)
#define CHANNEL_SWITCH_GET_ABSOLUTE_TIME(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 2, 1)
#define CHANNEL_SWITCH_SET_ABSOLUTE_TIME(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 2, 1, __value)
#define CHANNEL_SWITCH_GET_PERIODIC_OPTION(__h2c)                              \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 3, 2)
#define CHANNEL_SWITCH_SET_PERIODIC_OPTION(__h2c, __value)                     \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 3, 2, __value)
#define CHANNEL_SWITCH_GET_CHANNEL_INFO_LOC(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 8)
#define CHANNEL_SWITCH_SET_CHANNEL_INFO_LOC(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 8, __value)
#define CHANNEL_SWITCH_GET_CHANNEL_NUM(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 8)
#define CHANNEL_SWITCH_SET_CHANNEL_NUM(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 8, __value)
#define CHANNEL_SWITCH_GET_PRI_CH_IDX(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 24, 4)
#define CHANNEL_SWITCH_SET_PRI_CH_IDX(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 24, 4, __value)
#define CHANNEL_SWITCH_GET_DEST_BW(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 28, 4)
#define CHANNEL_SWITCH_SET_DEST_BW(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 28, 4, __value)
#define CHANNEL_SWITCH_GET_DEST_CH(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 8)
#define CHANNEL_SWITCH_SET_DEST_CH(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 8, __value)
#define CHANNEL_SWITCH_GET_NORMAL_PERIOD(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 8, 8)
#define CHANNEL_SWITCH_SET_NORMAL_PERIOD(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 8, 8, __value)
#define CHANNEL_SWITCH_GET_SLOW_PERIOD(__h2c)                                  \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 16, 8)
#define CHANNEL_SWITCH_SET_SLOW_PERIOD(__h2c, __value)                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 16, 8, __value)
#define CHANNEL_SWITCH_GET_NORMAL_CYCLE(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 24, 8)
#define CHANNEL_SWITCH_SET_NORMAL_CYCLE(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 24, 8, __value)
#define CHANNEL_SWITCH_GET_TSF_HIGH(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X10, 0, 32)
#define CHANNEL_SWITCH_SET_TSF_HIGH(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 0, 32, __value)
#define CHANNEL_SWITCH_GET_TSF_LOW(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X14, 0, 32)
#define CHANNEL_SWITCH_SET_TSF_LOW(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 0, 32, __value)
#define CHANNEL_SWITCH_GET_CHANNEL_INFO_SIZE(__h2c)                            \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 0, 16)
#define CHANNEL_SWITCH_SET_CHANNEL_INFO_SIZE(__h2c, __value)                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 0, 16, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_FUNC_EN(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 1)
#define UPDATE_BEACON_PARSING_INFO_SET_FUNC_EN(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 1, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_SIZE_TH(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 4)
#define UPDATE_BEACON_PARSING_INFO_SET_SIZE_TH(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 4, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_TIMEOUT(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 12, 4)
#define UPDATE_BEACON_PARSING_INFO_SET_TIMEOUT(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 12, 4, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_0(__h2c)                      \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_0(__h2c, __value)             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 32, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_1(__h2c)                      \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_1(__h2c, __value)             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 0, 32, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_2(__h2c)                      \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_2(__h2c, __value)             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 0, 32, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_3(__h2c)                      \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_3(__h2c, __value)             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 0, 32, __value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_4(__h2c)                      \
	LE_BITS_TO_4BYTE(__h2c + 0X1C, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_4(__h2c, __value)             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X1C, 0, 32, __value)
#define CFG_PARAMETER_GET_NUM(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 16)
#define CFG_PARAMETER_SET_NUM(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 16, __value)
#define CFG_PARAMETER_GET_INIT_CASE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 1)
#define CFG_PARAMETER_SET_INIT_CASE(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 1, __value)
#define CFG_PARAMETER_GET_PHY_PARAMETER_LOC(__h2c)                             \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 24, 8)
#define CFG_PARAMETER_SET_PHY_PARAMETER_LOC(__h2c, __value)                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 24, 8, __value)
#define UPDATE_DATAPACK_GET_SIZE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 16)
#define UPDATE_DATAPACK_SET_SIZE(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 16, __value)
#define UPDATE_DATAPACK_GET_DATAPACK_ID(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 8)
#define UPDATE_DATAPACK_SET_DATAPACK_ID(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 8, __value)
#define UPDATE_DATAPACK_GET_DATAPACK_LOC(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 24, 8)
#define UPDATE_DATAPACK_SET_DATAPACK_LOC(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 24, 8, __value)
#define UPDATE_DATAPACK_GET_DATAPACK_SEGMENT(__h2c)                            \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 8)
#define UPDATE_DATAPACK_SET_DATAPACK_SEGMENT(__h2c, __value)                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 8, __value)
#define UPDATE_DATAPACK_GET_END_SEGMENT(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 8, 1)
#define UPDATE_DATAPACK_SET_END_SEGMENT(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 8, 1, __value)
#define RUN_DATAPACK_GET_DATAPACK_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 8)
#define RUN_DATAPACK_SET_DATAPACK_ID(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 8, __value)
#define DOWNLOAD_FLASH_GET_SPI_CMD(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 8)
#define DOWNLOAD_FLASH_SET_SPI_CMD(__h2c, __value)                             \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 8, __value)
#define DOWNLOAD_FLASH_GET_LOCATION(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 16)
#define DOWNLOAD_FLASH_SET_LOCATION(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 16, __value)
#define DOWNLOAD_FLASH_GET_SIZE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 32)
#define DOWNLOAD_FLASH_SET_SIZE(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 32, __value)
#define DOWNLOAD_FLASH_GET_START_ADDR(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 0, 32)
#define DOWNLOAD_FLASH_SET_START_ADDR(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 0, 32, __value)
#define UPDATE_PACKET_GET_SIZE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 16)
#define UPDATE_PACKET_SET_SIZE(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 16, __value)
#define UPDATE_PACKET_GET_PACKET_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 8)
#define UPDATE_PACKET_SET_PACKET_ID(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 8, __value)
#define UPDATE_PACKET_GET_PACKET_LOC(__h2c)                                    \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 24, 8)
#define UPDATE_PACKET_SET_PACKET_LOC(__h2c, __value)                           \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 24, 8, __value)
#define GENERAL_INFO_GET_REF_TYPE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 8)
#define GENERAL_INFO_SET_REF_TYPE(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 8, __value)
#define GENERAL_INFO_GET_RF_TYPE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 9)
#define GENERAL_INFO_SET_RF_TYPE(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 9, __value)
#define GENERAL_INFO_GET_FW_TX_BOUNDARY(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 8)
#define GENERAL_INFO_SET_FW_TX_BOUNDARY(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 8, __value)
#define IQK_GET_CLEAR(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 1)
#define IQK_SET_CLEAR(__h2c, __value)                                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 1, __value)
#define IQK_GET_SEGMENT_IQK(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 1, 1)
#define IQK_SET_SEGMENT_IQK(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 1, 1, __value)
#define POWER_TRACKING_GET_ENABLE_A(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 1)
#define POWER_TRACKING_SET_ENABLE_A(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 1, __value)
#define POWER_TRACKING_GET_ENABLE_B(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 1, 1)
#define POWER_TRACKING_SET_ENABLE_B(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 1, 1, __value)
#define POWER_TRACKING_GET_ENABLE_C(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 2, 1)
#define POWER_TRACKING_SET_ENABLE_C(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 2, 1, __value)
#define POWER_TRACKING_GET_ENABLE_D(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 3, 1)
#define POWER_TRACKING_SET_ENABLE_D(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 3, 1, __value)
#define POWER_TRACKING_GET_TYPE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 4, 3)
#define POWER_TRACKING_SET_TYPE(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 4, 3, __value)
#define POWER_TRACKING_GET_BBSWING_INDEX(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 8)
#define POWER_TRACKING_SET_BBSWING_INDEX(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 8, __value)
#define POWER_TRACKING_GET_TX_PWR_INDEX_A(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 8)
#define POWER_TRACKING_SET_TX_PWR_INDEX_A(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 8, __value)
#define POWER_TRACKING_GET_PWR_TRACKING_OFFSET_VALUE_A(__h2c)                  \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 8, 8)
#define POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_A(__h2c, __value)         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 8, 8, __value)
#define POWER_TRACKING_GET_TSSI_VALUE_A(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 16, 8)
#define POWER_TRACKING_SET_TSSI_VALUE_A(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 16, 8, __value)
#define POWER_TRACKING_GET_TX_PWR_INDEX_B(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 0, 8)
#define POWER_TRACKING_SET_TX_PWR_INDEX_B(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 0, 8, __value)
#define POWER_TRACKING_GET_PWR_TRACKING_OFFSET_VALUE_B(__h2c)                  \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 8, 8)
#define POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_B(__h2c, __value)         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 8, 8, __value)
#define POWER_TRACKING_GET_TSSI_VALUE_B(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 16, 8)
#define POWER_TRACKING_SET_TSSI_VALUE_B(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 16, 8, __value)
#define POWER_TRACKING_GET_TX_PWR_INDEX_C(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 0, 8)
#define POWER_TRACKING_SET_TX_PWR_INDEX_C(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 0, 8, __value)
#define POWER_TRACKING_GET_PWR_TRACKING_OFFSET_VALUE_C(__h2c)                  \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 8, 8)
#define POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_C(__h2c, __value)         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 8, 8, __value)
#define POWER_TRACKING_GET_TSSI_VALUE_C(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 16, 8)
#define POWER_TRACKING_SET_TSSI_VALUE_C(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 16, 8, __value)
#define POWER_TRACKING_GET_TX_PWR_INDEX_D(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 0, 8)
#define POWER_TRACKING_SET_TX_PWR_INDEX_D(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 0, 8, __value)
#define POWER_TRACKING_GET_PWR_TRACKING_OFFSET_VALUE_D(__h2c)                  \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 8, 8)
#define POWER_TRACKING_SET_PWR_TRACKING_OFFSET_VALUE_D(__h2c, __value)         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 8, 8, __value)
#define POWER_TRACKING_GET_TSSI_VALUE_D(__h2c)                                 \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 16, 8)
#define POWER_TRACKING_SET_TSSI_VALUE_D(__h2c, __value)                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 16, 8, __value)
#define PSD_GET_START_PSD(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 16)
#define PSD_SET_START_PSD(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 16, __value)
#define PSD_GET_END_PSD(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 16)
#define PSD_SET_END_PSD(__h2c, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 16, __value)
#define P2PPS_GET_OFFLOAD_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 1)
#define P2PPS_SET_OFFLOAD_EN(__h2c, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 1, __value)
#define P2PPS_GET_ROLE(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 1, 1)
#define P2PPS_SET_ROLE(__h2c, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 1, 1, __value)
#define P2PPS_GET_CTWINDOW_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 2, 1)
#define P2PPS_SET_CTWINDOW_EN(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 2, 1, __value)
#define P2PPS_GET_NOA_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 3, 1)
#define P2PPS_SET_NOA_EN(__h2c, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 3, 1, __value)
#define P2PPS_GET_NOA_SEL(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 4, 1)
#define P2PPS_SET_NOA_SEL(__h2c, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 4, 1, __value)
#define P2PPS_GET_ALLSTASLEEP(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 5, 1)
#define P2PPS_SET_ALLSTASLEEP(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 5, 1, __value)
#define P2PPS_GET_DISCOVERY(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 6, 1)
#define P2PPS_SET_DISCOVERY(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 6, 1, __value)
#define P2PPS_GET_P2P_PORT_ID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 8)
#define P2PPS_SET_P2P_PORT_ID(__h2c, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 8, __value)
#define P2PPS_GET_P2P_GROUP(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 8)
#define P2PPS_SET_P2P_GROUP(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 8, __value)
#define P2PPS_GET_P2P_MACID(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 24, 8)
#define P2PPS_SET_P2P_MACID(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 24, 8, __value)
#define P2PPS_GET_CTWINDOW_LENGTH(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 8)
#define P2PPS_SET_CTWINDOW_LENGTH(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 8, __value)
#define P2PPS_GET_NOA_DURATION_PARA(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X10, 0, 32)
#define P2PPS_SET_NOA_DURATION_PARA(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 0, 32, __value)
#define P2PPS_GET_NOA_INTERVAL_PARA(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X14, 0, 32)
#define P2PPS_SET_NOA_INTERVAL_PARA(__h2c, __value)                            \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 0, 32, __value)
#define P2PPS_GET_NOA_START_TIME_PARA(__h2c)                                   \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 0, 32)
#define P2PPS_SET_NOA_START_TIME_PARA(__h2c, __value)                          \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 0, 32, __value)
#define P2PPS_GET_NOA_COUNT_PARA(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X1C, 0, 32)
#define P2PPS_SET_NOA_COUNT_PARA(__h2c, __value)                               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X1C, 0, 32, __value)
#define BT_COEX_GET_DATA_START(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 8)
#define BT_COEX_SET_DATA_START(__h2c, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 8, __value)
#define NAN_CTRL_GET_NAN_EN(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 2)
#define NAN_CTRL_SET_NAN_EN(__h2c, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 2, __value)
#define NAN_CTRL_GET_SUPPORT_BAND(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 2)
#define NAN_CTRL_SET_SUPPORT_BAND(__h2c, __value)                              \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 2, __value)
#define NAN_CTRL_GET_DISABLE_2G_DISC_BCN(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 10, 1)
#define NAN_CTRL_SET_DISABLE_2G_DISC_BCN(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 10, 1, __value)
#define NAN_CTRL_GET_DISABLE_5G_DISC_BCN(__h2c)                                \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 11, 1)
#define NAN_CTRL_SET_DISABLE_5G_DISC_BCN(__h2c, __value)                       \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 11, 1, __value)
#define NAN_CTRL_GET_BCN_RSVD_PAGE_OFFSET(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 16, 8)
#define NAN_CTRL_SET_BCN_RSVD_PAGE_OFFSET(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 16, 8, __value)
#define NAN_CTRL_GET_CHANNEL_2G(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X08, 24, 8)
#define NAN_CTRL_SET_CHANNEL_2G(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 24, 8, __value)
#define NAN_CTRL_GET_CHANNEL_5G(__h2c) LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 8)
#define NAN_CTRL_SET_CHANNEL_5G(__h2c, __value)                                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 8, __value)
#define NAN_CHANNEL_PLAN_0_GET_CHANNEL_NUMBER_0(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 8)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_0(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 8, __value)
#define NAN_CHANNEL_PLAN_0_GET_UNPAUSE_MACID_0(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 8)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_0(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 8, __value)
#define NAN_CHANNEL_PLAN_0_GET_START_TIME_SLOT_0(__h2c)                        \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 16)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_0(__h2c, __value)               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 16, __value)
#define NAN_CHANNEL_PLAN_0_GET_DURATION_0(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 16, 16)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_0(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 16, 16, __value)
#define NAN_CHANNEL_PLAN_0_GET_CHANNEL_NUMBER_1(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 0, 8)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_1(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 0, 8, __value)
#define NAN_CHANNEL_PLAN_0_GET_UNPAUSE_MACID_1(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 8, 8)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_1(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 8, 8, __value)
#define NAN_CHANNEL_PLAN_0_GET_START_TIME_SLOT_1(__h2c)                        \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 0, 16)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_1(__h2c, __value)               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 0, 16, __value)
#define NAN_CHANNEL_PLAN_0_GET_DURATION_1(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 16, 16)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_1(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 16, 16, __value)
#define NAN_CHANNEL_PLAN_0_GET_CHANNEL_NUMBER_2(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 0, 8)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_2(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 0, 8, __value)
#define NAN_CHANNEL_PLAN_0_GET_UNPAUSE_MACID_2(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 8, 8)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_2(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 8, 8, __value)
#define NAN_CHANNEL_PLAN_0_GET_START_TIME_SLOT_2(__h2c)                        \
	LE_BITS_TO_4BYTE(__h2c + 0X1C, 0, 16)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_2(__h2c, __value)               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X1C, 0, 16, __value)
#define NAN_CHANNEL_PLAN_0_GET_DURATION_2(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X1C, 16, 16)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_2(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X1C, 16, 16, __value)
#define NAN_CHANNEL_PLAN_1_GET_CHANNEL_NUMBER_3(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 0, 8)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_3(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 0, 8, __value)
#define NAN_CHANNEL_PLAN_1_GET_UNPAUSE_MACID_3(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X08, 8, 8)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_3(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X08, 8, 8, __value)
#define NAN_CHANNEL_PLAN_1_GET_START_TIME_SLOT_3(__h2c)                        \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 0, 16)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_3(__h2c, __value)               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 0, 16, __value)
#define NAN_CHANNEL_PLAN_1_GET_DURATION_3(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X0C, 16, 16)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_3(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X0C, 16, 16, __value)
#define NAN_CHANNEL_PLAN_1_GET_CHANNEL_NUMBER_4(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 0, 8)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_4(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 0, 8, __value)
#define NAN_CHANNEL_PLAN_1_GET_UNPAUSE_MACID_4(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X10, 8, 8)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_4(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X10, 8, 8, __value)
#define NAN_CHANNEL_PLAN_1_GET_START_TIME_SLOT_4(__h2c)                        \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 0, 16)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_4(__h2c, __value)               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 0, 16, __value)
#define NAN_CHANNEL_PLAN_1_GET_DURATION_4(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X14, 16, 16)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_4(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X14, 16, 16, __value)
#define NAN_CHANNEL_PLAN_1_GET_CHANNEL_NUMBER_5(__h2c)                         \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 0, 8)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_5(__h2c, __value)                \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 0, 8, __value)
#define NAN_CHANNEL_PLAN_1_GET_UNPAUSE_MACID_5(__h2c)                          \
	LE_BITS_TO_4BYTE(__h2c + 0X18, 8, 8)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_5(__h2c, __value)                 \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X18, 8, 8, __value)
#define NAN_CHANNEL_PLAN_1_GET_START_TIME_SLOT_5(__h2c)                        \
	LE_BITS_TO_4BYTE(__h2c + 0X1C, 0, 16)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_5(__h2c, __value)               \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X1C, 0, 16, __value)
#define NAN_CHANNEL_PLAN_1_GET_DURATION_5(__h2c)                               \
	LE_BITS_TO_4BYTE(__h2c + 0X1C, 16, 16)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_5(__h2c, __value)                      \
	SET_BITS_TO_LE_4BYTE(__h2c + 0X1C, 16, 16, __value)
#endif

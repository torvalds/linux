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

#ifndef _HAL_FWOFFLOADH2CFORMAT_H2C_C2H_AP_H_
#define _HAL_FWOFFLOADH2CFORMAT_H2C_C2H_AP_H_
#define CMD_ID_FW_OFFLOAD_H2C 0XFF
#define CMD_ID_FW_ACCESS_TEST 0XFF
#define CMD_ID_CH_SWITCH 0XFF
#define CMD_ID_DUMP_PHYSICAL_EFUSE 0XFF
#define CMD_ID_UPDATE_BEACON_PARSING_INFO 0XFF
#define CMD_ID_CFG_PARAM 0XFF
#define CMD_ID_UPDATE_DATAPACK 0XFF
#define CMD_ID_RUN_DATAPACK 0XFF
#define CMD_ID_DOWNLOAD_FLASH 0XFF
#define CMD_ID_UPDATE_PKT 0XFF
#define CMD_ID_GENERAL_INFO 0XFF
#define CMD_ID_IQK 0XFF
#define CMD_ID_PWR_TRK 0XFF
#define CMD_ID_PSD 0XFF
#define CMD_ID_PHYDM_INFO 0XFF
#define CMD_ID_FW_SNDING 0XFF
#define CMD_ID_FW_FWCTRL 0XFF
#define CMD_ID_H2C_LOOPBACK 0XFF
#define CMD_ID_FWCMD_LOOPBACK 0XFF
#define CMD_ID_SEND_SCAN_PKT 0XFF
#define CMD_ID_BCN_OFFLOAD 0XFF
#define CMD_ID_DROP_SCAN_PKT 0XFF
#define CMD_ID_P2PPS 0XFF
#define CMD_ID_BT_COEX 0XFF
#define CMD_ID_ACT_SCHEDULE_REQ 0XFF
#define CMD_ID_NAN_CTRL 0XFF
#define CMD_ID_NAN_CHANNEL_PLAN_0 0XFF
#define CMD_ID_NAN_CHANNEL_PLAN_1 0XFF
#define CMD_ID_NAN_FUNC_CTRL 0XFF
#define CATEGORY_H2C_CMD_HEADER 0X00
#define CATEGORY_FW_OFFLOAD_H2C 0X01
#define CATEGORY_FW_ACCESS_TEST 0X01
#define CATEGORY_CH_SWITCH 0X01
#define CATEGORY_DUMP_PHYSICAL_EFUSE 0X01
#define CATEGORY_UPDATE_BEACON_PARSING_INFO 0X01
#define CATEGORY_CFG_PARAM 0X01
#define CATEGORY_UPDATE_DATAPACK 0X01
#define CATEGORY_RUN_DATAPACK 0X01
#define CATEGORY_DOWNLOAD_FLASH 0X01
#define CATEGORY_UPDATE_PKT 0X01
#define CATEGORY_GENERAL_INFO 0X01
#define CATEGORY_IQK 0X01
#define CATEGORY_PWR_TRK 0X01
#define CATEGORY_PSD 0X01
#define CATEGORY_PHYDM_INFO 0X01
#define CATEGORY_FW_SNDING 0X01
#define CATEGORY_FW_FWCTRL 0X01
#define CATEGORY_H2C_LOOPBACK 0X01
#define CATEGORY_FWCMD_LOOPBACK 0X01
#define CATEGORY_SEND_SCAN_PKT 0X01
#define CATEGORY_BCN_OFFLOAD 0X01
#define CATEGORY_DROP_SCAN_PKT 0X01
#define CATEGORY_P2PPS 0X01
#define CATEGORY_BT_COEX 0X01
#define CATEGORY_ACT_SCHEDULE_REQ 0X01
#define CATEGORY_NAN_CTRL 0X01
#define CATEGORY_NAN_CHANNEL_PLAN_0 0X01
#define CATEGORY_NAN_CHANNEL_PLAN_1 0X01
#define CATEGORY_NAN_FUNC_CTRL 0X01
#define SUB_CMD_ID_FW_ACCESS_TEST 0X00
#define SUB_CMD_ID_CH_SWITCH 0X02
#define SUB_CMD_ID_DUMP_PHYSICAL_EFUSE 0X03
#define SUB_CMD_ID_UPDATE_BEACON_PARSING_INFO 0X05
#define SUB_CMD_ID_CFG_PARAM 0X08
#define SUB_CMD_ID_UPDATE_DATAPACK 0X09
#define SUB_CMD_ID_RUN_DATAPACK 0X0A
#define SUB_CMD_ID_DOWNLOAD_FLASH 0X0B
#define SUB_CMD_ID_UPDATE_PKT 0X0C
#define SUB_CMD_ID_GENERAL_INFO 0X0D
#define SUB_CMD_ID_IQK 0X0E
#define SUB_CMD_ID_PWR_TRK 0X0F
#define SUB_CMD_ID_PSD 0X10
#define SUB_CMD_ID_PHYDM_INFO 0X11
#define SUB_CMD_ID_FW_SNDING 0X12
#define SUB_CMD_ID_FW_FWCTRL 0X13
#define SUB_CMD_ID_H2C_LOOPBACK 0X14
#define SUB_CMD_ID_FWCMD_LOOPBACK 0X15
#define SUB_CMD_ID_SEND_SCAN_PKT 0X16
#define SUB_CMD_ID_BCN_OFFLOAD 0X17
#define SUB_CMD_ID_DROP_SCAN_PKT 0X18
#define SUB_CMD_ID_P2PPS 0X24
#define SUB_CMD_ID_BT_COEX 0X60
#define SUB_CMD_ID_ACT_SCHEDULE_REQ 0X70
#define SUB_CMD_ID_NAN_CTRL 0XB2
#define SUB_CMD_ID_NAN_CHANNEL_PLAN_0 0XB4
#define SUB_CMD_ID_NAN_CHANNEL_PLAN_1 0XB5
#define SUB_CMD_ID_NAN_FUNC_CTRL 0XB6
#define H2C_CMD_HEADER_GET_CATEGORY(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 7)
#define H2C_CMD_HEADER_SET_CATEGORY(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 7, value)
#define H2C_CMD_HEADER_SET_CATEGORY_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 7, value)
#define H2C_CMD_HEADER_GET_ACK(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 7, 1)
#define H2C_CMD_HEADER_SET_ACK(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 7, 1, value)
#define H2C_CMD_HEADER_SET_ACK_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 7, 1, value)
#define H2C_CMD_HEADER_GET_TOTAL_LEN(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 16)
#define H2C_CMD_HEADER_SET_TOTAL_LEN(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 16, value)
#define H2C_CMD_HEADER_SET_TOTAL_LEN_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 16, value)
#define H2C_CMD_HEADER_GET_SEQ_NUM(h2c_pkt)                                    \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 16)
#define H2C_CMD_HEADER_SET_SEQ_NUM(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 16, value)
#define H2C_CMD_HEADER_SET_SEQ_NUM_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 16, value)
#define FW_OFFLOAD_H2C_GET_CATEGORY(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 0, 7)
#define FW_OFFLOAD_H2C_SET_CATEGORY(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 0, 7, value)
#define FW_OFFLOAD_H2C_SET_CATEGORY_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 0, 7, value)
#define FW_OFFLOAD_H2C_GET_ACK(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 7, 1)
#define FW_OFFLOAD_H2C_SET_ACK(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 7, 1, value)
#define FW_OFFLOAD_H2C_SET_ACK_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 7, 1, value)
#define FW_OFFLOAD_H2C_GET_CMD_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X00, 8, 8)
#define FW_OFFLOAD_H2C_SET_CMD_ID(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 8, 8, value)
#define FW_OFFLOAD_H2C_SET_CMD_ID_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 8, 8, value)
#define FW_OFFLOAD_H2C_GET_SUB_CMD_ID(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X00, 16, 16)
#define FW_OFFLOAD_H2C_SET_SUB_CMD_ID(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X00, 16, 16, value)
#define FW_OFFLOAD_H2C_SET_SUB_CMD_ID_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X00, 16, 16, value)
#define FW_OFFLOAD_H2C_GET_TOTAL_LEN(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X04, 0, 16)
#define FW_OFFLOAD_H2C_SET_TOTAL_LEN(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 0, 16, value)
#define FW_OFFLOAD_H2C_SET_TOTAL_LEN_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 0, 16, value)
#define FW_OFFLOAD_H2C_GET_SEQ_NUM(h2c_pkt)                                    \
	GET_H2C_FIELD(h2c_pkt + 0X04, 16, 16)
#define FW_OFFLOAD_H2C_SET_SEQ_NUM(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X04, 16, 16, value)
#define FW_OFFLOAD_H2C_SET_SEQ_NUM_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X04, 16, 16, value)
#define FW_ACCESS_TEST_GET_ACCESS_TXFF(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define FW_ACCESS_TEST_SET_ACCESS_TXFF(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_TXFF_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_RXFF(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define FW_ACCESS_TEST_SET_ACCESS_RXFF(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_RXFF_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_FWFF(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X08, 2, 1)
#define FW_ACCESS_TEST_SET_ACCESS_FWFF(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 2, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_FWFF_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 2, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PHYFF(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 3, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PHYFF(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 3, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PHYFF_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 3, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_RPT_BUF(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X08, 4, 1)
#define FW_ACCESS_TEST_SET_ACCESS_RPT_BUF(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 4, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_RPT_BUF_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 4, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_CAM(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X08, 5, 1)
#define FW_ACCESS_TEST_SET_ACCESS_CAM(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 5, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_CAM_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 5, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_WOW_CAM(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X08, 6, 1)
#define FW_ACCESS_TEST_SET_ACCESS_WOW_CAM(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 6, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_WOW_CAM_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 6, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_RX_CAM(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X08, 7, 1)
#define FW_ACCESS_TEST_SET_ACCESS_RX_CAM(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 7, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_RX_CAM_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 7, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_BA_CAM(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X08, 8, 1)
#define FW_ACCESS_TEST_SET_ACCESS_BA_CAM(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_BA_CAM_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_MBSSID_CAM(h2c_pkt)                          \
	GET_H2C_FIELD(h2c_pkt + 0X08, 9, 1)
#define FW_ACCESS_TEST_SET_ACCESS_MBSSID_CAM(h2c_pkt, value)                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 9, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_MBSSID_CAM_NO_CLR(h2c_pkt, value)            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 9, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE0(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE0(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE0_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE1(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 17, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE1(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 17, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE1_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 17, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE2(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 18, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE2(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 18, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE2_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 18, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE3(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 19, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE3(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 19, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE3_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 19, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE4(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 20, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE4(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 20, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE4_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 20, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE5(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 21, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE5(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 21, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE5_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 21, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE6(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 22, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE6(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 22, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE6_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 22, 1, value)
#define FW_ACCESS_TEST_GET_ACCESS_PAGE7(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 23, 1)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE7(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 23, 1, value)
#define FW_ACCESS_TEST_SET_ACCESS_PAGE7_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 23, 1, value)
#define CH_SWITCH_GET_START(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define CH_SWITCH_SET_START(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define CH_SWITCH_SET_START_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define CH_SWITCH_GET_DEST_CH_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define CH_SWITCH_SET_DEST_CH_EN(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define CH_SWITCH_SET_DEST_CH_EN_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define CH_SWITCH_GET_ABSOLUTE_TIME(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 2, 1)
#define CH_SWITCH_SET_ABSOLUTE_TIME(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 2, 1, value)
#define CH_SWITCH_SET_ABSOLUTE_TIME_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 2, 1, value)
#define CH_SWITCH_GET_PERIODIC_OPT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 3, 2)
#define CH_SWITCH_SET_PERIODIC_OPT(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 3, 2, value)
#define CH_SWITCH_SET_PERIODIC_OPT_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 3, 2, value)
#define CH_SWITCH_GET_SCAN_MODE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 5, 1)
#define CH_SWITCH_SET_SCAN_MODE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 5, 1, value)
#define CH_SWITCH_SET_SCAN_MODE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 5, 1, value)
#define CH_SWITCH_GET_INFO_LOC(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define CH_SWITCH_SET_INFO_LOC(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define CH_SWITCH_SET_INFO_LOC_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define CH_SWITCH_GET_CH_NUM(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define CH_SWITCH_SET_CH_NUM(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define CH_SWITCH_SET_CH_NUM_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define CH_SWITCH_GET_PRI_CH_IDX(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 24, 4)
#define CH_SWITCH_SET_PRI_CH_IDX(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 4, value)
#define CH_SWITCH_SET_PRI_CH_IDX_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 4, value)
#define CH_SWITCH_GET_DEST_BW(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 28, 4)
#define CH_SWITCH_SET_DEST_BW(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 28, 4, value)
#define CH_SWITCH_SET_DEST_BW_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 28, 4, value)
#define CH_SWITCH_GET_DEST_CH(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 8)
#define CH_SWITCH_SET_DEST_CH(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define CH_SWITCH_SET_DEST_CH_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define CH_SWITCH_GET_NORMAL_PERIOD(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 8, 6)
#define CH_SWITCH_SET_NORMAL_PERIOD(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 8, 6, value)
#define CH_SWITCH_SET_NORMAL_PERIOD_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 8, 6, value)
#define CH_SWITCH_GET_NORMAL_PERIOD_SEL(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 14, 2)
#define CH_SWITCH_SET_NORMAL_PERIOD_SEL(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 14, 2, value)
#define CH_SWITCH_SET_NORMAL_PERIOD_SEL_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 14, 2, value)
#define CH_SWITCH_GET_SLOW_PERIOD(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 16, 6)
#define CH_SWITCH_SET_SLOW_PERIOD(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 16, 6, value)
#define CH_SWITCH_SET_SLOW_PERIOD_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 16, 6, value)
#define CH_SWITCH_GET_SLOW_PERIOD_SEL(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 22, 2)
#define CH_SWITCH_SET_SLOW_PERIOD_SEL(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 22, 2, value)
#define CH_SWITCH_SET_SLOW_PERIOD_SEL_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 22, 2, value)
#define CH_SWITCH_GET_NORMAL_CYCLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 24, 8)
#define CH_SWITCH_SET_NORMAL_CYCLE(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 24, 8, value)
#define CH_SWITCH_SET_NORMAL_CYCLE_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 24, 8, value)
#define CH_SWITCH_GET_TSF_HIGH(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X10, 0, 32)
#define CH_SWITCH_SET_TSF_HIGH(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 32, value)
#define CH_SWITCH_SET_TSF_HIGH_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 32, value)
#define CH_SWITCH_GET_TSF_LOW(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X14, 0, 32)
#define CH_SWITCH_SET_TSF_LOW(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 0, 32, value)
#define CH_SWITCH_SET_TSF_LOW_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 0, 32, value)
#define CH_SWITCH_GET_INFO_SIZE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X18, 0, 16)
#define CH_SWITCH_SET_INFO_SIZE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 0, 16, value)
#define CH_SWITCH_SET_INFO_SIZE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 0, 16, value)
#define UPDATE_BEACON_PARSING_INFO_GET_FUNC_EN(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define UPDATE_BEACON_PARSING_INFO_SET_FUNC_EN(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define UPDATE_BEACON_PARSING_INFO_SET_FUNC_EN_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define UPDATE_BEACON_PARSING_INFO_GET_SIZE_TH(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X08, 8, 4)
#define UPDATE_BEACON_PARSING_INFO_SET_SIZE_TH(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 4, value)
#define UPDATE_BEACON_PARSING_INFO_SET_SIZE_TH_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 4, value)
#define UPDATE_BEACON_PARSING_INFO_GET_TIMEOUT(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X08, 12, 4)
#define UPDATE_BEACON_PARSING_INFO_SET_TIMEOUT(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 12, 4, value)
#define UPDATE_BEACON_PARSING_INFO_SET_TIMEOUT_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 12, 4, value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_0(h2c_pkt)                    \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_0(h2c_pkt, value)             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_0_NO_CLR(h2c_pkt, value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_1(h2c_pkt)                    \
	GET_H2C_FIELD(h2c_pkt + 0X10, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_1(h2c_pkt, value)             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_1_NO_CLR(h2c_pkt, value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_2(h2c_pkt)                    \
	GET_H2C_FIELD(h2c_pkt + 0X14, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_2(h2c_pkt, value)             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_2_NO_CLR(h2c_pkt, value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_3(h2c_pkt)                    \
	GET_H2C_FIELD(h2c_pkt + 0X18, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_3(h2c_pkt, value)             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_3_NO_CLR(h2c_pkt, value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_GET_IE_ID_BMP_4(h2c_pkt)                    \
	GET_H2C_FIELD(h2c_pkt + 0X1C, 0, 32)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_4(h2c_pkt, value)             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X1C, 0, 32, value)
#define UPDATE_BEACON_PARSING_INFO_SET_IE_ID_BMP_4_NO_CLR(h2c_pkt, value)      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X1C, 0, 32, value)
#define CFG_PARAM_GET_NUM(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 16)
#define CFG_PARAM_SET_NUM(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 16, value)
#define CFG_PARAM_SET_NUM_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 16, value)
#define CFG_PARAM_GET_INIT_CASE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 1)
#define CFG_PARAM_SET_INIT_CASE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 1, value)
#define CFG_PARAM_SET_INIT_CASE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 1, value)
#define CFG_PARAM_GET_LOC(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define CFG_PARAM_SET_LOC(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define CFG_PARAM_SET_LOC_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define UPDATE_DATAPACK_GET_SIZE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 16)
#define UPDATE_DATAPACK_SET_SIZE(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 16, value)
#define UPDATE_DATAPACK_SET_SIZE_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 16, value)
#define UPDATE_DATAPACK_GET_DATAPACK_ID(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define UPDATE_DATAPACK_SET_DATAPACK_ID(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define UPDATE_DATAPACK_SET_DATAPACK_ID_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define UPDATE_DATAPACK_GET_DATAPACK_LOC(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define UPDATE_DATAPACK_SET_DATAPACK_LOC(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define UPDATE_DATAPACK_SET_DATAPACK_LOC_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define UPDATE_DATAPACK_GET_DATAPACK_SEGMENT(h2c_pkt)                          \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 8)
#define UPDATE_DATAPACK_SET_DATAPACK_SEGMENT(h2c_pkt, value)                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define UPDATE_DATAPACK_SET_DATAPACK_SEGMENT_NO_CLR(h2c_pkt, value)            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define UPDATE_DATAPACK_GET_END_SEGMENT(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 8, 1)
#define UPDATE_DATAPACK_SET_END_SEGMENT(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 8, 1, value)
#define UPDATE_DATAPACK_SET_END_SEGMENT_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 8, 1, value)
#define RUN_DATAPACK_GET_DATAPACK_ID(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define RUN_DATAPACK_SET_DATAPACK_ID(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define RUN_DATAPACK_SET_DATAPACK_ID_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define DOWNLOAD_FLASH_GET_SPI_CMD(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define DOWNLOAD_FLASH_SET_SPI_CMD(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define DOWNLOAD_FLASH_SET_SPI_CMD_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define DOWNLOAD_FLASH_GET_LOCATION(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X08, 8, 16)
#define DOWNLOAD_FLASH_SET_LOCATION(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 16, value)
#define DOWNLOAD_FLASH_SET_LOCATION_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 16, value)
#define DOWNLOAD_FLASH_GET_SIZE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 32)
#define DOWNLOAD_FLASH_SET_SIZE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 32, value)
#define DOWNLOAD_FLASH_SET_SIZE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 32, value)
#define DOWNLOAD_FLASH_GET_START_ADDR(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X10, 0, 32)
#define DOWNLOAD_FLASH_SET_START_ADDR(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 32, value)
#define DOWNLOAD_FLASH_SET_START_ADDR_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 32, value)
#define UPDATE_PKT_GET_SIZE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 16)
#define UPDATE_PKT_SET_SIZE(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 16, value)
#define UPDATE_PKT_SET_SIZE_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 16, value)
#define UPDATE_PKT_GET_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define UPDATE_PKT_SET_ID(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define UPDATE_PKT_SET_ID_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define UPDATE_PKT_GET_LOC(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define UPDATE_PKT_SET_LOC(h2c_pkt, value)                                     \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define UPDATE_PKT_SET_LOC_NO_CLR(h2c_pkt, value)                              \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define GENERAL_INFO_GET_FW_TX_BOUNDARY(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define GENERAL_INFO_SET_FW_TX_BOUNDARY(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define GENERAL_INFO_SET_FW_TX_BOUNDARY_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define IQK_GET_CLEAR(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define IQK_SET_CLEAR(h2c_pkt, value)                                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define IQK_SET_CLEAR_NO_CLR(h2c_pkt, value)                                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define IQK_GET_SEGMENT_IQK(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define IQK_SET_SEGMENT_IQK(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define IQK_SET_SEGMENT_IQK_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define PWR_TRK_GET_ENABLE_A(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define PWR_TRK_SET_ENABLE_A(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define PWR_TRK_SET_ENABLE_A_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define PWR_TRK_GET_ENABLE_B(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define PWR_TRK_SET_ENABLE_B(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define PWR_TRK_SET_ENABLE_B_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define PWR_TRK_GET_ENABLE_C(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 2, 1)
#define PWR_TRK_SET_ENABLE_C(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 2, 1, value)
#define PWR_TRK_SET_ENABLE_C_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 2, 1, value)
#define PWR_TRK_GET_ENABLE_D(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 3, 1)
#define PWR_TRK_SET_ENABLE_D(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 3, 1, value)
#define PWR_TRK_SET_ENABLE_D_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 3, 1, value)
#define PWR_TRK_GET_TYPE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 4, 3)
#define PWR_TRK_SET_TYPE(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 4, 3, value)
#define PWR_TRK_SET_TYPE_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 4, 3, value)
#define PWR_TRK_GET_BBSWING_INDEX(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define PWR_TRK_SET_BBSWING_INDEX(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define PWR_TRK_SET_BBSWING_INDEX_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define PWR_TRK_GET_TX_PWR_INDEX_A(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 8)
#define PWR_TRK_SET_TX_PWR_INDEX_A(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define PWR_TRK_SET_TX_PWR_INDEX_A_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define PWR_TRK_GET_OFFSET_VALUE_A(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 8, 8)
#define PWR_TRK_SET_OFFSET_VALUE_A(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 8, 8, value)
#define PWR_TRK_SET_OFFSET_VALUE_A_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 8, 8, value)
#define PWR_TRK_GET_TSSI_VALUE_A(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 16, 8)
#define PWR_TRK_SET_TSSI_VALUE_A(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 16, 8, value)
#define PWR_TRK_SET_TSSI_VALUE_A_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 16, 8, value)
#define PWR_TRK_GET_TX_PWR_INDEX_B(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X10, 0, 8)
#define PWR_TRK_SET_TX_PWR_INDEX_B(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 8, value)
#define PWR_TRK_SET_TX_PWR_INDEX_B_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 8, value)
#define PWR_TRK_GET_OFFSET_VALUE_B(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X10, 8, 8)
#define PWR_TRK_SET_OFFSET_VALUE_B(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 8, 8, value)
#define PWR_TRK_SET_OFFSET_VALUE_B_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 8, 8, value)
#define PWR_TRK_GET_TSSI_VALUE_B(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X10, 16, 8)
#define PWR_TRK_SET_TSSI_VALUE_B(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 16, 8, value)
#define PWR_TRK_SET_TSSI_VALUE_B_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 16, 8, value)
#define PWR_TRK_GET_TX_PWR_INDEX_C(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X14, 0, 8)
#define PWR_TRK_SET_TX_PWR_INDEX_C(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 0, 8, value)
#define PWR_TRK_SET_TX_PWR_INDEX_C_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 0, 8, value)
#define PWR_TRK_GET_OFFSET_VALUE_C(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X14, 8, 8)
#define PWR_TRK_SET_OFFSET_VALUE_C(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 8, 8, value)
#define PWR_TRK_SET_OFFSET_VALUE_C_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 8, 8, value)
#define PWR_TRK_GET_TSSI_VALUE_C(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X14, 16, 8)
#define PWR_TRK_SET_TSSI_VALUE_C(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 16, 8, value)
#define PWR_TRK_SET_TSSI_VALUE_C_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 16, 8, value)
#define PWR_TRK_GET_TX_PWR_INDEX_D(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X18, 0, 8)
#define PWR_TRK_SET_TX_PWR_INDEX_D(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 0, 8, value)
#define PWR_TRK_SET_TX_PWR_INDEX_D_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 0, 8, value)
#define PWR_TRK_GET_OFFSET_VALUE_D(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X18, 8, 8)
#define PWR_TRK_SET_OFFSET_VALUE_D(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 8, 8, value)
#define PWR_TRK_SET_OFFSET_VALUE_D_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 8, 8, value)
#define PWR_TRK_GET_TSSI_VALUE_D(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X18, 16, 8)
#define PWR_TRK_SET_TSSI_VALUE_D(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 16, 8, value)
#define PWR_TRK_SET_TSSI_VALUE_D_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 16, 8, value)
#define PSD_GET_START_PSD(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 16)
#define PSD_SET_START_PSD(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 16, value)
#define PSD_SET_START_PSD_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 16, value)
#define PSD_GET_END_PSD(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 16)
#define PSD_SET_END_PSD(h2c_pkt, value)                                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 16, value)
#define PSD_SET_END_PSD_NO_CLR(h2c_pkt, value)                                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 16, value)
#define PHYDM_INFO_GET_REF_TYPE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define PHYDM_INFO_SET_REF_TYPE(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define PHYDM_INFO_SET_REF_TYPE_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define PHYDM_INFO_GET_RF_TYPE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define PHYDM_INFO_SET_RF_TYPE(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define PHYDM_INFO_SET_RF_TYPE_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define PHYDM_INFO_GET_CUT_VER(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define PHYDM_INFO_SET_CUT_VER(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define PHYDM_INFO_SET_CUT_VER_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define PHYDM_INFO_GET_RX_ANT_STATUS(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X08, 24, 4)
#define PHYDM_INFO_SET_RX_ANT_STATUS(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 4, value)
#define PHYDM_INFO_SET_RX_ANT_STATUS_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 4, value)
#define PHYDM_INFO_GET_TX_ANT_STATUS(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X08, 28, 4)
#define PHYDM_INFO_SET_TX_ANT_STATUS(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 28, 4, value)
#define PHYDM_INFO_SET_TX_ANT_STATUS_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 28, 4, value)
#define PHYDM_INFO_GET_EXT_PA(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0XC, 0, 8)
#define PHYDM_INFO_SET_EXT_PA(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 0, 8, value)
#define PHYDM_INFO_SET_EXT_PA_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 0, 8, value)
#define PHYDM_INFO_GET_PACKAGE_TYPE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0XC, 8, 8)
#define PHYDM_INFO_SET_PACKAGE_TYPE(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 8, 8, value)
#define PHYDM_INFO_SET_PACKAGE_TYPE_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 8, 8, value)
#define PHYDM_INFO_GET_MP_MODE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0XC, 16, 1)
#define PHYDM_INFO_SET_MP_MODE(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 16, 1, value)
#define PHYDM_INFO_SET_MP_MODE_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 16, 1, value)
#define FW_SNDING_GET_SU0(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define FW_SNDING_SET_SU0(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define FW_SNDING_SET_SU0_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define FW_SNDING_GET_SU1(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define FW_SNDING_SET_SU1(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define FW_SNDING_SET_SU1_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define FW_SNDING_GET_MU(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 2, 1)
#define FW_SNDING_SET_MU(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 2, 1, value)
#define FW_SNDING_SET_MU_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 2, 1, value)
#define FW_SNDING_GET_PERIOD(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define FW_SNDING_SET_PERIOD(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define FW_SNDING_SET_PERIOD_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define FW_SNDING_GET_NDPA0_HEAD_PG(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define FW_SNDING_SET_NDPA0_HEAD_PG(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define FW_SNDING_SET_NDPA0_HEAD_PG_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define FW_SNDING_GET_NDPA1_HEAD_PG(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define FW_SNDING_SET_NDPA1_HEAD_PG(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define FW_SNDING_SET_NDPA1_HEAD_PG_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define FW_SNDING_GET_MU_NDPA_HEAD_PG(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0XC, 0, 8)
#define FW_SNDING_SET_MU_NDPA_HEAD_PG(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 0, 8, value)
#define FW_SNDING_SET_MU_NDPA_HEAD_PG_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 0, 8, value)
#define FW_SNDING_GET_RPT0_HEAD_PG(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0XC, 8, 8)
#define FW_SNDING_SET_RPT0_HEAD_PG(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 8, 8, value)
#define FW_SNDING_SET_RPT0_HEAD_PG_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 8, 8, value)
#define FW_SNDING_GET_RPT1_HEAD_PG(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0XC, 16, 8)
#define FW_SNDING_SET_RPT1_HEAD_PG(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 16, 8, value)
#define FW_SNDING_SET_RPT1_HEAD_PG_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 16, 8, value)
#define FW_SNDING_GET_RPT2_HEAD_PG(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0XC, 24, 8)
#define FW_SNDING_SET_RPT2_HEAD_PG(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 24, 8, value)
#define FW_SNDING_SET_RPT2_HEAD_PG_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 24, 8, value)
#define FW_FWCTRL_GET_SEQ_NUM(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define FW_FWCTRL_SET_SEQ_NUM(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define FW_FWCTRL_SET_SEQ_NUM_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define FW_FWCTRL_GET_MORE_CONTENT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 1)
#define FW_FWCTRL_SET_MORE_CONTENT(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 1, value)
#define FW_FWCTRL_SET_MORE_CONTENT_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 1, value)
#define FW_FWCTRL_GET_CONTENT_IDX(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 9, 7)
#define FW_FWCTRL_SET_CONTENT_IDX(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 9, 7, value)
#define FW_FWCTRL_SET_CONTENT_IDX_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 9, 7, value)
#define FW_FWCTRL_GET_CLASS_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define FW_FWCTRL_SET_CLASS_ID(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define FW_FWCTRL_SET_CLASS_ID_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define FW_FWCTRL_GET_LENGTH(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define FW_FWCTRL_SET_LENGTH(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define FW_FWCTRL_SET_LENGTH_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define FW_FWCTRL_GET_CONTENT(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 32)
#define FW_FWCTRL_SET_CONTENT(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 32, value)
#define FW_FWCTRL_SET_CONTENT_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 32, value)
#define SEND_SCAN_PKT_GET_SIZE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 16)
#define SEND_SCAN_PKT_SET_SIZE(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 16, value)
#define SEND_SCAN_PKT_SET_SIZE_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 16, value)
#define SEND_SCAN_PKT_GET_INDEX(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define SEND_SCAN_PKT_SET_INDEX(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define SEND_SCAN_PKT_SET_INDEX_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define SEND_SCAN_PKT_GET_LOC(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define SEND_SCAN_PKT_SET_LOC(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define SEND_SCAN_PKT_SET_LOC_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define BCN_OFFLOAD_GET_REQUEST_VERSION(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define BCN_OFFLOAD_SET_REQUEST_VERSION(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define BCN_OFFLOAD_SET_REQUEST_VERSION_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define BCN_OFFLOAD_GET_ENABLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define BCN_OFFLOAD_SET_ENABLE(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define BCN_OFFLOAD_SET_ENABLE_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define BCN_OFFLOAD_GET_MORE_RULE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 2, 1)
#define BCN_OFFLOAD_SET_MORE_RULE(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 2, 1, value)
#define BCN_OFFLOAD_SET_MORE_RULE_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 2, 1, value)
#define BCN_OFFLOAD_GET_C2H_PERIODIC_REPORT(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X08, 3, 1)
#define BCN_OFFLOAD_SET_C2H_PERIODIC_REPORT(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 3, 1, value)
#define BCN_OFFLOAD_SET_C2H_PERIODIC_REPORT_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 3, 1, value)
#define BCN_OFFLOAD_GET_REPORT_PERIOD(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define BCN_OFFLOAD_SET_REPORT_PERIOD(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define BCN_OFFLOAD_SET_REPORT_PERIOD_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define BCN_OFFLOAD_GET_RULE_LENGTH(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define BCN_OFFLOAD_SET_RULE_LENGTH(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define BCN_OFFLOAD_SET_RULE_LENGTH_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define BCN_OFFLOAD_GET_RULE_CONTENT(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 8)
#define BCN_OFFLOAD_SET_RULE_CONTENT(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define BCN_OFFLOAD_SET_RULE_CONTENT_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define DROP_SCAN_PKT_GET_DROP_ALL(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define DROP_SCAN_PKT_SET_DROP_ALL(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define DROP_SCAN_PKT_SET_DROP_ALL_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define DROP_SCAN_PKT_GET_DROP_SINGLE(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define DROP_SCAN_PKT_SET_DROP_SINGLE(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define DROP_SCAN_PKT_SET_DROP_SINGLE_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define DROP_SCAN_PKT_GET_DROP_IDX(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define DROP_SCAN_PKT_SET_DROP_IDX(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define DROP_SCAN_PKT_SET_DROP_IDX_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define P2PPS_GET_OFFLOAD_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 1)
#define P2PPS_SET_OFFLOAD_EN(h2c_pkt, value)                                   \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 1, value)
#define P2PPS_SET_OFFLOAD_EN_NO_CLR(h2c_pkt, value)                            \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 1, value)
#define P2PPS_GET_ROLE(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 1, 1)
#define P2PPS_SET_ROLE(h2c_pkt, value)                                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 1, 1, value)
#define P2PPS_SET_ROLE_NO_CLR(h2c_pkt, value)                                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 1, 1, value)
#define P2PPS_GET_CTWINDOW_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 2, 1)
#define P2PPS_SET_CTWINDOW_EN(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 2, 1, value)
#define P2PPS_SET_CTWINDOW_EN_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 2, 1, value)
#define P2PPS_GET_NOA_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 3, 1)
#define P2PPS_SET_NOA_EN(h2c_pkt, value)                                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 3, 1, value)
#define P2PPS_SET_NOA_EN_NO_CLR(h2c_pkt, value)                                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 3, 1, value)
#define P2PPS_GET_NOA_SEL(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 4, 1)
#define P2PPS_SET_NOA_SEL(h2c_pkt, value)                                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 4, 1, value)
#define P2PPS_SET_NOA_SEL_NO_CLR(h2c_pkt, value)                               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 4, 1, value)
#define P2PPS_GET_ALLSTASLEEP(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 5, 1)
#define P2PPS_SET_ALLSTASLEEP(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 5, 1, value)
#define P2PPS_SET_ALLSTASLEEP_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 5, 1, value)
#define P2PPS_GET_DISCOVERY(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 6, 1)
#define P2PPS_SET_DISCOVERY(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 6, 1, value)
#define P2PPS_SET_DISCOVERY_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 6, 1, value)
#define P2PPS_GET_DISABLE_CLOSERF(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 7, 1)
#define P2PPS_SET_DISABLE_CLOSERF(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 7, 1, value)
#define P2PPS_SET_DISABLE_CLOSERF_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 7, 1, value)
#define P2PPS_GET_P2P_PORT_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define P2PPS_SET_P2P_PORT_ID(h2c_pkt, value)                                  \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define P2PPS_SET_P2P_PORT_ID_NO_CLR(h2c_pkt, value)                           \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define P2PPS_GET_P2P_GROUP(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define P2PPS_SET_P2P_GROUP(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define P2PPS_SET_P2P_GROUP_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define P2PPS_GET_P2P_MACID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define P2PPS_SET_P2P_MACID(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define P2PPS_SET_P2P_MACID_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define P2PPS_GET_CTWINDOW_LENGTH(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 8)
#define P2PPS_SET_CTWINDOW_LENGTH(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define P2PPS_SET_CTWINDOW_LENGTH_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define P2PPS_GET_NOA_DURATION_PARA(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X10, 0, 32)
#define P2PPS_SET_NOA_DURATION_PARA(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 32, value)
#define P2PPS_SET_NOA_DURATION_PARA_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 32, value)
#define P2PPS_GET_NOA_INTERVAL_PARA(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X14, 0, 32)
#define P2PPS_SET_NOA_INTERVAL_PARA(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 0, 32, value)
#define P2PPS_SET_NOA_INTERVAL_PARA_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 0, 32, value)
#define P2PPS_GET_NOA_START_TIME_PARA(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X18, 0, 32)
#define P2PPS_SET_NOA_START_TIME_PARA(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 0, 32, value)
#define P2PPS_SET_NOA_START_TIME_PARA_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 0, 32, value)
#define P2PPS_GET_NOA_COUNT_PARA(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X1C, 0, 32)
#define P2PPS_SET_NOA_COUNT_PARA(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X1C, 0, 32, value)
#define P2PPS_SET_NOA_COUNT_PARA_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X1C, 0, 32, value)
#define BT_COEX_GET_DATA_START(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define BT_COEX_SET_DATA_START(h2c_pkt, value)                                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define BT_COEX_SET_DATA_START_NO_CLR(h2c_pkt, value)                          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define ACT_SCHEDULE_REQ_GET_MODULE_ID(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define ACT_SCHEDULE_REQ_SET_MODULE_ID(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define ACT_SCHEDULE_REQ_SET_MODULE_ID_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define ACT_SCHEDULE_REQ_GET_PRIORITY(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define ACT_SCHEDULE_REQ_SET_PRIORITY(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define ACT_SCHEDULE_REQ_SET_PRIORITY_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define ACT_SCHEDULE_REQ_GET_RSVD1(h2c_pkt)                                    \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 16)
#define ACT_SCHEDULE_REQ_SET_RSVD1(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 16, value)
#define ACT_SCHEDULE_REQ_SET_RSVD1_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 16, value)
#define ACT_SCHEDULE_REQ_GET_START_TIME(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0XC, 0, 32)
#define ACT_SCHEDULE_REQ_SET_START_TIME(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0XC, 0, 32, value)
#define ACT_SCHEDULE_REQ_SET_START_TIME_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0XC, 0, 32, value)
#define ACT_SCHEDULE_REQ_GET_DURATION(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X10, 0, 32)
#define ACT_SCHEDULE_REQ_SET_DURATION(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 32, value)
#define ACT_SCHEDULE_REQ_SET_DURATION_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 32, value)
#define ACT_SCHEDULE_REQ_GET_PERIOD(h2c_pkt)                                   \
	GET_H2C_FIELD(h2c_pkt + 0X14, 0, 32)
#define ACT_SCHEDULE_REQ_SET_PERIOD(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 0, 32, value)
#define ACT_SCHEDULE_REQ_SET_PERIOD_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 0, 32, value)
#define ACT_SCHEDULE_REQ_GET_TSF_IDX(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X18, 0, 8)
#define ACT_SCHEDULE_REQ_SET_TSF_IDX(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 0, 8, value)
#define ACT_SCHEDULE_REQ_SET_TSF_IDX_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 0, 8, value)
#define ACT_SCHEDULE_REQ_GET_CHANNEL(h2c_pkt)                                  \
	GET_H2C_FIELD(h2c_pkt + 0X18, 8, 8)
#define ACT_SCHEDULE_REQ_SET_CHANNEL(h2c_pkt, value)                           \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 8, 8, value)
#define ACT_SCHEDULE_REQ_SET_CHANNEL_NO_CLR(h2c_pkt, value)                    \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 8, 8, value)
#define ACT_SCHEDULE_REQ_GET_BW(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X18, 16, 8)
#define ACT_SCHEDULE_REQ_SET_BW(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 16, 8, value)
#define ACT_SCHEDULE_REQ_SET_BW_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 16, 8, value)
#define ACT_SCHEDULE_REQ_GET_PRIMART_CH_IDX(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X18, 24, 9)
#define ACT_SCHEDULE_REQ_SET_PRIMART_CH_IDX(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 24, 9, value)
#define ACT_SCHEDULE_REQ_SET_PRIMART_CH_IDX_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 24, 9, value)
#define NAN_CTRL_GET_NAN_EN(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 2)
#define NAN_CTRL_SET_NAN_EN(h2c_pkt, value)                                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 2, value)
#define NAN_CTRL_SET_NAN_EN_NO_CLR(h2c_pkt, value)                             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 2, value)
#define NAN_CTRL_GET_WARMUP_TIMER_FLAG(h2c_pkt)                                \
	GET_H2C_FIELD(h2c_pkt + 0X08, 2, 1)
#define NAN_CTRL_SET_WARMUP_TIMER_FLAG(h2c_pkt, value)                         \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 2, 1, value)
#define NAN_CTRL_SET_WARMUP_TIMER_FLAG_NO_CLR(h2c_pkt, value)                  \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 2, 1, value)
#define NAN_CTRL_GET_SUPPORT_BAND(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 2)
#define NAN_CTRL_SET_SUPPORT_BAND(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 2, value)
#define NAN_CTRL_SET_SUPPORT_BAND_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 2, value)
#define NAN_CTRL_GET_DISABLE_2G_DISC_BCN(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X08, 10, 1)
#define NAN_CTRL_SET_DISABLE_2G_DISC_BCN(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 10, 1, value)
#define NAN_CTRL_SET_DISABLE_2G_DISC_BCN_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 10, 1, value)
#define NAN_CTRL_GET_DISABLE_5G_DISC_BCN(h2c_pkt)                              \
	GET_H2C_FIELD(h2c_pkt + 0X08, 11, 1)
#define NAN_CTRL_SET_DISABLE_5G_DISC_BCN(h2c_pkt, value)                       \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 11, 1, value)
#define NAN_CTRL_SET_DISABLE_5G_DISC_BCN_NO_CLR(h2c_pkt, value)                \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 11, 1, value)
#define NAN_CTRL_GET_BCN_RSVD_PAGE_OFFSET(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define NAN_CTRL_SET_BCN_RSVD_PAGE_OFFSET(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define NAN_CTRL_SET_BCN_RSVD_PAGE_OFFSET_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define NAN_CTRL_GET_CHANNEL_2G(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define NAN_CTRL_SET_CHANNEL_2G(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define NAN_CTRL_SET_CHANNEL_2G_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define NAN_CTRL_GET_CHANNEL_5G(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 8)
#define NAN_CTRL_SET_CHANNEL_5G(h2c_pkt, value)                                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define NAN_CTRL_SET_CHANNEL_5G_NO_CLR(h2c_pkt, value)                         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define NAN_CTRL_GET_MASTERPREFERENCE_VALUE(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 8, 8)
#define NAN_CTRL_SET_MASTERPREFERENCE_VALUE(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 8, 8, value)
#define NAN_CTRL_SET_MASTERPREFERENCE_VALUE_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 8, 8, value)
#define NAN_CTRL_GET_RANDOMFACTOR_VALUE(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 16, 8)
#define NAN_CTRL_SET_RANDOMFACTOR_VALUE(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 16, 8, value)
#define NAN_CTRL_SET_RANDOMFACTOR_VALUE_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 16, 8, value)
#define NAN_CHANNEL_PLAN_0_GET_CHANNEL_NUMBER_0(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_0(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_0_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define NAN_CHANNEL_PLAN_0_GET_UNPAUSE_MACID_0(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_0(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_0_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define NAN_CHANNEL_PLAN_0_GET_START_TIME_SLOT_0(h2c_pkt)                      \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 16)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_0(h2c_pkt, value)               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 16, value)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_0_NO_CLR(h2c_pkt, value)        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 16, value)
#define NAN_CHANNEL_PLAN_0_GET_DURATION_0(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 16, 16)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_0(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 16, 16, value)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_0_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 16, 16, value)
#define NAN_CHANNEL_PLAN_0_GET_CHANNEL_NUMBER_1(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X10, 0, 8)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_1(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 8, value)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_1_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 8, value)
#define NAN_CHANNEL_PLAN_0_GET_UNPAUSE_MACID_1(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X10, 8, 8)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_1(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 8, 8, value)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_1_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 8, 8, value)
#define NAN_CHANNEL_PLAN_0_GET_START_TIME_SLOT_1(h2c_pkt)                      \
	GET_H2C_FIELD(h2c_pkt + 0X14, 0, 16)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_1(h2c_pkt, value)               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 0, 16, value)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_1_NO_CLR(h2c_pkt, value)        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 0, 16, value)
#define NAN_CHANNEL_PLAN_0_GET_DURATION_1(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X14, 16, 16)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_1(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 16, 16, value)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_1_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 16, 16, value)
#define NAN_CHANNEL_PLAN_0_GET_CHANNEL_NUMBER_2(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X18, 0, 8)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_2(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 0, 8, value)
#define NAN_CHANNEL_PLAN_0_SET_CHANNEL_NUMBER_2_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 0, 8, value)
#define NAN_CHANNEL_PLAN_0_GET_UNPAUSE_MACID_2(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X18, 8, 8)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_2(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 8, 8, value)
#define NAN_CHANNEL_PLAN_0_SET_UNPAUSE_MACID_2_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 8, 8, value)
#define NAN_CHANNEL_PLAN_0_GET_START_TIME_SLOT_2(h2c_pkt)                      \
	GET_H2C_FIELD(h2c_pkt + 0X1C, 0, 16)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_2(h2c_pkt, value)               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X1C, 0, 16, value)
#define NAN_CHANNEL_PLAN_0_SET_START_TIME_SLOT_2_NO_CLR(h2c_pkt, value)        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X1C, 0, 16, value)
#define NAN_CHANNEL_PLAN_0_GET_DURATION_2(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X1C, 16, 16)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_2(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X1C, 16, 16, value)
#define NAN_CHANNEL_PLAN_0_SET_DURATION_2_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X1C, 16, 16, value)
#define NAN_CHANNEL_PLAN_1_GET_CHANNEL_NUMBER_3(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_3(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_3_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define NAN_CHANNEL_PLAN_1_GET_UNPAUSE_MACID_3(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_3(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_3_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define NAN_CHANNEL_PLAN_1_GET_START_TIME_SLOT_3(h2c_pkt)                      \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 16)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_3(h2c_pkt, value)               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 16, value)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_3_NO_CLR(h2c_pkt, value)        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 16, value)
#define NAN_CHANNEL_PLAN_1_GET_DURATION_3(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X0C, 16, 16)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_3(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 16, 16, value)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_3_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 16, 16, value)
#define NAN_CHANNEL_PLAN_1_GET_CHANNEL_NUMBER_4(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X10, 0, 8)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_4(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 8, value)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_4_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 8, value)
#define NAN_CHANNEL_PLAN_1_GET_UNPAUSE_MACID_4(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X10, 8, 8)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_4(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 8, 8, value)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_4_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 8, 8, value)
#define NAN_CHANNEL_PLAN_1_GET_START_TIME_SLOT_4(h2c_pkt)                      \
	GET_H2C_FIELD(h2c_pkt + 0X14, 0, 16)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_4(h2c_pkt, value)               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 0, 16, value)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_4_NO_CLR(h2c_pkt, value)        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 0, 16, value)
#define NAN_CHANNEL_PLAN_1_GET_DURATION_4(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X14, 16, 16)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_4(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X14, 16, 16, value)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_4_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X14, 16, 16, value)
#define NAN_CHANNEL_PLAN_1_GET_CHANNEL_NUMBER_5(h2c_pkt)                       \
	GET_H2C_FIELD(h2c_pkt + 0X18, 0, 8)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_5(h2c_pkt, value)                \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 0, 8, value)
#define NAN_CHANNEL_PLAN_1_SET_CHANNEL_NUMBER_5_NO_CLR(h2c_pkt, value)         \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 0, 8, value)
#define NAN_CHANNEL_PLAN_1_GET_UNPAUSE_MACID_5(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X18, 8, 8)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_5(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X18, 8, 8, value)
#define NAN_CHANNEL_PLAN_1_SET_UNPAUSE_MACID_5_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X18, 8, 8, value)
#define NAN_CHANNEL_PLAN_1_GET_START_TIME_SLOT_5(h2c_pkt)                      \
	GET_H2C_FIELD(h2c_pkt + 0X1C, 0, 16)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_5(h2c_pkt, value)               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X1C, 0, 16, value)
#define NAN_CHANNEL_PLAN_1_SET_START_TIME_SLOT_5_NO_CLR(h2c_pkt, value)        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X1C, 0, 16, value)
#define NAN_CHANNEL_PLAN_1_GET_DURATION_5(h2c_pkt)                             \
	GET_H2C_FIELD(h2c_pkt + 0X1C, 16, 16)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_5(h2c_pkt, value)                      \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X1C, 16, 16, value)
#define NAN_CHANNEL_PLAN_1_SET_DURATION_5_NO_CLR(h2c_pkt, value)               \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X1C, 16, 16, value)
#define NAN_FUNC_CTRL_GET_PORT_IDX(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 0, 8)
#define NAN_FUNC_CTRL_SET_PORT_IDX(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 0, 8, value)
#define NAN_FUNC_CTRL_SET_PORT_IDX_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 0, 8, value)
#define NAN_FUNC_CTRL_GET_MAC_ID(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X08, 8, 8)
#define NAN_FUNC_CTRL_SET_MAC_ID(h2c_pkt, value)                               \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 8, 8, value)
#define NAN_FUNC_CTRL_SET_MAC_ID_NO_CLR(h2c_pkt, value)                        \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 8, 8, value)
#define NAN_FUNC_CTRL_GET_MASTER_PREF(h2c_pkt)                                 \
	GET_H2C_FIELD(h2c_pkt + 0X08, 16, 8)
#define NAN_FUNC_CTRL_SET_MASTER_PREF(h2c_pkt, value)                          \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 16, 8, value)
#define NAN_FUNC_CTRL_SET_MASTER_PREF_NO_CLR(h2c_pkt, value)                   \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 16, 8, value)
#define NAN_FUNC_CTRL_GET_RANDOM_FACTOR(h2c_pkt)                               \
	GET_H2C_FIELD(h2c_pkt + 0X08, 24, 8)
#define NAN_FUNC_CTRL_SET_RANDOM_FACTOR(h2c_pkt, value)                        \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X08, 24, 8, value)
#define NAN_FUNC_CTRL_SET_RANDOM_FACTOR_NO_CLR(h2c_pkt, value)                 \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X08, 24, 8, value)
#define NAN_FUNC_CTRL_GET_OP_CH_24G(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 0, 8)
#define NAN_FUNC_CTRL_SET_OP_CH_24G(h2c_pkt, value)                            \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define NAN_FUNC_CTRL_SET_OP_CH_24G_NO_CLR(h2c_pkt, value)                     \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 0, 8, value)
#define NAN_FUNC_CTRL_GET_OP_CH_5G(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 8, 8)
#define NAN_FUNC_CTRL_SET_OP_CH_5G(h2c_pkt, value)                             \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 8, 8, value)
#define NAN_FUNC_CTRL_SET_OP_CH_5G_NO_CLR(h2c_pkt, value)                      \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 8, 8, value)
#define NAN_FUNC_CTRL_GET_OPTIONS(h2c_pkt) GET_H2C_FIELD(h2c_pkt + 0X0C, 16, 16)
#define NAN_FUNC_CTRL_SET_OPTIONS(h2c_pkt, value)                              \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X0C, 16, 16, value)
#define NAN_FUNC_CTRL_SET_OPTIONS_NO_CLR(h2c_pkt, value)                       \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X0C, 16, 16, value)
#define NAN_FUNC_CTRL_GET_SYNC_BCN_RSVD_OFFSET(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X10, 0, 8)
#define NAN_FUNC_CTRL_SET_SYNC_BCN_RSVD_OFFSET(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 0, 8, value)
#define NAN_FUNC_CTRL_SET_SYNC_BCN_RSVD_OFFSET_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 0, 8, value)
#define NAN_FUNC_CTRL_GET_DISC_BCN_RSVD_OFFSET(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X10, 8, 8)
#define NAN_FUNC_CTRL_SET_DISC_BCN_RSVD_OFFSET(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 8, 8, value)
#define NAN_FUNC_CTRL_SET_DISC_BCN_RSVD_OFFSET_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 8, 8, value)
#define NAN_FUNC_CTRL_GET_DW_SCHDL_PRIORITY(h2c_pkt)                           \
	GET_H2C_FIELD(h2c_pkt + 0X10, 16, 8)
#define NAN_FUNC_CTRL_SET_DW_SCHDL_PRIORITY(h2c_pkt, value)                    \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 16, 8, value)
#define NAN_FUNC_CTRL_SET_DW_SCHDL_PRIORITY_NO_CLR(h2c_pkt, value)             \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 16, 8, value)
#define NAN_FUNC_CTRL_GET_TIME_INDICATE_PERIOD(h2c_pkt)                        \
	GET_H2C_FIELD(h2c_pkt + 0X10, 24, 8)
#define NAN_FUNC_CTRL_SET_TIME_INDICATE_PERIOD(h2c_pkt, value)                 \
	SET_H2C_FIELD_CLR(h2c_pkt + 0X10, 24, 8, value)
#define NAN_FUNC_CTRL_SET_TIME_INDICATE_PERIOD_NO_CLR(h2c_pkt, value)          \
	SET_H2C_FIELD_NO_CLR(h2c_pkt + 0X10, 24, 8, value)
#endif

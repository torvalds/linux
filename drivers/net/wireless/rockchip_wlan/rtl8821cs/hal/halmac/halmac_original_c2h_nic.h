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

#ifndef _HAL_ORIGINALC2HFORMAT_H2C_C2H_NIC_H_
#define _HAL_ORIGINALC2HFORMAT_H2C_C2H_NIC_H_
#define CMD_ID_C2H 0X00
#define CMD_ID_DBG 0X00
#define CMD_ID_C2H_LB 0X01
#define CMD_ID_C2H_SND_TXBF 0X02
#define CMD_ID_C2H_CCX_RPT 0X03
#define CMD_ID_C2H_AP_REQ_TXRPT 0X04
#define CMD_ID_C2H_INITIAL_RATE_COLLECTION 0X05
#define CMD_ID_C2H_RA_RPT 0X0C
#define CMD_ID_C2H_SPECIAL_STATISTICS 0X0D
#define CMD_ID_C2H_RA_PARA_RPT 0X0E
#define CMD_ID_C2H_CUR_CHANNEL 0X10
#define CMD_ID_C2H_GPIO_WAKEUP 0X14
#define CMD_ID_C2H_DROPID_RPT 0X2D
#define CMD_ID_C2H_LPS_STATUS_RPT 0X32
#define C2H_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_SET_CMD_ID(c2h_pkt, value)                                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_SET_SEQ(c2h_pkt, value)                                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define DBG_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define DBG_SET_CMD_ID(c2h_pkt, value)                                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define DBG_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define DBG_SET_SEQ(c2h_pkt, value)                                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define DBG_GET_DBG_STR1(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define DBG_SET_DBG_STR1(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define DBG_GET_DBG_STR2(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define DBG_SET_DBG_STR2(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define DBG_GET_DBG_STR3(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define DBG_SET_DBG_STR3(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define DBG_GET_DBG_STR4(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define DBG_SET_DBG_STR4(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define DBG_GET_DBG_STR5(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define DBG_SET_DBG_STR5(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define DBG_GET_DBG_STR6(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 24, 8)
#define DBG_SET_DBG_STR6(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 24, 8, value)
#define DBG_GET_DBG_STR7(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 8)
#define DBG_SET_DBG_STR7(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 8, value)
#define DBG_GET_DBG_STR8(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 8, 8)
#define DBG_SET_DBG_STR8(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 8, 8, value)
#define DBG_GET_DBG_STR9(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 16, 8)
#define DBG_SET_DBG_STR9(c2h_pkt, value)                                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 16, 8, value)
#define DBG_GET_DBG_STR10(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 24, 8)
#define DBG_SET_DBG_STR10(c2h_pkt, value)                                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 24, 8, value)
#define DBG_GET_DBG_STR11(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 0, 8)
#define DBG_SET_DBG_STR11(c2h_pkt, value)                                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 0, 8, value)
#define DBG_GET_DBG_STR12(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 8, 8)
#define DBG_SET_DBG_STR12(c2h_pkt, value)                                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 8, 8, value)
#define DBG_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define DBG_SET_LEN(c2h_pkt, value)                                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define DBG_GET_TRIGGER(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define DBG_SET_TRIGGER(c2h_pkt, value)                                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_LB_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_LB_SET_CMD_ID(c2h_pkt, value)                                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_LB_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_LB_SET_SEQ(c2h_pkt, value)                                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_LB_GET_PAYLOAD1(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 16)
#define C2H_LB_SET_PAYLOAD1(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 16, value)
#define C2H_LB_GET_PAYLOAD2(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 32)
#define C2H_LB_SET_PAYLOAD2(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 32, value)
#define C2H_LB_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_LB_SET_LEN(c2h_pkt, value)                                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_LB_GET_TRIGGER(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_LB_SET_TRIGGER(c2h_pkt, value)                                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_SND_TXBF_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_SND_TXBF_SET_CMD_ID(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_SND_TXBF_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_SND_TXBF_SET_SEQ(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_SND_TXBF_GET_SND_RESULT(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 1)
#define C2H_SND_TXBF_SET_SND_RESULT(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 1, value)
#define C2H_SND_TXBF_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_SND_TXBF_SET_LEN(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_SND_TXBF_GET_TRIGGER(c2h_pkt)                                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_SND_TXBF_SET_TRIGGER(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_CCX_RPT_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_CCX_RPT_SET_CMD_ID(c2h_pkt, value)                                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_CCX_RPT_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_CCX_RPT_SET_SEQ(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_CCX_RPT_GET_QSEL(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 5)
#define C2H_CCX_RPT_SET_QSEL(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 5, value)
#define C2H_CCX_RPT_GET_BMC(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 21, 1)
#define C2H_CCX_RPT_SET_BMC(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 21, 1, value)
#define C2H_CCX_RPT_GET_LIFE_TIME_OVER(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 22, 1)
#define C2H_CCX_RPT_SET_LIFE_TIME_OVER(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 22, 1, value)
#define C2H_CCX_RPT_GET_RETRY_OVER(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 23, 1)
#define C2H_CCX_RPT_SET_RETRY_OVER(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 23, 1, value)
#define C2H_CCX_RPT_GET_MACID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define C2H_CCX_RPT_SET_MACID(c2h_pkt, value)                                  \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define C2H_CCX_RPT_GET_DATA_RETRY_CNT(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 6)
#define C2H_CCX_RPT_SET_DATA_RETRY_CNT(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 6, value)
#define C2H_CCX_RPT_GET_QUEUE7_0(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define C2H_CCX_RPT_SET_QUEUE7_0(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define C2H_CCX_RPT_GET_QUEUE15_8(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define C2H_CCX_RPT_SET_QUEUE15_8(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define C2H_CCX_RPT_GET_FINAL_DATA_RATE(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 24, 8)
#define C2H_CCX_RPT_SET_FINAL_DATA_RATE(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 24, 8, value)
#define C2H_CCX_RPT_GET_SW_DEFINE_0(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 8)
#define C2H_CCX_RPT_SET_SW_DEFINE_0(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 8, value)
#define C2H_CCX_RPT_GET_SW_DEFINE_1(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 8, 4)
#define C2H_CCX_RPT_SET_SW_DEFINE_1(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 8, 4, value)
#define C2H_CCX_RPT_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_CCX_RPT_SET_LEN(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_CCX_RPT_GET_TRIGGER(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_CCX_RPT_SET_TRIGGER(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_AP_REQ_TXRPT_GET_CMD_ID(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_CMD_ID(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_AP_REQ_TXRPT_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_SEQ(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_AP_REQ_TXRPT_GET_STA1_MACID(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_STA1_MACID(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK1_0(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK1_0(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK1_1(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK1_1(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL1_0(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL1_0(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL1_1(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL1_1(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define C2H_AP_REQ_TXRPT_GET_INITIAL_RATE1(c2h_pkt)                            \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_INITIAL_RATE1(c2h_pkt, value)                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 24, 8, value)
#define C2H_AP_REQ_TXRPT_GET_STA2_MACID(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_STA2_MACID(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK2_0(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK2_0(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 8, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK2_1(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK2_1(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 16, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL2_0(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL2_0(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 24, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL2_1(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL2_1(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 0, 8, value)
#define C2H_AP_REQ_TXRPT_GET_INITIAL_RATE2(c2h_pkt)                            \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_INITIAL_RATE2(c2h_pkt, value)                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 8, 8, value)
#define C2H_AP_REQ_TXRPT_GET_LEN(c2h_pkt)                                      \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_LEN(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_AP_REQ_TXRPT_GET_TRIGGER(c2h_pkt)                                  \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_TRIGGER(c2h_pkt, value)                           \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_CMD_ID(c2h_pkt)                        \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_CMD_ID(c2h_pkt, value)                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_SEQ(c2h_pkt)                           \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_SEQ(c2h_pkt, value)                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_TRYING_BITMAP(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 7)
#define C2H_INITIAL_RATE_COLLECTION_SET_TRYING_BITMAP(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 7, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE1(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE1(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE2(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE2(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE3(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE3(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE4(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE4(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE5(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 24, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE5(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 24, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE6(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE6(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE7(c2h_pkt)                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 8, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE7(c2h_pkt, value)          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 8, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_LEN(c2h_pkt)                           \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_LEN(c2h_pkt, value)                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_INITIAL_RATE_COLLECTION_GET_TRIGGER(c2h_pkt)                       \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_TRIGGER(c2h_pkt, value)                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_RA_RPT_GET_CMD_ID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_RA_RPT_SET_CMD_ID(c2h_pkt, value)                                  \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_RA_RPT_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_RA_RPT_SET_SEQ(c2h_pkt, value)                                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_RA_RPT_GET_RATE(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define C2H_RA_RPT_SET_RATE(c2h_pkt, value)                                    \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define C2H_RA_RPT_GET_MACID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define C2H_RA_RPT_SET_MACID(c2h_pkt, value)                                   \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define C2H_RA_RPT_GET_USE_LDPC(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 1)
#define C2H_RA_RPT_SET_USE_LDPC(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 1, value)
#define C2H_RA_RPT_GET_USE_TXBF(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 1, 1)
#define C2H_RA_RPT_SET_USE_TXBF(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 1, 1, value)
#define C2H_RA_RPT_GET_COLLISION_STATE(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define C2H_RA_RPT_SET_COLLISION_STATE(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define C2H_RA_RPT_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_RA_RPT_SET_LEN(c2h_pkt, value)                                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_RA_RPT_GET_TRIGGER(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_RA_RPT_SET_TRIGGER(c2h_pkt, value)                                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_CMD_ID(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_SPECIAL_STATISTICS_SET_CMD_ID(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_SEQ(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_SPECIAL_STATISTICS_SET_SEQ(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_STATISTICS_IDX(c2h_pkt)                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_STATISTICS_IDX(c2h_pkt, value)              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA0(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA0(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA1(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA1(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA2(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 8, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA2(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 8, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA3(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA3(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 16, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA4(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 24, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA4(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 24, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA5(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 0, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA5(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 0, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA6(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 8, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA6(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 8, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_DATA7(c2h_pkt)                              \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X08, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA7(c2h_pkt, value)                       \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X08, 16, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_LEN(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_LEN(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_SPECIAL_STATISTICS_GET_TRIGGER(c2h_pkt)                            \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_SPECIAL_STATISTICS_SET_TRIGGER(c2h_pkt, value)                     \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_RA_PARA_RPT_GET_CMD_ID(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_RA_PARA_RPT_SET_CMD_ID(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_RA_PARA_RPT_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_RA_PARA_RPT_SET_SEQ(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_RA_PARA_RPT_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_RA_PARA_RPT_SET_LEN(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_RA_PARA_RPT_GET_TRIGGER(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_RA_PARA_RPT_SET_TRIGGER(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_CUR_CHANNEL_GET_CMD_ID(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_CUR_CHANNEL_SET_CMD_ID(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_CUR_CHANNEL_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_CUR_CHANNEL_SET_SEQ(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_CUR_CHANNEL_GET_CHANNEL_NUM(c2h_pkt)                               \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define C2H_CUR_CHANNEL_SET_CHANNEL_NUM(c2h_pkt, value)                        \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define C2H_CUR_CHANNEL_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_CUR_CHANNEL_SET_LEN(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_CUR_CHANNEL_GET_TRIGGER(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_CUR_CHANNEL_SET_TRIGGER(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_GPIO_WAKEUP_GET_CMD_ID(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_GPIO_WAKEUP_SET_CMD_ID(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_GPIO_WAKEUP_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_GPIO_WAKEUP_SET_SEQ(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_GPIO_WAKEUP_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_GPIO_WAKEUP_SET_LEN(c2h_pkt, value)                                \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_GPIO_WAKEUP_GET_TRIGGER(c2h_pkt)                                   \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_GPIO_WAKEUP_SET_TRIGGER(c2h_pkt, value)                            \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_DROPID_RPT_GET_CMD_ID(c2h_pkt)                                     \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_DROPID_RPT_SET_CMD_ID(c2h_pkt, value)                              \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_DROPID_RPT_GET_SEQ(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_DROPID_RPT_SET_SEQ(c2h_pkt, value)                                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_DROPID_RPT_GET_DROPIDBIT(c2h_pkt)                                  \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 4)
#define C2H_DROPID_RPT_SET_DROPIDBIT(c2h_pkt, value)                           \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 4, value)
#define C2H_DROPID_RPT_GET_CURDROPID(c2h_pkt)                                  \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 20, 2)
#define C2H_DROPID_RPT_SET_CURDROPID(c2h_pkt, value)                           \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 20, 2, value)
#define C2H_DROPID_RPT_GET_MACID(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X04, 0, 8)
#define C2H_DROPID_RPT_SET_MACID(c2h_pkt, value)                               \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X04, 0, 8, value)
#define C2H_DROPID_RPT_GET_LEN(c2h_pkt) LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_DROPID_RPT_SET_LEN(c2h_pkt, value)                                 \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_DROPID_RPT_GET_TRIGGER(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_DROPID_RPT_SET_TRIGGER(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#define C2H_LPS_STATUS_RPT_GET_CMD_ID(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 0, 8)
#define C2H_LPS_STATUS_RPT_SET_CMD_ID(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 0, 8, value)
#define C2H_LPS_STATUS_RPT_GET_SEQ(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 8, 8)
#define C2H_LPS_STATUS_RPT_SET_SEQ(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 8, 8, value)
#define C2H_LPS_STATUS_RPT_GET_ACTION(c2h_pkt)                                 \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 16, 8)
#define C2H_LPS_STATUS_RPT_SET_ACTION(c2h_pkt, value)                          \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 16, 8, value)
#define C2H_LPS_STATUS_RPT_GET_STATUSCODE(c2h_pkt)                             \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X00, 24, 8)
#define C2H_LPS_STATUS_RPT_SET_STATUSCODE(c2h_pkt, value)                      \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X00, 24, 8, value)
#define C2H_LPS_STATUS_RPT_GET_LEN(c2h_pkt)                                    \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 16, 8)
#define C2H_LPS_STATUS_RPT_SET_LEN(c2h_pkt, value)                             \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 16, 8, value)
#define C2H_LPS_STATUS_RPT_GET_TRIGGER(c2h_pkt)                                \
	LE_BITS_TO_4BYTE(c2h_pkt + 0X0C, 24, 8)
#define C2H_LPS_STATUS_RPT_SET_TRIGGER(c2h_pkt, value)                         \
	SET_BITS_TO_LE_4BYTE(c2h_pkt + 0X0C, 24, 8, value)
#endif

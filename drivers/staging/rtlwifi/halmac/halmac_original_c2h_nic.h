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
#define C2H_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_SET_CMD_ID(__c2h, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_SET_SEQ(__c2h, __value)                                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define DBG_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define DBG_SET_CMD_ID(__c2h, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define DBG_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define DBG_SET_SEQ(__c2h, __value)                                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define DBG_GET_DBG_STR1(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 8)
#define DBG_SET_DBG_STR1(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 8, __value)
#define DBG_GET_DBG_STR2(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 24, 8)
#define DBG_SET_DBG_STR2(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 24, 8, __value)
#define DBG_GET_DBG_STR3(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define DBG_SET_DBG_STR3(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define DBG_GET_DBG_STR4(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define DBG_SET_DBG_STR4(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define DBG_GET_DBG_STR5(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 8)
#define DBG_SET_DBG_STR5(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 8, __value)
#define DBG_GET_DBG_STR6(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 24, 8)
#define DBG_SET_DBG_STR6(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 24, 8, __value)
#define DBG_GET_DBG_STR7(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X08, 0, 8)
#define DBG_SET_DBG_STR7(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 0, 8, __value)
#define DBG_GET_DBG_STR8(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X08, 8, 8)
#define DBG_SET_DBG_STR8(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 8, 8, __value)
#define DBG_GET_DBG_STR9(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X08, 16, 8)
#define DBG_SET_DBG_STR9(__c2h, __value)                                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 16, 8, __value)
#define DBG_GET_DBG_STR10(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X08, 24, 8)
#define DBG_SET_DBG_STR10(__c2h, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 24, 8, __value)
#define DBG_GET_DBG_STR11(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 0, 8)
#define DBG_SET_DBG_STR11(__c2h, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 0, 8, __value)
#define DBG_GET_DBG_STR12(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 8, 8)
#define DBG_SET_DBG_STR12(__c2h, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 8, 8, __value)
#define DBG_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define DBG_SET_LEN(__c2h, __value)                                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define DBG_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define DBG_SET_TRIGGER(__c2h, __value)                                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_LB_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_LB_SET_CMD_ID(__c2h, __value)                                      \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_LB_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_LB_SET_SEQ(__c2h, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_LB_GET_PAYLOAD1(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 16)
#define C2H_LB_SET_PAYLOAD1(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 16, __value)
#define C2H_LB_GET_PAYLOAD2(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 32)
#define C2H_LB_SET_PAYLOAD2(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 32, __value)
#define C2H_LB_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_LB_SET_LEN(__c2h, __value)                                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_LB_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_LB_SET_TRIGGER(__c2h, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_SND_TXBF_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_SND_TXBF_SET_CMD_ID(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_SND_TXBF_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_SND_TXBF_SET_SEQ(__c2h, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_SND_TXBF_GET_SND_RESULT(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 1)
#define C2H_SND_TXBF_SET_SND_RESULT(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 1, __value)
#define C2H_SND_TXBF_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_SND_TXBF_SET_LEN(__c2h, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_SND_TXBF_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_SND_TXBF_SET_TRIGGER(__c2h, __value)                               \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_CCX_RPT_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_CCX_RPT_SET_CMD_ID(__c2h, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_CCX_RPT_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_CCX_RPT_SET_SEQ(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_CCX_RPT_GET_QSEL(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 5)
#define C2H_CCX_RPT_SET_QSEL(__c2h, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 5, __value)
#define C2H_CCX_RPT_GET_BMC(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 21, 1)
#define C2H_CCX_RPT_SET_BMC(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 21, 1, __value)
#define C2H_CCX_RPT_GET_LIFE_TIME_OVER(__c2h)                                  \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 22, 1)
#define C2H_CCX_RPT_SET_LIFE_TIME_OVER(__c2h, __value)                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 22, 1, __value)
#define C2H_CCX_RPT_GET_RETRY_OVER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 23, 1)
#define C2H_CCX_RPT_SET_RETRY_OVER(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 23, 1, __value)
#define C2H_CCX_RPT_GET_MACID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 24, 8)
#define C2H_CCX_RPT_SET_MACID(__c2h, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 24, 8, __value)
#define C2H_CCX_RPT_GET_DATA_RETRY_CNT(__c2h)                                  \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 6)
#define C2H_CCX_RPT_SET_DATA_RETRY_CNT(__c2h, __value)                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 6, __value)
#define C2H_CCX_RPT_GET_QUEUE7_0(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define C2H_CCX_RPT_SET_QUEUE7_0(__c2h, __value)                               \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define C2H_CCX_RPT_GET_QUEUE15_8(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 8)
#define C2H_CCX_RPT_SET_QUEUE15_8(__c2h, __value)                              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 8, __value)
#define C2H_CCX_RPT_GET_FINAL_DATA_RATE(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 24, 8)
#define C2H_CCX_RPT_SET_FINAL_DATA_RATE(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 24, 8, __value)
#define C2H_CCX_RPT_GET_SW_DEFINE_0(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X08, 0, 8)
#define C2H_CCX_RPT_SET_SW_DEFINE_0(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 0, 8, __value)
#define C2H_CCX_RPT_GET_SW_DEFINE_1(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X08, 8, 4)
#define C2H_CCX_RPT_SET_SW_DEFINE_1(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 8, 4, __value)
#define C2H_CCX_RPT_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_CCX_RPT_SET_LEN(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_CCX_RPT_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_CCX_RPT_SET_TRIGGER(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_CMD_ID(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_SEQ(__c2h, __value)                               \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_STA1_MACID(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_STA1_MACID(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK1_0(__c2h)                                   \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK1_0(__c2h, __value)                          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 24, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK1_1(__c2h)                                   \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK1_1(__c2h, __value)                          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL1_0(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL1_0(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL1_1(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL1_1(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_INITIAL_RATE1(__c2h)                              \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_INITIAL_RATE1(__c2h, __value)                     \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 24, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_STA2_MACID(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_STA2_MACID(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 0, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK2_0(__c2h)                                   \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK2_0(__c2h, __value)                          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 8, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_OK2_1(__c2h)                                   \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_OK2_1(__c2h, __value)                          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 16, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL2_0(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL2_0(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 24, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TX_FAIL2_1(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X0C, 0, 8)
#define C2H_AP_REQ_TXRPT_SET_TX_FAIL2_1(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 0, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_INITIAL_RATE2(__c2h)                              \
	LE_BITS_TO_4BYTE(__c2h + 0X0C, 8, 8)
#define C2H_AP_REQ_TXRPT_SET_INITIAL_RATE2(__c2h, __value)                     \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 8, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_AP_REQ_TXRPT_SET_LEN(__c2h, __value)                               \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_AP_REQ_TXRPT_GET_TRIGGER(__c2h)                                    \
	LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_AP_REQ_TXRPT_SET_TRIGGER(__c2h, __value)                           \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_CMD_ID(__c2h)                          \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_CMD_ID(__c2h, __value)                 \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_SEQ(__c2h)                             \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_SEQ(__c2h, __value)                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_TRYING_BITMAP(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 7)
#define C2H_INITIAL_RATE_COLLECTION_SET_TRYING_BITMAP(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 7, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE1(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 24, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE1(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 24, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE2(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE2(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE3(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE3(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE4(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE4(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE5(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 24, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE5(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 24, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE6(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 0, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE6(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 0, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_INITIAL_RATE7(__c2h)                   \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 8, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_INITIAL_RATE7(__c2h, __value)          \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 8, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_LEN(__c2h)                             \
	LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_LEN(__c2h, __value)                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_INITIAL_RATE_COLLECTION_GET_TRIGGER(__c2h)                         \
	LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_INITIAL_RATE_COLLECTION_SET_TRIGGER(__c2h, __value)                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_RA_RPT_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_RA_RPT_SET_CMD_ID(__c2h, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_RA_RPT_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_RA_RPT_SET_SEQ(__c2h, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_RA_RPT_GET_RATE(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 8)
#define C2H_RA_RPT_SET_RATE(__c2h, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 8, __value)
#define C2H_RA_RPT_GET_MACID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 24, 8)
#define C2H_RA_RPT_SET_MACID(__c2h, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 24, 8, __value)
#define C2H_RA_RPT_GET_USE_LDPC(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 1)
#define C2H_RA_RPT_SET_USE_LDPC(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 1, __value)
#define C2H_RA_RPT_GET_USE_TXBF(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X04, 1, 1)
#define C2H_RA_RPT_SET_USE_TXBF(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 1, 1, __value)
#define C2H_RA_RPT_GET_COLLISION_STATE(__c2h)                                  \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define C2H_RA_RPT_SET_COLLISION_STATE(__c2h, __value)                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define C2H_RA_RPT_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_RA_RPT_SET_LEN(__c2h, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_RA_RPT_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_RA_RPT_SET_TRIGGER(__c2h, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_CMD_ID(__c2h)                               \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_SPECIAL_STATISTICS_SET_CMD_ID(__c2h, __value)                      \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_SEQ(__c2h)                                  \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_SPECIAL_STATISTICS_SET_SEQ(__c2h, __value)                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_STATISTICS_IDX(__c2h)                       \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_STATISTICS_IDX(__c2h, __value)              \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA0(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 24, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA0(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 24, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA1(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 0, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA1(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 0, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA2(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 8, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA2(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 8, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA3(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA3(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 16, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA4(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X04, 24, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA4(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X04, 24, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA5(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 0, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA5(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 0, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA6(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 8, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA6(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 8, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_DATA7(__c2h)                                \
	LE_BITS_TO_4BYTE(__c2h + 0X08, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_DATA7(__c2h, __value)                       \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X08, 16, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_LEN(__c2h)                                  \
	LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_SPECIAL_STATISTICS_SET_LEN(__c2h, __value)                         \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_SPECIAL_STATISTICS_GET_TRIGGER(__c2h)                              \
	LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_SPECIAL_STATISTICS_SET_TRIGGER(__c2h, __value)                     \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_RA_PARA_RPT_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_RA_PARA_RPT_SET_CMD_ID(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_RA_PARA_RPT_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_RA_PARA_RPT_SET_SEQ(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_RA_PARA_RPT_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_RA_PARA_RPT_SET_LEN(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_RA_PARA_RPT_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_RA_PARA_RPT_SET_TRIGGER(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_CUR_CHANNEL_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_CUR_CHANNEL_SET_CMD_ID(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_CUR_CHANNEL_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_CUR_CHANNEL_SET_SEQ(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_CUR_CHANNEL_GET_CHANNEL_NUM(__c2h)                                 \
	LE_BITS_TO_4BYTE(__c2h + 0X00, 16, 8)
#define C2H_CUR_CHANNEL_SET_CHANNEL_NUM(__c2h, __value)                        \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 16, 8, __value)
#define C2H_CUR_CHANNEL_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_CUR_CHANNEL_SET_LEN(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_CUR_CHANNEL_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_CUR_CHANNEL_SET_TRIGGER(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#define C2H_GPIO_WAKEUP_GET_CMD_ID(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 0, 8)
#define C2H_GPIO_WAKEUP_SET_CMD_ID(__c2h, __value)                             \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 0, 8, __value)
#define C2H_GPIO_WAKEUP_GET_SEQ(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X00, 8, 8)
#define C2H_GPIO_WAKEUP_SET_SEQ(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X00, 8, 8, __value)
#define C2H_GPIO_WAKEUP_GET_LEN(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 16, 8)
#define C2H_GPIO_WAKEUP_SET_LEN(__c2h, __value)                                \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 16, 8, __value)
#define C2H_GPIO_WAKEUP_GET_TRIGGER(__c2h) LE_BITS_TO_4BYTE(__c2h + 0X0C, 24, 8)
#define C2H_GPIO_WAKEUP_SET_TRIGGER(__c2h, __value)                            \
	SET_BITS_TO_LE_4BYTE(__c2h + 0X0C, 24, 8, __value)
#endif

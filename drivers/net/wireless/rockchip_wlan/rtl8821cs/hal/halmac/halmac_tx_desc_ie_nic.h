/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef _HALMAC_TX_DESC_IE_NIC_H_
#define _HALMAC_TX_DESC_IE_NIC_H_
#if (HALMAC_8814B_SUPPORT)

#define IE0_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 31, 1)
#define IE0_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 31, 1, value)
#define IE0_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 30, 1)
#define IE0_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 30, 1, value)
#define IE0_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 24, 4)
#define IE0_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 24, 4, value)
#define IE0_GET_TX_DESC_ARFR_TABLE_SEL(txdesc_ie)                              \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 19, 1)
#define IE0_SET_TX_DESC_ARFR_TABLE_SEL(txdesc_ie, value)                       \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 19, 1, value)
#define IE0_GET_TX_DESC_ARFR_HT_EN(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 18, 1)
#define IE0_SET_TX_DESC_ARFR_HT_EN(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 18, 1, value)
#define IE0_GET_TX_DESC_ARFR_OFDM_EN(txdesc_ie)                                \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 17, 1)
#define IE0_SET_TX_DESC_ARFR_OFDM_EN(txdesc_ie, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 17, 1, value)
#define IE0_GET_TX_DESC_ARFR_CCK_EN(txdesc_ie)                                 \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 16, 1)
#define IE0_SET_TX_DESC_ARFR_CCK_EN(txdesc_ie, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 16, 1, value)
#define IE0_GET_TX_DESC_HW_RTS_EN(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 9, 1)
#define IE0_SET_TX_DESC_HW_RTS_EN(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 9, 1, value)
#define IE0_GET_TX_DESC_RTS_EN(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 8, 1)
#define IE0_SET_TX_DESC_RTS_EN(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 8, 1, value)
#define IE0_GET_TX_DESC_CTS2SELF(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 7, 1)
#define IE0_SET_TX_DESC_CTS2SELF(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 7, 1, value)
#define IE0_GET_TX_DESC_RTY_LMT_EN(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 6, 1)
#define IE0_SET_TX_DESC_RTY_LMT_EN(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 6, 1, value)
#define IE0_GET_TX_DESC_RTS_SHORT(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 5, 1)
#define IE0_SET_TX_DESC_RTS_SHORT(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 5, 1, value)
#define IE0_GET_TX_DESC_DISDATAFB(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 4, 1)
#define IE0_SET_TX_DESC_DISDATAFB(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 4, 1, value)
#define IE0_GET_TX_DESC_DISRTSFB(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 3, 1)
#define IE0_SET_TX_DESC_DISRTSFB(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 3, 1, value)
#define IE0_GET_TX_DESC_DATA_SHORT(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 2, 1)
#define IE0_SET_TX_DESC_DATA_SHORT(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 2, 1, value)
#define IE0_GET_TX_DESC_TRY_RATE(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 1, 1)
#define IE0_SET_TX_DESC_TRY_RATE(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 1, 1, value)
#define IE0_GET_TX_DESC_USERATE(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 0, 1)
#define IE0_SET_TX_DESC_USERATE(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 0, 1, value)
#define IE0_GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc_ie)                         \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 27, 4)
#define IE0_SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc_ie, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 27, 4, value)
#define IE0_GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc_ie)                        \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 22, 5)
#define IE0_SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc_ie, value)                 \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 22, 5, value)
#define IE0_GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc_ie)                            \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 16, 6)
#define IE0_SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc_ie, value)                     \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 16, 6, value)
#define IE0_GET_TX_DESC_DATA_BW(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 12, 2)
#define IE0_SET_TX_DESC_DATA_BW(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 12, 2, value)
#define IE0_GET_TX_DESC_RTSRATE(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 7, 4)
#define IE0_SET_TX_DESC_RTSRATE(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 7, 4, value)
#define IE0_GET_TX_DESC_DATARATE(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 0, 7)
#define IE0_SET_TX_DESC_DATARATE(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 0, 7, value)
#define IE1_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 31, 1)
#define IE1_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 31, 1, value)
#define IE1_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 30, 1)
#define IE1_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 30, 1, value)
#define IE1_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 24, 4)
#define IE1_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 24, 4, value)
#define IE1_GET_TX_DESC_AMPDU_DENSITY(txdesc_ie)                               \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 21, 3)
#define IE1_SET_TX_DESC_AMPDU_DENSITY(txdesc_ie, value)                        \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 21, 3, value)
#define IE1_GET_TX_DESC_MAX_AGG_NUM(txdesc_ie)                                 \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 16, 5)
#define IE1_SET_TX_DESC_MAX_AGG_NUM(txdesc_ie, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 16, 5, value)
#define IE1_GET_TX_DESC_SECTYPE(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 14, 2)
#define IE1_SET_TX_DESC_SECTYPE(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 14, 2, value)
#define IE1_GET_TX_DESC_MOREFRAG(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 13, 1)
#define IE1_SET_TX_DESC_MOREFRAG(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 13, 1, value)
#define IE1_GET_TX_DESC_NOACM(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 12, 1)
#define IE1_SET_TX_DESC_NOACM(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 12, 1, value)
#define IE1_GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc_ie)                             \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 11, 1)
#define IE1_SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc_ie, value)                      \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 11, 1, value)
#define IE1_GET_TX_DESC_NAVUSEHDR(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 10, 1)
#define IE1_SET_TX_DESC_NAVUSEHDR(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 10, 1, value)
#define IE1_GET_TX_DESC_HTC(txdesc_ie) LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 9, 1)
#define IE1_SET_TX_DESC_HTC(txdesc_ie, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 9, 1, value)
#define IE1_GET_TX_DESC_BMC(txdesc_ie) LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 8, 1)
#define IE1_SET_TX_DESC_BMC(txdesc_ie, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 8, 1, value)
#define IE1_GET_TX_DESC_TX_PKT_AFTER_PIFS(txdesc_ie)                           \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 7, 1)
#define IE1_SET_TX_DESC_TX_PKT_AFTER_PIFS(txdesc_ie, value)                    \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 7, 1, value)
#define IE1_GET_TX_DESC_USE_MAX_TIME_EN(txdesc_ie)                             \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 6, 1)
#define IE1_SET_TX_DESC_USE_MAX_TIME_EN(txdesc_ie, value)                      \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 6, 1, value)
#define IE1_GET_TX_DESC_HW_SSN_SEL(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 4, 2)
#define IE1_SET_TX_DESC_HW_SSN_SEL(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 4, 2, value)
#define IE1_GET_TX_DESC_DISQSELSEQ(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 3, 1)
#define IE1_SET_TX_DESC_DISQSELSEQ(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 3, 1, value)
#define IE1_GET_TX_DESC_EN_HWSEQ(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 2, 1)
#define IE1_SET_TX_DESC_EN_HWSEQ(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 2, 1, value)
#define IE1_GET_TX_DESC_EN_HWEXSEQ(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 1, 1)
#define IE1_SET_TX_DESC_EN_HWEXSEQ(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 1, 1, value)
#define IE1_GET_TX_DESC_EN_DESC_ID(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 0, 1)
#define IE1_SET_TX_DESC_EN_DESC_ID(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 0, 1, value)
#define IE1_GET_TX_DESC_AMPDU_MAX_TIME(txdesc_ie)                              \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 24, 8)
#define IE1_SET_TX_DESC_AMPDU_MAX_TIME(txdesc_ie, value)                       \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 24, 8, value)
#define IE1_GET_TX_DESC_P_AID(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 15, 9)
#define IE1_SET_TX_DESC_P_AID(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 15, 9, value)
#define IE1_GET_TX_DESC_MOREDATA(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 14, 1)
#define IE1_SET_TX_DESC_MOREDATA(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 14, 1, value)
#define IE1_GET_TX_DESC_SW_SEQ(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 0, 12)
#define IE1_SET_TX_DESC_SW_SEQ(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 0, 12, value)
#define IE2_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 31, 1)
#define IE2_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 31, 1, value)
#define IE2_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 30, 1)
#define IE2_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 30, 1, value)
#define IE2_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 24, 4)
#define IE2_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 24, 4, value)
#define IE2_GET_TX_DESC_ADDR_CAM(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 16, 8)
#define IE2_SET_TX_DESC_ADDR_CAM(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 16, 8, value)
#define IE2_GET_TX_DESC_MULTIPLE_PORT(txdesc_ie)                               \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 12, 3)
#define IE2_SET_TX_DESC_MULTIPLE_PORT(txdesc_ie, value)                        \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 12, 3, value)
#define IE2_GET_TX_DESC_RAW(txdesc_ie) LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 11, 1)
#define IE2_SET_TX_DESC_RAW(txdesc_ie, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 11, 1, value)
#define IE2_GET_TX_DESC_RDG_EN(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 10, 1)
#define IE2_SET_TX_DESC_RDG_EN(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 10, 1, value)
#define IE2_GET_TX_DESC_SPECIAL_CW(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 7, 1)
#define IE2_SET_TX_DESC_SPECIAL_CW(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 7, 1, value)
#define IE2_GET_TX_DESC_POLLUTED(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 6, 1)
#define IE2_SET_TX_DESC_POLLUTED(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 6, 1, value)
#define IE2_GET_TX_DESC_BT_NULL(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 5, 1)
#define IE2_SET_TX_DESC_BT_NULL(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 5, 1, value)
#define IE2_GET_TX_DESC_NULL_1(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 4, 1)
#define IE2_SET_TX_DESC_NULL_1(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 4, 1, value)
#define IE2_GET_TX_DESC_NULL_0(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 3, 1)
#define IE2_SET_TX_DESC_NULL_0(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 3, 1, value)
#define IE2_GET_TX_DESC_TRI_FRAME(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 2, 1)
#define IE2_SET_TX_DESC_TRI_FRAME(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 2, 1, value)
#define IE2_GET_TX_DESC_SPE_RPT(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 1, 1)
#define IE2_SET_TX_DESC_SPE_RPT(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 1, 1, value)
#define IE2_GET_TX_DESC_FTM_EN(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 0, 1)
#define IE2_SET_TX_DESC_FTM_EN(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 0, 1, value)
#define IE2_GET_TX_DESC_MBSSID(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 27, 4)
#define IE2_SET_TX_DESC_MBSSID(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 27, 4, value)
#define IE2_GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc_ie)                         \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 16, 11)
#define IE2_SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc_ie, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 16, 11, value)
#define IE2_GET_TX_DESC_RDG_NAV_EXT(txdesc_ie)                                 \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 15, 1)
#define IE2_SET_TX_DESC_RDG_NAV_EXT(txdesc_ie, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 15, 1, value)
#define IE2_GET_TX_DESC_DROP_ID(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 12, 2)
#define IE2_SET_TX_DESC_DROP_ID(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 12, 2, value)
#define IE2_GET_TX_DESC_SW_DEFINE(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 0, 12)
#define IE2_SET_TX_DESC_SW_DEFINE(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 0, 12, value)
#define IE3_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 31, 1)
#define IE3_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 31, 1, value)
#define IE3_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 30, 1)
#define IE3_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 30, 1, value)
#define IE3_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 24, 4)
#define IE3_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 24, 4, value)
#define IE3_GET_TX_DESC_DATA_SC(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 20, 4)
#define IE3_SET_TX_DESC_DATA_SC(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 20, 4, value)
#define IE3_GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc_ie)                         \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 16, 4)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc_ie, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 16, 4, value)
#define IE3_GET_TX_DESC_CTRL_CNT(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 8, 4)
#define IE3_SET_TX_DESC_CTRL_CNT(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 8, 4, value)
#define IE3_GET_TX_DESC_CTRL_CNT_VALID(txdesc_ie)                              \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 1, 1)
#define IE3_SET_TX_DESC_CTRL_CNT_VALID(txdesc_ie, value)                       \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 1, 1, value)
#define IE3_GET_TX_DESC_SIGNALING_TA_PKT_EN(txdesc_ie)                         \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 0, 1)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_EN(txdesc_ie, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 0, 1, value)
#define IE3_GET_TX_DESC_G_ID(txdesc_ie)                                        \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 24, 6)
#define IE3_SET_TX_DESC_G_ID(txdesc_ie, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 24, 6, value)
#define IE3_GET_TX_DESC_SND_TARGET(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 16, 8)
#define IE3_SET_TX_DESC_SND_TARGET(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 16, 8, value)
#define IE3_GET_TX_DESC_CCA_RTS(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 11, 2)
#define IE3_SET_TX_DESC_CCA_RTS(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 11, 2, value)
#define IE3_GET_TX_DESC_SND_PKT_SEL(txdesc_ie)                                 \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 9, 2)
#define IE3_SET_TX_DESC_SND_PKT_SEL(txdesc_ie, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 9, 2, value)
#define IE3_GET_TX_DESC_NDPA(txdesc_ie) LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 7, 2)
#define IE3_SET_TX_DESC_NDPA(txdesc_ie, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 7, 2, value)
#define IE3_GET_TX_DESC_MU_DATARATE(txdesc_ie)                                 \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 0, 7)
#define IE3_SET_TX_DESC_MU_DATARATE(txdesc_ie, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 0, 7, value)
#define IE4_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 31, 1)
#define IE4_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 31, 1, value)
#define IE4_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 30, 1)
#define IE4_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 30, 1, value)
#define IE4_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 24, 4)
#define IE4_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 24, 4, value)
#define IE4_GET_TX_DESC_VCS_STBC(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 10, 2)
#define IE4_SET_TX_DESC_VCS_STBC(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 10, 2, value)
#define IE4_GET_TX_DESC_DATA_STBC(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 8, 2)
#define IE4_SET_TX_DESC_DATA_STBC(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 8, 2, value)
#define IE4_GET_TX_DESC_DATA_LDPC(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 2, 1)
#define IE4_SET_TX_DESC_DATA_LDPC(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 2, 1, value)
#define IE4_GET_TX_DESC_GF(txdesc_ie) LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 1, 1)
#define IE4_SET_TX_DESC_GF(txdesc_ie, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 1, 1, value)
#define IE4_GET_TX_DESC_LSIG_TXOP_EN(txdesc_ie)                                \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 0, 1)
#define IE4_SET_TX_DESC_LSIG_TXOP_EN(txdesc_ie, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 0, 1, value)
#define IE4_GET_TX_DESC_PATH_MAPA(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 30, 2)
#define IE4_SET_TX_DESC_PATH_MAPA(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 30, 2, value)
#define IE4_GET_TX_DESC_PATH_MAPB(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 28, 2)
#define IE4_SET_TX_DESC_PATH_MAPB(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 28, 2, value)
#define IE4_GET_TX_DESC_PATH_MAPC(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 26, 2)
#define IE4_SET_TX_DESC_PATH_MAPC(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 26, 2, value)
#define IE4_GET_TX_DESC_PATH_MAPD(txdesc_ie)                                   \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 24, 2)
#define IE4_SET_TX_DESC_PATH_MAPD(txdesc_ie, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 24, 2, value)
#define IE4_GET_TX_DESC_ANTSEL_A(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 20, 4)
#define IE4_SET_TX_DESC_ANTSEL_A(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 20, 4, value)
#define IE4_GET_TX_DESC_ANTSEL_B(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 16, 4)
#define IE4_SET_TX_DESC_ANTSEL_B(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 16, 4, value)
#define IE4_GET_TX_DESC_ANTSEL_C(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 12, 4)
#define IE4_SET_TX_DESC_ANTSEL_C(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 12, 4, value)
#define IE4_GET_TX_DESC_ANTSEL_D(txdesc_ie)                                    \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 8, 4)
#define IE4_SET_TX_DESC_ANTSEL_D(txdesc_ie, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 8, 4, value)
#define IE4_GET_TX_DESC_NTX_PATH_EN(txdesc_ie)                                 \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 4, 4)
#define IE4_SET_TX_DESC_NTX_PATH_EN(txdesc_ie, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 4, 4, value)
#define IE4_GET_TX_DESC_ANTLSEL_EN(txdesc_ie)                                  \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 3, 1)
#define IE4_SET_TX_DESC_ANTLSEL_EN(txdesc_ie, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 3, 1, value)
#define IE4_GET_TX_DESC_TXPWR_OFSET_TYPE(txdesc_ie)                            \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 0, 2)
#define IE4_SET_TX_DESC_TXPWR_OFSET_TYPE(txdesc_ie, value)                     \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 0, 2, value)
#define IE5_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 31, 1)
#define IE5_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 31, 1, value)
#define IE5_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 30, 1)
#define IE5_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 30, 1, value)
#define IE5_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 24, 4)
#define IE5_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 24, 4, value)
#define IE5_GET_TX_DESC_LEN1_L(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 17, 7)
#define IE5_SET_TX_DESC_LEN1_L(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 17, 7, value)
#define IE5_GET_TX_DESC_LEN0(txdesc_ie)                                        \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 4, 13)
#define IE5_SET_TX_DESC_LEN0(txdesc_ie, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 4, 13, value)
#define IE5_GET_TX_DESC_PKT_NUM(txdesc_ie)                                     \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x00, 0, 4)
#define IE5_SET_TX_DESC_PKT_NUM(txdesc_ie, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x00, 0, 4, value)
#define IE5_GET_TX_DESC_LEN3(txdesc_ie)                                        \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 19, 13)
#define IE5_SET_TX_DESC_LEN3(txdesc_ie, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 19, 13, value)
#define IE5_GET_TX_DESC_LEN2(txdesc_ie)                                        \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 6, 13)
#define IE5_SET_TX_DESC_LEN2(txdesc_ie, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 6, 13, value)
#define IE5_GET_TX_DESC_LEN1_H(txdesc_ie)                                      \
	LE_BITS_TO_4BYTE(txdesc_ie + 0x04, 0, 6)
#define IE5_SET_TX_DESC_LEN1_H(txdesc_ie, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc_ie + 0x04, 0, 6, value)

#endif

#endif

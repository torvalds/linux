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

#ifndef _HALMAC_TX_DESC_IE_CHIP_H_
#define _HALMAC_TX_DESC_IE_CHIP_H_
#if (HALMAC_8814B_SUPPORT)

#define IE0_GET_TX_DESC_IE_END_8814B(txdesc_ie)                                \
	IE0_GET_TX_DESC_IE_END(txdesc_ie)
#define IE0_SET_TX_DESC_IE_END_8814B(txdesc_ie, value)                         \
	IE0_SET_TX_DESC_IE_END(txdesc_ie, value)
#define IE0_GET_TX_DESC_IE_UP_8814B(txdesc_ie) IE0_GET_TX_DESC_IE_UP(txdesc_ie)
#define IE0_SET_TX_DESC_IE_UP_8814B(txdesc_ie, value)                          \
	IE0_SET_TX_DESC_IE_UP(txdesc_ie, value)
#define IE0_GET_TX_DESC_IE_NUM_8814B(txdesc_ie)                                \
	IE0_GET_TX_DESC_IE_NUM(txdesc_ie)
#define IE0_SET_TX_DESC_IE_NUM_8814B(txdesc_ie, value)                         \
	IE0_SET_TX_DESC_IE_NUM(txdesc_ie, value)
#define IE0_GET_TX_DESC_ARFR_TABLE_SEL_8814B(txdesc_ie)                        \
	IE0_GET_TX_DESC_ARFR_TABLE_SEL(txdesc_ie)
#define IE0_SET_TX_DESC_ARFR_TABLE_SEL_8814B(txdesc_ie, value)                 \
	IE0_SET_TX_DESC_ARFR_TABLE_SEL(txdesc_ie, value)
#define IE0_GET_TX_DESC_ARFR_HT_EN_8814B(txdesc_ie)                            \
	IE0_GET_TX_DESC_ARFR_HT_EN(txdesc_ie)
#define IE0_SET_TX_DESC_ARFR_HT_EN_8814B(txdesc_ie, value)                     \
	IE0_SET_TX_DESC_ARFR_HT_EN(txdesc_ie, value)
#define IE0_GET_TX_DESC_ARFR_OFDM_EN_8814B(txdesc_ie)                          \
	IE0_GET_TX_DESC_ARFR_OFDM_EN(txdesc_ie)
#define IE0_SET_TX_DESC_ARFR_OFDM_EN_8814B(txdesc_ie, value)                   \
	IE0_SET_TX_DESC_ARFR_OFDM_EN(txdesc_ie, value)
#define IE0_GET_TX_DESC_ARFR_CCK_EN_8814B(txdesc_ie)                           \
	IE0_GET_TX_DESC_ARFR_CCK_EN(txdesc_ie)
#define IE0_SET_TX_DESC_ARFR_CCK_EN_8814B(txdesc_ie, value)                    \
	IE0_SET_TX_DESC_ARFR_CCK_EN(txdesc_ie, value)
#define IE0_GET_TX_DESC_HW_RTS_EN_8814B(txdesc_ie)                             \
	IE0_GET_TX_DESC_HW_RTS_EN(txdesc_ie)
#define IE0_SET_TX_DESC_HW_RTS_EN_8814B(txdesc_ie, value)                      \
	IE0_SET_TX_DESC_HW_RTS_EN(txdesc_ie, value)
#define IE0_GET_TX_DESC_RTS_EN_8814B(txdesc_ie)                                \
	IE0_GET_TX_DESC_RTS_EN(txdesc_ie)
#define IE0_SET_TX_DESC_RTS_EN_8814B(txdesc_ie, value)                         \
	IE0_SET_TX_DESC_RTS_EN(txdesc_ie, value)
#define IE0_GET_TX_DESC_CTS2SELF_8814B(txdesc_ie)                              \
	IE0_GET_TX_DESC_CTS2SELF(txdesc_ie)
#define IE0_SET_TX_DESC_CTS2SELF_8814B(txdesc_ie, value)                       \
	IE0_SET_TX_DESC_CTS2SELF(txdesc_ie, value)
#define IE0_GET_TX_DESC_RTY_LMT_EN_8814B(txdesc_ie)                            \
	IE0_GET_TX_DESC_RTY_LMT_EN(txdesc_ie)
#define IE0_SET_TX_DESC_RTY_LMT_EN_8814B(txdesc_ie, value)                     \
	IE0_SET_TX_DESC_RTY_LMT_EN(txdesc_ie, value)
#define IE0_GET_TX_DESC_RTS_SHORT_8814B(txdesc_ie)                             \
	IE0_GET_TX_DESC_RTS_SHORT(txdesc_ie)
#define IE0_SET_TX_DESC_RTS_SHORT_8814B(txdesc_ie, value)                      \
	IE0_SET_TX_DESC_RTS_SHORT(txdesc_ie, value)
#define IE0_GET_TX_DESC_DISDATAFB_8814B(txdesc_ie)                             \
	IE0_GET_TX_DESC_DISDATAFB(txdesc_ie)
#define IE0_SET_TX_DESC_DISDATAFB_8814B(txdesc_ie, value)                      \
	IE0_SET_TX_DESC_DISDATAFB(txdesc_ie, value)
#define IE0_GET_TX_DESC_DISRTSFB_8814B(txdesc_ie)                              \
	IE0_GET_TX_DESC_DISRTSFB(txdesc_ie)
#define IE0_SET_TX_DESC_DISRTSFB_8814B(txdesc_ie, value)                       \
	IE0_SET_TX_DESC_DISRTSFB(txdesc_ie, value)
#define IE0_GET_TX_DESC_DATA_SHORT_8814B(txdesc_ie)                            \
	IE0_GET_TX_DESC_DATA_SHORT(txdesc_ie)
#define IE0_SET_TX_DESC_DATA_SHORT_8814B(txdesc_ie, value)                     \
	IE0_SET_TX_DESC_DATA_SHORT(txdesc_ie, value)
#define IE0_GET_TX_DESC_TRY_RATE_8814B(txdesc_ie)                              \
	IE0_GET_TX_DESC_TRY_RATE(txdesc_ie)
#define IE0_SET_TX_DESC_TRY_RATE_8814B(txdesc_ie, value)                       \
	IE0_SET_TX_DESC_TRY_RATE(txdesc_ie, value)
#define IE0_GET_TX_DESC_USERATE_8814B(txdesc_ie)                               \
	IE0_GET_TX_DESC_USERATE(txdesc_ie)
#define IE0_SET_TX_DESC_USERATE_8814B(txdesc_ie, value)                        \
	IE0_SET_TX_DESC_USERATE(txdesc_ie, value)
#define IE0_GET_TX_DESC_RTS_RTY_LOWEST_RATE_8814B(txdesc_ie)                   \
	IE0_GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc_ie)
#define IE0_SET_TX_DESC_RTS_RTY_LOWEST_RATE_8814B(txdesc_ie, value)            \
	IE0_SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc_ie, value)
#define IE0_GET_TX_DESC_DATA_RTY_LOWEST_RATE_8814B(txdesc_ie)                  \
	IE0_GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc_ie)
#define IE0_SET_TX_DESC_DATA_RTY_LOWEST_RATE_8814B(txdesc_ie, value)           \
	IE0_SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc_ie, value)
#define IE0_GET_TX_DESC_RTS_DATA_RTY_LMT_8814B(txdesc_ie)                      \
	IE0_GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc_ie)
#define IE0_SET_TX_DESC_RTS_DATA_RTY_LMT_8814B(txdesc_ie, value)               \
	IE0_SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc_ie, value)
#define IE0_GET_TX_DESC_DATA_BW_8814B(txdesc_ie)                               \
	IE0_GET_TX_DESC_DATA_BW(txdesc_ie)
#define IE0_SET_TX_DESC_DATA_BW_8814B(txdesc_ie, value)                        \
	IE0_SET_TX_DESC_DATA_BW(txdesc_ie, value)
#define IE0_GET_TX_DESC_RTSRATE_8814B(txdesc_ie)                               \
	IE0_GET_TX_DESC_RTSRATE(txdesc_ie)
#define IE0_SET_TX_DESC_RTSRATE_8814B(txdesc_ie, value)                        \
	IE0_SET_TX_DESC_RTSRATE(txdesc_ie, value)
#define IE0_GET_TX_DESC_DATARATE_8814B(txdesc_ie)                              \
	IE0_GET_TX_DESC_DATARATE(txdesc_ie)
#define IE0_SET_TX_DESC_DATARATE_8814B(txdesc_ie, value)                       \
	IE0_SET_TX_DESC_DATARATE(txdesc_ie, value)
#define IE1_GET_TX_DESC_IE_END_8814B(txdesc_ie)                                \
	IE1_GET_TX_DESC_IE_END(txdesc_ie)
#define IE1_SET_TX_DESC_IE_END_8814B(txdesc_ie, value)                         \
	IE1_SET_TX_DESC_IE_END(txdesc_ie, value)
#define IE1_GET_TX_DESC_IE_UP_8814B(txdesc_ie) IE1_GET_TX_DESC_IE_UP(txdesc_ie)
#define IE1_SET_TX_DESC_IE_UP_8814B(txdesc_ie, value)                          \
	IE1_SET_TX_DESC_IE_UP(txdesc_ie, value)
#define IE1_GET_TX_DESC_IE_NUM_8814B(txdesc_ie)                                \
	IE1_GET_TX_DESC_IE_NUM(txdesc_ie)
#define IE1_SET_TX_DESC_IE_NUM_8814B(txdesc_ie, value)                         \
	IE1_SET_TX_DESC_IE_NUM(txdesc_ie, value)
#define IE1_GET_TX_DESC_AMPDU_DENSITY_8814B(txdesc_ie)                         \
	IE1_GET_TX_DESC_AMPDU_DENSITY(txdesc_ie)
#define IE1_SET_TX_DESC_AMPDU_DENSITY_8814B(txdesc_ie, value)                  \
	IE1_SET_TX_DESC_AMPDU_DENSITY(txdesc_ie, value)
#define IE1_GET_TX_DESC_MAX_AGG_NUM_8814B(txdesc_ie)                           \
	IE1_GET_TX_DESC_MAX_AGG_NUM(txdesc_ie)
#define IE1_SET_TX_DESC_MAX_AGG_NUM_8814B(txdesc_ie, value)                    \
	IE1_SET_TX_DESC_MAX_AGG_NUM(txdesc_ie, value)
#define IE1_GET_TX_DESC_SECTYPE_8814B(txdesc_ie)                               \
	IE1_GET_TX_DESC_SECTYPE(txdesc_ie)
#define IE1_SET_TX_DESC_SECTYPE_8814B(txdesc_ie, value)                        \
	IE1_SET_TX_DESC_SECTYPE(txdesc_ie, value)
#define IE1_GET_TX_DESC_MOREFRAG_8814B(txdesc_ie)                              \
	IE1_GET_TX_DESC_MOREFRAG(txdesc_ie)
#define IE1_SET_TX_DESC_MOREFRAG_8814B(txdesc_ie, value)                       \
	IE1_SET_TX_DESC_MOREFRAG(txdesc_ie, value)
#define IE1_GET_TX_DESC_NOACM_8814B(txdesc_ie) IE1_GET_TX_DESC_NOACM(txdesc_ie)
#define IE1_SET_TX_DESC_NOACM_8814B(txdesc_ie, value)                          \
	IE1_SET_TX_DESC_NOACM(txdesc_ie, value)
#define IE1_GET_TX_DESC_BCNPKT_TSF_CTRL_8814B(txdesc_ie)                       \
	IE1_GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc_ie)
#define IE1_SET_TX_DESC_BCNPKT_TSF_CTRL_8814B(txdesc_ie, value)                \
	IE1_SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc_ie, value)
#define IE1_GET_TX_DESC_NAVUSEHDR_8814B(txdesc_ie)                             \
	IE1_GET_TX_DESC_NAVUSEHDR(txdesc_ie)
#define IE1_SET_TX_DESC_NAVUSEHDR_8814B(txdesc_ie, value)                      \
	IE1_SET_TX_DESC_NAVUSEHDR(txdesc_ie, value)
#define IE1_GET_TX_DESC_HTC_8814B(txdesc_ie) IE1_GET_TX_DESC_HTC(txdesc_ie)
#define IE1_SET_TX_DESC_HTC_8814B(txdesc_ie, value)                            \
	IE1_SET_TX_DESC_HTC(txdesc_ie, value)
#define IE1_GET_TX_DESC_BMC_8814B(txdesc_ie) IE1_GET_TX_DESC_BMC(txdesc_ie)
#define IE1_SET_TX_DESC_BMC_8814B(txdesc_ie, value)                            \
	IE1_SET_TX_DESC_BMC(txdesc_ie, value)
#define IE1_GET_TX_DESC_TX_PKT_AFTER_PIFS_8814B(txdesc_ie)                     \
	IE1_GET_TX_DESC_TX_PKT_AFTER_PIFS(txdesc_ie)
#define IE1_SET_TX_DESC_TX_PKT_AFTER_PIFS_8814B(txdesc_ie, value)              \
	IE1_SET_TX_DESC_TX_PKT_AFTER_PIFS(txdesc_ie, value)
#define IE1_GET_TX_DESC_USE_MAX_TIME_EN_8814B(txdesc_ie)                       \
	IE1_GET_TX_DESC_USE_MAX_TIME_EN(txdesc_ie)
#define IE1_SET_TX_DESC_USE_MAX_TIME_EN_8814B(txdesc_ie, value)                \
	IE1_SET_TX_DESC_USE_MAX_TIME_EN(txdesc_ie, value)
#define IE1_GET_TX_DESC_HW_SSN_SEL_8814B(txdesc_ie)                            \
	IE1_GET_TX_DESC_HW_SSN_SEL(txdesc_ie)
#define IE1_SET_TX_DESC_HW_SSN_SEL_8814B(txdesc_ie, value)                     \
	IE1_SET_TX_DESC_HW_SSN_SEL(txdesc_ie, value)
#define IE1_GET_TX_DESC_DISQSELSEQ_8814B(txdesc_ie)                            \
	IE1_GET_TX_DESC_DISQSELSEQ(txdesc_ie)
#define IE1_SET_TX_DESC_DISQSELSEQ_8814B(txdesc_ie, value)                     \
	IE1_SET_TX_DESC_DISQSELSEQ(txdesc_ie, value)
#define IE1_GET_TX_DESC_EN_HWSEQ_8814B(txdesc_ie)                              \
	IE1_GET_TX_DESC_EN_HWSEQ(txdesc_ie)
#define IE1_SET_TX_DESC_EN_HWSEQ_8814B(txdesc_ie, value)                       \
	IE1_SET_TX_DESC_EN_HWSEQ(txdesc_ie, value)
#define IE1_GET_TX_DESC_EN_HWEXSEQ_8814B(txdesc_ie)                            \
	IE1_GET_TX_DESC_EN_HWEXSEQ(txdesc_ie)
#define IE1_SET_TX_DESC_EN_HWEXSEQ_8814B(txdesc_ie, value)                     \
	IE1_SET_TX_DESC_EN_HWEXSEQ(txdesc_ie, value)
#define IE1_GET_TX_DESC_EN_DESC_ID_8814B(txdesc_ie)                            \
	IE1_GET_TX_DESC_EN_DESC_ID(txdesc_ie)
#define IE1_SET_TX_DESC_EN_DESC_ID_8814B(txdesc_ie, value)                     \
	IE1_SET_TX_DESC_EN_DESC_ID(txdesc_ie, value)
#define IE1_GET_TX_DESC_AMPDU_MAX_TIME_8814B(txdesc_ie)                        \
	IE1_GET_TX_DESC_AMPDU_MAX_TIME(txdesc_ie)
#define IE1_SET_TX_DESC_AMPDU_MAX_TIME_8814B(txdesc_ie, value)                 \
	IE1_SET_TX_DESC_AMPDU_MAX_TIME(txdesc_ie, value)
#define IE1_GET_TX_DESC_P_AID_8814B(txdesc_ie) IE1_GET_TX_DESC_P_AID(txdesc_ie)
#define IE1_SET_TX_DESC_P_AID_8814B(txdesc_ie, value)                          \
	IE1_SET_TX_DESC_P_AID(txdesc_ie, value)
#define IE1_GET_TX_DESC_MOREDATA_8814B(txdesc_ie)                              \
	IE1_GET_TX_DESC_MOREDATA(txdesc_ie)
#define IE1_SET_TX_DESC_MOREDATA_8814B(txdesc_ie, value)                       \
	IE1_SET_TX_DESC_MOREDATA(txdesc_ie, value)
#define IE1_GET_TX_DESC_SW_SEQ_8814B(txdesc_ie)                                \
	IE1_GET_TX_DESC_SW_SEQ(txdesc_ie)
#define IE1_SET_TX_DESC_SW_SEQ_8814B(txdesc_ie, value)                         \
	IE1_SET_TX_DESC_SW_SEQ(txdesc_ie, value)
#define IE2_GET_TX_DESC_IE_END_8814B(txdesc_ie)                                \
	IE2_GET_TX_DESC_IE_END(txdesc_ie)
#define IE2_SET_TX_DESC_IE_END_8814B(txdesc_ie, value)                         \
	IE2_SET_TX_DESC_IE_END(txdesc_ie, value)
#define IE2_GET_TX_DESC_IE_UP_8814B(txdesc_ie) IE2_GET_TX_DESC_IE_UP(txdesc_ie)
#define IE2_SET_TX_DESC_IE_UP_8814B(txdesc_ie, value)                          \
	IE2_SET_TX_DESC_IE_UP(txdesc_ie, value)
#define IE2_GET_TX_DESC_IE_NUM_8814B(txdesc_ie)                                \
	IE2_GET_TX_DESC_IE_NUM(txdesc_ie)
#define IE2_SET_TX_DESC_IE_NUM_8814B(txdesc_ie, value)                         \
	IE2_SET_TX_DESC_IE_NUM(txdesc_ie, value)
#define IE2_GET_TX_DESC_ADDR_CAM_8814B(txdesc_ie)                              \
	IE2_GET_TX_DESC_ADDR_CAM(txdesc_ie)
#define IE2_SET_TX_DESC_ADDR_CAM_8814B(txdesc_ie, value)                       \
	IE2_SET_TX_DESC_ADDR_CAM(txdesc_ie, value)
#define IE2_GET_TX_DESC_MULTIPLE_PORT_8814B(txdesc_ie)                         \
	IE2_GET_TX_DESC_MULTIPLE_PORT(txdesc_ie)
#define IE2_SET_TX_DESC_MULTIPLE_PORT_8814B(txdesc_ie, value)                  \
	IE2_SET_TX_DESC_MULTIPLE_PORT(txdesc_ie, value)
#define IE2_GET_TX_DESC_RAW_8814B(txdesc_ie) IE2_GET_TX_DESC_RAW(txdesc_ie)
#define IE2_SET_TX_DESC_RAW_8814B(txdesc_ie, value)                            \
	IE2_SET_TX_DESC_RAW(txdesc_ie, value)
#define IE2_GET_TX_DESC_RDG_EN_8814B(txdesc_ie)                                \
	IE2_GET_TX_DESC_RDG_EN(txdesc_ie)
#define IE2_SET_TX_DESC_RDG_EN_8814B(txdesc_ie, value)                         \
	IE2_SET_TX_DESC_RDG_EN(txdesc_ie, value)
#define IE2_GET_TX_DESC_SPECIAL_CW_8814B(txdesc_ie)                            \
	IE2_GET_TX_DESC_SPECIAL_CW(txdesc_ie)
#define IE2_SET_TX_DESC_SPECIAL_CW_8814B(txdesc_ie, value)                     \
	IE2_SET_TX_DESC_SPECIAL_CW(txdesc_ie, value)
#define IE2_GET_TX_DESC_POLLUTED_8814B(txdesc_ie)                              \
	IE2_GET_TX_DESC_POLLUTED(txdesc_ie)
#define IE2_SET_TX_DESC_POLLUTED_8814B(txdesc_ie, value)                       \
	IE2_SET_TX_DESC_POLLUTED(txdesc_ie, value)
#define IE2_GET_TX_DESC_BT_NULL_8814B(txdesc_ie)                               \
	IE2_GET_TX_DESC_BT_NULL(txdesc_ie)
#define IE2_SET_TX_DESC_BT_NULL_8814B(txdesc_ie, value)                        \
	IE2_SET_TX_DESC_BT_NULL(txdesc_ie, value)
#define IE2_GET_TX_DESC_NULL_1_8814B(txdesc_ie)                                \
	IE2_GET_TX_DESC_NULL_1(txdesc_ie)
#define IE2_SET_TX_DESC_NULL_1_8814B(txdesc_ie, value)                         \
	IE2_SET_TX_DESC_NULL_1(txdesc_ie, value)
#define IE2_GET_TX_DESC_NULL_0_8814B(txdesc_ie)                                \
	IE2_GET_TX_DESC_NULL_0(txdesc_ie)
#define IE2_SET_TX_DESC_NULL_0_8814B(txdesc_ie, value)                         \
	IE2_SET_TX_DESC_NULL_0(txdesc_ie, value)
#define IE2_GET_TX_DESC_TRI_FRAME_8814B(txdesc_ie)                             \
	IE2_GET_TX_DESC_TRI_FRAME(txdesc_ie)
#define IE2_SET_TX_DESC_TRI_FRAME_8814B(txdesc_ie, value)                      \
	IE2_SET_TX_DESC_TRI_FRAME(txdesc_ie, value)
#define IE2_GET_TX_DESC_SPE_RPT_8814B(txdesc_ie)                               \
	IE2_GET_TX_DESC_SPE_RPT(txdesc_ie)
#define IE2_SET_TX_DESC_SPE_RPT_8814B(txdesc_ie, value)                        \
	IE2_SET_TX_DESC_SPE_RPT(txdesc_ie, value)
#define IE2_GET_TX_DESC_FTM_EN_8814B(txdesc_ie)                                \
	IE2_GET_TX_DESC_FTM_EN(txdesc_ie)
#define IE2_SET_TX_DESC_FTM_EN_8814B(txdesc_ie, value)                         \
	IE2_SET_TX_DESC_FTM_EN(txdesc_ie, value)
#define IE2_GET_TX_DESC_MBSSID_8814B(txdesc_ie)                                \
	IE2_GET_TX_DESC_MBSSID(txdesc_ie)
#define IE2_SET_TX_DESC_MBSSID_8814B(txdesc_ie, value)                         \
	IE2_SET_TX_DESC_MBSSID(txdesc_ie, value)
#define IE2_GET_TX_DESC_GROUP_BIT_IE_OFFSET_8814B(txdesc_ie)                   \
	IE2_GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc_ie)
#define IE2_SET_TX_DESC_GROUP_BIT_IE_OFFSET_8814B(txdesc_ie, value)            \
	IE2_SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc_ie, value)
#define IE2_GET_TX_DESC_RDG_NAV_EXT_8814B(txdesc_ie)                           \
	IE2_GET_TX_DESC_RDG_NAV_EXT(txdesc_ie)
#define IE2_SET_TX_DESC_RDG_NAV_EXT_8814B(txdesc_ie, value)                    \
	IE2_SET_TX_DESC_RDG_NAV_EXT(txdesc_ie, value)
#define IE2_GET_TX_DESC_DROP_ID_8814B(txdesc_ie)                               \
	IE2_GET_TX_DESC_DROP_ID(txdesc_ie)
#define IE2_SET_TX_DESC_DROP_ID_8814B(txdesc_ie, value)                        \
	IE2_SET_TX_DESC_DROP_ID(txdesc_ie, value)
#define IE2_GET_TX_DESC_SW_DEFINE_8814B(txdesc_ie)                             \
	IE2_GET_TX_DESC_SW_DEFINE(txdesc_ie)
#define IE2_SET_TX_DESC_SW_DEFINE_8814B(txdesc_ie, value)                      \
	IE2_SET_TX_DESC_SW_DEFINE(txdesc_ie, value)
#define IE3_GET_TX_DESC_IE_END_8814B(txdesc_ie)                                \
	IE3_GET_TX_DESC_IE_END(txdesc_ie)
#define IE3_SET_TX_DESC_IE_END_8814B(txdesc_ie, value)                         \
	IE3_SET_TX_DESC_IE_END(txdesc_ie, value)
#define IE3_GET_TX_DESC_IE_UP_8814B(txdesc_ie) IE3_GET_TX_DESC_IE_UP(txdesc_ie)
#define IE3_SET_TX_DESC_IE_UP_8814B(txdesc_ie, value)                          \
	IE3_SET_TX_DESC_IE_UP(txdesc_ie, value)
#define IE3_GET_TX_DESC_IE_NUM_8814B(txdesc_ie)                                \
	IE3_GET_TX_DESC_IE_NUM(txdesc_ie)
#define IE3_SET_TX_DESC_IE_NUM_8814B(txdesc_ie, value)                         \
	IE3_SET_TX_DESC_IE_NUM(txdesc_ie, value)
#define IE3_GET_TX_DESC_DATA_SC_8814B(txdesc_ie)                               \
	IE3_GET_TX_DESC_DATA_SC(txdesc_ie)
#define IE3_SET_TX_DESC_DATA_SC_8814B(txdesc_ie, value)                        \
	IE3_SET_TX_DESC_DATA_SC(txdesc_ie, value)
#define IE3_GET_TX_DESC_SIGNALING_TA_PKT_SC_8814B(txdesc_ie)                   \
	IE3_GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc_ie)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_SC_8814B(txdesc_ie, value)            \
	IE3_SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc_ie, value)
#define IE3_GET_TX_DESC_CTRL_CNT_8814B(txdesc_ie)                              \
	IE3_GET_TX_DESC_CTRL_CNT(txdesc_ie)
#define IE3_SET_TX_DESC_CTRL_CNT_8814B(txdesc_ie, value)                       \
	IE3_SET_TX_DESC_CTRL_CNT(txdesc_ie, value)
#define IE3_GET_TX_DESC_CTRL_CNT_VALID_8814B(txdesc_ie)                        \
	IE3_GET_TX_DESC_CTRL_CNT_VALID(txdesc_ie)
#define IE3_SET_TX_DESC_CTRL_CNT_VALID_8814B(txdesc_ie, value)                 \
	IE3_SET_TX_DESC_CTRL_CNT_VALID(txdesc_ie, value)
#define IE3_GET_TX_DESC_SIGNALING_TA_PKT_EN_8814B(txdesc_ie)                   \
	IE3_GET_TX_DESC_SIGNALING_TA_PKT_EN(txdesc_ie)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_EN_8814B(txdesc_ie, value)            \
	IE3_SET_TX_DESC_SIGNALING_TA_PKT_EN(txdesc_ie, value)
#define IE3_GET_TX_DESC_G_ID_8814B(txdesc_ie) IE3_GET_TX_DESC_G_ID(txdesc_ie)
#define IE3_SET_TX_DESC_G_ID_8814B(txdesc_ie, value)                           \
	IE3_SET_TX_DESC_G_ID(txdesc_ie, value)
#define IE3_GET_TX_DESC_SND_TARGET_8814B(txdesc_ie)                            \
	IE3_GET_TX_DESC_SND_TARGET(txdesc_ie)
#define IE3_SET_TX_DESC_SND_TARGET_8814B(txdesc_ie, value)                     \
	IE3_SET_TX_DESC_SND_TARGET(txdesc_ie, value)
#define IE3_GET_TX_DESC_CCA_RTS_8814B(txdesc_ie)                               \
	IE3_GET_TX_DESC_CCA_RTS(txdesc_ie)
#define IE3_SET_TX_DESC_CCA_RTS_8814B(txdesc_ie, value)                        \
	IE3_SET_TX_DESC_CCA_RTS(txdesc_ie, value)
#define IE3_GET_TX_DESC_SND_PKT_SEL_8814B(txdesc_ie)                           \
	IE3_GET_TX_DESC_SND_PKT_SEL(txdesc_ie)
#define IE3_SET_TX_DESC_SND_PKT_SEL_8814B(txdesc_ie, value)                    \
	IE3_SET_TX_DESC_SND_PKT_SEL(txdesc_ie, value)
#define IE3_GET_TX_DESC_NDPA_8814B(txdesc_ie) IE3_GET_TX_DESC_NDPA(txdesc_ie)
#define IE3_SET_TX_DESC_NDPA_8814B(txdesc_ie, value)                           \
	IE3_SET_TX_DESC_NDPA(txdesc_ie, value)
#define IE3_GET_TX_DESC_MU_DATARATE_8814B(txdesc_ie)                           \
	IE3_GET_TX_DESC_MU_DATARATE(txdesc_ie)
#define IE3_SET_TX_DESC_MU_DATARATE_8814B(txdesc_ie, value)                    \
	IE3_SET_TX_DESC_MU_DATARATE(txdesc_ie, value)
#define IE4_GET_TX_DESC_IE_END_8814B(txdesc_ie)                                \
	IE4_GET_TX_DESC_IE_END(txdesc_ie)
#define IE4_SET_TX_DESC_IE_END_8814B(txdesc_ie, value)                         \
	IE4_SET_TX_DESC_IE_END(txdesc_ie, value)
#define IE4_GET_TX_DESC_IE_UP_8814B(txdesc_ie) IE4_GET_TX_DESC_IE_UP(txdesc_ie)
#define IE4_SET_TX_DESC_IE_UP_8814B(txdesc_ie, value)                          \
	IE4_SET_TX_DESC_IE_UP(txdesc_ie, value)
#define IE4_GET_TX_DESC_IE_NUM_8814B(txdesc_ie)                                \
	IE4_GET_TX_DESC_IE_NUM(txdesc_ie)
#define IE4_SET_TX_DESC_IE_NUM_8814B(txdesc_ie, value)                         \
	IE4_SET_TX_DESC_IE_NUM(txdesc_ie, value)
#define IE4_GET_TX_DESC_VCS_STBC_8814B(txdesc_ie)                              \
	IE4_GET_TX_DESC_VCS_STBC(txdesc_ie)
#define IE4_SET_TX_DESC_VCS_STBC_8814B(txdesc_ie, value)                       \
	IE4_SET_TX_DESC_VCS_STBC(txdesc_ie, value)
#define IE4_GET_TX_DESC_DATA_STBC_8814B(txdesc_ie)                             \
	IE4_GET_TX_DESC_DATA_STBC(txdesc_ie)
#define IE4_SET_TX_DESC_DATA_STBC_8814B(txdesc_ie, value)                      \
	IE4_SET_TX_DESC_DATA_STBC(txdesc_ie, value)
#define IE4_GET_TX_DESC_DATA_LDPC_8814B(txdesc_ie)                             \
	IE4_GET_TX_DESC_DATA_LDPC(txdesc_ie)
#define IE4_SET_TX_DESC_DATA_LDPC_8814B(txdesc_ie, value)                      \
	IE4_SET_TX_DESC_DATA_LDPC(txdesc_ie, value)
#define IE4_GET_TX_DESC_GF_8814B(txdesc_ie) IE4_GET_TX_DESC_GF(txdesc_ie)
#define IE4_SET_TX_DESC_GF_8814B(txdesc_ie, value)                             \
	IE4_SET_TX_DESC_GF(txdesc_ie, value)
#define IE4_GET_TX_DESC_LSIG_TXOP_EN_8814B(txdesc_ie)                          \
	IE4_GET_TX_DESC_LSIG_TXOP_EN(txdesc_ie)
#define IE4_SET_TX_DESC_LSIG_TXOP_EN_8814B(txdesc_ie, value)                   \
	IE4_SET_TX_DESC_LSIG_TXOP_EN(txdesc_ie, value)
#define IE4_GET_TX_DESC_PATH_MAPA_8814B(txdesc_ie)                             \
	IE4_GET_TX_DESC_PATH_MAPA(txdesc_ie)
#define IE4_SET_TX_DESC_PATH_MAPA_8814B(txdesc_ie, value)                      \
	IE4_SET_TX_DESC_PATH_MAPA(txdesc_ie, value)
#define IE4_GET_TX_DESC_PATH_MAPB_8814B(txdesc_ie)                             \
	IE4_GET_TX_DESC_PATH_MAPB(txdesc_ie)
#define IE4_SET_TX_DESC_PATH_MAPB_8814B(txdesc_ie, value)                      \
	IE4_SET_TX_DESC_PATH_MAPB(txdesc_ie, value)
#define IE4_GET_TX_DESC_PATH_MAPC_8814B(txdesc_ie)                             \
	IE4_GET_TX_DESC_PATH_MAPC(txdesc_ie)
#define IE4_SET_TX_DESC_PATH_MAPC_8814B(txdesc_ie, value)                      \
	IE4_SET_TX_DESC_PATH_MAPC(txdesc_ie, value)
#define IE4_GET_TX_DESC_PATH_MAPD_8814B(txdesc_ie)                             \
	IE4_GET_TX_DESC_PATH_MAPD(txdesc_ie)
#define IE4_SET_TX_DESC_PATH_MAPD_8814B(txdesc_ie, value)                      \
	IE4_SET_TX_DESC_PATH_MAPD(txdesc_ie, value)
#define IE4_GET_TX_DESC_ANTSEL_A_8814B(txdesc_ie)                              \
	IE4_GET_TX_DESC_ANTSEL_A(txdesc_ie)
#define IE4_SET_TX_DESC_ANTSEL_A_8814B(txdesc_ie, value)                       \
	IE4_SET_TX_DESC_ANTSEL_A(txdesc_ie, value)
#define IE4_GET_TX_DESC_ANTSEL_B_8814B(txdesc_ie)                              \
	IE4_GET_TX_DESC_ANTSEL_B(txdesc_ie)
#define IE4_SET_TX_DESC_ANTSEL_B_8814B(txdesc_ie, value)                       \
	IE4_SET_TX_DESC_ANTSEL_B(txdesc_ie, value)
#define IE4_GET_TX_DESC_ANTSEL_C_8814B(txdesc_ie)                              \
	IE4_GET_TX_DESC_ANTSEL_C(txdesc_ie)
#define IE4_SET_TX_DESC_ANTSEL_C_8814B(txdesc_ie, value)                       \
	IE4_SET_TX_DESC_ANTSEL_C(txdesc_ie, value)
#define IE4_GET_TX_DESC_ANTSEL_D_8814B(txdesc_ie)                              \
	IE4_GET_TX_DESC_ANTSEL_D(txdesc_ie)
#define IE4_SET_TX_DESC_ANTSEL_D_8814B(txdesc_ie, value)                       \
	IE4_SET_TX_DESC_ANTSEL_D(txdesc_ie, value)
#define IE4_GET_TX_DESC_NTX_PATH_EN_8814B(txdesc_ie)                           \
	IE4_GET_TX_DESC_NTX_PATH_EN(txdesc_ie)
#define IE4_SET_TX_DESC_NTX_PATH_EN_8814B(txdesc_ie, value)                    \
	IE4_SET_TX_DESC_NTX_PATH_EN(txdesc_ie, value)
#define IE4_GET_TX_DESC_ANTLSEL_EN_8814B(txdesc_ie)                            \
	IE4_GET_TX_DESC_ANTLSEL_EN(txdesc_ie)
#define IE4_SET_TX_DESC_ANTLSEL_EN_8814B(txdesc_ie, value)                     \
	IE4_SET_TX_DESC_ANTLSEL_EN(txdesc_ie, value)
#define IE4_GET_TX_DESC_TXPWR_OFSET_TYPE_8814B(txdesc_ie)                      \
	IE4_GET_TX_DESC_TXPWR_OFSET_TYPE(txdesc_ie)
#define IE4_SET_TX_DESC_TXPWR_OFSET_TYPE_8814B(txdesc_ie, value)               \
	IE4_SET_TX_DESC_TXPWR_OFSET_TYPE(txdesc_ie, value)
#define IE5_GET_TX_DESC_IE_END_8814B(txdesc_ie)                                \
	IE5_GET_TX_DESC_IE_END(txdesc_ie)
#define IE5_SET_TX_DESC_IE_END_8814B(txdesc_ie, value)                         \
	IE5_SET_TX_DESC_IE_END(txdesc_ie, value)
#define IE5_GET_TX_DESC_IE_UP_8814B(txdesc_ie) IE5_GET_TX_DESC_IE_UP(txdesc_ie)
#define IE5_SET_TX_DESC_IE_UP_8814B(txdesc_ie, value)                          \
	IE5_SET_TX_DESC_IE_UP(txdesc_ie, value)
#define IE5_GET_TX_DESC_IE_NUM_8814B(txdesc_ie)                                \
	IE5_GET_TX_DESC_IE_NUM(txdesc_ie)
#define IE5_SET_TX_DESC_IE_NUM_8814B(txdesc_ie, value)                         \
	IE5_SET_TX_DESC_IE_NUM(txdesc_ie, value)
#define IE5_GET_TX_DESC_LEN1_L_8814B(txdesc_ie)                                \
	IE5_GET_TX_DESC_LEN1_L(txdesc_ie)
#define IE5_SET_TX_DESC_LEN1_L_8814B(txdesc_ie, value)                         \
	IE5_SET_TX_DESC_LEN1_L(txdesc_ie, value)
#define IE5_GET_TX_DESC_LEN0_8814B(txdesc_ie) IE5_GET_TX_DESC_LEN0(txdesc_ie)
#define IE5_SET_TX_DESC_LEN0_8814B(txdesc_ie, value)                           \
	IE5_SET_TX_DESC_LEN0(txdesc_ie, value)
#define IE5_GET_TX_DESC_PKT_NUM_8814B(txdesc_ie)                               \
	IE5_GET_TX_DESC_PKT_NUM(txdesc_ie)
#define IE5_SET_TX_DESC_PKT_NUM_8814B(txdesc_ie, value)                        \
	IE5_SET_TX_DESC_PKT_NUM(txdesc_ie, value)
#define IE5_GET_TX_DESC_LEN3_8814B(txdesc_ie) IE5_GET_TX_DESC_LEN3(txdesc_ie)
#define IE5_SET_TX_DESC_LEN3_8814B(txdesc_ie, value)                           \
	IE5_SET_TX_DESC_LEN3(txdesc_ie, value)
#define IE5_GET_TX_DESC_LEN2_8814B(txdesc_ie) IE5_GET_TX_DESC_LEN2(txdesc_ie)
#define IE5_SET_TX_DESC_LEN2_8814B(txdesc_ie, value)                           \
	IE5_SET_TX_DESC_LEN2(txdesc_ie, value)
#define IE5_GET_TX_DESC_LEN1_H_8814B(txdesc_ie)                                \
	IE5_GET_TX_DESC_LEN1_H(txdesc_ie)
#define IE5_SET_TX_DESC_LEN1_H_8814B(txdesc_ie, value)                         \
	IE5_SET_TX_DESC_LEN1_H(txdesc_ie, value)

#endif

#endif

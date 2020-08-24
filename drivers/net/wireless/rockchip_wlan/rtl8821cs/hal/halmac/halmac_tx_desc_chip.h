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

#ifndef _HALMAC_TX_DESC_CHIP_H_
#define _HALMAC_TX_DESC_CHIP_H_
#if (HALMAC_8814A_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8814A(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8814A(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8814A(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8814A(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8814A(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8814A(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8814A(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8814A(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8814A(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8814A(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8814A(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8814A(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8814A(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8814A(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8814A(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8814A(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8814A(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8814A(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_MOREDATA_8814A(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8814A(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8814A(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8814A(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8814A(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8814A(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8814A(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8814A(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8814A(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8814A(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8814A(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8814A(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8814A(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8814A(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8814A(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8814A(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8814A(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8814A(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8814A(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8814A(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV_8814A(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8814A(txdesc) GET_TX_DESC_HW_AES_IV(txdesc)
#define SET_TX_DESC_G_ID_8814A(txdesc, value) SET_TX_DESC_G_ID(txdesc, value)
#define GET_TX_DESC_G_ID_8814A(txdesc) GET_TX_DESC_G_ID(txdesc)
#define SET_TX_DESC_BT_NULL_8814A(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8814A(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8814A(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8814A(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8814A(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8814A(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8814A(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8814A(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8814A(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8814A(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8814A(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8814A(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8814A(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8814A(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8814A(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8814A(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8814A(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8814A(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8814A(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8814A(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8814A(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8814A(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_P_AID_8814A(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8814A(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8814A(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8814A(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8814A(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8814A(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8814A(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8814A(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8814A(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8814A(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8814A(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8814A(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8814A(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8814A(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8814A(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8814A(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8814A(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8814A(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8814A(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8814A(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8814A(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8814A(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8814A(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8814A(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8814A(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8814A(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8814A(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8814A(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8814A(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8814A(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8814A(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8814A(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8814A(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8814A(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8814A(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8814A(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8814A(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8814A(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8814A(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8814A(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8814A(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8814A(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8814A(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8814A(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8814A(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8814A(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8814A(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8814A(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8814A(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8814A(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_8814A(txdesc, value)                           \
	SET_TX_DESC_TXPWR_OFSET(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_8814A(txdesc) GET_TX_DESC_TXPWR_OFSET(txdesc)
#define SET_TX_DESC_TX_ANT_8814A(txdesc, value)                                \
	SET_TX_DESC_TX_ANT(txdesc, value)
#define GET_TX_DESC_TX_ANT_8814A(txdesc) GET_TX_DESC_TX_ANT(txdesc)
#define SET_TX_DESC_PORT_ID_8814A(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8814A(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8814A(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8814A(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_RTS_SC_8814A(txdesc, value)                                \
	SET_TX_DESC_RTS_SC(txdesc, value)
#define GET_TX_DESC_RTS_SC_8814A(txdesc) GET_TX_DESC_RTS_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8814A(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8814A(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8814A(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8814A(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8814A(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8814A(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8814A(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8814A(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8814A(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8814A(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8814A(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8814A(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8814A(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8814A(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D_8814A(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_D(txdesc, value)
#define GET_TX_DESC_ANTSEL_D_8814A(txdesc) GET_TX_DESC_ANTSEL_D(txdesc)
#define SET_TX_DESC_ANT_MAPD_8814A(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8814A(txdesc) GET_TX_DESC_ANT_MAPD(txdesc)
#define SET_TX_DESC_ANT_MAPC_8814A(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8814A(txdesc) GET_TX_DESC_ANT_MAPC(txdesc)
#define SET_TX_DESC_ANT_MAPB_8814A(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8814A(txdesc) GET_TX_DESC_ANT_MAPB(txdesc)
#define SET_TX_DESC_ANT_MAPA_8814A(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8814A(txdesc) GET_TX_DESC_ANT_MAPA(txdesc)
#define SET_TX_DESC_ANTSEL_C_8814A(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8814A(txdesc) GET_TX_DESC_ANTSEL_C(txdesc)
#define SET_TX_DESC_ANTSEL_B_8814A(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8814A(txdesc) GET_TX_DESC_ANTSEL_B(txdesc)
#define SET_TX_DESC_ANTSEL_A_8814A(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8814A(txdesc) GET_TX_DESC_ANTSEL_A(txdesc)
#define SET_TX_DESC_MBSSID_8814A(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8814A(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SW_DEFINE_8814A(txdesc, value)                             \
	SET_TX_DESC_SW_DEFINE(txdesc, value)
#define GET_TX_DESC_SW_DEFINE_8814A(txdesc) GET_TX_DESC_SW_DEFINE(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8814A(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8814A(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8814A(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8814A(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8814A(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8814A(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8814A(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8814A(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8814A(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8814A(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8814A(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8814A(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8814A(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8814A(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8814A(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8814A(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8814A(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8814A(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8814A(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8814A(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8814A(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8814A(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8814A(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8814A(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8814A(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8814A(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8814A(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8814A(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8814A(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8814A(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8814A(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8814A(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8814A(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8814A(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_8814A(txdesc, value)                              \
	SET_TX_DESC_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_8814A(txdesc) GET_TX_DESC_EN_HWSEQ(txdesc)
#define SET_TX_DESC_EN_HWEXSEQ_8814A(txdesc, value)                            \
	SET_TX_DESC_EN_HWEXSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWEXSEQ_8814A(txdesc) GET_TX_DESC_EN_HWEXSEQ(txdesc)
#define SET_TX_DESC_DATA_RC_8814A(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8814A(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8814A(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8814A(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8814A(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8814A(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8814A(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8814A(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8814A(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8814A(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8814A(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8814A(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8814A(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8814A(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8814A(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8814A(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8814A(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8814A(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#endif

#if (HALMAC_8822B_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8822B(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8822B(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8822B(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8822B(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8822B(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8822B(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8822B(txdesc, value)                       \
	SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8822B(txdesc)                              \
	GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8822B(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8822B(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8822B(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8822B(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8822B(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8822B(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8822B(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8822B(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8822B(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8822B(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8822B(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8822B(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_MOREDATA_8822B(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8822B(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8822B(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8822B(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8822B(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8822B(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8822B(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8822B(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8822B(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8822B(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8822B(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8822B(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8822B(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8822B(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8822B(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8822B(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8822B(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8822B(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8822B(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8822B(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV_8822B(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8822B(txdesc) GET_TX_DESC_HW_AES_IV(txdesc)
#define SET_TX_DESC_FTM_EN_8822B(txdesc, value)                                \
	SET_TX_DESC_FTM_EN(txdesc, value)
#define GET_TX_DESC_FTM_EN_8822B(txdesc) GET_TX_DESC_FTM_EN(txdesc)
#define SET_TX_DESC_G_ID_8822B(txdesc, value) SET_TX_DESC_G_ID(txdesc, value)
#define GET_TX_DESC_G_ID_8822B(txdesc) GET_TX_DESC_G_ID(txdesc)
#define SET_TX_DESC_BT_NULL_8822B(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8822B(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8822B(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8822B(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8822B(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8822B(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8822B(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8822B(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8822B(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8822B(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8822B(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8822B(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8822B(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8822B(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8822B(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8822B(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8822B(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8822B(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8822B(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8822B(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8822B(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8822B(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8822B(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8822B(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8822B(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8822B(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8822B(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8822B(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8822B(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8822B(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8822B(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8822B(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8822B(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8822B(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8822B(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8822B(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8822B(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8822B(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8822B(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8822B(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8822B(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8822B(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8822B(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8822B(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8822B(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8822B(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8822B(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8822B(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8822B(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8822B(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8822B(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8822B(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8822B(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8822B(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8822B(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8822B(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8822B(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8822B(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8822B(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8822B(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8822B(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8822B(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8822B(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8822B(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8822B(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8822B(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8822B(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8822B(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8822B(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8822B(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8822B(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_8822B(txdesc, value)                           \
	SET_TX_DESC_TXPWR_OFSET(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_8822B(txdesc) GET_TX_DESC_TXPWR_OFSET(txdesc)
#define SET_TX_DESC_TX_ANT_8822B(txdesc, value)                                \
	SET_TX_DESC_TX_ANT(txdesc, value)
#define GET_TX_DESC_TX_ANT_8822B(txdesc) GET_TX_DESC_TX_ANT(txdesc)
#define SET_TX_DESC_PORT_ID_8822B(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8822B(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_MULTIPLE_PORT_8822B(txdesc, value)                         \
	SET_TX_DESC_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_MULTIPLE_PORT_8822B(txdesc)                                \
	GET_TX_DESC_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8822B(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8822B(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_SIGNALING_TA_PKT_SC_8822B(txdesc, value)                   \
	SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc, value)
#define GET_TX_DESC_SIGNALING_TA_PKT_SC_8822B(txdesc)                          \
	GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8822B(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8822B(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8822B(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8822B(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8822B(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8822B(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8822B(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8822B(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8822B(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8822B(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8822B(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8822B(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8822B(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8822B(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D_8822B(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_D(txdesc, value)
#define GET_TX_DESC_ANTSEL_D_8822B(txdesc) GET_TX_DESC_ANTSEL_D(txdesc)
#define SET_TX_DESC_ANT_MAPD_8822B(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8822B(txdesc) GET_TX_DESC_ANT_MAPD(txdesc)
#define SET_TX_DESC_ANT_MAPC_8822B(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8822B(txdesc) GET_TX_DESC_ANT_MAPC(txdesc)
#define SET_TX_DESC_ANT_MAPB_8822B(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8822B(txdesc) GET_TX_DESC_ANT_MAPB(txdesc)
#define SET_TX_DESC_ANT_MAPA_8822B(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8822B(txdesc) GET_TX_DESC_ANT_MAPA(txdesc)
#define SET_TX_DESC_ANTSEL_C_8822B(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8822B(txdesc) GET_TX_DESC_ANTSEL_C(txdesc)
#define SET_TX_DESC_ANTSEL_B_8822B(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8822B(txdesc) GET_TX_DESC_ANTSEL_B(txdesc)
#define SET_TX_DESC_ANTSEL_A_8822B(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8822B(txdesc) GET_TX_DESC_ANTSEL_A(txdesc)
#define SET_TX_DESC_MBSSID_8822B(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8822B(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SW_DEFINE_8822B(txdesc, value)                             \
	SET_TX_DESC_SW_DEFINE(txdesc, value)
#define GET_TX_DESC_SW_DEFINE_8822B(txdesc) GET_TX_DESC_SW_DEFINE(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8822B(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8822B(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8822B(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8822B(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8822B(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8822B(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8822B(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8822B(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8822B(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8822B(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8822B(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8822B(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8822B(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8822B(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8822B(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8822B(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8822B(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8822B(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8822B(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8822B(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8822B(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8822B(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8822B(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8822B(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8822B(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8822B(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8822B(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8822B(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8822B(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8822B(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8822B(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8822B(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8822B(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8822B(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_8822B(txdesc, value)                              \
	SET_TX_DESC_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_8822B(txdesc) GET_TX_DESC_EN_HWSEQ(txdesc)
#define SET_TX_DESC_EN_HWEXSEQ_8822B(txdesc, value)                            \
	SET_TX_DESC_EN_HWEXSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWEXSEQ_8822B(txdesc) GET_TX_DESC_EN_HWEXSEQ(txdesc)
#define SET_TX_DESC_DATA_RC_8822B(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8822B(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8822B(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8822B(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8822B(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8822B(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8822B(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8822B(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8822B(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8822B(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8822B(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8822B(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8822B(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8822B(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8822B(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8822B(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8822B(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8822B(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#define SET_TX_DESC_MU_DATARATE_8822B(txdesc, value)                           \
	SET_TX_DESC_MU_DATARATE(txdesc, value)
#define GET_TX_DESC_MU_DATARATE_8822B(txdesc) GET_TX_DESC_MU_DATARATE(txdesc)
#define SET_TX_DESC_MU_RC_8822B(txdesc, value) SET_TX_DESC_MU_RC(txdesc, value)
#define GET_TX_DESC_MU_RC_8822B(txdesc) GET_TX_DESC_MU_RC(txdesc)
#define SET_TX_DESC_SND_PKT_SEL_8822B(txdesc, value)                           \
	SET_TX_DESC_SND_PKT_SEL(txdesc, value)
#define GET_TX_DESC_SND_PKT_SEL_8822B(txdesc) GET_TX_DESC_SND_PKT_SEL(txdesc)

#endif

#if (HALMAC_8197F_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8197F(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8197F(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8197F(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8197F(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8197F(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8197F(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8197F(txdesc, value)                       \
	SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8197F(txdesc)                              \
	GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8197F(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8197F(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8197F(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8197F(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8197F(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8197F(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8197F(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8197F(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8197F(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8197F(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8197F(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8197F(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_MOREDATA_8197F(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8197F(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8197F(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8197F(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8197F(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8197F(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8197F(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8197F(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8197F(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8197F(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8197F(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8197F(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8197F(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8197F(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8197F(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8197F(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8197F(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8197F(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8197F(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8197F(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV_8197F(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8197F(txdesc) GET_TX_DESC_HW_AES_IV(txdesc)
#define SET_TX_DESC_FTM_EN_8197F(txdesc, value)                                \
	SET_TX_DESC_FTM_EN(txdesc, value)
#define GET_TX_DESC_FTM_EN_8197F(txdesc) GET_TX_DESC_FTM_EN(txdesc)
#define SET_TX_DESC_G_ID_8197F(txdesc, value) SET_TX_DESC_G_ID(txdesc, value)
#define GET_TX_DESC_G_ID_8197F(txdesc) GET_TX_DESC_G_ID(txdesc)
#define SET_TX_DESC_BT_NULL_8197F(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8197F(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8197F(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8197F(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8197F(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8197F(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8197F(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8197F(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8197F(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8197F(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8197F(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8197F(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8197F(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8197F(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8197F(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8197F(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8197F(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8197F(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8197F(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8197F(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8197F(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8197F(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8197F(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8197F(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8197F(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8197F(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8197F(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8197F(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8197F(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8197F(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8197F(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8197F(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8197F(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8197F(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8197F(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8197F(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8197F(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8197F(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8197F(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8197F(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8197F(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8197F(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8197F(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8197F(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8197F(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8197F(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8197F(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8197F(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8197F(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8197F(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8197F(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8197F(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8197F(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8197F(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8197F(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8197F(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8197F(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8197F(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8197F(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8197F(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8197F(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8197F(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8197F(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8197F(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8197F(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8197F(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8197F(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8197F(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8197F(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8197F(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8197F(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8197F(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8197F(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8197F(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_8197F(txdesc, value)                           \
	SET_TX_DESC_TXPWR_OFSET(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_8197F(txdesc) GET_TX_DESC_TXPWR_OFSET(txdesc)
#define SET_TX_DESC_TX_ANT_8197F(txdesc, value)                                \
	SET_TX_DESC_TX_ANT(txdesc, value)
#define GET_TX_DESC_TX_ANT_8197F(txdesc) GET_TX_DESC_TX_ANT(txdesc)
#define SET_TX_DESC_PORT_ID_8197F(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8197F(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_MULTIPLE_PORT_8197F(txdesc, value)                         \
	SET_TX_DESC_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_MULTIPLE_PORT_8197F(txdesc)                                \
	GET_TX_DESC_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8197F(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8197F(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_RTS_SC_8197F(txdesc, value)                                \
	SET_TX_DESC_RTS_SC(txdesc, value)
#define GET_TX_DESC_RTS_SC_8197F(txdesc) GET_TX_DESC_RTS_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8197F(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8197F(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8197F(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8197F(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8197F(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8197F(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8197F(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8197F(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8197F(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8197F(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8197F(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8197F(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8197F(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8197F(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D_8197F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_D(txdesc, value)
#define GET_TX_DESC_ANTSEL_D_8197F(txdesc) GET_TX_DESC_ANTSEL_D(txdesc)
#define SET_TX_DESC_ANT_MAPD_8197F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8197F(txdesc) GET_TX_DESC_ANT_MAPD(txdesc)
#define SET_TX_DESC_ANT_MAPC_8197F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8197F(txdesc) GET_TX_DESC_ANT_MAPC(txdesc)
#define SET_TX_DESC_ANT_MAPB_8197F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8197F(txdesc) GET_TX_DESC_ANT_MAPB(txdesc)
#define SET_TX_DESC_ANT_MAPA_8197F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8197F(txdesc) GET_TX_DESC_ANT_MAPA(txdesc)
#define SET_TX_DESC_ANTSEL_C_8197F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8197F(txdesc) GET_TX_DESC_ANTSEL_C(txdesc)
#define SET_TX_DESC_ANTSEL_B_8197F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8197F(txdesc) GET_TX_DESC_ANTSEL_B(txdesc)
#define SET_TX_DESC_ANTSEL_A_8197F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8197F(txdesc) GET_TX_DESC_ANTSEL_A(txdesc)
#define SET_TX_DESC_MBSSID_8197F(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8197F(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SW_DEFINE_8197F(txdesc, value)                             \
	SET_TX_DESC_SW_DEFINE(txdesc, value)
#define GET_TX_DESC_SW_DEFINE_8197F(txdesc) GET_TX_DESC_SW_DEFINE(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8197F(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8197F(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8197F(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8197F(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8197F(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8197F(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8197F(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8197F(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8197F(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8197F(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8197F(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8197F(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8197F(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8197F(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8197F(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8197F(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8197F(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8197F(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8197F(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8197F(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8197F(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8197F(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8197F(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8197F(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8197F(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8197F(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8197F(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8197F(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8197F(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8197F(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8197F(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8197F(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8197F(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8197F(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_8197F(txdesc, value)                              \
	SET_TX_DESC_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_8197F(txdesc) GET_TX_DESC_EN_HWSEQ(txdesc)
#define SET_TX_DESC_EN_HWEXSEQ_8197F(txdesc, value)                            \
	SET_TX_DESC_EN_HWEXSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWEXSEQ_8197F(txdesc) GET_TX_DESC_EN_HWEXSEQ(txdesc)
#define SET_TX_DESC_DATA_RC_8197F(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8197F(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8197F(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8197F(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8197F(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8197F(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8197F(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8197F(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8197F(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8197F(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8197F(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8197F(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8197F(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8197F(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8197F(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8197F(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8197F(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8197F(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#endif

#if (HALMAC_8821C_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8821C(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8821C(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8821C(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8821C(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8821C(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8821C(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8821C(txdesc, value)                       \
	SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8821C(txdesc)                              \
	GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8821C(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8821C(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8821C(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8821C(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8821C(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8821C(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8821C(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8821C(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8821C(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8821C(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8821C(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8821C(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_MOREDATA_8821C(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8821C(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8821C(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8821C(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8821C(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8821C(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8821C(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8821C(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8821C(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8821C(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8821C(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8821C(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8821C(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8821C(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8821C(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8821C(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8821C(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8821C(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8821C(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8821C(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV_8821C(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8821C(txdesc) GET_TX_DESC_HW_AES_IV(txdesc)
#define SET_TX_DESC_FTM_EN_8821C(txdesc, value)                                \
	SET_TX_DESC_FTM_EN(txdesc, value)
#define GET_TX_DESC_FTM_EN_8821C(txdesc) GET_TX_DESC_FTM_EN(txdesc)
#define SET_TX_DESC_G_ID_8821C(txdesc, value) SET_TX_DESC_G_ID(txdesc, value)
#define GET_TX_DESC_G_ID_8821C(txdesc) GET_TX_DESC_G_ID(txdesc)
#define SET_TX_DESC_BT_NULL_8821C(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8821C(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8821C(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8821C(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8821C(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8821C(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8821C(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8821C(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8821C(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8821C(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8821C(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8821C(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8821C(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8821C(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8821C(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8821C(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8821C(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8821C(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8821C(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8821C(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8821C(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8821C(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8821C(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8821C(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8821C(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8821C(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8821C(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8821C(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8821C(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8821C(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8821C(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8821C(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8821C(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8821C(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8821C(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8821C(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8821C(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8821C(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8821C(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8821C(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8821C(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8821C(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8821C(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8821C(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8821C(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8821C(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8821C(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8821C(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8821C(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8821C(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8821C(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8821C(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8821C(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8821C(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8821C(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8821C(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8821C(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8821C(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8821C(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8821C(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8821C(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8821C(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8821C(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8821C(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8821C(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8821C(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8821C(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8821C(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8821C(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8821C(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8821C(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8821C(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8821C(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8821C(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_8821C(txdesc, value)                           \
	SET_TX_DESC_TXPWR_OFSET(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_8821C(txdesc) GET_TX_DESC_TXPWR_OFSET(txdesc)
#define SET_TX_DESC_TX_ANT_8821C(txdesc, value)                                \
	SET_TX_DESC_TX_ANT(txdesc, value)
#define GET_TX_DESC_TX_ANT_8821C(txdesc) GET_TX_DESC_TX_ANT(txdesc)
#define SET_TX_DESC_PORT_ID_8821C(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8821C(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_MULTIPLE_PORT_8821C(txdesc, value)                         \
	SET_TX_DESC_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_MULTIPLE_PORT_8821C(txdesc)                                \
	GET_TX_DESC_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8821C(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8821C(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_SIGNALING_TA_PKT_SC_8821C(txdesc, value)                   \
	SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc, value)
#define GET_TX_DESC_SIGNALING_TA_PKT_SC_8821C(txdesc)                          \
	GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8821C(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8821C(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8821C(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8821C(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8821C(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8821C(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8821C(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8821C(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8821C(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8821C(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8821C(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8821C(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8821C(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8821C(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D_8821C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_D(txdesc, value)
#define GET_TX_DESC_ANTSEL_D_8821C(txdesc) GET_TX_DESC_ANTSEL_D(txdesc)
#define SET_TX_DESC_ANT_MAPD_8821C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8821C(txdesc) GET_TX_DESC_ANT_MAPD(txdesc)
#define SET_TX_DESC_ANT_MAPC_8821C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8821C(txdesc) GET_TX_DESC_ANT_MAPC(txdesc)
#define SET_TX_DESC_ANT_MAPB_8821C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8821C(txdesc) GET_TX_DESC_ANT_MAPB(txdesc)
#define SET_TX_DESC_ANT_MAPA_8821C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8821C(txdesc) GET_TX_DESC_ANT_MAPA(txdesc)
#define SET_TX_DESC_ANTSEL_C_8821C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8821C(txdesc) GET_TX_DESC_ANTSEL_C(txdesc)
#define SET_TX_DESC_ANTSEL_B_8821C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8821C(txdesc) GET_TX_DESC_ANTSEL_B(txdesc)
#define SET_TX_DESC_ANTSEL_A_8821C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8821C(txdesc) GET_TX_DESC_ANTSEL_A(txdesc)
#define SET_TX_DESC_MBSSID_8821C(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8821C(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SW_DEFINE_8821C(txdesc, value)                             \
	SET_TX_DESC_SW_DEFINE(txdesc, value)
#define GET_TX_DESC_SW_DEFINE_8821C(txdesc) GET_TX_DESC_SW_DEFINE(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8821C(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8821C(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8821C(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8821C(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8821C(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8821C(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8821C(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8821C(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8821C(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8821C(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8821C(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8821C(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8821C(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8821C(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8821C(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8821C(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8821C(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8821C(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8821C(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8821C(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8821C(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8821C(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8821C(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8821C(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8821C(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8821C(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8821C(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8821C(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8821C(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8821C(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8821C(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8821C(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8821C(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8821C(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_8821C(txdesc, value)                              \
	SET_TX_DESC_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_8821C(txdesc) GET_TX_DESC_EN_HWSEQ(txdesc)
#define SET_TX_DESC_EN_HWEXSEQ_8821C(txdesc, value)                            \
	SET_TX_DESC_EN_HWEXSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWEXSEQ_8821C(txdesc) GET_TX_DESC_EN_HWEXSEQ(txdesc)
#define SET_TX_DESC_DATA_RC_8821C(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8821C(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8821C(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8821C(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8821C(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8821C(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8821C(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8821C(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8821C(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8821C(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8821C(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8821C(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8821C(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8821C(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8821C(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8821C(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8821C(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8821C(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#define SET_TX_DESC_MU_DATARATE_8821C(txdesc, value)                           \
	SET_TX_DESC_MU_DATARATE(txdesc, value)
#define GET_TX_DESC_MU_DATARATE_8821C(txdesc) GET_TX_DESC_MU_DATARATE(txdesc)
#define SET_TX_DESC_MU_RC_8821C(txdesc, value) SET_TX_DESC_MU_RC(txdesc, value)
#define GET_TX_DESC_MU_RC_8821C(txdesc) GET_TX_DESC_MU_RC(txdesc)
#define SET_TX_DESC_SND_PKT_SEL_8821C(txdesc, value)                           \
	SET_TX_DESC_SND_PKT_SEL(txdesc, value)
#define GET_TX_DESC_SND_PKT_SEL_8821C(txdesc) GET_TX_DESC_SND_PKT_SEL(txdesc)

#endif

#if (HALMAC_8814B_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_IE_END_BODY_8814B(txdesc, value)                           \
	SET_TX_DESC_IE_END_BODY(txdesc, value)
#define GET_TX_DESC_IE_END_BODY_8814B(txdesc) GET_TX_DESC_IE_END_BODY(txdesc)
#define SET_TX_DESC_AGG_EN_8814B(txdesc, value)                                \
	SET_TX_DESC_AGG_EN_V1(txdesc, value)
#define GET_TX_DESC_AGG_EN_8814B(txdesc) GET_TX_DESC_AGG_EN_V1(txdesc)
#define SET_TX_DESC_BK_8814B(txdesc, value) SET_TX_DESC_BK_V1(txdesc, value)
#define GET_TX_DESC_BK_8814B(txdesc) GET_TX_DESC_BK_V1(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8814B(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET_V1(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8814B(txdesc) GET_TX_DESC_PKT_OFFSET_V1(txdesc)
#define SET_TX_DESC_OFFSET_8814B(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8814B(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8814B(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8814B(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_AMSDU_8814B(txdesc, value) SET_TX_DESC_AMSDU(txdesc, value)
#define GET_TX_DESC_AMSDU_8814B(txdesc) GET_TX_DESC_AMSDU(txdesc)
#define SET_TX_DESC_HW_AES_IV_8814B(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV_V1(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8814B(txdesc) GET_TX_DESC_HW_AES_IV_V1(txdesc)
#define SET_TX_DESC_MHR_CP_8814B(txdesc, value)                                \
	SET_TX_DESC_MHR_CP(txdesc, value)
#define GET_TX_DESC_MHR_CP_8814B(txdesc) GET_TX_DESC_MHR_CP(txdesc)
#define SET_TX_DESC_SMH_EN_8814B(txdesc, value)                                \
	SET_TX_DESC_SMH_EN_V1(txdesc, value)
#define GET_TX_DESC_SMH_EN_8814B(txdesc) GET_TX_DESC_SMH_EN_V1(txdesc)
#define SET_TX_DESC_SMH_CAM_8814B(txdesc, value)                               \
	SET_TX_DESC_SMH_CAM(txdesc, value)
#define GET_TX_DESC_SMH_CAM_8814B(txdesc) GET_TX_DESC_SMH_CAM(txdesc)
#define SET_TX_DESC_EXT_EDCA_8814B(txdesc, value)                              \
	SET_TX_DESC_EXT_EDCA(txdesc, value)
#define GET_TX_DESC_EXT_EDCA_8814B(txdesc) GET_TX_DESC_EXT_EDCA(txdesc)
#define SET_TX_DESC_QSEL_8814B(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8814B(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8814B(txdesc, value)                                 \
	SET_TX_DESC_MACID_V1(txdesc, value)
#define GET_TX_DESC_MACID_8814B(txdesc) GET_TX_DESC_MACID_V1(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_CHK_EN_8814B(txdesc, value)                                \
	SET_TX_DESC_CHK_EN_V1(txdesc, value)
#define GET_TX_DESC_CHK_EN_8814B(txdesc) GET_TX_DESC_CHK_EN_V1(txdesc)
#define SET_TX_DESC_DMA_PRI_8814B(txdesc, value)                               \
	SET_TX_DESC_DMA_PRI(txdesc, value)
#define GET_TX_DESC_DMA_PRI_8814B(txdesc) GET_TX_DESC_DMA_PRI(txdesc)
#define SET_TX_DESC_MAX_AMSDU_MODE_8814B(txdesc, value)                        \
	SET_TX_DESC_MAX_AMSDU_MODE(txdesc, value)
#define GET_TX_DESC_MAX_AMSDU_MODE_8814B(txdesc)                               \
	GET_TX_DESC_MAX_AMSDU_MODE(txdesc)
#define SET_TX_DESC_DMA_TXAGG_NUM_8814B(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM_V1(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8814B(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM_V1(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8814B(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM_V1(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8814B(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM_V1(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_OFFLOAD_SIZE_8814B(txdesc, value)                          \
	SET_TX_DESC_OFFLOAD_SIZE(txdesc, value)
#define GET_TX_DESC_OFFLOAD_SIZE_8814B(txdesc) GET_TX_DESC_OFFLOAD_SIZE(txdesc)
#define SET_TX_DESC_CHANNEL_DMA_8814B(txdesc, value)                           \
	SET_TX_DESC_CHANNEL_DMA(txdesc, value)
#define GET_TX_DESC_CHANNEL_DMA_8814B(txdesc) GET_TX_DESC_CHANNEL_DMA(txdesc)
#define SET_TX_DESC_IE_CNT_8814B(txdesc, value)                                \
	SET_TX_DESC_IE_CNT(txdesc, value)
#define GET_TX_DESC_IE_CNT_8814B(txdesc) GET_TX_DESC_IE_CNT(txdesc)
#define SET_TX_DESC_IE_CNT_EN_8814B(txdesc, value)                             \
	SET_TX_DESC_IE_CNT_EN(txdesc, value)
#define GET_TX_DESC_IE_CNT_EN_8814B(txdesc) GET_TX_DESC_IE_CNT_EN(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8814B(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN_V1(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8814B(txdesc) GET_TX_DESC_WHEADER_LEN_V1(txdesc)

/*TXDESC_WORD4*/

/*TXDESC_WORD5*/

/*TXDESC_WORD6*/

/*TXDESC_WORD7*/

/*TXDESC_WORD8*/

/*TXDESC_WORD9*/

/*WORD10*/

#endif

#if (HALMAC_8198F_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8198F(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8198F(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8198F(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8198F(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8198F(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8198F(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8198F(txdesc, value)                       \
	SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8198F(txdesc)                              \
	GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8198F(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8198F(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8198F(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8198F(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8198F(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8198F(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8198F(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8198F(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8198F(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8198F(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8198F(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8198F(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_HW_AES_IV_8198F(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV_V2(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8198F(txdesc) GET_TX_DESC_HW_AES_IV_V2(txdesc)
#define SET_TX_DESC_FTM_EN_8198F(txdesc, value)                                \
	SET_TX_DESC_FTM_EN_V1(txdesc, value)
#define GET_TX_DESC_FTM_EN_8198F(txdesc) GET_TX_DESC_FTM_EN_V1(txdesc)
#define SET_TX_DESC_MOREDATA_8198F(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8198F(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8198F(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8198F(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8198F(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8198F(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8198F(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8198F(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8198F(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8198F(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8198F(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8198F(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8198F(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8198F(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8198F(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8198F(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8198F(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8198F(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_SPECIAL_CW_8198F(txdesc, value)                            \
	SET_TX_DESC_SPECIAL_CW(txdesc, value)
#define GET_TX_DESC_SPECIAL_CW_8198F(txdesc) GET_TX_DESC_SPECIAL_CW(txdesc)
#define SET_TX_DESC_MACID_8198F(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8198F(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_ANTCEL_D_8198F(txdesc, value)                              \
	SET_TX_DESC_ANTCEL_D_V1(txdesc, value)
#define GET_TX_DESC_ANTCEL_D_8198F(txdesc) GET_TX_DESC_ANTCEL_D_V1(txdesc)
#define SET_TX_DESC_ANTSEL_C_8198F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8198F(txdesc) GET_TX_DESC_ANTSEL_C_V1(txdesc)
#define SET_TX_DESC_BT_NULL_8198F(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8198F(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8198F(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8198F(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8198F(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8198F(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8198F(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8198F(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8198F(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8198F(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8198F(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8198F(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8198F(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8198F(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8198F(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8198F(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8198F(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8198F(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8198F(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8198F(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8198F(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8198F(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8198F(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8198F(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8198F(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8198F(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8198F(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8198F(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8198F(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8198F(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8198F(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8198F(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8198F(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8198F(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8198F(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8198F(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8198F(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8198F(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8198F(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8198F(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8198F(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8198F(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8198F(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8198F(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8198F(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8198F(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8198F(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8198F(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8198F(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8198F(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8198F(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8198F(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8198F(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8198F(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8198F(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8198F(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8198F(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8198F(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8198F(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8198F(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8198F(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8198F(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8198F(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8198F(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8198F(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8198F(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8198F(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8198F(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8198F(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8198F(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8198F(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8198F(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8198F(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8198F(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_8198F(txdesc, value)                           \
	SET_TX_DESC_TXPWR_OFSET(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_8198F(txdesc) GET_TX_DESC_TXPWR_OFSET(txdesc)
#define SET_TX_DESC_DROP_ID_8198F(txdesc, value)                               \
	SET_TX_DESC_DROP_ID(txdesc, value)
#define GET_TX_DESC_DROP_ID_8198F(txdesc) GET_TX_DESC_DROP_ID(txdesc)
#define SET_TX_DESC_PORT_ID_8198F(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8198F(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_MULTIPLE_PORT_8198F(txdesc, value)                         \
	SET_TX_DESC_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_MULTIPLE_PORT_8198F(txdesc)                                \
	GET_TX_DESC_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8198F(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8198F(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_RTS_SC_8198F(txdesc, value)                                \
	SET_TX_DESC_RTS_SC(txdesc, value)
#define GET_TX_DESC_RTS_SC_8198F(txdesc) GET_TX_DESC_RTS_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8198F(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8198F(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8198F(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8198F(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8198F(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8198F(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8198F(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8198F(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8198F(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8198F(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8198F(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8198F(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8198F(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8198F(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANT_MAPD_8198F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8198F(txdesc) GET_TX_DESC_ANT_MAPD_V1(txdesc)
#define SET_TX_DESC_ANT_MAPC_8198F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8198F(txdesc) GET_TX_DESC_ANT_MAPC_V1(txdesc)
#define SET_TX_DESC_ANT_MAPB_8198F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8198F(txdesc) GET_TX_DESC_ANT_MAPB_V1(txdesc)
#define SET_TX_DESC_ANT_MAPA_8198F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8198F(txdesc) GET_TX_DESC_ANT_MAPA_V1(txdesc)
#define SET_TX_DESC_ANTSEL_B_8198F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8198F(txdesc) GET_TX_DESC_ANTSEL_B_V1(txdesc)
#define SET_TX_DESC_ANTSEL_A_8198F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8198F(txdesc) GET_TX_DESC_ANTSEL_A_V1(txdesc)
#define SET_TX_DESC_MBSSID_8198F(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8198F(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SWPS_SEQ_8198F(txdesc, value)                              \
	SET_TX_DESC_SWPS_SEQ(txdesc, value)
#define GET_TX_DESC_SWPS_SEQ_8198F(txdesc) GET_TX_DESC_SWPS_SEQ(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8198F(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8198F(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8198F(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8198F(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8198F(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8198F(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_ANTSEL_EN_8198F(txdesc, value)                             \
	SET_TX_DESC_ANTSEL_EN(txdesc, value)
#define GET_TX_DESC_ANTSEL_EN_8198F(txdesc) GET_TX_DESC_ANTSEL_EN(txdesc)
#define SET_TX_DESC_MBSSID_EX_8198F(txdesc, value)                             \
	SET_TX_DESC_MBSSID_EX(txdesc, value)
#define GET_TX_DESC_MBSSID_EX_8198F(txdesc) GET_TX_DESC_MBSSID_EX(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8198F(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8198F(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8198F(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8198F(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8198F(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8198F(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8198F(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8198F(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8198F(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8198F(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8198F(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8198F(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8198F(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8198F(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8198F(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8198F(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8198F(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8198F(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8198F(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8198F(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8198F(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8198F(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8198F(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8198F(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8198F(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8198F(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8198F(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8198F(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_8198F(txdesc, value)                              \
	SET_TX_DESC_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_8198F(txdesc) GET_TX_DESC_EN_HWSEQ(txdesc)
#define SET_TX_DESC_EN_HWEXSEQ_8198F(txdesc, value)                            \
	SET_TX_DESC_EN_HWEXSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWEXSEQ_8198F(txdesc) GET_TX_DESC_EN_HWEXSEQ(txdesc)
#define SET_TX_DESC_DATA_RC_8198F(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8198F(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8198F(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8198F(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8198F(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8198F(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8198F(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8198F(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8198F(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8198F(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8198F(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8198F(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8198F(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8198F(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8198F(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8198F(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8198F(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8198F(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#endif

#if (HALMAC_8822C_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8822C(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8822C(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8822C(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8822C(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8822C(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8822C(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8822C(txdesc, value)                       \
	SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8822C(txdesc)                              \
	GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8822C(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8822C(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8822C(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8822C(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8822C(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8822C(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8822C(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8822C(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8822C(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8822C(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8822C(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8822C(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_MOREDATA_8822C(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8822C(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8822C(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8822C(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8822C(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8822C(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8822C(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8822C(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8822C(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8822C(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8822C(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8822C(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8822C(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8822C(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8822C(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8822C(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8822C(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8822C(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8822C(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8822C(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV_8822C(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8822C(txdesc) GET_TX_DESC_HW_AES_IV(txdesc)
#define SET_TX_DESC_FTM_EN_8822C(txdesc, value)                                \
	SET_TX_DESC_FTM_EN(txdesc, value)
#define GET_TX_DESC_FTM_EN_8822C(txdesc) GET_TX_DESC_FTM_EN(txdesc)
#define SET_TX_DESC_G_ID_8822C(txdesc, value) SET_TX_DESC_G_ID(txdesc, value)
#define GET_TX_DESC_G_ID_8822C(txdesc) GET_TX_DESC_G_ID(txdesc)
#define SET_TX_DESC_BT_NULL_8822C(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8822C(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8822C(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8822C(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8822C(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8822C(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8822C(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8822C(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8822C(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8822C(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8822C(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8822C(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8822C(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8822C(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8822C(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8822C(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8822C(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8822C(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8822C(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8822C(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8822C(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8822C(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8822C(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8822C(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8822C(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8822C(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8822C(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8822C(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8822C(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8822C(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8822C(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8822C(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8822C(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8822C(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8822C(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8822C(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8822C(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8822C(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8822C(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8822C(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8822C(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8822C(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8822C(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8822C(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8822C(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8822C(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8822C(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8822C(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8822C(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8822C(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8822C(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8822C(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8822C(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8822C(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8822C(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8822C(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8822C(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8822C(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8822C(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8822C(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8822C(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8822C(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8822C(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8822C(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8822C(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8822C(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822C(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8822C(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8822C(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8822C(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8822C(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8822C(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8822C(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8822C(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_ANTSEL_EN_8822C(txdesc, value)                             \
	SET_TX_DESC_ANTSEL_EN_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_EN_8822C(txdesc) GET_TX_DESC_ANTSEL_EN_V1(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_TYPE_8822C(txdesc, value)                      \
	SET_TX_DESC_TXPWR_OFSET_TYPE(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_TYPE_8822C(txdesc)                             \
	GET_TX_DESC_TXPWR_OFSET_TYPE(txdesc)
#define SET_TX_DESC_TX_ANT_8822C(txdesc, value)                                \
	SET_TX_DESC_TX_ANT(txdesc, value)
#define GET_TX_DESC_TX_ANT_8822C(txdesc) GET_TX_DESC_TX_ANT(txdesc)
#define SET_TX_DESC_PORT_ID_8822C(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8822C(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_MULTIPLE_PORT_8822C(txdesc, value)                         \
	SET_TX_DESC_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_MULTIPLE_PORT_8822C(txdesc)                                \
	GET_TX_DESC_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8822C(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8822C(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_SIGNALING_TA_PKT_SC_8822C(txdesc, value)                   \
	SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc, value)
#define GET_TX_DESC_SIGNALING_TA_PKT_SC_8822C(txdesc)                          \
	GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8822C(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8822C(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8822C(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8822C(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8822C(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8822C(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8822C(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8822C(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8822C(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8822C(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8822C(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8822C(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8822C(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8822C(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D_8822C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_D(txdesc, value)
#define GET_TX_DESC_ANTSEL_D_8822C(txdesc) GET_TX_DESC_ANTSEL_D(txdesc)
#define SET_TX_DESC_ANT_MAPD_8822C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8822C(txdesc) GET_TX_DESC_ANT_MAPD(txdesc)
#define SET_TX_DESC_ANT_MAPC_8822C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8822C(txdesc) GET_TX_DESC_ANT_MAPC(txdesc)
#define SET_TX_DESC_ANT_MAPB_8822C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8822C(txdesc) GET_TX_DESC_ANT_MAPB(txdesc)
#define SET_TX_DESC_ANT_MAPA_8822C(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8822C(txdesc) GET_TX_DESC_ANT_MAPA(txdesc)
#define SET_TX_DESC_ANTSEL_C_8822C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8822C(txdesc) GET_TX_DESC_ANTSEL_C(txdesc)
#define SET_TX_DESC_ANTSEL_B_8822C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8822C(txdesc) GET_TX_DESC_ANTSEL_B(txdesc)
#define SET_TX_DESC_ANTSEL_A_8822C(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8822C(txdesc) GET_TX_DESC_ANTSEL_A(txdesc)
#define SET_TX_DESC_MBSSID_8822C(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8822C(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SW_DEFINE_8822C(txdesc, value)                             \
	SET_TX_DESC_SW_DEFINE(txdesc, value)
#define GET_TX_DESC_SW_DEFINE_8822C(txdesc) GET_TX_DESC_SW_DEFINE(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8822C(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8822C(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8822C(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8822C(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8822C(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8822C(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8822C(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8822C(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8822C(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8822C(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8822C(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8822C(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8822C(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8822C(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8822C(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8822C(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8822C(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8822C(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8822C(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8822C(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8822C(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8822C(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8822C(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8822C(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8822C(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8822C(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8822C(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8822C(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8822C(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8822C(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8822C(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8822C(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8822C(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8822C(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_8822C(txdesc, value)                              \
	SET_TX_DESC_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_8822C(txdesc) GET_TX_DESC_EN_HWSEQ(txdesc)
#define SET_TX_DESC_EN_HWEXSEQ_8822C(txdesc, value)                            \
	SET_TX_DESC_EN_HWEXSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWEXSEQ_8822C(txdesc) GET_TX_DESC_EN_HWEXSEQ(txdesc)
#define SET_TX_DESC_DATA_RC_8822C(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8822C(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8822C(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8822C(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8822C(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8822C(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8822C(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8822C(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8822C(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8822C(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8822C(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8822C(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8822C(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8822C(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8822C(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8822C(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8822C(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8822C(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#define SET_TX_DESC_MU_DATARATE_8822C(txdesc, value)                           \
	SET_TX_DESC_MU_DATARATE(txdesc, value)
#define GET_TX_DESC_MU_DATARATE_8822C(txdesc) GET_TX_DESC_MU_DATARATE(txdesc)
#define SET_TX_DESC_MU_RC_8822C(txdesc, value) SET_TX_DESC_MU_RC(txdesc, value)
#define GET_TX_DESC_MU_RC_8822C(txdesc) GET_TX_DESC_MU_RC(txdesc)
#define SET_TX_DESC_SND_PKT_SEL_8822C(txdesc, value)                           \
	SET_TX_DESC_SND_PKT_SEL(txdesc, value)
#define GET_TX_DESC_SND_PKT_SEL_8822C(txdesc) GET_TX_DESC_SND_PKT_SEL(txdesc)

#endif

#if (HALMAC_8192F_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_GF_8192F(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8192F(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8192F(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8192F(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8192F(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8192F(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_HTC_8192F(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8192F(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8192F(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8192F(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8192F(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8192F(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8192F(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8192F(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_KEYID_SEL_8192F(txdesc, value)                             \
	SET_TX_DESC_KEYID_SEL(txdesc, value)
#define GET_TX_DESC_KEYID_SEL_8192F(txdesc) GET_TX_DESC_KEYID_SEL(txdesc)
#define SET_TX_DESC_MOREDATA_8192F(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8192F(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8192F(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8192F(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8192F(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8192F(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8192F(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8192F(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8192F(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8192F(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8192F(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8192F(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8192F(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8192F(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8192F(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8192F(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8192F(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8192F(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8192F(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8192F(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_FTM_EN_8192F(txdesc, value)                                \
	SET_TX_DESC_FTM_EN(txdesc, value)
#define GET_TX_DESC_FTM_EN_8192F(txdesc) GET_TX_DESC_FTM_EN(txdesc)
#define SET_TX_DESC_G_ID_8192F(txdesc, value) SET_TX_DESC_G_ID(txdesc, value)
#define GET_TX_DESC_G_ID_8192F(txdesc) GET_TX_DESC_G_ID(txdesc)
#define SET_TX_DESC_BT_NULL_8192F(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8192F(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8192F(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8192F(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8192F(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8192F(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8192F(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8192F(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8192F(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8192F(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8192F(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8192F(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8192F(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8192F(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8192F(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8192F(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8192F(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8192F(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8192F(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8192F(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8192F(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8192F(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8192F(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8192F(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8192F(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8192F(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8192F(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8192F(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8192F(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8192F(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8192F(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8192F(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8192F(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8192F(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8192F(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8192F(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8192F(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8192F(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8192F(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8192F(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8192F(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8192F(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8192F(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8192F(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8192F(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8192F(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8192F(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8192F(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8192F(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8192F(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8192F(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8192F(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8192F(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8192F(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8192F(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8192F(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8192F(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8192F(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8192F(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8192F(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8192F(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8192F(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8192F(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8192F(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8192F(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8192F(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8192F(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8192F(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8192F(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8192F(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8192F(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8192F(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8192F(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8192F(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_TYPE_8192F(txdesc, value)                      \
	SET_TX_DESC_TXPWR_OFSET_TYPE_V1(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_TYPE_8192F(txdesc)                             \
	GET_TX_DESC_TXPWR_OFSET_TYPE_V1(txdesc)
#define SET_TX_DESC_TX_ANT_8192F(txdesc, value)                                \
	SET_TX_DESC_TX_ANT(txdesc, value)
#define GET_TX_DESC_TX_ANT_8192F(txdesc) GET_TX_DESC_TX_ANT(txdesc)
#define SET_TX_DESC_DROP_ID_8192F(txdesc, value)                               \
	SET_TX_DESC_DROP_ID_V1(txdesc, value)
#define GET_TX_DESC_DROP_ID_8192F(txdesc) GET_TX_DESC_DROP_ID_V1(txdesc)
#define SET_TX_DESC_PORT_ID_8192F(txdesc, value)                               \
	SET_TX_DESC_PORT_ID_V1(txdesc, value)
#define GET_TX_DESC_PORT_ID_8192F(txdesc) GET_TX_DESC_PORT_ID_V1(txdesc)
#define SET_TX_DESC_RTS_SC_8192F(txdesc, value)                                \
	SET_TX_DESC_RTS_SC(txdesc, value)
#define GET_TX_DESC_RTS_SC_8192F(txdesc) GET_TX_DESC_RTS_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8192F(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8192F(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8192F(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8192F(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8192F(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8192F(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8192F(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8192F(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8192F(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8192F(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8192F(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8192F(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8192F(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8192F(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANT_MAPC_8192F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC_V2(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8192F(txdesc) GET_TX_DESC_ANT_MAPC_V2(txdesc)
#define SET_TX_DESC_ANT_MAPB_8192F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB_V2(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8192F(txdesc) GET_TX_DESC_ANT_MAPB_V2(txdesc)
#define SET_TX_DESC_ANT_MAPA_8192F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA_V2(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8192F(txdesc) GET_TX_DESC_ANT_MAPA_V2(txdesc)
#define SET_TX_DESC_ANTSEL_D_8192F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_D_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_D_8192F(txdesc) GET_TX_DESC_ANTSEL_D_V1(txdesc)
#define SET_TX_DESC_ANTSEL_C_8192F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C_V2(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8192F(txdesc) GET_TX_DESC_ANTSEL_C_V2(txdesc)
#define SET_TX_DESC_ANTSEL_B_8192F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B_V2(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8192F(txdesc) GET_TX_DESC_ANTSEL_B_V2(txdesc)
#define SET_TX_DESC_ANTSEL_A_8192F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A_V2(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8192F(txdesc) GET_TX_DESC_ANTSEL_A_V2(txdesc)
#define SET_TX_DESC_MBSSID_8192F(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8192F(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SWPS_SEQ_8192F(txdesc, value)                              \
	SET_TX_DESC_SWPS_SEQ(txdesc, value)
#define GET_TX_DESC_SWPS_SEQ_8192F(txdesc) GET_TX_DESC_SWPS_SEQ(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8192F(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8192F(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_ANT_MAPD_8192F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD_V2(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8192F(txdesc) GET_TX_DESC_ANT_MAPD_V2(txdesc)
#define SET_TX_DESC_ANTSEL_EN_8192F(txdesc, value)                             \
	SET_TX_DESC_ANTSEL_EN_V2(txdesc, value)
#define GET_TX_DESC_ANTSEL_EN_8192F(txdesc) GET_TX_DESC_ANTSEL_EN_V2(txdesc)
#define SET_TX_DESC_MBSSID_EX_8192F(txdesc, value)                             \
	SET_TX_DESC_MBSSID_EX_V1(txdesc, value)
#define GET_TX_DESC_MBSSID_EX_8192F(txdesc) GET_TX_DESC_MBSSID_EX_V1(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8192F(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8192F(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8192F(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8192F(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8192F(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8192F(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TAILPAGE_L_8192F(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8192F(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8192F(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8192F(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8192F(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8192F(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_8192F(txdesc, value)                              \
	SET_TX_DESC_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_8192F(txdesc) GET_TX_DESC_EN_HWSEQ(txdesc)
#define SET_TX_DESC_DATA_RC_8192F(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8192F(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8192F(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8192F(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8192F(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8192F(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_FINAL_DATA_RATE_8192F(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE_V1(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8192F(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE_V1(txdesc)
#define SET_TX_DESC_SW_SEQ_8192F(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8192F(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_PADDING_LEN_8192F(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8192F(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8192F(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8192F(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#endif

#if (HALMAC_8812F_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8812F(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8812F(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8812F(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8812F(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8812F(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8812F(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8812F(txdesc, value)                       \
	SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8812F(txdesc)                              \
	GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8812F(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8812F(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8812F(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8812F(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8812F(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8812F(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8812F(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8812F(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8812F(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8812F(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8812F(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8812F(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_MOREDATA_8812F(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8812F(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8812F(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8812F(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8812F(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8812F(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8812F(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8812F(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8812F(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8812F(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8812F(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8812F(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8812F(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8812F(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8812F(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8812F(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8812F(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8812F(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_MACID_8812F(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8812F(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV_8812F(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8812F(txdesc) GET_TX_DESC_HW_AES_IV(txdesc)
#define SET_TX_DESC_FTM_EN_8812F(txdesc, value)                                \
	SET_TX_DESC_FTM_EN(txdesc, value)
#define GET_TX_DESC_FTM_EN_8812F(txdesc) GET_TX_DESC_FTM_EN(txdesc)
#define SET_TX_DESC_G_ID_8812F(txdesc, value) SET_TX_DESC_G_ID(txdesc, value)
#define GET_TX_DESC_G_ID_8812F(txdesc) GET_TX_DESC_G_ID(txdesc)
#define SET_TX_DESC_BT_NULL_8812F(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8812F(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8812F(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8812F(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8812F(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8812F(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8812F(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8812F(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8812F(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8812F(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8812F(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8812F(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8812F(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8812F(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8812F(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8812F(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8812F(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8812F(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8812F(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8812F(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8812F(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8812F(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8812F(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8812F(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8812F(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8812F(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8812F(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8812F(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8812F(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8812F(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8812F(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8812F(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8812F(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8812F(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8812F(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8812F(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8812F(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8812F(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8812F(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8812F(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8812F(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8812F(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8812F(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8812F(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8812F(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8812F(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8812F(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8812F(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8812F(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8812F(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8812F(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8812F(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8812F(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8812F(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8812F(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8812F(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8812F(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8812F(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8812F(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8812F(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8812F(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8812F(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8812F(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8812F(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8812F(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8812F(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8812F(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8812F(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8812F(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8812F(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8812F(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8812F(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8812F(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8812F(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_ANTSEL_EN_8812F(txdesc, value)                             \
	SET_TX_DESC_ANTSEL_EN_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_EN_8812F(txdesc) GET_TX_DESC_ANTSEL_EN_V1(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_TYPE_8812F(txdesc, value)                      \
	SET_TX_DESC_TXPWR_OFSET_TYPE(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_TYPE_8812F(txdesc)                             \
	GET_TX_DESC_TXPWR_OFSET_TYPE(txdesc)
#define SET_TX_DESC_TX_ANT_8812F(txdesc, value)                                \
	SET_TX_DESC_TX_ANT(txdesc, value)
#define GET_TX_DESC_TX_ANT_8812F(txdesc) GET_TX_DESC_TX_ANT(txdesc)
#define SET_TX_DESC_PORT_ID_8812F(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8812F(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_MULTIPLE_PORT_8812F(txdesc, value)                         \
	SET_TX_DESC_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_MULTIPLE_PORT_8812F(txdesc)                                \
	GET_TX_DESC_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8812F(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8812F(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_SIGNALING_TA_PKT_SC_8812F(txdesc, value)                   \
	SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc, value)
#define GET_TX_DESC_SIGNALING_TA_PKT_SC_8812F(txdesc)                          \
	GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8812F(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8812F(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8812F(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8812F(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8812F(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8812F(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8812F(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8812F(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8812F(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8812F(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8812F(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8812F(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8812F(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8812F(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D_8812F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_D(txdesc, value)
#define GET_TX_DESC_ANTSEL_D_8812F(txdesc) GET_TX_DESC_ANTSEL_D(txdesc)
#define SET_TX_DESC_ANT_MAPD_8812F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8812F(txdesc) GET_TX_DESC_ANT_MAPD(txdesc)
#define SET_TX_DESC_ANT_MAPC_8812F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8812F(txdesc) GET_TX_DESC_ANT_MAPC(txdesc)
#define SET_TX_DESC_ANT_MAPB_8812F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8812F(txdesc) GET_TX_DESC_ANT_MAPB(txdesc)
#define SET_TX_DESC_ANT_MAPA_8812F(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8812F(txdesc) GET_TX_DESC_ANT_MAPA(txdesc)
#define SET_TX_DESC_ANTSEL_C_8812F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8812F(txdesc) GET_TX_DESC_ANTSEL_C(txdesc)
#define SET_TX_DESC_ANTSEL_B_8812F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8812F(txdesc) GET_TX_DESC_ANTSEL_B(txdesc)
#define SET_TX_DESC_ANTSEL_A_8812F(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8812F(txdesc) GET_TX_DESC_ANTSEL_A(txdesc)
#define SET_TX_DESC_MBSSID_8812F(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8812F(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SW_DEFINE_8812F(txdesc, value)                             \
	SET_TX_DESC_SW_DEFINE(txdesc, value)
#define GET_TX_DESC_SW_DEFINE_8812F(txdesc) GET_TX_DESC_SW_DEFINE(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8812F(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8812F(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8812F(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8812F(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8812F(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8812F(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8812F(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8812F(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8812F(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8812F(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8812F(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8812F(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8812F(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8812F(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8812F(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8812F(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8812F(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8812F(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8812F(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8812F(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8812F(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8812F(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8812F(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8812F(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8812F(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8812F(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8812F(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8812F(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8812F(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8812F(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8812F(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8812F(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8812F(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8812F(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_MODE_8812F(txdesc, value)                         \
	SET_TX_DESC_EN_HWSEQ_MODE(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_MODE_8812F(txdesc)                                \
	GET_TX_DESC_EN_HWSEQ_MODE(txdesc)
#define SET_TX_DESC_DATA_RC_8812F(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8812F(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8812F(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8812F(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8812F(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8812F(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8812F(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8812F(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8812F(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8812F(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8812F(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8812F(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8812F(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8812F(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8812F(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8812F(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8812F(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8812F(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#define SET_TX_DESC_HT_DATA_SND_8812F(txdesc, value)                           \
	SET_TX_DESC_HT_DATA_SND(txdesc, value)
#define GET_TX_DESC_HT_DATA_SND_8812F(txdesc) GET_TX_DESC_HT_DATA_SND(txdesc)
#define SET_TX_DESC_SHCUT_CAM_8812F(txdesc, value)                             \
	SET_TX_DESC_SHCUT_CAM(txdesc, value)
#define GET_TX_DESC_SHCUT_CAM_8812F(txdesc) GET_TX_DESC_SHCUT_CAM(txdesc)
#define SET_TX_DESC_MU_DATARATE_8812F(txdesc, value)                           \
	SET_TX_DESC_MU_DATARATE(txdesc, value)
#define GET_TX_DESC_MU_DATARATE_8812F(txdesc) GET_TX_DESC_MU_DATARATE(txdesc)
#define SET_TX_DESC_MU_RC_8812F(txdesc, value) SET_TX_DESC_MU_RC(txdesc, value)
#define GET_TX_DESC_MU_RC_8812F(txdesc) GET_TX_DESC_MU_RC(txdesc)
#define SET_TX_DESC_NDPA_RATE_SEL_8812F(txdesc, value)                         \
	SET_TX_DESC_NDPA_RATE_SEL(txdesc, value)
#define GET_TX_DESC_NDPA_RATE_SEL_8812F(txdesc)                                \
	GET_TX_DESC_NDPA_RATE_SEL(txdesc)
#define SET_TX_DESC_HW_NDPA_EN_8812F(txdesc, value)                            \
	SET_TX_DESC_HW_NDPA_EN(txdesc, value)
#define GET_TX_DESC_HW_NDPA_EN_8812F(txdesc) GET_TX_DESC_HW_NDPA_EN(txdesc)
#define SET_TX_DESC_SND_PKT_SEL_8812F(txdesc, value)                           \
	SET_TX_DESC_SND_PKT_SEL(txdesc, value)
#define GET_TX_DESC_SND_PKT_SEL_8812F(txdesc) GET_TX_DESC_SND_PKT_SEL(txdesc)

#endif

#if (HALMAC_8197G_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8197G(txdesc, value)                            \
	SET_TX_DESC_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_DISQSELSEQ_8197G(txdesc) GET_TX_DESC_DISQSELSEQ(txdesc)
#define SET_TX_DESC_GF_8197G(txdesc, value) SET_TX_DESC_GF(txdesc, value)
#define GET_TX_DESC_GF_8197G(txdesc) GET_TX_DESC_GF(txdesc)
#define SET_TX_DESC_NO_ACM_8197G(txdesc, value)                                \
	SET_TX_DESC_NO_ACM(txdesc, value)
#define GET_TX_DESC_NO_ACM_8197G(txdesc) GET_TX_DESC_NO_ACM(txdesc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8197G(txdesc, value)                       \
	SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8197G(txdesc)                              \
	GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_AMSDU_PAD_EN_8197G(txdesc, value)                          \
	SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)
#define GET_TX_DESC_AMSDU_PAD_EN_8197G(txdesc) GET_TX_DESC_AMSDU_PAD_EN(txdesc)
#define SET_TX_DESC_LS_8197G(txdesc, value) SET_TX_DESC_LS(txdesc, value)
#define GET_TX_DESC_LS_8197G(txdesc) GET_TX_DESC_LS(txdesc)
#define SET_TX_DESC_HTC_8197G(txdesc, value) SET_TX_DESC_HTC(txdesc, value)
#define GET_TX_DESC_HTC_8197G(txdesc) GET_TX_DESC_HTC(txdesc)
#define SET_TX_DESC_BMC_8197G(txdesc, value) SET_TX_DESC_BMC(txdesc, value)
#define GET_TX_DESC_BMC_8197G(txdesc) GET_TX_DESC_BMC(txdesc)
#define SET_TX_DESC_OFFSET_8197G(txdesc, value)                                \
	SET_TX_DESC_OFFSET(txdesc, value)
#define GET_TX_DESC_OFFSET_8197G(txdesc) GET_TX_DESC_OFFSET(txdesc)
#define SET_TX_DESC_TXPKTSIZE_8197G(txdesc, value)                             \
	SET_TX_DESC_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_TXPKTSIZE_8197G(txdesc) GET_TX_DESC_TXPKTSIZE(txdesc)

/*WORD1*/

#define SET_TX_DESC_HW_AES_IV_8197G(txdesc, value)                             \
	SET_TX_DESC_HW_AES_IV_V2(txdesc, value)
#define GET_TX_DESC_HW_AES_IV_8197G(txdesc) GET_TX_DESC_HW_AES_IV_V2(txdesc)
#define SET_TX_DESC_FTM_EN_8197G(txdesc, value)                                \
	SET_TX_DESC_FTM_EN_V1(txdesc, value)
#define GET_TX_DESC_FTM_EN_8197G(txdesc) GET_TX_DESC_FTM_EN_V1(txdesc)
#define SET_TX_DESC_MOREDATA_8197G(txdesc, value)                              \
	SET_TX_DESC_MOREDATA(txdesc, value)
#define GET_TX_DESC_MOREDATA_8197G(txdesc) GET_TX_DESC_MOREDATA(txdesc)
#define SET_TX_DESC_PKT_OFFSET_8197G(txdesc, value)                            \
	SET_TX_DESC_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_PKT_OFFSET_8197G(txdesc) GET_TX_DESC_PKT_OFFSET(txdesc)
#define SET_TX_DESC_SEC_TYPE_8197G(txdesc, value)                              \
	SET_TX_DESC_SEC_TYPE(txdesc, value)
#define GET_TX_DESC_SEC_TYPE_8197G(txdesc) GET_TX_DESC_SEC_TYPE(txdesc)
#define SET_TX_DESC_EN_DESC_ID_8197G(txdesc, value)                            \
	SET_TX_DESC_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_EN_DESC_ID_8197G(txdesc) GET_TX_DESC_EN_DESC_ID(txdesc)
#define SET_TX_DESC_RATE_ID_8197G(txdesc, value)                               \
	SET_TX_DESC_RATE_ID(txdesc, value)
#define GET_TX_DESC_RATE_ID_8197G(txdesc) GET_TX_DESC_RATE_ID(txdesc)
#define SET_TX_DESC_PIFS_8197G(txdesc, value) SET_TX_DESC_PIFS(txdesc, value)
#define GET_TX_DESC_PIFS_8197G(txdesc) GET_TX_DESC_PIFS(txdesc)
#define SET_TX_DESC_LSIG_TXOP_EN_8197G(txdesc, value)                          \
	SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_LSIG_TXOP_EN_8197G(txdesc) GET_TX_DESC_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_RD_NAV_EXT_8197G(txdesc, value)                            \
	SET_TX_DESC_RD_NAV_EXT(txdesc, value)
#define GET_TX_DESC_RD_NAV_EXT_8197G(txdesc) GET_TX_DESC_RD_NAV_EXT(txdesc)
#define SET_TX_DESC_QSEL_8197G(txdesc, value) SET_TX_DESC_QSEL(txdesc, value)
#define GET_TX_DESC_QSEL_8197G(txdesc) GET_TX_DESC_QSEL(txdesc)
#define SET_TX_DESC_SPECIAL_CW_8197G(txdesc, value)                            \
	SET_TX_DESC_SPECIAL_CW(txdesc, value)
#define GET_TX_DESC_SPECIAL_CW_8197G(txdesc) GET_TX_DESC_SPECIAL_CW(txdesc)
#define SET_TX_DESC_MACID_8197G(txdesc, value) SET_TX_DESC_MACID(txdesc, value)
#define GET_TX_DESC_MACID_8197G(txdesc) GET_TX_DESC_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_ANTCEL_D_8197G(txdesc, value)                              \
	SET_TX_DESC_ANTCEL_D_V1(txdesc, value)
#define GET_TX_DESC_ANTCEL_D_8197G(txdesc) GET_TX_DESC_ANTCEL_D_V1(txdesc)
#define SET_TX_DESC_ANTSEL_C_8197G(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_C_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_C_8197G(txdesc) GET_TX_DESC_ANTSEL_C_V1(txdesc)
#define SET_TX_DESC_BT_NULL_8197G(txdesc, value)                               \
	SET_TX_DESC_BT_NULL(txdesc, value)
#define GET_TX_DESC_BT_NULL_8197G(txdesc) GET_TX_DESC_BT_NULL(txdesc)
#define SET_TX_DESC_AMPDU_DENSITY_8197G(txdesc, value)                         \
	SET_TX_DESC_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_AMPDU_DENSITY_8197G(txdesc)                                \
	GET_TX_DESC_AMPDU_DENSITY(txdesc)
#define SET_TX_DESC_SPE_RPT_8197G(txdesc, value)                               \
	SET_TX_DESC_SPE_RPT(txdesc, value)
#define GET_TX_DESC_SPE_RPT_8197G(txdesc) GET_TX_DESC_SPE_RPT(txdesc)
#define SET_TX_DESC_RAW_8197G(txdesc, value) SET_TX_DESC_RAW(txdesc, value)
#define GET_TX_DESC_RAW_8197G(txdesc) GET_TX_DESC_RAW(txdesc)
#define SET_TX_DESC_MOREFRAG_8197G(txdesc, value)                              \
	SET_TX_DESC_MOREFRAG(txdesc, value)
#define GET_TX_DESC_MOREFRAG_8197G(txdesc) GET_TX_DESC_MOREFRAG(txdesc)
#define SET_TX_DESC_BK_8197G(txdesc, value) SET_TX_DESC_BK(txdesc, value)
#define GET_TX_DESC_BK_8197G(txdesc) GET_TX_DESC_BK(txdesc)
#define SET_TX_DESC_NULL_1_8197G(txdesc, value)                                \
	SET_TX_DESC_NULL_1(txdesc, value)
#define GET_TX_DESC_NULL_1_8197G(txdesc) GET_TX_DESC_NULL_1(txdesc)
#define SET_TX_DESC_NULL_0_8197G(txdesc, value)                                \
	SET_TX_DESC_NULL_0(txdesc, value)
#define GET_TX_DESC_NULL_0_8197G(txdesc) GET_TX_DESC_NULL_0(txdesc)
#define SET_TX_DESC_RDG_EN_8197G(txdesc, value)                                \
	SET_TX_DESC_RDG_EN(txdesc, value)
#define GET_TX_DESC_RDG_EN_8197G(txdesc) GET_TX_DESC_RDG_EN(txdesc)
#define SET_TX_DESC_AGG_EN_8197G(txdesc, value)                                \
	SET_TX_DESC_AGG_EN(txdesc, value)
#define GET_TX_DESC_AGG_EN_8197G(txdesc) GET_TX_DESC_AGG_EN(txdesc)
#define SET_TX_DESC_CCA_RTS_8197G(txdesc, value)                               \
	SET_TX_DESC_CCA_RTS(txdesc, value)
#define GET_TX_DESC_CCA_RTS_8197G(txdesc) GET_TX_DESC_CCA_RTS(txdesc)
#define SET_TX_DESC_TRI_FRAME_8197G(txdesc, value)                             \
	SET_TX_DESC_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_TRI_FRAME_8197G(txdesc) GET_TX_DESC_TRI_FRAME(txdesc)
#define SET_TX_DESC_P_AID_8197G(txdesc, value) SET_TX_DESC_P_AID(txdesc, value)
#define GET_TX_DESC_P_AID_8197G(txdesc) GET_TX_DESC_P_AID(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8197G(txdesc, value)                        \
	SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8197G(txdesc)                               \
	GET_TX_DESC_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_NDPA_8197G(txdesc, value) SET_TX_DESC_NDPA(txdesc, value)
#define GET_TX_DESC_NDPA_8197G(txdesc) GET_TX_DESC_NDPA(txdesc)
#define SET_TX_DESC_MAX_AGG_NUM_8197G(txdesc, value)                           \
	SET_TX_DESC_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_MAX_AGG_NUM_8197G(txdesc) GET_TX_DESC_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8197G(txdesc, value)                       \
	SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8197G(txdesc)                              \
	GET_TX_DESC_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_NAVUSEHDR_8197G(txdesc, value)                             \
	SET_TX_DESC_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_NAVUSEHDR_8197G(txdesc) GET_TX_DESC_NAVUSEHDR(txdesc)
#define SET_TX_DESC_CHK_EN_8197G(txdesc, value)                                \
	SET_TX_DESC_CHK_EN(txdesc, value)
#define GET_TX_DESC_CHK_EN_8197G(txdesc) GET_TX_DESC_CHK_EN(txdesc)
#define SET_TX_DESC_HW_RTS_EN_8197G(txdesc, value)                             \
	SET_TX_DESC_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_HW_RTS_EN_8197G(txdesc) GET_TX_DESC_HW_RTS_EN(txdesc)
#define SET_TX_DESC_RTSEN_8197G(txdesc, value) SET_TX_DESC_RTSEN(txdesc, value)
#define GET_TX_DESC_RTSEN_8197G(txdesc) GET_TX_DESC_RTSEN(txdesc)
#define SET_TX_DESC_CTS2SELF_8197G(txdesc, value)                              \
	SET_TX_DESC_CTS2SELF(txdesc, value)
#define GET_TX_DESC_CTS2SELF_8197G(txdesc) GET_TX_DESC_CTS2SELF(txdesc)
#define SET_TX_DESC_DISDATAFB_8197G(txdesc, value)                             \
	SET_TX_DESC_DISDATAFB(txdesc, value)
#define GET_TX_DESC_DISDATAFB_8197G(txdesc) GET_TX_DESC_DISDATAFB(txdesc)
#define SET_TX_DESC_DISRTSFB_8197G(txdesc, value)                              \
	SET_TX_DESC_DISRTSFB(txdesc, value)
#define GET_TX_DESC_DISRTSFB_8197G(txdesc) GET_TX_DESC_DISRTSFB(txdesc)
#define SET_TX_DESC_USE_RATE_8197G(txdesc, value)                              \
	SET_TX_DESC_USE_RATE(txdesc, value)
#define GET_TX_DESC_USE_RATE_8197G(txdesc) GET_TX_DESC_USE_RATE(txdesc)
#define SET_TX_DESC_HW_SSN_SEL_8197G(txdesc, value)                            \
	SET_TX_DESC_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_HW_SSN_SEL_8197G(txdesc) GET_TX_DESC_HW_SSN_SEL(txdesc)
#define SET_TX_DESC_WHEADER_LEN_8197G(txdesc, value)                           \
	SET_TX_DESC_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_WHEADER_LEN_8197G(txdesc) GET_TX_DESC_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8197G(txdesc, value)                         \
	SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)
#define GET_TX_DESC_PCTS_MASK_IDX_8197G(txdesc)                                \
	GET_TX_DESC_PCTS_MASK_IDX(txdesc)
#define SET_TX_DESC_PCTS_EN_8197G(txdesc, value)                               \
	SET_TX_DESC_PCTS_EN(txdesc, value)
#define GET_TX_DESC_PCTS_EN_8197G(txdesc) GET_TX_DESC_PCTS_EN(txdesc)
#define SET_TX_DESC_RTSRATE_8197G(txdesc, value)                               \
	SET_TX_DESC_RTSRATE(txdesc, value)
#define GET_TX_DESC_RTSRATE_8197G(txdesc) GET_TX_DESC_RTSRATE(txdesc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8197G(txdesc, value)                      \
	SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8197G(txdesc)                             \
	GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_RTY_LMT_EN_8197G(txdesc, value)                            \
	SET_TX_DESC_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_RTY_LMT_EN_8197G(txdesc) GET_TX_DESC_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8197G(txdesc, value)                   \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8197G(txdesc)                          \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8197G(txdesc, value)                  \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8197G(txdesc)                         \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_TRY_RATE_8197G(txdesc, value)                              \
	SET_TX_DESC_TRY_RATE(txdesc, value)
#define GET_TX_DESC_TRY_RATE_8197G(txdesc) GET_TX_DESC_TRY_RATE(txdesc)
#define SET_TX_DESC_DATARATE_8197G(txdesc, value)                              \
	SET_TX_DESC_DATARATE(txdesc, value)
#define GET_TX_DESC_DATARATE_8197G(txdesc) GET_TX_DESC_DATARATE(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8197G(txdesc, value)                              \
	SET_TX_DESC_POLLUTED(txdesc, value)
#define GET_TX_DESC_POLLUTED_8197G(txdesc) GET_TX_DESC_POLLUTED(txdesc)
#define SET_TX_DESC_TXPWR_OFSET_8197G(txdesc, value)                           \
	SET_TX_DESC_TXPWR_OFSET(txdesc, value)
#define GET_TX_DESC_TXPWR_OFSET_8197G(txdesc) GET_TX_DESC_TXPWR_OFSET(txdesc)
#define SET_TX_DESC_DROP_ID_8197G(txdesc, value)                               \
	SET_TX_DESC_DROP_ID(txdesc, value)
#define GET_TX_DESC_DROP_ID_8197G(txdesc) GET_TX_DESC_DROP_ID(txdesc)
#define SET_TX_DESC_PORT_ID_8197G(txdesc, value)                               \
	SET_TX_DESC_PORT_ID(txdesc, value)
#define GET_TX_DESC_PORT_ID_8197G(txdesc) GET_TX_DESC_PORT_ID(txdesc)
#define SET_TX_DESC_MULTIPLE_PORT_8197G(txdesc, value)                         \
	SET_TX_DESC_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_MULTIPLE_PORT_8197G(txdesc)                                \
	GET_TX_DESC_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8197G(txdesc, value)                    \
	SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8197G(txdesc)                           \
	GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)
#define SET_TX_DESC_RTS_SC_8197G(txdesc, value)                                \
	SET_TX_DESC_RTS_SC(txdesc, value)
#define GET_TX_DESC_RTS_SC_8197G(txdesc) GET_TX_DESC_RTS_SC(txdesc)
#define SET_TX_DESC_RTS_SHORT_8197G(txdesc, value)                             \
	SET_TX_DESC_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_RTS_SHORT_8197G(txdesc) GET_TX_DESC_RTS_SHORT(txdesc)
#define SET_TX_DESC_VCS_STBC_8197G(txdesc, value)                              \
	SET_TX_DESC_VCS_STBC(txdesc, value)
#define GET_TX_DESC_VCS_STBC_8197G(txdesc) GET_TX_DESC_VCS_STBC(txdesc)
#define SET_TX_DESC_DATA_STBC_8197G(txdesc, value)                             \
	SET_TX_DESC_DATA_STBC(txdesc, value)
#define GET_TX_DESC_DATA_STBC_8197G(txdesc) GET_TX_DESC_DATA_STBC(txdesc)
#define SET_TX_DESC_DATA_LDPC_8197G(txdesc, value)                             \
	SET_TX_DESC_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_DATA_LDPC_8197G(txdesc) GET_TX_DESC_DATA_LDPC(txdesc)
#define SET_TX_DESC_DATA_BW_8197G(txdesc, value)                               \
	SET_TX_DESC_DATA_BW(txdesc, value)
#define GET_TX_DESC_DATA_BW_8197G(txdesc) GET_TX_DESC_DATA_BW(txdesc)
#define SET_TX_DESC_DATA_SHORT_8197G(txdesc, value)                            \
	SET_TX_DESC_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_DATA_SHORT_8197G(txdesc) GET_TX_DESC_DATA_SHORT(txdesc)
#define SET_TX_DESC_DATA_SC_8197G(txdesc, value)                               \
	SET_TX_DESC_DATA_SC(txdesc, value)
#define GET_TX_DESC_DATA_SC_8197G(txdesc) GET_TX_DESC_DATA_SC(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANT_MAPD_8197G(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPD_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPD_8197G(txdesc) GET_TX_DESC_ANT_MAPD_V1(txdesc)
#define SET_TX_DESC_ANT_MAPC_8197G(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPC_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPC_8197G(txdesc) GET_TX_DESC_ANT_MAPC_V1(txdesc)
#define SET_TX_DESC_ANT_MAPB_8197G(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPB_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPB_8197G(txdesc) GET_TX_DESC_ANT_MAPB_V1(txdesc)
#define SET_TX_DESC_ANT_MAPA_8197G(txdesc, value)                              \
	SET_TX_DESC_ANT_MAPA_V1(txdesc, value)
#define GET_TX_DESC_ANT_MAPA_8197G(txdesc) GET_TX_DESC_ANT_MAPA_V1(txdesc)
#define SET_TX_DESC_ANTSEL_B_8197G(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_B_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_B_8197G(txdesc) GET_TX_DESC_ANTSEL_B_V1(txdesc)
#define SET_TX_DESC_ANTSEL_A_8197G(txdesc, value)                              \
	SET_TX_DESC_ANTSEL_A_V1(txdesc, value)
#define GET_TX_DESC_ANTSEL_A_8197G(txdesc) GET_TX_DESC_ANTSEL_A_V1(txdesc)
#define SET_TX_DESC_MBSSID_8197G(txdesc, value)                                \
	SET_TX_DESC_MBSSID(txdesc, value)
#define GET_TX_DESC_MBSSID_8197G(txdesc) GET_TX_DESC_MBSSID(txdesc)
#define SET_TX_DESC_SWPS_SEQ_8197G(txdesc, value)                              \
	SET_TX_DESC_SWPS_SEQ(txdesc, value)
#define GET_TX_DESC_SWPS_SEQ_8197G(txdesc) GET_TX_DESC_SWPS_SEQ(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8197G(txdesc, value)                         \
	SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8197G(txdesc)                                \
	GET_TX_DESC_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_FINAL_DATA_RATE_8197G(txdesc, value)                       \
	SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_FINAL_DATA_RATE_8197G(txdesc)                              \
	GET_TX_DESC_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_NTX_MAP_8197G(txdesc, value)                               \
	SET_TX_DESC_NTX_MAP(txdesc, value)
#define GET_TX_DESC_NTX_MAP_8197G(txdesc) GET_TX_DESC_NTX_MAP(txdesc)
#define SET_TX_DESC_ANTSEL_EN_8197G(txdesc, value)                             \
	SET_TX_DESC_ANTSEL_EN(txdesc, value)
#define GET_TX_DESC_ANTSEL_EN_8197G(txdesc) GET_TX_DESC_ANTSEL_EN(txdesc)
#define SET_TX_DESC_MBSSID_EX_8197G(txdesc, value)                             \
	SET_TX_DESC_MBSSID_EX(txdesc, value)
#define GET_TX_DESC_MBSSID_EX_8197G(txdesc) GET_TX_DESC_MBSSID_EX(txdesc)
#define SET_TX_DESC_TX_BUFF_SIZE_8197G(txdesc, value)                          \
	SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)
#define GET_TX_DESC_TX_BUFF_SIZE_8197G(txdesc) GET_TX_DESC_TX_BUFF_SIZE(txdesc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8197G(txdesc, value)                       \
	SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8197G(txdesc)                              \
	GET_TX_DESC_TXDESC_CHECKSUM(txdesc)
#define SET_TX_DESC_TIMESTAMP_8197G(txdesc, value)                             \
	SET_TX_DESC_TIMESTAMP(txdesc, value)
#define GET_TX_DESC_TIMESTAMP_8197G(txdesc) GET_TX_DESC_TIMESTAMP(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8197G(txdesc, value)                             \
	SET_TX_DESC_TXWIFI_CP(txdesc, value)
#define GET_TX_DESC_TXWIFI_CP_8197G(txdesc) GET_TX_DESC_TXWIFI_CP(txdesc)
#define SET_TX_DESC_MAC_CP_8197G(txdesc, value)                                \
	SET_TX_DESC_MAC_CP(txdesc, value)
#define GET_TX_DESC_MAC_CP_8197G(txdesc) GET_TX_DESC_MAC_CP(txdesc)
#define SET_TX_DESC_STW_PKTRE_DIS_8197G(txdesc, value)                         \
	SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)
#define GET_TX_DESC_STW_PKTRE_DIS_8197G(txdesc)                                \
	GET_TX_DESC_STW_PKTRE_DIS(txdesc)
#define SET_TX_DESC_STW_RB_DIS_8197G(txdesc, value)                            \
	SET_TX_DESC_STW_RB_DIS(txdesc, value)
#define GET_TX_DESC_STW_RB_DIS_8197G(txdesc) GET_TX_DESC_STW_RB_DIS(txdesc)
#define SET_TX_DESC_STW_RATE_DIS_8197G(txdesc, value)                          \
	SET_TX_DESC_STW_RATE_DIS(txdesc, value)
#define GET_TX_DESC_STW_RATE_DIS_8197G(txdesc) GET_TX_DESC_STW_RATE_DIS(txdesc)
#define SET_TX_DESC_STW_ANT_DIS_8197G(txdesc, value)                           \
	SET_TX_DESC_STW_ANT_DIS(txdesc, value)
#define GET_TX_DESC_STW_ANT_DIS_8197G(txdesc) GET_TX_DESC_STW_ANT_DIS(txdesc)
#define SET_TX_DESC_STW_EN_8197G(txdesc, value)                                \
	SET_TX_DESC_STW_EN(txdesc, value)
#define GET_TX_DESC_STW_EN_8197G(txdesc) GET_TX_DESC_STW_EN(txdesc)
#define SET_TX_DESC_SMH_EN_8197G(txdesc, value)                                \
	SET_TX_DESC_SMH_EN(txdesc, value)
#define GET_TX_DESC_SMH_EN_8197G(txdesc) GET_TX_DESC_SMH_EN(txdesc)
#define SET_TX_DESC_TAILPAGE_L_8197G(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_TAILPAGE_L_8197G(txdesc) GET_TX_DESC_TAILPAGE_L(txdesc)
#define SET_TX_DESC_SDIO_DMASEQ_8197G(txdesc, value)                           \
	SET_TX_DESC_SDIO_DMASEQ(txdesc, value)
#define GET_TX_DESC_SDIO_DMASEQ_8197G(txdesc) GET_TX_DESC_SDIO_DMASEQ(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8197G(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8197G(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_L(txdesc)
#define SET_TX_DESC_EN_HWSEQ_MODE_8197G(txdesc, value)                         \
	SET_TX_DESC_EN_HWSEQ_MODE(txdesc, value)
#define GET_TX_DESC_EN_HWSEQ_MODE_8197G(txdesc)                                \
	GET_TX_DESC_EN_HWSEQ_MODE(txdesc)
#define SET_TX_DESC_DATA_RC_8197G(txdesc, value)                               \
	SET_TX_DESC_DATA_RC(txdesc, value)
#define GET_TX_DESC_DATA_RC_8197G(txdesc) GET_TX_DESC_DATA_RC(txdesc)
#define SET_TX_DESC_BAR_RTY_TH_8197G(txdesc, value)                            \
	SET_TX_DESC_BAR_RTY_TH(txdesc, value)
#define GET_TX_DESC_BAR_RTY_TH_8197G(txdesc) GET_TX_DESC_BAR_RTY_TH(txdesc)
#define SET_TX_DESC_RTS_RC_8197G(txdesc, value)                                \
	SET_TX_DESC_RTS_RC(txdesc, value)
#define GET_TX_DESC_RTS_RC_8197G(txdesc) GET_TX_DESC_RTS_RC(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8197G(txdesc, value)                            \
	SET_TX_DESC_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_TAILPAGE_H_8197G(txdesc) GET_TX_DESC_TAILPAGE_H(txdesc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8197G(txdesc, value)                        \
	SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8197G(txdesc)                               \
	GET_TX_DESC_NEXTHEADPAGE_H(txdesc)
#define SET_TX_DESC_SW_SEQ_8197G(txdesc, value)                                \
	SET_TX_DESC_SW_SEQ(txdesc, value)
#define GET_TX_DESC_SW_SEQ_8197G(txdesc) GET_TX_DESC_SW_SEQ(txdesc)
#define SET_TX_DESC_TXBF_PATH_8197G(txdesc, value)                             \
	SET_TX_DESC_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_TXBF_PATH_8197G(txdesc) GET_TX_DESC_TXBF_PATH(txdesc)
#define SET_TX_DESC_PADDING_LEN_8197G(txdesc, value)                           \
	SET_TX_DESC_PADDING_LEN(txdesc, value)
#define GET_TX_DESC_PADDING_LEN_8197G(txdesc) GET_TX_DESC_PADDING_LEN(txdesc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8197G(txdesc, value)                   \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8197G(txdesc)                          \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)

/*WORD10*/

#define SET_TX_DESC_SHCUT_CAM_8197G(txdesc, value)                             \
	SET_TX_DESC_SHCUT_CAM(txdesc, value)
#define GET_TX_DESC_SHCUT_CAM_8197G(txdesc) GET_TX_DESC_SHCUT_CAM(txdesc)
#define SET_TX_DESC_SND_PKT_SEL_8197G(txdesc, value)                           \
	SET_TX_DESC_SND_PKT_SEL(txdesc, value)
#define GET_TX_DESC_SND_PKT_SEL_8197G(txdesc) GET_TX_DESC_SND_PKT_SEL(txdesc)

#endif

#endif

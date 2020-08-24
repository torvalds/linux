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

#ifndef _HALMAC_TX_DESC_BUFFER_CHIP_H_
#define _HALMAC_TX_DESC_BUFFER_CHIP_H_
#if (HALMAC_8814B_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_BUFFER_RDG_EN_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_RDG_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_RDG_EN_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_RDG_EN(txdesc)
#define SET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL_8814B(txdesc, value)                \
	SET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL(txdesc, value)
#define GET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL_8814B(txdesc)                       \
	GET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL(txdesc)
#define SET_TX_DESC_BUFFER_AGG_EN_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_AGG_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_AGG_EN_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_AGG_EN(txdesc)
#define SET_TX_DESC_BUFFER_PKT_OFFSET_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_PKT_OFFSET(txdesc, value)
#define GET_TX_DESC_BUFFER_PKT_OFFSET_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_PKT_OFFSET(txdesc)
#define SET_TX_DESC_BUFFER_OFFSET_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_OFFSET(txdesc, value)
#define GET_TX_DESC_BUFFER_OFFSET_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_OFFSET(txdesc)
#define SET_TX_DESC_BUFFER_TXPKTSIZE_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_TXPKTSIZE(txdesc, value)
#define GET_TX_DESC_BUFFER_TXPKTSIZE_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_TXPKTSIZE(txdesc)

/*TXDESC_WORD1*/

#define SET_TX_DESC_BUFFER_USERATE_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_USERATE(txdesc, value)
#define GET_TX_DESC_BUFFER_USERATE_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_USERATE(txdesc)
#define SET_TX_DESC_BUFFER_AMSDU_8814B(txdesc, value)                          \
	SET_TX_DESC_BUFFER_AMSDU(txdesc, value)
#define GET_TX_DESC_BUFFER_AMSDU_8814B(txdesc) GET_TX_DESC_BUFFER_AMSDU(txdesc)
#define SET_TX_DESC_BUFFER_EN_HWSEQ_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_EN_HWSEQ(txdesc, value)
#define GET_TX_DESC_BUFFER_EN_HWSEQ_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_EN_HWSEQ(txdesc)
#define SET_TX_DESC_BUFFER_EN_HWEXSEQ_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_EN_HWEXSEQ(txdesc, value)
#define GET_TX_DESC_BUFFER_EN_HWEXSEQ_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_EN_HWEXSEQ(txdesc)
#define SET_TX_DESC_BUFFER_SW_SEQ_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_SW_SEQ(txdesc, value)
#define GET_TX_DESC_BUFFER_SW_SEQ_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_SW_SEQ(txdesc)
#define SET_TX_DESC_BUFFER_DROP_ID_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_DROP_ID(txdesc, value)
#define GET_TX_DESC_BUFFER_DROP_ID_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_DROP_ID(txdesc)
#define SET_TX_DESC_BUFFER_MOREDATA_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_MOREDATA(txdesc, value)
#define GET_TX_DESC_BUFFER_MOREDATA_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_MOREDATA(txdesc)
#define SET_TX_DESC_BUFFER_QSEL_8814B(txdesc, value)                           \
	SET_TX_DESC_BUFFER_QSEL(txdesc, value)
#define GET_TX_DESC_BUFFER_QSEL_8814B(txdesc) GET_TX_DESC_BUFFER_QSEL(txdesc)
#define SET_TX_DESC_BUFFER_MACID_8814B(txdesc, value)                          \
	SET_TX_DESC_BUFFER_MACID(txdesc, value)
#define GET_TX_DESC_BUFFER_MACID_8814B(txdesc) GET_TX_DESC_BUFFER_MACID(txdesc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_BUFFER_CHK_EN_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_CHK_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_CHK_EN_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_CHK_EN(txdesc)
#define SET_TX_DESC_BUFFER_DISQSELSEQ_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_DISQSELSEQ(txdesc, value)
#define GET_TX_DESC_BUFFER_DISQSELSEQ_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_DISQSELSEQ(txdesc)
#define SET_TX_DESC_BUFFER_SND_PKT_SEL_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_SND_PKT_SEL(txdesc, value)
#define GET_TX_DESC_BUFFER_SND_PKT_SEL_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_SND_PKT_SEL(txdesc)
#define SET_TX_DESC_BUFFER_DMA_PRI_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_DMA_PRI(txdesc, value)
#define GET_TX_DESC_BUFFER_DMA_PRI_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_DMA_PRI(txdesc)
#define SET_TX_DESC_BUFFER_MAX_AMSDU_MODE_8814B(txdesc, value)                 \
	SET_TX_DESC_BUFFER_MAX_AMSDU_MODE(txdesc, value)
#define GET_TX_DESC_BUFFER_MAX_AMSDU_MODE_8814B(txdesc)                        \
	GET_TX_DESC_BUFFER_MAX_AMSDU_MODE(txdesc)
#define SET_TX_DESC_BUFFER_DMA_TXAGG_NUM_8814B(txdesc, value)                  \
	SET_TX_DESC_BUFFER_DMA_TXAGG_NUM(txdesc, value)
#define GET_TX_DESC_BUFFER_DMA_TXAGG_NUM_8814B(txdesc)                         \
	GET_TX_DESC_BUFFER_DMA_TXAGG_NUM(txdesc)
#define SET_TX_DESC_BUFFER_TXDESC_CHECKSUM_8814B(txdesc, value)                \
	SET_TX_DESC_BUFFER_TXDESC_CHECKSUM(txdesc, value)
#define GET_TX_DESC_BUFFER_TXDESC_CHECKSUM_8814B(txdesc)                       \
	GET_TX_DESC_BUFFER_TXDESC_CHECKSUM(txdesc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_BUFFER_OFFLOAD_SIZE_8814B(txdesc, value)                   \
	SET_TX_DESC_BUFFER_OFFLOAD_SIZE(txdesc, value)
#define GET_TX_DESC_BUFFER_OFFLOAD_SIZE_8814B(txdesc)                          \
	GET_TX_DESC_BUFFER_OFFLOAD_SIZE(txdesc)
#define SET_TX_DESC_BUFFER_CHANNEL_DMA_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_CHANNEL_DMA(txdesc, value)
#define GET_TX_DESC_BUFFER_CHANNEL_DMA_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_CHANNEL_DMA(txdesc)
#define SET_TX_DESC_BUFFER_MBSSID_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_MBSSID(txdesc, value)
#define GET_TX_DESC_BUFFER_MBSSID_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_MBSSID(txdesc)
#define SET_TX_DESC_BUFFER_BK_8814B(txdesc, value)                             \
	SET_TX_DESC_BUFFER_BK(txdesc, value)
#define GET_TX_DESC_BUFFER_BK_8814B(txdesc) GET_TX_DESC_BUFFER_BK(txdesc)
#define SET_TX_DESC_BUFFER_WHEADER_LEN_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_WHEADER_LEN(txdesc, value)
#define GET_TX_DESC_BUFFER_WHEADER_LEN_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_WHEADER_LEN(txdesc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_BUFFER_TRY_RATE_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_TRY_RATE(txdesc, value)
#define GET_TX_DESC_BUFFER_TRY_RATE_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_TRY_RATE(txdesc)
#define SET_TX_DESC_BUFFER_DATA_BW_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_DATA_BW(txdesc, value)
#define GET_TX_DESC_BUFFER_DATA_BW_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_DATA_BW(txdesc)
#define SET_TX_DESC_BUFFER_DATA_SHORT_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_DATA_SHORT(txdesc, value)
#define GET_TX_DESC_BUFFER_DATA_SHORT_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_DATA_SHORT(txdesc)
#define SET_TX_DESC_BUFFER_DATARATE_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_DATARATE(txdesc, value)
#define GET_TX_DESC_BUFFER_DATARATE_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_DATARATE(txdesc)
#define SET_TX_DESC_BUFFER_TXBF_PATH_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_TXBF_PATH(txdesc, value)
#define GET_TX_DESC_BUFFER_TXBF_PATH_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_TXBF_PATH(txdesc)
#define SET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET_8814B(txdesc, value)            \
	SET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET(txdesc, value)
#define GET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET_8814B(txdesc)                   \
	GET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET(txdesc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_BUFFER_RTY_LMT_EN_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_RTY_LMT_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_RTY_LMT_EN_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_RTY_LMT_EN(txdesc)
#define SET_TX_DESC_BUFFER_HW_RTS_EN_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_HW_RTS_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_HW_RTS_EN_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_HW_RTS_EN(txdesc)
#define SET_TX_DESC_BUFFER_RTS_EN_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_RTS_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_RTS_EN_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_RTS_EN(txdesc)
#define SET_TX_DESC_BUFFER_CTS2SELF_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_CTS2SELF(txdesc, value)
#define GET_TX_DESC_BUFFER_CTS2SELF_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_CTS2SELF(txdesc)
#define SET_TX_DESC_BUFFER_TAILPAGE_H_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_TAILPAGE_H(txdesc, value)
#define GET_TX_DESC_BUFFER_TAILPAGE_H_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_TAILPAGE_H(txdesc)
#define SET_TX_DESC_BUFFER_TAILPAGE_L_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_TAILPAGE_L(txdesc, value)
#define GET_TX_DESC_BUFFER_TAILPAGE_L_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_TAILPAGE_L(txdesc)
#define SET_TX_DESC_BUFFER_NAVUSEHDR_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_NAVUSEHDR(txdesc, value)
#define GET_TX_DESC_BUFFER_NAVUSEHDR_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_NAVUSEHDR(txdesc)
#define SET_TX_DESC_BUFFER_BMC_8814B(txdesc, value)                            \
	SET_TX_DESC_BUFFER_BMC(txdesc, value)
#define GET_TX_DESC_BUFFER_BMC_8814B(txdesc) GET_TX_DESC_BUFFER_BMC(txdesc)
#define SET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT_8814B(txdesc, value)               \
	SET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT(txdesc, value)
#define GET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT_8814B(txdesc)                      \
	GET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT(txdesc)
#define SET_TX_DESC_BUFFER_HW_AES_IV_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_HW_AES_IV(txdesc, value)
#define GET_TX_DESC_BUFFER_HW_AES_IV_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_HW_AES_IV(txdesc)
#define SET_TX_DESC_BUFFER_BT_NULL_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_BT_NULL(txdesc, value)
#define GET_TX_DESC_BUFFER_BT_NULL_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_BT_NULL(txdesc)
#define SET_TX_DESC_BUFFER_EN_DESC_ID_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_EN_DESC_ID(txdesc, value)
#define GET_TX_DESC_BUFFER_EN_DESC_ID_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_EN_DESC_ID(txdesc)
#define SET_TX_DESC_BUFFER_SECTYPE_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_SECTYPE(txdesc, value)
#define GET_TX_DESC_BUFFER_SECTYPE_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_SECTYPE(txdesc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_BUFFER_MULTIPLE_PORT_8814B(txdesc, value)                  \
	SET_TX_DESC_BUFFER_MULTIPLE_PORT(txdesc, value)
#define GET_TX_DESC_BUFFER_MULTIPLE_PORT_8814B(txdesc)                         \
	GET_TX_DESC_BUFFER_MULTIPLE_PORT(txdesc)
#define SET_TX_DESC_BUFFER_POLLUTED_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_POLLUTED(txdesc, value)
#define GET_TX_DESC_BUFFER_POLLUTED_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_POLLUTED(txdesc)
#define SET_TX_DESC_BUFFER_NULL_1_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_NULL_1(txdesc, value)
#define GET_TX_DESC_BUFFER_NULL_1_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_NULL_1(txdesc)
#define SET_TX_DESC_BUFFER_NULL_0_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_NULL_0(txdesc, value)
#define GET_TX_DESC_BUFFER_NULL_0_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_NULL_0(txdesc)
#define SET_TX_DESC_BUFFER_TRI_FRAME_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_TRI_FRAME(txdesc, value)
#define GET_TX_DESC_BUFFER_TRI_FRAME_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_TRI_FRAME(txdesc)
#define SET_TX_DESC_BUFFER_SPE_RPT_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_SPE_RPT(txdesc, value)
#define GET_TX_DESC_BUFFER_SPE_RPT_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_SPE_RPT(txdesc)
#define SET_TX_DESC_BUFFER_FTM_EN_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_FTM_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_FTM_EN_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_FTM_EN(txdesc)
#define SET_TX_DESC_BUFFER_MU_DATARATE_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_MU_DATARATE(txdesc, value)
#define GET_TX_DESC_BUFFER_MU_DATARATE_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_MU_DATARATE(txdesc)
#define SET_TX_DESC_BUFFER_CCA_RTS_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_CCA_RTS(txdesc, value)
#define GET_TX_DESC_BUFFER_CCA_RTS_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_CCA_RTS(txdesc)
#define SET_TX_DESC_BUFFER_NDPA_8814B(txdesc, value)                           \
	SET_TX_DESC_BUFFER_NDPA(txdesc, value)
#define GET_TX_DESC_BUFFER_NDPA_8814B(txdesc) GET_TX_DESC_BUFFER_NDPA(txdesc)
#define SET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE_8814B(txdesc, value)               \
	SET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE(txdesc, value)
#define GET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE_8814B(txdesc)                      \
	GET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE(txdesc)
#define SET_TX_DESC_BUFFER_P_AID_8814B(txdesc, value)                          \
	SET_TX_DESC_BUFFER_P_AID(txdesc, value)
#define GET_TX_DESC_BUFFER_P_AID_8814B(txdesc) GET_TX_DESC_BUFFER_P_AID(txdesc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_BUFFER_SW_DEFINE_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_SW_DEFINE(txdesc, value)
#define GET_TX_DESC_BUFFER_SW_DEFINE_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_SW_DEFINE(txdesc)
#define SET_TX_DESC_BUFFER_CTRL_CNT_VALID_8814B(txdesc, value)                 \
	SET_TX_DESC_BUFFER_CTRL_CNT_VALID(txdesc, value)
#define GET_TX_DESC_BUFFER_CTRL_CNT_VALID_8814B(txdesc)                        \
	GET_TX_DESC_BUFFER_CTRL_CNT_VALID(txdesc)
#define SET_TX_DESC_BUFFER_CTRL_CNT_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_CTRL_CNT(txdesc, value)
#define GET_TX_DESC_BUFFER_CTRL_CNT_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_CTRL_CNT(txdesc)
#define SET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE_8814B(txdesc, value)           \
	SET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE_8814B(txdesc)                  \
	GET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE(txdesc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_BUFFER_PATH_MAPA_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_PATH_MAPA(txdesc, value)
#define GET_TX_DESC_BUFFER_PATH_MAPA_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_PATH_MAPA(txdesc)
#define SET_TX_DESC_BUFFER_PATH_MAPB_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_PATH_MAPB(txdesc, value)
#define GET_TX_DESC_BUFFER_PATH_MAPB_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_PATH_MAPB(txdesc)
#define SET_TX_DESC_BUFFER_PATH_MAPC_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_PATH_MAPC(txdesc, value)
#define GET_TX_DESC_BUFFER_PATH_MAPC_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_PATH_MAPC(txdesc)
#define SET_TX_DESC_BUFFER_PATH_MAPD_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_PATH_MAPD(txdesc, value)
#define GET_TX_DESC_BUFFER_PATH_MAPD_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_PATH_MAPD(txdesc)
#define SET_TX_DESC_BUFFER_ANTSEL_A_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_ANTSEL_A(txdesc, value)
#define GET_TX_DESC_BUFFER_ANTSEL_A_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_ANTSEL_A(txdesc)
#define SET_TX_DESC_BUFFER_ANTSEL_B_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_ANTSEL_B(txdesc, value)
#define GET_TX_DESC_BUFFER_ANTSEL_B_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_ANTSEL_B(txdesc)
#define SET_TX_DESC_BUFFER_ANTSEL_C_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_ANTSEL_C(txdesc, value)
#define GET_TX_DESC_BUFFER_ANTSEL_C_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_ANTSEL_C(txdesc)
#define SET_TX_DESC_BUFFER_ANTSEL_D_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_ANTSEL_D(txdesc, value)
#define GET_TX_DESC_BUFFER_ANTSEL_D_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_ANTSEL_D(txdesc)
#define SET_TX_DESC_BUFFER_NTX_PATH_EN_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_NTX_PATH_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_NTX_PATH_EN_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_NTX_PATH_EN(txdesc)
#define SET_TX_DESC_BUFFER_ANTLSEL_EN_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_ANTLSEL_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_ANTLSEL_EN_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_ANTLSEL_EN(txdesc)
#define SET_TX_DESC_BUFFER_AMPDU_DENSITY_8814B(txdesc, value)                  \
	SET_TX_DESC_BUFFER_AMPDU_DENSITY(txdesc, value)
#define GET_TX_DESC_BUFFER_AMPDU_DENSITY_8814B(txdesc)                         \
	GET_TX_DESC_BUFFER_AMPDU_DENSITY(txdesc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_BUFFER_VCS_STBC_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_VCS_STBC(txdesc, value)
#define GET_TX_DESC_BUFFER_VCS_STBC_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_VCS_STBC(txdesc)
#define SET_TX_DESC_BUFFER_DATA_STBC_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_DATA_STBC(txdesc, value)
#define GET_TX_DESC_BUFFER_DATA_STBC_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_DATA_STBC(txdesc)
#define SET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE_8814B(txdesc, value)            \
	SET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE(txdesc, value)
#define GET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE_8814B(txdesc)                   \
	GET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE(txdesc)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN_8814B(txdesc, value)            \
	SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN_8814B(txdesc)                   \
	GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN(txdesc)
#define SET_TX_DESC_BUFFER_MHR_CP_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_MHR_CP(txdesc, value)
#define GET_TX_DESC_BUFFER_MHR_CP_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_MHR_CP(txdesc)
#define SET_TX_DESC_BUFFER_SMH_EN_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_SMH_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_SMH_EN_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_SMH_EN(txdesc)
#define SET_TX_DESC_BUFFER_RTSRATE_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_RTSRATE(txdesc, value)
#define GET_TX_DESC_BUFFER_RTSRATE_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_RTSRATE(txdesc)
#define SET_TX_DESC_BUFFER_SMH_CAM_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_SMH_CAM(txdesc, value)
#define GET_TX_DESC_BUFFER_SMH_CAM_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_SMH_CAM(txdesc)
#define SET_TX_DESC_BUFFER_ARFR_TABLE_SEL_8814B(txdesc, value)                 \
	SET_TX_DESC_BUFFER_ARFR_TABLE_SEL(txdesc, value)
#define GET_TX_DESC_BUFFER_ARFR_TABLE_SEL_8814B(txdesc)                        \
	GET_TX_DESC_BUFFER_ARFR_TABLE_SEL(txdesc)
#define SET_TX_DESC_BUFFER_ARFR_HT_EN_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_ARFR_HT_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_ARFR_HT_EN_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_ARFR_HT_EN(txdesc)
#define SET_TX_DESC_BUFFER_ARFR_OFDM_EN_8814B(txdesc, value)                   \
	SET_TX_DESC_BUFFER_ARFR_OFDM_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_ARFR_OFDM_EN_8814B(txdesc)                          \
	GET_TX_DESC_BUFFER_ARFR_OFDM_EN(txdesc)
#define SET_TX_DESC_BUFFER_ARFR_CCK_EN_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_ARFR_CCK_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_ARFR_CCK_EN_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_ARFR_CCK_EN(txdesc)
#define SET_TX_DESC_BUFFER_RTS_SHORT_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_RTS_SHORT(txdesc, value)
#define GET_TX_DESC_BUFFER_RTS_SHORT_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_RTS_SHORT(txdesc)
#define SET_TX_DESC_BUFFER_DISDATAFB_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_DISDATAFB(txdesc, value)
#define GET_TX_DESC_BUFFER_DISDATAFB_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_DISDATAFB(txdesc)
#define SET_TX_DESC_BUFFER_DISRTSFB_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_DISRTSFB(txdesc, value)
#define GET_TX_DESC_BUFFER_DISRTSFB_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_DISRTSFB(txdesc)
#define SET_TX_DESC_BUFFER_EXT_EDCA_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_EXT_EDCA(txdesc, value)
#define GET_TX_DESC_BUFFER_EXT_EDCA_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_EXT_EDCA(txdesc)

/*TXDESC_WORD10*/

#define SET_TX_DESC_BUFFER_AMPDU_MAX_TIME_8814B(txdesc, value)                 \
	SET_TX_DESC_BUFFER_AMPDU_MAX_TIME(txdesc, value)
#define GET_TX_DESC_BUFFER_AMPDU_MAX_TIME_8814B(txdesc)                        \
	GET_TX_DESC_BUFFER_AMPDU_MAX_TIME(txdesc)
#define SET_TX_DESC_BUFFER_SPECIAL_CW_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_SPECIAL_CW(txdesc, value)
#define GET_TX_DESC_BUFFER_SPECIAL_CW_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_SPECIAL_CW(txdesc)
#define SET_TX_DESC_BUFFER_RDG_NAV_EXT_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_RDG_NAV_EXT(txdesc, value)
#define GET_TX_DESC_BUFFER_RDG_NAV_EXT_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_RDG_NAV_EXT(txdesc)
#define SET_TX_DESC_BUFFER_RAW_8814B(txdesc, value)                            \
	SET_TX_DESC_BUFFER_RAW(txdesc, value)
#define GET_TX_DESC_BUFFER_RAW_8814B(txdesc) GET_TX_DESC_BUFFER_RAW(txdesc)
#define SET_TX_DESC_BUFFER_MAX_AGG_NUM_8814B(txdesc, value)                    \
	SET_TX_DESC_BUFFER_MAX_AGG_NUM(txdesc, value)
#define GET_TX_DESC_BUFFER_MAX_AGG_NUM_8814B(txdesc)                           \
	GET_TX_DESC_BUFFER_MAX_AGG_NUM(txdesc)
#define SET_TX_DESC_BUFFER_FINAL_DATA_RATE_8814B(txdesc, value)                \
	SET_TX_DESC_BUFFER_FINAL_DATA_RATE(txdesc, value)
#define GET_TX_DESC_BUFFER_FINAL_DATA_RATE_8814B(txdesc)                       \
	GET_TX_DESC_BUFFER_FINAL_DATA_RATE(txdesc)
#define SET_TX_DESC_BUFFER_GF_8814B(txdesc, value)                             \
	SET_TX_DESC_BUFFER_GF(txdesc, value)
#define GET_TX_DESC_BUFFER_GF_8814B(txdesc) GET_TX_DESC_BUFFER_GF(txdesc)
#define SET_TX_DESC_BUFFER_MOREFRAG_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_MOREFRAG(txdesc, value)
#define GET_TX_DESC_BUFFER_MOREFRAG_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_MOREFRAG(txdesc)
#define SET_TX_DESC_BUFFER_NOACM_8814B(txdesc, value)                          \
	SET_TX_DESC_BUFFER_NOACM(txdesc, value)
#define GET_TX_DESC_BUFFER_NOACM_8814B(txdesc) GET_TX_DESC_BUFFER_NOACM(txdesc)
#define SET_TX_DESC_BUFFER_HTC_8814B(txdesc, value)                            \
	SET_TX_DESC_BUFFER_HTC(txdesc, value)
#define GET_TX_DESC_BUFFER_HTC_8814B(txdesc) GET_TX_DESC_BUFFER_HTC(txdesc)
#define SET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS_8814B(txdesc, value)              \
	SET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS(txdesc, value)
#define GET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS_8814B(txdesc)                     \
	GET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS(txdesc)
#define SET_TX_DESC_BUFFER_USE_MAX_TIME_EN_8814B(txdesc, value)                \
	SET_TX_DESC_BUFFER_USE_MAX_TIME_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_USE_MAX_TIME_EN_8814B(txdesc)                       \
	GET_TX_DESC_BUFFER_USE_MAX_TIME_EN(txdesc)
#define SET_TX_DESC_BUFFER_HW_SSN_SEL_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_HW_SSN_SEL(txdesc, value)
#define GET_TX_DESC_BUFFER_HW_SSN_SEL_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_HW_SSN_SEL(txdesc)

/*TXDESC_WORD11*/

#define SET_TX_DESC_BUFFER_ADDR_CAM_8814B(txdesc, value)                       \
	SET_TX_DESC_BUFFER_ADDR_CAM(txdesc, value)
#define GET_TX_DESC_BUFFER_ADDR_CAM_8814B(txdesc)                              \
	GET_TX_DESC_BUFFER_ADDR_CAM(txdesc)
#define SET_TX_DESC_BUFFER_SND_TARGET_8814B(txdesc, value)                     \
	SET_TX_DESC_BUFFER_SND_TARGET(txdesc, value)
#define GET_TX_DESC_BUFFER_SND_TARGET_8814B(txdesc)                            \
	GET_TX_DESC_BUFFER_SND_TARGET(txdesc)
#define SET_TX_DESC_BUFFER_DATA_LDPC_8814B(txdesc, value)                      \
	SET_TX_DESC_BUFFER_DATA_LDPC(txdesc, value)
#define GET_TX_DESC_BUFFER_DATA_LDPC_8814B(txdesc)                             \
	GET_TX_DESC_BUFFER_DATA_LDPC(txdesc)
#define SET_TX_DESC_BUFFER_LSIG_TXOP_EN_8814B(txdesc, value)                   \
	SET_TX_DESC_BUFFER_LSIG_TXOP_EN(txdesc, value)
#define GET_TX_DESC_BUFFER_LSIG_TXOP_EN_8814B(txdesc)                          \
	GET_TX_DESC_BUFFER_LSIG_TXOP_EN(txdesc)
#define SET_TX_DESC_BUFFER_G_ID_8814B(txdesc, value)                           \
	SET_TX_DESC_BUFFER_G_ID(txdesc, value)
#define GET_TX_DESC_BUFFER_G_ID_8814B(txdesc) GET_TX_DESC_BUFFER_G_ID(txdesc)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC_8814B(txdesc, value)            \
	SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC(txdesc, value)
#define GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC_8814B(txdesc)                   \
	GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC(txdesc)
#define SET_TX_DESC_BUFFER_DATA_SC_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_DATA_SC(txdesc, value)
#define GET_TX_DESC_BUFFER_DATA_SC_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_DATA_SC(txdesc)

/*TXDESC_WORD12*/

#define SET_TX_DESC_BUFFER_LEN1_L_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_LEN1_L(txdesc, value)
#define GET_TX_DESC_BUFFER_LEN1_L_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_LEN1_L(txdesc)
#define SET_TX_DESC_BUFFER_LEN0_8814B(txdesc, value)                           \
	SET_TX_DESC_BUFFER_LEN0(txdesc, value)
#define GET_TX_DESC_BUFFER_LEN0_8814B(txdesc) GET_TX_DESC_BUFFER_LEN0(txdesc)
#define SET_TX_DESC_BUFFER_PKT_NUM_8814B(txdesc, value)                        \
	SET_TX_DESC_BUFFER_PKT_NUM(txdesc, value)
#define GET_TX_DESC_BUFFER_PKT_NUM_8814B(txdesc)                               \
	GET_TX_DESC_BUFFER_PKT_NUM(txdesc)

/*TXDESC_WORD13*/

#define SET_TX_DESC_BUFFER_LEN3_8814B(txdesc, value)                           \
	SET_TX_DESC_BUFFER_LEN3(txdesc, value)
#define GET_TX_DESC_BUFFER_LEN3_8814B(txdesc) GET_TX_DESC_BUFFER_LEN3(txdesc)
#define SET_TX_DESC_BUFFER_LEN2_8814B(txdesc, value)                           \
	SET_TX_DESC_BUFFER_LEN2(txdesc, value)
#define GET_TX_DESC_BUFFER_LEN2_8814B(txdesc) GET_TX_DESC_BUFFER_LEN2(txdesc)
#define SET_TX_DESC_BUFFER_LEN1_H_8814B(txdesc, value)                         \
	SET_TX_DESC_BUFFER_LEN1_H(txdesc, value)
#define GET_TX_DESC_BUFFER_LEN1_H_8814B(txdesc)                                \
	GET_TX_DESC_BUFFER_LEN1_H(txdesc)

#endif

#endif

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

#ifndef _HALMAC_TX_DESC_BUFFER_NIC_H_
#define _HALMAC_TX_DESC_BUFFER_NIC_H_
#if (HALMAC_8814B_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_BUFFER_RDG_EN(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 31, 1, value)
#define GET_TX_DESC_BUFFER_RDG_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 31, 1)
#define SET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL(txdesc, value)                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 30, 1, value)
#define GET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL(txdesc)                             \
	LE_BITS_TO_4BYTE(txdesc + 0x00, 30, 1)
#define SET_TX_DESC_BUFFER_AGG_EN(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 29, 1, value)
#define GET_TX_DESC_BUFFER_AGG_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 29, 1)
#define SET_TX_DESC_BUFFER_PKT_OFFSET(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 24, 5, value)
#define GET_TX_DESC_BUFFER_PKT_OFFSET(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x00, 24, 5)
#define SET_TX_DESC_BUFFER_OFFSET(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 16, 8, value)
#define GET_TX_DESC_BUFFER_OFFSET(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 16, 8)
#define SET_TX_DESC_BUFFER_TXPKTSIZE(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 0, 16, value)
#define GET_TX_DESC_BUFFER_TXPKTSIZE(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x00, 0, 16)

/*TXDESC_WORD1*/

#define SET_TX_DESC_BUFFER_USERATE(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 31, 1, value)
#define GET_TX_DESC_BUFFER_USERATE(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x04, 31, 1)
#define SET_TX_DESC_BUFFER_AMSDU(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 30, 1, value)
#define GET_TX_DESC_BUFFER_AMSDU(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 30, 1)
#define SET_TX_DESC_BUFFER_EN_HWSEQ(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 29, 1, value)
#define GET_TX_DESC_BUFFER_EN_HWSEQ(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x04, 29, 1)
#define SET_TX_DESC_BUFFER_EN_HWEXSEQ(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 28, 1, value)
#define GET_TX_DESC_BUFFER_EN_HWEXSEQ(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x04, 28, 1)
#define SET_TX_DESC_BUFFER_SW_SEQ(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 16, 12, value)
#define GET_TX_DESC_BUFFER_SW_SEQ(txdesc)                                      \
	LE_BITS_TO_4BYTE(txdesc + 0x04, 16, 12)
#define SET_TX_DESC_BUFFER_DROP_ID(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 14, 2, value)
#define GET_TX_DESC_BUFFER_DROP_ID(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x04, 14, 2)
#define SET_TX_DESC_BUFFER_MOREDATA(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 13, 1, value)
#define GET_TX_DESC_BUFFER_MOREDATA(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x04, 13, 1)
#define SET_TX_DESC_BUFFER_QSEL(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 8, 5, value)
#define GET_TX_DESC_BUFFER_QSEL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 8, 5)
#define SET_TX_DESC_BUFFER_MACID(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 0, 8, value)
#define GET_TX_DESC_BUFFER_MACID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 0, 8)

/*TXDESC_WORD2*/

#define SET_TX_DESC_BUFFER_CHK_EN(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 31, 1, value)
#define GET_TX_DESC_BUFFER_CHK_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 31, 1)
#define SET_TX_DESC_BUFFER_DISQSELSEQ(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 30, 1, value)
#define GET_TX_DESC_BUFFER_DISQSELSEQ(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 30, 1)
#define SET_TX_DESC_BUFFER_SND_PKT_SEL(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 28, 2, value)
#define GET_TX_DESC_BUFFER_SND_PKT_SEL(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 28, 2)
#define SET_TX_DESC_BUFFER_DMA_PRI(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 27, 1, value)
#define GET_TX_DESC_BUFFER_DMA_PRI(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 27, 1)
#define SET_TX_DESC_BUFFER_MAX_AMSDU_MODE(txdesc, value)                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 24, 3, value)
#define GET_TX_DESC_BUFFER_MAX_AMSDU_MODE(txdesc)                              \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 24, 3)
#define SET_TX_DESC_BUFFER_DMA_TXAGG_NUM(txdesc, value)                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 16, 8, value)
#define GET_TX_DESC_BUFFER_DMA_TXAGG_NUM(txdesc)                               \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 16, 8)
#define SET_TX_DESC_BUFFER_TXDESC_CHECKSUM(txdesc, value)                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 0, 16, value)
#define GET_TX_DESC_BUFFER_TXDESC_CHECKSUM(txdesc)                             \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 0, 16)

/*TXDESC_WORD3*/

#define SET_TX_DESC_BUFFER_OFFLOAD_SIZE(txdesc, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 16, 15, value)
#define GET_TX_DESC_BUFFER_OFFLOAD_SIZE(txdesc)                                \
	LE_BITS_TO_4BYTE(txdesc + 0x0C, 16, 15)
#define SET_TX_DESC_BUFFER_CHANNEL_DMA(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 11, 5, value)
#define GET_TX_DESC_BUFFER_CHANNEL_DMA(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x0C, 11, 5)
#define SET_TX_DESC_BUFFER_MBSSID(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 7, 4, value)
#define GET_TX_DESC_BUFFER_MBSSID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 7, 4)
#define SET_TX_DESC_BUFFER_BK(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 6, 1, value)
#define GET_TX_DESC_BUFFER_BK(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 6, 1)
#define SET_TX_DESC_BUFFER_WHEADER_LEN(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 0, 5, value)
#define GET_TX_DESC_BUFFER_WHEADER_LEN(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x0C, 0, 5)

/*TXDESC_WORD4*/

#define SET_TX_DESC_BUFFER_TRY_RATE(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 26, 1, value)
#define GET_TX_DESC_BUFFER_TRY_RATE(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 26, 1)
#define SET_TX_DESC_BUFFER_DATA_BW(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 24, 2, value)
#define GET_TX_DESC_BUFFER_DATA_BW(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 24, 2)
#define SET_TX_DESC_BUFFER_DATA_SHORT(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 23, 1, value)
#define GET_TX_DESC_BUFFER_DATA_SHORT(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 23, 1)
#define SET_TX_DESC_BUFFER_DATARATE(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 16, 7, value)
#define GET_TX_DESC_BUFFER_DATARATE(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 16, 7)
#define SET_TX_DESC_BUFFER_TXBF_PATH(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 11, 1, value)
#define GET_TX_DESC_BUFFER_TXBF_PATH(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 11, 1)
#define SET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET(txdesc, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 0, 11, value)
#define GET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET(txdesc)                         \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 0, 11)

/*TXDESC_WORD5*/

#define SET_TX_DESC_BUFFER_RTY_LMT_EN(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 31, 1, value)
#define GET_TX_DESC_BUFFER_RTY_LMT_EN(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 31, 1)
#define SET_TX_DESC_BUFFER_HW_RTS_EN(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 30, 1, value)
#define GET_TX_DESC_BUFFER_HW_RTS_EN(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 30, 1)
#define SET_TX_DESC_BUFFER_RTS_EN(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 29, 1, value)
#define GET_TX_DESC_BUFFER_RTS_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 29, 1)
#define SET_TX_DESC_BUFFER_CTS2SELF(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 28, 1, value)
#define GET_TX_DESC_BUFFER_CTS2SELF(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 28, 1)
#define SET_TX_DESC_BUFFER_TAILPAGE_H(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 24, 4, value)
#define GET_TX_DESC_BUFFER_TAILPAGE_H(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 24, 4)
#define SET_TX_DESC_BUFFER_TAILPAGE_L(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 16, 8, value)
#define GET_TX_DESC_BUFFER_TAILPAGE_L(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 16, 8)
#define SET_TX_DESC_BUFFER_NAVUSEHDR(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 15, 1, value)
#define GET_TX_DESC_BUFFER_NAVUSEHDR(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 15, 1)
#define SET_TX_DESC_BUFFER_BMC(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 14, 1, value)
#define GET_TX_DESC_BUFFER_BMC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 14, 1)
#define SET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT(txdesc, value)                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 8, 6, value)
#define GET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT(txdesc)                            \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 8, 6)
#define SET_TX_DESC_BUFFER_HW_AES_IV(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 7, 1, value)
#define GET_TX_DESC_BUFFER_HW_AES_IV(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 7, 1)
#define SET_TX_DESC_BUFFER_BT_NULL(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 3, 1, value)
#define GET_TX_DESC_BUFFER_BT_NULL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 3, 1)
#define SET_TX_DESC_BUFFER_EN_DESC_ID(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 2, 1, value)
#define GET_TX_DESC_BUFFER_EN_DESC_ID(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 2, 1)
#define SET_TX_DESC_BUFFER_SECTYPE(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 0, 2, value)
#define GET_TX_DESC_BUFFER_SECTYPE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 0, 2)

/*TXDESC_WORD6*/

#define SET_TX_DESC_BUFFER_MULTIPLE_PORT(txdesc, value)                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 29, 3, value)
#define GET_TX_DESC_BUFFER_MULTIPLE_PORT(txdesc)                               \
	LE_BITS_TO_4BYTE(txdesc + 0x18, 29, 3)
#define SET_TX_DESC_BUFFER_POLLUTED(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 28, 1, value)
#define GET_TX_DESC_BUFFER_POLLUTED(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x18, 28, 1)
#define SET_TX_DESC_BUFFER_NULL_1(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 27, 1, value)
#define GET_TX_DESC_BUFFER_NULL_1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 27, 1)
#define SET_TX_DESC_BUFFER_NULL_0(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 26, 1, value)
#define GET_TX_DESC_BUFFER_NULL_0(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 26, 1)
#define SET_TX_DESC_BUFFER_TRI_FRAME(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 25, 1, value)
#define GET_TX_DESC_BUFFER_TRI_FRAME(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x18, 25, 1)
#define SET_TX_DESC_BUFFER_SPE_RPT(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 24, 1, value)
#define GET_TX_DESC_BUFFER_SPE_RPT(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x18, 24, 1)
#define SET_TX_DESC_BUFFER_FTM_EN(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 23, 1, value)
#define GET_TX_DESC_BUFFER_FTM_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 23, 1)
#define SET_TX_DESC_BUFFER_MU_DATARATE(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 16, 7, value)
#define GET_TX_DESC_BUFFER_MU_DATARATE(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x18, 16, 7)
#define SET_TX_DESC_BUFFER_CCA_RTS(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 14, 2, value)
#define GET_TX_DESC_BUFFER_CCA_RTS(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x18, 14, 2)
#define SET_TX_DESC_BUFFER_NDPA(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 12, 2, value)
#define GET_TX_DESC_BUFFER_NDPA(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 12, 2)
#define SET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE(txdesc, value)                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 9, 2, value)
#define GET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE(txdesc)                            \
	LE_BITS_TO_4BYTE(txdesc + 0x18, 9, 2)
#define SET_TX_DESC_BUFFER_P_AID(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 0, 9, value)
#define GET_TX_DESC_BUFFER_P_AID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 0, 9)

/*TXDESC_WORD7*/

#define SET_TX_DESC_BUFFER_SW_DEFINE(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 16, 12, value)
#define GET_TX_DESC_BUFFER_SW_DEFINE(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x1C, 16, 12)
#define SET_TX_DESC_BUFFER_CTRL_CNT_VALID(txdesc, value)                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 9, 1, value)
#define GET_TX_DESC_BUFFER_CTRL_CNT_VALID(txdesc)                              \
	LE_BITS_TO_4BYTE(txdesc + 0x1C, 9, 1)
#define SET_TX_DESC_BUFFER_CTRL_CNT(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 5, 4, value)
#define GET_TX_DESC_BUFFER_CTRL_CNT(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x1C, 5, 4)
#define SET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE(txdesc, value)                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 0, 5, value)
#define GET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE(txdesc)                        \
	LE_BITS_TO_4BYTE(txdesc + 0x1C, 0, 5)

/*TXDESC_WORD8*/

#define SET_TX_DESC_BUFFER_PATH_MAPA(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 30, 2, value)
#define GET_TX_DESC_BUFFER_PATH_MAPA(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 30, 2)
#define SET_TX_DESC_BUFFER_PATH_MAPB(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 28, 2, value)
#define GET_TX_DESC_BUFFER_PATH_MAPB(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 28, 2)
#define SET_TX_DESC_BUFFER_PATH_MAPC(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 26, 2, value)
#define GET_TX_DESC_BUFFER_PATH_MAPC(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 26, 2)
#define SET_TX_DESC_BUFFER_PATH_MAPD(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 24, 2, value)
#define GET_TX_DESC_BUFFER_PATH_MAPD(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 24, 2)
#define SET_TX_DESC_BUFFER_ANTSEL_A(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 20, 4, value)
#define GET_TX_DESC_BUFFER_ANTSEL_A(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 20, 4)
#define SET_TX_DESC_BUFFER_ANTSEL_B(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 16, 4, value)
#define GET_TX_DESC_BUFFER_ANTSEL_B(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 16, 4)
#define SET_TX_DESC_BUFFER_ANTSEL_C(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 12, 4, value)
#define GET_TX_DESC_BUFFER_ANTSEL_C(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 12, 4)
#define SET_TX_DESC_BUFFER_ANTSEL_D(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 8, 4, value)
#define GET_TX_DESC_BUFFER_ANTSEL_D(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 8, 4)
#define SET_TX_DESC_BUFFER_NTX_PATH_EN(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 4, 4, value)
#define GET_TX_DESC_BUFFER_NTX_PATH_EN(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 4, 4)
#define SET_TX_DESC_BUFFER_ANTLSEL_EN(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 3, 1, value)
#define GET_TX_DESC_BUFFER_ANTLSEL_EN(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 3, 1)
#define SET_TX_DESC_BUFFER_AMPDU_DENSITY(txdesc, value)                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 0, 3, value)
#define GET_TX_DESC_BUFFER_AMPDU_DENSITY(txdesc)                               \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 0, 3)

/*TXDESC_WORD9*/

#define SET_TX_DESC_BUFFER_VCS_STBC(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 30, 2, value)
#define GET_TX_DESC_BUFFER_VCS_STBC(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 30, 2)
#define SET_TX_DESC_BUFFER_DATA_STBC(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 28, 2, value)
#define GET_TX_DESC_BUFFER_DATA_STBC(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 28, 2)
#define SET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE(txdesc, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 24, 4, value)
#define GET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE(txdesc)                         \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 24, 4)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN(txdesc, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 23, 1, value)
#define GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN(txdesc)                         \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 23, 1)
#define SET_TX_DESC_BUFFER_MHR_CP(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 22, 1, value)
#define GET_TX_DESC_BUFFER_MHR_CP(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x24, 22, 1)
#define SET_TX_DESC_BUFFER_SMH_EN(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 21, 1, value)
#define GET_TX_DESC_BUFFER_SMH_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x24, 21, 1)
#define SET_TX_DESC_BUFFER_RTSRATE(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 16, 5, value)
#define GET_TX_DESC_BUFFER_RTSRATE(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 16, 5)
#define SET_TX_DESC_BUFFER_SMH_CAM(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 8, 8, value)
#define GET_TX_DESC_BUFFER_SMH_CAM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x24, 8, 8)
#define SET_TX_DESC_BUFFER_ARFR_TABLE_SEL(txdesc, value)                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 7, 1, value)
#define GET_TX_DESC_BUFFER_ARFR_TABLE_SEL(txdesc)                              \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 7, 1)
#define SET_TX_DESC_BUFFER_ARFR_HT_EN(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 6, 1, value)
#define GET_TX_DESC_BUFFER_ARFR_HT_EN(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 6, 1)
#define SET_TX_DESC_BUFFER_ARFR_OFDM_EN(txdesc, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 5, 1, value)
#define GET_TX_DESC_BUFFER_ARFR_OFDM_EN(txdesc)                                \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 5, 1)
#define SET_TX_DESC_BUFFER_ARFR_CCK_EN(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 4, 1, value)
#define GET_TX_DESC_BUFFER_ARFR_CCK_EN(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 4, 1)
#define SET_TX_DESC_BUFFER_RTS_SHORT(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 3, 1, value)
#define GET_TX_DESC_BUFFER_RTS_SHORT(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 3, 1)
#define SET_TX_DESC_BUFFER_DISDATAFB(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 2, 1, value)
#define GET_TX_DESC_BUFFER_DISDATAFB(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 2, 1)
#define SET_TX_DESC_BUFFER_DISRTSFB(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 1, 1, value)
#define GET_TX_DESC_BUFFER_DISRTSFB(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 1, 1)
#define SET_TX_DESC_BUFFER_EXT_EDCA(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 0, 1, value)
#define GET_TX_DESC_BUFFER_EXT_EDCA(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 0, 1)

/*TXDESC_WORD10*/

#define SET_TX_DESC_BUFFER_AMPDU_MAX_TIME(txdesc, value)                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 24, 8, value)
#define GET_TX_DESC_BUFFER_AMPDU_MAX_TIME(txdesc)                              \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 24, 8)
#define SET_TX_DESC_BUFFER_SPECIAL_CW(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 23, 1, value)
#define GET_TX_DESC_BUFFER_SPECIAL_CW(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 23, 1)
#define SET_TX_DESC_BUFFER_RDG_NAV_EXT(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 22, 1, value)
#define GET_TX_DESC_BUFFER_RDG_NAV_EXT(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 22, 1)
#define SET_TX_DESC_BUFFER_RAW(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 21, 1, value)
#define GET_TX_DESC_BUFFER_RAW(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 21, 1)
#define SET_TX_DESC_BUFFER_MAX_AGG_NUM(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 16, 5, value)
#define GET_TX_DESC_BUFFER_MAX_AGG_NUM(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 16, 5)
#define SET_TX_DESC_BUFFER_FINAL_DATA_RATE(txdesc, value)                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 8, 8, value)
#define GET_TX_DESC_BUFFER_FINAL_DATA_RATE(txdesc)                             \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 8, 8)
#define SET_TX_DESC_BUFFER_GF(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 7, 1, value)
#define GET_TX_DESC_BUFFER_GF(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 7, 1)
#define SET_TX_DESC_BUFFER_MOREFRAG(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 6, 1, value)
#define GET_TX_DESC_BUFFER_MOREFRAG(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 6, 1)
#define SET_TX_DESC_BUFFER_NOACM(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 5, 1, value)
#define GET_TX_DESC_BUFFER_NOACM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 5, 1)
#define SET_TX_DESC_BUFFER_HTC(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 4, 1, value)
#define GET_TX_DESC_BUFFER_HTC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 4, 1)
#define SET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS(txdesc, value)                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 3, 1, value)
#define GET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS(txdesc)                           \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 3, 1)
#define SET_TX_DESC_BUFFER_USE_MAX_TIME_EN(txdesc, value)                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 2, 1, value)
#define GET_TX_DESC_BUFFER_USE_MAX_TIME_EN(txdesc)                             \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 2, 1)
#define SET_TX_DESC_BUFFER_HW_SSN_SEL(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 0, 2, value)
#define GET_TX_DESC_BUFFER_HW_SSN_SEL(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x28, 0, 2)

/*TXDESC_WORD11*/

#define SET_TX_DESC_BUFFER_ADDR_CAM(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x2C, 24, 8, value)
#define GET_TX_DESC_BUFFER_ADDR_CAM(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x2C, 24, 8)
#define SET_TX_DESC_BUFFER_SND_TARGET(txdesc, value)                           \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x2C, 16, 8, value)
#define GET_TX_DESC_BUFFER_SND_TARGET(txdesc)                                  \
	LE_BITS_TO_4BYTE(txdesc + 0x2C, 16, 8)
#define SET_TX_DESC_BUFFER_DATA_LDPC(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x2C, 15, 1, value)
#define GET_TX_DESC_BUFFER_DATA_LDPC(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x2C, 15, 1)
#define SET_TX_DESC_BUFFER_LSIG_TXOP_EN(txdesc, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x2C, 14, 1, value)
#define GET_TX_DESC_BUFFER_LSIG_TXOP_EN(txdesc)                                \
	LE_BITS_TO_4BYTE(txdesc + 0x2C, 14, 1)
#define SET_TX_DESC_BUFFER_G_ID(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x2C, 8, 6, value)
#define GET_TX_DESC_BUFFER_G_ID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x2C, 8, 6)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC(txdesc, value)                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x2C, 4, 4, value)
#define GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC(txdesc)                         \
	LE_BITS_TO_4BYTE(txdesc + 0x2C, 4, 4)
#define SET_TX_DESC_BUFFER_DATA_SC(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x2C, 0, 4, value)
#define GET_TX_DESC_BUFFER_DATA_SC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x2C, 0, 4)

/*TXDESC_WORD12*/

#define SET_TX_DESC_BUFFER_LEN1_L(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x30, 17, 7, value)
#define GET_TX_DESC_BUFFER_LEN1_L(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x30, 17, 7)
#define SET_TX_DESC_BUFFER_LEN0(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x30, 4, 13, value)
#define GET_TX_DESC_BUFFER_LEN0(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x30, 4, 13)
#define SET_TX_DESC_BUFFER_PKT_NUM(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x30, 0, 4, value)
#define GET_TX_DESC_BUFFER_PKT_NUM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x30, 0, 4)

/*TXDESC_WORD13*/

#define SET_TX_DESC_BUFFER_LEN3(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x34, 19, 13, value)
#define GET_TX_DESC_BUFFER_LEN3(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x34, 19, 13)
#define SET_TX_DESC_BUFFER_LEN2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x34, 6, 13, value)
#define GET_TX_DESC_BUFFER_LEN2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x34, 6, 13)
#define SET_TX_DESC_BUFFER_LEN1_H(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x34, 0, 6, value)
#define GET_TX_DESC_BUFFER_LEN1_H(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x34, 0, 6)

#endif

#endif

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

#ifndef _HALMAC_TX_DESC_NIC_H_
#define _HALMAC_TX_DESC_NIC_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 31, 1, value)
#define GET_TX_DESC_DISQSELSEQ(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 31, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_IE_END_BODY(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 31, 1, value)
#define GET_TX_DESC_IE_END_BODY(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 31, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_GF(txdesc, value)                                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 30, 1, value)
#define GET_TX_DESC_GF(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 30, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_AGG_EN_V1(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 30, 1, value)
#define GET_TX_DESC_AGG_EN_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 30, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_NO_ACM(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 29, 1, value)
#define GET_TX_DESC_NO_ACM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 29, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_BK_V1(txdesc, value)                                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 29, 1, value)
#define GET_TX_DESC_BK_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 29, 1)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT ||   \
     HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT ||   \
     HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 28, 1, value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x00, 28, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_AMSDU_PAD_EN(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 27, 1, value)
#define GET_TX_DESC_AMSDU_PAD_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 27, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_LS(txdesc, value)                                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 26, 1, value)
#define GET_TX_DESC_LS(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 26, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_HTC(txdesc, value)                                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 25, 1, value)
#define GET_TX_DESC_HTC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 25, 1)
#define SET_TX_DESC_BMC(txdesc, value)                                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 24, 1, value)
#define GET_TX_DESC_BMC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 24, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_PKT_OFFSET_V1(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 24, 5, value)
#define GET_TX_DESC_PKT_OFFSET_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 24, 5)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT ||   \
     HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_OFFSET(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 16, 8, value)
#define GET_TX_DESC_OFFSET(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 16, 8)
#define SET_TX_DESC_TXPKTSIZE(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x00, 0, 16, value)
#define GET_TX_DESC_TXPKTSIZE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x00, 0, 16)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

/*WORD1*/

#define SET_TX_DESC_HW_AES_IV_V2(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 31, 1, value)
#define GET_TX_DESC_HW_AES_IV_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 31, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_AMSDU(txdesc, value)                                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 30, 1, value)
#define GET_TX_DESC_AMSDU(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 30, 1)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_FTM_EN_V1(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 30, 1, value)
#define GET_TX_DESC_FTM_EN_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 30, 1)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_KEYID_SEL(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 30, 1, value)
#define GET_TX_DESC_KEYID_SEL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 30, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_MOREDATA(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 29, 1, value)
#define GET_TX_DESC_MOREDATA(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 29, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_HW_AES_IV_V1(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 29, 1, value)
#define GET_TX_DESC_HW_AES_IV_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 29, 1)
#define SET_TX_DESC_MHR_CP(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 25, 1, value)
#define GET_TX_DESC_MHR_CP(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 25, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_PKT_OFFSET(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 24, 5, value)
#define GET_TX_DESC_PKT_OFFSET(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 24, 5)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_SMH_EN_V1(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 24, 1, value)
#define GET_TX_DESC_SMH_EN_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 24, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_SEC_TYPE(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 22, 2, value)
#define GET_TX_DESC_SEC_TYPE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 22, 2)
#define SET_TX_DESC_EN_DESC_ID(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 21, 1, value)
#define GET_TX_DESC_EN_DESC_ID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 21, 1)
#define SET_TX_DESC_RATE_ID(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 16, 5, value)
#define GET_TX_DESC_RATE_ID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 16, 5)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_SMH_CAM(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 16, 8, value)
#define GET_TX_DESC_SMH_CAM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 16, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_PIFS(txdesc, value)                                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 15, 1, value)
#define GET_TX_DESC_PIFS(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 15, 1)
#define SET_TX_DESC_LSIG_TXOP_EN(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 14, 1, value)
#define GET_TX_DESC_LSIG_TXOP_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 14, 1)
#define SET_TX_DESC_RD_NAV_EXT(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 13, 1, value)
#define GET_TX_DESC_RD_NAV_EXT(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 13, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_EXT_EDCA(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 13, 1, value)
#define GET_TX_DESC_EXT_EDCA(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 13, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT ||   \
     HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_QSEL(txdesc, value)                                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 8, 5, value)
#define GET_TX_DESC_QSEL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 8, 5)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_SPECIAL_CW(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 7, 1, value)
#define GET_TX_DESC_SPECIAL_CW(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 7, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_MACID(txdesc, value)                                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 0, 7, value)
#define GET_TX_DESC_MACID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 0, 7)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_MACID_V1(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x04, 0, 7, value)
#define GET_TX_DESC_MACID_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x04, 0, 7)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 31, 1, value)
#define GET_TX_DESC_HW_AES_IV(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 31, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_CHK_EN_V1(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 31, 1, value)
#define GET_TX_DESC_CHK_EN_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 31, 1)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_FTM_EN(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 30, 1, value)
#define GET_TX_DESC_FTM_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 30, 1)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANTCEL_D_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 28, 4, value)
#define GET_TX_DESC_ANTCEL_D_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 28, 4)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_DMA_PRI(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 27, 1, value)
#define GET_TX_DESC_DMA_PRI(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 27, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_G_ID(txdesc, value)                                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 24, 6, value)
#define GET_TX_DESC_G_ID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 24, 6)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_MAX_AMSDU_MODE(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 24, 3, value)
#define GET_TX_DESC_MAX_AMSDU_MODE(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 24, 3)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANTSEL_C_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 24, 4, value)
#define GET_TX_DESC_ANTSEL_C_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 24, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_BT_NULL(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 23, 1, value)
#define GET_TX_DESC_BT_NULL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 23, 1)
#define SET_TX_DESC_AMPDU_DENSITY(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 20, 3, value)
#define GET_TX_DESC_AMPDU_DENSITY(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 20, 3)
#define SET_TX_DESC_SPE_RPT(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 19, 1, value)
#define GET_TX_DESC_SPE_RPT(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 19, 1)
#define SET_TX_DESC_RAW(txdesc, value)                                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 18, 1, value)
#define GET_TX_DESC_RAW(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 18, 1)
#define SET_TX_DESC_MOREFRAG(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 17, 1, value)
#define GET_TX_DESC_MOREFRAG(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 17, 1)
#define SET_TX_DESC_BK(txdesc, value)                                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 16, 1, value)
#define GET_TX_DESC_BK(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 16, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_DMA_TXAGG_NUM_V1(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 16, 8, value)
#define GET_TX_DESC_DMA_TXAGG_NUM_V1(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 16, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_NULL_1(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 15, 1, value)
#define GET_TX_DESC_NULL_1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 15, 1)
#define SET_TX_DESC_NULL_0(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 14, 1, value)
#define GET_TX_DESC_NULL_0(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 14, 1)
#define SET_TX_DESC_RDG_EN(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 13, 1, value)
#define GET_TX_DESC_RDG_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 13, 1)
#define SET_TX_DESC_AGG_EN(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 12, 1, value)
#define GET_TX_DESC_AGG_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 12, 1)
#define SET_TX_DESC_CCA_RTS(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 10, 2, value)
#define GET_TX_DESC_CCA_RTS(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 10, 2)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT ||   \
     HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_TRI_FRAME(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 9, 1, value)
#define GET_TX_DESC_TRI_FRAME(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 9, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_P_AID(txdesc, value)                                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 0, 9, value)
#define GET_TX_DESC_P_AID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x08, 0, 9)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_TXDESC_CHECKSUM_V1(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x08, 0, 16, value)
#define GET_TX_DESC_TXDESC_CHECKSUM_V1(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x08, 0, 16)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 24, 8, value)
#define GET_TX_DESC_AMPDU_MAX_TIME(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x0C, 24, 8)
#define SET_TX_DESC_NDPA(txdesc, value)                                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 22, 2, value)
#define GET_TX_DESC_NDPA(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 22, 2)
#define SET_TX_DESC_MAX_AGG_NUM(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 17, 5, value)
#define GET_TX_DESC_MAX_AGG_NUM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 17, 5)
#define SET_TX_DESC_USE_MAX_TIME_EN(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 16, 1, value)
#define GET_TX_DESC_USE_MAX_TIME_EN(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x0C, 16, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_OFFLOAD_SIZE(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 16, 15, value)
#define GET_TX_DESC_OFFLOAD_SIZE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 16, 15)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_NAVUSEHDR(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 15, 1, value)
#define GET_TX_DESC_NAVUSEHDR(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 15, 1)
#define SET_TX_DESC_CHK_EN(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 14, 1, value)
#define GET_TX_DESC_CHK_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 14, 1)
#define SET_TX_DESC_HW_RTS_EN(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 13, 1, value)
#define GET_TX_DESC_HW_RTS_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 13, 1)
#define SET_TX_DESC_RTSEN(txdesc, value)                                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 12, 1, value)
#define GET_TX_DESC_RTSEN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 12, 1)
#define SET_TX_DESC_CTS2SELF(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 11, 1, value)
#define GET_TX_DESC_CTS2SELF(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 11, 1)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_CHANNEL_DMA(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 11, 5, value)
#define GET_TX_DESC_CHANNEL_DMA(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 11, 5)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_DISDATAFB(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 10, 1, value)
#define GET_TX_DESC_DISDATAFB(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 10, 1)
#define SET_TX_DESC_DISRTSFB(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 9, 1, value)
#define GET_TX_DESC_DISRTSFB(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 9, 1)
#define SET_TX_DESC_USE_RATE(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 8, 1, value)
#define GET_TX_DESC_USE_RATE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 8, 1)
#define SET_TX_DESC_HW_SSN_SEL(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 6, 2, value)
#define GET_TX_DESC_HW_SSN_SEL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 6, 2)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_IE_CNT(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 6, 3, value)
#define GET_TX_DESC_IE_CNT(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 6, 3)
#define SET_TX_DESC_IE_CNT_EN(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 5, 1, value)
#define GET_TX_DESC_IE_CNT_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 5, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_WHEADER_LEN(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 0, 5, value)
#define GET_TX_DESC_WHEADER_LEN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 0, 5)

#endif

#if (HALMAC_8814B_SUPPORT)

#define SET_TX_DESC_WHEADER_LEN_V1(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x0C, 0, 5, value)
#define GET_TX_DESC_WHEADER_LEN_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x0C, 0, 5)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 30, 2, value)
#define GET_TX_DESC_PCTS_MASK_IDX(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x10, 30, 2)
#define SET_TX_DESC_PCTS_EN(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 29, 1, value)
#define GET_TX_DESC_PCTS_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x10, 29, 1)
#define SET_TX_DESC_RTSRATE(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 24, 5, value)
#define GET_TX_DESC_RTSRATE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x10, 24, 5)
#define SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 18, 6, value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 18, 6)
#define SET_TX_DESC_RTY_LMT_EN(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 17, 1, value)
#define GET_TX_DESC_RTY_LMT_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x10, 17, 1)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 13, 4, value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc)                                \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 13, 4)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc, value)                        \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 8, 5, value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc)                               \
	LE_BITS_TO_4BYTE(txdesc + 0x10, 8, 5)
#define SET_TX_DESC_TRY_RATE(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 7, 1, value)
#define GET_TX_DESC_TRY_RATE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x10, 7, 1)
#define SET_TX_DESC_DATARATE(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x10, 0, 7, value)
#define GET_TX_DESC_DATARATE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x10, 0, 7)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 31, 1, value)
#define GET_TX_DESC_POLLUTED(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 31, 1)

#endif

#if (HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_ANTSEL_EN_V1(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 30, 1, value)
#define GET_TX_DESC_ANTSEL_EN_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 30, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_TXPWR_OFSET(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 28, 3, value)
#define GET_TX_DESC_TXPWR_OFSET(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 28, 3)

#endif

#if (HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_TXPWR_OFSET_TYPE(txdesc, value)                            \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 28, 2, value)
#define GET_TX_DESC_TXPWR_OFSET_TYPE(txdesc)                                   \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 28, 2)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_TXPWR_OFSET_TYPE_V1(txdesc, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 28, 3, value)
#define GET_TX_DESC_TXPWR_OFSET_TYPE_V1(txdesc)                                \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 28, 3)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_TX_ANT(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 24, 4, value)
#define GET_TX_DESC_TX_ANT(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 24, 4)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_DROP_ID(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 24, 2, value)
#define GET_TX_DESC_DROP_ID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 24, 2)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_DROP_ID_V1(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 22, 2, value)
#define GET_TX_DESC_DROP_ID_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 22, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_PORT_ID(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 21, 3, value)
#define GET_TX_DESC_PORT_ID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 21, 3)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_PORT_ID_V1(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 21, 1, value)
#define GET_TX_DESC_PORT_ID_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 21, 1)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8821C_SUPPORT ||   \
     HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT ||   \
     HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_MULTIPLE_PORT(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 18, 3, value)
#define GET_TX_DESC_MULTIPLE_PORT(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 18, 3)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_SIGNALING_TAPKT_EN(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 17, 1, value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 17, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8197F_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_RTS_SC(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 13, 4, value)
#define GET_TX_DESC_RTS_SC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 13, 4)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 13, 4, value)
#define GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc)                                \
	LE_BITS_TO_4BYTE(txdesc + 0x14, 13, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_RTS_SHORT(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 12, 1, value)
#define GET_TX_DESC_RTS_SHORT(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 12, 1)
#define SET_TX_DESC_VCS_STBC(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 10, 2, value)
#define GET_TX_DESC_VCS_STBC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 10, 2)
#define SET_TX_DESC_DATA_STBC(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 8, 2, value)
#define GET_TX_DESC_DATA_STBC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 8, 2)
#define SET_TX_DESC_DATA_LDPC(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 7, 1, value)
#define GET_TX_DESC_DATA_LDPC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 7, 1)
#define SET_TX_DESC_DATA_BW(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 5, 2, value)
#define GET_TX_DESC_DATA_BW(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 5, 2)
#define SET_TX_DESC_DATA_SHORT(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 4, 1, value)
#define GET_TX_DESC_DATA_SHORT(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 4, 1)
#define SET_TX_DESC_DATA_SC(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x14, 0, 4, value)
#define GET_TX_DESC_DATA_SC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x14, 0, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 30, 2, value)
#define GET_TX_DESC_ANTSEL_D(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 30, 2)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANT_MAPD_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 30, 2, value)
#define GET_TX_DESC_ANT_MAPD_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 30, 2)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANT_MAPC_V2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 30, 2, value)
#define GET_TX_DESC_ANT_MAPC_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 30, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_ANT_MAPD(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 28, 2, value)
#define GET_TX_DESC_ANT_MAPD(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 28, 2)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANT_MAPC_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 28, 2, value)
#define GET_TX_DESC_ANT_MAPC_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 28, 2)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANT_MAPB_V2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 28, 2, value)
#define GET_TX_DESC_ANT_MAPB_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 28, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_ANT_MAPC(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 26, 2, value)
#define GET_TX_DESC_ANT_MAPC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 26, 2)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANT_MAPB_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 26, 2, value)
#define GET_TX_DESC_ANT_MAPB_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 26, 2)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANT_MAPA_V2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 26, 2, value)
#define GET_TX_DESC_ANT_MAPA_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 26, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_ANT_MAPB(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 24, 2, value)
#define GET_TX_DESC_ANT_MAPB(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 24, 2)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANT_MAPA_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 24, 2, value)
#define GET_TX_DESC_ANT_MAPA_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 24, 2)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANTSEL_D_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 24, 2, value)
#define GET_TX_DESC_ANTSEL_D_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 24, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_ANT_MAPA(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 22, 2, value)
#define GET_TX_DESC_ANT_MAPA(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 22, 2)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANTSEL_C_V2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 22, 2, value)
#define GET_TX_DESC_ANTSEL_C_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 22, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_ANTSEL_C(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 20, 2, value)
#define GET_TX_DESC_ANTSEL_C(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 20, 2)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANTSEL_B_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 20, 4, value)
#define GET_TX_DESC_ANTSEL_B_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 20, 4)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANTSEL_B_V2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 19, 3, value)
#define GET_TX_DESC_ANTSEL_B_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 19, 3)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_ANTSEL_B(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 18, 2, value)
#define GET_TX_DESC_ANTSEL_B(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 18, 2)
#define SET_TX_DESC_ANTSEL_A(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 16, 2, value)
#define GET_TX_DESC_ANTSEL_A(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 16, 2)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANTSEL_A_V1(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 16, 4, value)
#define GET_TX_DESC_ANTSEL_A_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 16, 4)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANTSEL_A_V2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 16, 3, value)
#define GET_TX_DESC_ANTSEL_A_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 16, 3)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_MBSSID(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 12, 4, value)
#define GET_TX_DESC_MBSSID(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 12, 4)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_SW_DEFINE(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 0, 12, value)
#define GET_TX_DESC_SW_DEFINE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 0, 12)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_SWPS_SEQ(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x18, 0, 12, value)
#define GET_TX_DESC_SWPS_SEQ(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x18, 0, 12)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 24, 8, value)
#define GET_TX_DESC_DMA_TXAGG_NUM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 24, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_FINAL_DATA_RATE(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 24, 8, value)
#define GET_TX_DESC_FINAL_DATA_RATE(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x1C, 24, 8)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_ANT_MAPD_V2(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 22, 2, value)
#define GET_TX_DESC_ANT_MAPD_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 22, 2)
#define SET_TX_DESC_ANTSEL_EN_V2(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 21, 1, value)
#define GET_TX_DESC_ANTSEL_EN_V2(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 21, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_NTX_MAP(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 20, 4, value)
#define GET_TX_DESC_NTX_MAP(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 20, 4)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_ANTSEL_EN(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 19, 1, value)
#define GET_TX_DESC_ANTSEL_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 19, 1)
#define SET_TX_DESC_MBSSID_EX(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 16, 3, value)
#define GET_TX_DESC_MBSSID_EX(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 16, 3)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_MBSSID_EX_V1(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 16, 1, value)
#define GET_TX_DESC_MBSSID_EX_V1(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 16, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_TX_BUFF_SIZE(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 0, 16, value)
#define GET_TX_DESC_TX_BUFF_SIZE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 0, 16)
#define SET_TX_DESC_TXDESC_CHECKSUM(txdesc, value)                             \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 0, 16, value)
#define GET_TX_DESC_TXDESC_CHECKSUM(txdesc)                                    \
	LE_BITS_TO_4BYTE(txdesc + 0x1C, 0, 16)
#define SET_TX_DESC_TIMESTAMP(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x1C, 0, 16, value)
#define GET_TX_DESC_TIMESTAMP(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x1C, 0, 16)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 31, 1, value)
#define GET_TX_DESC_TXWIFI_CP(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 31, 1)
#define SET_TX_DESC_MAC_CP(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 30, 1, value)
#define GET_TX_DESC_MAC_CP(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 30, 1)
#define SET_TX_DESC_STW_PKTRE_DIS(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 29, 1, value)
#define GET_TX_DESC_STW_PKTRE_DIS(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 29, 1)
#define SET_TX_DESC_STW_RB_DIS(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 28, 1, value)
#define GET_TX_DESC_STW_RB_DIS(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 28, 1)
#define SET_TX_DESC_STW_RATE_DIS(txdesc, value)                                \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 27, 1, value)
#define GET_TX_DESC_STW_RATE_DIS(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 27, 1)
#define SET_TX_DESC_STW_ANT_DIS(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 26, 1, value)
#define GET_TX_DESC_STW_ANT_DIS(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 26, 1)
#define SET_TX_DESC_STW_EN(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 25, 1, value)
#define GET_TX_DESC_STW_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 25, 1)
#define SET_TX_DESC_SMH_EN(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 24, 1, value)
#define GET_TX_DESC_SMH_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 24, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_TAILPAGE_L(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 24, 8, value)
#define GET_TX_DESC_TAILPAGE_L(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 24, 8)
#define SET_TX_DESC_SDIO_DMASEQ(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 16, 8, value)
#define GET_TX_DESC_SDIO_DMASEQ(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 16, 8)
#define SET_TX_DESC_NEXTHEADPAGE_L(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 16, 8, value)
#define GET_TX_DESC_NEXTHEADPAGE_L(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x20, 16, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_EN_HWSEQ(txdesc, value)                                    \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 15, 1, value)
#define GET_TX_DESC_EN_HWSEQ(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 15, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT)

#define SET_TX_DESC_EN_HWEXSEQ(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 14, 1, value)
#define GET_TX_DESC_EN_HWEXSEQ(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 14, 1)

#endif

#if (HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_EN_HWSEQ_MODE(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 14, 2, value)
#define GET_TX_DESC_EN_HWSEQ_MODE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 14, 2)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_DATA_RC(txdesc, value)                                     \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 8, 6, value)
#define GET_TX_DESC_DATA_RC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 8, 6)
#define SET_TX_DESC_BAR_RTY_TH(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 6, 2, value)
#define GET_TX_DESC_BAR_RTY_TH(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 6, 2)
#define SET_TX_DESC_RTS_RC(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x20, 0, 6, value)
#define GET_TX_DESC_RTS_RC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x20, 0, 6)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 28, 4, value)
#define GET_TX_DESC_TAILPAGE_H(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x24, 28, 4)
#define SET_TX_DESC_NEXTHEADPAGE_H(txdesc, value)                              \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 24, 4, value)
#define GET_TX_DESC_NEXTHEADPAGE_H(txdesc)                                     \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 24, 4)

#endif

#if (HALMAC_8192F_SUPPORT)

#define SET_TX_DESC_FINAL_DATA_RATE_V1(txdesc, value)                          \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 24, 8, value)
#define GET_TX_DESC_FINAL_DATA_RATE_V1(txdesc)                                 \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 24, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_SW_SEQ(txdesc, value)                                      \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 12, 12, value)
#define GET_TX_DESC_SW_SEQ(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x24, 12, 12)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_TXBF_PATH(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 11, 1, value)
#define GET_TX_DESC_TXBF_PATH(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x24, 11, 1)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_PADDING_LEN(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 0, 11, value)
#define GET_TX_DESC_PADDING_LEN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x24, 0, 11)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc, value)                         \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x24, 0, 8, value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc)                                \
	LE_BITS_TO_4BYTE(txdesc + 0x24, 0, 8)

#endif

#if (HALMAC_8812F_SUPPORT)

/*WORD10*/

#define SET_TX_DESC_HT_DATA_SND(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 31, 1, value)
#define GET_TX_DESC_HT_DATA_SND(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 31, 1)

#endif

#if (HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_SHCUT_CAM(txdesc, value)                                   \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 16, 6, value)
#define GET_TX_DESC_SHCUT_CAM(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 16, 6)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_MU_DATARATE(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 8, 8, value)
#define GET_TX_DESC_MU_DATARATE(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 8, 8)
#define SET_TX_DESC_MU_RC(txdesc, value)                                       \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 4, 4, value)
#define GET_TX_DESC_MU_RC(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 4, 4)

#endif

#if (HALMAC_8812F_SUPPORT)

#define SET_TX_DESC_NDPA_RATE_SEL(txdesc, value)                               \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 3, 1, value)
#define GET_TX_DESC_NDPA_RATE_SEL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 3, 1)
#define SET_TX_DESC_HW_NDPA_EN(txdesc, value)                                  \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 2, 1, value)
#define GET_TX_DESC_HW_NDPA_EN(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 2, 1)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8812F_SUPPORT || HALMAC_8197G_SUPPORT)

#define SET_TX_DESC_SND_PKT_SEL(txdesc, value)                                 \
	SET_BITS_TO_LE_4BYTE(txdesc + 0x28, 0, 2, value)
#define GET_TX_DESC_SND_PKT_SEL(txdesc) LE_BITS_TO_4BYTE(txdesc + 0x28, 0, 2)

#endif

#endif

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

#ifndef _HALMAC_RX_DESC_AP_H_
#define _HALMAC_RX_DESC_AP_H_
#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

/*RXDESC_WORD0*/

#define GET_RX_DESC_EOR(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      30)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_PHYPKTIDC(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      28)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_EVT_PKT(rxdesc)                                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      28)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_SWDEC(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      27)
#define GET_RX_DESC_PHYST(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      26)
#define GET_RX_DESC_SHIFT(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x3,  \
			      24)
#define GET_RX_DESC_QOS(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      23)
#define GET_RX_DESC_SECURITY(rxdesc)                                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x7,  \
			      20)
#define GET_RX_DESC_DRV_INFO_SIZE(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0xf,  \
			      16)
#define GET_RX_DESC_ICV_ERR(rxdesc)                                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      15)
#define GET_RX_DESC_CRC32(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0, 0x1,  \
			      14)
#define GET_RX_DESC_PKT_LEN(rxdesc)                                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword0,       \
			      0x3fff, 0)

/*RXDESC_WORD1*/

#define GET_RX_DESC_BC(rxdesc)                                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      31)
#define GET_RX_DESC_MC(rxdesc)                                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      30)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_TY_PE(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x3,  \
			      28)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_TYPE(rxdesc)                                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x3,  \
			      28)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_MF(rxdesc)                                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      27)
#define GET_RX_DESC_MD(rxdesc)                                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      26)
#define GET_RX_DESC_PWR(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      25)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_PAM(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      24)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_A1_MATCH(rxdesc)                                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      24)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_CHK_VLD(rxdesc)                                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      23)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_TCP_CHKSUM_VLD(rxdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      23)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RX_IS_TCP_UDP(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      22)
#define GET_RX_DESC_RX_IPV(rxdesc)                                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      21)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_CHKERR(rxdesc)                                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      20)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_TCP_CHKSUM_ERR(rxdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      20)
#define GET_RX_DESC_PHY_PKT_IDC(rxdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      17)
#define GET_RX_DESC_FW_FIFO_FULL(rxdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      16)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_PAGGR(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      15)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_AMPDU(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      15)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RXID_MATCH(rxdesc)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      14)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_RXCMD_IDC(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      14)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_AMSDU(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      13)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_MACID_VLD(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x1,  \
			      12)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_TID(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0xf, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_MACID(rxdesc)                                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword1, 0x7f, \
			      0)

/*RXDESC_WORD2*/

#define GET_RX_DESC_FCS_OK(rxdesc)                                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      31)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_AMSDU_CUT(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      31)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT ||   \
     HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT ||   \
     HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_PPDU_CNT(rxdesc)                                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x3,  \
			      29)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_C2H(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      28)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT)

#define GET_RX_DESC_HWRSVD_V1(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x7,  \
			      25)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_HWRSVD(rxdesc)                                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0xf,  \
			      24)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8192F_SUPPORT)

#define GET_RX_DESC_RXMAGPKT(rxdesc)                                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      24)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_WLANHD_IV_LEN(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x3f, \
			      18)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_LAST_MSDU(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      17)

#endif

#if (HALMAC_8822C_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RX_STATISTICS(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      17)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RX_IS_QOS(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      16)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_EXT_SEC_TYPE(rxdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0x1,  \
			      16)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_FRAG(rxdesc)                                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2, 0xf,  \
			      12)
#define GET_RX_DESC_SEQ(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword2,       \
			      0xfff, 0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT)

/*RXDESC_WORD3*/

#define GET_RX_DESC_MAGIC_WAKE(rxdesc)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1,  \
			      31)
#define GET_RX_DESC_UNICAST_WAKE(rxdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1,  \
			      30)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_PATTERN_MATCH(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1,  \
			      29)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_PATTERN_WAKE(rxdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1,  \
			      29)

#endif

#if (HALMAC_8822B_SUPPORT || HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT ||   \
     HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RXPAYLOAD_MATCH(rxdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1,  \
			      28)
#define GET_RX_DESC_RXPAYLOAD_ID(rxdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0xf,  \
			      24)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_DMA_AGG_NUM(rxdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0xff, \
			      16)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_BSSID_FIT_1_0(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x3,  \
			      12)
#define GET_RX_DESC_EOSP(rxdesc)                                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1,  \
			      11)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_BSSID_FIT(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1f, \
			      11)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_HTC(rxdesc)                                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1,  \
			      10)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_AMPDU_END_PKT(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1, 9)
#define GET_RX_DESC_ADDRESS_CAM_VLD(rxdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1, 8)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_BSSID_FIT_4_2(rxdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x7, 7)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_EOSP_V1(rxdesc)                                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x1, 7)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RX_RATE(rxdesc)                                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword3, 0x7f, \
			      0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT ||   \
     HALMAC_8812F_SUPPORT)

/*RXDESC_WORD4*/

#define GET_RX_DESC_A1_FIT(rxdesc)                                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1f, \
			      24)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_ADDRESS_CAM(rxdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0xff, \
			      24)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define GET_RX_DESC_A1_FIT_A1(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x7f, \
			      24)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_MACID_VLD_V1(rxdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1,  \
			      23)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_MACID_RPT_BUFF(rxdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x7f, \
			      17)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RX_PRE_NDP_VLD(rxdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1,  \
			      16)

#endif

#if (HALMAC_8192F_SUPPORT)

#define GET_RX_DESC_SWPS_RPT_V1(rxdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1,  \
			      16)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_MACID_V1(rxdesc)                                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0xff, \
			      15)

#endif

#if (HALMAC_8192F_SUPPORT)

#define GET_RX_DESC_FC_POWER_V1(rxdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1,  \
			      15)
#define GET_RX_DESC_TXRPTMID_CTL_MASK_V1(rxdesc)                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1,  \
			      14)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_RX_SCRAMBLER(rxdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x7f, \
			      9)
#define GET_RX_DESC_RX_EOF(rxdesc)                                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1, 8)

#endif

#if (HALMAC_8192F_SUPPORT)

#define GET_RX_DESC_SNIF_INFO(rxdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x3f, \
			      8)

#endif

#if (HALMAC_8197F_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define GET_RX_DESC_FC_POWER(rxdesc)                                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1, 7)

#endif

#if (HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define GET_RX_DESC_TXRPTMID_CTL_MASK(rxdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1, 6)

#endif

#if (HALMAC_8814B_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8197G_SUPPORT)

#define GET_RX_DESC_SWPS_RPT(rxdesc)                                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1, 5)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8821C_SUPPORT ||   \
     HALMAC_8822C_SUPPORT || HALMAC_8192F_SUPPORT || HALMAC_8812F_SUPPORT)

#define GET_RX_DESC_PATTERN_IDX(rxdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0xff, \
			      0)

#endif

#if (HALMAC_8197F_SUPPORT || HALMAC_8198F_SUPPORT)

#define GET_RX_DESC_PATTERN_IDX_V1(rxdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1f, \
			      0)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_PATTERN_IDX_V2(rxdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword4, 0x1f, \
			      0)

#endif

#if (HALMAC_8814A_SUPPORT || HALMAC_8822B_SUPPORT || HALMAC_8197F_SUPPORT ||   \
     HALMAC_8821C_SUPPORT || HALMAC_8198F_SUPPORT || HALMAC_8822C_SUPPORT ||   \
     HALMAC_8192F_SUPPORT || HALMAC_8197G_SUPPORT || HALMAC_8812F_SUPPORT)

/*RXDESC_WORD5*/

#define GET_RX_DESC_TSFL(rxdesc)                                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword5,       \
			      0xffffffff, 0)

#endif

#if (HALMAC_8814B_SUPPORT)

#define GET_RX_DESC_FREERUN_CNT(rxdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_rx_desc *)rxdesc)->dword5,       \
			      0xffffffff, 0)

#endif

#endif

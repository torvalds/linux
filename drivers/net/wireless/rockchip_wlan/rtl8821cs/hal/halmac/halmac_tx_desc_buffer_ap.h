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

#ifndef _HALMAC_TX_DESC_BUFFER_AP_H_
#define _HALMAC_TX_DESC_BUFFER_AP_H_
#if (HALMAC_8814B_SUPPORT)

/*TXDESC_WORD0*/

#define SET_TX_DESC_BUFFER_RDG_EN(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword0,   \
				  value, 0x1, 31)
#define SET_TX_DESC_BUFFER_RDG_EN_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword0, value, 0x1, 31)
#define GET_TX_DESC_BUFFER_RDG_EN(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword0, 0x1,  \
			      31)
#define SET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword0,   \
				  value, 0x1, 30)
#define SET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL_NO_CLR(txdesc, value)               \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword0, value, 0x1, 30)
#define GET_TX_DESC_BUFFER_BCNPKT_TSF_CTRL(txdesc)                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword0, 0x1,  \
			      30)
#define SET_TX_DESC_BUFFER_AGG_EN(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword0,   \
				  value, 0x1, 29)
#define SET_TX_DESC_BUFFER_AGG_EN_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword0, value, 0x1, 29)
#define GET_TX_DESC_BUFFER_AGG_EN(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword0, 0x1,  \
			      29)
#define SET_TX_DESC_BUFFER_PKT_OFFSET(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword0,   \
				  value, 0x1f, 24)
#define SET_TX_DESC_BUFFER_PKT_OFFSET_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword0, value, 0x1f, 24)
#define GET_TX_DESC_BUFFER_PKT_OFFSET(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword0, 0x1f, \
			      24)
#define SET_TX_DESC_BUFFER_OFFSET(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword0,   \
				  value, 0xff, 16)
#define SET_TX_DESC_BUFFER_OFFSET_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword0, value, 0xff, 16)
#define GET_TX_DESC_BUFFER_OFFSET(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword0, 0xff, \
			      16)
#define SET_TX_DESC_BUFFER_TXPKTSIZE(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword0,   \
				  value, 0xffff, 0)
#define SET_TX_DESC_BUFFER_TXPKTSIZE_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword0, value, 0xffff, 0)
#define GET_TX_DESC_BUFFER_TXPKTSIZE(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword0,       \
			      0xffff, 0)

/*TXDESC_WORD1*/

#define SET_TX_DESC_BUFFER_USERATE(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0x1, 31)
#define SET_TX_DESC_BUFFER_USERATE_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0x1, 31)
#define GET_TX_DESC_BUFFER_USERATE(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0x1,  \
			      31)
#define SET_TX_DESC_BUFFER_AMSDU(txdesc, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0x1, 30)
#define SET_TX_DESC_BUFFER_AMSDU_NO_CLR(txdesc, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0x1, 30)
#define GET_TX_DESC_BUFFER_AMSDU(txdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0x1,  \
			      30)
#define SET_TX_DESC_BUFFER_EN_HWSEQ(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0x1, 29)
#define SET_TX_DESC_BUFFER_EN_HWSEQ_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0x1, 29)
#define GET_TX_DESC_BUFFER_EN_HWSEQ(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0x1,  \
			      29)
#define SET_TX_DESC_BUFFER_EN_HWEXSEQ(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0x1, 28)
#define SET_TX_DESC_BUFFER_EN_HWEXSEQ_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0x1, 28)
#define GET_TX_DESC_BUFFER_EN_HWEXSEQ(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0x1,  \
			      28)
#define SET_TX_DESC_BUFFER_SW_SEQ(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0xfff, 16)
#define SET_TX_DESC_BUFFER_SW_SEQ_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0xfff, 16)
#define GET_TX_DESC_BUFFER_SW_SEQ(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1,       \
			      0xfff, 16)
#define SET_TX_DESC_BUFFER_DROP_ID(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0x3, 14)
#define SET_TX_DESC_BUFFER_DROP_ID_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0x3, 14)
#define GET_TX_DESC_BUFFER_DROP_ID(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0x3,  \
			      14)
#define SET_TX_DESC_BUFFER_MOREDATA(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0x1, 13)
#define SET_TX_DESC_BUFFER_MOREDATA_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0x1, 13)
#define GET_TX_DESC_BUFFER_MOREDATA(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0x1,  \
			      13)
#define SET_TX_DESC_BUFFER_QSEL(txdesc, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0x1f, 8)
#define SET_TX_DESC_BUFFER_QSEL_NO_CLR(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0x1f, 8)
#define GET_TX_DESC_BUFFER_QSEL(txdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0x1f, \
			      8)
#define SET_TX_DESC_BUFFER_MACID(txdesc, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword1,   \
				  value, 0xff, 0)
#define SET_TX_DESC_BUFFER_MACID_NO_CLR(txdesc, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword1, value, 0xff, 0)
#define GET_TX_DESC_BUFFER_MACID(txdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword1, 0xff, \
			      0)

/*TXDESC_WORD2*/

#define SET_TX_DESC_BUFFER_CHK_EN(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword2,   \
				  value, 0x1, 31)
#define SET_TX_DESC_BUFFER_CHK_EN_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword2, value, 0x1, 31)
#define GET_TX_DESC_BUFFER_CHK_EN(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword2, 0x1,  \
			      31)
#define SET_TX_DESC_BUFFER_DISQSELSEQ(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword2,   \
				  value, 0x1, 30)
#define SET_TX_DESC_BUFFER_DISQSELSEQ_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword2, value, 0x1, 30)
#define GET_TX_DESC_BUFFER_DISQSELSEQ(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword2, 0x1,  \
			      30)
#define SET_TX_DESC_BUFFER_SND_PKT_SEL(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword2,   \
				  value, 0x3, 28)
#define SET_TX_DESC_BUFFER_SND_PKT_SEL_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword2, value, 0x3, 28)
#define GET_TX_DESC_BUFFER_SND_PKT_SEL(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword2, 0x3,  \
			      28)
#define SET_TX_DESC_BUFFER_DMA_PRI(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword2,   \
				  value, 0x1, 27)
#define SET_TX_DESC_BUFFER_DMA_PRI_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword2, value, 0x1, 27)
#define GET_TX_DESC_BUFFER_DMA_PRI(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword2, 0x1,  \
			      27)
#define SET_TX_DESC_BUFFER_MAX_AMSDU_MODE(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword2,   \
				  value, 0x7, 24)
#define SET_TX_DESC_BUFFER_MAX_AMSDU_MODE_NO_CLR(txdesc, value)                \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword2, value, 0x7, 24)
#define GET_TX_DESC_BUFFER_MAX_AMSDU_MODE(txdesc)                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword2, 0x7,  \
			      24)
#define SET_TX_DESC_BUFFER_DMA_TXAGG_NUM(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword2,   \
				  value, 0xff, 16)
#define SET_TX_DESC_BUFFER_DMA_TXAGG_NUM_NO_CLR(txdesc, value)                 \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword2, value, 0xff, 16)
#define GET_TX_DESC_BUFFER_DMA_TXAGG_NUM(txdesc)                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword2, 0xff, \
			      16)
#define SET_TX_DESC_BUFFER_TXDESC_CHECKSUM(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword2,   \
				  value, 0xffff, 0)
#define SET_TX_DESC_BUFFER_TXDESC_CHECKSUM_NO_CLR(txdesc, value)               \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword2, value, 0xffff, 0)
#define GET_TX_DESC_BUFFER_TXDESC_CHECKSUM(txdesc)                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword2,       \
			      0xffff, 0)

/*TXDESC_WORD3*/

#define SET_TX_DESC_BUFFER_OFFLOAD_SIZE(txdesc, value)                         \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword3,   \
				  value, 0x7fff, 16)
#define SET_TX_DESC_BUFFER_OFFLOAD_SIZE_NO_CLR(txdesc, value)                  \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword3, value, 0x7fff, 16)
#define GET_TX_DESC_BUFFER_OFFLOAD_SIZE(txdesc)                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword3,       \
			      0x7fff, 16)
#define SET_TX_DESC_BUFFER_CHANNEL_DMA(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword3,   \
				  value, 0x1f, 11)
#define SET_TX_DESC_BUFFER_CHANNEL_DMA_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword3, value, 0x1f, 11)
#define GET_TX_DESC_BUFFER_CHANNEL_DMA(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword3, 0x1f, \
			      11)
#define SET_TX_DESC_BUFFER_MBSSID(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword3,   \
				  value, 0xf, 7)
#define SET_TX_DESC_BUFFER_MBSSID_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword3, value, 0xf, 7)
#define GET_TX_DESC_BUFFER_MBSSID(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword3, 0xf, 7)
#define SET_TX_DESC_BUFFER_BK(txdesc, value)                                   \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword3,   \
				  value, 0x1, 6)
#define SET_TX_DESC_BUFFER_BK_NO_CLR(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword3, value, 0x1, 6)
#define GET_TX_DESC_BUFFER_BK(txdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword3, 0x1, 6)
#define SET_TX_DESC_BUFFER_WHEADER_LEN(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword3,   \
				  value, 0x1f, 0)
#define SET_TX_DESC_BUFFER_WHEADER_LEN_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword3, value, 0x1f, 0)
#define GET_TX_DESC_BUFFER_WHEADER_LEN(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword3, 0x1f, \
			      0)

/*TXDESC_WORD4*/

#define SET_TX_DESC_BUFFER_TRY_RATE(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword4,   \
				  value, 0x1, 26)
#define SET_TX_DESC_BUFFER_TRY_RATE_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword4, value, 0x1, 26)
#define GET_TX_DESC_BUFFER_TRY_RATE(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword4, 0x1,  \
			      26)
#define SET_TX_DESC_BUFFER_DATA_BW(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword4,   \
				  value, 0x3, 24)
#define SET_TX_DESC_BUFFER_DATA_BW_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword4, value, 0x3, 24)
#define GET_TX_DESC_BUFFER_DATA_BW(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword4, 0x3,  \
			      24)
#define SET_TX_DESC_BUFFER_DATA_SHORT(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword4,   \
				  value, 0x1, 23)
#define SET_TX_DESC_BUFFER_DATA_SHORT_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword4, value, 0x1, 23)
#define GET_TX_DESC_BUFFER_DATA_SHORT(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword4, 0x1,  \
			      23)
#define SET_TX_DESC_BUFFER_DATARATE(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword4,   \
				  value, 0x7f, 16)
#define SET_TX_DESC_BUFFER_DATARATE_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword4, value, 0x7f, 16)
#define GET_TX_DESC_BUFFER_DATARATE(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword4, 0x7f, \
			      16)
#define SET_TX_DESC_BUFFER_TXBF_PATH(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword4,   \
				  value, 0x1, 11)
#define SET_TX_DESC_BUFFER_TXBF_PATH_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword4, value, 0x1, 11)
#define GET_TX_DESC_BUFFER_TXBF_PATH(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword4, 0x1,  \
			      11)
#define SET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET(txdesc, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword4,   \
				  value, 0x7ff, 0)
#define SET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET_NO_CLR(txdesc, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword4, value, 0x7ff, 0)
#define GET_TX_DESC_BUFFER_GROUP_BIT_IE_OFFSET(txdesc)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword4,       \
			      0x7ff, 0)

/*TXDESC_WORD5*/

#define SET_TX_DESC_BUFFER_RTY_LMT_EN(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 31)
#define SET_TX_DESC_BUFFER_RTY_LMT_EN_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 31)
#define GET_TX_DESC_BUFFER_RTY_LMT_EN(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1,  \
			      31)
#define SET_TX_DESC_BUFFER_HW_RTS_EN(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 30)
#define SET_TX_DESC_BUFFER_HW_RTS_EN_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 30)
#define GET_TX_DESC_BUFFER_HW_RTS_EN(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1,  \
			      30)
#define SET_TX_DESC_BUFFER_RTS_EN(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 29)
#define SET_TX_DESC_BUFFER_RTS_EN_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 29)
#define GET_TX_DESC_BUFFER_RTS_EN(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1,  \
			      29)
#define SET_TX_DESC_BUFFER_CTS2SELF(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 28)
#define SET_TX_DESC_BUFFER_CTS2SELF_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 28)
#define GET_TX_DESC_BUFFER_CTS2SELF(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1,  \
			      28)
#define SET_TX_DESC_BUFFER_TAILPAGE_H(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0xf, 24)
#define SET_TX_DESC_BUFFER_TAILPAGE_H_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0xf, 24)
#define GET_TX_DESC_BUFFER_TAILPAGE_H(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0xf,  \
			      24)
#define SET_TX_DESC_BUFFER_TAILPAGE_L(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0xff, 16)
#define SET_TX_DESC_BUFFER_TAILPAGE_L_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0xff, 16)
#define GET_TX_DESC_BUFFER_TAILPAGE_L(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0xff, \
			      16)
#define SET_TX_DESC_BUFFER_NAVUSEHDR(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 15)
#define SET_TX_DESC_BUFFER_NAVUSEHDR_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 15)
#define GET_TX_DESC_BUFFER_NAVUSEHDR(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1,  \
			      15)
#define SET_TX_DESC_BUFFER_BMC(txdesc, value)                                  \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 14)
#define SET_TX_DESC_BUFFER_BMC_NO_CLR(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 14)
#define GET_TX_DESC_BUFFER_BMC(txdesc)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1,  \
			      14)
#define SET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x3f, 8)
#define SET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT_NO_CLR(txdesc, value)              \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x3f, 8)
#define GET_TX_DESC_BUFFER_RTS_DATA_RTY_LMT(txdesc)                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x3f, \
			      8)
#define SET_TX_DESC_BUFFER_HW_AES_IV(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 7)
#define SET_TX_DESC_BUFFER_HW_AES_IV_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 7)
#define GET_TX_DESC_BUFFER_HW_AES_IV(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1, 7)
#define SET_TX_DESC_BUFFER_BT_NULL(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 3)
#define SET_TX_DESC_BUFFER_BT_NULL_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 3)
#define GET_TX_DESC_BUFFER_BT_NULL(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1, 3)
#define SET_TX_DESC_BUFFER_EN_DESC_ID(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x1, 2)
#define SET_TX_DESC_BUFFER_EN_DESC_ID_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x1, 2)
#define GET_TX_DESC_BUFFER_EN_DESC_ID(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x1, 2)
#define SET_TX_DESC_BUFFER_SECTYPE(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword5,   \
				  value, 0x3, 0)
#define SET_TX_DESC_BUFFER_SECTYPE_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword5, value, 0x3, 0)
#define GET_TX_DESC_BUFFER_SECTYPE(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword5, 0x3, 0)

/*TXDESC_WORD6*/

#define SET_TX_DESC_BUFFER_MULTIPLE_PORT(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x7, 29)
#define SET_TX_DESC_BUFFER_MULTIPLE_PORT_NO_CLR(txdesc, value)                 \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x7, 29)
#define GET_TX_DESC_BUFFER_MULTIPLE_PORT(txdesc)                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x7,  \
			      29)
#define SET_TX_DESC_BUFFER_POLLUTED(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x1, 28)
#define SET_TX_DESC_BUFFER_POLLUTED_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x1, 28)
#define GET_TX_DESC_BUFFER_POLLUTED(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x1,  \
			      28)
#define SET_TX_DESC_BUFFER_NULL_1(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x1, 27)
#define SET_TX_DESC_BUFFER_NULL_1_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x1, 27)
#define GET_TX_DESC_BUFFER_NULL_1(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x1,  \
			      27)
#define SET_TX_DESC_BUFFER_NULL_0(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x1, 26)
#define SET_TX_DESC_BUFFER_NULL_0_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x1, 26)
#define GET_TX_DESC_BUFFER_NULL_0(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x1,  \
			      26)
#define SET_TX_DESC_BUFFER_TRI_FRAME(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x1, 25)
#define SET_TX_DESC_BUFFER_TRI_FRAME_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x1, 25)
#define GET_TX_DESC_BUFFER_TRI_FRAME(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x1,  \
			      25)
#define SET_TX_DESC_BUFFER_SPE_RPT(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x1, 24)
#define SET_TX_DESC_BUFFER_SPE_RPT_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x1, 24)
#define GET_TX_DESC_BUFFER_SPE_RPT(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x1,  \
			      24)
#define SET_TX_DESC_BUFFER_FTM_EN(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x1, 23)
#define SET_TX_DESC_BUFFER_FTM_EN_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x1, 23)
#define GET_TX_DESC_BUFFER_FTM_EN(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x1,  \
			      23)
#define SET_TX_DESC_BUFFER_MU_DATARATE(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x7f, 16)
#define SET_TX_DESC_BUFFER_MU_DATARATE_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x7f, 16)
#define GET_TX_DESC_BUFFER_MU_DATARATE(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x7f, \
			      16)
#define SET_TX_DESC_BUFFER_CCA_RTS(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x3, 14)
#define SET_TX_DESC_BUFFER_CCA_RTS_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x3, 14)
#define GET_TX_DESC_BUFFER_CCA_RTS(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x3,  \
			      14)
#define SET_TX_DESC_BUFFER_NDPA(txdesc, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x3, 12)
#define SET_TX_DESC_BUFFER_NDPA_NO_CLR(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x3, 12)
#define GET_TX_DESC_BUFFER_NDPA(txdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x3,  \
			      12)
#define SET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x3, 9)
#define SET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE_NO_CLR(txdesc, value)              \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x3, 9)
#define GET_TX_DESC_BUFFER_TXPWR_OFSET_TYPE(txdesc)                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6, 0x3, 9)
#define SET_TX_DESC_BUFFER_P_AID(txdesc, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword6,   \
				  value, 0x1ff, 0)
#define SET_TX_DESC_BUFFER_P_AID_NO_CLR(txdesc, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword6, value, 0x1ff, 0)
#define GET_TX_DESC_BUFFER_P_AID(txdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword6,       \
			      0x1ff, 0)

/*TXDESC_WORD7*/

#define SET_TX_DESC_BUFFER_SW_DEFINE(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword7,   \
				  value, 0xfff, 16)
#define SET_TX_DESC_BUFFER_SW_DEFINE_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword7, value, 0xfff, 16)
#define GET_TX_DESC_BUFFER_SW_DEFINE(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword7,       \
			      0xfff, 16)
#define SET_TX_DESC_BUFFER_CTRL_CNT_VALID(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword7,   \
				  value, 0x1, 9)
#define SET_TX_DESC_BUFFER_CTRL_CNT_VALID_NO_CLR(txdesc, value)                \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword7, value, 0x1, 9)
#define GET_TX_DESC_BUFFER_CTRL_CNT_VALID(txdesc)                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword7, 0x1, 9)
#define SET_TX_DESC_BUFFER_CTRL_CNT(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword7,   \
				  value, 0xf, 5)
#define SET_TX_DESC_BUFFER_CTRL_CNT_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword7, value, 0xf, 5)
#define GET_TX_DESC_BUFFER_CTRL_CNT(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword7, 0xf, 5)
#define SET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE(txdesc, value)                 \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword7,   \
				  value, 0x1f, 0)
#define SET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE_NO_CLR(txdesc, value)          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword7, value, 0x1f, 0)
#define GET_TX_DESC_BUFFER_DATA_RTY_LOWEST_RATE(txdesc)                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword7, 0x1f, \
			      0)

/*TXDESC_WORD8*/

#define SET_TX_DESC_BUFFER_PATH_MAPA(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0x3, 30)
#define SET_TX_DESC_BUFFER_PATH_MAPA_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0x3, 30)
#define GET_TX_DESC_BUFFER_PATH_MAPA(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0x3,  \
			      30)
#define SET_TX_DESC_BUFFER_PATH_MAPB(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0x3, 28)
#define SET_TX_DESC_BUFFER_PATH_MAPB_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0x3, 28)
#define GET_TX_DESC_BUFFER_PATH_MAPB(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0x3,  \
			      28)
#define SET_TX_DESC_BUFFER_PATH_MAPC(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0x3, 26)
#define SET_TX_DESC_BUFFER_PATH_MAPC_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0x3, 26)
#define GET_TX_DESC_BUFFER_PATH_MAPC(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0x3,  \
			      26)
#define SET_TX_DESC_BUFFER_PATH_MAPD(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0x3, 24)
#define SET_TX_DESC_BUFFER_PATH_MAPD_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0x3, 24)
#define GET_TX_DESC_BUFFER_PATH_MAPD(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0x3,  \
			      24)
#define SET_TX_DESC_BUFFER_ANTSEL_A(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0xf, 20)
#define SET_TX_DESC_BUFFER_ANTSEL_A_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0xf, 20)
#define GET_TX_DESC_BUFFER_ANTSEL_A(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0xf,  \
			      20)
#define SET_TX_DESC_BUFFER_ANTSEL_B(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0xf, 16)
#define SET_TX_DESC_BUFFER_ANTSEL_B_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0xf, 16)
#define GET_TX_DESC_BUFFER_ANTSEL_B(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0xf,  \
			      16)
#define SET_TX_DESC_BUFFER_ANTSEL_C(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0xf, 12)
#define SET_TX_DESC_BUFFER_ANTSEL_C_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0xf, 12)
#define GET_TX_DESC_BUFFER_ANTSEL_C(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0xf,  \
			      12)
#define SET_TX_DESC_BUFFER_ANTSEL_D(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0xf, 8)
#define SET_TX_DESC_BUFFER_ANTSEL_D_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0xf, 8)
#define GET_TX_DESC_BUFFER_ANTSEL_D(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0xf, 8)
#define SET_TX_DESC_BUFFER_NTX_PATH_EN(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0xf, 4)
#define SET_TX_DESC_BUFFER_NTX_PATH_EN_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0xf, 4)
#define GET_TX_DESC_BUFFER_NTX_PATH_EN(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0xf, 4)
#define SET_TX_DESC_BUFFER_ANTLSEL_EN(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0x1, 3)
#define SET_TX_DESC_BUFFER_ANTLSEL_EN_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0x1, 3)
#define GET_TX_DESC_BUFFER_ANTLSEL_EN(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0x1, 3)
#define SET_TX_DESC_BUFFER_AMPDU_DENSITY(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword8,   \
				  value, 0x7, 0)
#define SET_TX_DESC_BUFFER_AMPDU_DENSITY_NO_CLR(txdesc, value)                 \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword8, value, 0x7, 0)
#define GET_TX_DESC_BUFFER_AMPDU_DENSITY(txdesc)                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword8, 0x7, 0)

/*TXDESC_WORD9*/

#define SET_TX_DESC_BUFFER_VCS_STBC(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x3, 30)
#define SET_TX_DESC_BUFFER_VCS_STBC_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x3, 30)
#define GET_TX_DESC_BUFFER_VCS_STBC(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x3,  \
			      30)
#define SET_TX_DESC_BUFFER_DATA_STBC(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x3, 28)
#define SET_TX_DESC_BUFFER_DATA_STBC_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x3, 28)
#define GET_TX_DESC_BUFFER_DATA_STBC(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x3,  \
			      28)
#define SET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE(txdesc, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0xf, 24)
#define SET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE_NO_CLR(txdesc, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0xf, 24)
#define GET_TX_DESC_BUFFER_RTS_RTY_LOWEST_RATE(txdesc)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0xf,  \
			      24)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN(txdesc, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 23)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN_NO_CLR(txdesc, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 23)
#define GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_EN(txdesc)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1,  \
			      23)
#define SET_TX_DESC_BUFFER_MHR_CP(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 22)
#define SET_TX_DESC_BUFFER_MHR_CP_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 22)
#define GET_TX_DESC_BUFFER_MHR_CP(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1,  \
			      22)
#define SET_TX_DESC_BUFFER_SMH_EN(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 21)
#define SET_TX_DESC_BUFFER_SMH_EN_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 21)
#define GET_TX_DESC_BUFFER_SMH_EN(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1,  \
			      21)
#define SET_TX_DESC_BUFFER_RTSRATE(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1f, 16)
#define SET_TX_DESC_BUFFER_RTSRATE_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1f, 16)
#define GET_TX_DESC_BUFFER_RTSRATE(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1f, \
			      16)
#define SET_TX_DESC_BUFFER_SMH_CAM(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0xff, 8)
#define SET_TX_DESC_BUFFER_SMH_CAM_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0xff, 8)
#define GET_TX_DESC_BUFFER_SMH_CAM(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0xff, \
			      8)
#define SET_TX_DESC_BUFFER_ARFR_TABLE_SEL(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 7)
#define SET_TX_DESC_BUFFER_ARFR_TABLE_SEL_NO_CLR(txdesc, value)                \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 7)
#define GET_TX_DESC_BUFFER_ARFR_TABLE_SEL(txdesc)                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 7)
#define SET_TX_DESC_BUFFER_ARFR_HT_EN(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 6)
#define SET_TX_DESC_BUFFER_ARFR_HT_EN_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 6)
#define GET_TX_DESC_BUFFER_ARFR_HT_EN(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 6)
#define SET_TX_DESC_BUFFER_ARFR_OFDM_EN(txdesc, value)                         \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 5)
#define SET_TX_DESC_BUFFER_ARFR_OFDM_EN_NO_CLR(txdesc, value)                  \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 5)
#define GET_TX_DESC_BUFFER_ARFR_OFDM_EN(txdesc)                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 5)
#define SET_TX_DESC_BUFFER_ARFR_CCK_EN(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 4)
#define SET_TX_DESC_BUFFER_ARFR_CCK_EN_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 4)
#define GET_TX_DESC_BUFFER_ARFR_CCK_EN(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 4)
#define SET_TX_DESC_BUFFER_RTS_SHORT(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 3)
#define SET_TX_DESC_BUFFER_RTS_SHORT_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 3)
#define GET_TX_DESC_BUFFER_RTS_SHORT(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 3)
#define SET_TX_DESC_BUFFER_DISDATAFB(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 2)
#define SET_TX_DESC_BUFFER_DISDATAFB_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 2)
#define GET_TX_DESC_BUFFER_DISDATAFB(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 2)
#define SET_TX_DESC_BUFFER_DISRTSFB(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 1)
#define SET_TX_DESC_BUFFER_DISRTSFB_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 1)
#define GET_TX_DESC_BUFFER_DISRTSFB(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 1)
#define SET_TX_DESC_BUFFER_EXT_EDCA(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword9,   \
				  value, 0x1, 0)
#define SET_TX_DESC_BUFFER_EXT_EDCA_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword9, value, 0x1, 0)
#define GET_TX_DESC_BUFFER_EXT_EDCA(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword9, 0x1, 0)

/*TXDESC_WORD10*/

#define SET_TX_DESC_BUFFER_AMPDU_MAX_TIME(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0xff, 24)
#define SET_TX_DESC_BUFFER_AMPDU_MAX_TIME_NO_CLR(txdesc, value)                \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0xff, 24)
#define GET_TX_DESC_BUFFER_AMPDU_MAX_TIME(txdesc)                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10,      \
			      0xff, 24)
#define SET_TX_DESC_BUFFER_SPECIAL_CW(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 23)
#define SET_TX_DESC_BUFFER_SPECIAL_CW_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 23)
#define GET_TX_DESC_BUFFER_SPECIAL_CW(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      23)
#define SET_TX_DESC_BUFFER_RDG_NAV_EXT(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 22)
#define SET_TX_DESC_BUFFER_RDG_NAV_EXT_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 22)
#define GET_TX_DESC_BUFFER_RDG_NAV_EXT(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      22)
#define SET_TX_DESC_BUFFER_RAW(txdesc, value)                                  \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 21)
#define SET_TX_DESC_BUFFER_RAW_NO_CLR(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 21)
#define GET_TX_DESC_BUFFER_RAW(txdesc)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      21)
#define SET_TX_DESC_BUFFER_MAX_AGG_NUM(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1f, 16)
#define SET_TX_DESC_BUFFER_MAX_AGG_NUM_NO_CLR(txdesc, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1f, 16)
#define GET_TX_DESC_BUFFER_MAX_AGG_NUM(txdesc)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10,      \
			      0x1f, 16)
#define SET_TX_DESC_BUFFER_FINAL_DATA_RATE(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0xff, 8)
#define SET_TX_DESC_BUFFER_FINAL_DATA_RATE_NO_CLR(txdesc, value)               \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0xff, 8)
#define GET_TX_DESC_BUFFER_FINAL_DATA_RATE(txdesc)                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10,      \
			      0xff, 8)
#define SET_TX_DESC_BUFFER_GF(txdesc, value)                                   \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 7)
#define SET_TX_DESC_BUFFER_GF_NO_CLR(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 7)
#define GET_TX_DESC_BUFFER_GF(txdesc)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      7)
#define SET_TX_DESC_BUFFER_MOREFRAG(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 6)
#define SET_TX_DESC_BUFFER_MOREFRAG_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 6)
#define GET_TX_DESC_BUFFER_MOREFRAG(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      6)
#define SET_TX_DESC_BUFFER_NOACM(txdesc, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 5)
#define SET_TX_DESC_BUFFER_NOACM_NO_CLR(txdesc, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 5)
#define GET_TX_DESC_BUFFER_NOACM(txdesc)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      5)
#define SET_TX_DESC_BUFFER_HTC(txdesc, value)                                  \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 4)
#define SET_TX_DESC_BUFFER_HTC_NO_CLR(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 4)
#define GET_TX_DESC_BUFFER_HTC(txdesc)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      4)
#define SET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 3)
#define SET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS_NO_CLR(txdesc, value)             \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 3)
#define GET_TX_DESC_BUFFER_TX_PKT_AFTER_PIFS(txdesc)                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      3)
#define SET_TX_DESC_BUFFER_USE_MAX_TIME_EN(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x1, 2)
#define SET_TX_DESC_BUFFER_USE_MAX_TIME_EN_NO_CLR(txdesc, value)               \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x1, 2)
#define GET_TX_DESC_BUFFER_USE_MAX_TIME_EN(txdesc)                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x1, \
			      2)
#define SET_TX_DESC_BUFFER_HW_SSN_SEL(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword10,  \
				  value, 0x3, 0)
#define SET_TX_DESC_BUFFER_HW_SSN_SEL_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword10, value, 0x3, 0)
#define GET_TX_DESC_BUFFER_HW_SSN_SEL(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword10, 0x3, \
			      0)

/*TXDESC_WORD11*/

#define SET_TX_DESC_BUFFER_ADDR_CAM(txdesc, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword11,  \
				  value, 0xff, 24)
#define SET_TX_DESC_BUFFER_ADDR_CAM_NO_CLR(txdesc, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword11, value, 0xff, 24)
#define GET_TX_DESC_BUFFER_ADDR_CAM(txdesc)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword11,      \
			      0xff, 24)
#define SET_TX_DESC_BUFFER_SND_TARGET(txdesc, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword11,  \
				  value, 0xff, 16)
#define SET_TX_DESC_BUFFER_SND_TARGET_NO_CLR(txdesc, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword11, value, 0xff, 16)
#define GET_TX_DESC_BUFFER_SND_TARGET(txdesc)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword11,      \
			      0xff, 16)
#define SET_TX_DESC_BUFFER_DATA_LDPC(txdesc, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword11,  \
				  value, 0x1, 15)
#define SET_TX_DESC_BUFFER_DATA_LDPC_NO_CLR(txdesc, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword11, value, 0x1, 15)
#define GET_TX_DESC_BUFFER_DATA_LDPC(txdesc)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword11, 0x1, \
			      15)
#define SET_TX_DESC_BUFFER_LSIG_TXOP_EN(txdesc, value)                         \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword11,  \
				  value, 0x1, 14)
#define SET_TX_DESC_BUFFER_LSIG_TXOP_EN_NO_CLR(txdesc, value)                  \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword11, value, 0x1, 14)
#define GET_TX_DESC_BUFFER_LSIG_TXOP_EN(txdesc)                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword11, 0x1, \
			      14)
#define SET_TX_DESC_BUFFER_G_ID(txdesc, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword11,  \
				  value, 0x3f, 8)
#define SET_TX_DESC_BUFFER_G_ID_NO_CLR(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword11, value, 0x3f, 8)
#define GET_TX_DESC_BUFFER_G_ID(txdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword11,      \
			      0x3f, 8)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC(txdesc, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword11,  \
				  value, 0xf, 4)
#define SET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC_NO_CLR(txdesc, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword11, value, 0xf, 4)
#define GET_TX_DESC_BUFFER_SIGNALING_TA_PKT_SC(txdesc)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword11, 0xf, \
			      4)
#define SET_TX_DESC_BUFFER_DATA_SC(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword11,  \
				  value, 0xf, 0)
#define SET_TX_DESC_BUFFER_DATA_SC_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword11, value, 0xf, 0)
#define GET_TX_DESC_BUFFER_DATA_SC(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword11, 0xf, \
			      0)

/*TXDESC_WORD12*/

#define SET_TX_DESC_BUFFER_LEN1_L(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword12,  \
				  value, 0x7f, 17)
#define SET_TX_DESC_BUFFER_LEN1_L_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword12, value, 0x7f, 17)
#define GET_TX_DESC_BUFFER_LEN1_L(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword12,      \
			      0x7f, 17)
#define SET_TX_DESC_BUFFER_LEN0(txdesc, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword12,  \
				  value, 0x1fff, 4)
#define SET_TX_DESC_BUFFER_LEN0_NO_CLR(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword12, value, 0x1fff, 4)
#define GET_TX_DESC_BUFFER_LEN0(txdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword12,      \
			      0x1fff, 4)
#define SET_TX_DESC_BUFFER_PKT_NUM(txdesc, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword12,  \
				  value, 0xf, 0)
#define SET_TX_DESC_BUFFER_PKT_NUM_NO_CLR(txdesc, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword12, value, 0xf, 0)
#define GET_TX_DESC_BUFFER_PKT_NUM(txdesc)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword12, 0xf, \
			      0)

/*TXDESC_WORD13*/

#define SET_TX_DESC_BUFFER_LEN3(txdesc, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword13,  \
				  value, 0x1fff, 19)
#define SET_TX_DESC_BUFFER_LEN3_NO_CLR(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword13, value, 0x1fff, 19)
#define GET_TX_DESC_BUFFER_LEN3(txdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword13,      \
			      0x1fff, 19)
#define SET_TX_DESC_BUFFER_LEN2(txdesc, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword13,  \
				  value, 0x1fff, 6)
#define SET_TX_DESC_BUFFER_LEN2_NO_CLR(txdesc, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword13, value, 0x1fff, 6)
#define GET_TX_DESC_BUFFER_LEN2(txdesc)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword13,      \
			      0x1fff, 6)
#define SET_TX_DESC_BUFFER_LEN1_H(txdesc, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(((struct halmac_tx_desc *)txdesc)->dword13,  \
				  value, 0x3f, 0)
#define SET_TX_DESC_BUFFER_LEN1_H_NO_CLR(txdesc, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc)->dword13, value, 0x3f, 0)
#define GET_TX_DESC_BUFFER_LEN1_H(txdesc)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc)->dword13,      \
			      0x3f, 0)

#endif

#endif

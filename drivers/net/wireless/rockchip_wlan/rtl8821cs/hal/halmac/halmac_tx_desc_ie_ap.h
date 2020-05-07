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

#ifndef _HALMAC_TX_DESC_IE_AP_H_
#define _HALMAC_TX_DESC_IE_AP_H_
#if (HALMAC_8814B_SUPPORT)

#define IE0_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 31)
#define IE0_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE0_SET_TX_DESC_IE_END_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE0_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 30)
#define IE0_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE0_SET_TX_DESC_IE_UP_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE0_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 24)
#define IE0_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE0_SET_TX_DESC_IE_NUM_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE0_GET_TX_DESC_ARFR_TABLE_SEL(txdesc_ie)                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 19)
#define IE0_SET_TX_DESC_ARFR_TABLE_SEL(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 19)
#define IE0_SET_TX_DESC_ARFR_TABLE_SEL_NO_CLR(txdesc_ie, value)                \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 19)
#define IE0_GET_TX_DESC_ARFR_HT_EN(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 18)
#define IE0_SET_TX_DESC_ARFR_HT_EN(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 18)
#define IE0_SET_TX_DESC_ARFR_HT_EN_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 18)
#define IE0_GET_TX_DESC_ARFR_OFDM_EN(txdesc_ie)                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 17)
#define IE0_SET_TX_DESC_ARFR_OFDM_EN(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 17)
#define IE0_SET_TX_DESC_ARFR_OFDM_EN_NO_CLR(txdesc_ie, value)                  \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 17)
#define IE0_GET_TX_DESC_ARFR_CCK_EN(txdesc_ie)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 16)
#define IE0_SET_TX_DESC_ARFR_CCK_EN(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 16)
#define IE0_SET_TX_DESC_ARFR_CCK_EN_NO_CLR(txdesc_ie, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 16)
#define IE0_GET_TX_DESC_HW_RTS_EN(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 9)
#define IE0_SET_TX_DESC_HW_RTS_EN(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 9)
#define IE0_SET_TX_DESC_HW_RTS_EN_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 9)
#define IE0_GET_TX_DESC_RTS_EN(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 8)
#define IE0_SET_TX_DESC_RTS_EN(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 8)
#define IE0_SET_TX_DESC_RTS_EN_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 8)
#define IE0_GET_TX_DESC_CTS2SELF(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 7)
#define IE0_SET_TX_DESC_CTS2SELF(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 7)
#define IE0_SET_TX_DESC_CTS2SELF_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 7)
#define IE0_GET_TX_DESC_RTY_LMT_EN(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 6)
#define IE0_SET_TX_DESC_RTY_LMT_EN(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 6)
#define IE0_SET_TX_DESC_RTY_LMT_EN_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 6)
#define IE0_GET_TX_DESC_RTS_SHORT(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 5)
#define IE0_SET_TX_DESC_RTS_SHORT(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 5)
#define IE0_SET_TX_DESC_RTS_SHORT_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 5)
#define IE0_GET_TX_DESC_DISDATAFB(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 4)
#define IE0_SET_TX_DESC_DISDATAFB(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 4)
#define IE0_SET_TX_DESC_DISDATAFB_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 4)
#define IE0_GET_TX_DESC_DISRTSFB(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 3)
#define IE0_SET_TX_DESC_DISRTSFB(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 3)
#define IE0_SET_TX_DESC_DISRTSFB_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 3)
#define IE0_GET_TX_DESC_DATA_SHORT(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 2)
#define IE0_SET_TX_DESC_DATA_SHORT(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE0_SET_TX_DESC_DATA_SHORT_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE0_GET_TX_DESC_TRY_RATE(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 1)
#define IE0_SET_TX_DESC_TRY_RATE(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE0_SET_TX_DESC_TRY_RATE_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE0_GET_TX_DESC_USERATE(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 0)
#define IE0_SET_TX_DESC_USERATE(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE0_SET_TX_DESC_USERATE_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE0_GET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc_ie)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 27)
#define IE0_SET_TX_DESC_RTS_RTY_LOWEST_RATE(txdesc_ie, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 27)
#define IE0_SET_TX_DESC_RTS_RTY_LOWEST_RATE_NO_CLR(txdesc_ie, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 27)
#define IE0_GET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc_ie)                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x1f, 22)
#define IE0_SET_TX_DESC_DATA_RTY_LOWEST_RATE(txdesc_ie, value)                 \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1f, 22)
#define IE0_SET_TX_DESC_DATA_RTY_LOWEST_RATE_NO_CLR(txdesc_ie, value)          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1f, 22)
#define IE0_GET_TX_DESC_RTS_DATA_RTY_LMT(txdesc_ie)                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3f, 16)
#define IE0_SET_TX_DESC_RTS_DATA_RTY_LMT(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3f, 16)
#define IE0_SET_TX_DESC_RTS_DATA_RTY_LMT_NO_CLR(txdesc_ie, value)              \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3f, 16)
#define IE0_GET_TX_DESC_DATA_BW(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 12)
#define IE0_SET_TX_DESC_DATA_BW(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 12)
#define IE0_SET_TX_DESC_DATA_BW_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 12)
#define IE0_GET_TX_DESC_RTSRATE(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 7)
#define IE0_SET_TX_DESC_RTSRATE(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 7)
#define IE0_SET_TX_DESC_RTSRATE_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 7)
#define IE0_GET_TX_DESC_DATARATE(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x7f, 0)
#define IE0_SET_TX_DESC_DATARATE(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x7f, 0)
#define IE0_SET_TX_DESC_DATARATE_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x7f, 0)
#define IE1_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 31)
#define IE1_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE1_SET_TX_DESC_IE_END_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE1_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 30)
#define IE1_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE1_SET_TX_DESC_IE_UP_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE1_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 24)
#define IE1_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE1_SET_TX_DESC_IE_NUM_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE1_GET_TX_DESC_AMPDU_DENSITY(txdesc_ie)                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x7, 21)
#define IE1_SET_TX_DESC_AMPDU_DENSITY(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x7, 21)
#define IE1_SET_TX_DESC_AMPDU_DENSITY_NO_CLR(txdesc_ie, value)                 \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x7, 21)
#define IE1_GET_TX_DESC_MAX_AGG_NUM(txdesc_ie)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1f, 16)
#define IE1_SET_TX_DESC_MAX_AGG_NUM(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1f, 16)
#define IE1_SET_TX_DESC_MAX_AGG_NUM_NO_CLR(txdesc_ie, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1f, 16)
#define IE1_GET_TX_DESC_SECTYPE(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x3, 14)
#define IE1_SET_TX_DESC_SECTYPE(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 14)
#define IE1_SET_TX_DESC_SECTYPE_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 14)
#define IE1_GET_TX_DESC_MOREFRAG(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 13)
#define IE1_SET_TX_DESC_MOREFRAG(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 13)
#define IE1_SET_TX_DESC_MOREFRAG_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 13)
#define IE1_GET_TX_DESC_NOACM(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 12)
#define IE1_SET_TX_DESC_NOACM(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 12)
#define IE1_SET_TX_DESC_NOACM_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 12)
#define IE1_GET_TX_DESC_BCNPKT_TSF_CTRL(txdesc_ie)                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 11)
#define IE1_SET_TX_DESC_BCNPKT_TSF_CTRL(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 11)
#define IE1_SET_TX_DESC_BCNPKT_TSF_CTRL_NO_CLR(txdesc_ie, value)               \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 11)
#define IE1_GET_TX_DESC_NAVUSEHDR(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 10)
#define IE1_SET_TX_DESC_NAVUSEHDR(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 10)
#define IE1_SET_TX_DESC_NAVUSEHDR_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 10)
#define IE1_GET_TX_DESC_HTC(txdesc_ie)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 9)
#define IE1_SET_TX_DESC_HTC(txdesc_ie, value)                                  \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 9)
#define IE1_SET_TX_DESC_HTC_NO_CLR(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 9)
#define IE1_GET_TX_DESC_BMC(txdesc_ie)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 8)
#define IE1_SET_TX_DESC_BMC(txdesc_ie, value)                                  \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 8)
#define IE1_SET_TX_DESC_BMC_NO_CLR(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 8)
#define IE1_GET_TX_DESC_TX_PKT_AFTER_PIFS(txdesc_ie)                           \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 7)
#define IE1_SET_TX_DESC_TX_PKT_AFTER_PIFS(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 7)
#define IE1_SET_TX_DESC_TX_PKT_AFTER_PIFS_NO_CLR(txdesc_ie, value)             \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 7)
#define IE1_GET_TX_DESC_USE_MAX_TIME_EN(txdesc_ie)                             \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 6)
#define IE1_SET_TX_DESC_USE_MAX_TIME_EN(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 6)
#define IE1_SET_TX_DESC_USE_MAX_TIME_EN_NO_CLR(txdesc_ie, value)               \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 6)
#define IE1_GET_TX_DESC_HW_SSN_SEL(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x3, 4)
#define IE1_SET_TX_DESC_HW_SSN_SEL(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 4)
#define IE1_SET_TX_DESC_HW_SSN_SEL_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 4)
#define IE1_GET_TX_DESC_DISQSELSEQ(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 3)
#define IE1_SET_TX_DESC_DISQSELSEQ(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 3)
#define IE1_SET_TX_DESC_DISQSELSEQ_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 3)
#define IE1_GET_TX_DESC_EN_HWSEQ(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 2)
#define IE1_SET_TX_DESC_EN_HWSEQ(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE1_SET_TX_DESC_EN_HWSEQ_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE1_GET_TX_DESC_EN_HWEXSEQ(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 1)
#define IE1_SET_TX_DESC_EN_HWEXSEQ(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE1_SET_TX_DESC_EN_HWEXSEQ_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE1_GET_TX_DESC_EN_DESC_ID(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 0)
#define IE1_SET_TX_DESC_EN_DESC_ID(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE1_SET_TX_DESC_EN_DESC_ID_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE1_GET_TX_DESC_AMPDU_MAX_TIME(txdesc_ie)                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xff, 24)
#define IE1_SET_TX_DESC_AMPDU_MAX_TIME(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xff, 24)
#define IE1_SET_TX_DESC_AMPDU_MAX_TIME_NO_CLR(txdesc_ie, value)                \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xff, 24)
#define IE1_GET_TX_DESC_P_AID(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x1ff, 15)
#define IE1_SET_TX_DESC_P_AID(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1ff,    \
		15)
#define IE1_SET_TX_DESC_P_AID_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1ff,    \
		15)
#define IE1_GET_TX_DESC_MOREDATA(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x1, 14)
#define IE1_SET_TX_DESC_MOREDATA(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1, 14)
#define IE1_SET_TX_DESC_MOREDATA_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1, 14)
#define IE1_GET_TX_DESC_SW_SEQ(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xfff, 0)
#define IE1_SET_TX_DESC_SW_SEQ(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xfff, 0)
#define IE1_SET_TX_DESC_SW_SEQ_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xfff, 0)
#define IE2_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 31)
#define IE2_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE2_SET_TX_DESC_IE_END_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE2_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 30)
#define IE2_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE2_SET_TX_DESC_IE_UP_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE2_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 24)
#define IE2_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE2_SET_TX_DESC_IE_NUM_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE2_GET_TX_DESC_ADDR_CAM(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xff, 16)
#define IE2_SET_TX_DESC_ADDR_CAM(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xff, 16)
#define IE2_SET_TX_DESC_ADDR_CAM_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xff, 16)
#define IE2_GET_TX_DESC_MULTIPLE_PORT(txdesc_ie)                               \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x7, 12)
#define IE2_SET_TX_DESC_MULTIPLE_PORT(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x7, 12)
#define IE2_SET_TX_DESC_MULTIPLE_PORT_NO_CLR(txdesc_ie, value)                 \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x7, 12)
#define IE2_GET_TX_DESC_RAW(txdesc_ie)                                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 11)
#define IE2_SET_TX_DESC_RAW(txdesc_ie, value)                                  \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 11)
#define IE2_SET_TX_DESC_RAW_NO_CLR(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 11)
#define IE2_GET_TX_DESC_RDG_EN(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 10)
#define IE2_SET_TX_DESC_RDG_EN(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 10)
#define IE2_SET_TX_DESC_RDG_EN_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 10)
#define IE2_GET_TX_DESC_SPECIAL_CW(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 7)
#define IE2_SET_TX_DESC_SPECIAL_CW(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 7)
#define IE2_SET_TX_DESC_SPECIAL_CW_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 7)
#define IE2_GET_TX_DESC_POLLUTED(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 6)
#define IE2_SET_TX_DESC_POLLUTED(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 6)
#define IE2_SET_TX_DESC_POLLUTED_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 6)
#define IE2_GET_TX_DESC_BT_NULL(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 5)
#define IE2_SET_TX_DESC_BT_NULL(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 5)
#define IE2_SET_TX_DESC_BT_NULL_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 5)
#define IE2_GET_TX_DESC_NULL_1(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 4)
#define IE2_SET_TX_DESC_NULL_1(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 4)
#define IE2_SET_TX_DESC_NULL_1_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 4)
#define IE2_GET_TX_DESC_NULL_0(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 3)
#define IE2_SET_TX_DESC_NULL_0(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 3)
#define IE2_SET_TX_DESC_NULL_0_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 3)
#define IE2_GET_TX_DESC_TRI_FRAME(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 2)
#define IE2_SET_TX_DESC_TRI_FRAME(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE2_SET_TX_DESC_TRI_FRAME_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE2_GET_TX_DESC_SPE_RPT(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 1)
#define IE2_SET_TX_DESC_SPE_RPT(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE2_SET_TX_DESC_SPE_RPT_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE2_GET_TX_DESC_FTM_EN(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 0)
#define IE2_SET_TX_DESC_FTM_EN(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE2_SET_TX_DESC_FTM_EN_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE2_GET_TX_DESC_MBSSID(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 27)
#define IE2_SET_TX_DESC_MBSSID(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 27)
#define IE2_SET_TX_DESC_MBSSID_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 27)
#define IE2_GET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc_ie)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x7ff, 16)
#define IE2_SET_TX_DESC_GROUP_BIT_IE_OFFSET(txdesc_ie, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x7ff,    \
		16)
#define IE2_SET_TX_DESC_GROUP_BIT_IE_OFFSET_NO_CLR(txdesc_ie, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x7ff,    \
		16)
#define IE2_GET_TX_DESC_RDG_NAV_EXT(txdesc_ie)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x1, 15)
#define IE2_SET_TX_DESC_RDG_NAV_EXT(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1, 15)
#define IE2_SET_TX_DESC_RDG_NAV_EXT_NO_CLR(txdesc_ie, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1, 15)
#define IE2_GET_TX_DESC_DROP_ID(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 12)
#define IE2_SET_TX_DESC_DROP_ID(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 12)
#define IE2_SET_TX_DESC_DROP_ID_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 12)
#define IE2_GET_TX_DESC_SW_DEFINE(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xfff, 0)
#define IE2_SET_TX_DESC_SW_DEFINE(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xfff, 0)
#define IE2_SET_TX_DESC_SW_DEFINE_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xfff, 0)
#define IE3_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 31)
#define IE3_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE3_SET_TX_DESC_IE_END_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE3_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 30)
#define IE3_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE3_SET_TX_DESC_IE_UP_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE3_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 24)
#define IE3_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE3_SET_TX_DESC_IE_NUM_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE3_GET_TX_DESC_DATA_SC(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 20)
#define IE3_SET_TX_DESC_DATA_SC(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 20)
#define IE3_SET_TX_DESC_DATA_SC_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 20)
#define IE3_GET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc_ie)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 16)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_SC(txdesc_ie, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 16)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_SC_NO_CLR(txdesc_ie, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 16)
#define IE3_GET_TX_DESC_CTRL_CNT(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 8)
#define IE3_SET_TX_DESC_CTRL_CNT(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 8)
#define IE3_SET_TX_DESC_CTRL_CNT_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 8)
#define IE3_GET_TX_DESC_CTRL_CNT_VALID(txdesc_ie)                              \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 1)
#define IE3_SET_TX_DESC_CTRL_CNT_VALID(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE3_SET_TX_DESC_CTRL_CNT_VALID_NO_CLR(txdesc_ie, value)                \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE3_GET_TX_DESC_SIGNALING_TA_PKT_EN(txdesc_ie)                         \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 0)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_EN(txdesc_ie, value)                  \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE3_SET_TX_DESC_SIGNALING_TA_PKT_EN_NO_CLR(txdesc_ie, value)           \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE3_GET_TX_DESC_G_ID(txdesc_ie)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3f, 24)
#define IE3_SET_TX_DESC_G_ID(txdesc_ie, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3f, 24)
#define IE3_SET_TX_DESC_G_ID_NO_CLR(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3f, 24)
#define IE3_GET_TX_DESC_SND_TARGET(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xff, 16)
#define IE3_SET_TX_DESC_SND_TARGET(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xff, 16)
#define IE3_SET_TX_DESC_SND_TARGET_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xff, 16)
#define IE3_GET_TX_DESC_CCA_RTS(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 11)
#define IE3_SET_TX_DESC_CCA_RTS(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 11)
#define IE3_SET_TX_DESC_CCA_RTS_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 11)
#define IE3_GET_TX_DESC_SND_PKT_SEL(txdesc_ie)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 9)
#define IE3_SET_TX_DESC_SND_PKT_SEL(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 9)
#define IE3_SET_TX_DESC_SND_PKT_SEL_NO_CLR(txdesc_ie, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 9)
#define IE3_GET_TX_DESC_NDPA(txdesc_ie)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 7)
#define IE3_SET_TX_DESC_NDPA(txdesc_ie, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 7)
#define IE3_SET_TX_DESC_NDPA_NO_CLR(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 7)
#define IE3_GET_TX_DESC_MU_DATARATE(txdesc_ie)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x7f, 0)
#define IE3_SET_TX_DESC_MU_DATARATE(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x7f, 0)
#define IE3_SET_TX_DESC_MU_DATARATE_NO_CLR(txdesc_ie, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x7f, 0)
#define IE4_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 31)
#define IE4_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE4_SET_TX_DESC_IE_END_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE4_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 30)
#define IE4_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE4_SET_TX_DESC_IE_UP_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE4_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 24)
#define IE4_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE4_SET_TX_DESC_IE_NUM_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE4_GET_TX_DESC_VCS_STBC(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x3, 10)
#define IE4_SET_TX_DESC_VCS_STBC(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 10)
#define IE4_SET_TX_DESC_VCS_STBC_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 10)
#define IE4_GET_TX_DESC_DATA_STBC(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x3, 8)
#define IE4_SET_TX_DESC_DATA_STBC(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 8)
#define IE4_SET_TX_DESC_DATA_STBC_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x3, 8)
#define IE4_GET_TX_DESC_DATA_LDPC(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 2)
#define IE4_SET_TX_DESC_DATA_LDPC(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE4_SET_TX_DESC_DATA_LDPC_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 2)
#define IE4_GET_TX_DESC_GF(txdesc_ie)                                          \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 1)
#define IE4_SET_TX_DESC_GF(txdesc_ie, value)                                   \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE4_SET_TX_DESC_GF_NO_CLR(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 1)
#define IE4_GET_TX_DESC_LSIG_TXOP_EN(txdesc_ie)                                \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 0)
#define IE4_SET_TX_DESC_LSIG_TXOP_EN(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE4_SET_TX_DESC_LSIG_TXOP_EN_NO_CLR(txdesc_ie, value)                  \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 0)
#define IE4_GET_TX_DESC_PATH_MAPA(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 30)
#define IE4_SET_TX_DESC_PATH_MAPA(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 30)
#define IE4_SET_TX_DESC_PATH_MAPA_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 30)
#define IE4_GET_TX_DESC_PATH_MAPB(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 28)
#define IE4_SET_TX_DESC_PATH_MAPB(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 28)
#define IE4_SET_TX_DESC_PATH_MAPB_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 28)
#define IE4_GET_TX_DESC_PATH_MAPC(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 26)
#define IE4_SET_TX_DESC_PATH_MAPC(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 26)
#define IE4_SET_TX_DESC_PATH_MAPC_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 26)
#define IE4_GET_TX_DESC_PATH_MAPD(txdesc_ie)                                   \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 24)
#define IE4_SET_TX_DESC_PATH_MAPD(txdesc_ie, value)                            \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 24)
#define IE4_SET_TX_DESC_PATH_MAPD_NO_CLR(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 24)
#define IE4_GET_TX_DESC_ANTSEL_A(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 20)
#define IE4_SET_TX_DESC_ANTSEL_A(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 20)
#define IE4_SET_TX_DESC_ANTSEL_A_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 20)
#define IE4_GET_TX_DESC_ANTSEL_B(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 16)
#define IE4_SET_TX_DESC_ANTSEL_B(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 16)
#define IE4_SET_TX_DESC_ANTSEL_B_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 16)
#define IE4_GET_TX_DESC_ANTSEL_C(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 12)
#define IE4_SET_TX_DESC_ANTSEL_C(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 12)
#define IE4_SET_TX_DESC_ANTSEL_C_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 12)
#define IE4_GET_TX_DESC_ANTSEL_D(txdesc_ie)                                    \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 8)
#define IE4_SET_TX_DESC_ANTSEL_D(txdesc_ie, value)                             \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 8)
#define IE4_SET_TX_DESC_ANTSEL_D_NO_CLR(txdesc_ie, value)                      \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 8)
#define IE4_GET_TX_DESC_NTX_PATH_EN(txdesc_ie)                                 \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0xf, 4)
#define IE4_SET_TX_DESC_NTX_PATH_EN(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 4)
#define IE4_SET_TX_DESC_NTX_PATH_EN_NO_CLR(txdesc_ie, value)                   \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0xf, 4)
#define IE4_GET_TX_DESC_ANTLSEL_EN(txdesc_ie)                                  \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x1, 3)
#define IE4_SET_TX_DESC_ANTLSEL_EN(txdesc_ie, value)                           \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1, 3)
#define IE4_SET_TX_DESC_ANTLSEL_EN_NO_CLR(txdesc_ie, value)                    \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1, 3)
#define IE4_GET_TX_DESC_TXPWR_OFSET_TYPE(txdesc_ie)                            \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3, 0)
#define IE4_SET_TX_DESC_TXPWR_OFSET_TYPE(txdesc_ie, value)                     \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 0)
#define IE4_SET_TX_DESC_TXPWR_OFSET_TYPE_NO_CLR(txdesc_ie, value)              \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3, 0)
#define IE5_GET_TX_DESC_IE_END(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 31)
#define IE5_SET_TX_DESC_IE_END(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE5_SET_TX_DESC_IE_END_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 31)
#define IE5_GET_TX_DESC_IE_UP(txdesc_ie)                                       \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1, 30)
#define IE5_SET_TX_DESC_IE_UP(txdesc_ie, value)                                \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE5_SET_TX_DESC_IE_UP_NO_CLR(txdesc_ie, value)                         \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1, 30)
#define IE5_GET_TX_DESC_IE_NUM(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 24)
#define IE5_SET_TX_DESC_IE_NUM(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE5_SET_TX_DESC_IE_NUM_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 24)
#define IE5_GET_TX_DESC_LEN1_L(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x7f, 17)
#define IE5_SET_TX_DESC_LEN1_L(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x7f, 17)
#define IE5_SET_TX_DESC_LEN1_L_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x7f, 17)
#define IE5_GET_TX_DESC_LEN0(txdesc_ie)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0x1fff, 4)
#define IE5_SET_TX_DESC_LEN0(txdesc_ie, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1fff,   \
		4)
#define IE5_SET_TX_DESC_LEN0_NO_CLR(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0x1fff,   \
		4)
#define IE5_GET_TX_DESC_PKT_NUM(txdesc_ie)                                     \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword0,    \
			      0xf, 0)
#define IE5_SET_TX_DESC_PKT_NUM(txdesc_ie, value)                              \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 0)
#define IE5_SET_TX_DESC_PKT_NUM_NO_CLR(txdesc_ie, value)                       \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword0, value, 0xf, 0)
#define IE5_GET_TX_DESC_LEN3(txdesc_ie)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x1fff, 19)
#define IE5_SET_TX_DESC_LEN3(txdesc_ie, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1fff,   \
		19)
#define IE5_SET_TX_DESC_LEN3_NO_CLR(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1fff,   \
		19)
#define IE5_GET_TX_DESC_LEN2(txdesc_ie)                                        \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x1fff, 6)
#define IE5_SET_TX_DESC_LEN2(txdesc_ie, value)                                 \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1fff,   \
		6)
#define IE5_SET_TX_DESC_LEN2_NO_CLR(txdesc_ie, value)                          \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x1fff,   \
		6)
#define IE5_GET_TX_DESC_LEN1_H(txdesc_ie)                                      \
	HALMAC_GET_DESC_FIELD(((struct halmac_tx_desc *)txdesc_ie)->dword1,    \
			      0x3f, 0)
#define IE5_SET_TX_DESC_LEN1_H(txdesc_ie, value)                               \
	HALMAC_SET_DESC_FIELD_CLR(                                             \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3f, 0)
#define IE5_SET_TX_DESC_LEN1_H_NO_CLR(txdesc_ie, value)                        \
	HALMAC_SET_DESC_FIELD_NO_CLR(                                          \
		((struct halmac_tx_desc *)txdesc_ie)->dword1, value, 0x3f, 0)

#endif

#endif

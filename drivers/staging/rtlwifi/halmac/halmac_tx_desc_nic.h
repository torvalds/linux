/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_TX_DESC_NIC_H_
#define _HALMAC_TX_DESC_NIC_H_

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 31, 1, __value)
#define GET_TX_DESC_DISQSELSEQ(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x00, 31, 1)

#define SET_TX_DESC_GF(__tx_desc, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 30, 1, __value)
#define GET_TX_DESC_GF(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x00, 30, 1)
#define SET_TX_DESC_NO_ACM(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 29, 1, __value)
#define GET_TX_DESC_NO_ACM(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x00, 29, 1)

#define SET_TX_DESC_BCNPKT_TSF_CTRL(__tx_desc, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 28, 1, __value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL(__tx_desc)                                 \
	LE_BITS_TO_4BYTE(__tx_desc + 0x00, 28, 1)

#define SET_TX_DESC_AMSDU_PAD_EN(__tx_desc, __value)                           \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 27, 1, __value)
#define GET_TX_DESC_AMSDU_PAD_EN(__tx_desc)                                    \
	LE_BITS_TO_4BYTE(__tx_desc + 0x00, 27, 1)

#define SET_TX_DESC_LS(__tx_desc, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 26, 1, __value)
#define GET_TX_DESC_LS(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x00, 26, 1)
#define SET_TX_DESC_HTC(__tx_desc, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 25, 1, __value)
#define GET_TX_DESC_HTC(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x00, 25, 1)
#define SET_TX_DESC_BMC(__tx_desc, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 24, 1, __value)
#define GET_TX_DESC_BMC(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x00, 24, 1)
#define SET_TX_DESC_OFFSET(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 16, 8, __value)
#define GET_TX_DESC_OFFSET(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x00, 16, 8)
#define SET_TX_DESC_TXPKTSIZE(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x00, 0, 16, __value)
#define GET_TX_DESC_TXPKTSIZE(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x00, 0, 16)

/*TXDESC_WORD1*/

#define SET_TX_DESC_MOREDATA(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 29, 1, __value)
#define GET_TX_DESC_MOREDATA(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x04, 29, 1)
#define SET_TX_DESC_PKT_OFFSET(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 24, 5, __value)
#define GET_TX_DESC_PKT_OFFSET(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x04, 24, 5)
#define SET_TX_DESC_SEC_TYPE(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 22, 2, __value)
#define GET_TX_DESC_SEC_TYPE(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x04, 22, 2)
#define SET_TX_DESC_EN_DESC_ID(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 21, 1, __value)
#define GET_TX_DESC_EN_DESC_ID(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x04, 21, 1)
#define SET_TX_DESC_RATE_ID(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 16, 5, __value)
#define GET_TX_DESC_RATE_ID(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x04, 16, 5)
#define SET_TX_DESC_PIFS(__tx_desc, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 15, 1, __value)
#define GET_TX_DESC_PIFS(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x04, 15, 1)
#define SET_TX_DESC_LSIG_TXOP_EN(__tx_desc, __value)                           \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 14, 1, __value)
#define GET_TX_DESC_LSIG_TXOP_EN(__tx_desc)                                    \
	LE_BITS_TO_4BYTE(__tx_desc + 0x04, 14, 1)
#define SET_TX_DESC_RD_NAV_EXT(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 13, 1, __value)
#define GET_TX_DESC_RD_NAV_EXT(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x04, 13, 1)
#define SET_TX_DESC_QSEL(__tx_desc, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 8, 5, __value)
#define GET_TX_DESC_QSEL(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x04, 8, 5)
#define SET_TX_DESC_MACID(__tx_desc, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x04, 0, 7, __value)
#define GET_TX_DESC_MACID(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x04, 0, 7)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 31, 1, __value)
#define GET_TX_DESC_HW_AES_IV(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x08, 31, 1)

#define SET_TX_DESC_FTM_EN(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 30, 1, __value)
#define GET_TX_DESC_FTM_EN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 30, 1)

#define SET_TX_DESC_G_ID(__tx_desc, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 24, 6, __value)
#define GET_TX_DESC_G_ID(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 24, 6)
#define SET_TX_DESC_BT_NULL(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 23, 1, __value)
#define GET_TX_DESC_BT_NULL(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 23, 1)
#define SET_TX_DESC_AMPDU_DENSITY(__tx_desc, __value)                          \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 20, 3, __value)
#define GET_TX_DESC_AMPDU_DENSITY(__tx_desc)                                   \
	LE_BITS_TO_4BYTE(__tx_desc + 0x08, 20, 3)
#ifdef SET_TX_DESC_SPE_RPT
#undef SET_TX_DESC_SPE_RPT
#endif
#define SET_TX_DESC_SPE_RPT(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 19, 1, __value)
#define GET_TX_DESC_SPE_RPT(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 19, 1)
#define SET_TX_DESC_RAW(__tx_desc, __value)                                    \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 18, 1, __value)
#define GET_TX_DESC_RAW(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 18, 1)
#define SET_TX_DESC_MOREFRAG(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 17, 1, __value)
#define GET_TX_DESC_MOREFRAG(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x08, 17, 1)
#define SET_TX_DESC_BK(__tx_desc, __value)                                     \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 16, 1, __value)
#define GET_TX_DESC_BK(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 16, 1)
#define SET_TX_DESC_NULL_1(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 15, 1, __value)
#define GET_TX_DESC_NULL_1(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 15, 1)
#define SET_TX_DESC_NULL_0(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 14, 1, __value)
#define GET_TX_DESC_NULL_0(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 14, 1)
#define SET_TX_DESC_RDG_EN(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 13, 1, __value)
#define GET_TX_DESC_RDG_EN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 13, 1)
#define SET_TX_DESC_AGG_EN(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 12, 1, __value)
#define GET_TX_DESC_AGG_EN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 12, 1)
#define SET_TX_DESC_CCA_RTS(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 10, 2, __value)
#define GET_TX_DESC_CCA_RTS(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 10, 2)

#define SET_TX_DESC_TRI_FRAME(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 9, 1, __value)
#define GET_TX_DESC_TRI_FRAME(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x08, 9, 1)

#define SET_TX_DESC_P_AID(__tx_desc, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x08, 0, 9, __value)
#define GET_TX_DESC_P_AID(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x08, 0, 9)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME(__tx_desc, __value)                         \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 24, 8, __value)
#define GET_TX_DESC_AMPDU_MAX_TIME(__tx_desc)                                  \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 24, 8)
#define SET_TX_DESC_NDPA(__tx_desc, __value)                                   \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 22, 2, __value)
#define GET_TX_DESC_NDPA(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 22, 2)
#define SET_TX_DESC_MAX_AGG_NUM(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 17, 5, __value)
#define GET_TX_DESC_MAX_AGG_NUM(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 17, 5)
#define SET_TX_DESC_USE_MAX_TIME_EN(__tx_desc, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 16, 1, __value)
#define GET_TX_DESC_USE_MAX_TIME_EN(__tx_desc)                                 \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 16, 1)
#define SET_TX_DESC_NAVUSEHDR(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 15, 1, __value)
#define GET_TX_DESC_NAVUSEHDR(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 15, 1)

#define SET_TX_DESC_CHK_EN(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 14, 1, __value)
#define GET_TX_DESC_CHK_EN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 14, 1)

#define SET_TX_DESC_HW_RTS_EN(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 13, 1, __value)
#define GET_TX_DESC_HW_RTS_EN(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 13, 1)
#define SET_TX_DESC_RTSEN(__tx_desc, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 12, 1, __value)
#define GET_TX_DESC_RTSEN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 12, 1)
#define SET_TX_DESC_CTS2SELF(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 11, 1, __value)
#define GET_TX_DESC_CTS2SELF(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 11, 1)
#define SET_TX_DESC_DISDATAFB(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 10, 1, __value)
#define GET_TX_DESC_DISDATAFB(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 10, 1)
#define SET_TX_DESC_DISRTSFB(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 9, 1, __value)
#define GET_TX_DESC_DISRTSFB(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 9, 1)
#define SET_TX_DESC_USE_RATE(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 8, 1, __value)
#define GET_TX_DESC_USE_RATE(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 8, 1)
#define SET_TX_DESC_HW_SSN_SEL(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 6, 2, __value)
#define GET_TX_DESC_HW_SSN_SEL(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 6, 2)

#define SET_TX_DESC_WHEADER_LEN(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x0C, 0, 5, __value)
#define GET_TX_DESC_WHEADER_LEN(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x0C, 0, 5)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX(__tx_desc, __value)                          \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 30, 2, __value)
#define GET_TX_DESC_PCTS_MASK_IDX(__tx_desc)                                   \
	LE_BITS_TO_4BYTE(__tx_desc + 0x10, 30, 2)
#define SET_TX_DESC_PCTS_EN(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 29, 1, __value)
#define GET_TX_DESC_PCTS_EN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x10, 29, 1)
#define SET_TX_DESC_RTSRATE(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 24, 5, __value)
#define GET_TX_DESC_RTSRATE(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x10, 24, 5)
#define SET_TX_DESC_RTS_DATA_RTY_LMT(__tx_desc, __value)                       \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 18, 6, __value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT(__tx_desc)                                \
	LE_BITS_TO_4BYTE(__tx_desc + 0x10, 18, 6)
#define SET_TX_DESC_RTY_LMT_EN(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 17, 1, __value)
#define GET_TX_DESC_RTY_LMT_EN(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x10, 17, 1)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE(__tx_desc, __value)                    \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 13, 4, __value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE(__tx_desc)                             \
	LE_BITS_TO_4BYTE(__tx_desc + 0x10, 13, 4)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE(__tx_desc, __value)                   \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 8, 5, __value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE(__tx_desc)                            \
	LE_BITS_TO_4BYTE(__tx_desc + 0x10, 8, 5)
#define SET_TX_DESC_TRY_RATE(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 7, 1, __value)
#define GET_TX_DESC_TRY_RATE(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x10, 7, 1)
#define SET_TX_DESC_DATARATE(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x10, 0, 7, __value)
#define GET_TX_DESC_DATARATE(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x10, 0, 7)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 31, 1, __value)
#define GET_TX_DESC_POLLUTED(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 31, 1)

#define SET_TX_DESC_TXPWR_OFSET(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 28, 3, __value)
#define GET_TX_DESC_TXPWR_OFSET(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 28, 3)
#define SET_TX_DESC_TX_ANT(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 24, 4, __value)
#define GET_TX_DESC_TX_ANT(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x14, 24, 4)
#define SET_TX_DESC_PORT_ID(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 21, 3, __value)
#define GET_TX_DESC_PORT_ID(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x14, 21, 3)

#define SET_TX_DESC_MULTIPLE_PORT(__tx_desc, __value)                          \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 18, 3, __value)
#define GET_TX_DESC_MULTIPLE_PORT(__tx_desc)                                   \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 18, 3)

#define SET_TX_DESC_SIGNALING_TAPKT_EN(__tx_desc, __value)                     \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 17, 1, __value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN(__tx_desc)                              \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 17, 1)

#define SET_TX_DESC_RTS_SC(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 13, 4, __value)
#define GET_TX_DESC_RTS_SC(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x14, 13, 4)
#define SET_TX_DESC_RTS_SHORT(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 12, 1, __value)
#define GET_TX_DESC_RTS_SHORT(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 12, 1)

#define SET_TX_DESC_VCS_STBC(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 10, 2, __value)
#define GET_TX_DESC_VCS_STBC(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 10, 2)

#define SET_TX_DESC_DATA_STBC(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 8, 2, __value)
#define GET_TX_DESC_DATA_STBC(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 8, 2)

#define SET_TX_DESC_DATA_LDPC(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 7, 1, __value)
#define GET_TX_DESC_DATA_LDPC(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 7, 1)

#define SET_TX_DESC_DATA_BW(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 5, 2, __value)
#define GET_TX_DESC_DATA_BW(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x14, 5, 2)
#define SET_TX_DESC_DATA_SHORT(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 4, 1, __value)
#define GET_TX_DESC_DATA_SHORT(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x14, 4, 1)
#define SET_TX_DESC_DATA_SC(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x14, 0, 4, __value)
#define GET_TX_DESC_DATA_SC(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x14, 0, 4)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 30, 2, __value)
#define GET_TX_DESC_ANTSEL_D(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 30, 2)
#define SET_TX_DESC_ANT_MAPD(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 28, 2, __value)
#define GET_TX_DESC_ANT_MAPD(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 28, 2)
#define SET_TX_DESC_ANT_MAPC(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 26, 2, __value)
#define GET_TX_DESC_ANT_MAPC(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 26, 2)
#define SET_TX_DESC_ANT_MAPB(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 24, 2, __value)
#define GET_TX_DESC_ANT_MAPB(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 24, 2)
#define SET_TX_DESC_ANT_MAPA(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 22, 2, __value)
#define GET_TX_DESC_ANT_MAPA(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 22, 2)
#define SET_TX_DESC_ANTSEL_C(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 20, 2, __value)
#define GET_TX_DESC_ANTSEL_C(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 20, 2)
#define SET_TX_DESC_ANTSEL_B(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 18, 2, __value)
#define GET_TX_DESC_ANTSEL_B(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 18, 2)

#define SET_TX_DESC_ANTSEL_A(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 16, 2, __value)
#define GET_TX_DESC_ANTSEL_A(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 16, 2)
#define SET_TX_DESC_MBSSID(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 12, 4, __value)
#define GET_TX_DESC_MBSSID(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x18, 12, 4)
#ifdef SET_TX_DESC_SW_DEFINE
#undef SET_TX_DESC_SW_DEFINE
#endif
#define SET_TX_DESC_SW_DEFINE(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x18, 0, 12, __value)
#define GET_TX_DESC_SW_DEFINE(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x18, 0, 12)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM(__tx_desc, __value)                          \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x1C, 24, 8, __value)
#define GET_TX_DESC_DMA_TXAGG_NUM(__tx_desc)                                   \
	LE_BITS_TO_4BYTE(__tx_desc + 0x1C, 24, 8)

#define SET_TX_DESC_FINAL_DATA_RATE(__tx_desc, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x1C, 24, 8, __value)
#define GET_TX_DESC_FINAL_DATA_RATE(__tx_desc)                                 \
	LE_BITS_TO_4BYTE(__tx_desc + 0x1C, 24, 8)
#define SET_TX_DESC_NTX_MAP(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x1C, 20, 4, __value)
#define GET_TX_DESC_NTX_MAP(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x1C, 20, 4)

#define SET_TX_DESC_TX_BUFF_SIZE(__tx_desc, __value)                           \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x1C, 0, 16, __value)
#define GET_TX_DESC_TX_BUFF_SIZE(__tx_desc)                                    \
	LE_BITS_TO_4BYTE(__tx_desc + 0x1C, 0, 16)
#define SET_TX_DESC_TXDESC_CHECKSUM(__tx_desc, __value)                        \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x1C, 0, 16, __value)
#define GET_TX_DESC_TXDESC_CHECKSUM(__tx_desc)                                 \
	LE_BITS_TO_4BYTE(__tx_desc + 0x1C, 0, 16)
#define SET_TX_DESC_TIMESTAMP(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x1C, 0, 16, __value)
#define GET_TX_DESC_TIMESTAMP(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x1C, 0, 16)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 31, 1, __value)
#define GET_TX_DESC_TXWIFI_CP(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 31, 1)
#define SET_TX_DESC_MAC_CP(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 30, 1, __value)
#define GET_TX_DESC_MAC_CP(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x20, 30, 1)
#define SET_TX_DESC_STW_PKTRE_DIS(__tx_desc, __value)                          \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 29, 1, __value)
#define GET_TX_DESC_STW_PKTRE_DIS(__tx_desc)                                   \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 29, 1)
#define SET_TX_DESC_STW_RB_DIS(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 28, 1, __value)
#define GET_TX_DESC_STW_RB_DIS(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 28, 1)
#define SET_TX_DESC_STW_RATE_DIS(__tx_desc, __value)                           \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 27, 1, __value)
#define GET_TX_DESC_STW_RATE_DIS(__tx_desc)                                    \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 27, 1)
#define SET_TX_DESC_STW_ANT_DIS(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 26, 1, __value)
#define GET_TX_DESC_STW_ANT_DIS(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 26, 1)
#define SET_TX_DESC_STW_EN(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 25, 1, __value)
#define GET_TX_DESC_STW_EN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x20, 25, 1)
#define SET_TX_DESC_SMH_EN(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 24, 1, __value)
#define GET_TX_DESC_SMH_EN(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x20, 24, 1)

#define SET_TX_DESC_TAILPAGE_L(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 24, 8, __value)
#define GET_TX_DESC_TAILPAGE_L(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 24, 8)

#define SET_TX_DESC_SDIO_DMASEQ(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 16, 8, __value)
#define GET_TX_DESC_SDIO_DMASEQ(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 16, 8)

#define SET_TX_DESC_NEXTHEADPAGE_L(__tx_desc, __value)                         \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 16, 8, __value)
#define GET_TX_DESC_NEXTHEADPAGE_L(__tx_desc)                                  \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 16, 8)
#define SET_TX_DESC_EN_HWSEQ(__tx_desc, __value)                               \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 15, 1, __value)
#define GET_TX_DESC_EN_HWSEQ(__tx_desc)                                        \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 15, 1)

#define SET_TX_DESC_EN_HWEXSEQ(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 14, 1, __value)
#define GET_TX_DESC_EN_HWEXSEQ(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 14, 1)

#define SET_TX_DESC_DATA_RC(__tx_desc, __value)                                \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 8, 6, __value)
#define GET_TX_DESC_DATA_RC(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x20, 8, 6)
#define SET_TX_DESC_BAR_RTY_TH(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 6, 2, __value)
#define GET_TX_DESC_BAR_RTY_TH(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x20, 6, 2)
#define SET_TX_DESC_RTS_RC(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x20, 0, 6, __value)
#define GET_TX_DESC_RTS_RC(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x20, 0, 6)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H(__tx_desc, __value)                             \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x24, 28, 4, __value)
#define GET_TX_DESC_TAILPAGE_H(__tx_desc)                                      \
	LE_BITS_TO_4BYTE(__tx_desc + 0x24, 28, 4)
#define SET_TX_DESC_NEXTHEADPAGE_H(__tx_desc, __value)                         \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x24, 24, 4, __value)
#define GET_TX_DESC_NEXTHEADPAGE_H(__tx_desc)                                  \
	LE_BITS_TO_4BYTE(__tx_desc + 0x24, 24, 4)

#define SET_TX_DESC_SW_SEQ(__tx_desc, __value)                                 \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x24, 12, 12, __value)
#define GET_TX_DESC_SW_SEQ(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x24, 12, 12)
#define SET_TX_DESC_TXBF_PATH(__tx_desc, __value)                              \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x24, 11, 1, __value)
#define GET_TX_DESC_TXBF_PATH(__tx_desc)                                       \
	LE_BITS_TO_4BYTE(__tx_desc + 0x24, 11, 1)
#define SET_TX_DESC_PADDING_LEN(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x24, 0, 11, __value)
#define GET_TX_DESC_PADDING_LEN(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x24, 0, 11)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET(__tx_desc, __value)                    \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x24, 0, 8, __value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET(__tx_desc)                             \
	LE_BITS_TO_4BYTE(__tx_desc + 0x24, 0, 8)

/*WORD10*/

#define SET_TX_DESC_MU_DATARATE(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x28, 8, 8, __value)
#define GET_TX_DESC_MU_DATARATE(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x28, 8, 8)
#define SET_TX_DESC_MU_RC(__tx_desc, __value)                                  \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x28, 4, 4, __value)
#define GET_TX_DESC_MU_RC(__tx_desc) LE_BITS_TO_4BYTE(__tx_desc + 0x28, 4, 4)
#define SET_TX_DESC_SND_PKT_SEL(__tx_desc, __value)                            \
	SET_BITS_TO_LE_4BYTE(__tx_desc + 0x28, 0, 2, __value)
#define GET_TX_DESC_SND_PKT_SEL(__tx_desc)                                     \
	LE_BITS_TO_4BYTE(__tx_desc + 0x28, 0, 2)

#endif

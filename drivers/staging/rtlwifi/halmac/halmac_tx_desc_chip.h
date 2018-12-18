/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_TX_DESC_CHIP_H_
#define _HALMAC_TX_DESC_CHIP_H_

/*TXDESC_WORD0*/

#define SET_TX_DESC_DISQSELSEQ_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_DISQSELSEQ(__tx_desc, __value)
#define GET_TX_DESC_DISQSELSEQ_8822B(__tx_desc)                                \
	GET_TX_DESC_DISQSELSEQ(__tx_desc)
#define SET_TX_DESC_GF_8822B(__tx_desc, __value)                               \
	SET_TX_DESC_GF(__tx_desc, __value)
#define GET_TX_DESC_GF_8822B(__tx_desc) GET_TX_DESC_GF(__tx_desc)
#define SET_TX_DESC_NO_ACM_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_NO_ACM(__tx_desc, __value)
#define GET_TX_DESC_NO_ACM_8822B(__tx_desc) GET_TX_DESC_NO_ACM(__tx_desc)
#define SET_TX_DESC_BCNPKT_TSF_CTRL_8822B(__tx_desc, __value)                  \
	SET_TX_DESC_BCNPKT_TSF_CTRL(__tx_desc, __value)
#define GET_TX_DESC_BCNPKT_TSF_CTRL_8822B(__tx_desc)                           \
	GET_TX_DESC_BCNPKT_TSF_CTRL(__tx_desc)
#define SET_TX_DESC_AMSDU_PAD_EN_8822B(__tx_desc, __value)                     \
	SET_TX_DESC_AMSDU_PAD_EN(__tx_desc, __value)
#define GET_TX_DESC_AMSDU_PAD_EN_8822B(__tx_desc)                              \
	GET_TX_DESC_AMSDU_PAD_EN(__tx_desc)
#define SET_TX_DESC_LS_8822B(__tx_desc, __value)                               \
	SET_TX_DESC_LS(__tx_desc, __value)
#define GET_TX_DESC_LS_8822B(__tx_desc) GET_TX_DESC_LS(__tx_desc)
#define SET_TX_DESC_HTC_8822B(__tx_desc, __value)                              \
	SET_TX_DESC_HTC(__tx_desc, __value)
#define GET_TX_DESC_HTC_8822B(__tx_desc) GET_TX_DESC_HTC(__tx_desc)
#define SET_TX_DESC_BMC_8822B(__tx_desc, __value)                              \
	SET_TX_DESC_BMC(__tx_desc, __value)
#define GET_TX_DESC_BMC_8822B(__tx_desc) GET_TX_DESC_BMC(__tx_desc)
#define SET_TX_DESC_OFFSET_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_OFFSET(__tx_desc, __value)
#define GET_TX_DESC_OFFSET_8822B(__tx_desc) GET_TX_DESC_OFFSET(__tx_desc)
#define SET_TX_DESC_TXPKTSIZE_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_TXPKTSIZE(__tx_desc, __value)
#define GET_TX_DESC_TXPKTSIZE_8822B(__tx_desc) GET_TX_DESC_TXPKTSIZE(__tx_desc)

/*TXDESC_WORD1*/

#define SET_TX_DESC_MOREDATA_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_MOREDATA(__tx_desc, __value)
#define GET_TX_DESC_MOREDATA_8822B(__tx_desc) GET_TX_DESC_MOREDATA(__tx_desc)
#define SET_TX_DESC_PKT_OFFSET_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_PKT_OFFSET(__tx_desc, __value)
#define GET_TX_DESC_PKT_OFFSET_8822B(__tx_desc)                                \
	GET_TX_DESC_PKT_OFFSET(__tx_desc)
#define SET_TX_DESC_SEC_TYPE_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_SEC_TYPE(__tx_desc, __value)
#define GET_TX_DESC_SEC_TYPE_8822B(__tx_desc) GET_TX_DESC_SEC_TYPE(__tx_desc)
#define SET_TX_DESC_EN_DESC_ID_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_EN_DESC_ID(__tx_desc, __value)
#define GET_TX_DESC_EN_DESC_ID_8822B(__tx_desc)                                \
	GET_TX_DESC_EN_DESC_ID(__tx_desc)
#define SET_TX_DESC_RATE_ID_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_RATE_ID(__tx_desc, __value)
#define GET_TX_DESC_RATE_ID_8822B(__tx_desc) GET_TX_DESC_RATE_ID(__tx_desc)
#define SET_TX_DESC_PIFS_8822B(__tx_desc, __value)                             \
	SET_TX_DESC_PIFS(__tx_desc, __value)
#define GET_TX_DESC_PIFS_8822B(__tx_desc) GET_TX_DESC_PIFS(__tx_desc)
#define SET_TX_DESC_LSIG_TXOP_EN_8822B(__tx_desc, __value)                     \
	SET_TX_DESC_LSIG_TXOP_EN(__tx_desc, __value)
#define GET_TX_DESC_LSIG_TXOP_EN_8822B(__tx_desc)                              \
	GET_TX_DESC_LSIG_TXOP_EN(__tx_desc)
#define SET_TX_DESC_RD_NAV_EXT_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_RD_NAV_EXT(__tx_desc, __value)
#define GET_TX_DESC_RD_NAV_EXT_8822B(__tx_desc)                                \
	GET_TX_DESC_RD_NAV_EXT(__tx_desc)
#define SET_TX_DESC_QSEL_8822B(__tx_desc, __value)                             \
	SET_TX_DESC_QSEL(__tx_desc, __value)
#define GET_TX_DESC_QSEL_8822B(__tx_desc) GET_TX_DESC_QSEL(__tx_desc)
#define SET_TX_DESC_MACID_8822B(__tx_desc, __value)                            \
	SET_TX_DESC_MACID(__tx_desc, __value)
#define GET_TX_DESC_MACID_8822B(__tx_desc) GET_TX_DESC_MACID(__tx_desc)

/*TXDESC_WORD2*/

#define SET_TX_DESC_HW_AES_IV_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_HW_AES_IV(__tx_desc, __value)
#define GET_TX_DESC_HW_AES_IV_8822B(__tx_desc) GET_TX_DESC_HW_AES_IV(__tx_desc)
#define SET_TX_DESC_FTM_EN_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_FTM_EN(__tx_desc, __value)
#define GET_TX_DESC_FTM_EN_8822B(__tx_desc) GET_TX_DESC_FTM_EN(__tx_desc)
#define SET_TX_DESC_G_ID_8822B(__tx_desc, __value)                             \
	SET_TX_DESC_G_ID(__tx_desc, __value)
#define GET_TX_DESC_G_ID_8822B(__tx_desc) GET_TX_DESC_G_ID(__tx_desc)
#define SET_TX_DESC_BT_NULL_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_BT_NULL(__tx_desc, __value)
#define GET_TX_DESC_BT_NULL_8822B(__tx_desc) GET_TX_DESC_BT_NULL(__tx_desc)
#define SET_TX_DESC_AMPDU_DENSITY_8822B(__tx_desc, __value)                    \
	SET_TX_DESC_AMPDU_DENSITY(__tx_desc, __value)
#define GET_TX_DESC_AMPDU_DENSITY_8822B(__tx_desc)                             \
	GET_TX_DESC_AMPDU_DENSITY(__tx_desc)
#define SET_TX_DESC_SPE_RPT_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_SPE_RPT(__tx_desc, __value)
#define GET_TX_DESC_SPE_RPT_8822B(__tx_desc) GET_TX_DESC_SPE_RPT(__tx_desc)
#define SET_TX_DESC_RAW_8822B(__tx_desc, __value)                              \
	SET_TX_DESC_RAW(__tx_desc, __value)
#define GET_TX_DESC_RAW_8822B(__tx_desc) GET_TX_DESC_RAW(__tx_desc)
#define SET_TX_DESC_MOREFRAG_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_MOREFRAG(__tx_desc, __value)
#define GET_TX_DESC_MOREFRAG_8822B(__tx_desc) GET_TX_DESC_MOREFRAG(__tx_desc)
#define SET_TX_DESC_BK_8822B(__tx_desc, __value)                               \
	SET_TX_DESC_BK(__tx_desc, __value)
#define GET_TX_DESC_BK_8822B(__tx_desc) GET_TX_DESC_BK(__tx_desc)
#define SET_TX_DESC_NULL_1_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_NULL_1(__tx_desc, __value)
#define GET_TX_DESC_NULL_1_8822B(__tx_desc) GET_TX_DESC_NULL_1(__tx_desc)
#define SET_TX_DESC_NULL_0_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_NULL_0(__tx_desc, __value)
#define GET_TX_DESC_NULL_0_8822B(__tx_desc) GET_TX_DESC_NULL_0(__tx_desc)
#define SET_TX_DESC_RDG_EN_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_RDG_EN(__tx_desc, __value)
#define GET_TX_DESC_RDG_EN_8822B(__tx_desc) GET_TX_DESC_RDG_EN(__tx_desc)
#define SET_TX_DESC_AGG_EN_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_AGG_EN(__tx_desc, __value)
#define GET_TX_DESC_AGG_EN_8822B(__tx_desc) GET_TX_DESC_AGG_EN(__tx_desc)
#define SET_TX_DESC_CCA_RTS_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_CCA_RTS(__tx_desc, __value)
#define GET_TX_DESC_CCA_RTS_8822B(__tx_desc) GET_TX_DESC_CCA_RTS(__tx_desc)
#define SET_TX_DESC_TRI_FRAME_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_TRI_FRAME(__tx_desc, __value)
#define GET_TX_DESC_TRI_FRAME_8822B(__tx_desc) GET_TX_DESC_TRI_FRAME(__tx_desc)
#define SET_TX_DESC_P_AID_8822B(__tx_desc, __value)                            \
	SET_TX_DESC_P_AID(__tx_desc, __value)
#define GET_TX_DESC_P_AID_8822B(__tx_desc) GET_TX_DESC_P_AID(__tx_desc)

/*TXDESC_WORD3*/

#define SET_TX_DESC_AMPDU_MAX_TIME_8822B(__tx_desc, __value)                   \
	SET_TX_DESC_AMPDU_MAX_TIME(__tx_desc, __value)
#define GET_TX_DESC_AMPDU_MAX_TIME_8822B(__tx_desc)                            \
	GET_TX_DESC_AMPDU_MAX_TIME(__tx_desc)
#define SET_TX_DESC_NDPA_8822B(__tx_desc, __value)                             \
	SET_TX_DESC_NDPA(__tx_desc, __value)
#define GET_TX_DESC_NDPA_8822B(__tx_desc) GET_TX_DESC_NDPA(__tx_desc)
#define SET_TX_DESC_MAX_AGG_NUM_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_MAX_AGG_NUM(__tx_desc, __value)
#define GET_TX_DESC_MAX_AGG_NUM_8822B(__tx_desc)                               \
	GET_TX_DESC_MAX_AGG_NUM(__tx_desc)
#define SET_TX_DESC_USE_MAX_TIME_EN_8822B(__tx_desc, __value)                  \
	SET_TX_DESC_USE_MAX_TIME_EN(__tx_desc, __value)
#define GET_TX_DESC_USE_MAX_TIME_EN_8822B(__tx_desc)                           \
	GET_TX_DESC_USE_MAX_TIME_EN(__tx_desc)
#define SET_TX_DESC_NAVUSEHDR_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_NAVUSEHDR(__tx_desc, __value)
#define GET_TX_DESC_NAVUSEHDR_8822B(__tx_desc) GET_TX_DESC_NAVUSEHDR(__tx_desc)
#define SET_TX_DESC_CHK_EN_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_CHK_EN(__tx_desc, __value)
#define GET_TX_DESC_CHK_EN_8822B(__tx_desc) GET_TX_DESC_CHK_EN(__tx_desc)
#define SET_TX_DESC_HW_RTS_EN_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_HW_RTS_EN(__tx_desc, __value)
#define GET_TX_DESC_HW_RTS_EN_8822B(__tx_desc) GET_TX_DESC_HW_RTS_EN(__tx_desc)
#define SET_TX_DESC_RTSEN_8822B(__tx_desc, __value)                            \
	SET_TX_DESC_RTSEN(__tx_desc, __value)
#define GET_TX_DESC_RTSEN_8822B(__tx_desc) GET_TX_DESC_RTSEN(__tx_desc)
#define SET_TX_DESC_CTS2SELF_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_CTS2SELF(__tx_desc, __value)
#define GET_TX_DESC_CTS2SELF_8822B(__tx_desc) GET_TX_DESC_CTS2SELF(__tx_desc)
#define SET_TX_DESC_DISDATAFB_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_DISDATAFB(__tx_desc, __value)
#define GET_TX_DESC_DISDATAFB_8822B(__tx_desc) GET_TX_DESC_DISDATAFB(__tx_desc)
#define SET_TX_DESC_DISRTSFB_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_DISRTSFB(__tx_desc, __value)
#define GET_TX_DESC_DISRTSFB_8822B(__tx_desc) GET_TX_DESC_DISRTSFB(__tx_desc)
#define SET_TX_DESC_USE_RATE_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_USE_RATE(__tx_desc, __value)
#define GET_TX_DESC_USE_RATE_8822B(__tx_desc) GET_TX_DESC_USE_RATE(__tx_desc)
#define SET_TX_DESC_HW_SSN_SEL_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_HW_SSN_SEL(__tx_desc, __value)
#define GET_TX_DESC_HW_SSN_SEL_8822B(__tx_desc)                                \
	GET_TX_DESC_HW_SSN_SEL(__tx_desc)
#define SET_TX_DESC_WHEADER_LEN_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_WHEADER_LEN(__tx_desc, __value)
#define GET_TX_DESC_WHEADER_LEN_8822B(__tx_desc)                               \
	GET_TX_DESC_WHEADER_LEN(__tx_desc)

/*TXDESC_WORD4*/

#define SET_TX_DESC_PCTS_MASK_IDX_8822B(__tx_desc, __value)                    \
	SET_TX_DESC_PCTS_MASK_IDX(__tx_desc, __value)
#define GET_TX_DESC_PCTS_MASK_IDX_8822B(__tx_desc)                             \
	GET_TX_DESC_PCTS_MASK_IDX(__tx_desc)
#define SET_TX_DESC_PCTS_EN_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_PCTS_EN(__tx_desc, __value)
#define GET_TX_DESC_PCTS_EN_8822B(__tx_desc) GET_TX_DESC_PCTS_EN(__tx_desc)
#define SET_TX_DESC_RTSRATE_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_RTSRATE(__tx_desc, __value)
#define GET_TX_DESC_RTSRATE_8822B(__tx_desc) GET_TX_DESC_RTSRATE(__tx_desc)
#define SET_TX_DESC_RTS_DATA_RTY_LMT_8822B(__tx_desc, __value)                 \
	SET_TX_DESC_RTS_DATA_RTY_LMT(__tx_desc, __value)
#define GET_TX_DESC_RTS_DATA_RTY_LMT_8822B(__tx_desc)                          \
	GET_TX_DESC_RTS_DATA_RTY_LMT(__tx_desc)
#define SET_TX_DESC_RTY_LMT_EN_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_RTY_LMT_EN(__tx_desc, __value)
#define GET_TX_DESC_RTY_LMT_EN_8822B(__tx_desc)                                \
	GET_TX_DESC_RTY_LMT_EN(__tx_desc)
#define SET_TX_DESC_RTS_RTY_LOWEST_RATE_8822B(__tx_desc, __value)              \
	SET_TX_DESC_RTS_RTY_LOWEST_RATE(__tx_desc, __value)
#define GET_TX_DESC_RTS_RTY_LOWEST_RATE_8822B(__tx_desc)                       \
	GET_TX_DESC_RTS_RTY_LOWEST_RATE(__tx_desc)
#define SET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(__tx_desc, __value)             \
	SET_TX_DESC_DATA_RTY_LOWEST_RATE(__tx_desc, __value)
#define GET_TX_DESC_DATA_RTY_LOWEST_RATE_8822B(__tx_desc)                      \
	GET_TX_DESC_DATA_RTY_LOWEST_RATE(__tx_desc)
#define SET_TX_DESC_TRY_RATE_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_TRY_RATE(__tx_desc, __value)
#define GET_TX_DESC_TRY_RATE_8822B(__tx_desc) GET_TX_DESC_TRY_RATE(__tx_desc)
#define SET_TX_DESC_DATARATE_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_DATARATE(__tx_desc, __value)
#define GET_TX_DESC_DATARATE_8822B(__tx_desc) GET_TX_DESC_DATARATE(__tx_desc)

/*TXDESC_WORD5*/

#define SET_TX_DESC_POLLUTED_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_POLLUTED(__tx_desc, __value)
#define GET_TX_DESC_POLLUTED_8822B(__tx_desc) GET_TX_DESC_POLLUTED(__tx_desc)
#define SET_TX_DESC_TXPWR_OFSET_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_TXPWR_OFSET(__tx_desc, __value)
#define GET_TX_DESC_TXPWR_OFSET_8822B(__tx_desc)                               \
	GET_TX_DESC_TXPWR_OFSET(__tx_desc)
#define SET_TX_DESC_TX_ANT_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_TX_ANT(__tx_desc, __value)
#define GET_TX_DESC_TX_ANT_8822B(__tx_desc) GET_TX_DESC_TX_ANT(__tx_desc)
#define SET_TX_DESC_PORT_ID_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_PORT_ID(__tx_desc, __value)
#define GET_TX_DESC_PORT_ID_8822B(__tx_desc) GET_TX_DESC_PORT_ID(__tx_desc)
#define SET_TX_DESC_MULTIPLE_PORT_8822B(__tx_desc, __value)                    \
	SET_TX_DESC_MULTIPLE_PORT(__tx_desc, __value)
#define GET_TX_DESC_MULTIPLE_PORT_8822B(__tx_desc)                             \
	GET_TX_DESC_MULTIPLE_PORT(__tx_desc)
#define SET_TX_DESC_SIGNALING_TAPKT_EN_8822B(__tx_desc, __value)               \
	SET_TX_DESC_SIGNALING_TAPKT_EN(__tx_desc, __value)
#define GET_TX_DESC_SIGNALING_TAPKT_EN_8822B(__tx_desc)                        \
	GET_TX_DESC_SIGNALING_TAPKT_EN(__tx_desc)
#define SET_TX_DESC_RTS_SC_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_RTS_SC(__tx_desc, __value)
#define GET_TX_DESC_RTS_SC_8822B(__tx_desc) GET_TX_DESC_RTS_SC(__tx_desc)
#define SET_TX_DESC_RTS_SHORT_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_RTS_SHORT(__tx_desc, __value)
#define GET_TX_DESC_RTS_SHORT_8822B(__tx_desc) GET_TX_DESC_RTS_SHORT(__tx_desc)
#define SET_TX_DESC_VCS_STBC_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_VCS_STBC(__tx_desc, __value)
#define GET_TX_DESC_VCS_STBC_8822B(__tx_desc) GET_TX_DESC_VCS_STBC(__tx_desc)
#define SET_TX_DESC_DATA_STBC_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_DATA_STBC(__tx_desc, __value)
#define GET_TX_DESC_DATA_STBC_8822B(__tx_desc) GET_TX_DESC_DATA_STBC(__tx_desc)
#define SET_TX_DESC_DATA_LDPC_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_DATA_LDPC(__tx_desc, __value)
#define GET_TX_DESC_DATA_LDPC_8822B(__tx_desc) GET_TX_DESC_DATA_LDPC(__tx_desc)
#define SET_TX_DESC_DATA_BW_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_DATA_BW(__tx_desc, __value)
#define GET_TX_DESC_DATA_BW_8822B(__tx_desc) GET_TX_DESC_DATA_BW(__tx_desc)
#define SET_TX_DESC_DATA_SHORT_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_DATA_SHORT(__tx_desc, __value)
#define GET_TX_DESC_DATA_SHORT_8822B(__tx_desc)                                \
	GET_TX_DESC_DATA_SHORT(__tx_desc)
#define SET_TX_DESC_DATA_SC_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_DATA_SC(__tx_desc, __value)
#define GET_TX_DESC_DATA_SC_8822B(__tx_desc) GET_TX_DESC_DATA_SC(__tx_desc)

/*TXDESC_WORD6*/

#define SET_TX_DESC_ANTSEL_D_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANTSEL_D(__tx_desc, __value)
#define GET_TX_DESC_ANTSEL_D_8822B(__tx_desc) GET_TX_DESC_ANTSEL_D(__tx_desc)
#define SET_TX_DESC_ANT_MAPD_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANT_MAPD(__tx_desc, __value)
#define GET_TX_DESC_ANT_MAPD_8822B(__tx_desc) GET_TX_DESC_ANT_MAPD(__tx_desc)
#define SET_TX_DESC_ANT_MAPC_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANT_MAPC(__tx_desc, __value)
#define GET_TX_DESC_ANT_MAPC_8822B(__tx_desc) GET_TX_DESC_ANT_MAPC(__tx_desc)
#define SET_TX_DESC_ANT_MAPB_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANT_MAPB(__tx_desc, __value)
#define GET_TX_DESC_ANT_MAPB_8822B(__tx_desc) GET_TX_DESC_ANT_MAPB(__tx_desc)
#define SET_TX_DESC_ANT_MAPA_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANT_MAPA(__tx_desc, __value)
#define GET_TX_DESC_ANT_MAPA_8822B(__tx_desc) GET_TX_DESC_ANT_MAPA(__tx_desc)
#define SET_TX_DESC_ANTSEL_C_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANTSEL_C(__tx_desc, __value)
#define GET_TX_DESC_ANTSEL_C_8822B(__tx_desc) GET_TX_DESC_ANTSEL_C(__tx_desc)
#define SET_TX_DESC_ANTSEL_B_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANTSEL_B(__tx_desc, __value)
#define GET_TX_DESC_ANTSEL_B_8822B(__tx_desc) GET_TX_DESC_ANTSEL_B(__tx_desc)
#define SET_TX_DESC_ANTSEL_A_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_ANTSEL_A(__tx_desc, __value)
#define GET_TX_DESC_ANTSEL_A_8822B(__tx_desc) GET_TX_DESC_ANTSEL_A(__tx_desc)
#define SET_TX_DESC_MBSSID_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_MBSSID(__tx_desc, __value)
#define GET_TX_DESC_MBSSID_8822B(__tx_desc) GET_TX_DESC_MBSSID(__tx_desc)
#define SET_TX_DESC_SW_DEFINE_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_SW_DEFINE(__tx_desc, __value)
#define GET_TX_DESC_SW_DEFINE_8822B(__tx_desc) GET_TX_DESC_SW_DEFINE(__tx_desc)

/*TXDESC_WORD7*/

#define SET_TX_DESC_DMA_TXAGG_NUM_8822B(__tx_desc, __value)                    \
	SET_TX_DESC_DMA_TXAGG_NUM(__tx_desc, __value)
#define GET_TX_DESC_DMA_TXAGG_NUM_8822B(__tx_desc)                             \
	GET_TX_DESC_DMA_TXAGG_NUM(__tx_desc)
#define SET_TX_DESC_FINAL_DATA_RATE_8822B(__tx_desc, __value)                  \
	SET_TX_DESC_FINAL_DATA_RATE(__tx_desc, __value)
#define GET_TX_DESC_FINAL_DATA_RATE_8822B(__tx_desc)                           \
	GET_TX_DESC_FINAL_DATA_RATE(__tx_desc)
#define SET_TX_DESC_NTX_MAP_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_NTX_MAP(__tx_desc, __value)
#define GET_TX_DESC_NTX_MAP_8822B(__tx_desc) GET_TX_DESC_NTX_MAP(__tx_desc)
#define SET_TX_DESC_TX_BUFF_SIZE_8822B(__tx_desc, __value)                     \
	SET_TX_DESC_TX_BUFF_SIZE(__tx_desc, __value)
#define GET_TX_DESC_TX_BUFF_SIZE_8822B(__tx_desc)                              \
	GET_TX_DESC_TX_BUFF_SIZE(__tx_desc)
#define SET_TX_DESC_TXDESC_CHECKSUM_8822B(__tx_desc, __value)                  \
	SET_TX_DESC_TXDESC_CHECKSUM(__tx_desc, __value)
#define GET_TX_DESC_TXDESC_CHECKSUM_8822B(__tx_desc)                           \
	GET_TX_DESC_TXDESC_CHECKSUM(__tx_desc)
#define SET_TX_DESC_TIMESTAMP_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_TIMESTAMP(__tx_desc, __value)
#define GET_TX_DESC_TIMESTAMP_8822B(__tx_desc) GET_TX_DESC_TIMESTAMP(__tx_desc)

/*TXDESC_WORD8*/

#define SET_TX_DESC_TXWIFI_CP_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_TXWIFI_CP(__tx_desc, __value)
#define GET_TX_DESC_TXWIFI_CP_8822B(__tx_desc) GET_TX_DESC_TXWIFI_CP(__tx_desc)
#define SET_TX_DESC_MAC_CP_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_MAC_CP(__tx_desc, __value)
#define GET_TX_DESC_MAC_CP_8822B(__tx_desc) GET_TX_DESC_MAC_CP(__tx_desc)
#define SET_TX_DESC_STW_PKTRE_DIS_8822B(__tx_desc, __value)                    \
	SET_TX_DESC_STW_PKTRE_DIS(__tx_desc, __value)
#define GET_TX_DESC_STW_PKTRE_DIS_8822B(__tx_desc)                             \
	GET_TX_DESC_STW_PKTRE_DIS(__tx_desc)
#define SET_TX_DESC_STW_RB_DIS_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_STW_RB_DIS(__tx_desc, __value)
#define GET_TX_DESC_STW_RB_DIS_8822B(__tx_desc)                                \
	GET_TX_DESC_STW_RB_DIS(__tx_desc)
#define SET_TX_DESC_STW_RATE_DIS_8822B(__tx_desc, __value)                     \
	SET_TX_DESC_STW_RATE_DIS(__tx_desc, __value)
#define GET_TX_DESC_STW_RATE_DIS_8822B(__tx_desc)                              \
	GET_TX_DESC_STW_RATE_DIS(__tx_desc)
#define SET_TX_DESC_STW_ANT_DIS_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_STW_ANT_DIS(__tx_desc, __value)
#define GET_TX_DESC_STW_ANT_DIS_8822B(__tx_desc)                               \
	GET_TX_DESC_STW_ANT_DIS(__tx_desc)
#define SET_TX_DESC_STW_EN_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_STW_EN(__tx_desc, __value)
#define GET_TX_DESC_STW_EN_8822B(__tx_desc) GET_TX_DESC_STW_EN(__tx_desc)
#define SET_TX_DESC_SMH_EN_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_SMH_EN(__tx_desc, __value)
#define GET_TX_DESC_SMH_EN_8822B(__tx_desc) GET_TX_DESC_SMH_EN(__tx_desc)
#define SET_TX_DESC_TAILPAGE_L_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_TAILPAGE_L(__tx_desc, __value)
#define GET_TX_DESC_TAILPAGE_L_8822B(__tx_desc)                                \
	GET_TX_DESC_TAILPAGE_L(__tx_desc)
#define SET_TX_DESC_SDIO_DMASEQ_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_SDIO_DMASEQ(__tx_desc, __value)
#define GET_TX_DESC_SDIO_DMASEQ_8822B(__tx_desc)                               \
	GET_TX_DESC_SDIO_DMASEQ(__tx_desc)
#define SET_TX_DESC_NEXTHEADPAGE_L_8822B(__tx_desc, __value)                   \
	SET_TX_DESC_NEXTHEADPAGE_L(__tx_desc, __value)
#define GET_TX_DESC_NEXTHEADPAGE_L_8822B(__tx_desc)                            \
	GET_TX_DESC_NEXTHEADPAGE_L(__tx_desc)
#define SET_TX_DESC_EN_HWSEQ_8822B(__tx_desc, __value)                         \
	SET_TX_DESC_EN_HWSEQ(__tx_desc, __value)
#define GET_TX_DESC_EN_HWSEQ_8822B(__tx_desc) GET_TX_DESC_EN_HWSEQ(__tx_desc)
#define SET_TX_DESC_EN_HWEXSEQ_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_EN_HWEXSEQ(__tx_desc, __value)
#define GET_TX_DESC_EN_HWEXSEQ_8822B(__tx_desc)                                \
	GET_TX_DESC_EN_HWEXSEQ(__tx_desc)
#define SET_TX_DESC_DATA_RC_8822B(__tx_desc, __value)                          \
	SET_TX_DESC_DATA_RC(__tx_desc, __value)
#define GET_TX_DESC_DATA_RC_8822B(__tx_desc) GET_TX_DESC_DATA_RC(__tx_desc)
#define SET_TX_DESC_BAR_RTY_TH_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_BAR_RTY_TH(__tx_desc, __value)
#define GET_TX_DESC_BAR_RTY_TH_8822B(__tx_desc)                                \
	GET_TX_DESC_BAR_RTY_TH(__tx_desc)
#define SET_TX_DESC_RTS_RC_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_RTS_RC(__tx_desc, __value)
#define GET_TX_DESC_RTS_RC_8822B(__tx_desc) GET_TX_DESC_RTS_RC(__tx_desc)

/*TXDESC_WORD9*/

#define SET_TX_DESC_TAILPAGE_H_8822B(__tx_desc, __value)                       \
	SET_TX_DESC_TAILPAGE_H(__tx_desc, __value)
#define GET_TX_DESC_TAILPAGE_H_8822B(__tx_desc)                                \
	GET_TX_DESC_TAILPAGE_H(__tx_desc)
#define SET_TX_DESC_NEXTHEADPAGE_H_8822B(__tx_desc, __value)                   \
	SET_TX_DESC_NEXTHEADPAGE_H(__tx_desc, __value)
#define GET_TX_DESC_NEXTHEADPAGE_H_8822B(__tx_desc)                            \
	GET_TX_DESC_NEXTHEADPAGE_H(__tx_desc)
#define SET_TX_DESC_SW_SEQ_8822B(__tx_desc, __value)                           \
	SET_TX_DESC_SW_SEQ(__tx_desc, __value)
#define GET_TX_DESC_SW_SEQ_8822B(__tx_desc) GET_TX_DESC_SW_SEQ(__tx_desc)
#define SET_TX_DESC_TXBF_PATH_8822B(__tx_desc, __value)                        \
	SET_TX_DESC_TXBF_PATH(__tx_desc, __value)
#define GET_TX_DESC_TXBF_PATH_8822B(__tx_desc) GET_TX_DESC_TXBF_PATH(__tx_desc)
#define SET_TX_DESC_PADDING_LEN_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_PADDING_LEN(__tx_desc, __value)
#define GET_TX_DESC_PADDING_LEN_8822B(__tx_desc)                               \
	GET_TX_DESC_PADDING_LEN(__tx_desc)
#define SET_TX_DESC_GROUP_BIT_IE_OFFSET_8822B(__tx_desc, __value)              \
	SET_TX_DESC_GROUP_BIT_IE_OFFSET(__tx_desc, __value)
#define GET_TX_DESC_GROUP_BIT_IE_OFFSET_8822B(__tx_desc)                       \
	GET_TX_DESC_GROUP_BIT_IE_OFFSET(__tx_desc)

/*WORD10*/

#define SET_TX_DESC_MU_DATARATE_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_MU_DATARATE(__tx_desc, __value)
#define GET_TX_DESC_MU_DATARATE_8822B(__tx_desc)                               \
	GET_TX_DESC_MU_DATARATE(__tx_desc)
#define SET_TX_DESC_MU_RC_8822B(__tx_desc, __value)                            \
	SET_TX_DESC_MU_RC(__tx_desc, __value)
#define GET_TX_DESC_MU_RC_8822B(__tx_desc) GET_TX_DESC_MU_RC(__tx_desc)
#define SET_TX_DESC_SND_PKT_SEL_8822B(__tx_desc, __value)                      \
	SET_TX_DESC_SND_PKT_SEL(__tx_desc, __value)
#define GET_TX_DESC_SND_PKT_SEL_8822B(__tx_desc)                               \
	GET_TX_DESC_SND_PKT_SEL(__tx_desc)

#endif

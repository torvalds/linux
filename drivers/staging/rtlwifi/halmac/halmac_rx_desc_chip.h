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
#ifndef _HALMAC_RX_DESC_CHIP_H_
#define _HALMAC_RX_DESC_CHIP_H_

/*RXDESC_WORD0*/

#define GET_RX_DESC_EOR_8822B(__rx_desc) GET_RX_DESC_EOR(__rx_desc)
#define GET_RX_DESC_PHYPKTIDC_8822B(__rx_desc) GET_RX_DESC_PHYPKTIDC(__rx_desc)
#define GET_RX_DESC_SWDEC_8822B(__rx_desc) GET_RX_DESC_SWDEC(__rx_desc)
#define GET_RX_DESC_PHYST_8822B(__rx_desc) GET_RX_DESC_PHYST(__rx_desc)
#define GET_RX_DESC_SHIFT_8822B(__rx_desc) GET_RX_DESC_SHIFT(__rx_desc)
#define GET_RX_DESC_QOS_8822B(__rx_desc) GET_RX_DESC_QOS(__rx_desc)
#define GET_RX_DESC_SECURITY_8822B(__rx_desc) GET_RX_DESC_SECURITY(__rx_desc)
#define GET_RX_DESC_DRV_INFO_SIZE_8822B(__rx_desc)                             \
	GET_RX_DESC_DRV_INFO_SIZE(__rx_desc)
#define GET_RX_DESC_ICV_ERR_8822B(__rx_desc) GET_RX_DESC_ICV_ERR(__rx_desc)
#define GET_RX_DESC_CRC32_8822B(__rx_desc) GET_RX_DESC_CRC32(__rx_desc)
#define GET_RX_DESC_PKT_LEN_8822B(__rx_desc) GET_RX_DESC_PKT_LEN(__rx_desc)

/*RXDESC_WORD1*/

#define GET_RX_DESC_BC_8822B(__rx_desc) GET_RX_DESC_BC(__rx_desc)
#define GET_RX_DESC_MC_8822B(__rx_desc) GET_RX_DESC_MC(__rx_desc)
#define GET_RX_DESC_TY_PE_8822B(__rx_desc) GET_RX_DESC_TY_PE(__rx_desc)
#define GET_RX_DESC_MF_8822B(__rx_desc) GET_RX_DESC_MF(__rx_desc)
#define GET_RX_DESC_MD_8822B(__rx_desc) GET_RX_DESC_MD(__rx_desc)
#define GET_RX_DESC_PWR_8822B(__rx_desc) GET_RX_DESC_PWR(__rx_desc)
#define GET_RX_DESC_PAM_8822B(__rx_desc) GET_RX_DESC_PAM(__rx_desc)
#define GET_RX_DESC_CHK_VLD_8822B(__rx_desc) GET_RX_DESC_CHK_VLD(__rx_desc)
#define GET_RX_DESC_RX_IS_TCP_UDP_8822B(__rx_desc)                             \
	GET_RX_DESC_RX_IS_TCP_UDP(__rx_desc)
#define GET_RX_DESC_RX_IPV_8822B(__rx_desc) GET_RX_DESC_RX_IPV(__rx_desc)
#define GET_RX_DESC_CHKERR_8822B(__rx_desc) GET_RX_DESC_CHKERR(__rx_desc)
#define GET_RX_DESC_PAGGR_8822B(__rx_desc) GET_RX_DESC_PAGGR(__rx_desc)
#define GET_RX_DESC_RXID_MATCH_8822B(__rx_desc)                                \
	GET_RX_DESC_RXID_MATCH(__rx_desc)
#define GET_RX_DESC_AMSDU_8822B(__rx_desc) GET_RX_DESC_AMSDU(__rx_desc)
#define GET_RX_DESC_MACID_VLD_8822B(__rx_desc) GET_RX_DESC_MACID_VLD(__rx_desc)
#define GET_RX_DESC_TID_8822B(__rx_desc) GET_RX_DESC_TID(__rx_desc)
#define GET_RX_DESC_EXT_SECTYPE_8822B(__rx_desc)                               \
	GET_RX_DESC_EXT_SECTYPE(__rx_desc)
#define GET_RX_DESC_MACID_8822B(__rx_desc) GET_RX_DESC_MACID(__rx_desc)

/*RXDESC_WORD2*/

#define GET_RX_DESC_FCS_OK_8822B(__rx_desc) GET_RX_DESC_FCS_OK(__rx_desc)
#define GET_RX_DESC_PPDU_CNT_8822B(__rx_desc) GET_RX_DESC_PPDU_CNT(__rx_desc)
#define GET_RX_DESC_C2H_8822B(__rx_desc) GET_RX_DESC_C2H(__rx_desc)
#define GET_RX_DESC_HWRSVD_8822B(__rx_desc) GET_RX_DESC_HWRSVD(__rx_desc)
#define GET_RX_DESC_WLANHD_IV_LEN_8822B(__rx_desc)                             \
	GET_RX_DESC_WLANHD_IV_LEN(__rx_desc)
#define GET_RX_DESC_RX_IS_QOS_8822B(__rx_desc) GET_RX_DESC_RX_IS_QOS(__rx_desc)
#define GET_RX_DESC_FRAG_8822B(__rx_desc) GET_RX_DESC_FRAG(__rx_desc)
#define GET_RX_DESC_SEQ_8822B(__rx_desc) GET_RX_DESC_SEQ(__rx_desc)

/*RXDESC_WORD3*/

#define GET_RX_DESC_MAGIC_WAKE_8822B(__rx_desc)                                \
	GET_RX_DESC_MAGIC_WAKE(__rx_desc)
#define GET_RX_DESC_UNICAST_WAKE_8822B(__rx_desc)                              \
	GET_RX_DESC_UNICAST_WAKE(__rx_desc)
#define GET_RX_DESC_PATTERN_MATCH_8822B(__rx_desc)                             \
	GET_RX_DESC_PATTERN_MATCH(__rx_desc)
#define GET_RX_DESC_RXPAYLOAD_MATCH_8822B(__rx_desc)                           \
	GET_RX_DESC_RXPAYLOAD_MATCH(__rx_desc)
#define GET_RX_DESC_RXPAYLOAD_ID_8822B(__rx_desc)                              \
	GET_RX_DESC_RXPAYLOAD_ID(__rx_desc)
#define GET_RX_DESC_DMA_AGG_NUM_8822B(__rx_desc)                               \
	GET_RX_DESC_DMA_AGG_NUM(__rx_desc)
#define GET_RX_DESC_BSSID_FIT_1_0_8822B(__rx_desc)                             \
	GET_RX_DESC_BSSID_FIT_1_0(__rx_desc)
#define GET_RX_DESC_EOSP_8822B(__rx_desc) GET_RX_DESC_EOSP(__rx_desc)
#define GET_RX_DESC_HTC_8822B(__rx_desc) GET_RX_DESC_HTC(__rx_desc)
#define GET_RX_DESC_BSSID_FIT_4_2_8822B(__rx_desc)                             \
	GET_RX_DESC_BSSID_FIT_4_2(__rx_desc)
#define GET_RX_DESC_RX_RATE_8822B(__rx_desc) GET_RX_DESC_RX_RATE(__rx_desc)

/*RXDESC_WORD4*/

#define GET_RX_DESC_A1_FIT_8822B(__rx_desc) GET_RX_DESC_A1_FIT(__rx_desc)
#define GET_RX_DESC_MACID_RPT_BUFF_8822B(__rx_desc)                            \
	GET_RX_DESC_MACID_RPT_BUFF(__rx_desc)
#define GET_RX_DESC_RX_PRE_NDP_VLD_8822B(__rx_desc)                            \
	GET_RX_DESC_RX_PRE_NDP_VLD(__rx_desc)
#define GET_RX_DESC_RX_SCRAMBLER_8822B(__rx_desc)                              \
	GET_RX_DESC_RX_SCRAMBLER(__rx_desc)
#define GET_RX_DESC_RX_EOF_8822B(__rx_desc) GET_RX_DESC_RX_EOF(__rx_desc)
#define GET_RX_DESC_PATTERN_IDX_8822B(__rx_desc)                               \
	GET_RX_DESC_PATTERN_IDX(__rx_desc)

/*RXDESC_WORD5*/

#define GET_RX_DESC_TSFL_8822B(__rx_desc) GET_RX_DESC_TSFL(__rx_desc)

#endif

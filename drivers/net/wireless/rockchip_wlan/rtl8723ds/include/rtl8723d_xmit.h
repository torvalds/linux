/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __RTL8723D_XMIT_H__
#define __RTL8723D_XMIT_H__


#define MAX_TID (15)


#ifndef __INC_HAL8723DDESC_H
#define __INC_HAL8723DDESC_H

#define RX_STATUS_DESC_SIZE_8723D		24
#define RX_DRV_INFO_SIZE_UNIT_8723D 8


/* DWORD 0 */
#define SET_RX_STATUS_DESC_PKT_LEN_8723D(__pRxStatusDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 0, 14, __Value)
#define SET_RX_STATUS_DESC_EOR_8723D(__pRxStatusDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 30, 1, __Value)
#define SET_RX_STATUS_DESC_OWN_8723D(__pRxStatusDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 31, 1, __Value)

#define GET_RX_STATUS_DESC_PKT_LEN_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 0, 14)
#define GET_RX_STATUS_DESC_CRC32_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 14, 1)
#define GET_RX_STATUS_DESC_ICV_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 15, 1)
#define GET_RX_STATUS_DESC_DRVINFO_SIZE_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 16, 4)
#define GET_RX_STATUS_DESC_SECURITY_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 20, 3)
#define GET_RX_STATUS_DESC_QOS_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 23, 1)
#define GET_RX_STATUS_DESC_SHIFT_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 24, 2)
#define GET_RX_STATUS_DESC_PHY_STATUS_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 26, 1)
#define GET_RX_STATUS_DESC_SWDEC_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 27, 1)
#define GET_RX_STATUS_DESC_EOR_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 30, 1)
#define GET_RX_STATUS_DESC_OWN_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc, 31, 1)

/* DWORD 1 */
#define GET_RX_STATUS_DESC_MACID_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 0, 7)
#define GET_RX_STATUS_DESC_TID_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 8, 4)
#define GET_RX_STATUS_DESC_AMSDU_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 13, 1)
#define GET_RX_STATUS_DESC_RXID_MATCH_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 14, 1)
#define GET_RX_STATUS_DESC_PAGGR_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 15, 1)
#define GET_RX_STATUS_DESC_A1_FIT_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 16, 4)
#define GET_RX_STATUS_DESC_CHKERR_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 20, 1)
#define GET_RX_STATUS_DESC_IPVER_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 21, 1)
#define GET_RX_STATUS_DESC_IS_TCPUDP__8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 22, 1)
#define GET_RX_STATUS_DESC_CHK_VLD_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 23, 1)
#define GET_RX_STATUS_DESC_PAM_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 24, 1)
#define GET_RX_STATUS_DESC_PWR_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 25, 1)
#define GET_RX_STATUS_DESC_MORE_DATA_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 26, 1)
#define GET_RX_STATUS_DESC_MORE_FRAG_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 27, 1)
#define GET_RX_STATUS_DESC_TYPE_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 28, 2)
#define GET_RX_STATUS_DESC_MC_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 30, 1)
#define GET_RX_STATUS_DESC_BC_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+4, 31, 1)

/* DWORD 2 */
#define GET_RX_STATUS_DESC_SEQ_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 0, 12)
#define GET_RX_STATUS_DESC_FRAG_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 12, 4)
#define GET_RX_STATUS_DESC_RX_IS_QOS_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 16, 1)
#define GET_RX_STATUS_DESC_WLANHD_IV_LEN_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 18, 6)
#define GET_RX_STATUS_DESC_RPT_SEL_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 28, 1)
#define GET_RX_STATUS_DESC_FCS_OK_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 31, 1)

/* DWORD 3 */
#define GET_RX_STATUS_DESC_RX_RATE_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 0, 7)
#define GET_RX_STATUS_DESC_HTC_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 10, 1)
#define GET_RX_STATUS_DESC_EOSP_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 11, 1)
#define GET_RX_STATUS_DESC_BSSID_FIT_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 12, 2)
#ifdef CONFIG_USB_RX_AGGREGATION
#define GET_RX_STATUS_DESC_USB_AGG_PKTNUM_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 16, 8)
#endif
#define GET_RX_STATUS_DESC_PATTERN_MATCH_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+12, 29, 1)
#define GET_RX_STATUS_DESC_UNICAST_MATCH_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+12, 30, 1)
#define GET_RX_STATUS_DESC_MAGIC_MATCH_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+12, 31, 1)

/* DWORD 6 */
#define GET_RX_STATUS_DESC_MATCH_ID_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+16, 0, 7)

/* DWORD 5 */
#define GET_RX_STATUS_DESC_TSFL_8723D(__pRxStatusDesc) \
	LE_BITS_TO_4BYTE(__pRxStatusDesc+20, 0, 32)

#define GET_RX_STATUS_DESC_BUFF_ADDR_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+24, 0, 32)
#define GET_RX_STATUS_DESC_BUFF_ADDR64_8723D(__pRxDesc) \
	LE_BITS_TO_4BYTE(__pRxDesc+28, 0, 32)

#define SET_RX_STATUS_DESC_BUFF_ADDR_8723D(__pRxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxDesc+24, 0, 32, __Value)


/* Dword 0, rsvd: bit26, bit28 */
#define GET_TX_DESC_OWN_8723D(__pTxDesc)\
	LE_BITS_TO_4BYTE(__pTxDesc, 31, 1)

#define SET_TX_DESC_PKT_SIZE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc, 0, 16, __Value)
#define SET_TX_DESC_OFFSET_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc, 16, 8, __Value)
#define SET_TX_DESC_BMC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc, 24, 1, __Value)
#define SET_TX_DESC_HTC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc, 25, 1, __Value)
#define SET_TX_DESC_AMSDU_PAD_EN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc, 27, 1, __Value)
#define SET_TX_DESC_NO_ACM_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc, 29, 1, __Value)
#define SET_TX_DESC_GF_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc, 30, 1, __Value)

/* Dword 1 */
#define SET_TX_DESC_MACID_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 0, 7, __Value)
#define SET_TX_DESC_QUEUE_SEL_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 8, 5, __Value)
#define SET_TX_DESC_RDG_NAV_EXT_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 13, 1, __Value)
#define SET_TX_DESC_LSIG_TXOP_EN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 14, 1, __Value)
#define SET_TX_DESC_PIFS_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 15, 1, __Value)
#define SET_TX_DESC_RATE_ID_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 16, 5, __Value)
#define SET_TX_DESC_EN_DESC_ID_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 21, 1, __Value)
#define SET_TX_DESC_SEC_TYPE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 22, 2, __Value)
#define SET_TX_DESC_PKT_OFFSET_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 24, 5, __Value)
#define SET_TX_DESC_MORE_DATA_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 29, 1, __Value)

/* Dword 2  remove P_AID, G_ID field*/
#define SET_TX_DESC_CCA_RTS_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 10, 2, __Value)
#define SET_TX_DESC_AGG_ENABLE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 12, 1, __Value)
#define SET_TX_DESC_RDG_ENABLE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 13, 1, __Value)
#define SET_TX_DESC_NULL0_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 14, 1, __Value)
#define SET_TX_DESC_NULL1_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 15, 1, __Value)
#define SET_TX_DESC_BK_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 16, 1, __Value)
#define SET_TX_DESC_MORE_FRAG_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 17, 1, __Value)
#define SET_TX_DESC_RAW_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 18, 1, __Value)
#define SET_TX_DESC_CCX_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 19, 1, __Value)
#define SET_TX_DESC_AMPDU_DENSITY_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 20, 3, __Value)
#define SET_TX_DESC_BT_INT_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 23, 1, __Value)
#define SET_TX_DESC_FTM_EN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 30, 1, __Value)

/* Dword 3 */
#define SET_TX_DESC_HWSEQ_SEL_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 6, 2, __Value)
#define SET_TX_DESC_USE_RATE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 8, 1, __Value)
#define SET_TX_DESC_DISABLE_RTS_FB_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 9, 1, __Value)
#define SET_TX_DESC_DISABLE_FB_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 10, 1, __Value)
#define SET_TX_DESC_CTS2SELF_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 11, 1, __Value)
#define SET_TX_DESC_RTS_ENABLE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 12, 1, __Value)
#define SET_TX_DESC_HW_RTS_ENABLE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 13, 1, __Value)
#define SET_TX_DESC_PORT_ID_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 14, 2, __Value)
#define SET_TX_DESC_NAV_USE_HDR_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 15, 1, __Value)
#define SET_TX_DESC_USE_MAX_LEN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 16, 1, __Value)
#define SET_TX_DESC_MAX_AGG_NUM_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 17, 5, __Value)
#define SET_TX_DESC_AMPDU_MAX_TIME_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 24, 8, __Value)

/* Dword 4 */
#define SET_TX_DESC_TX_RATE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 0, 7, __Value)
#define SET_TX_DESC_TX_TRY_RATE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 7, 1, __Value)
#define SET_TX_DESC_DATA_RATE_FB_LIMIT_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 8, 5, __Value)
#define SET_TX_DESC_RTS_RATE_FB_LIMIT_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 13, 4, __Value)
#define SET_TX_DESC_RETRY_LIMIT_ENABLE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 17, 1, __Value)
#define SET_TX_DESC_DATA_RETRY_LIMIT_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 18, 6, __Value)
#define SET_TX_DESC_RTS_RATE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 24, 5, __Value)
#define SET_TX_DESC_PCTS_EN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 29, 1, __Value)
#define SET_TX_DESC_PCTS_MASK_IDX_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 30, 2, __Value)

/* Dword 5 */
#define SET_TX_DESC_DATA_SC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 0, 4, __Value)
#define SET_TX_DESC_DATA_SHORT_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 4, 1, __Value)
#define SET_TX_DESC_DATA_BW_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 5, 2, __Value)
#define SET_TX_DESC_DATA_STBC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 8, 2, __Value)
#define SET_TX_DESC_RTS_STBC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 10, 2, __Value)
#define SET_TX_DESC_RTS_SHORT_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 12, 1, __Value)
#define SET_TX_DESC_RTS_SC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 13, 4, __Value)
#define SET_TX_DESC_PATH_A_EN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 24, 1, __Value)
#define SET_TX_DESC_TXPWR_OF_SET_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 28, 3, __Value)

/* Dword 6 */
#define SET_TX_DESC_SW_DEFINE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 0, 12, __Value)
#define SET_TX_DESC_MBSSID_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 12, 4, __Value)
#define SET_TX_DESC_RF_SEL_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 16, 3, __Value)

/* Dword 7 */
#ifdef CONFIG_PCI_HCI
#define SET_TX_DESC_TX_BUFFER_SIZE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#endif

#ifdef CONFIG_USB_HCI
#define SET_TX_DESC_TX_DESC_CHECKSUM_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#endif

#ifdef CONFIG_SDIO_HCI
#define SET_TX_DESC_TX_TIMESTAMP_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 6, 18, __Value)
#endif

#define SET_TX_DESC_USB_TXAGG_NUM_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 24, 8, __Value)

/* Dword 8 */
#define SET_TX_DESC_RTS_RC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 0, 6, __Value)
#define SET_TX_DESC_BAR_RC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 6, 2, __Value)
#define SET_TX_DESC_DATA_RC_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 8, 6, __Value)
#define SET_TX_DESC_HWSEQ_EN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 15, 1, __Value)
#define SET_TX_DESC_NEXTHEADPAGE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 16, 8, __Value)
#define SET_TX_DESC_TAILPAGE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 24, 8, __Value)

/* Dword 9 */
#define SET_TX_DESC_PADDING_LEN_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 0, 11, __Value)
#define SET_TX_DESC_SEQ_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 12, 12, __Value)
#define SET_TX_DESC_FINAL_DATA_RATE_8723D(__pTxDesc, __Value) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 24, 8, __Value)


#define SET_EARLYMODE_PKTNUM_8723D(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 0, 4, __Value)
#define SET_EARLYMODE_LEN0_8723D(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 4, 15, __Value)
#define SET_EARLYMODE_LEN1_1_8723D(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 19, 13, __Value)
#define SET_EARLYMODE_LEN1_2_8723D(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 0, 2, __Value)
#define SET_EARLYMODE_LEN2_8723D(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 2, 15,	__Value)
#define SET_EARLYMODE_LEN3_8723D(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 17, 15, __Value)


/*-----------------------------------------------------------------*/
/*	RTL8723D TX BUFFER DESC                                      */
/*-----------------------------------------------------------------*/
#ifdef CONFIG_64BIT_DMA
	#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16), 0, 16, __Valeu)
	#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16), 31, 1, __Valeu)
	#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16)+4, 0, 32, __Valeu)
	#define SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16)+8, 0, 32, __Valeu)
#else
	#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8), 0, 16, __Valeu)
	#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8), 31, 1, __Valeu)
	#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8)+4, 0, 32, __Valeu)
	#define SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, __Offset, __Valeu)	/* 64 BIT mode only */
#endif
/* ********************************************************* */

/* 64 bits  -- 32 bits */
/* =======     ======= */
/* Dword 0     0 */
#define SET_TX_BUFF_DESC_LEN_0_8723D(__pTxDesc, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc, 0, 14, __Valeu)
#define SET_TX_BUFF_DESC_PSB_8723D(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 16, 15, __Value)
#define SET_TX_BUFF_DESC_OWN_8723D(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc, 31, 1, __Value)

/* Dword 1     1 */
#define SET_TX_BUFF_DESC_ADDR_LOW_0_8723D(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 0, 32, __Value)
#define GET_TX_BUFF_DESC_ADDR_LOW_0_8723D(__pTxDesc) LE_BITS_TO_4BYTE(__pTxDesc+4, 0, 32)
/* Dword 2     NA */
#define SET_TX_BUFF_DESC_ADDR_HIGH_0_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, 0, __Value)
#ifdef CONFIG_64BIT_DMA
	#define GET_TX_BUFF_DESC_ADDR_HIGH_0_8723D(__pTxDesc) LE_BITS_TO_4BYTE(__pTxDesc+8, 0, 32)
#else
	#define GET_TX_BUFF_DESC_ADDR_HIGH_0_8723D(__pTxDesc) 0
#endif
/* Dword 3     NA */
/* RESERVED 0 */
/* Dword 4     2 */
#define SET_TX_BUFF_DESC_LEN_1_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, 1, __Value)
#define SET_TX_BUFF_DESC_AMSDU_1_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, 1, __Value)
/* Dword 5     3 */
#define SET_TX_BUFF_DESC_ADDR_LOW_1_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, 1, __Value)
/* Dword 6     NA */
#define SET_TX_BUFF_DESC_ADDR_HIGH_1_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, 1, __Value)
/* Dword 7     NA */
/*RESERVED 0 */
/* Dword 8     4 */
#define SET_TX_BUFF_DESC_LEN_2_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, 2, __Value)
#define SET_TX_BUFF_DESC_AMSDU_2_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, 2, __Value)
/* Dword 9     5 */
#define SET_TX_BUFF_DESC_ADDR_LOW_2_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, 2, __Value)
/* Dword 10    NA */
#define SET_TX_BUFF_DESC_ADDR_HIGH_2_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, 2, __Value)
/* Dword 11    NA */
/*RESERVED 0 */
/* Dword 12    6 */
#define SET_TX_BUFF_DESC_LEN_3_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, 3, __Value)
#define SET_TX_BUFF_DESC_AMSDU_3_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, 3, __Value)
/* Dword 13    7 */
#define SET_TX_BUFF_DESC_ADDR_LOW_3_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, 3, __Value)
/* Dword 14    NA */
#define SET_TX_BUFF_DESC_ADDR_HIGH_3_8723D(__pTxDesc, __Value) SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, 3, __Value)
/* Dword 15    NA */
/*RESERVED 0 */


#endif
/* -----------------------------------------------------------
 *
 *	Rate
 *
 * -----------------------------------------------------------
 * CCK Rates, TxHT = 0 */
#define DESC8723D_RATE1M				0x00
#define DESC8723D_RATE2M				0x01
#define DESC8723D_RATE5_5M				0x02
#define DESC8723D_RATE11M				0x03

/* OFDM Rates, TxHT = 0 */
#define DESC8723D_RATE6M				0x04
#define DESC8723D_RATE9M				0x05
#define DESC8723D_RATE12M				0x06
#define DESC8723D_RATE18M				0x07
#define DESC8723D_RATE24M				0x08
#define DESC8723D_RATE36M				0x09
#define DESC8723D_RATE48M				0x0a
#define DESC8723D_RATE54M				0x0b

/* MCS Rates, TxHT = 1 */
#define DESC8723D_RATEMCS0				0x0c
#define DESC8723D_RATEMCS1				0x0d
#define DESC8723D_RATEMCS2				0x0e
#define DESC8723D_RATEMCS3				0x0f
#define DESC8723D_RATEMCS4				0x10
#define DESC8723D_RATEMCS5				0x11
#define DESC8723D_RATEMCS6				0x12
#define DESC8723D_RATEMCS7				0x13
#define DESC8723D_RATEMCS8				0x14
#define DESC8723D_RATEMCS9				0x15
#define DESC8723D_RATEMCS10		0x16
#define DESC8723D_RATEMCS11		0x17
#define DESC8723D_RATEMCS12		0x18
#define DESC8723D_RATEMCS13		0x19
#define DESC8723D_RATEMCS14		0x1a
#define DESC8723D_RATEMCS15		0x1b
#define DESC8723D_RATEVHTSS1MCS0		0x2c
#define DESC8723D_RATEVHTSS1MCS1		0x2d
#define DESC8723D_RATEVHTSS1MCS2		0x2e
#define DESC8723D_RATEVHTSS1MCS3		0x2f
#define DESC8723D_RATEVHTSS1MCS4		0x30
#define DESC8723D_RATEVHTSS1MCS5		0x31
#define DESC8723D_RATEVHTSS1MCS6		0x32
#define DESC8723D_RATEVHTSS1MCS7		0x33
#define DESC8723D_RATEVHTSS1MCS8		0x34
#define DESC8723D_RATEVHTSS1MCS9		0x35
#define DESC8723D_RATEVHTSS2MCS0		0x36
#define DESC8723D_RATEVHTSS2MCS1		0x37
#define DESC8723D_RATEVHTSS2MCS2		0x38
#define DESC8723D_RATEVHTSS2MCS3		0x39
#define DESC8723D_RATEVHTSS2MCS4		0x3a
#define DESC8723D_RATEVHTSS2MCS5		0x3b
#define DESC8723D_RATEVHTSS2MCS6		0x3c
#define DESC8723D_RATEVHTSS2MCS7		0x3d
#define DESC8723D_RATEVHTSS2MCS8		0x3e
#define DESC8723D_RATEVHTSS2MCS9		0x3f


#define	RX_HAL_IS_CCK_RATE_8723D(pDesc)\
	(GET_RX_STATUS_DESC_RX_RATE_8723D(pDesc) == DESC8723D_RATE1M || \
	 GET_RX_STATUS_DESC_RX_RATE_8723D(pDesc) == DESC8723D_RATE2M || \
	 GET_RX_STATUS_DESC_RX_RATE_8723D(pDesc) == DESC8723D_RATE5_5M || \
	 GET_RX_STATUS_DESC_RX_RATE_8723D(pDesc) == DESC8723D_RATE11M)

#ifdef CONFIG_TRX_BD_ARCH
	struct tx_desc;
#endif

void rtl8723d_cal_txdesc_chksum(struct tx_desc *ptxdesc);
void rtl8723d_update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem);
void rtl8723d_fill_txdesc_sectype(struct pkt_attrib *pattrib, struct tx_desc *ptxdesc);
void rtl8723d_fill_txdesc_vcs(PADAPTER padapter, struct pkt_attrib *pattrib, struct tx_desc *ptxdesc);
void rtl8723d_fill_txdesc_phy(PADAPTER padapter, struct pkt_attrib *pattrib, struct tx_desc *ptxdesc);
void rtl8723d_fill_fake_txdesc(PADAPTER padapter, u8 *pDesc, u32 BufferLen, u8 IsPsPoll, u8 IsBTQosNull, u8 bDataFrame);

void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc);
void fill_txdesc_bmc_tx_rate(struct pkt_attrib *pattrib, u8 *ptxdesc);

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	s32 rtl8723ds_init_xmit_priv(PADAPTER padapter);
	void rtl8723ds_free_xmit_priv(PADAPTER padapter);
	s32 rtl8723ds_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8723ds_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	rtl8723ds_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	s32 rtl8723ds_xmit_buf_handler(PADAPTER padapter);
	thread_return rtl8723ds_xmit_thread(thread_context context);
	#define hal_xmit_handler rtl8723ds_xmit_buf_handler
#endif

#ifdef CONFIG_USB_HCI
	s32 rtl8723du_xmit_buf_handler(PADAPTER padapter);
	#define hal_xmit_handler rtl8723du_xmit_buf_handler
	s32 rtl8723du_init_xmit_priv(PADAPTER padapter);
	void rtl8723du_free_xmit_priv(PADAPTER padapter);
	s32 rtl8723du_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8723du_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	 rtl8723du_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	void rtl8723du_xmit_tasklet(void *priv);
	s32 rtl8723du_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
	void _dbg_dump_tx_info(_adapter	*padapter, int frame_tag, struct tx_desc *ptxdesc);
#endif

#ifdef CONFIG_PCI_HCI
	s32 rtl8723de_init_xmit_priv(PADAPTER padapter);
	void rtl8723de_free_xmit_priv(PADAPTER padapter);
	struct xmit_buf *rtl8723de_dequeue_xmitbuf(struct rtw_tx_ring *ring);
	void	rtl8723de_xmitframe_resume(_adapter *padapter);
	s32 rtl8723de_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8723de_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	rtl8723de_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	void rtl8723de_xmit_tasklet(void *priv);
#endif

u8	BWMapping_8723D(PADAPTER Adapter, struct pkt_attrib *pattrib);
u8	SCMapping_8723D(PADAPTER Adapter, struct pkt_attrib	*pattrib);

#endif

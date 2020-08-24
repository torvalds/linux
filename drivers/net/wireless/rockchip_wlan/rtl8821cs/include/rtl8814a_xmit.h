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
#ifndef __RTL8814A_XMIT_H__
#define __RTL8814A_XMIT_H__

typedef struct txdescriptor_8814 {
	/* Offset 0 */
	u32 pktlen:16;
	u32 offset:8;
	u32 bmc:1;
	u32 htc:1;
	u32 ls:1;
} TXDESC_8814, *PTXDESC_8814;


#define OFFSET_SZ	0
#define OFFSET_SHT	16



#ifdef CONFIG_SDIO_HCI
	#define SET_TX_DESC_SDIO_TXSEQ_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 16, 8, __Value)
#endif /* CONFIG_SDIO_HCI */

/* -----------------------------------------------------------------
 *	RTL8814A TX BUFFER DESC
 * -----------------------------------------------------------------
 *
- Each TXBD has 4 segment.
 -- For 32 bit, each segment is 8 bytes.
 -- For 64 bit, each segment is 16 bytes.
*/
#if 0
	#if 1 /* 32 bit */
		#define SET_TX_EXTBUFF_DESC_LEN_8814A(__pTxDesc, __Value, __Set) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Set*8), 0, 16, __Value)
		#define SET_TX_EXTBUFF_DESC_ADDR_LOW_8814A(__pTxDesc, __Value, __Set) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Set*8)+4, 0, 32, __Value)
	#else /* 64 bit */
		#define SET_TX_EXTBUFF_DESC_LEN_8814A(__pTxDesc, __Value, __Set) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Set*16), 0, 16, __Value)
		#define SET_TX_EXTBUFF_DESC_ADDR_LOW_8814A(__pTxDesc, __Value, __Set) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Set*16)+4, 0, 32, __Value)
	#endif
	#define SET_TX_EXTBUFF_DESC_ADDR_HIGH_8814A(__pTxDesc, __Value, __Set) SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Set*16)+8, 0, 32, __Value)
#endif
/*c2h-DWORD 2*/
#define GET_RX_STATUS_DESC_RPT_SEL_8814A(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+8, 28, 1)

/* *********************************************************
 * for Txfilldescroptor8814Ae, fill the desc content. */
#if 1 /* 32 bit */
	#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*8), 0, 16, __Valeu)
	#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*8), 31, 1, __Valeu)
	#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*8)+4, 0, 32, __Valeu)
#else /* 64 bit */
	#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16), 0, 16, __Valeu)
	#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16), 31, 1, __Valeu)
	#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16)+4, 0, 32, __Valeu)
#endif
#define SET_TXBUFFER_DESC_ADD_HIGT_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) SET_BITS_TO_LE_4BYTE(__pTxDesc+((__Offset)*16)+8, 0, 32, __Valeu)

/* ********************************************************* */

/* TX buffer
 * *************
 * Dword 0 */
#define SET_TX_BUFF_DESC_LEN_0_8814A(__pTxDesc, __Valeu)			SET_BITS_TO_LE_4BYTE(__pTxDesc, 0, 16, __Valeu)
#define SET_TX_BUFF_DESC_PSB_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc, 16, 15, __Value)
#define SET_TX_BUFF_DESC_OWN_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc, 31, 1, __Value)
#define GET_TX_BUFF_DESC_OWN_8814A(__pTxDesc)				LE_BITS_TO_4BYTE(__pTxDesc, 31, 1)

/* Dword 1 */
#define SET_TX_BUFF_DESC_ADDR_LOW_0_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 0, 32, __Value)
#define GET_TX_BUFF_DESC_ADDR_LOW_0_8814A(__pTxDesc)			LE_BITS_TO_4BYTE(__pTxDesc+4, 0, 32)
/* Dword 2 */
#define SET_TX_BUFF_DESC_ADDR_HIGH_0_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 0, 32, __Value)
#define GET_TX_BUFF_DESC_ADDR_HIGH_0_8814A(__pTxDesc)			LE_BITS_TO_4BYTE(__pTxDesc+8, 0, 32)
/* Dword 3 */ /* RESERVED 0 */

#if 0 /* 64 bit */
	/* Dword 4 */
	#define SET_TX_BUFF_DESC_LEN_1_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 0, 16, __Value)
	#define SET_TX_BUFF_DESC_AMSDU_1_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 31, 1, __Value)
	/* Dword 5 */
	#define SET_TX_BUFF_DESC_ADDR_LOW_1_8814A(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 0, 32, __Value)
	/* Dword 6 */
	#define SET_TX_BUFF_DESC_ADDR_HIGH_1_8814A(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 0, 32, __Value)
	/* Dword 7 */ /* RESERVED 0 */
	/* Dword 8 */
	#define SET_TX_BUFF_DESC_LEN_2_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 0, 16, __Value)
	#define SET_TX_BUFF_DESC_AMSDU_2_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 31, 1, __Value)
	/* Dword 9 */
	#define SET_TX_BUFF_DESC_ADDR_LOW_2_8814A(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 0, 32, __Value)
	/* Dword 10 */
	#define SET_TX_BUFF_DESC_ADDR_HIGH_2_8814A(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+40, 0, 32, __Value)
	/* Dword 11 */ /* RESERVED 0 */
	/* Dword 12 */
	#define SET_TX_BUFF_DESC_LEN_3_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+48, 0, 16, __Value)
	#define SET_TX_BUFF_DESC_AMSDU_3_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+48, 31, 1, __Value)
	/* Dword 13 */
	#define SET_TX_BUFF_DESC_ADDR_LOW_3_8814A(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+52, 0, 32, __Value)
	/* Dword 14 */
	#define SET_TX_BUFF_DESC_ADDR_HIGH_3_8814A(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+56, 0, 32, __Value)
	/* Dword 15 */ /* RESERVED 0 */
#endif

/* *****Desc content
 * TX Info
 * *************
 * Dword 0 */
#define SET_TX_DESC_PKT_SIZE_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc, 0, 16, __Value)
#define GET_TX_DESC_PKT_SIZE_8814A(__pTxDesc)									LE_BITS_TO_4BYTE(__pTxDesc, 0, 16)
#define SET_TX_DESC_OFFSET_8814A(__pTxDesc, __Value)							SET_BITS_TO_LE_4BYTE(__pTxDesc, 16, 8, __Value)
#define GET_TX_DESC_OFFSET_8814A(__pTxDesc)									LE_BITS_TO_4BYTE(__pTxDesc, 16, 8)
#define SET_TX_DESC_BMC_8814A(__pTxDesc, __Value)							SET_BITS_TO_LE_4BYTE(__pTxDesc, 24, 1, __Value)
#define SET_TX_DESC_HTC_8814A(__pTxDesc, __Value)								SET_BITS_TO_LE_4BYTE(__pTxDesc, 25, 1, __Value)
#define SET_TX_DESC_LAST_SEG_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc, 26, 1, __Value)
#define SET_TX_DESC_LINIP_8814A(__pTxDesc, __Value)							SET_BITS_TO_LE_4BYTE(__pTxDesc, 28, 1, __Value)
#define SET_TX_DESC_AMSDU_PAD_EN_8814A(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc, 27, 1, __Value)
#define SET_TX_DESC_NO_ACM_8814A(__pTxDesc, __Value)							SET_BITS_TO_LE_4BYTE(__pTxDesc, 29, 1, __Value)
#define SET_TX_DESC_GF_8814A(__pTxDesc, __Value)								SET_BITS_TO_LE_4BYTE(__pTxDesc, 30, 1, __Value)
#define SET_TX_DESC_DISQSELSEQ_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc, 31, 1, __Value)

/* Dword 1 */
#define SET_TX_DESC_MACID_8814A(__pTxDesc, __Value)							SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 0, 7, __Value)
#define SET_TX_DESC_QUEUE_SEL_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 8, 5, __Value)
#define SET_TX_DESC_RDG_NAV_EXT_8814A(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 13, 1, __Value)
#define SET_TX_DESC_LSIG_TXOP_EN_8814A(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 14, 1, __Value)
#define SET_TX_DESC_PIFS_8814A(__pTxDesc, __Value)							SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 15, 1, __Value)
#define SET_TX_DESC_RATE_ID_8814A(__pTxDesc, __Value)							SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 16, 5, __Value)
#define SET_TX_DESC_EN_DESC_ID_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 21, 1, __Value)
#define SET_TX_DESC_SEC_TYPE_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 22, 2, __Value)
#define SET_TX_DESC_PKT_OFFSET_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 24, 5, __Value)
#define SET_TX_DESC_MORE_DATA_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 29, 1, __Value)
#define SET_TX_DESC_TXOP_PS_CAP_8814A(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 30, 1, __Value)
#define SET_TX_DESC_TXOP_PS_MODE_8814A(__pTxDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pTxDesc+4, 31, 1, __Value)


/* Dword 2 */
#define SET_TX_DESC_PAID_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 0,  9, __Value)
#define SET_TX_DESC_CCA_RTS_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 10, 2, __Value)
#define SET_TX_DESC_AGG_ENABLE_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 12, 1, __Value)
#define SET_TX_DESC_RDG_ENABLE_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 13, 1, __Value)
#define SET_TX_DESC_NULL_0_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 14, 1, __Value)
#define SET_TX_DESC_NULL_1_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 15, 1, __Value)
#define SET_TX_DESC_BK_8814A(__pTxDesc, __Value)				SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 16, 1, __Value)
#define SET_TX_DESC_MORE_FRAG_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 17, 1, __Value)
#define GET_TX_DESC_MORE_FRAG_8814A(__pTxDesc)				LE_BITS_TO_4BYTE(__pTxDesc+8, 17, 1)
#define SET_TX_DESC_RAW_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 18, 1, __Value)
#define SET_TX_DESC_SPE_RPT_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 19, 1, __Value)
#define SET_TX_DESC_AMPDU_DENSITY_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 20, 3, __Value)
#define SET_TX_DESC_BT_NULL_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 23, 1, __Value)
#define SET_TX_DESC_GID_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 24, 6, __Value)
#define SET_TX_DESC_HW_AES_IV_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 31, 1, __Value)


/* Dword 3 */
#define SET_TX_DESC_WHEADER_LEN_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 0, 5, __Value)
#define SET_TX_DESC_EARLY_RATE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 5, 1, __Value)
#define SET_TX_DESC_HW_SSN_SEL_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 6, 2, __Value)
#define SET_TX_DESC_USE_RATE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 8, 1, __Value)
#define SET_TX_DESC_DISABLE_RTS_FB_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 9, 1, __Value)
#define SET_TX_DESC_DISABLE_FB_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 10, 1, __Value)
#define SET_TX_DESC_CTS2SELF_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 11, 1, __Value)
#define SET_TX_DESC_RTS_ENABLE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 12, 1, __Value)
#define SET_TX_DESC_HW_RTS_ENABLE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 13, 1, __Value)
#define SET_TX_DESC_CHECK_EN_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 14, 1, __Value)
#define SET_TX_DESC_NAV_USE_HDR_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 15, 1, __Value)
#define SET_TX_DESC_USE_MAX_LEN_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 16, 1, __Value)
#define SET_TX_DESC_MAX_AGG_NUM_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 17, 5, __Value)
#define SET_TX_DESC_NDPA_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 22, 2, __Value)
#define SET_TX_DESC_AMPDU_MAX_TIME_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+12, 24, 8, __Value)

/* Dword 4 */
#define SET_TX_DESC_TX_RATE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 0, 7, __Value)
#define SET_TX_DESC_TRY_RATE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 7, 1, __Value)
#define SET_TX_DESC_DATA_RATE_FB_LIMIT_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 8, 5, __Value)
#define SET_TX_DESC_RTS_RATE_FB_LIMIT_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 13, 4, __Value)
#define SET_TX_DESC_RETRY_LIMIT_ENABLE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 17, 1, __Value)
#define SET_TX_DESC_DATA_RETRY_LIMIT_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 18, 6, __Value)
#define SET_TX_DESC_RTS_RATE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 24, 5, __Value)
#define SET_TX_DESC_PCTS_ENABLE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 29, 1, __Value)
#define SET_TX_DESC_PCTS_MASK_IDX_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+16, 30, 2, __Value)


/* Dword 5 */
#define SET_TX_DESC_DATA_SC_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 0, 4, __Value)
#define SET_TX_DESC_DATA_SHORT_8814A(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 4, 1, __Value)
#define SET_TX_DESC_DATA_BW_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 5, 2, __Value)
#define SET_TX_DESC_DATA_LDPC_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 7, 1, __Value)
#define SET_TX_DESC_DATA_STBC_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 8, 2, __Value)
#define SET_TX_DESC_CTROL_STBC_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 10, 2, __Value)
#define SET_TX_DESC_RTS_SHORT_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 12, 1, __Value)
#define SET_TX_DESC_RTS_SC_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 13, 4, __Value)
#define SET_TX_DESC_SIGNALING_TA_PKT_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 17, 1, __Value)
#define SET_TX_DESC_PORT_ID_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 21, 3, __Value)/* 20130415 KaiYuan add for 8814 */
#define SET_TX_DESC_TX_ANT_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 24, 4, __Value)
#define SET_TX_DESC_TX_POWER_OFFSET_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+20, 28, 3, __Value)

/* Dword 6 */
#define SET_TX_DESC_SW_DEFINE_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 0, 12, __Value)
#define SET_TX_DESC_MBSSID_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 12, 4, __Value)
#define SET_TX_DESC_ANTSEL_A_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 16, 3, __Value)
#define SET_TX_DESC_ANTSEL_B_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 19, 3, __Value)
#define SET_TX_DESC_ANT_MAPA_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 22, 2, __Value)
#define SET_TX_DESC_ANT_MAPB_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 24, 2, __Value)
#define SET_TX_DESC_ANT_MAPC_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 26, 2, __Value)
#define SET_TX_DESC_ANT_MAPD_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+24, 28, 2, __Value)


/* Dword 7 */
#ifdef CONFIG_PCI_HCI
	#define SET_TX_DESC_TX_BUFFER_SIZE_8814A(__pTxDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#endif
#if defined(CONFIG_SDIO_HCI)|| defined(CONFIG_USB_HCI)
	#define SET_TX_DESC_TX_DESC_CHECKSUM_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 0, 16, __Value)
#endif
#define SET_TX_DESC_NTX_MAP_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 20, 4, __Value)
#define SET_TX_DESC_USB_TXAGG_NUM_8814A(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 24, 8, __Value)


/* Dword 8 */
#define SET_TX_DESC_RTS_RC_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 0, 6, __Value)
#define SET_TX_DESC_BAR_RTY_TH_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 6, 2, __Value)
#define SET_TX_DESC_DATA_RC_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 8, 6, __Value)
#define SET_TX_DESC_EN_HWEXSEQ_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 14, 1, __Value)
#define SET_TX_DESC_HWSEQ_EN_8814A(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 15, 1, __Value)
#if defined(CONFIG_PCI_HCI)|| defined(CONFIG_USB_HCI)
	#define SET_TX_DESC_NEXT_HEAD_PAGE_L_8814A(__pTxDesc, __Value)(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 16, 8, __Value)
#endif
#ifdef CONFIG_SDIO_HCI
	#define SET_TX_DESC_SDIO_SEQ_8814A(__pTxDesc, __Value)(__pTxDesc, __Value) 			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 16, 8, __Value) /* 20130415 KaiYuan add for 8814AS */
#endif
#define SET_TX_DESC_TAIL_PAGE_L_8814A(__pTxDesc, __Value)(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+32, 24, 8, __Value)

/* Dword 9 */
#define SET_TX_DESC_PADDING_LENGTH_8814A(__pTxDesc, __Value)						SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 0, 11, __Value)
#define SET_TX_DESC_TXBF_PATH_8814A(__pTxDesc, __Value)								SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 11, 1, __Value)
#define SET_TX_DESC_SEQ_8814A(__pTxDesc, __Value)										SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 12, 12, __Value)
#define SET_TX_DESC_NEXT_HEAD_PAGE_H_8814A(__pTxDesc, __Value)(__pTxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 24, 4, __Value)
#define SET_TX_DESC_TAIL_PAGE_H_8814A(__pTxDesc, __Value)(__pTxDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pTxDesc+36, 28, 4, __Value)



#define SET_EARLYMODE_PKTNUM_8814A(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 0, 4, __Value)
#define SET_EARLYMODE_LEN0_8814A(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 4, 15, __Value)
#define SET_EARLYMODE_LEN1_1_8814A(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr, 19, 13, __Value)
#define SET_EARLYMODE_LEN1_2_8814A(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 0, 2, __Value)
#define SET_EARLYMODE_LEN2_8814A(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 2, 15,  __Value)
#define SET_EARLYMODE_LEN3_8814A(__pAddr, __Value)					SET_BITS_TO_LE_4BYTE(__pAddr+4, 17, 15, __Value)


void rtl8814a_cal_txdesc_chksum(u8 *ptxdesc);
void rtl8814a_fill_fake_txdesc(PADAPTER	padapter, u8 *pDesc, u32 BufferLen, u8 IsPsPoll, u8	IsBTQosNull, u8 bDataFrame);
void rtl8814a_fill_txdesc_sectype(struct pkt_attrib *pattrib, u8 *ptxdesc);
void rtl8814a_fill_txdesc_vcs(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc);
void rtl8814a_fill_txdesc_phy(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc);
void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc);
void fill_txdesc_bmc_tx_rate(struct pkt_attrib *pattrib, u8 *ptxdesc);

#ifdef CONFIG_USB_HCI
	s32 rtl8814au_init_xmit_priv(PADAPTER padapter);
	void rtl8814au_free_xmit_priv(PADAPTER padapter);
	s32 rtl8814au_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8814au_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	 rtl8814au_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	s32 rtl8814au_xmit_buf_handler(PADAPTER padapter);
	void rtl8814au_xmit_tasklet(void *priv);
	s32 rtl8814au_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
#endif /* CONFIG_USB_HCI */

#ifdef CONFIG_PCI_HCI
	s32 rtl8814ae_init_xmit_priv(PADAPTER padapter);
	void rtl8814ae_free_xmit_priv(PADAPTER padapter);
	struct xmit_buf *rtl8814ae_dequeue_xmitbuf(struct rtw_tx_ring *ring);
	void rtl8814ae_xmitframe_resume(_adapter *padapter);
	s32 rtl8814ae_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
	s32 rtl8814ae_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
	s32	rtl8814ae_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
	void rtl8814ae_xmit_tasklet(void *priv);
#ifdef CONFIG_XMIT_THREAD_MODE
	s32 rtl8814ae_xmit_buf_handler(_adapter *padapter);
#endif
#endif

void _dbg_dump_tx_info(_adapter	*padapter, int frame_tag, u8 *ptxdesc);
u8
SCMapping_8814(
		PADAPTER		Adapter,
		struct pkt_attrib	*pattrib
);

u8
BWMapping_8814(
		PADAPTER		Adapter,
		struct pkt_attrib	*pattrib
);


#endif /* __RTL8814_XMIT_H__ */

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
#ifndef __RTL8812A_RECV_H__
#define __RTL8812A_RECV_H__

#if defined(CONFIG_USB_HCI)

	#ifndef MAX_RECVBUF_SZ
		#ifndef CONFIG_MINIMAL_MEMORY_USAGE
			#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
				#define MAX_RECVBUF_SZ (rtw_rtkm_get_buff_size()) /*depend rtkm*/
			#else
				#define MAX_RECVBUF_SZ (32768)  /*32k*/
			#endif
			/* #define MAX_RECVBUF_SZ (24576) */ /* 24k */
			/* #define MAX_RECVBUF_SZ (20480) */ /* 20K */
			/* #define MAX_RECVBUF_SZ (10240) */ /* 10K */
			/* #define MAX_RECVBUF_SZ (15360) */ /* 15k < 16k */
			/* #define MAX_RECVBUF_SZ (8192+1024) */ /* 8K+1k */
			#ifdef CONFIG_PLATFORM_NOVATEK_NT72668
				#undef MAX_RECVBUF_SZ
				#define MAX_RECVBUF_SZ (15360) /* 15k < 16k */
			#endif /* CONFIG_PLATFORM_NOVATEK_NT72668 */
		#else
			#define MAX_RECVBUF_SZ (4000) /* about 4K */
		#endif
	#endif /* !MAX_RECVBUF_SZ */

#elif defined(CONFIG_PCI_HCI)
	/* #ifndef CONFIG_MINIMAL_MEMORY_USAGE */
	/*	#define MAX_RECVBUF_SZ (9100) */
	/* #else */
	#define MAX_RECVBUF_SZ (4000) /* about 4K
	* #endif */


#elif defined(CONFIG_SDIO_HCI)

	#define MAX_RECVBUF_SZ (RX_DMA_BOUNDARY_8821 + 1)

#endif


/* Rx smooth factor */
#define Rx_Smooth_Factor (20)

/* DWORD 0 */
#define SET_RX_STATUS_DESC_PKT_LEN_8812(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 0, 14, __Value)
#define SET_RX_STATUS_DESC_EOR_8812(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 30, 1, __Value)
#define SET_RX_STATUS_DESC_OWN_8812(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 31, 1, __Value)

#define GET_RX_STATUS_DESC_PKT_LEN_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 0, 14)
#define GET_RX_STATUS_DESC_CRC32_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 14, 1)
#define GET_RX_STATUS_DESC_ICV_8812(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc, 15, 1)
#define GET_RX_STATUS_DESC_DRVINFO_SIZE_8812(__pRxStatusDesc)		LE_BITS_TO_4BYTE(__pRxStatusDesc, 16, 4)
#define GET_RX_STATUS_DESC_SECURITY_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 20, 3)
#define GET_RX_STATUS_DESC_QOS_8812(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc, 23, 1)
#define GET_RX_STATUS_DESC_SHIFT_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 24, 2)
#define GET_RX_STATUS_DESC_PHY_STATUS_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 26, 1)
#define GET_RX_STATUS_DESC_SWDEC_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 27, 1)
#define GET_RX_STATUS_DESC_LAST_SEG_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 28, 1)
#define GET_RX_STATUS_DESC_FIRST_SEG_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc, 29, 1)
#define GET_RX_STATUS_DESC_EOR_8812(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc, 30, 1)
#define GET_RX_STATUS_DESC_OWN_8812(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc, 31, 1)

/* DWORD 1 */
#define GET_RX_STATUS_DESC_MACID_8812(__pRxDesc)					LE_BITS_TO_4BYTE(__pRxDesc+4, 0, 7)
#define GET_RX_STATUS_DESC_TID_8812(__pRxDesc)					LE_BITS_TO_4BYTE(__pRxDesc+4, 8, 4)
#define GET_RX_STATUS_DESC_AMSDU_8812(__pRxDesc)					LE_BITS_TO_4BYTE(__pRxDesc+4, 13, 1)
#define GET_RX_STATUS_DESC_RXID_MATCH_8812(__pRxDesc)		LE_BITS_TO_4BYTE(__pRxDesc+4, 14, 1)
#define GET_RX_STATUS_DESC_PAGGR_8812(__pRxDesc)				LE_BITS_TO_4BYTE(__pRxDesc+4, 15, 1)
#define GET_RX_STATUS_DESC_A1_FIT_8812(__pRxDesc)				LE_BITS_TO_4BYTE(__pRxDesc+4, 16, 4)
#define GET_RX_STATUS_DESC_CHKERR_8812(__pRxDesc)				LE_BITS_TO_4BYTE(__pRxDesc+4, 20, 1)
#define GET_RX_STATUS_DESC_IPVER_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+4, 21, 1)
#define GET_RX_STATUS_DESC_IS_TCPUDP__8812(__pRxDesc)		LE_BITS_TO_4BYTE(__pRxDesc+4, 22, 1)
#define GET_RX_STATUS_DESC_CHK_VLD_8812(__pRxDesc)		LE_BITS_TO_4BYTE(__pRxDesc+4, 23, 1)
#define GET_RX_STATUS_DESC_PAM_8812(__pRxDesc)				LE_BITS_TO_4BYTE(__pRxDesc+4, 24, 1)
#define GET_RX_STATUS_DESC_PWR_8812(__pRxDesc)				LE_BITS_TO_4BYTE(__pRxDesc+4, 25, 1)
#define GET_RX_STATUS_DESC_MORE_DATA_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+4, 26, 1)
#define GET_RX_STATUS_DESC_MORE_FRAG_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+4, 27, 1)
#define GET_RX_STATUS_DESC_TYPE_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+4, 28, 2)
#define GET_RX_STATUS_DESC_MC_8812(__pRxDesc)				LE_BITS_TO_4BYTE(__pRxDesc+4, 30, 1)
#define GET_RX_STATUS_DESC_BC_8812(__pRxDesc)				LE_BITS_TO_4BYTE(__pRxDesc+4, 31, 1)

/* DWORD 2 */
#define GET_RX_STATUS_DESC_SEQ_8812(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 0, 12)
#define GET_RX_STATUS_DESC_FRAG_8812(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 12, 4)
#define GET_RX_STATUS_DESC_RX_IS_QOS_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 16, 1)
#define GET_RX_STATUS_DESC_WLANHD_IV_LEN_8812(__pRxStatusDesc)		LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 18, 6)
#define GET_RX_STATUS_DESC_RPT_SEL_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc+8, 28, 1)

/* DWORD 3 */
#define GET_RX_STATUS_DESC_RX_RATE_8812(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 0, 7)
#define GET_RX_STATUS_DESC_HTC_8812(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 10, 1)
#define GET_RX_STATUS_DESC_EOSP_8812(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 11, 1)
#define GET_RX_STATUS_DESC_BSSID_FIT_8812(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 12, 2)
#ifdef CONFIG_USB_RX_AGGREGATION
#define GET_RX_STATUS_DESC_USB_AGG_PKTNUM_8812(__pRxStatusDesc)	LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 16, 8)
#endif
#define GET_RX_STATUS_DESC_PATTERN_MATCH_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+12, 29, 1)
#define GET_RX_STATUS_DESC_UNICAST_MATCH_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+12, 30, 1)
#define GET_RX_STATUS_DESC_MAGIC_MATCH_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+12, 31, 1)

/* DWORD 6 */
#define GET_RX_STATUS_DESC_SPLCP_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+16, 0, 1)
#define GET_RX_STATUS_DESC_LDPC_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+16, 1, 1)
#define GET_RX_STATUS_DESC_STBC_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+16, 2, 1)
#define GET_RX_STATUS_DESC_BW_8812(__pRxDesc)			LE_BITS_TO_4BYTE(__pRxDesc+16, 4, 2)

/* DWORD 5 */
#define GET_RX_STATUS_DESC_TSFL_8812(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc+20, 0, 32)

#define GET_RX_STATUS_DESC_BUFF_ADDR_8812(__pRxDesc)		LE_BITS_TO_4BYTE(__pRxDesc+24, 0, 32)
#define GET_RX_STATUS_DESC_BUFF_ADDR64_8812(__pRxDesc)		LE_BITS_TO_4BYTE(__pRxDesc+28, 0, 32)

#define SET_RX_STATUS_DESC_BUFF_ADDR_8812(__pRxDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pRxDesc+24, 0, 32, __Value)


#ifdef CONFIG_SDIO_HCI
s32 InitRecvPriv8821AS(PADAPTER padapter);
void FreeRecvPriv8821AS(PADAPTER padapter);
#endif /* CONFIG_SDIO_HCI */

#ifdef CONFIG_USB_HCI
void rtl8812au_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf);
s32 rtl8812au_init_recv_priv(PADAPTER padapter);
void rtl8812au_free_recv_priv(PADAPTER padapter);
#endif

#ifdef CONFIG_PCI_HCI
s32 rtl8812ae_init_recv_priv(PADAPTER padapter);
void rtl8812ae_free_recv_priv(PADAPTER padapter);
#endif

void rtl8812_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc);

#endif /* __RTL8812A_RECV_H__ */

/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTL8814A_RECV_H__
#define __RTL8814A_RECV_H__

#if defined(CONFIG_USB_HCI)

#ifndef MAX_RECVBUF_SZ
#ifdef PLATFORM_OS_CE
#define MAX_RECVBUF_SZ (8192+1024) // 8K+1k
#else
	#ifndef CONFIG_MINIMAL_MEMORY_USAGE
		#ifdef CONFIG_PLATFORM_MSTAR
			#define MAX_RECVBUF_SZ (8192) // 8K
		#else
		#define MAX_RECVBUF_SZ (32768) // 32k
		#endif
		//#define MAX_RECVBUF_SZ (24576) // 24k
		//#define MAX_RECVBUF_SZ (20480) //20K
		//#define MAX_RECVBUF_SZ (10240) //10K
		//#define MAX_RECVBUF_SZ (15360) // 15k < 16k
		//#define MAX_RECVBUF_SZ (8192+1024) // 8K+1k
	#else
		#define MAX_RECVBUF_SZ (4000) // about 4K
	#endif
#endif
#endif //!MAX_RECVBUF_SZ

#elif defined(CONFIG_PCI_HCI)
//#ifndef CONFIG_MINIMAL_MEMORY_USAGE
//	#define MAX_RECVBUF_SZ (9100)
//#else
	#define MAX_RECVBUF_SZ (4000) // about 4K
//#endif


#elif defined(CONFIG_SDIO_HCI)
/* temp solution
#ifdef CONFIG_SDIO_RX_COPY
#define MAX_RECVBUF_SZ (10240)
#else // !CONFIG_SDIO_RX_COPY
#define MAX_RECVBUF_SZ	MAX_RX_DMA_BUFFER_SIZE_8821
#endif // !CONFIG_SDIO_RX_COPY
*/
#endif


/* RX buffer descriptor */
/* DWORD 0 */
#define SET_RX_BUFFER_DESC_DATA_LENGTH_8814A(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 0, 14, __Value)
#define SET_RX_BUFFER_DESC_LS_8814A(__pRxStatusDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 14, 1, __Value)
#define SET_RX_BUFFER_DESC_FS_8814A(__pRxStatusDesc, __Value)					SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 15, 1, __Value)
#define SET_RX_BUFFER_DESC_TOTAL_LENGTH_8814A(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 16, 16, __Value)

#define GET_RX_BUFFER_DESC_OWN_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE(__pRxStatusDesc, 31, 1)
#define GET_RX_BUFFER_DESC_LS_8814A(__pRxStatusDesc)							LE_BITS_TO_4BYTE(__pRxStatusDesc, 14, 1)
#define GET_RX_BUFFER_DESC_FS_8814A(__pRxStatusDesc)							LE_BITS_TO_4BYTE(__pRxStatusDesc, 15, 1)
#define GET_RX_BUFFER_DESC_TOTAL_LENGTH_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc, 16, 15)

/* DWORD 1 */
#define SET_RX_BUFFER_PHYSICAL_LOW_8814A(__pRxStatusDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pRxStatusDesc+4, 0, 32, __Value)
#define GET_RX_BUFFER_PHYSICAL_LOW_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+4, 0, 32)

/* DWORD 2 */
#define SET_RX_BUFFER_PHYSICAL_HIGH_8814A(__pRxStatusDesc, __Value)			SET_BITS_TO_LE_4BYTE(__pRxStatusDesc+8, 0, 32, __Value)

/* DWORD 3*/ /* RESERVED */


/*=============
//RX Info
==============*/
//DWORD 0
#define SET_RX_STATUS_DESC_PKT_LEN_8814A(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 0, 14, __Value)
#define SET_RX_STATUS_DESC_EOR_8814A(__pRxStatusDesc, __Value)			SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 30, 1, __Value)
#define SET_RX_STATUS_DESC_OWN_8814AE(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 31, 1, __Value)

#define GET_RX_STATUS_DESC_PKT_LEN_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 0, 14)
#define GET_RX_STATUS_DESC_CRC32_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc, 14, 1)
#define GET_RX_STATUS_DESC_ICV_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc, 15, 1)
#define GET_RX_STATUS_DESC_DRVINFO_SIZE_8814A(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 16, 4)
#define GET_RX_STATUS_DESC_SECURITY_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 20, 3)
#define GET_RX_STATUS_DESC_QOS_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc, 23, 1)
#define GET_RX_STATUS_DESC_SHIFT_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc, 24, 2)
#define GET_RX_STATUS_DESC_PHY_STATUS_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 26, 1)
#define GET_RX_STATUS_DESC_SWDEC_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc, 27, 1)
#define GET_RX_STATUS_DESC_LAST_SEG_8814AE(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 28, 1)
#define GET_RX_STATUS_DESC_EOR_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc, 30, 1)

//DWORD 1
#define GET_RX_STATUS_DESC_MACID_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+4, 0, 7)
#define GET_RX_STATUS_DESC_EXT_SECTYPE_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc+4, 7, 1)/* 20130415 KaiYuan add for 8814 */
#define GET_RX_STATUS_DESC_TID_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+4, 8, 4)
#define GET_RX_STATUS_DESC_MACID_VLD_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc+4, 12, 1)
#define GET_RX_STATUS_DESC_AMSDU_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+4, 13, 1)
#define GET_RX_STATUS_DESC_RXID_MATCH_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc+4, 14, 1)
#define GET_RX_STATUS_DESC_PAGGR_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 15, 1)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_CHKERR_8814A(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 20, 1)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_IPVER_8814A(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 21, 1)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_IS_TCPUDP_8814A(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 22, 1)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_CHK_VLD_8814A(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 23, 1)
#define GET_RX_STATUS_DESC_PAM_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 24, 1)
#define GET_RX_STATUS_DESC_PWR_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 25, 1)
#define GET_RX_STATUS_DESC_MORE_DATA_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 26, 1)
#define GET_RX_STATUS_DESC_MORE_FRAG_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 27, 1)
#define GET_RX_STATUS_DESC_TYPE_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 28, 2)
#define GET_RX_STATUS_DESC_FIRST_SEG_8814AE(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 29, 1)
#define GET_RX_STATUS_DESC_EOR_8814AE(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 30, 1)
#define GET_RX_STATUS_DESC_MC_8814A(__pRxStatusDesc)							LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 30, 1)
#define GET_RX_STATUS_DESC_BC_8814A(__pRxStatusDesc)							LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 31, 1)

//DWORD 2
#define GET_RX_STATUS_DESC_SEQ_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 0, 12)
#define GET_RX_STATUS_DESC_FRAG_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 12, 4)
#ifdef CONFIG_USB_RX_AGGREGATION
#define GET_RX_STATUS_DESC_USB_AGG_PKTNUM_8814A(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 16, 8)
#else
#define GET_RX_STATUS_DESC_RX_IS_QOS_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 16, 1)
#endif
#define GET_RX_STATUS_DESC_WLANHD_IV_LEN_8814A(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 18, 6)
#define GET_RX_STATUS_DESC_HWRSVD_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 24, 4)
#define GET_RX_STATUS_C2H_8814A(__pRxStatusDesc)								LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 28, 1)
#define GET_RX_STATUS_DESC_FCS_OK_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 31, 1)

//DWORD 3
#define GET_RX_STATUS_DESC_RX_RATE_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 0, 7)
#define GET_RX_STATUS_DESC_BSSID_FIT_H_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 7, 3)//20130415 KaiYuan add for 8814
#define GET_RX_STATUS_DESC_HTC_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 10, 1)
#define GET_RX_STATUS_DESC_EOSP_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 11, 1)
#define GET_RX_STATUS_DESC_BSSID_FIT_L_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 12, 2)
#define GET_RX_STATUS_DESC_DMA_AGG_NUM_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 16, 8)//20130415 KaiYuan Check if it exist anymore
#define GET_RX_STATUS_DESC_PATTERN_MATCH_8814A(__pRxStatusDesc)			LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 29, 1)
#define GET_RX_STATUS_DESC_UNICAST_8814A(__pRxStatusDesc)					LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 30, 1)
#define GET_RX_STATUS_DESC_MAGIC_WAKE_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE(__pRxStatusDesc+12, 31, 1)

//DWORD 4
#define GET_RX_STATUS_DESC_PATTERN_IDX_8814A(__pRxStatusDesc)	 			LE_BITS_TO_4BYTE( __pRxStatusDesc+16, 0, 8)
#define GET_RX_STATUS_DESC_RX_EOF_8814A(__pRxStatusDesc)	 				LE_BITS_TO_4BYTE( __pRxStatusDesc+16, 8, 1)
#define GET_RX_STATUS_DESC_RX_SCRAMBLER_8814A(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+16, 9, 7)
#define GET_RX_STATUS_DESC_RX_PRE_NDP_VLD_8814A(__pRxStatusDesc)	 		LE_BITS_TO_4BYTE( __pRxStatusDesc+16, 16, 1)
#define GET_RX_STATUS_DESC_A1_FIT_8814A(__pRxStatusDesc)	 					LE_BITS_TO_4BYTE( __pRxStatusDesc+16, 24, 5)


//DWORD 5
#define GET_RX_STATUS_DESC_TSFL_8814A(__pRxStatusDesc)						LE_BITS_TO_4BYTE( __pRxStatusDesc+20, 0, 32)


// Rx smooth factor
#define Rx_Smooth_Factor (20)

#ifdef CONFIG_USB_HCI
s32 rtl8814au_init_recv_priv(PADAPTER padapter);
void rtl8814au_free_recv_priv(PADAPTER padapter);
#endif

#ifdef CONFIG_PCI_HCI
s32 rtl8814ae_init_recv_priv(PADAPTER padapter);
void rtl8814ae_free_recv_priv(PADAPTER padapter);
#endif

/* temp solution
#ifdef CONFIG_SDIO_HCI
s32 InitRecvPriv8821AS(PADAPTER padapter);
void FreeRecvPriv8821AS(PADAPTER padapter);
#endif // CONFIG_SDIO_HCI
*/

void rtl8814_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc);

#endif /* __RTL8814A_RECV_H__ */


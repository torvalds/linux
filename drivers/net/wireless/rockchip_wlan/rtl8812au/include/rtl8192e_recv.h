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
#ifndef __RTL8192E_RECV_H__
#define __RTL8192E_RECV_H__

#if defined(CONFIG_USB_HCI)

#ifndef MAX_RECVBUF_SZ
#ifdef PLATFORM_OS_CE
#define MAX_RECVBUF_SZ (8192+1024) // 8K+1k
#else
	#ifdef CONFIG_MINIMAL_MEMORY_USAGE
		#define MAX_RECVBUF_SZ (4000) // about 4K
	#else
		#ifdef CONFIG_PLATFORM_MSTAR
			#define MAX_RECVBUF_SZ (8192) // 8K
		#else
		#define MAX_RECVBUF_SZ (32768) // 32k
		#endif
		//#define MAX_RECVBUF_SZ (20480) //20K
		//#define MAX_RECVBUF_SZ (10240) //10K 
		//#define MAX_RECVBUF_SZ (16384) //  16k - 92E RX BUF :16K
		//#define MAX_RECVBUF_SZ (8192+1024) // 8K+1k		
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

#define MAX_RECVBUF_SZ (10240)

#endif


// Rx smooth factor
#define Rx_Smooth_Factor (20)

//=============
// [1] Rx Buffer Descriptor (for PCIE) buffer descriptor architecture
//DWORD 0
#define SET_RX_BUFFER_DESC_DATA_LENGTH_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 0, 14, __Value)
#define SET_RX_BUFFER_DESC_LS_92E(__pRxStatusDesc,__Value)	SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 15, 1, __Value)
#define SET_RX_BUFFER_DESC_FS_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 16, 1, __Value)
#define SET_RX_BUFFER_DESC_TOTAL_LENGTH_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 16, 15, __Value)

#define GET_RX_BUFFER_DESC_OWN_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc, 31, 1)
#define GET_RX_BUFFER_DESC_LS_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc, 15, 1)
#define GET_RX_BUFFER_DESC_FS_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc, 16, 1)
#define GET_RX_BUFFER_DESC_TOTAL_LENGTH_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc, 16, 15)


//DWORD 1
#define SET_RX_BUFFER_PHYSICAL_LOW_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc+4, 0, 32, __Value)
#define GET_RX_BUFFER_PHYSICAL_LOW_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 0, 32)

//DWORD 2
#define SET_RX_BUFFER_PHYSICAL_HIGH_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc+8, 0, 32, __Value)

//=============
// [2] Rx Descriptor
//DWORD 0
#define GET_RX_STATUS_DESC_PKT_LEN_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 0, 14)
#define GET_RX_STATUS_DESC_CRC32_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 14, 1)
#define GET_RX_STATUS_DESC_ICVERR_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 15, 1)
#define GET_RX_STATUS_DESC_DRVINFO_SIZE_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc, 16, 4)
#define GET_RX_STATUS_DESC_SECURITY_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 20, 3)
#define GET_RX_STATUS_DESC_QOS_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 23, 1)
#define GET_RX_STATUS_DESC_SHIFT_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 24, 2)
#define GET_RX_STATUS_DESC_PHY_STATUS_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc, 26, 1)
#define GET_RX_STATUS_DESC_SWDEC_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc, 27, 1)
#define GET_RX_STATUS_DESC_EOR_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 30, 1)
#define GET_RX_STATUS_DESC_OWN_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc, 31, 1)


#define SET_RX_STATUS_DESC_PKT_LEN_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 0, 14, __Value)
#define SET_RX_STATUS_DESC_EOR_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 30, 1, __Value)
#define SET_RX_STATUS_DESC_OWN_92E(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE( __pRxStatusDesc, 31, 1, __Value)

//DWORD 1
#define GET_RX_STATUS_DESC_MACID_92E(__pRxDesc) 					LE_BITS_TO_4BYTE(__pRxDesc+4, 0, 7)
#define GET_RX_STATUS_DESC_TID_92E(__pRxDesc) 					LE_BITS_TO_4BYTE(__pRxDesc+4, 8, 4)
#define GET_RX_STATUS_DESC_MACID_VLD_92E(__pRxDesc) 				LE_BITS_TO_4BYTE(__pRxDesc+4, 12, 1)
#define GET_RX_STATUS_DESC_AMSDU_92E(__pRxDesc) 					LE_BITS_TO_4BYTE(__pRxDesc+4, 13, 1)
#define GET_RX_STATUS_DESC_RXID_MATCH_92E(__pRxDesc) 			LE_BITS_TO_4BYTE(__pRxDesc+4, 14, 1)
#define GET_RX_STATUS_DESC_PAGGR_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 15, 1)
#define GET_RX_STATUS_DESC_A1_FITS_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 16, 4)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_CHKERR_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 20, 1)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_IPVER_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 21, 1)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_IS_TCPUDP_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 22, 1)
#define GET_RX_STATUS_DESC_TCPOFFLOAD_CHK_VLD_92E(__pRxStatusDesc)		LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 23, 1)
#define GET_RX_STATUS_DESC_PAM_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 24, 1)
#define GET_RX_STATUS_DESC_PWR_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 25, 1)
#define GET_RX_STATUS_DESC_MORE_DATA_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 26, 1)
#define GET_RX_STATUS_DESC_MORE_FRAG_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 27, 1)
#define GET_RX_STATUS_DESC_TYPE_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 28, 2)
#define GET_RX_STATUS_DESC_MC_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 30, 1)
#define GET_RX_STATUS_DESC_BC_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+4, 31, 1)

//DWORD 2
#define GET_RX_STATUS_DESC_SEQ_92E(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 0, 12)
#define GET_RX_STATUS_DESC_FRAG_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 12, 4)
#define GET_RX_STATUS_DESC_RX_IS_QOS_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 16, 1)

#define GET_RX_STATUS_DESC_WLANHD_IV_LEN_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 18, 6)
#define GET_RX_STATUS_DESC_HWRSVD_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 24, 4)
#define GET_RX_STATUS_DESC_FCS_OK_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 31, 1)
#define GET_RX_STATUS_DESC_RPT_SEL_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+8, 28, 1)

//DWORD 3
#define GET_RX_STATUS_DESC_RX_RATE_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 0, 7)
#define GET_RX_STATUS_DESC_HTC_92E(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 10, 1)
#define GET_RX_STATUS_DESC_EOSP_92E(__pRxStatusDesc)					LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 11, 1)
#define GET_RX_STATUS_DESC_BSSID_FIT_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 12, 2)
#define GET_RX_STATUS_DESC_DMA_AGG_NUM_92E(__pRxStatusDesc)			LE_BITS_TO_4BYTE( __pRxStatusDesc+12, 16, 8)

#define GET_RX_STATUS_DESC_PATTERN_MATCH_92E(__pRxDesc)			LE_BITS_TO_4BYTE( __pRxDesc+12, 29, 1)
#define GET_RX_STATUS_DESC_UNICAST_92E(__pRxDesc)			LE_BITS_TO_4BYTE( __pRxDesc+12, 30, 1)
#define GET_RX_STATUS_DESC_MAGIC_WAKE_92E(__pRxDesc)			LE_BITS_TO_4BYTE( __pRxDesc+12, 31, 1)


//DWORD 5
#define GET_RX_STATUS_DESC_TSFL_92E(__pRxStatusDesc)				LE_BITS_TO_4BYTE( __pRxStatusDesc+20, 0, 32)

#define GET_RX_STATUS_DESC_BUFF_ADDR_92E(__pRxDesc) 		LE_BITS_TO_4BYTE(__pRxDesc+24, 0, 32)
#define GET_RX_STATUS_DESC_BUFF_ADDR64_92E(__pRxDesc) 		LE_BITS_TO_4BYTE(__pRxDesc+28, 0, 32)


#ifdef CONFIG_SDIO_HCI
s32 rtl8812s_init_recv_priv(PADAPTER padapter);
void rtl8812s_free_recv_priv(PADAPTER padapter);
void rtl8812s_recv_hdl(PADAPTER padapter, struct recv_buf *precvbuf);
#endif

#ifdef CONFIG_USB_HCI
#define INTERRUPT_MSG_FORMAT_LEN 60
void rtl8192eu_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf);
s32 rtl8192eu_init_recv_priv(PADAPTER padapter);
void rtl8192eu_free_recv_priv(PADAPTER padapter);
void rtl8192eu_recv_hdl(PADAPTER padapter, struct recv_buf *precvbuf);
void rtl8192eu_recv_tasklet(void *priv);

#endif

#ifdef CONFIG_PCI_HCI
s32 rtl8192ee_init_recv_priv(PADAPTER padapter);
void rtl8192ee_free_recv_priv(PADAPTER padapter);
#endif

void rtl8192e_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc);
void rtl8192e_query_rx_phy_status(union recv_frame *prframe, u8 *pphy_stat);

#endif


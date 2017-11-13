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
#ifndef __RTL8723D_RECV_H__
#define __RTL8723D_RECV_H__

#define RECV_BLK_SZ 512
#define RECV_BLK_CNT 16
#define RECV_BLK_TH RECV_BLK_CNT

#if defined(CONFIG_USB_HCI)

	#ifndef MAX_RECVBUF_SZ
		#ifdef PLATFORM_OS_CE
			#define MAX_RECVBUF_SZ (8192+1024) /* 8K+1k */
		#else
			#ifndef CONFIG_MINIMAL_MEMORY_USAGE
				/* #define MAX_RECVBUF_SZ (32768) */ /* 32k */
				/* #define MAX_RECVBUF_SZ (16384) */ /* 16K */
				/* #define MAX_RECVBUF_SZ (10240) */ /* 10K */
				#ifdef CONFIG_PLATFORM_MSTAR
					#define MAX_RECVBUF_SZ (8192) /* 8K */
				#else
					#define MAX_RECVBUF_SZ (15360) /* 15k < 16k */
				#endif
				/* #define MAX_RECVBUF_SZ (8192+1024) */ /* 8K+1k */
			#else
				#define MAX_RECVBUF_SZ (4000) /* about 4K */
			#endif
		#endif
	#endif /* !MAX_RECVBUF_SZ */

#elif defined(CONFIG_PCI_HCI)
	/* #ifndef CONFIG_MINIMAL_MEMORY_USAGE */
	/*	#define MAX_RECVBUF_SZ (9100) */
	/* #else */
	#define MAX_RECVBUF_SZ (4000) /* about 4K
	* #endif */


#elif defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)

	#define MAX_RECVBUF_SZ (RX_DMA_BOUNDARY_8723D + 1)

#endif

/* Rx smooth factor */
#define	Rx_Smooth_Factor (20)

#ifdef CONFIG_SDIO_HCI
	#ifndef CONFIG_SDIO_RX_COPY
		#undef MAX_RECVBUF_SZ
		#define MAX_RECVBUF_SZ	(RX_DMA_SIZE_8723D - RX_DMA_RESERVED_SIZE_8723D)
	#endif /* !CONFIG_SDIO_RX_COPY */
#endif /* CONFIG_SDIO_HCI */

/*-----------------------------------------------------------------*/
/*	RTL8723D RX BUFFER DESC                                      */
/*-----------------------------------------------------------------*/
/*DWORD 0*/
#define SET_RX_BUFFER_DESC_DATA_LENGTH_8723D(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 0, 14, __Value)
#define SET_RX_BUFFER_DESC_LS_8723D(__pRxStatusDesc, __Value)	SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 15, 1, __Value)
#define SET_RX_BUFFER_DESC_FS_8723D(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 16, 1, __Value)
#define SET_RX_BUFFER_DESC_TOTAL_LENGTH_8723D(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc, 16, 15, __Value)

#define GET_RX_BUFFER_DESC_OWN_8723D(__pRxStatusDesc)		LE_BITS_TO_4BYTE(__pRxStatusDesc, 31, 1)
#define GET_RX_BUFFER_DESC_LS_8723D(__pRxStatusDesc)		LE_BITS_TO_4BYTE(__pRxStatusDesc, 15, 1)
#define GET_RX_BUFFER_DESC_FS_8723D(__pRxStatusDesc)		LE_BITS_TO_4BYTE(__pRxStatusDesc, 16, 1)
#ifdef USING_RX_TAG
	#define GET_RX_BUFFER_DESC_RX_TAG_8723D(__pRxStatusDesc)		LE_BITS_TO_4BYTE(__pRxStatusDesc, 16, 13)
#else
	#define GET_RX_BUFFER_DESC_TOTAL_LENGTH_8723D(__pRxStatusDesc)		LE_BITS_TO_4BYTE(__pRxStatusDesc, 16, 15)
#endif

/*DWORD 1*/
#define SET_RX_BUFFER_PHYSICAL_LOW_8723D(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc+4, 0, 32, __Value)

/*DWORD 2*/
#ifdef CONFIG_64BIT_DMA
	#define SET_RX_BUFFER_PHYSICAL_HIGH_8723D(__pRxStatusDesc, __Value)		SET_BITS_TO_LE_4BYTE(__pRxStatusDesc+8, 0, 32, __Value)
#else
	#define SET_RX_BUFFER_PHYSICAL_HIGH_8723D(__pRxStatusDesc, __Value)
#endif


#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	s32 rtl8723ds_init_recv_priv(PADAPTER padapter);
	void rtl8723ds_free_recv_priv(PADAPTER padapter);
	s32 rtl8723ds_recv_hdl(_adapter *padapter);
#endif

#ifdef CONFIG_USB_HCI
	int rtl8723du_init_recv_priv(_adapter *padapter);
	void rtl8723du_free_recv_priv(_adapter *padapter);
	void rtl8723du_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf);
#endif

#ifdef CONFIG_PCI_HCI
	s32 rtl8723de_init_recv_priv(PADAPTER padapter);
	void rtl8723de_free_recv_priv(PADAPTER padapter);
#endif

void rtl8723d_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc);

#endif /* __RTL8723D_RECV_H__ */

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
#ifndef __RTL8703B_RECV_H__
#define __RTL8703B_RECV_H__

#define RECV_BLK_SZ 512
#define RECV_BLK_CNT 16
#define RECV_BLK_TH RECV_BLK_CNT

#if defined(CONFIG_USB_HCI)

	#ifndef MAX_RECVBUF_SZ
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
	#endif /* !MAX_RECVBUF_SZ */

#elif defined(CONFIG_PCI_HCI)
	/* #ifndef CONFIG_MINIMAL_MEMORY_USAGE */
	/*	#define MAX_RECVBUF_SZ (9100) */
	/* #else */
	#define MAX_RECVBUF_SZ (4000) /* about 4K
	* #endif */


#elif defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)

	#define MAX_RECVBUF_SZ (RX_DMA_SIZE_8703B - RX_DMA_RESERVED_SIZE_8703B)

#endif

/* Rx smooth factor */
#define	Rx_Smooth_Factor (20)

#ifdef CONFIG_SDIO_HCI
	#ifndef CONFIG_SDIO_RX_COPY
		#undef MAX_RECVBUF_SZ
		#define MAX_RECVBUF_SZ	(RX_DMA_SIZE_8703B - RX_DMA_RESERVED_SIZE_8703B)
	#endif /* !CONFIG_SDIO_RX_COPY */
#endif /* CONFIG_SDIO_HCI */

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	s32 rtl8703bs_init_recv_priv(PADAPTER padapter);
	void rtl8703bs_free_recv_priv(PADAPTER padapter);
#endif

#ifdef CONFIG_USB_HCI
	int rtl8703bu_init_recv_priv(_adapter *padapter);
	void rtl8703bu_free_recv_priv(_adapter *padapter);
	void rtl8703bu_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf);
#endif

#ifdef CONFIG_PCI_HCI
	s32 rtl8703be_init_recv_priv(PADAPTER padapter);
	void rtl8703be_free_recv_priv(PADAPTER padapter);
#endif

void rtl8703b_query_rx_desc_status(union recv_frame *precvframe, u8 *pdesc);

#endif /* __RTL8703B_RECV_H__ */

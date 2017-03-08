/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
#ifndef _RTL8822BE_H_
#define _RTL8822BE_H_

#include <drv_types.h>		/* PADAPTER */
#include "../../hal_halmac.h"	/* HALMAC_RX_FIFO_SIZE_8822B */

/* pic buffer descriptor */
#define TX_BD_NUM_8822BE	128
#define RX_BD_NUM_8822BE	128
#define TX_BD_NUM_8822BE_BCN	2
#define TX_BD_NUM_8822BE_CMD	128

#define RTL8822BE_SEG_NUM       1 /* 0:2 seg, 1: 4 seg, 2: 8 seg */

#ifndef MAX_RECVBUF_SZ
	#ifdef PLATFORM_OS_CE
		#define MAX_RECVBUF_SZ (8192+1024)
	#else /* !PLATFORM_OS_CE */
		#ifndef CONFIG_MINIMAL_MEMORY_USAGE
			#define MAX_RECVBUF_SZ (32768)
		#else
			#define MAX_RECVBUF_SZ (4000)
		#endif
	#endif /* PLATFORM_OS_CE */
#endif /* !MAX_RECVBUF_SZ */

#define TX_BUFFER_SEG_NUM	1 /* 0:2 seg, 1: 4 seg, 2: 8 seg. */

#define MAX_RECVBUF_SZ_8822B	HALMAC_RX_FIFO_SIZE_8822B

/* TX BD */
#define SET_TXBUFFER_DESC_LEN_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8), 0, 16, __Valeu)
#define SET_TXBUFFER_DESC_AMSDU_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8), 31, 1, __Valeu)
#define SET_TXBUFFER_DESC_ADD_LOW_WITH_OFFSET(__pTxDesc, __Offset, __Valeu) \
	SET_BITS_TO_LE_4BYTE(__pTxDesc+(__Offset*8)+4, 0, 32, __Valeu)

/* RX BD */
#define SET_RX_BD_PHYSICAL_ADDR_LOW(__pRxBd, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxBd + 0x04, 0, 32, __Value)
#define SET_RX_BD_RXBUFFSIZE(__pRxBd, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxBd + 0x00, 0, 14, __Value)
#define SET_RX_BD_LS(__pRxBd, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxBd + 0x00, 14, 1, __Value)
#define SET_RX_BD_FS(__pRxBd, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxBd + 0x00, 15, 1, __Value)
#define SET_RX_BD_TOTALRXPKTSIZE(__pRxBd, __Value) \
	SET_BITS_TO_LE_4BYTE(__pRxBd + 0x00, 16, 13, __Value)

/* rtl8822be_halinit.c */
u32 rtl8822be_init(PADAPTER);
void rtl8822be_init_default_value(PADAPTER);

/* rtl8822be_halmac.c */
int rtl8822be_halmac_init_adapter(PADAPTER);

/* rtl8822be_io.c */

/* rtl8822be_led.c */
void rtl8822be_initswleds(PADAPTER);
void rtl8822be_deinitswleds(PADAPTER);

/* rtl8822bs_xmit.c */
#define OFFSET_SZ 0

s32 rtl8822be_init_xmit_priv(PADAPTER);
void rtl8822be_free_xmit_priv(PADAPTER);
struct xmit_buf *rtl8822be_dequeue_xmitbuf(struct rtw_tx_ring *);
void rtl8822be_fill_fake_txdesc(PADAPTER, u8 *pDesc, u32 BufferLen,
				u8 IsPsPoll, u8 IsBTQosNull, u8 bDataFrame);
int rtl8822be_init_txbd_ring(PADAPTER, unsigned int q_idx,
			     unsigned int entries);
void rtl8822be_free_txbd_ring(PADAPTER, unsigned int prio);

void rtl8822be_tx_isr(PADAPTER, int prio);

s32 rtl8822be_mgnt_xmit(PADAPTER, struct xmit_frame *);
s32 rtl8822be_hal_xmit(PADAPTER, struct xmit_frame *);
s32 rtl8822be_hal_xmitframe_enqueue(PADAPTER, struct xmit_frame *);

#ifdef CONFIG_XMIT_THREAD_MODE
	s32 rtl8822be_xmit_buf_handler(PADAPTER);
#endif

void rtl8822be_xmitframe_resume(PADAPTER);

/* rtl8822bs_recv.c */
s32 rtl8822be_init_recv_priv(PADAPTER);
void rtl8822be_free_recv_priv(PADAPTER);
int rtl8822be_init_rxbd_ring(PADAPTER);
void rtl8822be_free_rxbd_ring(PADAPTER);
#ifdef CONFIG_NAPI
int rtl8822be_rx_mpdu(_adapter *padapter, int remaing_rxdesc, int budget);
u16 rtl8822be_check_rxdesc_remain(_adapter *padapter, int rx_queue_idx);
#endif

/* rtl8822bs_ops.c */

#endif /* _RTL8822BE_H_ */

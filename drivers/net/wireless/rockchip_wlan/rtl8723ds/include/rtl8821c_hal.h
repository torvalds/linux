/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#ifndef _RTL8821C_HAL_H_
#define _RTL8821C_HAL_H_

#include <osdep_service.h>		/* BIT(x) */
#include "../hal/halmac/halmac_api.h"	/* MAC REG definition */
#include "hal_data.h"
#include "rtl8821c_spec.h"
#include "../hal/rtl8821c/hal8821c_fw.h"

#ifdef CONFIG_USB_HCI
#include <rtl8821cu_hal.h>
#endif
#ifdef CONFIG_SDIO_HCI
#include <rtl8821cs_hal.h>
#endif
#ifdef CONFIG_PCI_HCI
#include <rtl8821ce_hal.h>
#endif

#ifdef CONFIG_SUPPORT_TRX_SHARED
#define FIFO_BLOCK_SIZE		32768 /*@Block size = 32K*/
#define RX_FIFO_EXPANDING	(1 * FIFO_BLOCK_SIZE)
#else
#define RX_FIFO_EXPANDING	0
#endif


#if defined(CONFIG_USB_HCI)

	#ifndef MAX_RECVBUF_SZ
		#ifndef CONFIG_MINIMAL_MEMORY_USAGE
			/* 8821C - RX FIFO :16K ,for RX agg DMA mode = 16K, Rx agg USB mode could large than 16k*/
			/* #define MAX_RECVBUF_SZ		(16384 + RX_FIFO_EXPANDING)*/
			/* For Max throughput issue , need to use USB AGG mode to replace DMA AGG mode*/
			#define MAX_RECVBUF_SZ (32768)

			/*#define MAX_RECVBUF_SZ_8821C (24576)*/ /* 24k*/
			/*#define MAX_RECVBUF_SZ_8821C (20480)*/ /*20K*/
			/*#define MAX_RECVBUF_SZ_8821C (10240) */ /*10K*/
			/*#define MAX_RECVBUF_SZ_8821C (15360)*/ /*15k < 16k*/
			/*#define MAX_RECVBUF_SZ_8821C (8192+1024)*/ /* 8K+1k*/
		#else
			#define MAX_RECVBUF_SZ (4096 + RX_FIFO_EXPANDING) /* about 4K */
		#endif
	#endif/* !MAX_RECVBUF_SZ*/

#elif defined(CONFIG_PCI_HCI)
	/*#ifndef CONFIG_MINIMAL_MEMORY_USAGE
	#define MAX_RECVBUF_SZ (9100)
	#else*/
	#define MAX_RECVBUF_SZ (4096 + RX_FIFO_EXPANDING) /* about 4K */
	/*#endif*/

#elif defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	#define MAX_RECVBUF_SZ	(16384 + RX_FIFO_EXPANDING)
#endif

void init_hal_spec_rtl8821c(PADAPTER);
/* MP Functions */
#ifdef CONFIG_MP_INCLUDED
void rtl8821c_prepare_mp_txdesc(PADAPTER, struct mp_priv *);	/* rtw_mp.c */
void rtl8821c_mp_config_rfpath(PADAPTER);			/* hal_mp.c */
#endif
void rtl8821c_dl_rsvd_page(PADAPTER adapter, u8 mstatus);

#ifdef CONFIG_PCI_HCI
u16 get_txbd_rw_reg(u16 q_idx);
#endif

#endif /* _RTL8821C_HAL_H_ */

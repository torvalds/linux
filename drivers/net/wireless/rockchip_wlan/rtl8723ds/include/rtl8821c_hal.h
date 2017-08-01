/******************************************************************************
 *
 * Copyright(c) 2016 Realtek Corporation. All rights reserved.
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
#ifndef _RTL8821C_HAL_H_
#define _RTL8821C_HAL_H_

#include <osdep_service.h>		/* BIT(x) */
#include "../hal/halmac/halmac_api.h"	/* MAC REG definition */
#include "hal_data.h"

#if defined(CONFIG_USB_HCI)

	#ifndef MAX_RECVBUF_SZ
		#ifndef CONFIG_MINIMAL_MEMORY_USAGE
			#ifdef CONFIG_PLATFORM_MSTAR
				#define MAX_RECVBUF_SZ (8192) /* 8K */
			#else
				/* 8821C - RX FIFO :16K ,for RX agg DMA mode = 16K, Rx agg USB mode could large than 16k*/
				#define MAX_RECVBUF_SZ		HALMAC_RX_FIFO_SIZE_8821C
			#endif
			/*#define MAX_RECVBUF_SZ_8821C (24576)*/ /* 24k*/
			/*#define MAX_RECVBUF_SZ_8821C (20480)*/ /*20K*/
			/*#define MAX_RECVBUF_SZ_8821C (10240) */ /*10K*/
			/*#define MAX_RECVBUF_SZ_8821C (15360)*/ /*15k < 16k*/
			/*#define MAX_RECVBUF_SZ_8821C (8192+1024)*/ /* 8K+1k*/
		#else
			#define MAX_RECVBUF_SZ (4096) /* about 4K */
		#endif
	#endif/* !MAX_RECVBUF_SZ*/

#elif defined(CONFIG_PCI_HCI)
	/*#ifndef CONFIG_MINIMAL_MEMORY_USAGE
	#define MAX_RECVBUF_SZ (9100)
	#else*/
	#define MAX_RECVBUF_SZ (4096) /* about 4K */
	/*#endif*/

#elif defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)

	#define MAX_RECVBUF_SZ	HALMAC_RX_FIFO_SIZE_8821C

#endif

void init_hal_spec_rtl8821c(PADAPTER);
/* MP Functions */
#ifdef CONFIG_MP_INCLUDED
void rtl8821c_phy_init_haldm(PADAPTER);				/* rtw_mp.c */
void rtl8821c_prepare_mp_txdesc(PADAPTER, struct mp_priv *);	/* rtw_mp.c */
void rtl8821c_mp_config_rfpath(PADAPTER);			/* hal_mp.c */
#endif

#endif /* _RTL8821C_HAL_H_ */

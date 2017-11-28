/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#ifndef __GSPI_HAL_H__
#define __GSPI_HAL_H__


void spi_int_dpc(PADAPTER padapter, u32 sdio_hisr);
u8 rtw_set_hal_ops(_adapter *padapter);

#ifdef CONFIG_RTL8188E
	void rtl8188es_set_hal_ops(PADAPTER padapter);
#endif

#ifdef CONFIG_RTL8723B
	void rtl8723bs_set_hal_ops(PADAPTER padapter);
#endif

#endif /* __GSPI_HAL_H__ */

/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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

#include <drv_types.h>
#include <hal_data.h>	/* HAL_DATA_TYPE */

#ifdef CONFIG_SDIO_MULTI_FUNCTION_COEX

int rtw_sdio_multi_state = SDIO_MULTI_WIFI;
EXPORT_SYMBOL(rtw_sdio_multi_state);

bool ex_hal_sdio_multi_if_bus_available(PADAPTER adapter)
{
	return rtw_sdio_multi_state == SDIO_MULTI_WIFI;
}

#endif  /* CONFIG_SDIO_MULTI_FUNCTION_COEX */


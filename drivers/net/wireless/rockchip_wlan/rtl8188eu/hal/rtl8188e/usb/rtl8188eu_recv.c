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
#define _RTL8188EU_RECV_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>

int	rtl8188eu_init_recv_priv(_adapter *padapter)
{
	return usb_init_recv_priv(padapter, INTERRUPT_MSG_FORMAT_LEN);
}

void rtl8188eu_free_recv_priv(_adapter *padapter)
{
	usb_free_recv_priv(padapter, INTERRUPT_MSG_FORMAT_LEN);
}

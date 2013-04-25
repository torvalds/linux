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
#ifndef _RTL8188E_SRESET_H_
#define _RTL8188E_SRESET_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


#ifdef DBG_CONFIG_ERROR_DETECT
#define	WIFI_STATUS_SUCCESS 		0
#define	USB_VEN_REQ_CMD_FAIL 	BIT0
#define	USB_READ_PORT_FAIL 		BIT1
#define	USB_WRITE_PORT_FAIL		BIT2
#define	WIFI_MAC_TXDMA_ERROR 	BIT3			
#define   WIFI_TX_HANG				BIT4
#define	WIFI_RX_HANG				BIT5
#define 	WIFI_IF_NOT_EXIST			BIT6

struct sreset_priv {
	_mutex 	silentreset_mutex;
	u8 	silent_reset_inprogress;
	u8	Wifi_Error_Status;
	unsigned long last_tx_time;
	unsigned long last_tx_complete_time;
};


extern void rtl8188e_sreset_init_value(_adapter *padapter);
extern void rtl8188e_sreset_reset_value(_adapter *padapter);
extern void rtl8188e_silentreset_for_specific_platform(_adapter *padapter);
extern void rtl8188e_sreset_xmit_status_check(_adapter *padapter);
extern void rtl8188e_sreset_linked_status_check(_adapter *padapter);
extern u8 rtl8188e_sreset_get_wifi_status(_adapter *padapter);
#endif
#endif


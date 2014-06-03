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
 ******************************************************************************/
#ifndef _RTW_SRESET_C_
#define _RTW_SRESET_C_

#include <osdep_service.h>
#include <drv_types.h>

enum {
	SRESET_TGP_NULL = 0,
	SRESET_TGP_XMIT_STATUS = 1,
	SRESET_TGP_LINK_STATUS = 2,
};

struct sreset_priv {
	struct mutex	silentreset_mutex;
	u8	silent_reset_inprogress;
	u8	Wifi_Error_Status;
	unsigned long last_tx_time;
	unsigned long last_tx_complete_time;

	s32 dbg_trigger_point;
};

#include <rtl8723a_hal.h>

#define	WIFI_STATUS_SUCCESS	0
#define	USB_VEN_REQ_CMD_FAIL	BIT(0)
#define	USB_READ_PORT_FAIL	BIT(1)
#define	USB_WRITE_PORT_FAIL	BIT(2)
#define	WIFI_MAC_TXDMA_ERROR	BIT(3)
#define	WIFI_TX_HANG		BIT(4)
#define	WIFI_RX_HANG		BIT(5)
#define	WIFI_IF_NOT_EXIST	BIT(6)

void rtw_sreset_init(struct rtw_adapter *padapter);
void rtw_sreset_reset_value(struct rtw_adapter *padapter);
u8 rtw_sreset_get_wifi_status(struct rtw_adapter *padapter);
bool rtw_sreset_inprogress(struct rtw_adapter *padapter);
void sreset_set_wifi_error_status23a(struct rtw_adapter *padapter, u32 status);
void sreset_set_trigger_point(struct rtw_adapter *padapter, s32 tgp);
void rtw_sreset_reset(struct rtw_adapter *active_adapter);

#endif

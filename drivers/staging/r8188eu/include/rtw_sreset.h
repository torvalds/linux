/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#ifndef _RTW_SRESET_C_
#define _RTW_SRESET_C_

#include "osdep_service.h"
#include "drv_types.h"

struct sreset_priv {
	struct mutex	silentreset_mutex;
	u8	wifi_error_status;
	unsigned long last_tx_time;
	unsigned long last_tx_complete_time;
};

#include "rtl8188e_hal.h"

#define	WIFI_STATUS_SUCCESS		0
#define	USB_VEN_REQ_CMD_FAIL	BIT(0)
#define	USB_READ_PORT_FAIL		BIT(1)
#define	USB_WRITE_PORT_FAIL		BIT(2)

void sreset_init_value(struct adapter *padapter);
void sreset_reset_value(struct adapter *padapter);
void sreset_set_wifi_error_status(struct adapter *padapter, u32 status);

#endif

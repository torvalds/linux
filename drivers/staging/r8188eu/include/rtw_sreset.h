/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#ifndef _RTW_SRESET_C_
#define _RTW_SRESET_C_

#include "osdep_service.h"
#include "drv_types.h"

struct sreset_priv {
	unsigned long last_tx_time;
	unsigned long last_tx_complete_time;
};

#include "rtl8188e_hal.h"

void sreset_init_value(struct adapter *padapter);
void sreset_reset_value(struct adapter *padapter);

#endif

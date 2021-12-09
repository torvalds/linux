/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef _RTL8188E_SRESET_H_
#define _RTL8188E_SRESET_H_

#include "osdep_service.h"
#include "drv_types.h"

void rtl8188e_sreset_xmit_status_check(struct adapter *padapter);
void rtl8188e_sreset_linked_status_check(struct adapter *padapter);

#endif

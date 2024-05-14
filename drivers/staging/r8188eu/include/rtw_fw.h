/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_FW_H__
#define __RTW_FW_H__

struct rt_firmware {
	u8 *data;
	u32 size;
};

#include "drv_types.h"

int rtl8188e_firmware_download(struct adapter *padapter);
void rtw_reset_8051(struct adapter *padapter);

#endif

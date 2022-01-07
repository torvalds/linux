/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_FW_H__
#define __RTW_FW_H__

#include "drv_types.h"

#define MAX_REG_BOLCK_SIZE	196
#define FW_8188E_START_ADDRESS	0x1000
#define MAX_PAGE_SIZE		4096

#define IS_FW_HEADER_EXIST(_fwhdr)				\
	((le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x92C0 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x88C0 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x2300 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x88E0)

int rtl8188e_firmware_download(struct adapter *padapter);
void rtw_reset_8051(struct adapter *padapter);

#endif

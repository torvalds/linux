// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Realtek PCI-Express card reader
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * Author:
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#include "general.h"

int bit1cnt_long(u32 data)
{
	int i, cnt = 0;

	for (i = 0; i < 32; i++) {
		if (data & 0x01)
			cnt++;
		data >>= 1;
	}
	return cnt;
}


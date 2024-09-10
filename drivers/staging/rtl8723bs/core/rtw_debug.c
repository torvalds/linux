// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <drv_types.h>
#include <rtw_debug.h>
#include <hal_btcoex.h>

#include <rtw_version.h>

static void dump_4_regs(struct adapter *adapter, int offset)
{
	u32 reg[4];
	int i;

	for (i = 0; i < 4; i++)
		reg[i] = rtw_read32(adapter, offset + i);

	netdev_dbg(adapter->pnetdev, "0x%03x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   i, reg[0], reg[1], reg[2], reg[3]);
}

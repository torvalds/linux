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

void mac_reg_dump(struct adapter *adapter)
{
	int i;

	netdev_dbg(adapter->pnetdev, "======= MAC REG =======\n");

	for (i = 0x0; i < 0x800; i += 4)
		dump_4_regs(adapter, i);
}

void bb_reg_dump(struct adapter *adapter)
{
	int i;

	netdev_dbg(adapter->pnetdev, "======= BB REG =======\n");

	for (i = 0x800; i < 0x1000 ; i += 4)
		dump_4_regs(adapter, i);
}

static void dump_4_rf_regs(struct adapter *adapter, int path, int offset)
{
	u8 reg[4];
	int i;

	for (i = 0; i < 4; i++)
		reg[i] = rtw_hal_read_rfreg(adapter, path, offset + i,
					    0xffffffff);

	netdev_dbg(adapter->pnetdev, "0x%02x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		   i, reg[0], reg[1], reg[2], reg[3]);
}

void rf_reg_dump(struct adapter *adapter)
{
	int i, path = 0;

	netdev_dbg(adapter->pnetdev, "======= RF REG =======\n");

	netdev_dbg(adapter->pnetdev, "RF_Path(%x)\n", path);
	for (i = 0; i < 0x100; i++)
		dump_4_rf_regs(adapter, path, i);
}

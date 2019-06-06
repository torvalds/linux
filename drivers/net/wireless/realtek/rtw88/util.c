// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "util.h"
#include "reg.h"

bool check_hw_ready(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 target)
{
	u32 cnt;

	for (cnt = 0; cnt < 1000; cnt++) {
		if (rtw_read32_mask(rtwdev, addr, mask) == target)
			return true;

		udelay(10);
	}

	return false;
}

bool ltecoex_read_reg(struct rtw_dev *rtwdev, u16 offset, u32 *val)
{
	if (!check_hw_ready(rtwdev, LTECOEX_ACCESS_CTRL, LTECOEX_READY, 1))
		return false;

	rtw_write32(rtwdev, LTECOEX_ACCESS_CTRL, 0x800F0000 | offset);
	*val = rtw_read32(rtwdev, LTECOEX_READ_DATA);

	return true;
}

bool ltecoex_reg_write(struct rtw_dev *rtwdev, u16 offset, u32 value)
{
	if (!check_hw_ready(rtwdev, LTECOEX_ACCESS_CTRL, LTECOEX_READY, 1))
		return false;

	rtw_write32(rtwdev, LTECOEX_WRITE_DATA, value);
	rtw_write32(rtwdev, LTECOEX_ACCESS_CTRL, 0xC00F0000 | offset);

	return true;
}

void rtw_restore_reg(struct rtw_dev *rtwdev,
		     struct rtw_backup_info *bckp, u32 num)
{
	u8 len;
	u32 reg;
	u32 val;
	int i;

	for (i = 0; i < num; i++, bckp++) {
		len = bckp->len;
		reg = bckp->reg;
		val = bckp->val;

		switch (len) {
		case 1:
			rtw_write8(rtwdev, reg, (u8)val);
			break;
		case 2:
			rtw_write16(rtwdev, reg, (u16)val);
			break;
		case 4:
			rtw_write32(rtwdev, reg, (u32)val);
			break;
		default:
			break;
		}
	}
}

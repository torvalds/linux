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
EXPORT_SYMBOL(check_hw_ready);

bool ltecoex_read_reg(struct rtw_dev *rtwdev, u16 offset, u32 *val)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_ltecoex_addr *ltecoex = chip->ltecoex_addr;

	if (!check_hw_ready(rtwdev, ltecoex->ctrl, LTECOEX_READY, 1))
		return false;

	rtw_write32(rtwdev, ltecoex->ctrl, 0x800F0000 | offset);
	*val = rtw_read32(rtwdev, ltecoex->rdata);

	return true;
}

bool ltecoex_reg_write(struct rtw_dev *rtwdev, u16 offset, u32 value)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_ltecoex_addr *ltecoex = chip->ltecoex_addr;

	if (!check_hw_ready(rtwdev, ltecoex->ctrl, LTECOEX_READY, 1))
		return false;

	rtw_write32(rtwdev, ltecoex->wdata, value);
	rtw_write32(rtwdev, ltecoex->ctrl, 0xC00F0000 | offset);

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
EXPORT_SYMBOL(rtw_restore_reg);

void rtw_desc_to_mcsrate(u16 rate, u8 *mcs, u8 *nss)
{
	if (rate <= DESC_RATE54M)
		return;

	if (rate >= DESC_RATEVHT1SS_MCS0 &&
	    rate <= DESC_RATEVHT1SS_MCS9) {
		*nss = 1;
		*mcs = rate - DESC_RATEVHT1SS_MCS0;
	} else if (rate >= DESC_RATEVHT2SS_MCS0 &&
		   rate <= DESC_RATEVHT2SS_MCS9) {
		*nss = 2;
		*mcs = rate - DESC_RATEVHT2SS_MCS0;
	} else if (rate >= DESC_RATEVHT3SS_MCS0 &&
		   rate <= DESC_RATEVHT3SS_MCS9) {
		*nss = 3;
		*mcs = rate - DESC_RATEVHT3SS_MCS0;
	} else if (rate >= DESC_RATEVHT4SS_MCS0 &&
		   rate <= DESC_RATEVHT4SS_MCS9) {
		*nss = 4;
		*mcs = rate - DESC_RATEVHT4SS_MCS0;
	} else if (rate >= DESC_RATEMCS0 &&
		   rate <= DESC_RATEMCS15) {
		*mcs = rate - DESC_RATEMCS0;
	}
}

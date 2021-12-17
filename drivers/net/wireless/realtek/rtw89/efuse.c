// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "debug.h"
#include "efuse.h"
#include "reg.h"

enum rtw89_efuse_bank {
	RTW89_EFUSE_BANK_WIFI,
	RTW89_EFUSE_BANK_BT,
};

static int rtw89_switch_efuse_bank(struct rtw89_dev *rtwdev,
				   enum rtw89_efuse_bank bank)
{
	u8 val;

	val = rtw89_read32_mask(rtwdev, R_AX_EFUSE_CTRL_1,
				B_AX_EF_CELL_SEL_MASK);
	if (bank == val)
		return 0;

	rtw89_write32_mask(rtwdev, R_AX_EFUSE_CTRL_1, B_AX_EF_CELL_SEL_MASK,
			   bank);

	val = rtw89_read32_mask(rtwdev, R_AX_EFUSE_CTRL_1,
				B_AX_EF_CELL_SEL_MASK);
	if (bank == val)
		return 0;

	return -EBUSY;
}

static int rtw89_dump_physical_efuse_map(struct rtw89_dev *rtwdev, u8 *map,
					 u32 dump_addr, u32 dump_size)
{
	u32 efuse_ctl;
	u32 addr;
	int ret;

	rtw89_switch_efuse_bank(rtwdev, RTW89_EFUSE_BANK_WIFI);

	for (addr = dump_addr; addr < dump_addr + dump_size; addr++) {
		efuse_ctl = u32_encode_bits(addr, B_AX_EF_ADDR_MASK);
		rtw89_write32(rtwdev, R_AX_EFUSE_CTRL, efuse_ctl & ~B_AX_EF_RDY);

		ret = read_poll_timeout_atomic(rtw89_read32, efuse_ctl,
					       efuse_ctl & B_AX_EF_RDY, 1, 1000000,
					       true, rtwdev, R_AX_EFUSE_CTRL);
		if (ret)
			return -EBUSY;

		*map++ = (u8)(efuse_ctl & 0xff);
	}

	return 0;
}

#define invalid_efuse_header(hdr1, hdr2) \
	((hdr1) == 0xff || (hdr2) == 0xff)
#define invalid_efuse_content(word_en, i) \
	(((word_en) & BIT(i)) != 0x0)
#define get_efuse_blk_idx(hdr1, hdr2) \
	((((hdr2) & 0xf0) >> 4) | (((hdr1) & 0x0f) << 4))
#define block_idx_to_logical_idx(blk_idx, i) \
	(((blk_idx) << 3) + ((i) << 1))
static int rtw89_dump_logical_efuse_map(struct rtw89_dev *rtwdev, u8 *phy_map,
					u8 *log_map)
{
	u32 physical_size = rtwdev->chip->physical_efuse_size;
	u32 logical_size = rtwdev->chip->logical_efuse_size;
	u8 sec_ctrl_size = rtwdev->chip->sec_ctrl_efuse_size;
	u32 phy_idx = sec_ctrl_size;
	u32 log_idx;
	u8 hdr1, hdr2;
	u8 blk_idx;
	u8 word_en;
	int i;

	while (phy_idx < physical_size - sec_ctrl_size) {
		hdr1 = phy_map[phy_idx];
		hdr2 = phy_map[phy_idx + 1];
		if (invalid_efuse_header(hdr1, hdr2))
			break;

		blk_idx = get_efuse_blk_idx(hdr1, hdr2);
		word_en = hdr2 & 0xf;
		phy_idx += 2;

		for (i = 0; i < 4; i++) {
			if (invalid_efuse_content(word_en, i))
				continue;

			log_idx = block_idx_to_logical_idx(blk_idx, i);
			if (phy_idx + 1 > physical_size - sec_ctrl_size - 1 ||
			    log_idx + 1 > logical_size)
				return -EINVAL;

			log_map[log_idx] = phy_map[phy_idx];
			log_map[log_idx + 1] = phy_map[phy_idx + 1];
			phy_idx += 2;
		}
	}
	return 0;
}

int rtw89_parse_efuse_map(struct rtw89_dev *rtwdev)
{
	u32 phy_size = rtwdev->chip->physical_efuse_size;
	u32 log_size = rtwdev->chip->logical_efuse_size;
	u8 *phy_map = NULL;
	u8 *log_map = NULL;
	int ret;

	if (rtw89_read16(rtwdev, R_AX_SYS_WL_EFUSE_CTRL) & B_AX_AUTOLOAD_SUS)
		rtwdev->efuse.valid = true;
	else
		rtw89_warn(rtwdev, "failed to check efuse autoload\n");

	phy_map = kmalloc(phy_size, GFP_KERNEL);
	log_map = kmalloc(log_size, GFP_KERNEL);

	if (!phy_map || !log_map) {
		ret = -ENOMEM;
		goto out_free;
	}

	ret = rtw89_dump_physical_efuse_map(rtwdev, phy_map, 0, phy_size);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse physical map\n");
		goto out_free;
	}

	memset(log_map, 0xff, log_size);
	ret = rtw89_dump_logical_efuse_map(rtwdev, phy_map, log_map);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse logical map\n");
		goto out_free;
	}

	rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "log_map: ", log_map, log_size);

	ret = rtwdev->chip->ops->read_efuse(rtwdev, log_map);
	if (ret) {
		rtw89_warn(rtwdev, "failed to read efuse map\n");
		goto out_free;
	}

out_free:
	kfree(log_map);
	kfree(phy_map);

	return ret;
}

int rtw89_parse_phycap_map(struct rtw89_dev *rtwdev)
{
	u32 phycap_addr = rtwdev->chip->phycap_addr;
	u32 phycap_size = rtwdev->chip->phycap_size;
	u8 *phycap_map = NULL;
	int ret = 0;

	if (!phycap_size)
		return 0;

	phycap_map = kmalloc(phycap_size, GFP_KERNEL);
	if (!phycap_map)
		return -ENOMEM;

	ret = rtw89_dump_physical_efuse_map(rtwdev, phycap_map,
					    phycap_addr, phycap_size);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump phycap map\n");
		goto out_free;
	}

	ret = rtwdev->chip->ops->read_phycap(rtwdev, phycap_map);
	if (ret) {
		rtw89_warn(rtwdev, "failed to read phycap map\n");
		goto out_free;
	}

out_free:
	kfree(phycap_map);

	return ret;
}

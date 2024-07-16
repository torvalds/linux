// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#include <linux/iopoll.h>

#include "main.h"
#include "efuse.h"
#include "reg.h"
#include "debug.h"

#define RTW_EFUSE_BANK_WIFI		0x0

static void switch_efuse_bank(struct rtw_dev *rtwdev)
{
	rtw_write32_mask(rtwdev, REG_LDO_EFUSE_CTRL, BIT_MASK_EFUSE_BANK_SEL,
			 RTW_EFUSE_BANK_WIFI);
}

#define invalid_efuse_header(hdr1, hdr2) \
	((hdr1) == 0xff || (((hdr1) & 0x1f) == 0xf && (hdr2) == 0xff))
#define invalid_efuse_content(word_en, i) \
	(((word_en) & BIT(i)) != 0x0)
#define get_efuse_blk_idx_2_byte(hdr1, hdr2) \
	((((hdr2) & 0xf0) >> 1) | (((hdr1) >> 5) & 0x07))
#define get_efuse_blk_idx_1_byte(hdr1) \
	(((hdr1) & 0xf0) >> 4)
#define block_idx_to_logical_idx(blk_idx, i) \
	(((blk_idx) << 3) + ((i) << 1))

/* efuse header format
 *
 * | 7        5   4    0 | 7        4   3          0 | 15  8  7   0 |
 *   block[2:0]   0 1111   block[6:3]   word_en[3:0]   byte0  byte1
 * | header 1 (optional) |          header 2         |    word N    |
 *
 * word_en: 4 bits each word. 0 -> write; 1 -> not write
 * N: 1~4, depends on word_en
 */
static int rtw_dump_logical_efuse_map(struct rtw_dev *rtwdev, u8 *phy_map,
				      u8 *log_map)
{
	u32 physical_size = rtwdev->efuse.physical_size;
	u32 protect_size = rtwdev->efuse.protect_size;
	u32 logical_size = rtwdev->efuse.logical_size;
	u32 phy_idx, log_idx;
	u8 hdr1, hdr2;
	u8 blk_idx;
	u8 word_en;
	int i;

	for (phy_idx = 0; phy_idx < physical_size - protect_size;) {
		hdr1 = phy_map[phy_idx];
		hdr2 = phy_map[phy_idx + 1];
		if (invalid_efuse_header(hdr1, hdr2))
			break;

		if ((hdr1 & 0x1f) == 0xf) {
			/* 2-byte header format */
			blk_idx = get_efuse_blk_idx_2_byte(hdr1, hdr2);
			word_en = hdr2 & 0xf;
			phy_idx += 2;
		} else {
			/* 1-byte header format */
			blk_idx = get_efuse_blk_idx_1_byte(hdr1);
			word_en = hdr1 & 0xf;
			phy_idx += 1;
		}

		for (i = 0; i < 4; i++) {
			if (invalid_efuse_content(word_en, i))
				continue;

			log_idx = block_idx_to_logical_idx(blk_idx, i);
			if (phy_idx + 1 > physical_size - protect_size ||
			    log_idx + 1 > logical_size)
				return -EINVAL;

			log_map[log_idx] = phy_map[phy_idx];
			log_map[log_idx + 1] = phy_map[phy_idx + 1];
			phy_idx += 2;
		}
	}
	return 0;
}

static int rtw_dump_physical_efuse_map(struct rtw_dev *rtwdev, u8 *map)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	u32 size = rtwdev->efuse.physical_size;
	u32 efuse_ctl;
	u32 addr;
	u32 cnt;

	rtw_chip_efuse_grant_on(rtwdev);

	switch_efuse_bank(rtwdev);

	/* disable 2.5V LDO */
	chip->ops->cfg_ldo25(rtwdev, false);

	efuse_ctl = rtw_read32(rtwdev, REG_EFUSE_CTRL);

	for (addr = 0; addr < size; addr++) {
		efuse_ctl &= ~(BIT_MASK_EF_DATA | BITS_EF_ADDR);
		efuse_ctl |= (addr & BIT_MASK_EF_ADDR) << BIT_SHIFT_EF_ADDR;
		rtw_write32(rtwdev, REG_EFUSE_CTRL, efuse_ctl & (~BIT_EF_FLAG));

		cnt = 1000000;
		do {
			udelay(1);
			efuse_ctl = rtw_read32(rtwdev, REG_EFUSE_CTRL);
			if (--cnt == 0)
				return -EBUSY;
		} while (!(efuse_ctl & BIT_EF_FLAG));

		*(map + addr) = (u8)(efuse_ctl & BIT_MASK_EF_DATA);
	}

	rtw_chip_efuse_grant_off(rtwdev);

	return 0;
}

int rtw_read8_physical_efuse(struct rtw_dev *rtwdev, u16 addr, u8 *data)
{
	u32 efuse_ctl;
	int ret;

	rtw_write32_mask(rtwdev, REG_EFUSE_CTRL, 0x3ff00, addr);
	rtw_write32_clr(rtwdev, REG_EFUSE_CTRL, BIT_EF_FLAG);

	ret = read_poll_timeout(rtw_read32, efuse_ctl, efuse_ctl & BIT_EF_FLAG,
				1000, 100000, false, rtwdev, REG_EFUSE_CTRL);
	if (ret) {
		*data = EFUSE_READ_FAIL;
		return ret;
	}

	*data = rtw_read8(rtwdev, REG_EFUSE_CTRL);

	return 0;
}
EXPORT_SYMBOL(rtw_read8_physical_efuse);

int rtw_parse_efuse_map(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u32 phy_size = efuse->physical_size;
	u32 log_size = efuse->logical_size;
	u8 *phy_map = NULL;
	u8 *log_map = NULL;
	int ret = 0;

	phy_map = kmalloc(phy_size, GFP_KERNEL);
	log_map = kmalloc(log_size, GFP_KERNEL);
	if (!phy_map || !log_map) {
		ret = -ENOMEM;
		goto out_free;
	}

	ret = rtw_dump_physical_efuse_map(rtwdev, phy_map);
	if (ret) {
		rtw_err(rtwdev, "failed to dump efuse physical map\n");
		goto out_free;
	}

	memset(log_map, 0xff, log_size);
	ret = rtw_dump_logical_efuse_map(rtwdev, phy_map, log_map);
	if (ret) {
		rtw_err(rtwdev, "failed to dump efuse logical map\n");
		goto out_free;
	}

	ret = chip->ops->read_efuse(rtwdev, log_map);
	if (ret) {
		rtw_err(rtwdev, "failed to read efuse map\n");
		goto out_free;
	}

out_free:
	kfree(log_map);
	kfree(phy_map);

	return ret;
}

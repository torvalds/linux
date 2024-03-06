// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2023  Realtek Corporation
 */

#include "debug.h"
#include "efuse.h"
#include "mac.h"
#include "reg.h"

static void rtw89_enable_efuse_pwr_cut_ddv_be(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	bool aphy_patch = true;

	if (chip->chip_id == RTL8922A && hal->cv == CHIP_CAV)
		aphy_patch = false;

	rtw89_write8_set(rtwdev, R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);

	if (aphy_patch) {
		rtw89_write16_set(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_S);
		mdelay(1);
		rtw89_write16_set(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_B);
		rtw89_write16_clr(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_ISO_EB2CORE);
	}

	rtw89_write32_set(rtwdev, R_BE_EFUSE_CTRL_2_V1, B_BE_EF_BURST);
}

static void rtw89_disable_efuse_pwr_cut_ddv_be(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	bool aphy_patch = true;

	if (chip->chip_id == RTL8922A && hal->cv == CHIP_CAV)
		aphy_patch = false;

	if (aphy_patch) {
		rtw89_write16_set(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_ISO_EB2CORE);
		rtw89_write16_clr(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_B);
		mdelay(1);
		rtw89_write16_clr(rtwdev, R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_S);
	}

	rtw89_write8_clr(rtwdev, R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);
	rtw89_write32_clr(rtwdev, R_BE_EFUSE_CTRL_2_V1, B_BE_EF_BURST);
}

static int rtw89_dump_physical_efuse_map_ddv_be(struct rtw89_dev *rtwdev, u8 *map,
						u32 dump_addr, u32 dump_size)
{
	u32 efuse_ctl;
	u32 addr;
	u32 data;
	int ret;

	if (!IS_ALIGNED(dump_addr, 4) || !IS_ALIGNED(dump_size, 4)) {
		rtw89_err(rtwdev, "Efuse addr 0x%x or size 0x%x not aligned\n",
			  dump_addr, dump_size);
		return -EINVAL;
	}

	rtw89_enable_efuse_pwr_cut_ddv_be(rtwdev);

	for (addr = dump_addr; addr < dump_addr + dump_size; addr += 4, map += 4) {
		efuse_ctl = u32_encode_bits(addr, B_BE_EF_ADDR_MASK);
		rtw89_write32(rtwdev, R_BE_EFUSE_CTRL, efuse_ctl & ~B_BE_EF_RDY);

		ret = read_poll_timeout_atomic(rtw89_read32, efuse_ctl,
					       efuse_ctl & B_BE_EF_RDY, 1, 1000000,
					       true, rtwdev, R_BE_EFUSE_CTRL);
		if (ret)
			return -EBUSY;

		data = rtw89_read32(rtwdev, R_BE_EFUSE_CTRL_1_V1);
		*((__le32 *)map) = cpu_to_le32(data);
	}

	rtw89_disable_efuse_pwr_cut_ddv_be(rtwdev);

	return 0;
}

static int rtw89_dump_physical_efuse_map_dav_be(struct rtw89_dev *rtwdev, u8 *map,
						u32 dump_addr, u32 dump_size)
{
	u32 addr;
	u8 val8;
	int err;
	int ret;

	for (addr = dump_addr; addr < dump_addr + dump_size; addr++) {
		ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_CTRL, 0x40,
					      FULL_BIT_MASK);
		if (ret)
			return ret;
		ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_LOW_ADDR, addr & 0xff,
					      XTAL_SI_LOW_ADDR_MASK);
		if (ret)
			return ret;
		ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_CTRL, addr >> 8,
					      XTAL_SI_HIGH_ADDR_MASK);
		if (ret)
			return ret;
		ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_CTRL, 0,
					      XTAL_SI_MODE_SEL_MASK);
		if (ret)
			return ret;

		ret = read_poll_timeout_atomic(rtw89_mac_read_xtal_si, err,
					       !err && (val8 & XTAL_SI_RDY),
					       1, 10000, false,
					       rtwdev, XTAL_SI_CTRL, &val8);
		if (ret) {
			rtw89_warn(rtwdev, "failed to read dav efuse\n");
			return ret;
		}

		ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_READ_VAL, &val8);
		if (ret)
			return ret;
		*map++ = val8;
	}

	return 0;
}

int rtw89_cnv_efuse_state_be(struct rtw89_dev *rtwdev, bool idle)
{
	u32 val;
	int ret = 0;

	if (idle) {
		rtw89_write32_set(rtwdev, R_BE_WL_BT_PWR_CTRL, B_BE_BT_DISN_EN);
	} else {
		rtw89_write32_clr(rtwdev, R_BE_WL_BT_PWR_CTRL, B_BE_BT_DISN_EN);

		ret = read_poll_timeout(rtw89_read32_mask, val,
					val == MAC_AX_SYS_ACT, 50, 5000,
					false, rtwdev, R_BE_IC_PWR_STATE,
					B_BE_WHOLE_SYS_PWR_STE_MASK);
		if (ret)
			rtw89_warn(rtwdev, "failed to convert efuse state\n");
	}

	return ret;
}

static int rtw89_dump_physical_efuse_map_be(struct rtw89_dev *rtwdev, u8 *map,
					    u32 dump_addr, u32 dump_size, bool dav)
{
	int ret;

	if (!map || dump_size == 0)
		return 0;

	rtw89_cnv_efuse_state_be(rtwdev, false);

	if (dav) {
		ret = rtw89_dump_physical_efuse_map_dav_be(rtwdev, map,
							   dump_addr, dump_size);
		if (ret)
			return ret;

		rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "phy_map dav: ", map, dump_size);
	} else {
		ret = rtw89_dump_physical_efuse_map_ddv_be(rtwdev, map,
							   dump_addr, dump_size);
		if (ret)
			return ret;

		rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "phy_map ddv: ", map, dump_size);
	}

	rtw89_cnv_efuse_state_be(rtwdev, true);

	return 0;
}

#define EFUSE_HDR_CONST_MASK GENMASK(23, 20)
#define EFUSE_HDR_PAGE_MASK GENMASK(19, 17)
#define EFUSE_HDR_OFFSET_MASK GENMASK(16, 4)
#define EFUSE_HDR_OFFSET_DAV_MASK GENMASK(11, 4)
#define EFUSE_HDR_WORD_EN_MASK GENMASK(3, 0)

#define invalid_efuse_header_be(hdr1, hdr2, hdr3) \
	((hdr1) == 0xff || (hdr2) == 0xff || (hdr3) == 0xff)
#define invalid_efuse_content_be(word_en, i) \
	(((word_en) & BIT(i)) != 0x0)
#define get_efuse_blk_idx_be(hdr1, hdr2, hdr3) \
	(((hdr1) << 16) | ((hdr2) << 8) | (hdr3))
#define block_idx_to_logical_idx_be(blk_idx, i) \
	(((blk_idx) << 3) + ((i) << 1))

#define invalid_efuse_header_dav_be(hdr1, hdr2) \
	((hdr1) == 0xff || (hdr2) == 0xff)
#define get_efuse_blk_idx_dav_be(hdr1, hdr2) \
	(((hdr1) << 8) | (hdr2))

static int rtw89_eeprom_parser_be(struct rtw89_dev *rtwdev,
				  const u8 *phy_map, u32 phy_size, u8 *log_map,
				  const struct rtw89_efuse_block_cfg *efuse_block)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	enum rtw89_efuse_block blk_page, page;
	u32 size = efuse_block->size;
	u32 phy_idx, log_idx;
	u32 hdr, page_offset;
	u8 hdr1, hdr2, hdr3;
	u8 i, val0, val1;
	u32 min, max;
	u16 blk_idx;
	u8 word_en;

	page = u32_get_bits(efuse_block->offset, RTW89_EFUSE_BLOCK_ID_MASK);
	page_offset = u32_get_bits(efuse_block->offset, RTW89_EFUSE_BLOCK_SIZE_MASK);

	min = ALIGN_DOWN(page_offset, 2);
	max = ALIGN(page_offset + size, 2);

	memset(log_map, 0xff, size);

	phy_idx = chip->sec_ctrl_efuse_size;

	do {
		if (page == RTW89_EFUSE_BLOCK_ADIE) {
			hdr1 = phy_map[phy_idx];
			hdr2 = phy_map[phy_idx + 1];
			if (invalid_efuse_header_dav_be(hdr1, hdr2))
				break;

			phy_idx += 2;

			hdr = get_efuse_blk_idx_dav_be(hdr1, hdr2);

			blk_page = RTW89_EFUSE_BLOCK_ADIE;
			blk_idx = u32_get_bits(hdr, EFUSE_HDR_OFFSET_DAV_MASK);
			word_en = u32_get_bits(hdr, EFUSE_HDR_WORD_EN_MASK);
		} else {
			hdr1 = phy_map[phy_idx];
			hdr2 = phy_map[phy_idx + 1];
			hdr3 = phy_map[phy_idx + 2];
			if (invalid_efuse_header_be(hdr1, hdr2, hdr3))
				break;

			phy_idx += 3;

			hdr = get_efuse_blk_idx_be(hdr1, hdr2, hdr3);

			blk_page = u32_get_bits(hdr, EFUSE_HDR_PAGE_MASK);
			blk_idx = u32_get_bits(hdr, EFUSE_HDR_OFFSET_MASK);
			word_en = u32_get_bits(hdr, EFUSE_HDR_WORD_EN_MASK);
		}

		if (blk_idx >= RTW89_EFUSE_MAX_BLOCK_SIZE >> 3) {
			rtw89_err(rtwdev, "[ERR]efuse idx:0x%X\n", phy_idx - 3);
			rtw89_err(rtwdev, "[ERR]read hdr:0x%X\n", hdr);
			return -EINVAL;
		}

		for (i = 0; i < 4; i++) {
			if (invalid_efuse_content_be(word_en, i))
				continue;

			if (phy_idx >= phy_size - 1)
				return -EINVAL;

			log_idx = block_idx_to_logical_idx_be(blk_idx, i);

			if (blk_page == page && log_idx >= min && log_idx < max) {
				val0 = phy_map[phy_idx];
				val1 = phy_map[phy_idx + 1];

				if (log_idx == min && page_offset > min) {
					log_map[log_idx - page_offset + 1] = val1;
				} else if (log_idx + 2 == max &&
					   page_offset + size < max) {
					log_map[log_idx - page_offset] = val0;
				} else {
					log_map[log_idx - page_offset] = val0;
					log_map[log_idx - page_offset + 1] = val1;
				}
			}
			phy_idx += 2;
		}
	} while (phy_idx < phy_size);

	return 0;
}

static int rtw89_parse_logical_efuse_block_be(struct rtw89_dev *rtwdev,
					      const u8 *phy_map, u32 phy_size,
					      enum rtw89_efuse_block block)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_efuse_block_cfg *efuse_block;
	u8 *log_map;
	int ret;

	efuse_block = &chip->efuse_blocks[block];

	log_map = kmalloc(efuse_block->size, GFP_KERNEL);
	if (!log_map)
		return -ENOMEM;

	ret = rtw89_eeprom_parser_be(rtwdev, phy_map, phy_size, log_map, efuse_block);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse logical block %d\n", block);
		goto out_free;
	}

	rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "log_map: ", log_map, efuse_block->size);

	ret = rtwdev->chip->ops->read_efuse(rtwdev, log_map, block);
	if (ret) {
		rtw89_warn(rtwdev, "failed to read efuse map\n");
		goto out_free;
	}

out_free:
	kfree(log_map);

	return ret;
}

int rtw89_parse_efuse_map_be(struct rtw89_dev *rtwdev)
{
	u32 phy_size = rtwdev->chip->physical_efuse_size;
	u32 dav_phy_size = rtwdev->chip->dav_phy_efuse_size;
	enum rtw89_efuse_block block;
	u8 *phy_map = NULL;
	u8 *dav_phy_map = NULL;
	int ret;

	if (rtw89_read16(rtwdev, R_BE_SYS_WL_EFUSE_CTRL) & B_BE_AUTOLOAD_SUS)
		rtwdev->efuse.valid = true;
	else
		rtw89_warn(rtwdev, "failed to check efuse autoload\n");

	phy_map = kmalloc(phy_size, GFP_KERNEL);
	if (dav_phy_size)
		dav_phy_map = kmalloc(dav_phy_size, GFP_KERNEL);

	if (!phy_map || (dav_phy_size && !dav_phy_map)) {
		ret = -ENOMEM;
		goto out_free;
	}

	ret = rtw89_dump_physical_efuse_map_be(rtwdev, phy_map, 0, phy_size, false);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse physical map\n");
		goto out_free;
	}
	ret = rtw89_dump_physical_efuse_map_be(rtwdev, dav_phy_map, 0, dav_phy_size, true);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse dav physical map\n");
		goto out_free;
	}

	if (rtwdev->hci.type == RTW89_HCI_TYPE_USB)
		block = RTW89_EFUSE_BLOCK_HCI_DIG_USB;
	else
		block = RTW89_EFUSE_BLOCK_HCI_DIG_PCIE_SDIO;

	ret = rtw89_parse_logical_efuse_block_be(rtwdev, phy_map, phy_size, block);
	if (ret) {
		rtw89_warn(rtwdev, "failed to parse efuse logic block %d\n",
			   RTW89_EFUSE_BLOCK_HCI_DIG_PCIE_SDIO);
		goto out_free;
	}

	ret = rtw89_parse_logical_efuse_block_be(rtwdev, phy_map, phy_size,
						 RTW89_EFUSE_BLOCK_RF);
	if (ret) {
		rtw89_warn(rtwdev, "failed to parse efuse logic block %d\n",
			   RTW89_EFUSE_BLOCK_RF);
		goto out_free;
	}

out_free:
	kfree(dav_phy_map);
	kfree(phy_map);

	return ret;
}

int rtw89_parse_phycap_map_be(struct rtw89_dev *rtwdev)
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

	ret = rtw89_dump_physical_efuse_map_be(rtwdev, phycap_map,
					       phycap_addr, phycap_size, false);
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

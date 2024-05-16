// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "debug.h"
#include "efuse.h"
#include "mac.h"
#include "reg.h"

#define EF_FV_OFSET 0x5ea
#define EF_CV_MASK GENMASK(7, 4)
#define EF_CV_INV 15

enum rtw89_efuse_bank {
	RTW89_EFUSE_BANK_WIFI,
	RTW89_EFUSE_BANK_BT,
};

static int rtw89_switch_efuse_bank(struct rtw89_dev *rtwdev,
				   enum rtw89_efuse_bank bank)
{
	u8 val;

	if (rtwdev->chip->chip_id != RTL8852A)
		return 0;

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

static void rtw89_enable_otp_burst_mode(struct rtw89_dev *rtwdev, bool en)
{
	if (en)
		rtw89_write32_set(rtwdev, R_AX_EFUSE_CTRL_1_V1, B_AX_EF_BURST);
	else
		rtw89_write32_clr(rtwdev, R_AX_EFUSE_CTRL_1_V1, B_AX_EF_BURST);
}

static void rtw89_enable_efuse_pwr_cut_ddv(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	struct rtw89_hal *hal = &rtwdev->hal;

	if (chip_id == RTL8852A)
		return;

	rtw89_write8_set(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
	rtw89_write16_set(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);

	fsleep(1000);

	rtw89_write16_set(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);
	rtw89_write16_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	if (chip_id == RTL8852B && hal->cv == CHIP_CAV)
		rtw89_enable_otp_burst_mode(rtwdev, true);
}

static void rtw89_disable_efuse_pwr_cut_ddv(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;
	struct rtw89_hal *hal = &rtwdev->hal;

	if (chip_id == RTL8852A)
		return;

	if (chip_id == RTL8852B && hal->cv == CHIP_CAV)
		rtw89_enable_otp_burst_mode(rtwdev, false);

	rtw89_write16_set(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_ISO_EB2CORE);
	rtw89_write16_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B15);

	fsleep(1000);

	rtw89_write16_clr(rtwdev, R_AX_SYS_ISO_CTRL, B_AX_PWC_EV2EF_B14);
	rtw89_write8_clr(rtwdev, R_AX_PMC_DBG_CTRL2, B_AX_SYSON_DIS_PMCR_AX_WRMSK);
}

static int rtw89_dump_physical_efuse_map_ddv(struct rtw89_dev *rtwdev, u8 *map,
					     u32 dump_addr, u32 dump_size)
{
	u32 efuse_ctl;
	u32 addr;
	int ret;

	rtw89_enable_efuse_pwr_cut_ddv(rtwdev);

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

	rtw89_disable_efuse_pwr_cut_ddv(rtwdev);

	return 0;
}

int rtw89_cnv_efuse_state_ax(struct rtw89_dev *rtwdev, bool idle)
{
	return 0;
}

static int rtw89_dump_physical_efuse_map_dav(struct rtw89_dev *rtwdev, u8 *map,
					     u32 dump_addr, u32 dump_size)
{
	u32 addr;
	u8 val8;
	int err;
	int ret;

	for (addr = dump_addr; addr < dump_addr + dump_size; addr++) {
		ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_CTRL, 0x40, FULL_BIT_MASK);
		if (ret)
			return ret;
		ret = rtw89_mac_write_xtal_si(rtwdev, XTAL_SI_LOW_ADDR,
					      addr & 0xff, XTAL_SI_LOW_ADDR_MASK);
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

static int rtw89_dump_physical_efuse_map(struct rtw89_dev *rtwdev, u8 *map,
					 u32 dump_addr, u32 dump_size, bool dav)
{
	int ret;

	if (!map || dump_size == 0)
		return 0;

	rtw89_switch_efuse_bank(rtwdev, RTW89_EFUSE_BANK_WIFI);

	if (dav) {
		ret = rtw89_dump_physical_efuse_map_dav(rtwdev, map, dump_addr, dump_size);
		if (ret)
			return ret;
	} else {
		ret = rtw89_dump_physical_efuse_map_ddv(rtwdev, map, dump_addr, dump_size);
		if (ret)
			return ret;
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

	if (!phy_map)
		return 0;

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

int rtw89_parse_efuse_map_ax(struct rtw89_dev *rtwdev)
{
	u32 phy_size = rtwdev->chip->physical_efuse_size;
	u32 log_size = rtwdev->chip->logical_efuse_size;
	u32 dav_phy_size = rtwdev->chip->dav_phy_efuse_size;
	u32 dav_log_size = rtwdev->chip->dav_log_efuse_size;
	u32 full_log_size = log_size + dav_log_size;
	u8 *phy_map = NULL;
	u8 *log_map = NULL;
	u8 *dav_phy_map = NULL;
	u8 *dav_log_map = NULL;
	int ret;

	if (rtw89_read16(rtwdev, R_AX_SYS_WL_EFUSE_CTRL) & B_AX_AUTOLOAD_SUS)
		rtwdev->efuse.valid = true;
	else
		rtw89_warn(rtwdev, "failed to check efuse autoload\n");

	phy_map = kmalloc(phy_size, GFP_KERNEL);
	log_map = kmalloc(full_log_size, GFP_KERNEL);
	if (dav_phy_size && dav_log_size) {
		dav_phy_map = kmalloc(dav_phy_size, GFP_KERNEL);
		dav_log_map = log_map + log_size;
	}

	if (!phy_map || !log_map || (dav_phy_size && !dav_phy_map)) {
		ret = -ENOMEM;
		goto out_free;
	}

	ret = rtw89_dump_physical_efuse_map(rtwdev, phy_map, 0, phy_size, false);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse physical map\n");
		goto out_free;
	}
	ret = rtw89_dump_physical_efuse_map(rtwdev, dav_phy_map, 0, dav_phy_size, true);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse dav physical map\n");
		goto out_free;
	}

	memset(log_map, 0xff, full_log_size);
	ret = rtw89_dump_logical_efuse_map(rtwdev, phy_map, log_map);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse logical map\n");
		goto out_free;
	}
	ret = rtw89_dump_logical_efuse_map(rtwdev, dav_phy_map, dav_log_map);
	if (ret) {
		rtw89_warn(rtwdev, "failed to dump efuse dav logical map\n");
		goto out_free;
	}

	rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "log_map: ", log_map, full_log_size);

	ret = rtwdev->chip->ops->read_efuse(rtwdev, log_map, RTW89_EFUSE_BLOCK_IGNORE);
	if (ret) {
		rtw89_warn(rtwdev, "failed to read efuse map\n");
		goto out_free;
	}

out_free:
	kfree(dav_phy_map);
	kfree(log_map);
	kfree(phy_map);

	return ret;
}

int rtw89_parse_phycap_map_ax(struct rtw89_dev *rtwdev)
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

int rtw89_read_efuse_ver(struct rtw89_dev *rtwdev, u8 *ecv)
{
	int ret;
	u8 val;

	ret = rtw89_dump_physical_efuse_map(rtwdev, &val, EF_FV_OFSET, 1, false);
	if (ret)
		return ret;

	*ecv = u8_get_bits(val, EF_CV_MASK);
	if (*ecv == EF_CV_INV)
		return -ENOENT;

	return 0;
}
EXPORT_SYMBOL(rtw89_read_efuse_ver);

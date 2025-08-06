// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright 2024 Fiona Klute
 *
 * Based on code originally in rtw8723d.[ch],
 * Copyright(c) 2018-2019  Realtek Corporation
 */

#include "main.h"
#include "debug.h"
#include "phy.h"
#include "reg.h"
#include "tx.h"
#include "rtw8723x.h"

static const struct rtw_hw_reg rtw8723x_txagc[] = {
	[DESC_RATE1M]	= { .addr = 0xe08, .mask = 0x0000ff00 },
	[DESC_RATE2M]	= { .addr = 0x86c, .mask = 0x0000ff00 },
	[DESC_RATE5_5M]	= { .addr = 0x86c, .mask = 0x00ff0000 },
	[DESC_RATE11M]	= { .addr = 0x86c, .mask = 0xff000000 },
	[DESC_RATE6M]	= { .addr = 0xe00, .mask = 0x000000ff },
	[DESC_RATE9M]	= { .addr = 0xe00, .mask = 0x0000ff00 },
	[DESC_RATE12M]	= { .addr = 0xe00, .mask = 0x00ff0000 },
	[DESC_RATE18M]	= { .addr = 0xe00, .mask = 0xff000000 },
	[DESC_RATE24M]	= { .addr = 0xe04, .mask = 0x000000ff },
	[DESC_RATE36M]	= { .addr = 0xe04, .mask = 0x0000ff00 },
	[DESC_RATE48M]	= { .addr = 0xe04, .mask = 0x00ff0000 },
	[DESC_RATE54M]	= { .addr = 0xe04, .mask = 0xff000000 },
	[DESC_RATEMCS0]	= { .addr = 0xe10, .mask = 0x000000ff },
	[DESC_RATEMCS1]	= { .addr = 0xe10, .mask = 0x0000ff00 },
	[DESC_RATEMCS2]	= { .addr = 0xe10, .mask = 0x00ff0000 },
	[DESC_RATEMCS3]	= { .addr = 0xe10, .mask = 0xff000000 },
	[DESC_RATEMCS4]	= { .addr = 0xe14, .mask = 0x000000ff },
	[DESC_RATEMCS5]	= { .addr = 0xe14, .mask = 0x0000ff00 },
	[DESC_RATEMCS6]	= { .addr = 0xe14, .mask = 0x00ff0000 },
	[DESC_RATEMCS7]	= { .addr = 0xe14, .mask = 0xff000000 },
};

static void __rtw8723x_lck(struct rtw_dev *rtwdev)
{
	u32 lc_cal;
	u8 val_ctx, rf_val;
	int ret;

	val_ctx = rtw_read8(rtwdev, REG_CTX);
	if ((val_ctx & BIT_MASK_CTX_TYPE) != 0)
		rtw_write8(rtwdev, REG_CTX, val_ctx & ~BIT_MASK_CTX_TYPE);
	else
		rtw_write8(rtwdev, REG_TXPAUSE, 0xFF);
	lc_cal = rtw_read_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal | BIT_LCK);

	ret = read_poll_timeout(rtw_read_rf, rf_val, rf_val != 0x1,
				10000, 1000000, false,
				rtwdev, RF_PATH_A, RF_CFGCH, BIT_LCK);
	if (ret)
		rtw_warn(rtwdev, "failed to poll LCK status bit\n");

	rtw_write_rf(rtwdev, RF_PATH_A, RF_CFGCH, RFREG_MASK, lc_cal);
	if ((val_ctx & BIT_MASK_CTX_TYPE) != 0)
		rtw_write8(rtwdev, REG_CTX, val_ctx);
	else
		rtw_write8(rtwdev, REG_TXPAUSE, 0x00);
}

#define DBG_EFUSE_VAL(rtwdev, map, name)			\
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, # name "=0x%02x\n",	\
		(map)->name)
#define DBG_EFUSE_2BYTE(rtwdev, map, name)			\
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, # name "=0x%02x%02x\n",	\
		(map)->name[0], (map)->name[1])
#define DBG_EFUSE_FIX(rtwdev, name)					\
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "Fixed invalid EFUSE value: "	\
		# name "=0x%x\n", rtwdev->efuse.name)

static void rtw8723xe_efuse_debug(struct rtw_dev *rtwdev,
				  struct rtw8723x_efuse *map)
{
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "mac_addr=%pM\n", map->e.mac_addr);
	DBG_EFUSE_2BYTE(rtwdev, map, e.vendor_id);
	DBG_EFUSE_2BYTE(rtwdev, map, e.device_id);
	DBG_EFUSE_2BYTE(rtwdev, map, e.sub_vendor_id);
	DBG_EFUSE_2BYTE(rtwdev, map, e.sub_device_id);
}

static void rtw8723xu_efuse_debug(struct rtw_dev *rtwdev,
				  struct rtw8723x_efuse *map)
{
	DBG_EFUSE_2BYTE(rtwdev, map, u.vendor_id);
	DBG_EFUSE_2BYTE(rtwdev, map, u.product_id);
	DBG_EFUSE_VAL(rtwdev, map, u.usb_option);
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "mac_addr=%pM\n", map->u.mac_addr);
}

static void rtw8723xs_efuse_debug(struct rtw_dev *rtwdev,
				  struct rtw8723x_efuse *map)
{
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "mac_addr=%pM\n", map->s.mac_addr);
}

static void __rtw8723x_debug_txpwr_limit(struct rtw_dev *rtwdev,
					 struct rtw_txpwr_idx *table,
					 int tx_path_count)
{
	if (!rtw_dbg_is_enabled(rtwdev, RTW_DBG_EFUSE))
		return;

	rtw_dbg(rtwdev, RTW_DBG_EFUSE,
		"Power index table (2.4G):\n");
	/* CCK base */
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "CCK base\n");
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "RF    G0  G1  G2  G3  G4  G5\n");
	for (int i = 0; i < tx_path_count; i++)
		rtw_dbg(rtwdev, RTW_DBG_EFUSE,
			"[%c]: %3u %3u %3u %3u %3u %3u\n",
			'A' + i,
			table[i].pwr_idx_2g.cck_base[0],
			table[i].pwr_idx_2g.cck_base[1],
			table[i].pwr_idx_2g.cck_base[2],
			table[i].pwr_idx_2g.cck_base[3],
			table[i].pwr_idx_2g.cck_base[4],
			table[i].pwr_idx_2g.cck_base[5]);
	/* CCK diff */
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "CCK diff\n");
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "RF   1S 2S 3S 4S\n");
	for (int i = 0; i < tx_path_count; i++)
		rtw_dbg(rtwdev, RTW_DBG_EFUSE,
			"[%c]: %2d %2d %2d %2d\n",
			'A' + i, 0 /* no diff for 1S */,
			table[i].pwr_idx_2g.ht_2s_diff.cck,
			table[i].pwr_idx_2g.ht_3s_diff.cck,
			table[i].pwr_idx_2g.ht_4s_diff.cck);
	/* BW40-1S base */
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "BW40-1S base\n");
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "RF    G0  G1  G2  G3  G4\n");
	for (int i = 0; i < tx_path_count; i++)
		rtw_dbg(rtwdev, RTW_DBG_EFUSE,
			"[%c]: %3u %3u %3u %3u %3u\n",
			'A' + i,
			table[i].pwr_idx_2g.bw40_base[0],
			table[i].pwr_idx_2g.bw40_base[1],
			table[i].pwr_idx_2g.bw40_base[2],
			table[i].pwr_idx_2g.bw40_base[3],
			table[i].pwr_idx_2g.bw40_base[4]);
	/* OFDM diff */
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "OFDM diff\n");
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "RF   1S 2S 3S 4S\n");
	for (int i = 0; i < tx_path_count; i++)
		rtw_dbg(rtwdev, RTW_DBG_EFUSE,
			"[%c]: %2d %2d %2d %2d\n",
			'A' + i,
			table[i].pwr_idx_2g.ht_1s_diff.ofdm,
			table[i].pwr_idx_2g.ht_2s_diff.ofdm,
			table[i].pwr_idx_2g.ht_3s_diff.ofdm,
			table[i].pwr_idx_2g.ht_4s_diff.ofdm);
	/* BW20 diff */
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "BW20 diff\n");
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "RF   1S 2S 3S 4S\n");
	for (int i = 0; i < tx_path_count; i++)
		rtw_dbg(rtwdev, RTW_DBG_EFUSE,
			"[%c]: %2d %2d %2d %2d\n",
			'A' + i,
			table[i].pwr_idx_2g.ht_1s_diff.bw20,
			table[i].pwr_idx_2g.ht_2s_diff.bw20,
			table[i].pwr_idx_2g.ht_3s_diff.bw20,
			table[i].pwr_idx_2g.ht_4s_diff.bw20);
	/* BW40 diff */
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "BW40 diff\n");
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "RF   1S 2S 3S 4S\n");
	for (int i = 0; i < tx_path_count; i++)
		rtw_dbg(rtwdev, RTW_DBG_EFUSE,
			"[%c]: %2d %2d %2d %2d\n",
			'A' + i, 0 /* no diff for 1S */,
			table[i].pwr_idx_2g.ht_2s_diff.bw40,
			table[i].pwr_idx_2g.ht_3s_diff.bw40,
			table[i].pwr_idx_2g.ht_4s_diff.bw40);
}

static void efuse_debug_dump(struct rtw_dev *rtwdev,
			     struct rtw8723x_efuse *map)
{
	if (!rtw_dbg_is_enabled(rtwdev, RTW_DBG_EFUSE))
		return;

	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "EFUSE raw logical map:\n");
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 16, 1,
		       (u8 *)map, sizeof(struct rtw8723x_efuse), false);
	rtw_dbg(rtwdev, RTW_DBG_EFUSE, "Parsed rtw8723x EFUSE data:\n");
	DBG_EFUSE_VAL(rtwdev, map, rtl_id);
	DBG_EFUSE_VAL(rtwdev, map, afe);
	rtw8723x_debug_txpwr_limit(rtwdev, map->txpwr_idx_table, 4);
	DBG_EFUSE_VAL(rtwdev, map, channel_plan);
	DBG_EFUSE_VAL(rtwdev, map, xtal_k);
	DBG_EFUSE_VAL(rtwdev, map, thermal_meter);
	DBG_EFUSE_VAL(rtwdev, map, iqk_lck);
	DBG_EFUSE_VAL(rtwdev, map, pa_type);
	DBG_EFUSE_2BYTE(rtwdev, map, lna_type_2g);
	DBG_EFUSE_2BYTE(rtwdev, map, lna_type_5g);
	DBG_EFUSE_VAL(rtwdev, map, rf_board_option);
	DBG_EFUSE_VAL(rtwdev, map, rf_feature_option);
	DBG_EFUSE_VAL(rtwdev, map, rf_bt_setting);
	DBG_EFUSE_VAL(rtwdev, map, eeprom_version);
	DBG_EFUSE_VAL(rtwdev, map, eeprom_customer_id);
	DBG_EFUSE_VAL(rtwdev, map, tx_bb_swing_setting_2g);
	DBG_EFUSE_VAL(rtwdev, map, tx_pwr_calibrate_rate);
	DBG_EFUSE_VAL(rtwdev, map, rf_antenna_option);
	DBG_EFUSE_VAL(rtwdev, map, rfe_option);
	DBG_EFUSE_2BYTE(rtwdev, map, country_code);

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw8723xe_efuse_debug(rtwdev, map);
		break;
	case RTW_HCI_TYPE_USB:
		rtw8723xu_efuse_debug(rtwdev, map);
		break;
	case RTW_HCI_TYPE_SDIO:
		rtw8723xs_efuse_debug(rtwdev, map);
		break;
	default:
		/* unsupported now */
		break;
	}
}

static void rtw8723xe_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8723x_efuse *map)
{
	ether_addr_copy(efuse->addr, map->e.mac_addr);
}

static void rtw8723xu_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8723x_efuse *map)
{
	ether_addr_copy(efuse->addr, map->u.mac_addr);
}

static void rtw8723xs_efuse_parsing(struct rtw_efuse *efuse,
				    struct rtw8723x_efuse *map)
{
	ether_addr_copy(efuse->addr, map->s.mac_addr);
}

/* Default power index table for RTL8703B/RTL8723D, used if EFUSE does
 * not contain valid data. Replaces EFUSE data from offset 0x10 (start
 * of txpwr_idx_table).
 */
static const u8 rtw8723x_txpwr_idx_table[] = {
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x2D,
	0x2D, 0x2D, 0x2D, 0x2D, 0x2D, 0x02
};

static int __rtw8723x_read_efuse(struct rtw_dev *rtwdev, u8 *log_map)
{
	struct rtw_efuse *efuse = &rtwdev->efuse;
	u8 *pwr = (u8 *)efuse->txpwr_idx_table;
	struct rtw8723x_efuse *map;
	bool valid = false;
	int i;

	map = (struct rtw8723x_efuse *)log_map;
	efuse_debug_dump(rtwdev, map);

	efuse->rfe_option = 0;
	efuse->rf_board_option = map->rf_board_option;
	efuse->crystal_cap = map->xtal_k;
	efuse->pa_type_2g = map->pa_type;
	efuse->lna_type_2g = map->lna_type_2g[0];
	efuse->channel_plan = map->channel_plan;
	efuse->country_code[0] = map->country_code[0];
	efuse->country_code[1] = map->country_code[1];
	efuse->bt_setting = map->rf_bt_setting;
	efuse->regd = map->rf_board_option & 0x7;
	efuse->thermal_meter[0] = map->thermal_meter;
	efuse->thermal_meter_k = map->thermal_meter;
	efuse->afe = map->afe;

	for (i = 0; i < 4; i++)
		efuse->txpwr_idx_table[i] = map->txpwr_idx_table[i];

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw8723xe_efuse_parsing(efuse, map);
		break;
	case RTW_HCI_TYPE_USB:
		rtw8723xu_efuse_parsing(efuse, map);
		break;
	case RTW_HCI_TYPE_SDIO:
		rtw8723xs_efuse_parsing(efuse, map);
		break;
	default:
		/* unsupported now */
		return -EOPNOTSUPP;
	}

	/* If TX power index table in EFUSE is invalid, fall back to
	 * built-in table.
	 */
	for (i = 0; i < ARRAY_SIZE(rtw8723x_txpwr_idx_table); i++)
		if (pwr[i] != 0xff) {
			valid = true;
			break;
		}
	if (!valid) {
		for (i = 0; i < ARRAY_SIZE(rtw8723x_txpwr_idx_table); i++)
			pwr[i] = rtw8723x_txpwr_idx_table[i];
		rtw_dbg(rtwdev, RTW_DBG_EFUSE,
			"Replaced invalid EFUSE TX power index table.");
		rtw8723x_debug_txpwr_limit(rtwdev,
					   efuse->txpwr_idx_table, 2);
	}

	/* Override invalid antenna settings. */
	if (efuse->bt_setting == 0xff) {
		/* shared antenna */
		efuse->bt_setting |= BIT(0);
		/* RF path A */
		efuse->bt_setting &= ~BIT(6);
		DBG_EFUSE_FIX(rtwdev, bt_setting);
	}

	/* Override invalid board options: The coex code incorrectly
	 * assumes that if bits 6 & 7 are set the board doesn't
	 * support coex. Regd is also derived from rf_board_option and
	 * should be 0 if there's no valid data.
	 */
	if (efuse->rf_board_option == 0xff) {
		efuse->regd = 0;
		efuse->rf_board_option &= GENMASK(5, 0);
		DBG_EFUSE_FIX(rtwdev, rf_board_option);
	}

	/* Override invalid crystal cap setting, default comes from
	 * vendor driver. Chip specific.
	 */
	if (efuse->crystal_cap == 0xff) {
		efuse->crystal_cap = 0x20;
		DBG_EFUSE_FIX(rtwdev, crystal_cap);
	}

	return 0;
}

#define BIT_CFENDFORM		BIT(9)
#define BIT_WMAC_TCR_ERR0	BIT(12)
#define BIT_WMAC_TCR_ERR1	BIT(13)
#define BIT_TCR_CFG		(BIT_CFENDFORM | BIT_WMAC_TCR_ERR0 |	       \
				 BIT_WMAC_TCR_ERR1)
#define WLAN_RX_FILTER0		0xFFFF
#define WLAN_RX_FILTER1		0x400
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0x700060CE

static int __rtw8723x_mac_init(struct rtw_dev *rtwdev)
{
	rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 1, WLAN_TXQ_RPT_EN);
	rtw_write32(rtwdev, REG_TCR, BIT_TCR_CFG);

	rtw_write16(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP1, WLAN_RX_FILTER1);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);

	rtw_write32(rtwdev, REG_INT_MIG, 0);
	rtw_write32(rtwdev, REG_MCUTST_1, 0x0);

	rtw_write8(rtwdev, REG_MISC_CTRL, BIT_DIS_SECOND_CCA);
	rtw_write8(rtwdev, REG_2ND_CCA_CTRL, 0);

	return 0;
}

static void __rtw8723x_cfg_ldo25(struct rtw_dev *rtwdev, bool enable)
{
	u8 ldo_pwr;

	ldo_pwr = rtw_read8(rtwdev, REG_LDO_EFUSE_CTRL + 3);
	if (enable) {
		ldo_pwr &= ~BIT_MASK_LDO25_VOLTAGE;
		ldo_pwr |= (BIT_LDO25_VOLTAGE_V25 << 4) | BIT_LDO25_EN;
	} else {
		ldo_pwr &= ~BIT_LDO25_EN;
	}
	rtw_write8(rtwdev, REG_LDO_EFUSE_CTRL + 3, ldo_pwr);
}

static void
rtw8723x_set_tx_power_index_by_rate(struct rtw_dev *rtwdev, u8 path, u8 rs)
{
	struct rtw_hal *hal = &rtwdev->hal;
	const struct rtw_hw_reg *txagc;
	u8 rate, pwr_index;
	int j;

	for (j = 0; j < rtw_rate_size[rs]; j++) {
		rate = rtw_rate_section[rs][j];
		pwr_index = hal->tx_pwr_tbl[path][rate];

		if (rate >= ARRAY_SIZE(rtw8723x_txagc)) {
			rtw_warn(rtwdev, "rate 0x%x isn't supported\n", rate);
			continue;
		}
		txagc = &rtw8723x_txagc[rate];
		if (!txagc->addr) {
			rtw_warn(rtwdev, "rate 0x%x isn't defined\n", rate);
			continue;
		}

		rtw_write32_mask(rtwdev, txagc->addr, txagc->mask, pwr_index);
	}
}

static void __rtw8723x_set_tx_power_index(struct rtw_dev *rtwdev)
{
	struct rtw_hal *hal = &rtwdev->hal;
	int rs, path;

	for (path = 0; path < hal->rf_path_num; path++) {
		for (rs = 0; rs <= RTW_RATE_SECTION_HT_1S; rs++)
			rtw8723x_set_tx_power_index_by_rate(rtwdev, path, rs);
	}
}

static void __rtw8723x_efuse_grant(struct rtw_dev *rtwdev, bool on)
{
	if (on) {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		rtw_write16_set(rtwdev, REG_SYS_FUNC_EN, BIT_FEN_ELDR);
		rtw_write16_set(rtwdev, REG_SYS_CLKR, BIT_LOADER_CLK_EN | BIT_ANA8M);
	} else {
		rtw_write8(rtwdev, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);
	}
}

static void __rtw8723x_false_alarm_statistics(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u32 cck_fa_cnt;
	u32 ofdm_fa_cnt;
	u32 crc32_cnt;
	u32 val32;

	/* hold counter */
	rtw_write32_mask(rtwdev, REG_OFDM_FA_HOLDC_11N, BIT_MASK_OFDM_FA_KEEP, 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_KEEP1, 1);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_CNT_KEEP, 1);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_FA_KEEP, 1);

	cck_fa_cnt = rtw_read32_mask(rtwdev, REG_CCK_FA_LSB_11N, MASKBYTE0);
	cck_fa_cnt += rtw_read32_mask(rtwdev, REG_CCK_FA_MSB_11N, MASKBYTE3) << 8;

	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE1_11N);
	ofdm_fa_cnt = u32_get_bits(val32, BIT_MASK_OFDM_FF_CNT);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_SF_CNT);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE2_11N);
	dm_info->ofdm_cca_cnt = u32_get_bits(val32, BIT_MASK_OFDM_CCA_CNT);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_PF_CNT);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE3_11N);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_RI_CNT);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_CRC_CNT);
	val32 = rtw_read32(rtwdev, REG_OFDM_FA_TYPE4_11N);
	ofdm_fa_cnt += u32_get_bits(val32, BIT_MASK_OFDM_MNS_CNT);

	dm_info->cck_fa_cnt = cck_fa_cnt;
	dm_info->ofdm_fa_cnt = ofdm_fa_cnt;
	dm_info->total_fa_cnt = cck_fa_cnt + ofdm_fa_cnt;

	dm_info->cck_err_cnt = rtw_read32(rtwdev, REG_IGI_C_11N);
	dm_info->cck_ok_cnt = rtw_read32(rtwdev, REG_IGI_D_11N);
	crc32_cnt = rtw_read32(rtwdev, REG_OFDM_CRC32_CNT_11N);
	dm_info->ofdm_err_cnt = u32_get_bits(crc32_cnt, BIT_MASK_OFDM_LCRC_ERR);
	dm_info->ofdm_ok_cnt = u32_get_bits(crc32_cnt, BIT_MASK_OFDM_LCRC_OK);
	crc32_cnt = rtw_read32(rtwdev, REG_HT_CRC32_CNT_11N);
	dm_info->ht_err_cnt = u32_get_bits(crc32_cnt, BIT_MASK_HT_CRC_ERR);
	dm_info->ht_ok_cnt = u32_get_bits(crc32_cnt, BIT_MASK_HT_CRC_OK);
	dm_info->vht_err_cnt = 0;
	dm_info->vht_ok_cnt = 0;

	val32 = rtw_read32(rtwdev, REG_CCK_CCA_CNT_11N);
	dm_info->cck_cca_cnt = (u32_get_bits(val32, BIT_MASK_CCK_FA_MSB) << 8) |
			       u32_get_bits(val32, BIT_MASK_CCK_FA_LSB);
	dm_info->total_cca_cnt = dm_info->cck_cca_cnt + dm_info->ofdm_cca_cnt;

	/* reset counter */
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTC_11N, BIT_MASK_OFDM_FA_RST, 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTC_11N, BIT_MASK_OFDM_FA_RST, 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_RST1, 1);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_RST1, 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_HOLDC_11N, BIT_MASK_OFDM_FA_KEEP, 0);
	rtw_write32_mask(rtwdev, REG_OFDM_FA_RSTD_11N, BIT_MASK_OFDM_FA_KEEP1, 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_CNT_KPEN, 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_CNT_KPEN, 2);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_FA_KPEN, 0);
	rtw_write32_mask(rtwdev, REG_CCK_FA_RST_11N, BIT_MASK_CCK_FA_KPEN, 2);
	rtw_write32_mask(rtwdev, REG_PAGE_F_RST_11N, BIT_MASK_F_RST_ALL, 1);
	rtw_write32_mask(rtwdev, REG_PAGE_F_RST_11N, BIT_MASK_F_RST_ALL, 0);
}

/* IQK (IQ calibration) */

static
void __rtw8723x_iqk_backup_regs(struct rtw_dev *rtwdev,
				struct rtw8723x_iqk_backup_regs *backup)
{
	int i;

	for (i = 0; i < RTW8723X_IQK_ADDA_REG_NUM; i++)
		backup->adda[i] = rtw_read32(rtwdev,
					     rtw8723x_common.iqk_adda_regs[i]);

	for (i = 0; i < RTW8723X_IQK_MAC8_REG_NUM; i++)
		backup->mac8[i] = rtw_read8(rtwdev,
					    rtw8723x_common.iqk_mac8_regs[i]);
	for (i = 0; i < RTW8723X_IQK_MAC32_REG_NUM; i++)
		backup->mac32[i] = rtw_read32(rtwdev,
					      rtw8723x_common.iqk_mac32_regs[i]);

	for (i = 0; i < RTW8723X_IQK_BB_REG_NUM; i++)
		backup->bb[i] = rtw_read32(rtwdev,
					   rtw8723x_common.iqk_bb_regs[i]);

	backup->igia = rtw_read32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0);
	backup->igib = rtw_read32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0);

	backup->bb_sel_btg = rtw_read32(rtwdev, REG_BB_SEL_BTG);
}

static
void __rtw8723x_iqk_restore_regs(struct rtw_dev *rtwdev,
				 const struct rtw8723x_iqk_backup_regs *backup)
{
	int i;

	for (i = 0; i < RTW8723X_IQK_ADDA_REG_NUM; i++)
		rtw_write32(rtwdev, rtw8723x_common.iqk_adda_regs[i],
			    backup->adda[i]);

	for (i = 0; i < RTW8723X_IQK_MAC8_REG_NUM; i++)
		rtw_write8(rtwdev, rtw8723x_common.iqk_mac8_regs[i],
			   backup->mac8[i]);
	for (i = 0; i < RTW8723X_IQK_MAC32_REG_NUM; i++)
		rtw_write32(rtwdev, rtw8723x_common.iqk_mac32_regs[i],
			    backup->mac32[i]);

	for (i = 0; i < RTW8723X_IQK_BB_REG_NUM; i++)
		rtw_write32(rtwdev, rtw8723x_common.iqk_bb_regs[i],
			    backup->bb[i]);

	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XAAGC1, MASKBYTE0, backup->igia);

	rtw_write32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0, 0x50);
	rtw_write32_mask(rtwdev, REG_OFDM0_XBAGC1, MASKBYTE0, backup->igib);

	rtw_write32(rtwdev, REG_TXIQK_TONE_A_11N, 0x01008c00);
	rtw_write32(rtwdev, REG_RXIQK_TONE_A_11N, 0x01008c00);
}

static
bool __rtw8723x_iqk_similarity_cmp(struct rtw_dev *rtwdev,
				   s32 result[][IQK_NR],
				   u8 c1, u8 c2)
{
	u32 i, j, diff;
	u32 bitmap = 0;
	u8 candidate[PATH_NR] = {IQK_ROUND_INVALID, IQK_ROUND_INVALID};
	bool ret = true;

	s32 tmp1, tmp2;

	for (i = 0; i < IQK_NR; i++) {
		tmp1 = iqkxy_to_s32(result[c1][i]);
		tmp2 = iqkxy_to_s32(result[c2][i]);

		diff = abs(tmp1 - tmp2);

		if (diff <= MAX_TOLERANCE)
			continue;

		if ((i == IQK_S1_RX_X || i == IQK_S0_RX_X) && !bitmap) {
			if (result[c1][i] + result[c1][i + 1] == 0)
				candidate[i / IQK_SX_NR] = c2;
			else if (result[c2][i] + result[c2][i + 1] == 0)
				candidate[i / IQK_SX_NR] = c1;
			else
				bitmap |= BIT(i);
		} else {
			bitmap |= BIT(i);
		}
	}

	if (bitmap != 0)
		goto check_sim;

	for (i = 0; i < PATH_NR; i++) {
		if (candidate[i] == IQK_ROUND_INVALID)
			continue;

		for (j = i * IQK_SX_NR; j < i * IQK_SX_NR + 2; j++)
			result[IQK_ROUND_HYBRID][j] = result[candidate[i]][j];
		ret = false;
	}

	return ret;

check_sim:
	for (i = 0; i < IQK_NR; i++) {
		j = i & ~1;	/* 2 bits are a pair for IQ[X, Y] */
		if (bitmap & GENMASK(j + 1, j))
			continue;

		result[IQK_ROUND_HYBRID][i] = result[c1][i];
	}

	return false;
}

static u8 __rtw8723x_pwrtrack_get_limit_ofdm(struct rtw_dev *rtwdev)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	u8 tx_rate = dm_info->tx_rate;
	u8 limit_ofdm = 30;

	switch (tx_rate) {
	case DESC_RATE1M...DESC_RATE5_5M:
	case DESC_RATE11M:
		break;
	case DESC_RATE6M...DESC_RATE48M:
		limit_ofdm = 36;
		break;
	case DESC_RATE54M:
		limit_ofdm = 34;
		break;
	case DESC_RATEMCS0...DESC_RATEMCS2:
		limit_ofdm = 38;
		break;
	case DESC_RATEMCS3...DESC_RATEMCS4:
		limit_ofdm = 36;
		break;
	case DESC_RATEMCS5...DESC_RATEMCS7:
		limit_ofdm = 34;
		break;
	default:
		rtw_warn(rtwdev, "pwrtrack unhandled tx_rate 0x%x\n", tx_rate);
		break;
	}

	return limit_ofdm;
}

static
void __rtw8723x_pwrtrack_set_xtal(struct rtw_dev *rtwdev, u8 therm_path,
				  u8 delta)
{
	struct rtw_dm_info *dm_info = &rtwdev->dm_info;
	const struct rtw_rfe_def *rfe_def = rtw_get_rfe_def(rtwdev);
	const struct rtw_pwr_track_tbl *tbl = rfe_def->pwr_track_tbl;
	const s8 *pwrtrk_xtal;
	s8 xtal_cap;

	if (dm_info->thermal_avg[therm_path] >
	    rtwdev->efuse.thermal_meter[therm_path])
		pwrtrk_xtal = tbl->pwrtrk_xtal_p;
	else
		pwrtrk_xtal = tbl->pwrtrk_xtal_n;

	xtal_cap = rtwdev->efuse.crystal_cap & 0x3F;
	xtal_cap = clamp_t(s8, xtal_cap + pwrtrk_xtal[delta], 0, 0x3F);
	rtw_write32_mask(rtwdev, REG_AFE_CTRL3, BIT_MASK_XTAL,
			 xtal_cap | (xtal_cap << 6));
}

static
void __rtw8723x_fill_txdesc_checksum(struct rtw_dev *rtwdev,
				     struct rtw_tx_pkt_info *pkt_info,
				     u8 *txdesc)
{
	size_t words = 32 / 2; /* calculate the first 32 bytes (16 words) */
	__le16 chksum = 0;
	__le16 *data = (__le16 *)(txdesc);
	struct rtw_tx_desc *tx_desc = (struct rtw_tx_desc *)txdesc;

	le32p_replace_bits(&tx_desc->w7, 0, RTW_TX_DESC_W7_TXDESC_CHECKSUM);

	while (words--)
		chksum ^= *data++;

	chksum = ~chksum;

	le32p_replace_bits(&tx_desc->w7, __le16_to_cpu(chksum),
			   RTW_TX_DESC_W7_TXDESC_CHECKSUM);
}

static void __rtw8723x_coex_cfg_init(struct rtw_dev *rtwdev)
{
	/* enable TBTT nterrupt */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* BT report packet sample rate	 */
	/* 0x790[5:0]=0x5 */
	rtw_write8_mask(rtwdev, REG_BT_TDMA_TIME, BIT_MASK_SAMPLE_RATE, 0x5);

	/* enable BT counter statistics */
	rtw_write8(rtwdev, REG_BT_STAT_CTRL, 0x1);

	/* enable PTA (3-wire function form BT side) */
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_BT_PTA_EN);
	rtw_write32_set(rtwdev, REG_GPIO_MUXCFG, BIT_PO_BT_PTA_PINS);

	/* enable PTA (tx/rx signal form WiFi side) */
	rtw_write8_set(rtwdev, REG_QUEUE_CTRL, BIT_PTA_WL_TX_EN);
}

const struct rtw8723x_common rtw8723x_common = {
	.iqk_adda_regs = {
		0x85c, 0xe6c, 0xe70, 0xe74, 0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4, 0xed8, 0xedc, 0xee0, 0xeec
	},
	.iqk_mac8_regs = {0x522, 0x550, 0x551},
	.iqk_mac32_regs = {0x40},
	.iqk_bb_regs = {
		0xc04, 0xc08, 0x874, 0xb68, 0xb6c, 0x870, 0x860, 0x864, 0xa04
	},

	.ltecoex_addr = {
		.ctrl = REG_LTECOEX_CTRL,
		.wdata = REG_LTECOEX_WRITE_DATA,
		.rdata = REG_LTECOEX_READ_DATA,
	},
	.rf_sipi_addr = {
		[RF_PATH_A] = { .hssi_1 = 0x820, .lssi_read    = 0x8a0,
				.hssi_2 = 0x824, .lssi_read_pi = 0x8b8},
		[RF_PATH_B] = { .hssi_1 = 0x828, .lssi_read    = 0x8a4,
				.hssi_2 = 0x82c, .lssi_read_pi = 0x8bc},
	},
	.dig = {
		[0] = { .addr = 0xc50, .mask = 0x7f },
		[1] = { .addr = 0xc50, .mask = 0x7f },
	},
	.dig_cck = {
		[0] = { .addr = 0xa0c, .mask = 0x3f00 },
	},
	.prioq_addrs = {
		.prio[RTW_DMA_MAPPING_EXTRA] = {
			.rsvd = REG_RQPN_NPQ + 2, .avail = REG_RQPN_NPQ + 3,
		},
		.prio[RTW_DMA_MAPPING_LOW] = {
			.rsvd = REG_RQPN + 1, .avail = REG_FIFOPAGE_CTRL_2 + 1,
		},
		.prio[RTW_DMA_MAPPING_NORMAL] = {
			.rsvd = REG_RQPN_NPQ, .avail = REG_RQPN_NPQ + 1,
		},
		.prio[RTW_DMA_MAPPING_HIGH] = {
			.rsvd = REG_RQPN, .avail = REG_FIFOPAGE_CTRL_2,
		},
		.wsize = false,
	},

	.lck = __rtw8723x_lck,
	.read_efuse = __rtw8723x_read_efuse,
	.mac_init = __rtw8723x_mac_init,
	.cfg_ldo25 = __rtw8723x_cfg_ldo25,
	.set_tx_power_index = __rtw8723x_set_tx_power_index,
	.efuse_grant = __rtw8723x_efuse_grant,
	.false_alarm_statistics = __rtw8723x_false_alarm_statistics,
	.iqk_backup_regs = __rtw8723x_iqk_backup_regs,
	.iqk_restore_regs = __rtw8723x_iqk_restore_regs,
	.iqk_similarity_cmp = __rtw8723x_iqk_similarity_cmp,
	.pwrtrack_get_limit_ofdm = __rtw8723x_pwrtrack_get_limit_ofdm,
	.pwrtrack_set_xtal = __rtw8723x_pwrtrack_set_xtal,
	.coex_cfg_init = __rtw8723x_coex_cfg_init,
	.fill_txdesc_checksum = __rtw8723x_fill_txdesc_checksum,
	.debug_txpwr_limit = __rtw8723x_debug_txpwr_limit,
};
EXPORT_SYMBOL(rtw8723x_common);

MODULE_AUTHOR("Realtek Corporation");
MODULE_AUTHOR("Fiona Klute <fiona.klute@gmx.de>");
MODULE_DESCRIPTION("Common functions for Realtek 802.11n wireless 8723x drivers");
MODULE_LICENSE("Dual BSD/GPL");

/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_PHY_H_
#define __RTW_PHY_H_

#include "debug.h"

extern u8 rtw_cck_rates[];
extern u8 rtw_ofdm_rates[];
extern u8 rtw_ht_1s_rates[];
extern u8 rtw_ht_2s_rates[];
extern u8 rtw_vht_1s_rates[];
extern u8 rtw_vht_2s_rates[];
extern u8 *rtw_rate_section[];
extern u8 rtw_rate_size[];

void rtw_phy_init(struct rtw_dev *rtwdev);
void rtw_phy_dynamic_mechanism(struct rtw_dev *rtwdev);
u8 rtw_phy_rf_power_2_rssi(s8 *rf_power, u8 path_num);
u32 rtw_phy_read_rf(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
		    u32 addr, u32 mask);
bool rtw_phy_write_rf_reg_sipi(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			       u32 addr, u32 mask, u32 data);
bool rtw_phy_write_rf_reg(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			  u32 addr, u32 mask, u32 data);
bool rtw_phy_write_rf_reg_mix(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			      u32 addr, u32 mask, u32 data);
void phy_store_tx_power_by_rate(void *adapter, u32 band, u32 rfpath, u32 txnum,
				u32 regaddr, u32 bitmask, u32 data);
void phy_set_tx_power_limit(struct rtw_dev *rtwdev, u8 regd, u8 band,
			    u8 bw, u8 rs, u8 ch, s8 pwr_limit);
void phy_set_tx_power_index_by_rs(void *adapter, u8 ch, u8 path, u8 rs);
void rtw_phy_setup_phy_cond(struct rtw_dev *rtwdev, u32 pkg);
void rtw_parse_tbl_phy_cond(struct rtw_dev *rtwdev, const struct rtw_table *tbl);
void rtw_parse_tbl_bb_pg(struct rtw_dev *rtwdev, const struct rtw_table *tbl);
void rtw_parse_tbl_txpwr_lmt(struct rtw_dev *rtwdev, const struct rtw_table *tbl);
void rtw_phy_cfg_mac(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data);
void rtw_phy_cfg_agc(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		     u32 addr, u32 data);
void rtw_phy_cfg_bb(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data);
void rtw_phy_cfg_rf(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		    u32 addr, u32 data);
void rtw_hw_init_tx_power(struct rtw_hal *hal);
void rtw_phy_load_tables(struct rtw_dev *rtwdev);
void rtw_phy_set_tx_power_level(struct rtw_dev *rtwdev, u8 channel);
void rtw_phy_tx_power_by_rate_config(struct rtw_hal *hal);
void rtw_phy_tx_power_limit_config(struct rtw_hal *hal);

#define RTW_DECL_TABLE_PHY_COND_CORE(name, cfg, path)	\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.parse = rtw_parse_tbl_phy_cond,		\
	.do_cfg = cfg,					\
	.rf_path = path,				\
}

#define RTW_DECL_TABLE_PHY_COND(name, cfg)		\
	RTW_DECL_TABLE_PHY_COND_CORE(name, cfg, 0)

#define RTW_DECL_TABLE_RF_RADIO(name, path)		\
	RTW_DECL_TABLE_PHY_COND_CORE(name, rtw_phy_cfg_rf, RF_PATH_ ## path)

#define RTW_DECL_TABLE_BB_PG(name)			\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.parse = rtw_parse_tbl_bb_pg,			\
}

#define RTW_DECL_TABLE_TXPWR_LMT(name)			\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.parse = rtw_parse_tbl_txpwr_lmt,		\
}

static inline const struct rtw_rfe_def *rtw_get_rfe_def(struct rtw_dev *rtwdev)
{
	struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_efuse *efuse = &rtwdev->efuse;
	const struct rtw_rfe_def *rfe_def = NULL;

	if (chip->rfe_defs_size == 0)
		return NULL;

	if (efuse->rfe_option < chip->rfe_defs_size)
		rfe_def = &chip->rfe_defs[efuse->rfe_option];

	rtw_dbg(rtwdev, RTW_DBG_PHY, "use rfe_def[%d]\n", efuse->rfe_option);
	return rfe_def;
}

static inline int rtw_check_supported_rfe(struct rtw_dev *rtwdev)
{
	const struct rtw_rfe_def *rfe_def = rtw_get_rfe_def(rtwdev);

	if (!rfe_def || !rfe_def->phy_pg_tbl || !rfe_def->txpwr_lmt_tbl) {
		rtw_err(rtwdev, "rfe %d isn't supported\n",
			rtwdev->efuse.rfe_option);
		return -ENODEV;
	}

	return 0;
}

void rtw_phy_dig_write(struct rtw_dev *rtwdev, u8 igi);

#define	MASKBYTE0		0xff
#define	MASKBYTE1		0xff00
#define	MASKBYTE2		0xff0000
#define	MASKBYTE3		0xff000000
#define	MASKHWORD		0xffff0000
#define	MASKLWORD		0x0000ffff
#define	MASKDWORD		0xffffffff
#define RFREG_MASK		0xfffff

#define	MASK7BITS		0x7f
#define	MASK12BITS		0xfff
#define	MASKH4BITS		0xf0000000
#define	MASK20BITS		0xfffff
#define	MASK24BITS		0xffffff

#define MASKH3BYTES		0xffffff00
#define MASKL3BYTES		0x00ffffff
#define MASKBYTE2HIGHNIBBLE	0x00f00000
#define MASKBYTE3LOWNIBBLE	0x0f000000
#define	MASKL3BYTES		0x00ffffff

#endif

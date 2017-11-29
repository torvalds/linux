/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../mp_precomp.h"
#include "../phydm_precomp.h"

void odm_config_rf_reg_8822b(struct phy_dm_struct *dm, u32 addr, u32 data,
			     enum odm_rf_radio_path RF_PATH, u32 reg_addr)
{
	if (addr == 0xffe) {
		ODM_sleep_ms(50);
	} else if (addr == 0xfe) {
		ODM_delay_us(100);
	} else {
		odm_set_rf_reg(dm, RF_PATH, reg_addr, RFREGOFFSETMASK, data);

		/* Add 1us delay between BB/RF register setting. */
		ODM_delay_us(1);
	}
}

void odm_config_rf_radio_a_8822b(struct phy_dm_struct *dm, u32 addr, u32 data)
{
	u32 content = 0x1000; /* RF_Content: radioa_txt */
	u32 maskfor_phy_set = (u32)(content & 0xE000);

	odm_config_rf_reg_8822b(dm, addr, data, ODM_RF_PATH_A,
				addr | maskfor_phy_set);

	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"===> odm_config_rf_with_header_file: [RadioA] %08X %08X\n",
		addr, data);
}

void odm_config_rf_radio_b_8822b(struct phy_dm_struct *dm, u32 addr, u32 data)
{
	u32 content = 0x1001; /* RF_Content: radiob_txt */
	u32 maskfor_phy_set = (u32)(content & 0xE000);

	odm_config_rf_reg_8822b(dm, addr, data, ODM_RF_PATH_B,
				addr | maskfor_phy_set);

	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"===> odm_config_rf_with_header_file: [RadioB] %08X %08X\n",
		addr, data);
}

void odm_config_mac_8822b(struct phy_dm_struct *dm, u32 addr, u8 data)
{
	odm_write_1byte(dm, addr, data);
	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"===> odm_config_mac_with_header_file: [MAC_REG] %08X %08X\n",
		addr, data);
}

void odm_update_agc_big_jump_lmt_8822b(struct phy_dm_struct *dm, u32 addr,
				       u32 data)
{
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	u8 rf_gain_idx = (u8)((data & 0xFF000000) >> 24);
	u8 bb_gain_idx = (u8)((data & 0x00ff0000) >> 16);
	u8 agc_table_idx = (u8)((data & 0x00000f00) >> 8);
	static bool is_limit;

	if (addr != 0x81c)
		return;

	if (bb_gain_idx > 0x3c) {
		if ((rf_gain_idx == dig_tab->rf_gain_idx) && !is_limit) {
			is_limit = true;
			dig_tab->big_jump_lmt[agc_table_idx] = bb_gain_idx - 2;
			ODM_RT_TRACE(
				dm, ODM_COMP_DIG,
				"===> [AGC_TAB] big_jump_lmt [%d] = 0x%x\n",
				agc_table_idx,
				dig_tab->big_jump_lmt[agc_table_idx]);
		}
	} else {
		is_limit = false;
	}

	dig_tab->rf_gain_idx = rf_gain_idx;
}

void odm_config_bb_agc_8822b(struct phy_dm_struct *dm, u32 addr, u32 bitmask,
			     u32 data)
{
	odm_update_agc_big_jump_lmt_8822b(dm, addr, data);

	odm_set_bb_reg(dm, addr, bitmask, data);

	/* Add 1us delay between BB/RF register setting. */
	ODM_delay_us(1);

	ODM_RT_TRACE(dm, ODM_COMP_INIT, "===> %s: [AGC_TAB] %08X %08X\n",
		     __func__, addr, data);
}

void odm_config_bb_phy_reg_pg_8822b(struct phy_dm_struct *dm, u32 band,
				    u32 rf_path, u32 tx_num, u32 addr,
				    u32 bitmask, u32 data)
{
	if (addr == 0xfe || addr == 0xffe) {
		ODM_sleep_ms(50);
	} else {
		phy_store_tx_power_by_rate(dm->adapter, band, rf_path, tx_num,
					   addr, bitmask, data);
	}
	ODM_RT_TRACE(dm, ODM_COMP_INIT, "===> %s: [PHY_REG] %08X %08X %08X\n",
		     __func__, addr, bitmask, data);
}

void odm_config_bb_phy_8822b(struct phy_dm_struct *dm, u32 addr, u32 bitmask,
			     u32 data)
{
	if (addr == 0xfe)
		ODM_sleep_ms(50);
	else if (addr == 0xfd)
		ODM_delay_ms(5);
	else if (addr == 0xfc)
		ODM_delay_ms(1);
	else if (addr == 0xfb)
		ODM_delay_us(50);
	else if (addr == 0xfa)
		ODM_delay_us(5);
	else if (addr == 0xf9)
		ODM_delay_us(1);
	else
		odm_set_bb_reg(dm, addr, bitmask, data);

	/* Add 1us delay between BB/RF register setting. */
	ODM_delay_us(1);
	ODM_RT_TRACE(dm, ODM_COMP_INIT, "===> %s: [PHY_REG] %08X %08X\n",
		     __func__, addr, data);
}

void odm_config_bb_txpwr_lmt_8822b(struct phy_dm_struct *dm, u8 *regulation,
				   u8 *band, u8 *bandwidth, u8 *rate_section,
				   u8 *rf_path, u8 *channel, u8 *power_limit)
{
	phy_set_tx_power_limit(dm, regulation, band, bandwidth, rate_section,
			       rf_path, channel, power_limit);
}

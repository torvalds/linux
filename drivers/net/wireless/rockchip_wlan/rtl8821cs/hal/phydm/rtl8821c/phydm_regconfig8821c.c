/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8821C_SUPPORT == 1)

void odm_config_rf_reg_8821c(struct dm_struct *dm, u32 addr, u32 data,
			     enum rf_path rf_path, u32 reg_addr)
{
	if (dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD) {
		if (addr == 0xffe) {
			phydm_set_reg_by_fw(dm,
					    PHYDM_HALMAC_CMD_DELAY_MS,
					    reg_addr,
					    data,
					    RFREGOFFSETMASK,
					    rf_path,
					    50);
		} else if (addr == 0xfe) {
			phydm_set_reg_by_fw(dm,
					    PHYDM_HALMAC_CMD_DELAY_US,
					    reg_addr,
					    data,
					    RFREGOFFSETMASK,
					    rf_path,
					    100);
		} else {
			phydm_set_reg_by_fw(dm,
					    PHYDM_HALMAC_CMD_RF_W,
					    reg_addr,
					    data,
					    RFREGOFFSETMASK,
					    rf_path,
					    0);
			phydm_set_reg_by_fw(dm,
					    PHYDM_HALMAC_CMD_DELAY_US,
					    reg_addr,
					    data,
					    RFREGOFFSETMASK,
					    rf_path,
					    1);
		}
	} else {
		if (addr == 0xffe) {
#ifdef CONFIG_LONG_DELAY_ISSUE
			ODM_sleep_ms(50);
#else
			ODM_delay_ms(50);
#endif
		} else if (addr == 0xfe) {
#ifdef CONFIG_LONG_DELAY_ISSUE
			ODM_sleep_us(100);
#else
			ODM_delay_us(100);
#endif
		} else {
			odm_set_rf_reg(dm, rf_path, reg_addr, RFREGOFFSETMASK, data);

			/* Add 1us delay between BB/RF register setting. */
			ODM_delay_us(1);
		}
	}
}

void odm_config_rf_radio_a_8821c(struct dm_struct *dm, u32 addr, u32 data)
{
	u32 content = 0x1000; /* RF_Content: radioa_txt */
	u32 maskfor_phy_set = (u32)(content & 0xE000);

	odm_config_rf_reg_8821c(dm, addr, data, RF_PATH_A, addr | maskfor_phy_set);

	PHYDM_DBG(dm, ODM_COMP_INIT,
		  "===> odm_config_rf_with_header_file: [RadioA] %08X %08X\n",
		  addr, data);
}

void odm_config_mac_8821c(struct dm_struct *dm, u32 addr, u8 data)
{
	if (dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD)
		phydm_set_reg_by_fw(dm,
				    PHYDM_HALMAC_CMD_MAC_W8,
				    addr,
				    data,
				    0,
				    (enum rf_path)0,
				    0);
	else
		odm_write_1byte(dm, addr, data);
	PHYDM_DBG(dm, ODM_COMP_INIT, "===> config_mac: [MAC_REG] %08X %08X\n",
		  addr, data);
}

void odm_update_agc_big_jump_lmt_8821c(struct dm_struct *dm, u32 addr, u32 data)
{
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	u8 rf_gain_idx = (u8)((data & 0xFF000000) >> 24);
	u8 bb_gain_idx = (u8)((data & 0x00ff0000) >> 16);
	u8 agc_table_idx = (u8)((data & 0x00000f00) >> 8);
	static boolean is_limit;

	if (addr != 0x81c)
		return;

	/*dbg_print("data = 0x%x, rf_gain_idx = 0x%x, bb_gain_idx = 0x%x, agc_table_idx = 0x%x\n", data, rf_gain_idx, bb_gain_idx, agc_table_idx);*/
	/*dbg_print("rf_gain_idx = 0x%x, dig_t->rf_gain_idx = 0x%x\n", rf_gain_idx, dig_t->rf_gain_idx);*/

	if (bb_gain_idx > 0x3c) {
		if (rf_gain_idx == dig_t->rf_gain_idx && is_limit == false) {
			is_limit = true;
			dig_t->big_jump_lmt[agc_table_idx] = bb_gain_idx - 2;
			PHYDM_DBG(dm, DBG_DIG,
				  "===> [AGC_TAB] big_jump_lmt [%d] = 0x%x\n",
				  agc_table_idx,
				  dig_t->big_jump_lmt[agc_table_idx]);
		}
	} else {
		is_limit = false;
	}

	dig_t->rf_gain_idx = rf_gain_idx;
}

void odm_config_bb_agc_8821c(struct dm_struct *dm, u32 addr, u32 bitmask,
			     u32 data)
{
	odm_update_agc_big_jump_lmt_8821c(dm, addr, data);

	if (dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD)
		phydm_set_reg_by_fw(dm,
				    PHYDM_HALMAC_CMD_BB_W32,
				    addr,
				    data,
				    bitmask,
				    (enum rf_path)0,
				    0);
	else
		odm_set_bb_reg(dm, addr, bitmask, data);

	PHYDM_DBG(dm, ODM_COMP_INIT, "===> config_bb: [AGC_TAB] %08X %08X\n",
		  addr, data);
}

void odm_config_bb_phy_reg_pg_8821c(struct dm_struct *dm, u32 band, u32 rf_path,
				    u32 tx_num, u32 addr, u32 bitmask, u32 data)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_store_tx_power_by_rate(dm->adapter, band, rf_path, tx_num, addr, bitmask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PHY_StoreTxPowerByRate(dm->adapter, band, rf_path, tx_num, addr, bitmask, data);
#endif

	PHYDM_DBG(dm, ODM_COMP_INIT,
		  "===> odm_config_bb_with_header_file: [PHY_REG] %08X %08X %08X\n",
		  addr, bitmask, data);
}

void odm_config_bb_phy_8821c(struct dm_struct *dm, u32 addr, u32 bitmask,
			     u32 data)
{
	if (dm->fw_offload_ability & PHYDM_PHY_PARAM_OFFLOAD) {
		u32 delay_time = 0;

		if (addr >= 0xf9 && addr <= 0xfe) {
			if (addr == 0xfe || addr == 0xfb)
				delay_time = 50;
			else if (addr == 0xfd || addr == 0xfa)
				delay_time = 5;
			else
				delay_time = 1;

			if (addr >= 0xfc && addr <= 0xfe)
				phydm_set_reg_by_fw(dm,
						    PHYDM_HALMAC_CMD_DELAY_MS,
						    addr,
						    data,
						    bitmask,
						    (enum rf_path)0,
						    delay_time);
			else
				phydm_set_reg_by_fw(dm,
						    PHYDM_HALMAC_CMD_DELAY_US,
						    addr,
						    data,
						    bitmask,
						    (enum rf_path)0,
						    delay_time);
		} else {
			phydm_set_reg_by_fw(dm,
					    PHYDM_HALMAC_CMD_BB_W32,
					    addr,
					    data,
					    bitmask,
					    (enum rf_path)0,
					    0);
		}
	} else {
		if (addr == 0xfe)
#ifdef CONFIG_LONG_DELAY_ISSUE
			ODM_sleep_ms(50);
#else
			ODM_delay_ms(50);
#endif
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
	}

	PHYDM_DBG(dm, ODM_COMP_INIT, "===> config_bb: [PHY_REG] %08X %08X\n",
		  addr, data);
}

void odm_config_bb_txpwr_lmt_8821c(struct dm_struct *dm, u8 *regulation,
				   u8 *band, u8 *bandwidth, u8 *rate_section,
				   u8 *rf_path, u8 *channel, u8 *power_limit)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_set_tx_power_limit(dm, regulation, band,
			       bandwidth, rate_section, rf_path, channel, power_limit);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PHY_SetTxPowerLimit(dm, regulation, band,
			    bandwidth, rate_section, rf_path, channel, power_limit);
#endif
}

#endif

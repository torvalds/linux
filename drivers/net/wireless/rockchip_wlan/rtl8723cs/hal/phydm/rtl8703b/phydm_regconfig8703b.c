/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8703B_SUPPORT == 1)

void
odm_config_rf_reg_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data,
	enum odm_rf_radio_path_e     RF_PATH,
	u32				    reg_addr
)
{
	if (addr == 0xfe || addr == 0xffe) {
#ifdef CONFIG_LONG_DELAY_ISSUE
		ODM_sleep_ms(50);
#else
		ODM_delay_ms(50);
#endif
	} else {
		odm_set_rf_reg(p_dm_odm, RF_PATH, reg_addr, RFREGOFFSETMASK, data);
		/* Add 1us delay between BB/RF register setting. */
		ODM_delay_us(1);
	}
}


void
odm_config_rf_radio_a_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
)
{
	u32  content = 0x1000; /* RF_Content: radioa_txt */
	u32	maskfor_phy_set = (u32)(content & 0xE000);

	odm_config_rf_reg_8703b(p_dm_odm, addr, data, ODM_RF_PATH_A, addr | maskfor_phy_set);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> odm_config_rf_with_header_file: [RadioA] %08X %08X\n", addr, data));
}

void
odm_config_rf_radio_b_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
)
{
	u32  content = 0x1001; /* RF_Content: radiob_txt */
	u32	maskfor_phy_set = (u32)(content & 0xE000);

	odm_config_rf_reg_8703b(p_dm_odm, addr, data, ODM_RF_PATH_B, addr | maskfor_phy_set);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> odm_config_rf_with_header_file: [RadioB] %08X %08X\n", addr, data));

}

void
odm_config_rf_radio_c_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
)
{
	u32  content = 0x1001; /* RF_Content: radiob_txt */
	u32	maskfor_phy_set = (u32)(content & 0xE000);

	odm_config_rf_reg_8703b(p_dm_odm, addr, data, ODM_RF_PATH_C, addr | maskfor_phy_set);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> odm_config_rf_with_header_file: [RadioC] %08X %08X\n", addr, data));

}

void
odm_config_rf_radio_d_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
)
{
	u32  content = 0x1001; /* RF_Content: radiob_txt */
	u32	maskfor_phy_set = (u32)(content & 0xE000);

	odm_config_rf_reg_8703b(p_dm_odm, addr, data, ODM_RF_PATH_D, addr | maskfor_phy_set);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> odm_config_rf_with_header_file: [RadioD] %08X %08X\n", addr, data));

}

void
odm_config_mac_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		addr,
	u8		data
)
{
	odm_write_1byte(p_dm_odm, addr, data);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> odm_config_mac_with_header_file: [MAC_REG] %08X %08X\n", addr, data));
}

void
odm_config_bb_agc_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		addr,
	u32		bitmask,
	u32		data
)
{
	odm_set_bb_reg(p_dm_odm, addr, bitmask, data);
	/* Add 1us delay between BB/RF register setting. */
	ODM_delay_us(1);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> odm_config_bb_with_header_file: [AGC_TAB] %08X %08X\n", addr, data));
}

void
odm_config_bb_phy_reg_pg_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		band,
	u32		rf_path,
	u32		tx_num,
	u32		addr,
	u32		bitmask,
	u32		data
)
{
	if (addr == 0xfe || addr == 0xffe)
#ifdef CONFIG_LONG_DELAY_ISSUE
		ODM_sleep_ms(50);
#else
		ODM_delay_ms(50);
#endif
	else {
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
		phy_store_tx_power_by_rate(p_dm_odm->adapter, band, rf_path, tx_num, addr, bitmask, data);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		PHY_StoreTxPowerByRate(p_dm_odm->adapter, band, rf_path, tx_num, addr, bitmask, data);
#endif
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_config_bb_with_header_file: [PHY_REG] %08X %08X %08X\n", addr, bitmask, data));
}

void
odm_config_bb_phy_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		addr,
	u32		bitmask,
	u32		data
)
{
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
		odm_set_bb_reg(p_dm_odm, addr, bitmask, data);

	/* Add 1us delay between BB/RF register setting. */
	ODM_delay_us(1);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("===> odm_config_bb_with_header_file: [PHY_REG] %08X %08X\n", addr, data));
}

void
odm_config_bb_txpwr_lmt_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		*regulation,
	u8		*band,
	u8		*bandwidth,
	u8		*rate_section,
	u8		*rf_path,
	u8	*channel,
	u8		*power_limit
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	phy_set_tx_power_limit(p_dm_odm, regulation, band,
		       bandwidth, rate_section, rf_path, channel, power_limit);
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PHY_SetTxPowerLimit(p_dm_odm, regulation, band,
		       bandwidth, rate_section, rf_path, channel, power_limit);
#endif
}
#endif

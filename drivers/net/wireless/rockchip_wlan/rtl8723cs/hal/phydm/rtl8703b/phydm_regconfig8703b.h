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
#ifndef __INC_ODM_REGCONFIG_H_8703B
#define __INC_ODM_REGCONFIG_H_8703B

#if (RTL8703B_SUPPORT == 1)

void
odm_config_rf_reg_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data,
	enum odm_rf_radio_path_e     RF_PATH,
	u32				    reg_addr
);

void
odm_config_rf_radio_a_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
);

void
odm_config_rf_radio_b_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
);

void
odm_config_rf_radio_c_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
);

void
odm_config_rf_radio_d_8703b(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					addr,
	u32					data
);

void
odm_config_mac_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		addr,
	u8		data
);

void
odm_config_bb_agc_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		addr,
	u32		bitmask,
	u32		data
);

void
odm_config_bb_phy_reg_pg_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		band,
	u32		rf_path,
	u32		tx_num,
	u32		addr,
	u32		bitmask,
	u32		data
);

void
odm_config_bb_phy_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		addr,
	u32		bitmask,
	u32		data
);

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
);
#endif
#endif /* end of SUPPORT */

/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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


#ifndef	__PHYDMKFREE_H__
#define    __PHYDKFREE_H__

#define KFREE_VERSION	"1.0"

#define	KFREE_BAND_NUM		6

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_AP))

#define	BB_GAIN_NUM		6
#define KFREE_FLAG_ON				BIT(0)
#define KFREE_FLAG_THERMAL_K_ON		BIT(1)

#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211)
#define KFREE_FLAG_ON				BIT(0)
#define KFREE_FLAG_THERMAL_K_ON		BIT(1)
#endif

#define KFREE_FLAG_ON_2G				BIT(2)
#define KFREE_FLAG_ON_5G				BIT(3)

#define PPG_THERMAL_OFFSET_8821C				0x1EF
#define PPG_BB_GAIN_2G_TXAB_OFFSET_8821C		0x1EE
#define PPG_BB_GAIN_5GL1_TXA_OFFSET_8821C		0x1EC
#define PPG_BB_GAIN_5GL2_TXA_OFFSET_8821C		0x1E8
#define PPG_BB_GAIN_5GM1_TXA_OFFSET_8821C		0x1E4
#define PPG_BB_GAIN_5GM2_TXA_OFFSET_8821C		0x1E0
#define PPG_BB_GAIN_5GH1_TXA_OFFSET_8821C		0x1DC



#define PPG_THERMAL_OFFSET				0x3EF
#define PPG_BB_GAIN_2G_TXAB_OFFSET		0x3EE
#define PPG_BB_GAIN_2G_TXCD_OFFSET		0x3ED
#define PPG_BB_GAIN_5GL1_TXA_OFFSET		0x3EC
#define PPG_BB_GAIN_5GL1_TXB_OFFSET		0x3EB
#define PPG_BB_GAIN_5GL1_TXC_OFFSET		0x3EA
#define PPG_BB_GAIN_5GL1_TXD_OFFSET		0x3E9
#define PPG_BB_GAIN_5GL2_TXA_OFFSET		0x3E8
#define PPG_BB_GAIN_5GL2_TXB_OFFSET		0x3E7
#define PPG_BB_GAIN_5GL2_TXC_OFFSET		0x3E6
#define PPG_BB_GAIN_5GL2_TXD_OFFSET		0x3E5
#define PPG_BB_GAIN_5GM1_TXA_OFFSET		0x3E4
#define PPG_BB_GAIN_5GM1_TXB_OFFSET		0x3E3
#define PPG_BB_GAIN_5GM1_TXC_OFFSET		0x3E2
#define PPG_BB_GAIN_5GM1_TXD_OFFSET		0x3E1
#define PPG_BB_GAIN_5GM2_TXA_OFFSET		0x3E0
#define PPG_BB_GAIN_5GM2_TXB_OFFSET		0x3DF
#define PPG_BB_GAIN_5GM2_TXC_OFFSET		0x3DE
#define PPG_BB_GAIN_5GM2_TXD_OFFSET		0x3DD
#define PPG_BB_GAIN_5GH1_TXA_OFFSET		0x3DC
#define PPG_BB_GAIN_5GH1_TXB_OFFSET		0x3DB
#define PPG_BB_GAIN_5GH1_TXC_OFFSET		0x3DA
#define PPG_BB_GAIN_5GH1_TXD_OFFSET		0x3D9

#define PPG_PA_BIAS_2G_TXA_OFFSET		0x3D5
#define PPG_PA_BIAS_2G_TXB_OFFSET		0x3D6



struct odm_power_trim_data {
	u8 flag;
	s8 bb_gain[KFREE_BAND_NUM][MAX_RF_PATH];
	s8 thermal;
};



enum phydm_kfree_channeltosw {
	PHYDM_2G = 0,
	PHYDM_5GLB1 = 1,
	PHYDM_5GLB2 = 2,
	PHYDM_5GMB1 = 3,
	PHYDM_5GMB2 = 4,
	PHYDM_5GHB = 5,
};



void
phydm_get_thermal_trim_offset(
	void	*p_dm_void
);

void
phydm_get_power_trim_offset(
	void	*p_dm_void
);

void
phydm_get_pa_bias_offset(
	void	*p_dm_void
);

s8
phydm_get_thermal_offset(
	void	*p_dm_void
);

void
phydm_clear_kfree_to_rf(
	void		*p_dm_void,
	u8		e_rf_path,
	u8		data
);


void
phydm_config_kfree(
	void	*p_dm_void,
	u8	channel_to_sw
);


#endif

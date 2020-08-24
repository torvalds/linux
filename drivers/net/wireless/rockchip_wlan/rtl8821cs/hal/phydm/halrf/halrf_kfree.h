/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

#ifndef __HALRF_KFREE_H__
#define __HALRF_KFREE_H__

#define KFREE_VERSION "1.0"

#define KFREE_BAND_NUM 9
#define KFREE_CH_NUM 3

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_AP))

#define BB_GAIN_NUM 6

#endif

#define KFREE_FLAG_ON BIT(0)
#define KFREE_FLAG_THERMAL_K_ON BIT(1)

#define KFREE_FLAG_ON_2G BIT(2)
#define KFREE_FLAG_ON_5G BIT(3)

#define PA_BIAS_FLAG_ON BIT(4)

#define TSSI_TRIM_FLAG_ON BIT(5)

#define LNA_FLAG_ON BIT(6)


#define PPG_THERMAL_OFFSET_98F 0x50
#define PPG_2GM_TXAB_98F 0x51
#define PPG_2GM_TXCD_98F 0x52
#define PPG_2GL_TXAB_98F 0x53
#define PPG_2GL_TXCD_98F 0x54
#define PPG_2GH_TXAB_98F 0x55
#define PPG_2GH_TXCD_98F 0x56

#define PPG_PABIAS_2GAB_98F 0x57
#define PPG_PABIAS_2GCD_98F 0x58

#define PPG_LNA_2GA_98F 0x59
#define PPG_LNA_2GB_98F 0x5a
#define PPG_LNA_2GC_98F 0x5b
#define PPG_LNA_2GD_98F 0x5c

#define PPG_THERMAL_OFFSET_21C 0x1EF
#define PPG_2G_TXAB_21C 0x1EE
#define PPG_5GL1_TXA_21C 0x1EC
#define PPG_5GL2_TXA_21C 0x1E8
#define PPG_5GM1_TXA_21C 0x1E4
#define PPG_5GM2_TXA_21C 0x1E0
#define PPG_5GH1_TXA_21C 0x1DC

#define PPG_THERMAL_OFFSET_22B 0x3EF
#define PPG_2G_TXAB_22B 0x3EE
#define PPG_2G_TXCD_22B 0x3ED
#define PPG_5GL1_TXA_22B 0x3EC
#define PPG_5GL1_TXB_22B 0x3EB
#define PPG_5GL1_TXC_22B 0x3EA
#define PPG_5GL1_TXD_22B 0x3E9
#define PPG_5GL2_TXA_22B 0x3E8
#define PPG_5GL2_TXB_22B 0x3E7
#define PPG_5GL2_TXC_22B 0x3E6
#define PPG_5GL2_TXD_22B 0x3E5
#define PPG_5GM1_TXA_22B 0x3E4
#define PPG_5GM1_TXB_22B 0x3E3
#define PPG_5GM1_TXC_22B 0x3E2
#define PPG_5GM1_TXD_22B 0x3E1
#define PPG_5GM2_TXA_22B 0x3E0
#define PPG_5GM2_TXB_22B 0x3DF
#define PPG_5GM2_TXC_22B 0x3DE
#define PPG_5GM2_TXD_22B 0x3DD
#define PPG_5GH1_TXA_22B 0x3DC
#define PPG_5GH1_TXB_22B 0x3DB
#define PPG_5GH1_TXC_22B 0x3DA
#define PPG_5GH1_TXD_22B 0x3D9

#define PPG_PABIAS_2GA_22B 0x3D5
#define PPG_PABIAS_2GB_22B 0x3D6

#define PPG_THERMAL_A_OFFSET_22C 0x1ef
#define PPG_THERMAL_B_OFFSET_22C 0x1b0
#define PPG_2GL_TXAB_22C 0x1d4
#define PPG_2GM_TXAB_22C 0x1ee
#define PPG_2GH_TXAB_22C 0x1d2
#define PPG_5GL1_TXA_22C 0x1ec
#define PPG_5GL1_TXB_22C 0x1eb
#define PPG_5GL2_TXA_22C 0x1e8
#define PPG_5GL2_TXB_22C 0x1e7
#define PPG_5GM1_TXA_22C 0x1e4
#define PPG_5GM1_TXB_22C 0x1e3
#define PPG_5GM2_TXA_22C 0x1e0
#define PPG_5GM2_TXB_22C 0x1df
#define PPG_5GH1_TXA_22C 0x1dc
#define PPG_5GH1_TXB_22C 0x1db

#define PPG_PABIAS_2GA_22C 0x1d6
#define PPG_PABIAS_2GB_22C 0x1d5
#define PPG_PABIAS_5GA_22C 0x1d8
#define PPG_PABIAS_5GB_22C 0x1d7

#define TSSI_2GM_TXA_22C 0x1c0
#define TSSI_2GM_TXB_22C 0x1bf
#define TSSI_2GH_TXA_22C 0x1be
#define TSSI_2GH_TXB_22C 0x1bd
#define TSSI_5GL1_TXA_22C 0x1bc
#define TSSI_5GL1_TXB_22C 0x1bb
#define TSSI_5GL2_TXA_22C 0x1ba
#define TSSI_5GL2_TXB_22C 0x1b9
#define TSSI_5GM1_TXA_22C 0x1b8
#define TSSI_5GM1_TXB_22C 0x1b7
#define TSSI_5GM2_TXA_22C 0x1b6
#define TSSI_5GM2_TXB_22C 0x1b5
#define TSSI_5GH1_TXA_22C 0x1b4
#define TSSI_5GH1_TXB_22C 0x1b3
#define TSSI_5GH2_TXA_22C 0x1b2
#define TSSI_5GH2_TXB_22C 0x1b1
 
/*8195B*/
#define PPG_THERMAL_OFFSET_95B 0x1ef
#define PPG_2GL_TXA_95B 0x1d4
#define PPG_2GM_TXA_95B 0x1ee
#define PPG_2GH_TXA_95B 0x1d2
#define PPG_5GL1_TXA_95B 0x1ec
#define PPG_5GL2_TXA_95B 0x1e8
#define PPG_5GM1_TXA_95B 0x1e4
#define PPG_5GM2_TXA_95B 0x1e0
#define PPG_5GH1_TXA_95B 0x1dc

#define PPG_PABIAS_2GA_95B 0x1d6
#define PPG_PABIAS_5GA_95B 0x1d8

/*8721D*/
/*#define KFREE_BAND_NUM_8721D 6*/
#define PPG_THERMAL_OFFSET_8721D 0x1EF
#define PPG_2G_TXA_8721D 0x1EE
#define PPG_5GL1_TXA_8721D 0x1ED
#define PPG_5GL2_TXA_8721D 0x1EC
#define PPG_5GM1_TXA_8721D 0x1EB
#define PPG_5GM2_TXA_8721D 0x1EA
#define PPG_5GH1_TXA_8721D 0x1E9

/*8197G*/
#define PPG_THERMAL_OFFSET_97G 0x50
#define PPG_2GM_TXAB_97G 0x51
#define PPG_2GL_TXAB_97G 0x53
#define PPG_2GH_TXAB_97G 0x55
#define PPG_PABIAS_2GAB_97G 0x57
#define PPG_LNA_2GA_97G 0x21
#define PPG_LNA_2GB_97G 0x22

/*8710C Ameba Z2*/
#define PPG_THERMAL_OFFSET_10C 0x1EF
#define PPG_2GL_TX_10C 0x1D4
#define PPG_2GM_TX_10C 0x1EE
#define PPG_2GH_TX_10C 0x1D2
#define PPG_PABIAS_10C 0x1D6
#define PPG_LNA_10C 0x1D0

/*8814B*/
#define PPG_THERMAL_A_OFFSET_14B 0x3D4
#define PPG_THERMAL_B_OFFSET_14B 0x3D3
#define PPG_THERMAL_C_OFFSET_14B 0x3D2
#define PPG_THERMAL_D_OFFSET_14B 0x3D1

struct odm_power_trim_data {
	u8 flag;
	u8 pa_bias_flag;
	u8 lna_flag;
	s8 bb_gain[KFREE_BAND_NUM][MAX_RF_PATH];
	s8 tssi_trim[KFREE_BAND_NUM][MAX_RF_PATH];
	s8 lna_trim[MAX_RF_PATH];
	s8 thermal;
	s8 multi_thermal[MAX_RF_PATH];
};

enum phydm_kfree_channeltosw {
	PHYDM_2G = 0,
	PHYDM_5GLB1 = 1,
	PHYDM_5GLB2 = 2,
	PHYDM_5GMB1 = 3,
	PHYDM_5GMB2 = 4,
	PHYDM_5GHB = 5,
};

void phydm_get_thermal_trim_offset(void *dm_void);

void phydm_get_power_trim_offset(void *dm_void);

void phydm_get_pa_bias_offset(void *dm_void);

s8 phydm_get_thermal_offset(void *dm_void);

s8 phydm_get_multi_thermal_offset(void *dm_void, u8 path);

void phydm_clear_kfree_to_rf(void *dm_void, u8 e_rf_path, u8 data);

void phydm_config_new_kfree(void *dm_void);

s8 phydm_get_tssi_trim_de(void *dm_void, u8 path);

void phydm_config_kfree(void *dm_void, u8 channel_to_sw);

void phydm_set_lna_trim_offset (void *dm_void, u8 path, u8 cg_cs, u8 enable);

#endif /*__HALRF_KFREE_H__*/

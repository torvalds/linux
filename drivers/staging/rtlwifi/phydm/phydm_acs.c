// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/* ************************************************************
 * include files
 * *************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

u8 odm_get_auto_channel_select_result(void *dm_void, u8 band)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct acs_info *acs = &dm->dm_acs;
	u8 result;

	if (band == ODM_BAND_2_4G) {
		ODM_RT_TRACE(
			dm, ODM_COMP_ACS,
			"[struct acs_info] %s(): clean_channel_2g(%d)\n",
			__func__, acs->clean_channel_2g);
		result = (u8)acs->clean_channel_2g;
	} else {
		ODM_RT_TRACE(
			dm, ODM_COMP_ACS,
			"[struct acs_info] %s(): clean_channel_5g(%d)\n",
			__func__, acs->clean_channel_5g);
		result = (u8)acs->clean_channel_5g;
	}

	return result;
}

static void odm_auto_channel_select_setting(void *dm_void, bool is_enable)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u16 period = 0x2710; /* 40ms in default */
	u16 nhm_type = 0x7;

	ODM_RT_TRACE(dm, ODM_COMP_ACS, "%s()=========>\n", __func__);

	if (is_enable) {
		/* 20 ms */
		period = 0x1388;
		nhm_type = 0x1;
	}

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/* PHY parameters initialize for ac series */

		/* 0x990[31:16]=0x2710
		 * Time duration for NHM unit: 4us, 0x2710=40ms
		 */
		odm_write_2byte(dm, ODM_REG_CCX_PERIOD_11AC + 2, period);
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		/* PHY parameters initialize for n series */

		/* 0x894[31:16]=0x2710
		 * Time duration for NHM unit: 4us, 0x2710=40ms
		 */
		odm_write_2byte(dm, ODM_REG_CCX_PERIOD_11N + 2, period);
	}
}

void odm_auto_channel_select_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct acs_info *acs = &dm->dm_acs;
	u8 i;

	if (!(dm->support_ability & ODM_BB_NHM_CNT))
		return;

	if (acs->is_force_acs_result)
		return;

	ODM_RT_TRACE(dm, ODM_COMP_ACS, "%s()=========>\n", __func__);

	acs->clean_channel_2g = 1;
	acs->clean_channel_5g = 36;

	for (i = 0; i < ODM_MAX_CHANNEL_2G; ++i) {
		acs->channel_info_2g[0][i] = 0;
		acs->channel_info_2g[1][i] = 0;
	}

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		for (i = 0; i < ODM_MAX_CHANNEL_5G; ++i) {
			acs->channel_info_5g[0][i] = 0;
			acs->channel_info_5g[1][i] = 0;
		}
	}
}

void odm_auto_channel_select_reset(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct acs_info *acs = &dm->dm_acs;

	if (!(dm->support_ability & ODM_BB_NHM_CNT))
		return;

	if (acs->is_force_acs_result)
		return;

	ODM_RT_TRACE(dm, ODM_COMP_ACS, "%s()=========>\n", __func__);

	odm_auto_channel_select_setting(dm, true); /* for 20ms measurement */
	phydm_nhm_counter_statistics_reset(dm);
}

void odm_auto_channel_select(void *dm_void, u8 channel)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct acs_info *acs = &dm->dm_acs;
	u8 channel_idx = 0, search_idx = 0;
	u16 max_score = 0;

	if (!(dm->support_ability & ODM_BB_NHM_CNT)) {
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"%s(): Return: support_ability ODM_BB_NHM_CNT is disabled\n",
			__func__);
		return;
	}

	if (acs->is_force_acs_result) {
		ODM_RT_TRACE(
			dm, ODM_COMP_DIG,
			"%s(): Force 2G clean channel = %d, 5G clean channel = %d\n",
			__func__, acs->clean_channel_2g, acs->clean_channel_5g);
		return;
	}

	ODM_RT_TRACE(dm, ODM_COMP_ACS, "%s(): channel = %d=========>\n",
		     __func__, channel);

	phydm_get_nhm_counter_statistics(dm);
	odm_auto_channel_select_setting(dm, false);

	if (channel >= 1 && channel <= 14) {
		channel_idx = channel - 1;
		acs->channel_info_2g[1][channel_idx]++;

		if (acs->channel_info_2g[1][channel_idx] >= 2)
			acs->channel_info_2g[0][channel_idx] =
				(acs->channel_info_2g[0][channel_idx] >> 1) +
				(acs->channel_info_2g[0][channel_idx] >> 2) +
				(dm->nhm_cnt_0 >> 2);
		else
			acs->channel_info_2g[0][channel_idx] = dm->nhm_cnt_0;

		ODM_RT_TRACE(dm, ODM_COMP_ACS, "%s(): nhm_cnt_0 = %d\n",
			     __func__, dm->nhm_cnt_0);
		ODM_RT_TRACE(
			dm, ODM_COMP_ACS,
			"%s(): Channel_Info[0][%d] = %d, Channel_Info[1][%d] = %d\n",
			__func__, channel_idx,
			acs->channel_info_2g[0][channel_idx], channel_idx,
			acs->channel_info_2g[1][channel_idx]);

		for (search_idx = 0; search_idx < ODM_MAX_CHANNEL_2G;
		     search_idx++) {
			if (acs->channel_info_2g[1][search_idx] != 0 &&
			    acs->channel_info_2g[0][search_idx] >= max_score) {
				max_score = acs->channel_info_2g[0][search_idx];
				acs->clean_channel_2g = search_idx + 1;
			}
		}
		ODM_RT_TRACE(
			dm, ODM_COMP_ACS,
			"(1)%s(): 2G: clean_channel_2g = %d, max_score = %d\n",
			__func__, acs->clean_channel_2g, max_score);

	} else if (channel >= 36) {
		/* Need to do */
		acs->clean_channel_5g = channel;
	}
}

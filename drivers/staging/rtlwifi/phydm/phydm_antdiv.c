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

/* ******************************************************
 * when antenna test utility is on or some testing need to disable antenna
 * diversity, call this function to disable all ODM related mechanisms which
 * will switch antenna.
 * *******************************************************/
void odm_stop_antenna_switch_dm(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	/* disable ODM antenna diversity */
	dm->support_ability &= ~ODM_BB_ANT_DIV;
	ODM_RT_TRACE(dm, ODM_COMP_ANT_DIV, "STOP Antenna Diversity\n");
}

void phydm_enable_antenna_diversity(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	dm->support_ability |= ODM_BB_ANT_DIV;
	ODM_RT_TRACE(dm, ODM_COMP_ANT_DIV,
		     "AntDiv is enabled & Re-Init AntDiv\n");
	odm_antenna_diversity_init(dm);
}

void odm_set_ant_config(void *dm_void, u8 ant_setting /* 0=A, 1=B, 2=C, .... */
			)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type == ODM_RTL8723B) {
		if (ant_setting == 0) /* ant A*/
			odm_set_bb_reg(dm, 0x948, MASKDWORD, 0x00000000);
		else if (ant_setting == 1)
			odm_set_bb_reg(dm, 0x948, MASKDWORD, 0x00000280);
	} else if (dm->support_ic_type == ODM_RTL8723D) {
		if (ant_setting == 0) /* ant A*/
			odm_set_bb_reg(dm, 0x948, MASKLWORD, 0x0000);
		else if (ant_setting == 1)
			odm_set_bb_reg(dm, 0x948, MASKLWORD, 0x0280);
	}
}

/* ****************************************************** */

void odm_sw_ant_div_rest_after_link(void *dm_void) {}

void odm_ant_div_reset(void *dm_void) {}

void odm_antenna_diversity_init(void *dm_void) {}

void odm_antenna_diversity(void *dm_void) {}

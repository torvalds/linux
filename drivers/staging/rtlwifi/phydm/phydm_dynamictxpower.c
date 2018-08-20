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

void odm_dynamic_tx_power_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	dm->last_dtp_lvl = tx_high_pwr_level_normal;
	dm->dynamic_tx_high_power_lvl = tx_high_pwr_level_normal;
	dm->tx_agc_ofdm_18_6 =
		odm_get_bb_reg(dm, 0xC24, MASKDWORD); /*TXAGC {18M 12M 9M 6M}*/
}

void odm_dynamic_tx_power_save_power_index(void *dm_void) {}

void odm_dynamic_tx_power_restore_power_index(void *dm_void) {}

void odm_dynamic_tx_power_write_power_index(void *dm_void, u8 value)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 index;
	u32 power_index_reg[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};

	for (index = 0; index < 6; index++)
		odm_write_1byte(dm, power_index_reg[index], value);
}

static void odm_dynamic_tx_power_nic_ce(void *dm_void) {}

void odm_dynamic_tx_power(void *dm_void)
{
	/*  */
	/* For AP/ADSL use struct rtl8192cd_priv* */
	/* For CE/NIC use struct void* */
	/*  */
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;
	/* 2011/09/29 MH In HW integration first stage, we provide 4 different
	 * handle to operate at the same time.
	 * In the stage2/3, we need to prive universal interface and merge all
	 * HW dynamic mechanism.
	 */
	switch (dm->support_platform) {
	case ODM_WIN:
		odm_dynamic_tx_power_nic(dm);
		break;
	case ODM_CE:
		odm_dynamic_tx_power_nic_ce(dm);
		break;
	case ODM_AP:
		odm_dynamic_tx_power_ap(dm);
		break;
	default:
		break;
	}
}

void odm_dynamic_tx_power_nic(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (!(dm->support_ability & ODM_BB_DYNAMIC_TXPWR))
		return;
}

void odm_dynamic_tx_power_ap(void *dm_void

			     )
{
}

void odm_dynamic_tx_power_8821(void *dm_void, u8 *desc, u8 mac_id) {}

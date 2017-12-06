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

/* ************************************************************
 * include files
 * *************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

static inline void phydm_update_rf_state(struct phy_dm_struct *dm,
					 struct dyn_pwr_saving *dm_ps_table,
					 int _rssi_up_bound,
					 int _rssi_low_bound,
					 int _is_force_in_normal)
{
	if (_is_force_in_normal) {
		dm_ps_table->cur_rf_state = rf_normal;
		return;
	}

	if (dm->rssi_min == 0xFF) {
		dm_ps_table->cur_rf_state = RF_MAX;
		return;
	}

	if (dm_ps_table->pre_rf_state == rf_normal) {
		if (dm->rssi_min >= _rssi_up_bound)
			dm_ps_table->cur_rf_state = rf_save;
		else
			dm_ps_table->cur_rf_state = rf_normal;
	} else {
		if (dm->rssi_min <= _rssi_low_bound)
			dm_ps_table->cur_rf_state = rf_normal;
		else
			dm_ps_table->cur_rf_state = rf_save;
	}
}

void odm_dynamic_bb_power_saving_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dyn_pwr_saving *dm_ps_table = &dm->dm_ps_table;

	dm_ps_table->pre_cca_state = CCA_MAX;
	dm_ps_table->cur_cca_state = CCA_MAX;
	dm_ps_table->pre_rf_state = RF_MAX;
	dm_ps_table->cur_rf_state = RF_MAX;
	dm_ps_table->rssi_val_min = 0;
	dm_ps_table->initialize = 0;
}

void odm_rf_saving(void *dm_void, u8 is_force_in_normal)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dyn_pwr_saving *dm_ps_table = &dm->dm_ps_table;
	u8 rssi_up_bound = 30;
	u8 rssi_low_bound = 25;

	if (dm->patch_id == 40) { /* RT_CID_819x_FUNAI_TV */
		rssi_up_bound = 50;
		rssi_low_bound = 45;
	}
	if (dm_ps_table->initialize == 0) {
		dm_ps_table->reg874 =
			(odm_get_bb_reg(dm, 0x874, MASKDWORD) & 0x1CC000) >> 14;
		dm_ps_table->regc70 =
			(odm_get_bb_reg(dm, 0xc70, MASKDWORD) & BIT(3)) >> 3;
		dm_ps_table->reg85c =
			(odm_get_bb_reg(dm, 0x85c, MASKDWORD) & 0xFF000000) >>
			24;
		dm_ps_table->rega74 =
			(odm_get_bb_reg(dm, 0xa74, MASKDWORD) & 0xF000) >> 12;
		/* Reg818 = phy_query_bb_reg(adapter, 0x818, MASKDWORD); */
		dm_ps_table->initialize = 1;
	}

	phydm_update_rf_state(dm, dm_ps_table, rssi_up_bound, rssi_low_bound,
			      is_force_in_normal);

	if (dm_ps_table->pre_rf_state != dm_ps_table->cur_rf_state) {
		if (dm_ps_table->cur_rf_state == rf_save) {
			odm_set_bb_reg(dm, 0x874, 0x1C0000,
				       0x2); /* reg874[20:18]=3'b010 */
			odm_set_bb_reg(dm, 0xc70, BIT(3),
				       0); /* regc70[3]=1'b0 */
			odm_set_bb_reg(dm, 0x85c, 0xFF000000,
				       0x63); /* reg85c[31:24]=0x63 */
			odm_set_bb_reg(dm, 0x874, 0xC000,
				       0x2); /* reg874[15:14]=2'b10 */
			odm_set_bb_reg(dm, 0xa74, 0xF000,
				       0x3); /* RegA75[7:4]=0x3 */
			odm_set_bb_reg(dm, 0x818, BIT(28),
				       0x0); /* Reg818[28]=1'b0 */
			odm_set_bb_reg(dm, 0x818, BIT(28),
				       0x1); /* Reg818[28]=1'b1 */
		} else {
			odm_set_bb_reg(dm, 0x874, 0x1CC000,
				       dm_ps_table->reg874);
			odm_set_bb_reg(dm, 0xc70, BIT(3), dm_ps_table->regc70);
			odm_set_bb_reg(dm, 0x85c, 0xFF000000,
				       dm_ps_table->reg85c);
			odm_set_bb_reg(dm, 0xa74, 0xF000, dm_ps_table->rega74);
			odm_set_bb_reg(dm, 0x818, BIT(28), 0x0);
		}
		dm_ps_table->pre_rf_state = dm_ps_table->cur_rf_state;
	}
}

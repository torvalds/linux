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

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (defined(CONFIG_BB_POWER_SAVING))

void
odm_dynamic_bb_power_saving_init(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _dynamic_power_saving	*p_dm_ps_table = &p_dm_odm->dm_ps_table;

	p_dm_ps_table->pre_cca_state = CCA_MAX;
	p_dm_ps_table->cur_cca_state = CCA_MAX;
	p_dm_ps_table->pre_rf_state = RF_MAX;
	p_dm_ps_table->cur_rf_state = RF_MAX;
	p_dm_ps_table->rssi_val_min = 0;
	p_dm_ps_table->initialize = 0;
}

void
odm_rf_saving(
	void					*p_dm_void,
	u8		is_force_in_normal
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
	struct _dynamic_power_saving	*p_dm_ps_table = &p_dm_odm->dm_ps_table;
	u8	rssi_up_bound = 30 ;
	u8	rssi_low_bound = 25;
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (p_dm_odm->patch_id == 40) { /* RT_CID_819x_FUNAI_TV */
		rssi_up_bound = 50 ;
		rssi_low_bound = 45;
	}
#endif
	if (p_dm_ps_table->initialize == 0) {

		p_dm_ps_table->reg874 = (odm_get_bb_reg(p_dm_odm, 0x874, MASKDWORD) & 0x1CC000) >> 14;
		p_dm_ps_table->regc70 = (odm_get_bb_reg(p_dm_odm, 0xc70, MASKDWORD) & BIT(3)) >> 3;
		p_dm_ps_table->reg85c = (odm_get_bb_reg(p_dm_odm, 0x85c, MASKDWORD) & 0xFF000000) >> 24;
		p_dm_ps_table->rega74 = (odm_get_bb_reg(p_dm_odm, 0xa74, MASKDWORD) & 0xF000) >> 12;
		/* Reg818 = phy_query_bb_reg(p_adapter, 0x818, MASKDWORD); */
		p_dm_ps_table->initialize = 1;
	}

	if (!is_force_in_normal) {
		if (p_dm_odm->rssi_min != 0xFF) {
			if (p_dm_ps_table->pre_rf_state == rf_normal) {
				if (p_dm_odm->rssi_min >= rssi_up_bound)
					p_dm_ps_table->cur_rf_state = rf_save;
				else
					p_dm_ps_table->cur_rf_state = rf_normal;
			} else {
				if (p_dm_odm->rssi_min <= rssi_low_bound)
					p_dm_ps_table->cur_rf_state = rf_normal;
				else
					p_dm_ps_table->cur_rf_state = rf_save;
			}
		} else
			p_dm_ps_table->cur_rf_state = RF_MAX;
	} else
		p_dm_ps_table->cur_rf_state = rf_normal;

	if (p_dm_ps_table->pre_rf_state != p_dm_ps_table->cur_rf_state) {
		if (p_dm_ps_table->cur_rf_state == rf_save) {
			odm_set_bb_reg(p_dm_odm, 0x874, 0x1C0000, 0x2); /* reg874[20:18]=3'b010 */
			odm_set_bb_reg(p_dm_odm, 0xc70, BIT(3), 0); /* regc70[3]=1'b0 */
			odm_set_bb_reg(p_dm_odm, 0x85c, 0xFF000000, 0x63); /* reg85c[31:24]=0x63 */
			odm_set_bb_reg(p_dm_odm, 0x874, 0xC000, 0x2); /* reg874[15:14]=2'b10 */
			odm_set_bb_reg(p_dm_odm, 0xa74, 0xF000, 0x3); /* RegA75[7:4]=0x3 */
			odm_set_bb_reg(p_dm_odm, 0x818, BIT(28), 0x0); /* Reg818[28]=1'b0 */
			odm_set_bb_reg(p_dm_odm, 0x818, BIT(28), 0x1); /* Reg818[28]=1'b1 */
		} else {
			odm_set_bb_reg(p_dm_odm, 0x874, 0x1CC000, p_dm_ps_table->reg874);
			odm_set_bb_reg(p_dm_odm, 0xc70, BIT(3), p_dm_ps_table->regc70);
			odm_set_bb_reg(p_dm_odm, 0x85c, 0xFF000000, p_dm_ps_table->reg85c);
			odm_set_bb_reg(p_dm_odm, 0xa74, 0xF000, p_dm_ps_table->rega74);
			odm_set_bb_reg(p_dm_odm, 0x818, BIT(28), 0x0);
		}
		p_dm_ps_table->pre_rf_state = p_dm_ps_table->cur_rf_state;
	}
#endif
}

#endif

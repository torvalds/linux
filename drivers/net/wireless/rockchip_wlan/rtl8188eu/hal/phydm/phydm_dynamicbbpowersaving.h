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

#ifndef	__PHYDMDYNAMICBBPOWERSAVING_H__
#define    __PHYDMDYNAMICBBPOWERSAVING_H__

#define DYNAMIC_BBPWRSAV_VERSION	"1.1"

#if (defined(CONFIG_BB_POWER_SAVING))

struct _dynamic_power_saving {
	u8		pre_cca_state;
	u8		cur_cca_state;

	u8		pre_rf_state;
	u8		cur_rf_state;

	int		    rssi_val_min;

	u8		initialize;
	u32		reg874, regc70, reg85c, rega74;

};

#define dm_rf_saving	odm_rf_saving

void odm_rf_saving(
	void					*p_dm_void,
	u8		is_force_in_normal
);

void
odm_dynamic_bb_power_saving_init(
	void					*p_dm_void
);
#else
#define dm_rf_saving(p_dm_void, is_force_in_normal)
#endif

#endif

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

#ifndef	__PHYDMACS_H__
#define    __PHYDMACS_H__

#define ACS_VERSION	"1.1"	/*20150729 by YuChen*/
#define CLM_VERSION "1.0"

#define ODM_MAX_CHANNEL_2G			14
#define ODM_MAX_CHANNEL_5G			24

/* For phydm_auto_channel_select_setting_ap() */
#define STORE_DEFAULT_NHM_SETTING               0
#define RESTORE_DEFAULT_NHM_SETTING             1
#define ACS_NHM_SETTING                         2

struct _ACS_ {
	boolean		is_force_acs_result;
	u8		clean_channel_2g;
	u8		clean_channel_5g;
	u16		channel_info_2g[2][ODM_MAX_CHANNEL_2G];		/* Channel_Info[1]: channel score, Channel_Info[2]:Channel_Scan_Times */
	u16		channel_info_5g[2][ODM_MAX_CHANNEL_5G];

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	u8              acs_step;
	/* NHM count 0-11 */
	u8              nhm_cnt[14][11];

	/* AC-Series, for storing previous setting */
	u32              reg0x990;
	u32              reg0x994;
	u32              reg0x998;
	u32              reg0x99c;
	u8              reg0x9a0;   /* u8 */

	/* N-Series, for storing previous setting */
	u32              reg0x890;
	u32              reg0x894;
	u32              reg0x898;
	u32              reg0x89c;
	u8              reg0xe28;   /* u8 */
#endif

};


void
odm_auto_channel_select_init(
	void			*p_dm_void
);

void
odm_auto_channel_select_reset(
	void			*p_dm_void
);

void
odm_auto_channel_select(
	void			*p_dm_void,
	u8			channel
);

u8
odm_get_auto_channel_select_result(
	void			*p_dm_void,
	u8			band
);

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)

void
phydm_auto_channel_select_setting_ap(
	void   *p_dm_void,
	u32  setting,             /* 0: STORE_DEFAULT_NHM_SETTING; 1: RESTORE_DEFAULT_NHM_SETTING, 2: ACS_NHM_SETTING */
	u32  acs_step
);

void
phydm_get_nhm_statistics_ap(
	void       *p_dm_void,
	u32      idx,                /* @ 2G, Real channel number = idx+1 */
	u32      acs_step
);

#endif  /* #if ( DM_ODM_SUPPORT_TYPE & ODM_AP ) */

#endif  /* #ifndef	__PHYDMACS_H__ */

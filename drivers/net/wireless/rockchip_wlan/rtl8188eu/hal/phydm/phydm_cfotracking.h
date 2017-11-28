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

#ifndef	__PHYDMCFOTRACK_H__
#define    __PHYDMCFOTRACK_H__

#define CFO_TRACKING_VERSION	"1.4" /*2015.10.01	Stanley, Modify for 8822B*/

#define		CFO_TH_XTAL_HIGH			20			/* kHz */
#define		CFO_TH_XTAL_LOW			10			/* kHz */
#define		CFO_TH_ATC					80			/* kHz */

struct _CFO_TRACKING_ {
	bool			is_atc_status;
	bool			large_cfo_hit;
	bool			is_adjust;
	u8			crystal_cap;
	u8			def_x_cap;
	s32			CFO_tail[4];
	u32			CFO_cnt[4];
	s32			CFO_ave_pre;
	u32			packet_count;
	u32			packet_count_pre;

	bool			is_force_xtal_cap;
	bool			is_reset;
};

void
odm_cfo_tracking_reset(
	void					*p_dm_void
);

void
odm_cfo_tracking_init(
	void					*p_dm_void
);

void
odm_cfo_tracking(
	void					*p_dm_void
);

void
odm_parsing_cfo(
	void					*p_dm_void,
	void					*p_pktinfo_void,
	s8					*pcfotail,
	u8					num_ss
);

#endif

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

#ifndef __PHYDMCFOTRACK_H__
#define __PHYDMCFOTRACK_H__

#define CFO_TRACKING_VERSION "2.3"

#define		CFO_TRK_ENABLE_TH	20 /* @kHz enable CFO_Track threshold*/
#define		CFO_TRK_STOP_TH		10 /* @kHz disable CFO_Track threshold*/
#define		CFO_TH_ATC		80 /* @kHz */

struct phydm_cfo_track_struct {
	boolean		is_atc_status;
	boolean		is_adjust;	/*@already modify crystal cap*/
	u8		crystal_cap;
	u8		crystal_cap_default;
	u8		def_x_cap;
	s32		CFO_tail[4];
	u32		CFO_cnt[4];
	s32		CFO_ave_pre;
	u32		packet_count;
	u32		packet_count_pre;
};

struct phydm_cfo_rpt {
	s32 cfo_rpt_s[PHYDM_MAX_RF_PATH];
	s32 cfo_rpt_l[PHYDM_MAX_RF_PATH];
	s32 cfo_rpt_acq[PHYDM_MAX_RF_PATH];
	s32 cfo_rpt_sec[PHYDM_MAX_RF_PATH];
	s32 cfo_rpt_end[PHYDM_MAX_RF_PATH];
};

void phydm_get_cfo_info(void *dm_void, struct phydm_cfo_rpt *cfo);

boolean phydm_set_crystal_cap_reg(void *dm_void, u8 crystal_cap);

void phydm_set_crystal_cap(void *dm_void, u8 crystal_cap);

void phydm_cfo_tracking_init(void *dm_void);

void phydm_cfo_tracking(void *dm_void);

void phydm_parsing_cfo(void *dm_void, void *pktinfo_void, s8 *pcfotail,
		       u8 num_ss);
void phydm_cfo_tracking_debug(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phy_Init_crystal_capacity(void *dm_void, u8 crystal_cap);
#endif
#endif

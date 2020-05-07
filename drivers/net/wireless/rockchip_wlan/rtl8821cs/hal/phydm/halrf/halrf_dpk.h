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

#ifndef __HALRF_DPK_H__
#define __HALRF_DPK_H__

/*@--------------------------Define Parameters-------------------------------*/
#define GAIN_LOSS 1
#define DO_DPK 2
#define DPK_ON 3
#define GAIN_LOSS_PULSE 4
#define DPK_PAS 5
#define DPK_LMS 6
#define DPK_LOK 4
#define DPK_TXK 5
#define DAGC 4
#define LOSS_CHK 0
#define GAIN_CHK 1
#define PAS_READ 2
#define AVG_THERMAL_NUM 8
#define AVG_THERMAL_NUM_DPK 8
#define THERMAL_DPK_AVG_NUM 4

/*@---------------------------End Define Parameters---------------------------*/

struct dm_dpk_info {

	boolean	is_dpk_enable;
	boolean	is_dpk_pwr_on;
	boolean	is_dpk_by_channel;
	boolean is_tssi_mode;
	boolean is_reload;
	u16 dpk_path_ok;
	/*@BIT(15)~BIT(12) : 5G reserved, BIT(11)~BIT(8) 5G_S3~5G_S0*/
	/*@BIT(7)~BIT(4) : 2G reserved, BIT(3)~BIT(0) 2G_S3~2G_S0*/
	u8	thermal_dpk[4];					/*path*/	
	u8	thermal_dpk_avg[4][AVG_THERMAL_NUM_DPK];	/*path*/
	u8	pre_pwsf[4];	
	u8	thermal_dpk_avg_index;
	u32	gnt_control;
	u32	gnt_value;
	u8	dpk_ch;
	u8	dpk_band;
	u8	dpk_bw;

#if (RTL8822C_SUPPORT == 1 || RTL8812F_SUPPORT == 1)
	u8	result[2];			/*path*/
	u8	dpk_txagc[2];			/*path*/
	u32	coef[2][20];			/*path/MDPD coefficient*/
	u16	dpk_gs[2];			/*MDPD coef gs*/
	u8	thermal_dpk_delta[2];		/*path*/
#endif

#if (RTL8198F_SUPPORT == 1 || RTL8192F_SUPPORT == 1 || RTL8197F_SUPPORT == 1 ||\
     RTL8814B_SUPPORT == 1 || RTL8197G_SUPPORT == 1)
	/*2G DPK data*/
	u8 	dpk_result[4][3];		/*path/group*/
	u8 	pwsf_2g[4][3];			/*path/group*/	
	u32	lut_2g_even[4][3][64];		/*path/group/LUT data*/
	u32	lut_2g_odd[4][3][64];		/*path/group/LUT data*/
	s16	tmp_pas_i[32];			/*PAScan I data*/
	s16	tmp_pas_q[32];			/*PAScan Q data*/
	/*5G DPK data*/
	u8	dpk_5g_result[4][6];		/*path/group*/
	u8	pwsf_5g[4][6];			/*path/group*/
	u32	lut_5g[4][6][64];		/*path/group/LUT data*/
	u32	lut_2g[4][3][64];		/*path/group/LUT data*/
	/*8814B*/
	u8	rxbb[4];			/*path/group*/
	u8	txbb[4];			/*path/group*/
	u8	tx_gain;
#endif

#if (RTL8195B_SUPPORT == 1)
		/*2G DPK data*/
		u8	dpk_2g_result[1][3];		/*path/group*/
		u8	pwsf_2g[1][3];			/*path/group*/
		u32	lut_2g_even[1][3][16];		/*path/group/LUT data*/
		u32	lut_2g_odd[1][3][16];		/*path/group/LUT data*/
		/*5G DPK data*/
		u8	dpk_5g_result[1][13];		/*path/group*/
		u8	pwsf_5g[1][13];			/*path/group*/
		u32	lut_5g_even[1][13][16];		/*path/group/LUT data*/
		u32	lut_5g_odd[1][13][16];		/*path/group/LUT data*/
#endif

#if (RTL8721D_SUPPORT == 1)
		u8	dpk_txagc;
		/*2G DPK data*/
		u8	dpk_2g_result[1][3];		/*path/group*/
		u8	pwsf_2g[1][3];			/*path/group*/
		u32	lut_2g_even[1][3][16];		/*path/group/LUT data*/
		u32	lut_2g_odd[1][3][16];		/*path/group/LUT data*/
		/*5G DPK data*/
		u8	dpk_5g_result[1][6];		/*path/group*/
		u8	pwsf_5g[1][6];			/*path/group*/
		u32	lut_5g_even[1][6][16];		/*path/group/LUT data*/
		u32	lut_5g_odd[1][6][16];		/*path/group/LUT data*/
#endif

};

#endif /*__HALRF_DPK_H__*/

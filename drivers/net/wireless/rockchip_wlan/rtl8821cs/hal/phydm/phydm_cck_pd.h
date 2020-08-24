/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __PHYDM_CCK_PD_H__
#define __PHYDM_CCK_PD_H__

/* 2019.05.09 Modify the return criterion of supportability of CCK_PD*/
#define CCK_PD_VERSION "3.5"

/*@
 * 1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */
#define CCK_FA_MA_RESET 0xffffffff

#define INVALID_CS_RATIO_0 0x1b /* @ only for type4 ICs*/
#define INVALID_CS_RATIO_1 0x1d /* @ only for type4 ICs*/
#define MAXVALID_CS_RATIO 0x1f
/*@Run time flag of CCK_PD HW type*/
#define CCK_PD_IC_TYPE1 (ODM_RTL8188E | ODM_RTL8812 | ODM_RTL8821 |\
			ODM_RTL8192E | ODM_RTL8723B | ODM_RTL8814A |\
			ODM_RTL8881A | ODM_RTL8822B | ODM_RTL8703B |\
			ODM_RTL8195A | ODM_RTL8188F)

#define CCK_PD_IC_TYPE2 (ODM_RTL8197F | ODM_RTL8821C | ODM_RTL8723D |\
			ODM_RTL8710B | ODM_RTL8195B) /*extend 0xaaa*/

#define CCK_PD_IC_TYPE3 (ODM_RTL8192F | ODM_RTL8721D | ODM_RTL8710C)
/*@extend for different bw & path*/

#define CCK_PD_IC_TYPE4 ODM_IC_JGR3_SERIES /*@extend for different bw & path*/

/*@Compile time flag of CCK_PD HW type*/
#if (RTL8188E_SUPPORT || RTL8812A_SUPPORT || RTL8821A_SUPPORT ||\
	RTL8192E_SUPPORT || RTL8723B_SUPPORT || RTL8814A_SUPPORT ||\
	RTL8881A_SUPPORT || RTL8822B_SUPPORT || RTL8703B_SUPPORT ||\
	RTL8195A_SUPPORT || RTL8188F_SUPPORT)
	#define PHYDM_COMPILE_CCKPD_TYPE1 /*@only 0xa0a*/
#endif

#if (RTL8197F_SUPPORT || RTL8821C_SUPPORT || RTL8723D_SUPPORT ||\
	RTL8710B_SUPPORT || RTL8195B_SUPPORT)
	#define PHYDM_COMPILE_CCKPD_TYPE2 /*@extend 0xaaa*/
#endif

#if (RTL8192F_SUPPORT || RTL8721D_SUPPORT || RTL8710C_SUPPORT)
	#define PHYDM_COMPILE_CCKPD_TYPE3 /*@extend for different & path*/
#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	#define PHYDM_COMPILE_CCKPD_TYPE4 /*@extend for different bw & path*/
#endif
/*@
 * 1 ============================================================
 * 1  enumeration
 * 1 ============================================================
 */
enum cckpd_lv {
	CCK_PD_LV_INIT = 0xff,
	CCK_PD_LV_0 = 0,
	CCK_PD_LV_1 = 1,
	CCK_PD_LV_2 = 2,
	CCK_PD_LV_3 = 3,
	CCK_PD_LV_4 = 4,
	CCK_PD_LV_MAX = 5
};

enum cckpd_mode {
	CCK_BW20_1R = 0,
	CCK_BW20_2R = 1,
	CCK_BW20_3R = 2,
	CCK_BW20_4R = 3,
	CCK_BW40_1R = 4,
	CCK_BW40_2R = 5,
	CCK_BW40_3R = 6,
	CCK_BW40_4R = 7
};

/*@
 * 1 ============================================================
 * 1  structure
 * 1 ============================================================
 */

#ifdef PHYDM_SUPPORT_CCKPD
struct phydm_cckpd_struct {
	u8		cckpd_hw_type;
	u8		cur_cck_cca_thres; /*@current cck_pd value 0xa0a*/
	u32		cck_fa_ma;
	u32		rvrt_val; /*all rvrt_val for pause API must set to u32*/
	u8		pause_lv;
	u8		cck_n_rx;
	enum channel_width cck_bw;
	enum cckpd_lv	cck_pd_lv;
	#ifdef PHYDM_COMPILE_CCKPD_TYPE2
	u8		cck_cca_th_aaa; /*@current cs_ratio value 0xaaa*/
	u8		aaa_default;	/*@Init cs_ratio value - 0xaaa*/
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE3
	/*Default value*/
	u8		cck_pd_20m_1r;
	u8		cck_pd_20m_2r;
	u8		cck_pd_40m_1r;
	u8		cck_pd_40m_2r;
	u8		cck_cs_ratio_20m_1r;
	u8		cck_cs_ratio_20m_2r;
	u8		cck_cs_ratio_40m_1r;
	u8		cck_cs_ratio_40m_2r;
	/*Current value*/
	u8		cur_cck_pd_20m_1r;
	u8		cur_cck_pd_20m_2r;
	u8		cur_cck_pd_40m_1r;
	u8		cur_cck_pd_40m_2r;
	u8		cur_cck_cs_ratio_20m_1r;
	u8		cur_cck_cs_ratio_20m_2r;
	u8		cur_cck_cs_ratio_40m_1r;
	u8		cur_cck_cs_ratio_40m_2r;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE4
	/*@[bw][nrx][0:PD/1:CS][lv]*/
	u8		cckpd_jgr3[2][4][2][CCK_PD_LV_MAX];
	#endif
};
#endif

/*@
 * 1 ============================================================
 * 1  function prototype
 * 1 ============================================================
 */
void phydm_set_cckpd_val(void *dm_void, u32 *val_buf, u8 val_len);

void phydm_cck_pd_th(void *dm_void);

void phydm_cck_pd_init(void *dm_void);
#endif

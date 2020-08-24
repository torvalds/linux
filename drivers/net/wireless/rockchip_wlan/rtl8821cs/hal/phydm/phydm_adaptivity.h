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

#ifndef __PHYDMADAPTIVITY_H__
#define __PHYDMADAPTIVITY_H__

#define ADAPTIVITY_VERSION "9.7.07" /*@20190321 changed by Kevin,
				     *add 8721D threshold l2h init
				     */
#define ADC_BACKOFF 12
#define EDCCA_TH_L2H_LB 48
#define TH_L2H_DIFF_IGI 8
#define EDCCA_HL_DIFF_NORMAL 8
#define IGI_2_DBM(igi) (igi - 110)
/*@ [PHYDM-337][Old IC] EDCCA TH = IGI + REG setting*/
#define ODM_IC_PWDB_EDCCA (ODM_RTL8188E | ODM_RTL8723B | ODM_RTL8192E |\
			   ODM_RTL8881A | ODM_RTL8821 | ODM_RTL8812)

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
	#define ADAPT_DC_BACKOFF 2
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	#define ADAPT_DC_BACKOFF 4
#elif (DM_ODM_SUPPORT_TYPE & ODM_IOT)
	#define ADAPT_DC_BACKOFF 0
#endif
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
enum phydm_regulation_type {
	REGULATION_FCC		= 0,
	REGULATION_MKK		= 1,
	REGULATION_ETSI		= 2,
	REGULATION_WW		= 3,
	MAX_REGULATION_NUM	= 4
};
#endif

enum phydm_edcca_mode {
	PHYDM_EDCCA_NORMAL_MODE = 0,
	PHYDM_EDCCA_ADAPT_MODE = 1
};

enum phydm_adapinfo {
	PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE = 0,
	PHYDM_ADAPINFO_TH_L2H_INI,
	PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF,
	PHYDM_ADAPINFO_AP_NUM_TH,
	PHYDM_ADAPINFO_DOMAIN_CODE_2G,
	PHYDM_ADAPINFO_DOMAIN_CODE_5G
};

enum phydm_mac_edcca_type {
	PHYDM_IGNORE_EDCCA		= 0,
	PHYDM_DONT_IGNORE_EDCCA		= 1
};

enum phydm_adaptivity_debug_mode {
	PHYDM_ADAPT_MSG			= 0,
	PHYDM_ADAPT_DEBUG		= 1,
	PHYDM_ADAPT_RESUME		= 2,
};

struct phydm_adaptivity_struct {
	boolean			mode_cvrt_en;
	s8			th_l2h_ini_backup;
	s8			th_edcca_hl_diff_backup;
	s8			igi_base;
	s8			h2l_lb;
	s8			l2h_lb;
	u8			ap_num_th;
	u8			l2h_dyn_min;
	u32			adaptivity_dbg_port; /*N:0x208, AC:0x209*/
	u8			debug_mode;
	u16			igi_up_bound_lmt_cnt;	/*@When igi_up_bound_lmt_cnt !=0, limit IGI upper bound to "adapt_igi_up"*/
	u16			igi_up_bound_lmt_val;	/*@max value of igi_up_bound_lmt_cnt*/
	boolean			igi_lmt_en;
	u8			adapt_igi_up;
	u32			rvrt_val[2]; /*@all rvrt_val for pause API must set to u32*/
	s8			th_l2h;
	s8			th_h2l;
	u8			regulation_2g;
	u8			regulation_5g;
};

#ifdef PHYDM_SUPPORT_ADAPTIVITY
void phydm_adaptivity_debug(void *dm_void, char input[][16], u32 *_used,
			    char *output, u32 *_out_len);

void phydm_set_edcca_val(void *dm_void, u32 *val_buf, u8 val_len);
#endif

void phydm_set_edcca_threshold_api(void *dm_void);

void phydm_adaptivity_info_init(void *dm_void, enum phydm_adapinfo cmn_info,
				u32 value);

void phydm_adaptivity_info_update(void *dm_void, enum phydm_adapinfo cmn_info,
				  u32 value);

void phydm_adaptivity_init(void *dm_void);

void phydm_adaptivity(void *dm_void);

#endif

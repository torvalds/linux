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

/*@************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#ifdef PHYDM_SUPPORT_CCKPD
#ifdef PHYDM_COMPILE_CCKPD_TYPE1
void phydm_write_cck_pd_type1(void *dm_void, u8 cca_th)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;

	PHYDM_DBG(dm, DBG_CCKPD, "[%s] cck_cca_th=((0x%x))\n",
		  __func__, cca_th);

	odm_write_1byte(dm, R_0xa0a, cca_th);
	cckpd_t->cur_cck_cca_thres = cca_th;
}

void phydm_set_cckpd_lv_type1(void *dm_void, enum cckpd_lv lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u8 pd_th = 0;

	PHYDM_DBG(dm, DBG_CCKPD, "%s ======>\n", __func__);
	PHYDM_DBG(dm, DBG_CCKPD, "lv: (%d) -> (%d)\n", cckpd_t->cck_pd_lv, lv);

	if (cckpd_t->cck_pd_lv == lv) {
		PHYDM_DBG(dm, DBG_CCKPD, "stay in lv=%d\n", lv);
		return;
	}

	cckpd_t->cck_pd_lv = lv;
	cckpd_t->cck_fa_ma = CCK_FA_MA_RESET;

	if (lv == CCK_PD_LV_4)
		pd_th = 0xed;
	else if (lv == CCK_PD_LV_3)
		pd_th = 0xdd;
	else if (lv == CCK_PD_LV_2)
		pd_th = 0xcd;
	else if (lv == CCK_PD_LV_1)
		pd_th = 0x83;
	else if (lv == CCK_PD_LV_0)
		pd_th = 0x40;

	phydm_write_cck_pd_type1(dm, pd_th);
}

void phydm_cckpd_type1(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	enum cckpd_lv lv = CCK_PD_LV_INIT;
	boolean is_update = true;

	if (dm->is_linked) {
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		if (dm->rssi_min > 60) {
			lv = CCK_PD_LV_3;
		} else if (dm->rssi_min > 35) {
			lv = CCK_PD_LV_2;
		} else if (dm->rssi_min > 20) {
			if (cckpd_t->cck_fa_ma > 500)
				lv = CCK_PD_LV_2;
			else if (cckpd_t->cck_fa_ma < 250)
				lv = CCK_PD_LV_1;
			else
				is_update = false;
		} else { /*RSSI < 20*/
			lv = CCK_PD_LV_1;
		}
	#else /*ODM_AP*/
		if (dig_t->cur_ig_value > 0x32)
			lv = CCK_PD_LV_4;
		else if (dig_t->cur_ig_value > 0x2a)
			lv = CCK_PD_LV_3;
		else if (dig_t->cur_ig_value > 0x24)
			lv = CCK_PD_LV_2;
		else
			lv = CCK_PD_LV_1;
	#endif
	} else {
		if (cckpd_t->cck_fa_ma > 1000)
			lv = CCK_PD_LV_1;
		else if (cckpd_t->cck_fa_ma < 500)
			lv = CCK_PD_LV_0;
		else
			is_update = false;
	}

	/*[Abnormal case] =================================================*/
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	/*@HP 22B LPS power consumption issue & [PCIE-1596]*/
	if (dm->hp_hw_id && dm->traffic_load == TRAFFIC_ULTRA_LOW) {
		lv = CCK_PD_LV_0;
		PHYDM_DBG(dm, DBG_CCKPD, "CCKPD Abnormal case1\n");
	} else if ((dm->p_advance_ota & PHYDM_ASUS_OTA_SETTING) &&
	    cckpd_t->cck_fa_ma > 200 && dm->rssi_min <= 20) {
		lv = CCK_PD_LV_1;
		cckpd_t->cck_pd_lv = lv;
		phydm_write_cck_pd_type1(dm, 0xc3); /*@for ASUS OTA test*/
		is_update = false;
		PHYDM_DBG(dm, DBG_CCKPD, "CCKPD Abnormal case2\n");
	}
	#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))
		#ifdef MCR_WIRELESS_EXTEND
		lv = CCK_PD_LV_2;
		cckpd_t->cck_pd_lv = lv;
		phydm_write_cck_pd_type1(dm, 0x43);
		is_update = false;
		PHYDM_DBG(dm, DBG_CCKPD, "CCKPD Abnormal case3\n");
		#endif
	#endif
	/*=================================================================*/

	if (is_update)
		phydm_set_cckpd_lv_type1(dm, lv);

	PHYDM_DBG(dm, DBG_CCKPD, "is_linked=%d, lv=%d, pd_th=0x%x\n\n",
		  dm->is_linked, cckpd_t->cck_pd_lv,
		  cckpd_t->cur_cck_cca_thres);
}
#endif /*#ifdef PHYDM_COMPILE_CCKPD_TYPE1*/

#ifdef PHYDM_COMPILE_CCKPD_TYPE2
void phydm_write_cck_pd_type2(void *dm_void, u8 cca_th, u8 cca_th_aaa)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;

	PHYDM_DBG(dm, DBG_CCKPD, "[%s] pd_th=0x%x, cs_ratio=0x%x\n",
		  __func__, cca_th, cca_th_aaa);

	odm_set_bb_reg(dm, R_0xa08, 0x3f0000, cca_th);
	odm_set_bb_reg(dm, R_0xaa8, 0x1f0000, cca_th_aaa);
	cckpd_t->cur_cck_cca_thres = cca_th;
	cckpd_t->cck_cca_th_aaa = cca_th_aaa;
}

void phydm_set_cckpd_lv_type2(void *dm_void, enum cckpd_lv lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u8 pd_th = 0, cs_ratio = 0, cs_2r_offset = 0;
	u8 cck_n_rx = 1;

	PHYDM_DBG(dm, DBG_CCKPD, "%s ======>\n", __func__);
	PHYDM_DBG(dm, DBG_CCKPD, "lv: (%d) -> (%d)\n", cckpd_t->cck_pd_lv, lv);

	/*@r_mrx & r_cca_mrc*/
	cck_n_rx = (odm_get_bb_reg(dm, R_0xa2c, BIT(18)) &&
		    odm_get_bb_reg(dm, R_0xa2c, BIT(22))) ? 2 : 1;

	if (cckpd_t->cck_pd_lv == lv && cckpd_t->cck_n_rx == cck_n_rx) {
		PHYDM_DBG(dm, DBG_CCKPD, "stay in lv=%d\n", lv);
		return;
	}

	cckpd_t->cck_n_rx = cck_n_rx;
	cckpd_t->cck_pd_lv = lv;
	cckpd_t->cck_fa_ma = CCK_FA_MA_RESET;

	if (lv == CCK_PD_LV_4) {
		cs_ratio = cckpd_t->aaa_default + 8;
		cs_2r_offset = 5;
		pd_th = 0xd;
	} else if (lv == CCK_PD_LV_3) {
		cs_ratio = cckpd_t->aaa_default + 6;
		cs_2r_offset = 4;
		pd_th = 0xd;
	} else if (lv == CCK_PD_LV_2) {
		cs_ratio = cckpd_t->aaa_default + 4;
		cs_2r_offset = 3;
		pd_th = 0xd;
	} else if (lv == CCK_PD_LV_1) {
		cs_ratio = cckpd_t->aaa_default + 2;
		cs_2r_offset = 1;
		pd_th = 0x7;
	} else if (lv == CCK_PD_LV_0) {
		cs_ratio = cckpd_t->aaa_default;
		cs_2r_offset = 0;
		pd_th = 0x3;
	}

	if (cckpd_t->cck_n_rx == 2) {
		if (cs_ratio >= cs_2r_offset)
			cs_ratio = cs_ratio - cs_2r_offset;
		else
			cs_ratio = 0;
	}
	phydm_write_cck_pd_type2(dm, pd_th, cs_ratio);
}

void phydm_cckpd_type2(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	enum cckpd_lv lv = CCK_PD_LV_INIT;
	u8 igi = dig_t->cur_ig_value;
	u8 rssi_min = dm->rssi_min;
	boolean is_update = true;

	PHYDM_DBG(dm, DBG_CCKPD, "%s ======>\n", __func__);

	if (dm->is_linked) {
		if (igi > 0x38 && rssi_min > 32) {
			lv = CCK_PD_LV_4;
		} else if (igi > 0x2a && rssi_min > 32) {
			lv = CCK_PD_LV_3;
		} else if (igi > 0x24 || (rssi_min > 24 && rssi_min <= 30)) {
			lv = CCK_PD_LV_2;
		} else if (igi <= 0x24 || rssi_min < 22) {
			if (cckpd_t->cck_fa_ma > 1000) {
				lv = CCK_PD_LV_1;
			} else if (cckpd_t->cck_fa_ma < 500) {
				lv = CCK_PD_LV_0;
			} else {
				is_update = false;
			}
		} else {
			is_update = false;
		}
	} else {
		if (cckpd_t->cck_fa_ma > 1000) {
			lv = CCK_PD_LV_1;
		} else if (cckpd_t->cck_fa_ma < 500) {
			lv = CCK_PD_LV_0;
		} else {
			is_update = false;
		}
	}

	/*[Abnormal case] =================================================*/
	#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	/*@21C Miracast lag issue & [PCIE-3298]*/
	if (dm->support_ic_type & ODM_RTL8821C && rssi_min > 60) {
		lv = CCK_PD_LV_4;
		cckpd_t->cck_pd_lv = lv;
		phydm_write_cck_pd_type2(dm, 0x1d, (cckpd_t->aaa_default + 8));
		is_update = false;
		PHYDM_DBG(dm, DBG_CCKPD, "CCKPD Abnormal case1\n");
	}
	#endif
	/*=================================================================*/

	if (is_update) {
		phydm_set_cckpd_lv_type2(dm, lv);
	}

	PHYDM_DBG(dm, DBG_CCKPD,
		  "is_linked=%d, lv=%d, n_rx=%d, cs_ratio=0x%x, pd_th=0x%x\n\n",
		  dm->is_linked, cckpd_t->cck_pd_lv, cckpd_t->cck_n_rx,
		  cckpd_t->cck_cca_th_aaa, cckpd_t->cur_cck_cca_thres);
}
#endif /*#ifdef PHYDM_COMPILE_CCKPD_TYPE2*/

#ifdef PHYDM_COMPILE_CCKPD_TYPE3
void phydm_write_cck_pd_type3(void *dm_void, u8 pd_th, u8 cs_ratio,
			      enum cckpd_mode mode)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;

	PHYDM_DBG(dm, DBG_CCKPD,
		  "[%s] mode=%d, pd_th=0x%x, cs_ratio=0x%x\n", __func__,
		  mode, pd_th, cs_ratio);

	switch (mode) {
	case CCK_BW20_1R: /*RFBW20_1R*/
	{
		cckpd_t->cur_cck_pd_20m_1r = pd_th;
		cckpd_t->cur_cck_cs_ratio_20m_1r = cs_ratio;
		odm_set_bb_reg(dm, R_0xac8, 0xff, pd_th);
		odm_set_bb_reg(dm, R_0xad0, 0x1f, cs_ratio);
	} break;
	case CCK_BW20_2R: /*RFBW20_2R*/
	{
		cckpd_t->cur_cck_pd_20m_2r = pd_th;
		cckpd_t->cur_cck_cs_ratio_20m_2r = cs_ratio;
		odm_set_bb_reg(dm, R_0xac8, 0xff00, pd_th);
		odm_set_bb_reg(dm, R_0xad0, 0x3e0, cs_ratio);
	} break;
	case CCK_BW40_1R: /*RFBW40_1R*/
	{
		cckpd_t->cur_cck_pd_40m_1r = pd_th;
		cckpd_t->cur_cck_cs_ratio_40m_1r = cs_ratio;
		odm_set_bb_reg(dm, R_0xacc, 0xff, pd_th);
		odm_set_bb_reg(dm, R_0xad0, 0x1f00000, cs_ratio);
	} break;
	case CCK_BW40_2R: /*RFBW40_2R*/
	{
		cckpd_t->cur_cck_pd_40m_2r = pd_th;
		cckpd_t->cur_cck_cs_ratio_40m_2r = cs_ratio;
		odm_set_bb_reg(dm, R_0xacc, 0xff00, pd_th);
		odm_set_bb_reg(dm, R_0xad0, 0x3e000000, cs_ratio);
	} break;

	default:
		/*@pr_debug("[%s] warning!\n", __func__);*/
		break;
	}
}

void phydm_set_cckpd_lv_type3(void *dm_void, enum cckpd_lv lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	enum cckpd_mode cck_mode = CCK_BW20_2R;
	enum channel_width cck_bw = CHANNEL_WIDTH_20;
	u8 cck_n_rx = 1;
	u8 pd_th;
	u8 cs_ratio;

	PHYDM_DBG(dm, DBG_CCKPD, "%s ======>\n", __func__);
	PHYDM_DBG(dm, DBG_CCKPD, "lv: (%d) -> (%d)\n", cckpd_t->cck_pd_lv, lv);

	/*[Check Nrx]*/
	cck_n_rx = (odm_get_bb_reg(dm, R_0xa2c, BIT(17))) ? 2 : 1;

	/*[Check BW]*/
	if (odm_get_bb_reg(dm, R_0x800, BIT(0)))
		cck_bw = CHANNEL_WIDTH_40;
	else
		cck_bw = CHANNEL_WIDTH_20;

	/*[Check LV]*/
	if (cckpd_t->cck_pd_lv == lv &&
	    cckpd_t->cck_n_rx == cck_n_rx &&
	    cckpd_t->cck_bw == cck_bw) {
		PHYDM_DBG(dm, DBG_CCKPD, "stay in lv=%d\n", lv);
		return;
	}

	cckpd_t->cck_bw = cck_bw;
	cckpd_t->cck_n_rx = cck_n_rx;
	cckpd_t->cck_pd_lv = lv;
	cckpd_t->cck_fa_ma = CCK_FA_MA_RESET;

	if (cck_n_rx == 2) {
		if (cck_bw == CHANNEL_WIDTH_20) {
			pd_th = cckpd_t->cck_pd_20m_2r;
			cs_ratio = cckpd_t->cck_cs_ratio_20m_2r;
			cck_mode = CCK_BW20_2R;
		} else {
			pd_th = cckpd_t->cck_pd_40m_2r;
			cs_ratio = cckpd_t->cck_cs_ratio_40m_2r;
			cck_mode = CCK_BW40_2R;
		}
	} else {
		if (cck_bw == CHANNEL_WIDTH_20) {
			pd_th = cckpd_t->cck_pd_20m_1r;
			cs_ratio = cckpd_t->cck_cs_ratio_20m_1r;
			cck_mode = CCK_BW20_1R;
		} else {
			pd_th = cckpd_t->cck_pd_40m_1r;
			cs_ratio = cckpd_t->cck_cs_ratio_40m_1r;
			cck_mode = CCK_BW40_1R;
		}
	}

	if (lv == CCK_PD_LV_4) {
		if (cck_n_rx == 2) {
			pd_th += 4;
			cs_ratio += 2;
		} else {
			pd_th += 4;
			cs_ratio += 3;
		}
	} else if (lv == CCK_PD_LV_3) {
		if (cck_n_rx == 2) {
			pd_th += 3;
			cs_ratio += 1;
		} else {
			pd_th += 3;
			cs_ratio += 2;
		}
	} else if (lv == CCK_PD_LV_2) {
		pd_th += 2;
		cs_ratio += 1;
	} else if (lv == CCK_PD_LV_1) {
		pd_th += 1;
		cs_ratio += 1;
	}
	#if 0
	else if (lv == CCK_PD_LV_0) {
		pd_th += 0;
		cs_ratio += 0;
	}
	#endif

	phydm_write_cck_pd_type3(dm, pd_th, cs_ratio, cck_mode);
}

void phydm_cckpd_type3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	enum cckpd_lv lv = CCK_PD_LV_INIT;
	u8 igi = dm->dm_dig_table.cur_ig_value;
	boolean is_update = true;
	u8 pd_th = 0;
	u8 cs_ratio = 0;

	PHYDM_DBG(dm, DBG_CCKPD, "%s ======>\n", __func__);

	if (dm->is_linked) {
		if (igi > 0x38 && dm->rssi_min > 32) {
			lv = CCK_PD_LV_4;
		} else if ((igi > 0x2a) && (dm->rssi_min > 32)) {
			lv = CCK_PD_LV_3;
		} else if ((igi > 0x24) ||
			   (dm->rssi_min > 24 && dm->rssi_min <= 30)) {
			lv = CCK_PD_LV_2;
		} else if ((igi <= 0x24) || (dm->rssi_min < 22)) {
			if (cckpd_t->cck_fa_ma > 1000)
				lv = CCK_PD_LV_1;
			else if (cckpd_t->cck_fa_ma < 500)
				lv = CCK_PD_LV_0;
			else
				is_update = false;
		}
	} else {
		if (cckpd_t->cck_fa_ma > 1000)
			lv = CCK_PD_LV_1;
		else if (cckpd_t->cck_fa_ma < 500)
			lv = CCK_PD_LV_0;
		else
			is_update = false;
	}

	if (is_update)
		phydm_set_cckpd_lv_type3(dm, lv);

	if (cckpd_t->cck_n_rx == 2) {
		if (cckpd_t->cck_bw == CHANNEL_WIDTH_20) {
			pd_th = cckpd_t->cur_cck_pd_20m_2r;
			cs_ratio = cckpd_t->cur_cck_cs_ratio_20m_2r;
		} else {
			pd_th = cckpd_t->cur_cck_pd_40m_2r;
			cs_ratio = cckpd_t->cur_cck_cs_ratio_40m_2r;
		}
	} else {
		if (cckpd_t->cck_bw == CHANNEL_WIDTH_20) {
			pd_th = cckpd_t->cur_cck_pd_20m_1r;
			cs_ratio = cckpd_t->cur_cck_cs_ratio_20m_1r;
		} else {
			pd_th = cckpd_t->cur_cck_pd_40m_1r;
			cs_ratio = cckpd_t->cur_cck_cs_ratio_40m_1r;
		}
	}
	PHYDM_DBG(dm, DBG_CCKPD,
		  "[%dR][%dM] is_linked=%d, lv=%d, cs_ratio=0x%x, pd_th=0x%x\n\n",
		  cckpd_t->cck_n_rx, 20 << cckpd_t->cck_bw, dm->is_linked,
		  cckpd_t->cck_pd_lv, cs_ratio, pd_th);
}

void phydm_cck_pd_init_type3(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u32 reg_tmp = 0;

	/*Get Default value*/
	cckpd_t->cck_pd_20m_1r = (u8)odm_get_bb_reg(dm, R_0xac8, 0xff);
	cckpd_t->cck_pd_20m_2r = (u8)odm_get_bb_reg(dm, R_0xac8, 0xff00);
	cckpd_t->cck_pd_40m_1r = (u8)odm_get_bb_reg(dm, R_0xacc, 0xff);
	cckpd_t->cck_pd_40m_2r = (u8)odm_get_bb_reg(dm, R_0xacc, 0xff00);

	reg_tmp = odm_get_bb_reg(dm, R_0xad0, MASKDWORD);
	cckpd_t->cck_cs_ratio_20m_1r = (u8)(reg_tmp & 0x1f);
	cckpd_t->cck_cs_ratio_20m_2r = (u8)((reg_tmp & 0x3e0) >> 5);
	cckpd_t->cck_cs_ratio_40m_1r = (u8)((reg_tmp & 0x1f00000) >> 20);
	cckpd_t->cck_cs_ratio_40m_2r = (u8)((reg_tmp & 0x3e000000) >> 25);

	phydm_set_cckpd_lv_type3(dm, CCK_PD_LV_0);
}
#endif /*#ifdef PHYDM_COMPILE_CCKPD_TYPE3*/

#ifdef PHYDM_COMPILE_CCKPD_TYPE4
void phydm_write_cck_pd_type4(void *dm_void, enum cckpd_lv lv,
			      enum cckpd_mode mode)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u32 val = 0;

	PHYDM_DBG(dm, DBG_CCKPD, "write CCK CCA parameters(CS_ratio & PD)\n");
	switch (mode) {
	case CCK_BW20_1R: /*RFBW20_1R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[0][0][0][lv];
		odm_set_bb_reg(dm, R_0x1ac8, 0xff, val);
		val = cckpd_t->cck_pd_table_jgr3[0][0][1][lv];
		odm_set_bb_reg(dm, R_0x1ad0, 0x1f, val);
	} break;
	case CCK_BW40_1R: /*RFBW40_1R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[1][0][0][lv];
		odm_set_bb_reg(dm, R_0x1acc, 0xff, val);
		val = cckpd_t->cck_pd_table_jgr3[1][0][1][lv];
		odm_set_bb_reg(dm, R_0x1ad0, 0x01F00000, val);
	} break;
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	case CCK_BW20_2R: /*RFBW20_2R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[0][1][0][lv];
		odm_set_bb_reg(dm, R_0x1ac8, 0xff00, val);
		val = cckpd_t->cck_pd_table_jgr3[0][1][1][lv];
		odm_set_bb_reg(dm, R_0x1ad0, 0x3e0, val);
	} break;
	case CCK_BW40_2R: /*RFBW40_2R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[1][1][0][lv];
		odm_set_bb_reg(dm, R_0x1acc, 0xff00, val);
		val = cckpd_t->cck_pd_table_jgr3[1][1][1][lv];
		odm_set_bb_reg(dm, R_0x1ad0, 0x3E000000, val);
	} break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	case CCK_BW20_3R: /*RFBW20_3R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[0][2][0][lv];
		odm_set_bb_reg(dm, R_0x1ac8, 0xff0000, val);
		val = cckpd_t->cck_pd_table_jgr3[0][2][1][lv];
		odm_set_bb_reg(dm, R_0x1ad0, 0x7c00, val);
	} break;
	case CCK_BW40_3R: /*RFBW40_3R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[1][2][0][lv];
		odm_set_bb_reg(dm, R_0x1acc, 0xff0000, val);
		val = cckpd_t->cck_pd_table_jgr3[1][2][1][lv] & 0x3;
		odm_set_bb_reg(dm, R_0x1ad0, 0xC0000000, val);
		val = (cckpd_t->cck_pd_table_jgr3[1][2][1][lv] & 0x1c) >> 2;
		odm_set_bb_reg(dm, R_0x1ad4, 0x7, val);
	} break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	case CCK_BW20_4R: /*RFBW20_4R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[0][3][0][lv];
		odm_set_bb_reg(dm, R_0x1ac8, 0xff000000, val);
		val = cckpd_t->cck_pd_table_jgr3[0][3][1][lv];
		odm_set_bb_reg(dm, R_0x1ad0, 0xF8000, val);
	} break;
	case CCK_BW40_4R: /*RFBW40_4R*/
	{
		val = cckpd_t->cck_pd_table_jgr3[1][3][0][lv];
		odm_set_bb_reg(dm, R_0x1acc, 0xff000000, val);
		val = cckpd_t->cck_pd_table_jgr3[1][3][1][lv];
		odm_set_bb_reg(dm, R_0x1ad4, 0xf8, val);
	} break;
	#endif
	default:
		/*@pr_debug("[%s] warning!\n", __func__);*/
		break;
	}
}

void phydm_set_cck_pd_lv_type4(void *dm_void, enum cckpd_lv lv)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	enum cckpd_mode cck_mode = CCK_BW20_2R;
	enum channel_width cck_bw = CHANNEL_WIDTH_20;
	u8 cck_n_rx = 0;
	u32 val = 0;
	/*u32 val_dbg = 0;*/

	PHYDM_DBG(dm, DBG_CCKPD, "%s ======>\n", __func__);
	PHYDM_DBG(dm, DBG_CCKPD, "lv: (%d) -> (%d)\n", cckpd_t->cck_pd_lv, lv);

	/*[Check Nrx]*/
	cck_n_rx = (u8)odm_get_bb_reg(dm, R_0x1a2c, 0x60000) + 1;

	/*[Check BW]*/
	val = odm_get_bb_reg(dm, R_0x9b0, 0xc);
	if (val == 0)
		cck_bw = CHANNEL_WIDTH_20;
	else if (val == 1)
		cck_bw = CHANNEL_WIDTH_40;
	else
		cck_bw = CHANNEL_WIDTH_80;

	/*[Check LV]*/
	if (cckpd_t->cck_pd_lv == lv &&
	    cckpd_t->cck_n_rx == cck_n_rx &&
	    cckpd_t->cck_bw == cck_bw) {
		PHYDM_DBG(dm, DBG_CCKPD, "stay in lv=%d\n", lv);
		return;
	}

	cckpd_t->cck_bw = cck_bw;
	cckpd_t->cck_n_rx = cck_n_rx;
	cckpd_t->cck_pd_lv = lv;
	cckpd_t->cck_fa_ma = CCK_FA_MA_RESET;

	switch (cck_n_rx) {
	case 1: /*1R*/
	{
		if (cck_bw == CHANNEL_WIDTH_20)
			cck_mode = CCK_BW20_1R;
		else if (cck_bw == CHANNEL_WIDTH_40)
			cck_mode = CCK_BW40_1R;
	} break;
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	case 2: /*2R*/
	{
		if (cck_bw == CHANNEL_WIDTH_20)
			cck_mode = CCK_BW20_2R;
		else if (cck_bw == CHANNEL_WIDTH_40)
			cck_mode = CCK_BW40_2R;
	} break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	case 3: /*3R*/
	{
		if (cck_bw == CHANNEL_WIDTH_20)
			cck_mode = CCK_BW20_3R;
		else if (cck_bw == CHANNEL_WIDTH_40)
			cck_mode = CCK_BW40_3R;
	} break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	case 4: /*4R*/
	{
		if (cck_bw == CHANNEL_WIDTH_20)
			cck_mode = CCK_BW20_4R;
		else if (cck_bw == CHANNEL_WIDTH_40)
			cck_mode = CCK_BW40_4R;
	} break;
	#endif
	default:
		/*@pr_debug("[%s] warning!\n", __func__);*/
		break;
	}
phydm_write_cck_pd_type4(dm, lv, cck_mode);
}

void phydm_read_cckpd_para_type4(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u8 bw = 0; /*r_RX_RF_BW*/
	u8 n_rx = 0;
	u8 curr_cck_pd_t[2][4][2];
	u32 reg0 = 0;
	u32 reg1 = 0;
	u32 reg2 = 0;
	u32 reg3 = 0;

	bw = (u8)odm_get_bb_reg(dm, R_0x9b0, 0xc);
	n_rx = (u8)odm_get_bb_reg(dm, R_0x1a2c, 0x60000) + 1;

	reg0 = odm_get_bb_reg(dm, R_0x1ac8, MASKDWORD);
	reg1 = odm_get_bb_reg(dm, R_0x1acc, MASKDWORD);
	reg2 = odm_get_bb_reg(dm, R_0x1ad0, MASKDWORD);
	reg3 = odm_get_bb_reg(dm, R_0x1ad4, MASKDWORD);
	curr_cck_pd_t[0][0][0] = (u8)(reg0 & 0x000000ff);
	curr_cck_pd_t[1][0][0] = (u8)(reg1 & 0x000000ff);
	curr_cck_pd_t[0][0][1] = (u8)(reg2 & 0x0000001f);
	curr_cck_pd_t[1][0][1] = (u8)((reg2 & 0x01f00000) >> 20);
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_2SS) {
		curr_cck_pd_t[0][1][0] = (u8)((reg0 & 0x0000ff00) >> 8);
		curr_cck_pd_t[1][1][0] = (u8)((reg1 & 0x0000ff00) >> 8);
		curr_cck_pd_t[0][1][1] = (u8)((reg2 & 0x000003E0) >> 5);
		curr_cck_pd_t[1][1][1] = (u8)((reg2 & 0x3E000000) >> 25);
	}
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_3SS) {
		curr_cck_pd_t[0][2][0] = (u8)((reg0 & 0x00ff0000) >> 16);
		curr_cck_pd_t[1][2][0] = (u8)((reg1 & 0x00ff0000) >> 16);
		curr_cck_pd_t[0][2][1] = (u8)((reg2 & 0x00007C00) >> 10);
		curr_cck_pd_t[1][2][1] = (u8)((reg2 & 0xC0000000) >> 30) |
					 (u8)((reg3 & 0x00000007) << 3);
	}
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {
		curr_cck_pd_t[0][3][0] = (u8)((reg0 & 0xff000000) >> 24);
		curr_cck_pd_t[1][3][0] = (u8)((reg1 & 0xff000000) >> 24);
		curr_cck_pd_t[0][3][1] = (u8)((reg2 & 0x000F8000) >> 15);
		curr_cck_pd_t[1][3][1] = (u8)((reg3 & 0x000000F8) >> 3);
	}
	#endif

	PHYDM_DBG(dm, DBG_CCKPD, "bw=%dM, Nrx=%d\n", 20 << bw, n_rx);
	PHYDM_DBG(dm, DBG_CCKPD, "lv=%d, readback CS_th=0x%x, PD th=0x%x\n",
		  cckpd_t->cck_pd_lv,
		  curr_cck_pd_t[bw][n_rx - 1][1],
		  curr_cck_pd_t[bw][n_rx - 1][0]);
}

void phydm_cckpd_type4(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u8 igi = dm->dm_dig_table.cur_ig_value;
	enum cckpd_lv lv = 0;
	boolean is_update = true;

	PHYDM_DBG(dm, DBG_CCKPD, "%s ======>\n", __func__);

	if (dm->is_linked) {
		PHYDM_DBG(dm, DBG_CCKPD, "Linked!!!\n");
		if (igi > 0x38 && dm->rssi_min > 32) {
			lv = CCK_PD_LV_4;
			PHYDM_DBG(dm, DBG_CCKPD, "Order 1\n");
		} else if (igi > 0x2a && dm->rssi_min > 32) {
			lv = CCK_PD_LV_3;
			PHYDM_DBG(dm, DBG_CCKPD, "Order 2\n");
		} else if (igi > 0x24 || dm->rssi_min > 24) {
			lv = CCK_PD_LV_2;
			PHYDM_DBG(dm, DBG_CCKPD, "Order 3\n");
		} else {
			if (cckpd_t->cck_fa_ma > 1000) {
				lv = CCK_PD_LV_1;
				PHYDM_DBG(dm, DBG_CCKPD, "Order 4-1\n");
			} else if (cckpd_t->cck_fa_ma < 500) {
				lv = CCK_PD_LV_0;
				PHYDM_DBG(dm, DBG_CCKPD, "Order 4-2\n");
			} else {
				is_update = false;
				PHYDM_DBG(dm, DBG_CCKPD, "Order 4-3\n");
			}
		}
	} else {
		PHYDM_DBG(dm, DBG_CCKPD, "UnLinked!!!\n");
		if (cckpd_t->cck_fa_ma > 1000) {
			lv = CCK_PD_LV_1;
			PHYDM_DBG(dm, DBG_CCKPD, "Order 1\n");
		} else if (cckpd_t->cck_fa_ma < 500) {
			lv = CCK_PD_LV_0;
			PHYDM_DBG(dm, DBG_CCKPD, "Order 2\n");
		} else {
			is_update = false;
			PHYDM_DBG(dm, DBG_CCKPD, "Order 3\n");
		}
	}

	if (is_update) {
		phydm_set_cck_pd_lv_type4(dm, lv);

		PHYDM_DBG(dm, DBG_CCKPD, "setting CS_th = 0x%x, PD th = 0x%x\n",
			  cckpd_t->cck_pd_table_jgr3[cckpd_t->cck_bw]
			  [cckpd_t->cck_n_rx - 1][1][lv],
			  cckpd_t->cck_pd_table_jgr3[cckpd_t->cck_bw]
			  [cckpd_t->cck_n_rx - 1][0][lv]);
	}

	phydm_read_cckpd_para_type4(dm);
}

void phydm_cck_pd_init_type4(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u32 reg0 = 0;
	u32 reg1 = 0;
	u32 reg2 = 0;
	u32 reg3 = 0;
	u8 pd_step = 0;
	u8 cck_bw = 0; /*r_RX_RF_BW*/
	u8 cck_n_rx = 0;
	u8 val = 0;
	u8 i = 0;

	PHYDM_DBG(dm, DBG_CCKPD, "[%s]======>\n", __func__);

	#if 0
	/*@
	 *cckpd_t[0][0][0][0] =  1ac8[7:0]	r_PD_lim_RFBW20_1R
	 *cckpd_t[0][1][0][0] =  1ac8[15:8]	r_PD_lim_RFBW20_2R
	 *cckpd_t[0][2][0][0] =  1ac8[23:16]	r_PD_lim_RFBW20_3R
	 *cckpd_t[0][3][0][0] =  1ac8[31:24]	r_PD_lim_RFBW20_4R
	 *cckpd_t[1][0][0][0] =  1acc[7:0]	r_PD_lim_RFBW40_1R
	 *cckpd_t[1][1][0][0] =  1acc[15:8]	r_PD_lim_RFBW40_2R
	 *cckpd_t[1][2][0][0] =  1acc[23:16]	r_PD_lim_RFBW40_3R
	 *cckpd_t[1][3][0][0] =  1acc[31:24]	r_PD_lim_RFBW40_4R
	 *
	 *
	 *cckpd_t[0][0][1][0] =  1ad0[4:0]	r_CS_ratio_RFBW20_1R[4:0]
	 *cckpd_t[0][1][1][0] =  1ad0[9:5]	r_CS_ratio_RFBW20_2R[4:0]
	 *cckpd_t[0][2][1][0] =  1ad0[14:10]	r_CS_ratio_RFBW20_3R[4:0]
	 *cckpd_t[0][3][1][0] =  1ad0[19:15]	r_CS_ratio_RFBW20_4R[4:0]
	 *cckpd_t[1][0][1][0] =  1ad0[24:20]	r_CS_ratio_RFBW40_1R[4:0]
	 *cckpd_t[1][1][1][0] =  1ad0[29:25]	r_CS_ratio_RFBW40_2R[4:0]
	 *cckpd_t[1][2][1][0] =  1ad0[31:30]	r_CS_ratio_RFBW40_3R[1:0]
	 *			  1ad4[2:0]	r_CS_ratio_RFBW40_3R[4:2]
	 *cckpd_t[1][3][1][0] =  1ad4[7:3]	r_CS_ratio_RFBW40_4R[4:0]
	 */
	#endif
	/*[Check Nrx]*/
	cck_n_rx = (u8)odm_get_bb_reg(dm, R_0x1a2c, 0x60000) + 1;

	/*[Check BW]*/
	val = (u8)odm_get_bb_reg(dm, R_0x9b0, 0xc);
	if (val == 0)
		cck_bw = CHANNEL_WIDTH_20;
	else if (val == 1)
		cck_bw = CHANNEL_WIDTH_40;
	else
		cck_bw = CHANNEL_WIDTH_80;

	cckpd_t->cck_bw = cck_bw;
	cckpd_t->cck_n_rx = cck_n_rx;
	reg0 = odm_get_bb_reg(dm, R_0x1ac8, MASKDWORD);
	reg1 = odm_get_bb_reg(dm, R_0x1acc, MASKDWORD);
	reg2 = odm_get_bb_reg(dm, R_0x1ad0, MASKDWORD);
	reg3 = odm_get_bb_reg(dm, R_0x1ad4, MASKDWORD);

	for (i = 0 ; i < CCK_PD_LV_MAX ; i++) {
		pd_step = i * 2;

		val = (u8)(reg0 & 0x000000ff) + pd_step;
		PHYDM_DBG(dm, DBG_CCKPD, "lvl %d val = %x\n\n", i, val);
		cckpd_t->cck_pd_table_jgr3[0][0][0][i] = val;

		val = (u8)(reg1 & 0x000000ff) + pd_step;
		cckpd_t->cck_pd_table_jgr3[1][0][0][i] = val;

		val = (u8)(reg2 & 0x0000001F) + pd_step;
		cckpd_t->cck_pd_table_jgr3[0][0][1][i] = val;

		val = (u8)((reg2 & 0x01F00000) >> 20) + pd_step;
		cckpd_t->cck_pd_table_jgr3[1][0][1][i] = val;

		#ifdef PHYDM_COMPILE_ABOVE_2SS
		if (dm->support_ic_type & PHYDM_IC_ABOVE_2SS) {
			val = (u8)((reg0 & 0x0000ff00) >> 8) + pd_step;
			cckpd_t->cck_pd_table_jgr3[0][1][0][i] = val;

			val = (u8)((reg1 & 0x0000ff00) >> 8) + pd_step;
			cckpd_t->cck_pd_table_jgr3[1][1][0][i] = val;

			val = (u8)((reg2 & 0x000003E0) >> 5) + pd_step;
			cckpd_t->cck_pd_table_jgr3[0][1][1][i] = val;

			val = (u8)((reg2 & 0x3E000000) >> 25) + pd_step;
			cckpd_t->cck_pd_table_jgr3[1][1][1][i] = val;
		}
		#endif

		#ifdef PHYDM_COMPILE_ABOVE_3SS
		if (dm->support_ic_type & PHYDM_IC_ABOVE_3SS) {
			val = (u8)((reg0 & 0x00ff0000) >> 16) + pd_step;
			cckpd_t->cck_pd_table_jgr3[0][2][0][i] = val;

			val = (u8)((reg1 & 0x00ff0000) >> 16) + pd_step;
			cckpd_t->cck_pd_table_jgr3[1][2][0][i] = val;
			val = (u8)((reg2 & 0x00007C00) >> 10) + pd_step;
			cckpd_t->cck_pd_table_jgr3[0][2][1][i] = val;
			val = (u8)(((reg2 & 0xC0000000) >> 30) |
			      ((reg3 & 0x7) << 3)) + pd_step;
			cckpd_t->cck_pd_table_jgr3[1][2][1][i] = val;
		}
		#endif

		#ifdef PHYDM_COMPILE_ABOVE_4SS
		if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {
			val = (u8)((reg0 & 0xff000000) >> 24) + pd_step;
			cckpd_t->cck_pd_table_jgr3[0][3][0][i] = val;

			val = (u8)((reg1 & 0xff000000) >> 24) + pd_step;
			cckpd_t->cck_pd_table_jgr3[1][3][0][i] = val;

			val = (u8)((reg2 & 0x000F8000) >> 15) + pd_step;
			cckpd_t->cck_pd_table_jgr3[0][3][1][i] = val;

			val = (u8)((reg3 & 0x000000F8) >> 3) + pd_step;
			cckpd_t->cck_pd_table_jgr3[1][3][1][i] = val;
		}
		#endif
	}
}
#endif /*#ifdef PHYDM_COMPILE_CCKPD_TYPE4*/

void phydm_set_cckpd_val(void *dm_void, u32 *val_buf, u8 val_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	enum cckpd_lv lv;

	if (val_len != 1) {
		PHYDM_DBG(dm, ODM_COMP_API, "[Error][CCKPD]Need val_len=1\n");
		return;
	}

	lv = (enum cckpd_lv)val_buf[0];

	if (lv > CCK_PD_LV_4) {
		pr_debug("[%s] warning! lv=%d\n", __func__, lv);
		return;
	}

	switch (cckpd_t->cckpd_hw_type) {
	#ifdef PHYDM_COMPILE_CCKPD_TYPE1
	case 1:
		phydm_set_cckpd_lv_type1(dm, lv);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE2
	case 2:
		phydm_set_cckpd_lv_type2(dm, lv);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE3
	case 3:
		phydm_set_cckpd_lv_type3(dm, lv);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE4
	case 4:
		phydm_set_cck_pd_lv_type4(dm, lv);
		break;
	#endif
	default:
		pr_debug("[%s]warning\n", __func__);
		break;
	}
}

boolean
phydm_stop_cck_pd_th(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!(dm->support_ability & (ODM_BB_CCK_PD | ODM_BB_FA_CNT))) {
		PHYDM_DBG(dm, DBG_CCKPD, "Not Support\n");
		return true;
	}

	if (dm->pause_ability & ODM_BB_CCK_PD) {
		PHYDM_DBG(dm, DBG_CCKPD, "Return: Pause CCKPD in LV=%d\n",
			  dm->pause_lv_table.lv_cckpd);
		return true;
	}

	if (dm->is_linked && (*dm->channel > 36)) {
		PHYDM_DBG(dm, DBG_CCKPD, "Return: 5G CH=%d\n", *dm->channel);
		return true;
	}
	return false;
}

void phydm_cck_pd_th(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fa_struct *fa_t = &dm->false_alm_cnt;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;
	u32 cck_fa = fa_t->cnt_cck_fail;
	#ifdef PHYDM_TDMA_DIG_SUPPORT
	struct phydm_fa_acc_struct *fa_acc_t = &dm->false_alm_cnt_acc;
	#endif

	PHYDM_DBG(dm, DBG_CCKPD, "[%s] ======>\n", __func__);

	if (phydm_stop_cck_pd_th(dm))
		return;

	#ifdef PHYDM_TDMA_DIG_SUPPORT
	if (dm->original_dig_restore)
		cck_fa = fa_t->cnt_cck_fail;
	else
		cck_fa = fa_acc_t->cnt_cck_fail_1sec;
	#endif

	if (cckpd_t->cck_fa_ma == CCK_FA_MA_RESET)
		cckpd_t->cck_fa_ma = cck_fa;
	else
		cckpd_t->cck_fa_ma = (cckpd_t->cck_fa_ma * 3 + cck_fa) >> 2;

	PHYDM_DBG(dm, DBG_CCKPD,
		  "IGI=0x%x, rssi_min=%d, cck_fa=%d, cck_fa_ma=%d\n",
		  dm->dm_dig_table.cur_ig_value, dm->rssi_min,
		  cck_fa, cckpd_t->cck_fa_ma);

	switch (cckpd_t->cckpd_hw_type) {
	#ifdef PHYDM_COMPILE_CCKPD_TYPE1
	case 1:
		phydm_cckpd_type1(dm);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE2
	case 2:
		phydm_cckpd_type2(dm);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE3
	case 3:
		phydm_cckpd_type3(dm);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE4
	case 4:
		phydm_cckpd_type4(dm);
		break;
	#endif
	default:
		pr_debug("[%s]warning\n", __func__);
		break;
	}
}

void phydm_cck_pd_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cckpd_struct *cckpd_t = &dm->dm_cckpd_table;

	if (dm->support_ic_type & CCK_PD_IC_TYPE1)
		cckpd_t->cckpd_hw_type = 1;
	else if (dm->support_ic_type & CCK_PD_IC_TYPE2)
		cckpd_t->cckpd_hw_type = 2;
	else if (dm->support_ic_type & CCK_PD_IC_TYPE3)
		cckpd_t->cckpd_hw_type = 3;
	else if (dm->support_ic_type & CCK_PD_IC_TYPE4)
		cckpd_t->cckpd_hw_type = 4;

	PHYDM_DBG(dm, DBG_CCKPD, "[%s] cckpd_hw_type=%d\n",
		  __func__, cckpd_t->cckpd_hw_type);

	cckpd_t->cck_pd_lv = CCK_PD_LV_INIT;
	cckpd_t->cck_n_rx = 0xff;
	cckpd_t->cck_bw = CHANNEL_WIDTH_MAX;

	switch (cckpd_t->cckpd_hw_type) {
	#ifdef PHYDM_COMPILE_CCKPD_TYPE1
	case 1:
		phydm_set_cckpd_lv_type1(dm, CCK_PD_LV_0);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE2
	case 2:
		cckpd_t->aaa_default = odm_read_1byte(dm, 0xaaa) & 0x1f;
		phydm_set_cckpd_lv_type2(dm, CCK_PD_LV_0);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE3
	case 3:
		phydm_cck_pd_init_type3(dm);
		break;
	#endif
	#ifdef PHYDM_COMPILE_CCKPD_TYPE4
	case 4:
		phydm_cck_pd_init_type4(dm);
		break;
	#endif
	default:
		pr_debug("[%s]warning\n", __func__);
		break;
	}
}
#endif /*#ifdef PHYDM_SUPPORT_CCKPD*/


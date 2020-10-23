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
#include "mp_precomp.h"
#include "phydm_precomp.h"

s32 phydm_get_cfo_hz(void *dm_void, u32 val, u8 bit_num, u8 frac_num)
{
	s32 val_s = 0;

	val_s = phydm_cnvrt_2_sign(val, bit_num);

	if (frac_num == 10) /*@ (X*312500)/1024 ~= X*305*/
		val_s *= 305;
	else if (frac_num == 11) /*@ (X*312500)/2048 ~= X*152*/
		val_s *= 152;
	else if (frac_num == 12) /*@ (X*312500)/4096 ~= X*76*/
		val_s *= 76;

	return val_s;
}

#if (ODM_IC_11AC_SERIES_SUPPORT)
void phydm_get_cfo_info_ac(void *dm_void, struct phydm_cfo_rpt *cfo)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i = 0;
	u32 val[4] = {0};
	u32 val_1[4] = {0};
	u32 val_2[4] = {0};
	u32 val_tmp = 0;

	val[0] = odm_read_4byte(dm, R_0xd0c);
	val_1[0] = odm_read_4byte(dm, R_0xd10);
	val_2[0] = odm_get_bb_reg(dm, R_0xd14, 0x1fff0000);

	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	val[1] = odm_read_4byte(dm, R_0xd4c);
	val_1[1] = odm_read_4byte(dm, R_0xd50);
	val_2[1] = odm_get_bb_reg(dm, R_0xd54, 0x1fff0000);
	#endif

	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	val[2] = odm_read_4byte(dm, R_0xd8c);
	val_1[2] = odm_read_4byte(dm, R_0xd90);
	val_2[2] = odm_get_bb_reg(dm, R_0xd94, 0x1fff0000);
	#endif

	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	val[3] = odm_read_4byte(dm, R_0xdcc);
	val_1[3] = odm_read_4byte(dm, R_0xdd0);
	val_2[3] = odm_get_bb_reg(dm, R_0xdd4, 0x1fff0000);
	#endif

	for (i = 0; i < dm->num_rf_path; i++) {
		val_tmp = val[i] & 0xfff;	/*@ Short CFO, S(12,11)*/
		cfo->cfo_rpt_s[i] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);

		val_tmp = val[i] >> 16;		/*@ Long CFO, S(13,12)*/
		cfo->cfo_rpt_l[i] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);

		val_tmp = val_1[i] & 0x7ff;	/*@ SCFO, S(11,10)*/
		cfo->cfo_rpt_sec[i] = phydm_get_cfo_hz(dm, val_tmp, 11, 10);

		val_tmp = val_1[i] >> 16;	/*@ Acq CFO, S(13,12)*/
		cfo->cfo_rpt_acq[i] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);

		val_tmp = val_2[i];		/*@ End CFO, S(13,12)*/
		cfo->cfo_rpt_end[i] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);
	}
}
#endif

#if (ODM_IC_11N_SERIES_SUPPORT)
void phydm_get_cfo_info_n(void *dm_void, struct phydm_cfo_rpt *cfo)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 val[5] = {0};
	u32 val_tmp = 0;

	odm_set_bb_reg(dm, R_0xd00, BIT(26), 1);

	val[0] = odm_read_4byte(dm, R_0xdac); /*@ Short CFO*/
	val[1] = odm_read_4byte(dm, R_0xdb0); /*@ Long CFO*/
	val[2] = odm_read_4byte(dm, R_0xdb8); /*@ Sec CFO*/
	val[3] = odm_read_4byte(dm, R_0xde0); /*@ Acq CFO*/
	val[4] = odm_read_4byte(dm, R_0xdbc); /*@ End CFO*/

	/*@[path-A]*/
	if (dm->support_ic_type & (ODM_RTL8721D | ODM_RTL8710C)) {
		val_tmp = (val[0] & 0x0fff0000) >> 16; /*@ Short CFO, S(12,11)*/
		cfo->cfo_rpt_s[0] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);
		val_tmp = (val[1] & 0x0fff0000) >> 16;	/*@ Long CFO, S(12,11)*/
		cfo->cfo_rpt_l[0] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);
		val_tmp = (val[2] & 0x0fff0000) >> 16;	/*@ Sec CFO, S(12,11)*/
		cfo->cfo_rpt_sec[0] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);
		val_tmp = (val[3] & 0x0fff0000) >> 16;	/*@ Acq CFO, S(12,11)*/
		cfo->cfo_rpt_acq[0] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);
		val_tmp = (val[4] & 0x0fff0000) >> 16;	/*@ Acq CFO, S(12,11)*/
		cfo->cfo_rpt_end[0] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);
	} else {
		val_tmp = (val[0] & 0x0fff0000) >> 16; /*@ Short CFO, S(12,11)*/
		cfo->cfo_rpt_s[0] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);
		val_tmp = (val[1] & 0x1fff0000) >> 16;	/*@ Long CFO, S(13,12)*/
		cfo->cfo_rpt_l[0] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);
		val_tmp = (val[2] & 0x7ff0000) >> 16;	/*@ Sec CFO, S(11,10)*/
		cfo->cfo_rpt_sec[0] = phydm_get_cfo_hz(dm, val_tmp, 11, 10);
		val_tmp = (val[3] & 0x1fff0000) >> 16;	/*@ Acq CFO, S(13,12)*/
		cfo->cfo_rpt_acq[0] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);
		val_tmp = (val[4] & 0x1fff0000) >> 16;	/*@ Acq CFO, S(13,12)*/
		cfo->cfo_rpt_end[0] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);
	}

	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	/*@[path-B]*/
	val_tmp = val[0] & 0xfff;		/*@ Short CFO, S(12,11)*/
	cfo->cfo_rpt_s[1] = phydm_get_cfo_hz(dm, val_tmp, 12, 11);
	val_tmp = val[1] & 0x1fff;		/*@ Long CFO, S(13,12)*/
	cfo->cfo_rpt_l[1] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);
	val_tmp = val[2] & 0x7ff;		/*@ Sec CFO, S(11,10)*/
	cfo->cfo_rpt_sec[1] = phydm_get_cfo_hz(dm, val_tmp, 11, 10);
	val_tmp = val[3] & 0x1fff;		/*@ Acq CFO, S(13,12)*/
	cfo->cfo_rpt_acq[1] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);
	val_tmp = val[4] & 0x1fff;		/*@ Acq CFO, S(13,12)*/
	cfo->cfo_rpt_end[1] = phydm_get_cfo_hz(dm, val_tmp, 13, 12);
	#endif
}

void phydm_set_atc_status(void *dm_void, boolean atc_status)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;
	u32 reg_tmp = 0;
	u32 mask_tmp = 0;

	PHYDM_DBG(dm, DBG_CFO_TRK, "[%s]ATC_en=%d\n", __func__, atc_status);

	if (cfo_track->is_atc_status == atc_status)
		return;

	reg_tmp = ODM_REG(BB_ATC, dm);
	mask_tmp = ODM_BIT(BB_ATC, dm);
	odm_set_bb_reg(dm, reg_tmp, mask_tmp, atc_status);
	cfo_track->is_atc_status = atc_status;
}

boolean
phydm_get_atc_status(void *dm_void)
{
	boolean atc_status = false;
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 reg_tmp = 0;
	u32 mask_tmp = 0;

	reg_tmp = ODM_REG(BB_ATC, dm);
	mask_tmp = ODM_BIT(BB_ATC, dm);

	atc_status = (boolean)odm_get_bb_reg(dm, reg_tmp, mask_tmp);

	PHYDM_DBG(dm, DBG_CFO_TRK, "[%s]atc_status=%d\n", __func__, atc_status);
	return atc_status;
}
#endif

void phydm_get_cfo_info(void *dm_void, struct phydm_cfo_rpt *cfo)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	switch (dm->ic_ip_series) {
	#if (ODM_IC_11N_SERIES_SUPPORT)
	case PHYDM_IC_N:
		phydm_get_cfo_info_n(dm, cfo);
		break;
	#endif
	#if (ODM_IC_11AC_SERIES_SUPPORT)
	case PHYDM_IC_AC:
		phydm_get_cfo_info_ac(dm, cfo);
		break;
	#endif
	default:
		break;
	}
}

boolean
phydm_set_crystal_cap_reg(void *dm_void, u8 crystal_cap)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;
	u32 reg_val = 0;

	if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8814B |
	    ODM_RTL8195B | ODM_RTL8812F | ODM_RTL8721D | ODM_RTL8710C|ODM_RTL8723F)) {
		crystal_cap &= 0x7F;
		reg_val = crystal_cap | (crystal_cap << 7);
	} else {
		crystal_cap &= 0x3F;
		reg_val = crystal_cap | (crystal_cap << 6);
	}

	cfo_track->crystal_cap = crystal_cap;

	if (dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8188F)) {
		#if (RTL8188E_SUPPORT || RTL8188F_SUPPORT)
		/* write 0x24[22:17] = 0x24[16:11] = crystal_cap */
		odm_set_mac_reg(dm, R_0x24, 0x7ff800, reg_val);
		#endif
	}
	#if (RTL8812A_SUPPORT)
	else if (dm->support_ic_type & ODM_RTL8812) {
		/* write 0x2C[30:25] = 0x2C[24:19] = crystal_cap */
		odm_set_mac_reg(dm, R_0x2c, 0x7FF80000, reg_val);
	}
	#endif
	#if (RTL8703B_SUPPORT || RTL8723B_SUPPORT || RTL8192E_SUPPORT ||\
	     RTL8821A_SUPPORT || RTL8723D_SUPPORT)
	else if ((dm->support_ic_type &
		 (ODM_RTL8703B | ODM_RTL8723B | ODM_RTL8192E | ODM_RTL8821 |
		 ODM_RTL8723D))) {
		/* @0x2C[23:18] = 0x2C[17:12] = crystal_cap */
		odm_set_mac_reg(dm, R_0x2c, 0x00FFF000, reg_val);
	}
	#endif
	#if (RTL8814A_SUPPORT)
	else if (dm->support_ic_type & ODM_RTL8814A) {
		/* write 0x2C[26:21] = 0x2C[20:15] = crystal_cap */
		odm_set_mac_reg(dm, R_0x2c, 0x07FF8000, reg_val);
	}
	#endif
	#if (RTL8822B_SUPPORT || RTL8821C_SUPPORT || RTL8197F_SUPPORT ||\
	     RTL8192F_SUPPORT || RTL8197G_SUPPORT || RTL8198F_SUPPORT)
	else if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C |
		 ODM_RTL8197F | ODM_RTL8192F | ODM_RTL8197G | ODM_RTL8198F)) {
		/* write 0x24[30:25] = 0x28[6:1] = crystal_cap */
		odm_set_mac_reg(dm, R_0x24, 0x7e000000, crystal_cap);
		odm_set_mac_reg(dm, R_0x28, 0x7e, crystal_cap);
	}
	#endif
	#if (RTL8710B_SUPPORT)
	else if (dm->support_ic_type & (ODM_RTL8710B)) {
		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		/* write 0x60[29:24] = 0x60[23:18] = crystal_cap */
		HAL_SetSYSOnReg(dm->adapter, R_0x60, 0x3FFC0000, reg_val);
		#endif
	}
	#endif
	#if (RTL8195B_SUPPORT)
	else if (dm->support_ic_type & ODM_RTL8195B) {
		phydm_set_crystalcap(dm, (u8)(reg_val & 0x7f));
	}
	#endif
	#if (RTL8721D_SUPPORT)
	else if (dm->support_ic_type & (ODM_RTL8721D)) {
		/* write 0x4800_0228[30:24] crystal_cap */
		/*HAL_SetSYSOnReg(dm->adapter, */
		/*REG_SYS_XTAL_8721d, 0x7F000000, crystal_cap);*/
		u32 temp_val = HAL_READ32(SYSTEM_CTRL_BASE_LP,
					   REG_SYS_EFUSE_SYSCFG2);
		temp_val = ((crystal_cap << 24) & 0x7F000000)
						| (temp_val & (~0x7F000000));
		HAL_WRITE32(SYSTEM_CTRL_BASE_LP, REG_SYS_EFUSE_SYSCFG2,
			    temp_val);
	}
	#endif
	#if (RTL8710C_SUPPORT)
	else if (dm->support_ic_type & (ODM_RTL8710C)) {
		/* write MAC reg 0x28[13:7][6:0] crystal_cap */
		phydm_set_crystalcap(dm, (u8)(reg_val & 0x7f));
	}
	#endif
	#if (RTL8723F_SUPPORT)
	else if (dm->support_ic_type & ODM_RTL8723F) {
		/* write 0x103c[23:17] = 0x103c[16:10] = crystal_cap */
		odm_set_mac_reg(dm, R_0x103c, 0x00FFFC00, reg_val);
	}
	#endif
#if (RTL8822C_SUPPORT || RTL8814B_SUPPORT || RTL8812F_SUPPORT)
	else if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8814B |
		 ODM_RTL8812F)) {
		/* write 0x1040[23:17] = 0x1040[16:10] = crystal_cap */
		odm_set_mac_reg(dm, R_0x1040, 0x00FFFC00, reg_val);
	} else {
		return false;
	}
#endif
	return true;
}

void phydm_set_crystal_cap(void *dm_void, u8 crystal_cap)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;

	if (cfo_track->crystal_cap == crystal_cap)
		return;

	if (phydm_set_crystal_cap_reg(dm, crystal_cap))
		PHYDM_DBG(dm, DBG_CFO_TRK, "Set crystal_cap = 0x%x\n",
			  cfo_track->crystal_cap);
	else
		PHYDM_DBG(dm, DBG_CFO_TRK, "Set fail\n");
}

void phydm_cfo_tracking_reset(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;

	PHYDM_DBG(dm, DBG_CFO_TRK, "%s ======>\n", __func__);

	if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8814B | ODM_RTL8195B |
	    ODM_RTL8812F|ODM_RTL8723F))
		cfo_track->def_x_cap = cfo_track->crystal_cap_default & 0x7f;
	else
		cfo_track->def_x_cap = cfo_track->crystal_cap_default & 0x3f;

	cfo_track->is_adjust = true;

	if (cfo_track->crystal_cap > cfo_track->def_x_cap) {
		phydm_set_crystal_cap(dm, cfo_track->crystal_cap - 1);
		PHYDM_DBG(dm, DBG_CFO_TRK, "approch to Init-val (0x%x)\n",
			  cfo_track->crystal_cap);

	} else if (cfo_track->crystal_cap < cfo_track->def_x_cap) {
		phydm_set_crystal_cap(dm, cfo_track->crystal_cap + 1);
		PHYDM_DBG(dm, DBG_CFO_TRK, "approch to init-val 0x%x\n",
			  cfo_track->crystal_cap);
	}

#if ODM_IC_11N_SERIES_SUPPORT
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		phydm_set_atc_status(dm, true);
#endif
#endif
#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE | ODM_AP))
	if (dm->support_ic_type & ODM_RTL8814B) {
		/*Disable advance time for CFO residual*/
		odm_set_bb_reg(dm, R_0xc2c, BIT29, 0x0);
	}
#endif
#endif
}

void phydm_cfo_tracking_init(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;

	PHYDM_DBG(dm, DBG_CFO_TRK, "[%s]=========>\n", __func__);
	if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8814B | ODM_RTL8195B |
	    ODM_RTL8812F|ODM_RTL8723F))
		cfo_track->crystal_cap = cfo_track->crystal_cap_default & 0x7f;
	else
		cfo_track->crystal_cap = cfo_track->crystal_cap_default & 0x3f;

	cfo_track->def_x_cap = cfo_track->crystal_cap;
	cfo_track->is_adjust = true;
	PHYDM_DBG(dm, DBG_CFO_TRK, "crystal_cap=0x%x\n", cfo_track->def_x_cap);

#if (RTL8822B_SUPPORT || RTL8821C_SUPPORT)
	/* @Crystal cap. control by WiFi */
	if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C))
		odm_set_mac_reg(dm, R_0x10, 0x40, 0x1);
#endif
}

void phydm_cfo_tracking(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;
	s32 cfo_avg = 0, cfo_path_sum = 0, cfo_abs = 0;
	u32 cfo_rpt_sum = 0, cfo_khz_avg[4] = {0};
	s8 crystal_cap = cfo_track->crystal_cap;
	u8 i = 0, valid_path_cnt = 0;

	if (!(dm->support_ability & ODM_BB_CFO_TRACKING))
		return;

	PHYDM_DBG(dm, DBG_CFO_TRK, "%s ======>\n", __func__);

	if (!dm->is_linked || !dm->is_one_entry_only) {
		phydm_cfo_tracking_reset(dm);
		PHYDM_DBG(dm, DBG_CFO_TRK, "is_linked=%d, one_entry_only=%d\n",
			  dm->is_linked, dm->is_one_entry_only);

	} else {
		/* No new packet */
		if (cfo_track->packet_count == cfo_track->packet_count_pre) {
			PHYDM_DBG(dm, DBG_CFO_TRK, "Pkt cnt doesn't change\n");
			return;
		}
		cfo_track->packet_count_pre = cfo_track->packet_count;

		/*@Calculate CFO */
		for (i = 0; i < dm->num_rf_path; i++) {
			if (!(dm->rx_ant_status & BIT(i)))
				continue;

			valid_path_cnt++;

			if (cfo_track->CFO_tail[i] < 0)
				cfo_abs = 0 - cfo_track->CFO_tail[i];
			else
				cfo_abs = cfo_track->CFO_tail[i];

			cfo_rpt_sum = (u32)CFO_HW_RPT_2_KHZ(cfo_abs);
			cfo_khz_avg[i] = PHYDM_DIV(cfo_rpt_sum,
						   cfo_track->CFO_cnt[i]);

			PHYDM_DBG(dm, DBG_CFO_TRK,
				  "[Path-%d] CFO_sum=((%d)), cnt=((%d)), CFO_avg=((%s%d))kHz\n",
				  i, cfo_rpt_sum, cfo_track->CFO_cnt[i],
				  ((cfo_track->CFO_tail[i] < 0) ? "-" : " "),
				  cfo_khz_avg[i]);

			if (cfo_track->CFO_tail[i] < 0)
				cfo_path_sum += (0 - (s32)cfo_khz_avg[i]);
			else
				cfo_path_sum += (s32)cfo_khz_avg[i];
		}

		if (valid_path_cnt >= 2)
			cfo_avg = cfo_path_sum / valid_path_cnt;
		else
			cfo_avg = cfo_path_sum;

		cfo_track->CFO_ave_pre = cfo_avg;

		PHYDM_DBG(dm, DBG_CFO_TRK, "path_cnt=%d, CFO_avg_path=%d kHz\n",
			  valid_path_cnt, cfo_avg);

		/*reset counter*/
		for (i = 0; i < dm->num_rf_path; i++) {
			cfo_track->CFO_tail[i] = 0;
			cfo_track->CFO_cnt[i] = 0;
		}

		/* To adjust crystal cap or not */
		if (!cfo_track->is_adjust) {
			if (cfo_avg > CFO_TRK_ENABLE_TH ||
			    cfo_avg < (-CFO_TRK_ENABLE_TH))
				cfo_track->is_adjust = true;
		} else {
			if (cfo_avg <= CFO_TRK_STOP_TH &&
			    cfo_avg >= (-CFO_TRK_STOP_TH))
				cfo_track->is_adjust = false;
		}

		#ifdef ODM_CONFIG_BT_COEXIST
		/*@BT case: Disable CFO tracking */
		if (dm->bt_info_table.is_bt_enabled) {
			cfo_track->is_adjust = false;
			phydm_set_crystal_cap(dm, cfo_track->def_x_cap);
			PHYDM_DBG(dm, DBG_CFO_TRK, "[BT]Disable CFO_track\n");
		}
		#endif

		/*@Adjust Crystal Cap. */
		if (cfo_track->is_adjust) {
			if (cfo_avg > CFO_TRK_STOP_TH)
				crystal_cap += 1;
			else if (cfo_avg < (-CFO_TRK_STOP_TH))
				crystal_cap -= 1;

			if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8814B |
			    ODM_RTL8195B | ODM_RTL8812F|ODM_RTL8723F)) {
				if (crystal_cap > 0x7F)
					crystal_cap = 0x7F;
			} else {
				if (crystal_cap > 0x3F)
					crystal_cap = 0x3F;
			}
			if (crystal_cap < 0)
				crystal_cap = 0;

			phydm_set_crystal_cap(dm, (u8)crystal_cap);
		}

		PHYDM_DBG(dm, DBG_CFO_TRK, "X_cap{Curr,Default}={0x%x,0x%x}\n",
			  cfo_track->crystal_cap, cfo_track->def_x_cap);

		/* @Dynamic ATC switch */
		#if ODM_IC_11N_SERIES_SUPPORT
		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		if (dm->support_ic_type & ODM_IC_11N_SERIES) {
			if (cfo_avg < CFO_TH_ATC && cfo_avg > -CFO_TH_ATC)
				phydm_set_atc_status(dm, false);
			else
				phydm_set_atc_status(dm, true);

		}
		#endif
		#endif
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE | ODM_AP))
		if (dm->support_ic_type & ODM_RTL8814B) {
			//Disable advance time for CFO residual
			odm_set_bb_reg(dm, R_0xc2c, BIT29, 0x0);
		}
		#endif
		#endif
	}
}

void phydm_parsing_cfo(void *dm_void, void *pktinfo_void, s8 *pcfotail,
		       u8 num_ss)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_perpkt_info_struct *pktinfo = NULL;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;
	boolean valid_info = false;
	u8 i = 0;

	if (!(dm->support_ability & ODM_BB_CFO_TRACKING))
		return;

	pktinfo = (struct phydm_perpkt_info_struct *)pktinfo_void;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE | ODM_IOT))
	if (pktinfo->is_packet_match_bssid)
		valid_info = true;
#else
	if (dm->number_active_client == 1)
		valid_info = true;
#endif
	if (valid_info) {
		if (num_ss > dm->num_rf_path) /*@For fool proof*/
			num_ss = dm->num_rf_path;
		#if 0
		PHYDM_DBG(dm, DBG_CFO_TRK, "num_ss=%d, num_rf_path=%d\n",
			  num_ss, dm->num_rf_path);
		#endif

		/* @ Update CFO report for path-A & path-B */
		/* Only paht-A and path-B have CFO tail and short CFO */
		for (i = 0; i < dm->num_rf_path; i++) {
			if (!(dm->rx_ant_status & BIT(i)))
				continue;
			cfo_track->CFO_tail[i] += pcfotail[i];
			cfo_track->CFO_cnt[i]++;
			#if 0
			PHYDM_DBG(dm, DBG_CFO_TRK,
				  "[ID %d][path %d][rate 0x%x] CFO_tail = ((%d)), CFO_tail_sum = ((%d)), CFO_cnt = ((%d))\n",
				  pktinfo->station_id, i, pktinfo->data_rate,
				  pcfotail[i], cfo_track->CFO_tail[i],
				  cfo_track->CFO_cnt[i]);
			#endif
		}

		/* @ Update packet counter */
		if (cfo_track->packet_count == 0xffffffff)
			cfo_track->packet_count = 0;
		else
			cfo_track->packet_count++;
	}
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phy_Init_crystal_capacity(void *dm_void, u8 crystal_cap)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (!phydm_set_crystal_cap_reg(dm, crystal_cap))
		RT_TRACE_F(COMP_INIT, DBG_SERIOUS,
			   ("Crystal is not initialized!\n"));
}
#endif

void phydm_cfo_tracking_debug(void *dm_void, char input[][16], u32 *_used,
			      char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_track = &dm->dm_cfo_track;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "set Xcap: {1}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "show Xcap: {100}\n");
	} else {
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (var1[0] == 1) {
			PHYDM_SSCANF(input[2], DCMD_HEX, &var1[1]);
			phydm_set_crystal_cap(dm, (u8)var1[1]);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Set X_cap=0x%x\n", cfo_track->crystal_cap);
		} else if (var1[0] == 100) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "X_cap=0x%x\n", cfo_track->crystal_cap);
		}
	}
	*_used = used;
	*_out_len = out_len;
}


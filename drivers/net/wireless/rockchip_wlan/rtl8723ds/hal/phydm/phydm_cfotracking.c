/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#include "mp_precomp.h"
#include "phydm_precomp.h"

void
phydm_set_crystal_cap(
	void					*p_dm_void,
	u8					crystal_cap
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cfo_track_struct				*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);

	if (p_cfo_track->crystal_cap == crystal_cap)
		return;

	crystal_cap = crystal_cap & 0x3F;
	p_cfo_track->crystal_cap = crystal_cap;

	if (p_dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8188F)) {
		#if (RTL8188E_SUPPORT == 1) || (RTL8188F_SUPPORT == 1)
		/* write 0x24[22:17] = 0x24[16:11] = crystal_cap */
		odm_set_bb_reg(p_dm, REG_AFE_XTAL_CTRL, 0x007ff800, (crystal_cap | (crystal_cap << 6)));
		#endif
	}
	#if (RTL8812A_SUPPORT == 1)
	else if (p_dm->support_ic_type & ODM_RTL8812) {
	
		/* write 0x2C[30:25] = 0x2C[24:19] = crystal_cap */
		odm_set_bb_reg(p_dm, REG_MAC_PHY_CTRL, 0x7FF80000, (crystal_cap | (crystal_cap << 6)));
		
	} 
	#endif
	#if (RTL8703B_SUPPORT == 1) || (RTL8723B_SUPPORT == 1) || (RTL8192E_SUPPORT == 1) || (RTL8821A_SUPPORT == 1) || (RTL8723D_SUPPORT == 1)
	else if ((p_dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723B | ODM_RTL8192E | ODM_RTL8821 | ODM_RTL8723D))) {
	
		/* 0x2C[23:18] = 0x2C[17:12] = crystal_cap */
		odm_set_bb_reg(p_dm, REG_MAC_PHY_CTRL, 0x00FFF000, (crystal_cap | (crystal_cap << 6)));
		
	}
	#endif
	#if (RTL8814A_SUPPORT == 1)	
	else if (p_dm->support_ic_type & ODM_RTL8814A) {
	
		/* write 0x2C[26:21] = 0x2C[20:15] = crystal_cap */
		odm_set_bb_reg(p_dm, REG_MAC_PHY_CTRL, 0x07FF8000, (crystal_cap | (crystal_cap << 6)));
		
	}
	#endif
	#if (RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1) || (RTL8197F_SUPPORT == 1)
	else if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C | ODM_RTL8197F)) {
	
		/* write 0x24[30:25] = 0x28[6:1] = crystal_cap */
		odm_set_bb_reg(p_dm, REG_AFE_XTAL_CTRL, 0x7e000000, crystal_cap);
		odm_set_bb_reg(p_dm, REG_AFE_PLL_CTRL, 0x7e, crystal_cap);
		
	}
	#endif
	#if (RTL8710B_SUPPORT == 1)
	else if (p_dm->support_ic_type & (ODM_RTL8710B)) {
	
		#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)		
		/* write 0x60[29:24] = 0x60[23:18] = crystal_cap */
		HAL_SetSYSOnReg(p_dm->adapter, REG_SYS_XTAL_CTRL0, 0x3FFC0000, (crystal_cap | (crystal_cap << 6)));
		#endif
	}
	#endif
	PHYDM_DBG(p_dm, DBG_CFO_TRK, ("Set rystal_cap = 0x%x\n", p_cfo_track->crystal_cap));

}

u8
odm_get_default_crytaltal_cap(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8						crystal_cap = 0x20;

#if (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
	struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;
	struct rtl_efuse *rtlefuse = rtl_efuse(rtlpriv);

	crystal_cap = rtlefuse->crystalcap;
#elif (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct _ADAPTER					*adapter = p_dm->adapter;
	HAL_DATA_TYPE				*p_hal_data = GET_HAL_DATA(adapter);

	crystal_cap = p_hal_data->crystal_cap;
#else
	struct rtl8192cd_priv	*priv		= p_dm->priv;

	if (priv->pmib->dot11RFEntry.xcap > 0)
		crystal_cap = priv->pmib->dot11RFEntry.xcap;
#endif

	crystal_cap = crystal_cap & 0x3f;

	return crystal_cap;
}

void
odm_set_atc_status(
	void					*p_dm_void,
	boolean					atc_status
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cfo_track_struct				*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);

	if (p_cfo_track->is_atc_status == atc_status)
		return;

	odm_set_bb_reg(p_dm, ODM_REG(BB_ATC, p_dm), ODM_BIT(BB_ATC, p_dm), atc_status);
	p_cfo_track->is_atc_status = atc_status;
}

boolean
odm_get_atc_status(
	void					*p_dm_void
)
{
	boolean						atc_status;
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	atc_status = (boolean)odm_get_bb_reg(p_dm, ODM_REG(BB_ATC, p_dm), ODM_BIT(BB_ATC, p_dm));
	return atc_status;
}

void
odm_cfo_tracking_reset(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cfo_track_struct				*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);

	p_cfo_track->def_x_cap = odm_get_default_crytaltal_cap(p_dm);
	p_cfo_track->is_adjust = true;

	if (p_cfo_track->crystal_cap > p_cfo_track->def_x_cap) {
		phydm_set_crystal_cap(p_dm, p_cfo_track->crystal_cap - 1);
		PHYDM_DBG(p_dm, DBG_CFO_TRK,
			("odm_cfo_tracking_reset(): approch default value (0x%x)\n", p_cfo_track->crystal_cap));
	} else if (p_cfo_track->crystal_cap < p_cfo_track->def_x_cap) {
		phydm_set_crystal_cap(p_dm, p_cfo_track->crystal_cap + 1);
		PHYDM_DBG(p_dm, DBG_CFO_TRK,
			("odm_cfo_tracking_reset(): approch default value (0x%x)\n", p_cfo_track->crystal_cap));
	}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	odm_set_atc_status(p_dm, true);
#endif
}

void
phydm_cfo_tracking_init(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cfo_track_struct				*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);

	p_cfo_track->def_x_cap = p_cfo_track->crystal_cap = odm_get_default_crytaltal_cap(p_dm);
	p_cfo_track->is_atc_status = odm_get_atc_status(p_dm);
	p_cfo_track->is_adjust = true;
	PHYDM_DBG(p_dm, DBG_CFO_TRK, ("ODM_CfoTracking_init()=========>\n"));
	PHYDM_DBG(p_dm, DBG_CFO_TRK, ("ODM_CfoTracking_init(): is_atc_status = %d, crystal_cap = 0x%x\n", p_cfo_track->is_atc_status, p_cfo_track->def_x_cap));

#if RTL8822B_SUPPORT
	/* Crystal cap. control by WiFi */
	if (p_dm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(p_dm, 0x10, 0x40, 0x1);
#endif

#if RTL8821C_SUPPORT
	/* Crystal cap. control by WiFi */
	if (p_dm->support_ic_type & ODM_RTL8821C)
		odm_set_bb_reg(p_dm, 0x10, 0x40, 0x1);
#endif
}

void
odm_cfo_tracking(
	void					*p_dm_void
)
{
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cfo_track_struct				*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);
	s32						CFO_ave = 0;
	u32						CFO_rpt_sum, cfo_khz_avg[4] = {0};
	s32						CFO_ave_diff;
	s8						crystal_cap = p_cfo_track->crystal_cap;
	u8						adjust_xtal = 1, i, valid_path_cnt = 0;

	/* 4 Support ability */
	if (!(p_dm->support_ability & ODM_BB_CFO_TRACKING)) {
		PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): Return: support_ability ODM_BB_CFO_TRACKING is disabled\n"));
		return;
	}

	PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking()=========>\n"));

	if (!p_dm->is_linked || !p_dm->is_one_entry_only) {
		/* 4 No link or more than one entry */
		odm_cfo_tracking_reset(p_dm);
		PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): Reset: is_linked = %d, is_one_entry_only = %d\n",
			p_dm->is_linked, p_dm->is_one_entry_only));
	} else {
		/* 3 1. CFO Tracking */
		/* 4 1.1 No new packet */
		if (p_cfo_track->packet_count == p_cfo_track->packet_count_pre) {
			PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): packet counter doesn't change\n"));
			return;
		}
		p_cfo_track->packet_count_pre = p_cfo_track->packet_count;

		/* 4 1.2 Calculate CFO */
		for (i = 0; i < p_dm->num_rf_path; i++) {

			if (p_cfo_track->CFO_cnt[i] == 0)
				continue;

			valid_path_cnt++;
			CFO_rpt_sum = (u32)((p_cfo_track->CFO_tail[i] < 0) ? (0 - p_cfo_track->CFO_tail[i]) :  p_cfo_track->CFO_tail[i]);
			cfo_khz_avg[i] = CFO_HW_RPT_2_MHZ(CFO_rpt_sum) / p_cfo_track->CFO_cnt[i];

			PHYDM_DBG(p_dm, DBG_CFO_TRK, ("[path %d] CFO_rpt_sum = (( %d )), CFO_cnt = (( %d )) , CFO_avg= (( %s%d )) kHz\n",
				i, CFO_rpt_sum, p_cfo_track->CFO_cnt[i], ((p_cfo_track->CFO_tail[i] < 0) ? "-" : " "), cfo_khz_avg[i]));
		}

		for (i = 0; i < valid_path_cnt; i++) {

			/* PHYDM_DBG(p_dm, DBG_CFO_TRK, ("path [%d], p_cfo_track->CFO_tail = %d\n", i, p_cfo_track->CFO_tail[i])); */
			if (p_cfo_track->CFO_tail[i] < 0) {
				CFO_ave += (0 - (s32)cfo_khz_avg[i]);
				/* PHYDM_DBG(p_dm, DBG_CFO_TRK, ("CFO_ave = %d\n", CFO_ave)); */
			} else
				CFO_ave += (s32)cfo_khz_avg[i];
		}

		if (valid_path_cnt >= 2)
			CFO_ave = CFO_ave / valid_path_cnt;

		PHYDM_DBG(p_dm, DBG_CFO_TRK, ("valid_path_cnt = ((%d)), CFO_ave = ((%d kHz))\n", valid_path_cnt, CFO_ave));

		/*reset counter*/
		for (i = 0; i < p_dm->num_rf_path; i++) {
			p_cfo_track->CFO_tail[i] = 0;
			p_cfo_track->CFO_cnt[i] = 0;
		}

		/* 4 1.3 Avoid abnormal large CFO */
		CFO_ave_diff = (p_cfo_track->CFO_ave_pre >= CFO_ave) ? (p_cfo_track->CFO_ave_pre - CFO_ave) : (CFO_ave - p_cfo_track->CFO_ave_pre);
		if (CFO_ave_diff > 20 && p_cfo_track->large_cfo_hit == 0 && !p_cfo_track->is_adjust) {
			PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): first large CFO hit\n"));
			p_cfo_track->large_cfo_hit = 1;
			return;
		} else
			p_cfo_track->large_cfo_hit = 0;
		p_cfo_track->CFO_ave_pre = CFO_ave;

		/* 4 1.4 Dynamic Xtal threshold */
		if (p_cfo_track->is_adjust == false) {
			if (CFO_ave > CFO_TH_XTAL_HIGH || CFO_ave < (-CFO_TH_XTAL_HIGH))
				p_cfo_track->is_adjust = true;
		} else {
			if (CFO_ave < CFO_TH_XTAL_LOW && CFO_ave > (-CFO_TH_XTAL_LOW))
				p_cfo_track->is_adjust = false;
		}

#ifdef ODM_CONFIG_BT_COEXIST
		/* 4 1.5 BT case: Disable CFO tracking */
		if (p_dm->bt_info_table.is_bt_enabled) {
			p_cfo_track->is_adjust = false;
			phydm_set_crystal_cap(p_dm, p_cfo_track->def_x_cap);
			PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): Disable CFO tracking for BT!!\n"));
		}
#if 0
		/* 4 1.6 Big jump */
		if (p_cfo_track->is_adjust) {
			if (CFO_ave > CFO_TH_XTAL_LOW)
				adjust_xtal =  adjust_xtal + ((CFO_ave - CFO_TH_XTAL_LOW) >> 2);
			else if (CFO_ave < (-CFO_TH_XTAL_LOW))
				adjust_xtal =  adjust_xtal + ((CFO_TH_XTAL_LOW - CFO_ave) >> 2);

			PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): Crystal cap offset = %d\n", adjust_xtal));
		}
#endif
#endif
		/* 4 1.7 Adjust Crystal Cap. */
		if (p_cfo_track->is_adjust) {
			if (CFO_ave > CFO_TH_XTAL_LOW)
				crystal_cap = crystal_cap + adjust_xtal;
			else if (CFO_ave < (-CFO_TH_XTAL_LOW))
				crystal_cap = crystal_cap - adjust_xtal;

			if (crystal_cap > 0x3f)
				crystal_cap = 0x3f;
			else if (crystal_cap < 0)
				crystal_cap = 0;

			phydm_set_crystal_cap(p_dm, (u8)crystal_cap);
		}
		PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): Crystal cap = 0x%x, Default Crystal cap = 0x%x\n",
			p_cfo_track->crystal_cap, p_cfo_track->def_x_cap));

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
			return;

		/* 3 2. Dynamic ATC switch */
		if (CFO_ave < CFO_TH_ATC && CFO_ave > -CFO_TH_ATC) {
			odm_set_atc_status(p_dm, false);
			PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): Disable ATC!!\n"));
		} else {
			odm_set_atc_status(p_dm, true);
			PHYDM_DBG(p_dm, DBG_CFO_TRK, ("odm_cfo_tracking(): Enable ATC!!\n"));
		}
#endif
	}
}

void
odm_parsing_cfo(
	void			*p_dm_void,
	void			*p_pktinfo_void,
	s8			*pcfotail,
	u8			num_ss
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_perpkt_info_struct		*p_pktinfo = (struct phydm_perpkt_info_struct *)p_pktinfo_void;
	struct phydm_cfo_track_struct			*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);
	u8					i;

	if (!(p_dm->support_ability & ODM_BB_CFO_TRACKING))
		return;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (p_pktinfo->is_packet_match_bssid)
#else
	if (p_pktinfo->station_id != 0)
#endif
	{
		if (num_ss > p_dm->num_rf_path) /*For fool proof*/
			num_ss = p_dm->num_rf_path;

		/*PHYDM_DBG(p_dm, DBG_CFO_TRK, ("num_ss = ((%d)),  p_dm->num_rf_path = ((%d))\n", num_ss,  p_dm->num_rf_path));*/


		/* 3 Update CFO report for path-A & path-B */
		/* Only paht-A and path-B have CFO tail and short CFO */
		for (i = 0; i < num_ss; i++) {
			p_cfo_track->CFO_tail[i] += pcfotail[i];
			p_cfo_track->CFO_cnt[i]++;
			/*PHYDM_DBG(p_dm, DBG_CFO_TRK, ("[ID %d][path %d][rate 0x%x] CFO_tail = ((%d)), CFO_tail_sum = ((%d)), CFO_cnt = ((%d))\n",
				p_pktinfo->station_id, i, p_pktinfo->data_rate, pcfotail[i], p_cfo_track->CFO_tail[i], p_cfo_track->CFO_cnt[i]));
			*/
		}

		/* 3 Update packet counter */
		if (p_cfo_track->packet_count == 0xffffffff)
			p_cfo_track->packet_count = 0;
		else
			p_cfo_track->packet_count++;
	}
}

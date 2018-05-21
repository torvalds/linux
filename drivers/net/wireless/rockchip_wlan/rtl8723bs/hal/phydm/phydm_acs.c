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

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"


u8
odm_get_auto_channel_select_result(
	void			*p_dm_void,
	u8			band
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ACS_					*p_acs = &p_dm->dm_acs;

	PHYDM_DBG(p_dm, ODM_COMP_API, ("%s ======>\n", __func__));

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (band == ODM_BAND_2_4G) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("clean_CH_2g=%d\n", p_acs->clean_channel_2g));
		return (u8)p_acs->clean_channel_2g;
	} else {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("clean_CH_5g=%d\n", p_acs->clean_channel_5g));
		return (u8)p_acs->clean_channel_5g;
	}
#else
	return (u8)p_acs->clean_channel_2g;
#endif

}

void
odm_auto_channel_select_init(
	void			*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ACS_						*p_acs = &p_dm->dm_acs;
	u8						i;

	if (!(p_dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	if (p_acs->is_force_acs_result)
		return;

	PHYDM_DBG(p_dm, ODM_COMP_API, ("%s ======>\n", __func__));

	p_acs->clean_channel_2g = 1;
	p_acs->clean_channel_5g = 36;

	for (i = 0; i < ODM_MAX_CHANNEL_2G; ++i) {
		p_acs->channel_info_2g[0][i] = 0;
		p_acs->channel_info_2g[1][i] = 0;
	}

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		for (i = 0; i < ODM_MAX_CHANNEL_5G; ++i) {
			p_acs->channel_info_5g[0][i] = 0;
			p_acs->channel_info_5g[1][i] = 0;
		}
	}
#endif
}

void
odm_auto_channel_select_reset(
	void			*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ACS_						*p_acs = &p_dm->dm_acs;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;

	if (!(p_dm->support_ability & ODM_BB_ENV_MONITOR))
		return;

	if (p_acs->is_force_acs_result)
		return;

	PHYDM_DBG(p_dm, ODM_COMP_API, ("%s ======>\n", __func__));

	ccx_info->nhm_period = 0x1388;	/*20ms*/
	phydm_nhm_setting(p_dm, SET_NHM_SETTING);
	phydm_nhm_trigger(p_dm);
#endif
}

void
odm_auto_channel_select(
	void			*p_dm_void,
	u8			channel
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT					*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ACS_						*p_acs = &p_dm->dm_acs;
	struct _CCX_INFO		*ccx_info = &p_dm->dm_ccx_info;
	u8						channel_idx = 0, search_idx = 0;
	u8						noisy_nhm_th = 0x52;
	u8						i, noisy_nhm_th_index, low_pwr_cnt = 0;
	u16						max_score = 0;

	PHYDM_DBG(p_dm, ODM_COMP_API, ("%s ======>\n", __func__));

	if (!(p_dm->support_ability & ODM_BB_ENV_MONITOR)) {
		PHYDM_DBG(p_dm, DBG_DIG, ("Return: Not support\n"));
		return;
	}

	if (p_acs->is_force_acs_result) {
		PHYDM_DBG(p_dm, DBG_DIG, ("Force clean CH{2G,5G}={%d,%d}\n",
			p_acs->clean_channel_2g, p_acs->clean_channel_5g));
		return;
	}

	PHYDM_DBG(p_dm, ODM_COMP_API, ("CH=%d\n", channel));

	phydm_get_nhm_result(p_dm);
	noisy_nhm_th_index = (noisy_nhm_th - ccx_info->nhm_th[0]) << 2;

	for (i = 0; i <= 11; i++) {
		if (i <= noisy_nhm_th_index)
			low_pwr_cnt += ccx_info->nhm_result[i];
	}

	ccx_info->nhm_period = 0x2710;
	phydm_nhm_setting(p_dm, SET_NHM_SETTING);

	if (channel >= 1 && channel <= 14) {
		channel_idx = channel - 1;
		p_acs->channel_info_2g[1][channel_idx]++;

		if (p_acs->channel_info_2g[1][channel_idx] >= 2)
			p_acs->channel_info_2g[0][channel_idx] = (p_acs->channel_info_2g[0][channel_idx] >> 1) +
				(p_acs->channel_info_2g[0][channel_idx] >> 2) + (low_pwr_cnt >> 2);
		else
			p_acs->channel_info_2g[0][channel_idx] = low_pwr_cnt;

		PHYDM_DBG(p_dm, ODM_COMP_API, ("low_pwr_cnt = %d\n", low_pwr_cnt));
		PHYDM_DBG(p_dm, ODM_COMP_API, ("CH_Info[0][%d]=%d, CH_Info[1][%d]=%d\n", channel_idx, p_acs->channel_info_2g[0][channel_idx], channel_idx, p_acs->channel_info_2g[1][channel_idx]));

		for (search_idx = 0; search_idx < ODM_MAX_CHANNEL_2G; search_idx++) {
			if (p_acs->channel_info_2g[1][search_idx] != 0 && p_acs->channel_info_2g[0][search_idx] >= max_score) {
				max_score = p_acs->channel_info_2g[0][search_idx];
				p_acs->clean_channel_2g = search_idx + 1;
			}
		}
		PHYDM_DBG(p_dm, ODM_COMP_API, ("clean_CH_2g=%d, max_score=%d\n",
				p_acs->clean_channel_2g, max_score));

	} else if (channel >= 36) {
		/* Need to do */
		p_acs->clean_channel_5g = channel;
	}
#endif
}

boolean
phydm_acs_check(
	void	*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct rtl8192cd_priv		*priv = p_dm->priv;

	if ((priv->auto_channel != 0) && (priv->auto_channel != 2)) /* if struct _ACS_ running, do not do FA/CCA counter read */
		return true;
	else
		return false;
#else
	return false;
#endif
}

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)

void
phydm_auto_channel_select_setting_ap(
	void   *p_dm_void,
	u32  setting,             /* 0: STORE_DEFAULT_NHM_SETTING; 1: RESTORE_DEFAULT_NHM_SETTING, 2: ACS_NHM_SETTING */
	u32  acs_step
)
{
	struct PHY_DM_STRUCT           *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct rtl8192cd_priv       *priv           = p_dm->priv;
	struct _ACS_                    *p_acs         = &p_dm->dm_acs;

	PHYDM_DBG(p_dm, ODM_COMP_API, ("%s ======>\n", __func__));

	/* 3 Store Default setting */
	if (setting == STORE_DEFAULT_NHM_SETTING) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("STORE_DEFAULT_NHM_SETTING\n"));

		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {  /* store reg0x990, reg0x994, reg0x998, reg0x99c, Reg0x9a0 */
			p_acs->reg0x990 = odm_read_4byte(p_dm, ODM_REG_CCX_PERIOD_11AC);                /* reg0x990 */
			p_acs->reg0x994 = odm_read_4byte(p_dm, ODM_REG_NHM_TH9_TH10_11AC);           /* reg0x994 */
			p_acs->reg0x998 = odm_read_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC);       /* reg0x998 */
			p_acs->reg0x99c = odm_read_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC);       /* Reg0x99c */
			p_acs->reg0x9a0 = odm_read_1byte(p_dm, ODM_REG_NHM_TH8_11AC);                   /* Reg0x9a0, u8 */
		} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
			p_acs->reg0x890 = odm_read_4byte(p_dm, ODM_REG_NHM_TH9_TH10_11N);             /* reg0x890 */
			p_acs->reg0x894 = odm_read_4byte(p_dm, ODM_REG_CCX_PERIOD_11N);                  /* reg0x894 */
			p_acs->reg0x898 = odm_read_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N);         /* reg0x898 */
			p_acs->reg0x89c = odm_read_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N);         /* Reg0x89c */
			p_acs->reg0xe28 = odm_read_1byte(p_dm, ODM_REG_NHM_TH8_11N);                     /* Reg0xe28, u8 */
		}
	}

	/* 3 Restore Default setting */
	else if (setting == RESTORE_DEFAULT_NHM_SETTING) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("RESTORE_DEFAULT_NHM_SETTING\n"));

		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {  /* store reg0x990, reg0x994, reg0x998, reg0x99c, Reg0x9a0 */
			odm_write_4byte(p_dm, ODM_REG_CCX_PERIOD_11AC,          p_acs->reg0x990);
			odm_write_4byte(p_dm, ODM_REG_NHM_TH9_TH10_11AC,     p_acs->reg0x994);
			odm_write_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, p_acs->reg0x998);
			odm_write_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, p_acs->reg0x99c);
			odm_write_1byte(p_dm, ODM_REG_NHM_TH8_11AC,             p_acs->reg0x9a0);
		} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
			odm_write_4byte(p_dm, ODM_REG_NHM_TH9_TH10_11N,     p_acs->reg0x890);
			odm_write_4byte(p_dm, ODM_REG_CCX_PERIOD_11AC,          p_acs->reg0x894);
			odm_write_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, p_acs->reg0x898);
			odm_write_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, p_acs->reg0x89c);
			odm_write_1byte(p_dm, ODM_REG_NHM_TH8_11N,             p_acs->reg0xe28);
		}
	}

	/* 3 struct _ACS_ setting */
	else if (setting == ACS_NHM_SETTING) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("ACS_NHM_SETTING\n"));
		u16  period;
		period = 0x61a8;
		p_acs->acs_step = acs_step;

		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
			/* 4 Set NHM period, 0x990[31:16]=0x61a8, Time duration for NHM unit: 4us, 0x61a8=100ms */
			odm_write_2byte(p_dm, ODM_REG_CCX_PERIOD_11AC + 2, period);
			/* 4 Set NHM ignore_cca=1, ignore_txon=1, ccx_en=0 */
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11AC, BIT(8) | BIT(9) | BIT(10), 3);

			if (p_acs->acs_step == 0) {
				/* 4 Set IGI */
				odm_set_bb_reg(p_dm, 0xc50, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x3E);
				if (get_rf_mimo_mode(priv) != RF_1T1R)
					odm_set_bb_reg(p_dm, 0xe50, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x3E);

				/* 4 Set struct _ACS_ NHM threshold */
				odm_write_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, 0x82786e64);
				odm_write_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, 0xffffff8c);
				odm_write_1byte(p_dm, ODM_REG_NHM_TH8_11AC, 0xff);
				odm_write_2byte(p_dm, ODM_REG_NHM_TH9_TH10_11AC + 2, 0xffff);

			} else if (p_acs->acs_step == 1) {
				/* 4 Set IGI */
				odm_set_bb_reg(p_dm, 0xc50, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x2A);
				if (get_rf_mimo_mode(priv) != RF_1T1R)
					odm_set_bb_reg(p_dm, 0xe50, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x2A);

				/* 4 Set struct _ACS_ NHM threshold */
				odm_write_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11AC, 0x5a50463c);
				odm_write_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11AC, 0xffffff64);

			}

		} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
			/* 4 Set NHM period, 0x894[31:16]=0x61a8, Time duration for NHM unit: 4us, 0x61a8=100ms */
			odm_write_2byte(p_dm, ODM_REG_CCX_PERIOD_11AC + 2, period);
			/* 4 Set NHM ignore_cca=1, ignore_txon=1, ccx_en=0 */
			odm_set_bb_reg(p_dm, ODM_REG_NHM_TH9_TH10_11N, BIT(8) | BIT(9) | BIT(10), 3);

			if (p_acs->acs_step == 0) {
				/* 4 Set IGI */
				odm_set_bb_reg(p_dm, 0xc50, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x3E);
				if (get_rf_mimo_mode(priv) != RF_1T1R)
					odm_set_bb_reg(p_dm, 0xc58, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x3E);

				/* 4 Set struct _ACS_ NHM threshold */
				odm_write_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, 0x82786e64);
				odm_write_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, 0xffffff8c);
				odm_write_1byte(p_dm, ODM_REG_NHM_TH8_11N, 0xff);
				odm_write_2byte(p_dm, ODM_REG_NHM_TH9_TH10_11N + 2, 0xffff);

			} else if (p_acs->acs_step == 1) {
				/* 4 Set IGI */
				odm_set_bb_reg(p_dm, 0xc50, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x2A);
				if (get_rf_mimo_mode(priv) != RF_1T1R)
					odm_set_bb_reg(p_dm, 0xc58, BIT(0) | BIT(1) | BIT(2) | BIT(3) | BIT(4) | BIT(5) | BIT(6), 0x2A);

				/* 4 Set struct _ACS_ NHM threshold */
				odm_write_4byte(p_dm, ODM_REG_NHM_TH3_TO_TH0_11N, 0x5a50463c);
				odm_write_4byte(p_dm, ODM_REG_NHM_TH7_TO_TH4_11N, 0xffffff64);

			}
		}
	}

}

void
phydm_get_nhm_statistics_ap(
	void       *p_dm_void,
	u32      idx,                /* @ 2G, Real channel number = idx+1 */
	u32      acs_step
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct rtl8192cd_priv     *priv    = p_dm->priv;
	struct _ACS_                  *p_acs    = &p_dm->dm_acs;
	u32                value32 = 0;
	u8                i;

	p_acs->acs_step = acs_step;

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		/* 4 Check if NHM result is ready */
		for (i = 0; i < 20; i++) {

			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm, REG_FPGA0_PSD_REPORT, BIT(17)))
				break;
		}

		/* 4 Get NHM Statistics */
		if (p_acs->acs_step == 1) {

			value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT7_TO_CNT4_11N);

			p_acs->nhm_cnt[idx][9] = (value32 & MASKBYTE1) >> 8;
			p_acs->nhm_cnt[idx][8] = (value32 & MASKBYTE0);

			value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT_11N);   /* ODM_REG_NHM_CNT3_TO_CNT0_11N */

			p_acs->nhm_cnt[idx][7] = (value32 & MASKBYTE3) >> 24;
			p_acs->nhm_cnt[idx][6] = (value32 & MASKBYTE2) >> 16;
			p_acs->nhm_cnt[idx][5] = (value32 & MASKBYTE1) >> 8;

		} else if (p_acs->acs_step == 2) {

			value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT_11N);  /* ODM_REG_NHM_CNT3_TO_CNT0_11N */

			p_acs->nhm_cnt[idx][4] = odm_read_1byte(p_dm, ODM_REG_NHM_CNT7_TO_CNT4_11N);
			p_acs->nhm_cnt[idx][3] = (value32 & MASKBYTE3) >> 24;
			p_acs->nhm_cnt[idx][2] = (value32 & MASKBYTE2) >> 16;
			p_acs->nhm_cnt[idx][1] = (value32 & MASKBYTE1) >> 8;
			p_acs->nhm_cnt[idx][0] = (value32 & MASKBYTE0);
		}
	} else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		/* 4 Check if NHM result is ready */
		for (i = 0; i < 20; i++) {

			ODM_delay_ms(1);
			if (odm_get_bb_reg(p_dm, ODM_REG_NHM_DUR_READY_11AC, BIT(16)))
				break;
		}

		if (p_acs->acs_step == 1) {

			value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT7_TO_CNT4_11AC);

			p_acs->nhm_cnt[idx][9] = (value32 & MASKBYTE1) >> 8;
			p_acs->nhm_cnt[idx][8] = (value32 & MASKBYTE0);

			value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT_11AC);    /* ODM_REG_NHM_CNT3_TO_CNT0_11AC */

			p_acs->nhm_cnt[idx][7] = (value32 & MASKBYTE3) >> 24;
			p_acs->nhm_cnt[idx][6] = (value32 & MASKBYTE2) >> 16;
			p_acs->nhm_cnt[idx][5] = (value32 & MASKBYTE1) >> 8;

		} else if (p_acs->acs_step == 2) {

			value32 = odm_read_4byte(p_dm, ODM_REG_NHM_CNT_11AC);     /* ODM_REG_NHM_CNT3_TO_CNT0_11AC */

			p_acs->nhm_cnt[idx][4] = odm_read_1byte(p_dm, ODM_REG_NHM_CNT7_TO_CNT4_11AC);
			p_acs->nhm_cnt[idx][3] = (value32 & MASKBYTE3) >> 24;
			p_acs->nhm_cnt[idx][2] = (value32 & MASKBYTE2) >> 16;
			p_acs->nhm_cnt[idx][1] = (value32 & MASKBYTE1) >> 8;
			p_acs->nhm_cnt[idx][0] = (value32 & MASKBYTE0);
		}
	}

}


/* #define ACS_DEBUG_INFO */ /* acs debug default off */
#if 0
int phydm_AutoChannelSelectAP(
	void   *p_dm_void,
	u32  ACS_Type,                      /*  0: RXCount_Type, 1:NHM_Type */
	u32  available_chnl_num        /*  amount of all channels */
)
{
	struct PHY_DM_STRUCT               *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ACS_                    *p_acs    = &p_dm->dm_acs;
	struct rtl8192cd_priv			*priv    = p_dm->priv;

	static u32           score2G[MAX_2G_CHANNEL_NUM], score5G[MAX_5G_CHANNEL_NUM];
	u32                  score[MAX_BSS_NUM], use_nhm = 0;
	u32                  minScore = 0xffffffff;
	u32                  tmpScore, tmpIdx = 0;
	u32                  traffic_check = 0;
	u32                  fa_count_weighting = 1;
	int                     i, j, idx = 0, idx_2G_end = -1, idx_5G_begin = -1, minChan = 0;
	struct bss_desc *pBss = NULL;

#ifdef _DEBUG_RTL8192CD_
	char tmpbuf[400];
	int len = 0;
#endif

	memset(score2G, '\0', sizeof(score2G));
	memset(score5G, '\0', sizeof(score5G));

	for (i = 0; i < priv->available_chnl_num; i++) {
		if (priv->available_chnl[i] <= 14)
			idx_2G_end = i;
		else
			break;
	}

	for (i = 0; i < priv->available_chnl_num; i++) {
		if (priv->available_chnl[i] > 14) {
			idx_5G_begin = i;
			break;
		}
	}

	/*  DELETE */
#ifndef CONFIG_RTL_NEW_AUTOCH
	for (i = 0; i < priv->site_survey->count; i++) {
		pBss = &priv->site_survey->bss[i];
		for (idx = 0; idx < priv->available_chnl_num; idx++) {
			if (pBss->channel == priv->available_chnl[idx]) {
				if (pBss->channel <= 14)
					setChannelScore(idx, score2G, 0, MAX_2G_CHANNEL_NUM - 1);
				else
					score5G[idx - idx_5G_begin] += 5;
				break;
			}
		}
	}
#endif

	if (idx_2G_end >= 0)
		for (i = 0; i <= idx_2G_end; i++)
			score[i] = score2G[i];
	if (idx_5G_begin >= 0)
		for (i = idx_5G_begin; i < priv->available_chnl_num; i++)
			score[i] = score5G[i - idx_5G_begin];

#ifdef CONFIG_RTL_NEW_AUTOCH
	{
		u32 y, ch_begin = 0, ch_end = priv->available_chnl_num;

		u32 do_ap_check = 1, ap_ratio = 0;

		if (idx_2G_end >= 0)
			ch_end = idx_2G_end + 1;
		if (idx_5G_begin >= 0)
			ch_begin = idx_5G_begin;

#ifdef ACS_DEBUG_INFO/* for debug */
		printk("\n");
		for (y = ch_begin; y < ch_end; y++)
			printk("1. init: chnl[%d] 20M_rx[%d] 40M_rx[%d] fa_cnt[%d] score[%d]\n",
			       priv->available_chnl[y],
			       priv->chnl_ss_mac_rx_count[y],
			       priv->chnl_ss_mac_rx_count_40M[y],
			       priv->chnl_ss_fa_count[y],
			       score[y]);
		printk("\n");
#endif

#if defined(CONFIG_RTL_88E_SUPPORT) || defined(CONFIG_WLAN_HAL_8192EE)
		if (p_dm->support_ic_type & (ODM_RTL8188E | ODM_RTL8192E) && priv->pmib->dot11RFEntry.acs_type) {
			u32 tmp_score[MAX_BSS_NUM];
			memcpy(tmp_score, score, sizeof(score));
			if (find_clean_channel(priv, ch_begin, ch_end, tmp_score)) {
				/* memcpy(score, tmp_score, sizeof(score)); */
#ifdef _DEBUG_RTL8192CD_
				printk("!! Found clean channel, select minimum FA channel\n");
#endif
				goto USE_CLN_CH;
			}
#ifdef _DEBUG_RTL8192CD_
			printk("!! Not found clean channel, use NHM algorithm\n");
#endif
			use_nhm = 1;
USE_CLN_CH:
			for (y = ch_begin; y < ch_end; y++) {
				for (i = 0; i <= 9; i++) {
					u32 val32 = priv->nhm_cnt[y][i];
					for (j = 0; j < i; j++)
						val32 *= 3;
					score[y] += val32;
				}

#ifdef _DEBUG_RTL8192CD_
				printk("nhm_cnt_%d: H<-[ %3d %3d %3d %3d %3d %3d %3d %3d %3d %3d]->L, score: %d\n",
				       y + 1, priv->nhm_cnt[y][9], priv->nhm_cnt[y][8], priv->nhm_cnt[y][7],
				       priv->nhm_cnt[y][6], priv->nhm_cnt[y][5], priv->nhm_cnt[y][4],
				       priv->nhm_cnt[y][3], priv->nhm_cnt[y][2], priv->nhm_cnt[y][1],
				       priv->nhm_cnt[y][0], score[y]);
#endif
			}

			if (!use_nhm)
				memcpy(score, tmp_score, sizeof(score));

			goto choose_ch;
		}
#endif

		/*  For each channel, weighting behind channels with MAC RX counter */
		/* For each channel, weighting the channel with FA counter */

		for (y = ch_begin; y < ch_end; y++) {
			score[y] += 8 * priv->chnl_ss_mac_rx_count[y];
			if (priv->chnl_ss_mac_rx_count[y] > 30)
				do_ap_check = 0;
			if (priv->chnl_ss_mac_rx_count[y] > MAC_RX_COUNT_THRESHOLD)
				traffic_check = 1;

#ifdef RTK_5G_SUPPORT
			if (*p_dm->p_band_type == ODM_BAND_2_4G)
#endif
			{
				if ((int)(y - 4) >= (int)ch_begin)
					score[y - 4] += 2 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y - 3) >= (int)ch_begin)
					score[y - 3] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y - 2) >= (int)ch_begin)
					score[y - 2] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y - 1) >= (int)ch_begin)
					score[y - 1] += 10 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y + 1) < (int)ch_end)
					score[y + 1] += 10 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y + 2) < (int)ch_end)
					score[y + 2] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y + 3) < (int)ch_end)
					score[y + 3] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y + 4) < (int)ch_end)
					score[y + 4] += 2 * priv->chnl_ss_mac_rx_count[y];
			}

			/* this is for CH_LOAD caculation */
			if (priv->chnl_ss_cca_count[y] > priv->chnl_ss_fa_count[y])
				priv->chnl_ss_cca_count[y] -= priv->chnl_ss_fa_count[y];
			else
				priv->chnl_ss_cca_count[y] = 0;
		}

#ifdef ACS_DEBUG_INFO/* for debug */
		printk("\n");
		for (y = ch_begin; y < ch_end; y++)
			printk("2. after 20M check: chnl[%d] score[%d]\n", priv->available_chnl[y], score[y]);
		printk("\n");
#endif

		for (y = ch_begin; y < ch_end; y++) {
			if (priv->chnl_ss_mac_rx_count_40M[y]) {
				score[y] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
				if (priv->chnl_ss_mac_rx_count_40M[y] > 30)
					do_ap_check = 0;
				if (priv->chnl_ss_mac_rx_count_40M[y] > MAC_RX_COUNT_THRESHOLD)
					traffic_check = 1;

#ifdef RTK_5G_SUPPORT
				if (*p_dm->p_band_type == ODM_BAND_2_4G)
#endif
				{
					if ((int)(y - 6) >= (int)ch_begin)
						score[y - 6] += 1 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y - 5) >= (int)ch_begin)
						score[y - 5] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y - 4) >= (int)ch_begin)
						score[y - 4] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y - 3) >= (int)ch_begin)
						score[y - 3] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y - 2) >= (int)ch_begin)
						score[y - 2] += (5 * priv->chnl_ss_mac_rx_count_40M[y]) / 2;
					if ((int)(y - 1) >= (int)ch_begin)
						score[y - 1] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y + 1) < (int)ch_end)
						score[y + 1] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y + 2) < (int)ch_end)
						score[y + 2] += (5 * priv->chnl_ss_mac_rx_count_40M[y]) / 2;
					if ((int)(y + 3) < (int)ch_end)
						score[y + 3] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y + 4) < (int)ch_end)
						score[y + 4] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y + 5) < (int)ch_end)
						score[y + 5] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y + 6) < (int)ch_end)
						score[y + 6] += 1 * priv->chnl_ss_mac_rx_count_40M[y];
				}
			}
		}

#ifdef ACS_DEBUG_INFO/* for debug */
		printk("\n");
		for (y = ch_begin; y < ch_end; y++)
			printk("3. after 40M check: chnl[%d] score[%d]\n", priv->available_chnl[y], score[y]);
		printk("\n");
		printk("4. do_ap_check=%d traffic_check=%d\n", do_ap_check, traffic_check);
		printk("\n");
#endif

		if (traffic_check == 0)
			fa_count_weighting = 5;
		else
			fa_count_weighting = 1;

		for (y = ch_begin; y < ch_end; y++)
			score[y] += fa_count_weighting * priv->chnl_ss_fa_count[y];

#ifdef ACS_DEBUG_INFO/* for debug */
		printk("\n");
		for (y = ch_begin; y < ch_end; y++)
			printk("5. after fa check: chnl[%d] score[%d]\n", priv->available_chnl[y], score[y]);
		printk("\n");
#endif

		if (do_ap_check) {
			for (i = 0; i < priv->site_survey->count; i++) {
				pBss = &priv->site_survey->bss[i];
				for (y = ch_begin; y < ch_end; y++) {
					if (pBss->channel == priv->available_chnl[y]) {
						if (pBss->channel <= 14) {
#ifdef ACS_DEBUG_INFO/* for debug */
							printk("\n");
							printk("chnl[%d] has ap rssi=%d bw[0x%02x]\n",
							       pBss->channel, pBss->rssi, pBss->t_stamp[1]);
							printk("\n");
#endif
							if (pBss->rssi > 60)
								ap_ratio = 4;
							else if (pBss->rssi > 35)
								ap_ratio = 2;
							else
								ap_ratio = 1;

							if ((pBss->t_stamp[1] & 0x6) == 0) {
								score[y] += 50 * ap_ratio;
								if ((int)(y - 4) >= (int)ch_begin)
									score[y - 4] += 10 * ap_ratio;
								if ((int)(y - 3) >= (int)ch_begin)
									score[y - 3] += 20 * ap_ratio;
								if ((int)(y - 2) >= (int)ch_begin)
									score[y - 2] += 30 * ap_ratio;
								if ((int)(y - 1) >= (int)ch_begin)
									score[y - 1] += 40 * ap_ratio;
								if ((int)(y + 1) < (int)ch_end)
									score[y + 1] += 40 * ap_ratio;
								if ((int)(y + 2) < (int)ch_end)
									score[y + 2] += 30 * ap_ratio;
								if ((int)(y + 3) < (int)ch_end)
									score[y + 3] += 20 * ap_ratio;
								if ((int)(y + 4) < (int)ch_end)
									score[y + 4] += 10 * ap_ratio;
							} else if ((pBss->t_stamp[1] & 0x4) == 0) {
								score[y] += 50 * ap_ratio;
								if ((int)(y - 3) >= (int)ch_begin)
									score[y - 3] += 20 * ap_ratio;
								if ((int)(y - 2) >= (int)ch_begin)
									score[y - 2] += 30 * ap_ratio;
								if ((int)(y - 1) >= (int)ch_begin)
									score[y - 1] += 40 * ap_ratio;
								if ((int)(y + 1) < (int)ch_end)
									score[y + 1] += 50 * ap_ratio;
								if ((int)(y + 2) < (int)ch_end)
									score[y + 2] += 50 * ap_ratio;
								if ((int)(y + 3) < (int)ch_end)
									score[y + 3] += 50 * ap_ratio;
								if ((int)(y + 4) < (int)ch_end)
									score[y + 4] += 50 * ap_ratio;
								if ((int)(y + 5) < (int)ch_end)
									score[y + 5] += 40 * ap_ratio;
								if ((int)(y + 6) < (int)ch_end)
									score[y + 6] += 30 * ap_ratio;
								if ((int)(y + 7) < (int)ch_end)
									score[y + 7] += 20 * ap_ratio;
							} else {
								score[y] += 50 * ap_ratio;
								if ((int)(y - 7) >= (int)ch_begin)
									score[y - 7] += 20 * ap_ratio;
								if ((int)(y - 6) >= (int)ch_begin)
									score[y - 6] += 30 * ap_ratio;
								if ((int)(y - 5) >= (int)ch_begin)
									score[y - 5] += 40 * ap_ratio;
								if ((int)(y - 4) >= (int)ch_begin)
									score[y - 4] += 50 * ap_ratio;
								if ((int)(y - 3) >= (int)ch_begin)
									score[y - 3] += 50 * ap_ratio;
								if ((int)(y - 2) >= (int)ch_begin)
									score[y - 2] += 50 * ap_ratio;
								if ((int)(y - 1) >= (int)ch_begin)
									score[y - 1] += 50 * ap_ratio;
								if ((int)(y + 1) < (int)ch_end)
									score[y + 1] += 40 * ap_ratio;
								if ((int)(y + 2) < (int)ch_end)
									score[y + 2] += 30 * ap_ratio;
								if ((int)(y + 3) < (int)ch_end)
									score[y + 3] += 20 * ap_ratio;
							}
						} else {
							if ((pBss->t_stamp[1] & 0x6) == 0)
								score[y] += 500;
							else if ((pBss->t_stamp[1] & 0x4) == 0) {
								score[y] += 500;
								if ((int)(y + 1) < (int)ch_end)
									score[y + 1] += 500;
							} else {
								score[y] += 500;
								if ((int)(y - 1) >= (int)ch_begin)
									score[y - 1] += 500;
							}
						}
						break;
					}
				}
			}
		}

#ifdef ACS_DEBUG_INFO/* for debug */
		printk("\n");
		for (y = ch_begin; y < ch_end; y++)
			printk("6. after ap check: chnl[%d]:%d\n", priv->available_chnl[y], score[y]);
		printk("\n");
#endif

#ifdef SS_CH_LOAD_PROC

		/*  caculate noise level -- suggested by wilson */
		for (y = ch_begin; y < ch_end; y++)  {
			int fa_lv = 0, cca_lv = 0;
			if (priv->chnl_ss_fa_count[y] > 1000)
				fa_lv = 100;
			else if (priv->chnl_ss_fa_count[y] > 500)
				fa_lv = 34 * (priv->chnl_ss_fa_count[y] - 500) / 500 + 66;
			else if (priv->chnl_ss_fa_count[y] > 200)
				fa_lv = 33 * (priv->chnl_ss_fa_count[y] - 200) / 300 + 33;
			else if (priv->chnl_ss_fa_count[y] > 100)
				fa_lv = 18 * (priv->chnl_ss_fa_count[y] - 100) / 100 + 15;
			else
				fa_lv = 15 * priv->chnl_ss_fa_count[y] / 100;
			if (priv->chnl_ss_cca_count[y] > 400)
				cca_lv = 100;
			else if (priv->chnl_ss_cca_count[y] > 200)
				cca_lv = 34 * (priv->chnl_ss_cca_count[y] - 200) / 200 + 66;
			else if (priv->chnl_ss_cca_count[y] > 80)
				cca_lv = 33 * (priv->chnl_ss_cca_count[y] - 80) / 120 + 33;
			else if (priv->chnl_ss_cca_count[y] > 40)
				cca_lv = 18 * (priv->chnl_ss_cca_count[y] - 40) / 40 + 15;
			else
				cca_lv = 15 * priv->chnl_ss_cca_count[y] / 40;

			priv->chnl_ss_load[y] = (((fa_lv > cca_lv) ? fa_lv : cca_lv) * 75 + ((score[y] > 100) ? 100 : score[y]) * 25) / 100;

			DEBUG_INFO("ch:%d f=%d (%d), c=%d (%d), fl=%d, cl=%d, sc=%d, cu=%d\n",
				   priv->available_chnl[y],
				   priv->chnl_ss_fa_count[y], fa_thd,
				   priv->chnl_ss_cca_count[y], cca_thd,
				   fa_lv,
				   cca_lv,
				   score[y],
				   priv->chnl_ss_load[y]);

		}
#endif
	}
#endif

choose_ch:

#ifdef DFS
	/*  heavy weighted DFS channel */
	if (idx_5G_begin >= 0) {
		for (i = idx_5G_begin; i < priv->available_chnl_num; i++) {
			if (!priv->pmib->dot11DFSEntry.disable_DFS && is_DFS_channel(priv->available_chnl[i])
			    && (score[i] != 0xffffffff))
				score[i] += 1600;
		}
	}
#endif


	/* prevent Auto channel selecting wrong channel in 40M mode----------------- */
	if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N)
	    && priv->pshare->is_40m_bw) {
#if 0
		if (GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset == 1) {
			/* Upper Primary channel, cannot select the two lowest channels */
			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11G) {
				score[0] = 0xffffffff;
				score[1] = 0xffffffff;
				score[2] = 0xffffffff;
				score[3] = 0xffffffff;
				score[4] = 0xffffffff;

				score[13] = 0xffffffff;
				score[12] = 0xffffffff;
				score[11] = 0xffffffff;
			}

			/*			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11A) { */
			/*				score[idx_5G_begin] = 0xffffffff; */
			/*				score[idx_5G_begin + 1] = 0xffffffff; */
			/*			} */
		} else if (GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset == 2) {
			/* Lower Primary channel, cannot select the two highest channels */
			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11G) {
				score[0] = 0xffffffff;
				score[1] = 0xffffffff;
				score[2] = 0xffffffff;

				score[13] = 0xffffffff;
				score[12] = 0xffffffff;
				score[11] = 0xffffffff;
				score[10] = 0xffffffff;
				score[9] = 0xffffffff;
			}

			/*			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11A) { */
			/*				score[priv->available_chnl_num - 2] = 0xffffffff; */
			/*				score[priv->available_chnl_num - 1] = 0xffffffff; */
			/*			} */
		}
#endif
		for (i = 0; i <= idx_2G_end; ++i)
			if (priv->available_chnl[i] == 14)
				score[i] = 0xffffffff;		/*  mask chan14 */

#ifdef RTK_5G_SUPPORT
		if (idx_5G_begin >= 0) {
			for (i = idx_5G_begin; i < priv->available_chnl_num; i++) {
				int ch = priv->available_chnl[i];
				if (priv->available_chnl[i] > 144)
					--ch;
				if ((ch % 4) || ch == 140 || ch == 164)	/* mask ch 140, ch 165, ch 184... */
					score[i] = 0xffffffff;
			}
		}
#endif


	}

	if (priv->pmib->dot11RFEntry.disable_ch1213) {
		for (i = 0; i <= idx_2G_end; ++i) {
			int ch = priv->available_chnl[i];
			if ((ch == 12) || (ch == 13))
				score[i] = 0xffffffff;
		}
	}

	if (((priv->pmib->dot11StationConfigEntry.dot11RegDomain == DOMAIN_GLOBAL) ||
	     (priv->pmib->dot11StationConfigEntry.dot11RegDomain == DOMAIN_WORLD_WIDE)) &&
	    (idx_2G_end >= 11) && (idx_2G_end < 14)) {
		score[13] = 0xffffffff;	/*  mask chan14 */
		score[12] = 0xffffffff; /*  mask chan13 */
		score[11] = 0xffffffff; /*  mask chan12 */
	}

	/* ------------------------------------------------------------------ */

#ifdef _DEBUG_RTL8192CD_
	for (i = 0; i < priv->available_chnl_num; i++)
		len += sprintf(tmpbuf + len, "ch%d:%u ", priv->available_chnl[i], score[i]);
	strcat(tmpbuf, "\n");
	panic_printk("%s", tmpbuf);

#endif

	if ((*p_dm->p_band_type == ODM_BAND_5G)
	    && (priv->pmib->dot11nConfigEntry.dot11nUse40M == CHANNEL_WIDTH_80)) {
		for (i = 0; i < priv->available_chnl_num; i++) {
			if (is80MChannel(priv->available_chnl, priv->available_chnl_num, priv->available_chnl[i])) {
				tmpScore = 0;
				for (j = 0; j < 4; j++) {
					if ((tmpScore != 0xffffffff) && (score[i + j] != 0xffffffff))
						tmpScore += score[i + j];
					else
						tmpScore = 0xffffffff;
				}
				tmpScore = tmpScore / 4;
				if (minScore > tmpScore) {
					minScore = tmpScore;

					tmpScore = 0xffffffff;
					for (j = 0; j < 4; j++) {
						if (score[i + j] < tmpScore) {
							tmpScore = score[i + j];
							tmpIdx = i + j;
						}
					}

					idx = tmpIdx;
				}
				i += 3;
			}
		}
		if (minScore == 0xffffffff) {
			/*  there is no 80M channels */
			priv->pshare->is_40m_bw = CHANNEL_WIDTH_20;
			for (i = 0; i < priv->available_chnl_num; i++) {
				if (score[i] < minScore) {
					minScore = score[i];
					idx = i;
				}
			}
		}
	} else if ((*p_dm->p_band_type == ODM_BAND_5G)
		&& (priv->pmib->dot11nConfigEntry.dot11nUse40M == CHANNEL_WIDTH_40)) {
		for (i = 0; i < priv->available_chnl_num; i++) {
			if (is40MChannel(priv->available_chnl, priv->available_chnl_num, priv->available_chnl[i])) {
				tmpScore = 0;
				for (j = 0; j < 2; j++) {
					if ((tmpScore != 0xffffffff) && (score[i + j] != 0xffffffff))
						tmpScore += score[i + j];
					else
						tmpScore = 0xffffffff;
				}
				tmpScore = tmpScore / 2;
				if (minScore > tmpScore) {
					minScore = tmpScore;

					tmpScore = 0xffffffff;
					for (j = 0; j < 2; j++) {
						if (score[i + j] < tmpScore) {
							tmpScore = score[i + j];
							tmpIdx = i + j;
						}
					}

					idx = tmpIdx;
				}
				i += 1;
			}
		}
		if (minScore == 0xffffffff) {
			/*  there is no 40M channels */
			priv->pshare->is_40m_bw = CHANNEL_WIDTH_20;
			for (i = 0; i < priv->available_chnl_num; i++) {
				if (score[i] < minScore) {
					minScore = score[i];
					idx = i;
				}
			}
		}
	} else if ((*p_dm->p_band_type == ODM_BAND_2_4G)
		&& (priv->pmib->dot11nConfigEntry.dot11nUse40M == CHANNEL_WIDTH_40)
		   && (priv->available_chnl_num >= 8)) {
		u32 groupScore[14];

		memset(groupScore, 0xff, sizeof(groupScore));
		for (i = 0; i < priv->available_chnl_num - 4; i++) {
			if (score[i] != 0xffffffff && score[i + 4] != 0xffffffff) {
				groupScore[i] = score[i] + score[i + 4];
				DEBUG_INFO("groupScore, ch %d,%d: %d\n", i + 1, i + 5, groupScore[i]);
				if (groupScore[i] < minScore) {
#ifdef AUTOCH_SS_SPEEDUP
					if (priv->pmib->miscEntry.autoch_1611_enable) {
						if (priv->available_chnl[i] == 1 || priv->available_chnl[i] == 6 || priv->available_chnl[i] == 11) {
							minScore = groupScore[i];
							idx = i;
						}
					} else
#endif
					{
						minScore = groupScore[i];
						idx = i;
					}
				}
			}
		}

		if (score[idx] < score[idx + 4]) {
			GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
			priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;
		} else {
			idx = idx + 4;
			GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
			priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;
		}
	} else {
		for (i = 0; i < priv->available_chnl_num; i++) {
			if (score[i] < minScore) {
#ifdef AUTOCH_SS_SPEEDUP
				if (priv->pmib->miscEntry.autoch_1611_enable) {
					if (priv->available_chnl[i] == 1 || priv->available_chnl[i] == 6 || priv->available_chnl[i] == 11) {
						minScore = score[i];
						idx = i;
					}
				} else
#endif
				{
					minScore = score[i];
					idx = i;
				}
			}
		}
	}

	if (IS_A_CUT_8881A(priv) &&
	    (priv->pmib->dot11nConfigEntry.dot11nUse40M == CHANNEL_WIDTH_80)) {
		if ((priv->available_chnl[idx] == 36) ||
		    (priv->available_chnl[idx] == 52) ||
		    (priv->available_chnl[idx] == 100) ||
		    (priv->available_chnl[idx] == 116) ||
		    (priv->available_chnl[idx] == 132) ||
		    (priv->available_chnl[idx] == 149) ||
		    (priv->available_chnl[idx] == 165))
			idx++;
		else if ((priv->available_chnl[idx] == 48) ||
			 (priv->available_chnl[idx] == 64) ||
			 (priv->available_chnl[idx] == 112) ||
			 (priv->available_chnl[idx] == 128) ||
			 (priv->available_chnl[idx] == 144) ||
			 (priv->available_chnl[idx] == 161) ||
			 (priv->available_chnl[idx] == 177))
			idx--;
	}

	minChan = priv->available_chnl[idx];

	/*  skip channel 14 if don't support ofdm */
	if ((priv->pmib->dot11RFEntry.disable_ch14_ofdm) &&
	    (minChan == 14)) {
		score[idx] = 0xffffffff;

		minScore = 0xffffffff;
		for (i = 0; i < priv->available_chnl_num; i++) {
			if (score[i] < minScore) {
				minScore = score[i];
				idx = i;
			}
		}
		minChan = priv->available_chnl[idx];
	}

#if 0
	/* Check if selected channel available for 80M/40M BW or NOT ? */
	if (*p_dm->p_band_type == ODM_BAND_5G) {
		if (priv->pmib->dot11nConfigEntry.dot11nUse40M == CHANNEL_WIDTH_80) {
			if (!is80MChannel(priv->available_chnl, priv->available_chnl_num, minChan)) {

				/* priv->pmib->dot11n_config_entry.dot11nUse40M = CHANNEL_WIDTH_40; */
				priv->pshare->is_40m_bw = CHANNEL_WIDTH_40;
			}
		}

		if (priv->pmib->dot11nConfigEntry.dot11nUse40M == CHANNEL_WIDTH_40) {
			if (!is40MChannel(priv->available_chnl, priv->available_chnl_num, minChan)) {

				/* priv->pmib->dot11n_config_entry.dot11nUse40M = CHANNEL_WIDTH_20; */
				priv->pshare->is_40m_bw = CHANNEL_WIDTH_20;
			}
		}
	}
#endif

#ifdef CONFIG_RTL_NEW_AUTOCH
	RTL_W32(RXERR_RPT, RXERR_RPT_RST);
#endif

	/*  auto adjust contro-sideband */
	if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N)
	    && (priv->pshare->is_40m_bw == 1 || priv->pshare->is_40m_bw == 2)) {

#ifdef RTK_5G_SUPPORT
		if (*p_dm->p_band_type == ODM_BAND_5G) {
			if ((minChan > 144) ? ((minChan - 1) % 8) : (minChan % 8)) {
				GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
				priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;
			} else {
				GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
				priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;
			}

		} else
#endif
		{
#if 0
#ifdef CONFIG_RTL_NEW_AUTOCH
			unsigned int ch_max;

			if (priv->available_chnl[idx_2G_end] >= 13)
				ch_max = 13;
			else
				ch_max = priv->available_chnl[idx_2G_end];

			if ((minChan >= 5) && (minChan <= (ch_max - 5))) {
				if (score[minChan + 4] > score[minChan - 4]) { /*  what if some channels were cancelled? */
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;
				} else {
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;
				}
			} else
#endif
			{
				if (minChan < 5) {
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;
				} else if (minChan > 7) {
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;
				}
			}
#endif
		}
	}
	/* ----------------------- */

#if defined(__ECOS) && defined(CONFIG_SDIO_HCI)
	panic_printk("Auto channel choose ch:%d\n", minChan);
#else
#ifdef _DEBUG_RTL8192CD_
	panic_printk("Auto channel choose ch:%d\n", minChan);
#endif
#endif
#ifdef ACS_DEBUG_INFO/* for debug */
	printk("7. minChan:%d 2nd_offset:%d\n", minChan, priv->pshare->offset_2nd_chan);
#endif

	return minChan;
}
#endif

#endif

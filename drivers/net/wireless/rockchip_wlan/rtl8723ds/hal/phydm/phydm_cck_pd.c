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
 
#ifdef PHYDM_SUPPORT_CCKPD

void
phydm_write_cck_cca_th_new_cs_ratio(
	void			*p_dm_void,
	u8			cca_th,
	u8			cca_th_aaa
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cckpd_struct	*p_cckpd_t = &p_dm->dm_cckpd_table;

	PHYDM_DBG(p_dm, DBG_CCKPD, ("%s ======>\n", __func__));
	PHYDM_DBG(p_dm, DBG_CCKPD, ("[New] pd_th=0x%x, cs_ratio=0x%x\n\n", cca_th, cca_th_aaa));

	if (p_cckpd_t->cur_cck_cca_thres != cca_th) {
		
		p_cckpd_t->cur_cck_cca_thres = cca_th;
		odm_set_bb_reg(p_dm, 0xa08, 0xf0000, cca_th);
		p_cckpd_t->cck_fa_ma = CCK_FA_MA_RESET;
		
	}

	if (p_cckpd_t->cck_cca_th_aaa != cca_th_aaa) {
		
		p_cckpd_t->cck_cca_th_aaa = cca_th_aaa;
		odm_set_bb_reg(p_dm, 0xaa8, 0x1f0000, cca_th_aaa);
		p_cckpd_t->cck_fa_ma = CCK_FA_MA_RESET;
	}
	
}

void
phydm_write_cck_cca_th(
	void			*p_dm_void,
	u8			cca_th
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cckpd_struct	*p_cckpd_t = &p_dm->dm_cckpd_table;

	PHYDM_DBG(p_dm, DBG_CCKPD, ("%s ======>\n", __func__));
	PHYDM_DBG(p_dm, DBG_CCKPD, ("New cck_cca_th=((0x%x))\n\n", cca_th));

	if (p_cckpd_t->cur_cck_cca_thres != cca_th) {
		
		odm_write_1byte(p_dm, ODM_REG(CCK_CCA, p_dm), cca_th);
		p_cckpd_t->cck_fa_ma = CCK_FA_MA_RESET;
	}
	p_cckpd_t->cur_cck_cca_thres = cca_th;
}

void
phydm_set_cckpd_val(
	void			*p_dm_void,
	u32			*val_buf,
	u8			val_len
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;


	if (val_len != 2) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("[Error][CCKPD]Need val_len=2\n"));
		return;
	}
	
	/*val_buf[0]: 0xa0a*/
	/*val_buf[1]: 0xaaa*/
	
	if (p_dm->support_ic_type & EXTEND_CCK_CCATH_AAA_IC) {
		phydm_write_cck_cca_th_new_cs_ratio(p_dm, (u8)val_buf[0], (u8)val_buf[1]);
	} else {
		phydm_write_cck_cca_th(p_dm, (u8)val_buf[0]);
	}

}

boolean
phydm_stop_cck_pd_th(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (!(p_dm->support_ability & (ODM_BB_CCK_PD | ODM_BB_FA_CNT))) {
		
		PHYDM_DBG(p_dm, DBG_CCKPD, ("Not Support\n"));

		#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
		#ifdef MCR_WIRELESS_EXTEND
		phydm_write_cck_cca_th(p_dm, 0x43);
		#endif
		#endif
		
		return true;
	}

	if (p_dm->pause_ability & ODM_BB_CCK_PD) {
		
		PHYDM_DBG(p_dm, DBG_CCKPD, ("Return: Pause CCKPD in LV=%d\n", p_dm->pause_lv_table.lv_cckpd));
		return true;
	}

	#if 0/*(DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))*/
	if (p_dm->ext_lna)
		return true;
	#endif

	return false;
	
}

void
phydm_cckpd(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct phydm_cckpd_struct	*p_cckpd_t = &p_dm->dm_cckpd_table;
	u8	cur_cck_cca_th= p_cckpd_t->cur_cck_cca_thres;

	if (p_dm->is_linked) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))

		/*Add hp_hw_id condition due to 22B LPS power consumption issue and [PCIE-1596]*/
		if (p_dm->hp_hw_id && (p_dm->traffic_load == TRAFFIC_ULTRA_LOW))
			cur_cck_cca_th = 0x40;
		else if (p_dm->rssi_min > 35)
			cur_cck_cca_th = 0xcd;
		else if (p_dm->rssi_min > 20) {
			
			if (p_cckpd_t->cck_fa_ma > 500)
				cur_cck_cca_th = 0xcd;
			else if (p_cckpd_t->cck_fa_ma < 250)
				cur_cck_cca_th = 0x83;
			
		} else {

			if((p_dm->p_advance_ota & PHYDM_ASUS_OTA_SETTING) && (p_cckpd_t->cck_fa_ma > 200))
				cur_cck_cca_th = 0xc3; /*for ASUS OTA test*/
			else
				cur_cck_cca_th = 0x83;
		}
		
#else	/*ODM_AP*/
		if (p_dig_t->cur_ig_value > 0x32)
			cur_cck_cca_th = 0xed;
		else if (p_dig_t->cur_ig_value > 0x2a)
			cur_cck_cca_th = 0xdd;
		else if (p_dig_t->cur_ig_value > 0x24)
			cur_cck_cca_th = 0xcd;
		else 
			cur_cck_cca_th = 0x83;
		
#endif
	} else {
	
		if (p_cckpd_t->cck_fa_ma > 1000)
			cur_cck_cca_th = 0x83;
		else if (p_cckpd_t->cck_fa_ma < 500)
			cur_cck_cca_th = 0x40;
	}

	phydm_write_cck_cca_th(p_dm, cur_cck_cca_th);
	/*PHYDM_DBG(p_dm, DBG_CCKPD, ("New cck_cca_th=((0x%x))\n\n", cur_cck_cca_th));*/

}

void
phydm_cckpd_new_cs_ratio(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct phydm_cckpd_struct	*p_cckpd_t = &p_dm->dm_cckpd_table;
	u8	pd_th = 0, cs_ration = 0, cs_2r_offset = 0;
	u8	igi_curr = p_dig_t->cur_ig_value;
	u8	en_2rcca;

	PHYDM_DBG(p_dm, DBG_CCKPD, ("%s ======>\n", __func__));

	en_2rcca = (u8)(odm_get_bb_reg(p_dm, 0xa2c, BIT(18)) && odm_get_bb_reg(p_dm, 0xa2c, BIT(22)));

	if (p_dm->is_linked) {
		
		if ((igi_curr > 0x38) && (p_dm->rssi_min > 32)) {
			cs_ration = p_dig_t->aaa_default + AAA_BASE + AAA_STEP * 2;
			cs_2r_offset = 5;
			pd_th = 0xd;
		} else if ((igi_curr > 0x2a) && (p_dm->rssi_min > 32)) {
			cs_ration = p_dig_t->aaa_default + AAA_BASE + AAA_STEP;
			cs_2r_offset = 4;
			pd_th = 0xd;
		} else if ((igi_curr > 0x24) || (p_dm->rssi_min > 24 && p_dm->rssi_min <= 30)) {
			cs_ration = p_dig_t->aaa_default + AAA_BASE;
			cs_2r_offset = 3;
			pd_th = 0xd;
		} else if ((igi_curr <= 0x24) || (p_dm->rssi_min < 22)) {
			
			if (p_cckpd_t->cck_fa_ma > 1000) {
				cs_ration = p_dig_t->aaa_default + AAA_STEP;
				cs_2r_offset = 1;
				pd_th = 0x7;
			} else if (p_cckpd_t->cck_fa_ma < 500) {
				cs_ration = p_dig_t->aaa_default;
				pd_th = 0x3;
			} else {
				cs_ration = p_cckpd_t->cck_cca_th_aaa;
				pd_th = p_cckpd_t->cur_cck_cca_thres;
			}
		}
	} else {
	
		if (p_cckpd_t->cck_fa_ma > 1000) {
			cs_ration = p_dig_t->aaa_default + AAA_STEP;
			cs_2r_offset = 1;
			pd_th = 0x7;
		} else if (p_cckpd_t->cck_fa_ma < 500) {
			cs_ration = p_dig_t->aaa_default;
			pd_th = 0x3;
		} else {
			cs_ration = p_cckpd_t->cck_cca_th_aaa;
			pd_th = p_cckpd_t->cur_cck_cca_thres;
		}
	}
	
	if (en_2rcca)
		cs_ration = (cs_ration >= cs_2r_offset) ? (cs_ration - cs_2r_offset) : 0;

	p_cckpd_t->cur_cck_cca_thres = pd_th;
	p_cckpd_t->cck_cca_th_aaa = cs_ration;

	PHYDM_DBG(p_dm, DBG_CCKPD, 
	("[New] cs_ratio=0x%x, pd_th=0x%x\n", cs_ration, pd_th));

	odm_set_bb_reg(p_dm, 0xa08, 0xf0000, pd_th);
	odm_set_bb_reg(p_dm, 0xaa8, 0x1f0000, cs_ration);

	/*phydm_write_cck_cca_th_new_cs_ratio(p_dm, pd_th, cs_ration);*/
}

#endif

void
phydm_cck_pd_th(
	void		*p_dm_void
)
{
#ifdef PHYDM_SUPPORT_CCKPD
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_fa_struct		*p_fa_t= &p_dm->false_alm_cnt;
	struct phydm_cckpd_struct	*p_cckpd_t = &p_dm->dm_cckpd_table;
	u32	cnt_cck_fail_tmp = p_fa_t->cnt_cck_fail;
	#ifdef PHYDM_TDMA_DIG_SUPPORT
	struct phydm_fa_acc_struct	*p_fa_acc_t = &p_dm->false_alm_cnt_acc;
	#endif
	
	PHYDM_DBG(p_dm, DBG_CCKPD, ("%s ======>\n", __func__));

	if (phydm_stop_cck_pd_th(p_dm) == true)
		return;

#ifdef PHYDM_TDMA_DIG_SUPPORT
	cnt_cck_fail_tmp = (p_dm->original_dig_restore) ? (p_fa_t->cnt_cck_fail) : (p_fa_acc_t->cnt_cck_fail_1sec);
#endif
	
	if (p_cckpd_t->cck_fa_ma == CCK_FA_MA_RESET)
		p_cckpd_t->cck_fa_ma = cnt_cck_fail_tmp;
	else {
		p_cckpd_t->cck_fa_ma = ((p_cckpd_t->cck_fa_ma << 1) +
									p_cckpd_t->cck_fa_ma + cnt_cck_fail_tmp) >> 2;
	}
	
	PHYDM_DBG(p_dm, DBG_CCKPD, ("CCK FA=%d\n", p_cckpd_t->cck_fa_ma));

	if (p_dm->support_ic_type & EXTEND_CCK_CCATH_AAA_IC)
		phydm_cckpd_new_cs_ratio(p_dm);
	else
		phydm_cckpd(p_dm);
	
#endif
}

void
odm_pause_cck_packet_detection(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type,
	enum phydm_pause_level		pause_lv,
	u8					cck_pd_th
)
{
#ifdef PHYDM_SUPPORT_CCKPD
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cckpd_struct	*p_cckpd_t = &p_dm->dm_cckpd_table;
	s8	max_level;
	u8	i;

	PHYDM_DBG(p_dm, DBG_CCKPD, ("%s ======>\n", __func__));

	if ((p_cckpd_t->pause_bitmap == 0) &&
		(!(p_dm->support_ability & (ODM_BB_CCK_PD | ODM_BB_FA_CNT)))) {
		
		PHYDM_DBG(p_dm, DBG_CCKPD, ("Return: not support\n"));
		return;
	}

	if (pause_lv >= PHYDM_PAUSE_MAX_NUM) {
		PHYDM_DBG(p_dm, DBG_CCKPD, ("Return: Wrong LV !\n"));
		return;
	}
	PHYDM_DBG(p_dm, DBG_CCKPD, ("Set pause{Type, LV, val} = {%d, %d, 0x%x}\n", 
		pause_type, pause_lv, cck_pd_th));

	PHYDM_DBG(p_dm, DBG_CCKPD, ("pause LV=0x%x\n", p_cckpd_t->pause_bitmap));

	for (i = 0; i < PHYDM_PAUSE_MAX_NUM; i ++) {
		PHYDM_DBG(p_dm, DBG_CCKPD, ("pause val[%d]=0x%x\n", 
										i, p_cckpd_t->pause_cckpd_value[i]));
	}

	switch (pause_type) {

	case PHYDM_PAUSE:
	{
		/* Disable CCK PD */
		p_dm->support_ability &= ~ODM_BB_CCK_PD;
		
		PHYDM_DBG(p_dm, DBG_CCKPD, ("Pause CCK PD th\n"));

		/* Backup original CCK PD threshold decided by CCK PD mechanism */
		if (p_cckpd_t->pause_bitmap == 0) {
			
			p_cckpd_t->cckpd_bkp = p_cckpd_t->cur_cck_cca_thres;
			
			PHYDM_DBG(p_dm, DBG_CCKPD, ("cckpd_bkp=0x%x\n", 
				p_cckpd_t->cckpd_bkp));
		}

		p_cckpd_t->pause_bitmap |= BIT(pause_lv); /* Update pause level */
		p_cckpd_t->pause_cckpd_value[pause_lv] = cck_pd_th; 

		/* Write new CCK PD threshold */
		if (BIT(pause_lv + 1) > p_cckpd_t->pause_bitmap) {

			PHYDM_DBG(p_dm, DBG_CCKPD, ("> ori pause LV=0x%x\n", 
				p_cckpd_t->pause_bitmap));
			
			phydm_write_cck_cca_th(p_dm, cck_pd_th);
		}
		break;
	}
	case PHYDM_RESUME:
	{
		/* check if the level is illegal or not */
		if ((p_cckpd_t->pause_bitmap & (BIT(pause_lv))) != 0) {
			
			p_cckpd_t->pause_bitmap &= (~(BIT(pause_lv)));
			p_cckpd_t->pause_cckpd_value[pause_lv] = 0;
			PHYDM_DBG(p_dm, DBG_CCKPD, ("Resume CCK PD\n"));
		} else {
		
			PHYDM_DBG(p_dm, DBG_CCKPD, ("Wrong resume LV\n"));
			break;
		}

		/* Resume CCKPD */
		if (p_cckpd_t->pause_bitmap == 0) {
			
			PHYDM_DBG(p_dm, DBG_CCKPD,("Revert bkp_CCKPD=0x%x\n", 
														p_cckpd_t->cckpd_bkp));
			
			phydm_write_cck_cca_th(p_dm, p_cckpd_t->cckpd_bkp);
			p_dm->support_ability |= ODM_BB_CCK_PD;/* Enable CCKPD */
			break;
		}

		if (BIT(pause_lv) > p_cckpd_t->pause_bitmap) {

			/* Calculate the maximum level now */
			for (max_level = (pause_lv - 1); max_level >= 0; max_level--) {
				if (p_cckpd_t->pause_bitmap & BIT(max_level))
					break;
			}

			/* write CCKPD of lower level */
			phydm_write_cck_cca_th(p_dm, p_cckpd_t->pause_cckpd_value[max_level]);
			PHYDM_DBG(p_dm, DBG_CCKPD, ("Write CCKPD=0x%x for max_LV=%d\n",
				p_cckpd_t->pause_cckpd_value[max_level], max_level));
			break;
		}
		break;
	}
	default:
		PHYDM_DBG(p_dm, DBG_CCKPD,("Wrong  type\n"));
		break;
	}

	PHYDM_DBG(p_dm, DBG_CCKPD, ("New pause bitmap=0x%x\n", 
													p_cckpd_t->pause_bitmap));
	
	for (i = 0; i < PHYDM_PAUSE_MAX_NUM; i ++) {
		PHYDM_DBG(p_dm, DBG_CCKPD, ("pause val[%d]=0x%x\n", 
										i, p_cckpd_t->pause_cckpd_value[i]));
	}
#endif
}

void
phydm_cck_pd_init(
	void		*p_dm_void
)
{
#ifdef PHYDM_SUPPORT_CCKPD
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cckpd_struct		*p_cckpd_t = &p_dm->dm_cckpd_table;
	struct phydm_dig_struct		*p_dig_t = &p_dm->dm_dig_table;

	p_cckpd_t->cur_cck_cca_thres = 0;
	p_cckpd_t->cck_cca_th_aaa = 0;
	
	p_cckpd_t->pause_bitmap = 0;

	if (p_dm->support_ic_type & EXTEND_CCK_CCATH_AAA_IC)
		p_dig_t->aaa_default = odm_read_1byte(p_dm, 0xaaa) & 0x1f;
	
	odm_memory_set(p_dm, p_cckpd_t->pause_cckpd_value, 0, PHYDM_PAUSE_MAX_NUM);
#endif
}



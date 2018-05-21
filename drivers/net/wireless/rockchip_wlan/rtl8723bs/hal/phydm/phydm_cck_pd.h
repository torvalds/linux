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


#ifndef	__PHYDM_CCK_PD_H__
#define    __PHYDM_CCK_PD_H__

#define CCK_PD_VERSION	"1.0"		/* 2017.05.09  Dino, Add phydm_cck_pd.h*/


/* 1 ============================================================
 * 1  Definition
 * 1 ============================================================ */


#define	AAA_BASE	4
#define	AAA_STEP	2

#define	CCK_FA_MA_RESET	0xffffffff

#define	EXTEND_CCK_CCATH_AAA_IC	(ODM_RTL8197F | ODM_RTL8821C | ODM_RTL8723D |ODM_RTL8710B)
/* 1 ============================================================
 * 1  structure
 * 1 ============================================================ */

#ifdef PHYDM_SUPPORT_CCKPD
struct phydm_cckpd_struct {

	u8		cur_cck_cca_thres; /*0xA0A*/
	u8		cck_cca_th_aaa; /*0xAAA*/
	u32		cck_fa_ma;
	u8		cckpd_bkp;
	u32		rvrt_val[2];
	u8		pause_bitmap;/*will be removed*/
	u8		pause_lv;
	u8		pause_cckpd_value[PHYDM_PAUSE_MAX_NUM]; /*will be removed*/
};
#endif

/* 1 ============================================================
 * 1  enumeration
 * 1 ============================================================ */

/* 1 ============================================================
 * 1  function prototype
 * 1 ============================================================ */

void
phydm_set_cckpd_val(
	void			*p_dm_void,
	u32			*val_buf,
	u8			val_len
);

void
phydm_cck_pd_th(
	void		*p_dm_void
);

void
odm_pause_cck_packet_detection(
	void					*p_dm_void,
	enum phydm_pause_type		pause_type,
	enum phydm_pause_level		pause_level,
	u8					cck_pd_threshold
);

void
phydm_cck_pd_init(
	void		*p_dm_void
);

#endif

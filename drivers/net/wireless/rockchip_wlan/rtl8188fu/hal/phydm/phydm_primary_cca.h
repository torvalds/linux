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

#ifndef __PHYDM_PRIMARYCCA_H__
#define __PHYDM_PRIMARYCCA_H__

#ifdef PHYDM_PRIMARY_CCA
#define PRIMARYCCA_VERSION "2.0"

/*@============================================================*/
/*@Definition */
/*@============================================================*/

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
#define	SECOND_CH_AT_LSB	2	/*@primary CH @ MSB,  SD4: HAL_PRIME_CHNL_OFFSET_UPPER*/
#define	SECOND_CH_AT_USB	1	/*@primary CH @ LSB,   SD4: HAL_PRIME_CHNL_OFFSET_LOWER*/
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define	SECOND_CH_AT_LSB	2	/*@primary CH @ MSB,  SD7: HAL_PRIME_CHNL_OFFSET_UPPER*/
#define	SECOND_CH_AT_USB	1	/*@primary CH @ LSB,   SD7: HAL_PRIME_CHNL_OFFSET_LOWER*/
#else /*if (DM_ODM_SUPPORT_TYPE == ODM_AP)*/
#define	SECOND_CH_AT_LSB	1	/*@primary CH @ MSB,  SD8: HT_2NDCH_OFFSET_BELOW*/
#define	SECOND_CH_AT_USB	2	/*@primary CH @ LSB,   SD8: HT_2NDCH_OFFSET_ABOVE*/
#endif

#define	OFDMCCA_TH		500
#define	bw_ind_bias		500
#define	PRI_CCA_MONITOR_TIME	30

/*@============================================================*/
/*structure and define*/
/*@============================================================*/
enum primary_cca_ch_position { /*N-series REG0xc6c[8:7]*/
	MF_USC_LSC	= 0,
	MF_LSC		= 1,
	MF_USC		= 2
};

struct phydm_pricca_struct {
	#if (RTL8188E_SUPPORT == 1) || (RTL8192E_SUPPORT == 1)
	u8	pri_cca_flag;
	u8	intf_flag;
	u8	intf_type;
	u8	monitor_flag;
	u8	ch_offset;
	#endif
	u8	dup_rts_flag;
	u8	cca_th_40m_bkp; /*@c84[31:28]*/
	enum channel_width	pre_bw;
	u8	pri_cca_is_become_linked;
	u8	mf_state;
};

/*@============================================================*/
/*@function prototype*/
/*@============================================================*/
void phydm_write_dynamic_cca(void *dm_void, u8 curr_mf_state);

boolean odm_dynamic_primary_cca_dup_rts(void *dm_void);

void phydm_primary_cca_init(void *dm_void);

void phydm_primary_cca(void *dm_void);
#endif /*@#ifdef PHYDM_PRIMARY_CCA*/
#endif /*@#ifndef	__PHYDM_PRIMARYCCA_H__*/


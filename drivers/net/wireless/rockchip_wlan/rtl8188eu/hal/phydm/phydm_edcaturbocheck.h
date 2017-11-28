/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#ifndef	__PHYDMEDCATURBOCHECK_H__
#define    __PHYDMEDCATURBOCHECK_H__

#if PHYDM_SUPPORT_EDCA
/*#define EDCATURBO_VERSION	"2.1"*/
#define EDCATURBO_VERSION	"2.3"	/*2015.07.29 by YuChen*/

struct _EDCA_TURBO_ {
	bool is_current_turbo_edca;
	bool is_cur_rdl_state;

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	u32	prv_traffic_idx; /* edca turbo */
#endif

};

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
static u32 edca_setting_UL[HT_IOT_PEER_MAX] =
	/* UNKNOWN		REALTEK_90	REALTEK_92SE	BROADCOM		RALINK		ATHEROS		CISCO		MERU        MARVELL	92U_AP		SELF_AP(DownLink/Tx) */
{ 0x5e4322,		0xa44f,		0x5e4322,		0x5ea32b,		0x5ea422,	0x5ea322,	0x3ea430,	0x5ea42b, 0x5ea44f,	0x5e4322,	0x5e4322};


static u32 edca_setting_DL[HT_IOT_PEER_MAX] =
	/* UNKNOWN		REALTEK_90	REALTEK_92SE	BROADCOM		RALINK		ATHEROS		CISCO		MERU,       MARVELL	92U_AP		SELF_AP(UpLink/Rx) */
{ 0xa44f,		0x5ea44f,	0x5e4322,		0x5ea42b,		0xa44f,		0xa630,		0x5ea630,	0x5ea42b, 0xa44f,		0xa42b,		0xa42b};

static u32 edca_setting_dl_g_mode[HT_IOT_PEER_MAX] =
	/* UNKNOWN		REALTEK_90	REALTEK_92SE	BROADCOM		RALINK		ATHEROS		CISCO		MERU,       MARVELL	92U_AP		SELF_AP */
{ 0x4322,		0xa44f,		0x5e4322,		0xa42b,			0x5e4322,	0x4322,		0xa42b,		0x5ea42b, 0xa44f,		0x5e4322,	0x5ea42b};

#endif



void
odm_edca_turbo_check(
	void		*p_dm_void
);
void
odm_edca_turbo_init(
	void		*p_dm_void
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
odm_edca_turbo_check_mp(
	void		*p_dm_void
);

/* check if edca turbo is disabled */
bool
odm_is_edca_turbo_disable(
	void		*p_dm_void
);
/* choose edca paramter for special IOT case */
void
odm_edca_para_sel_by_iot(
	void					*p_dm_void,
	u32		*EDCA_BE_UL,
	u32		*EDCA_BE_DL
);
/* check if it is UL or DL */
void
odm_edca_choose_traffic_idx(
	void		*p_dm_void,
	u64			cur_tx_bytes,
	u64			cur_rx_bytes,
	bool		is_bias_on_rx,
	bool		*p_is_cur_rdl_state
);

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
void
odm_edca_turbo_check_ce(
	void		*p_dm_void
);
#endif

#endif /*PHYDM_SUPPORT_EDCA*/

#endif

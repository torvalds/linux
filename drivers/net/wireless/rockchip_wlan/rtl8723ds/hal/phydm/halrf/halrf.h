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


#ifndef	_HALRF_H__
#define _HALRF_H__

/*============================================================*/
/*include files*/
/*============================================================*/



/*============================================================*/
/*Definition */
/*============================================================*/
/*IQK version*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#define IQK_VERSION_8188E	"0x14"
#define IQK_VERSION_8192E	"0x01"
#define IQK_VERSION_8723B	"0x1d"
#define IQK_VERSION_8812A	"0x01"
#define IQK_VERSION_8821A	"0x01"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
#define IQK_VERSION_8188E	"0x01"
#define IQK_VERSION_8192E	"0x01"
#define IQK_VERSION_8723B	"0x19"
#define IQK_VERSION_8812A	"0x01"
#define IQK_VERSION_8821A	"0x01"
#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#define IQK_VERSION_8188E	"0x01"
#define IQK_VERSION_8192E	"0x01"
#define IQK_VERSION_8723B	"0x01"
#define IQK_VERSION_8812A	"0x01"
#define IQK_VERSION_8821A	"0x01"
#endif
#define IQK_VERSION_8814A	"0x0f"
#define IQK_VERSION_8188F	"0x01"
#define IQK_VERSION_8197F	"0x01"
#define IQK_VERSION_8703B	"0x04"
#define IQK_VERSION_8710B	"0x01"
#define IQK_VERSION_8723D	"0x01"
#define IQK_VERSION_8822B	"0x2e"
#define IQK_VERSION_8821C	"0x23"

/*LCK version*/
#define LCK_VERSION_8188E	"0x01"
#define LCK_VERSION_8192E	"0x01"
#define LCK_VERSION_8723B	"0x01"
#define LCK_VERSION_8812A	"0x01"
#define LCK_VERSION_8821A	"0x01"
#define LCK_VERSION_8814A	"0x01"
#define LCK_VERSION_8188F	"0x01"
#define LCK_VERSION_8197F	"0x01"
#define LCK_VERSION_8703B	"0x01"
#define LCK_VERSION_8710B	"0x01"
#define LCK_VERSION_8723D	"0x01"
#define LCK_VERSION_8822B	"0x01"
#define LCK_VERSION_8821C	"0x01"


#define HALRF_IQK_VER	(p_dm->support_ic_type == ODM_RTL8188E)? IQK_VERSION_8188E :\
						(p_dm->support_ic_type == ODM_RTL8192E)? IQK_VERSION_8192E :\
						(p_dm->support_ic_type == ODM_RTL8723B)? IQK_VERSION_8723B :\
						(p_dm->support_ic_type == ODM_RTL8812)? IQK_VERSION_8812A :\
						(p_dm->support_ic_type == ODM_RTL8821)? IQK_VERSION_8821A :\
						(p_dm->support_ic_type == ODM_RTL8814A)? IQK_VERSION_8814A :\
						(p_dm->support_ic_type == ODM_RTL8188F)? IQK_VERSION_8188F :\
						(p_dm->support_ic_type == ODM_RTL8197F)? IQK_VERSION_8197F :\
						(p_dm->support_ic_type == ODM_RTL8703B)? IQK_VERSION_8703B :\
						(p_dm->support_ic_type == ODM_RTL8710B)? IQK_VERSION_8710B :\
						(p_dm->support_ic_type == ODM_RTL8723D)? IQK_VERSION_8723D :\
						(p_dm->support_ic_type == ODM_RTL8822B)? IQK_VERSION_8822B :\
						(p_dm->support_ic_type == ODM_RTL8821C)? IQK_VERSION_8821C :"unknown"


#define HALRF_LCK_VER	(p_dm->support_ic_type == ODM_RTL8188E)? LCK_VERSION_8188E :\
						(p_dm->support_ic_type == ODM_RTL8192E)? LCK_VERSION_8192E :\
						(p_dm->support_ic_type == ODM_RTL8723B)? LCK_VERSION_8723B :\
						(p_dm->support_ic_type == ODM_RTL8812)? LCK_VERSION_8812A :\
						(p_dm->support_ic_type == ODM_RTL8821)? LCK_VERSION_8821A :\
						(p_dm->support_ic_type == ODM_RTL8814A)? LCK_VERSION_8814A :\
						(p_dm->support_ic_type == ODM_RTL8188F)? LCK_VERSION_8188F :\
						(p_dm->support_ic_type == ODM_RTL8197F)? LCK_VERSION_8197F :\
						(p_dm->support_ic_type == ODM_RTL8703B)? LCK_VERSION_8703B :\
						(p_dm->support_ic_type == ODM_RTL8710B)? LCK_VERSION_8710B :\
						(p_dm->support_ic_type == ODM_RTL8723D)? LCK_VERSION_8723D :\
						(p_dm->support_ic_type == ODM_RTL8822B)? LCK_VERSION_8822B :\
						(p_dm->support_ic_type == ODM_RTL8821C)? LCK_VERSION_8821C :"unknown"
#define HALRF_DPK_VER	"0x02"

#define IQK_THRESHOLD			8
#define DPK_THRESHOLD			4

/*============================================================*/
/* enumeration */
/*============================================================*/
enum halrf_ability_e {

	HAL_RF_TX_PWR_TRACK	= BIT(0),
	HAL_RF_IQK				= BIT(1),
	HAL_RF_LCK				= BIT(2),
	HAL_RF_DPK				= BIT(3),
	HAL_RF_TXGAPK			= BIT(4)
};

enum halrf_cmninfo_init_e {
	HALRF_CMNINFO_ABILITY = 0,
	HALRF_CMNINFO_DPK_EN = 1,
	HALRF_CMNINFO_EEPROM_THERMAL_VALUE,
	HALRF_CMNINFO_FW_VER,
	HALRF_CMNINFO_RFK_FORBIDDEN,
	HALRF_CMNINFO_IQK_SEGMENT,
	HALRF_CMNINFO_RATE_INDEX
};

enum halrf_cmninfo_hook_e {
	HALRF_CMNINFO_CON_TX,
	HALRF_CMNINFO_SINGLE_TONE,
	HALRF_CMNINFO_CARRIER_SUPPRESSION,	
	HALRF_CMNINFO_MP_RATE_INDEX
};

enum phydm_lna_set {
	phydm_lna_disable		= 0,
	phydm_lna_enable		= 1,
};


/*============================================================*/
/* structure */
/*============================================================*/

struct _hal_rf_ {
	/*hook*/
	u8		*test1;

	/*update*/
	u32		rf_supportability;

	u8		eeprom_thermal;
	u8		dpk_en;			/*Enable Function DPK OFF/ON = 0/1*/
	boolean	dpk_done;
	u32		fw_ver;

	boolean	*p_is_con_tx;
	boolean	*p_is_single_tone;
	boolean	*p_is_carrier_suppresion;

	u8		*p_mp_rate_index;
	u32		p_rate_index;
};

/*============================================================*/
/* function prototype */
/*============================================================*/

void halrf_basic_profile(
	void			*p_dm_void,
	u32			*_used,
	char			*output,
	u32			*_out_len
);
#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
void halrf_iqk_info_dump(
	void *p_dm_void,
	u32 *_used,
	char *output,
	u32 *_out_len
);

void
halrf_iqk_hwtx_check(
	void *p_dm_void,
	boolean		is_check
);
#endif

void
halrf_support_ability_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len
);

void
halrf_cmn_info_init(
	void		*p_dm_void,
	enum halrf_cmninfo_init_e	cmn_info,
	u32		value
);

void
halrf_cmn_info_hook(
	void		*p_dm_void,
	u32		cmn_info,
	void		*p_value
);

void
halrf_cmn_info_set(
	void		*p_dm_void,
	u32			cmn_info,
	u64			value
);

u64
halrf_cmn_info_get(
	void		*p_dm_void,
	u32			cmn_info
);

void
halrf_watchdog(
	void			*p_dm_void
);

void
halrf_supportability_init(
	void		*p_dm_void
);

void
halrf_init(
	void			*p_dm_void
);

void
halrf_iqk_trigger(
	void			*p_dm_void,
	boolean		is_recovery
);

void
halrf_segment_iqk_trigger(
	void			*p_dm_void,
	boolean		clear,
	boolean		segment_iqk
);

void
halrf_lck_trigger(
	void			*p_dm_void
);

void
halrf_iqk_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
);

void
phydm_get_iqk_cfir(
	void		*p_dm_void,
	u8 idx,
	u8 path,
	boolean debug
);

void 
halrf_iqk_xym_read(
	void *p_dm_void,
	u8 path,
	u8 xym_type
 );

void
halrf_rf_lna_setting(
	void	*p_dm_void,
	enum phydm_lna_set type
);


void
halrf_do_imr_test(
	void	*p_dm_void,
	u8 data
);

u32
halrf_psd_log2base(
	IN u32 val
);


#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
void halrf_iqk_dbg(void	*p_dm_void);
#endif
#endif



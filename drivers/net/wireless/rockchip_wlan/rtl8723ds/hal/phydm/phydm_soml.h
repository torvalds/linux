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
#ifndef	__PHYDMSOML_H__
#define    __PHYDMSOML_H__

#define ADAPTIVE_SOML_VERSION	"1.0"

#define ODM_ADAPTIVE_SOML_SUPPORT_IC	(ODM_RTL8822B | ODM_RTL8197F)

#define INIT_SOML_TIMMER			0
#define CANCEL_SOML_TIMMER		1
#define RELEASE_SOML_TIMMER		2

#define SOML_RSSI_TH_HIGH	25
#define SOML_RSSI_TH_LOW	20

#define HT_RATE_IDX			32
#define VHT_RATE_IDX		40

#define SOML_ON		1
#define SOML_OFF	0

#ifdef CONFIG_ADAPTIVE_SOML

struct adaptive_soml {
	boolean		is_soml_method_enable;
	u8			soml_on_off;
	u8			soml_state_cnt;
	u8			soml_delay_time;
	u8			soml_intvl;
	u8			soml_train_num;
	u8			soml_counter;
	u8			soml_period;
	u8			soml_select;
	u8			soml_last_state;
	u32			num_ht_bytes[HT_RATE_IDX];
	u32			pre_num_ht_bytes[HT_RATE_IDX];
	u32			num_ht_bytes_on[HT_RATE_IDX];
	u32			num_ht_bytes_off[HT_RATE_IDX];
	#if	ODM_IC_11AC_SERIES_SUPPORT
	u32			num_vht_bytes[VHT_RATE_IDX];
	u32			pre_num_vht_bytes[VHT_RATE_IDX];
	u32			num_vht_bytes_on[VHT_RATE_IDX];
	u32			num_vht_bytes_off[VHT_RATE_IDX];
	#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if USE_WORKITEM
	RT_WORK_ITEM	phydm_adaptive_soml_workitem;
#endif
#endif
	struct timer_list		phydm_adaptive_soml_timer;

};

void
phydm_dynamicsoftmletting(
	struct PHY_DM_STRUCT		*p_dm
);

void
phydm_soml_on_off(
	void		*p_dm_void,
	u8		swch
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
phydm_adaptive_soml_callback(
	struct timer_list		*p_timer
);

void
phydm_adaptive_soml_workitem_callback(
	void		*p_context
);

#else

void
phydm_adaptive_soml_callback(
	void		*p_dm_void
);

#endif

void
phydm_soml_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
);

void
phydm_soml_statistics(
	void		*p_dm_void,
	u8		on_off_state

);

void
phydm_adsl(
	void		*p_dm_void
);

void
phydm_adaptive_soml_reset(
	void		*p_dm_void
);

#endif

void
phydm_adaptive_soml_timers(
	void		*p_dm_void,
	u8		state
);

void
phydm_adaptive_soml_init(
	void		*p_dm_void
);

void
phydm_adaptive_soml(
	void		*p_dm_void
);

void
phydm_init_soft_ml_setting(
	void		*p_dm_void
);

#endif /*#ifndef	__PHYDMSOML_H__*/

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

#ifndef	__PHYDMRAINFO_H__
#define    __PHYDMRAINFO_H__

/*#define RAINFO_VERSION	"2.0"*/  /*2014.11.04*/
/*#define RAINFO_VERSION	"3.0"*/  /*2015.01.13 Dino*/
/*#define RAINFO_VERSION	"3.1"*/  /*2015.01.14 Dino*/
/*#define RAINFO_VERSION	"3.3"*/  /*2015.07.29 YuChen*/
/*#define RAINFO_VERSION	"3.4"*/  /*2015.12.15 Stanley*/
/*#define RAINFO_VERSION	"4.0"*/  /*2016.03.24 Dino, Add more RA mask state and Phydm-lize partial ra mask function  */
/*#define RAINFO_VERSION	"4.1"*/  /*2016.04.20 Dino, Add new function to adjust PCR RA threshold  */
/*#define RAINFO_VERSION	"4.2"*/  /*2016.05.17 Dino, Add H2C debug cmd  */
/*#define RAINFO_VERSION	"4.3"*/  /*2016.07.11 Dino, Fix RA hang in CCK 1M problem  */
#define RAINFO_VERSION	"5.0"  /*2017.04.20 Dino, the 3rd PHYDM reform*/

#define	FORCED_UPDATE_RAMASK_PERIOD	5

#define	H2C_MAX_LENGTH	7

#define	RA_FLOOR_UP_GAP		3
#define	RA_FLOOR_TABLE_SIZE	7

#define	ACTIVE_TP_THRESHOLD	1
#define	RA_RETRY_DESCEND_NUM	2
#define	RA_RETRY_LIMIT_LOW	4
#define	RA_RETRY_LIMIT_HIGH	32

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#define		RA_FIRST_MACID	1
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define	RA_FIRST_MACID	0
	#define	WIN_DEFAULT_PORT_MACID	0
	#define	WIN_BT_PORT_MACID	2
#else /*if (DM_ODM_SUPPORT_TYPE == ODM_CE)*/
	#define		RA_FIRST_MACID	0
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#define AP_InitRateAdaptiveState		odm_rate_adaptive_state_ap_init
#endif


#define		DM_RATR_STA_INIT			0
#define		DM_RATR_STA_HIGH			1
#define		DM_RATR_STA_MIDDLE		2
#define		DM_RATR_STA_LOW			3
#define		DM_RATR_STA_ULTRA_LOW	4

enum phydm_ra_dbg_para_e {
	RADBG_PCR_TH_OFFSET		=	0,
	RADBG_RTY_PENALTY		=	1,
	RADBG_N_HIGH				=	2,
	RADBG_N_LOW				=	3,
	RADBG_TRATE_UP_TABLE		=	4,
	RADBG_TRATE_DOWN_TABLE	=	5,
	RADBG_TRYING_NECESSARY	=	6,
	RADBG_TDROPING_NECESSARY =	7,
	RADBG_RATE_UP_RTY_RATIO	=	8,
	RADBG_RATE_DOWN_RTY_RATIO =	9, /* u8 */

	RADBG_DEBUG_MONITOR1 = 0xc,
	RADBG_DEBUG_MONITOR2 = 0xd,
	RADBG_DEBUG_MONITOR3 = 0xe,
	RADBG_DEBUG_MONITOR4 = 0xf,
	RADBG_DEBUG_MONITOR5 = 0x10,
	NUM_RA_PARA
};

enum phydm_wireless_mode_e {

	PHYDM_WIRELESS_MODE_UNKNOWN = 0x00,
	PHYDM_WIRELESS_MODE_A		= 0x01,
	PHYDM_WIRELESS_MODE_B		= 0x02,
	PHYDM_WIRELESS_MODE_G		= 0x04,
	PHYDM_WIRELESS_MODE_AUTO	= 0x08,
	PHYDM_WIRELESS_MODE_N_24G	= 0x10,
	PHYDM_WIRELESS_MODE_N_5G	= 0x20,
	PHYDM_WIRELESS_MODE_AC_5G	= 0x40,
	PHYDM_WIRELESS_MODE_AC_24G	= 0x80,
	PHYDM_WIRELESS_MODE_AC_ONLY	= 0x100,
	PHYDM_WIRELESS_MODE_MAX		= 0x800,
	PHYDM_WIRELESS_MODE_ALL		= 0xFFFF
};

enum phydm_rateid_idx_e {

	PHYDM_BGN_40M_2SS	= 0,
	PHYDM_BGN_40M_1SS	= 1,
	PHYDM_BGN_20M_2SS	= 2,
	PHYDM_BGN_20M_1SS	= 3,
	PHYDM_GN_N2SS			= 4,
	PHYDM_GN_N1SS			= 5,
	PHYDM_BG				= 6,
	PHYDM_G				= 7,
	PHYDM_B_20M			= 8,
	PHYDM_ARFR0_AC_2SS	= 9,
	PHYDM_ARFR1_AC_1SS	= 10,
	PHYDM_ARFR2_AC_2G_1SS	= 11,
	PHYDM_ARFR3_AC_2G_2SS	= 12,
	PHYDM_ARFR4_AC_3SS	= 13,
	PHYDM_ARFR5_N_3SS		= 14
};

#if (RATE_ADAPTIVE_SUPPORT == 1)/* 88E RA */
struct _odm_ra_info_ {
	u8 rate_id;
	u32 rate_mask;
	u32 ra_use_rate;
	u8 rate_sgi;
	u8 rssi_sta_ra;
	u8 pre_rssi_sta_ra;
	u8 sgi_enable;
	u8 decision_rate;
	u8 pre_rate;
	u8 highest_rate;
	u8 lowest_rate;
	u32 nsc_up;
	u32 nsc_down;
	u16 RTY[5];
	u32 TOTAL;
	u16 DROP;
	u8 active;
	u16 rpt_time;
	u8 ra_waiting_counter;
	u8 ra_pending_counter;
	u8 ra_drop_after_down;
#if 1 /* POWER_TRAINING_ACTIVE == 1 */ /* For compile  pass only~! */
	u8 pt_active;  /* on or off */
	u8 pt_try_state;  /* 0 trying state, 1 for decision state */
	u8 pt_stage;  /* 0~6 */
	u8 pt_stop_count; /* Stop PT counter */
	u8 pt_pre_rate;  /* if rate change do PT */
	u8 pt_pre_rssi; /* if RSSI change 5% do PT */
	u8 pt_mode_ss;  /* decide whitch rate should do PT */
	u8 ra_stage;  /* StageRA, decide how many times RA will be done between PT */
	u8 pt_smooth_factor;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) &&	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	u8 rate_down_counter;
	u8 rate_up_counter;
	u8 rate_direction;
	u8 bounding_type;
	u8 bounding_counter;
	u8 bounding_learning_time;
	u8 rate_down_start_time;
#endif
};
#endif


struct _rate_adaptive_table_ {
	u8		firstconnect;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	boolean		PT_collision_pre;
#endif

#if (defined(CONFIG_RA_DBG_CMD))
	boolean		is_ra_dbg_init;

	u8	RTY_P[ODM_NUM_RATE_IDX];
	u8	RTY_P_default[ODM_NUM_RATE_IDX];
	boolean	RTY_P_modify_note[ODM_NUM_RATE_IDX];

	u8	RATE_UP_RTY_RATIO[ODM_NUM_RATE_IDX];
	u8	RATE_UP_RTY_RATIO_default[ODM_NUM_RATE_IDX];
	boolean	RATE_UP_RTY_RATIO_modify_note[ODM_NUM_RATE_IDX];

	u8	RATE_DOWN_RTY_RATIO[ODM_NUM_RATE_IDX];
	u8	RATE_DOWN_RTY_RATIO_default[ODM_NUM_RATE_IDX];
	boolean	RATE_DOWN_RTY_RATIO_modify_note[ODM_NUM_RATE_IDX];

	boolean ra_para_feedback_req;

	u8   para_idx;
	u8	rate_idx;
	u8	value;
	u16	value_16;
	u8	rate_length;
#endif
	/*u8	link_tx_rate[ODM_ASSOCIATE_ENTRY_NUM];*/
	u8	ra_ratio[ODM_ASSOCIATE_ENTRY_NUM];
	u8	mu1_rate[30];
	u8	highest_client_tx_order;
	u16	highest_client_tx_rate_order;
	u8	power_tracking_flag;
	u8	RA_threshold_offset;
	u8	RA_offset_direction;
	u8	up_ramask_cnt; /*force update_ra_mask counter*/
	u8	up_ramask_cnt_tmp; /*Just for debug, should be removed latter*/

#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))
	u8	per_rate_retrylimit_20M[ODM_NUM_RATE_IDX];
	u8	per_rate_retrylimit_40M[ODM_NUM_RATE_IDX];
	u8	retry_descend_num;
	u8	retrylimit_low;
	u8	retrylimit_high;
#endif
	u8	ldpc_thres;			/* if RSSI > ldpc_thres => switch from LPDC to BCC */

	void (*record_ra_info)(void *p_dm_void, u8 macid, struct cmn_sta_info *p_sta, u64 ra_mask);
};

void
phydm_h2C_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
);

#if (defined(CONFIG_RA_DBG_CMD))

void
odm_RA_debug(
	void		*p_dm_void,
	u32		*const dm_value
);

void
odm_ra_para_adjust_init(
	void		*p_dm_void
);

#endif

void
phydm_ra_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len
);

void
odm_c2h_ra_para_report_handler(
	void	*p_dm_void,
	u8	*cmd_buf,
	u8	cmd_len
);

void
odm_ra_para_adjust(
	void		*p_dm_void
);

void
phydm_ra_dynamic_retry_count(
	void	*p_dm_void
);

void
phydm_ra_dynamic_retry_limit(
	void	*p_dm_void
);

void
phydm_print_rate(
	void	*p_dm_void,
	u8	rate,
	u32	dbg_component
);

void
phydm_c2h_ra_report_handler(
	void	*p_dm_void,
	u8   *cmd_buf,
	u8   cmd_len
);

u8
phydm_rate_order_compute(
	void	*p_dm_void,
	u8	rate_idx
);

void
phydm_ra_info_watchdog(
	void	*p_dm_void
);

void
phydm_ra_info_init(
	void	*p_dm_void
);

void
phydm_modify_RA_PCR_threshold(
	void		*p_dm_void,
	u8		RA_offset_direction,
	u8		RA_threshold_offset
);

u8
phydm_vht_en_mapping(
	void			*p_dm_void,
	u32			wireless_mode
);

u8
phydm_rate_id_mapping(
	void			*p_dm_void,
	u32			wireless_mode,
	u8			rf_type,
	u8			bw
);

void
phydm_update_hal_ra_mask(
	void			*p_dm_void,
	u32			wireless_mode,
	u8			rf_type,
	u8			BW,
	u8			mimo_ps_enable,
	u8			disable_cck_rate,
	u32			*ratr_bitmap_msb_in,
	u32			*ratr_bitmap_in,
	u8			tx_rate_level
);

void
phydm_refresh_rate_adaptive_mask(
	void		*p_dm_void
);

u8
phydm_rssi_lv_dec(
	void			*p_dm_void,
	u32			rssi,
	u8			ratr_state
);

void
odm_ra_post_action_on_assoc(
	void	*p_dm
);

u8
odm_find_rts_rate(
	void		*p_dm_void,
	u8			tx_rate,
	boolean			is_erp_protect
);

void
phydm_show_sta_info(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
);

#ifdef	PHYDM_3RD_REFORM_RA_MASK

void
phydm_ra_registed(
	void	*p_dm_void,
	u8	macid,
	u8	rssi_from_assoc
);

void
phydm_ra_offline(
	void	*p_dm_void,
	u8	macid
);


void
phydm_ra_mask_watchdog(
	void	*p_dm_void
);

#endif


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void
odm_refresh_basic_rate_mask(
	void		*p_dm_void
);

void
odm_update_init_rate_work_item_callback(
	void	*p_context
);

void
odm_refresh_ldpc_rts_mp(
	struct _ADAPTER			*p_adapter,
	struct PHY_DM_STRUCT			*p_dm,
	u8				m_mac_id,
	u8				iot_peer,
	s32				undecorated_smoothed_pwdb
);

void
odm_rate_adaptive_state_ap_init(
	void			*PADAPTER_VOID,
	struct cmn_sta_info	*p_entry
);

#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))

void
phydm_gen_ramask_h2c_AP(
	void			*p_dm_void,
	struct rtl8192cd_priv *priv,
	struct sta_info *p_entry,
	u8			rssi_level
);

#endif/*#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))*/


#if (defined(CONFIG_RA_DYNAMIC_RATE_ID))
void
phydm_ra_dynamic_rate_id_on_assoc(
	void	*p_dm_void,
	u8	wireless_mode,
	u8	init_rate_id
);

void
phydm_ra_dynamic_rate_id_init(
	void	*p_dm_void
);

void
phydm_update_rate_id(
	void	*p_dm_void,
	u8	rate,
	u8	platform_macid
);

#endif

#endif /*#ifndef	__ODMRAINFO_H__*/

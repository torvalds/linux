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

#ifndef	__PHYDMRAINFO_H__
#define    __PHYDMRAINFO_H__

/*#define RAINFO_VERSION	"2.0"  //2014.11.04*/
/*#define RAINFO_VERSION	"3.0"  //2015.01.13 Dino*/
/*#define RAINFO_VERSION	"3.1"  //2015.01.14 Dino*/
/*#define RAINFO_VERSION	"3.3"  2015.07.29 YuChen*/
/*#define RAINFO_VERSION	"3.4"*/  /*2015.12.15 Stanley*/
/*#define RAINFO_VERSION	"4.0"*/  /*2016.03.24 Dino, Add more RA mask state and Phydm-lize partial ra mask function  */
/*#define RAINFO_VERSION	"4.1"*/  /*2016.04.20 Dino, Add new function to adjust PCR RA threshold  */
/*#define RAINFO_VERSION	"4.2"*/  /*2016.05.17 Dino, Add H2C debug cmd  */
#define RAINFO_VERSION	"4.3"  /*2016.07.11 Dino, Fix RA hang in CCK 1M problem  */

#define	FORCED_UPDATE_RAMASK_PERIOD	5

#define	H2C_0X42_LENGTH	5
#define	H2C_MAX_LENGTH	7

#define	RA_FLOOR_UP_GAP		3
#define	RA_FLOOR_TABLE_SIZE	7

#define	ACTIVE_TP_THRESHOLD	150
#define	RA_RETRY_DESCEND_NUM	2
#define	RA_RETRY_LIMIT_LOW	4
#define	RA_RETRY_LIMIT_HIGH	32

#define RAINFO_BE_RX_STATE			BIT(0)  /* 1:RX    */ /* ULDL */
#define RAINFO_STBC_STATE			BIT(1)
/* #define RAINFO_LDPC_STATE			BIT2 */
#define RAINFO_NOISY_STATE 			BIT(2)    /* set by Noisy_Detection */
#define RAINFO_SHURTCUT_STATE		BIT(3)
#define RAINFO_SHURTCUT_FLAG		BIT(4)
#define RAINFO_INIT_RSSI_RATE_STATE  BIT(5)
#define RAINFO_BF_STATE				BIT(6)
#define RAINFO_BE_TX_STATE 			BIT(7) /* 1:TX */

#define	RA_MASK_CCK		0xf
#define	RA_MASK_OFDM		0xff0
#define	RA_MASK_HT1SS		0xff000
#define	RA_MASK_HT2SS		0xff00000
/*#define	RA_MASK_MCS3SS	*/
#define	RA_MASK_HT4SS		0xff0
#define	RA_MASK_VHT1SS	0x3ff000
#define	RA_MASK_VHT2SS	0xffc00000

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
#else
#define ap_init_rate_adaptive_state	odm_rate_adaptive_state_ap_init
#endif

#if (RA_MASK_PHYDMLIZE_CE || RA_MASK_PHYDMLIZE_AP || RA_MASK_PHYDMLIZE_WIN)
	#define		DM_RATR_STA_INIT			0
	#define		DM_RATR_STA_HIGH			1
	#define		DM_RATR_STA_MIDDLE		2
	#define		DM_RATR_STA_LOW			3
	#define		DM_RATR_STA_ULTRA_LOW	4
#endif

enum phydm_ra_arfr_num_e {
	ARFR_0_RATE_ID	=	0x9,
	ARFR_1_RATE_ID	=	0xa,
	ARFR_2_RATE_ID	=	0xb,
	ARFR_3_RATE_ID	=	0xc,
	ARFR_4_RATE_ID	=	0xd,
	ARFR_5_RATE_ID	=	0xe
};

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

enum phydm_rf_type_def_e {
	PHYDM_RF_1T1R = 0,
	PHYDM_RF_1T2R,
	PHYDM_RF_2T2R,
	PHYDM_RF_2T2R_GREEN,
	PHYDM_RF_2T3R,
	PHYDM_RF_2T4R,
	PHYDM_RF_3T3R,
	PHYDM_RF_3T4R,
	PHYDM_RF_4T4R,
	PHYDM_RF_MAX_TYPE
};

enum phydm_bw_e {
	PHYDM_BW_20	= 0,
	PHYDM_BW_40,
	PHYDM_BW_80,
	PHYDM_BW_80_80,
	PHYDM_BW_160,
	PHYDM_BW_10,
	PHYDM_BW_5
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
	u8	link_tx_rate[ODM_ASSOCIATE_ENTRY_NUM];
	u8	highest_client_tx_order;
	u16	highest_client_tx_rate_order;
	u8	power_tracking_flag;
	u8	RA_threshold_offset;
	u8	RA_offset_direction;
	u8	force_update_ra_mask_count;

#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))
	u8 per_rate_retrylimit_20M[ODM_NUM_RATE_IDX];
	u8 per_rate_retrylimit_40M[ODM_NUM_RATE_IDX];
	u8			retry_descend_num;
	u8			retrylimit_low;
	u8			retrylimit_high;
#endif


};

struct _ODM_RATE_ADAPTIVE {
	u8				type;				/* dm_type_by_fw/dm_type_by_driver */
	u8				high_rssi_thresh;		/* if RSSI > high_rssi_thresh	=> ratr_state is DM_RATR_STA_HIGH */
	u8				low_rssi_thresh;		/* if RSSI <= low_rssi_thresh	=> ratr_state is DM_RATR_STA_LOW */
	u8				ratr_state;			/* Current RSSI level, DM_RATR_STA_HIGH/DM_RATR_STA_MIDDLE/DM_RATR_STA_LOW */

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	u8				ldpc_thres;			/* if RSSI > ldpc_thres => switch from LPDC to BCC */
	boolean				is_lower_rts_rate;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	u8				rts_thres;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	boolean				is_use_ldpc;
#else
	u8				ultra_low_rssi_thresh;
	u32				last_ratr;			/* RATR Register Content */
#endif

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

#else

void
phydm_RA_debug_PCR(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
);

#endif

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
phydm_ra_dynamic_rate_id_on_assoc(
	void	*p_dm_void,
	u8	wireless_mode,
	u8	init_rate_id
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
odm_rssi_monitor_init(
	void	*p_dm_void
);

void
phydm_modify_RA_PCR_threshold(
	void		*p_dm_void,
	u8		RA_offset_direction,
	u8		RA_threshold_offset
);

void
odm_rssi_monitor_check(
	void	*p_dm_void
);

void
phydm_init_ra_info(
	void		*p_dm_void
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
odm_rate_adaptive_mask_init(
	void	*p_dm_void
);

void
odm_refresh_rate_adaptive_mask(
	void		*p_dm_void
);

void
odm_refresh_rate_adaptive_mask_mp(
	void		*p_dm_void
);

void
odm_refresh_rate_adaptive_mask_ce(
	void		*p_dm_void
);

void
odm_refresh_rate_adaptive_mask_apadsl(
	void		*p_dm_void
);

u8
phydm_RA_level_decision(
	void			*p_dm_void,
	u32			rssi,
	u8			ratr_state
);

boolean
odm_ra_state_check(
	void		*p_dm_void,
	s32			RSSI,
	boolean			is_force_update,
	u8			*p_ra_tr_state
);

void
odm_refresh_basic_rate_mask(
	void		*p_dm_void
);
void
odm_ra_post_action_on_assoc(
	void	*p_dm_odm
);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))

u8
odm_find_rts_rate(
	void		*p_dm_void,
	u8			tx_rate,
	boolean			is_erp_protect
);

void
odm_update_noisy_state(
	void		*p_dm_void,
	boolean		is_noisy_state_from_c2h
);

void
phydm_update_pwr_track(
	void		*p_dm_void,
	u8		rate
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

s32
phydm_find_minimum_rssi(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _ADAPTER		*p_adapter,
	OUT	boolean	*p_is_link_temp
);

void
odm_update_init_rate_work_item_callback(
	void	*p_context
);

void
odm_rssi_dump_to_register(
	void	*p_dm_void
);

void
odm_refresh_ldpc_rts_mp(
	struct _ADAPTER			*p_adapter,
	struct PHY_DM_STRUCT			*p_dm_odm,
	u8				m_mac_id,
	u8				iot_peer,
	s32				undecorated_smoothed_pwdb
);

#if 0
void
odm_dynamic_arfb_select(
	void		*p_dm_void,
	u8		rate,
	boolean		collision_state
);
#endif

void
odm_rate_adaptive_state_ap_init(
	void			*PADAPTER_VOID,
	struct sta_info	*p_entry
);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

static void
find_minimum_rssi(
	struct _ADAPTER	*p_adapter
);

u64
phydm_get_rate_bitmap_ex(
	void		*p_dm_void,
	u32		macid,
	u64		ra_mask,
	u8		rssi_level,
	u64	*dm_ra_mask,
	u8	*dm_rte_id
);
u32
odm_get_rate_bitmap(
	void	*p_dm_void,
	u32		macid,
	u32		ra_mask,
	u8		rssi_level
);

void phydm_ra_rssi_rpt_wk(void *p_context);
#endif/*#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)*/

#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))
/*
void
phydm_gen_ramask_h2c_AP(
	void					*p_dm_void,
	struct rtl8192cd_priv 	*priv,
	struct sta_info 		*p_entry,
	u8					rssi_level
);
*/
#endif/*#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN| ODM_CE))*/

#endif /*#ifndef	__ODMRAINFO_H__*/

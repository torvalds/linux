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

#ifndef	__PHYDMPATHDIV_H__
#define    __PHYDMPATHDIV_H__
/*#define PATHDIV_VERSION "2.0" //2014.11.04*/
#define PATHDIV_VERSION	"3.1" /*2015.07.29 by YuChen*/

#if (defined(CONFIG_PATH_DIVERSITY))
#define USE_PATH_A_AS_DEFAULT_ANT   /* for 8814 dynamic TX path selection */

#define	NUM_RESET_DTP_PERIOD 5
#define	ANT_DECT_RSSI_TH 3

#define PATH_A 1
#define PATH_B 2
#define PATH_C 3
#define PATH_D 4

#define PHYDM_AUTO_PATH	0
#define PHYDM_FIX_PATH		1

#define NUM_CHOOSE2_FROM4 6
#define NUM_CHOOSE3_FROM4 4

enum phydm_dtp_state {
	PHYDM_DTP_INIT = 1,
	PHYDM_DTP_RUNNING_1

};

enum phydm_path_div_type {
	PHYDM_2R_PATH_DIV = 1,
	PHYDM_4R_PATH_DIV = 2
};

void
phydm_process_rssi_for_path_div(
	void			*p_dm_void,
	void			*p_phy_info_void,
	void			*p_pkt_info_void
);

struct _ODM_PATH_DIVERSITY_ {
	u8	resp_tx_path;
	u8	path_sel[ODM_ASSOCIATE_ENTRY_NUM];
	u32	path_a_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32	path_b_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u16	path_a_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u16	path_b_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u8	phydm_path_div_type;
#if RTL8814A_SUPPORT

	u32	path_a_sum_all;
	u32	path_b_sum_all;
	u32	path_c_sum_all;
	u32	path_d_sum_all;

	u32	path_a_cnt_all;
	u32	path_b_cnt_all;
	u32	path_c_cnt_all;
	u32	path_d_cnt_all;

	u8	dtp_period;
	boolean	is_become_linked;
	boolean	is_u3_mode;
	u8	num_tx_path;
	u8	default_path;
	u8	num_candidate;
	u8	ant_candidate_1;
	u8	ant_candidate_2;
	u8	ant_candidate_3;
	u8     phydm_dtp_state;
	u8	dtp_check_patha_counter;
	boolean	fix_path_bfer;
	u8	search_space_2[NUM_CHOOSE2_FROM4];
	u8	search_space_3[NUM_CHOOSE3_FROM4];

	u8	pre_tx_path;
	u8	use_path_a_as_default_ant;
	boolean is_path_a_exist;

#endif
};


#endif /* #if(defined(CONFIG_PATH_DIVERSITY)) */

void
phydm_c2h_dtp_handler(
	void	*p_dm_void,
	u8   *cmd_buf,
	u8	cmd_len
);

void
phydm_path_diversity_init(
	void	*p_dm_void
);

void
odm_path_diversity(
	void	*p_dm_void
);

void
odm_pathdiv_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
);



/* 1 [OLD IC]-------------------------------------------------------------------------------- */






#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))

/* #define   PATHDIV_ENABLE	 1 */
#define dm_path_div_rssi_check	odm_path_div_chk_per_pkt_rssi
#define path_div_check_before_link8192c	odm_path_diversity_before_link92c




struct _path_div_parameter_define_ {
	u32 org_5g_rege30;
	u32 org_5g_regc14;
	u32 org_5g_regca0;
	u32 swt_5g_rege30;
	u32 swt_5g_regc14;
	u32 swt_5g_regca0;
	/* for 2G IQK information */
	u32 org_2g_regc80;
	u32 org_2g_regc4c;
	u32 org_2g_regc94;
	u32 org_2g_regc14;
	u32 org_2g_regca0;

	u32 swt_2g_regc80;
	u32 swt_2g_regc4c;
	u32 swt_2g_regc94;
	u32 swt_2g_regc14;
	u32 swt_2g_regca0;
};

void
odm_path_diversity_init_92c(
	struct _ADAPTER	*adapter
);

void
odm_2t_path_diversity_init_92c(
	struct _ADAPTER	*adapter
);

void
odm_1t_path_diversity_init_92c(
	struct _ADAPTER	*adapter
);

boolean
odm_is_connected_92c(
	struct _ADAPTER	*adapter
);

boolean
odm_path_diversity_before_link92c(
	/* struct _ADAPTER*	adapter */
	struct PHY_DM_STRUCT		*p_dm
);

void
odm_path_diversity_after_link_92c(
	struct _ADAPTER	*adapter
);

void
odm_set_resp_path_92c(
	struct _ADAPTER	*adapter,
	u8	default_resp_path
);

void
odm_ofdm_tx_path_diversity_92c(
	struct _ADAPTER	*adapter
);

void
odm_cck_tx_path_diversity_92c(
	struct _ADAPTER	*adapter
);

void
odm_reset_path_diversity_92c(
	struct _ADAPTER	*adapter
);

void
odm_cck_tx_path_diversity_callback(
	struct timer_list		*p_timer
);

void
odm_cck_tx_path_diversity_work_item_callback(
	void            *p_context
);

void
odm_path_div_chk_ant_switch_callback(
	struct timer_list		*p_timer
);

void
odm_path_div_chk_ant_switch_workitem_callback(
	void            *p_context
);


void
odm_path_div_chk_ant_switch(
	struct PHY_DM_STRUCT    *p_dm
);

void
odm_cck_path_diversity_chk_per_pkt_rssi(
	struct _ADAPTER		*adapter,
	boolean			is_def_port,
	boolean			is_match_bssid,
	struct _WLAN_STA	*p_entry,
	PRT_RFD			p_rfd,
	u8			*p_desc
);

void
odm_path_div_chk_per_pkt_rssi(
	struct _ADAPTER		*adapter,
	boolean			is_def_port,
	boolean			is_match_bssid,
	struct _WLAN_STA	*p_entry,
	PRT_RFD			p_rfd
);

void
odm_path_div_rest_after_link(
	struct PHY_DM_STRUCT		*p_dm
);

void
odm_fill_tx_path_in_txdesc(
	struct _ADAPTER	*adapter,
	PRT_TCB		p_tcb,
	u8		*p_desc
);

void
odm_path_div_init_92d(
	struct PHY_DM_STRUCT	*p_dm
);

u8
odm_sw_ant_div_select_scan_chnl(
	struct _ADAPTER	*adapter
);

void
odm_sw_ant_div_construct_scan_chnl(
	struct _ADAPTER	*adapter,
	u8		scan_chnl
);

#endif       /* #if(DM_ODM_SUPPORT_TYPE & (ODM_WIN)) */


#endif		 /* #ifndef  __ODMPATHDIV_H__ */

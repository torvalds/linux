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

#ifndef	__PHYDMANTDIV_H__
#define    __PHYDMANTDIV_H__

/*#define ANTDIV_VERSION	"2.0"  //2014.11.04*/
/*#define ANTDIV_VERSION	"2.1"  //2015.01.13  Dino*/
/*#define ANTDIV_VERSION	"2.2"  2015.01.16  Dino*/
/*#define ANTDIV_VERSION	"3.1"  2015.07.29  YuChen, remove 92c 92d 8723a*/
/*#define ANTDIV_VERSION	"3.2"  2015.08.11  Stanley, disable antenna diversity when BT is enable for 8723B*/
/*#define ANTDIV_VERSION	"3.3"  2015.08.12  Stanley. 8723B does not need to check the antenna is control by BT,
							because antenna diversity only works when BT is disable or radio off*/
/*#define ANTDIV_VERSION	"3.4"  2015.08.28  Dino  1.Add 8821A Smart Antenna 2. Add 8188F SW S0S1 Antenna Diversity*/
/*#define ANTDIV_VERSION	"3.5"  2015.10.07  Stanley  Always check antenna detection result from BT-coex. for 8723B, not from PHYDM*/
/*#define ANTDIV_VERSION	"3.6"*/  /*2015.11.16  Stanley  */
/*#define ANTDIV_VERSION	"3.7"*/  /*2015.11.20  Dino Add SmartAnt FAT Patch */
/*#define ANTDIV_VERSION	"3.8"  2015.12.21  Dino, Add SmartAnt dynamic training packet num */
#define ANTDIV_VERSION	"3.9"  /*2016.01.05  Dino, Add SmartAnt cmd for converting single & two smtant, and add cmd for adjust truth table */

/* 1 ============================================================
 * 1  Definition
 * 1 ============================================================ */

#define	ANTDIV_INIT		0xff
#define	MAIN_ANT	1		/*ant A or ant Main   or S1*/
#define	AUX_ANT		2		/*AntB or ant Aux   or S0*/
#define	MAX_ANT		3		/* 3 for AP using*/

#define ANT1_2G 0 /* = ANT2_5G	for 8723D  BTG S1 RX S0S1 diversity for 8723D, TX fixed at S1 */
#define ANT2_2G 1 /* = ANT1_5G	for 8723D  BTG S0  RX S0S1 diversity for 8723D, TX fixed at S1 */
/*smart antenna*/
#define SUPPORT_RF_PATH_NUM 4
#define SUPPORT_BEAM_PATTERN_NUM 4
#define NUM_ANTENNA_8821A	2

#define SUPPORT_BEAM_SET_PATTERN_NUM		8

#define	NO_FIX_TX_ANT		0
#define	FIX_TX_AT_MAIN	1
#define	FIX_AUX_AT_MAIN	2

/* Antenna Diversty Control type */
#define	ODM_AUTO_ANT		0
#define	ODM_FIX_MAIN_ANT	1
#define	ODM_FIX_AUX_ANT	2

#define ODM_N_ANTDIV_SUPPORT		(ODM_RTL8188E | ODM_RTL8192E | ODM_RTL8723B | ODM_RTL8188F | ODM_RTL8723D | ODM_RTL8195A)
#define ODM_AC_ANTDIV_SUPPORT	(ODM_RTL8821 | ODM_RTL8881A | ODM_RTL8812 | ODM_RTL8821C | ODM_RTL8822B | ODM_RTL8814B)
#define ODM_ANTDIV_SUPPORT		(ODM_N_ANTDIV_SUPPORT | ODM_AC_ANTDIV_SUPPORT)
#define ODM_SMART_ANT_SUPPORT	(ODM_RTL8188E | ODM_RTL8192E)
#define ODM_HL_SMART_ANT_TYPE1_SUPPORT		(ODM_RTL8821 | ODM_RTL8822B)

#define ODM_ANTDIV_2G_SUPPORT_IC			(ODM_RTL8188E | ODM_RTL8192E | ODM_RTL8723B | ODM_RTL8881A | ODM_RTL8188F | ODM_RTL8723D)
#define ODM_ANTDIV_5G_SUPPORT_IC			(ODM_RTL8821 | ODM_RTL8881A | ODM_RTL8812 | ODM_RTL8821C)

#define ODM_EVM_ENHANCE_ANTDIV_SUPPORT_IC	(ODM_RTL8192E)

#define ODM_ANTDIV_2G	BIT(0)
#define ODM_ANTDIV_5G	BIT(1)

#define ANTDIV_ON	1
#define ANTDIV_OFF	0

#define FAT_ON	1
#define FAT_OFF	0

#define TX_BY_DESC	1
#define TX_BY_REG	0

#define RSSI_METHOD		0
#define EVM_METHOD		1
#define CRC32_METHOD	2

#define INIT_ANTDIV_TIMMER		0
#define CANCEL_ANTDIV_TIMMER	1
#define RELEASE_ANTDIV_TIMMER	2

#define CRC32_FAIL	1
#define CRC32_OK	0

#define evm_rssi_th_high	25
#define evm_rssi_th_low	20

#define NORMAL_STATE_MIAN	1
#define NORMAL_STATE_AUX	2
#define TRAINING_STATE		3

#define FORCE_RSSI_DIFF 10

#define CSI_ON	1
#define CSI_OFF	0

#define DIVON_CSIOFF 1
#define DIVOFF_CSION 2

#define BDC_DIV_TRAIN_STATE	0
#define bdc_bfer_train_state	1
#define BDC_DECISION_STATE		2
#define BDC_BF_HOLD_STATE		3
#define BDC_DIV_HOLD_STATE		4

#define BDC_MODE_1 1
#define BDC_MODE_2 2
#define BDC_MODE_3 3
#define BDC_MODE_4 4
#define BDC_MODE_NULL 0xff

/*SW S0S1 antenna diversity*/
#define SWAW_STEP_INIT			0xff
#define SWAW_STEP_PEEK		0
#define SWAW_STEP_DETERMINE	1

#define RSSI_CHECK_RESET_PERIOD	10
#define RSSI_CHECK_THRESHOLD		50

/*Hong Lin Smart antenna*/
#define HL_SMTANT_2WIRE_DATA_LEN 24

/* 1 ============================================================
 * 1  structure
 * 1 ============================================================ */


struct _sw_antenna_switch_ {
	u8		double_chk_flag;	/*If current antenna RSSI > "RSSI_CHECK_THRESHOLD", than check this antenna again*/
	u8		try_flag;
	s32		pre_rssi;
	u8		cur_antenna;
	u8		pre_antenna;
	u8		rssi_trying;
	u8		reset_idx;
	u8		train_time;
	u8		train_time_flag; /*base on RSSI difference between two antennas*/
	struct timer_list	phydm_sw_antenna_switch_timer;
	u32		pkt_cnt_sw_ant_div_by_ctrl_frame;
	boolean		is_sw_ant_div_by_ctrl_frame;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if USE_WORKITEM
	RT_WORK_ITEM	phydm_sw_antenna_switch_workitem;
#endif
#endif

	/* AntDect (Before link Antenna Switch check) need to be moved*/
	u16		single_ant_counter;
	u16		dual_ant_counter;
	u16		aux_fail_detec_counter;
	u16		retry_counter;
	u8		swas_no_link_state;
	u32		swas_no_link_bk_reg948;
	boolean		ANTA_ON;	/*To indicate ant A is or not*/
	boolean		ANTB_ON;	/*To indicate ant B is on or not*/
	boolean		pre_aux_fail_detec;
	boolean		rssi_ant_dect_result;
	u8		ant_5g;
	u8		ant_2g;


};


#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
struct _BF_DIV_COEX_ {
	boolean w_bfer_client[ODM_ASSOCIATE_ENTRY_NUM];
	boolean w_bfee_client[ODM_ASSOCIATE_ENTRY_NUM];
	u32	MA_rx_TP[ODM_ASSOCIATE_ENTRY_NUM];
	u32	MA_rx_TP_DIV[ODM_ASSOCIATE_ENTRY_NUM];

	u8  bd_ccoex_type_wbfer;
	u8 num_txbfee_client;
	u8 num_txbfer_client;
	u8 bdc_try_counter;
	u8 bdc_hold_counter;
	u8 bdc_mode;
	u8 bdc_active_mode;
	u8 BDC_state;
	u8 bdc_rx_idle_update_counter;
	u8 num_client;
	u8 pre_num_client;
	u8 num_bf_tar;
	u8 num_div_tar;

	boolean is_all_div_sta_idle;
	boolean is_all_bf_sta_idle;
	boolean bdc_try_flag;
	boolean BF_pass;
	boolean DIV_pass;
};
#endif
#endif

#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1)) || (defined(CONFIG_HL_SMART_ANTENNA_TYPE2))
struct _SMART_ANTENNA_TRAINNING_ {
	u32	latch_time;
	boolean	pkt_skip_statistic_en;
	u32	fix_beam_pattern_en;
	u32	fix_training_num_en;
	u32	fix_beam_pattern_codeword;
	u32	update_beam_codeword;
	u32	ant_num; /*number of "used" smart beam antenna*/
	u32	ant_num_total;/*number of "total" smart beam antenna*/
	u32	first_train_ant; /*decide witch antenna to train first*/

	#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
	u32	pkt_rssi_pre[SUPPORT_RF_PATH_NUM][SUPPORT_BEAM_PATTERN_NUM];/*rssi of each path with a certain beam pattern*/
	u8	beam_train_rssi_diff[SUPPORT_RF_PATH_NUM][SUPPORT_BEAM_PATTERN_NUM];
	u8	beam_train_cnt[SUPPORT_RF_PATH_NUM][SUPPORT_BEAM_PATTERN_NUM];
	u32	rfu_codeword_table[4]; /*2G beam truth table*/
	u32	rfu_codeword_table_5g[4]; /*5G beam truth table*/
	u32	beam_patten_num_each_ant;/*number of  beam can be switched in each antenna*/
	u32	rx_idle_beam[SUPPORT_RF_PATH_NUM];
	#endif
	
	u32	fast_training_beam_num;/*current training beam_set index*/
	u32	pre_fast_training_beam_num;/*pre training beam_set index*/
	u32	rfu_codeword_total_bit_num; /* total bit number of RFU protocol*/
	u32	rfu_each_ant_bit_num; /* bit number of RFU protocol for each ant*/
	u8	per_beam_training_pkt_num;
	u8	decision_holding_period;

	u32	pkt_rssi_sum[8][SUPPORT_BEAM_PATTERN_NUM];
	u32	pkt_rssi_cnt[8][SUPPORT_BEAM_PATTERN_NUM];
	u32	pre_codeword;
	boolean	force_update_beam_en;
	u32	beacon_counter;
	u32	pre_beacon_counter;
	u8	pkt_counter;		/*packet number that each beam-set should be colected in training state*/
	u8	update_beam_idx;	/*the index announce that the beam can be updated*/
	u8	rfu_protocol_type;

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_WORK_ITEM	hl_smart_antenna_workitem;
	RT_WORK_ITEM	hl_smart_antenna_decision_workitem;
	#endif


	#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
	u8	beam_set_avg_rssi_pre[SUPPORT_BEAM_SET_PATTERN_NUM];		/*avg pre_rssi of each beam set*/
	u8	beam_set_train_rssi_diff[SUPPORT_BEAM_SET_PATTERN_NUM];	/*rssi of a beam pattern set, ex: a set = {ant1_beam=1, ant2_beam=3}*/
	u8	beam_set_train_cnt[SUPPORT_BEAM_SET_PATTERN_NUM];			/*training pkt num of each beam set*/
	
	u8	total_beam_set_num;	/*number of  beam set can be switched*/
	u8	total_beam_set_num_2g;/*number of  beam set can be switched in 2G*/
	u8	total_beam_set_num_5g;/*number of  beam set can be switched in 5G*/

	u8	rfu_codeword_table_2g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B]; /*2G beam truth table*/
	u8	rfu_codeword_table_5g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B]; /*5G beam truth table*/
	u8	rx_idle_beam_set_idx;	/*the filanl decsion result*/
	#endif
	

};
#endif

struct _FAST_ANTENNA_TRAINNING_ {
	u8	bssid[6];
	u8	antsel_rx_keep_0;
	u8	antsel_rx_keep_1;
	u8	antsel_rx_keep_2;
	u8	antsel_rx_keep_3;
	u32	ant_sum_rssi[7];
	u32	ant_rssi_cnt[7];
	u32	ant_ave_rssi[7];
	u8	fat_state;
	u32	train_idx;
	u8	antsel_a[ODM_ASSOCIATE_ENTRY_NUM];
	u8	antsel_b[ODM_ASSOCIATE_ENTRY_NUM];
	u8	antsel_c[ODM_ASSOCIATE_ENTRY_NUM];
	u16	main_ant_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u16	aux_ant_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u16	main_ant_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u16	aux_ant_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u16	main_ant_sum_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u16	aux_ant_sum_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u16	main_ant_cnt_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u16	aux_ant_cnt_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u8	rx_idle_ant;
	u8	ant_div_on_off;
	boolean	is_become_linked;
	u32	min_max_rssi;
	u8	idx_ant_div_counter_2g;
	u8	idx_ant_div_counter_5g;
	u8	ant_div_2g_5g;

#ifdef ODM_EVM_ENHANCE_ANTDIV
	u32	main_ant_evm_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32	aux_ant_evm_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32	main_ant_evm_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u32	aux_ant_evm_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	boolean	EVM_method_enable;
	u8	target_ant_evm;
	u8	target_ant_crc32;
	u8	target_ant_enhance;
	u8	pre_target_ant_enhance;
	u16	main_mpdu_ok_cnt;
	u16	aux_mpdu_ok_cnt;

	u32	crc32_ok_cnt;
	u32	crc32_fail_cnt;
	u32	main_crc32_ok_cnt;
	u32	aux_crc32_ok_cnt;
	u32	main_crc32_fail_cnt;
	u32	aux_crc32_fail_cnt;
#endif
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	u32    cck_ctrl_frame_cnt_main;
	u32    cck_ctrl_frame_cnt_aux;
	u32    ofdm_ctrl_frame_cnt_main;
	u32    ofdm_ctrl_frame_cnt_aux;
	u32	main_ant_ctrl_frame_sum;
	u32	aux_ant_ctrl_frame_sum;
	u32	main_ant_ctrl_frame_cnt;
	u32	aux_ant_ctrl_frame_cnt;
#endif
	u8	b_fix_tx_ant;
	boolean	fix_ant_bfee;
	boolean	enable_ctrl_frame_antdiv;
	boolean	use_ctrl_frame_antdiv;
	u8	hw_antsw_occur;
	u8	*p_force_tx_ant_by_desc;
	u8	force_tx_ant_by_desc; /*A temp value, will hook to driver team's outer parameter later*/
	u8    *p_default_s0_s1;
	u8    default_s0_s1;
};


/* 1 ============================================================
 * 1  enumeration
 * 1 ============================================================ */



enum fat_state_e /*Fast antenna training*/
{
	FAT_BEFORE_LINK_STATE	= 0,
	FAT_PREPARE_STATE			= 1,
	FAT_TRAINING_STATE		= 2,
	FAT_DECISION_STATE		= 3
};

enum ant_div_type_e {
	NO_ANTDIV			= 0xFF,
	CG_TRX_HW_ANTDIV			= 0x01,
	CGCS_RX_HW_ANTDIV		= 0x02,
	FIXED_HW_ANTDIV		= 0x03,
	CG_TRX_SMART_ANTDIV	= 0x04,
	CGCS_RX_SW_ANTDIV	= 0x05,
	S0S1_SW_ANTDIV          = 0x06, /*8723B intrnal switch S0 S1*/
	S0S1_TRX_HW_ANTDIV     = 0x07, /*TRX S0S1 diversity for 8723D*/
	HL_SW_SMART_ANT_TYPE1	= 0x10, /*Hong-Lin Smart antenna use for 8821AE which is a 2 ant. entitys, and each ant. is equipped with 4 antenna patterns*/
	HL_SW_SMART_ANT_TYPE2	= 0x11 /*Hong-Bo Smart antenna use for 8822B which is a 2 ant. entitys*/
};


/* 1 ============================================================
 * 1  function prototype
 * 1 ============================================================ */


void
odm_stop_antenna_switch_dm(
	void	*p_dm_void
);

void
phydm_enable_antenna_diversity(
	void			*p_dm_void
);

void
odm_set_ant_config(
	void	*p_dm_void,
	u8		ant_setting	/* 0=A, 1=B, 2=C, .... */
);


#define sw_ant_div_rest_after_link	odm_sw_ant_div_rest_after_link

void odm_sw_ant_div_rest_after_link(
	void	*p_dm_void
);

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))

void
phydm_antdiv_reset_statistic(
	void	*p_dm_void,
	u32	macid
);

void
odm_update_rx_idle_ant(
	void		*p_dm_void,
	u8		ant
);

#if (RTL8723B_SUPPORT == 1)
void
odm_update_rx_idle_ant_8723b(
	void			*p_dm_void,
	u8			ant,
	u32			default_ant,
	u32			optional_ant
);
#endif

#if (RTL8188F_SUPPORT == 1)
void
phydm_update_rx_idle_antenna_8188F(
	void	*p_dm_void,
	u32	default_ant
);
#endif

#if (RTL8723D_SUPPORT == 1)

void
phydm_set_tx_ant_pwr_8723d(
	void			*p_dm_void,
	u8			ant
);

#endif

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
odm_sw_antdiv_callback(
	struct timer_list		*p_timer
);

void
odm_sw_antdiv_workitem_callback(
	void	*p_context
);


#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

void
odm_sw_antdiv_workitem_callback(
	void	*p_context
);

void
odm_sw_antdiv_callback(
	void		*function_context
);

#endif

void
odm_s0s1_sw_ant_div_by_ctrl_frame(
	void			*p_dm_void,
	u8			step
);

void
odm_antsel_statistics_of_ctrl_frame(
	void			*p_dm_void,
	u8			antsel_tr_mux,
	u32			rx_pwdb_all
);

void
odm_s0s1_sw_ant_div_by_ctrl_frame_process_rssi(
	void				*p_dm_void,
	void		*p_phy_info_void,
	void		*p_pkt_info_void
);

#endif

#ifdef ODM_EVM_ENHANCE_ANTDIV
void
odm_evm_fast_ant_training_callback(
	void		*p_dm_void
);
#endif

void
odm_hw_ant_div(
	void		*p_dm_void
);

#if (defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY)) || (defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
void
odm_fast_ant_training(
	void		*p_dm_void
);

void
odm_fast_ant_training_callback(
	void		*p_dm_void
);

void
odm_fast_ant_training_work_item_callback(
	void		*p_dm_void
);
#endif


#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1)) || (defined(CONFIG_HL_SMART_ANTENNA_TYPE2))

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void
phydm_beam_switch_workitem_callback(
	void	*p_context
);

void
phydm_beam_decision_workitem_callback(
	void	*p_context
);

#endif

void
phydm_update_beam_pattern(
	void		*p_dm_void,
	u32		codeword,
	u32		codeword_length
);

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
void
phydm_set_all_ant_same_beam_num(
	void		*p_dm_void
);
#endif

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
void
phydm_set_rfu_beam_pattern_type2(
	void		*p_dm_void
);
#endif

void
phydm_hl_smart_ant_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
);

#endif/*#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1)) || (defined(CONFIG_HL_SMART_ANTENNA_TYPE2))*/

void
odm_ant_div_init(
	void		*p_dm_void
);

void
odm_ant_div(
	void		*p_dm_void
);

void
odm_antsel_statistics(
	void			*p_dm_void,
	u8			antsel_tr_mux,
	u32			mac_id,
	u32			utility,
	u8			method,
	u8			is_cck_rate
);

void
odm_process_rssi_for_ant_div(
	void		*p_dm_void,
	void		*p_phy_info_void,
	void		*p_pkt_info_void
);



#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
void
odm_set_tx_ant_by_tx_info(
	void			*p_dm_void,
	u8			*p_desc,
	u8			mac_id
);

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

struct tx_desc; /*declared tx_desc here or compile error happened when enabled 8822B*/

void
odm_set_tx_ant_by_tx_info(
	struct	rtl8192cd_priv		*priv,
	struct	tx_desc			*pdesc,
	unsigned short			aid
);

#if 1/*def def CONFIG_WLAN_HAL*/
void
odm_set_tx_ant_by_tx_info_hal(
	struct	rtl8192cd_priv		*priv,
	void	*pdesc_data,
	u16		aid
);
#endif	/*#ifdef CONFIG_WLAN_HAL*/
#endif


void
odm_ant_div_config(
	void		*p_dm_void
);

void
odm_ant_div_timers(
	void		*p_dm_void,
	u8		state
);

void
phydm_antdiv_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
);

#endif /*#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))*/

void
odm_ant_div_reset(
	void		*p_dm_void
);

void
odm_antenna_diversity_init(
	void		*p_dm_void
);

void
odm_antenna_diversity(
	void		*p_dm_void
);

#endif /*#ifndef	__ODMANTDIV_H__*/

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

#ifndef __PHYDMSMTANT_H__
#define __PHYDMSMTANT_H__

/*@#define SMT_ANT_VERSION	"1.1"*/ /*@2017.03.13*/
/*@#define SMT_ANT_VERSION	"1.2"*/ /*@2017.03.28*/
#define SMT_ANT_VERSION "2.0" /* @Add Cumitek SmtAnt 2017.05.25*/

#define	SMTANT_RTK		1
#define	SMTANT_HON_BO	2
#define	SMTANT_CUMITEK	3

#if (defined(CONFIG_SMART_ANTENNA))

#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))
struct smt_ant_cumitek {
	u8	tx_ant_idx[2][ODM_ASSOCIATE_ENTRY_NUM]; /*@[pathA~B] [MACID 0~128]*/
	u8	rx_default_ant_idx[2]; /*@[pathA~B]*/
};
#endif

#if (defined(CONFIG_HL_SMART_ANTENNA))
struct smt_ant_honbo {
	u32	latch_time;
	boolean	pkt_skip_statistic_en;
	u32	fix_beam_pattern_en;
	u32	fix_training_num_en;
	u32	fix_beam_pattern_codeword;
	u32	update_beam_codeword;
	u32	ant_num; /*number of "used" smart beam antenna*/
	u32	ant_num_total;/*number of "total" smart beam antenna*/
	u32	first_train_ant; /*@decide witch antenna to train first*/

	#ifdef CONFIG_HL_SMART_ANTENNA_TYPE1
	u32	pkt_rssi_pre[SUPPORT_RF_PATH_NUM][SUPPORT_BEAM_PATTERN_NUM];/*@rssi of each path with a certain beam pattern*/
	u8	beam_train_rssi_diff[SUPPORT_RF_PATH_NUM][SUPPORT_BEAM_PATTERN_NUM];
	u8	beam_train_cnt[SUPPORT_RF_PATH_NUM][SUPPORT_BEAM_PATTERN_NUM];
	u32	rfu_codeword_table[4]; /*@2G beam truth table*/
	u32	rfu_codeword_table_5g[4]; /*@5G beam truth table*/
	u32	beam_patten_num_each_ant;/*@number of  beam can be switched in each antenna*/
	u32	rx_idle_beam[SUPPORT_RF_PATH_NUM];
	u32	pkt_rssi_sum[8][SUPPORT_BEAM_PATTERN_NUM];
	u32	pkt_rssi_cnt[8][SUPPORT_BEAM_PATTERN_NUM];
	#endif

	u32	fast_training_beam_num;/*@current training beam_set index*/
	u32	pre_fast_training_beam_num;/*pre training beam_set index*/
	u32	rfu_codeword_total_bit_num; /* @total bit number of RFU protocol*/
	u32	rfu_each_ant_bit_num; /* @bit number of RFU protocol for each ant*/
	u8	per_beam_training_pkt_num;
	u8	decision_holding_period;


	u32	pre_codeword;
	boolean	force_update_beam_en;
	u32	beacon_counter;
	u32	pre_beacon_counter;
	u8	pkt_counter;		/*@packet number that each beam-set should be colected in training state*/
	u8	update_beam_idx;	/*@the index announce that the beam can be updated*/
	u8	rfu_protocol_type;
	u16	rfu_protocol_delay_time;

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_WORK_ITEM	hl_smart_antenna_workitem;
	RT_WORK_ITEM	hl_smart_antenna_decision_workitem;
	#endif


	#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
	u8	beam_set_avg_rssi_pre[SUPPORT_BEAM_SET_PATTERN_NUM];		/*@avg pre_rssi of each beam set*/
	u8	beam_set_train_val_diff[SUPPORT_BEAM_SET_PATTERN_NUM];	/*@rssi of a beam pattern set, ex: a set = {ant1_beam=1, ant2_beam=3}*/
	u8	beam_set_train_cnt[SUPPORT_BEAM_SET_PATTERN_NUM];			/*@training pkt num of each beam set*/
	u32	beam_set_rssi_avg_sum[SUPPORT_BEAM_SET_PATTERN_NUM];			/*@RSSI_sum of avg(pathA,pathB) for each beam-set)*/
	u32	beam_path_rssi_sum[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B];/*@RSSI_sum of each path for each beam-set)*/

	u8	beam_set_avg_evm_2ss_pre[SUPPORT_BEAM_SET_PATTERN_NUM];
	u32	beam_path_evm_2ss_sum[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B];/*@2SS evm_sum of each path for each beam-set)*/
	u32	beam_path_evm_2ss_cnt[SUPPORT_BEAM_SET_PATTERN_NUM];

	u8	beam_set_avg_evm_1ss_pre[SUPPORT_BEAM_SET_PATTERN_NUM];
	u32	beam_path_evm_1ss_sum[SUPPORT_BEAM_SET_PATTERN_NUM];/*@1SS evm_sum of each path for each beam-set)*/
	u32	beam_path_evm_1ss_cnt[SUPPORT_BEAM_SET_PATTERN_NUM];

	u32	statistic_pkt_cnt[SUPPORT_BEAM_SET_PATTERN_NUM];				/*@statistic_pkt_cnt for SmtAnt make decision*/

	u8	total_beam_set_num;	/*@number of  beam set can be switched*/
	u8	total_beam_set_num_2g;/*@number of  beam set can be switched in 2G*/
	u8	total_beam_set_num_5g;/*@number of  beam set can be switched in 5G*/

	u8	rfu_codeword_table_2g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B]; /*@2G beam truth table*/
	u8	rfu_codeword_table_5g[SUPPORT_BEAM_SET_PATTERN_NUM][MAX_PATH_NUM_8822B]; /*@5G beam truth table*/
	u8	rx_idle_beam_set_idx;	/*the filanl decsion result*/
	#endif


};
#endif /*@#if (defined(CONFIG_HL_SMART_ANTENNA))*/

struct smt_ant {
	u8	smt_ant_vendor;
	u8	smt_ant_type;
	u8	tx_desc_mode; /*@0:3 bit mode, 1:2 bit mode*/
	#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))
	struct	smt_ant_cumitek	cumi_smtant_table;
	#endif
};

#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))
void phydm_cumitek_smt_tx_ant_update(
	void *dm_void,
	u8 tx_ant_idx_path_a,
	u8 tx_ant_idx_path_b,
	u32 mac_id);

void phydm_cumitek_smt_rx_default_ant_update(
	void *dm_void,
	u8 rx_ant_idx_path_a,
	u8 rx_ant_idx_path_b);

void phydm_cumitek_smt_ant_debug(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len);

#endif

#if (defined(CONFIG_HL_SMART_ANTENNA))
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phydm_beam_switch_workitem_callback(
	void *context);

void phydm_beam_decision_workitem_callback(
	void *context);
#endif /*@#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/

#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
void phydm_hl_smart_ant_type2_init_8822b(
	void *dm_void);

void phydm_update_beam_pattern_type2(
	void *dm_void,
	u32 codeword,
	u32 codeword_length);

void phydm_set_rfu_beam_pattern_type2(
	void *dm_void);

void phydm_hl_smt_ant_dbg_type2(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len);

void phydm_process_rssi_for_hb_smtant_type2(
	void *dm_void,
	void *phy_info_void,
	void *pkt_info_void,
	u8 rssi_avg);

#endif /*@#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE2))*/

#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1))

void phydm_update_beam_pattern(
	void *dm_void,
	u32 codeword,
	u32 codeword_length);

void phydm_set_all_ant_same_beam_num(
	void *dm_void);

void phydm_hl_smart_ant_debug(
	void *dm_void,
	char input[][16],
	u32 *_used,
	char *output,
	u32 *_out_len);

#endif /*@#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1))*/
#endif /*@#if (defined(CONFIG_HL_SMART_ANTENNA))*/
void phydm_smt_ant_init(void *dm_void);
#endif /*@#if (defined(CONFIG_SMART_ANTENNA))*/
#endif
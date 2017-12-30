/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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

#ifndef __PHYDMANTDIV_H__
#define __PHYDMANTDIV_H__

/* 2.0 2014.11.04
 * 2.1 2015.01.13 Dino
 * 2.2 2015.01.16 Dino
 * 3.1 2015.07.29 YuChen, remove 92c 92d 8723a
 * 3.2 2015.08.11 Stanley, disable antenna diversity when BT is enable for 8723B
 * 3.3 2015.08.12 Stanley. 8723B does not need to check the antenna is control
 *		  by BT, because antenna diversity only works when BT is disable
 *		  or radio off
 * 3.4 2015.08.28 Dino  1.Add 8821A Smart Antenna 2. Add 8188F SW S0S1 Antenna
 *		  Diversity
 * 3.5 2015.10.07 Stanley  Always check antenna detection result from BT-coex.
 *		  for 8723B, not from PHYDM
 * 3.6 2015.11.16 Stanley
 * 3.7 2015.11.20 Dino Add SmartAnt FAT Patch
 * 3.8 2015.12.21 Dino, Add SmartAnt dynamic training packet num
 * 3.9 2016.01.05 Dino, Add SmartAnt cmd for converting single & two smtant, and
 *		  add cmd for adjust truth table
 */
#define ANTDIV_VERSION "3.9"

/* 1 ============================================================
 * 1  Definition
 * 1 ============================================================
 */

#define ANTDIV_INIT 0xff
#define MAIN_ANT 1 /*ant A or ant Main   or S1*/
#define AUX_ANT 2 /*AntB or ant Aux   or S0*/
#define MAX_ANT 3 /* 3 for AP using*/

#define ANT1_2G 0 /* = ANT2_5G	for 8723D  BTG S1 RX S0S1 diversity for 8723D,
		   * TX fixed at S1
		   */
#define ANT2_2G 1 /* = ANT1_5G	for 8723D  BTG S0  RX S0S1 diversity for 8723D,
		   * TX fixed at S1
		   */
/*smart antenna*/
#define SUPPORT_RF_PATH_NUM 4
#define SUPPORT_BEAM_PATTERN_NUM 4
#define NUM_ANTENNA_8821A 2

#define SUPPORT_BEAM_SET_PATTERN_NUM 8

#define NO_FIX_TX_ANT 0
#define FIX_TX_AT_MAIN 1
#define FIX_AUX_AT_MAIN 2

/* Antenna Diversty Control type */
#define ODM_AUTO_ANT 0
#define ODM_FIX_MAIN_ANT 1
#define ODM_FIX_AUX_ANT 2

#define ODM_N_ANTDIV_SUPPORT                                                   \
	(ODM_RTL8188E | ODM_RTL8192E | ODM_RTL8723B | ODM_RTL8188F |           \
	 ODM_RTL8723D | ODM_RTL8195A)
#define ODM_AC_ANTDIV_SUPPORT                                                  \
	(ODM_RTL8821 | ODM_RTL8881A | ODM_RTL8812 | ODM_RTL8821C |             \
	 ODM_RTL8822B | ODM_RTL8814B)
#define ODM_ANTDIV_SUPPORT (ODM_N_ANTDIV_SUPPORT | ODM_AC_ANTDIV_SUPPORT)
#define ODM_SMART_ANT_SUPPORT (ODM_RTL8188E | ODM_RTL8192E)
#define ODM_HL_SMART_ANT_TYPE1_SUPPORT (ODM_RTL8821 | ODM_RTL8822B)

#define ODM_ANTDIV_2G_SUPPORT_IC                                               \
	(ODM_RTL8188E | ODM_RTL8192E | ODM_RTL8723B | ODM_RTL8881A |           \
	 ODM_RTL8188F | ODM_RTL8723D)
#define ODM_ANTDIV_5G_SUPPORT_IC                                               \
	(ODM_RTL8821 | ODM_RTL8881A | ODM_RTL8812 | ODM_RTL8821C)

#define ODM_EVM_ENHANCE_ANTDIV_SUPPORT_IC (ODM_RTL8192E)

#define ODM_ANTDIV_2G BIT(0)
#define ODM_ANTDIV_5G BIT(1)

#define ANTDIV_ON 1
#define ANTDIV_OFF 0

#define FAT_ON 1
#define FAT_OFF 0

#define TX_BY_DESC 1
#define TX_BY_REG 0

#define RSSI_METHOD 0
#define EVM_METHOD 1
#define CRC32_METHOD 2

#define INIT_ANTDIV_TIMMER 0
#define CANCEL_ANTDIV_TIMMER 1
#define RELEASE_ANTDIV_TIMMER 2

#define CRC32_FAIL 1
#define CRC32_OK 0

#define evm_rssi_th_high 25
#define evm_rssi_th_low 20

#define NORMAL_STATE_MIAN 1
#define NORMAL_STATE_AUX 2
#define TRAINING_STATE 3

#define FORCE_RSSI_DIFF 10

#define CSI_ON 1
#define CSI_OFF 0

#define DIVON_CSIOFF 1
#define DIVOFF_CSION 2

#define BDC_DIV_TRAIN_STATE 0
#define bdc_bfer_train_state 1
#define BDC_DECISION_STATE 2
#define BDC_BF_HOLD_STATE 3
#define BDC_DIV_HOLD_STATE 4

#define BDC_MODE_1 1
#define BDC_MODE_2 2
#define BDC_MODE_3 3
#define BDC_MODE_4 4
#define BDC_MODE_NULL 0xff

/*SW S0S1 antenna diversity*/
#define SWAW_STEP_INIT 0xff
#define SWAW_STEP_PEEK 0
#define SWAW_STEP_DETERMINE 1

#define RSSI_CHECK_RESET_PERIOD 10
#define RSSI_CHECK_THRESHOLD 50

/*Hong Lin Smart antenna*/
#define HL_SMTANT_2WIRE_DATA_LEN 24

/* 1 ============================================================
 * 1  structure
 * 1 ============================================================
 */

struct sw_antenna_switch {
	u8 double_chk_flag; /*If current antenna RSSI > "RSSI_CHECK_THRESHOLD",
			     *than check this antenna again
			     */
	u8 try_flag;
	s32 pre_rssi;
	u8 cur_antenna;
	u8 pre_antenna;
	u8 rssi_trying;
	u8 reset_idx;
	u8 train_time;
	u8 train_time_flag; /*base on RSSI difference between two antennas*/
	struct timer_list phydm_sw_antenna_switch_timer;
	u32 pkt_cnt_sw_ant_div_by_ctrl_frame;
	bool is_sw_ant_div_by_ctrl_frame;

	/* AntDect (Before link Antenna Switch check) need to be moved*/
	u16 single_ant_counter;
	u16 dual_ant_counter;
	u16 aux_fail_detec_counter;
	u16 retry_counter;
	u8 swas_no_link_state;
	u32 swas_no_link_bk_reg948;
	bool ANTA_ON; /*To indicate ant A is or not*/
	bool ANTB_ON; /*To indicate ant B is on or not*/
	bool pre_aux_fail_detec;
	bool rssi_ant_dect_result;
	u8 ant_5g;
	u8 ant_2g;
};

struct fast_antenna_training {
	u8 bssid[6];
	u8 antsel_rx_keep_0;
	u8 antsel_rx_keep_1;
	u8 antsel_rx_keep_2;
	u8 antsel_rx_keep_3;
	u32 ant_sum_rssi[7];
	u32 ant_rssi_cnt[7];
	u32 ant_ave_rssi[7];
	u8 fat_state;
	u32 train_idx;
	u8 antsel_a[ODM_ASSOCIATE_ENTRY_NUM];
	u8 antsel_b[ODM_ASSOCIATE_ENTRY_NUM];
	u8 antsel_c[ODM_ASSOCIATE_ENTRY_NUM];
	u16 main_ant_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u16 aux_ant_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u16 main_ant_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u16 aux_ant_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u16 main_ant_sum_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u16 aux_ant_sum_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u16 main_ant_cnt_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u16 aux_ant_cnt_cck[ODM_ASSOCIATE_ENTRY_NUM];
	u8 rx_idle_ant;
	u8 ant_div_on_off;
	bool is_become_linked;
	u32 min_max_rssi;
	u8 idx_ant_div_counter_2g;
	u8 idx_ant_div_counter_5g;
	u8 ant_div_2g_5g;

	u32 cck_ctrl_frame_cnt_main;
	u32 cck_ctrl_frame_cnt_aux;
	u32 ofdm_ctrl_frame_cnt_main;
	u32 ofdm_ctrl_frame_cnt_aux;
	u32 main_ant_ctrl_frame_sum;
	u32 aux_ant_ctrl_frame_sum;
	u32 main_ant_ctrl_frame_cnt;
	u32 aux_ant_ctrl_frame_cnt;
	u8 b_fix_tx_ant;
	bool fix_ant_bfee;
	bool enable_ctrl_frame_antdiv;
	bool use_ctrl_frame_antdiv;
	u8 hw_antsw_occur;
	u8 *p_force_tx_ant_by_desc;
	u8 force_tx_ant_by_desc; /*A temp value, will hook to driver team's
				  *outer parameter later
				  */
	u8 *p_default_s0_s1;
	u8 default_s0_s1;
};

/* 1 ============================================================
 * 1  enumeration
 * 1 ============================================================
 */

/*Fast antenna training*/
enum fat_state {
	FAT_BEFORE_LINK_STATE = 0,
	FAT_PREPARE_STATE = 1,
	FAT_TRAINING_STATE = 2,
	FAT_DECISION_STATE = 3
};

enum ant_div_type {
	NO_ANTDIV = 0xFF,
	CG_TRX_HW_ANTDIV = 0x01,
	CGCS_RX_HW_ANTDIV = 0x02,
	FIXED_HW_ANTDIV = 0x03,
	CG_TRX_SMART_ANTDIV = 0x04,
	CGCS_RX_SW_ANTDIV = 0x05,
	/*8723B intrnal switch S0 S1*/
	S0S1_SW_ANTDIV = 0x06,
	/*TRX S0S1 diversity for 8723D*/
	S0S1_TRX_HW_ANTDIV = 0x07,
	/*Hong-Lin Smart antenna use for 8821AE which is a 2 ant. entitys, and
	 *each ant. is equipped with 4 antenna patterns
	 */
	HL_SW_SMART_ANT_TYPE1 = 0x10,
	/*Hong-Bo Smart antenna use for 8822B which is a 2 ant. entitys*/
	HL_SW_SMART_ANT_TYPE2 = 0x11,
};

/* 1 ============================================================
 * 1  function prototype
 * 1 ============================================================
 */

void odm_stop_antenna_switch_dm(void *dm_void);

void phydm_enable_antenna_diversity(void *dm_void);

void odm_set_ant_config(void *dm_void, u8 ant_setting /* 0=A, 1=B, 2=C, .... */
			);

#define sw_ant_div_rest_after_link odm_sw_ant_div_rest_after_link

void odm_sw_ant_div_rest_after_link(void *dm_void);

void odm_ant_div_reset(void *dm_void);

void odm_antenna_diversity_init(void *dm_void);

void odm_antenna_diversity(void *dm_void);

#endif /*#ifndef	__ODMANTDIV_H__*/

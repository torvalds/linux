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

#ifndef __INC_PHYDM_BEAMFORMING_H
#define __INC_PHYDM_BEAMFORMING_H

/*@Beamforming Related*/
#include "txbf/halcomtxbf.h"
#include "txbf/haltxbfjaguar.h"
#include "txbf/haltxbf8192e.h"
#include "txbf/haltxbf8814a.h"
#include "txbf/haltxbf8822b.h"
#include "txbf/haltxbfinterface.h"

#ifdef PHYDM_BEAMFORMING_SUPPORT

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#define eq_mac_addr(a, b) (((a)[0] == (b)[0] && (a)[1] == (b)[1] && (a)[2] == (b)[2] && (a)[3] == (b)[3] && (a)[4] == (b)[4] && (a)[5] == (b)[5]) ? 1 : 0)
#define cp_mac_addr(des, src) ((des)[0] = (src)[0], (des)[1] = (src)[1], (des)[2] = (src)[2], (des)[3] = (src)[3], (des)[4] = (src)[4], (des)[5] = (src)[5])

#endif

#define MAX_BEAMFORMEE_SU 2
#define MAX_BEAMFORMER_SU 2
#if ((RTL8822B_SUPPORT == 1) || (RTL8812F_SUPPORT == 1))
#define MAX_BEAMFORMEE_MU 6
#define MAX_BEAMFORMER_MU 1
#else
#define MAX_BEAMFORMEE_MU 0
#define MAX_BEAMFORMER_MU 0
#endif

#define BEAMFORMEE_ENTRY_NUM (MAX_BEAMFORMEE_SU + MAX_BEAMFORMEE_MU)
#define BEAMFORMER_ENTRY_NUM (MAX_BEAMFORMER_SU + MAX_BEAMFORMER_MU)

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/*@for different naming between WIN and CE*/
#define BEACON_QUEUE BCN_QUEUE_INX
#define NORMAL_QUEUE MGT_QUEUE_INX
#define RT_DISABLE_FUNC RTW_DISABLE_FUNC
#define RT_ENABLE_FUNC RTW_ENABLE_FUNC
#endif

enum beamforming_entry_state {
	BEAMFORMING_ENTRY_STATE_UNINITIALIZE,
	BEAMFORMING_ENTRY_STATE_INITIALIZEING,
	BEAMFORMING_ENTRY_STATE_INITIALIZED,
	BEAMFORMING_ENTRY_STATE_PROGRESSING,
	BEAMFORMING_ENTRY_STATE_PROGRESSED
};

enum beamforming_notify_state {
	BEAMFORMING_NOTIFY_NONE,
	BEAMFORMING_NOTIFY_ADD,
	BEAMFORMING_NOTIFY_DELETE,
	BEAMFORMEE_NOTIFY_ADD_SU,
	BEAMFORMEE_NOTIFY_DELETE_SU,
	BEAMFORMEE_NOTIFY_ADD_MU,
	BEAMFORMEE_NOTIFY_DELETE_MU,
	BEAMFORMING_NOTIFY_RESET
};

enum beamforming_cap {
	BEAMFORMING_CAP_NONE = 0x0,
	BEAMFORMER_CAP_HT_EXPLICIT = BIT(1),
	BEAMFORMEE_CAP_HT_EXPLICIT = BIT(2),
	BEAMFORMER_CAP_VHT_SU = BIT(5), /* @Self has er Cap, because Reg er  & peer ee */
	BEAMFORMEE_CAP_VHT_SU = BIT(6), /* @Self has ee Cap, because Reg ee & peer er */
	BEAMFORMER_CAP_VHT_MU = BIT(7), /* @Self has er Cap, because Reg er  & peer ee */
	BEAMFORMEE_CAP_VHT_MU = BIT(8), /* @Self has ee Cap, because Reg ee & peer er */
	BEAMFORMER_CAP = BIT(9),
	BEAMFORMEE_CAP = BIT(10),
};

enum sounding_mode {
	SOUNDING_SW_VHT_TIMER = 0x0,
	SOUNDING_SW_HT_TIMER = 0x1,
	sounding_stop_all_timer = 0x2,
	SOUNDING_HW_VHT_TIMER = 0x3,
	SOUNDING_HW_HT_TIMER = 0x4,
	SOUNDING_STOP_OID_TIMER = 0x5,
	SOUNDING_AUTO_VHT_TIMER = 0x6,
	SOUNDING_AUTO_HT_TIMER = 0x7,
	SOUNDING_FW_VHT_TIMER = 0x8,
	SOUNDING_FW_HT_TIMER = 0x9,
};

struct _RT_BEAMFORM_STAINFO {
	u8 *ra;
	u16 aid;
	u16 mac_id;
	u8 my_mac_addr[6];
	/*WIRELESS_MODE				wireless_mode;*/
	enum channel_width bw;
	enum beamforming_cap beamform_cap;
	u8 ht_beamform_cap;
	u16 vht_beamform_cap;
	u8 cur_beamform;
	u16 cur_beamform_vht;
};

struct _RT_BEAMFORMEE_ENTRY {
	boolean is_used;
	boolean is_txbf;
	boolean is_sound;
	u16 aid; /*Used to construct AID field of NDPA packet.*/
	u16 mac_id; /*Used to Set Reg42C in IBSS mode. */
	u16 p_aid; /*@Used to fill Reg42C & Reg714 to compare with P_AID of Tx DESC. */
	u8 g_id; /*Used to fill Tx DESC*/
	u8 my_mac_addr[6];
	u8 mac_addr[6]; /*@Used to fill Reg6E4 to fill Mac address of CSI report frame.*/
	enum channel_width sound_bw; /*Sounding band_width*/
	u16 sound_period;
	enum beamforming_cap beamform_entry_cap;
	enum beamforming_entry_state beamform_entry_state;
	boolean is_beamforming_in_progress;
	/*@u8	log_seq;									// Move to _RT_BEAMFORMER_ENTRY*/
	/*@u16	log_retry_cnt:3;		// 0~4				// Move to _RT_BEAMFORMER_ENTRY*/
	/*@u16	LogSuccessCnt:2;		// 0~2				// Move to _RT_BEAMFORMER_ENTRY*/
	u16 log_status_fail_cnt : 5; /* @0~21 */
	u16 default_csi_cnt : 5; /* @0~21 */
	u8 csi_matrix[327];
	u16 csi_matrix_len;
	u8 num_of_sounding_dim;
	u8 comp_steering_num_of_bfer;
	u8 su_reg_index;
	/*@For MU-MIMO*/
	boolean is_mu_sta;
	u8 mu_reg_index;
	u8 gid_valid[8];
	u8 user_position[16];
};

struct _RT_BEAMFORMER_ENTRY {
	boolean is_used;
	/*P_AID of BFer entry is probably not used*/
	u16 p_aid; /*@Used to fill Reg42C & Reg714 to compare with P_AID of Tx DESC. */
	u8 g_id;
	u8 my_mac_addr[6];
	u8 mac_addr[6];
	enum beamforming_cap beamform_entry_cap;
	u8 num_of_sounding_dim;
	u8 clock_reset_times; /*@Modified by Jeffery @2015-04-10*/
	u8 pre_log_seq; /*@Modified by Jeffery @2015-03-30*/
	u8 log_seq; /*@Modified by Jeffery @2014-10-29*/
	u16 log_retry_cnt : 3; /*@Modified by Jeffery @2014-10-29*/
	u16 log_success : 2; /*@Modified by Jeffery @2014-10-29*/
	u8 su_reg_index;
	/*@For MU-MIMO*/
	boolean is_mu_ap;
	u8 gid_valid[8];
	u8 user_position[16];
	u16 aid;
};

struct _RT_SOUNDING_INFO {
	u8 sound_idx;
	enum channel_width sound_bw;
	enum sounding_mode sound_mode;
	u16 sound_period;
};

struct _RT_BEAMFORMING_OID_INFO {
	u8 sound_oid_idx;
	enum channel_width sound_oid_bw;
	enum sounding_mode sound_oid_mode;
	u16 sound_oid_period;
};

struct _RT_BEAMFORMING_INFO {
	enum beamforming_cap beamform_cap;
	struct _RT_BEAMFORMEE_ENTRY beamformee_entry[BEAMFORMEE_ENTRY_NUM];
	struct _RT_BEAMFORMER_ENTRY beamformer_entry[BEAMFORMER_ENTRY_NUM];
	struct _RT_BEAMFORM_STAINFO beamform_sta_info;
	u8 beamformee_cur_idx;
	struct phydm_timer_list beamforming_timer;
	struct phydm_timer_list mu_timer;
	struct _RT_SOUNDING_INFO sounding_info;
	struct _RT_BEAMFORMING_OID_INFO beamforming_oid_info;
	struct _HAL_TXBF_INFO txbf_info;
	u8 sounding_sequence;
	u8 beamformee_su_cnt;
	u8 beamformer_su_cnt;
	u32 beamformee_su_reg_maping;
	u32 beamformer_su_reg_maping;
	/*@For MU-MINO*/
	u8 beamformee_mu_cnt;
	u8 beamformer_mu_cnt;
	u32 beamformee_mu_reg_maping;
	u8 mu_ap_index;
	boolean is_mu_sounding;
	u8 first_mu_bfee_index;
	boolean is_mu_sounding_in_progress;
	boolean dbg_disable_mu_tx;
	boolean apply_v_matrix;
	boolean snding3ss;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *source_adapter;
#endif
	/* @Control register */
	u32 reg_mu_tx_ctrl; /* @For USB/SDIO interfaces aync I/O */
	u8 tx_bf_data_rate;
	u8 last_usb_hub;
};

void phydm_get_txbf_device_num(
	void *dm_void,
	u8 macid);

struct _RT_NDPA_STA_INFO {
	u16 aid : 12;
	u16 feedback_type : 1;
	u16 nc_index : 3;
};

enum phydm_acting_type {
	phydm_acting_as_ibss = 0,
	phydm_acting_as_ap = 1
};

enum beamforming_cap
phydm_beamforming_get_entry_beam_cap_by_mac_id(
	void *dm_void,
	u8 mac_id);

struct _RT_BEAMFORMEE_ENTRY *
phydm_beamforming_get_bfee_entry_by_addr(
	void *dm_void,
	u8 *RA,
	u8 *idx);

struct _RT_BEAMFORMER_ENTRY *
phydm_beamforming_get_bfer_entry_by_addr(
	void *dm_void,
	u8 *TA,
	u8 *idx);

void phydm_beamforming_notify(
	void *dm_void);

boolean
phydm_acting_determine(
	void *dm_void,
	enum phydm_acting_type type);

void beamforming_enter(void *dm_void, u16 sta_idx, u8 *my_mac_addr);

void beamforming_leave(
	void *dm_void,
	u8 *RA);

boolean
beamforming_start_fw(
	void *dm_void,
	u8 idx);

void beamforming_check_sounding_success(
	void *dm_void,
	boolean status);

void phydm_beamforming_end_sw(
	void *dm_void,
	boolean status);

void beamforming_timer_callback(
	void *dm_void);

void phydm_beamforming_init(
	void *dm_void);

enum beamforming_cap
phydm_beamforming_get_beam_cap(
	void *dm_void,
	struct _RT_BEAMFORMING_INFO *beam_info);

enum beamforming_cap
phydm_get_beamform_cap(
	void *dm_void);

boolean
beamforming_control_v1(
	void *dm_void,
	u8 *RA,
	u8 AID,
	u8 mode,
	enum channel_width BW,
	u8 rate);

boolean
phydm_beamforming_control_v2(
	void *dm_void,
	u8 idx,
	u8 mode,
	enum channel_width BW,
	u16 period);

void phydm_beamforming_watchdog(
	void *dm_void);

void beamforming_sw_timer_callback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct phydm_timer_list *timer
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	void *function_context
#endif
	);

boolean
beamforming_send_ht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	enum channel_width BW,
	u8 q_idx);

boolean
beamforming_send_vht_ndpa_packet(
	void *dm_void,
	u8 *RA,
	u16 AID,
	enum channel_width BW,
	u8 q_idx);

#else
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_AP))
#define beamforming_gid_paid(adapter, tcb)
#define phydm_acting_determine(dm, type) false
#define beamforming_enter(dm, sta_idx, my_mac_addr)
#define beamforming_leave(dm, RA)
#define beamforming_end_fw(dm)
#define beamforming_control_v1(dm, RA, AID, mode, BW, rate) true
#define beamforming_control_v2(dm, idx, mode, BW, period) true
#define phydm_beamforming_end_sw(dm, _status)
#define beamforming_timer_callback(dm)
#define phydm_beamforming_init(dm)
#define phydm_beamforming_control_v2(dm, _idx, _mode, _BW, _period) false
#define beamforming_watchdog(dm)
#define phydm_beamforming_watchdog(dm)
#endif /*@(DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))*/
#endif /*@#ifdef PHYDM_BEAMFORMING_SUPPORT*/
#endif

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
#ifndef __PHYDMSOML_H__
#define __PHYDMSOML_H__

/*@#define ADAPTIVE_SOML_VERSION	"1.0" Byte counter version*/
#define ADAPTIVE_SOML_VERSION "2.0" /*@add avg. phy rate decision 20180126*/

#define PHYDM_ADAPTIVE_SOML_IC	(ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8192F)
/*@jj add 20170822*/

#define INIT_SOML_TIMMER			0
#define CANCEL_SOML_TIMMER			1
#define RELEASE_SOML_TIMMER		2

#define SOML_RSSI_TH_HIGH	25
#define SOML_RSSI_TH_LOW	20

#define HT_RATE_IDX			16
#define VHT_RATE_IDX		20

#define HT_ORDER_TYPE		3
#define VHT_ORDER_TYPE		4

#define CRC_FAIL	1
#define CRC_OK		0

#if 0
#define CFO_QPSK_TH			20
#define CFO_QAM16_TH		20
#define CFO_QAM64_TH		20
#define CFO_QAM256_TH		20

#define BPSK_QPSK_DIST		20
#define QAM16_DIST			30
#define QAM64_DIST			30
#define QAM256_DIST			20
#endif
#define HT_TYPE		1
#define VHT_TYPE		2

#define SOML_ON		1
#define SOML_OFF		0

#ifdef CONFIG_ADAPTIVE_SOML

struct adaptive_soml {
	u32			rvrt_val; /*all rvrt_val for pause API must set to u32*/
	boolean			is_soml_method_enable;
	boolean			get_stats;
	u8			soml_on_off;
	u8			soml_state_cnt;
	u8			soml_delay_time;
	u8			soml_intvl;
	u8			soml_train_num;
	u8			soml_counter;
	u8			soml_period;
	u8			soml_select;
	u8			soml_last_state;
	u8			cfo_qpsk_th;
	u8			cfo_qam16_th;
	u8			cfo_qam64_th;
	u8			cfo_qam256_th;
	u8			bpsk_qpsk_dist_th;
	u8			qam16_dist_th;
	u8			qam64_dist_th;
	u8			qam256_dist_th;
	u8			cfo_cnt;
	s32			cfo_diff_a;
	s32			cfo_diff_b;
	s32			cfo_diff_sum_a;
	s32			cfo_diff_sum_b;
	s32			cfo_diff_avg_a;
	s32			cfo_diff_avg_b;
	u16			ht_cnt[HT_RATE_IDX];
	u16			pre_ht_cnt[HT_RATE_IDX];
	u16			ht_cnt_on[HT_RATE_IDX];
	u16			ht_cnt_off[HT_RATE_IDX];
	u16			ht_crc_ok_cnt_on[HT_RATE_IDX];
	u16			ht_crc_fail_cnt_on[HT_RATE_IDX];
	u16			ht_crc_ok_cnt_off[HT_RATE_IDX];
	u16			ht_crc_fail_cnt_off[HT_RATE_IDX];
	u16			vht_crc_ok_cnt_on[VHT_RATE_IDX];
	u16			vht_crc_fail_cnt_on[VHT_RATE_IDX];
	u16			vht_crc_ok_cnt_off[VHT_RATE_IDX];
	u16			vht_crc_fail_cnt_off[VHT_RATE_IDX];

	u16			vht_cnt[VHT_RATE_IDX];
	u16			pre_vht_cnt[VHT_RATE_IDX];
	u16			vht_cnt_on[VHT_RATE_IDX];
	u16			vht_cnt_off[VHT_RATE_IDX];

	u16			num_ht_qam[HT_ORDER_TYPE];
	u16			ht_byte[HT_RATE_IDX];
	u16			pre_ht_byte[HT_RATE_IDX];
	u16			ht_byte_on[HT_RATE_IDX];
	u16			ht_byte_off[HT_RATE_IDX];
	u16			num_vht_qam[VHT_ORDER_TYPE];
	u16			vht_byte[VHT_RATE_IDX];
	u16			pre_vht_byte[VHT_RATE_IDX];
	u16			vht_byte_on[VHT_RATE_IDX];
	u16			vht_byte_off[VHT_RATE_IDX];

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if USE_WORKITEM
	RT_WORK_ITEM	phydm_adaptive_soml_workitem;
#endif
#endif
	struct phydm_timer_list		phydm_adaptive_soml_timer;

};

enum qam_order {
	BPSK_QPSK	= 0,
	QAM16		= 1,
	QAM64		= 2,
	QAM256		= 3
};

void phydm_dynamicsoftmletting(void *dm_void);

void phydm_soml_on_off(void *dm_void, u8 swch);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void phydm_adaptive_soml_callback(struct phydm_timer_list *timer);

void phydm_adaptive_soml_workitem_callback(void *context);

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
void phydm_adaptive_soml_callback(void *dm_void);

void phydm_adaptive_soml_workitem_callback(void *context);

#else
void phydm_adaptive_soml_callback(void *dm_void);
#endif

void phydm_rx_rate_for_soml(void *dm_void, void *pkt_info_void);

void phydm_rx_qam_for_soml(void *dm_void, void *pkt_info_void);

void phydm_soml_reset_rx_rate(void *dm_void);

void phydm_soml_reset_qam(void *dm_void);

void phydm_soml_cfo_process(void *dm_void, s32 *diff_a, s32 *diff_b);

void phydm_soml_debug(void *dm_void, char input[][16], u32 *_used,
		      char *output, u32 *_out_len);

void phydm_soml_statistics(void *dm_void, u8 on_off_state);

void phydm_adsl(void *dm_void);

void phydm_adaptive_soml_reset(void *dm_void);

void phydm_set_adsl_val(void *dm_void, u32 *val_buf, u8 val_len);

void phydm_soml_crc_acq(void *dm_void, u8 rate_id, boolean crc32, u32 length);

void phydm_soml_bytes_acq(void *dm_void, u8 rate_id, u32 length);

void phydm_adaptive_soml_timers(void *dm_void, u8 state);

void phydm_adaptive_soml_init(void *dm_void);

void phydm_adaptive_soml(void *dm_void);

void phydm_enable_adaptive_soml(void *dm_void);

void phydm_stop_adaptive_soml(void *dm_void);

void phydm_adaptive_soml_para_set(void *dm_void, u8 train_num, u8 intvl,
				  u8 period, u8 delay_time);
#endif
void phydm_init_soft_ml_setting(void *dm_void);
#endif /*@#ifndef	__PHYDMSOML_H__*/

/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __RTW_RM_H_
#define __RTW_RM_H_

u8 rm_post_event_hdl(_adapter *padapter, u8 *pbuf);

#define RM_TIMER_NUM 		32
#define RM_ALL_MEAS		BIT(1)
#define RM_ID_FOR_ALL(aid)	((aid<<16)|RM_ALL_MEAS)

#define RM_CAP_ARG(x) ((u8 *)(x))[4], ((u8 *)(x))[3], ((u8 *)(x))[2], ((u8 *)(x))[1], ((u8 *)(x))[0]
#define RM_CAP_FMT "%02x %02x%02x %02x%02x"

/* remember to modify rm_event_name() when adding new event */
enum RM_EV_ID {
	RM_EV_state_in,
	RM_EV_busy_timer_expire,
	RM_EV_delay_timer_expire,
	RM_EV_meas_timer_expire,
	RM_EV_retry_timer_expire,
	RM_EV_repeat_delay_expire,
	RM_EV_request_timer_expire,
	RM_EV_wait_report,
	RM_EV_start_meas,
	RM_EV_survey_done,
	RM_EV_recv_rep,
	RM_EV_cancel,
	RM_EV_state_out,
	RM_EV_max
};

struct rm_event {
	u32 rmid;
	enum RM_EV_ID evid;
	_list list;
};

#ifdef CONFIG_RTW_80211K

struct rm_clock {
	struct rm_obj *prm;
	ATOMIC_T counter;
	enum RM_EV_ID evid;
};

struct rm_priv {
	u8 enable;
	_queue ev_queue;
	_queue rm_queue;
	_timer rm_timer;

	struct rm_clock clock[RM_TIMER_NUM];
	u8 rm_en_cap_def[5];
	u8 rm_en_cap_assoc[5];

	u8 meas_token;
	/* rm debug */
	void *prm_sel;
};

#define	MAX_CH_NUM_IN_OP_CLASS	11
typedef struct _RT_OPERATING_CLASS {
	int	global_op_class;
	int	Len;
	u8	Channel[MAX_CH_NUM_IN_OP_CLASS];
} RT_OPERATING_CLASS, *PRT_OPERATING_CLASS;

int rtw_init_rm(_adapter *padapter);
int rtw_free_rm_priv(_adapter *padapter);

unsigned int rm_on_action(_adapter *padapter, union recv_frame *precv_frame);
void RM_IE_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE);
void rtw_ap_parse_sta_rm_en_cap(_adapter *padapter,
	struct sta_info *psta, struct rtw_ieee802_11_elems *elems);

int rm_post_event(_adapter *padapter, u32 rmid, enum RM_EV_ID evid);
void rm_handler(_adapter *padapter, struct rm_event *pev);

u8 rm_add_nb_req(_adapter *padapter, struct sta_info *psta);

/* from ioctl */
int rm_send_bcn_reqs(_adapter *padapter, u8 *sta_addr, u8 op_class, u8 ch,
	u16 measure_duration, u8 measure_mode, u8 *bssid, u8 *ssid,
	u8 reporting_detail,
	u8 n_ap_ch_rpt, struct _RT_OPERATING_CLASS *rpt,
	u8 n_elem_id, u8 *elem_id_list);
void indicate_beacon_report(u8 *sta_addr,
	u8 n_measure_rpt, u32 elem_len, u8 *elem);

#endif /*CONFIG_RTW_80211K */
#endif /* __RTW_RM_H_ */

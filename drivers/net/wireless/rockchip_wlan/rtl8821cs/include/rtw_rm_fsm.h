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

#ifndef __RTW_RM_FSM_H_
#define __RTW_RM_FSM_H_

#ifdef CONFIG_RTW_80211K

#define RM_SUPPORT_IWPRIV_DBG	1
#define RM_MORE_DBG_MSG		0

#define DBG_BCN_REQ_DETAIL	0
#define DBG_BCN_REQ_WILDCARD	0
#define DBG_BCN_REQ_SSID	0
#define DBG_BCN_REQ_SSID_NAME	"RealKungFu"

#define RM_REQ_TIMEOUT		10000	/* 10 seconds */
#define RM_MEAS_TIMEOUT		10000	/* 10 seconds */
#define RM_REPT_SCAN_INTVL	5000	/*  5 seconds */
#define RM_REPT_POLL_INTVL	2000	/*  2 seconds */
#define RM_COND_INTVL		2000	/*  2 seconds */
#define RM_SCAN_DENY_TIMES	10
#define RM_BUSY_TRAFFIC_TIMES	10
#define RM_WAIT_BUSY_TIMEOUT	1000	/*  1 seconds */

#define MEAS_REQ_MOD_PARALLEL	BIT(0)
#define MEAS_REQ_MOD_ENABLE	BIT(1)
#define MEAS_REQ_MOD_REQUEST	BIT(2)
#define MEAS_REQ_MOD_REPORT	BIT(3)
#define MEAS_REQ_MOD_DUR_MAND	BIT(4)

#define MEAS_REP_MOD_LATE	BIT(0)
#define MEAS_REP_MOD_INCAP	BIT(1)
#define MEAS_REP_MOD_REFUSE	BIT(2)

#define RM_MASTER		BIT(0)	/* STA who issue meas_req */
#define RM_SLAVE		0	/* STA who do measurement */

#define CLOCK_UNIT		10	/* ms */
#define RTW_MAX_NB_RPT_IE_NUM	16

#define RM_GET_AID(rmid)	((rmid&0xffff0000)>>16)
#define RM_IS_ID_FOR_ALL(rmid)	(rmid&RM_ALL_MEAS)

#define	MAX_OP_CHANNEL_SET_NUM	11
typedef struct _RT_OPERATING_CLASS {
	int	global_op_class;
	int	Len;
	u16	Channel[MAX_OP_CHANNEL_SET_NUM];
} RT_OPERATING_CLASS, *PRT_OPERATING_CLASS;

/* IEEE 802.11-2012 Table 8-59 Measurement Type definitions
*  for measurement request
*  modify rm_meas_type_req_name() when adding new type
*/
enum meas_type_of_req {
	basic_req,	/* spectrum measurement */
	cca_req,
	rpi_histo_req,
	ch_load_req,
	noise_histo_req,
	bcn_req,
	frame_req,
	sta_statis_req,
	lci_req,
	meas_type_req_max,
};

/* IEEE 802.11-2012 Table 8-81 Measurement Type definitions
*  for measurement report
*  modify rm_type_rep_name() when adding new type
*/
enum meas_type_of_rep {
	basic_rep,	/* spectrum measurement */
	cca_rep,
	rpi_histo_rep,
	ch_load_rep,	/* radio measurement */
	noise_histo_rep,
	bcn_rep,
	frame_rep,
	sta_statis_rep,	/* Radio measurement and WNM */
	lci_rep,
	meas_type_rep_max
};

/*
* Beacon request
*/
/* IEEE 802.11-2012 Table 8-64 Measurement mode for Beacon Request element */
enum bcn_req_meas_mode {
	bcn_req_passive,
	bcn_req_active,
	bcn_req_bcn_table
};

/* IEEE 802.11-2012 Table 8-65 optional subelement IDs for Beacon Request */
enum bcn_req_opt_sub_id{
	bcn_req_ssid = 0,		/* len 0-32 */
	bcn_req_rep_info = 1,		/* len 2 */
	bcn_req_rep_detail = 2,		/* len 1 */
	bcn_req_req = 10,		/* len 0-237 */
	bcn_req_ac_ch_rep = 51		/* len 1-237 */
};

/* IEEE 802.11-2012 Table 8-66 Reporting condition of Beacon Report */
enum bcn_rep_cound_id{
	bcn_rep_cond_immediately,	/* default */
	bcn_req_cond_rcpi_greater,
	bcn_req_cond_rcpi_less,
	bcn_req_cond_rsni_greater,
	bcn_req_cond_rsni_less,
	bcn_req_cond_max
};

struct opt_rep_info {
	u8 cond;
	u8 threshold;
};

#define BCN_REQ_OPT_MAX_NUM		16
struct bcn_req_opt {
	/* all req cmd id */
	u8 opt_id[BCN_REQ_OPT_MAX_NUM];
	u8 opt_id_num;
	u8 rep_detail;
	NDIS_802_11_SSID ssid;

	/* bcn report condition */
	struct opt_rep_info rep_cond;

	/* 0:default(Report to be issued after each measurement) */
	u8 *req_start;	/*id : 10 request;start  */
	u8 req_len;	/*id : 10 request;length */
};

/*
* channel load
*/
/* IEEE 802.11-2012 Table 8-60 optional subelement IDs for channel load request */
enum ch_load_opt_sub_id{
	ch_load_rsvd,
	ch_load_rep_info
};

/* IEEE 802.11-2012 Table 8-61 Reporting condition for channel load Report */
enum ch_load_cound_id{
	ch_load_cond_immediately,	/* default */
	ch_load_cond_anpi_equal_greater,
	ch_load_cond_anpi_equal_less,
	ch_load_cond_max
};

/*
* Noise histogram
*/
/* IEEE 802.11-2012 Table 8-62 optional subelement IDs for noise histogram */
enum noise_histo_opt_sub_id{
	noise_histo_rsvd,
	noise_histo_rep_info
};

/* IEEE 802.11-2012 Table 8-63 Reporting condition for noise historgarm Report */
enum noise_histo_cound_id{
	noise_histo_cond_immediately,	/* default */
	noise_histo_cond_anpi_equal_greater,
	noise_histo_cond_anpi_equal_less,
	noise_histo_cond_max
};

struct meas_req_opt {
	/* report condition */
	struct opt_rep_info rep_cond;
};

/*
* State machine
*/

enum RM_STATE {
	RM_ST_IDLE,
	RM_ST_DO_MEAS,
	RM_ST_WAIT_MEAS,
	RM_ST_SEND_REPORT,
	RM_ST_RECV_REPORT,
	RM_ST_END,
	RM_ST_MAX
};

struct rm_meas_req {
	u8 category;
	u8 action_code;		/* T8-206  */
	u8 diag_token;
	u16 rpt;

	u8 e_id;
	u8 len;
	u8 m_token;
	u8 m_mode;		/* req:F8-105, rep:F8-141 */
	u8 m_type;		/* T8-59 */
	u8 op_class;
	u8 ch_num;
	u16 rand_intvl;		/* units of TU */
	u16 meas_dur;		/* units of TU */

	u8 bssid[6];		/* for bcn_req */

	u8 *pssid;
	u8 *opt_s_elem_start;
	int opt_s_elem_len;

	s8 tx_pwr_used;		/* for link measurement */
	s8 tx_pwr_max;		/* for link measurement */

	union {
		struct bcn_req_opt bcn;
		struct meas_req_opt clm;
		struct meas_req_opt nhm;
	}opt;

	struct rtw_ieee80211_channel ch_set[MAX_OP_CHANNEL_SET_NUM];
	u8 ch_set_ch_amount;
	s8 rx_pwr;		/* in dBm */
	u8 rx_bw;
	u8 rx_rate;
	u8 rx_rsni;
};

struct rm_meas_rep {
	u8 category;
	u8 action_code;		/* T8-206  */
	u8 diag_token;

	u8 e_id;		/* T8-54, 38 request; 39 report */
	u8 len;
	u8 m_token;
	u8 m_mode;		/* req:F8-105, rep:F8-141 */
	u8 m_type;		/* T8-59 */
	u8 op_class;
	u8 ch_num;

	u8 ch_load;
	u8 anpi;
	u8 ipi[11];

	u16 rpt;
	u8 bssid[6];		/* for bcn_req */
};

#define MAX_BUF_NUM	128
struct data_buf {
	u8 *pbuf;
	u16 len;
};

struct rm_obj {

	/* aid << 16 
		|diag_token << 8
		|B(1) 1/0:All_AID/UNIC
		|B(0) 1/0:RM_MASTER/RM_SLAVE */
	u32 rmid;

	enum RM_STATE state;
	struct rm_meas_req q;
	struct rm_meas_rep p;
	struct sta_info *psta;
	struct rm_clock *pclock;

	/* meas report */
	u64 meas_start_time;
	u64 meas_end_time;
	int wait_busy;
	u8 poll_mode;

	struct data_buf buf[MAX_BUF_NUM];

	_list list;
};

/*
* Measurement
*/
struct opt_subelement {
	u8 id;
	u8 length;
	u8 *data;
};

/* 802.11-2012 Table 8-206 Radio Measurment Action field */
enum rm_action_code {
	RM_ACT_RADIO_MEAS_REQ,
	RM_ACT_RADIO_MEAS_REP,
	RM_ACT_LINK_MEAS_REQ,
	RM_ACT_LINK_MEAS_REP,
	RM_ACT_NB_REP_REQ,	/* 4 */
	RM_ACT_NB_REP_RESP,
	RM_ACT_RESV,
	RM_ACT_MAX
};

/* 802.11-2012 Table 8-119 RM Enabled Capabilities definition */
enum rm_cap_en {
	RM_LINK_MEAS_CAP_EN,
	RM_NB_REP_CAP_EN,		/* neighbor report */
	RM_PARAL_MEAS_CAP_EN,		/* parallel report */
	RM_REPEAT_MEAS_CAP_EN,
	RM_BCN_PASSIVE_MEAS_CAP_EN,
	RM_BCN_ACTIVE_MEAS_CAP_EN,
	RM_BCN_TABLE_MEAS_CAP_EN,
	RM_BCN_MEAS_REP_COND_CAP_EN,	/* conditions */

	RM_FRAME_MEAS_CAP_EN,
	RM_CH_LOAD_CAP_EN,
	RM_NOISE_HISTO_CAP_EN,		/* noise historgram */
	RM_STATIS_MEAS_CAP_EN,		/* statistics */
	RM_LCI_MEAS_CAP_EN,		/* 12 */
	RM_LCI_AMIMUTH_CAP_EN,
	RM_TRANS_STREAM_CAT_MEAS_CAP_EN,
	RM_TRIG_TRANS_STREAM_CAT_MEAS_CAP_EN,

	RM_AP_CH_REP_CAP_EN,
	RM_RM_MIB_CAP_EN,
	RM_OP_CH_MAX_MEAS_DUR0,		/* 18-20 */
	RM_OP_CH_MAX_MEAS_DUR1,
	RM_OP_CH_MAX_MEAS_DUR2,
	RM_NONOP_CH_MAX_MEAS_DUR0,	/* 21-23 */
	RM_NONOP_CH_MAX_MEAS_DUR1,
	RM_NONOP_CH_MAX_MEAS_DUR2,

	RM_MEAS_PILOT_CAP0,		/* 24-26 */
	RM_MEAS_PILOT_CAP1,
	RM_MEAS_PILOT_CAP2,
	RM_MEAS_PILOT_TRANS_INFO_CAP_EN,
	RM_NB_REP_TSF_OFFSET_CAP_EN,
	RM_RCPI_MEAS_CAP_EN,		/* 29 */
	RM_RSNI_MEAS_CAP_EN,
	RM_BSS_AVG_ACCESS_DELAY_CAP_EN,

	RM_AVALB_ADMIS_CAPACITY_CAP_EN,
	RM_ANT_CAP_EN,
	RM_RSVD,			/* 34-39 */
	RM_MAX
};

char *rm_state_name(enum RM_STATE state);
char *rm_event_name(enum RM_EV_ID evid);
char *rm_type_req_name(u8 meas_type);
int _rm_post_event(_adapter *padapter, u32 rmid, enum RM_EV_ID evid);
int rm_enqueue_rmobj(_adapter *padapter, struct rm_obj *obj, bool to_head);

void rm_free_rmobj(struct rm_obj *prm);
struct rm_obj *rm_alloc_rmobj(_adapter *padapter);
struct rm_obj *rm_get_rmobj(_adapter *padapter, u32 rmid);
struct sta_info *rm_get_psta(_adapter *padapter, u32 rmid);

int retrieve_radio_meas_result(struct rm_obj *prm);
int rm_radio_meas_report_cond(struct rm_obj *prm);
int rm_recv_radio_mens_req(_adapter *padapter,
	union recv_frame *precv_frame,struct sta_info *psta);
int rm_recv_radio_mens_rep(_adapter *padapter,
	union recv_frame *precv_frame, struct sta_info *psta);
int rm_recv_link_mens_req(_adapter *padapter,
	union recv_frame *precv_frame,struct sta_info *psta);
int rm_recv_link_mens_rep(_adapter *padapter,
	union recv_frame *precv_frame, struct sta_info *psta);
int rm_radio_mens_nb_rep(_adapter *padapter,
	union recv_frame *precv_frame, struct sta_info *psta);
int issue_null_reply(struct rm_obj *prm);
int issue_beacon_rep(struct rm_obj *prm);
int issue_nb_req(struct rm_obj *prm);
int issue_radio_meas_req(struct rm_obj *prm);
int issue_radio_meas_rep(struct rm_obj *prm);
int issue_link_meas_req(struct rm_obj *prm);
int issue_link_meas_rep(struct rm_obj *prm);

void rm_set_rep_mode(struct rm_obj *prm, u8 mode);

int ready_for_scan(struct rm_obj *prm);
int rm_sitesurvey(struct rm_obj *prm);

#endif /*CONFIG_RTW_80211K*/
#endif /*__RTW_RM_FSM_H_*/

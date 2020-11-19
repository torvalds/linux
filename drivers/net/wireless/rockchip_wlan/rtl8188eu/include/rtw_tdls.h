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
#ifndef __RTW_TDLS_H_
#define __RTW_TDLS_H_


#ifdef CONFIG_TDLS
/* TDLS STA state */


/* TDLS Diect Link Establishment */
#define	TDLS_STATE_NONE				0x00000000		/* Default state */
#define	TDLS_INITIATOR_STATE		BIT(28)			/* 0x10000000 */
#define	TDLS_RESPONDER_STATE		BIT(29)			/* 0x20000000 */
#define	TDLS_LINKED_STATE			BIT(30)			/* 0x40000000 */
/* TDLS PU Buffer STA */
#define	TDLS_WAIT_PTR_STATE			BIT(24)			/* 0x01000000 */	/* Waiting peer's TDLS_PEER_TRAFFIC_RESPONSE frame */
/* TDLS Check ALive */
#define	TDLS_ALIVE_STATE			BIT(20)			/* 0x00100000 */	/* Check if peer sta is alived. */
/* TDLS Channel Switch */
#define	TDLS_CH_SWITCH_PREPARE_STATE	BIT(15)			/* 0x00008000 */
#define	TDLS_CH_SWITCH_ON_STATE			BIT(16)			/* 0x00010000 */
#define	TDLS_PEER_AT_OFF_STATE			BIT(17)			/* 0x00020000 */	/* Could send pkt on target ch */
#define	TDLS_CH_SW_INITIATOR_STATE		BIT(18)			/* 0x00040000 */	/* Avoid duplicated or unconditional ch. switch rsp. */
#define	TDLS_WAIT_CH_RSP_STATE			BIT(19)			/* 0x00080000 */	/* Wait Ch. response as we are TDLS channel switch initiator */


#define	TDLS_TPK_RESEND_COUNT			86400	/*Unit: seconds */
#define	TDLS_CH_SWITCH_TIME				15
#define	TDLS_CH_SWITCH_TIMEOUT			30
#define	TDLS_CH_SWITCH_OPER_OFFLOAD_TIMEOUT	10
#define	TDLS_SIGNAL_THRESH			0x20
#define	TDLS_WATCHDOG_PERIOD		10	/* Periodically sending tdls discovery request in TDLS_WATCHDOG_PERIOD * 2 sec */
#define	TDLS_HANDSHAKE_TIME			3000
#define	TDLS_PTI_TIME				7000

#define TDLS_CH_SW_STAY_ON_BASE_CHNL_TIMEOUT	20		/* ms */
#define TDLS_CH_SW_MONITOR_TIMEOUT				2000	/*ms */

#define TDLS_MIC_LEN 16
#define WPA_NONCE_LEN 32
#define TDLS_TIMEOUT_LEN 4

enum TDLS_CH_SW_CHNL {
	TDLS_CH_SW_BASE_CHNL = 0,
	TDLS_CH_SW_OFF_CHNL
};

#define TDLS_MIC_CTRL_LEN 2
#define TDLS_FTIE_DATA_LEN (TDLS_MIC_CTRL_LEN + TDLS_MIC_LEN + \
							WPA_NONCE_LEN + WPA_NONCE_LEN)
struct wpa_tdls_ftie {
	u8 ie_type; /* FTIE */
	u8 ie_len;
	union {
		struct {
			u8 mic_ctrl[TDLS_MIC_CTRL_LEN];
			u8 mic[TDLS_MIC_LEN];
			u8 Anonce[WPA_NONCE_LEN]; /* Responder Nonce in TDLS */
			u8 Snonce[WPA_NONCE_LEN]; /* Initiator Nonce in TDLS */
		};
		struct {
			u8 data[TDLS_FTIE_DATA_LEN];
		};
	};
	/* followed by optional elements */
} ;

struct wpa_tdls_lnkid {
	u8 ie_type; /* Link Identifier IE */
	u8 ie_len;
	u8 bssid[ETH_ALEN];
	u8 init_sta[ETH_ALEN];
	u8 resp_sta[ETH_ALEN];
} ;

static u8 TDLS_RSNIE[20] = {	0x01, 0x00,	/* Version shall be set to 1 */
				0x00, 0x0f, 0xac, 0x07,	/* Group sipher suite */
				0x01, 0x00,	/* Pairwise cipher suite count */
	0x00, 0x0f, 0xac, 0x04,	/* Pairwise cipher suite list; CCMP only */
				0x01, 0x00,	/* AKM suite count */
				0x00, 0x0f, 0xac, 0x07,	/* TPK Handshake */
				0x0c, 0x02,
				/* PMKID shall not be present */
			   };

static u8 TDLS_WMMIE[] = {0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};	/* Qos info all set zero */

static u8 TDLS_WMM_PARAM_IE[] = {0x00, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4, 0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00};

static u8 TDLS_EXT_CAPIE[] = {0x00, 0x00, 0x00, 0x50, 0x20, 0x00, 0x00, 0x00};	/* bit(28), bit(30), bit(37) */

/* SRC: Supported Regulatory Classes */
static u8 TDLS_SRC[] = { 0x01, 0x01, 0x02, 0x03, 0x04, 0x0c, 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x21 };

int check_ap_tdls_prohibited(u8 *pframe, u8 pkt_len);
int check_ap_tdls_ch_switching_prohibited(u8 *pframe, u8 pkt_len);

void rtw_set_tdls_enable(_adapter *padapter, u8 enable);
u8 rtw_is_tdls_enabled(_adapter *padapter);
u8 rtw_is_tdls_sta_existed(_adapter *padapter);
u8 rtw_tdls_is_setup_allowed(_adapter *padapter);
#ifdef CONFIG_TDLS_CH_SW
u8 rtw_tdls_is_chsw_allowed(_adapter *padapter);
#endif

void rtw_tdls_set_link_established(_adapter *adapter, bool en);
void rtw_reset_tdls_info(_adapter *padapter);
int rtw_init_tdls_info(_adapter *padapter);
void rtw_free_tdls_info(struct tdls_info *ptdlsinfo);
void rtw_free_all_tdls_sta(_adapter *padapter, u8 enqueue_cmd);
void rtw_enable_tdls_func(_adapter *padapter);
void rtw_disable_tdls_func(_adapter *padapter, u8 enqueue_cmd);
int issue_nulldata_to_TDLS_peer_STA(_adapter *padapter, unsigned char *da, unsigned int power_mode, int try_cnt, int wait_ms);
void rtw_init_tdls_timer(_adapter *padapter, struct sta_info *psta);
void	rtw_cancel_tdls_timer(struct sta_info *psta);
void rtw_tdls_teardown_pre_hdl(_adapter *padapter, struct sta_info *psta);
void rtw_tdls_teardown_post_hdl(_adapter *padapter, struct sta_info *psta, u8 enqueue_cmd);

#ifdef CONFIG_TDLS_CH_SW
void rtw_tdls_set_ch_sw_oper_control(_adapter *padapter, u8 enable);
void rtw_tdls_ch_sw_back_to_base_chnl(_adapter *padapter);
s32 rtw_tdls_do_ch_sw(_adapter *padapter, struct sta_info *ptdls_sta, u8 chnl_type, u8 channel, u8 channel_offset, u16 bwmode, u16 ch_switch_time);
void rtw_tdls_chsw_oper_done(_adapter *padapter);
#endif

#ifdef CONFIG_WFD
int issue_tunneled_probe_req(_adapter *padapter);
int issue_tunneled_probe_rsp(_adapter *padapter, union recv_frame *precv_frame);
#endif /* CONFIG_WFD */
int issue_tdls_dis_req(_adapter *padapter, struct tdls_txmgmt *ptxmgmt);
int issue_tdls_setup_req(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, int wait_ack);
int issue_tdls_setup_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt);
int issue_tdls_setup_cfm(_adapter *padapter, struct tdls_txmgmt *ptxmgmt);
int issue_tdls_dis_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, u8 privacy);
int issue_tdls_teardown(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, u8 wait_ack);
int issue_tdls_peer_traffic_rsp(_adapter *padapter, struct sta_info *psta, struct tdls_txmgmt *ptxmgmt);
int issue_tdls_peer_traffic_indication(_adapter *padapter, struct sta_info *psta);
#ifdef CONFIG_TDLS_CH_SW
int issue_tdls_ch_switch_req(_adapter *padapter, struct sta_info *ptdls_sta);
int issue_tdls_ch_switch_rsp(_adapter *padapter, struct tdls_txmgmt *ptxmgmt, int wait_ack);
#endif
sint On_TDLS_Dis_Rsp(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Setup_Req(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
int On_TDLS_Setup_Rsp(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
int On_TDLS_Setup_Cfm(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
int On_TDLS_Dis_Req(_adapter *adapter, union recv_frame *precv_frame);
int On_TDLS_Teardown(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
int On_TDLS_Peer_Traffic_Indication(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
int On_TDLS_Peer_Traffic_Rsp(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
#ifdef CONFIG_TDLS_CH_SW
sint On_TDLS_Ch_Switch_Req(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
sint On_TDLS_Ch_Switch_Rsp(_adapter *adapter, union recv_frame *precv_frame, struct sta_info *ptdls_sta);
void rtw_build_tdls_ch_switch_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
void rtw_build_tdls_ch_switch_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
#endif
void rtw_build_tdls_setup_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
void rtw_build_tdls_setup_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
void rtw_build_tdls_setup_cfm_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
void rtw_build_tdls_teardown_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
void rtw_build_tdls_dis_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt);
void rtw_build_tdls_dis_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, u8 privacy);
void rtw_build_tdls_peer_traffic_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
void rtw_build_tdls_peer_traffic_indication_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe, struct tdls_txmgmt *ptxmgmt, struct sta_info *ptdls_sta);
void rtw_build_tunneled_probe_req_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe);
void rtw_build_tunneled_probe_rsp_ies(_adapter *padapter, struct xmit_frame *pxmitframe, u8 *pframe);

int rtw_tdls_is_driver_setup(_adapter *padapter);
void rtw_tdls_set_key(_adapter *padapter, struct sta_info *ptdls_sta);
const char *rtw_tdls_action_txt(enum TDLS_ACTION_FIELD action);
#endif /* CONFIG_TDLS */

#endif

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
#ifndef __RTW_TDLS_H_
#define __RTW_TDLS_H_


#ifdef CONFIG_TDLS
/* TDLS STA state */
#define	TDLS_STATE_NONE				0x00000000	//default state
#define	TDLS_INITIATOR_STATE			0x10000000
#define	TDLS_RESPONDER_STATE			0x20000000
#define	TDLS_LINKED_STATE				0x40000000
#define	TDLS_CH_SWITCH_ON_STATE		0x01000000
#define	TDLS_PEER_AT_OFF_STATE		0x02000000	//could send pkt on target ch
#define	TDLS_AT_OFF_CH_STATE			0x04000000
#define	TDLS_CH_SW_INITIATOR_STATE	0x08000000	//avoiding duplicated or unconditional ch. switch rsp.
#define	TDLS_APSD_CHSW_STATE		0x00100000	//in APSD and want to setup channel switch
#define	TDLS_PEER_SLEEP_STATE		0x00200000	//peer sta is sleeping
#define	TDLS_SW_OFF_STATE			0x00400000	//terminate channel swithcing
#define	TDLS_ALIVE_STATE				0x00010000	//Check if peer sta is alived.

#define	TPK_RESEND_COUNT				301
#define 	CH_SWITCH_TIME				10
#define 	CH_SWITCH_TIMEOUT			30
#define	TDLS_STAY_TIME				500
#define	TDLS_SIGNAL_THRESH			0x20
#define	TDLS_WATCHDOG_PERIOD		10	//Periodically sending tdls discovery request in TDLS_WATCHDOG_PERIOD * 2 sec
#define	TDLS_ALIVE_TIMER_PH1			5000
#define	TDLS_ALIVE_TIMER_PH2			2000
#define	TDLS_STAY_TIME				500
#define	TDLS_HANDSHAKE_TIME			8000
#define	TDLS_ALIVE_COUNT				3
#define	TDLS_INI_MACID_ENTRY			6

/* TDLS */
#define TDLS_MIC_LEN 16
#define WPA_NONCE_LEN 32
#define TDLS_TIMEOUT_LEN 4

struct wpa_tdls_ftie {
	u8 ie_type; /* FTIE */
	u8 ie_len;
	u8 mic_ctrl[2];
	u8 mic[TDLS_MIC_LEN];
	u8 Anonce[WPA_NONCE_LEN]; /* Responder Nonce in TDLS */
	u8 Snonce[WPA_NONCE_LEN]; /* Initiator Nonce in TDLS */
	/* followed by optional elements */
} ;

struct wpa_tdls_lnkid {
	u8 ie_type; /* Link Identifier IE */
	u8 ie_len;
	u8 bssid[ETH_ALEN];
	u8 init_sta[ETH_ALEN];
	u8 resp_sta[ETH_ALEN];
} ;

static u8 TDLS_RSNIE[]={	0x01, 0x00,	//version shall be set to 1
						0x00, 0x0f, 0xac, 0x07,	//group sipher suite
						0x01, 0x00,	//pairwise cipher suite count
						0x00, 0x0f, 0xac, 0x04,	//pairwise cipher suite list; CCMP only
						0x01, 0x00,	//AKM suite count
						0x00, 0x0f, 0xac, 0x07,	//TPK Handshake
						0x00, 0x02,
						//PMKID shall not be present
						};

static u8 TDLS_WMMIE[]={0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};	//Qos info all set zero

static u8 TDLS_EXT_CAPIE[] = {0x00, 0x00, 0x00, 0x50, 0x20};	//bit(28), bit(30), bit(37)

//	SRC: Supported Regulatory Classes
static u8 TDLS_SRC[] = { 0x01, 0x01, 0x02, 0x03, 0x04, 0x0c, 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x21 };

void rtw_reset_tdls_info(_adapter* padapter);
int rtw_init_tdls_info(_adapter* padapter);
void rtw_free_tdls_info(struct tdls_info *ptdlsinfo);
void issue_nulldata_to_TDLS_peer_STA(_adapter *padapter, struct sta_info *ptdls_sta, unsigned int power_mode);
void init_TPK_timer(_adapter *padapter, struct sta_info *psta);
void init_ch_switch_timer(_adapter *padapter, struct sta_info *psta);
void init_base_ch_timer(_adapter *padapter, struct sta_info *psta);
void init_off_ch_timer(_adapter *padapter, struct sta_info *psta);
void init_tdls_alive_timer(_adapter *padapter, struct sta_info *psta);
void init_handshake_timer(_adapter *padapter, struct sta_info *psta);
void free_tdls_sta(_adapter *padapter, struct sta_info *ptdls_sta);
#ifdef CONFIG_WFD
void issue_tunneled_probe_req(_adapter *padapter);
void issue_tunneled_probe_rsp(_adapter *padapter, union recv_frame *precv_frame);
#endif //CONFIG_WFD
void issue_tdls_dis_req(_adapter *padapter, u8 *mac_addr);
void issue_tdls_setup_req(_adapter *padapter, u8 *mac_addr);
void issue_tdls_setup_rsp(_adapter *padapter, union recv_frame *precv_frame);
void issue_tdls_setup_cfm(_adapter *padapter, union recv_frame *precv_frame);
void issue_tdls_dis_rsp(_adapter * padapter, union recv_frame * precv_frame, u8 dialog);
void issue_tdls_teardown(_adapter *padapter, u8 *mac_addr);
void issue_tdls_peer_traffic_indication(_adapter *padapter, struct sta_info *psta);
void issue_tdls_ch_switch_req(_adapter *padapter, u8 *mac_addr);
void issue_tdls_ch_switch_rsp(_adapter *padapter, u8 *mac_addr);
sint On_TDLS_Dis_Rsp(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Setup_Req(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Setup_Rsp(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Setup_Cfm(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Dis_Req(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Teardown(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Peer_Traffic_Rsp(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Ch_Switch_Req(_adapter *adapter, union recv_frame *precv_frame);
sint On_TDLS_Ch_Switch_Rsp(_adapter *adapter, union recv_frame *precv_frame);
void rtw_build_tdls_setup_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tdls_setup_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tdls_setup_cfm_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tdls_teardown_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tdls_dis_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tdls_dis_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, u8 dialog);
void rtw_build_tdls_peer_traffic_indication_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tdls_ch_switch_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tdls_ch_switch_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tunneled_probe_req_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);
void rtw_build_tunneled_probe_rsp_ies(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe);

u8	update_sgi_tdls(_adapter *padapter, struct sta_info *psta);
u32 update_mask_tdls(_adapter *padapter, struct sta_info *psta);
#endif //CONFIG_TDLS

#endif


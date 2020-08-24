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
#ifndef __RTW_AP_H_
#define __RTW_AP_H_


#ifdef CONFIG_AP_MODE

/* external function */
extern void rtw_indicate_sta_assoc_event(_adapter *padapter, struct sta_info *psta);
extern void rtw_indicate_sta_disassoc_event(_adapter *padapter, struct sta_info *psta);


void init_mlme_ap_info(_adapter *padapter);
void free_mlme_ap_info(_adapter *padapter);
u8 rtw_set_tim_ie(u8 dtim_cnt, u8 dtim_period
	, const u8 *tim_bmp, u8 tim_bmp_len, u8 *tim_ie);
/* void update_BCNTIM(_adapter *padapter); */
void rtw_add_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index, u8 *data, u8 len);
void rtw_remove_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index);
void _update_beacon(_adapter *padapter, u8 ie_id, u8 *oui, u8 tx, u8 flags, const char *tag);
#define update_beacon(adapter, ie_id, oui, tx, flags) _update_beacon((adapter), (ie_id), (oui), (tx), (flags), __func__)
/*update_beacon - (flags) can set to normal enqueue (0) and RTW_CMDF_WAIT_ACK enqueue. 
 (flags) = RTW_CMDF_DIRECTLY  is not currently implemented, it will do normal enqueue.*/

void rtw_ap_update_sta_ra_info(_adapter *padapter, struct sta_info *psta);

void expire_timeout_chk(_adapter *padapter);
void update_sta_info_apmode(_adapter *padapter, struct sta_info *psta);
void rtw_start_bss_hdl_after_chbw_decided(_adapter *adapter);
void start_bss_network(_adapter *padapter, struct createbss_parm *parm);
int rtw_check_beacon_data(_adapter *padapter, u8 *pbuf,  int len);
void rtw_ap_restore_network(_adapter *padapter);

#if CONFIG_RTW_MACADDR_ACL
void rtw_macaddr_acl_init(_adapter *adapter, u8 period);
void rtw_macaddr_acl_deinit(_adapter *adapter, u8 period);
void rtw_macaddr_acl_clear(_adapter *adapter, u8 period);
void rtw_set_macaddr_acl(_adapter *adapter, u8 period, int mode);
int rtw_acl_add_sta(_adapter *adapter, u8 period, const u8 *addr);
int rtw_acl_remove_sta(_adapter *adapter, u8 period, const u8 *addr);
#endif /* CONFIG_RTW_MACADDR_ACL */

u8 rtw_ap_set_sta_key(_adapter *adapter, const u8 *addr, u8 alg, const u8 *key, u8 keyid, u8 gk);
u8 rtw_ap_set_pairwise_key(_adapter *padapter, struct sta_info *psta);
int rtw_ap_set_group_key(_adapter *padapter, u8 *key, u8 alg, int keyid);
int rtw_ap_set_wep_key(_adapter *padapter, u8 *key, u8 keylen, int keyid, u8 set_tx);

#ifdef CONFIG_NATIVEAP_MLME
void associated_clients_update(_adapter *padapter, u8 updated, u32 sta_info_type);
void bss_cap_update_on_sta_join(_adapter *padapter, struct sta_info *psta);
u8 bss_cap_update_on_sta_leave(_adapter *padapter, struct sta_info *psta);
void sta_info_update(_adapter *padapter, struct sta_info *psta);
void ap_sta_info_defer_update(_adapter *padapter, struct sta_info *psta);
u8 ap_free_sta(_adapter *padapter, struct sta_info *psta, bool active, u16 reason, bool enqueue);
int rtw_sta_flush(_adapter *padapter, bool enqueue);
int rtw_ap_inform_ch_switch(_adapter *padapter, u8 new_ch, u8 ch_offset);
void start_ap_mode(_adapter *padapter);
void stop_ap_mode(_adapter *padapter);
#endif

void rtw_ap_update_bss_chbw(_adapter *adapter, WLAN_BSSID_EX *bss, u8 ch, u8 bw, u8 offset);
u8 rtw_ap_chbw_decision(_adapter *adapter, u8 ifbmp, u8 excl_ifbmp
	, s16 req_ch, s8 req_bw, s8 req_offset, u8 *ch, u8 *bw, u8 *offset, u8 *chbw_allow);

#ifdef CONFIG_AUTO_AP_MODE
void rtw_auto_ap_rx_msg_dump(_adapter *padapter, union recv_frame *precv_frame, u8 *ehdr_pos);
extern void rtw_start_auto_ap(_adapter *adapter);
#endif /* CONFIG_AUTO_AP_MODE */

void rtw_ap_parse_sta_capability(_adapter *adapter, struct sta_info *sta, u8 *cap);
u16 rtw_ap_parse_sta_supported_rates(_adapter *adapter, struct sta_info *sta, u8 *tlv_ies, u16 tlv_ies_len);
u16 rtw_ap_parse_sta_security_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems);
void rtw_ap_parse_sta_wmm_ie(_adapter *adapter, struct sta_info *sta, u8 *tlv_ies, u16 tlv_ies_len);
void rtw_ap_parse_sta_ht_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems);
void rtw_ap_parse_sta_vht_ie(_adapter *adapter, struct sta_info *sta, struct rtw_ieee802_11_elems *elems);

void update_bmc_sta(_adapter *padapter);

#ifdef CONFIG_BMC_TX_RATE_SELECT
void rtw_update_bmc_sta_tx_rate(_adapter *adapter);
#endif

void rtw_process_ht_action_smps(_adapter *padapter, u8 *ta, u8 ctrl_field);
void rtw_process_public_act_bsscoex(_adapter *padapter, u8 *pframe, uint frame_len);
#ifdef CONFIG_80211N_HT
int rtw_ht_operation_update(_adapter *padapter);
#endif /* CONFIG_80211N_HT */
u8 rtw_ap_sta_states_check(_adapter *adapter);

#ifdef CONFIG_FW_HANDLE_TXBCN
#define rtw_ap_get_nums(adapter)	(adapter_to_dvobj(adapter)->nr_ap_if)
bool rtw_ap_nums_check(_adapter *adapter);
#endif

#ifdef CONFIG_SWTIMER_BASED_TXBCN
void tx_beacon_handlder(struct dvobj_priv *pdvobj);
void tx_beacon_timer_handlder(void *ctx);
#endif /*CONFIG_SWTIMER_BASED_TXBCN*/

#endif /* end of CONFIG_AP_MODE */
#endif /*__RTW_AP_H_*/

/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#ifndef __RTW_AP_H_
#define __RTW_AP_H_


#ifdef CONFIG_AP_MODE

/* external function */
extern void rtw_indicate_sta_assoc_event(_adapter *padapter, struct sta_info *psta);
extern void rtw_indicate_sta_disassoc_event(_adapter *padapter, struct sta_info *psta);


void init_mlme_ap_info(_adapter *padapter);
void free_mlme_ap_info(_adapter *padapter);
/* void update_BCNTIM(_adapter *padapter); */
void rtw_add_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index, u8 *data, u8 len);
void rtw_remove_bcn_ie(_adapter *padapter, WLAN_BSSID_EX *pnetwork, u8 index);
void _update_beacon(_adapter *padapter, u8 ie_id, u8 *oui, u8 tx, const char *tag);
#define update_beacon(adapter, ie_id, oui, tx) _update_beacon((adapter), (ie_id), (oui), (tx), __func__)
void add_RATid(_adapter *padapter, struct sta_info *psta, u8 rssi_level);
void expire_timeout_chk(_adapter *padapter);
void update_sta_info_apmode(_adapter *padapter, struct sta_info *psta);
void rtw_start_bss_hdl_after_chbw_decided(_adapter *adapter);
void start_bss_network(_adapter *padapter, struct createbss_parm *parm);
int rtw_check_beacon_data(_adapter *padapter, u8 *pbuf,  int len);
void rtw_ap_restore_network(_adapter *padapter);

#if CONFIG_RTW_MACADDR_ACL
void rtw_set_macaddr_acl(_adapter *adapter, int mode);
int rtw_acl_add_sta(_adapter *adapter, const u8 *addr);
int rtw_acl_remove_sta(_adapter *adapter, const u8 *addr);
#endif /* CONFIG_RTW_MACADDR_ACL */

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
bool rtw_ap_chbw_decision(_adapter *adapter, s16 req_ch, s8 req_bw, s8 req_offset, u8 *ch, u8 *bw, u8 *offset, u8 *chbw_allow);

#ifdef CONFIG_AUTO_AP_MODE
extern void rtw_start_auto_ap(_adapter *adapter);
#endif /* CONFIG_AUTO_AP_MODE */

#endif /* end of CONFIG_AP_MODE */

#endif
void update_bmc_sta(_adapter *padapter);
void rtw_process_public_act_bsscoex(_adapter *padapter, u8 *pframe, uint frame_len);

#ifdef CONFIG_80211N_HT
void rtw_process_ht_action_smps(_adapter *padapter, u8 *ta, u8 ctrl_field);
int rtw_ht_operation_update(_adapter *padapter);
#endif

#ifdef CONFIG_SWTIMER_BASED_TXBCN
void tx_beacon_handlder(struct dvobj_priv *pdvobj);
void tx_beacon_timer_handlder(struct dvobj_priv *pdvobj);
#endif

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
#ifndef __RTW_RSON_H_
#define __RTW_RSON_H_


#define RTW_RSON_VER						1

#define RTW_RSON_SCORE_NOTSUP			0x0
#define RTW_RSON_SCORE_NOTCNNT			0x1
#define RTW_RSON_SCORE_MAX				0xFF
#define RTW_RSON_HC_NOTREADY			0xFF
#define RTW_RSON_HC_ROOT				0x0
#define RTW_RSON_ALLOWCONNECT			0x1
#define RTW_RSON_DENYCONNECT			0x0



/*	for rtw self-origanization spec 1	*/
struct rtw_rson_struct {
	u8 ver;
	u32 id;
	u8 hopcnt;
	u8 connectible;
	u8 loading;
	u8 res[16];
} __attribute__((__packed__));

void init_rtw_rson_data(struct dvobj_priv *dvobj);
void rtw_rson_get_property_str(_adapter *padapter, char *rson_data_str);
int rtw_rson_set_property(_adapter *padapter, char *field, char *value);
int rtw_rson_choose(struct wlan_network **candidate, struct wlan_network *competitor);
int rtw_get_rson_struct(WLAN_BSSID_EX *bssid, struct  rtw_rson_struct *rson_data);
u8 rtw_cal_rson_score(struct rtw_rson_struct *cand_rson_data, NDIS_802_11_RSSI  Rssi);
void rtw_rson_handle_ie(WLAN_BSSID_EX *bssid, u8 ie_offset);
u32 rtw_rson_append_ie(_adapter *padapter, unsigned char *pframe, u32 *len);
void rtw_rson_do_disconnect(_adapter *padapter);
void rtw_rson_join_done(_adapter *padapter);
int rtw_rson_isupdate_roamcan(struct mlme_priv *mlme, struct wlan_network **candidate, struct wlan_network *competitor);
void rtw_rson_show_survey_info(struct seq_file *m, _list *plist, _list *phead);
u8 rtw_rson_ap_check_sta(_adapter *padapter, u8 *pframe, uint pkt_len, unsigned short ie_offset);
u8 rtw_rson_scan_wk_cmd(_adapter *padapter, int op);
void rtw_rson_scan_cmd_hdl(_adapter *padapter, int op);
#endif /* __RTW_RSON_H_ */

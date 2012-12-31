/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#ifndef _RTL871X_WLAN_MLME_H_
#define _RTL871X_WLAN_MLME_H_

#include <ieee80211_ext.h>


#define SURVEY_TO		(200)
#define REAUTH_TO		(1000)
#define REASSOC_TO		(1000)
#define DISCONNECT_TO	(10000)

#define BCN_TO			(100)

#define REAUTH_LIMIT	6
#define REASSOC_LIMIT	6


enum Synchronization_Sta_State {
        STATE_Sta_Min			= 0,
        STATE_Sta_No_Bss		= 1,
        STATE_Sta_Bss			= 2,
        STATE_Sta_Ibss_Active	= 3,
        STATE_Sta_Ibss_Idle		= 4,
        STATE_Sta_Auth_Success	= 5,
        STATE_Sta_Roaming_Scan	= 6,
};

int xmit_mgnt_frame(_adapter *padapter, struct mgnt_frame *pmgntframe);

#ifdef CONFIG_AP_MODE
unsigned char *gen_wmm_ie(struct mlme_ext_priv *pmlmeext, unsigned char *eid, unsigned int *sz);
#endif

int set_wpa_ie(struct wpa_psk *pwpa_psk, u8 *buf, int len, int key_mgmt);
int set_rsn_ie(struct wpa_psk *pwpa_psk, u8 *buf, int len, int key_mgmt, const u8 *pmkid);


void report_sta_join_event(_adapter *padapter, struct sta_info *psta);
void report_join_res(_adapter *padapter, int res);
void report_join_res_ex(_adapter *padapter, u8 *piebuf, int iebuf_len, int res);

unsigned int issue_assocreq(_adapter *padapter);
void issue_asocrsp(_adapter *padapter, unsigned short status, struct sta_info *pstat, int pkt_type);
void issue_auth(_adapter *padapter, struct sta_info *pstat, unsigned short status);
void issue_probereq(_adapter *padapter);
void issue_probersp(_adapter *padapter, unsigned char *da, int set_privacy);

void start_clnt_assoc(_adapter *padapter);
void start_clnt_auth(_adapter* padapter);
void start_clnt_join(_adapter* padapter);
void start_create_bss(_adapter *padapter, int mode);

void report_BSSID_info(_adapter *padapter, u8 *pframe, uint len);
void site_survey(_adapter *padapter);


void survey_timer_hdl (_adapter *padapter);
void reauth_timer_hdl(_adapter *padapter);
void reassoc_timer_hdl(_adapter *padapter);



int setup_bcnframe(_adapter *padapter, unsigned char *pframe);
void process_addba_request(_adapter *padapter, struct sta_info *psta, struct ieee80211_mgmt *mgmt, uint len);

#endif


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
#ifndef __IEEE80211_EXT_H
#define __IEEE80211_EXT_H

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#define WMM_OUI_TYPE 2
#define WMM_OUI_SUBTYPE_INFORMATION_ELEMENT 0
#define WMM_OUI_SUBTYPE_PARAMETER_ELEMENT 1
#define WMM_OUI_SUBTYPE_TSPEC_ELEMENT 2
#define WMM_VERSION 1

#define WPA_PROTO_WPA BIT(0)
#define WPA_PROTO_RSN BIT(1)

#define WPA_KEY_MGMT_IEEE8021X BIT(0)
#define WPA_KEY_MGMT_PSK BIT(1)
#define WPA_KEY_MGMT_NONE BIT(2)
#define WPA_KEY_MGMT_IEEE8021X_NO_WPA BIT(3)
#define WPA_KEY_MGMT_WPA_NONE BIT(4)


#define WPA_CAPABILITY_PREAUTH BIT(0)
#define WPA_CAPABILITY_MGMT_FRAME_PROTECTION BIT(6)
#define WPA_CAPABILITY_PEERKEY_ENABLED BIT(9)


#define PMKID_LEN 16


#ifdef PLATFORM_LINUX
struct wpa_ie_hdr {
	u8 elem_id;
	u8 len;
	u8 oui[4]; /* 24-bit OUI followed by 8-bit OUI type */
	u8 version[2]; /* little endian */
}__attribute__ ((packed));

struct rsn_ie_hdr {
	u8 elem_id; /* WLAN_EID_RSN */
	u8 len;
	u8 version[2]; /* little endian */
}__attribute__ ((packed));

struct wme_ac_parameter {
#if defined(CONFIG_LITTLE_ENDIAN)
	/* byte 1 */
	u8 	aifsn:4,
		acm:1,
	 	aci:2,
	 	reserved:1;

	/* byte 2 */
	u8 	eCWmin:4,
	 	eCWmax:4;
#elif defined(CONFIG_BIG_ENDIAN)
	/* byte 1 */
	u8 	reserved:1,
	 	aci:2,
	 	acm:1,
	 	aifsn:4;

	/* byte 2 */
	u8 	eCWmax:4,
	 	eCWmin:4;
#else
#error	"Please fix <endian.h>"
#endif

	/* bytes 3 & 4 */
	u16 txopLimit;
} __attribute__ ((packed));

struct wme_parameter_element {
	/* required fields for WME version 1 */
	u8 oui[3];
	u8 oui_type;
	u8 oui_subtype;
	u8 version;
	u8 acInfo;
	u8 reserved;
	struct wme_ac_parameter ac[4];

} __attribute__ ((packed));

#endif

#ifdef PLATFORM_WINDOWS

#pragma pack(1)

struct wpa_ie_hdr {
	u8 elem_id;
	u8 len;
	u8 oui[4]; /* 24-bit OUI followed by 8-bit OUI type */
	u8 version[2]; /* little endian */
};

struct rsn_ie_hdr {
	u8 elem_id; /* WLAN_EID_RSN */
	u8 len;
	u8 version[2]; /* little endian */
};

#pragma pack()

#endif

#define WPA_PUT_LE16(a, val)			\
	do {					\
		(a)[1] = ((u16) (val)) >> 8;	\
		(a)[0] = ((u16) (val)) & 0xff;	\
	} while (0)

#define WPA_PUT_BE32(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[3] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define WPA_PUT_LE32(a, val)					\
	do {							\
		(a)[3] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[0] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

#define RSN_SELECTOR_PUT(a, val) WPA_PUT_BE32((u8 *) (a), (val))
//#define RSN_SELECTOR_PUT(a, val) WPA_PUT_LE32((u8 *) (a), (val))



/* Action category code */
enum ieee80211_category {
	WLAN_CATEGORY_SPECTRUM_MGMT = 0,
	WLAN_CATEGORY_QOS = 1,
	WLAN_CATEGORY_DLS = 2,
	WLAN_CATEGORY_BACK = 3,
	WLAN_CATEGORY_HT = 7,
	WLAN_CATEGORY_WMM = 17,
};

/* SPECTRUM_MGMT action code */
enum ieee80211_spectrum_mgmt_actioncode {
	WLAN_ACTION_SPCT_MSR_REQ = 0,
	WLAN_ACTION_SPCT_MSR_RPRT = 1,
	WLAN_ACTION_SPCT_TPC_REQ = 2,
	WLAN_ACTION_SPCT_TPC_RPRT = 3,
	WLAN_ACTION_SPCT_CHL_SWITCH = 4,
	WLAN_ACTION_SPCT_EXT_CHL_SWITCH = 5,
};

/* BACK action code */
enum ieee80211_back_actioncode {
	WLAN_ACTION_ADDBA_REQ = 0,
	WLAN_ACTION_ADDBA_RESP = 1,
	WLAN_ACTION_DELBA = 2,
};

/* HT features action code */
enum ieee80211_ht_actioncode {
	WLAN_ACTION_NOTIFY_CH_WIDTH = 0,
       WLAN_ACTION_SM_PS = 1,
       WLAN_ACTION_PSPM = 2,
       WLAN_ACTION_PCO_PHASE = 3,
       WLAN_ACTION_MIMO_CSI_MX = 4,
       WLAN_ACTION_MIMO_NONCP_BF = 5,
       WLAN_ACTION_MIMP_CP_BF = 6,
       WLAN_ACTION_ASEL_INDICATES_FB = 7,
       WLAN_ACTION_HI_INFO_EXCHG = 8,
};

/* BACK (block-ack) parties */
enum ieee80211_back_parties {
	WLAN_BACK_RECIPIENT = 0,
	WLAN_BACK_INITIATOR = 1,
	WLAN_BACK_TIMER = 2,
};

#ifdef PLATFORM_LINUX

struct ieee80211_mgmt {
	u16 frame_control;
	u16 duration;
	u8 da[6];
	u8 sa[6];
	u8 bssid[6];
	u16 seq_ctrl;
	union {
		struct {
			u16 auth_alg;
			u16 auth_transaction;
			u16 status_code;
			/* possibly followed by Challenge text */
			u8 variable[0];
		}  __attribute__ ((packed)) auth;
		struct {
			u16 reason_code;
		}  __attribute__ ((packed)) deauth;
		struct {
			u16 capab_info;
			u16 listen_interval;
			/* followed by SSID and Supported rates */
			u8 variable[0];
		}  __attribute__ ((packed)) assoc_req;
		struct {
			u16 capab_info;
			u16 status_code;
			u16 aid;
			/* followed by Supported rates */
			u8 variable[0];
		}  __attribute__ ((packed)) assoc_resp, reassoc_resp;
		struct {
			u16 capab_info;
			u16 listen_interval;
			u8 current_ap[6];
			/* followed by SSID and Supported rates */
			u8 variable[0];
		}  __attribute__ ((packed)) reassoc_req;
		struct {
			u16 reason_code;
		}  __attribute__ ((packed)) disassoc;
		struct {
			__le64 timestamp;
			u16 beacon_int;
			u16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params, TIM */
			u8 variable[0];
		}  __attribute__ ((packed)) beacon;
		struct {
			/* only variable items: SSID, Supported rates */
			u8 variable[0];
		}  __attribute__ ((packed)) probe_req;
		struct {
			__le64 timestamp;
			u16 beacon_int;
			u16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params */
			u8 variable[0];
		}  __attribute__ ((packed)) probe_resp;
		struct {
			u8 category;
			union {
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 status_code;
					u8 variable[0];
				}  __attribute__ ((packed)) wme_action;
#if 0
				struct{
					u8 action_code;
					u8 element_id;
					u8 length;
					struct ieee80211_channel_sw_ie sw_elem;
				}  __attribute__ ((packed)) chan_switch;
				struct{
					u8 action_code;
					u8 dialog_token;
					u8 element_id;
					u8 length;
					struct ieee80211_msrment_ie msr_elem;
				}  __attribute__ ((packed)) measurement;
#endif
				struct{
					u8 action_code;
					u8 dialog_token;
					u16 capab;
					u16 timeout;
					u16 start_seq_num;
				}  __attribute__ ((packed)) addba_req;
				struct{
					u8 action_code;
					u8 dialog_token;
					u16 status;
					u16 capab;
					u16 timeout;
				}  __attribute__ ((packed)) addba_resp;
				struct{
					u8 action_code;
					u16 params;
					u16 reason_code;
				}  __attribute__ ((packed)) delba;
				struct{
					u8 action_code;
					/* capab_info for open and confirm,
					 * reason for close
					 */
					u16 aux;
					/* Followed in plink_confirm by status
					 * code, AID and supported rates,
					 * and directly by supported rates in
					 * plink_open and plink_close
					 */
					u8 variable[0];
				}  __attribute__ ((packed)) plink_action;
				struct{
					u8 action_code;
					u8 variable[0];
				}  __attribute__ ((packed)) mesh_action;
			} __attribute__ ((packed)) u;
		}  __attribute__ ((packed)) action;
	} __attribute__ ((packed)) u;
}__attribute__ ((packed));

#endif


#ifdef PLATFORM_WINDOWS

#pragma pack(1)

struct ieee80211_mgmt {
	u16 frame_control;
	u16 duration;
	u8 da[6];
	u8 sa[6];
	u8 bssid[6];
	u16 seq_ctrl;
	union {
		struct {
			u16 auth_alg;
			u16 auth_transaction;
			u16 status_code;
			/* possibly followed by Challenge text */
			u8 variable[0];
		}  auth;
		struct {
			u16 reason_code;
		}  deauth;
		struct {
			u16 capab_info;
			u16 listen_interval;
			/* followed by SSID and Supported rates */
			u8 variable[0];
		}  assoc_req;
		struct {
			u16 capab_info;
			u16 status_code;
			u16 aid;
			/* followed by Supported rates */
			u8 variable[0];
		}  assoc_resp, reassoc_resp;
		struct {
			u16 capab_info;
			u16 listen_interval;
			u8 current_ap[6];
			/* followed by SSID and Supported rates */
			u8 variable[0];
		}  reassoc_req;
		struct {
			u16 reason_code;
		}  disassoc;
#if 0		
		struct {
			__le64 timestamp;
			u16 beacon_int;
			u16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params, TIM */
			u8 variable[0];
		}  beacon;
		struct {
			/* only variable items: SSID, Supported rates */
			u8 variable[0];
		}  probe_req;
		
		struct {
			__le64 timestamp;
			u16 beacon_int;
			u16 capab_info;
			/* followed by some of SSID, Supported rates,
			 * FH Params, DS Params, CF Params, IBSS Params */
			u8 variable[0];
		}  probe_resp;
#endif	
		struct {
			u8 category;
			union {
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 status_code;
					u8 variable[0];
				}  wme_action;
/*				
				struct{
					u8 action_code;
					u8 element_id;
					u8 length;
					struct ieee80211_channel_sw_ie sw_elem;
				}  chan_switch;
				struct{
					u8 action_code;
					u8 dialog_token;
					u8 element_id;
					u8 length;
					struct ieee80211_msrment_ie msr_elem;
				}  measurement;
*/				
				struct{
					u8 action_code;
					u8 dialog_token;
					u16 capab;
					u16 timeout;
					u16 start_seq_num;
				}  addba_req;
				struct{
					u8 action_code;
					u8 dialog_token;
					u16 status;
					u16 capab;
					u16 timeout;
				}  addba_resp;
				struct{
					u8 action_code;
					u16 params;
					u16 reason_code;
				}  delba;
				struct{
					u8 action_code;
					/* capab_info for open and confirm,
					 * reason for close
					 */
					u16 aux;
					/* Followed in plink_confirm by status
					 * code, AID and supported rates,
					 * and directly by supported rates in
					 * plink_open and plink_close
					 */
					u8 variable[0];
				}  plink_action;
				struct{
					u8 action_code;
					u8 variable[0];
				}  mesh_action;
			} u;
		}  action;
	} u;
} ;

#pragma pack()

#endif

/* mgmt header + 1 byte category code */
#define IEEE80211_MIN_ACTION_SIZE FIELD_OFFSET(struct ieee80211_mgmt, u.action.u)



#endif


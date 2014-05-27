#ifndef __RTL_COMPAT_H__
#define __RTL_COMPAT_H__

#define RX_FLAG_MACTIME_MPDU RX_FLAG_MACTIME_START
#define IEEE80211_KEY_FLAG_SW_MGMT IEEE80211_KEY_FLAG_SW_MGMT_TX

struct ieee80211_mgmt_compat {
	__le16 frame_control;
	__le16 duration;
	u8 da[6];
	u8 sa[6];
	u8 bssid[6];
	__le16 seq_ctrl;
	union {
		struct {
			u8 category;
			union {
				struct {
					u8 action_code;
					u8 dialog_token;
					u8 status_code;
					u8 variable[0];
				} __attribute__ ((packed)) wme_action;
				struct{
					u8 action_code;
					u8 dialog_token;
					__le16 capab;
					__le16 timeout;
					__le16 start_seq_num;
				} __attribute__((packed)) addba_req;
				struct{
					u8 action_code;
					u8 dialog_token;
					__le16 status;
					__le16 capab;
					__le16 timeout;
				} __attribute__((packed)) addba_resp;
				struct{
					u8 action_code;
					__le16 params;
					__le16 reason_code;
				} __attribute__((packed)) delba;
				struct{
					u8 action_code;
					/* capab_info for open and confirm,
					 * reason for close
					 */
					__le16 aux;
					/* Followed in plink_confirm by status
					 * code, AID and supported rates,
					 * and directly by supported rates in
					 * plink_open and plink_close
					 */
					u8 variable[0];
				} __attribute__((packed)) plink_action;
				struct{
					u8 action_code;
					u8 variable[0];
				} __attribute__((packed)) mesh_action;
				struct {
					u8 action;
					u8 smps_control;
				} __attribute__ ((packed)) ht_smps;
			} u;
		} __attribute__ ((packed)) action;
	} u;
} __attribute__ ((packed));
#endif

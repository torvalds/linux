/*
 * Copyright (c) 2013 Eugene Krasnikov <k.eugene.e@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _SMD_H_
#define _SMD_H_

#include "wcn36xx.h"

/* Max shared size is 4k but we take less.*/
#define WCN36XX_NV_FRAGMENT_SIZE			3072

#define WCN36XX_HAL_BUF_SIZE				4096

#define HAL_MSG_TIMEOUT 10000
#define WCN36XX_SMSM_WLAN_TX_ENABLE			0x00000400
#define WCN36XX_SMSM_WLAN_TX_RINGS_EMPTY		0x00000200
/* The PNO version info be contained in the rsp msg */
#define WCN36XX_FW_MSG_PNO_VERSION_MASK			0x8000

enum wcn36xx_fw_msg_result {
	WCN36XX_FW_MSG_RESULT_SUCCESS			= 0,
	WCN36XX_FW_MSG_RESULT_SUCCESS_SYNC		= 1,

	WCN36XX_FW_MSG_RESULT_MEM_FAIL			= 5,
};

/******************************/
/* SMD requests and responses */
/******************************/
struct wcn36xx_fw_msg_status_rsp {
	u32	status;
} __packed;

struct wcn36xx_hal_ind_msg {
	struct list_head list;
	size_t msg_len;
	u8 msg[];
};

struct wcn36xx;
struct rpmsg_device;

int wcn36xx_smd_open(struct wcn36xx *wcn);
void wcn36xx_smd_close(struct wcn36xx *wcn);

int wcn36xx_smd_load_nv(struct wcn36xx *wcn);
int wcn36xx_smd_start(struct wcn36xx *wcn);
int wcn36xx_smd_stop(struct wcn36xx *wcn);
int wcn36xx_smd_start_scan(struct wcn36xx *wcn, u8 scan_channel);
int wcn36xx_smd_end_scan(struct wcn36xx *wcn, u8 scan_channel);
int wcn36xx_smd_finish_scan(struct wcn36xx *wcn, enum wcn36xx_hal_sys_mode mode,
			    struct ieee80211_vif *vif);
int wcn36xx_smd_init_scan(struct wcn36xx *wcn, enum wcn36xx_hal_sys_mode mode,
			  struct ieee80211_vif *vif);

int wcn36xx_smd_update_scan_params(struct wcn36xx *wcn, u8 *channels, size_t channel_count);
int wcn36xx_smd_start_hw_scan(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			      struct cfg80211_scan_request *req);
int wcn36xx_smd_stop_hw_scan(struct wcn36xx *wcn);
int wcn36xx_smd_add_sta_self(struct wcn36xx *wcn, struct ieee80211_vif *vif);
int wcn36xx_smd_delete_sta_self(struct wcn36xx *wcn, u8 *addr);
int wcn36xx_smd_delete_sta(struct wcn36xx *wcn, u8 sta_index);
int wcn36xx_smd_join(struct wcn36xx *wcn, const u8 *bssid, u8 *vif, u8 ch);
int wcn36xx_smd_set_link_st(struct wcn36xx *wcn, const u8 *bssid,
			    const u8 *sta_mac,
			    enum wcn36xx_hal_link_state state);
int wcn36xx_smd_config_bss(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, const u8 *bssid,
			   bool update);
int wcn36xx_smd_delete_bss(struct wcn36xx *wcn, struct ieee80211_vif *vif);
int wcn36xx_smd_config_sta(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
int wcn36xx_smd_send_beacon(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			    struct sk_buff *skb_beacon, u16 tim_off,
			    u16 p2p_off);
int wcn36xx_smd_switch_channel(struct wcn36xx *wcn,
			       struct ieee80211_vif *vif, int ch);
int wcn36xx_smd_process_ptt_msg(struct wcn36xx *wcn,
				struct ieee80211_vif *vif,
				void *ptt_msg, size_t len,
				void **ptt_rsp_msg);
int wcn36xx_smd_update_proberesp_tmpl(struct wcn36xx *wcn,
				      struct ieee80211_vif *vif,
				      struct sk_buff *skb);
int wcn36xx_smd_set_stakey(struct wcn36xx *wcn,
			   enum ani_ed_type enc_type,
			   u8 keyidx,
			   u8 keylen,
			   u8 *key,
			   u8 sta_index);
int wcn36xx_smd_set_bsskey(struct wcn36xx *wcn,
			   enum ani_ed_type enc_type,
			   u8 bssidx,
			   u8 keyidx,
			   u8 keylen,
			   u8 *key);
int wcn36xx_smd_remove_stakey(struct wcn36xx *wcn,
			      enum ani_ed_type enc_type,
			      u8 keyidx,
			      u8 sta_index);
int wcn36xx_smd_remove_bsskey(struct wcn36xx *wcn,
			      enum ani_ed_type enc_type,
			      u8 bssidx,
			      u8 keyidx);
int wcn36xx_smd_enter_bmps(struct wcn36xx *wcn, struct ieee80211_vif *vif);
int wcn36xx_smd_exit_bmps(struct wcn36xx *wcn, struct ieee80211_vif *vif);
int wcn36xx_smd_set_power_params(struct wcn36xx *wcn, bool ignore_dtim);
int wcn36xx_smd_keep_alive_req(struct wcn36xx *wcn,
			       struct ieee80211_vif *vif,
			       int packet_type);
int wcn36xx_smd_dump_cmd_req(struct wcn36xx *wcn, u32 arg1, u32 arg2,
			     u32 arg3, u32 arg4, u32 arg5);
int wcn36xx_smd_feature_caps_exchange(struct wcn36xx *wcn);
void set_feat_caps(u32 *bitmap, enum place_holder_in_cap_bitmap cap);
int get_feat_caps(u32 *bitmap, enum place_holder_in_cap_bitmap cap);
void clear_feat_caps(u32 *bitmap, enum place_holder_in_cap_bitmap cap);

int wcn36xx_smd_add_ba_session(struct wcn36xx *wcn,
		struct ieee80211_sta *sta,
		u16 tid,
		u16 *ssn,
		u8 direction,
		u8 sta_index);
int wcn36xx_smd_add_ba(struct wcn36xx *wcn, u8 session_id);
int wcn36xx_smd_del_ba(struct wcn36xx *wcn, u16 tid, u8 direction, u8 sta_index);
int wcn36xx_smd_trigger_ba(struct wcn36xx *wcn, u8 sta_index, u16 tid, u8 session_id);

int wcn36xx_smd_update_cfg(struct wcn36xx *wcn, u32 cfg_id, u32 value);

int wcn36xx_smd_rsp_process(struct rpmsg_device *rpdev,
			    void *buf, int len, void *priv, u32 addr);

int wcn36xx_smd_set_mc_list(struct wcn36xx *wcn,
			    struct ieee80211_vif *vif,
			    struct wcn36xx_hal_rcv_flt_mc_addr_list_type *fp);

int wcn36xx_smd_arp_offload(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			    bool enable);

int wcn36xx_smd_ipv6_ns_offload(struct wcn36xx *wcn, struct ieee80211_vif *vif,
				bool enable);

int wcn36xx_smd_gtk_offload(struct wcn36xx *wcn, struct ieee80211_vif *vif,
			    bool enable);

int wcn36xx_smd_gtk_offload_get_info(struct wcn36xx *wcn,
				     struct ieee80211_vif *vif);

int wcn36xx_smd_wlan_host_suspend_ind(struct wcn36xx *wcn);

#endif	/* _SMD_H_ */

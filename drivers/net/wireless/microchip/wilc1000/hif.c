// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include "netdev.h"

#define WILC_HIF_SCAN_TIMEOUT_MS                5000
#define WILC_HIF_CONNECT_TIMEOUT_MS             9500

#define WILC_FALSE_FRMWR_CHANNEL		100

#define WILC_SCAN_WID_LIST_SIZE		6

struct wilc_rcvd_mac_info {
	u8 status;
};

struct wilc_set_multicast {
	u32 enabled;
	u32 cnt;
	u8 *mc_list;
};

struct host_if_wowlan_trigger {
	u8 wowlan_trigger;
};

struct wilc_del_all_sta {
	u8 assoc_sta;
	u8 mac[WILC_MAX_NUM_STA][ETH_ALEN];
};

union wilc_message_body {
	struct wilc_rcvd_net_info net_info;
	struct wilc_rcvd_mac_info mac_info;
	struct wilc_set_multicast mc_info;
	struct wilc_remain_ch remain_on_ch;
	char *data;
	struct host_if_wowlan_trigger wow_trigger;
};

struct host_if_msg {
	union wilc_message_body body;
	struct wilc_vif *vif;
	struct work_struct work;
	void (*fn)(struct work_struct *ws);
	struct completion work_comp;
	bool is_sync;
};

/* 'msg' should be free by the caller for syc */
static struct host_if_msg*
wilc_alloc_work(struct wilc_vif *vif, void (*work_fun)(struct work_struct *),
		bool is_sync)
{
	struct host_if_msg *msg;

	if (!work_fun)
		return ERR_PTR(-EINVAL);

	msg = kzalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return ERR_PTR(-ENOMEM);
	msg->fn = work_fun;
	msg->vif = vif;
	msg->is_sync = is_sync;
	if (is_sync)
		init_completion(&msg->work_comp);

	return msg;
}

static int wilc_enqueue_work(struct host_if_msg *msg)
{
	INIT_WORK(&msg->work, msg->fn);

	if (!msg->vif || !msg->vif->wilc || !msg->vif->wilc->hif_workqueue)
		return -EINVAL;

	if (!queue_work(msg->vif->wilc->hif_workqueue, &msg->work))
		return -EINVAL;

	return 0;
}

/* The idx starts from 0 to (NUM_CONCURRENT_IFC - 1), but 0 index used as
 * special purpose in wilc device, so we add 1 to the index to starts from 1.
 * As a result, the returned index will be 1 to NUM_CONCURRENT_IFC.
 */
int wilc_get_vif_idx(struct wilc_vif *vif)
{
	return vif->idx + 1;
}

/* We need to minus 1 from idx which is from wilc device to get real index
 * of wilc->vif[], because we add 1 when pass to wilc device in the function
 * wilc_get_vif_idx.
 * As a result, the index should be between 0 and (NUM_CONCURRENT_IFC - 1).
 */
static struct wilc_vif *wilc_get_vif_from_idx(struct wilc *wilc, int idx)
{
	int index = idx - 1;
	struct wilc_vif *vif;

	if (index < 0 || index >= WILC_NUM_CONCURRENT_IFC)
		return NULL;

	wilc_for_each_vif(wilc, vif) {
		if (vif->idx == index)
			return vif;
	}

	return NULL;
}

static int handle_scan_done(struct wilc_vif *vif, enum scan_event evt)
{
	int result = 0;
	u8 abort_running_scan;
	struct wid wid;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_user_scan_req *scan_req;

	if (evt == SCAN_EVENT_ABORTED) {
		abort_running_scan = 1;
		wid.id = WID_ABORT_RUNNING_SCAN;
		wid.type = WID_CHAR;
		wid.val = (s8 *)&abort_running_scan;
		wid.size = sizeof(char);

		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
		if (result) {
			netdev_err(vif->ndev, "Failed to set abort running\n");
			result = -EFAULT;
		}
	}

	if (!hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL\n", __func__);
		return result;
	}

	scan_req = &hif_drv->usr_scan_req;
	if (scan_req->scan_result) {
		scan_req->scan_result(evt, NULL, scan_req->priv);
		scan_req->scan_result = NULL;
	}

	return result;
}

int wilc_scan(struct wilc_vif *vif, u8 scan_source,
	      u8 scan_type, u8 *ch_freq_list,
	      void (*scan_result_fn)(enum scan_event,
				     struct wilc_rcvd_net_info *,
				     struct wilc_priv *),
	      struct cfg80211_scan_request *request)
{
	int result = 0;
	struct wid wid_list[WILC_SCAN_WID_LIST_SIZE];
	u32 index = 0;
	u32 i, scan_timeout;
	u8 *buffer;
	u8 valuesize = 0;
	u8 *search_ssid_vals = NULL;
	const u8 ch_list_len = request->n_channels;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (hif_drv->hif_state >= HOST_IF_SCANNING &&
	    hif_drv->hif_state < HOST_IF_CONNECTED) {
		netdev_err(vif->ndev, "Already scan\n");
		result = -EBUSY;
		goto error;
	}

	if (vif->connecting) {
		netdev_err(vif->ndev, "Don't do obss scan\n");
		result = -EBUSY;
		goto error;
	}

	hif_drv->usr_scan_req.ch_cnt = 0;

	if (request->n_ssids) {
		for (i = 0; i < request->n_ssids; i++)
			valuesize += ((request->ssids[i].ssid_len) + 1);
		search_ssid_vals = kmalloc(valuesize + 1, GFP_KERNEL);
		if (search_ssid_vals) {
			wid_list[index].id = WID_SSID_PROBE_REQ;
			wid_list[index].type = WID_STR;
			wid_list[index].val = search_ssid_vals;
			buffer = wid_list[index].val;

			*buffer++ = request->n_ssids;

			for (i = 0; i < request->n_ssids; i++) {
				*buffer++ = request->ssids[i].ssid_len;
				memcpy(buffer, request->ssids[i].ssid,
				       request->ssids[i].ssid_len);
				buffer += request->ssids[i].ssid_len;
			}
			wid_list[index].size = (s32)(valuesize + 1);
			index++;
		}
	}

	wid_list[index].id = WID_INFO_ELEMENT_PROBE;
	wid_list[index].type = WID_BIN_DATA;
	wid_list[index].val = (s8 *)request->ie;
	wid_list[index].size = request->ie_len;
	index++;

	wid_list[index].id = WID_SCAN_TYPE;
	wid_list[index].type = WID_CHAR;
	wid_list[index].size = sizeof(char);
	wid_list[index].val = (s8 *)&scan_type;
	index++;

	if (scan_type == WILC_FW_PASSIVE_SCAN && request->duration) {
		wid_list[index].id = WID_PASSIVE_SCAN_TIME;
		wid_list[index].type = WID_SHORT;
		wid_list[index].size = sizeof(u16);
		wid_list[index].val = (s8 *)&request->duration;
		index++;

		scan_timeout = (request->duration * ch_list_len) + 500;
	} else {
		scan_timeout = WILC_HIF_SCAN_TIMEOUT_MS;
	}

	wid_list[index].id = WID_SCAN_CHANNEL_LIST;
	wid_list[index].type = WID_BIN_DATA;

	if (ch_freq_list && ch_list_len > 0) {
		for (i = 0; i < ch_list_len; i++) {
			if (ch_freq_list[i] > 0)
				ch_freq_list[i] -= 1;
		}
	}

	wid_list[index].val = ch_freq_list;
	wid_list[index].size = ch_list_len;
	index++;

	wid_list[index].id = WID_START_SCAN_REQ;
	wid_list[index].type = WID_CHAR;
	wid_list[index].size = sizeof(char);
	wid_list[index].val = (s8 *)&scan_source;
	index++;

	hif_drv->usr_scan_req.scan_result = scan_result_fn;
	hif_drv->usr_scan_req.priv = &vif->priv;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list, index);
	if (result) {
		netdev_err(vif->ndev, "Failed to send scan parameters\n");
		goto error;
	}

	hif_drv->scan_timer_vif = vif;
	mod_timer(&hif_drv->scan_timer,
		  jiffies + msecs_to_jiffies(scan_timeout));

error:

	kfree(search_ssid_vals);

	return result;
}

static int wilc_send_connect_wid(struct wilc_vif *vif)
{
	int result = 0;
	struct wid wid_list[5];
	u32 wid_cnt = 0;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_conn_info *conn_attr = &hif_drv->conn_info;
	struct wilc_join_bss_param *bss_param = conn_attr->param;


        wid_list[wid_cnt].id = WID_SET_MFP;
        wid_list[wid_cnt].type = WID_CHAR;
        wid_list[wid_cnt].size = sizeof(char);
        wid_list[wid_cnt].val = (s8 *)&conn_attr->mfp_type;
        wid_cnt++;

	wid_list[wid_cnt].id = WID_INFO_ELEMENT_ASSOCIATE;
	wid_list[wid_cnt].type = WID_BIN_DATA;
	wid_list[wid_cnt].val = conn_attr->req_ies;
	wid_list[wid_cnt].size = conn_attr->req_ies_len;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_11I_MODE;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&conn_attr->security;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_AUTH_TYPE;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&conn_attr->auth_type;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_JOIN_REQ_EXTENDED;
	wid_list[wid_cnt].type = WID_STR;
	wid_list[wid_cnt].size = sizeof(*bss_param);
	wid_list[wid_cnt].val = (u8 *)bss_param;
	wid_cnt++;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list, wid_cnt);
	if (result) {
		netdev_err(vif->ndev, "failed to send config packet\n");
		goto error;
	} else {
                if (conn_attr->auth_type == WILC_FW_AUTH_SAE)
                        hif_drv->hif_state = HOST_IF_EXTERNAL_AUTH;
                else
                        hif_drv->hif_state = HOST_IF_WAITING_CONN_RESP;
	}

	return 0;

error:

	kfree(conn_attr->req_ies);
	conn_attr->req_ies = NULL;

	return result;
}

static void handle_connect_timeout(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	int result;
	struct wid wid;
	u16 dummy_reason_code = 0;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (!hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL\n", __func__);
		goto out;
	}

	hif_drv->hif_state = HOST_IF_IDLE;

	if (hif_drv->conn_info.conn_result) {
		hif_drv->conn_info.conn_result(CONN_DISCONN_EVENT_CONN_RESP,
					       WILC_MAC_STATUS_DISCONNECTED,
					       hif_drv->conn_info.priv);

	} else {
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);
	}

	wid.id = WID_DISCONNECT;
	wid.type = WID_CHAR;
	wid.val = (s8 *)&dummy_reason_code;
	wid.size = sizeof(char);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send disconnect\n");

	hif_drv->conn_info.req_ies_len = 0;
	kfree(hif_drv->conn_info.req_ies);
	hif_drv->conn_info.req_ies = NULL;

out:
	kfree(msg);
}

struct wilc_join_bss_param *
wilc_parse_join_bss_param(struct cfg80211_bss *bss,
			  struct cfg80211_crypto_settings *crypto)
{
	const u8 *ies_data, *tim_elm, *ssid_elm, *rates_ie, *supp_rates_ie;
	const u8 *ht_ie, *wpa_ie, *wmm_ie, *rsn_ie;
	struct ieee80211_p2p_noa_attr noa_attr;
	const struct cfg80211_bss_ies *ies;
	struct wilc_join_bss_param *param;
	u8 rates_len = 0;
	int ies_len;
	u64 ies_tsf;
	int ret;

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		return NULL;

	rcu_read_lock();
	ies = rcu_dereference(bss->ies);
	ies_data = kmemdup(ies->data, ies->len, GFP_ATOMIC);
	if (!ies_data) {
		rcu_read_unlock();
		kfree(param);
		return NULL;
	}
	ies_len = ies->len;
	ies_tsf = ies->tsf;
	rcu_read_unlock();

	param->beacon_period = cpu_to_le16(bss->beacon_interval);
	param->cap_info = cpu_to_le16(bss->capability);
	param->bss_type = WILC_FW_BSS_TYPE_INFRA;
	param->ch = ieee80211_frequency_to_channel(bss->channel->center_freq);
	ether_addr_copy(param->bssid, bss->bssid);

	ssid_elm = cfg80211_find_ie(WLAN_EID_SSID, ies_data, ies_len);
	if (ssid_elm) {
		if (ssid_elm[1] <= IEEE80211_MAX_SSID_LEN)
			memcpy(param->ssid, ssid_elm + 2, ssid_elm[1]);
	}

	tim_elm = cfg80211_find_ie(WLAN_EID_TIM, ies_data, ies_len);
	if (tim_elm && tim_elm[1] >= 2)
		param->dtim_period = tim_elm[3];

	memset(param->p_suites, 0xFF, 3);
	memset(param->akm_suites, 0xFF, 3);

	rates_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, ies_data, ies_len);
	if (rates_ie) {
		rates_len = rates_ie[1];
		if (rates_len > WILC_MAX_RATES_SUPPORTED)
			rates_len = WILC_MAX_RATES_SUPPORTED;
		param->supp_rates[0] = rates_len;
		memcpy(&param->supp_rates[1], rates_ie + 2, rates_len);
	}

	if (rates_len < WILC_MAX_RATES_SUPPORTED) {
		supp_rates_ie = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES,
						 ies_data, ies_len);
		if (supp_rates_ie) {
			u8 ext_rates = supp_rates_ie[1];

			if (ext_rates > (WILC_MAX_RATES_SUPPORTED - rates_len))
				param->supp_rates[0] = WILC_MAX_RATES_SUPPORTED;
			else
				param->supp_rates[0] += ext_rates;

			memcpy(&param->supp_rates[rates_len + 1],
			       supp_rates_ie + 2,
			       (param->supp_rates[0] - rates_len));
		}
	}

	ht_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, ies_data, ies_len);
	if (ht_ie)
		param->ht_capable = true;

	ret = cfg80211_get_p2p_attr(ies_data, ies_len,
				    IEEE80211_P2P_ATTR_ABSENCE_NOTICE,
				    (u8 *)&noa_attr, sizeof(noa_attr));
	if (ret > 0) {
		param->tsf_lo = cpu_to_le32(ies_tsf);
		param->noa_enabled = 1;
		param->idx = noa_attr.index;
		if (noa_attr.oppps_ctwindow & IEEE80211_P2P_OPPPS_ENABLE_BIT) {
			param->opp_enabled = 1;
			param->opp_en.ct_window = noa_attr.oppps_ctwindow;
			param->opp_en.cnt = noa_attr.desc[0].count;
			param->opp_en.duration = noa_attr.desc[0].duration;
			param->opp_en.interval = noa_attr.desc[0].interval;
			param->opp_en.start_time = noa_attr.desc[0].start_time;
		} else {
			param->opp_enabled = 0;
			param->opp_dis.cnt = noa_attr.desc[0].count;
			param->opp_dis.duration = noa_attr.desc[0].duration;
			param->opp_dis.interval = noa_attr.desc[0].interval;
			param->opp_dis.start_time = noa_attr.desc[0].start_time;
		}
	}
	wmm_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					 WLAN_OUI_TYPE_MICROSOFT_WMM,
					 ies_data, ies_len);
	if (wmm_ie) {
		struct ieee80211_wmm_param_ie *ie;

		ie = (struct ieee80211_wmm_param_ie *)wmm_ie;
		if ((ie->oui_subtype == 0 || ie->oui_subtype == 1) &&
		    ie->version == 1) {
			param->wmm_cap = true;
			if (ie->qos_info & BIT(7))
				param->uapsd_cap = true;
		}
	}

	wpa_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					 WLAN_OUI_TYPE_MICROSOFT_WPA,
					 ies_data, ies_len);
	if (wpa_ie) {
		param->mode_802_11i = 1;
		param->rsn_found = true;
	}

	rsn_ie = cfg80211_find_ie(WLAN_EID_RSN, ies_data, ies_len);
	if (rsn_ie) {
		int rsn_ie_len = sizeof(struct element) + rsn_ie[1];
		int offset = 8;

		param->mode_802_11i = 2;
		param->rsn_found = true;

		/* extract RSN capabilities */
		if (offset < rsn_ie_len) {
			/* skip over pairwise suites */
			offset += (rsn_ie[offset] * 4) + 2;

			if (offset < rsn_ie_len) {
				/* skip over authentication suites */
				offset += (rsn_ie[offset] * 4) + 2;

				if (offset + 1 < rsn_ie_len)
					memcpy(param->rsn_cap, &rsn_ie[offset], 2);
			}
		}
	}

	if (param->rsn_found) {
		int i;

		param->rsn_grp_policy = crypto->cipher_group & 0xFF;
		for (i = 0; i < crypto->n_ciphers_pairwise && i < 3; i++)
			param->p_suites[i] = crypto->ciphers_pairwise[i] & 0xFF;

		for (i = 0; i < crypto->n_akm_suites && i < 3; i++)
			param->akm_suites[i] = crypto->akm_suites[i] & 0xFF;
	}

	kfree(ies_data);
	return (void *)param;
}

static void handle_rcvd_ntwrk_info(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_rcvd_net_info *rcvd_info = &msg->body.net_info;
	struct wilc_user_scan_req *scan_req = &msg->vif->hif_drv->usr_scan_req;
	const u8 *ch_elm;
	u8 *ies;
	int ies_len;
	size_t offset;

	if (ieee80211_is_probe_resp(rcvd_info->mgmt->frame_control))
		offset = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
	else if (ieee80211_is_beacon(rcvd_info->mgmt->frame_control))
		offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	else
		goto done;

	ies = rcvd_info->mgmt->u.beacon.variable;
	ies_len = rcvd_info->frame_len - offset;
	if (ies_len <= 0)
		goto done;

	ch_elm = cfg80211_find_ie(WLAN_EID_DS_PARAMS, ies, ies_len);
	if (ch_elm && ch_elm[1] > 0)
		rcvd_info->ch = ch_elm[2];

	if (scan_req->scan_result)
		scan_req->scan_result(SCAN_EVENT_NETWORK_FOUND, rcvd_info,
				      scan_req->priv);

done:
	kfree(rcvd_info->mgmt);
	kfree(msg);
}

static void host_int_get_assoc_res_info(struct wilc_vif *vif,
					u8 *assoc_resp_info,
					u32 max_assoc_resp_info_len,
					u32 *rcvd_assoc_resp_info_len)
{
	int result;
	struct wid wid;

	wid.id = WID_ASSOC_RES_INFO;
	wid.type = WID_STR;
	wid.val = assoc_resp_info;
	wid.size = max_assoc_resp_info_len;

	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result) {
		*rcvd_assoc_resp_info_len = 0;
		netdev_err(vif->ndev, "Failed to send association response\n");
		return;
	}

	*rcvd_assoc_resp_info_len = wid.size;
}

static s32 wilc_parse_assoc_resp_info(u8 *buffer, u32 buffer_len,
				      struct wilc_conn_info *ret_conn_info)
{
	u8 *ies;
	u16 ies_len;
	struct wilc_assoc_resp *res = (struct wilc_assoc_resp *)buffer;

	ret_conn_info->status = le16_to_cpu(res->status_code);
	if (ret_conn_info->status == WLAN_STATUS_SUCCESS) {
		ies = &buffer[sizeof(*res)];
		ies_len = buffer_len - sizeof(*res);

		ret_conn_info->resp_ies = kmemdup(ies, ies_len, GFP_KERNEL);
		if (!ret_conn_info->resp_ies)
			return -ENOMEM;

		ret_conn_info->resp_ies_len = ies_len;
	}

	return 0;
}

static inline void host_int_parse_assoc_resp_info(struct wilc_vif *vif,
						  u8 mac_status)
{
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_conn_info *conn_info = &hif_drv->conn_info;

	if (mac_status == WILC_MAC_STATUS_CONNECTED) {
		u32 assoc_resp_info_len;

		memset(hif_drv->assoc_resp, 0, WILC_MAX_ASSOC_RESP_FRAME_SIZE);

		host_int_get_assoc_res_info(vif, hif_drv->assoc_resp,
					    WILC_MAX_ASSOC_RESP_FRAME_SIZE,
					    &assoc_resp_info_len);

		if (assoc_resp_info_len != 0) {
			s32 err = 0;

			err = wilc_parse_assoc_resp_info(hif_drv->assoc_resp,
							 assoc_resp_info_len,
							 conn_info);
			if (err)
				netdev_err(vif->ndev,
					   "wilc_parse_assoc_resp_info() returned error %d\n",
					   err);
		}
	}

	del_timer(&hif_drv->connect_timer);
	conn_info->conn_result(CONN_DISCONN_EVENT_CONN_RESP, mac_status,
			       hif_drv->conn_info.priv);

	if (mac_status == WILC_MAC_STATUS_CONNECTED &&
	    conn_info->status == WLAN_STATUS_SUCCESS) {
		ether_addr_copy(hif_drv->assoc_bssid, conn_info->bssid);
		hif_drv->hif_state = HOST_IF_CONNECTED;
	} else {
		hif_drv->hif_state = HOST_IF_IDLE;
	}

	kfree(conn_info->resp_ies);
	conn_info->resp_ies = NULL;
	conn_info->resp_ies_len = 0;

	kfree(conn_info->req_ies);
	conn_info->req_ies = NULL;
	conn_info->req_ies_len = 0;
}

void wilc_handle_disconnect(struct wilc_vif *vif)
{
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (hif_drv->usr_scan_req.scan_result) {
		del_timer(&hif_drv->scan_timer);
		handle_scan_done(vif, SCAN_EVENT_ABORTED);
	}

	if (hif_drv->conn_info.conn_result)
		hif_drv->conn_info.conn_result(CONN_DISCONN_EVENT_DISCONN_NOTIF,
					       0, hif_drv->conn_info.priv);

	eth_zero_addr(hif_drv->assoc_bssid);

	hif_drv->conn_info.req_ies_len = 0;
	kfree(hif_drv->conn_info.req_ies);
	hif_drv->conn_info.req_ies = NULL;
	hif_drv->hif_state = HOST_IF_IDLE;
}

static void handle_rcvd_gnrl_async_info(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct wilc_rcvd_mac_info *mac_info = &msg->body.mac_info;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (!hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL\n", __func__);
		goto free_msg;
	}

	if (!hif_drv->conn_info.conn_result) {
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);
		goto free_msg;
	}


        if (hif_drv->hif_state == HOST_IF_EXTERNAL_AUTH) {
                cfg80211_external_auth_request(vif->ndev, &vif->auth,
					       GFP_KERNEL);
                hif_drv->hif_state = HOST_IF_WAITING_CONN_RESP;
        } else if (hif_drv->hif_state == HOST_IF_WAITING_CONN_RESP) {
		host_int_parse_assoc_resp_info(vif, mac_info->status);
	} else if (mac_info->status == WILC_MAC_STATUS_DISCONNECTED) {
		if (hif_drv->hif_state == HOST_IF_CONNECTED) {
			wilc_handle_disconnect(vif);
		} else if (hif_drv->usr_scan_req.scan_result) {
			del_timer(&hif_drv->scan_timer);
			handle_scan_done(vif, SCAN_EVENT_ABORTED);
		}
	}

free_msg:
	kfree(msg);
}

int wilc_disconnect(struct wilc_vif *vif)
{
	struct wid wid;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_user_scan_req *scan_req;
	struct wilc_conn_info *conn_info;
	int result;
	u16 dummy_reason_code = 0;

	wid.id = WID_DISCONNECT;
	wid.type = WID_CHAR;
	wid.val = (s8 *)&dummy_reason_code;
	wid.size = sizeof(char);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result) {
		netdev_err(vif->ndev, "Failed to send disconnect\n");
		return result;
	}

	scan_req = &hif_drv->usr_scan_req;
	conn_info = &hif_drv->conn_info;

	if (scan_req->scan_result) {
		del_timer(&hif_drv->scan_timer);
		scan_req->scan_result(SCAN_EVENT_ABORTED, NULL, scan_req->priv);
		scan_req->scan_result = NULL;
	}

	if (conn_info->conn_result) {
		if (hif_drv->hif_state == HOST_IF_WAITING_CONN_RESP ||
		    hif_drv->hif_state == HOST_IF_EXTERNAL_AUTH)
			del_timer(&hif_drv->connect_timer);

		conn_info->conn_result(CONN_DISCONN_EVENT_DISCONN_NOTIF, 0,
				       conn_info->priv);
	} else {
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);
	}

	hif_drv->hif_state = HOST_IF_IDLE;

	eth_zero_addr(hif_drv->assoc_bssid);

	conn_info->req_ies_len = 0;
	kfree(conn_info->req_ies);
	conn_info->req_ies = NULL;

	return 0;
}

int wilc_get_statistics(struct wilc_vif *vif, struct rf_info *stats)
{
	struct wid wid_list[5];
	u32 wid_cnt = 0, result;

	wid_list[wid_cnt].id = WID_LINKSPEED;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&stats->link_speed;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_RSSI;
	wid_list[wid_cnt].type = WID_CHAR;
	wid_list[wid_cnt].size = sizeof(char);
	wid_list[wid_cnt].val = (s8 *)&stats->rssi;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_SUCCESS_FRAME_COUNT;
	wid_list[wid_cnt].type = WID_INT;
	wid_list[wid_cnt].size = sizeof(u32);
	wid_list[wid_cnt].val = (s8 *)&stats->tx_cnt;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_RECEIVED_FRAGMENT_COUNT;
	wid_list[wid_cnt].type = WID_INT;
	wid_list[wid_cnt].size = sizeof(u32);
	wid_list[wid_cnt].val = (s8 *)&stats->rx_cnt;
	wid_cnt++;

	wid_list[wid_cnt].id = WID_FAILED_COUNT;
	wid_list[wid_cnt].type = WID_INT;
	wid_list[wid_cnt].size = sizeof(u32);
	wid_list[wid_cnt].val = (s8 *)&stats->tx_fail_cnt;
	wid_cnt++;

	result = wilc_send_config_pkt(vif, WILC_GET_CFG, wid_list, wid_cnt);
	if (result) {
		netdev_err(vif->ndev, "Failed to send scan parameters\n");
		return result;
	}

	if (stats->link_speed > TCP_ACK_FILTER_LINK_SPEED_THRESH &&
	    stats->link_speed != DEFAULT_LINK_SPEED)
		wilc_enable_tcp_ack_filter(vif, true);
	else if (stats->link_speed != DEFAULT_LINK_SPEED)
		wilc_enable_tcp_ack_filter(vif, false);

	return result;
}

static void handle_get_statistics(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct rf_info *stats = (struct rf_info *)msg->body.data;

	wilc_get_statistics(vif, stats);

	kfree(msg);
}

static void wilc_hif_pack_sta_param(u8 *cur_byte, const u8 *mac,
				    struct station_parameters *params)
{
	ether_addr_copy(cur_byte, mac);
	cur_byte += ETH_ALEN;

	put_unaligned_le16(params->aid, cur_byte);
	cur_byte += 2;

	*cur_byte++ = params->link_sta_params.supported_rates_len;
	if (params->link_sta_params.supported_rates_len > 0)
		memcpy(cur_byte, params->link_sta_params.supported_rates,
		       params->link_sta_params.supported_rates_len);
	cur_byte += params->link_sta_params.supported_rates_len;

	if (params->link_sta_params.ht_capa) {
		*cur_byte++ = true;
		memcpy(cur_byte, params->link_sta_params.ht_capa,
		       sizeof(struct ieee80211_ht_cap));
	} else {
		*cur_byte++ = false;
	}
	cur_byte += sizeof(struct ieee80211_ht_cap);

	put_unaligned_le16(params->sta_flags_mask, cur_byte);
	cur_byte += 2;
	put_unaligned_le16(params->sta_flags_set, cur_byte);
}

static int handle_remain_on_chan(struct wilc_vif *vif,
				 struct wilc_remain_ch *hif_remain_ch)
{
	int result;
	u8 remain_on_chan_flag;
	struct wid wid;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (hif_drv->usr_scan_req.scan_result)
		return -EBUSY;

	if (hif_drv->hif_state == HOST_IF_WAITING_CONN_RESP)
		return -EBUSY;

	if (vif->connecting)
		return -EBUSY;

	remain_on_chan_flag = true;
	wid.id = WID_REMAIN_ON_CHAN;
	wid.type = WID_STR;
	wid.size = 2;
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		return -ENOMEM;

	wid.val[0] = remain_on_chan_flag;
	wid.val[1] = (s8)hif_remain_ch->ch;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	kfree(wid.val);
	if (result)
		return -EBUSY;

	hif_drv->remain_on_ch.vif = hif_remain_ch->vif;
	hif_drv->remain_on_ch.expired = hif_remain_ch->expired;
	hif_drv->remain_on_ch.ch = hif_remain_ch->ch;
	hif_drv->remain_on_ch.cookie = hif_remain_ch->cookie;
	hif_drv->remain_on_ch_timer_vif = vif;

	return 0;
}

static int wilc_handle_roc_expired(struct wilc_vif *vif, u64 cookie)
{
	u8 remain_on_chan_flag;
	struct wid wid;
	int result;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (vif->priv.p2p_listen_state) {
		remain_on_chan_flag = false;
		wid.id = WID_REMAIN_ON_CHAN;
		wid.type = WID_STR;
		wid.size = 2;

		wid.val = kmalloc(wid.size, GFP_KERNEL);
		if (!wid.val)
			return -ENOMEM;

		wid.val[0] = remain_on_chan_flag;
		wid.val[1] = WILC_FALSE_FRMWR_CHANNEL;

		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
		kfree(wid.val);
		if (result != 0) {
			netdev_err(vif->ndev, "Failed to set remain channel\n");
			return -EINVAL;
		}

		if (hif_drv->remain_on_ch.expired) {
			hif_drv->remain_on_ch.expired(hif_drv->remain_on_ch.vif,
						      cookie);
		}
	} else {
		netdev_dbg(vif->ndev, "Not in listen state\n");
	}

	return 0;
}

static void wilc_handle_listen_state_expired(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);

	wilc_handle_roc_expired(msg->vif, msg->body.remain_on_ch.cookie);
	kfree(msg);
}

static void listen_timer_cb(struct timer_list *t)
{
	struct host_if_drv *hif_drv = from_timer(hif_drv, t,
						      remain_on_ch_timer);
	struct wilc_vif *vif = hif_drv->remain_on_ch_timer_vif;
	int result;
	struct host_if_msg *msg;

	del_timer(&vif->hif_drv->remain_on_ch_timer);

	msg = wilc_alloc_work(vif, wilc_handle_listen_state_expired, false);
	if (IS_ERR(msg))
		return;

	msg->body.remain_on_ch.cookie = vif->hif_drv->remain_on_ch.cookie;

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
	}
}

static void handle_set_mcast_filter(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);
	struct wilc_vif *vif = msg->vif;
	struct wilc_set_multicast *set_mc = &msg->body.mc_info;
	int result;
	struct wid wid;
	u8 *cur_byte;

	wid.id = WID_SETUP_MULTICAST_FILTER;
	wid.type = WID_BIN;
	wid.size = sizeof(struct wilc_set_multicast) + (set_mc->cnt * ETH_ALEN);
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		goto error;

	cur_byte = wid.val;
	put_unaligned_le32(set_mc->enabled, cur_byte);
	cur_byte += 4;

	put_unaligned_le32(set_mc->cnt, cur_byte);
	cur_byte += 4;

	if (set_mc->cnt > 0 && set_mc->mc_list)
		memcpy(cur_byte, set_mc->mc_list, set_mc->cnt * ETH_ALEN);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send setup multicast\n");

error:
	kfree(set_mc->mc_list);
	kfree(wid.val);
	kfree(msg);
}

void wilc_set_wowlan_trigger(struct wilc_vif *vif, bool enabled)
{
	int ret;
	struct wid wid;
	u8 wowlan_trigger = 0;

	if (enabled)
		wowlan_trigger = 1;

	wid.id = WID_WOWLAN_TRIGGER;
	wid.type = WID_CHAR;
	wid.val = &wowlan_trigger;
	wid.size = sizeof(char);

	ret = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (ret)
		pr_err("Failed to send wowlan trigger config packet\n");
}

int wilc_set_external_auth_param(struct wilc_vif *vif,
				 struct cfg80211_external_auth_params *auth)
{
	int ret;
	struct wid wid;
	struct wilc_external_auth_param *param;

	wid.id = WID_EXTERNAL_AUTH_PARAM;
	wid.type = WID_BIN_DATA;
	wid.size = sizeof(*param);
	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param)
		return -EINVAL;

	wid.val = (u8 *)param;
	param->action = auth->action;
	ether_addr_copy(param->bssid, auth->bssid);
	memcpy(param->ssid, auth->ssid.ssid, auth->ssid.ssid_len);
	param->ssid_len = auth->ssid.ssid_len;
	ret = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);

	kfree(param);
	return ret;
}

static void handle_scan_timer(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);

	handle_scan_done(msg->vif, SCAN_EVENT_ABORTED);
	kfree(msg);
}

static void handle_scan_complete(struct work_struct *work)
{
	struct host_if_msg *msg = container_of(work, struct host_if_msg, work);

	del_timer(&msg->vif->hif_drv->scan_timer);

	handle_scan_done(msg->vif, SCAN_EVENT_DONE);

	kfree(msg);
}

static void timer_scan_cb(struct timer_list *t)
{
	struct host_if_drv *hif_drv = from_timer(hif_drv, t, scan_timer);
	struct wilc_vif *vif = hif_drv->scan_timer_vif;
	struct host_if_msg *msg;
	int result;

	msg = wilc_alloc_work(vif, handle_scan_timer, false);
	if (IS_ERR(msg))
		return;

	result = wilc_enqueue_work(msg);
	if (result)
		kfree(msg);
}

static void timer_connect_cb(struct timer_list *t)
{
	struct host_if_drv *hif_drv = from_timer(hif_drv, t,
						      connect_timer);
	struct wilc_vif *vif = hif_drv->connect_timer_vif;
	struct host_if_msg *msg;
	int result;

	msg = wilc_alloc_work(vif, handle_connect_timeout, false);
	if (IS_ERR(msg))
		return;

	result = wilc_enqueue_work(msg);
	if (result)
		kfree(msg);
}

int wilc_add_ptk(struct wilc_vif *vif, const u8 *ptk, u8 ptk_key_len,
		 const u8 *mac_addr, const u8 *rx_mic, const u8 *tx_mic,
		 u8 mode, u8 cipher_mode, u8 index)
{
	int result = 0;
	u8 t_key_len  = ptk_key_len + WILC_RX_MIC_KEY_LEN + WILC_TX_MIC_KEY_LEN;

	if (mode == WILC_AP_MODE) {
		struct wid wid_list[2];
		struct wilc_ap_wpa_ptk *key_buf;

		wid_list[0].id = WID_11I_MODE;
		wid_list[0].type = WID_CHAR;
		wid_list[0].size = sizeof(char);
		wid_list[0].val = (s8 *)&cipher_mode;

		key_buf = kzalloc(sizeof(*key_buf) + t_key_len, GFP_KERNEL);
		if (!key_buf)
			return -ENOMEM;

		ether_addr_copy(key_buf->mac_addr, mac_addr);
		key_buf->index = index;
		key_buf->key_len = t_key_len;
		memcpy(&key_buf->key[0], ptk, ptk_key_len);

		if (rx_mic)
			memcpy(&key_buf->key[ptk_key_len], rx_mic,
			       WILC_RX_MIC_KEY_LEN);

		if (tx_mic)
			memcpy(&key_buf->key[ptk_key_len + WILC_RX_MIC_KEY_LEN],
			       tx_mic, WILC_TX_MIC_KEY_LEN);

		wid_list[1].id = WID_ADD_PTK;
		wid_list[1].type = WID_STR;
		wid_list[1].size = sizeof(*key_buf) + t_key_len;
		wid_list[1].val = (u8 *)key_buf;
		result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list,
					      ARRAY_SIZE(wid_list));
		kfree(key_buf);
	} else if (mode == WILC_STATION_MODE) {
		struct wid wid;
		struct wilc_sta_wpa_ptk *key_buf;

		key_buf = kzalloc(sizeof(*key_buf) + t_key_len, GFP_KERNEL);
		if (!key_buf)
			return -ENOMEM;

		ether_addr_copy(key_buf->mac_addr, mac_addr);
		key_buf->key_len = t_key_len;
		memcpy(&key_buf->key[0], ptk, ptk_key_len);

		if (rx_mic)
			memcpy(&key_buf->key[ptk_key_len], rx_mic,
			       WILC_RX_MIC_KEY_LEN);

		if (tx_mic)
			memcpy(&key_buf->key[ptk_key_len + WILC_RX_MIC_KEY_LEN],
			       tx_mic, WILC_TX_MIC_KEY_LEN);

		wid.id = WID_ADD_PTK;
		wid.type = WID_STR;
		wid.size = sizeof(*key_buf) + t_key_len;
		wid.val = (s8 *)key_buf;
		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
		kfree(key_buf);
	}

	return result;
}

int wilc_add_igtk(struct wilc_vif *vif, const u8 *igtk, u8 igtk_key_len,
		  const u8 *pn, u8 pn_len, const u8 *mac_addr, u8 mode, u8 index)
{
	int result = 0;
	u8 t_key_len = igtk_key_len;
	struct wid wid;
	struct wilc_wpa_igtk *key_buf;

	key_buf = kzalloc(sizeof(*key_buf) + t_key_len, GFP_KERNEL);
	if (!key_buf)
		return -ENOMEM;

	key_buf->index = index;

	memcpy(&key_buf->pn[0], pn, pn_len);
	key_buf->pn_len = pn_len;

	memcpy(&key_buf->key[0], igtk, igtk_key_len);
	key_buf->key_len = t_key_len;

	wid.id = WID_ADD_IGTK;
	wid.type = WID_STR;
	wid.size = sizeof(*key_buf) + t_key_len;
	wid.val = (s8 *)key_buf;
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	kfree(key_buf);

	return result;
}

int wilc_add_rx_gtk(struct wilc_vif *vif, const u8 *rx_gtk, u8 gtk_key_len,
		    u8 index, u32 key_rsc_len, const u8 *key_rsc,
		    const u8 *rx_mic, const u8 *tx_mic, u8 mode,
		    u8 cipher_mode)
{
	int result = 0;
	struct wilc_gtk_key *gtk_key;
	int t_key_len = gtk_key_len + WILC_RX_MIC_KEY_LEN + WILC_TX_MIC_KEY_LEN;

	gtk_key = kzalloc(sizeof(*gtk_key) + t_key_len, GFP_KERNEL);
	if (!gtk_key)
		return -ENOMEM;

	/* fill bssid value only in station mode */
	if (mode == WILC_STATION_MODE &&
	    vif->hif_drv->hif_state == HOST_IF_CONNECTED)
		memcpy(gtk_key->mac_addr, vif->hif_drv->assoc_bssid, ETH_ALEN);

	if (key_rsc)
		memcpy(gtk_key->rsc, key_rsc, 8);
	gtk_key->index = index;
	gtk_key->key_len = t_key_len;
	memcpy(&gtk_key->key[0], rx_gtk, gtk_key_len);

	if (rx_mic)
		memcpy(&gtk_key->key[gtk_key_len], rx_mic, WILC_RX_MIC_KEY_LEN);

	if (tx_mic)
		memcpy(&gtk_key->key[gtk_key_len + WILC_RX_MIC_KEY_LEN],
		       tx_mic, WILC_TX_MIC_KEY_LEN);

	if (mode == WILC_AP_MODE) {
		struct wid wid_list[2];

		wid_list[0].id = WID_11I_MODE;
		wid_list[0].type = WID_CHAR;
		wid_list[0].size = sizeof(char);
		wid_list[0].val = (s8 *)&cipher_mode;

		wid_list[1].id = WID_ADD_RX_GTK;
		wid_list[1].type = WID_STR;
		wid_list[1].size = sizeof(*gtk_key) + t_key_len;
		wid_list[1].val = (u8 *)gtk_key;

		result = wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list,
					      ARRAY_SIZE(wid_list));
	} else if (mode == WILC_STATION_MODE) {
		struct wid wid;

		wid.id = WID_ADD_RX_GTK;
		wid.type = WID_STR;
		wid.size = sizeof(*gtk_key) + t_key_len;
		wid.val = (u8 *)gtk_key;
		result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	}

	kfree(gtk_key);
	return result;
}

int wilc_set_pmkid_info(struct wilc_vif *vif, struct wilc_pmkid_attr *pmkid)
{
	struct wid wid;

	wid.id = WID_PMKID_INFO;
	wid.type = WID_STR;
	wid.size = (pmkid->numpmkid * sizeof(struct wilc_pmkid)) + 1;
	wid.val = (u8 *)pmkid;

	return wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
}

int wilc_get_mac_address(struct wilc_vif *vif, u8 *mac_addr)
{
	int result;
	struct wid wid;

	wid.id = WID_MAC_ADDR;
	wid.type = WID_STR;
	wid.size = ETH_ALEN;
	wid.val = mac_addr;

	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to get mac address\n");

	return result;
}

int wilc_set_mac_address(struct wilc_vif *vif, const u8 *mac_addr)
{
	struct wid wid;
	int result;

	wid.id = WID_MAC_ADDR;
	wid.type = WID_STR;
	wid.size = ETH_ALEN;
	wid.val = (u8 *)mac_addr;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to set mac address\n");

	return result;
}

int wilc_set_join_req(struct wilc_vif *vif, u8 *bssid, const u8 *ies,
		      size_t ies_len)
{
	int result;
	struct host_if_drv *hif_drv = vif->hif_drv;
	struct wilc_conn_info *conn_info = &hif_drv->conn_info;

	if (bssid)
		ether_addr_copy(conn_info->bssid, bssid);

	if (ies) {
		conn_info->req_ies_len = ies_len;
		conn_info->req_ies = kmemdup(ies, ies_len, GFP_KERNEL);
		if (!conn_info->req_ies)
			return -ENOMEM;
	}

	result = wilc_send_connect_wid(vif);
	if (result)
		goto free_ies;

	hif_drv->connect_timer_vif = vif;
	mod_timer(&hif_drv->connect_timer,
		  jiffies + msecs_to_jiffies(WILC_HIF_CONNECT_TIMEOUT_MS));

	return 0;

free_ies:
	kfree(conn_info->req_ies);

	return result;
}

int wilc_set_mac_chnl_num(struct wilc_vif *vif, u8 channel)
{
	struct wid wid;
	int result;

	wid.id = WID_CURRENT_CHANNEL;
	wid.type = WID_CHAR;
	wid.size = sizeof(char);
	wid.val = &channel;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to set channel\n");

	return result;
}

int wilc_set_operation_mode(struct wilc_vif *vif, int index, u8 mode,
			    u8 ifc_id)
{
	struct wid wid;
	int result;
	struct wilc_drv_handler drv;

	wid.id = WID_SET_OPERATION_MODE;
	wid.type = WID_STR;
	wid.size = sizeof(drv);
	wid.val = (u8 *)&drv;

	drv.handler = cpu_to_le32(index);
	drv.mode = (ifc_id | (mode << 1));

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to set driver handler\n");

	return result;
}

s32 wilc_get_inactive_time(struct wilc_vif *vif, const u8 *mac, u32 *out_val)
{
	struct wid wid;
	s32 result;

	wid.id = WID_SET_STA_MAC_INACTIVE_TIME;
	wid.type = WID_STR;
	wid.size = ETH_ALEN;
	wid.val = kzalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		return -ENOMEM;

	ether_addr_copy(wid.val, mac);
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	kfree(wid.val);
	if (result) {
		netdev_err(vif->ndev, "Failed to set inactive mac\n");
		return result;
	}

	wid.id = WID_GET_INACTIVE_TIME;
	wid.type = WID_INT;
	wid.val = (s8 *)out_val;
	wid.size = sizeof(u32);
	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to get inactive time\n");

	return result;
}

int wilc_get_rssi(struct wilc_vif *vif, s8 *rssi_level)
{
	struct wid wid;
	int result;

	if (!rssi_level) {
		netdev_err(vif->ndev, "%s: RSSI level is NULL\n", __func__);
		return -EFAULT;
	}

	wid.id = WID_RSSI;
	wid.type = WID_CHAR;
	wid.size = sizeof(char);
	wid.val = rssi_level;
	result = wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to get RSSI value\n");

	return result;
}

static int wilc_get_stats_async(struct wilc_vif *vif, struct rf_info *stats)
{
	int result;
	struct host_if_msg *msg;

	msg = wilc_alloc_work(vif, handle_get_statistics, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->body.data = (char *)stats;

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
		return result;
	}

	return result;
}

int wilc_hif_set_cfg(struct wilc_vif *vif, struct cfg_param_attr *param)
{
	struct wid wid_list[4];
	int i = 0;

	if (param->flag & WILC_CFG_PARAM_RETRY_SHORT) {
		wid_list[i].id = WID_SHORT_RETRY_LIMIT;
		wid_list[i].val = (s8 *)&param->short_retry_limit;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}
	if (param->flag & WILC_CFG_PARAM_RETRY_LONG) {
		wid_list[i].id = WID_LONG_RETRY_LIMIT;
		wid_list[i].val = (s8 *)&param->long_retry_limit;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}
	if (param->flag & WILC_CFG_PARAM_FRAG_THRESHOLD) {
		wid_list[i].id = WID_FRAG_THRESHOLD;
		wid_list[i].val = (s8 *)&param->frag_threshold;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}
	if (param->flag & WILC_CFG_PARAM_RTS_THRESHOLD) {
		wid_list[i].id = WID_RTS_THRESHOLD;
		wid_list[i].val = (s8 *)&param->rts_threshold;
		wid_list[i].type = WID_SHORT;
		wid_list[i].size = sizeof(u16);
		i++;
	}

	return wilc_send_config_pkt(vif, WILC_SET_CFG, wid_list, i);
}

static void get_periodic_rssi(struct timer_list *t)
{
	struct wilc_vif *vif = from_timer(vif, t, periodic_rssi);

	if (!vif->hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL", __func__);
		return;
	}

	if (vif->hif_drv->hif_state == HOST_IF_CONNECTED)
		wilc_get_stats_async(vif, &vif->periodic_stat);

	mod_timer(&vif->periodic_rssi, jiffies + msecs_to_jiffies(5000));
}

int wilc_init(struct net_device *dev, struct host_if_drv **hif_drv_handler)
{
	struct host_if_drv *hif_drv;
	struct wilc_vif *vif = netdev_priv(dev);

	hif_drv  = kzalloc(sizeof(*hif_drv), GFP_KERNEL);
	if (!hif_drv)
		return -ENOMEM;

	*hif_drv_handler = hif_drv;

	vif->hif_drv = hif_drv;

	timer_setup(&vif->periodic_rssi, get_periodic_rssi, 0);
	mod_timer(&vif->periodic_rssi, jiffies + msecs_to_jiffies(5000));

	timer_setup(&hif_drv->scan_timer, timer_scan_cb, 0);
	timer_setup(&hif_drv->connect_timer, timer_connect_cb, 0);
	timer_setup(&hif_drv->remain_on_ch_timer, listen_timer_cb, 0);

	hif_drv->hif_state = HOST_IF_IDLE;

	hif_drv->p2p_timeout = 0;

	return 0;
}

int wilc_deinit(struct wilc_vif *vif)
{
	int result = 0;
	struct host_if_drv *hif_drv = vif->hif_drv;

	if (!hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL", __func__);
		return -EFAULT;
	}

	mutex_lock(&vif->wilc->deinit_lock);

	timer_shutdown_sync(&hif_drv->scan_timer);
	timer_shutdown_sync(&hif_drv->connect_timer);
	del_timer_sync(&vif->periodic_rssi);
	timer_shutdown_sync(&hif_drv->remain_on_ch_timer);

	if (hif_drv->usr_scan_req.scan_result) {
		hif_drv->usr_scan_req.scan_result(SCAN_EVENT_ABORTED, NULL,
						  hif_drv->usr_scan_req.priv);
		hif_drv->usr_scan_req.scan_result = NULL;
	}

	hif_drv->hif_state = HOST_IF_IDLE;

	kfree(hif_drv);
	vif->hif_drv = NULL;
	mutex_unlock(&vif->wilc->deinit_lock);
	return result;
}

void wilc_network_info_received(struct wilc *wilc, u8 *buffer, u32 length)
{
	struct host_if_drv *hif_drv;
	struct host_if_msg *msg;
	struct wilc_vif *vif;
	int srcu_idx;
	int result;
	int id;

	id = get_unaligned_le32(&buffer[length - 4]);
	srcu_idx = srcu_read_lock(&wilc->srcu);
	vif = wilc_get_vif_from_idx(wilc, id);
	if (!vif)
		goto out;

	hif_drv = vif->hif_drv;
	if (!hif_drv) {
		netdev_err(vif->ndev, "driver not init[%p]\n", hif_drv);
		goto out;
	}

	msg = wilc_alloc_work(vif, handle_rcvd_ntwrk_info, false);
	if (IS_ERR(msg))
		goto out;

	msg->body.net_info.frame_len = get_unaligned_le16(&buffer[6]) - 1;
	msg->body.net_info.rssi = buffer[8];
	msg->body.net_info.mgmt = kmemdup(&buffer[9],
					  msg->body.net_info.frame_len,
					  GFP_KERNEL);
	if (!msg->body.net_info.mgmt) {
		kfree(msg);
		goto out;
	}

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg->body.net_info.mgmt);
		kfree(msg);
	}
out:
	srcu_read_unlock(&wilc->srcu, srcu_idx);
}

void wilc_gnrl_async_info_received(struct wilc *wilc, u8 *buffer, u32 length)
{
	struct host_if_drv *hif_drv;
	struct host_if_msg *msg;
	struct wilc_vif *vif;
	int srcu_idx;
	int result;
	int id;

	mutex_lock(&wilc->deinit_lock);

	id = get_unaligned_le32(&buffer[length - 4]);
	srcu_idx = srcu_read_lock(&wilc->srcu);
	vif = wilc_get_vif_from_idx(wilc, id);
	if (!vif)
		goto out;

	hif_drv = vif->hif_drv;

	if (!hif_drv) {
		goto out;
	}

	if (!hif_drv->conn_info.conn_result) {
		netdev_err(vif->ndev, "%s: conn_result is NULL\n", __func__);
		goto out;
	}

	msg = wilc_alloc_work(vif, handle_rcvd_gnrl_async_info, false);
	if (IS_ERR(msg))
		goto out;

	msg->body.mac_info.status = buffer[7];
	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
	}
out:
	srcu_read_unlock(&wilc->srcu, srcu_idx);
	mutex_unlock(&wilc->deinit_lock);
}

void wilc_scan_complete_received(struct wilc *wilc, u8 *buffer, u32 length)
{
	struct host_if_drv *hif_drv;
	struct wilc_vif *vif;
	int srcu_idx;
	int result;
	int id;

	id = get_unaligned_le32(&buffer[length - 4]);
	srcu_idx = srcu_read_lock(&wilc->srcu);
	vif = wilc_get_vif_from_idx(wilc, id);
	if (!vif)
		goto out;

	hif_drv = vif->hif_drv;
	if (!hif_drv) {
		goto out;
	}

	if (hif_drv->usr_scan_req.scan_result) {
		struct host_if_msg *msg;

		msg = wilc_alloc_work(vif, handle_scan_complete, false);
		if (IS_ERR(msg))
			goto out;

		result = wilc_enqueue_work(msg);
		if (result) {
			netdev_err(vif->ndev, "%s: enqueue work failed\n",
				   __func__);
			kfree(msg);
		}
	}
out:
	srcu_read_unlock(&wilc->srcu, srcu_idx);
}

int wilc_remain_on_channel(struct wilc_vif *vif, u64 cookie, u16 chan,
			   void (*expired)(struct wilc_vif *, u64))
{
	struct wilc_remain_ch roc;
	int result;

	roc.ch = chan;
	roc.expired = expired;
	roc.vif = vif;
	roc.cookie = cookie;
	result = handle_remain_on_chan(vif, &roc);
	if (result)
		netdev_err(vif->ndev, "%s: failed to set remain on channel\n",
			   __func__);

	return result;
}

int wilc_listen_state_expired(struct wilc_vif *vif, u64 cookie)
{
	if (!vif->hif_drv) {
		netdev_err(vif->ndev, "%s: hif driver is NULL", __func__);
		return -EFAULT;
	}

	del_timer(&vif->hif_drv->remain_on_ch_timer);

	return wilc_handle_roc_expired(vif, cookie);
}

void wilc_frame_register(struct wilc_vif *vif, u16 frame_type, bool reg)
{
	struct wid wid;
	int result;
	struct wilc_reg_frame reg_frame;

	wid.id = WID_REGISTER_FRAME;
	wid.type = WID_STR;
	wid.size = sizeof(reg_frame);
	wid.val = (u8 *)&reg_frame;

	memset(&reg_frame, 0x0, sizeof(reg_frame));

	if (reg)
		reg_frame.reg = 1;

	switch (frame_type) {
	case IEEE80211_STYPE_ACTION:
		reg_frame.reg_id = WILC_FW_ACTION_FRM_IDX;
		break;

	case IEEE80211_STYPE_PROBE_REQ:
		reg_frame.reg_id = WILC_FW_PROBE_REQ_IDX;
		break;

        case IEEE80211_STYPE_AUTH:
                reg_frame.reg_id = WILC_FW_AUTH_REQ_IDX;
                break;

	default:
		break;
	}
	reg_frame.frame_type = cpu_to_le16(frame_type);
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to frame register\n");
}

int wilc_add_beacon(struct wilc_vif *vif, u32 interval, u32 dtim_period,
		    struct cfg80211_beacon_data *params)
{
	struct wid wid;
	int result;
	u8 *cur_byte;

	wid.id = WID_ADD_BEACON;
	wid.type = WID_BIN;
	wid.size = params->head_len + params->tail_len + 16;
	wid.val = kzalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		return -ENOMEM;

	cur_byte = wid.val;
	put_unaligned_le32(interval, cur_byte);
	cur_byte += 4;
	put_unaligned_le32(dtim_period, cur_byte);
	cur_byte += 4;
	put_unaligned_le32(params->head_len, cur_byte);
	cur_byte += 4;

	if (params->head_len > 0)
		memcpy(cur_byte, params->head, params->head_len);
	cur_byte += params->head_len;

	put_unaligned_le32(params->tail_len, cur_byte);
	cur_byte += 4;

	if (params->tail_len > 0)
		memcpy(cur_byte, params->tail, params->tail_len);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send add beacon\n");

	kfree(wid.val);

	return result;
}

int wilc_del_beacon(struct wilc_vif *vif)
{
	int result;
	struct wid wid;
	u8 del_beacon = 0;

	wid.id = WID_DEL_BEACON;
	wid.type = WID_CHAR;
	wid.size = sizeof(char);
	wid.val = &del_beacon;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send delete beacon\n");

	return result;
}

int wilc_add_station(struct wilc_vif *vif, const u8 *mac,
		     struct station_parameters *params)
{
	struct wid wid;
	int result;
	u8 *cur_byte;

	wid.id = WID_ADD_STA;
	wid.type = WID_BIN;
	wid.size = WILC_ADD_STA_LENGTH +
		   params->link_sta_params.supported_rates_len;
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		return -ENOMEM;

	cur_byte = wid.val;
	wilc_hif_pack_sta_param(cur_byte, mac, params);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result != 0)
		netdev_err(vif->ndev, "Failed to send add station\n");

	kfree(wid.val);

	return result;
}

int wilc_del_station(struct wilc_vif *vif, const u8 *mac_addr)
{
	struct wid wid;
	int result;

	wid.id = WID_REMOVE_STA;
	wid.type = WID_BIN;
	wid.size = ETH_ALEN;
	wid.val = kzalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		return -ENOMEM;

	if (!mac_addr)
		eth_broadcast_addr(wid.val);
	else
		ether_addr_copy(wid.val, mac_addr);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to del station\n");

	kfree(wid.val);

	return result;
}

int wilc_del_allstation(struct wilc_vif *vif, u8 mac_addr[][ETH_ALEN])
{
	struct wid wid;
	int result;
	int i;
	u8 assoc_sta = 0;
	struct wilc_del_all_sta del_sta;

	memset(&del_sta, 0x0, sizeof(del_sta));
	for (i = 0; i < WILC_MAX_NUM_STA; i++) {
		if (!is_zero_ether_addr(mac_addr[i])) {
			assoc_sta++;
			ether_addr_copy(del_sta.mac[i], mac_addr[i]);
		}
	}

	if (!assoc_sta)
		return 0;

	del_sta.assoc_sta = assoc_sta;

	wid.id = WID_DEL_ALL_STA;
	wid.type = WID_STR;
	wid.size = (assoc_sta * ETH_ALEN) + 1;
	wid.val = (u8 *)&del_sta;

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send delete all station\n");

	return result;
}

int wilc_edit_station(struct wilc_vif *vif, const u8 *mac,
		      struct station_parameters *params)
{
	struct wid wid;
	int result;
	u8 *cur_byte;

	wid.id = WID_EDIT_STA;
	wid.type = WID_BIN;
	wid.size = WILC_ADD_STA_LENGTH +
		   params->link_sta_params.supported_rates_len;
	wid.val = kmalloc(wid.size, GFP_KERNEL);
	if (!wid.val)
		return -ENOMEM;

	cur_byte = wid.val;
	wilc_hif_pack_sta_param(cur_byte, mac, params);

	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send edit station\n");

	kfree(wid.val);
	return result;
}

int wilc_set_power_mgmt(struct wilc_vif *vif, bool enabled, u32 timeout)
{
	struct wilc *wilc = vif->wilc;
	struct wid wid;
	int result;
	s8 power_mode;

	if (enabled)
		power_mode = WILC_FW_MIN_FAST_PS;
	else
		power_mode = WILC_FW_NO_POWERSAVE;

	wid.id = WID_POWER_MANAGEMENT;
	wid.val = &power_mode;
	wid.size = sizeof(char);
	result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
	if (result)
		netdev_err(vif->ndev, "Failed to send power management\n");
	else
		wilc->power_save_mode = enabled;

	return result;
}

int wilc_setup_multicast_filter(struct wilc_vif *vif, u32 enabled, u32 count,
				u8 *mc_list)
{
	int result;
	struct host_if_msg *msg;

	msg = wilc_alloc_work(vif, handle_set_mcast_filter, false);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	msg->body.mc_info.enabled = enabled;
	msg->body.mc_info.cnt = count;
	msg->body.mc_info.mc_list = mc_list;

	result = wilc_enqueue_work(msg);
	if (result) {
		netdev_err(vif->ndev, "%s: enqueue work failed\n", __func__);
		kfree(msg);
	}
	return result;
}

int wilc_set_tx_power(struct wilc_vif *vif, u8 tx_power)
{
	struct wid wid;

	wid.id = WID_TX_POWER;
	wid.type = WID_CHAR;
	wid.val = &tx_power;
	wid.size = sizeof(char);

	return wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
}

int wilc_get_tx_power(struct wilc_vif *vif, u8 *tx_power)
{
	struct wid wid;

	wid.id = WID_TX_POWER;
	wid.type = WID_CHAR;
	wid.val = tx_power;
	wid.size = sizeof(char);

	return wilc_send_config_pkt(vif, WILC_GET_CFG, &wid, 1);
}

int wilc_set_default_mgmt_key_index(struct wilc_vif *vif, u8 index)
{
        struct wid wid;
        int result;

        wid.id = WID_DEFAULT_MGMT_KEY_ID;
        wid.type = WID_CHAR;
        wid.size = sizeof(char);
        wid.val = &index;
        result = wilc_send_config_pkt(vif, WILC_SET_CFG, &wid, 1);
        if (result)
                netdev_err(vif->ndev,
                           "Failed to send default mgmt key index\n");

        return result;
}

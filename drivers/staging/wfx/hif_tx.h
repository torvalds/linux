/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of host-to-chip commands (aka request/confirmation) of WFxxx
 * Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (C) 2010, ST-Ericsson SA
 */
#ifndef WFX_HIF_TX_H
#define WFX_HIF_TX_H

struct ieee80211_channel;
struct ieee80211_bss_conf;
struct ieee80211_tx_queue_params;
struct cfg80211_scan_request;
struct hif_req_add_key;
struct wfx_dev;
struct wfx_vif;

struct wfx_hif_cmd {
	struct mutex      lock;
	struct completion ready;
	struct completion done;
	struct hif_msg    *buf_send;
	void              *buf_recv;
	size_t            len_recv;
	int               ret;
};

void wfx_init_hif_cmd(struct wfx_hif_cmd *wfx_hif_cmd);
int wfx_cmd_send(struct wfx_dev *wdev, struct hif_msg *request,
		 void *reply, size_t reply_len, bool async);

int hif_shutdown(struct wfx_dev *wdev);
int hif_configuration(struct wfx_dev *wdev, const u8 *conf, size_t len);
int hif_reset(struct wfx_vif *wvif, bool reset_stat);
int hif_read_mib(struct wfx_dev *wdev, int vif_id, u16 mib_id,
		 void *buf, size_t buf_size);
int hif_write_mib(struct wfx_dev *wdev, int vif_id, u16 mib_id,
		  void *buf, size_t buf_size);
int hif_scan(struct wfx_vif *wvif, struct cfg80211_scan_request *req80211,
	     int chan_start, int chan_num);
int hif_stop_scan(struct wfx_vif *wvif);
int hif_join(struct wfx_vif *wvif, const struct ieee80211_bss_conf *conf,
	     struct ieee80211_channel *channel, const u8 *ssid, int ssidlen);
int hif_set_pm(struct wfx_vif *wvif, bool ps, int dynamic_ps_timeout);
int hif_set_bss_params(struct wfx_vif *wvif, int aid, int beacon_lost_count);
int hif_add_key(struct wfx_dev *wdev, const struct hif_req_add_key *arg);
int hif_remove_key(struct wfx_dev *wdev, int idx);
int hif_set_edca_queue_params(struct wfx_vif *wvif, u16 queue,
			      const struct ieee80211_tx_queue_params *arg);
int hif_start(struct wfx_vif *wvif, const struct ieee80211_bss_conf *conf,
	      const struct ieee80211_channel *channel);
int hif_beacon_transmit(struct wfx_vif *wvif, bool enable);
int hif_map_link(struct wfx_vif *wvif,
		 bool unmap, u8 *mac_addr, int sta_id, bool mfp);
int hif_update_ie_beacon(struct wfx_vif *wvif, const u8 *ies, size_t ies_len);

#endif

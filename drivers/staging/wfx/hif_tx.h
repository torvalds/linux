/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Implementation of host-to-chip commands (aka request/confirmation) of WFxxx
 * Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (C) 2010, ST-Ericsson SA
 */
#ifndef WFX_HIF_TX_H
#define WFX_HIF_TX_H

#include "hif_api_cmd.h"

struct wfx_dev;
struct wfx_vif;

struct wfx_scan_params {
	struct hif_req_start_scan scan_req;
	struct hif_ssid_def *ssids;
	uint8_t *ch;
};

struct wfx_hif_cmd {
	struct mutex      lock;
	struct mutex      key_renew_lock;
	struct completion ready;
	struct completion done;
	bool              async;
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
int hif_scan(struct wfx_vif *wvif, const struct wfx_scan_params *arg);
int hif_stop_scan(struct wfx_vif *wvif);
int hif_join(struct wfx_vif *wvif, const struct hif_req_join *arg);
int hif_set_pm(struct wfx_vif *wvif, const struct hif_req_set_pm_mode *arg);
int hif_set_bss_params(struct wfx_vif *wvif,
		       const struct hif_req_set_bss_params *arg);
int hif_add_key(struct wfx_dev *wdev, const struct hif_req_add_key *arg);
int hif_remove_key(struct wfx_dev *wdev, int idx);
int hif_set_edca_queue_params(struct wfx_vif *wvif,
			      const struct hif_req_edca_queue_params *arg);
int hif_start(struct wfx_vif *wvif, const struct hif_req_start *arg);
int hif_beacon_transmit(struct wfx_vif *wvif, bool enable);
int hif_map_link(struct wfx_vif *wvif, u8 *mac_addr, int flags, int sta_id);
int hif_update_ie(struct wfx_vif *wvif, const struct hif_ie_flags *target_frame,
		  const u8 *ies, size_t ies_len);
int hif_sl_set_mac_key(struct wfx_dev *wdev, const uint8_t *slk_key, int destination);
int hif_sl_config(struct wfx_dev *wdev, const unsigned long *bitmap);
int hif_sl_send_pub_keys(struct wfx_dev *wdev,
			 const uint8_t *pubkey, const uint8_t *pubkey_hmac);

#endif

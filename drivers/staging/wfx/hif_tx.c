// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of host-to-chip commands (aka request/confirmation) of WFxxx
 * Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/etherdevice.h>

#include "hif_tx.h"
#include "wfx.h"
#include "bh.h"
#include "hwio.h"
#include "debug.h"
#include "sta.h"

void wfx_init_hif_cmd(struct wfx_hif_cmd *hif_cmd)
{
	init_completion(&hif_cmd->ready);
	init_completion(&hif_cmd->done);
	mutex_init(&hif_cmd->lock);
}

static void wfx_fill_header(struct hif_msg *hif, int if_id,
			    unsigned int cmd, size_t size)
{
	if (if_id == -1)
		if_id = 2;

	WARN(cmd > 0x3f, "invalid WSM command %#.2x", cmd);
	WARN(size > 0xFFF, "requested buffer is too large: %zu bytes", size);
	WARN(if_id > 0x3, "invalid interface ID %d", if_id);

	hif->len = cpu_to_le16(size + 4);
	hif->id = cmd;
	hif->interface = if_id;
}

static void *wfx_alloc_hif(size_t body_len, struct hif_msg **hif)
{
	*hif = kzalloc(sizeof(struct hif_msg) + body_len, GFP_KERNEL);
	if (*hif)
		return (*hif)->body;
	else
		return NULL;
}

int wfx_cmd_send(struct wfx_dev *wdev, struct hif_msg *request,
		 void *reply, size_t reply_len, bool no_reply)
{
	const char *mib_name = "";
	const char *mib_sep = "";
	int cmd = request->id;
	int vif = request->interface;
	int ret;

	// Do not wait for any reply if chip is frozen
	if (wdev->chip_frozen)
		return -ETIMEDOUT;

	mutex_lock(&wdev->hif_cmd.lock);
	WARN(wdev->hif_cmd.buf_send, "data locking error");

	// Note: call to complete() below has an implicit memory barrier that
	// hopefully protect buf_send
	wdev->hif_cmd.buf_send = request;
	wdev->hif_cmd.buf_recv = reply;
	wdev->hif_cmd.len_recv = reply_len;
	complete(&wdev->hif_cmd.ready);

	wfx_bh_request_tx(wdev);

	if (no_reply) {
		// Chip won't reply. Give enough time to the wq to send the
		// buffer.
		msleep(100);
		wdev->hif_cmd.buf_send = NULL;
		mutex_unlock(&wdev->hif_cmd.lock);
		return 0;
	}

	if (wdev->poll_irq)
		wfx_bh_poll_irq(wdev);

	ret = wait_for_completion_timeout(&wdev->hif_cmd.done, 1 * HZ);
	if (!ret) {
		dev_err(wdev->dev, "chip is abnormally long to answer\n");
		reinit_completion(&wdev->hif_cmd.ready);
		ret = wait_for_completion_timeout(&wdev->hif_cmd.done, 3 * HZ);
	}
	if (!ret) {
		dev_err(wdev->dev, "chip did not answer\n");
		wfx_pending_dump_old_frames(wdev, 3000);
		wdev->chip_frozen = true;
		reinit_completion(&wdev->hif_cmd.done);
		ret = -ETIMEDOUT;
	} else {
		ret = wdev->hif_cmd.ret;
	}

	wdev->hif_cmd.buf_send = NULL;
	mutex_unlock(&wdev->hif_cmd.lock);

	if (ret &&
	    (cmd == HIF_REQ_ID_READ_MIB || cmd == HIF_REQ_ID_WRITE_MIB)) {
		mib_name = get_mib_name(((u16 *)request)[2]);
		mib_sep = "/";
	}
	if (ret < 0)
		dev_err(wdev->dev,
			"WSM request %s%s%s (%#.2x) on vif %d returned error %d\n",
			get_hif_name(cmd), mib_sep, mib_name, cmd, vif, ret);
	if (ret > 0)
		dev_warn(wdev->dev,
			 "WSM request %s%s%s (%#.2x) on vif %d returned status %d\n",
			 get_hif_name(cmd), mib_sep, mib_name, cmd, vif, ret);

	return ret;
}

// This function is special. After HIF_REQ_ID_SHUT_DOWN, chip won't reply to any
// request anymore. Obviously, only call this function during device unregister.
int hif_shutdown(struct wfx_dev *wdev)
{
	int ret;
	struct hif_msg *hif;

	wfx_alloc_hif(0, &hif);
	if (!hif)
		return -ENOMEM;
	wfx_fill_header(hif, -1, HIF_REQ_ID_SHUT_DOWN, 0);
	ret = wfx_cmd_send(wdev, hif, NULL, 0, true);
	if (wdev->pdata.gpio_wakeup)
		gpiod_set_value(wdev->pdata.gpio_wakeup, 0);
	else
		control_reg_write(wdev, 0);
	kfree(hif);
	return ret;
}

int hif_configuration(struct wfx_dev *wdev, const u8 *conf, size_t len)
{
	int ret;
	size_t buf_len = sizeof(struct hif_req_configuration) + len;
	struct hif_msg *hif;
	struct hif_req_configuration *body = wfx_alloc_hif(buf_len, &hif);

	if (!hif)
		return -ENOMEM;
	body->length = cpu_to_le16(len);
	memcpy(body->pds_data, conf, len);
	wfx_fill_header(hif, -1, HIF_REQ_ID_CONFIGURATION, buf_len);
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_reset(struct wfx_vif *wvif, bool reset_stat)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_reset *body = wfx_alloc_hif(sizeof(*body), &hif);

	if (!hif)
		return -ENOMEM;
	body->reset_stat = reset_stat;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_RESET, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_read_mib(struct wfx_dev *wdev, int vif_id, u16 mib_id,
		 void *val, size_t val_len)
{
	int ret;
	struct hif_msg *hif;
	int buf_len = sizeof(struct hif_cnf_read_mib) + val_len;
	struct hif_req_read_mib *body = wfx_alloc_hif(sizeof(*body), &hif);
	struct hif_cnf_read_mib *reply = kmalloc(buf_len, GFP_KERNEL);

	if (!body || !reply) {
		ret = -ENOMEM;
		goto out;
	}
	body->mib_id = cpu_to_le16(mib_id);
	wfx_fill_header(hif, vif_id, HIF_REQ_ID_READ_MIB, sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, reply, buf_len, false);

	if (!ret && mib_id != le16_to_cpu(reply->mib_id)) {
		dev_warn(wdev->dev, "%s: confirmation mismatch request\n",
			 __func__);
		ret = -EIO;
	}
	if (ret == -ENOMEM)
		dev_err(wdev->dev, "buffer is too small to receive %s (%zu < %d)\n",
			get_mib_name(mib_id), val_len,
			le16_to_cpu(reply->length));
	if (!ret)
		memcpy(val, &reply->mib_data, le16_to_cpu(reply->length));
	else
		memset(val, 0xFF, val_len);
out:
	kfree(hif);
	kfree(reply);
	return ret;
}

int hif_write_mib(struct wfx_dev *wdev, int vif_id, u16 mib_id,
		  void *val, size_t val_len)
{
	int ret;
	struct hif_msg *hif;
	int buf_len = sizeof(struct hif_req_write_mib) + val_len;
	struct hif_req_write_mib *body = wfx_alloc_hif(buf_len, &hif);

	if (!hif)
		return -ENOMEM;
	body->mib_id = cpu_to_le16(mib_id);
	body->length = cpu_to_le16(val_len);
	memcpy(&body->mib_data, val, val_len);
	wfx_fill_header(hif, vif_id, HIF_REQ_ID_WRITE_MIB, buf_len);
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_scan(struct wfx_vif *wvif, struct cfg80211_scan_request *req,
	     int chan_start_idx, int chan_num, int *timeout)
{
	int ret, i;
	struct hif_msg *hif;
	size_t buf_len =
		sizeof(struct hif_req_start_scan_alt) + chan_num * sizeof(u8);
	struct hif_req_start_scan_alt *body = wfx_alloc_hif(buf_len, &hif);
	int tmo_chan_fg, tmo_chan_bg, tmo;

	WARN(chan_num > HIF_API_MAX_NB_CHANNELS, "invalid params");
	WARN(req->n_ssids > HIF_API_MAX_NB_SSIDS, "invalid params");

	if (!hif)
		return -ENOMEM;
	for (i = 0; i < req->n_ssids; i++) {
		memcpy(body->ssid_def[i].ssid, req->ssids[i].ssid,
		       IEEE80211_MAX_SSID_LEN);
		body->ssid_def[i].ssid_length =
			cpu_to_le32(req->ssids[i].ssid_len);
	}
	body->num_of_ssids = HIF_API_MAX_NB_SSIDS;
	body->maintain_current_bss = 1;
	body->disallow_ps = 1;
	body->tx_power_level =
		cpu_to_le32(req->channels[chan_start_idx]->max_power);
	body->num_of_channels = chan_num;
	for (i = 0; i < chan_num; i++)
		body->channel_list[i] =
			req->channels[i + chan_start_idx]->hw_value;
	if (req->no_cck)
		body->max_transmit_rate = API_RATE_INDEX_G_6MBPS;
	else
		body->max_transmit_rate = API_RATE_INDEX_B_1MBPS;
	if (req->channels[chan_start_idx]->flags & IEEE80211_CHAN_NO_IR) {
		body->min_channel_time = cpu_to_le32(50);
		body->max_channel_time = cpu_to_le32(150);
	} else {
		body->min_channel_time = cpu_to_le32(10);
		body->max_channel_time = cpu_to_le32(50);
		body->num_of_probe_requests = 2;
		body->probe_delay = 100;
	}
	tmo_chan_bg = le32_to_cpu(body->max_channel_time) * USEC_PER_TU;
	tmo_chan_fg = 512 * USEC_PER_TU + body->probe_delay;
	tmo_chan_fg *= body->num_of_probe_requests;
	tmo = chan_num * max(tmo_chan_bg, tmo_chan_fg) + 512 * USEC_PER_TU;
	if (timeout)
		*timeout = usecs_to_jiffies(tmo);

	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_START_SCAN, buf_len);
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_stop_scan(struct wfx_vif *wvif)
{
	int ret;
	struct hif_msg *hif;
	// body associated to HIF_REQ_ID_STOP_SCAN is empty
	wfx_alloc_hif(0, &hif);

	if (!hif)
		return -ENOMEM;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_STOP_SCAN, 0);
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_join(struct wfx_vif *wvif, const struct ieee80211_bss_conf *conf,
	     struct ieee80211_channel *channel, const u8 *ssid, int ssidlen)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_join *body = wfx_alloc_hif(sizeof(*body), &hif);

	WARN_ON(!conf->beacon_int);
	WARN_ON(!conf->basic_rates);
	WARN_ON(sizeof(body->ssid) < ssidlen);
	WARN(!conf->ibss_joined && !ssidlen, "joining an unknown BSS");
	if (WARN_ON(!channel))
		return -EINVAL;
	if (!hif)
		return -ENOMEM;
	body->infrastructure_bss_mode = !conf->ibss_joined;
	body->short_preamble = conf->use_short_preamble;
	if (channel->flags & IEEE80211_CHAN_NO_IR)
		body->probe_for_join = 0;
	else
		body->probe_for_join = 1;
	body->channel_number = channel->hw_value;
	body->beacon_interval = cpu_to_le32(conf->beacon_int);
	body->basic_rate_set =
		cpu_to_le32(wfx_rate_mask_to_hw(wvif->wdev, conf->basic_rates));
	memcpy(body->bssid, conf->bssid, sizeof(body->bssid));
	if (ssid) {
		body->ssid_length = cpu_to_le32(ssidlen);
		memcpy(body->ssid, ssid, ssidlen);
	}
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_JOIN, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_set_bss_params(struct wfx_vif *wvif, int aid, int beacon_lost_count)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_set_bss_params *body =
		wfx_alloc_hif(sizeof(*body), &hif);

	if (!hif)
		return -ENOMEM;
	body->aid = cpu_to_le16(aid);
	body->beacon_lost_count = beacon_lost_count;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_SET_BSS_PARAMS,
			sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_add_key(struct wfx_dev *wdev, const struct hif_req_add_key *arg)
{
	int ret;
	struct hif_msg *hif;
	// FIXME: only send necessary bits
	struct hif_req_add_key *body = wfx_alloc_hif(sizeof(*body), &hif);

	if (!hif)
		return -ENOMEM;
	// FIXME: swap bytes as necessary in body
	memcpy(body, arg, sizeof(*body));
	if (wfx_api_older_than(wdev, 1, 5))
		// Legacy firmwares expect that add_key to be sent on right
		// interface.
		wfx_fill_header(hif, arg->int_id, HIF_REQ_ID_ADD_KEY,
				sizeof(*body));
	else
		wfx_fill_header(hif, -1, HIF_REQ_ID_ADD_KEY, sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_remove_key(struct wfx_dev *wdev, int idx)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_remove_key *body = wfx_alloc_hif(sizeof(*body), &hif);

	if (!hif)
		return -ENOMEM;
	body->entry_index = idx;
	wfx_fill_header(hif, -1, HIF_REQ_ID_REMOVE_KEY, sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_set_edca_queue_params(struct wfx_vif *wvif, u16 queue,
			      const struct ieee80211_tx_queue_params *arg)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_edca_queue_params *body = wfx_alloc_hif(sizeof(*body),
							       &hif);

	if (!body)
		return -ENOMEM;

	WARN_ON(arg->aifs > 255);
	if (!hif)
		return -ENOMEM;
	body->aifsn = arg->aifs;
	body->cw_min = cpu_to_le16(arg->cw_min);
	body->cw_max = cpu_to_le16(arg->cw_max);
	body->tx_op_limit = cpu_to_le16(arg->txop * USEC_PER_TXOP);
	body->queue_id = 3 - queue;
	// API 2.0 has changed queue IDs values
	if (wfx_api_older_than(wvif->wdev, 2, 0) && queue == IEEE80211_AC_BE)
		body->queue_id = HIF_QUEUE_ID_BACKGROUND;
	if (wfx_api_older_than(wvif->wdev, 2, 0) && queue == IEEE80211_AC_BK)
		body->queue_id = HIF_QUEUE_ID_BESTEFFORT;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_EDCA_QUEUE_PARAMS,
			sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_set_pm(struct wfx_vif *wvif, bool ps, int dynamic_ps_timeout)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_set_pm_mode *body = wfx_alloc_hif(sizeof(*body), &hif);

	if (!body)
		return -ENOMEM;

	if (!hif)
		return -ENOMEM;
	if (ps) {
		body->enter_psm = 1;
		// Firmware does not support more than 128ms
		body->fast_psm_idle_period = min(dynamic_ps_timeout * 2, 255);
		if (body->fast_psm_idle_period)
			body->fast_psm = 1;
	}
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_SET_PM_MODE, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_start(struct wfx_vif *wvif, const struct ieee80211_bss_conf *conf,
	      const struct ieee80211_channel *channel)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_start *body = wfx_alloc_hif(sizeof(*body), &hif);

	WARN_ON(!conf->beacon_int);
	if (!hif)
		return -ENOMEM;
	body->dtim_period = conf->dtim_period;
	body->short_preamble = conf->use_short_preamble;
	body->channel_number = channel->hw_value;
	body->beacon_interval = cpu_to_le32(conf->beacon_int);
	body->basic_rate_set =
		cpu_to_le32(wfx_rate_mask_to_hw(wvif->wdev, conf->basic_rates));
	body->ssid_length = conf->ssid_len;
	memcpy(body->ssid, conf->ssid, conf->ssid_len);
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_START, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_beacon_transmit(struct wfx_vif *wvif, bool enable)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_beacon_transmit *body = wfx_alloc_hif(sizeof(*body),
							     &hif);

	if (!hif)
		return -ENOMEM;
	body->enable_beaconing = enable ? 1 : 0;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_BEACON_TRANSMIT,
			sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_map_link(struct wfx_vif *wvif, bool unmap, u8 *mac_addr, int sta_id, bool mfp)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_map_link *body = wfx_alloc_hif(sizeof(*body), &hif);

	if (!hif)
		return -ENOMEM;
	if (mac_addr)
		ether_addr_copy(body->mac_addr, mac_addr);
	body->mfpc = mfp ? 1 : 0;
	body->unmap = unmap ? 1 : 0;
	body->peer_sta_id = sta_id;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_MAP_LINK, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_update_ie_beacon(struct wfx_vif *wvif, const u8 *ies, size_t ies_len)
{
	int ret;
	struct hif_msg *hif;
	int buf_len = sizeof(struct hif_req_update_ie) + ies_len;
	struct hif_req_update_ie *body = wfx_alloc_hif(buf_len, &hif);

	if (!hif)
		return -ENOMEM;
	body->beacon = 1;
	body->num_ies = cpu_to_le16(1);
	memcpy(body->ie, ies, ies_len);
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_UPDATE_IE, buf_len);
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

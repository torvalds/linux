// SPDX-License-Identifier: GPL-2.0-only
/*
 * Implementation of host-to-chip commands (aka request/confirmation) of WFxxx
 * Split Mac (WSM) API.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/skbuff.h>
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
	mutex_init(&hif_cmd->key_renew_lock);
}

static void wfx_fill_header(struct hif_msg *hif, int if_id, unsigned int cmd,
			    size_t size)
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

int wfx_cmd_send(struct wfx_dev *wdev, struct hif_msg *request, void *reply,
		 size_t reply_len, bool async)
{
	const char *mib_name = "";
	const char *mib_sep = "";
	int cmd = request->id;
	int vif = request->interface;
	int ret;

	WARN(wdev->hif_cmd.buf_recv && wdev->hif_cmd.async, "API usage error");

	// Do not wait for any reply if chip is frozen
	if (wdev->chip_frozen)
		return -ETIMEDOUT;

	if (cmd != HIF_REQ_ID_SL_EXCHANGE_PUB_KEYS)
		mutex_lock(&wdev->hif_cmd.key_renew_lock);

	mutex_lock(&wdev->hif_cmd.lock);
	WARN(wdev->hif_cmd.buf_send, "data locking error");

	// Note: call to complete() below has an implicit memory barrier that
	// hopefully protect buf_send
	wdev->hif_cmd.buf_send = request;
	wdev->hif_cmd.buf_recv = reply;
	wdev->hif_cmd.len_recv = reply_len;
	wdev->hif_cmd.async = async;
	complete(&wdev->hif_cmd.ready);

	wfx_bh_request_tx(wdev);

	// NOTE: no timeout is catched async is enabled
	if (async)
		return 0;

	ret = wait_for_completion_timeout(&wdev->hif_cmd.done, 1 * HZ);
	if (!ret) {
		dev_err(wdev->dev, "chip is abnormally long to answer\n");
		reinit_completion(&wdev->hif_cmd.ready);
		ret = wait_for_completion_timeout(&wdev->hif_cmd.done, 3 * HZ);
	}
	if (!ret) {
		dev_err(wdev->dev, "chip did not answer\n");
		wfx_pending_dump_old_frames(wdev, 3000);
		wdev->chip_frozen = 1;
		reinit_completion(&wdev->hif_cmd.done);
		ret = -ETIMEDOUT;
	} else {
		ret = wdev->hif_cmd.ret;
	}

	wdev->hif_cmd.buf_send = NULL;
	mutex_unlock(&wdev->hif_cmd.lock);

	if (ret &&
	    (cmd == HIF_REQ_ID_READ_MIB || cmd == HIF_REQ_ID_WRITE_MIB)) {
		mib_name = get_mib_name(((u16 *) request)[2]);
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

	if (cmd != HIF_REQ_ID_SL_EXCHANGE_PUB_KEYS)
		mutex_unlock(&wdev->hif_cmd.key_renew_lock);
	return ret;
}

// This function is special. After HIF_REQ_ID_SHUT_DOWN, chip won't reply to any
// request anymore. We need to slightly hack struct wfx_hif_cmd for that job. Be
// carefull to only call this funcion during device unregister.
int hif_shutdown(struct wfx_dev *wdev)
{
	int ret;
	struct hif_msg *hif;

	wfx_alloc_hif(0, &hif);
	wfx_fill_header(hif, -1, HIF_REQ_ID_SHUT_DOWN, 0);
	ret = wfx_cmd_send(wdev, hif, NULL, 0, true);
	// After this command, chip won't reply. Be sure to give enough time to
	// bh to send buffer:
	msleep(100);
	wdev->hif_cmd.buf_send = NULL;
	if (wdev->pdata.gpio_wakeup)
		gpiod_set_value(wdev->pdata.gpio_wakeup, 0);
	else
		control_reg_write(wdev, 0);
	mutex_unlock(&wdev->hif_cmd.lock);
	kfree(hif);
	return ret;
}

int hif_configuration(struct wfx_dev *wdev, const u8 *conf, size_t len)
{
	int ret;
	size_t buf_len = sizeof(struct hif_req_configuration) + len;
	struct hif_msg *hif;
	struct hif_req_configuration *body = wfx_alloc_hif(buf_len, &hif);

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

	body->reset_flags.reset_stat = reset_stat;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_RESET, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_read_mib(struct wfx_dev *wdev, int vif_id, u16 mib_id, void *val,
		 size_t val_len)
{
	int ret;
	struct hif_msg *hif;
	int buf_len = sizeof(struct hif_cnf_read_mib) + val_len;
	struct hif_req_read_mib *body = wfx_alloc_hif(sizeof(*body), &hif);
	struct hif_cnf_read_mib *reply = kmalloc(buf_len, GFP_KERNEL);

	body->mib_id = cpu_to_le16(mib_id);
	wfx_fill_header(hif, vif_id, HIF_REQ_ID_READ_MIB, sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, reply, buf_len, false);

	if (!ret && mib_id != reply->mib_id) {
		dev_warn(wdev->dev,
			 "%s: confirmation mismatch request\n", __func__);
		ret = -EIO;
	}
	if (ret == -ENOMEM)
		dev_err(wdev->dev,
			"buffer is too small to receive %s (%zu < %d)\n",
			get_mib_name(mib_id), val_len, reply->length);
	if (!ret)
		memcpy(val, &reply->mib_data, reply->length);
	else
		memset(val, 0xFF, val_len);
	kfree(hif);
	kfree(reply);
	return ret;
}

int hif_write_mib(struct wfx_dev *wdev, int vif_id, u16 mib_id, void *val,
		  size_t val_len)
{
	int ret;
	struct hif_msg *hif;
	int buf_len = sizeof(struct hif_req_write_mib) + val_len;
	struct hif_req_write_mib *body = wfx_alloc_hif(buf_len, &hif);

	body->mib_id = cpu_to_le16(mib_id);
	body->length = cpu_to_le16(val_len);
	memcpy(&body->mib_data, val, val_len);
	wfx_fill_header(hif, vif_id, HIF_REQ_ID_WRITE_MIB, buf_len);
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_scan(struct wfx_vif *wvif, const struct wfx_scan_params *arg)
{
	int ret, i;
	struct hif_msg *hif;
	struct hif_ssid_def *ssids;
	size_t buf_len = sizeof(struct hif_req_start_scan) +
		arg->scan_req.num_of_channels * sizeof(u8) +
		arg->scan_req.num_of_ssi_ds * sizeof(struct hif_ssid_def);
	struct hif_req_start_scan *body = wfx_alloc_hif(buf_len, &hif);
	u8 *ptr = (u8 *) body + sizeof(*body);

	WARN(arg->scan_req.num_of_channels > HIF_API_MAX_NB_CHANNELS, "invalid params");
	WARN(arg->scan_req.num_of_ssi_ds > 2, "invalid params");
	WARN(arg->scan_req.band > 1, "invalid params");

	// FIXME: This API is unnecessary complex, fixing NumOfChannels and
	// adding a member SsidDef at end of struct hif_req_start_scan would
	// simplify that a lot.
	memcpy(body, &arg->scan_req, sizeof(*body));
	cpu_to_le32s(&body->min_channel_time);
	cpu_to_le32s(&body->max_channel_time);
	cpu_to_le32s(&body->tx_power_level);
	memcpy(ptr, arg->ssids,
	       arg->scan_req.num_of_ssi_ds * sizeof(struct hif_ssid_def));
	ssids = (struct hif_ssid_def *) ptr;
	for (i = 0; i < body->num_of_ssi_ds; ++i)
		cpu_to_le32s(&ssids[i].ssid_length);
	ptr += arg->scan_req.num_of_ssi_ds * sizeof(struct hif_ssid_def);
	memcpy(ptr, arg->ch, arg->scan_req.num_of_channels * sizeof(u8));
	ptr += arg->scan_req.num_of_channels * sizeof(u8);
	WARN(buf_len != ptr - (u8 *) body, "allocation size mismatch");
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

	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_STOP_SCAN, 0);
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_join(struct wfx_vif *wvif, const struct hif_req_join *arg)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_join *body = wfx_alloc_hif(sizeof(*body), &hif);

	memcpy(body, arg, sizeof(struct hif_req_join));
	cpu_to_le16s(&body->channel_number);
	cpu_to_le16s(&body->atim_window);
	cpu_to_le32s(&body->ssid_length);
	cpu_to_le32s(&body->beacon_interval);
	cpu_to_le32s(&body->basic_rate_set);
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_JOIN, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_set_bss_params(struct wfx_vif *wvif,
		       const struct hif_req_set_bss_params *arg)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_set_bss_params *body = wfx_alloc_hif(sizeof(*body),
							    &hif);

	memcpy(body, arg, sizeof(*body));
	cpu_to_le16s(&body->aid);
	cpu_to_le32s(&body->operational_rate_set);
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

	body->entry_index = idx;
	wfx_fill_header(hif, -1, HIF_REQ_ID_REMOVE_KEY, sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_set_edca_queue_params(struct wfx_vif *wvif,
			      const struct hif_req_edca_queue_params *arg)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_edca_queue_params *body = wfx_alloc_hif(sizeof(*body),
							       &hif);

	// NOTE: queues numerotation are not the same between WFx and Linux
	memcpy(body, arg, sizeof(*body));
	cpu_to_le16s(&body->cw_min);
	cpu_to_le16s(&body->cw_max);
	cpu_to_le16s(&body->tx_op_limit);
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_EDCA_QUEUE_PARAMS,
			sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_set_pm(struct wfx_vif *wvif, const struct hif_req_set_pm_mode *arg)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_set_pm_mode *body = wfx_alloc_hif(sizeof(*body), &hif);

	memcpy(body, arg, sizeof(*body));
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_SET_PM_MODE, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_start(struct wfx_vif *wvif, const struct hif_req_start *arg)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_start *body = wfx_alloc_hif(sizeof(*body), &hif);

	memcpy(body, arg, sizeof(*body));
	cpu_to_le16s(&body->channel_number);
	cpu_to_le32s(&body->beacon_interval);
	cpu_to_le32s(&body->basic_rate_set);
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_START, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_beacon_transmit(struct wfx_vif *wvif, bool enable_beaconing)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_beacon_transmit *body = wfx_alloc_hif(sizeof(*body),
							     &hif);

	body->enable_beaconing = enable_beaconing ? 1 : 0;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_BEACON_TRANSMIT,
			sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_map_link(struct wfx_vif *wvif, u8 *mac_addr, int flags, int sta_id)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_map_link *body = wfx_alloc_hif(sizeof(*body), &hif);

	if (mac_addr)
		ether_addr_copy(body->mac_addr, mac_addr);
	body->map_link_flags = *(struct hif_map_link_flags *) &flags;
	body->peer_sta_id = sta_id;
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_MAP_LINK, sizeof(*body));
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_update_ie(struct wfx_vif *wvif, const struct hif_ie_flags *target_frame,
		  const u8 *ies, size_t ies_len)
{
	int ret;
	struct hif_msg *hif;
	int buf_len = sizeof(struct hif_req_update_ie) + ies_len;
	struct hif_req_update_ie *body = wfx_alloc_hif(buf_len, &hif);

	memcpy(&body->ie_flags, target_frame, sizeof(struct hif_ie_flags));
	body->num_i_es = cpu_to_le16(1);
	memcpy(body->ie, ies, ies_len);
	wfx_fill_header(hif, wvif->id, HIF_REQ_ID_UPDATE_IE, buf_len);
	ret = wfx_cmd_send(wvif->wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_sl_send_pub_keys(struct wfx_dev *wdev, const uint8_t *pubkey,
			 const uint8_t *pubkey_hmac)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_sl_exchange_pub_keys *body = wfx_alloc_hif(sizeof(*body),
								  &hif);

	body->algorithm = HIF_SL_CURVE25519;
	memcpy(body->host_pub_key, pubkey, sizeof(body->host_pub_key));
	memcpy(body->host_pub_key_mac, pubkey_hmac,
	       sizeof(body->host_pub_key_mac));
	wfx_fill_header(hif, -1, HIF_REQ_ID_SL_EXCHANGE_PUB_KEYS,
			sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	// Compatibility with legacy secure link
	if (ret == SL_PUB_KEY_EXCHANGE_STATUS_SUCCESS)
		ret = 0;
	return ret;
}

int hif_sl_config(struct wfx_dev *wdev, const unsigned long *bitmap)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_sl_configure *body = wfx_alloc_hif(sizeof(*body), &hif);

	memcpy(body->encr_bmp, bitmap, sizeof(body->encr_bmp));
	wfx_fill_header(hif, -1, HIF_REQ_ID_SL_CONFIGURE, sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	return ret;
}

int hif_sl_set_mac_key(struct wfx_dev *wdev, const u8 *slk_key,
		       int destination)
{
	int ret;
	struct hif_msg *hif;
	struct hif_req_set_sl_mac_key *body = wfx_alloc_hif(sizeof(*body),
							    &hif);

	memcpy(body->key_value, slk_key, sizeof(body->key_value));
	body->otp_or_ram = destination;
	wfx_fill_header(hif, -1, HIF_REQ_ID_SET_SL_MAC_KEY, sizeof(*body));
	ret = wfx_cmd_send(wdev, hif, NULL, 0, false);
	kfree(hif);
	// Compatibility with legacy secure link
	if (ret == SL_MAC_KEY_STATUS_SUCCESS)
		ret = 0;
	return ret;
}

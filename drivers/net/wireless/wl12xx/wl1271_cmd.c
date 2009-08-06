/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>
#include <linux/etherdevice.h>

#include "wl1271.h"
#include "wl1271_reg.h"
#include "wl1271_spi.h"
#include "wl1271_acx.h"
#include "wl12xx_80211.h"
#include "wl1271_cmd.h"

/*
 * send command to firmware
 *
 * @wl: wl struct
 * @id: command id
 * @buf: buffer containing the command, must work with dma
 * @len: length of the buffer
 */
int wl1271_cmd_send(struct wl1271 *wl, u16 id, void *buf, size_t len)
{
	struct wl1271_cmd_header *cmd;
	unsigned long timeout;
	u32 intr;
	int ret = 0;

	cmd = buf;
	cmd->id = id;
	cmd->status = 0;

	WARN_ON(len % 4 != 0);

	wl1271_spi_mem_write(wl, wl->cmd_box_addr, buf, len);

	wl1271_reg_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_CMD);

	timeout = jiffies + msecs_to_jiffies(WL1271_COMMAND_TIMEOUT);

	intr = wl1271_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	while (!(intr & WL1271_ACX_INTR_CMD_COMPLETE)) {
		if (time_after(jiffies, timeout)) {
			wl1271_error("command complete timeout");
			ret = -ETIMEDOUT;
			goto out;
		}

		msleep(1);

		intr = wl1271_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	}

	wl1271_reg_write32(wl, ACX_REG_INTERRUPT_ACK,
			   WL1271_ACX_INTR_CMD_COMPLETE);

out:
	return ret;
}

int wl1271_cmd_cal_channel_tune(struct wl1271 *wl)
{
	struct wl1271_cmd_cal_channel_tune *cmd;
	int ret = 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->test.id = TEST_CMD_CHANNEL_TUNE;

	cmd->band = WL1271_CHANNEL_TUNE_BAND_2_4;
	/* set up any channel, 7 is in the middle of the range */
	cmd->channel = 7;

	ret = wl1271_cmd_test(wl, cmd, sizeof(*cmd), 0);
	if (ret < 0)
		wl1271_warning("TEST_CMD_CHANNEL_TUNE failed");

	kfree(cmd);
	return ret;
}

int wl1271_cmd_cal_update_ref_point(struct wl1271 *wl)
{
	struct wl1271_cmd_cal_update_ref_point *cmd;
	int ret = 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->test.id = TEST_CMD_UPDATE_PD_REFERENCE_POINT;

	/* FIXME: still waiting for the correct values */
	cmd->ref_power    = 0;
	cmd->ref_detector = 0;

	cmd->sub_band     = WL1271_PD_REFERENCE_POINT_BAND_B_G;

	ret = wl1271_cmd_test(wl, cmd, sizeof(*cmd), 0);
	if (ret < 0)
		wl1271_warning("TEST_CMD_UPDATE_PD_REFERENCE_POINT failed");

	kfree(cmd);
	return ret;
}

int wl1271_cmd_cal_p2g(struct wl1271 *wl)
{
	struct wl1271_cmd_cal_p2g *cmd;
	int ret = 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->test.id = TEST_CMD_P2G_CAL;

	cmd->sub_band_mask = WL1271_CAL_P2G_BAND_B_G;

	ret = wl1271_cmd_test(wl, cmd, sizeof(*cmd), 0);
	if (ret < 0)
		wl1271_warning("TEST_CMD_P2G_CAL failed");

	kfree(cmd);
	return ret;
}

int wl1271_cmd_cal(struct wl1271 *wl)
{
	/*
	 * FIXME: we must make sure that we're not sleeping when calibration
	 * is done
	 */
	int ret;

	wl1271_notice("performing tx calibration");

	ret = wl1271_cmd_cal_channel_tune(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_cal_update_ref_point(wl);
	if (ret < 0)
		return ret;

	ret = wl1271_cmd_cal_p2g(wl);
	if (ret < 0)
		return ret;

	return ret;
}

int wl1271_cmd_join(struct wl1271 *wl, u8 bss_type, u8 dtim_interval,
		    u16 beacon_interval, u8 wait)
{
	static bool do_cal = true;
	unsigned long timeout;
	struct wl1271_cmd_join *join;
	int ret, i;
	u8 *bssid;

	/* FIXME: remove when we get calibration from the factory */
	if (do_cal) {
		ret = wl1271_cmd_cal(wl);
		if (ret < 0)
			wl1271_warning("couldn't calibrate");
		else
			do_cal = false;
	}


	join = kzalloc(sizeof(*join), GFP_KERNEL);
	if (!join) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd join");

	/* Reverse order BSSID */
	bssid = (u8 *) &join->bssid_lsb;
	for (i = 0; i < ETH_ALEN; i++)
		bssid[i] = wl->bssid[ETH_ALEN - i - 1];

	join->rx_config_options = wl->rx_config;
	join->rx_filter_options = wl->rx_filter;

	join->basic_rate_set = RATE_MASK_1MBPS | RATE_MASK_2MBPS |
		RATE_MASK_5_5MBPS | RATE_MASK_11MBPS;

	join->beacon_interval = beacon_interval;
	join->dtim_interval = dtim_interval;
	join->bss_type = bss_type;
	join->channel = wl->channel;
	join->ssid_len = wl->ssid_len;
	memcpy(join->ssid, wl->ssid, wl->ssid_len);
	join->ctrl = WL1271_JOIN_CMD_CTRL_TX_FLUSH;

	/* increment the session counter */
	wl->session_counter++;
	if (wl->session_counter >= SESSION_COUNTER_MAX)
		wl->session_counter = 0;

	join->ctrl |= wl->session_counter << WL1271_JOIN_CMD_TX_SESSION_OFFSET;


	ret = wl1271_cmd_send(wl, CMD_START_JOIN, join, sizeof(*join));
	if (ret < 0) {
		wl1271_error("failed to initiate cmd join");
		goto out_free;
	}

	timeout = msecs_to_jiffies(JOIN_TIMEOUT);

	/*
	 * ugly hack: we should wait for JOIN_EVENT_COMPLETE_ID but to
	 * simplify locking we just sleep instead, for now
	 */
	if (wait)
		msleep(10);

out_free:
	kfree(join);

out:
	return ret;
}

/**
 * send test command to firmware
 *
 * @wl: wl struct
 * @buf: buffer containing the command, with all headers, must work with dma
 * @len: length of the buffer
 * @answer: is answer needed
 */
int wl1271_cmd_test(struct wl1271 *wl, void *buf, size_t buf_len, u8 answer)
{
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd test");

	ret = wl1271_cmd_send(wl, CMD_TEST, buf, buf_len);

	if (ret < 0) {
		wl1271_warning("TEST command failed");
		return ret;
	}

	if (answer) {
		struct wl1271_command *cmd_answer;

		/*
		 * The test command got in, we can read the answer.
		 * The answer would be a wl1271_command, where the
		 * parameter array contains the actual answer.
		 */
		wl1271_spi_mem_read(wl, wl->cmd_box_addr, buf, buf_len);

		cmd_answer = buf;

		if (cmd_answer->header.status != CMD_STATUS_SUCCESS)
			wl1271_error("TEST command answer error: %d",
				     cmd_answer->header.status);
	}

	return 0;
}

/**
 * read acx from firmware
 *
 * @wl: wl struct
 * @id: acx id
 * @buf: buffer for the response, including all headers, must work with dma
 * @len: lenght of buf
 */
int wl1271_cmd_interrogate(struct wl1271 *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd interrogate");

	acx->id = id;

	/* payload length, does not include any headers */
	acx->len = len - sizeof(*acx);

	ret = wl1271_cmd_send(wl, CMD_INTERROGATE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1271_error("INTERROGATE command failed");
		goto out;
	}

	/* the interrogate command got in, we can read the answer */
	wl1271_spi_mem_read(wl, wl->cmd_box_addr, buf, len);

	acx = buf;
	if (acx->cmd.status != CMD_STATUS_SUCCESS)
		wl1271_error("INTERROGATE command error: %d",
			     acx->cmd.status);

out:
	return ret;
}

/**
 * write acx value to firmware
 *
 * @wl: wl struct
 * @id: acx id
 * @buf: buffer containing acx, including all headers, must work with dma
 * @len: length of buf
 */
int wl1271_cmd_configure(struct wl1271 *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl1271_debug(DEBUG_CMD, "cmd configure");

	acx->id = id;

	/* payload length, does not include any headers */
	acx->len = len - sizeof(*acx);

	ret = wl1271_cmd_send(wl, CMD_CONFIGURE, acx, len);
	if (ret < 0) {
		wl1271_warning("CONFIGURE command NOK");
		return ret;
	}

	return 0;
}

int wl1271_cmd_data_path(struct wl1271 *wl, u8 channel, bool enable)
{
	struct cmd_enabledisable_path *cmd;
	int ret;
	u16 cmd_rx, cmd_tx;

	wl1271_debug(DEBUG_CMD, "cmd data path");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->channel = channel;

	if (enable) {
		cmd_rx = CMD_ENABLE_RX;
		cmd_tx = CMD_ENABLE_TX;
	} else {
		cmd_rx = CMD_DISABLE_RX;
		cmd_tx = CMD_DISABLE_TX;
	}

	ret = wl1271_cmd_send(wl, cmd_rx, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1271_error("rx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
		goto out;
	}

	wl1271_debug(DEBUG_BOOT, "rx %s cmd channel %d",
		     enable ? "start" : "stop", channel);

	ret = wl1271_cmd_send(wl, cmd_tx, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1271_error("tx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
		return ret;
	}

	wl1271_debug(DEBUG_BOOT, "tx %s cmd channel %d",
		     enable ? "start" : "stop", channel);

out:
	kfree(cmd);
	return ret;
}

int wl1271_cmd_ps_mode(struct wl1271 *wl, u8 ps_mode)
{
	struct wl1271_cmd_ps_params *ps_params = NULL;
	int ret = 0;

	/* FIXME: this should be in ps.c */
	ret = wl1271_acx_wake_up_conditions(wl, WAKE_UP_EVENT_DTIM_BITMAP,
					    wl->listen_int);
	if (ret < 0) {
		wl1271_error("couldn't set wake up conditions");
		goto out;
	}

	wl1271_debug(DEBUG_CMD, "cmd set ps mode");

	ps_params = kzalloc(sizeof(*ps_params), GFP_KERNEL);
	if (!ps_params) {
		ret = -ENOMEM;
		goto out;
	}

	ps_params->ps_mode = ps_mode;
	ps_params->send_null_data = 1;
	ps_params->retries = 5;
	ps_params->hang_over_period = 128;
	ps_params->null_data_rate = 1; /* 1 Mbps */

	ret = wl1271_cmd_send(wl, CMD_SET_PS_MODE, ps_params,
			      sizeof(*ps_params));
	if (ret < 0) {
		wl1271_error("cmd set_ps_mode failed");
		goto out;
	}

out:
	kfree(ps_params);
	return ret;
}

int wl1271_cmd_read_memory(struct wl1271 *wl, u32 addr, void *answer,
			   size_t len)
{
	struct cmd_read_write_memory *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd read memory");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	WARN_ON(len > MAX_READ_SIZE);
	len = min_t(size_t, len, MAX_READ_SIZE);

	cmd->addr = addr;
	cmd->size = len;

	ret = wl1271_cmd_send(wl, CMD_READ_MEMORY, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1271_error("read memory command failed: %d", ret);
		goto out;
	}

	/* the read command got in, we can now read the answer */
	wl1271_spi_mem_read(wl, wl->cmd_box_addr, cmd, sizeof(*cmd));

	if (cmd->header.status != CMD_STATUS_SUCCESS)
		wl1271_error("error in read command result: %d",
			     cmd->header.status);

	memcpy(answer, cmd->value, len);

out:
	kfree(cmd);
	return ret;
}

int wl1271_cmd_scan(struct wl1271 *wl, u8 *ssid, size_t len,
		    u8 active_scan, u8 high_prio, u8 num_channels,
		    u8 probe_requests)
{

	struct wl1271_cmd_trigger_scan_to *trigger = NULL;
	struct wl1271_cmd_scan *params = NULL;
	int i, ret;
	u16 scan_options = 0;

	if (wl->scanning)
		return -EINVAL;

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;

	params->params.rx_config_options = cpu_to_le32(CFG_RX_ALL_GOOD);
	params->params.rx_filter_options =
		cpu_to_le32(CFG_RX_PRSP_EN | CFG_RX_MGMT_EN | CFG_RX_BCN_EN);

	if (!active_scan)
		scan_options |= WL1271_SCAN_OPT_PASSIVE;
	if (high_prio)
		scan_options |= WL1271_SCAN_OPT_PRIORITY_HIGH;
	params->params.scan_options = scan_options;

	params->params.num_channels = num_channels;
	params->params.num_probe_requests = probe_requests;
	params->params.tx_rate = cpu_to_le32(RATE_MASK_2MBPS);
	params->params.tid_trigger = 0;
	params->params.scan_tag = WL1271_SCAN_DEFAULT_TAG;

	for (i = 0; i < num_channels; i++) {
		params->channels[i].min_duration =
			cpu_to_le32(WL1271_SCAN_CHAN_MIN_DURATION);
		params->channels[i].max_duration =
			cpu_to_le32(WL1271_SCAN_CHAN_MAX_DURATION);
		memset(&params->channels[i].bssid_lsb, 0xff, 4);
		memset(&params->channels[i].bssid_msb, 0xff, 2);
		params->channels[i].early_termination = 0;
		params->channels[i].tx_power_att = WL1271_SCAN_CURRENT_TX_PWR;
		params->channels[i].channel = i + 1;
	}

	if (len && ssid) {
		params->params.ssid_len = len;
		memcpy(params->params.ssid, ssid, len);
	}

	ret = wl1271_cmd_build_probe_req(wl, ssid, len);
	if (ret < 0) {
		wl1271_error("PROBE request template failed");
		goto out;
	}

	trigger = kzalloc(sizeof(*trigger), GFP_KERNEL);
	if (!trigger) {
		ret = -ENOMEM;
		goto out;
	}

	/* disable the timeout */
	trigger->timeout = 0;

	ret = wl1271_cmd_send(wl, CMD_TRIGGER_SCAN_TO, trigger,
			      sizeof(*trigger));
	if (ret < 0) {
		wl1271_error("trigger scan to failed for hw scan");
		goto out;
	}

	wl1271_dump(DEBUG_SCAN, "SCAN: ", params, sizeof(*params));

	wl->scanning = true;

	ret = wl1271_cmd_send(wl, CMD_SCAN, params, sizeof(*params));
	if (ret < 0) {
		wl1271_error("SCAN failed");
		goto out;
	}

	wl1271_spi_mem_read(wl, wl->cmd_box_addr, params, sizeof(*params));

	if (params->header.status != CMD_STATUS_SUCCESS) {
		wl1271_error("Scan command error: %d",
			     params->header.status);
		wl->scanning = false;
		ret = -EIO;
		goto out;
	}

out:
	kfree(params);
	return ret;
}

int wl1271_cmd_template_set(struct wl1271 *wl, u16 template_id,
			    void *buf, size_t buf_len)
{
	struct wl1271_cmd_template_set *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd template_set %d", template_id);

	WARN_ON(buf_len > WL1271_CMD_TEMPL_MAX_SIZE);
	buf_len = min_t(size_t, buf_len, WL1271_CMD_TEMPL_MAX_SIZE);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->len = cpu_to_le16(buf_len);
	cmd->template_type = template_id;
	cmd->enabled_rates = ACX_RATE_MASK_UNSPECIFIED;
	cmd->short_retry_limit = ACX_RATE_RETRY_LIMIT;
	cmd->long_retry_limit = ACX_RATE_RETRY_LIMIT;

	if (buf)
		memcpy(cmd->template_data, buf, buf_len);

	ret = wl1271_cmd_send(wl, CMD_SET_TEMPLATE, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1271_warning("cmd set_template failed: %d", ret);
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

static int wl1271_build_basic_rates(char *rates)
{
	u8 index = 0;

	rates[index++] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_1MB;
	rates[index++] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_2MB;
	rates[index++] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_5MB;
	rates[index++] = IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_11MB;

	return index;
}

static int wl1271_build_extended_rates(char *rates)
{
	u8 index = 0;

	rates[index++] = IEEE80211_OFDM_RATE_6MB;
	rates[index++] = IEEE80211_OFDM_RATE_9MB;
	rates[index++] = IEEE80211_OFDM_RATE_12MB;
	rates[index++] = IEEE80211_OFDM_RATE_18MB;
	rates[index++] = IEEE80211_OFDM_RATE_24MB;
	rates[index++] = IEEE80211_OFDM_RATE_36MB;
	rates[index++] = IEEE80211_OFDM_RATE_48MB;
	rates[index++] = IEEE80211_OFDM_RATE_54MB;

	return index;
}

int wl1271_cmd_build_null_data(struct wl1271 *wl)
{
	struct wl12xx_null_data_template template;

	if (!is_zero_ether_addr(wl->bssid)) {
		memcpy(template.header.da, wl->bssid, ETH_ALEN);
		memcpy(template.header.bssid, wl->bssid, ETH_ALEN);
	} else {
		memset(template.header.da, 0xff, ETH_ALEN);
		memset(template.header.bssid, 0xff, ETH_ALEN);
	}

	memcpy(template.header.sa, wl->mac_addr, ETH_ALEN);
	template.header.frame_ctl = cpu_to_le16(IEEE80211_FTYPE_DATA |
						IEEE80211_STYPE_NULLFUNC);

	return wl1271_cmd_template_set(wl, CMD_TEMPL_NULL_DATA, &template,
				       sizeof(template));

}

int wl1271_cmd_build_ps_poll(struct wl1271 *wl, u16 aid)
{
	struct wl12xx_ps_poll_template template;

	memcpy(template.bssid, wl->bssid, ETH_ALEN);
	memcpy(template.ta, wl->mac_addr, ETH_ALEN);
	template.aid = aid;
	template.fc = cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_PSPOLL);

	return wl1271_cmd_template_set(wl, CMD_TEMPL_PS_POLL, &template,
				       sizeof(template));

}

int wl1271_cmd_build_probe_req(struct wl1271 *wl, u8 *ssid, size_t ssid_len)
{
	struct wl12xx_probe_req_template template;
	struct wl12xx_ie_rates *rates;
	char *ptr;
	u16 size;

	ptr = (char *)&template;
	size = sizeof(struct ieee80211_header);

	memset(template.header.da, 0xff, ETH_ALEN);
	memset(template.header.bssid, 0xff, ETH_ALEN);
	memcpy(template.header.sa, wl->mac_addr, ETH_ALEN);
	template.header.frame_ctl = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);

	/* IEs */
	/* SSID */
	template.ssid.header.id = WLAN_EID_SSID;
	template.ssid.header.len = ssid_len;
	if (ssid_len && ssid)
		memcpy(template.ssid.ssid, ssid, ssid_len);
	size += sizeof(struct wl12xx_ie_header) + ssid_len;
	ptr += size;

	/* Basic Rates */
	rates = (struct wl12xx_ie_rates *)ptr;
	rates->header.id = WLAN_EID_SUPP_RATES;
	rates->header.len = wl1271_build_basic_rates(rates->rates);
	size += sizeof(struct wl12xx_ie_header) + rates->header.len;
	ptr += sizeof(struct wl12xx_ie_header) + rates->header.len;

	/* Extended rates */
	rates = (struct wl12xx_ie_rates *)ptr;
	rates->header.id = WLAN_EID_EXT_SUPP_RATES;
	rates->header.len = wl1271_build_extended_rates(rates->rates);
	size += sizeof(struct wl12xx_ie_header) + rates->header.len;

	wl1271_dump(DEBUG_SCAN, "PROBE REQ: ", &template, size);

	return wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_2_4,
				       &template, size);
}

int wl1271_cmd_set_default_wep_key(struct wl1271 *wl, u8 id)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set_default_wep_key %d", id);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->id = id;
	cmd->key_action = KEY_SET_ID;
	cmd->key_type = KEY_WEP;

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1271_warning("cmd set_default_wep_key failed: %d", ret);
		goto out;
	}

out:
	kfree(cmd);

	return ret;
}

int wl1271_cmd_set_key(struct wl1271 *wl, u16 action, u8 id, u8 key_type,
		       u8 key_size, const u8 *key, const u8 *addr)
{
	struct wl1271_cmd_set_keys *cmd;
	int ret = 0;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	if (key_type != KEY_WEP)
		memcpy(cmd->addr, addr, ETH_ALEN);

	cmd->key_action = action;
	cmd->key_size = key_size;
	cmd->key_type = key_type;

	/* we have only one SSID profile */
	cmd->ssid_profile = 0;

	cmd->id = id;

	/* FIXME: this is from wl1251, needs to be checked */
	if (key_type == KEY_TKIP) {
		/*
		 * We get the key in the following form:
		 * TKIP (16 bytes) - TX MIC (8 bytes) - RX MIC (8 bytes)
		 * but the target is expecting:
		 * TKIP - RX MIC - TX MIC
		 */
		memcpy(cmd->key, key, 16);
		memcpy(cmd->key + 16, key + 24, 8);
		memcpy(cmd->key + 24, key + 16, 8);

	} else {
		memcpy(cmd->key, key, key_size);
	}

	wl1271_dump(DEBUG_CRYPT, "TARGET KEY: ", cmd, sizeof(*cmd));

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1271_warning("could not set keys");
		goto out;
	}

out:
	kfree(cmd);

	return ret;
}

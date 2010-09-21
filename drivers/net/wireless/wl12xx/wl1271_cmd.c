/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009-2010 Nokia Corporation
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
#include <linux/ieee80211.h>
#include <linux/slab.h>

#include "wl1271.h"
#include "wl1271_reg.h"
#include "wl1271_io.h"
#include "wl1271_acx.h"
#include "wl12xx_80211.h"
#include "wl1271_cmd.h"
#include "wl1271_event.h"

#define WL1271_CMD_FAST_POLL_COUNT       50

/*
 * send command to firmware
 *
 * @wl: wl struct
 * @id: command id
 * @buf: buffer containing the command, must work with dma
 * @len: length of the buffer
 */
int wl1271_cmd_send(struct wl1271 *wl, u16 id, void *buf, size_t len,
		    size_t res_len)
{
	struct wl1271_cmd_header *cmd;
	unsigned long timeout;
	u32 intr;
	int ret = 0;
	u16 status;
	u16 poll_count = 0;

	cmd = buf;
	cmd->id = cpu_to_le16(id);
	cmd->status = 0;

	WARN_ON(len % 4 != 0);

	wl1271_write(wl, wl->cmd_box_addr, buf, len, false);

	wl1271_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_CMD);

	timeout = jiffies + msecs_to_jiffies(WL1271_COMMAND_TIMEOUT);

	intr = wl1271_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	while (!(intr & WL1271_ACX_INTR_CMD_COMPLETE)) {
		if (time_after(jiffies, timeout)) {
			wl1271_error("command complete timeout");
			ret = -ETIMEDOUT;
			goto out;
		}

		poll_count++;
		if (poll_count < WL1271_CMD_FAST_POLL_COUNT)
			udelay(10);
		else
			msleep(1);

		intr = wl1271_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	}

	/* read back the status code of the command */
	if (res_len == 0)
		res_len = sizeof(struct wl1271_cmd_header);
	wl1271_read(wl, wl->cmd_box_addr, cmd, res_len, false);

	status = le16_to_cpu(cmd->status);
	if (status != CMD_STATUS_SUCCESS) {
		wl1271_error("command execute failure %d", status);
		ieee80211_queue_work(wl->hw, &wl->recovery_work);
		ret = -EIO;
	}

	wl1271_write32(wl, ACX_REG_INTERRUPT_ACK,
		       WL1271_ACX_INTR_CMD_COMPLETE);

out:
	return ret;
}

int wl1271_cmd_general_parms(struct wl1271 *wl)
{
	struct wl1271_general_parms_cmd *gen_parms;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	gen_parms = kzalloc(sizeof(*gen_parms), GFP_KERNEL);
	if (!gen_parms)
		return -ENOMEM;

	gen_parms->test.id = TEST_CMD_INI_FILE_GENERAL_PARAM;

	memcpy(&gen_parms->general_params, &wl->nvs->general_params,
	       sizeof(struct wl1271_ini_general_params));

	ret = wl1271_cmd_test(wl, gen_parms, sizeof(*gen_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_GENERAL_PARAM failed");

	kfree(gen_parms);
	return ret;
}

int wl1271_cmd_radio_parms(struct wl1271 *wl)
{
	struct wl1271_radio_parms_cmd *radio_parms;
	struct wl1271_ini_general_params *gp = &wl->nvs->general_params;
	int ret;

	if (!wl->nvs)
		return -ENODEV;

	radio_parms = kzalloc(sizeof(*radio_parms), GFP_KERNEL);
	if (!radio_parms)
		return -ENOMEM;

	radio_parms->test.id = TEST_CMD_INI_FILE_RADIO_PARAM;

	/* 2.4GHz parameters */
	memcpy(&radio_parms->static_params_2, &wl->nvs->stat_radio_params_2,
	       sizeof(struct wl1271_ini_band_params_2));
	memcpy(&radio_parms->dyn_params_2,
	       &wl->nvs->dyn_radio_params_2[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl1271_ini_fem_params_2));

	/* 5GHz parameters */
	memcpy(&radio_parms->static_params_5,
	       &wl->nvs->stat_radio_params_5,
	       sizeof(struct wl1271_ini_band_params_5));
	memcpy(&radio_parms->dyn_params_5,
	       &wl->nvs->dyn_radio_params_5[gp->tx_bip_fem_manufacturer].params,
	       sizeof(struct wl1271_ini_fem_params_5));

	wl1271_dump(DEBUG_CMD, "TEST_CMD_INI_FILE_RADIO_PARAM: ",
		    radio_parms, sizeof(*radio_parms));

	ret = wl1271_cmd_test(wl, radio_parms, sizeof(*radio_parms), 0);
	if (ret < 0)
		wl1271_warning("CMD_INI_FILE_RADIO_PARAM failed");

	kfree(radio_parms);
	return ret;
}

/*
 * Poll the mailbox event field until any of the bits in the mask is set or a
 * timeout occurs (WL1271_EVENT_TIMEOUT in msecs)
 */
static int wl1271_cmd_wait_for_event(struct wl1271 *wl, u32 mask)
{
	u32 events_vector, event;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(WL1271_EVENT_TIMEOUT);

	do {
		if (time_after(jiffies, timeout)) {
			ieee80211_queue_work(wl->hw, &wl->recovery_work);
			return -ETIMEDOUT;
		}

		msleep(1);

		/* read from both event fields */
		wl1271_read(wl, wl->mbox_ptr[0], &events_vector,
			    sizeof(events_vector), false);
		event = events_vector & mask;
		wl1271_read(wl, wl->mbox_ptr[1], &events_vector,
			    sizeof(events_vector), false);
		event |= events_vector & mask;
	} while (!event);

	return 0;
}

int wl1271_cmd_join(struct wl1271 *wl, u8 bss_type)
{
	struct wl1271_cmd_join *join;
	int ret, i;
	u8 *bssid;

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

	join->rx_config_options = cpu_to_le32(wl->rx_config);
	join->rx_filter_options = cpu_to_le32(wl->rx_filter);
	join->bss_type = bss_type;
	join->basic_rate_set = cpu_to_le32(wl->basic_rate_set);

	if (wl->band == IEEE80211_BAND_5GHZ)
		join->bss_type |= WL1271_JOIN_CMD_BSS_TYPE_5GHZ;

	join->beacon_interval = cpu_to_le16(wl->beacon_int);
	join->dtim_interval = WL1271_DEFAULT_DTIM_PERIOD;

	join->channel = wl->channel;
	join->ssid_len = wl->ssid_len;
	memcpy(join->ssid, wl->ssid, wl->ssid_len);

	join->ctrl |= wl->session_counter << WL1271_JOIN_CMD_TX_SESSION_OFFSET;

	/* reset TX security counters */
	wl->tx_security_last_seq = 0;
	wl->tx_security_seq = 0;

	ret = wl1271_cmd_send(wl, CMD_START_JOIN, join, sizeof(*join), 0);
	if (ret < 0) {
		wl1271_error("failed to initiate cmd join");
		goto out_free;
	}

	ret = wl1271_cmd_wait_for_event(wl, JOIN_EVENT_COMPLETE_ID);
	if (ret < 0)
		wl1271_error("cmd join event completion error");

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
	size_t res_len = 0;

	wl1271_debug(DEBUG_CMD, "cmd test");

	if (answer)
		res_len = buf_len;

	ret = wl1271_cmd_send(wl, CMD_TEST, buf, buf_len, res_len);

	if (ret < 0) {
		wl1271_warning("TEST command failed");
		return ret;
	}

	return ret;
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

	acx->id = cpu_to_le16(id);

	/* payload length, does not include any headers */
	acx->len = cpu_to_le16(len - sizeof(*acx));

	ret = wl1271_cmd_send(wl, CMD_INTERROGATE, acx, sizeof(*acx), len);
	if (ret < 0)
		wl1271_error("INTERROGATE command failed");

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

	acx->id = cpu_to_le16(id);

	/* payload length, does not include any headers */
	acx->len = cpu_to_le16(len - sizeof(*acx));

	ret = wl1271_cmd_send(wl, CMD_CONFIGURE, acx, len, 0);
	if (ret < 0) {
		wl1271_warning("CONFIGURE command NOK");
		return ret;
	}

	return 0;
}

int wl1271_cmd_data_path(struct wl1271 *wl, bool enable)
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

	/* the channel here is only used for calibration, so hardcoded to 1 */
	cmd->channel = 1;

	if (enable) {
		cmd_rx = CMD_ENABLE_RX;
		cmd_tx = CMD_ENABLE_TX;
	} else {
		cmd_rx = CMD_DISABLE_RX;
		cmd_tx = CMD_DISABLE_TX;
	}

	ret = wl1271_cmd_send(wl, cmd_rx, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("rx %s cmd for channel %d failed",
			     enable ? "start" : "stop", cmd->channel);
		goto out;
	}

	wl1271_debug(DEBUG_BOOT, "rx %s cmd channel %d",
		     enable ? "start" : "stop", cmd->channel);

	ret = wl1271_cmd_send(wl, cmd_tx, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("tx %s cmd for channel %d failed",
			     enable ? "start" : "stop", cmd->channel);
		goto out;
	}

	wl1271_debug(DEBUG_BOOT, "tx %s cmd channel %d",
		     enable ? "start" : "stop", cmd->channel);

out:
	kfree(cmd);
	return ret;
}

int wl1271_cmd_ps_mode(struct wl1271 *wl, u8 ps_mode, u32 rates, bool send)
{
	struct wl1271_cmd_ps_params *ps_params = NULL;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set ps mode");

	ps_params = kzalloc(sizeof(*ps_params), GFP_KERNEL);
	if (!ps_params) {
		ret = -ENOMEM;
		goto out;
	}

	ps_params->ps_mode = ps_mode;
	ps_params->send_null_data = send;
	ps_params->retries = 5;
	ps_params->hang_over_period = 1;
	ps_params->null_data_rate = cpu_to_le32(rates);

	ret = wl1271_cmd_send(wl, CMD_SET_PS_MODE, ps_params,
			      sizeof(*ps_params), 0);
	if (ret < 0) {
		wl1271_error("cmd set_ps_mode failed");
		goto out;
	}

out:
	kfree(ps_params);
	return ret;
}

int wl1271_cmd_template_set(struct wl1271 *wl, u16 template_id,
			    void *buf, size_t buf_len, int index, u32 rates)
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
	cmd->enabled_rates = cpu_to_le32(rates);
	cmd->short_retry_limit = wl->conf.tx.rc_conf.short_retry_limit;
	cmd->long_retry_limit = wl->conf.tx.rc_conf.long_retry_limit;
	cmd->index = index;

	if (buf)
		memcpy(cmd->template_data, buf, buf_len);

	ret = wl1271_cmd_send(wl, CMD_SET_TEMPLATE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("cmd set_template failed: %d", ret);
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl1271_cmd_build_null_data(struct wl1271 *wl)
{
	struct sk_buff *skb = NULL;
	int size;
	void *ptr;
	int ret = -ENOMEM;


	if (wl->bss_type == BSS_TYPE_IBSS) {
		size = sizeof(struct wl12xx_null_data_template);
		ptr = NULL;
	} else {
		skb = ieee80211_nullfunc_get(wl->hw, wl->vif);
		if (!skb)
			goto out;
		size = skb->len;
		ptr = skb->data;
	}

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_NULL_DATA, ptr, size, 0,
				      WL1271_RATE_AUTOMATIC);

out:
	dev_kfree_skb(skb);
	if (ret)
		wl1271_warning("cmd buld null data failed %d", ret);

	return ret;

}

int wl1271_cmd_build_klv_null_data(struct wl1271 *wl)
{
	struct sk_buff *skb = NULL;
	int ret = -ENOMEM;

	skb = ieee80211_nullfunc_get(wl->hw, wl->vif);
	if (!skb)
		goto out;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_KLV,
				      skb->data, skb->len,
				      CMD_TEMPL_KLV_IDX_NULL_DATA,
				      WL1271_RATE_AUTOMATIC);

out:
	dev_kfree_skb(skb);
	if (ret)
		wl1271_warning("cmd build klv null data failed %d", ret);

	return ret;

}

int wl1271_cmd_build_ps_poll(struct wl1271 *wl, u16 aid)
{
	struct sk_buff *skb;
	int ret = 0;

	skb = ieee80211_pspoll_get(wl->hw, wl->vif);
	if (!skb)
		goto out;

	ret = wl1271_cmd_template_set(wl, CMD_TEMPL_PS_POLL, skb->data,
				      skb->len, 0, wl->basic_rate_set);

out:
	dev_kfree_skb(skb);
	return ret;
}

int wl1271_cmd_build_probe_req(struct wl1271 *wl,
			       const u8 *ssid, size_t ssid_len,
			       const u8 *ie, size_t ie_len, u8 band)
{
	struct sk_buff *skb;
	int ret;

	skb = ieee80211_probereq_get(wl->hw, wl->vif, ssid, ssid_len,
				     ie, ie_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	wl1271_dump(DEBUG_SCAN, "PROBE REQ: ", skb->data, skb->len);

	if (band == IEEE80211_BAND_2GHZ)
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_2_4,
					      skb->data, skb->len, 0,
					      wl->conf.tx.basic_rate);
	else
		ret = wl1271_cmd_template_set(wl, CMD_TEMPL_CFG_PROBE_REQ_5,
					      skb->data, skb->len, 0,
					      wl->conf.tx.basic_rate_5);

out:
	dev_kfree_skb(skb);
	return ret;
}

int wl1271_build_qos_null_data(struct wl1271 *wl)
{
	struct ieee80211_qos_hdr template;

	memset(&template, 0, sizeof(template));

	memcpy(template.addr1, wl->bssid, ETH_ALEN);
	memcpy(template.addr2, wl->mac_addr, ETH_ALEN);
	memcpy(template.addr3, wl->bssid, ETH_ALEN);

	template.frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					     IEEE80211_STYPE_QOS_NULLFUNC |
					     IEEE80211_FCTL_TODS);

	/* FIXME: not sure what priority to use here */
	template.qos_ctrl = cpu_to_le16(0);

	return wl1271_cmd_template_set(wl, CMD_TEMPL_QOS_NULL_DATA, &template,
				       sizeof(template), 0,
				       WL1271_RATE_AUTOMATIC);
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
	cmd->key_action = cpu_to_le16(KEY_SET_ID);
	cmd->key_type = KEY_WEP;

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("cmd set_default_wep_key failed: %d", ret);
		goto out;
	}

out:
	kfree(cmd);

	return ret;
}

int wl1271_cmd_set_key(struct wl1271 *wl, u16 action, u8 id, u8 key_type,
		       u8 key_size, const u8 *key, const u8 *addr,
		       u32 tx_seq_32, u16 tx_seq_16)
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

	cmd->key_action = cpu_to_le16(action);
	cmd->key_size = key_size;
	cmd->key_type = key_type;

	cmd->ac_seq_num16[0] = cpu_to_le16(tx_seq_16);
	cmd->ac_seq_num32[0] = cpu_to_le32(tx_seq_32);

	/* we have only one SSID profile */
	cmd->ssid_profile = 0;

	cmd->id = id;

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

	ret = wl1271_cmd_send(wl, CMD_SET_KEYS, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_warning("could not set keys");
	goto out;
	}

out:
	kfree(cmd);

	return ret;
}

int wl1271_cmd_disconnect(struct wl1271 *wl)
{
	struct wl1271_cmd_disconnect *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd disconnect");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->rx_config_options = cpu_to_le32(wl->rx_config);
	cmd->rx_filter_options = cpu_to_le32(wl->rx_filter);
	/* disconnect reason is not used in immediate disconnections */
	cmd->type = DISCONNECT_IMMEDIATE;

	ret = wl1271_cmd_send(wl, CMD_DISCONNECT, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send disconnect command");
		goto out_free;
	}

	ret = wl1271_cmd_wait_for_event(wl, DISCONNECT_EVENT_COMPLETE_ID);
	if (ret < 0)
		wl1271_error("cmd disconnect event completion error");

out_free:
	kfree(cmd);

out:
	return ret;
}

int wl1271_cmd_set_sta_state(struct wl1271 *wl)
{
	struct wl1271_cmd_set_sta_state *cmd;
	int ret = 0;

	wl1271_debug(DEBUG_CMD, "cmd set sta state");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->state = WL1271_CMD_STA_STATE_CONNECTED;

	ret = wl1271_cmd_send(wl, CMD_SET_STA_STATE, cmd, sizeof(*cmd), 0);
	if (ret < 0) {
		wl1271_error("failed to send set STA state command");
		goto out_free;
	}

out_free:
	kfree(cmd);

out:
	return ret;
}

// SPDX-License-Identifier: GPL-2.0
#include "cmd.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>

#include "wl1251.h"
#include "reg.h"
#include "io.h"
#include "ps.h"
#include "acx.h"

/**
 * send command to firmware
 *
 * @wl: wl struct
 * @id: command id
 * @buf: buffer containing the command, must work with dma
 * @len: length of the buffer
 */
int wl1251_cmd_send(struct wl1251 *wl, u16 id, void *buf, size_t len)
{
	struct wl1251_cmd_header *cmd;
	unsigned long timeout;
	u32 intr;
	int ret = 0;

	cmd = buf;
	cmd->id = id;
	cmd->status = 0;

	WARN_ON(len % 4 != 0);

	wl1251_mem_write(wl, wl->cmd_box_addr, buf, len);

	wl1251_reg_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_CMD);

	timeout = jiffies + msecs_to_jiffies(WL1251_COMMAND_TIMEOUT);

	intr = wl1251_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	while (!(intr & WL1251_ACX_INTR_CMD_COMPLETE)) {
		if (time_after(jiffies, timeout)) {
			wl1251_error("command complete timeout");
			ret = -ETIMEDOUT;
			goto out;
		}

		msleep(1);

		intr = wl1251_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	}

	wl1251_reg_write32(wl, ACX_REG_INTERRUPT_ACK,
			   WL1251_ACX_INTR_CMD_COMPLETE);

out:
	return ret;
}

/**
 * send test command to firmware
 *
 * @wl: wl struct
 * @buf: buffer containing the command, with all headers, must work with dma
 * @buf_len: length of the buffer
 * @answer: is answer needed
 */
int wl1251_cmd_test(struct wl1251 *wl, void *buf, size_t buf_len, u8 answer)
{
	int ret;

	wl1251_debug(DEBUG_CMD, "cmd test");

	ret = wl1251_cmd_send(wl, CMD_TEST, buf, buf_len);

	if (ret < 0) {
		wl1251_warning("TEST command failed");
		return ret;
	}

	if (answer) {
		struct wl1251_command *cmd_answer;

		/*
		 * The test command got in, we can read the answer.
		 * The answer would be a wl1251_command, where the
		 * parameter array contains the actual answer.
		 */
		wl1251_mem_read(wl, wl->cmd_box_addr, buf, buf_len);

		cmd_answer = buf;

		if (cmd_answer->header.status != CMD_STATUS_SUCCESS)
			wl1251_error("TEST command answer error: %d",
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
 * @len: length of buf
 */
int wl1251_cmd_interrogate(struct wl1251 *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl1251_debug(DEBUG_CMD, "cmd interrogate");

	acx->id = id;

	/* payload length, does not include any headers */
	acx->len = len - sizeof(*acx);

	ret = wl1251_cmd_send(wl, CMD_INTERROGATE, acx, sizeof(*acx));
	if (ret < 0) {
		wl1251_error("INTERROGATE command failed");
		goto out;
	}

	/* the interrogate command got in, we can read the answer */
	wl1251_mem_read(wl, wl->cmd_box_addr, buf, len);

	acx = buf;
	if (acx->cmd.status != CMD_STATUS_SUCCESS)
		wl1251_error("INTERROGATE command error: %d",
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
int wl1251_cmd_configure(struct wl1251 *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl1251_debug(DEBUG_CMD, "cmd configure");

	acx->id = id;

	/* payload length, does not include any headers */
	acx->len = len - sizeof(*acx);

	ret = wl1251_cmd_send(wl, CMD_CONFIGURE, acx, len);
	if (ret < 0) {
		wl1251_warning("CONFIGURE command NOK");
		return ret;
	}

	return 0;
}

int wl1251_cmd_vbm(struct wl1251 *wl, u8 identity,
		   void *bitmap, u16 bitmap_len, u8 bitmap_control)
{
	struct wl1251_cmd_vbm_update *vbm;
	int ret;

	wl1251_debug(DEBUG_CMD, "cmd vbm");

	vbm = kzalloc(sizeof(*vbm), GFP_KERNEL);
	if (!vbm)
		return -ENOMEM;

	/* Count and period will be filled by the target */
	vbm->tim.bitmap_ctrl = bitmap_control;
	if (bitmap_len > PARTIAL_VBM_MAX) {
		wl1251_warning("cmd vbm len is %d B, truncating to %d",
			       bitmap_len, PARTIAL_VBM_MAX);
		bitmap_len = PARTIAL_VBM_MAX;
	}
	memcpy(vbm->tim.pvb_field, bitmap, bitmap_len);
	vbm->tim.identity = identity;
	vbm->tim.length = bitmap_len + 3;

	vbm->len = cpu_to_le16(bitmap_len + 5);

	ret = wl1251_cmd_send(wl, CMD_VBM, vbm, sizeof(*vbm));
	if (ret < 0) {
		wl1251_error("VBM command failed");
		goto out;
	}

out:
	kfree(vbm);
	return ret;
}

int wl1251_cmd_data_path_rx(struct wl1251 *wl, u8 channel, bool enable)
{
	struct cmd_enabledisable_path *cmd;
	int ret;
	u16 cmd_rx;

	wl1251_debug(DEBUG_CMD, "cmd data path");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->channel = channel;

	if (enable)
		cmd_rx = CMD_ENABLE_RX;
	else
		cmd_rx = CMD_DISABLE_RX;

	ret = wl1251_cmd_send(wl, cmd_rx, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1251_error("rx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
		goto out;
	}

	wl1251_debug(DEBUG_BOOT, "rx %s cmd channel %d",
		     enable ? "start" : "stop", channel);

out:
	kfree(cmd);
	return ret;
}

int wl1251_cmd_data_path_tx(struct wl1251 *wl, u8 channel, bool enable)
{
	struct cmd_enabledisable_path *cmd;
	int ret;
	u16 cmd_tx;

	wl1251_debug(DEBUG_CMD, "cmd data path");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->channel = channel;

	if (enable)
		cmd_tx = CMD_ENABLE_TX;
	else
		cmd_tx = CMD_DISABLE_TX;

	ret = wl1251_cmd_send(wl, cmd_tx, cmd, sizeof(*cmd));
	if (ret < 0)
		wl1251_error("tx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
	else
		wl1251_debug(DEBUG_BOOT, "tx %s cmd channel %d",
			     enable ? "start" : "stop", channel);

	kfree(cmd);
	return ret;
}

int wl1251_cmd_join(struct wl1251 *wl, u8 bss_type, u8 channel,
		    u16 beacon_interval, u8 dtim_interval)
{
	struct cmd_join *join;
	int ret, i;
	u8 *bssid;

	join = kzalloc(sizeof(*join), GFP_KERNEL);
	if (!join)
		return -ENOMEM;

	wl1251_debug(DEBUG_CMD, "cmd join%s ch %d %d/%d",
		     bss_type == BSS_TYPE_IBSS ? " ibss" : "",
		     channel, beacon_interval, dtim_interval);

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
	join->channel = channel;
	join->ctrl = JOIN_CMD_CTRL_TX_FLUSH;

	ret = wl1251_cmd_send(wl, CMD_START_JOIN, join, sizeof(*join));
	if (ret < 0) {
		wl1251_error("failed to initiate cmd join");
		goto out;
	}

out:
	kfree(join);
	return ret;
}

int wl1251_cmd_ps_mode(struct wl1251 *wl, u8 ps_mode)
{
	struct wl1251_cmd_ps_params *ps_params = NULL;
	int ret = 0;

	wl1251_debug(DEBUG_CMD, "cmd set ps mode");

	ps_params = kzalloc(sizeof(*ps_params), GFP_KERNEL);
	if (!ps_params)
		return -ENOMEM;

	ps_params->ps_mode = ps_mode;
	ps_params->send_null_data = 1;
	ps_params->retries = 5;
	ps_params->hang_over_period = 128;
	ps_params->null_data_rate = 1; /* 1 Mbps */

	ret = wl1251_cmd_send(wl, CMD_SET_PS_MODE, ps_params,
			      sizeof(*ps_params));
	if (ret < 0) {
		wl1251_error("cmd set_ps_mode failed");
		goto out;
	}

out:
	kfree(ps_params);
	return ret;
}

int wl1251_cmd_read_memory(struct wl1251 *wl, u32 addr, void *answer,
			   size_t len)
{
	struct cmd_read_write_memory *cmd;
	int ret = 0;

	wl1251_debug(DEBUG_CMD, "cmd read memory");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	WARN_ON(len > MAX_READ_SIZE);
	len = min_t(size_t, len, MAX_READ_SIZE);

	cmd->addr = addr;
	cmd->size = len;

	ret = wl1251_cmd_send(wl, CMD_READ_MEMORY, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1251_error("read memory command failed: %d", ret);
		goto out;
	}

	/* the read command got in, we can now read the answer */
	wl1251_mem_read(wl, wl->cmd_box_addr, cmd, sizeof(*cmd));

	if (cmd->header.status != CMD_STATUS_SUCCESS)
		wl1251_error("error in read command result: %d",
			     cmd->header.status);

	memcpy(answer, cmd->value, len);

out:
	kfree(cmd);
	return ret;
}

int wl1251_cmd_template_set(struct wl1251 *wl, u16 cmd_id,
			    void *buf, size_t buf_len)
{
	struct wl1251_cmd_packet_template *cmd;
	size_t cmd_len;
	int ret = 0;

	wl1251_debug(DEBUG_CMD, "cmd template %d", cmd_id);

	WARN_ON(buf_len > WL1251_MAX_TEMPLATE_SIZE);
	buf_len = min_t(size_t, buf_len, WL1251_MAX_TEMPLATE_SIZE);
	cmd_len = ALIGN(sizeof(*cmd) + buf_len, 4);

	cmd = kzalloc(cmd_len, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->size = cpu_to_le16(buf_len);

	if (buf)
		memcpy(cmd->data, buf, buf_len);

	ret = wl1251_cmd_send(wl, cmd_id, cmd, cmd_len);
	if (ret < 0) {
		wl1251_warning("cmd set_template failed: %d", ret);
		goto out;
	}

out:
	kfree(cmd);
	return ret;
}

int wl1251_cmd_scan(struct wl1251 *wl, u8 *ssid, size_t ssid_len,
		    struct ieee80211_channel *channels[],
		    unsigned int n_channels, unsigned int n_probes)
{
	struct wl1251_cmd_scan *cmd;
	int i, ret = 0;

	wl1251_debug(DEBUG_CMD, "cmd scan channels %d", n_channels);

	WARN_ON(n_channels > SCAN_MAX_NUM_OF_CHANNELS);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->params.rx_config_options = cpu_to_le32(CFG_RX_ALL_GOOD);
	cmd->params.rx_filter_options = cpu_to_le32(CFG_RX_PRSP_EN |
						    CFG_RX_MGMT_EN |
						    CFG_RX_BCN_EN);
	cmd->params.scan_options = 0;
	/*
	 * Use high priority scan when not associated to prevent fw issue
	 * causing never-ending scans (sometimes 20+ minutes).
	 * Note: This bug may be caused by the fw's DTIM handling.
	 */
	if (is_zero_ether_addr(wl->bssid))
		cmd->params.scan_options |= cpu_to_le16(WL1251_SCAN_OPT_PRIORITY_HIGH);
	cmd->params.num_channels = n_channels;
	cmd->params.num_probe_requests = n_probes;
	cmd->params.tx_rate = cpu_to_le16(1 << 1); /* 2 Mbps */
	cmd->params.tid_trigger = 0;

	for (i = 0; i < n_channels; i++) {
		cmd->channels[i].min_duration =
			cpu_to_le32(WL1251_SCAN_MIN_DURATION);
		cmd->channels[i].max_duration =
			cpu_to_le32(WL1251_SCAN_MAX_DURATION);
		memset(&cmd->channels[i].bssid_lsb, 0xff, 4);
		memset(&cmd->channels[i].bssid_msb, 0xff, 2);
		cmd->channels[i].early_termination = 0;
		cmd->channels[i].tx_power_att = 0;
		cmd->channels[i].channel = channels[i]->hw_value;
	}

	if (ssid) {
		int len = clamp_val(ssid_len, 0, IEEE80211_MAX_SSID_LEN);

		cmd->params.ssid_len = len;
		memcpy(cmd->params.ssid, ssid, len);
	}

	ret = wl1251_cmd_send(wl, CMD_SCAN, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1251_error("cmd scan failed: %d", ret);
		goto out;
	}

	wl1251_mem_read(wl, wl->cmd_box_addr, cmd, sizeof(*cmd));

	if (cmd->header.status != CMD_STATUS_SUCCESS) {
		wl1251_error("cmd scan status wasn't success: %d",
			     cmd->header.status);
		ret = -EIO;
		goto out;
	}

out:
	kfree(cmd);
	return ret;
}

int wl1251_cmd_trigger_scan_to(struct wl1251 *wl, u32 timeout)
{
	struct wl1251_cmd_trigger_scan_to *cmd;
	int ret;

	wl1251_debug(DEBUG_CMD, "cmd trigger scan to");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->timeout = timeout;

	ret = wl1251_cmd_send(wl, CMD_TRIGGER_SCAN_TO, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl1251_error("cmd trigger scan to failed: %d", ret);
		goto out;
	}

out:
	kfree(cmd);
	return ret;
}

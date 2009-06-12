#include "wl1251_cmd.h"

#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>

#include "wl12xx.h"
#include "wl12xx_80211.h"
#include "reg.h"
#include "wl1251_spi.h"
#include "wl1251_ps.h"
#include "wl1251_acx.h"

/**
 * send command to firmware
 *
 * @wl: wl struct
 * @id: command id
 * @buf: buffer containing the command, must work with dma
 * @len: length of the buffer
 */
int wl12xx_cmd_send(struct wl12xx *wl, u16 id, void *buf, size_t len)
{
	struct wl12xx_cmd_header *cmd;
	unsigned long timeout;
	u32 intr;
	int ret = 0;

	cmd = buf;
	cmd->id = id;
	cmd->status = 0;

	WARN_ON(len % 4 != 0);

	wl12xx_spi_mem_write(wl, wl->cmd_box_addr, buf, len);

	wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_TRIG, INTR_TRIG_CMD);

	timeout = jiffies + msecs_to_jiffies(WL12XX_COMMAND_TIMEOUT);

	intr = wl12xx_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	while (!(intr & wl->chip.intr_cmd_complete)) {
		if (time_after(jiffies, timeout)) {
			wl12xx_error("command complete timeout");
			ret = -ETIMEDOUT;
			goto out;
		}

		msleep(1);

		intr = wl12xx_reg_read32(wl, ACX_REG_INTERRUPT_NO_CLEAR);
	}

	wl12xx_reg_write32(wl, ACX_REG_INTERRUPT_ACK,
			   wl->chip.intr_cmd_complete);

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
int wl12xx_cmd_test(struct wl12xx *wl, void *buf, size_t buf_len, u8 answer)
{
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd test");

	ret = wl12xx_cmd_send(wl, CMD_TEST, buf, buf_len);

	if (ret < 0) {
		wl12xx_warning("TEST command failed");
		return ret;
	}

	if (answer) {
		struct wl12xx_command *cmd_answer;

		/*
		 * The test command got in, we can read the answer.
		 * The answer would be a wl12xx_command, where the
		 * parameter array contains the actual answer.
		 */
		wl12xx_spi_mem_read(wl, wl->cmd_box_addr, buf, buf_len);

		cmd_answer = buf;

		if (cmd_answer->header.status != CMD_STATUS_SUCCESS)
			wl12xx_error("TEST command answer error: %d",
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
int wl12xx_cmd_interrogate(struct wl12xx *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd interrogate");

	acx->id = id;

	/* payload length, does not include any headers */
	acx->len = len - sizeof(*acx);

	ret = wl12xx_cmd_send(wl, CMD_INTERROGATE, acx, sizeof(*acx));
	if (ret < 0) {
		wl12xx_error("INTERROGATE command failed");
		goto out;
	}

	/* the interrogate command got in, we can read the answer */
	wl12xx_spi_mem_read(wl, wl->cmd_box_addr, buf, len);

	acx = buf;
	if (acx->cmd.status != CMD_STATUS_SUCCESS)
		wl12xx_error("INTERROGATE command error: %d",
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
int wl12xx_cmd_configure(struct wl12xx *wl, u16 id, void *buf, size_t len)
{
	struct acx_header *acx = buf;
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd configure");

	acx->id = id;

	/* payload length, does not include any headers */
	acx->len = len - sizeof(*acx);

	ret = wl12xx_cmd_send(wl, CMD_CONFIGURE, acx, len);
	if (ret < 0) {
		wl12xx_warning("CONFIGURE command NOK");
		return ret;
	}

	return 0;
}

int wl12xx_cmd_vbm(struct wl12xx *wl, u8 identity,
		   void *bitmap, u16 bitmap_len, u8 bitmap_control)
{
	struct wl12xx_cmd_vbm_update *vbm;
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd vbm");

	vbm = kzalloc(sizeof(*vbm), GFP_KERNEL);
	if (!vbm) {
		ret = -ENOMEM;
		goto out;
	}

	/* Count and period will be filled by the target */
	vbm->tim.bitmap_ctrl = bitmap_control;
	if (bitmap_len > PARTIAL_VBM_MAX) {
		wl12xx_warning("cmd vbm len is %d B, truncating to %d",
			       bitmap_len, PARTIAL_VBM_MAX);
		bitmap_len = PARTIAL_VBM_MAX;
	}
	memcpy(vbm->tim.pvb_field, bitmap, bitmap_len);
	vbm->tim.identity = identity;
	vbm->tim.length = bitmap_len + 3;

	vbm->len = cpu_to_le16(bitmap_len + 5);

	ret = wl12xx_cmd_send(wl, CMD_VBM, vbm, sizeof(*vbm));
	if (ret < 0) {
		wl12xx_error("VBM command failed");
		goto out;
	}

out:
	kfree(vbm);
	return 0;
}

int wl12xx_cmd_data_path(struct wl12xx *wl, u8 channel, bool enable)
{
	struct cmd_enabledisable_path *cmd;
	int ret;
	u16 cmd_rx, cmd_tx;

	wl12xx_debug(DEBUG_CMD, "cmd data path");

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

	ret = wl12xx_cmd_send(wl, cmd_rx, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl12xx_error("rx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
		goto out;
	}

	wl12xx_debug(DEBUG_BOOT, "rx %s cmd channel %d",
		     enable ? "start" : "stop", channel);

	ret = wl12xx_cmd_send(wl, cmd_tx, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl12xx_error("tx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
		return ret;
	}

	wl12xx_debug(DEBUG_BOOT, "tx %s cmd channel %d",
		     enable ? "start" : "stop", channel);

out:
	kfree(cmd);
	return ret;
}

int wl1251_cmd_join(struct wl12xx *wl, u8 bss_type, u8 dtim_interval,
		    u16 beacon_interval, u8 wait)
{
	unsigned long timeout;
	struct cmd_join *join;
	int ret, i;
	u8 *bssid;

	join = kzalloc(sizeof(*join), GFP_KERNEL);
	if (!join) {
		ret = -ENOMEM;
		goto out;
	}

	/* FIXME: this should be in main.c */
	ret = wl12xx_acx_frame_rates(wl, DEFAULT_HW_GEN_TX_RATE,
				     DEFAULT_HW_GEN_MODULATION_TYPE,
				     wl->tx_mgmt_frm_rate,
				     wl->tx_mgmt_frm_mod);
	if (ret < 0)
		goto out;

	wl12xx_debug(DEBUG_CMD, "cmd join");

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
	join->ctrl = JOIN_CMD_CTRL_TX_FLUSH;

	ret = wl12xx_cmd_send(wl, CMD_START_JOIN, join, sizeof(*join));
	if (ret < 0) {
		wl12xx_error("failed to initiate cmd join");
		goto out;
	}

	timeout = msecs_to_jiffies(JOIN_TIMEOUT);

	/*
	 * ugly hack: we should wait for JOIN_EVENT_COMPLETE_ID but to
	 * simplify locking we just sleep instead, for now
	 */
	if (wait)
		msleep(10);

out:
	kfree(join);
	return ret;
}

int wl12xx_cmd_ps_mode(struct wl12xx *wl, u8 ps_mode)
{
	struct wl12xx_cmd_ps_params *ps_params = NULL;
	int ret = 0;

	/* FIXME: this should be in ps.c */
	ret = wl12xx_acx_wake_up_conditions(wl, WAKE_UP_EVENT_DTIM_BITMAP,
					    wl->listen_int);
	if (ret < 0) {
		wl12xx_error("couldn't set wake up conditions");
		goto out;
	}

	wl12xx_debug(DEBUG_CMD, "cmd set ps mode");

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

	ret = wl12xx_cmd_send(wl, CMD_SET_PS_MODE, ps_params,
			      sizeof(*ps_params));
	if (ret < 0) {
		wl12xx_error("cmd set_ps_mode failed");
		goto out;
	}

out:
	kfree(ps_params);
	return ret;
}

int wl12xx_cmd_read_memory(struct wl12xx *wl, u32 addr, void *answer,
			   size_t len)
{
	struct cmd_read_write_memory *cmd;
	int ret = 0;

	wl12xx_debug(DEBUG_CMD, "cmd read memory");

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	WARN_ON(len > MAX_READ_SIZE);
	len = min_t(size_t, len, MAX_READ_SIZE);

	cmd->addr = addr;
	cmd->size = len;

	ret = wl12xx_cmd_send(wl, CMD_READ_MEMORY, cmd, sizeof(*cmd));
	if (ret < 0) {
		wl12xx_error("read memory command failed: %d", ret);
		goto out;
	}

	/* the read command got in, we can now read the answer */
	wl12xx_spi_mem_read(wl, wl->cmd_box_addr, cmd, sizeof(*cmd));

	if (cmd->header.status != CMD_STATUS_SUCCESS)
		wl12xx_error("error in read command result: %d",
			     cmd->header.status);

	memcpy(answer, cmd->value, len);

out:
	kfree(cmd);
	return ret;
}

int wl12xx_cmd_template_set(struct wl12xx *wl, u16 cmd_id,
			    void *buf, size_t buf_len)
{
	struct wl12xx_cmd_packet_template *cmd;
	size_t cmd_len;
	int ret = 0;

	wl12xx_debug(DEBUG_CMD, "cmd template %d", cmd_id);

	WARN_ON(buf_len > WL12XX_MAX_TEMPLATE_SIZE);
	buf_len = min_t(size_t, buf_len, WL12XX_MAX_TEMPLATE_SIZE);
	cmd_len = ALIGN(sizeof(*cmd) + buf_len, 4);

	cmd = kzalloc(cmd_len, GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	cmd->size = cpu_to_le16(buf_len);

	if (buf)
		memcpy(cmd->data, buf, buf_len);

	ret = wl12xx_cmd_send(wl, cmd_id, cmd, cmd_len);
	if (ret < 0) {
		wl12xx_warning("cmd set_template failed: %d", ret);
		goto out;
	}

out:
	kfree(cmd);
	return ret;
}

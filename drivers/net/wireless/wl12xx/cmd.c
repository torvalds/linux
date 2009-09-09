#include "cmd.h"

#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>

#include "wl12xx.h"
#include "wl12xx_80211.h"
#include "reg.h"
#include "spi.h"
#include "ps.h"

int wl12xx_cmd_send(struct wl12xx *wl, u16 type, void *buf, size_t buf_len)
{
	struct wl12xx_command cmd;
	unsigned long timeout;
	size_t cmd_len;
	u32 intr;
	int ret = 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.id = type;
	cmd.status = 0;
	memcpy(cmd.parameters, buf, buf_len);
	cmd_len = ALIGN(buf_len, 4) + CMDMBOX_HEADER_LEN;

	wl12xx_ps_elp_wakeup(wl);

	wl12xx_spi_mem_write(wl, wl->cmd_box_addr, &cmd, cmd_len);

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
	wl12xx_ps_elp_sleep(wl);

	return ret;
}

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

		wl12xx_ps_elp_wakeup(wl);

		wl12xx_spi_mem_read(wl, wl->cmd_box_addr, buf, buf_len);

		wl12xx_ps_elp_sleep(wl);

		cmd_answer = buf;
		if (cmd_answer->status != CMD_STATUS_SUCCESS)
			wl12xx_error("TEST command answer error: %d",
				     cmd_answer->status);
	}

	return 0;
}


int wl12xx_cmd_interrogate(struct wl12xx *wl, u16 ie_id, u16 ie_len,
			   void *answer)
{
	struct wl12xx_command *cmd;
	struct acx_header header;
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd interrogate");

	header.id = ie_id;
	header.len = ie_len - sizeof(header);

	ret = wl12xx_cmd_send(wl, CMD_INTERROGATE, &header, sizeof(header));
	if (ret < 0) {
		wl12xx_error("INTERROGATE command failed");
		return ret;
	}

	wl12xx_ps_elp_wakeup(wl);

	/* the interrogate command got in, we can read the answer */
	wl12xx_spi_mem_read(wl, wl->cmd_box_addr, answer,
			    CMDMBOX_HEADER_LEN + ie_len);

	wl12xx_ps_elp_sleep(wl);

	cmd = answer;
	if (cmd->status != CMD_STATUS_SUCCESS)
		wl12xx_error("INTERROGATE command error: %d",
			     cmd->status);

	return 0;

}

int wl12xx_cmd_configure(struct wl12xx *wl, void *ie, int ie_len)
{
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd configure");

	ret = wl12xx_cmd_send(wl, CMD_CONFIGURE, ie,
			      ie_len);
	if (ret < 0) {
		wl12xx_warning("CONFIGURE command NOK");
		return ret;
	}

	return 0;

}

int wl12xx_cmd_vbm(struct wl12xx *wl, u8 identity,
		   void *bitmap, u16 bitmap_len, u8 bitmap_control)
{
	struct vbm_update_request vbm;
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd vbm");

	/* Count and period will be filled by the target */
	vbm.tim.bitmap_ctrl = bitmap_control;
	if (bitmap_len > PARTIAL_VBM_MAX) {
		wl12xx_warning("cmd vbm len is %d B, truncating to %d",
			       bitmap_len, PARTIAL_VBM_MAX);
		bitmap_len = PARTIAL_VBM_MAX;
	}
	memcpy(vbm.tim.pvb_field, bitmap, bitmap_len);
	vbm.tim.identity = identity;
	vbm.tim.length = bitmap_len + 3;

	vbm.len = cpu_to_le16(bitmap_len + 5);

	ret = wl12xx_cmd_send(wl, CMD_VBM, &vbm, sizeof(vbm));
	if (ret < 0) {
		wl12xx_error("VBM command failed");
		return ret;
	}

	return 0;
}

int wl12xx_cmd_data_path(struct wl12xx *wl, u8 channel, u8 enable)
{
	int ret;
	u16 cmd_rx, cmd_tx;

	wl12xx_debug(DEBUG_CMD, "cmd data path");

	if (enable) {
		cmd_rx = CMD_ENABLE_RX;
		cmd_tx = CMD_ENABLE_TX;
	} else {
		cmd_rx = CMD_DISABLE_RX;
		cmd_tx = CMD_DISABLE_TX;
	}

	ret = wl12xx_cmd_send(wl, cmd_rx, &channel, sizeof(channel));
	if (ret < 0) {
		wl12xx_error("rx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
		return ret;
	}

	wl12xx_debug(DEBUG_BOOT, "rx %s cmd channel %d",
		     enable ? "start" : "stop", channel);

	ret = wl12xx_cmd_send(wl, cmd_tx, &channel, sizeof(channel));
	if (ret < 0) {
		wl12xx_error("tx %s cmd for channel %d failed",
			     enable ? "start" : "stop", channel);
		return ret;
	}

	wl12xx_debug(DEBUG_BOOT, "tx %s cmd channel %d",
		     enable ? "start" : "stop", channel);

	return 0;
}

int wl12xx_cmd_join(struct wl12xx *wl, u8 bss_type, u8 dtim_interval,
		    u16 beacon_interval, u8 wait)
{
	unsigned long timeout;
	struct cmd_join join = {};
	int ret, i;
	u8 *bssid;

	/* FIXME: this should be in main.c */
	ret = wl12xx_acx_frame_rates(wl, DEFAULT_HW_GEN_TX_RATE,
				     DEFAULT_HW_GEN_MODULATION_TYPE,
				     wl->tx_mgmt_frm_rate,
				     wl->tx_mgmt_frm_mod);
	if (ret < 0)
		return ret;

	wl12xx_debug(DEBUG_CMD, "cmd join");

	/* Reverse order BSSID */
	bssid = (u8 *)&join.bssid_lsb;
	for (i = 0; i < ETH_ALEN; i++)
		bssid[i] = wl->bssid[ETH_ALEN - i - 1];

	join.rx_config_options = wl->rx_config;
	join.rx_filter_options = wl->rx_filter;

	join.basic_rate_set = RATE_MASK_1MBPS | RATE_MASK_2MBPS |
		RATE_MASK_5_5MBPS | RATE_MASK_11MBPS;

	join.beacon_interval = beacon_interval;
	join.dtim_interval = dtim_interval;
	join.bss_type = bss_type;
	join.channel = wl->channel;
	join.ctrl = JOIN_CMD_CTRL_TX_FLUSH;

	ret = wl12xx_cmd_send(wl, CMD_START_JOIN, &join, sizeof(join));
	if (ret < 0) {
		wl12xx_error("failed to initiate cmd join");
		return ret;
	}

	timeout = msecs_to_jiffies(JOIN_TIMEOUT);

	/*
	 * ugly hack: we should wait for JOIN_EVENT_COMPLETE_ID but to
	 * simplify locking we just sleep instead, for now
	 */
	if (wait)
		msleep(10);

	return 0;
}

int wl12xx_cmd_ps_mode(struct wl12xx *wl, u8 ps_mode)
{
	int ret;
	struct acx_ps_params ps_params;

	/* FIXME: this should be in ps.c */
	ret = wl12xx_acx_wake_up_conditions(wl, wl->listen_int);
	if (ret < 0) {
		wl12xx_error("Couldnt set wake up conditions");
		return ret;
	}

	wl12xx_debug(DEBUG_CMD, "cmd set ps mode");

	ps_params.ps_mode = ps_mode;
	ps_params.send_null_data = 1;
	ps_params.retries = 5;
	ps_params.hang_over_period = 128;
	ps_params.null_data_rate = 1; /* 1 Mbps */

	ret = wl12xx_cmd_send(wl, CMD_SET_PS_MODE, &ps_params,
			      sizeof(ps_params));
	if (ret < 0) {
		wl12xx_error("cmd set_ps_mode failed");
		return ret;
	}

	return 0;
}

int wl12xx_cmd_read_memory(struct wl12xx *wl, u32 addr, u32 len, void *answer)
{
	struct cmd_read_write_memory mem_cmd, *mem_answer;
	struct wl12xx_command cmd;
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd read memory");

	memset(&mem_cmd, 0, sizeof(mem_cmd));
	mem_cmd.addr = addr;
	mem_cmd.size = len;

	ret = wl12xx_cmd_send(wl, CMD_READ_MEMORY, &mem_cmd, sizeof(mem_cmd));
	if (ret < 0) {
		wl12xx_error("read memory command failed: %d", ret);
		return ret;
	}

	/* the read command got in, we can now read the answer */
	wl12xx_spi_mem_read(wl, wl->cmd_box_addr, &cmd,
			    CMDMBOX_HEADER_LEN + sizeof(mem_cmd));

	if (cmd.status != CMD_STATUS_SUCCESS)
		wl12xx_error("error in read command result: %d", cmd.status);

	mem_answer = (struct cmd_read_write_memory *) cmd.parameters;
	memcpy(answer, mem_answer->value, len);

	return 0;
}

int wl12xx_cmd_template_set(struct wl12xx *wl, u16 cmd_id,
			    void *buf, size_t buf_len)
{
	struct wl12xx_cmd_packet_template template;
	int ret;

	wl12xx_debug(DEBUG_CMD, "cmd template %d", cmd_id);

	memset(&template, 0, sizeof(template));

	WARN_ON(buf_len > WL12XX_MAX_TEMPLATE_SIZE);
	buf_len = min_t(size_t, buf_len, WL12XX_MAX_TEMPLATE_SIZE);
	template.size = cpu_to_le16(buf_len);

	if (buf)
		memcpy(template.template, buf, buf_len);

	ret = wl12xx_cmd_send(wl, cmd_id, &template,
			      sizeof(template.size) + buf_len);
	if (ret < 0) {
		wl12xx_warning("cmd set_template failed: %d", ret);
		return ret;
	}

	return 0;
}

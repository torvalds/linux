// SPDX-License-Identifier: GPL-2.0+
/*
 * SoundWire AMD Manager driver
 *
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/pm_runtime.h>
#include <linux/wait.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "bus.h"
#include "amd_manager.h"

#define DRV_NAME "amd_sdw_manager"

#define to_amd_sdw(b)	container_of(b, struct amd_sdw_manager, bus)

static void amd_enable_sdw_pads(struct amd_sdw_manager *amd_manager)
{
	u32 sw_pad_pulldown_val;
	u32 val;

	mutex_lock(amd_manager->acp_sdw_lock);
	val = readl(amd_manager->acp_mmio + ACP_SW_PAD_KEEPER_EN);
	val |= amd_manager->reg_mask->sw_pad_enable_mask;
	writel(val, amd_manager->acp_mmio + ACP_SW_PAD_KEEPER_EN);
	usleep_range(1000, 1500);

	sw_pad_pulldown_val = readl(amd_manager->acp_mmio + ACP_PAD_PULLDOWN_CTRL);
	sw_pad_pulldown_val &= amd_manager->reg_mask->sw_pad_pulldown_mask;
	writel(sw_pad_pulldown_val, amd_manager->acp_mmio + ACP_PAD_PULLDOWN_CTRL);
	mutex_unlock(amd_manager->acp_sdw_lock);
}

static int amd_init_sdw_manager(struct amd_sdw_manager *amd_manager)
{
	u32 val;
	int ret;

	writel(AMD_SDW_ENABLE, amd_manager->mmio + ACP_SW_EN);
	ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_EN_STATUS, val, val, ACP_DELAY_US,
				 AMD_SDW_TIMEOUT);
	if (ret)
		return ret;

	/* SoundWire manager bus reset */
	writel(AMD_SDW_BUS_RESET_REQ, amd_manager->mmio + ACP_SW_BUS_RESET_CTRL);
	ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_BUS_RESET_CTRL, val,
				 (val & AMD_SDW_BUS_RESET_DONE), ACP_DELAY_US, AMD_SDW_TIMEOUT);
	if (ret)
		return ret;

	writel(AMD_SDW_BUS_RESET_CLEAR_REQ, amd_manager->mmio + ACP_SW_BUS_RESET_CTRL);
	ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_BUS_RESET_CTRL, val, !val,
				 ACP_DELAY_US, AMD_SDW_TIMEOUT);
	if (ret) {
		dev_err(amd_manager->dev, "Failed to reset SoundWire manager instance%d\n",
			amd_manager->instance);
		return ret;
	}

	writel(AMD_SDW_DISABLE, amd_manager->mmio + ACP_SW_EN);
	return readl_poll_timeout(amd_manager->mmio + ACP_SW_EN_STATUS, val, !val, ACP_DELAY_US,
				  AMD_SDW_TIMEOUT);
}

static int amd_enable_sdw_manager(struct amd_sdw_manager *amd_manager)
{
	u32 val;

	writel(AMD_SDW_ENABLE, amd_manager->mmio + ACP_SW_EN);
	return readl_poll_timeout(amd_manager->mmio + ACP_SW_EN_STATUS, val, val, ACP_DELAY_US,
				  AMD_SDW_TIMEOUT);
}

static int amd_disable_sdw_manager(struct amd_sdw_manager *amd_manager)
{
	u32 val;

	writel(AMD_SDW_DISABLE, amd_manager->mmio + ACP_SW_EN);
	/*
	 * After invoking manager disable sequence, check whether
	 * manager has executed clock stop sequence. In this case,
	 * manager should ignore checking enable status register.
	 */
	val = readl(amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL);
	if (val)
		return 0;
	return readl_poll_timeout(amd_manager->mmio + ACP_SW_EN_STATUS, val, !val, ACP_DELAY_US,
				  AMD_SDW_TIMEOUT);
}

static void amd_enable_sdw_interrupts(struct amd_sdw_manager *amd_manager)
{
	struct sdw_manager_reg_mask *reg_mask = amd_manager->reg_mask;
	u32 val;

	mutex_lock(amd_manager->acp_sdw_lock);
	val = readl(amd_manager->acp_mmio + ACP_EXTERNAL_INTR_CNTL(amd_manager->instance));
	val |= reg_mask->acp_sdw_intr_mask;
	writel(val, amd_manager->acp_mmio + ACP_EXTERNAL_INTR_CNTL(amd_manager->instance));
	mutex_unlock(amd_manager->acp_sdw_lock);

	writel(AMD_SDW_IRQ_MASK_0TO7, amd_manager->mmio +
		       ACP_SW_STATE_CHANGE_STATUS_MASK_0TO7);
	writel(AMD_SDW_IRQ_MASK_8TO11, amd_manager->mmio +
		       ACP_SW_STATE_CHANGE_STATUS_MASK_8TO11);
	writel(AMD_SDW_IRQ_ERROR_MASK, amd_manager->mmio + ACP_SW_ERROR_INTR_MASK);
}

static void amd_disable_sdw_interrupts(struct amd_sdw_manager *amd_manager)
{
	struct sdw_manager_reg_mask *reg_mask = amd_manager->reg_mask;
	u32 val;

	mutex_lock(amd_manager->acp_sdw_lock);
	val = readl(amd_manager->acp_mmio + ACP_EXTERNAL_INTR_CNTL(amd_manager->instance));
	val &= ~reg_mask->acp_sdw_intr_mask;
	writel(val, amd_manager->acp_mmio + ACP_EXTERNAL_INTR_CNTL(amd_manager->instance));
	mutex_unlock(amd_manager->acp_sdw_lock);

	writel(0x00, amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_MASK_0TO7);
	writel(0x00, amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_MASK_8TO11);
	writel(0x00, amd_manager->mmio + ACP_SW_ERROR_INTR_MASK);
}

static int amd_deinit_sdw_manager(struct amd_sdw_manager *amd_manager)
{
	amd_disable_sdw_interrupts(amd_manager);
	return amd_disable_sdw_manager(amd_manager);
}

static void amd_sdw_set_frameshape(struct amd_sdw_manager *amd_manager)
{
	u32 frame_size;

	frame_size = (amd_manager->rows_index << 3) | amd_manager->cols_index;
	writel(frame_size, amd_manager->mmio + ACP_SW_FRAMESIZE);
}

static void amd_sdw_ctl_word_prep(u32 *lower_word, u32 *upper_word, struct sdw_msg *msg,
				  int cmd_offset)
{
	u32 upper_data;
	u32 lower_data = 0;
	u16 addr;
	u8 upper_addr, lower_addr;
	u8 data = 0;

	addr = msg->addr + cmd_offset;
	upper_addr = (addr & 0xFF00) >> 8;
	lower_addr = addr & 0xFF;

	if (msg->flags == SDW_MSG_FLAG_WRITE)
		data = msg->buf[cmd_offset];

	upper_data = FIELD_PREP(AMD_SDW_MCP_CMD_DEV_ADDR, msg->dev_num);
	upper_data |= FIELD_PREP(AMD_SDW_MCP_CMD_COMMAND, msg->flags + 2);
	upper_data |= FIELD_PREP(AMD_SDW_MCP_CMD_REG_ADDR_HIGH, upper_addr);
	lower_data |= FIELD_PREP(AMD_SDW_MCP_CMD_REG_ADDR_LOW, lower_addr);
	lower_data |= FIELD_PREP(AMD_SDW_MCP_CMD_REG_DATA, data);

	*upper_word = upper_data;
	*lower_word = lower_data;
}

static u64 amd_sdw_send_cmd_get_resp(struct amd_sdw_manager *amd_manager, u32 lower_data,
				     u32 upper_data)
{
	u64 resp;
	u32 lower_resp, upper_resp;
	u32 sts;
	int ret;

	ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_IMM_CMD_STS, sts,
				 !(sts & AMD_SDW_IMM_CMD_BUSY), ACP_DELAY_US, AMD_SDW_TIMEOUT);
	if (ret) {
		dev_err(amd_manager->dev, "SDW%x previous cmd status clear failed\n",
			amd_manager->instance);
		return ret;
	}

	if (sts & AMD_SDW_IMM_RES_VALID) {
		dev_err(amd_manager->dev, "SDW%x manager is in bad state\n", amd_manager->instance);
		writel(0x00, amd_manager->mmio + ACP_SW_IMM_CMD_STS);
	}
	writel(upper_data, amd_manager->mmio + ACP_SW_IMM_CMD_UPPER_WORD);
	writel(lower_data, amd_manager->mmio + ACP_SW_IMM_CMD_LOWER_QWORD);

	ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_IMM_CMD_STS, sts,
				 (sts & AMD_SDW_IMM_RES_VALID), ACP_DELAY_US, AMD_SDW_TIMEOUT);
	if (ret) {
		dev_err(amd_manager->dev, "SDW%x cmd response timeout occurred\n",
			amd_manager->instance);
		return ret;
	}
	upper_resp = readl(amd_manager->mmio + ACP_SW_IMM_RESP_UPPER_WORD);
	lower_resp = readl(amd_manager->mmio + ACP_SW_IMM_RESP_LOWER_QWORD);

	writel(AMD_SDW_IMM_RES_VALID, amd_manager->mmio + ACP_SW_IMM_CMD_STS);
	ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_IMM_CMD_STS, sts,
				 !(sts & AMD_SDW_IMM_RES_VALID), ACP_DELAY_US, AMD_SDW_TIMEOUT);
	if (ret) {
		dev_err(amd_manager->dev, "SDW%x cmd status retry failed\n",
			amd_manager->instance);
		return ret;
	}
	resp = upper_resp;
	resp = (resp << 32) | lower_resp;
	return resp;
}

static enum sdw_command_response
amd_program_scp_addr(struct amd_sdw_manager *amd_manager, struct sdw_msg *msg)
{
	struct sdw_msg scp_msg = {0};
	u64 response_buf[2] = {0};
	u32 upper_data = 0, lower_data = 0;
	int index;

	scp_msg.dev_num = msg->dev_num;
	scp_msg.addr = SDW_SCP_ADDRPAGE1;
	scp_msg.buf = &msg->addr_page1;
	scp_msg.flags = SDW_MSG_FLAG_WRITE;
	amd_sdw_ctl_word_prep(&lower_data, &upper_data, &scp_msg, 0);
	response_buf[0] = amd_sdw_send_cmd_get_resp(amd_manager, lower_data, upper_data);
	scp_msg.addr = SDW_SCP_ADDRPAGE2;
	scp_msg.buf = &msg->addr_page2;
	amd_sdw_ctl_word_prep(&lower_data, &upper_data, &scp_msg, 0);
	response_buf[1] = amd_sdw_send_cmd_get_resp(amd_manager, lower_data, upper_data);

	for (index = 0; index < 2; index++) {
		if (response_buf[index] == -ETIMEDOUT) {
			dev_err_ratelimited(amd_manager->dev,
					    "SCP_addrpage command timeout for Slave %d\n",
					    msg->dev_num);
			return SDW_CMD_TIMEOUT;
		} else if (!(response_buf[index] & AMD_SDW_MCP_RESP_ACK)) {
			if (response_buf[index] & AMD_SDW_MCP_RESP_NACK) {
				dev_err_ratelimited(amd_manager->dev,
						    "SCP_addrpage NACKed for Slave %d\n",
						    msg->dev_num);
				return SDW_CMD_FAIL;
			}
			dev_dbg_ratelimited(amd_manager->dev, "SCP_addrpage ignored for Slave %d\n",
					    msg->dev_num);
			return SDW_CMD_IGNORED;
		}
	}
	return SDW_CMD_OK;
}

static int amd_prep_msg(struct amd_sdw_manager *amd_manager, struct sdw_msg *msg)
{
	int ret;

	if (msg->page) {
		ret = amd_program_scp_addr(amd_manager, msg);
		if (ret) {
			msg->len = 0;
			return ret;
		}
	}
	switch (msg->flags) {
	case SDW_MSG_FLAG_READ:
	case SDW_MSG_FLAG_WRITE:
		break;
	default:
		dev_err(amd_manager->dev, "Invalid msg cmd: %d\n", msg->flags);
		return -EINVAL;
	}
	return 0;
}

static enum sdw_command_response amd_sdw_fill_msg_resp(struct amd_sdw_manager *amd_manager,
						       struct sdw_msg *msg, u64 response,
						       int offset)
{
	if (response & AMD_SDW_MCP_RESP_ACK) {
		if (msg->flags == SDW_MSG_FLAG_READ)
			msg->buf[offset] = FIELD_GET(AMD_SDW_MCP_RESP_RDATA, response);
	} else {
		if (response == -ETIMEDOUT) {
			dev_err_ratelimited(amd_manager->dev, "command timeout for Slave %d\n",
					    msg->dev_num);
			return SDW_CMD_TIMEOUT;
		} else if (response & AMD_SDW_MCP_RESP_NACK) {
			dev_err_ratelimited(amd_manager->dev,
					    "command response NACK received for Slave %d\n",
					    msg->dev_num);
			return SDW_CMD_FAIL;
		}
		dev_err_ratelimited(amd_manager->dev, "command is ignored for Slave %d\n",
				    msg->dev_num);
		return SDW_CMD_IGNORED;
	}
	return SDW_CMD_OK;
}

static unsigned int _amd_sdw_xfer_msg(struct amd_sdw_manager *amd_manager, struct sdw_msg *msg,
				      int cmd_offset)
{
	u64 response;
	u32 upper_data = 0, lower_data = 0;

	amd_sdw_ctl_word_prep(&lower_data, &upper_data, msg, cmd_offset);
	response = amd_sdw_send_cmd_get_resp(amd_manager, lower_data, upper_data);
	return amd_sdw_fill_msg_resp(amd_manager, msg, response, cmd_offset);
}

static enum sdw_command_response amd_sdw_xfer_msg(struct sdw_bus *bus, struct sdw_msg *msg)
{
	struct amd_sdw_manager *amd_manager = to_amd_sdw(bus);
	int ret, i;

	ret = amd_prep_msg(amd_manager, msg);
	if (ret)
		return SDW_CMD_FAIL_OTHER;
	for (i = 0; i < msg->len; i++) {
		ret = _amd_sdw_xfer_msg(amd_manager, msg, i);
		if (ret)
			return ret;
	}
	return SDW_CMD_OK;
}

static void amd_sdw_fill_slave_status(struct amd_sdw_manager *amd_manager, u16 index, u32 status)
{
	switch (status) {
	case SDW_SLAVE_ATTACHED:
	case SDW_SLAVE_UNATTACHED:
	case SDW_SLAVE_ALERT:
		amd_manager->status[index] = status;
		break;
	default:
		amd_manager->status[index] = SDW_SLAVE_RESERVED;
		break;
	}
}

static void amd_sdw_process_ping_status(u64 response, struct amd_sdw_manager *amd_manager)
{
	u64 slave_stat;
	u32 val;
	u16 dev_index;

	/* slave status response */
	slave_stat = FIELD_GET(AMD_SDW_MCP_SLAVE_STAT_0_3, response);
	slave_stat |= FIELD_GET(AMD_SDW_MCP_SLAVE_STAT_4_11, response) << 8;
	dev_dbg(amd_manager->dev, "slave_stat:0x%llx\n", slave_stat);
	for (dev_index = 0; dev_index <= SDW_MAX_DEVICES; ++dev_index) {
		val = (slave_stat >> (dev_index * 2)) & AMD_SDW_MCP_SLAVE_STATUS_MASK;
		dev_dbg(amd_manager->dev, "val:0x%x\n", val);
		amd_sdw_fill_slave_status(amd_manager, dev_index, val);
	}
}

static void amd_sdw_read_and_process_ping_status(struct amd_sdw_manager *amd_manager)
{
	u64 response;

	mutex_lock(&amd_manager->bus.msg_lock);
	response = amd_sdw_send_cmd_get_resp(amd_manager, 0, 0);
	mutex_unlock(&amd_manager->bus.msg_lock);
	amd_sdw_process_ping_status(response, amd_manager);
}

static u32 amd_sdw_read_ping_status(struct sdw_bus *bus)
{
	struct amd_sdw_manager *amd_manager = to_amd_sdw(bus);
	u64 response;
	u32 slave_stat;

	response = amd_sdw_send_cmd_get_resp(amd_manager, 0, 0);
	/* slave status from ping response */
	slave_stat = FIELD_GET(AMD_SDW_MCP_SLAVE_STAT_0_3, response);
	slave_stat |= FIELD_GET(AMD_SDW_MCP_SLAVE_STAT_4_11, response) << 8;
	dev_dbg(amd_manager->dev, "slave_stat:0x%x\n", slave_stat);
	return slave_stat;
}

static int amd_sdw_compute_params(struct sdw_bus *bus)
{
	struct sdw_transport_data t_data = {0};
	struct sdw_master_runtime *m_rt;
	struct sdw_port_runtime *p_rt;
	struct sdw_bus_params *b_params = &bus->params;
	int port_bo, hstart, hstop, sample_int;
	unsigned int rate, bps;

	port_bo = 0;
	hstart = 1;
	hstop = bus->params.col - 1;
	t_data.hstop = hstop;
	t_data.hstart = hstart;

	list_for_each_entry(m_rt, &bus->m_rt_list, bus_node) {
		rate = m_rt->stream->params.rate;
		bps = m_rt->stream->params.bps;
		sample_int = (bus->params.curr_dr_freq / rate);
		list_for_each_entry(p_rt, &m_rt->port_list, port_node) {
			port_bo = (p_rt->num * 64) + 1;
			dev_dbg(bus->dev, "p_rt->num=%d hstart=%d hstop=%d port_bo=%d\n",
				p_rt->num, hstart, hstop, port_bo);
			sdw_fill_xport_params(&p_rt->transport_params, p_rt->num,
					      false, SDW_BLK_GRP_CNT_1, sample_int,
					      port_bo, port_bo >> 8, hstart, hstop,
					      SDW_BLK_PKG_PER_PORT, 0x0);

			sdw_fill_port_params(&p_rt->port_params,
					     p_rt->num, bps,
					     SDW_PORT_FLOW_MODE_ISOCH,
					     b_params->m_data_mode);
			t_data.hstart = hstart;
			t_data.hstop = hstop;
			t_data.block_offset = port_bo;
			t_data.sub_block_offset = 0;
		}
		sdw_compute_slave_ports(m_rt, &t_data);
	}
	return 0;
}

static int amd_sdw_port_params(struct sdw_bus *bus, struct sdw_port_params *p_params,
			       unsigned int bank)
{
	struct amd_sdw_manager *amd_manager = to_amd_sdw(bus);
	u32 frame_fmt_reg, dpn_frame_fmt;

	dev_dbg(amd_manager->dev, "p_params->num:0x%x\n", p_params->num);
	switch (amd_manager->instance) {
	case ACP_SDW0:
		frame_fmt_reg = sdw0_manager_dp_reg[p_params->num].frame_fmt_reg;
		break;
	case ACP_SDW1:
		frame_fmt_reg = sdw1_manager_dp_reg[p_params->num].frame_fmt_reg;
		break;
	default:
		return -EINVAL;
	}

	dpn_frame_fmt = readl(amd_manager->mmio + frame_fmt_reg);
	u32p_replace_bits(&dpn_frame_fmt, p_params->flow_mode, AMD_DPN_FRAME_FMT_PFM);
	u32p_replace_bits(&dpn_frame_fmt, p_params->data_mode, AMD_DPN_FRAME_FMT_PDM);
	u32p_replace_bits(&dpn_frame_fmt, p_params->bps - 1, AMD_DPN_FRAME_FMT_WORD_LEN);
	writel(dpn_frame_fmt, amd_manager->mmio + frame_fmt_reg);
	return 0;
}

static int amd_sdw_transport_params(struct sdw_bus *bus,
				    struct sdw_transport_params *params,
				    enum sdw_reg_bank bank)
{
	struct amd_sdw_manager *amd_manager = to_amd_sdw(bus);
	u32 dpn_frame_fmt;
	u32 dpn_sampleinterval;
	u32 dpn_hctrl;
	u32 dpn_offsetctrl;
	u32 dpn_lanectrl;
	u32 frame_fmt_reg, sample_int_reg, hctrl_dp0_reg;
	u32 offset_reg, lane_ctrl_ch_en_reg;

	switch (amd_manager->instance) {
	case ACP_SDW0:
		frame_fmt_reg = sdw0_manager_dp_reg[params->port_num].frame_fmt_reg;
		sample_int_reg = sdw0_manager_dp_reg[params->port_num].sample_int_reg;
		hctrl_dp0_reg = sdw0_manager_dp_reg[params->port_num].hctrl_dp0_reg;
		offset_reg = sdw0_manager_dp_reg[params->port_num].offset_reg;
		lane_ctrl_ch_en_reg = sdw0_manager_dp_reg[params->port_num].lane_ctrl_ch_en_reg;
		break;
	case ACP_SDW1:
		frame_fmt_reg = sdw1_manager_dp_reg[params->port_num].frame_fmt_reg;
		sample_int_reg = sdw1_manager_dp_reg[params->port_num].sample_int_reg;
		hctrl_dp0_reg = sdw1_manager_dp_reg[params->port_num].hctrl_dp0_reg;
		offset_reg = sdw1_manager_dp_reg[params->port_num].offset_reg;
		lane_ctrl_ch_en_reg = sdw1_manager_dp_reg[params->port_num].lane_ctrl_ch_en_reg;
		break;
	default:
		return -EINVAL;
	}
	writel(AMD_SDW_SSP_COUNTER_VAL, amd_manager->mmio + ACP_SW_SSP_COUNTER);

	dpn_frame_fmt = readl(amd_manager->mmio + frame_fmt_reg);
	u32p_replace_bits(&dpn_frame_fmt, params->blk_pkg_mode, AMD_DPN_FRAME_FMT_BLK_PKG_MODE);
	u32p_replace_bits(&dpn_frame_fmt, params->blk_grp_ctrl, AMD_DPN_FRAME_FMT_BLK_GRP_CTRL);
	u32p_replace_bits(&dpn_frame_fmt, SDW_STREAM_PCM, AMD_DPN_FRAME_FMT_PCM_OR_PDM);
	writel(dpn_frame_fmt, amd_manager->mmio + frame_fmt_reg);

	dpn_sampleinterval = params->sample_interval - 1;
	writel(dpn_sampleinterval, amd_manager->mmio + sample_int_reg);

	dpn_hctrl = FIELD_PREP(AMD_DPN_HCTRL_HSTOP, params->hstop);
	dpn_hctrl |= FIELD_PREP(AMD_DPN_HCTRL_HSTART, params->hstart);
	writel(dpn_hctrl, amd_manager->mmio + hctrl_dp0_reg);

	dpn_offsetctrl = FIELD_PREP(AMD_DPN_OFFSET_CTRL_1, params->offset1);
	dpn_offsetctrl |= FIELD_PREP(AMD_DPN_OFFSET_CTRL_2, params->offset2);
	writel(dpn_offsetctrl, amd_manager->mmio + offset_reg);

	/*
	 * lane_ctrl_ch_en_reg will be used to program lane_ctrl and ch_mask
	 * parameters.
	 */
	dpn_lanectrl = readl(amd_manager->mmio + lane_ctrl_ch_en_reg);
	u32p_replace_bits(&dpn_lanectrl, params->lane_ctrl, AMD_DPN_CH_EN_LCTRL);
	writel(dpn_lanectrl, amd_manager->mmio + lane_ctrl_ch_en_reg);
	return 0;
}

static int amd_sdw_port_enable(struct sdw_bus *bus,
			       struct sdw_enable_ch *enable_ch,
			       unsigned int bank)
{
	struct amd_sdw_manager *amd_manager = to_amd_sdw(bus);
	u32 dpn_ch_enable;
	u32 lane_ctrl_ch_en_reg;

	switch (amd_manager->instance) {
	case ACP_SDW0:
		lane_ctrl_ch_en_reg = sdw0_manager_dp_reg[enable_ch->port_num].lane_ctrl_ch_en_reg;
		break;
	case ACP_SDW1:
		lane_ctrl_ch_en_reg = sdw1_manager_dp_reg[enable_ch->port_num].lane_ctrl_ch_en_reg;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * lane_ctrl_ch_en_reg will be used to program lane_ctrl and ch_mask
	 * parameters.
	 */
	dpn_ch_enable = readl(amd_manager->mmio + lane_ctrl_ch_en_reg);
	u32p_replace_bits(&dpn_ch_enable, enable_ch->ch_mask, AMD_DPN_CH_EN_CHMASK);
	if (enable_ch->enable)
		writel(dpn_ch_enable, amd_manager->mmio + lane_ctrl_ch_en_reg);
	else
		writel(0, amd_manager->mmio + lane_ctrl_ch_en_reg);
	return 0;
}

static int sdw_master_read_amd_prop(struct sdw_bus *bus)
{
	struct amd_sdw_manager *amd_manager = to_amd_sdw(bus);
	struct fwnode_handle *link;
	struct sdw_master_prop *prop;
	u32 quirk_mask = 0;
	u32 wake_en_mask = 0;
	u32 power_mode_mask = 0;
	char name[32];

	prop = &bus->prop;
	/* Find manager handle */
	snprintf(name, sizeof(name), "mipi-sdw-link-%d-subproperties", bus->link_id);
	link = device_get_named_child_node(bus->dev, name);
	if (!link) {
		dev_err(bus->dev, "Manager node %s not found\n", name);
		return -EIO;
	}
	fwnode_property_read_u32(link, "amd-sdw-enable", &quirk_mask);
	if (!(quirk_mask & AMD_SDW_QUIRK_MASK_BUS_ENABLE))
		prop->hw_disabled = true;
	prop->quirks = SDW_MASTER_QUIRKS_CLEAR_INITIAL_CLASH |
		       SDW_MASTER_QUIRKS_CLEAR_INITIAL_PARITY;

	fwnode_property_read_u32(link, "amd-sdw-wakeup-enable", &wake_en_mask);
	amd_manager->wake_en_mask = wake_en_mask;
	fwnode_property_read_u32(link, "amd-sdw-power-mode", &power_mode_mask);
	amd_manager->power_mode_mask = power_mode_mask;
	return 0;
}

static int amd_prop_read(struct sdw_bus *bus)
{
	sdw_master_read_prop(bus);
	sdw_master_read_amd_prop(bus);
	return 0;
}

static const struct sdw_master_port_ops amd_sdw_port_ops = {
	.dpn_set_port_params = amd_sdw_port_params,
	.dpn_set_port_transport_params = amd_sdw_transport_params,
	.dpn_port_enable_ch = amd_sdw_port_enable,
};

static const struct sdw_master_ops amd_sdw_ops = {
	.read_prop = amd_prop_read,
	.xfer_msg = amd_sdw_xfer_msg,
	.read_ping_status = amd_sdw_read_ping_status,
};

static int amd_sdw_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct amd_sdw_manager *amd_manager = snd_soc_dai_get_drvdata(dai);
	struct sdw_amd_dai_runtime *dai_runtime;
	struct sdw_stream_config sconfig;
	struct sdw_port_config *pconfig;
	int ch, dir;
	int ret;

	dai_runtime = amd_manager->dai_runtime_array[dai->id];
	if (!dai_runtime)
		return -EIO;

	ch = params_channels(params);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dir = SDW_DATA_DIR_RX;
	else
		dir = SDW_DATA_DIR_TX;
	dev_dbg(amd_manager->dev, "dir:%d dai->id:0x%x\n", dir, dai->id);

	sconfig.direction = dir;
	sconfig.ch_count = ch;
	sconfig.frame_rate = params_rate(params);
	sconfig.type = dai_runtime->stream_type;

	sconfig.bps = snd_pcm_format_width(params_format(params));

	/* Port configuration */
	pconfig = kzalloc(sizeof(*pconfig), GFP_KERNEL);
	if (!pconfig) {
		ret =  -ENOMEM;
		goto error;
	}

	pconfig->num = dai->id;
	pconfig->ch_mask = (1 << ch) - 1;
	ret = sdw_stream_add_master(&amd_manager->bus, &sconfig,
				    pconfig, 1, dai_runtime->stream);
	if (ret)
		dev_err(amd_manager->dev, "add manager to stream failed:%d\n", ret);

	kfree(pconfig);
error:
	return ret;
}

static int amd_sdw_hw_free(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct amd_sdw_manager *amd_manager = snd_soc_dai_get_drvdata(dai);
	struct sdw_amd_dai_runtime *dai_runtime;
	int ret;

	dai_runtime = amd_manager->dai_runtime_array[dai->id];
	if (!dai_runtime)
		return -EIO;

	ret = sdw_stream_remove_master(&amd_manager->bus, dai_runtime->stream);
	if (ret < 0)
		dev_err(dai->dev, "remove manager from stream %s failed: %d\n",
			dai_runtime->stream->name, ret);
	return ret;
}

static int amd_set_sdw_stream(struct snd_soc_dai *dai, void *stream, int direction)
{
	struct amd_sdw_manager *amd_manager = snd_soc_dai_get_drvdata(dai);
	struct sdw_amd_dai_runtime *dai_runtime;

	dai_runtime = amd_manager->dai_runtime_array[dai->id];
	if (stream) {
		/* first paranoia check */
		if (dai_runtime) {
			dev_err(dai->dev, "dai_runtime already allocated for dai %s\n",	dai->name);
			return -EINVAL;
		}

		/* allocate and set dai_runtime info */
		dai_runtime = kzalloc(sizeof(*dai_runtime), GFP_KERNEL);
		if (!dai_runtime)
			return -ENOMEM;

		dai_runtime->stream_type = SDW_STREAM_PCM;
		dai_runtime->bus = &amd_manager->bus;
		dai_runtime->stream = stream;
		amd_manager->dai_runtime_array[dai->id] = dai_runtime;
	} else {
		/* second paranoia check */
		if (!dai_runtime) {
			dev_err(dai->dev, "dai_runtime not allocated for dai %s\n", dai->name);
			return -EINVAL;
		}

		/* for NULL stream we release allocated dai_runtime */
		kfree(dai_runtime);
		amd_manager->dai_runtime_array[dai->id] = NULL;
	}
	return 0;
}

static int amd_pcm_set_sdw_stream(struct snd_soc_dai *dai, void *stream, int direction)
{
	return amd_set_sdw_stream(dai, stream, direction);
}

static void *amd_get_sdw_stream(struct snd_soc_dai *dai, int direction)
{
	struct amd_sdw_manager *amd_manager = snd_soc_dai_get_drvdata(dai);
	struct sdw_amd_dai_runtime *dai_runtime;

	dai_runtime = amd_manager->dai_runtime_array[dai->id];
	if (!dai_runtime)
		return ERR_PTR(-EINVAL);

	return dai_runtime->stream;
}

static const struct snd_soc_dai_ops amd_sdw_dai_ops = {
	.hw_params = amd_sdw_hw_params,
	.hw_free = amd_sdw_hw_free,
	.set_stream = amd_pcm_set_sdw_stream,
	.get_stream = amd_get_sdw_stream,
};

static const struct snd_soc_component_driver amd_sdw_dai_component = {
	.name = "soundwire",
};

static int amd_sdw_register_dais(struct amd_sdw_manager *amd_manager)
{
	struct sdw_amd_dai_runtime **dai_runtime_array;
	struct snd_soc_dai_driver *dais;
	struct snd_soc_pcm_stream *stream;
	struct device *dev;
	int i, num_dais;

	dev = amd_manager->dev;
	num_dais = amd_manager->num_dout_ports + amd_manager->num_din_ports;
	dais = devm_kcalloc(dev, num_dais, sizeof(*dais), GFP_KERNEL);
	if (!dais)
		return -ENOMEM;

	dai_runtime_array = devm_kcalloc(dev, num_dais,
					 sizeof(struct sdw_amd_dai_runtime *),
					 GFP_KERNEL);
	if (!dai_runtime_array)
		return -ENOMEM;
	amd_manager->dai_runtime_array = dai_runtime_array;
	for (i = 0; i < num_dais; i++) {
		dais[i].name = devm_kasprintf(dev, GFP_KERNEL, "SDW%d Pin%d", amd_manager->instance,
					      i);
		if (!dais[i].name)
			return -ENOMEM;
		if (i < amd_manager->num_dout_ports)
			stream = &dais[i].playback;
		else
			stream = &dais[i].capture;

		stream->channels_min = 2;
		stream->channels_max = 2;
		stream->rates = SNDRV_PCM_RATE_48000;
		stream->formats = SNDRV_PCM_FMTBIT_S16_LE;

		dais[i].ops = &amd_sdw_dai_ops;
		dais[i].id = i;
	}

	return devm_snd_soc_register_component(dev, &amd_sdw_dai_component,
					       dais, num_dais);
}

static void amd_sdw_update_slave_status_work(struct work_struct *work)
{
	struct amd_sdw_manager *amd_manager =
		container_of(work, struct amd_sdw_manager, amd_sdw_work);
	int retry_count = 0;

	if (amd_manager->status[0] == SDW_SLAVE_ATTACHED) {
		writel(0, amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_MASK_0TO7);
		writel(0, amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_MASK_8TO11);
	}

update_status:
	sdw_handle_slave_status(&amd_manager->bus, amd_manager->status);
	/*
	 * During the peripheral enumeration sequence, the SoundWire manager interrupts
	 * are masked. Once the device number programming is done for all peripherals,
	 * interrupts will be unmasked. Read the peripheral device status from ping command
	 * and process the response. This sequence will ensure all peripheral devices enumerated
	 * and initialized properly.
	 */
	if (amd_manager->status[0] == SDW_SLAVE_ATTACHED) {
		if (retry_count++ < SDW_MAX_DEVICES) {
			writel(AMD_SDW_IRQ_MASK_0TO7, amd_manager->mmio +
			       ACP_SW_STATE_CHANGE_STATUS_MASK_0TO7);
			writel(AMD_SDW_IRQ_MASK_8TO11, amd_manager->mmio +
			       ACP_SW_STATE_CHANGE_STATUS_MASK_8TO11);
			amd_sdw_read_and_process_ping_status(amd_manager);
			goto update_status;
		} else {
			dev_err_ratelimited(amd_manager->dev,
					    "Device0 detected after %d iterations\n",
					    retry_count);
		}
	}
}

static void amd_sdw_update_slave_status(u32 status_change_0to7, u32 status_change_8to11,
					struct amd_sdw_manager *amd_manager)
{
	u64 slave_stat;
	u32 val;
	int dev_index;

	if (status_change_0to7 == AMD_SDW_SLAVE_0_ATTACHED)
		memset(amd_manager->status, 0, sizeof(amd_manager->status));
	slave_stat = status_change_0to7;
	slave_stat |= FIELD_GET(AMD_SDW_MCP_SLAVE_STATUS_8TO_11, status_change_8to11) << 32;
	dev_dbg(amd_manager->dev, "status_change_0to7:0x%x status_change_8to11:0x%x\n",
		status_change_0to7, status_change_8to11);
	if (slave_stat) {
		for (dev_index = 0; dev_index <= SDW_MAX_DEVICES; ++dev_index) {
			if (slave_stat & AMD_SDW_MCP_SLAVE_STATUS_VALID_MASK(dev_index)) {
				val = (slave_stat >> AMD_SDW_MCP_SLAVE_STAT_SHIFT_MASK(dev_index)) &
				      AMD_SDW_MCP_SLAVE_STATUS_MASK;
				amd_sdw_fill_slave_status(amd_manager, dev_index, val);
			}
		}
	}
}

static void amd_sdw_process_wake_event(struct amd_sdw_manager *amd_manager)
{
	pm_request_resume(amd_manager->dev);
	writel(0x00, amd_manager->acp_mmio + ACP_SW_WAKE_EN(amd_manager->instance));
	writel(0x00, amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_8TO11);
}

static void amd_sdw_irq_thread(struct work_struct *work)
{
	struct amd_sdw_manager *amd_manager =
			container_of(work, struct amd_sdw_manager, amd_sdw_irq_thread);
	u32 status_change_8to11;
	u32 status_change_0to7;

	status_change_8to11 = readl(amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_8TO11);
	status_change_0to7 = readl(amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_0TO7);
	dev_dbg(amd_manager->dev, "[SDW%d] SDW INT: 0to7=0x%x, 8to11=0x%x\n",
		amd_manager->instance, status_change_0to7, status_change_8to11);
	if (status_change_8to11 & AMD_SDW_WAKE_STAT_MASK)
		return amd_sdw_process_wake_event(amd_manager);

	if (status_change_8to11 & AMD_SDW_PREQ_INTR_STAT) {
		amd_sdw_read_and_process_ping_status(amd_manager);
	} else {
		/* Check for the updated status on peripheral device */
		amd_sdw_update_slave_status(status_change_0to7, status_change_8to11, amd_manager);
	}
	if (status_change_8to11 || status_change_0to7)
		schedule_work(&amd_manager->amd_sdw_work);
	writel(0x00, amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_8TO11);
	writel(0x00, amd_manager->mmio + ACP_SW_STATE_CHANGE_STATUS_0TO7);
}

static void amd_sdw_probe_work(struct work_struct *work)
{
	struct amd_sdw_manager *amd_manager = container_of(work, struct amd_sdw_manager,
							   probe_work);
	struct sdw_master_prop *prop;
	int ret;

	prop = &amd_manager->bus.prop;
	if (!prop->hw_disabled) {
		amd_enable_sdw_pads(amd_manager);
		ret = amd_init_sdw_manager(amd_manager);
		if (ret)
			return;
		amd_enable_sdw_interrupts(amd_manager);
		ret = amd_enable_sdw_manager(amd_manager);
		if (ret)
			return;
		amd_sdw_set_frameshape(amd_manager);
	}
	/* Enable runtime PM */
	pm_runtime_set_autosuspend_delay(amd_manager->dev, AMD_SDW_MASTER_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(amd_manager->dev);
	pm_runtime_mark_last_busy(amd_manager->dev);
	pm_runtime_set_active(amd_manager->dev);
	pm_runtime_enable(amd_manager->dev);
}

static int amd_sdw_manager_probe(struct platform_device *pdev)
{
	const struct acp_sdw_pdata *pdata = pdev->dev.platform_data;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct sdw_master_prop *prop;
	struct sdw_bus_params *params;
	struct amd_sdw_manager *amd_manager;
	int ret;

	amd_manager = devm_kzalloc(dev, sizeof(struct amd_sdw_manager), GFP_KERNEL);
	if (!amd_manager)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	amd_manager->acp_mmio = devm_ioremap(dev, res->start, resource_size(res));
	if (!amd_manager->acp_mmio) {
		dev_err(dev, "mmio not found\n");
		return -ENOMEM;
	}
	amd_manager->instance = pdata->instance;
	amd_manager->mmio = amd_manager->acp_mmio +
			    (amd_manager->instance * SDW_MANAGER_REG_OFFSET);
	amd_manager->acp_sdw_lock = pdata->acp_sdw_lock;
	amd_manager->cols_index = sdw_find_col_index(AMD_SDW_DEFAULT_COLUMNS);
	amd_manager->rows_index = sdw_find_row_index(AMD_SDW_DEFAULT_ROWS);
	amd_manager->dev = dev;
	amd_manager->bus.ops = &amd_sdw_ops;
	amd_manager->bus.port_ops = &amd_sdw_port_ops;
	amd_manager->bus.compute_params = &amd_sdw_compute_params;
	amd_manager->bus.clk_stop_timeout = 200;
	amd_manager->bus.link_id = amd_manager->instance;

	/*
	 * Due to BIOS compatibility, the two links are exposed within
	 * the scope of a single controller. If this changes, the
	 * controller_id will have to be updated with drv_data
	 * information.
	 */
	amd_manager->bus.controller_id = 0;

	switch (amd_manager->instance) {
	case ACP_SDW0:
		amd_manager->num_dout_ports = AMD_SDW0_MAX_TX_PORTS;
		amd_manager->num_din_ports = AMD_SDW0_MAX_RX_PORTS;
		break;
	case ACP_SDW1:
		amd_manager->num_dout_ports = AMD_SDW1_MAX_TX_PORTS;
		amd_manager->num_din_ports = AMD_SDW1_MAX_RX_PORTS;
		break;
	default:
		return -EINVAL;
	}

	amd_manager->reg_mask = &sdw_manager_reg_mask_array[amd_manager->instance];
	params = &amd_manager->bus.params;

	params->col = AMD_SDW_DEFAULT_COLUMNS;
	params->row = AMD_SDW_DEFAULT_ROWS;
	prop = &amd_manager->bus.prop;
	prop->clk_freq = &amd_sdw_freq_tbl[0];
	prop->mclk_freq = AMD_SDW_BUS_BASE_FREQ;
	prop->max_clk_freq = AMD_SDW_DEFAULT_CLK_FREQ;

	ret = sdw_bus_master_add(&amd_manager->bus, dev, dev->fwnode);
	if (ret) {
		dev_err(dev, "Failed to register SoundWire manager(%d)\n", ret);
		return ret;
	}
	ret = amd_sdw_register_dais(amd_manager);
	if (ret) {
		dev_err(dev, "CPU DAI registration failed\n");
		sdw_bus_master_delete(&amd_manager->bus);
		return ret;
	}
	dev_set_drvdata(dev, amd_manager);
	INIT_WORK(&amd_manager->amd_sdw_irq_thread, amd_sdw_irq_thread);
	INIT_WORK(&amd_manager->amd_sdw_work, amd_sdw_update_slave_status_work);
	INIT_WORK(&amd_manager->probe_work, amd_sdw_probe_work);
	/*
	 * Instead of having lengthy probe sequence, use deferred probe.
	 */
	schedule_work(&amd_manager->probe_work);
	return 0;
}

static void amd_sdw_manager_remove(struct platform_device *pdev)
{
	struct amd_sdw_manager *amd_manager = dev_get_drvdata(&pdev->dev);
	int ret;

	pm_runtime_disable(&pdev->dev);
	cancel_work_sync(&amd_manager->probe_work);
	amd_disable_sdw_interrupts(amd_manager);
	sdw_bus_master_delete(&amd_manager->bus);
	ret = amd_disable_sdw_manager(amd_manager);
	if (ret)
		dev_err(&pdev->dev, "Failed to disable device (%pe)\n", ERR_PTR(ret));
}

static int amd_sdw_clock_stop(struct amd_sdw_manager *amd_manager)
{
	u32 val;
	int ret;

	ret = sdw_bus_prep_clk_stop(&amd_manager->bus);
	if (ret < 0 && ret != -ENODATA) {
		dev_err(amd_manager->dev, "prepare clock stop failed %d", ret);
		return 0;
	}
	ret = sdw_bus_clk_stop(&amd_manager->bus);
	if (ret < 0 && ret != -ENODATA) {
		dev_err(amd_manager->dev, "bus clock stop failed %d", ret);
		return 0;
	}

	ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL, val,
				 (val & AMD_SDW_CLK_STOP_DONE), ACP_DELAY_US, AMD_SDW_TIMEOUT);
	if (ret) {
		dev_err(amd_manager->dev, "SDW%x clock stop failed\n", amd_manager->instance);
		return 0;
	}

	amd_manager->clk_stopped = true;
	if (amd_manager->wake_en_mask)
		writel(0x01, amd_manager->acp_mmio + ACP_SW_WAKE_EN(amd_manager->instance));

	dev_dbg(amd_manager->dev, "SDW%x clock stop successful\n", amd_manager->instance);
	return 0;
}

static int amd_sdw_clock_stop_exit(struct amd_sdw_manager *amd_manager)
{
	int ret;
	u32 val;

	if (amd_manager->clk_stopped) {
		val = readl(amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL);
		val |= AMD_SDW_CLK_RESUME_REQ;
		writel(val, amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL);
		ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL, val,
					 (val & AMD_SDW_CLK_RESUME_DONE), ACP_DELAY_US,
					 AMD_SDW_TIMEOUT);
		if (val & AMD_SDW_CLK_RESUME_DONE) {
			writel(0, amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL);
			ret = sdw_bus_exit_clk_stop(&amd_manager->bus);
			if (ret < 0)
				dev_err(amd_manager->dev, "bus failed to exit clock stop %d\n",
					ret);
			amd_manager->clk_stopped = false;
		}
	}
	if (amd_manager->clk_stopped) {
		dev_err(amd_manager->dev, "SDW%x clock stop exit failed\n", amd_manager->instance);
		return 0;
	}
	dev_dbg(amd_manager->dev, "SDW%x clock stop exit successful\n", amd_manager->instance);
	return 0;
}

static int amd_resume_child_device(struct device *dev, void *data)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	int ret;

	if (!slave->probed) {
		dev_dbg(dev, "skipping device, no probed driver\n");
		return 0;
	}
	if (!slave->dev_num_sticky) {
		dev_dbg(dev, "skipping device, never detected on bus\n");
		return 0;
	}
	ret = pm_request_resume(dev);
	if (ret < 0) {
		dev_err(dev, "pm_request_resume failed: %d\n", ret);
		return ret;
	}
	return 0;
}

static int __maybe_unused amd_pm_prepare(struct device *dev)
{
	struct amd_sdw_manager *amd_manager = dev_get_drvdata(dev);
	struct sdw_bus *bus = &amd_manager->bus;
	int ret;

	if (bus->prop.hw_disabled) {
		dev_dbg(bus->dev, "SoundWire manager %d is disabled, ignoring\n",
			bus->link_id);
		return 0;
	}
	/*
	 * When multiple peripheral devices connected over the same link, if SoundWire manager
	 * device is not in runtime suspend state, observed that device alerts are missing
	 * without pm_prepare on AMD platforms in clockstop mode0.
	 */
	if (amd_manager->power_mode_mask & AMD_SDW_CLK_STOP_MODE) {
		ret = pm_request_resume(dev);
		if (ret < 0) {
			dev_err(bus->dev, "pm_request_resume failed: %d\n", ret);
			return 0;
		}
	}
	/* To force peripheral devices to system level suspend state, resume the devices
	 * from runtime suspend state first. Without that unable to dispatch the alert
	 * status to peripheral driver during system level resume as they are in runtime
	 * suspend state.
	 */
	ret = device_for_each_child(bus->dev, NULL, amd_resume_child_device);
	if (ret < 0)
		dev_err(dev, "amd_resume_child_device failed: %d\n", ret);
	return 0;
}

static int __maybe_unused amd_suspend(struct device *dev)
{
	struct amd_sdw_manager *amd_manager = dev_get_drvdata(dev);
	struct sdw_bus *bus = &amd_manager->bus;
	int ret;

	if (bus->prop.hw_disabled) {
		dev_dbg(bus->dev, "SoundWire manager %d is disabled, ignoring\n",
			bus->link_id);
		return 0;
	}

	if (amd_manager->power_mode_mask & AMD_SDW_CLK_STOP_MODE) {
		return amd_sdw_clock_stop(amd_manager);
	} else if (amd_manager->power_mode_mask & AMD_SDW_POWER_OFF_MODE) {
		/*
		 * As per hardware programming sequence on AMD platforms,
		 * clock stop should be invoked first before powering-off
		 */
		ret = amd_sdw_clock_stop(amd_manager);
		if (ret)
			return ret;
		return amd_deinit_sdw_manager(amd_manager);
	}
	return 0;
}

static int __maybe_unused amd_suspend_runtime(struct device *dev)
{
	struct amd_sdw_manager *amd_manager = dev_get_drvdata(dev);
	struct sdw_bus *bus = &amd_manager->bus;
	int ret;

	if (bus->prop.hw_disabled) {
		dev_dbg(bus->dev, "SoundWire manager %d is disabled,\n",
			bus->link_id);
		return 0;
	}
	if (amd_manager->power_mode_mask & AMD_SDW_CLK_STOP_MODE) {
		return amd_sdw_clock_stop(amd_manager);
	} else if (amd_manager->power_mode_mask & AMD_SDW_POWER_OFF_MODE) {
		ret = amd_sdw_clock_stop(amd_manager);
		if (ret)
			return ret;
		return amd_deinit_sdw_manager(amd_manager);
	}
	return 0;
}

static int __maybe_unused amd_resume_runtime(struct device *dev)
{
	struct amd_sdw_manager *amd_manager = dev_get_drvdata(dev);
	struct sdw_bus *bus = &amd_manager->bus;
	int ret;
	u32 val;

	if (bus->prop.hw_disabled) {
		dev_dbg(bus->dev, "SoundWire manager %d is disabled, ignoring\n",
			bus->link_id);
		return 0;
	}

	if (amd_manager->power_mode_mask & AMD_SDW_CLK_STOP_MODE) {
		return amd_sdw_clock_stop_exit(amd_manager);
	} else if (amd_manager->power_mode_mask & AMD_SDW_POWER_OFF_MODE) {
		val = readl(amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL);
		if (val) {
			val |= AMD_SDW_CLK_RESUME_REQ;
			writel(val, amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL);
			ret = readl_poll_timeout(amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL, val,
						 (val & AMD_SDW_CLK_RESUME_DONE), ACP_DELAY_US,
						 AMD_SDW_TIMEOUT);
			if (val & AMD_SDW_CLK_RESUME_DONE) {
				writel(0, amd_manager->mmio + ACP_SW_CLK_RESUME_CTRL);
				amd_manager->clk_stopped = false;
			}
		}
		sdw_clear_slave_status(bus, SDW_UNATTACH_REQUEST_MASTER_RESET);
		amd_init_sdw_manager(amd_manager);
		amd_enable_sdw_interrupts(amd_manager);
		ret = amd_enable_sdw_manager(amd_manager);
		if (ret)
			return ret;
		amd_sdw_set_frameshape(amd_manager);
	}
	return 0;
}

static const struct dev_pm_ops amd_pm = {
	.prepare = amd_pm_prepare,
	SET_SYSTEM_SLEEP_PM_OPS(amd_suspend, amd_resume_runtime)
	SET_RUNTIME_PM_OPS(amd_suspend_runtime, amd_resume_runtime, NULL)
};

static struct platform_driver amd_sdw_driver = {
	.probe	= &amd_sdw_manager_probe,
	.remove_new = &amd_sdw_manager_remove,
	.driver = {
		.name	= "amd_sdw_manager",
		.pm = &amd_pm,
	}
};
module_platform_driver(amd_sdw_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD SoundWire driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);

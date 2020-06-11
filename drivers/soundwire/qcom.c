// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019, Linaro Limited

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/slimbus.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "bus.h"

#define SWRM_COMP_HW_VERSION					0x00
#define SWRM_COMP_CFG_ADDR					0x04
#define SWRM_COMP_CFG_IRQ_LEVEL_OR_PULSE_MSK			BIT(1)
#define SWRM_COMP_CFG_ENABLE_MSK				BIT(0)
#define SWRM_COMP_PARAMS					0x100
#define SWRM_COMP_PARAMS_DOUT_PORTS_MASK			GENMASK(4, 0)
#define SWRM_COMP_PARAMS_DIN_PORTS_MASK				GENMASK(9, 5)
#define SWRM_INTERRUPT_STATUS					0x200
#define SWRM_INTERRUPT_STATUS_RMSK				GENMASK(16, 0)
#define SWRM_INTERRUPT_STATUS_NEW_SLAVE_ATTACHED		BIT(1)
#define SWRM_INTERRUPT_STATUS_CHANGE_ENUM_SLAVE_STATUS		BIT(2)
#define SWRM_INTERRUPT_STATUS_CMD_ERROR				BIT(7)
#define SWRM_INTERRUPT_STATUS_SPECIAL_CMD_ID_FINISHED		BIT(10)
#define SWRM_INTERRUPT_MASK_ADDR				0x204
#define SWRM_INTERRUPT_CLEAR					0x208
#define SWRM_CMD_FIFO_WR_CMD					0x300
#define SWRM_CMD_FIFO_RD_CMD					0x304
#define SWRM_CMD_FIFO_CMD					0x308
#define SWRM_CMD_FIFO_STATUS					0x30C
#define SWRM_CMD_FIFO_CFG_ADDR					0x314
#define SWRM_RD_WR_CMD_RETRIES					0x7
#define SWRM_CMD_FIFO_RD_FIFO_ADDR				0x318
#define SWRM_ENUMERATOR_CFG_ADDR				0x500
#define SWRM_MCP_FRAME_CTRL_BANK_ADDR(m)		(0x101C + 0x40 * (m))
#define SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_SHFT			3
#define SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_BMSK			GENMASK(2, 0)
#define SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_BMSK			GENMASK(7, 3)
#define SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_SHFT			0
#define SWRM_MCP_CFG_ADDR					0x1048
#define SWRM_MCP_CFG_MAX_NUM_OF_CMD_NO_PINGS_BMSK		GENMASK(21, 17)
#define SWRM_MCP_CFG_MAX_NUM_OF_CMD_NO_PINGS_SHFT		0x11
#define SWRM_DEF_CMD_NO_PINGS					0x1f
#define SWRM_MCP_STATUS						0x104C
#define SWRM_MCP_STATUS_BANK_NUM_MASK				BIT(0)
#define SWRM_MCP_SLV_STATUS					0x1090
#define SWRM_MCP_SLV_STATUS_MASK				GENMASK(1, 0)
#define SWRM_DP_PORT_CTRL_BANK(n, m)	(0x1124 + 0x100 * (n - 1) + 0x40 * m)
#define SWRM_DP_PORT_CTRL_EN_CHAN_SHFT				0x18
#define SWRM_DP_PORT_CTRL_OFFSET2_SHFT				0x10
#define SWRM_DP_PORT_CTRL_OFFSET1_SHFT				0x08
#define SWRM_AHB_BRIDGE_WR_DATA_0				0xc85
#define SWRM_AHB_BRIDGE_WR_ADDR_0				0xc89
#define SWRM_AHB_BRIDGE_RD_ADDR_0				0xc8d
#define SWRM_AHB_BRIDGE_RD_DATA_0				0xc91

#define SWRM_REG_VAL_PACK(data, dev, id, reg)	\
			((reg) | ((id) << 16) | ((dev) << 20) | ((data) << 24))

#define SWRM_MAX_ROW_VAL	0 /* Rows = 48 */
#define SWRM_DEFAULT_ROWS	48
#define SWRM_MIN_COL_VAL	0 /* Cols = 2 */
#define SWRM_DEFAULT_COL	16
#define SWRM_MAX_COL_VAL	7
#define SWRM_SPECIAL_CMD_ID	0xF
#define MAX_FREQ_NUM		1
#define TIMEOUT_MS		(2 * HZ)
#define QCOM_SWRM_MAX_RD_LEN	0xf
#define QCOM_SDW_MAX_PORTS	14
#define DEFAULT_CLK_FREQ	9600000
#define SWRM_MAX_DAIS		0xF

struct qcom_swrm_port_config {
	u8 si;
	u8 off1;
	u8 off2;
};

struct qcom_swrm_ctrl {
	struct sdw_bus bus;
	struct device *dev;
	struct regmap *regmap;
	struct completion *comp;
	struct work_struct slave_work;
	/* read/write lock */
	spinlock_t comp_lock;
	/* Port alloc/free lock */
	struct mutex port_lock;
	struct clk *hclk;
	u8 wr_cmd_id;
	u8 rd_cmd_id;
	int irq;
	unsigned int version;
	int num_din_ports;
	int num_dout_ports;
	unsigned long dout_port_mask;
	unsigned long din_port_mask;
	struct qcom_swrm_port_config pconfig[QCOM_SDW_MAX_PORTS];
	struct sdw_stream_runtime *sruntime[SWRM_MAX_DAIS];
	enum sdw_slave_status status[SDW_MAX_DEVICES];
	int (*reg_read)(struct qcom_swrm_ctrl *ctrl, int reg, u32 *val);
	int (*reg_write)(struct qcom_swrm_ctrl *ctrl, int reg, int val);
};

#define to_qcom_sdw(b)	container_of(b, struct qcom_swrm_ctrl, bus)

static int qcom_swrm_abh_reg_read(struct qcom_swrm_ctrl *ctrl, int reg,
				  u32 *val)
{
	struct regmap *wcd_regmap = ctrl->regmap;
	int ret;

	/* pg register + offset */
	ret = regmap_bulk_write(wcd_regmap, SWRM_AHB_BRIDGE_RD_ADDR_0,
			  (u8 *)&reg, 4);
	if (ret < 0)
		return SDW_CMD_FAIL;

	ret = regmap_bulk_read(wcd_regmap, SWRM_AHB_BRIDGE_RD_DATA_0,
			       val, 4);
	if (ret < 0)
		return SDW_CMD_FAIL;

	return SDW_CMD_OK;
}

static int qcom_swrm_ahb_reg_write(struct qcom_swrm_ctrl *ctrl,
				   int reg, int val)
{
	struct regmap *wcd_regmap = ctrl->regmap;
	int ret;
	/* pg register + offset */
	ret = regmap_bulk_write(wcd_regmap, SWRM_AHB_BRIDGE_WR_DATA_0,
			  (u8 *)&val, 4);
	if (ret)
		return SDW_CMD_FAIL;

	/* write address register */
	ret = regmap_bulk_write(wcd_regmap, SWRM_AHB_BRIDGE_WR_ADDR_0,
			  (u8 *)&reg, 4);
	if (ret)
		return SDW_CMD_FAIL;

	return SDW_CMD_OK;
}

static int qcom_swrm_cmd_fifo_wr_cmd(struct qcom_swrm_ctrl *ctrl, u8 cmd_data,
				     u8 dev_addr, u16 reg_addr)
{
	DECLARE_COMPLETION_ONSTACK(comp);
	unsigned long flags;
	u32 val;
	int ret;

	spin_lock_irqsave(&ctrl->comp_lock, flags);
	ctrl->comp = &comp;
	spin_unlock_irqrestore(&ctrl->comp_lock, flags);
	val = SWRM_REG_VAL_PACK(cmd_data, dev_addr,
				SWRM_SPECIAL_CMD_ID, reg_addr);
	ret = ctrl->reg_write(ctrl, SWRM_CMD_FIFO_WR_CMD, val);
	if (ret)
		goto err;

	ret = wait_for_completion_timeout(ctrl->comp,
					  msecs_to_jiffies(TIMEOUT_MS));

	if (!ret)
		ret = SDW_CMD_IGNORED;
	else
		ret = SDW_CMD_OK;
err:
	spin_lock_irqsave(&ctrl->comp_lock, flags);
	ctrl->comp = NULL;
	spin_unlock_irqrestore(&ctrl->comp_lock, flags);

	return ret;
}

static int qcom_swrm_cmd_fifo_rd_cmd(struct qcom_swrm_ctrl *ctrl,
				     u8 dev_addr, u16 reg_addr,
				     u32 len, u8 *rval)
{
	int i, ret;
	u32 val;
	DECLARE_COMPLETION_ONSTACK(comp);
	unsigned long flags;

	spin_lock_irqsave(&ctrl->comp_lock, flags);
	ctrl->comp = &comp;
	spin_unlock_irqrestore(&ctrl->comp_lock, flags);

	val = SWRM_REG_VAL_PACK(len, dev_addr, SWRM_SPECIAL_CMD_ID, reg_addr);
	ret = ctrl->reg_write(ctrl, SWRM_CMD_FIFO_RD_CMD, val);
	if (ret)
		goto err;

	ret = wait_for_completion_timeout(ctrl->comp,
					  msecs_to_jiffies(TIMEOUT_MS));

	if (!ret) {
		ret = SDW_CMD_IGNORED;
		goto err;
	} else {
		ret = SDW_CMD_OK;
	}

	for (i = 0; i < len; i++) {
		ctrl->reg_read(ctrl, SWRM_CMD_FIFO_RD_FIFO_ADDR, &val);
		rval[i] = val & 0xFF;
	}

err:
	spin_lock_irqsave(&ctrl->comp_lock, flags);
	ctrl->comp = NULL;
	spin_unlock_irqrestore(&ctrl->comp_lock, flags);

	return ret;
}

static void qcom_swrm_get_device_status(struct qcom_swrm_ctrl *ctrl)
{
	u32 val;
	int i;

	ctrl->reg_read(ctrl, SWRM_MCP_SLV_STATUS, &val);

	for (i = 0; i < SDW_MAX_DEVICES; i++) {
		u32 s;

		s = (val >> (i * 2));
		s &= SWRM_MCP_SLV_STATUS_MASK;
		ctrl->status[i] = s;
	}
}

static irqreturn_t qcom_swrm_irq_handler(int irq, void *dev_id)
{
	struct qcom_swrm_ctrl *ctrl = dev_id;
	u32 sts, value;
	unsigned long flags;

	ctrl->reg_read(ctrl, SWRM_INTERRUPT_STATUS, &sts);

	if (sts & SWRM_INTERRUPT_STATUS_CMD_ERROR) {
		ctrl->reg_read(ctrl, SWRM_CMD_FIFO_STATUS, &value);
		dev_err_ratelimited(ctrl->dev,
				    "CMD error, fifo status 0x%x\n",
				     value);
		ctrl->reg_write(ctrl, SWRM_CMD_FIFO_CMD, 0x1);
	}

	if ((sts & SWRM_INTERRUPT_STATUS_NEW_SLAVE_ATTACHED) ||
	    sts & SWRM_INTERRUPT_STATUS_CHANGE_ENUM_SLAVE_STATUS)
		schedule_work(&ctrl->slave_work);

	/**
	 * clear the interrupt before complete() is called, as complete can
	 * schedule new read/writes which require interrupts, clearing the
	 * interrupt would avoid missing interrupts in such cases.
	 */
	ctrl->reg_write(ctrl, SWRM_INTERRUPT_CLEAR, sts);

	if (sts & SWRM_INTERRUPT_STATUS_SPECIAL_CMD_ID_FINISHED) {
		spin_lock_irqsave(&ctrl->comp_lock, flags);
		if (ctrl->comp)
			complete(ctrl->comp);
		spin_unlock_irqrestore(&ctrl->comp_lock, flags);
	}

	return IRQ_HANDLED;
}
static int qcom_swrm_init(struct qcom_swrm_ctrl *ctrl)
{
	u32 val;

	/* Clear Rows and Cols */
	val = (SWRM_MAX_ROW_VAL << SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_SHFT |
	       SWRM_MIN_COL_VAL << SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_SHFT);

	ctrl->reg_write(ctrl, SWRM_MCP_FRAME_CTRL_BANK_ADDR(0), val);

	/* Disable Auto enumeration */
	ctrl->reg_write(ctrl, SWRM_ENUMERATOR_CFG_ADDR, 0);

	/* Mask soundwire interrupts */
	ctrl->reg_write(ctrl, SWRM_INTERRUPT_MASK_ADDR,
			SWRM_INTERRUPT_STATUS_RMSK);

	/* Configure No pings */
	ctrl->reg_read(ctrl, SWRM_MCP_CFG_ADDR, &val);
	val &= ~SWRM_MCP_CFG_MAX_NUM_OF_CMD_NO_PINGS_BMSK;
	val |= (SWRM_DEF_CMD_NO_PINGS <<
		SWRM_MCP_CFG_MAX_NUM_OF_CMD_NO_PINGS_SHFT);
	ctrl->reg_write(ctrl, SWRM_MCP_CFG_ADDR, val);

	/* Configure number of retries of a read/write cmd */
	ctrl->reg_write(ctrl, SWRM_CMD_FIFO_CFG_ADDR, SWRM_RD_WR_CMD_RETRIES);

	/* Set IRQ to PULSE */
	ctrl->reg_write(ctrl, SWRM_COMP_CFG_ADDR,
			SWRM_COMP_CFG_IRQ_LEVEL_OR_PULSE_MSK |
			SWRM_COMP_CFG_ENABLE_MSK);
	return 0;
}

static enum sdw_command_response qcom_swrm_xfer_msg(struct sdw_bus *bus,
						    struct sdw_msg *msg)
{
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	int ret, i, len;

	if (msg->flags == SDW_MSG_FLAG_READ) {
		for (i = 0; i < msg->len;) {
			if ((msg->len - i) < QCOM_SWRM_MAX_RD_LEN)
				len = msg->len - i;
			else
				len = QCOM_SWRM_MAX_RD_LEN;

			ret = qcom_swrm_cmd_fifo_rd_cmd(ctrl, msg->dev_num,
							msg->addr + i, len,
						       &msg->buf[i]);
			if (ret)
				return ret;

			i = i + len;
		}
	} else if (msg->flags == SDW_MSG_FLAG_WRITE) {
		for (i = 0; i < msg->len; i++) {
			ret = qcom_swrm_cmd_fifo_wr_cmd(ctrl, msg->buf[i],
							msg->dev_num,
						       msg->addr + i);
			if (ret)
				return SDW_CMD_IGNORED;
		}
	}

	return SDW_CMD_OK;
}

static int qcom_swrm_pre_bank_switch(struct sdw_bus *bus)
{
	u32 reg = SWRM_MCP_FRAME_CTRL_BANK_ADDR(bus->params.next_bank);
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	u32 val;

	ctrl->reg_read(ctrl, reg, &val);

	val &= ~SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_BMSK;
	val &= ~SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_BMSK;

	val |= (SWRM_MAX_ROW_VAL << SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_SHFT |
		SWRM_MAX_COL_VAL << SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_SHFT);

	return ctrl->reg_write(ctrl, reg, val);
}

static int qcom_swrm_port_params(struct sdw_bus *bus,
				 struct sdw_port_params *p_params,
				 unsigned int bank)
{
	/* TBD */
	return 0;
}

static int qcom_swrm_transport_params(struct sdw_bus *bus,
				      struct sdw_transport_params *params,
				      enum sdw_reg_bank bank)
{
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	u32 value;

	value = params->offset1 << SWRM_DP_PORT_CTRL_OFFSET1_SHFT;
	value |= params->offset2 << SWRM_DP_PORT_CTRL_OFFSET2_SHFT;
	value |= params->sample_interval - 1;

	return ctrl->reg_write(ctrl,
			       SWRM_DP_PORT_CTRL_BANK((params->port_num), bank),
			       value);
}

static int qcom_swrm_port_enable(struct sdw_bus *bus,
				 struct sdw_enable_ch *enable_ch,
				 unsigned int bank)
{
	u32 reg = SWRM_DP_PORT_CTRL_BANK(enable_ch->port_num, bank);
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	u32 val;

	ctrl->reg_read(ctrl, reg, &val);

	if (enable_ch->enable)
		val |= (enable_ch->ch_mask << SWRM_DP_PORT_CTRL_EN_CHAN_SHFT);
	else
		val &= ~(0xff << SWRM_DP_PORT_CTRL_EN_CHAN_SHFT);

	return ctrl->reg_write(ctrl, reg, val);
}

static struct sdw_master_port_ops qcom_swrm_port_ops = {
	.dpn_set_port_params = qcom_swrm_port_params,
	.dpn_set_port_transport_params = qcom_swrm_transport_params,
	.dpn_port_enable_ch = qcom_swrm_port_enable,
};

static struct sdw_master_ops qcom_swrm_ops = {
	.xfer_msg = qcom_swrm_xfer_msg,
	.pre_bank_switch = qcom_swrm_pre_bank_switch,
};

static int qcom_swrm_compute_params(struct sdw_bus *bus)
{
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	struct sdw_master_runtime *m_rt;
	struct sdw_slave_runtime *s_rt;
	struct sdw_port_runtime *p_rt;
	struct qcom_swrm_port_config *pcfg;
	int i = 0;

	list_for_each_entry(m_rt, &bus->m_rt_list, bus_node) {
		list_for_each_entry(p_rt, &m_rt->port_list, port_node) {
			pcfg = &ctrl->pconfig[p_rt->num - 1];
			p_rt->transport_params.port_num = p_rt->num;
			p_rt->transport_params.sample_interval = pcfg->si + 1;
			p_rt->transport_params.offset1 = pcfg->off1;
			p_rt->transport_params.offset2 = pcfg->off2;
		}

		list_for_each_entry(s_rt, &m_rt->slave_rt_list, m_rt_node) {
			list_for_each_entry(p_rt, &s_rt->port_list, port_node) {
				pcfg = &ctrl->pconfig[i];
				p_rt->transport_params.port_num = p_rt->num;
				p_rt->transport_params.sample_interval =
					pcfg->si + 1;
				p_rt->transport_params.offset1 = pcfg->off1;
				p_rt->transport_params.offset2 = pcfg->off2;
				i++;
			}
		}
	}

	return 0;
}

static u32 qcom_swrm_freq_tbl[MAX_FREQ_NUM] = {
	DEFAULT_CLK_FREQ,
};

static void qcom_swrm_slave_wq(struct work_struct *work)
{
	struct qcom_swrm_ctrl *ctrl =
			container_of(work, struct qcom_swrm_ctrl, slave_work);

	qcom_swrm_get_device_status(ctrl);
	sdw_handle_slave_status(&ctrl->bus, ctrl->status);
}


static void qcom_swrm_stream_free_ports(struct qcom_swrm_ctrl *ctrl,
					struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt;
	struct sdw_port_runtime *p_rt;
	unsigned long *port_mask;

	mutex_lock(&ctrl->port_lock);

	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		if (m_rt->direction == SDW_DATA_DIR_RX)
			port_mask = &ctrl->dout_port_mask;
		else
			port_mask = &ctrl->din_port_mask;

		list_for_each_entry(p_rt, &m_rt->port_list, port_node)
			clear_bit(p_rt->num - 1, port_mask);
	}

	mutex_unlock(&ctrl->port_lock);
}

static int qcom_swrm_stream_alloc_ports(struct qcom_swrm_ctrl *ctrl,
					struct sdw_stream_runtime *stream,
				       struct snd_pcm_hw_params *params,
				       int direction)
{
	struct sdw_port_config pconfig[QCOM_SDW_MAX_PORTS];
	struct sdw_stream_config sconfig;
	struct sdw_master_runtime *m_rt;
	struct sdw_slave_runtime *s_rt;
	struct sdw_port_runtime *p_rt;
	unsigned long *port_mask;
	int i, maxport, pn, nports = 0, ret = 0;

	mutex_lock(&ctrl->port_lock);
	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		if (m_rt->direction == SDW_DATA_DIR_RX) {
			maxport = ctrl->num_dout_ports;
			port_mask = &ctrl->dout_port_mask;
		} else {
			maxport = ctrl->num_din_ports;
			port_mask = &ctrl->din_port_mask;
		}

		list_for_each_entry(s_rt, &m_rt->slave_rt_list, m_rt_node) {
			list_for_each_entry(p_rt, &s_rt->port_list, port_node) {
				/* Port numbers start from 1 - 14*/
				pn = find_first_zero_bit(port_mask, maxport);
				if (pn > (maxport - 1)) {
					dev_err(ctrl->dev, "All ports busy\n");
					ret = -EBUSY;
					goto err;
				}
				set_bit(pn, port_mask);
				pconfig[nports].num = pn + 1;
				pconfig[nports].ch_mask = p_rt->ch_mask;
				nports++;
			}
		}
	}

	if (direction == SNDRV_PCM_STREAM_CAPTURE)
		sconfig.direction = SDW_DATA_DIR_TX;
	else
		sconfig.direction = SDW_DATA_DIR_RX;

	/* hw parameters wil be ignored as we only support PDM */
	sconfig.ch_count = 1;
	sconfig.frame_rate = params_rate(params);
	sconfig.type = stream->type;
	sconfig.bps = 1;
	sdw_stream_add_master(&ctrl->bus, &sconfig, pconfig,
			      nports, stream);
err:
	if (ret) {
		for (i = 0; i < nports; i++)
			clear_bit(pconfig[i].num - 1, port_mask);
	}

	mutex_unlock(&ctrl->port_lock);

	return ret;
}

static int qcom_swrm_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			      struct snd_soc_dai *dai)
{
	struct qcom_swrm_ctrl *ctrl = dev_get_drvdata(dai->dev);
	struct sdw_stream_runtime *sruntime = ctrl->sruntime[dai->id];
	int ret;

	ret = qcom_swrm_stream_alloc_ports(ctrl, sruntime, params,
					   substream->stream);
	if (ret)
		qcom_swrm_stream_free_ports(ctrl, sruntime);

	return ret;
}

static int qcom_swrm_hw_free(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct qcom_swrm_ctrl *ctrl = dev_get_drvdata(dai->dev);
	struct sdw_stream_runtime *sruntime = ctrl->sruntime[dai->id];

	qcom_swrm_stream_free_ports(ctrl, sruntime);
	sdw_stream_remove_master(&ctrl->bus, sruntime);

	return 0;
}

static int qcom_swrm_set_sdw_stream(struct snd_soc_dai *dai,
				    void *stream, int direction)
{
	struct qcom_swrm_ctrl *ctrl = dev_get_drvdata(dai->dev);

	ctrl->sruntime[dai->id] = stream;

	return 0;
}

static void *qcom_swrm_get_sdw_stream(struct snd_soc_dai *dai, int direction)
{
	struct qcom_swrm_ctrl *ctrl = dev_get_drvdata(dai->dev);

	return ctrl->sruntime[dai->id];
}

static int qcom_swrm_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct qcom_swrm_ctrl *ctrl = dev_get_drvdata(dai->dev);
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct sdw_stream_runtime *sruntime;
	struct snd_soc_dai *codec_dai;
	int ret, i;

	sruntime = sdw_alloc_stream(dai->name);
	if (!sruntime)
		return -ENOMEM;

	ctrl->sruntime[dai->id] = sruntime;

	for_each_rtd_codec_dais(rtd, i, codec_dai) {
		ret = snd_soc_dai_set_sdw_stream(codec_dai, sruntime,
						 substream->stream);
		if (ret < 0 && ret != -ENOTSUPP) {
			dev_err(dai->dev, "Failed to set sdw stream on %s",
				codec_dai->name);
			sdw_release_stream(sruntime);
			return ret;
		}
	}

	return 0;
}

static void qcom_swrm_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct qcom_swrm_ctrl *ctrl = dev_get_drvdata(dai->dev);

	sdw_release_stream(ctrl->sruntime[dai->id]);
	ctrl->sruntime[dai->id] = NULL;
}

static const struct snd_soc_dai_ops qcom_swrm_pdm_dai_ops = {
	.hw_params = qcom_swrm_hw_params,
	.hw_free = qcom_swrm_hw_free,
	.startup = qcom_swrm_startup,
	.shutdown = qcom_swrm_shutdown,
	.set_sdw_stream = qcom_swrm_set_sdw_stream,
	.get_sdw_stream = qcom_swrm_get_sdw_stream,
};

static const struct snd_soc_component_driver qcom_swrm_dai_component = {
	.name = "soundwire",
};

static int qcom_swrm_register_dais(struct qcom_swrm_ctrl *ctrl)
{
	int num_dais = ctrl->num_dout_ports + ctrl->num_din_ports;
	struct snd_soc_dai_driver *dais;
	struct snd_soc_pcm_stream *stream;
	struct device *dev = ctrl->dev;
	int i;

	/* PDM dais are only tested for now */
	dais = devm_kcalloc(dev, num_dais, sizeof(*dais), GFP_KERNEL);
	if (!dais)
		return -ENOMEM;

	for (i = 0; i < num_dais; i++) {
		dais[i].name = devm_kasprintf(dev, GFP_KERNEL, "SDW Pin%d", i);
		if (!dais[i].name)
			return -ENOMEM;

		if (i < ctrl->num_dout_ports)
			stream = &dais[i].playback;
		else
			stream = &dais[i].capture;

		stream->channels_min = 1;
		stream->channels_max = 1;
		stream->rates = SNDRV_PCM_RATE_48000;
		stream->formats = SNDRV_PCM_FMTBIT_S16_LE;

		dais[i].ops = &qcom_swrm_pdm_dai_ops;
		dais[i].id = i;
	}

	return devm_snd_soc_register_component(ctrl->dev,
						&qcom_swrm_dai_component,
						dais, num_dais);
}

static int qcom_swrm_get_port_config(struct qcom_swrm_ctrl *ctrl)
{
	struct device_node *np = ctrl->dev->of_node;
	u8 off1[QCOM_SDW_MAX_PORTS];
	u8 off2[QCOM_SDW_MAX_PORTS];
	u8 si[QCOM_SDW_MAX_PORTS];
	int i, ret, nports, val;

	ctrl->reg_read(ctrl, SWRM_COMP_PARAMS, &val);

	ctrl->num_dout_ports = val & SWRM_COMP_PARAMS_DOUT_PORTS_MASK;
	ctrl->num_din_ports = (val & SWRM_COMP_PARAMS_DIN_PORTS_MASK) >> 5;

	ret = of_property_read_u32(np, "qcom,din-ports", &val);
	if (ret)
		return ret;

	if (val > ctrl->num_din_ports)
		return -EINVAL;

	ctrl->num_din_ports = val;

	ret = of_property_read_u32(np, "qcom,dout-ports", &val);
	if (ret)
		return ret;

	if (val > ctrl->num_dout_ports)
		return -EINVAL;

	ctrl->num_dout_ports = val;

	nports = ctrl->num_dout_ports + ctrl->num_din_ports;

	ret = of_property_read_u8_array(np, "qcom,ports-offset1",
					off1, nports);
	if (ret)
		return ret;

	ret = of_property_read_u8_array(np, "qcom,ports-offset2",
					off2, nports);
	if (ret)
		return ret;

	ret = of_property_read_u8_array(np, "qcom,ports-sinterval-low",
					si, nports);
	if (ret)
		return ret;

	for (i = 0; i < nports; i++) {
		ctrl->pconfig[i].si = si[i];
		ctrl->pconfig[i].off1 = off1[i];
		ctrl->pconfig[i].off2 = off2[i];
	}

	return 0;
}

static int qcom_swrm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdw_master_prop *prop;
	struct sdw_bus_params *params;
	struct qcom_swrm_ctrl *ctrl;
	int ret;
	u32 val;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	if (dev->parent->bus == &slimbus_bus) {
		ctrl->reg_read = qcom_swrm_abh_reg_read;
		ctrl->reg_write = qcom_swrm_ahb_reg_write;
		ctrl->regmap = dev_get_regmap(dev->parent, NULL);
		if (!ctrl->regmap)
			return -EINVAL;
	} else {
		/* Only WCD based SoundWire controller is supported */
		return -ENOTSUPP;
	}

	ctrl->irq = of_irq_get(dev->of_node, 0);
	if (ctrl->irq < 0) {
		ret = ctrl->irq;
		goto err_init;
	}

	ctrl->hclk = devm_clk_get(dev, "iface");
	if (IS_ERR(ctrl->hclk)) {
		ret = PTR_ERR(ctrl->hclk);
		goto err_init;
	}

	clk_prepare_enable(ctrl->hclk);

	ctrl->dev = dev;
	dev_set_drvdata(&pdev->dev, ctrl);
	spin_lock_init(&ctrl->comp_lock);
	mutex_init(&ctrl->port_lock);
	INIT_WORK(&ctrl->slave_work, qcom_swrm_slave_wq);

	ctrl->bus.ops = &qcom_swrm_ops;
	ctrl->bus.port_ops = &qcom_swrm_port_ops;
	ctrl->bus.compute_params = &qcom_swrm_compute_params;

	ret = qcom_swrm_get_port_config(ctrl);
	if (ret)
		goto err_clk;

	params = &ctrl->bus.params;
	params->max_dr_freq = DEFAULT_CLK_FREQ;
	params->curr_dr_freq = DEFAULT_CLK_FREQ;
	params->col = SWRM_DEFAULT_COL;
	params->row = SWRM_DEFAULT_ROWS;
	ctrl->reg_read(ctrl, SWRM_MCP_STATUS, &val);
	params->curr_bank = val & SWRM_MCP_STATUS_BANK_NUM_MASK;
	params->next_bank = !params->curr_bank;

	prop = &ctrl->bus.prop;
	prop->max_clk_freq = DEFAULT_CLK_FREQ;
	prop->num_clk_gears = 0;
	prop->num_clk_freq = MAX_FREQ_NUM;
	prop->clk_freq = &qcom_swrm_freq_tbl[0];
	prop->default_col = SWRM_DEFAULT_COL;
	prop->default_row = SWRM_DEFAULT_ROWS;

	ctrl->reg_read(ctrl, SWRM_COMP_HW_VERSION, &ctrl->version);

	ret = devm_request_threaded_irq(dev, ctrl->irq, NULL,
					qcom_swrm_irq_handler,
					IRQF_TRIGGER_RISING |
					IRQF_ONESHOT,
					"soundwire", ctrl);
	if (ret) {
		dev_err(dev, "Failed to request soundwire irq\n");
		goto err_clk;
	}

	ret = sdw_bus_master_add(&ctrl->bus, dev, dev->fwnode);
	if (ret) {
		dev_err(dev, "Failed to register Soundwire controller (%d)\n",
			ret);
		goto err_clk;
	}

	qcom_swrm_init(ctrl);
	ret = qcom_swrm_register_dais(ctrl);
	if (ret)
		goto err_master_add;

	dev_info(dev, "Qualcomm Soundwire controller v%x.%x.%x Registered\n",
		 (ctrl->version >> 24) & 0xff, (ctrl->version >> 16) & 0xff,
		 ctrl->version & 0xffff);

	return 0;

err_master_add:
	sdw_bus_master_delete(&ctrl->bus);
err_clk:
	clk_disable_unprepare(ctrl->hclk);
err_init:
	return ret;
}

static int qcom_swrm_remove(struct platform_device *pdev)
{
	struct qcom_swrm_ctrl *ctrl = dev_get_drvdata(&pdev->dev);

	sdw_bus_master_delete(&ctrl->bus);
	clk_disable_unprepare(ctrl->hclk);

	return 0;
}

static const struct of_device_id qcom_swrm_of_match[] = {
	{ .compatible = "qcom,soundwire-v1.3.0", },
	{/* sentinel */},
};

MODULE_DEVICE_TABLE(of, qcom_swrm_of_match);

static struct platform_driver qcom_swrm_driver = {
	.probe	= &qcom_swrm_probe,
	.remove = &qcom_swrm_remove,
	.driver = {
		.name	= "qcom-soundwire",
		.of_match_table = qcom_swrm_of_match,
	}
};
module_platform_driver(qcom_swrm_driver);

MODULE_DESCRIPTION("Qualcomm soundwire driver");
MODULE_LICENSE("GPL v2");

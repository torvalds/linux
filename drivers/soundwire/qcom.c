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
#define SWRM_COMP_PARAMS_WR_FIFO_DEPTH				GENMASK(14, 10)
#define SWRM_COMP_PARAMS_RD_FIFO_DEPTH				GENMASK(19, 15)
#define SWRM_COMP_PARAMS_DOUT_PORTS_MASK			GENMASK(4, 0)
#define SWRM_COMP_PARAMS_DIN_PORTS_MASK				GENMASK(9, 5)
#define SWRM_INTERRUPT_STATUS					0x200
#define SWRM_INTERRUPT_STATUS_RMSK				GENMASK(16, 0)
#define SWRM_INTERRUPT_STATUS_SLAVE_PEND_IRQ			BIT(0)
#define SWRM_INTERRUPT_STATUS_NEW_SLAVE_ATTACHED		BIT(1)
#define SWRM_INTERRUPT_STATUS_CHANGE_ENUM_SLAVE_STATUS		BIT(2)
#define SWRM_INTERRUPT_STATUS_MASTER_CLASH_DET			BIT(3)
#define SWRM_INTERRUPT_STATUS_RD_FIFO_OVERFLOW			BIT(4)
#define SWRM_INTERRUPT_STATUS_RD_FIFO_UNDERFLOW			BIT(5)
#define SWRM_INTERRUPT_STATUS_WR_CMD_FIFO_OVERFLOW		BIT(6)
#define SWRM_INTERRUPT_STATUS_CMD_ERROR				BIT(7)
#define SWRM_INTERRUPT_STATUS_DOUT_PORT_COLLISION		BIT(8)
#define SWRM_INTERRUPT_STATUS_READ_EN_RD_VALID_MISMATCH		BIT(9)
#define SWRM_INTERRUPT_STATUS_SPECIAL_CMD_ID_FINISHED		BIT(10)
#define SWRM_INTERRUPT_STATUS_BUS_RESET_FINISHED_V2             BIT(13)
#define SWRM_INTERRUPT_STATUS_CLK_STOP_FINISHED_V2              BIT(14)
#define SWRM_INTERRUPT_STATUS_EXT_CLK_STOP_WAKEUP               BIT(16)
#define SWRM_INTERRUPT_MAX					17
#define SWRM_INTERRUPT_MASK_ADDR				0x204
#define SWRM_INTERRUPT_CLEAR					0x208
#define SWRM_INTERRUPT_CPU_EN					0x210
#define SWRM_CMD_FIFO_WR_CMD					0x300
#define SWRM_CMD_FIFO_RD_CMD					0x304
#define SWRM_CMD_FIFO_CMD					0x308
#define SWRM_CMD_FIFO_FLUSH					0x1
#define SWRM_CMD_FIFO_STATUS					0x30C
#define SWRM_RD_CMD_FIFO_CNT_MASK				GENMASK(20, 16)
#define SWRM_WR_CMD_FIFO_CNT_MASK				GENMASK(12, 8)
#define SWRM_CMD_FIFO_CFG_ADDR					0x314
#define SWRM_CONTINUE_EXEC_ON_CMD_IGNORE			BIT(31)
#define SWRM_RD_WR_CMD_RETRIES					0x7
#define SWRM_CMD_FIFO_RD_FIFO_ADDR				0x318
#define SWRM_RD_FIFO_CMD_ID_MASK				GENMASK(11, 8)
#define SWRM_ENUMERATOR_CFG_ADDR				0x500
#define SWRM_ENUMERATOR_SLAVE_DEV_ID_1(m)		(0x530 + 0x8 * (m))
#define SWRM_ENUMERATOR_SLAVE_DEV_ID_2(m)		(0x534 + 0x8 * (m))
#define SWRM_MCP_FRAME_CTRL_BANK_ADDR(m)		(0x101C + 0x40 * (m))
#define SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_BMSK			GENMASK(2, 0)
#define SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_BMSK			GENMASK(7, 3)
#define SWRM_MCP_BUS_CTRL					0x1044
#define SWRM_MCP_BUS_CLK_START					BIT(1)
#define SWRM_MCP_CFG_ADDR					0x1048
#define SWRM_MCP_CFG_MAX_NUM_OF_CMD_NO_PINGS_BMSK		GENMASK(21, 17)
#define SWRM_DEF_CMD_NO_PINGS					0x1f
#define SWRM_MCP_STATUS						0x104C
#define SWRM_MCP_STATUS_BANK_NUM_MASK				BIT(0)
#define SWRM_MCP_SLV_STATUS					0x1090
#define SWRM_MCP_SLV_STATUS_MASK				GENMASK(1, 0)
#define SWRM_MCP_SLV_STATUS_SZ					2
#define SWRM_DP_PORT_CTRL_BANK(n, m)	(0x1124 + 0x100 * (n - 1) + 0x40 * m)
#define SWRM_DP_PORT_CTRL_2_BANK(n, m)	(0x1128 + 0x100 * (n - 1) + 0x40 * m)
#define SWRM_DP_BLOCK_CTRL_1(n)		(0x112C + 0x100 * (n - 1))
#define SWRM_DP_BLOCK_CTRL2_BANK(n, m)	(0x1130 + 0x100 * (n - 1) + 0x40 * m)
#define SWRM_DP_PORT_HCTRL_BANK(n, m)	(0x1134 + 0x100 * (n - 1) + 0x40 * m)
#define SWRM_DP_BLOCK_CTRL3_BANK(n, m)	(0x1138 + 0x100 * (n - 1) + 0x40 * m)
#define SWRM_DIN_DPn_PCM_PORT_CTRL(n)	(0x1054 + 0x100 * (n - 1))

#define SWRM_DP_PORT_CTRL_EN_CHAN_SHFT				0x18
#define SWRM_DP_PORT_CTRL_OFFSET2_SHFT				0x10
#define SWRM_DP_PORT_CTRL_OFFSET1_SHFT				0x08
#define SWRM_AHB_BRIDGE_WR_DATA_0				0xc85
#define SWRM_AHB_BRIDGE_WR_ADDR_0				0xc89
#define SWRM_AHB_BRIDGE_RD_ADDR_0				0xc8d
#define SWRM_AHB_BRIDGE_RD_DATA_0				0xc91

#define SWRM_REG_VAL_PACK(data, dev, id, reg)	\
			((reg) | ((id) << 16) | ((dev) << 20) | ((data) << 24))

#define SWRM_SPECIAL_CMD_ID	0xF
#define MAX_FREQ_NUM		1
#define TIMEOUT_MS		(2 * HZ)
#define QCOM_SWRM_MAX_RD_LEN	0x1
#define QCOM_SDW_MAX_PORTS	14
#define DEFAULT_CLK_FREQ	9600000
#define SWRM_MAX_DAIS		0xF
#define SWR_INVALID_PARAM 0xFF
#define SWR_HSTOP_MAX_VAL 0xF
#define SWR_HSTART_MIN_VAL 0x0
#define SWR_BROADCAST_CMD_ID    0x0F
#define SWR_MAX_CMD_ID	14
#define MAX_FIFO_RD_RETRY 3
#define SWR_OVERFLOW_RETRY_COUNT 30

struct qcom_swrm_port_config {
	u8 si;
	u8 off1;
	u8 off2;
	u8 bp_mode;
	u8 hstart;
	u8 hstop;
	u8 word_length;
	u8 blk_group_count;
	u8 lane_control;
};

struct qcom_swrm_ctrl {
	struct sdw_bus bus;
	struct device *dev;
	struct regmap *regmap;
	void __iomem *mmio;
	struct completion broadcast;
	struct completion enumeration;
	struct work_struct slave_work;
	/* Port alloc/free lock */
	struct mutex port_lock;
	struct clk *hclk;
	u8 wr_cmd_id;
	u8 rd_cmd_id;
	int irq;
	unsigned int version;
	int num_din_ports;
	int num_dout_ports;
	int cols_index;
	int rows_index;
	unsigned long dout_port_mask;
	unsigned long din_port_mask;
	u32 intr_mask;
	u8 rcmd_id;
	u8 wcmd_id;
	struct qcom_swrm_port_config pconfig[QCOM_SDW_MAX_PORTS];
	struct sdw_stream_runtime *sruntime[SWRM_MAX_DAIS];
	enum sdw_slave_status status[SDW_MAX_DEVICES];
	int (*reg_read)(struct qcom_swrm_ctrl *ctrl, int reg, u32 *val);
	int (*reg_write)(struct qcom_swrm_ctrl *ctrl, int reg, int val);
	u32 slave_status;
	u32 wr_fifo_depth;
	u32 rd_fifo_depth;
};

struct qcom_swrm_data {
	u32 default_cols;
	u32 default_rows;
};

static struct qcom_swrm_data swrm_v1_3_data = {
	.default_rows = 48,
	.default_cols = 16,
};

static struct qcom_swrm_data swrm_v1_5_data = {
	.default_rows = 50,
	.default_cols = 16,
};

#define to_qcom_sdw(b)	container_of(b, struct qcom_swrm_ctrl, bus)

static int qcom_swrm_ahb_reg_read(struct qcom_swrm_ctrl *ctrl, int reg,
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

static int qcom_swrm_cpu_reg_read(struct qcom_swrm_ctrl *ctrl, int reg,
				  u32 *val)
{
	*val = readl(ctrl->mmio + reg);
	return SDW_CMD_OK;
}

static int qcom_swrm_cpu_reg_write(struct qcom_swrm_ctrl *ctrl, int reg,
				   int val)
{
	writel(val, ctrl->mmio + reg);
	return SDW_CMD_OK;
}

static u32 swrm_get_packed_reg_val(u8 *cmd_id, u8 cmd_data,
				   u8 dev_addr, u16 reg_addr)
{
	u32 val;
	u8 id = *cmd_id;

	if (id != SWR_BROADCAST_CMD_ID) {
		if (id < SWR_MAX_CMD_ID)
			id += 1;
		else
			id = 0;
		*cmd_id = id;
	}
	val = SWRM_REG_VAL_PACK(cmd_data, dev_addr, id, reg_addr);

	return val;
}

static int swrm_wait_for_rd_fifo_avail(struct qcom_swrm_ctrl *swrm)
{
	u32 fifo_outstanding_data, value;
	int fifo_retry_count = SWR_OVERFLOW_RETRY_COUNT;

	do {
		/* Check for fifo underflow during read */
		swrm->reg_read(swrm, SWRM_CMD_FIFO_STATUS, &value);
		fifo_outstanding_data = FIELD_GET(SWRM_RD_CMD_FIFO_CNT_MASK, value);

		/* Check if read data is available in read fifo */
		if (fifo_outstanding_data > 0)
			return 0;

		usleep_range(500, 510);
	} while (fifo_retry_count--);

	if (fifo_outstanding_data == 0) {
		dev_err_ratelimited(swrm->dev, "%s err read underflow\n", __func__);
		return -EIO;
	}

	return 0;
}

static int swrm_wait_for_wr_fifo_avail(struct qcom_swrm_ctrl *swrm)
{
	u32 fifo_outstanding_cmds, value;
	int fifo_retry_count = SWR_OVERFLOW_RETRY_COUNT;

	do {
		/* Check for fifo overflow during write */
		swrm->reg_read(swrm, SWRM_CMD_FIFO_STATUS, &value);
		fifo_outstanding_cmds = FIELD_GET(SWRM_WR_CMD_FIFO_CNT_MASK, value);

		/* Check for space in write fifo before writing */
		if (fifo_outstanding_cmds < swrm->wr_fifo_depth)
			return 0;

		usleep_range(500, 510);
	} while (fifo_retry_count--);

	if (fifo_outstanding_cmds == swrm->wr_fifo_depth) {
		dev_err_ratelimited(swrm->dev, "%s err write overflow\n", __func__);
		return -EIO;
	}

	return 0;
}

static int qcom_swrm_cmd_fifo_wr_cmd(struct qcom_swrm_ctrl *swrm, u8 cmd_data,
				     u8 dev_addr, u16 reg_addr)
{

	u32 val;
	int ret = 0;
	u8 cmd_id = 0x0;

	if (dev_addr == SDW_BROADCAST_DEV_NUM) {
		cmd_id = SWR_BROADCAST_CMD_ID;
		val = swrm_get_packed_reg_val(&cmd_id, cmd_data,
					      dev_addr, reg_addr);
	} else {
		val = swrm_get_packed_reg_val(&swrm->wcmd_id, cmd_data,
					      dev_addr, reg_addr);
	}

	if (swrm_wait_for_wr_fifo_avail(swrm))
		return SDW_CMD_FAIL_OTHER;

	/* Its assumed that write is okay as we do not get any status back */
	swrm->reg_write(swrm, SWRM_CMD_FIFO_WR_CMD, val);

	/* version 1.3 or less */
	if (swrm->version <= 0x01030000)
		usleep_range(150, 155);

	if (cmd_id == SWR_BROADCAST_CMD_ID) {
		/*
		 * sleep for 10ms for MSM soundwire variant to allow broadcast
		 * command to complete.
		 */
		ret = wait_for_completion_timeout(&swrm->broadcast,
						  msecs_to_jiffies(TIMEOUT_MS));
		if (!ret)
			ret = SDW_CMD_IGNORED;
		else
			ret = SDW_CMD_OK;

	} else {
		ret = SDW_CMD_OK;
	}
	return ret;
}

static int qcom_swrm_cmd_fifo_rd_cmd(struct qcom_swrm_ctrl *swrm,
				     u8 dev_addr, u16 reg_addr,
				     u32 len, u8 *rval)
{
	u32 cmd_data, cmd_id, val, retry_attempt = 0;

	val = swrm_get_packed_reg_val(&swrm->rcmd_id, len, dev_addr, reg_addr);

	/* wait for FIFO RD to complete to avoid overflow */
	usleep_range(100, 105);
	swrm->reg_write(swrm, SWRM_CMD_FIFO_RD_CMD, val);
	/* wait for FIFO RD CMD complete to avoid overflow */
	usleep_range(250, 255);

	if (swrm_wait_for_rd_fifo_avail(swrm))
		return SDW_CMD_FAIL_OTHER;

	do {
		swrm->reg_read(swrm, SWRM_CMD_FIFO_RD_FIFO_ADDR, &cmd_data);
		rval[0] = cmd_data & 0xFF;
		cmd_id = FIELD_GET(SWRM_RD_FIFO_CMD_ID_MASK, cmd_data);

		if (cmd_id != swrm->rcmd_id) {
			if (retry_attempt < (MAX_FIFO_RD_RETRY - 1)) {
				/* wait 500 us before retry on fifo read failure */
				usleep_range(500, 505);
				swrm->reg_write(swrm, SWRM_CMD_FIFO_CMD,
						SWRM_CMD_FIFO_FLUSH);
				swrm->reg_write(swrm, SWRM_CMD_FIFO_RD_CMD, val);
			}
			retry_attempt++;
		} else {
			return SDW_CMD_OK;
		}

	} while (retry_attempt < MAX_FIFO_RD_RETRY);

	dev_err(swrm->dev, "failed to read fifo: reg: 0x%x, rcmd_id: 0x%x,\
		dev_num: 0x%x, cmd_data: 0x%x\n",
		reg_addr, swrm->rcmd_id, dev_addr, cmd_data);

	return SDW_CMD_IGNORED;
}

static int qcom_swrm_get_alert_slave_dev_num(struct qcom_swrm_ctrl *ctrl)
{
	u32 val, status;
	int dev_num;

	ctrl->reg_read(ctrl, SWRM_MCP_SLV_STATUS, &val);

	for (dev_num = 0; dev_num < SDW_MAX_DEVICES; dev_num++) {
		status = (val >> (dev_num * SWRM_MCP_SLV_STATUS_SZ));

		if ((status & SWRM_MCP_SLV_STATUS_MASK) == SDW_SLAVE_ALERT) {
			ctrl->status[dev_num] = status;
			return dev_num;
		}
	}

	return -EINVAL;
}

static void qcom_swrm_get_device_status(struct qcom_swrm_ctrl *ctrl)
{
	u32 val;
	int i;

	ctrl->reg_read(ctrl, SWRM_MCP_SLV_STATUS, &val);
	ctrl->slave_status = val;

	for (i = 0; i < SDW_MAX_DEVICES; i++) {
		u32 s;

		s = (val >> (i * 2));
		s &= SWRM_MCP_SLV_STATUS_MASK;
		ctrl->status[i] = s;
	}
}

static void qcom_swrm_set_slave_dev_num(struct sdw_bus *bus,
					struct sdw_slave *slave, int devnum)
{
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	u32 status;

	ctrl->reg_read(ctrl, SWRM_MCP_SLV_STATUS, &status);
	status = (status >> (devnum * SWRM_MCP_SLV_STATUS_SZ));
	status &= SWRM_MCP_SLV_STATUS_MASK;

	if (status == SDW_SLAVE_ATTACHED) {
		if (slave)
			slave->dev_num = devnum;
		mutex_lock(&bus->bus_lock);
		set_bit(devnum, bus->assigned);
		mutex_unlock(&bus->bus_lock);
	}
}

static int qcom_swrm_enumerate(struct sdw_bus *bus)
{
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	struct sdw_slave *slave, *_s;
	struct sdw_slave_id id;
	u32 val1, val2;
	bool found;
	u64 addr;
	int i;
	char *buf1 = (char *)&val1, *buf2 = (char *)&val2;

	for (i = 1; i <= SDW_MAX_DEVICES; i++) {
		/*SCP_Devid5 - Devid 4*/
		ctrl->reg_read(ctrl, SWRM_ENUMERATOR_SLAVE_DEV_ID_1(i), &val1);

		/*SCP_Devid3 - DevId 2 Devid 1 Devid 0*/
		ctrl->reg_read(ctrl, SWRM_ENUMERATOR_SLAVE_DEV_ID_2(i), &val2);

		if (!val1 && !val2)
			break;

		addr = buf2[1] | (buf2[0] << 8) | (buf1[3] << 16) |
			((u64)buf1[2] << 24) | ((u64)buf1[1] << 32) |
			((u64)buf1[0] << 40);

		sdw_extract_slave_id(bus, addr, &id);
		found = false;
		/* Now compare with entries */
		list_for_each_entry_safe(slave, _s, &bus->slaves, node) {
			if (sdw_compare_devid(slave, id) == 0) {
				qcom_swrm_set_slave_dev_num(bus, slave, i);
				found = true;
				break;
			}
		}

		if (!found) {
			qcom_swrm_set_slave_dev_num(bus, NULL, i);
			sdw_slave_add(bus, &id, NULL);
		}
	}

	complete(&ctrl->enumeration);
	return 0;
}

static irqreturn_t qcom_swrm_irq_handler(int irq, void *dev_id)
{
	struct qcom_swrm_ctrl *swrm = dev_id;
	u32 value, intr_sts, intr_sts_masked, slave_status;
	u32 i;
	int devnum;
	int ret = IRQ_HANDLED;

	swrm->reg_read(swrm, SWRM_INTERRUPT_STATUS, &intr_sts);
	intr_sts_masked = intr_sts & swrm->intr_mask;

	do {
		for (i = 0; i < SWRM_INTERRUPT_MAX; i++) {
			value = intr_sts_masked & BIT(i);
			if (!value)
				continue;

			switch (value) {
			case SWRM_INTERRUPT_STATUS_SLAVE_PEND_IRQ:
				devnum = qcom_swrm_get_alert_slave_dev_num(swrm);
				if (devnum < 0) {
					dev_err_ratelimited(swrm->dev,
					    "no slave alert found.spurious interrupt\n");
				} else {
					sdw_handle_slave_status(&swrm->bus, swrm->status);
				}

				break;
			case SWRM_INTERRUPT_STATUS_NEW_SLAVE_ATTACHED:
			case SWRM_INTERRUPT_STATUS_CHANGE_ENUM_SLAVE_STATUS:
				dev_err_ratelimited(swrm->dev, "%s: SWR new slave attached\n",
					__func__);
				swrm->reg_read(swrm, SWRM_MCP_SLV_STATUS, &slave_status);
				if (swrm->slave_status == slave_status) {
					dev_err(swrm->dev, "Slave status not changed %x\n",
						slave_status);
				} else {
					qcom_swrm_get_device_status(swrm);
					qcom_swrm_enumerate(&swrm->bus);
					sdw_handle_slave_status(&swrm->bus, swrm->status);
				}
				break;
			case SWRM_INTERRUPT_STATUS_MASTER_CLASH_DET:
				dev_err_ratelimited(swrm->dev,
						"%s: SWR bus clsh detected\n",
						__func__);
				swrm->intr_mask &= ~SWRM_INTERRUPT_STATUS_MASTER_CLASH_DET;
				swrm->reg_write(swrm, SWRM_INTERRUPT_CPU_EN, swrm->intr_mask);
				break;
			case SWRM_INTERRUPT_STATUS_RD_FIFO_OVERFLOW:
				swrm->reg_read(swrm, SWRM_CMD_FIFO_STATUS, &value);
				dev_err_ratelimited(swrm->dev,
					"%s: SWR read FIFO overflow fifo status 0x%x\n",
					__func__, value);
				break;
			case SWRM_INTERRUPT_STATUS_RD_FIFO_UNDERFLOW:
				swrm->reg_read(swrm, SWRM_CMD_FIFO_STATUS, &value);
				dev_err_ratelimited(swrm->dev,
					"%s: SWR read FIFO underflow fifo status 0x%x\n",
					__func__, value);
				break;
			case SWRM_INTERRUPT_STATUS_WR_CMD_FIFO_OVERFLOW:
				swrm->reg_read(swrm, SWRM_CMD_FIFO_STATUS, &value);
				dev_err(swrm->dev,
					"%s: SWR write FIFO overflow fifo status %x\n",
					__func__, value);
				swrm->reg_write(swrm, SWRM_CMD_FIFO_CMD, 0x1);
				break;
			case SWRM_INTERRUPT_STATUS_CMD_ERROR:
				swrm->reg_read(swrm, SWRM_CMD_FIFO_STATUS, &value);
				dev_err_ratelimited(swrm->dev,
					"%s: SWR CMD error, fifo status 0x%x, flushing fifo\n",
					__func__, value);
				swrm->reg_write(swrm, SWRM_CMD_FIFO_CMD, 0x1);
				break;
			case SWRM_INTERRUPT_STATUS_DOUT_PORT_COLLISION:
				dev_err_ratelimited(swrm->dev,
						"%s: SWR Port collision detected\n",
						__func__);
				swrm->intr_mask &= ~SWRM_INTERRUPT_STATUS_DOUT_PORT_COLLISION;
				swrm->reg_write(swrm,
					SWRM_INTERRUPT_CPU_EN, swrm->intr_mask);
				break;
			case SWRM_INTERRUPT_STATUS_READ_EN_RD_VALID_MISMATCH:
				dev_err_ratelimited(swrm->dev,
					"%s: SWR read enable valid mismatch\n",
					__func__);
				swrm->intr_mask &=
					~SWRM_INTERRUPT_STATUS_READ_EN_RD_VALID_MISMATCH;
				swrm->reg_write(swrm,
					SWRM_INTERRUPT_CPU_EN, swrm->intr_mask);
				break;
			case SWRM_INTERRUPT_STATUS_SPECIAL_CMD_ID_FINISHED:
				complete(&swrm->broadcast);
				break;
			case SWRM_INTERRUPT_STATUS_BUS_RESET_FINISHED_V2:
				break;
			case SWRM_INTERRUPT_STATUS_CLK_STOP_FINISHED_V2:
				break;
			case SWRM_INTERRUPT_STATUS_EXT_CLK_STOP_WAKEUP:
				break;
			default:
				dev_err_ratelimited(swrm->dev,
						"%s: SWR unknown interrupt value: %d\n",
						__func__, value);
				ret = IRQ_NONE;
				break;
			}
		}
		swrm->reg_write(swrm, SWRM_INTERRUPT_CLEAR, intr_sts);
		swrm->reg_read(swrm, SWRM_INTERRUPT_STATUS, &intr_sts);
		intr_sts_masked = intr_sts & swrm->intr_mask;
	} while (intr_sts_masked);

	return ret;
}

static int qcom_swrm_init(struct qcom_swrm_ctrl *ctrl)
{
	u32 val;

	/* Clear Rows and Cols */
	val = FIELD_PREP(SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_BMSK, ctrl->rows_index);
	val |= FIELD_PREP(SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_BMSK, ctrl->cols_index);

	ctrl->reg_write(ctrl, SWRM_MCP_FRAME_CTRL_BANK_ADDR(0), val);

	/* Enable Auto enumeration */
	ctrl->reg_write(ctrl, SWRM_ENUMERATOR_CFG_ADDR, 1);

	ctrl->intr_mask = SWRM_INTERRUPT_STATUS_RMSK;
	/* Mask soundwire interrupts */
	ctrl->reg_write(ctrl, SWRM_INTERRUPT_MASK_ADDR,
			SWRM_INTERRUPT_STATUS_RMSK);

	/* Configure No pings */
	ctrl->reg_read(ctrl, SWRM_MCP_CFG_ADDR, &val);
	u32p_replace_bits(&val, SWRM_DEF_CMD_NO_PINGS, SWRM_MCP_CFG_MAX_NUM_OF_CMD_NO_PINGS_BMSK);
	ctrl->reg_write(ctrl, SWRM_MCP_CFG_ADDR, val);

	ctrl->reg_write(ctrl, SWRM_MCP_BUS_CTRL, SWRM_MCP_BUS_CLK_START);
	/* Configure number of retries of a read/write cmd */
	if (ctrl->version > 0x01050001) {
		/* Only for versions >= 1.5.1 */
		ctrl->reg_write(ctrl, SWRM_CMD_FIFO_CFG_ADDR,
				SWRM_RD_WR_CMD_RETRIES |
				SWRM_CONTINUE_EXEC_ON_CMD_IGNORE);
	} else {
		ctrl->reg_write(ctrl, SWRM_CMD_FIFO_CFG_ADDR,
				SWRM_RD_WR_CMD_RETRIES);
	}

	/* Set IRQ to PULSE */
	ctrl->reg_write(ctrl, SWRM_COMP_CFG_ADDR,
			SWRM_COMP_CFG_IRQ_LEVEL_OR_PULSE_MSK |
			SWRM_COMP_CFG_ENABLE_MSK);

	/* enable CPU IRQs */
	if (ctrl->mmio) {
		ctrl->reg_write(ctrl, SWRM_INTERRUPT_CPU_EN,
				SWRM_INTERRUPT_STATUS_RMSK);
	}
	ctrl->slave_status = 0;
	ctrl->reg_read(ctrl, SWRM_COMP_PARAMS, &val);
	ctrl->rd_fifo_depth = FIELD_GET(SWRM_COMP_PARAMS_RD_FIFO_DEPTH, val);
	ctrl->wr_fifo_depth = FIELD_GET(SWRM_COMP_PARAMS_WR_FIFO_DEPTH, val);

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

	u32p_replace_bits(&val, ctrl->cols_index, SWRM_MCP_FRAME_CTRL_BANK_COL_CTRL_BMSK);
	u32p_replace_bits(&val, ctrl->rows_index, SWRM_MCP_FRAME_CTRL_BANK_ROW_CTRL_BMSK);

	return ctrl->reg_write(ctrl, reg, val);
}

static int qcom_swrm_port_params(struct sdw_bus *bus,
				 struct sdw_port_params *p_params,
				 unsigned int bank)
{
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);

	return ctrl->reg_write(ctrl, SWRM_DP_BLOCK_CTRL_1(p_params->num),
			       p_params->bps - 1);

}

static int qcom_swrm_transport_params(struct sdw_bus *bus,
				      struct sdw_transport_params *params,
				      enum sdw_reg_bank bank)
{
	struct qcom_swrm_ctrl *ctrl = to_qcom_sdw(bus);
	struct qcom_swrm_port_config *pcfg;
	u32 value;
	int reg = SWRM_DP_PORT_CTRL_BANK((params->port_num), bank);
	int ret;

	pcfg = &ctrl->pconfig[params->port_num];

	value = pcfg->off1 << SWRM_DP_PORT_CTRL_OFFSET1_SHFT;
	value |= pcfg->off2 << SWRM_DP_PORT_CTRL_OFFSET2_SHFT;
	value |= pcfg->si;

	ret = ctrl->reg_write(ctrl, reg, value);
	if (ret)
		goto err;

	if (pcfg->lane_control != SWR_INVALID_PARAM) {
		reg = SWRM_DP_PORT_CTRL_2_BANK(params->port_num, bank);
		value = pcfg->lane_control;
		ret = ctrl->reg_write(ctrl, reg, value);
		if (ret)
			goto err;
	}

	if (pcfg->blk_group_count != SWR_INVALID_PARAM) {
		reg = SWRM_DP_BLOCK_CTRL2_BANK(params->port_num, bank);
		value = pcfg->blk_group_count;
		ret = ctrl->reg_write(ctrl, reg, value);
		if (ret)
			goto err;
	}

	if (pcfg->hstart != SWR_INVALID_PARAM
			&& pcfg->hstop != SWR_INVALID_PARAM) {
		reg = SWRM_DP_PORT_HCTRL_BANK(params->port_num, bank);
		value = (pcfg->hstop << 4) | pcfg->hstart;
		ret = ctrl->reg_write(ctrl, reg, value);
	} else {
		reg = SWRM_DP_PORT_HCTRL_BANK(params->port_num, bank);
		value = (SWR_HSTOP_MAX_VAL << 4) | SWR_HSTART_MIN_VAL;
		ret = ctrl->reg_write(ctrl, reg, value);
	}

	if (ret)
		goto err;

	if (pcfg->bp_mode != SWR_INVALID_PARAM) {
		reg = SWRM_DP_BLOCK_CTRL3_BANK(params->port_num, bank);
		ret = ctrl->reg_write(ctrl, reg, pcfg->bp_mode);
	}

err:
	return ret;
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

static const struct sdw_master_port_ops qcom_swrm_port_ops = {
	.dpn_set_port_params = qcom_swrm_port_params,
	.dpn_set_port_transport_params = qcom_swrm_transport_params,
	.dpn_port_enable_ch = qcom_swrm_port_enable,
};

static const struct sdw_master_ops qcom_swrm_ops = {
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
	struct sdw_slave *slave;
	unsigned int m_port;
	int i = 1;

	list_for_each_entry(m_rt, &bus->m_rt_list, bus_node) {
		list_for_each_entry(p_rt, &m_rt->port_list, port_node) {
			pcfg = &ctrl->pconfig[p_rt->num];
			p_rt->transport_params.port_num = p_rt->num;
			if (pcfg->word_length != SWR_INVALID_PARAM) {
				sdw_fill_port_params(&p_rt->port_params,
					     p_rt->num,  pcfg->word_length + 1,
					     SDW_PORT_FLOW_MODE_ISOCH,
					     SDW_PORT_DATA_MODE_NORMAL);
			}

		}

		list_for_each_entry(s_rt, &m_rt->slave_rt_list, m_rt_node) {
			slave = s_rt->slave;
			list_for_each_entry(p_rt, &s_rt->port_list, port_node) {
				m_port = slave->m_port_map[p_rt->num];
				/* port config starts at offset 0 so -1 from actual port number */
				if (m_port)
					pcfg = &ctrl->pconfig[m_port];
				else
					pcfg = &ctrl->pconfig[i];
				p_rt->transport_params.port_num = p_rt->num;
				p_rt->transport_params.sample_interval =
					pcfg->si + 1;
				p_rt->transport_params.offset1 = pcfg->off1;
				p_rt->transport_params.offset2 = pcfg->off2;
				p_rt->transport_params.blk_pkg_mode = pcfg->bp_mode;
				p_rt->transport_params.blk_grp_ctrl = pcfg->blk_group_count;

				p_rt->transport_params.hstart = pcfg->hstart;
				p_rt->transport_params.hstop = pcfg->hstop;
				p_rt->transport_params.lane_ctrl = pcfg->lane_control;
				if (pcfg->word_length != SWR_INVALID_PARAM) {
					sdw_fill_port_params(&p_rt->port_params,
						     p_rt->num,
						     pcfg->word_length + 1,
						     SDW_PORT_FLOW_MODE_ISOCH,
						     SDW_PORT_DATA_MODE_NORMAL);
				}
				i++;
			}
		}
	}

	return 0;
}

static u32 qcom_swrm_freq_tbl[MAX_FREQ_NUM] = {
	DEFAULT_CLK_FREQ,
};

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
			clear_bit(p_rt->num, port_mask);
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
	struct sdw_slave *slave;
	unsigned long *port_mask;
	int i, maxport, pn, nports = 0, ret = 0;
	unsigned int m_port;

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
			slave = s_rt->slave;
			list_for_each_entry(p_rt, &s_rt->port_list, port_node) {
				m_port = slave->m_port_map[p_rt->num];
				/* Port numbers start from 1 - 14*/
				if (m_port)
					pn = m_port;
				else
					pn = find_first_zero_bit(port_mask, maxport);

				if (pn > maxport) {
					dev_err(ctrl->dev, "All ports busy\n");
					ret = -EBUSY;
					goto err;
				}
				set_bit(pn, port_mask);
				pconfig[nports].num = pn;
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
			clear_bit(pconfig[i].num, port_mask);
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
			dev_err(dai->dev, "Failed to set sdw stream on %s\n",
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
	u8 bp_mode[QCOM_SDW_MAX_PORTS] = { 0, };
	u8 hstart[QCOM_SDW_MAX_PORTS];
	u8 hstop[QCOM_SDW_MAX_PORTS];
	u8 word_length[QCOM_SDW_MAX_PORTS];
	u8 blk_group_count[QCOM_SDW_MAX_PORTS];
	u8 lane_control[QCOM_SDW_MAX_PORTS];
	int i, ret, nports, val;

	ctrl->reg_read(ctrl, SWRM_COMP_PARAMS, &val);

	ctrl->num_dout_ports = FIELD_GET(SWRM_COMP_PARAMS_DOUT_PORTS_MASK, val);
	ctrl->num_din_ports = FIELD_GET(SWRM_COMP_PARAMS_DIN_PORTS_MASK, val);

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
	/* Valid port numbers are from 1-14, so mask out port 0 explicitly */
	set_bit(0, &ctrl->dout_port_mask);
	set_bit(0, &ctrl->din_port_mask);

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

	ret = of_property_read_u8_array(np, "qcom,ports-block-pack-mode",
					bp_mode, nports);
	if (ret)
		return ret;

	memset(hstart, SWR_INVALID_PARAM, QCOM_SDW_MAX_PORTS);
	of_property_read_u8_array(np, "qcom,ports-hstart", hstart, nports);

	memset(hstop, SWR_INVALID_PARAM, QCOM_SDW_MAX_PORTS);
	of_property_read_u8_array(np, "qcom,ports-hstop", hstop, nports);

	memset(word_length, SWR_INVALID_PARAM, QCOM_SDW_MAX_PORTS);
	of_property_read_u8_array(np, "qcom,ports-word-length", word_length, nports);

	memset(blk_group_count, SWR_INVALID_PARAM, QCOM_SDW_MAX_PORTS);
	of_property_read_u8_array(np, "qcom,ports-block-group-count", blk_group_count, nports);

	memset(lane_control, SWR_INVALID_PARAM, QCOM_SDW_MAX_PORTS);
	of_property_read_u8_array(np, "qcom,ports-lane-control", lane_control, nports);

	for (i = 0; i < nports; i++) {
		/* Valid port number range is from 1-14 */
		ctrl->pconfig[i + 1].si = si[i];
		ctrl->pconfig[i + 1].off1 = off1[i];
		ctrl->pconfig[i + 1].off2 = off2[i];
		ctrl->pconfig[i + 1].bp_mode = bp_mode[i];
		ctrl->pconfig[i + 1].hstart = hstart[i];
		ctrl->pconfig[i + 1].hstop = hstop[i];
		ctrl->pconfig[i + 1].word_length = word_length[i];
		ctrl->pconfig[i + 1].blk_group_count = blk_group_count[i];
		ctrl->pconfig[i + 1].lane_control = lane_control[i];
	}

	return 0;
}

static int qcom_swrm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sdw_master_prop *prop;
	struct sdw_bus_params *params;
	struct qcom_swrm_ctrl *ctrl;
	const struct qcom_swrm_data *data;
	int ret;
	u32 val;

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	data = of_device_get_match_data(dev);
	ctrl->rows_index = sdw_find_row_index(data->default_rows);
	ctrl->cols_index = sdw_find_col_index(data->default_cols);
#if IS_REACHABLE(CONFIG_SLIMBUS)
	if (dev->parent->bus == &slimbus_bus) {
#else
	if (false) {
#endif
		ctrl->reg_read = qcom_swrm_ahb_reg_read;
		ctrl->reg_write = qcom_swrm_ahb_reg_write;
		ctrl->regmap = dev_get_regmap(dev->parent, NULL);
		if (!ctrl->regmap)
			return -EINVAL;
	} else {
		ctrl->reg_read = qcom_swrm_cpu_reg_read;
		ctrl->reg_write = qcom_swrm_cpu_reg_write;
		ctrl->mmio = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(ctrl->mmio))
			return PTR_ERR(ctrl->mmio);
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
	mutex_init(&ctrl->port_lock);
	init_completion(&ctrl->broadcast);
	init_completion(&ctrl->enumeration);

	ctrl->bus.ops = &qcom_swrm_ops;
	ctrl->bus.port_ops = &qcom_swrm_port_ops;
	ctrl->bus.compute_params = &qcom_swrm_compute_params;

	ret = qcom_swrm_get_port_config(ctrl);
	if (ret)
		goto err_clk;

	params = &ctrl->bus.params;
	params->max_dr_freq = DEFAULT_CLK_FREQ;
	params->curr_dr_freq = DEFAULT_CLK_FREQ;
	params->col = data->default_cols;
	params->row = data->default_rows;
	ctrl->reg_read(ctrl, SWRM_MCP_STATUS, &val);
	params->curr_bank = val & SWRM_MCP_STATUS_BANK_NUM_MASK;
	params->next_bank = !params->curr_bank;

	prop = &ctrl->bus.prop;
	prop->max_clk_freq = DEFAULT_CLK_FREQ;
	prop->num_clk_gears = 0;
	prop->num_clk_freq = MAX_FREQ_NUM;
	prop->clk_freq = &qcom_swrm_freq_tbl[0];
	prop->default_col = data->default_cols;
	prop->default_row = data->default_rows;

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
	wait_for_completion_timeout(&ctrl->enumeration,
				    msecs_to_jiffies(TIMEOUT_MS));
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
	{ .compatible = "qcom,soundwire-v1.3.0", .data = &swrm_v1_3_data },
	{ .compatible = "qcom,soundwire-v1.5.1", .data = &swrm_v1_5_data },
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

// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * Cadence SoundWire Master module
 * Used by Master driver
 */

#include <linux/cleanup.h>
#include <linux/crc8.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <linux/workqueue.h>
#include "bus.h"
#include "cadence_master.h"

static int interrupt_mask;
module_param_named(cnds_mcp_int_mask, interrupt_mask, int, 0444);
MODULE_PARM_DESC(cdns_mcp_int_mask, "Cadence MCP IntMask");

#define CDNS_MCP_CONFIG				0x0
#define CDNS_MCP_CONFIG_BUS_REL			BIT(6)

#define CDNS_IP_MCP_CONFIG			0x0 /* IP offset added at run-time */

#define CDNS_IP_MCP_CONFIG_MCMD_RETRY		GENMASK(27, 24)
#define CDNS_IP_MCP_CONFIG_MPREQ_DELAY		GENMASK(20, 16)
#define CDNS_IP_MCP_CONFIG_MMASTER		BIT(7)
#define CDNS_IP_MCP_CONFIG_SNIFFER		BIT(5)
#define CDNS_IP_MCP_CONFIG_CMD			BIT(3)
#define CDNS_IP_MCP_CONFIG_OP			GENMASK(2, 0)
#define CDNS_IP_MCP_CONFIG_OP_NORMAL		0

#define CDNS_MCP_CONTROL			0x4

#define CDNS_MCP_CONTROL_CMD_RST		BIT(7)
#define CDNS_MCP_CONTROL_SOFT_RST		BIT(6)
#define CDNS_MCP_CONTROL_HW_RST			BIT(4)
#define CDNS_MCP_CONTROL_CLK_STOP_CLR		BIT(2)

#define CDNS_IP_MCP_CONTROL			0x4  /* IP offset added at run-time */

#define CDNS_IP_MCP_CONTROL_RST_DELAY		GENMASK(10, 8)
#define CDNS_IP_MCP_CONTROL_SW_RST		BIT(5)
#define CDNS_IP_MCP_CONTROL_CLK_PAUSE		BIT(3)
#define CDNS_IP_MCP_CONTROL_CMD_ACCEPT		BIT(1)
#define CDNS_IP_MCP_CONTROL_BLOCK_WAKEUP	BIT(0)

#define CDNS_IP_MCP_CMDCTRL			0x8 /* IP offset added at run-time */

#define CDNS_IP_MCP_CMDCTRL_INSERT_PARITY_ERR	BIT(2)

#define CDNS_MCP_SSPSTAT			0xC
#define CDNS_MCP_FRAME_SHAPE			0x10
#define CDNS_MCP_FRAME_SHAPE_INIT		0x14
#define CDNS_MCP_FRAME_SHAPE_COL_MASK		GENMASK(2, 0)
#define CDNS_MCP_FRAME_SHAPE_ROW_MASK		GENMASK(7, 3)

#define CDNS_MCP_CONFIG_UPDATE			0x18
#define CDNS_MCP_CONFIG_UPDATE_BIT		BIT(0)

#define CDNS_MCP_PHYCTRL			0x1C
#define CDNS_MCP_SSP_CTRL0			0x20
#define CDNS_MCP_SSP_CTRL1			0x28
#define CDNS_MCP_CLK_CTRL0			0x30
#define CDNS_MCP_CLK_CTRL1			0x38
#define CDNS_MCP_CLK_MCLKD_MASK		GENMASK(7, 0)

#define CDNS_MCP_STAT				0x40

#define CDNS_MCP_STAT_ACTIVE_BANK		BIT(20)
#define CDNS_MCP_STAT_CLK_STOP			BIT(16)

#define CDNS_MCP_INTSTAT			0x44
#define CDNS_MCP_INTMASK			0x48

#define CDNS_MCP_INT_IRQ			BIT(31)
#define CDNS_MCP_INT_RESERVED1			GENMASK(30, 17)
#define CDNS_MCP_INT_WAKEUP			BIT(16)
#define CDNS_MCP_INT_SLAVE_RSVD			BIT(15)
#define CDNS_MCP_INT_SLAVE_ALERT		BIT(14)
#define CDNS_MCP_INT_SLAVE_ATTACH		BIT(13)
#define CDNS_MCP_INT_SLAVE_NATTACH		BIT(12)
#define CDNS_MCP_INT_SLAVE_MASK			GENMASK(15, 12)
#define CDNS_MCP_INT_DPINT			BIT(11)
#define CDNS_MCP_INT_CTRL_CLASH			BIT(10)
#define CDNS_MCP_INT_DATA_CLASH			BIT(9)
#define CDNS_MCP_INT_PARITY			BIT(8)
#define CDNS_MCP_INT_CMD_ERR			BIT(7)
#define CDNS_MCP_INT_RESERVED2			GENMASK(6, 4)
#define CDNS_MCP_INT_RX_NE			BIT(3)
#define CDNS_MCP_INT_RX_WL			BIT(2)
#define CDNS_MCP_INT_TXE			BIT(1)
#define CDNS_MCP_INT_TXF			BIT(0)
#define CDNS_MCP_INT_RESERVED (CDNS_MCP_INT_RESERVED1 | CDNS_MCP_INT_RESERVED2)

#define CDNS_MCP_INTSET				0x4C

#define CDNS_MCP_SLAVE_STAT			0x50
#define CDNS_MCP_SLAVE_STAT_MASK		GENMASK(1, 0)

#define CDNS_MCP_SLAVE_INTSTAT0			0x54
#define CDNS_MCP_SLAVE_INTSTAT1			0x58
#define CDNS_MCP_SLAVE_INTSTAT_NPRESENT		BIT(0)
#define CDNS_MCP_SLAVE_INTSTAT_ATTACHED		BIT(1)
#define CDNS_MCP_SLAVE_INTSTAT_ALERT		BIT(2)
#define CDNS_MCP_SLAVE_INTSTAT_RESERVED		BIT(3)
#define CDNS_MCP_SLAVE_STATUS_BITS		GENMASK(3, 0)
#define CDNS_MCP_SLAVE_STATUS_NUM		4

#define CDNS_MCP_SLAVE_INTMASK0			0x5C
#define CDNS_MCP_SLAVE_INTMASK1			0x60

#define CDNS_MCP_SLAVE_INTMASK0_MASK		GENMASK(31, 0)
#define CDNS_MCP_SLAVE_INTMASK1_MASK		GENMASK(15, 0)

#define CDNS_MCP_PORT_INTSTAT			0x64
#define CDNS_MCP_PDI_STAT			0x6C

#define CDNS_MCP_FIFOLEVEL			0x78
#define CDNS_MCP_FIFOSTAT			0x7C
#define CDNS_MCP_RX_FIFO_AVAIL			GENMASK(5, 0)

#define CDNS_IP_MCP_CMD_BASE			0x80 /* IP offset added at run-time */
#define CDNS_IP_MCP_RESP_BASE			0x80 /* IP offset added at run-time */
/* FIFO can hold 8 commands */
#define CDNS_MCP_CMD_LEN			8
#define CDNS_MCP_CMD_WORD_LEN			0x4

#define CDNS_MCP_CMD_SSP_TAG			BIT(31)
#define CDNS_MCP_CMD_COMMAND			GENMASK(30, 28)
#define CDNS_MCP_CMD_DEV_ADDR			GENMASK(27, 24)
#define CDNS_MCP_CMD_REG_ADDR			GENMASK(23, 8)
#define CDNS_MCP_CMD_REG_DATA			GENMASK(7, 0)

#define CDNS_MCP_CMD_READ			2
#define CDNS_MCP_CMD_WRITE			3

#define CDNS_MCP_RESP_RDATA			GENMASK(15, 8)
#define CDNS_MCP_RESP_ACK			BIT(0)
#define CDNS_MCP_RESP_NACK			BIT(1)

#define CDNS_DP_SIZE				128

#define CDNS_DPN_B0_CONFIG(n)			(0x100 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B0_CH_EN(n)			(0x104 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B0_SAMPLE_CTRL(n)		(0x108 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B0_OFFSET_CTRL(n)		(0x10C + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B0_HCTRL(n)			(0x110 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B0_ASYNC_CTRL(n)		(0x114 + CDNS_DP_SIZE * (n))

#define CDNS_DPN_B1_CONFIG(n)			(0x118 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B1_CH_EN(n)			(0x11C + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B1_SAMPLE_CTRL(n)		(0x120 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B1_OFFSET_CTRL(n)		(0x124 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B1_HCTRL(n)			(0x128 + CDNS_DP_SIZE * (n))
#define CDNS_DPN_B1_ASYNC_CTRL(n)		(0x12C + CDNS_DP_SIZE * (n))

#define CDNS_DPN_CONFIG_BPM			BIT(18)
#define CDNS_DPN_CONFIG_BGC			GENMASK(17, 16)
#define CDNS_DPN_CONFIG_WL			GENMASK(12, 8)
#define CDNS_DPN_CONFIG_PORT_DAT		GENMASK(3, 2)
#define CDNS_DPN_CONFIG_PORT_FLOW		GENMASK(1, 0)

#define CDNS_DPN_SAMPLE_CTRL_SI			GENMASK(15, 0)

#define CDNS_DPN_OFFSET_CTRL_1			GENMASK(7, 0)
#define CDNS_DPN_OFFSET_CTRL_2			GENMASK(15, 8)

#define CDNS_DPN_HCTRL_HSTOP			GENMASK(3, 0)
#define CDNS_DPN_HCTRL_HSTART			GENMASK(7, 4)
#define CDNS_DPN_HCTRL_LCTRL			GENMASK(10, 8)

#define CDNS_PORTCTRL				0x130
#define CDNS_PORTCTRL_TEST_FAILED		BIT(1)
#define CDNS_PORTCTRL_DIRN			BIT(7)
#define CDNS_PORTCTRL_BANK_INVERT		BIT(8)
#define CDNS_PORTCTRL_BULK_ENABLE		BIT(16)

#define CDNS_PORT_OFFSET			0x80

#define CDNS_PDI_CONFIG(n)			(0x1100 + (n) * 16)

#define CDNS_PDI_CONFIG_SOFT_RESET		BIT(24)
#define CDNS_PDI_CONFIG_CHANNEL			GENMASK(15, 8)
#define CDNS_PDI_CONFIG_PORT			GENMASK(4, 0)

/* Driver defaults */
#define CDNS_TX_TIMEOUT				500

#define CDNS_SCP_RX_FIFOLEVEL			0x2

/*
 * register accessor helpers
 */
static inline u32 cdns_readl(struct sdw_cdns *cdns, int offset)
{
	return readl(cdns->registers + offset);
}

static inline void cdns_writel(struct sdw_cdns *cdns, int offset, u32 value)
{
	writel(value, cdns->registers + offset);
}

static inline u32 cdns_ip_readl(struct sdw_cdns *cdns, int offset)
{
	return cdns_readl(cdns, cdns->ip_offset + offset);
}

static inline void cdns_ip_writel(struct sdw_cdns *cdns, int offset, u32 value)
{
	return cdns_writel(cdns, cdns->ip_offset + offset, value);
}

static inline void cdns_updatel(struct sdw_cdns *cdns,
				int offset, u32 mask, u32 val)
{
	u32 tmp;

	tmp = cdns_readl(cdns, offset);
	tmp = (tmp & ~mask) | val;
	cdns_writel(cdns, offset, tmp);
}

static inline void cdns_ip_updatel(struct sdw_cdns *cdns,
				   int offset, u32 mask, u32 val)
{
	cdns_updatel(cdns, cdns->ip_offset + offset, mask, val);
}

static int cdns_set_wait(struct sdw_cdns *cdns, int offset, u32 mask, u32 value)
{
	int timeout = 10;
	u32 reg_read;

	/* Wait for bit to be set */
	do {
		reg_read = readl(cdns->registers + offset);
		if ((reg_read & mask) == value)
			return 0;

		timeout--;
		usleep_range(50, 100);
	} while (timeout != 0);

	return -ETIMEDOUT;
}

static int cdns_clear_bit(struct sdw_cdns *cdns, int offset, u32 value)
{
	writel(value, cdns->registers + offset);

	/* Wait for bit to be self cleared */
	return cdns_set_wait(cdns, offset, value, 0);
}

/*
 * all changes to the MCP_CONFIG, MCP_CONTROL, MCP_CMDCTRL and MCP_PHYCTRL
 * need to be confirmed with a write to MCP_CONFIG_UPDATE
 */
static int cdns_config_update(struct sdw_cdns *cdns)
{
	int ret;

	if (sdw_cdns_is_clock_stop(cdns)) {
		dev_err(cdns->dev, "Cannot program MCP_CONFIG_UPDATE in ClockStopMode\n");
		return -EINVAL;
	}

	ret = cdns_clear_bit(cdns, CDNS_MCP_CONFIG_UPDATE,
			     CDNS_MCP_CONFIG_UPDATE_BIT);
	if (ret < 0)
		dev_err(cdns->dev, "Config update timedout\n");

	return ret;
}

/**
 * sdw_cdns_config_update() - Update configurations
 * @cdns: Cadence instance
 */
void sdw_cdns_config_update(struct sdw_cdns *cdns)
{
	/* commit changes */
	cdns_writel(cdns, CDNS_MCP_CONFIG_UPDATE, CDNS_MCP_CONFIG_UPDATE_BIT);
}
EXPORT_SYMBOL(sdw_cdns_config_update);

/**
 * sdw_cdns_config_update_set_wait() - wait until configuration update bit is self-cleared
 * @cdns: Cadence instance
 */
int sdw_cdns_config_update_set_wait(struct sdw_cdns *cdns)
{
	/* the hardware recommendation is to wait at least 300us */
	return cdns_set_wait(cdns, CDNS_MCP_CONFIG_UPDATE,
			     CDNS_MCP_CONFIG_UPDATE_BIT, 0);
}
EXPORT_SYMBOL(sdw_cdns_config_update_set_wait);

/*
 * debugfs
 */
#ifdef CONFIG_DEBUG_FS

#define RD_BUF (2 * PAGE_SIZE)

static ssize_t cdns_sprintf(struct sdw_cdns *cdns,
			    char *buf, size_t pos, unsigned int reg)
{
	return scnprintf(buf + pos, RD_BUF - pos,
			 "%4x\t%8x\n", reg, cdns_readl(cdns, reg));
}

static int cdns_reg_show(struct seq_file *s, void *data)
{
	struct sdw_cdns *cdns = s->private;
	ssize_t ret;
	int num_ports;
	int i, j;

	char *buf __free(kfree) = kzalloc(RD_BUF, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = scnprintf(buf, RD_BUF, "Register  Value\n");
	ret += scnprintf(buf + ret, RD_BUF - ret, "\nMCP Registers\n");
	/* 8 MCP registers */
	for (i = CDNS_MCP_CONFIG; i <= CDNS_MCP_PHYCTRL; i += sizeof(u32))
		ret += cdns_sprintf(cdns, buf, ret, i);

	ret += scnprintf(buf + ret, RD_BUF - ret,
			 "\nStatus & Intr Registers\n");
	/* 13 Status & Intr registers (offsets 0x70 and 0x74 not defined) */
	for (i = CDNS_MCP_STAT; i <=  CDNS_MCP_FIFOSTAT; i += sizeof(u32))
		ret += cdns_sprintf(cdns, buf, ret, i);

	ret += scnprintf(buf + ret, RD_BUF - ret,
			 "\nSSP & Clk ctrl Registers\n");
	ret += cdns_sprintf(cdns, buf, ret, CDNS_MCP_SSP_CTRL0);
	ret += cdns_sprintf(cdns, buf, ret, CDNS_MCP_SSP_CTRL1);
	ret += cdns_sprintf(cdns, buf, ret, CDNS_MCP_CLK_CTRL0);
	ret += cdns_sprintf(cdns, buf, ret, CDNS_MCP_CLK_CTRL1);

	ret += scnprintf(buf + ret, RD_BUF - ret,
			 "\nDPn B0 Registers\n");

	num_ports = cdns->num_ports;

	for (i = 0; i < num_ports; i++) {
		ret += scnprintf(buf + ret, RD_BUF - ret,
				 "\nDP-%d\n", i);
		for (j = CDNS_DPN_B0_CONFIG(i);
		     j < CDNS_DPN_B0_ASYNC_CTRL(i); j += sizeof(u32))
			ret += cdns_sprintf(cdns, buf, ret, j);
	}

	ret += scnprintf(buf + ret, RD_BUF - ret,
			 "\nDPn B1 Registers\n");
	for (i = 0; i < num_ports; i++) {
		ret += scnprintf(buf + ret, RD_BUF - ret,
				 "\nDP-%d\n", i);

		for (j = CDNS_DPN_B1_CONFIG(i);
		     j < CDNS_DPN_B1_ASYNC_CTRL(i); j += sizeof(u32))
			ret += cdns_sprintf(cdns, buf, ret, j);
	}

	ret += scnprintf(buf + ret, RD_BUF - ret,
			 "\nDPn Control Registers\n");
	for (i = 0; i < num_ports; i++)
		ret += cdns_sprintf(cdns, buf, ret,
				CDNS_PORTCTRL + i * CDNS_PORT_OFFSET);

	ret += scnprintf(buf + ret, RD_BUF - ret,
			 "\nPDIn Config Registers\n");

	/* number of PDI and ports is interchangeable */
	for (i = 0; i < num_ports; i++)
		ret += cdns_sprintf(cdns, buf, ret, CDNS_PDI_CONFIG(i));

	seq_printf(s, "%s", buf);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cdns_reg);

static int cdns_hw_reset(void *data, u64 value)
{
	struct sdw_cdns *cdns = data;
	int ret;

	if (value != 1)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	ret = sdw_cdns_exit_reset(cdns);

	dev_dbg(cdns->dev, "link hw_reset done: %d\n", ret);

	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(cdns_hw_reset_fops, NULL, cdns_hw_reset, "%llu\n");

static int cdns_parity_error_injection(void *data, u64 value)
{
	struct sdw_cdns *cdns = data;
	struct sdw_bus *bus;
	int ret;

	if (value != 1)
		return -EINVAL;

	bus = &cdns->bus;

	/*
	 * Resume Master device. If this results in a bus reset, the
	 * Slave devices will re-attach and be re-enumerated.
	 */
	ret = pm_runtime_resume_and_get(bus->dev);
	if (ret < 0 && ret != -EACCES) {
		dev_err_ratelimited(cdns->dev,
				    "pm_runtime_resume_and_get failed in %s, ret %d\n",
				    __func__, ret);
		return ret;
	}

	/*
	 * wait long enough for Slave(s) to be in steady state. This
	 * does not need to be super precise.
	 */
	msleep(200);

	/*
	 * Take the bus lock here to make sure that any bus transactions
	 * will be queued while we inject a parity error on a dummy read
	 */
	mutex_lock(&bus->bus_lock);

	/* program hardware to inject parity error */
	cdns_ip_updatel(cdns, CDNS_IP_MCP_CMDCTRL,
			CDNS_IP_MCP_CMDCTRL_INSERT_PARITY_ERR,
			CDNS_IP_MCP_CMDCTRL_INSERT_PARITY_ERR);

	/* commit changes */
	ret = cdns_clear_bit(cdns, CDNS_MCP_CONFIG_UPDATE, CDNS_MCP_CONFIG_UPDATE_BIT);
	if (ret < 0)
		goto unlock;

	/* do a broadcast dummy read to avoid bus clashes */
	ret = sdw_bread_no_pm_unlocked(&cdns->bus, 0xf, SDW_SCP_DEVID_0);
	dev_info(cdns->dev, "parity error injection, read: %d\n", ret);

	/* program hardware to disable parity error */
	cdns_ip_updatel(cdns, CDNS_IP_MCP_CMDCTRL,
			CDNS_IP_MCP_CMDCTRL_INSERT_PARITY_ERR,
			0);

	/* commit changes */
	ret = cdns_clear_bit(cdns, CDNS_MCP_CONFIG_UPDATE, CDNS_MCP_CONFIG_UPDATE_BIT);
	if (ret < 0)
		goto unlock;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

unlock:
	/* Continue bus operation with parity error injection disabled */
	mutex_unlock(&bus->bus_lock);

	/*
	 * allow Master device to enter pm_runtime suspend. This may
	 * also result in Slave devices suspending.
	 */
	pm_runtime_mark_last_busy(bus->dev);
	pm_runtime_put_autosuspend(bus->dev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(cdns_parity_error_fops, NULL,
			 cdns_parity_error_injection, "%llu\n");

static int cdns_set_pdi_loopback_source(void *data, u64 value)
{
	struct sdw_cdns *cdns = data;
	unsigned int pdi_out_num = cdns->pcm.num_bd + cdns->pcm.num_out;

	if (value > pdi_out_num)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	cdns->pdi_loopback_source = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cdns_pdi_loopback_source_fops, NULL, cdns_set_pdi_loopback_source, "%llu\n");

static int cdns_set_pdi_loopback_target(void *data, u64 value)
{
	struct sdw_cdns *cdns = data;
	unsigned int pdi_in_num = cdns->pcm.num_bd + cdns->pcm.num_in;

	if (value > pdi_in_num)
		return -EINVAL;

	/* Userspace changed the hardware state behind the kernel's back */
	add_taint(TAINT_USER, LOCKDEP_STILL_OK);

	cdns->pdi_loopback_target = value;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(cdns_pdi_loopback_target_fops, NULL, cdns_set_pdi_loopback_target, "%llu\n");

/**
 * sdw_cdns_debugfs_init() - Cadence debugfs init
 * @cdns: Cadence instance
 * @root: debugfs root
 */
void sdw_cdns_debugfs_init(struct sdw_cdns *cdns, struct dentry *root)
{
	debugfs_create_file("cdns-registers", 0400, root, cdns, &cdns_reg_fops);

	debugfs_create_file("cdns-hw-reset", 0200, root, cdns,
			    &cdns_hw_reset_fops);

	debugfs_create_file("cdns-parity-error-injection", 0200, root, cdns,
			    &cdns_parity_error_fops);

	cdns->pdi_loopback_source = -1;
	cdns->pdi_loopback_target = -1;

	debugfs_create_file("cdns-pdi-loopback-source", 0200, root, cdns,
			    &cdns_pdi_loopback_source_fops);

	debugfs_create_file("cdns-pdi-loopback-target", 0200, root, cdns,
			    &cdns_pdi_loopback_target_fops);

}
EXPORT_SYMBOL_GPL(sdw_cdns_debugfs_init);

#endif /* CONFIG_DEBUG_FS */

/*
 * IO Calls
 */
static enum sdw_command_response
cdns_fill_msg_resp(struct sdw_cdns *cdns,
		   struct sdw_msg *msg, int count, int offset)
{
	int nack = 0, no_ack = 0;
	int i;

	/* check message response */
	for (i = 0; i < count; i++) {
		if (!(cdns->response_buf[i] & CDNS_MCP_RESP_ACK)) {
			no_ack = 1;
			dev_vdbg(cdns->dev, "Msg Ack not received, cmd %d\n", i);
		}
		if (cdns->response_buf[i] & CDNS_MCP_RESP_NACK) {
			nack = 1;
			dev_err_ratelimited(cdns->dev, "Msg NACK received, cmd %d\n", i);
		}
	}

	if (nack) {
		dev_err_ratelimited(cdns->dev, "Msg NACKed for Slave %d\n", msg->dev_num);
		return SDW_CMD_FAIL;
	}

	if (no_ack) {
		dev_dbg_ratelimited(cdns->dev, "Msg ignored for Slave %d\n", msg->dev_num);
		return SDW_CMD_IGNORED;
	}

	if (msg->flags == SDW_MSG_FLAG_READ) {
		/* fill response */
		for (i = 0; i < count; i++)
			msg->buf[i + offset] = FIELD_GET(CDNS_MCP_RESP_RDATA,
							 cdns->response_buf[i]);
	}

	return SDW_CMD_OK;
}

static void cdns_read_response(struct sdw_cdns *cdns)
{
	u32 num_resp, cmd_base;
	int i;

	/* RX_FIFO_AVAIL can be 2 entries more than the FIFO size */
	BUILD_BUG_ON(ARRAY_SIZE(cdns->response_buf) < CDNS_MCP_CMD_LEN + 2);

	num_resp = cdns_readl(cdns, CDNS_MCP_FIFOSTAT);
	num_resp &= CDNS_MCP_RX_FIFO_AVAIL;
	if (num_resp > ARRAY_SIZE(cdns->response_buf)) {
		dev_warn(cdns->dev, "RX AVAIL %d too long\n", num_resp);
		num_resp = ARRAY_SIZE(cdns->response_buf);
	}

	cmd_base = CDNS_IP_MCP_CMD_BASE;

	for (i = 0; i < num_resp; i++) {
		cdns->response_buf[i] = cdns_ip_readl(cdns, cmd_base);
		cmd_base += CDNS_MCP_CMD_WORD_LEN;
	}
}

static enum sdw_command_response
_cdns_xfer_msg(struct sdw_cdns *cdns, struct sdw_msg *msg, int cmd,
	       int offset, int count, bool defer)
{
	unsigned long time;
	u32 base, i, data;
	u16 addr;

	/* Program the watermark level for RX FIFO */
	if (cdns->msg_count != count) {
		cdns_writel(cdns, CDNS_MCP_FIFOLEVEL, count);
		cdns->msg_count = count;
	}

	base = CDNS_IP_MCP_CMD_BASE;
	addr = msg->addr + offset;

	for (i = 0; i < count; i++) {
		data = FIELD_PREP(CDNS_MCP_CMD_DEV_ADDR, msg->dev_num);
		data |= FIELD_PREP(CDNS_MCP_CMD_COMMAND, cmd);
		data |= FIELD_PREP(CDNS_MCP_CMD_REG_ADDR, addr);
		addr++;

		if (msg->flags == SDW_MSG_FLAG_WRITE)
			data |= msg->buf[i + offset];

		data |= FIELD_PREP(CDNS_MCP_CMD_SSP_TAG, msg->ssp_sync);
		cdns_ip_writel(cdns, base, data);
		base += CDNS_MCP_CMD_WORD_LEN;
	}

	if (defer)
		return SDW_CMD_OK;

	/* wait for timeout or response */
	time = wait_for_completion_timeout(&cdns->tx_complete,
					   msecs_to_jiffies(CDNS_TX_TIMEOUT));
	if (!time) {
		dev_err(cdns->dev, "IO transfer timed out, cmd %d device %d addr %x len %d\n",
			cmd, msg->dev_num, msg->addr, msg->len);
		msg->len = 0;

		/* Drain anything in the RX_FIFO */
		cdns_read_response(cdns);

		return SDW_CMD_TIMEOUT;
	}

	return cdns_fill_msg_resp(cdns, msg, count, offset);
}

static enum sdw_command_response
cdns_program_scp_addr(struct sdw_cdns *cdns, struct sdw_msg *msg)
{
	int nack = 0, no_ack = 0;
	unsigned long time;
	u32 data[2], base;
	int i;

	/* Program the watermark level for RX FIFO */
	if (cdns->msg_count != CDNS_SCP_RX_FIFOLEVEL) {
		cdns_writel(cdns, CDNS_MCP_FIFOLEVEL, CDNS_SCP_RX_FIFOLEVEL);
		cdns->msg_count = CDNS_SCP_RX_FIFOLEVEL;
	}

	data[0] = FIELD_PREP(CDNS_MCP_CMD_DEV_ADDR, msg->dev_num);
	data[0] |= FIELD_PREP(CDNS_MCP_CMD_COMMAND, 0x3);
	data[1] = data[0];

	data[0] |= FIELD_PREP(CDNS_MCP_CMD_REG_ADDR, SDW_SCP_ADDRPAGE1);
	data[1] |= FIELD_PREP(CDNS_MCP_CMD_REG_ADDR, SDW_SCP_ADDRPAGE2);

	data[0] |= msg->addr_page1;
	data[1] |= msg->addr_page2;

	base = CDNS_IP_MCP_CMD_BASE;
	cdns_ip_writel(cdns, base, data[0]);
	base += CDNS_MCP_CMD_WORD_LEN;
	cdns_ip_writel(cdns, base, data[1]);

	time = wait_for_completion_timeout(&cdns->tx_complete,
					   msecs_to_jiffies(CDNS_TX_TIMEOUT));
	if (!time) {
		dev_err(cdns->dev, "SCP Msg trf timed out\n");
		msg->len = 0;
		return SDW_CMD_TIMEOUT;
	}

	/* check response the writes */
	for (i = 0; i < 2; i++) {
		if (!(cdns->response_buf[i] & CDNS_MCP_RESP_ACK)) {
			no_ack = 1;
			dev_err(cdns->dev, "Program SCP Ack not received\n");
			if (cdns->response_buf[i] & CDNS_MCP_RESP_NACK) {
				nack = 1;
				dev_err(cdns->dev, "Program SCP NACK received\n");
			}
		}
	}

	/* For NACK, NO ack, don't return err if we are in Broadcast mode */
	if (nack) {
		dev_err_ratelimited(cdns->dev,
				    "SCP_addrpage NACKed for Slave %d\n", msg->dev_num);
		return SDW_CMD_FAIL;
	}

	if (no_ack) {
		dev_dbg_ratelimited(cdns->dev,
				    "SCP_addrpage ignored for Slave %d\n", msg->dev_num);
		return SDW_CMD_IGNORED;
	}

	return SDW_CMD_OK;
}

static int cdns_prep_msg(struct sdw_cdns *cdns, struct sdw_msg *msg, int *cmd)
{
	int ret;

	if (msg->page) {
		ret = cdns_program_scp_addr(cdns, msg);
		if (ret) {
			msg->len = 0;
			return ret;
		}
	}

	switch (msg->flags) {
	case SDW_MSG_FLAG_READ:
		*cmd = CDNS_MCP_CMD_READ;
		break;

	case SDW_MSG_FLAG_WRITE:
		*cmd = CDNS_MCP_CMD_WRITE;
		break;

	default:
		dev_err(cdns->dev, "Invalid msg cmd: %d\n", msg->flags);
		return -EINVAL;
	}

	return 0;
}

enum sdw_command_response
cdns_xfer_msg(struct sdw_bus *bus, struct sdw_msg *msg)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	int cmd = 0, ret, i;

	ret = cdns_prep_msg(cdns, msg, &cmd);
	if (ret)
		return SDW_CMD_FAIL_OTHER;

	for (i = 0; i < msg->len / CDNS_MCP_CMD_LEN; i++) {
		ret = _cdns_xfer_msg(cdns, msg, cmd, i * CDNS_MCP_CMD_LEN,
				     CDNS_MCP_CMD_LEN, false);
		if (ret != SDW_CMD_OK)
			return ret;
	}

	if (!(msg->len % CDNS_MCP_CMD_LEN))
		return SDW_CMD_OK;

	return _cdns_xfer_msg(cdns, msg, cmd, i * CDNS_MCP_CMD_LEN,
			      msg->len % CDNS_MCP_CMD_LEN, false);
}
EXPORT_SYMBOL(cdns_xfer_msg);

enum sdw_command_response
cdns_xfer_msg_defer(struct sdw_bus *bus)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	struct sdw_defer *defer = &bus->defer_msg;
	struct sdw_msg *msg = defer->msg;
	int cmd = 0, ret;

	/* for defer only 1 message is supported */
	if (msg->len > 1)
		return -ENOTSUPP;

	ret = cdns_prep_msg(cdns, msg, &cmd);
	if (ret)
		return SDW_CMD_FAIL_OTHER;

	return _cdns_xfer_msg(cdns, msg, cmd, 0, msg->len, true);
}
EXPORT_SYMBOL(cdns_xfer_msg_defer);

u32 cdns_read_ping_status(struct sdw_bus *bus)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);

	return cdns_readl(cdns, CDNS_MCP_SLAVE_STAT);
}
EXPORT_SYMBOL(cdns_read_ping_status);

/*
 * IRQ handling
 */

static int cdns_update_slave_status(struct sdw_cdns *cdns,
				    u64 slave_intstat)
{
	enum sdw_slave_status status[SDW_MAX_DEVICES + 1];
	bool is_slave = false;
	u32 mask;
	u32 val;
	int i, set_status;

	memset(status, 0, sizeof(status));

	for (i = 0; i <= SDW_MAX_DEVICES; i++) {
		mask = (slave_intstat >> (i * CDNS_MCP_SLAVE_STATUS_NUM)) &
			CDNS_MCP_SLAVE_STATUS_BITS;

		set_status = 0;

		if (mask) {
			is_slave = true;

			if (mask & CDNS_MCP_SLAVE_INTSTAT_RESERVED) {
				status[i] = SDW_SLAVE_RESERVED;
				set_status++;
			}

			if (mask & CDNS_MCP_SLAVE_INTSTAT_ATTACHED) {
				status[i] = SDW_SLAVE_ATTACHED;
				set_status++;
			}

			if (mask & CDNS_MCP_SLAVE_INTSTAT_ALERT) {
				status[i] = SDW_SLAVE_ALERT;
				set_status++;
			}

			if (mask & CDNS_MCP_SLAVE_INTSTAT_NPRESENT) {
				status[i] = SDW_SLAVE_UNATTACHED;
				set_status++;
			}
		}

		/*
		 * check that there was a single reported Slave status and when
		 * there is not use the latest status extracted from PING commands
		 */
		if (set_status != 1) {
			val = cdns_readl(cdns, CDNS_MCP_SLAVE_STAT);
			val >>= (i * 2);

			switch (val & 0x3) {
			case 0:
				status[i] = SDW_SLAVE_UNATTACHED;
				break;
			case 1:
				status[i] = SDW_SLAVE_ATTACHED;
				break;
			case 2:
				status[i] = SDW_SLAVE_ALERT;
				break;
			case 3:
			default:
				status[i] = SDW_SLAVE_RESERVED;
				break;
			}
		}
	}

	if (is_slave) {
		int ret;

		mutex_lock(&cdns->status_update_lock);
		ret = sdw_handle_slave_status(&cdns->bus, status);
		mutex_unlock(&cdns->status_update_lock);
		return ret;
	}

	return 0;
}

/**
 * sdw_cdns_irq() - Cadence interrupt handler
 * @irq: irq number
 * @dev_id: irq context
 */
irqreturn_t sdw_cdns_irq(int irq, void *dev_id)
{
	struct sdw_cdns *cdns = dev_id;
	u32 int_status;

	/* Check if the link is up */
	if (!cdns->link_up)
		return IRQ_NONE;

	int_status = cdns_readl(cdns, CDNS_MCP_INTSTAT);

	/* check for reserved values read as zero */
	if (int_status & CDNS_MCP_INT_RESERVED)
		return IRQ_NONE;

	if (!(int_status & CDNS_MCP_INT_IRQ))
		return IRQ_NONE;

	if (int_status & CDNS_MCP_INT_RX_WL) {
		struct sdw_bus *bus = &cdns->bus;
		struct sdw_defer *defer = &bus->defer_msg;

		cdns_read_response(cdns);

		if (defer && defer->msg) {
			cdns_fill_msg_resp(cdns, defer->msg,
					   defer->length, 0);
			complete(&defer->complete);
		} else {
			complete(&cdns->tx_complete);
		}
	}

	if (int_status & CDNS_MCP_INT_PARITY) {
		/* Parity error detected by Master */
		dev_err_ratelimited(cdns->dev, "Parity error\n");
	}

	if (int_status & CDNS_MCP_INT_CTRL_CLASH) {
		/* Slave is driving bit slot during control word */
		dev_err_ratelimited(cdns->dev, "Bus clash for control word\n");
	}

	if (int_status & CDNS_MCP_INT_DATA_CLASH) {
		/*
		 * Multiple slaves trying to drive bit slot, or issue with
		 * ownership of data bits or Slave gone bonkers
		 */
		dev_err_ratelimited(cdns->dev, "Bus clash for data word\n");
	}

	if (cdns->bus.params.m_data_mode != SDW_PORT_DATA_MODE_NORMAL &&
	    int_status & CDNS_MCP_INT_DPINT) {
		u32 port_intstat;

		/* just log which ports report an error */
		port_intstat = cdns_readl(cdns, CDNS_MCP_PORT_INTSTAT);
		dev_err_ratelimited(cdns->dev, "DP interrupt: PortIntStat %8x\n",
				    port_intstat);

		/* clear status w/ write1 */
		cdns_writel(cdns, CDNS_MCP_PORT_INTSTAT, port_intstat);
	}

	if (int_status & CDNS_MCP_INT_SLAVE_MASK) {
		/* Mask the Slave interrupt and wake thread */
		cdns_updatel(cdns, CDNS_MCP_INTMASK,
			     CDNS_MCP_INT_SLAVE_MASK, 0);

		int_status &= ~CDNS_MCP_INT_SLAVE_MASK;

		/*
		 * Deal with possible race condition between interrupt
		 * handling and disabling interrupts on suspend.
		 *
		 * If the master is in the process of disabling
		 * interrupts, don't schedule a workqueue
		 */
		if (cdns->interrupt_enabled)
			schedule_work(&cdns->work);
	}

	cdns_writel(cdns, CDNS_MCP_INTSTAT, int_status);
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(sdw_cdns_irq);

static void cdns_check_attached_status_dwork(struct work_struct *work)
{
	struct sdw_cdns *cdns =
		container_of(work, struct sdw_cdns, attach_dwork.work);
	enum sdw_slave_status status[SDW_MAX_DEVICES + 1];
	u32 val;
	int ret;
	int i;

	val = cdns_readl(cdns, CDNS_MCP_SLAVE_STAT);

	for (i = 0; i <= SDW_MAX_DEVICES; i++) {
		status[i] = val & 0x3;
		if (status[i])
			dev_dbg(cdns->dev, "Peripheral %d status: %d\n", i, status[i]);
		val >>= 2;
	}

	mutex_lock(&cdns->status_update_lock);
	ret = sdw_handle_slave_status(&cdns->bus, status);
	mutex_unlock(&cdns->status_update_lock);
	if (ret < 0)
		dev_err(cdns->dev, "%s: sdw_handle_slave_status failed: %d\n", __func__, ret);
}

/**
 * cdns_update_slave_status_work - update slave status in a work since we will need to handle
 * other interrupts eg. CDNS_MCP_INT_RX_WL during the update slave
 * process.
 * @work: cdns worker thread
 */
static void cdns_update_slave_status_work(struct work_struct *work)
{
	struct sdw_cdns *cdns =
		container_of(work, struct sdw_cdns, work);
	u32 slave0, slave1;
	u64 slave_intstat;
	u32 device0_status;
	int retry_count = 0;

	/*
	 * Clear main interrupt first so we don't lose any assertions
	 * that happen during this function.
	 */
	cdns_writel(cdns, CDNS_MCP_INTSTAT, CDNS_MCP_INT_SLAVE_MASK);

	slave0 = cdns_readl(cdns, CDNS_MCP_SLAVE_INTSTAT0);
	slave1 = cdns_readl(cdns, CDNS_MCP_SLAVE_INTSTAT1);

	/*
	 * Clear the bits before handling so we don't lose any
	 * bits that re-assert.
	 */
	cdns_writel(cdns, CDNS_MCP_SLAVE_INTSTAT0, slave0);
	cdns_writel(cdns, CDNS_MCP_SLAVE_INTSTAT1, slave1);

	/* combine the two status */
	slave_intstat = ((u64)slave1 << 32) | slave0;

	dev_dbg_ratelimited(cdns->dev, "Slave status change: 0x%llx\n", slave_intstat);

update_status:
	cdns_update_slave_status(cdns, slave_intstat);

	/*
	 * When there is more than one peripheral per link, it's
	 * possible that a deviceB becomes attached after we deal with
	 * the attachment of deviceA. Since the hardware does a
	 * logical AND, the attachment of the second device does not
	 * change the status seen by the driver.
	 *
	 * In that case, clearing the registers above would result in
	 * the deviceB never being detected - until a change of status
	 * is observed on the bus.
	 *
	 * To avoid this race condition, re-check if any device0 needs
	 * attention with PING commands. There is no need to check for
	 * ALERTS since they are not allowed until a non-zero
	 * device_number is assigned.
	 *
	 * Do not clear the INTSTAT0/1. While looping to enumerate devices on
	 * #0 there could be status changes on other devices - these must
	 * be kept in the INTSTAT so they can be handled when all #0 devices
	 * have been handled.
	 */

	device0_status = cdns_readl(cdns, CDNS_MCP_SLAVE_STAT);
	device0_status &= 3;

	if (device0_status == SDW_SLAVE_ATTACHED) {
		if (retry_count++ < SDW_MAX_DEVICES) {
			dev_dbg_ratelimited(cdns->dev,
					    "Device0 detected after clearing status, iteration %d\n",
					    retry_count);
			slave_intstat = CDNS_MCP_SLAVE_INTSTAT_ATTACHED;
			goto update_status;
		} else {
			dev_err_ratelimited(cdns->dev,
					    "Device0 detected after %d iterations\n",
					    retry_count);
		}
	}

	/* unmask Slave interrupt now */
	cdns_updatel(cdns, CDNS_MCP_INTMASK,
		     CDNS_MCP_INT_SLAVE_MASK, CDNS_MCP_INT_SLAVE_MASK);

}

/* paranoia check to make sure self-cleared bits are indeed cleared */
void sdw_cdns_check_self_clearing_bits(struct sdw_cdns *cdns, const char *string,
				       bool initial_delay, int reset_iterations)
{
	u32 ip_mcp_control;
	u32 mcp_control;
	u32 mcp_config_update;
	int i;

	if (initial_delay)
		usleep_range(1000, 1500);

	ip_mcp_control = cdns_ip_readl(cdns, CDNS_IP_MCP_CONTROL);

	/* the following bits should be cleared immediately */
	if (ip_mcp_control & CDNS_IP_MCP_CONTROL_SW_RST)
		dev_err(cdns->dev, "%s failed: IP_MCP_CONTROL_SW_RST is not cleared\n", string);

	mcp_control = cdns_readl(cdns, CDNS_MCP_CONTROL);

	/* the following bits should be cleared immediately */
	if (mcp_control & CDNS_MCP_CONTROL_CMD_RST)
		dev_err(cdns->dev, "%s failed: MCP_CONTROL_CMD_RST is not cleared\n", string);
	if (mcp_control & CDNS_MCP_CONTROL_SOFT_RST)
		dev_err(cdns->dev, "%s failed: MCP_CONTROL_SOFT_RST is not cleared\n", string);
	if (mcp_control & CDNS_MCP_CONTROL_CLK_STOP_CLR)
		dev_err(cdns->dev, "%s failed: MCP_CONTROL_CLK_STOP_CLR is not cleared\n", string);

	mcp_config_update = cdns_readl(cdns, CDNS_MCP_CONFIG_UPDATE);
	if (mcp_config_update & CDNS_MCP_CONFIG_UPDATE_BIT)
		dev_err(cdns->dev, "%s failed: MCP_CONFIG_UPDATE_BIT is not cleared\n", string);

	i = 0;
	while (mcp_control & CDNS_MCP_CONTROL_HW_RST) {
		if (i == reset_iterations) {
			dev_err(cdns->dev, "%s failed: MCP_CONTROL_HW_RST is not cleared\n", string);
			break;
		}

		dev_dbg(cdns->dev, "%s: MCP_CONTROL_HW_RST is not cleared at iteration %d\n", string, i);
		i++;

		usleep_range(1000, 1500);
		mcp_control = cdns_readl(cdns, CDNS_MCP_CONTROL);
	}

}
EXPORT_SYMBOL(sdw_cdns_check_self_clearing_bits);

/*
 * init routines
 */

/**
 * sdw_cdns_exit_reset() - Program reset parameters and start bus operations
 * @cdns: Cadence instance
 */
int sdw_cdns_exit_reset(struct sdw_cdns *cdns)
{
	/* keep reset delay unchanged to 4096 cycles */

	/* use hardware generated reset */
	cdns_updatel(cdns, CDNS_MCP_CONTROL,
		     CDNS_MCP_CONTROL_HW_RST,
		     CDNS_MCP_CONTROL_HW_RST);

	/* commit changes */
	return cdns_config_update(cdns);
}
EXPORT_SYMBOL(sdw_cdns_exit_reset);

/**
 * cdns_enable_slave_interrupts() - Enable SDW slave interrupts
 * @cdns: Cadence instance
 * @state: boolean for true/false
 */
static void cdns_enable_slave_interrupts(struct sdw_cdns *cdns, bool state)
{
	u32 mask;

	mask = cdns_readl(cdns, CDNS_MCP_INTMASK);
	if (state)
		mask |= CDNS_MCP_INT_SLAVE_MASK;
	else
		mask &= ~CDNS_MCP_INT_SLAVE_MASK;

	cdns_writel(cdns, CDNS_MCP_INTMASK, mask);
}

/**
 * sdw_cdns_enable_interrupt() - Enable SDW interrupts
 * @cdns: Cadence instance
 * @state: True if we are trying to enable interrupt.
 */
int sdw_cdns_enable_interrupt(struct sdw_cdns *cdns, bool state)
{
	u32 slave_intmask0 = 0;
	u32 slave_intmask1 = 0;
	u32 mask = 0;

	if (!state)
		goto update_masks;

	slave_intmask0 = CDNS_MCP_SLAVE_INTMASK0_MASK;
	slave_intmask1 = CDNS_MCP_SLAVE_INTMASK1_MASK;

	/* enable detection of all slave state changes */
	mask = CDNS_MCP_INT_SLAVE_MASK;

	/* enable detection of bus issues */
	mask |= CDNS_MCP_INT_CTRL_CLASH | CDNS_MCP_INT_DATA_CLASH |
		CDNS_MCP_INT_PARITY;

	/* port interrupt limited to test modes for now */
	if (cdns->bus.params.m_data_mode != SDW_PORT_DATA_MODE_NORMAL)
		mask |= CDNS_MCP_INT_DPINT;

	/* enable detection of RX fifo level */
	mask |= CDNS_MCP_INT_RX_WL;

	/*
	 * CDNS_MCP_INT_IRQ needs to be set otherwise all previous
	 * settings are irrelevant
	 */
	mask |= CDNS_MCP_INT_IRQ;

	if (interrupt_mask) /* parameter override */
		mask = interrupt_mask;

update_masks:
	/* clear slave interrupt status before enabling interrupt */
	if (state) {
		u32 slave_state;

		slave_state = cdns_readl(cdns, CDNS_MCP_SLAVE_INTSTAT0);
		cdns_writel(cdns, CDNS_MCP_SLAVE_INTSTAT0, slave_state);
		slave_state = cdns_readl(cdns, CDNS_MCP_SLAVE_INTSTAT1);
		cdns_writel(cdns, CDNS_MCP_SLAVE_INTSTAT1, slave_state);
	}
	cdns->interrupt_enabled = state;

	/*
	 * Complete any on-going status updates before updating masks,
	 * and cancel queued status updates.
	 *
	 * There could be a race with a new interrupt thrown before
	 * the 3 mask updates below are complete, so in the interrupt
	 * we use the 'interrupt_enabled' status to prevent new work
	 * from being queued.
	 */
	if (!state)
		cancel_work_sync(&cdns->work);

	cdns_writel(cdns, CDNS_MCP_SLAVE_INTMASK0, slave_intmask0);
	cdns_writel(cdns, CDNS_MCP_SLAVE_INTMASK1, slave_intmask1);
	cdns_writel(cdns, CDNS_MCP_INTMASK, mask);

	return 0;
}
EXPORT_SYMBOL(sdw_cdns_enable_interrupt);

static int cdns_allocate_pdi(struct sdw_cdns *cdns,
			     struct sdw_cdns_pdi **stream,
			     u32 num)
{
	struct sdw_cdns_pdi *pdi;
	int i;

	if (!num)
		return 0;

	pdi = devm_kcalloc(cdns->dev, num, sizeof(*pdi), GFP_KERNEL);
	if (!pdi)
		return -ENOMEM;

	for (i = 0; i < num; i++) {
		pdi[i].num = i;
	}

	*stream = pdi;
	return 0;
}

/**
 * sdw_cdns_pdi_init() - PDI initialization routine
 *
 * @cdns: Cadence instance
 * @config: Stream configurations
 */
int sdw_cdns_pdi_init(struct sdw_cdns *cdns,
		      struct sdw_cdns_stream_config config)
{
	struct sdw_cdns_streams *stream;
	int ret;

	cdns->pcm.num_bd = config.pcm_bd;
	cdns->pcm.num_in = config.pcm_in;
	cdns->pcm.num_out = config.pcm_out;

	/* Allocate PDIs for PCMs */
	stream = &cdns->pcm;

	/* we allocate PDI0 and PDI1 which are used for Bulk */
	ret = cdns_allocate_pdi(cdns, &stream->bd, stream->num_bd);
	if (ret)
		return ret;

	ret = cdns_allocate_pdi(cdns, &stream->in, stream->num_in);
	if (ret)
		return ret;

	ret = cdns_allocate_pdi(cdns, &stream->out, stream->num_out);
	if (ret)
		return ret;

	/* Update total number of PCM PDIs */
	stream->num_pdi = stream->num_bd + stream->num_in + stream->num_out;
	cdns->num_ports = stream->num_pdi;

	return 0;
}
EXPORT_SYMBOL(sdw_cdns_pdi_init);

static u32 cdns_set_initial_frame_shape(int n_rows, int n_cols)
{
	u32 val;
	int c;
	int r;

	r = sdw_find_row_index(n_rows);
	c = sdw_find_col_index(n_cols);

	val = FIELD_PREP(CDNS_MCP_FRAME_SHAPE_ROW_MASK, r);
	val |= FIELD_PREP(CDNS_MCP_FRAME_SHAPE_COL_MASK, c);

	return val;
}

static int cdns_init_clock_ctrl(struct sdw_cdns *cdns)
{
	struct sdw_bus *bus = &cdns->bus;
	struct sdw_master_prop *prop = &bus->prop;
	u32 val;
	u32 ssp_interval;
	int divider;

	dev_dbg(cdns->dev, "mclk %d max %d row %d col %d\n",
		prop->mclk_freq,
		prop->max_clk_freq,
		prop->default_row,
		prop->default_col);

	if (!prop->default_frame_rate || !prop->default_row) {
		dev_err(cdns->dev, "Default frame_rate %d or row %d is invalid\n",
			prop->default_frame_rate, prop->default_row);
		return -EINVAL;
	}

	/* Set clock divider */
	divider	= (prop->mclk_freq * SDW_DOUBLE_RATE_FACTOR /
		bus->params.curr_dr_freq) - 1;

	cdns_updatel(cdns, CDNS_MCP_CLK_CTRL0,
		     CDNS_MCP_CLK_MCLKD_MASK, divider);
	cdns_updatel(cdns, CDNS_MCP_CLK_CTRL1,
		     CDNS_MCP_CLK_MCLKD_MASK, divider);

	/* Set frame shape base on the actual bus frequency. */
	prop->default_col = bus->params.curr_dr_freq /
			    prop->default_frame_rate / prop->default_row;

	/*
	 * Frame shape changes after initialization have to be done
	 * with the bank switch mechanism
	 */
	val = cdns_set_initial_frame_shape(prop->default_row,
					   prop->default_col);
	cdns_writel(cdns, CDNS_MCP_FRAME_SHAPE_INIT, val);

	/* Set SSP interval to default value */
	ssp_interval = prop->default_frame_rate / SDW_CADENCE_GSYNC_HZ;
	cdns_writel(cdns, CDNS_MCP_SSP_CTRL0, ssp_interval);
	cdns_writel(cdns, CDNS_MCP_SSP_CTRL1, ssp_interval);

	return 0;
}

/**
 * sdw_cdns_soft_reset() - Cadence soft-reset
 * @cdns: Cadence instance
 */
int sdw_cdns_soft_reset(struct sdw_cdns *cdns)
{
	int ret;

	cdns_updatel(cdns, CDNS_MCP_CONTROL, CDNS_MCP_CONTROL_SOFT_RST,
		     CDNS_MCP_CONTROL_SOFT_RST);

	ret = cdns_config_update(cdns);
	if (ret < 0) {
		dev_err(cdns->dev, "%s: config update failed\n", __func__);
		return ret;
	}

	ret = cdns_set_wait(cdns, CDNS_MCP_CONTROL, CDNS_MCP_CONTROL_SOFT_RST, 0);
	if (ret < 0)
		dev_err(cdns->dev, "%s: Soft Reset timed out\n", __func__);

	return ret;
}
EXPORT_SYMBOL(sdw_cdns_soft_reset);

/**
 * sdw_cdns_init() - Cadence initialization
 * @cdns: Cadence instance
 */
int sdw_cdns_init(struct sdw_cdns *cdns)
{
	int ret;
	u32 val;

	ret = cdns_init_clock_ctrl(cdns);
	if (ret)
		return ret;

	sdw_cdns_check_self_clearing_bits(cdns, __func__, false, 0);

	/* reset msg_count to default value of FIFOLEVEL */
	cdns->msg_count = cdns_readl(cdns, CDNS_MCP_FIFOLEVEL);

	/* flush command FIFOs */
	cdns_updatel(cdns, CDNS_MCP_CONTROL, CDNS_MCP_CONTROL_CMD_RST,
		     CDNS_MCP_CONTROL_CMD_RST);

	/* Set cmd accept mode */
	cdns_ip_updatel(cdns, CDNS_IP_MCP_CONTROL, CDNS_IP_MCP_CONTROL_CMD_ACCEPT,
			CDNS_IP_MCP_CONTROL_CMD_ACCEPT);

	/* disable wakeup */
	cdns_ip_updatel(cdns, CDNS_IP_MCP_CONTROL,
			CDNS_IP_MCP_CONTROL_BLOCK_WAKEUP,
			0);

	/* Configure mcp config */
	val = cdns_readl(cdns, CDNS_MCP_CONFIG);

	/* Disable auto bus release */
	val &= ~CDNS_MCP_CONFIG_BUS_REL;

	cdns_writel(cdns, CDNS_MCP_CONFIG, val);

	/* Configure IP mcp config */
	val = cdns_ip_readl(cdns, CDNS_IP_MCP_CONFIG);

	/* enable bus operations with clock and data */
	val &= ~CDNS_IP_MCP_CONFIG_OP;
	val |= CDNS_IP_MCP_CONFIG_OP_NORMAL;

	/* Set cmd mode for Tx and Rx cmds */
	val &= ~CDNS_IP_MCP_CONFIG_CMD;

	/* Disable sniffer mode */
	val &= ~CDNS_IP_MCP_CONFIG_SNIFFER;

	if (cdns->bus.multi_link)
		/* Set Multi-master mode to take gsync into account */
		val |= CDNS_IP_MCP_CONFIG_MMASTER;

	/* leave frame delay to hardware default of 0x1F */

	/* leave command retry to hardware default of 0 */

	cdns_ip_writel(cdns, CDNS_IP_MCP_CONFIG, val);

	/* changes will be committed later */
	return 0;
}
EXPORT_SYMBOL(sdw_cdns_init);

int cdns_bus_conf(struct sdw_bus *bus, struct sdw_bus_params *params)
{
	struct sdw_master_prop *prop = &bus->prop;
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	int mcp_clkctrl_off;
	int divider;

	if (!params->curr_dr_freq) {
		dev_err(cdns->dev, "NULL curr_dr_freq\n");
		return -EINVAL;
	}

	divider	= prop->mclk_freq * SDW_DOUBLE_RATE_FACTOR /
		params->curr_dr_freq;
	divider--; /* divider is 1/(N+1) */

	if (params->next_bank)
		mcp_clkctrl_off = CDNS_MCP_CLK_CTRL1;
	else
		mcp_clkctrl_off = CDNS_MCP_CLK_CTRL0;

	cdns_updatel(cdns, mcp_clkctrl_off, CDNS_MCP_CLK_MCLKD_MASK, divider);

	return 0;
}
EXPORT_SYMBOL(cdns_bus_conf);

static int cdns_port_params(struct sdw_bus *bus,
			    struct sdw_port_params *p_params, unsigned int bank)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	int dpn_config_off_source;
	int dpn_config_off_target;
	int target_num = p_params->num;
	int source_num = p_params->num;
	bool override = false;
	int dpn_config;

	if (target_num == cdns->pdi_loopback_target &&
	    cdns->pdi_loopback_source != -1) {
		source_num = cdns->pdi_loopback_source;
		override = true;
	}

	if (bank) {
		dpn_config_off_source = CDNS_DPN_B1_CONFIG(source_num);
		dpn_config_off_target = CDNS_DPN_B1_CONFIG(target_num);
	} else {
		dpn_config_off_source = CDNS_DPN_B0_CONFIG(source_num);
		dpn_config_off_target = CDNS_DPN_B0_CONFIG(target_num);
	}

	dpn_config = cdns_readl(cdns, dpn_config_off_source);

	/* use port params if there is no loopback, otherwise use source as is */
	if (!override) {
		u32p_replace_bits(&dpn_config, p_params->bps - 1, CDNS_DPN_CONFIG_WL);
		u32p_replace_bits(&dpn_config, p_params->flow_mode, CDNS_DPN_CONFIG_PORT_FLOW);
		u32p_replace_bits(&dpn_config, p_params->data_mode, CDNS_DPN_CONFIG_PORT_DAT);
	}

	cdns_writel(cdns, dpn_config_off_target, dpn_config);

	return 0;
}

static int cdns_transport_params(struct sdw_bus *bus,
				 struct sdw_transport_params *t_params,
				 enum sdw_reg_bank bank)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	int dpn_config;
	int dpn_config_off_source;
	int dpn_config_off_target;
	int dpn_hctrl;
	int dpn_hctrl_off_source;
	int dpn_hctrl_off_target;
	int dpn_offsetctrl;
	int dpn_offsetctrl_off_source;
	int dpn_offsetctrl_off_target;
	int dpn_samplectrl;
	int dpn_samplectrl_off_source;
	int dpn_samplectrl_off_target;
	int source_num = t_params->port_num;
	int target_num = t_params->port_num;
	bool override = false;

	if (target_num == cdns->pdi_loopback_target &&
	    cdns->pdi_loopback_source != -1) {
		source_num = cdns->pdi_loopback_source;
		override = true;
	}

	/*
	 * Note: Only full data port is supported on the Master side for
	 * both PCM and PDM ports.
	 */

	if (bank) {
		dpn_config_off_source = CDNS_DPN_B1_CONFIG(source_num);
		dpn_hctrl_off_source = CDNS_DPN_B1_HCTRL(source_num);
		dpn_offsetctrl_off_source = CDNS_DPN_B1_OFFSET_CTRL(source_num);
		dpn_samplectrl_off_source = CDNS_DPN_B1_SAMPLE_CTRL(source_num);

		dpn_config_off_target = CDNS_DPN_B1_CONFIG(target_num);
		dpn_hctrl_off_target = CDNS_DPN_B1_HCTRL(target_num);
		dpn_offsetctrl_off_target = CDNS_DPN_B1_OFFSET_CTRL(target_num);
		dpn_samplectrl_off_target = CDNS_DPN_B1_SAMPLE_CTRL(target_num);

	} else {
		dpn_config_off_source = CDNS_DPN_B0_CONFIG(source_num);
		dpn_hctrl_off_source = CDNS_DPN_B0_HCTRL(source_num);
		dpn_offsetctrl_off_source = CDNS_DPN_B0_OFFSET_CTRL(source_num);
		dpn_samplectrl_off_source = CDNS_DPN_B0_SAMPLE_CTRL(source_num);

		dpn_config_off_target = CDNS_DPN_B0_CONFIG(target_num);
		dpn_hctrl_off_target = CDNS_DPN_B0_HCTRL(target_num);
		dpn_offsetctrl_off_target = CDNS_DPN_B0_OFFSET_CTRL(target_num);
		dpn_samplectrl_off_target = CDNS_DPN_B0_SAMPLE_CTRL(target_num);
	}

	dpn_config = cdns_readl(cdns, dpn_config_off_source);
	if (!override) {
		u32p_replace_bits(&dpn_config, t_params->blk_grp_ctrl, CDNS_DPN_CONFIG_BGC);
		u32p_replace_bits(&dpn_config, t_params->blk_pkg_mode, CDNS_DPN_CONFIG_BPM);
	}
	cdns_writel(cdns, dpn_config_off_target, dpn_config);

	if (!override) {
		dpn_offsetctrl = 0;
		u32p_replace_bits(&dpn_offsetctrl, t_params->offset1, CDNS_DPN_OFFSET_CTRL_1);
		u32p_replace_bits(&dpn_offsetctrl, t_params->offset2, CDNS_DPN_OFFSET_CTRL_2);
	} else {
		dpn_offsetctrl = cdns_readl(cdns, dpn_offsetctrl_off_source);
	}
	cdns_writel(cdns, dpn_offsetctrl_off_target,  dpn_offsetctrl);

	if (!override) {
		dpn_hctrl = 0;
		u32p_replace_bits(&dpn_hctrl, t_params->hstart, CDNS_DPN_HCTRL_HSTART);
		u32p_replace_bits(&dpn_hctrl, t_params->hstop, CDNS_DPN_HCTRL_HSTOP);
		u32p_replace_bits(&dpn_hctrl, t_params->lane_ctrl, CDNS_DPN_HCTRL_LCTRL);
	} else {
		dpn_hctrl = cdns_readl(cdns, dpn_hctrl_off_source);
	}
	cdns_writel(cdns, dpn_hctrl_off_target, dpn_hctrl);

	if (!override)
		dpn_samplectrl = t_params->sample_interval - 1;
	else
		dpn_samplectrl = cdns_readl(cdns, dpn_samplectrl_off_source);
	cdns_writel(cdns, dpn_samplectrl_off_target, dpn_samplectrl);

	return 0;
}

static int cdns_port_enable(struct sdw_bus *bus,
			    struct sdw_enable_ch *enable_ch, unsigned int bank)
{
	struct sdw_cdns *cdns = bus_to_cdns(bus);
	int dpn_chnen_off, ch_mask;

	if (bank)
		dpn_chnen_off = CDNS_DPN_B1_CH_EN(enable_ch->port_num);
	else
		dpn_chnen_off = CDNS_DPN_B0_CH_EN(enable_ch->port_num);

	ch_mask = enable_ch->ch_mask * enable_ch->enable;
	cdns_writel(cdns, dpn_chnen_off, ch_mask);

	return 0;
}

static const struct sdw_master_port_ops cdns_port_ops = {
	.dpn_set_port_params = cdns_port_params,
	.dpn_set_port_transport_params = cdns_transport_params,
	.dpn_port_enable_ch = cdns_port_enable,
};

/**
 * sdw_cdns_is_clock_stop: Check clock status
 *
 * @cdns: Cadence instance
 */
bool sdw_cdns_is_clock_stop(struct sdw_cdns *cdns)
{
	return !!(cdns_readl(cdns, CDNS_MCP_STAT) & CDNS_MCP_STAT_CLK_STOP);
}
EXPORT_SYMBOL(sdw_cdns_is_clock_stop);

/**
 * sdw_cdns_clock_stop: Cadence clock stop configuration routine
 *
 * @cdns: Cadence instance
 * @block_wake: prevent wakes if required by the platform
 */
int sdw_cdns_clock_stop(struct sdw_cdns *cdns, bool block_wake)
{
	bool slave_present = false;
	struct sdw_slave *slave;
	int ret;

	sdw_cdns_check_self_clearing_bits(cdns, __func__, false, 0);

	/* Check suspend status */
	if (sdw_cdns_is_clock_stop(cdns)) {
		dev_dbg(cdns->dev, "Clock is already stopped\n");
		return 0;
	}

	/*
	 * Before entering clock stop we mask the Slave
	 * interrupts. This helps avoid having to deal with e.g. a
	 * Slave becoming UNATTACHED while the clock is being stopped
	 */
	cdns_enable_slave_interrupts(cdns, false);

	/*
	 * For specific platforms, it is required to be able to put
	 * master into a state in which it ignores wake-up trials
	 * in clock stop state
	 */
	if (block_wake)
		cdns_ip_updatel(cdns, CDNS_IP_MCP_CONTROL,
				CDNS_IP_MCP_CONTROL_BLOCK_WAKEUP,
				CDNS_IP_MCP_CONTROL_BLOCK_WAKEUP);

	list_for_each_entry(slave, &cdns->bus.slaves, node) {
		if (slave->status == SDW_SLAVE_ATTACHED ||
		    slave->status == SDW_SLAVE_ALERT) {
			slave_present = true;
			break;
		}
	}

	/* commit changes */
	ret = cdns_config_update(cdns);
	if (ret < 0) {
		dev_err(cdns->dev, "%s: config_update failed\n", __func__);
		return ret;
	}

	/* Prepare slaves for clock stop */
	if (slave_present) {
		ret = sdw_bus_prep_clk_stop(&cdns->bus);
		if (ret < 0 && ret != -ENODATA) {
			dev_err(cdns->dev, "prepare clock stop failed %d\n", ret);
			return ret;
		}
	}

	/*
	 * Enter clock stop mode and only report errors if there are
	 * Slave devices present (ALERT or ATTACHED)
	 */
	ret = sdw_bus_clk_stop(&cdns->bus);
	if (ret < 0 && slave_present && ret != -ENODATA) {
		dev_err(cdns->dev, "bus clock stop failed %d\n", ret);
		return ret;
	}

	ret = cdns_set_wait(cdns, CDNS_MCP_STAT,
			    CDNS_MCP_STAT_CLK_STOP,
			    CDNS_MCP_STAT_CLK_STOP);
	if (ret < 0)
		dev_err(cdns->dev, "Clock stop failed %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(sdw_cdns_clock_stop);

/**
 * sdw_cdns_clock_restart: Cadence PM clock restart configuration routine
 *
 * @cdns: Cadence instance
 * @bus_reset: context may be lost while in low power modes and the bus
 * may require a Severe Reset and re-enumeration after a wake.
 */
int sdw_cdns_clock_restart(struct sdw_cdns *cdns, bool bus_reset)
{
	int ret;

	/* unmask Slave interrupts that were masked when stopping the clock */
	cdns_enable_slave_interrupts(cdns, true);

	ret = cdns_clear_bit(cdns, CDNS_MCP_CONTROL,
			     CDNS_MCP_CONTROL_CLK_STOP_CLR);
	if (ret < 0) {
		dev_err(cdns->dev, "Couldn't exit from clock stop\n");
		return ret;
	}

	ret = cdns_set_wait(cdns, CDNS_MCP_STAT, CDNS_MCP_STAT_CLK_STOP, 0);
	if (ret < 0) {
		dev_err(cdns->dev, "clock stop exit failed %d\n", ret);
		return ret;
	}

	cdns_ip_updatel(cdns, CDNS_IP_MCP_CONTROL,
			CDNS_IP_MCP_CONTROL_BLOCK_WAKEUP, 0);

	cdns_ip_updatel(cdns, CDNS_IP_MCP_CONTROL, CDNS_IP_MCP_CONTROL_CMD_ACCEPT,
			CDNS_IP_MCP_CONTROL_CMD_ACCEPT);

	if (!bus_reset) {

		/* enable bus operations with clock and data */
		cdns_ip_updatel(cdns, CDNS_IP_MCP_CONFIG,
				CDNS_IP_MCP_CONFIG_OP,
				CDNS_IP_MCP_CONFIG_OP_NORMAL);

		ret = cdns_config_update(cdns);
		if (ret < 0) {
			dev_err(cdns->dev, "%s: config_update failed\n", __func__);
			return ret;
		}

		ret = sdw_bus_exit_clk_stop(&cdns->bus);
		if (ret < 0)
			dev_err(cdns->dev, "bus failed to exit clock stop %d\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL(sdw_cdns_clock_restart);

/**
 * sdw_cdns_probe() - Cadence probe routine
 * @cdns: Cadence instance
 */
int sdw_cdns_probe(struct sdw_cdns *cdns)
{
	init_completion(&cdns->tx_complete);
	cdns->bus.port_ops = &cdns_port_ops;

	mutex_init(&cdns->status_update_lock);

	INIT_WORK(&cdns->work, cdns_update_slave_status_work);
	INIT_DELAYED_WORK(&cdns->attach_dwork, cdns_check_attached_status_dwork);

	return 0;
}
EXPORT_SYMBOL(sdw_cdns_probe);

int cdns_set_sdw_stream(struct snd_soc_dai *dai,
			void *stream, int direction)
{
	struct sdw_cdns *cdns = snd_soc_dai_get_drvdata(dai);
	struct sdw_cdns_dai_runtime *dai_runtime;

	dai_runtime = cdns->dai_runtime_array[dai->id];

	if (stream) {
		/* first paranoia check */
		if (dai_runtime) {
			dev_err(dai->dev,
				"dai_runtime already allocated for dai %s\n",
				dai->name);
			return -EINVAL;
		}

		/* allocate and set dai_runtime info */
		dai_runtime = kzalloc(sizeof(*dai_runtime), GFP_KERNEL);
		if (!dai_runtime)
			return -ENOMEM;

		dai_runtime->stream_type = SDW_STREAM_PCM;

		dai_runtime->bus = &cdns->bus;
		dai_runtime->link_id = cdns->instance;

		dai_runtime->stream = stream;
		dai_runtime->direction = direction;

		cdns->dai_runtime_array[dai->id] = dai_runtime;
	} else {
		/* second paranoia check */
		if (!dai_runtime) {
			dev_err(dai->dev,
				"dai_runtime not allocated for dai %s\n",
				dai->name);
			return -EINVAL;
		}

		/* for NULL stream we release allocated dai_runtime */
		kfree(dai_runtime);
		cdns->dai_runtime_array[dai->id] = NULL;
	}
	return 0;
}
EXPORT_SYMBOL(cdns_set_sdw_stream);

/**
 * cdns_find_pdi() - Find a free PDI
 *
 * @cdns: Cadence instance
 * @num: Number of PDIs
 * @pdi: PDI instances
 * @dai_id: DAI id
 *
 * Find a PDI for a given PDI array. The PDI num and dai_id are
 * expected to match, return NULL otherwise.
 */
static struct sdw_cdns_pdi *cdns_find_pdi(struct sdw_cdns *cdns,
					  unsigned int num,
					  struct sdw_cdns_pdi *pdi,
					  int dai_id)
{
	int i;

	for (i = 0; i < num; i++)
		if (pdi[i].num == dai_id)
			return &pdi[i];

	return NULL;
}

/**
 * sdw_cdns_config_stream: Configure a stream
 *
 * @cdns: Cadence instance
 * @ch: Channel count
 * @dir: Data direction
 * @pdi: PDI to be used
 */
void sdw_cdns_config_stream(struct sdw_cdns *cdns,
			    u32 ch, u32 dir, struct sdw_cdns_pdi *pdi)
{
	u32 offset, val = 0;

	if (dir == SDW_DATA_DIR_RX) {
		val = CDNS_PORTCTRL_DIRN;

		if (cdns->bus.params.m_data_mode != SDW_PORT_DATA_MODE_NORMAL)
			val |= CDNS_PORTCTRL_TEST_FAILED;
	} else if (pdi->num == 0 || pdi->num == 1) {
		val |= CDNS_PORTCTRL_BULK_ENABLE;
	}
	offset = CDNS_PORTCTRL + pdi->num * CDNS_PORT_OFFSET;
	cdns_updatel(cdns, offset,
		     CDNS_PORTCTRL_DIRN | CDNS_PORTCTRL_TEST_FAILED |
		     CDNS_PORTCTRL_BULK_ENABLE,
		     val);

	/* The DataPort0 needs to be mapped to both PDI0 and PDI1 ! */
	if (pdi->num == 1)
		val = 0;
	else
		val = pdi->num;
	val |= CDNS_PDI_CONFIG_SOFT_RESET;
	val |= FIELD_PREP(CDNS_PDI_CONFIG_CHANNEL, (1 << ch) - 1);
	cdns_writel(cdns, CDNS_PDI_CONFIG(pdi->num), val);
}
EXPORT_SYMBOL(sdw_cdns_config_stream);

/**
 * sdw_cdns_alloc_pdi() - Allocate a PDI
 *
 * @cdns: Cadence instance
 * @stream: Stream to be allocated
 * @ch: Channel count
 * @dir: Data direction
 * @dai_id: DAI id
 */
struct sdw_cdns_pdi *sdw_cdns_alloc_pdi(struct sdw_cdns *cdns,
					struct sdw_cdns_streams *stream,
					u32 ch, u32 dir, int dai_id)
{
	struct sdw_cdns_pdi *pdi = NULL;

	if (dir == SDW_DATA_DIR_RX)
		pdi = cdns_find_pdi(cdns, stream->num_in, stream->in,
				    dai_id);
	else
		pdi = cdns_find_pdi(cdns, stream->num_out, stream->out,
				    dai_id);

	/* check if we found a PDI, else find in bi-directional */
	if (!pdi)
		pdi = cdns_find_pdi(cdns, stream->num_bd, stream->bd,
				    dai_id);

	if (pdi) {
		pdi->l_ch_num = 0;
		pdi->h_ch_num = ch - 1;
		pdi->dir = dir;
		pdi->ch_count = ch;
	}

	return pdi;
}
EXPORT_SYMBOL(sdw_cdns_alloc_pdi);

/*
 * the MIPI SoundWire CRC8 polynomial is X^8 + X^6 + X^3 + X^2 + 1, MSB first
 * The value is (1)01001101 = 0x4D
 *
 * the table below was generated with
 *
 *	u8 crc8_lookup_table[CRC8_TABLE_SIZE];
 *	crc8_populate_msb(crc8_lookup_table, SDW_CRC8_POLY);
 *
 */
#define SDW_CRC8_SEED 0xFF
#define SDW_CRC8_POLY 0x4D

static const u8 sdw_crc8_lookup_msb[CRC8_TABLE_SIZE] = {
	0x00, 0x4d, 0x9a, 0xd7, 0x79, 0x34, 0xe3, 0xae, /* 0 - 7 */
	0xf2, 0xbf, 0x68, 0x25, 0x8b, 0xc6, 0x11, 0x5c, /* 8 -15 */
	0xa9, 0xe4, 0x33, 0x7e, 0xd0, 0x9d, 0x4a, 0x07, /* 16 - 23 */
	0x5b, 0x16, 0xc1, 0x8c, 0x22, 0x6f, 0xb8, 0xf5, /* 24 - 31 */
	0x1f, 0x52, 0x85, 0xc8, 0x66, 0x2b, 0xfc, 0xb1, /* 32 - 39 */
	0xed, 0xa0, 0x77, 0x3a, 0x94, 0xd9, 0x0e, 0x43, /* 40 - 47 */
	0xb6, 0xfb, 0x2c, 0x61, 0xcf, 0x82, 0x55, 0x18, /* 48 - 55 */
	0x44, 0x09, 0xde, 0x93, 0x3d, 0x70, 0xa7, 0xea, /* 56 - 63 */
	0x3e, 0x73, 0xa4, 0xe9, 0x47, 0x0a, 0xdd, 0x90, /* 64 - 71 */
	0xcc, 0x81, 0x56, 0x1b, 0xb5, 0xf8, 0x2f, 0x62, /* 72 - 79 */
	0x97, 0xda, 0x0d, 0x40, 0xee, 0xa3, 0x74, 0x39, /* 80 - 87 */
	0x65, 0x28, 0xff, 0xb2, 0x1c, 0x51, 0x86, 0xcb, /* 88 - 95 */
	0x21, 0x6c, 0xbb, 0xf6, 0x58, 0x15, 0xc2, 0x8f, /* 96 - 103 */
	0xd3, 0x9e, 0x49, 0x04, 0xaa, 0xe7, 0x30, 0x7d, /* 104 - 111 */
	0x88, 0xc5, 0x12, 0x5f, 0xf1, 0xbc, 0x6b, 0x26, /* 112 - 119 */
	0x7a, 0x37, 0xe0, 0xad, 0x03, 0x4e, 0x99, 0xd4, /* 120 - 127 */
	0x7c, 0x31, 0xe6, 0xab, 0x05, 0x48, 0x9f, 0xd2, /* 128 - 135 */
	0x8e, 0xc3, 0x14, 0x59, 0xf7, 0xba, 0x6d, 0x20, /* 136 - 143 */
	0xd5, 0x98, 0x4f, 0x02, 0xac, 0xe1, 0x36, 0x7b, /* 144 - 151 */
	0x27, 0x6a, 0xbd, 0xf0, 0x5e, 0x13, 0xc4, 0x89, /* 152 - 159 */
	0x63, 0x2e, 0xf9, 0xb4, 0x1a, 0x57, 0x80, 0xcd, /* 160 - 167 */
	0x91, 0xdc, 0x0b, 0x46, 0xe8, 0xa5, 0x72, 0x3f, /* 168 - 175 */
	0xca, 0x87, 0x50, 0x1d, 0xb3, 0xfe, 0x29, 0x64, /* 176 - 183 */
	0x38, 0x75, 0xa2, 0xef, 0x41, 0x0c, 0xdb, 0x96, /* 184 - 191 */
	0x42, 0x0f, 0xd8, 0x95, 0x3b, 0x76, 0xa1, 0xec, /* 192 - 199 */
	0xb0, 0xfd, 0x2a, 0x67, 0xc9, 0x84, 0x53, 0x1e, /* 200 - 207 */
	0xeb, 0xa6, 0x71, 0x3c, 0x92, 0xdf, 0x08, 0x45, /* 208 - 215 */
	0x19, 0x54, 0x83, 0xce, 0x60, 0x2d, 0xfa, 0xb7, /* 216 - 223 */
	0x5d, 0x10, 0xc7, 0x8a, 0x24, 0x69, 0xbe, 0xf3, /* 224 - 231 */
	0xaf, 0xe2, 0x35, 0x78, 0xd6, 0x9b, 0x4c, 0x01, /* 232 - 239 */
	0xf4, 0xb9, 0x6e, 0x23, 0x8d, 0xc0, 0x17, 0x5a, /* 240 - 247 */
	0x06, 0x4b, 0x9c, 0xd1, 0x7f, 0x32, 0xe5, 0xa8  /* 248 - 255 */
};

/* BPT/BRA helpers */

#define SDW_CDNS_BRA_HDR			6 /* defined by MIPI */
#define SDW_CDNS_BRA_HDR_CRC			1 /* defined by MIPI */
#define SDW_CDNS_BRA_HDR_CRC_PAD		1 /* Cadence only */
#define SDW_CDNS_BRA_HDR_RESP			1 /* defined by MIPI */
#define SDW_CDNS_BRA_HDR_RESP_PAD		1 /* Cadence only */

#define SDW_CDNS_BRA_DATA_PAD			1 /* Cadence only */
#define SDW_CDNS_BRA_DATA_CRC			1 /* defined by MIPI */
#define SDW_CDNS_BRA_DATA_CRC_PAD		1 /* Cadence only */

#define SDW_CDNS_BRA_FOOTER_RESP		1 /* defined by MIPI */
#define SDW_CDNS_BRA_FOOTER_RESP_PAD		1 /* Cadence only */

#define SDW_CDNS_WRITE_PDI1_BUFFER_SIZE							\
	((SDW_CDNS_BRA_HDR_RESP + SDW_CDNS_BRA_HDR_RESP_PAD +				\
	 SDW_CDNS_BRA_FOOTER_RESP + SDW_CDNS_BRA_FOOTER_RESP_PAD) * 2)

#define SDW_CDNS_READ_PDI0_BUFFER_SIZE							\
	((SDW_CDNS_BRA_HDR + SDW_CDNS_BRA_HDR_CRC + SDW_CDNS_BRA_HDR_CRC_PAD) * 2)

static unsigned int sdw_cdns_bra_actual_data_size(unsigned int allocated_bytes_per_frame)
{
	unsigned int total;

	if (allocated_bytes_per_frame < (SDW_CDNS_BRA_HDR + SDW_CDNS_BRA_HDR_CRC +
					 SDW_CDNS_BRA_HDR_RESP + SDW_CDNS_BRA_DATA_CRC +
					 SDW_CDNS_BRA_FOOTER_RESP))
		return 0;

	total = allocated_bytes_per_frame - SDW_CDNS_BRA_HDR - SDW_CDNS_BRA_HDR_CRC -
		SDW_CDNS_BRA_HDR_RESP - SDW_CDNS_BRA_DATA_CRC - SDW_CDNS_BRA_FOOTER_RESP;

	return total;
}

static unsigned int sdw_cdns_write_pdi0_buffer_size(unsigned int actual_data_size)
{
	unsigned int total;

	total = SDW_CDNS_BRA_HDR + SDW_CDNS_BRA_HDR_CRC + SDW_CDNS_BRA_HDR_CRC_PAD;

	total += actual_data_size;
	if (actual_data_size & 1)
		total += SDW_CDNS_BRA_DATA_PAD;

	total += SDW_CDNS_BRA_DATA_CRC + SDW_CDNS_BRA_DATA_CRC_PAD;

	return total * 2;
}

static unsigned int sdw_cdns_read_pdi1_buffer_size(unsigned int actual_data_size)
{
	unsigned int total;

	total = SDW_CDNS_BRA_HDR_RESP + SDW_CDNS_BRA_HDR_RESP_PAD;

	total += actual_data_size;
	if (actual_data_size & 1)
		total += SDW_CDNS_BRA_DATA_PAD;

	total += SDW_CDNS_BRA_HDR_CRC +	SDW_CDNS_BRA_HDR_CRC_PAD;

	total += SDW_CDNS_BRA_FOOTER_RESP + SDW_CDNS_BRA_FOOTER_RESP_PAD;

	return total * 2;
}

int sdw_cdns_bpt_find_buffer_sizes(int command, /* 0: write, 1: read */
				   int row, int col, unsigned int data_bytes,
				   unsigned int requested_bytes_per_frame,
				   unsigned int *data_per_frame, unsigned int *pdi0_buffer_size,
				   unsigned int *pdi1_buffer_size, unsigned int *num_frames)
{
	unsigned int bpt_bits = row * (col - 1);
	unsigned int bpt_bytes = bpt_bits >> 3;
	unsigned int actual_bpt_bytes;
	unsigned int pdi0_tx_size;
	unsigned int pdi1_rx_size;
	unsigned int remainder;

	if (!data_bytes)
		return -EINVAL;

	actual_bpt_bytes = sdw_cdns_bra_actual_data_size(bpt_bytes);
	if (!actual_bpt_bytes)
		return -EINVAL;

	if (data_bytes < actual_bpt_bytes)
		actual_bpt_bytes = data_bytes;

	/*
	 * the caller may want to set the number of bytes per frame,
	 * allow when possible
	 */
	if (requested_bytes_per_frame < actual_bpt_bytes)
		actual_bpt_bytes = requested_bytes_per_frame;

	*data_per_frame = actual_bpt_bytes;

	if (command == 0) {
		/*
		 * for writes we need to send all the data_bytes per frame,
		 * even for the last frame which may only transport fewer bytes
		 */

		*num_frames = DIV_ROUND_UP(data_bytes, actual_bpt_bytes);

		pdi0_tx_size = sdw_cdns_write_pdi0_buffer_size(actual_bpt_bytes);
		pdi1_rx_size = SDW_CDNS_WRITE_PDI1_BUFFER_SIZE;

		*pdi0_buffer_size = pdi0_tx_size * *num_frames;
		*pdi1_buffer_size = pdi1_rx_size * *num_frames;
	} else {
		/*
		 * for reads we need to retrieve only what is requested in the BPT
		 * header, so the last frame needs to be special-cased
		 */
		*num_frames = data_bytes / actual_bpt_bytes;

		pdi0_tx_size = SDW_CDNS_READ_PDI0_BUFFER_SIZE;
		pdi1_rx_size = sdw_cdns_read_pdi1_buffer_size(actual_bpt_bytes);

		*pdi0_buffer_size = pdi0_tx_size * *num_frames;
		*pdi1_buffer_size = pdi1_rx_size * *num_frames;

		remainder = data_bytes % actual_bpt_bytes;
		if (remainder) {
			pdi0_tx_size = SDW_CDNS_READ_PDI0_BUFFER_SIZE;
			pdi1_rx_size = sdw_cdns_read_pdi1_buffer_size(remainder);

			*num_frames = *num_frames + 1;
			*pdi0_buffer_size += pdi0_tx_size;
			*pdi1_buffer_size += pdi1_rx_size;
		}
	}

	return 0;
}
EXPORT_SYMBOL(sdw_cdns_bpt_find_buffer_sizes);

static int sdw_cdns_copy_write_data(u8 *data, int data_size, u8 *dma_buffer, int dma_buffer_size)
{
	/*
	 * the implementation copies the data one byte at a time. Experiments with
	 * two bytes at a time did not seem to improve the performance
	 */
	int i, j;

	/* size check to prevent out of bounds access */
	i = data_size - 1;
	j = (2 * i) - (i & 1);
	if (data_size & 1)
		j++;
	j += 2;
	if (j >= dma_buffer_size)
		return -EINVAL;

	/* copy data */
	for (i = 0; i < data_size; i++) {
		j = (2 * i) - (i & 1);
		dma_buffer[j] = data[i];
	}
	/* add required pad */
	if (data_size & 1)
		dma_buffer[++j] = 0;
	/* skip last two bytes */
	j += 2;

	/* offset and data are off-by-one */
	return j + 1;
}

static int sdw_cdns_prepare_write_pd0_buffer(u8 *header, unsigned int header_size,
					     u8 *data, unsigned int data_size,
					     u8 *dma_buffer, unsigned int dma_buffer_size,
					     unsigned int *dma_data_written,
					     unsigned int frame_counter)
{
	int data_written;
	u8 *last_byte;
	u8 crc;

	*dma_data_written = 0;

	data_written = sdw_cdns_copy_write_data(header, header_size, dma_buffer, dma_buffer_size);
	if (data_written < 0)
		return data_written;
	dma_buffer[3] = BIT(7);
	dma_buffer[3] |= frame_counter & GENMASK(3, 0);

	dma_buffer += data_written;
	dma_buffer_size -= data_written;
	*dma_data_written += data_written;

	crc = SDW_CRC8_SEED;
	crc = crc8(sdw_crc8_lookup_msb, header, header_size, crc);

	data_written = sdw_cdns_copy_write_data(&crc, 1, dma_buffer, dma_buffer_size);
	if (data_written < 0)
		return data_written;
	dma_buffer += data_written;
	dma_buffer_size -= data_written;
	*dma_data_written += data_written;

	data_written = sdw_cdns_copy_write_data(data, data_size, dma_buffer, dma_buffer_size);
	if (data_written < 0)
		return data_written;
	dma_buffer += data_written;
	dma_buffer_size -= data_written;
	*dma_data_written += data_written;

	crc = SDW_CRC8_SEED;
	crc = crc8(sdw_crc8_lookup_msb, data, data_size, crc);
	data_written = sdw_cdns_copy_write_data(&crc, 1, dma_buffer, dma_buffer_size);
	if (data_written < 0)
		return data_written;
	dma_buffer += data_written;
	dma_buffer_size -= data_written;
	*dma_data_written += data_written;

	/* tag last byte */
	last_byte = dma_buffer - 1;
	last_byte[0] = BIT(6);

	return 0;
}

static int sdw_cdns_prepare_read_pd0_buffer(u8 *header, unsigned int header_size,
					    u8 *dma_buffer, unsigned int dma_buffer_size,
					    unsigned int *dma_data_written,
					    unsigned int frame_counter)
{
	int data_written;
	u8 *last_byte;
	u8 crc;

	*dma_data_written = 0;

	data_written = sdw_cdns_copy_write_data(header, header_size, dma_buffer, dma_buffer_size);
	if (data_written < 0)
		return data_written;
	dma_buffer[3] = BIT(7);
	dma_buffer[3] |= frame_counter & GENMASK(3, 0);

	dma_buffer += data_written;
	dma_buffer_size -= data_written;
	*dma_data_written += data_written;

	crc = SDW_CRC8_SEED;
	crc = crc8(sdw_crc8_lookup_msb, header, header_size, crc);

	data_written = sdw_cdns_copy_write_data(&crc, 1, dma_buffer, dma_buffer_size);
	if (data_written < 0)
		return data_written;
	dma_buffer += data_written;
	dma_buffer_size -= data_written;
	*dma_data_written += data_written;

	/* tag last byte */
	last_byte = dma_buffer - 1;
	last_byte[0] = BIT(6);

	return 0;
}

#define CDNS_BPT_ROLLING_COUNTER_START 1

int sdw_cdns_prepare_write_dma_buffer(u8 dev_num, u32 start_register, u8 *data, int data_size,
				      int data_per_frame, u8 *dma_buffer, int dma_buffer_size,
				      int *dma_buffer_total_bytes)
{
	int total_dma_data_written = 0;
	u8 *p_dma_buffer = dma_buffer;
	u8 header[SDW_CDNS_BRA_HDR];
	int dma_data_written;
	u8 *p_data = data;
	u8 counter;
	int ret;

	counter = CDNS_BPT_ROLLING_COUNTER_START;

	header[0] = BIT(1);		/* write command: BIT(1) set */
	header[0] |= GENMASK(7, 6);	/* header is active */
	header[0] |= (dev_num << 2);

	while (data_size >= data_per_frame) {
		header[1] = data_per_frame;
		header[2] = start_register >> 24 & 0xFF;
		header[3] = start_register >> 16 & 0xFF;
		header[4] = start_register >> 8 & 0xFF;
		header[5] = start_register >> 0 & 0xFF;

		ret = sdw_cdns_prepare_write_pd0_buffer(header, SDW_CDNS_BRA_HDR,
							p_data, data_per_frame,
							p_dma_buffer, dma_buffer_size,
							&dma_data_written, counter);
		if (ret < 0)
			return ret;

		counter++;

		p_data += data_per_frame;
		data_size -= data_per_frame;

		p_dma_buffer += dma_data_written;
		dma_buffer_size -= dma_data_written;
		total_dma_data_written += dma_data_written;

		start_register += data_per_frame;
	}

	if (data_size) {
		header[1] = data_size;
		header[2] = start_register >> 24 & 0xFF;
		header[3] = start_register >> 16 & 0xFF;
		header[4] = start_register >> 8 & 0xFF;
		header[5] = start_register >> 0 & 0xFF;

		ret = sdw_cdns_prepare_write_pd0_buffer(header, SDW_CDNS_BRA_HDR,
							p_data, data_size,
							p_dma_buffer, dma_buffer_size,
							&dma_data_written, counter);
		if (ret < 0)
			return ret;

		total_dma_data_written += dma_data_written;
	}

	*dma_buffer_total_bytes = total_dma_data_written;

	return 0;
}
EXPORT_SYMBOL(sdw_cdns_prepare_write_dma_buffer);

int sdw_cdns_prepare_read_dma_buffer(u8 dev_num, u32 start_register, int data_size,
				     int data_per_frame, u8 *dma_buffer, int dma_buffer_size,
				     int *dma_buffer_total_bytes)
{
	int total_dma_data_written = 0;
	u8 *p_dma_buffer = dma_buffer;
	u8 header[SDW_CDNS_BRA_HDR];
	int dma_data_written;
	u8 counter;
	int ret;

	counter = CDNS_BPT_ROLLING_COUNTER_START;

	header[0] = 0;			/* read command: BIT(1) cleared */
	header[0] |= GENMASK(7, 6);	/* header is active */
	header[0] |= (dev_num << 2);

	while (data_size >= data_per_frame) {
		header[1] = data_per_frame;
		header[2] = start_register >> 24 & 0xFF;
		header[3] = start_register >> 16 & 0xFF;
		header[4] = start_register >> 8 & 0xFF;
		header[5] = start_register >> 0 & 0xFF;

		ret = sdw_cdns_prepare_read_pd0_buffer(header, SDW_CDNS_BRA_HDR, p_dma_buffer,
						       dma_buffer_size, &dma_data_written,
						       counter);
		if (ret < 0)
			return ret;

		counter++;

		data_size -= data_per_frame;

		p_dma_buffer += dma_data_written;
		dma_buffer_size -= dma_data_written;
		total_dma_data_written += dma_data_written;

		start_register += data_per_frame;
	}

	if (data_size) {
		header[1] = data_size;
		header[2] = start_register >> 24 & 0xFF;
		header[3] = start_register >> 16 & 0xFF;
		header[4] = start_register >> 8 & 0xFF;
		header[5] = start_register >> 0 & 0xFF;

		ret = sdw_cdns_prepare_read_pd0_buffer(header, SDW_CDNS_BRA_HDR, p_dma_buffer,
						       dma_buffer_size, &dma_data_written,
						       counter);
		if (ret < 0)
			return ret;

		total_dma_data_written += dma_data_written;
	}

	*dma_buffer_total_bytes = total_dma_data_written;

	return 0;
}
EXPORT_SYMBOL(sdw_cdns_prepare_read_dma_buffer);

static int check_counter(u32 val, u8 counter)
{
	u8 frame;

	frame = (val >> 24) & GENMASK(3, 0);
	if (counter != frame)
		return -EIO;
	return 0;
}

static int check_response(u32 val)
{
	u8 response;

	response = (val >> 3) & GENMASK(1, 0);
	if (response == 0) /* Ignored */
		return -ENODATA;
	if (response != 1) /* ACK */
		return -EIO;

	return 0;
}

static int check_frame_start(u32 header, u8 counter)
{
	int ret;

	/* check frame_start marker */
	if (!(header & BIT(31)))
		return -EIO;

	ret = check_counter(header, counter);
	if (ret < 0)
		return ret;

	return check_response(header);
}

static int check_frame_end(u32 footer)
{
	/* check frame_end marker */
	if (!(footer & BIT(30)))
		return -EIO;

	return check_response(footer);
}

int sdw_cdns_check_write_response(struct device *dev, u8 *dma_buffer,
				  int dma_buffer_size, int num_frames)
{
	u32 *p_data;
	int counter;
	u32 header;
	u32 footer;
	int ret;
	int i;

	/* paranoia check on buffer size */
	if (dma_buffer_size != num_frames * 8)
		return -EINVAL;

	counter = CDNS_BPT_ROLLING_COUNTER_START;
	p_data = (u32 *)dma_buffer;

	for (i = 0; i < num_frames; i++) {
		header = *p_data++;
		footer = *p_data++;

		ret = check_frame_start(header, counter);
		if (ret < 0) {
			dev_err(dev, "%s: bad frame %d/%d start header %x\n",
				__func__, i, num_frames, header);
			return ret;
		}

		ret = check_frame_end(footer);
		if (ret < 0) {
			dev_err(dev, "%s: bad frame %d/%d end footer %x\n",
				__func__, i, num_frames, footer);
			return ret;
		}

		counter++;
		counter &= GENMASK(3, 0);
	}
	return 0;
}
EXPORT_SYMBOL(sdw_cdns_check_write_response);

static u8 extract_read_data(u32 *data, int num_bytes, u8 *buffer)
{
	u32 val;
	int i;
	u8 crc;
	u8 b0;
	u8 b1;

	crc = SDW_CRC8_SEED;

	/* process two bytes at a time */
	for (i = 0; i < num_bytes / 2; i++) {
		val = *data++;

		b0 = val & 0xff;
		b1 = (val >> 8) & 0xff;

		*buffer++ = b0;
		crc = crc8(sdw_crc8_lookup_msb, &b0, 1, crc);

		*buffer++ = b1;
		crc = crc8(sdw_crc8_lookup_msb, &b1, 1, crc);
	}
	/* handle remaining byte if it exists */
	if (num_bytes & 1) {
		val = *data;

		b0 = val & 0xff;

		*buffer++ = b0;
		crc = crc8(sdw_crc8_lookup_msb, &b0, 1, crc);
	}
	return crc;
}

int sdw_cdns_check_read_response(struct device *dev, u8 *dma_buffer, int dma_buffer_size,
				 u8 *buffer, int buffer_size, int num_frames, int data_per_frame)
{
	int total_num_bytes = 0;
	u32 *p_data;
	u8 *p_buf;
	int counter;
	u32 header;
	u32 footer;
	u8 expected_crc;
	u8 crc;
	int len;
	int ret;
	int i;

	counter = CDNS_BPT_ROLLING_COUNTER_START;
	p_data = (u32 *)dma_buffer;
	p_buf = buffer;

	for (i = 0; i < num_frames; i++) {
		header = *p_data++;

		ret = check_frame_start(header, counter);
		if (ret < 0) {
			dev_err(dev, "%s: bad frame %d/%d start header %x\n",
				__func__, i, num_frames, header);
			return ret;
		}

		len = data_per_frame;
		if (total_num_bytes + data_per_frame > buffer_size)
			len = buffer_size - total_num_bytes;

		crc = extract_read_data(p_data, len, p_buf);

		p_data += (len + 1) / 2;
		expected_crc = *p_data++ & 0xff;

		if (crc != expected_crc) {
			dev_err(dev, "%s: bad frame %d/%d crc %#x expected %#x\n",
				__func__, i, num_frames, crc, expected_crc);
			return -EIO;
		}

		p_buf += len;
		total_num_bytes += len;

		footer = *p_data++;
		ret = check_frame_end(footer);
		if (ret < 0) {
			dev_err(dev, "%s: bad frame %d/%d end footer %x\n",
				__func__, i, num_frames, footer);
			return ret;
		}

		counter++;
		counter &= GENMASK(3, 0);
	}
	return 0;
}
EXPORT_SYMBOL(sdw_cdns_check_read_response);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Cadence Soundwire Library");

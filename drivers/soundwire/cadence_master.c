// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * Cadence SoundWire Master module
 * Used by Master driver
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"
#include "cadence_master.h"

#define CDNS_MCP_CONFIG				0x0

#define CDNS_MCP_CONFIG_MCMD_RETRY		GENMASK(27, 24)
#define CDNS_MCP_CONFIG_MPREQ_DELAY		GENMASK(20, 16)
#define CDNS_MCP_CONFIG_MMASTER			BIT(7)
#define CDNS_MCP_CONFIG_BUS_REL			BIT(6)
#define CDNS_MCP_CONFIG_SNIFFER			BIT(5)
#define CDNS_MCP_CONFIG_SSPMOD			BIT(4)
#define CDNS_MCP_CONFIG_CMD			BIT(3)
#define CDNS_MCP_CONFIG_OP			GENMASK(2, 0)
#define CDNS_MCP_CONFIG_OP_NORMAL		0

#define CDNS_MCP_CONTROL			0x4

#define CDNS_MCP_CONTROL_RST_DELAY		GENMASK(10, 8)
#define CDNS_MCP_CONTROL_CMD_RST		BIT(7)
#define CDNS_MCP_CONTROL_SOFT_RST		BIT(6)
#define CDNS_MCP_CONTROL_SW_RST			BIT(5)
#define CDNS_MCP_CONTROL_HW_RST			BIT(4)
#define CDNS_MCP_CONTROL_CLK_PAUSE		BIT(3)
#define CDNS_MCP_CONTROL_CLK_STOP_CLR		BIT(2)
#define CDNS_MCP_CONTROL_CMD_ACCEPT		BIT(1)
#define CDNS_MCP_CONTROL_BLOCK_WAKEUP		BIT(0)


#define CDNS_MCP_CMDCTRL			0x8
#define CDNS_MCP_SSPSTAT			0xC
#define CDNS_MCP_FRAME_SHAPE			0x10
#define CDNS_MCP_FRAME_SHAPE_INIT		0x14

#define CDNS_MCP_CONFIG_UPDATE			0x18
#define CDNS_MCP_CONFIG_UPDATE_BIT		BIT(0)

#define CDNS_MCP_PHYCTRL			0x1C
#define CDNS_MCP_SSP_CTRL0			0x20
#define CDNS_MCP_SSP_CTRL1			0x28
#define CDNS_MCP_CLK_CTRL0			0x30
#define CDNS_MCP_CLK_CTRL1			0x38

#define CDNS_MCP_STAT				0x40

#define CDNS_MCP_STAT_ACTIVE_BANK		BIT(20)
#define CDNS_MCP_STAT_CLK_STOP			BIT(16)

#define CDNS_MCP_INTSTAT			0x44
#define CDNS_MCP_INTMASK			0x48

#define CDNS_MCP_INT_IRQ			BIT(31)
#define CDNS_MCP_INT_WAKEUP			BIT(16)
#define CDNS_MCP_INT_SLAVE_RSVD			BIT(15)
#define CDNS_MCP_INT_SLAVE_ALERT		BIT(14)
#define CDNS_MCP_INT_SLAVE_ATTACH		BIT(13)
#define CDNS_MCP_INT_SLAVE_NATTACH		BIT(12)
#define CDNS_MCP_INT_SLAVE_MASK			GENMASK(15, 12)
#define CDNS_MCP_INT_DPINT			BIT(11)
#define CDNS_MCP_INT_CTRL_CLASH			BIT(10)
#define CDNS_MCP_INT_DATA_CLASH			BIT(9)
#define CDNS_MCP_INT_CMD_ERR			BIT(7)
#define CDNS_MCP_INT_RX_WL			BIT(2)
#define CDNS_MCP_INT_TXE			BIT(1)

#define CDNS_MCP_INTSET				0x4C

#define CDNS_SDW_SLAVE_STAT			0x50
#define CDNS_MCP_SLAVE_STAT_MASK		BIT(1, 0)

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

#define CDNS_MCP_SLAVE_INTMASK0_MASK		GENMASK(30, 0)
#define CDNS_MCP_SLAVE_INTMASK1_MASK		GENMASK(16, 0)

#define CDNS_MCP_PORT_INTSTAT			0x64
#define CDNS_MCP_PDI_STAT			0x6C

#define CDNS_MCP_FIFOLEVEL			0x78
#define CDNS_MCP_FIFOSTAT			0x7C
#define CDNS_MCP_RX_FIFO_AVAIL			GENMASK(5, 0)

#define CDNS_MCP_CMD_BASE			0x80
#define CDNS_MCP_RESP_BASE			0x80
#define CDNS_MCP_CMD_LEN			0x20
#define CDNS_MCP_CMD_WORD_LEN			0x4

#define CDNS_MCP_CMD_SSP_TAG			BIT(31)
#define CDNS_MCP_CMD_COMMAND			GENMASK(30, 28)
#define CDNS_MCP_CMD_DEV_ADDR			GENMASK(27, 24)
#define CDNS_MCP_CMD_REG_ADDR_H			GENMASK(23, 16)
#define CDNS_MCP_CMD_REG_ADDR_L			GENMASK(15, 8)
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
#define CDNS_PORTCTRL_DIRN			BIT(7)
#define CDNS_PORTCTRL_BANK_INVERT		BIT(8)

#define CDNS_PORT_OFFSET			0x80

#define CDNS_PDI_CONFIG(n)			(0x1100 + (n) * 16)

#define CDNS_PDI_CONFIG_SOFT_RESET		BIT(24)
#define CDNS_PDI_CONFIG_CHANNEL			GENMASK(15, 8)
#define CDNS_PDI_CONFIG_PORT			GENMASK(4, 0)

/* Driver defaults */

#define CDNS_DEFAULT_CLK_DIVIDER		0
#define CDNS_DEFAULT_FRAME_SHAPE		0x30
#define CDNS_DEFAULT_SSP_INTERVAL		0x18
#define CDNS_TX_TIMEOUT				2000

#define CDNS_PCM_PDI_OFFSET			0x2
#define CDNS_PDM_PDI_OFFSET			0x6

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

static inline void cdns_updatel(struct sdw_cdns *cdns,
				int offset, u32 mask, u32 val)
{
	u32 tmp;

	tmp = cdns_readl(cdns, offset);
	tmp = (tmp & ~mask) | val;
	cdns_writel(cdns, offset, tmp);
}

static int cdns_clear_bit(struct sdw_cdns *cdns, int offset, u32 value)
{
	int timeout = 10;
	u32 reg_read;

	writel(value, cdns->registers + offset);

	/* Wait for bit to be self cleared */
	do {
		reg_read = readl(cdns->registers + offset);
		if ((reg_read & value) == 0)
			return 0;

		timeout--;
		udelay(50);
	} while (timeout != 0);

	return -EAGAIN;
}

/*
 * IRQ handling
 */

static int cdns_update_slave_status(struct sdw_cdns *cdns,
					u32 slave0, u32 slave1)
{
	enum sdw_slave_status status[SDW_MAX_DEVICES + 1];
	bool is_slave = false;
	u64 slave, mask;
	int i, set_status;

	/* combine the two status */
	slave = ((u64)slave1 << 32) | slave0;
	memset(status, 0, sizeof(status));

	for (i = 0; i <= SDW_MAX_DEVICES; i++) {
		mask = (slave >> (i * CDNS_MCP_SLAVE_STATUS_NUM)) &
				CDNS_MCP_SLAVE_STATUS_BITS;
		if (!mask)
			continue;

		is_slave = true;
		set_status = 0;

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

		/* first check if Slave reported multiple status */
		if (set_status > 1) {
			dev_warn(cdns->dev,
					"Slave reported multiple Status: %d\n",
					status[i]);
			/*
			 * TODO: we need to reread the status here by
			 * issuing a PING cmd
			 */
		}
	}

	if (is_slave)
		return sdw_handle_slave_status(&cdns->bus, status);

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
	int ret = IRQ_HANDLED;

	/* Check if the link is up */
	if (!cdns->link_up)
		return IRQ_NONE;

	int_status = cdns_readl(cdns, CDNS_MCP_INTSTAT);

	if (!(int_status & CDNS_MCP_INT_IRQ))
		return IRQ_NONE;

	if (int_status & CDNS_MCP_INT_CTRL_CLASH) {

		/* Slave is driving bit slot during control word */
		dev_err_ratelimited(cdns->dev, "Bus clash for control word\n");
		int_status |= CDNS_MCP_INT_CTRL_CLASH;
	}

	if (int_status & CDNS_MCP_INT_DATA_CLASH) {
		/*
		 * Multiple slaves trying to drive bit slot, or issue with
		 * ownership of data bits or Slave gone bonkers
		 */
		dev_err_ratelimited(cdns->dev, "Bus clash for data word\n");
		int_status |= CDNS_MCP_INT_DATA_CLASH;
	}

	if (int_status & CDNS_MCP_INT_SLAVE_MASK) {
		/* Mask the Slave interrupt and wake thread */
		cdns_updatel(cdns, CDNS_MCP_INTMASK,
				CDNS_MCP_INT_SLAVE_MASK, 0);

		int_status &= ~CDNS_MCP_INT_SLAVE_MASK;
		ret = IRQ_WAKE_THREAD;
	}

	cdns_writel(cdns, CDNS_MCP_INTSTAT, int_status);
	return ret;
}
EXPORT_SYMBOL(sdw_cdns_irq);

/**
 * sdw_cdns_thread() - Cadence irq thread handler
 * @irq: irq number
 * @dev_id: irq context
 */
irqreturn_t sdw_cdns_thread(int irq, void *dev_id)
{
	struct sdw_cdns *cdns = dev_id;
	u32 slave0, slave1;

	dev_dbg(cdns->dev, "Slave status change\n");

	slave0 = cdns_readl(cdns, CDNS_MCP_SLAVE_INTSTAT0);
	slave1 = cdns_readl(cdns, CDNS_MCP_SLAVE_INTSTAT1);

	cdns_update_slave_status(cdns, slave0, slave1);
	cdns_writel(cdns, CDNS_MCP_SLAVE_INTSTAT0, slave0);
	cdns_writel(cdns, CDNS_MCP_SLAVE_INTSTAT1, slave1);

	/* clear and unmask Slave interrupt now */
	cdns_writel(cdns, CDNS_MCP_INTSTAT, CDNS_MCP_INT_SLAVE_MASK);
	cdns_updatel(cdns, CDNS_MCP_INTMASK,
			CDNS_MCP_INT_SLAVE_MASK, CDNS_MCP_INT_SLAVE_MASK);

	return IRQ_HANDLED;
}
EXPORT_SYMBOL(sdw_cdns_thread);

/*
 * init routines
 */

/**
 * sdw_cdns_init() - Cadence initialization
 * @cdns: Cadence instance
 */
int sdw_cdns_init(struct sdw_cdns *cdns)
{
	u32 val;
	int ret;

	/* Exit clock stop */
	ret = cdns_clear_bit(cdns, CDNS_MCP_CONTROL,
			CDNS_MCP_CONTROL_CLK_STOP_CLR);
	if (ret < 0) {
		dev_err(cdns->dev, "Couldn't exit from clock stop\n");
		return ret;
	}

	/* Set clock divider */
	val = cdns_readl(cdns, CDNS_MCP_CLK_CTRL0);
	val |= CDNS_DEFAULT_CLK_DIVIDER;
	cdns_writel(cdns, CDNS_MCP_CLK_CTRL0, val);

	/* Set the default frame shape */
	cdns_writel(cdns, CDNS_MCP_FRAME_SHAPE_INIT, CDNS_DEFAULT_FRAME_SHAPE);

	/* Set SSP interval to default value */
	cdns_writel(cdns, CDNS_MCP_SSP_CTRL0, CDNS_DEFAULT_SSP_INTERVAL);
	cdns_writel(cdns, CDNS_MCP_SSP_CTRL1, CDNS_DEFAULT_SSP_INTERVAL);

	/* Set cmd accept mode */
	cdns_updatel(cdns, CDNS_MCP_CONTROL, CDNS_MCP_CONTROL_CMD_ACCEPT,
					CDNS_MCP_CONTROL_CMD_ACCEPT);

	/* Configure mcp config */
	val = cdns_readl(cdns, CDNS_MCP_CONFIG);

	/* Set Max cmd retry to 15 */
	val |= CDNS_MCP_CONFIG_MCMD_RETRY;

	/* Set frame delay between PREQ and ping frame to 15 frames */
	val |= 0xF << SDW_REG_SHIFT(CDNS_MCP_CONFIG_MPREQ_DELAY);

	/* Disable auto bus release */
	val &= ~CDNS_MCP_CONFIG_BUS_REL;

	/* Disable sniffer mode */
	val &= ~CDNS_MCP_CONFIG_SNIFFER;

	/* Set cmd mode for Tx and Rx cmds */
	val &= ~CDNS_MCP_CONFIG_CMD;

	/* Set operation to normal */
	val &= ~CDNS_MCP_CONFIG_OP;
	val |= CDNS_MCP_CONFIG_OP_NORMAL;

	cdns_writel(cdns, CDNS_MCP_CONFIG, val);

	return 0;
}
EXPORT_SYMBOL(sdw_cdns_init);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Cadence Soundwire Library");

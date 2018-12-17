/*
 * drivers/i2c/busses/i2c-tegra.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/iopoll.h>

#include <asm/unaligned.h>

#define TEGRA_I2C_TIMEOUT (msecs_to_jiffies(1000))
#define BYTES_PER_FIFO_WORD 4

#define I2C_CNFG				0x000
#define I2C_CNFG_DEBOUNCE_CNT_SHIFT		12
#define I2C_CNFG_PACKET_MODE_EN			BIT(10)
#define I2C_CNFG_NEW_MASTER_FSM			BIT(11)
#define I2C_CNFG_MULTI_MASTER_MODE		BIT(17)
#define I2C_STATUS				0x01C
#define I2C_SL_CNFG				0x020
#define I2C_SL_CNFG_NACK			BIT(1)
#define I2C_SL_CNFG_NEWSL			BIT(2)
#define I2C_SL_ADDR1				0x02c
#define I2C_SL_ADDR2				0x030
#define I2C_TX_FIFO				0x050
#define I2C_RX_FIFO				0x054
#define I2C_PACKET_TRANSFER_STATUS		0x058
#define I2C_FIFO_CONTROL			0x05c
#define I2C_FIFO_CONTROL_TX_FLUSH		BIT(1)
#define I2C_FIFO_CONTROL_RX_FLUSH		BIT(0)
#define I2C_FIFO_CONTROL_TX_TRIG_SHIFT		5
#define I2C_FIFO_CONTROL_RX_TRIG_SHIFT		2
#define I2C_FIFO_STATUS				0x060
#define I2C_FIFO_STATUS_TX_MASK			0xF0
#define I2C_FIFO_STATUS_TX_SHIFT		4
#define I2C_FIFO_STATUS_RX_MASK			0x0F
#define I2C_FIFO_STATUS_RX_SHIFT		0
#define I2C_INT_MASK				0x064
#define I2C_INT_STATUS				0x068
#define I2C_INT_PACKET_XFER_COMPLETE		BIT(7)
#define I2C_INT_ALL_PACKETS_XFER_COMPLETE	BIT(6)
#define I2C_INT_TX_FIFO_OVERFLOW		BIT(5)
#define I2C_INT_RX_FIFO_UNDERFLOW		BIT(4)
#define I2C_INT_NO_ACK				BIT(3)
#define I2C_INT_ARBITRATION_LOST		BIT(2)
#define I2C_INT_TX_FIFO_DATA_REQ		BIT(1)
#define I2C_INT_RX_FIFO_DATA_REQ		BIT(0)
#define I2C_CLK_DIVISOR				0x06c
#define I2C_CLK_DIVISOR_STD_FAST_MODE_SHIFT	16
#define I2C_CLK_MULTIPLIER_STD_FAST_MODE	8

#define DVC_CTRL_REG1				0x000
#define DVC_CTRL_REG1_INTR_EN			BIT(10)
#define DVC_CTRL_REG2				0x004
#define DVC_CTRL_REG3				0x008
#define DVC_CTRL_REG3_SW_PROG			BIT(26)
#define DVC_CTRL_REG3_I2C_DONE_INTR_EN		BIT(30)
#define DVC_STATUS				0x00c
#define DVC_STATUS_I2C_DONE_INTR		BIT(30)

#define I2C_ERR_NONE				0x00
#define I2C_ERR_NO_ACK				0x01
#define I2C_ERR_ARBITRATION_LOST		0x02
#define I2C_ERR_UNKNOWN_INTERRUPT		0x04

#define PACKET_HEADER0_HEADER_SIZE_SHIFT	28
#define PACKET_HEADER0_PACKET_ID_SHIFT		16
#define PACKET_HEADER0_CONT_ID_SHIFT		12
#define PACKET_HEADER0_PROTOCOL_I2C		BIT(4)

#define I2C_HEADER_HIGHSPEED_MODE		BIT(22)
#define I2C_HEADER_CONT_ON_NAK			BIT(21)
#define I2C_HEADER_SEND_START_BYTE		BIT(20)
#define I2C_HEADER_READ				BIT(19)
#define I2C_HEADER_10BIT_ADDR			BIT(18)
#define I2C_HEADER_IE_ENABLE			BIT(17)
#define I2C_HEADER_REPEAT_START			BIT(16)
#define I2C_HEADER_CONTINUE_XFER		BIT(15)
#define I2C_HEADER_MASTER_ADDR_SHIFT		12
#define I2C_HEADER_SLAVE_ADDR_SHIFT		1

#define I2C_CONFIG_LOAD				0x08C
#define I2C_MSTR_CONFIG_LOAD			BIT(0)
#define I2C_SLV_CONFIG_LOAD			BIT(1)
#define I2C_TIMEOUT_CONFIG_LOAD			BIT(2)

#define I2C_CLKEN_OVERRIDE			0x090
#define I2C_MST_CORE_CLKEN_OVR			BIT(0)

#define I2C_CONFIG_LOAD_TIMEOUT			1000000

#define I2C_MST_FIFO_CONTROL			0x0b4
#define I2C_MST_FIFO_CONTROL_RX_FLUSH		BIT(0)
#define I2C_MST_FIFO_CONTROL_TX_FLUSH		BIT(1)
#define I2C_MST_FIFO_CONTROL_RX_TRIG(x)		(((x) - 1) <<  4)
#define I2C_MST_FIFO_CONTROL_TX_TRIG(x)		(((x) - 1) << 16)

#define I2C_MST_FIFO_STATUS			0x0b8
#define I2C_MST_FIFO_STATUS_RX_MASK		0xff
#define I2C_MST_FIFO_STATUS_RX_SHIFT		0
#define I2C_MST_FIFO_STATUS_TX_MASK		0xff0000
#define I2C_MST_FIFO_STATUS_TX_SHIFT		16

/*
 * msg_end_type: The bus control which need to be send at end of transfer.
 * @MSG_END_STOP: Send stop pulse at end of transfer.
 * @MSG_END_REPEAT_START: Send repeat start at end of transfer.
 * @MSG_END_CONTINUE: The following on message is coming and so do not send
 *		stop or repeat start.
 */
enum msg_end_type {
	MSG_END_STOP,
	MSG_END_REPEAT_START,
	MSG_END_CONTINUE,
};

/**
 * struct tegra_i2c_hw_feature : Different HW support on Tegra
 * @has_continue_xfer_support: Continue transfer supports.
 * @has_per_pkt_xfer_complete_irq: Has enable/disable capability for transfer
 *		complete interrupt per packet basis.
 * @has_single_clk_source: The I2C controller has single clock source. Tegra30
 *		and earlier SoCs have two clock sources i.e. div-clk and
 *		fast-clk.
 * @has_config_load_reg: Has the config load register to load the new
 *		configuration.
 * @clk_divisor_hs_mode: Clock divisor in HS mode.
 * @clk_divisor_std_fast_mode: Clock divisor in standard/fast mode. It is
 *		applicable if there is no fast clock source i.e. single clock
 *		source.
 * @clk_divisor_fast_plus_mode: Clock divisor in fast mode plus. It is
 *		applicable if there is no fast clock source (i.e. single
 *		clock source).
 * @has_multi_master_mode: The I2C controller supports running in single-master
 *		or multi-master mode.
 * @has_slcg_override_reg: The I2C controller supports a register that
 *		overrides the second level clock gating.
 * @has_mst_fifo: The I2C controller contains the new MST FIFO interface that
 *		provides additional features and allows for longer messages to
 *		be transferred in one go.
 */
struct tegra_i2c_hw_feature {
	bool has_continue_xfer_support;
	bool has_per_pkt_xfer_complete_irq;
	bool has_single_clk_source;
	bool has_config_load_reg;
	int clk_divisor_hs_mode;
	int clk_divisor_std_fast_mode;
	u16 clk_divisor_fast_plus_mode;
	bool has_multi_master_mode;
	bool has_slcg_override_reg;
	bool has_mst_fifo;
};

/**
 * struct tegra_i2c_dev - per device I2C context
 * @dev: device reference for power management
 * @hw: Tegra I2C HW feature
 * @adapter: core I2C layer adapter information
 * @div_clk: clock reference for div clock of I2C controller
 * @fast_clk: clock reference for fast clock of I2C controller
 * @rst: reset control for the I2C controller
 * @base: ioremapped registers cookie
 * @cont_id: I2C controller ID, used for packet header
 * @irq: IRQ number of transfer complete interrupt
 * @irq_disabled: used to track whether or not the interrupt is enabled
 * @is_dvc: identifies the DVC I2C controller, has a different register layout
 * @msg_complete: transfer completion notifier
 * @msg_err: error code for completed message
 * @msg_buf: pointer to current message data
 * @msg_buf_remaining: size of unsent data in the message buffer
 * @msg_read: identifies read transfers
 * @bus_clk_rate: current I2C bus clock rate
 * @clk_divisor_non_hs_mode: clock divider for non-high-speed modes
 * @is_multimaster_mode: track if I2C controller is in multi-master mode
 * @xfer_lock: lock to serialize transfer submission and processing
 */
struct tegra_i2c_dev {
	struct device *dev;
	const struct tegra_i2c_hw_feature *hw;
	struct i2c_adapter adapter;
	struct clk *div_clk;
	struct clk *fast_clk;
	struct reset_control *rst;
	void __iomem *base;
	int cont_id;
	int irq;
	bool irq_disabled;
	int is_dvc;
	struct completion msg_complete;
	int msg_err;
	u8 *msg_buf;
	size_t msg_buf_remaining;
	int msg_read;
	u32 bus_clk_rate;
	u16 clk_divisor_non_hs_mode;
	bool is_multimaster_mode;
	spinlock_t xfer_lock;
};

static void dvc_writel(struct tegra_i2c_dev *i2c_dev, u32 val,
		       unsigned long reg)
{
	writel(val, i2c_dev->base + reg);
}

static u32 dvc_readl(struct tegra_i2c_dev *i2c_dev, unsigned long reg)
{
	return readl(i2c_dev->base + reg);
}

/*
 * i2c_writel and i2c_readl will offset the register if necessary to talk
 * to the I2C block inside the DVC block
 */
static unsigned long tegra_i2c_reg_addr(struct tegra_i2c_dev *i2c_dev,
	unsigned long reg)
{
	if (i2c_dev->is_dvc)
		reg += (reg >= I2C_TX_FIFO) ? 0x10 : 0x40;
	return reg;
}

static void i2c_writel(struct tegra_i2c_dev *i2c_dev, u32 val,
	unsigned long reg)
{
	writel(val, i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));

	/* Read back register to make sure that register writes completed */
	if (reg != I2C_TX_FIFO)
		readl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));
}

static u32 i2c_readl(struct tegra_i2c_dev *i2c_dev, unsigned long reg)
{
	return readl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg));
}

static void i2c_writesl(struct tegra_i2c_dev *i2c_dev, void *data,
	unsigned long reg, int len)
{
	writesl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg), data, len);
}

static void i2c_readsl(struct tegra_i2c_dev *i2c_dev, void *data,
	unsigned long reg, int len)
{
	readsl(i2c_dev->base + tegra_i2c_reg_addr(i2c_dev, reg), data, len);
}

static void tegra_i2c_mask_irq(struct tegra_i2c_dev *i2c_dev, u32 mask)
{
	u32 int_mask;

	int_mask = i2c_readl(i2c_dev, I2C_INT_MASK) & ~mask;
	i2c_writel(i2c_dev, int_mask, I2C_INT_MASK);
}

static void tegra_i2c_unmask_irq(struct tegra_i2c_dev *i2c_dev, u32 mask)
{
	u32 int_mask;

	int_mask = i2c_readl(i2c_dev, I2C_INT_MASK) | mask;
	i2c_writel(i2c_dev, int_mask, I2C_INT_MASK);
}

static int tegra_i2c_flush_fifos(struct tegra_i2c_dev *i2c_dev)
{
	unsigned long timeout = jiffies + HZ;
	unsigned int offset;
	u32 mask, val;

	if (i2c_dev->hw->has_mst_fifo) {
		mask = I2C_MST_FIFO_CONTROL_TX_FLUSH |
		       I2C_MST_FIFO_CONTROL_RX_FLUSH;
		offset = I2C_MST_FIFO_CONTROL;
	} else {
		mask = I2C_FIFO_CONTROL_TX_FLUSH |
		       I2C_FIFO_CONTROL_RX_FLUSH;
		offset = I2C_FIFO_CONTROL;
	}

	val = i2c_readl(i2c_dev, offset);
	val |= mask;
	i2c_writel(i2c_dev, val, offset);

	while (i2c_readl(i2c_dev, offset) & mask) {
		if (time_after(jiffies, timeout)) {
			dev_warn(i2c_dev->dev, "timeout waiting for fifo flush\n");
			return -ETIMEDOUT;
		}
		msleep(1);
	}
	return 0;
}

static int tegra_i2c_empty_rx_fifo(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;
	int rx_fifo_avail;
	u8 *buf = i2c_dev->msg_buf;
	size_t buf_remaining = i2c_dev->msg_buf_remaining;
	int words_to_transfer;

	if (i2c_dev->hw->has_mst_fifo) {
		val = i2c_readl(i2c_dev, I2C_MST_FIFO_STATUS);
		rx_fifo_avail = (val & I2C_MST_FIFO_STATUS_RX_MASK) >>
			I2C_MST_FIFO_STATUS_RX_SHIFT;
	} else {
		val = i2c_readl(i2c_dev, I2C_FIFO_STATUS);
		rx_fifo_avail = (val & I2C_FIFO_STATUS_RX_MASK) >>
			I2C_FIFO_STATUS_RX_SHIFT;
	}

	/* Rounds down to not include partial word at the end of buf */
	words_to_transfer = buf_remaining / BYTES_PER_FIFO_WORD;
	if (words_to_transfer > rx_fifo_avail)
		words_to_transfer = rx_fifo_avail;

	i2c_readsl(i2c_dev, buf, I2C_RX_FIFO, words_to_transfer);

	buf += words_to_transfer * BYTES_PER_FIFO_WORD;
	buf_remaining -= words_to_transfer * BYTES_PER_FIFO_WORD;
	rx_fifo_avail -= words_to_transfer;

	/*
	 * If there is a partial word at the end of buf, handle it manually to
	 * prevent overwriting past the end of buf
	 */
	if (rx_fifo_avail > 0 && buf_remaining > 0) {
		BUG_ON(buf_remaining > 3);
		val = i2c_readl(i2c_dev, I2C_RX_FIFO);
		val = cpu_to_le32(val);
		memcpy(buf, &val, buf_remaining);
		buf_remaining = 0;
		rx_fifo_avail--;
	}

	BUG_ON(rx_fifo_avail > 0 && buf_remaining > 0);
	i2c_dev->msg_buf_remaining = buf_remaining;
	i2c_dev->msg_buf = buf;

	return 0;
}

static int tegra_i2c_fill_tx_fifo(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;
	int tx_fifo_avail;
	u8 *buf = i2c_dev->msg_buf;
	size_t buf_remaining = i2c_dev->msg_buf_remaining;
	int words_to_transfer;

	if (i2c_dev->hw->has_mst_fifo) {
		val = i2c_readl(i2c_dev, I2C_MST_FIFO_STATUS);
		tx_fifo_avail = (val & I2C_MST_FIFO_STATUS_TX_MASK) >>
			I2C_MST_FIFO_STATUS_TX_SHIFT;
	} else {
		val = i2c_readl(i2c_dev, I2C_FIFO_STATUS);
		tx_fifo_avail = (val & I2C_FIFO_STATUS_TX_MASK) >>
			I2C_FIFO_STATUS_TX_SHIFT;
	}

	/* Rounds down to not include partial word at the end of buf */
	words_to_transfer = buf_remaining / BYTES_PER_FIFO_WORD;

	/* It's very common to have < 4 bytes, so optimize that case. */
	if (words_to_transfer) {
		if (words_to_transfer > tx_fifo_avail)
			words_to_transfer = tx_fifo_avail;

		/*
		 * Update state before writing to FIFO.  If this casues us
		 * to finish writing all bytes (AKA buf_remaining goes to 0) we
		 * have a potential for an interrupt (PACKET_XFER_COMPLETE is
		 * not maskable).  We need to make sure that the isr sees
		 * buf_remaining as 0 and doesn't call us back re-entrantly.
		 */
		buf_remaining -= words_to_transfer * BYTES_PER_FIFO_WORD;
		tx_fifo_avail -= words_to_transfer;
		i2c_dev->msg_buf_remaining = buf_remaining;
		i2c_dev->msg_buf = buf +
			words_to_transfer * BYTES_PER_FIFO_WORD;
		barrier();

		i2c_writesl(i2c_dev, buf, I2C_TX_FIFO, words_to_transfer);

		buf += words_to_transfer * BYTES_PER_FIFO_WORD;
	}

	/*
	 * If there is a partial word at the end of buf, handle it manually to
	 * prevent reading past the end of buf, which could cross a page
	 * boundary and fault.
	 */
	if (tx_fifo_avail > 0 && buf_remaining > 0) {
		BUG_ON(buf_remaining > 3);
		memcpy(&val, buf, buf_remaining);
		val = le32_to_cpu(val);

		/* Again update before writing to FIFO to make sure isr sees. */
		i2c_dev->msg_buf_remaining = 0;
		i2c_dev->msg_buf = NULL;
		barrier();

		i2c_writel(i2c_dev, val, I2C_TX_FIFO);
	}

	return 0;
}

/*
 * One of the Tegra I2C blocks is inside the DVC (Digital Voltage Controller)
 * block.  This block is identical to the rest of the I2C blocks, except that
 * it only supports master mode, it has registers moved around, and it needs
 * some extra init to get it into I2C mode.  The register moves are handled
 * by i2c_readl and i2c_writel
 */
static void tegra_dvc_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;

	val = dvc_readl(i2c_dev, DVC_CTRL_REG3);
	val |= DVC_CTRL_REG3_SW_PROG;
	val |= DVC_CTRL_REG3_I2C_DONE_INTR_EN;
	dvc_writel(i2c_dev, val, DVC_CTRL_REG3);

	val = dvc_readl(i2c_dev, DVC_CTRL_REG1);
	val |= DVC_CTRL_REG1_INTR_EN;
	dvc_writel(i2c_dev, val, DVC_CTRL_REG1);
}

static int tegra_i2c_runtime_resume(struct device *dev)
{
	struct tegra_i2c_dev *i2c_dev = dev_get_drvdata(dev);
	int ret;

	ret = pinctrl_pm_select_default_state(i2c_dev->dev);
	if (ret)
		return ret;

	if (!i2c_dev->hw->has_single_clk_source) {
		ret = clk_enable(i2c_dev->fast_clk);
		if (ret < 0) {
			dev_err(i2c_dev->dev,
				"Enabling fast clk failed, err %d\n", ret);
			return ret;
		}
	}

	ret = clk_enable(i2c_dev->div_clk);
	if (ret < 0) {
		dev_err(i2c_dev->dev,
			"Enabling div clk failed, err %d\n", ret);
		clk_disable(i2c_dev->fast_clk);
		return ret;
	}

	return 0;
}

static int tegra_i2c_runtime_suspend(struct device *dev)
{
	struct tegra_i2c_dev *i2c_dev = dev_get_drvdata(dev);

	clk_disable(i2c_dev->div_clk);
	if (!i2c_dev->hw->has_single_clk_source)
		clk_disable(i2c_dev->fast_clk);

	return pinctrl_pm_select_idle_state(i2c_dev->dev);
}

static int tegra_i2c_wait_for_config_load(struct tegra_i2c_dev *i2c_dev)
{
	unsigned long reg_offset;
	void __iomem *addr;
	u32 val;
	int err;

	if (i2c_dev->hw->has_config_load_reg) {
		reg_offset = tegra_i2c_reg_addr(i2c_dev, I2C_CONFIG_LOAD);
		addr = i2c_dev->base + reg_offset;
		i2c_writel(i2c_dev, I2C_MSTR_CONFIG_LOAD, I2C_CONFIG_LOAD);
		if (in_interrupt())
			err = readl_poll_timeout_atomic(addr, val, val == 0,
					1000, I2C_CONFIG_LOAD_TIMEOUT);
		else
			err = readl_poll_timeout(addr, val, val == 0,
					1000, I2C_CONFIG_LOAD_TIMEOUT);

		if (err) {
			dev_warn(i2c_dev->dev,
				 "timeout waiting for config load\n");
			return err;
		}
	}

	return 0;
}

static int tegra_i2c_init(struct tegra_i2c_dev *i2c_dev)
{
	u32 val;
	int err;
	u32 clk_divisor;

	err = pm_runtime_get_sync(i2c_dev->dev);
	if (err < 0) {
		dev_err(i2c_dev->dev, "runtime resume failed %d\n", err);
		return err;
	}

	reset_control_assert(i2c_dev->rst);
	udelay(2);
	reset_control_deassert(i2c_dev->rst);

	if (i2c_dev->is_dvc)
		tegra_dvc_init(i2c_dev);

	val = I2C_CNFG_NEW_MASTER_FSM | I2C_CNFG_PACKET_MODE_EN |
		(0x2 << I2C_CNFG_DEBOUNCE_CNT_SHIFT);

	if (i2c_dev->hw->has_multi_master_mode)
		val |= I2C_CNFG_MULTI_MASTER_MODE;

	i2c_writel(i2c_dev, val, I2C_CNFG);
	i2c_writel(i2c_dev, 0, I2C_INT_MASK);

	/* Make sure clock divisor programmed correctly */
	clk_divisor = i2c_dev->hw->clk_divisor_hs_mode;
	clk_divisor |= i2c_dev->clk_divisor_non_hs_mode <<
					I2C_CLK_DIVISOR_STD_FAST_MODE_SHIFT;
	i2c_writel(i2c_dev, clk_divisor, I2C_CLK_DIVISOR);

	if (!i2c_dev->is_dvc) {
		u32 sl_cfg = i2c_readl(i2c_dev, I2C_SL_CNFG);

		sl_cfg |= I2C_SL_CNFG_NACK | I2C_SL_CNFG_NEWSL;
		i2c_writel(i2c_dev, sl_cfg, I2C_SL_CNFG);
		i2c_writel(i2c_dev, 0xfc, I2C_SL_ADDR1);
		i2c_writel(i2c_dev, 0x00, I2C_SL_ADDR2);
	}

	if (i2c_dev->hw->has_mst_fifo) {
		val = I2C_MST_FIFO_CONTROL_TX_TRIG(8) |
		      I2C_MST_FIFO_CONTROL_RX_TRIG(1);
		i2c_writel(i2c_dev, val, I2C_MST_FIFO_CONTROL);
	} else {
		val = 7 << I2C_FIFO_CONTROL_TX_TRIG_SHIFT |
			0 << I2C_FIFO_CONTROL_RX_TRIG_SHIFT;
		i2c_writel(i2c_dev, val, I2C_FIFO_CONTROL);
	}

	err = tegra_i2c_flush_fifos(i2c_dev);
	if (err)
		goto err;

	if (i2c_dev->is_multimaster_mode && i2c_dev->hw->has_slcg_override_reg)
		i2c_writel(i2c_dev, I2C_MST_CORE_CLKEN_OVR, I2C_CLKEN_OVERRIDE);

	err = tegra_i2c_wait_for_config_load(i2c_dev);
	if (err)
		goto err;

	if (i2c_dev->irq_disabled) {
		i2c_dev->irq_disabled = false;
		enable_irq(i2c_dev->irq);
	}

err:
	pm_runtime_put(i2c_dev->dev);
	return err;
}

static int tegra_i2c_disable_packet_mode(struct tegra_i2c_dev *i2c_dev)
{
	u32 cnfg;

	/*
	 * NACK interrupt is generated before the I2C controller generates
	 * the STOP condition on the bus. So wait for 2 clock periods
	 * before disabling the controller so that the STOP condition has
	 * been delivered properly.
	 */
	udelay(DIV_ROUND_UP(2 * 1000000, i2c_dev->bus_clk_rate));

	cnfg = i2c_readl(i2c_dev, I2C_CNFG);
	if (cnfg & I2C_CNFG_PACKET_MODE_EN)
		i2c_writel(i2c_dev, cnfg & ~I2C_CNFG_PACKET_MODE_EN, I2C_CNFG);

	return tegra_i2c_wait_for_config_load(i2c_dev);
}

static irqreturn_t tegra_i2c_isr(int irq, void *dev_id)
{
	u32 status;
	const u32 status_err = I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST;
	struct tegra_i2c_dev *i2c_dev = dev_id;
	unsigned long flags;

	status = i2c_readl(i2c_dev, I2C_INT_STATUS);

	spin_lock_irqsave(&i2c_dev->xfer_lock, flags);
	if (status == 0) {
		dev_warn(i2c_dev->dev, "irq status 0 %08x %08x %08x\n",
			 i2c_readl(i2c_dev, I2C_PACKET_TRANSFER_STATUS),
			 i2c_readl(i2c_dev, I2C_STATUS),
			 i2c_readl(i2c_dev, I2C_CNFG));
		i2c_dev->msg_err |= I2C_ERR_UNKNOWN_INTERRUPT;

		if (!i2c_dev->irq_disabled) {
			disable_irq_nosync(i2c_dev->irq);
			i2c_dev->irq_disabled = true;
		}
		goto err;
	}

	if (unlikely(status & status_err)) {
		tegra_i2c_disable_packet_mode(i2c_dev);
		if (status & I2C_INT_NO_ACK)
			i2c_dev->msg_err |= I2C_ERR_NO_ACK;
		if (status & I2C_INT_ARBITRATION_LOST)
			i2c_dev->msg_err |= I2C_ERR_ARBITRATION_LOST;
		goto err;
	}

	if (i2c_dev->msg_read && (status & I2C_INT_RX_FIFO_DATA_REQ)) {
		if (i2c_dev->msg_buf_remaining)
			tegra_i2c_empty_rx_fifo(i2c_dev);
		else
			BUG();
	}

	if (!i2c_dev->msg_read && (status & I2C_INT_TX_FIFO_DATA_REQ)) {
		if (i2c_dev->msg_buf_remaining)
			tegra_i2c_fill_tx_fifo(i2c_dev);
		else
			tegra_i2c_mask_irq(i2c_dev, I2C_INT_TX_FIFO_DATA_REQ);
	}

	i2c_writel(i2c_dev, status, I2C_INT_STATUS);
	if (i2c_dev->is_dvc)
		dvc_writel(i2c_dev, DVC_STATUS_I2C_DONE_INTR, DVC_STATUS);

	if (status & I2C_INT_PACKET_XFER_COMPLETE) {
		BUG_ON(i2c_dev->msg_buf_remaining);
		complete(&i2c_dev->msg_complete);
	}
	goto done;
err:
	/* An error occurred, mask all interrupts */
	tegra_i2c_mask_irq(i2c_dev, I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST |
		I2C_INT_PACKET_XFER_COMPLETE | I2C_INT_TX_FIFO_DATA_REQ |
		I2C_INT_RX_FIFO_DATA_REQ);
	i2c_writel(i2c_dev, status, I2C_INT_STATUS);
	if (i2c_dev->is_dvc)
		dvc_writel(i2c_dev, DVC_STATUS_I2C_DONE_INTR, DVC_STATUS);

	complete(&i2c_dev->msg_complete);
done:
	spin_unlock_irqrestore(&i2c_dev->xfer_lock, flags);
	return IRQ_HANDLED;
}

static int tegra_i2c_xfer_msg(struct tegra_i2c_dev *i2c_dev,
	struct i2c_msg *msg, enum msg_end_type end_state)
{
	u32 packet_header;
	u32 int_mask;
	unsigned long time_left;
	unsigned long flags;

	tegra_i2c_flush_fifos(i2c_dev);

	i2c_dev->msg_buf = msg->buf;
	i2c_dev->msg_buf_remaining = msg->len;
	i2c_dev->msg_err = I2C_ERR_NONE;
	i2c_dev->msg_read = (msg->flags & I2C_M_RD);
	reinit_completion(&i2c_dev->msg_complete);

	spin_lock_irqsave(&i2c_dev->xfer_lock, flags);

	int_mask = I2C_INT_NO_ACK | I2C_INT_ARBITRATION_LOST;
	tegra_i2c_unmask_irq(i2c_dev, int_mask);

	packet_header = (0 << PACKET_HEADER0_HEADER_SIZE_SHIFT) |
			PACKET_HEADER0_PROTOCOL_I2C |
			(i2c_dev->cont_id << PACKET_HEADER0_CONT_ID_SHIFT) |
			(1 << PACKET_HEADER0_PACKET_ID_SHIFT);
	i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	packet_header = msg->len - 1;
	i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	packet_header = I2C_HEADER_IE_ENABLE;
	if (end_state == MSG_END_CONTINUE)
		packet_header |= I2C_HEADER_CONTINUE_XFER;
	else if (end_state == MSG_END_REPEAT_START)
		packet_header |= I2C_HEADER_REPEAT_START;
	if (msg->flags & I2C_M_TEN) {
		packet_header |= msg->addr;
		packet_header |= I2C_HEADER_10BIT_ADDR;
	} else {
		packet_header |= msg->addr << I2C_HEADER_SLAVE_ADDR_SHIFT;
	}
	if (msg->flags & I2C_M_IGNORE_NAK)
		packet_header |= I2C_HEADER_CONT_ON_NAK;
	if (msg->flags & I2C_M_RD)
		packet_header |= I2C_HEADER_READ;
	i2c_writel(i2c_dev, packet_header, I2C_TX_FIFO);

	if (!(msg->flags & I2C_M_RD))
		tegra_i2c_fill_tx_fifo(i2c_dev);

	if (i2c_dev->hw->has_per_pkt_xfer_complete_irq)
		int_mask |= I2C_INT_PACKET_XFER_COMPLETE;
	if (msg->flags & I2C_M_RD)
		int_mask |= I2C_INT_RX_FIFO_DATA_REQ;
	else if (i2c_dev->msg_buf_remaining)
		int_mask |= I2C_INT_TX_FIFO_DATA_REQ;

	tegra_i2c_unmask_irq(i2c_dev, int_mask);
	spin_unlock_irqrestore(&i2c_dev->xfer_lock, flags);
	dev_dbg(i2c_dev->dev, "unmasked irq: %02x\n",
		i2c_readl(i2c_dev, I2C_INT_MASK));

	time_left = wait_for_completion_timeout(&i2c_dev->msg_complete,
						TEGRA_I2C_TIMEOUT);
	tegra_i2c_mask_irq(i2c_dev, int_mask);

	if (time_left == 0) {
		dev_err(i2c_dev->dev, "i2c transfer timed out\n");

		tegra_i2c_init(i2c_dev);
		return -ETIMEDOUT;
	}

	dev_dbg(i2c_dev->dev, "transfer complete: %lu %d %d\n",
		time_left, completion_done(&i2c_dev->msg_complete),
		i2c_dev->msg_err);

	if (likely(i2c_dev->msg_err == I2C_ERR_NONE))
		return 0;

	tegra_i2c_init(i2c_dev);
	if (i2c_dev->msg_err == I2C_ERR_NO_ACK) {
		if (msg->flags & I2C_M_IGNORE_NAK)
			return 0;
		return -EREMOTEIO;
	}

	return -EIO;
}

static int tegra_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
	int num)
{
	struct tegra_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	int i;
	int ret = 0;

	ret = pm_runtime_get_sync(i2c_dev->dev);
	if (ret < 0) {
		dev_err(i2c_dev->dev, "runtime resume failed %d\n", ret);
		return ret;
	}

	for (i = 0; i < num; i++) {
		enum msg_end_type end_type = MSG_END_STOP;

		if (i < (num - 1)) {
			if (msgs[i + 1].flags & I2C_M_NOSTART)
				end_type = MSG_END_CONTINUE;
			else
				end_type = MSG_END_REPEAT_START;
		}
		ret = tegra_i2c_xfer_msg(i2c_dev, &msgs[i], end_type);
		if (ret)
			break;
	}

	pm_runtime_put(i2c_dev->dev);

	return ret ?: i;
}

static u32 tegra_i2c_func(struct i2c_adapter *adap)
{
	struct tegra_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	u32 ret = I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
		  I2C_FUNC_10BIT_ADDR |	I2C_FUNC_PROTOCOL_MANGLING;

	if (i2c_dev->hw->has_continue_xfer_support)
		ret |= I2C_FUNC_NOSTART;
	return ret;
}

static void tegra_i2c_parse_dt(struct tegra_i2c_dev *i2c_dev)
{
	struct device_node *np = i2c_dev->dev->of_node;
	int ret;

	ret = of_property_read_u32(np, "clock-frequency",
			&i2c_dev->bus_clk_rate);
	if (ret)
		i2c_dev->bus_clk_rate = 100000; /* default clock rate */

	i2c_dev->is_multimaster_mode = of_property_read_bool(np,
			"multi-master");
}

static const struct i2c_algorithm tegra_i2c_algo = {
	.master_xfer	= tegra_i2c_xfer,
	.functionality	= tegra_i2c_func,
};

/* payload size is only 12 bit */
static const struct i2c_adapter_quirks tegra_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
	.max_read_len = 4096,
	.max_write_len = 4096 - 12,
};

static const struct tegra_i2c_hw_feature tegra20_i2c_hw = {
	.has_continue_xfer_support = false,
	.has_per_pkt_xfer_complete_irq = false,
	.has_single_clk_source = false,
	.clk_divisor_hs_mode = 3,
	.clk_divisor_std_fast_mode = 0,
	.clk_divisor_fast_plus_mode = 0,
	.has_config_load_reg = false,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = false,
	.has_mst_fifo = false,
};

static const struct tegra_i2c_hw_feature tegra30_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = false,
	.has_single_clk_source = false,
	.clk_divisor_hs_mode = 3,
	.clk_divisor_std_fast_mode = 0,
	.clk_divisor_fast_plus_mode = 0,
	.has_config_load_reg = false,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = false,
	.has_mst_fifo = false,
};

static const struct tegra_i2c_hw_feature tegra114_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.has_single_clk_source = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = false,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = false,
	.has_mst_fifo = false,
};

static const struct tegra_i2c_hw_feature tegra124_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.has_single_clk_source = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = true,
	.has_multi_master_mode = false,
	.has_slcg_override_reg = true,
	.has_mst_fifo = false,
};

static const struct tegra_i2c_hw_feature tegra210_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.has_single_clk_source = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = true,
	.has_multi_master_mode = true,
	.has_slcg_override_reg = true,
	.has_mst_fifo = false,
};

static const struct tegra_i2c_hw_feature tegra194_i2c_hw = {
	.has_continue_xfer_support = true,
	.has_per_pkt_xfer_complete_irq = true,
	.has_single_clk_source = true,
	.clk_divisor_hs_mode = 1,
	.clk_divisor_std_fast_mode = 0x19,
	.clk_divisor_fast_plus_mode = 0x10,
	.has_config_load_reg = true,
	.has_multi_master_mode = true,
	.has_slcg_override_reg = true,
	.has_mst_fifo = true,
};

/* Match table for of_platform binding */
static const struct of_device_id tegra_i2c_of_match[] = {
	{ .compatible = "nvidia,tegra194-i2c", .data = &tegra194_i2c_hw, },
	{ .compatible = "nvidia,tegra210-i2c", .data = &tegra210_i2c_hw, },
	{ .compatible = "nvidia,tegra124-i2c", .data = &tegra124_i2c_hw, },
	{ .compatible = "nvidia,tegra114-i2c", .data = &tegra114_i2c_hw, },
	{ .compatible = "nvidia,tegra30-i2c", .data = &tegra30_i2c_hw, },
	{ .compatible = "nvidia,tegra20-i2c", .data = &tegra20_i2c_hw, },
	{ .compatible = "nvidia,tegra20-i2c-dvc", .data = &tegra20_i2c_hw, },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_i2c_of_match);

static int tegra_i2c_probe(struct platform_device *pdev)
{
	struct tegra_i2c_dev *i2c_dev;
	struct resource *res;
	struct clk *div_clk;
	struct clk *fast_clk;
	void __iomem *base;
	int irq;
	int ret = 0;
	int clk_multiplier = I2C_CLK_MULTIPLIER_STD_FAST_MODE;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no irq resource\n");
		return -EINVAL;
	}
	irq = res->start;

	div_clk = devm_clk_get(&pdev->dev, "div-clk");
	if (IS_ERR(div_clk)) {
		dev_err(&pdev->dev, "missing controller clock\n");
		return PTR_ERR(div_clk);
	}

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	i2c_dev->base = base;
	i2c_dev->div_clk = div_clk;
	i2c_dev->adapter.algo = &tegra_i2c_algo;
	i2c_dev->adapter.quirks = &tegra_i2c_quirks;
	i2c_dev->irq = irq;
	i2c_dev->cont_id = pdev->id;
	i2c_dev->dev = &pdev->dev;

	i2c_dev->rst = devm_reset_control_get_exclusive(&pdev->dev, "i2c");
	if (IS_ERR(i2c_dev->rst)) {
		dev_err(&pdev->dev, "missing controller reset\n");
		return PTR_ERR(i2c_dev->rst);
	}

	tegra_i2c_parse_dt(i2c_dev);

	i2c_dev->hw = of_device_get_match_data(&pdev->dev);
	i2c_dev->is_dvc = of_device_is_compatible(pdev->dev.of_node,
						  "nvidia,tegra20-i2c-dvc");
	init_completion(&i2c_dev->msg_complete);
	spin_lock_init(&i2c_dev->xfer_lock);

	if (!i2c_dev->hw->has_single_clk_source) {
		fast_clk = devm_clk_get(&pdev->dev, "fast-clk");
		if (IS_ERR(fast_clk)) {
			dev_err(&pdev->dev, "missing fast clock\n");
			return PTR_ERR(fast_clk);
		}
		i2c_dev->fast_clk = fast_clk;
	}

	platform_set_drvdata(pdev, i2c_dev);

	if (!i2c_dev->hw->has_single_clk_source) {
		ret = clk_prepare(i2c_dev->fast_clk);
		if (ret < 0) {
			dev_err(i2c_dev->dev, "Clock prepare failed %d\n", ret);
			return ret;
		}
	}

	i2c_dev->clk_divisor_non_hs_mode =
			i2c_dev->hw->clk_divisor_std_fast_mode;
	if (i2c_dev->hw->clk_divisor_fast_plus_mode &&
		(i2c_dev->bus_clk_rate == 1000000))
		i2c_dev->clk_divisor_non_hs_mode =
			i2c_dev->hw->clk_divisor_fast_plus_mode;

	clk_multiplier *= (i2c_dev->clk_divisor_non_hs_mode + 1);
	ret = clk_set_rate(i2c_dev->div_clk,
			   i2c_dev->bus_clk_rate * clk_multiplier);
	if (ret) {
		dev_err(i2c_dev->dev, "Clock rate change failed %d\n", ret);
		goto unprepare_fast_clk;
	}

	ret = clk_prepare(i2c_dev->div_clk);
	if (ret < 0) {
		dev_err(i2c_dev->dev, "Clock prepare failed %d\n", ret);
		goto unprepare_fast_clk;
	}

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = tegra_i2c_runtime_resume(&pdev->dev);
		if (ret < 0) {
			dev_err(&pdev->dev, "runtime resume failed\n");
			goto unprepare_div_clk;
		}
	}

	if (i2c_dev->is_multimaster_mode) {
		ret = clk_enable(i2c_dev->div_clk);
		if (ret < 0) {
			dev_err(i2c_dev->dev, "div_clk enable failed %d\n",
				ret);
			goto disable_rpm;
		}
	}

	ret = tegra_i2c_init(i2c_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize i2c controller\n");
		goto disable_div_clk;
	}

	ret = devm_request_irq(&pdev->dev, i2c_dev->irq,
			tegra_i2c_isr, 0, dev_name(&pdev->dev), i2c_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq %i\n", i2c_dev->irq);
		goto disable_div_clk;
	}

	i2c_set_adapdata(&i2c_dev->adapter, i2c_dev);
	i2c_dev->adapter.owner = THIS_MODULE;
	i2c_dev->adapter.class = I2C_CLASS_DEPRECATED;
	strlcpy(i2c_dev->adapter.name, dev_name(&pdev->dev),
		sizeof(i2c_dev->adapter.name));
	i2c_dev->adapter.dev.parent = &pdev->dev;
	i2c_dev->adapter.nr = pdev->id;
	i2c_dev->adapter.dev.of_node = pdev->dev.of_node;

	ret = i2c_add_numbered_adapter(&i2c_dev->adapter);
	if (ret)
		goto disable_div_clk;

	return 0;

disable_div_clk:
	if (i2c_dev->is_multimaster_mode)
		clk_disable(i2c_dev->div_clk);

disable_rpm:
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_i2c_runtime_suspend(&pdev->dev);

unprepare_div_clk:
	clk_unprepare(i2c_dev->div_clk);

unprepare_fast_clk:
	if (!i2c_dev->hw->has_single_clk_source)
		clk_unprepare(i2c_dev->fast_clk);

	return ret;
}

static int tegra_i2c_remove(struct platform_device *pdev)
{
	struct tegra_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c_dev->adapter);

	if (i2c_dev->is_multimaster_mode)
		clk_disable(i2c_dev->div_clk);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_i2c_runtime_suspend(&pdev->dev);

	clk_unprepare(i2c_dev->div_clk);
	if (!i2c_dev->hw->has_single_clk_source)
		clk_unprepare(i2c_dev->fast_clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static const struct dev_pm_ops tegra_i2c_pm = {
	SET_RUNTIME_PM_OPS(tegra_i2c_runtime_suspend, tegra_i2c_runtime_resume,
			   NULL)
};
#define TEGRA_I2C_PM	(&tegra_i2c_pm)
#else
#define TEGRA_I2C_PM	NULL
#endif

static struct platform_driver tegra_i2c_driver = {
	.probe   = tegra_i2c_probe,
	.remove  = tegra_i2c_remove,
	.driver  = {
		.name  = "tegra-i2c",
		.of_match_table = tegra_i2c_of_match,
		.pm    = TEGRA_I2C_PM,
	},
};

static int __init tegra_i2c_init_driver(void)
{
	return platform_driver_register(&tegra_i2c_driver);
}

static void __exit tegra_i2c_exit_driver(void)
{
	platform_driver_unregister(&tegra_i2c_driver);
}

subsys_initcall(tegra_i2c_init_driver);
module_exit(tegra_i2c_exit_driver);

MODULE_DESCRIPTION("nVidia Tegra2 I2C Bus Controller driver");
MODULE_AUTHOR("Colin Cross");
MODULE_LICENSE("GPL v2");

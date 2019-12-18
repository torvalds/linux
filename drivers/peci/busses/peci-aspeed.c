// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2012-2017 ASPEED Technology Inc.
// Copyright (c) 2018-2019 Intel Corporation

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/peci.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

/* ASPEED PECI Registers */
/* Control Register */
#define ASPEED_PECI_CTRL			0x00
#define   ASPEED_PECI_CTRL_SAMPLING_MASK	GENMASK(19, 16)
#define   ASPEED_PECI_CTRL_READ_MODE_MASK	GENMASK(13, 12)
#define   ASPEED_PECI_CTRL_READ_MODE_COUNT	BIT(12)
#define   ASPEED_PECI_CTRL_READ_MODE_DBG	BIT(13)
#define   ASPEED_PECI_CTRL_CLK_SOURCE_MASK	BIT(11)
#define   ASPEED_PECI_CTRL_CLK_DIV_MASK		GENMASK(10, 8)
#define   ASPEED_PECI_CTRL_INVERT_OUT		BIT(7)
#define   ASPEED_PECI_CTRL_INVERT_IN		BIT(6)
#define   ASPEED_PECI_CTRL_BUS_CONTENT_EN	BIT(5)
#define   ASPEED_PECI_CTRL_PECI_EN		BIT(4)
#define   ASPEED_PECI_CTRL_PECI_CLK_EN		BIT(0)

/* Timing Negotiation Register */
#define ASPEED_PECI_TIMING_NEGOTIATION		0x04
#define   ASPEED_PECI_TIMING_MESSAGE_MASK	GENMASK(15, 8)
#define   ASPEED_PECI_TIMING_ADDRESS_MASK	GENMASK(7, 0)

/* Command Register */
#define ASPEED_PECI_CMD				0x08
#define   ASPEED_PECI_CMD_PIN_MON		BIT(31)
#define   ASPEED_PECI_CMD_STS_MASK		GENMASK(27, 24)
#define   ASPEED_PECI_CMD_IDLE_MASK		\
	  (ASPEED_PECI_CMD_STS_MASK | ASPEED_PECI_CMD_PIN_MON)
#define   ASPEED_PECI_CMD_FIRE			BIT(0)

/* Read/Write Length Register */
#define ASPEED_PECI_RW_LENGTH			0x0c
#define   ASPEED_PECI_AW_FCS_EN			BIT(31)
#define   ASPEED_PECI_READ_LEN_MASK		GENMASK(23, 16)
#define   ASPEED_PECI_WRITE_LEN_MASK		GENMASK(15, 8)
#define   ASPEED_PECI_TAGET_ADDR_MASK		GENMASK(7, 0)

/* Expected FCS Data Register */
#define ASPEED_PECI_EXP_FCS			0x10
#define   ASPEED_PECI_EXP_READ_FCS_MASK		GENMASK(23, 16)
#define   ASPEED_PECI_EXP_AW_FCS_AUTO_MASK	GENMASK(15, 8)
#define   ASPEED_PECI_EXP_WRITE_FCS_MASK	GENMASK(7, 0)

/* Captured FCS Data Register */
#define ASPEED_PECI_CAP_FCS			0x14
#define   ASPEED_PECI_CAP_READ_FCS_MASK		GENMASK(23, 16)
#define   ASPEED_PECI_CAP_WRITE_FCS_MASK	GENMASK(7, 0)

/* Interrupt Register */
#define ASPEED_PECI_INT_CTRL			0x18
#define   ASPEED_PECI_TIMING_NEGO_SEL_MASK	GENMASK(31, 30)
#define     ASPEED_PECI_1ST_BIT_OF_ADDR_NEGO	0
#define     ASPEED_PECI_2ND_BIT_OF_ADDR_NEGO	1
#define     ASPEED_PECI_MESSAGE_NEGO		2
#define   ASPEED_PECI_INT_MASK			GENMASK(4, 0)
#define   ASPEED_PECI_INT_BUS_TIMEOUT		BIT(4)
#define   ASPEED_PECI_INT_BUS_CONNECT		BIT(3)
#define   ASPEED_PECI_INT_W_FCS_BAD		BIT(2)
#define   ASPEED_PECI_INT_W_FCS_ABORT		BIT(1)
#define   ASPEED_PECI_INT_CMD_DONE		BIT(0)

/* Interrupt Status Register */
#define ASPEED_PECI_INT_STS			0x1c
#define   ASPEED_PECI_INT_TIMING_RESULT_MASK	GENMASK(29, 16)
	  /* bits[4..0]: Same bit fields in the 'Interrupt Register' */

/* Rx/Tx Data Buffer Registers */
#define ASPEED_PECI_W_DATA0			0x20
#define ASPEED_PECI_W_DATA1			0x24
#define ASPEED_PECI_W_DATA2			0x28
#define ASPEED_PECI_W_DATA3			0x2c
#define ASPEED_PECI_R_DATA0			0x30
#define ASPEED_PECI_R_DATA1			0x34
#define ASPEED_PECI_R_DATA2			0x38
#define ASPEED_PECI_R_DATA3			0x3c
#define ASPEED_PECI_W_DATA4			0x40
#define ASPEED_PECI_W_DATA5			0x44
#define ASPEED_PECI_W_DATA6			0x48
#define ASPEED_PECI_W_DATA7			0x4c
#define ASPEED_PECI_R_DATA4			0x50
#define ASPEED_PECI_R_DATA5			0x54
#define ASPEED_PECI_R_DATA6			0x58
#define ASPEED_PECI_R_DATA7			0x5c
#define   ASPEED_PECI_DATA_BUF_SIZE_MAX		32

/* Timing Negotiation */
#define ASPEED_PECI_RD_SAMPLING_POINT_DEFAULT	8
#define ASPEED_PECI_RD_SAMPLING_POINT_MAX	15
#define ASPEED_PECI_CLK_DIV_DEFAULT		0
#define ASPEED_PECI_CLK_DIV_MAX			7
#define ASPEED_PECI_MSG_TIMING_DEFAULT		1
#define ASPEED_PECI_MSG_TIMING_MAX		255
#define ASPEED_PECI_ADDR_TIMING_DEFAULT		1
#define ASPEED_PECI_ADDR_TIMING_MAX		255

/* Timeout */
#define ASPEED_PECI_IDLE_CHECK_TIMEOUT_USEC	50000
#define ASPEED_PECI_IDLE_CHECK_INTERVAL_USEC	10000
#define ASPEED_PECI_CMD_TIMEOUT_MS_DEFAULT	1000
#define ASPEED_PECI_CMD_TIMEOUT_MS_MAX		60000

struct aspeed_peci {
	struct peci_adapter	*adapter;
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	struct reset_control	*rst;
	int			irq;
	spinlock_t		lock; /* to sync completion status handling */
	struct completion	xfer_complete;
	u32			status;
	u32			cmd_timeout_ms;
};

static inline int aspeed_peci_check_idle(struct aspeed_peci *priv)
{
	u32 cmd_sts;

	return readl_poll_timeout(priv->base + ASPEED_PECI_CMD,
				  cmd_sts,
				  !(cmd_sts & ASPEED_PECI_CMD_IDLE_MASK),
				  ASPEED_PECI_IDLE_CHECK_INTERVAL_USEC,
				  ASPEED_PECI_IDLE_CHECK_TIMEOUT_USEC);
}

static int aspeed_peci_xfer(struct peci_adapter *adapter,
			    struct peci_xfer_msg *msg)
{
	struct aspeed_peci *priv = peci_get_adapdata(adapter);
	long err, timeout = msecs_to_jiffies(priv->cmd_timeout_ms);
	u32 peci_head, peci_state, rx_data = 0;
	ulong flags;
	int i, ret;
	uint reg;

	if (msg->tx_len > ASPEED_PECI_DATA_BUF_SIZE_MAX ||
	    msg->rx_len > ASPEED_PECI_DATA_BUF_SIZE_MAX)
		return -EINVAL;

	/* Check command sts and bus idle state */
	ret = aspeed_peci_check_idle(priv);
	if (ret)
		return ret; /* -ETIMEDOUT */

	spin_lock_irqsave(&priv->lock, flags);
	reinit_completion(&priv->xfer_complete);

	peci_head = FIELD_PREP(ASPEED_PECI_TAGET_ADDR_MASK, msg->addr) |
		    FIELD_PREP(ASPEED_PECI_WRITE_LEN_MASK, msg->tx_len) |
		    FIELD_PREP(ASPEED_PECI_READ_LEN_MASK, msg->rx_len);

	writel(peci_head, priv->base + ASPEED_PECI_RW_LENGTH);

	for (i = 0; i < msg->tx_len; i += 4) {
		reg = i < 16 ? ASPEED_PECI_W_DATA0 + i % 16 :
			       ASPEED_PECI_W_DATA4 + i % 16;
		writel(le32_to_cpup((__le32 *)&msg->tx_buf[i]),
		       priv->base + reg);
	}

	dev_dbg(priv->dev, "HEAD : 0x%08x\n", peci_head);
	print_hex_dump_debug("TX : ", DUMP_PREFIX_NONE, 16, 1,
			     msg->tx_buf, msg->tx_len, true);

	priv->status = 0;
	writel(ASPEED_PECI_CMD_FIRE, priv->base + ASPEED_PECI_CMD);
	spin_unlock_irqrestore(&priv->lock, flags);

	err = wait_for_completion_interruptible_timeout(&priv->xfer_complete,
							timeout);

	spin_lock_irqsave(&priv->lock, flags);
	dev_dbg(priv->dev, "INT_STS : 0x%08x\n", priv->status);
	peci_state = readl(priv->base + ASPEED_PECI_CMD);
	dev_dbg(priv->dev, "PECI_STATE : 0x%lx\n",
		FIELD_GET(ASPEED_PECI_CMD_STS_MASK, peci_state));

	writel(0, priv->base + ASPEED_PECI_CMD);

	if (err <= 0 || priv->status != ASPEED_PECI_INT_CMD_DONE) {
		if (err < 0) { /* -ERESTARTSYS */
			ret = (int)err;
			goto err_irqrestore;
		} else if (err == 0) {
			dev_dbg(priv->dev, "Timeout waiting for a response!\n");
			ret = -ETIMEDOUT;
			goto err_irqrestore;
		}

		dev_dbg(priv->dev, "No valid response!\n");
		ret = -EIO;
		goto err_irqrestore;
	}

	/*
	 * Note that rx_len and rx_buf size can be an odd number.
	 * Byte handling is more efficient.
	 */
	for (i = 0; i < msg->rx_len; i++) {
		u8 byte_offset = i % 4;

		if (byte_offset == 0) {
			reg = i < 16 ? ASPEED_PECI_R_DATA0 + i % 16 :
				       ASPEED_PECI_R_DATA4 + i % 16;
			rx_data = readl(priv->base + reg);
		}

		msg->rx_buf[i] = (u8)(rx_data >> (byte_offset << 3));
	}

	print_hex_dump_debug("RX : ", DUMP_PREFIX_NONE, 16, 1,
			     msg->rx_buf, msg->rx_len, true);

	peci_state = readl(priv->base + ASPEED_PECI_CMD);
	dev_dbg(priv->dev, "PECI_STATE : 0x%lx\n",
		FIELD_GET(ASPEED_PECI_CMD_STS_MASK, peci_state));
	dev_dbg(priv->dev, "------------------------\n");

err_irqrestore:
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static irqreturn_t aspeed_peci_irq_handler(int irq, void *arg)
{
	struct aspeed_peci *priv = arg;
	u32 status;

	spin_lock(&priv->lock);
	status = readl(priv->base + ASPEED_PECI_INT_STS);
	writel(status, priv->base + ASPEED_PECI_INT_STS);
	priv->status |= (status & ASPEED_PECI_INT_MASK);

	/*
	 * In most cases, interrupt bits will be set one by one but also note
	 * that multiple interrupt bits could be set at the same time.
	 */
	if (status & ASPEED_PECI_INT_BUS_TIMEOUT)
		dev_dbg(priv->dev, "ASPEED_PECI_INT_BUS_TIMEOUT\n");

	if (status & ASPEED_PECI_INT_BUS_CONNECT)
		dev_dbg(priv->dev, "ASPEED_PECI_INT_BUS_CONNECT\n");

	if (status & ASPEED_PECI_INT_W_FCS_BAD)
		dev_dbg(priv->dev, "ASPEED_PECI_INT_W_FCS_BAD\n");

	if (status & ASPEED_PECI_INT_W_FCS_ABORT)
		dev_dbg(priv->dev, "ASPEED_PECI_INT_W_FCS_ABORT\n");

	/*
	 * All commands should be ended up with a ASPEED_PECI_INT_CMD_DONE bit
	 * set even in an error case.
	 */
	if (status & ASPEED_PECI_INT_CMD_DONE) {
		dev_dbg(priv->dev, "ASPEED_PECI_INT_CMD_DONE\n");
		complete(&priv->xfer_complete);
	}

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static int aspeed_peci_init_ctrl(struct aspeed_peci *priv)
{
	u32 msg_timing, addr_timing, rd_sampling_point;
	u32 clk_freq, clk_divisor, clk_div_val = 0;
	int ret;

	priv->clk = devm_clk_get(priv->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(priv->dev, "Failed to get clk source.\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(priv->dev, "Failed to enable clock.\n");
		return ret;
	}

	ret = device_property_read_u32(priv->dev, "clock-frequency", &clk_freq);
	if (ret) {
		dev_err(priv->dev,
			"Could not read clock-frequency property.\n");
		clk_disable_unprepare(priv->clk);
		return ret;
	}

	clk_divisor = clk_get_rate(priv->clk) / clk_freq;

	while ((clk_divisor >> 1) && (clk_div_val < ASPEED_PECI_CLK_DIV_MAX))
		clk_div_val++;

	ret = device_property_read_u32(priv->dev, "msg-timing", &msg_timing);
	if (ret || msg_timing > ASPEED_PECI_MSG_TIMING_MAX) {
		if (!ret)
			dev_warn(priv->dev,
				 "Invalid msg-timing : %u, Use default : %u\n",
				 msg_timing, ASPEED_PECI_MSG_TIMING_DEFAULT);
		msg_timing = ASPEED_PECI_MSG_TIMING_DEFAULT;
	}

	ret = device_property_read_u32(priv->dev, "addr-timing", &addr_timing);
	if (ret || addr_timing > ASPEED_PECI_ADDR_TIMING_MAX) {
		if (!ret)
			dev_warn(priv->dev,
				 "Invalid addr-timing : %u, Use default : %u\n",
				 addr_timing, ASPEED_PECI_ADDR_TIMING_DEFAULT);
		addr_timing = ASPEED_PECI_ADDR_TIMING_DEFAULT;
	}

	ret = device_property_read_u32(priv->dev, "rd-sampling-point",
				       &rd_sampling_point);
	if (ret || rd_sampling_point > ASPEED_PECI_RD_SAMPLING_POINT_MAX) {
		if (!ret)
			dev_warn(priv->dev,
				 "Invalid rd-sampling-point : %u. Use default : %u\n",
				 rd_sampling_point,
				 ASPEED_PECI_RD_SAMPLING_POINT_DEFAULT);
		rd_sampling_point = ASPEED_PECI_RD_SAMPLING_POINT_DEFAULT;
	}

	ret = device_property_read_u32(priv->dev, "cmd-timeout-ms",
				       &priv->cmd_timeout_ms);
	if (ret || priv->cmd_timeout_ms > ASPEED_PECI_CMD_TIMEOUT_MS_MAX ||
	    priv->cmd_timeout_ms == 0) {
		if (!ret)
			dev_warn(priv->dev,
				 "Invalid cmd-timeout-ms : %u. Use default : %u\n",
				 priv->cmd_timeout_ms,
				 ASPEED_PECI_CMD_TIMEOUT_MS_DEFAULT);
		priv->cmd_timeout_ms = ASPEED_PECI_CMD_TIMEOUT_MS_DEFAULT;
	}

	writel(FIELD_PREP(ASPEED_PECI_CTRL_CLK_DIV_MASK,
			  ASPEED_PECI_CLK_DIV_DEFAULT) |
	       ASPEED_PECI_CTRL_PECI_CLK_EN, priv->base + ASPEED_PECI_CTRL);

	/*
	 * Timing negotiation period setting.
	 * The unit of the programmed value is 4 times of PECI clock period.
	 */
	writel(FIELD_PREP(ASPEED_PECI_TIMING_MESSAGE_MASK, msg_timing) |
	       FIELD_PREP(ASPEED_PECI_TIMING_ADDRESS_MASK, addr_timing),
	       priv->base + ASPEED_PECI_TIMING_NEGOTIATION);

	/* Clear interrupts */
	writel(readl(priv->base + ASPEED_PECI_INT_STS) | ASPEED_PECI_INT_MASK,
	       priv->base + ASPEED_PECI_INT_STS);

	/* Set timing negotiation mode and enable interrupts */
	writel(FIELD_PREP(ASPEED_PECI_TIMING_NEGO_SEL_MASK,
			  ASPEED_PECI_1ST_BIT_OF_ADDR_NEGO) |
	       ASPEED_PECI_INT_MASK, priv->base + ASPEED_PECI_INT_CTRL);

	/* Read sampling point and clock speed setting */
	writel(FIELD_PREP(ASPEED_PECI_CTRL_SAMPLING_MASK, rd_sampling_point) |
	       FIELD_PREP(ASPEED_PECI_CTRL_CLK_DIV_MASK, clk_div_val) |
	       ASPEED_PECI_CTRL_PECI_EN | ASPEED_PECI_CTRL_PECI_CLK_EN,
	       priv->base + ASPEED_PECI_CTRL);

	return 0;
}

static int aspeed_peci_probe(struct platform_device *pdev)
{
	struct peci_adapter *adapter;
	struct aspeed_peci *priv;
	int ret;

	adapter = peci_alloc_adapter(&pdev->dev, sizeof(*priv));
	if (!adapter)
		return -ENOMEM;

	priv = peci_get_adapdata(adapter);
	priv->adapter = adapter;
	priv->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, priv);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto err_put_adapter_dev;
	}

	priv->irq = platform_get_irq(pdev, 0);
	if (!priv->irq) {
		ret = -ENODEV;
		goto err_put_adapter_dev;
	}

	ret = devm_request_irq(&pdev->dev, priv->irq, aspeed_peci_irq_handler,
			       0, "peci-aspeed-irq", priv);
	if (ret)
		goto err_put_adapter_dev;

	init_completion(&priv->xfer_complete);
	spin_lock_init(&priv->lock);

	priv->adapter->owner = THIS_MODULE;
	priv->adapter->dev.of_node = of_node_get(dev_of_node(priv->dev));
	strlcpy(priv->adapter->name, pdev->name, sizeof(priv->adapter->name));
	priv->adapter->xfer = aspeed_peci_xfer;
	priv->adapter->use_dma = false;

	priv->rst = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(priv->rst)) {
		dev_err(&pdev->dev,
			"missing or invalid reset controller entry\n");
		ret = PTR_ERR(priv->rst);
		goto err_put_adapter_dev;
	}
	reset_control_deassert(priv->rst);

	ret = aspeed_peci_init_ctrl(priv);
	if (ret)
		goto err_put_adapter_dev;

	ret = peci_add_adapter(priv->adapter);
	if (ret)
		goto err_put_adapter_dev;

	dev_info(&pdev->dev, "peci bus %d registered, irq %d\n",
		 priv->adapter->nr, priv->irq);

	return 0;

err_put_adapter_dev:
	put_device(&adapter->dev);

	return ret;
}

static int aspeed_peci_remove(struct platform_device *pdev)
{
	struct aspeed_peci *priv = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(priv->clk);
	reset_control_assert(priv->rst);
	peci_del_adapter(priv->adapter);
	of_node_put(priv->adapter->dev.of_node);

	return 0;
}

static const struct of_device_id aspeed_peci_of_table[] = {
	{ .compatible = "aspeed,ast2400-peci", },
	{ .compatible = "aspeed,ast2500-peci", },
	{ .compatible = "aspeed,ast2600-peci", },
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_peci_of_table);

static struct platform_driver aspeed_peci_driver = {
	.probe  = aspeed_peci_probe,
	.remove = aspeed_peci_remove,
	.driver = {
		.name           = KBUILD_MODNAME,
		.of_match_table = of_match_ptr(aspeed_peci_of_table),
	},
};
module_platform_driver(aspeed_peci_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_DESCRIPTION("ASPEED PECI driver");
MODULE_LICENSE("GPL v2");

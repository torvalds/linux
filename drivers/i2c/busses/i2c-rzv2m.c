// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Renesas RZ/V2M I2C unit
 *
 * Copyright (C) 2016-2022 Renesas Electronics Corporation
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

/* Register offsets */
#define IICB0DAT	0x00		/* Data Register */
#define IICB0CTL0	0x08		/* Control Register 0 */
#define IICB0TRG	0x0C		/* Trigger Register */
#define IICB0STR0	0x10		/* Status Register 0 */
#define IICB0CTL1	0x20		/* Control Register 1 */
#define IICB0WL		0x24		/* Low Level Width Setting Reg */
#define IICB0WH		0x28		/* How Level Width Setting Reg */

/* IICB0CTL0 */
#define IICB0IICE	BIT(7)		/* I2C Enable */
#define IICB0SLWT	BIT(1)		/* Interrupt Request Timing */
#define IICB0SLAC	BIT(0)		/* Acknowledge */

/* IICB0TRG */
#define IICB0WRET	BIT(2)		/* Quit Wait Trigger */
#define IICB0STT	BIT(1)		/* Create Start Condition Trigger */
#define IICB0SPT	BIT(0)		/* Create Stop Condition Trigger */

/* IICB0STR0 */
#define IICB0SSAC	BIT(8)		/* Ack Flag */
#define IICB0SSBS	BIT(6)		/* Bus Flag */
#define IICB0SSSP	BIT(4)		/* Stop Condition Flag */

/* IICB0CTL1 */
#define IICB0MDSC	BIT(7)		/* Bus Mode */
#define IICB0SLSE	BIT(1)		/* Start condition output */

struct rzv2m_i2c_priv {
	void __iomem *base;
	struct i2c_adapter adap;
	struct clk *clk;
	int bus_mode;
	struct completion msg_tia_done;
	u32 iicb0wl;
	u32 iicb0wh;
};

enum bcr_index {
	RZV2M_I2C_100K = 0,
	RZV2M_I2C_400K,
};

struct bitrate_config {
	unsigned int percent_low;
	unsigned int min_hold_time_ns;
};

static const struct bitrate_config bitrate_configs[] = {
	[RZV2M_I2C_100K] = { 47, 3450 },
	[RZV2M_I2C_400K] = { 52, 900 },
};

static inline void bit_setl(void __iomem *addr, u32 val)
{
	writel(readl(addr) | val, addr);
}

static inline void bit_clrl(void __iomem *addr, u32 val)
{
	writel(readl(addr) & ~val, addr);
}

static irqreturn_t rzv2m_i2c_tia_irq_handler(int this_irq, void *dev_id)
{
	struct rzv2m_i2c_priv *priv = dev_id;

	complete(&priv->msg_tia_done);

	return IRQ_HANDLED;
}

/* Calculate IICB0WL and IICB0WH */
static int rzv2m_i2c_clock_calculate(struct device *dev,
				     struct rzv2m_i2c_priv *priv)
{
	const struct bitrate_config *config;
	unsigned int hold_time_ns;
	unsigned int total_pclks;
	unsigned int trf_pclks;
	unsigned long pclk_hz;
	struct i2c_timings t;
	u32 trf_ns;

	i2c_parse_fw_timings(dev, &t, true);

	pclk_hz = clk_get_rate(priv->clk);
	total_pclks = pclk_hz / t.bus_freq_hz;

	trf_ns = t.scl_rise_ns + t.scl_fall_ns;
	trf_pclks = mul_u64_u32_div(pclk_hz, trf_ns, NSEC_PER_SEC);

	/* Config setting */
	switch (t.bus_freq_hz) {
	case I2C_MAX_FAST_MODE_FREQ:
		priv->bus_mode = RZV2M_I2C_400K;
		break;
	case I2C_MAX_STANDARD_MODE_FREQ:
		priv->bus_mode = RZV2M_I2C_100K;
		break;
	default:
		dev_err(dev, "transfer speed is invalid\n");
		return -EINVAL;
	}
	config = &bitrate_configs[priv->bus_mode];

	/* IICB0WL = (percent_low / Transfer clock) x PCLK */
	priv->iicb0wl = total_pclks * config->percent_low / 100;
	if (priv->iicb0wl > (BIT(10) - 1))
		return -EINVAL;

	/* IICB0WH = ((percent_high / Transfer clock) x PCLK) - (tR + tF) */
	priv->iicb0wh = total_pclks - priv->iicb0wl - trf_pclks;
	if (priv->iicb0wh > (BIT(10) - 1))
		return -EINVAL;

	/*
	 * Data hold time must be less than 0.9us in fast mode and
	 * 3.45us in standard mode.
	 * Data hold time = IICB0WL[9:2] / PCLK
	 */
	hold_time_ns = div64_ul((u64)(priv->iicb0wl >> 2) * NSEC_PER_SEC, pclk_hz);
	if (hold_time_ns > config->min_hold_time_ns) {
		dev_err(dev, "data hold time %dns is over %dns\n",
			hold_time_ns, config->min_hold_time_ns);
		return -EINVAL;
	}

	return 0;
}

static void rzv2m_i2c_init(struct rzv2m_i2c_priv *priv)
{
	u32 i2c_ctl0;
	u32 i2c_ctl1;

	/* i2c disable */
	writel(0, priv->base + IICB0CTL0);

	/* IICB0CTL1 setting */
	i2c_ctl1 = IICB0SLSE;
	if (priv->bus_mode == RZV2M_I2C_400K)
		i2c_ctl1 |= IICB0MDSC;
	writel(i2c_ctl1, priv->base + IICB0CTL1);

	/* IICB0WL IICB0WH setting */
	writel(priv->iicb0wl, priv->base + IICB0WL);
	writel(priv->iicb0wh, priv->base + IICB0WH);

	/* i2c enable after setting */
	i2c_ctl0 = IICB0SLWT | IICB0SLAC | IICB0IICE;
	writel(i2c_ctl0, priv->base + IICB0CTL0);
}

static int rzv2m_i2c_write_with_ack(struct rzv2m_i2c_priv *priv, u32 data)
{
	unsigned long time_left;

	reinit_completion(&priv->msg_tia_done);

	writel(data, priv->base + IICB0DAT);

	time_left = wait_for_completion_timeout(&priv->msg_tia_done,
						priv->adap.timeout);
	if (!time_left)
		return -ETIMEDOUT;

	/* Confirm ACK */
	if ((readl(priv->base + IICB0STR0) & IICB0SSAC) != IICB0SSAC)
		return -ENXIO;

	return 0;
}

static int rzv2m_i2c_read_with_ack(struct rzv2m_i2c_priv *priv, u8 *data,
				   bool last)
{
	unsigned long time_left;
	u32 data_tmp;

	reinit_completion(&priv->msg_tia_done);

	/* Interrupt request timing : 8th clock */
	bit_clrl(priv->base + IICB0CTL0, IICB0SLWT);

	/* Exit the wait state */
	writel(IICB0WRET, priv->base + IICB0TRG);

	/* Wait for transaction */
	time_left = wait_for_completion_timeout(&priv->msg_tia_done,
						priv->adap.timeout);
	if (!time_left)
		return -ETIMEDOUT;

	if (last) {
		/* Disable ACK */
		bit_clrl(priv->base + IICB0CTL0, IICB0SLAC);

		/* Read data*/
		data_tmp = readl(priv->base + IICB0DAT);

		/* Interrupt request timing : 9th clock */
		bit_setl(priv->base + IICB0CTL0, IICB0SLWT);

		/* Exit the wait state */
		writel(IICB0WRET, priv->base + IICB0TRG);

		/* Wait for transaction */
		time_left = wait_for_completion_timeout(&priv->msg_tia_done,
							priv->adap.timeout);
		if (!time_left)
			return -ETIMEDOUT;

		/* Enable ACK */
		bit_setl(priv->base + IICB0CTL0, IICB0SLAC);
	} else {
		/* Read data */
		data_tmp = readl(priv->base + IICB0DAT);
	}

	*data = data_tmp;

	return 0;
}

static int rzv2m_i2c_send(struct rzv2m_i2c_priv *priv, struct i2c_msg *msg,
			  unsigned int *count)
{
	unsigned int i;
	int ret;

	for (i = 0; i < msg->len; i++) {
		ret = rzv2m_i2c_write_with_ack(priv, msg->buf[i]);
		if (ret < 0)
			return ret;
	}
	*count = i;

	return 0;
}

static int rzv2m_i2c_receive(struct rzv2m_i2c_priv *priv, struct i2c_msg *msg,
			     unsigned int *count)
{
	unsigned int i;
	int ret;

	for (i = 0; i < msg->len; i++) {
		ret = rzv2m_i2c_read_with_ack(priv, &msg->buf[i],
					      (msg->len - 1) == i);
		if (ret < 0)
			return ret;
	}
	*count = i;

	return 0;
}

static int rzv2m_i2c_send_address(struct rzv2m_i2c_priv *priv,
				  struct i2c_msg *msg)
{
	u32 addr;
	int ret;

	if (msg->flags & I2C_M_TEN) {
		/*
		 * 10-bit address
		 *   addr_1: 5'b11110 | addr[9:8] | (R/nW)
		 *   addr_2: addr[7:0]
		 */
		addr = 0xf0 | ((msg->addr & GENMASK(9, 8)) >> 7);
		addr |= !!(msg->flags & I2C_M_RD);
		/* Send 1st address(extend code) */
		ret = rzv2m_i2c_write_with_ack(priv, addr);
		if (ret)
			return ret;

		/* Send 2nd address */
		ret = rzv2m_i2c_write_with_ack(priv, msg->addr & 0xff);
	} else {
		/* 7-bit address */
		addr = i2c_8bit_addr_from_msg(msg);
		ret = rzv2m_i2c_write_with_ack(priv, addr);
	}

	return ret;
}

static int rzv2m_i2c_stop_condition(struct rzv2m_i2c_priv *priv)
{
	u32 value;

	/* Send stop condition */
	writel(IICB0SPT, priv->base + IICB0TRG);
	return readl_poll_timeout(priv->base + IICB0STR0,
				  value, value & IICB0SSSP,
				  100, jiffies_to_usecs(priv->adap.timeout));
}

static int rzv2m_i2c_xfer_msg(struct rzv2m_i2c_priv *priv,
			      struct i2c_msg *msg, int stop)
{
	unsigned int count = 0;
	int ret, read = !!(msg->flags & I2C_M_RD);

	/* Send start condition */
	writel(IICB0STT, priv->base + IICB0TRG);

	ret = rzv2m_i2c_send_address(priv, msg);
	if (!ret) {
		if (read)
			ret = rzv2m_i2c_receive(priv, msg, &count);
		else
			ret = rzv2m_i2c_send(priv, msg, &count);

		if (!ret && stop)
			ret = rzv2m_i2c_stop_condition(priv);
	}

	if (ret == -ENXIO)
		rzv2m_i2c_stop_condition(priv);
	else if (ret < 0)
		rzv2m_i2c_init(priv);
	else
		ret = count;

	return ret;
}

static int rzv2m_i2c_xfer(struct i2c_adapter *adap,
			  struct i2c_msg *msgs, int num)
{
	struct rzv2m_i2c_priv *priv = i2c_get_adapdata(adap);
	struct device *dev = priv->adap.dev.parent;
	unsigned int i;
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	if (readl(priv->base + IICB0STR0) & IICB0SSBS) {
		ret = -EAGAIN;
		goto out;
	}

	/* I2C main transfer */
	for (i = 0; i < num; i++) {
		ret = rzv2m_i2c_xfer_msg(priv, &msgs[i], i == (num - 1));
		if (ret < 0)
			goto out;
	}
	ret = num;

out:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static u32 rzv2m_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | (I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
	       I2C_FUNC_10BIT_ADDR;
}

static int rzv2m_i2c_disable(struct device *dev, struct rzv2m_i2c_priv *priv)
{
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	bit_clrl(priv->base + IICB0CTL0, IICB0IICE);
	pm_runtime_put(dev);

	return 0;
}

static const struct i2c_adapter_quirks rzv2m_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

static struct i2c_algorithm rzv2m_i2c_algo = {
	.xfer = rzv2m_i2c_xfer,
	.functionality = rzv2m_i2c_func,
};

static int rzv2m_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzv2m_i2c_priv *priv;
	struct reset_control *rstc;
	struct i2c_adapter *adap;
	struct resource *res;
	int irq, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk), "Can't get clock\n");

	rstc = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc), "Missing reset ctrl\n");
	/*
	 * The reset also affects other HW that is not under the control
	 * of Linux. Therefore, all we can do is deassert the reset.
	 */
	reset_control_deassert(rstc);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, rzv2m_i2c_tia_irq_handler, 0,
			       dev_name(dev), priv);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Unable to request irq %d\n", irq);

	adap = &priv->adap;
	adap->nr = pdev->id;
	adap->algo = &rzv2m_i2c_algo;
	adap->quirks = &rzv2m_i2c_quirks;
	adap->dev.parent = dev;
	adap->owner = THIS_MODULE;
	device_set_node(&adap->dev, dev_fwnode(dev));
	i2c_set_adapdata(adap, priv);
	strscpy(adap->name, pdev->name, sizeof(adap->name));
	init_completion(&priv->msg_tia_done);

	ret = rzv2m_i2c_clock_calculate(dev, priv);
	if (ret < 0)
		return ret;

	pm_runtime_enable(dev);

	pm_runtime_get_sync(dev);
	rzv2m_i2c_init(priv);
	pm_runtime_put(dev);

	platform_set_drvdata(pdev, priv);

	ret = i2c_add_numbered_adapter(adap);
	if (ret < 0) {
		rzv2m_i2c_disable(dev, priv);
		pm_runtime_disable(dev);
	}

	return ret;
}

static void rzv2m_i2c_remove(struct platform_device *pdev)
{
	struct rzv2m_i2c_priv *priv = platform_get_drvdata(pdev);
	struct device *dev = priv->adap.dev.parent;

	i2c_del_adapter(&priv->adap);
	rzv2m_i2c_disable(dev, priv);
	pm_runtime_disable(dev);
}

static int rzv2m_i2c_suspend(struct device *dev)
{
	struct rzv2m_i2c_priv *priv = dev_get_drvdata(dev);

	return rzv2m_i2c_disable(dev, priv);
}

static int rzv2m_i2c_resume(struct device *dev)
{
	struct rzv2m_i2c_priv *priv = dev_get_drvdata(dev);
	int ret;

	ret = rzv2m_i2c_clock_calculate(dev, priv);
	if (ret < 0)
		return ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	rzv2m_i2c_init(priv);
	pm_runtime_put(dev);

	return 0;
}

static const struct of_device_id rzv2m_i2c_ids[] = {
	{ .compatible = "renesas,rzv2m-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, rzv2m_i2c_ids);

static const struct dev_pm_ops rzv2m_i2c_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(rzv2m_i2c_suspend, rzv2m_i2c_resume)
};

static struct platform_driver rzv2m_i2c_driver = {
	.driver = {
		.name = "rzv2m-i2c",
		.of_match_table = rzv2m_i2c_ids,
		.pm = pm_sleep_ptr(&rzv2m_i2c_pm_ops),
	},
	.probe	= rzv2m_i2c_probe,
	.remove_new = rzv2m_i2c_remove,
};
module_platform_driver(rzv2m_i2c_driver);

MODULE_DESCRIPTION("RZ/V2M I2C bus driver");
MODULE_AUTHOR("Renesas Electronics Corporation");
MODULE_LICENSE("GPL");

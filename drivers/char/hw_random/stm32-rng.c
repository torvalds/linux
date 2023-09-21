// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2015, Daniel Thompson
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define RNG_CR			0x00
#define RNG_CR_RNGEN		BIT(2)
#define RNG_CR_CED		BIT(5)
#define RNG_CR_CONFIG1		GENMASK(11, 8)
#define RNG_CR_NISTC		BIT(12)
#define RNG_CR_CONFIG2		GENMASK(15, 13)
#define RNG_CR_CONFIG3		GENMASK(25, 20)
#define RNG_CR_CONDRST		BIT(30)
#define RNG_CR_CONFLOCK		BIT(31)
#define RNG_CR_ENTROPY_SRC_MASK	(RNG_CR_CONFIG1 | RNG_CR_NISTC | RNG_CR_CONFIG2 | RNG_CR_CONFIG3)
#define RNG_CR_CONFIG_MASK	(RNG_CR_ENTROPY_SRC_MASK | RNG_CR_CED)

#define RNG_SR		0x04
#define RNG_SR_SEIS	BIT(6)
#define RNG_SR_CEIS	BIT(5)
#define RNG_SR_DRDY	BIT(0)

#define RNG_DR			0x08

#define RNG_NSCR		0x0C
#define RNG_NSCR_MASK		GENMASK(17, 0)

#define RNG_HTCR		0x10

struct stm32_rng_data {
	u32	cr;
	u32	nscr;
	u32	htcr;
	bool	has_cond_reset;
};

struct stm32_rng_private {
	struct hwrng rng;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	const struct stm32_rng_data *data;
	bool ced;
};

static int stm32_rng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct stm32_rng_private *priv =
	    container_of(rng, struct stm32_rng_private, rng);
	u32 sr;
	int retval = 0;

	pm_runtime_get_sync((struct device *) priv->rng.priv);

	while (max >= sizeof(u32)) {
		sr = readl_relaxed(priv->base + RNG_SR);
		/* Manage timeout which is based on timer and take */
		/* care of initial delay time when enabling rng	*/
		if (!sr && wait) {
			int err;

			err = readl_relaxed_poll_timeout_atomic(priv->base
								   + RNG_SR,
								   sr, sr,
								   10, 50000);
			if (err)
				dev_err((struct device *)priv->rng.priv,
					"%s: timeout %x!\n", __func__, sr);
		}

		/* If error detected or data not ready... */
		if (sr != RNG_SR_DRDY) {
			if (WARN_ONCE(sr & (RNG_SR_SEIS | RNG_SR_CEIS),
					"bad RNG status - %x\n", sr))
				writel_relaxed(0, priv->base + RNG_SR);
			break;
		}

		*(u32 *)data = readl_relaxed(priv->base + RNG_DR);

		retval += sizeof(u32);
		data += sizeof(u32);
		max -= sizeof(u32);
	}

	pm_runtime_mark_last_busy((struct device *) priv->rng.priv);
	pm_runtime_put_sync_autosuspend((struct device *) priv->rng.priv);

	return retval || !wait ? retval : -EIO;
}

static int stm32_rng_init(struct hwrng *rng)
{
	struct stm32_rng_private *priv =
	    container_of(rng, struct stm32_rng_private, rng);
	int err;
	u32 reg;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return err;

	/* clear error indicators */
	writel_relaxed(0, priv->base + RNG_SR);

	reg = readl_relaxed(priv->base + RNG_CR);

	/*
	 * Keep default RNG configuration if none was specified.
	 * 0 is an invalid value as it disables all entropy sources.
	 */
	if (priv->data->has_cond_reset && priv->data->cr) {
		reg &= ~RNG_CR_CONFIG_MASK;
		reg |= RNG_CR_CONDRST | (priv->data->cr & RNG_CR_ENTROPY_SRC_MASK);
		if (priv->ced)
			reg &= ~RNG_CR_CED;
		else
			reg |= RNG_CR_CED;
		writel_relaxed(reg, priv->base + RNG_CR);

		/* Health tests and noise control registers */
		writel_relaxed(priv->data->htcr, priv->base + RNG_HTCR);
		writel_relaxed(priv->data->nscr & RNG_NSCR_MASK, priv->base + RNG_NSCR);

		reg &= ~RNG_CR_CONDRST;
		reg |= RNG_CR_RNGEN;
		writel_relaxed(reg, priv->base + RNG_CR);

		err = readl_relaxed_poll_timeout_atomic(priv->base + RNG_CR, reg,
							(!(reg & RNG_CR_CONDRST)),
							10, 50000);
		if (err) {
			dev_err((struct device *)priv->rng.priv,
				"%s: timeout %x!\n", __func__, reg);
			return -EINVAL;
		}
	} else {
		/* Handle all RNG versions by checking if conditional reset should be set */
		if (priv->data->has_cond_reset)
			reg |= RNG_CR_CONDRST;

		if (priv->ced)
			reg &= ~RNG_CR_CED;
		else
			reg |= RNG_CR_CED;

		writel_relaxed(reg, priv->base + RNG_CR);

		if (priv->data->has_cond_reset)
			reg &= ~RNG_CR_CONDRST;

		reg |= RNG_CR_RNGEN;

		writel_relaxed(reg, priv->base + RNG_CR);
	}

	err = readl_relaxed_poll_timeout_atomic(priv->base + RNG_SR, reg,
						reg & RNG_SR_DRDY,
						10, 100000);
	if (err | (reg & ~RNG_SR_DRDY)) {
		clk_disable_unprepare(priv->clk);
		dev_err((struct device *)priv->rng.priv,
			"%s: timeout:%x SR: %x!\n", __func__, err, reg);
		return -EINVAL;
	}

	return 0;
}

static int stm32_rng_remove(struct platform_device *ofdev)
{
	pm_runtime_disable(&ofdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int stm32_rng_runtime_suspend(struct device *dev)
{
	u32 reg;
	struct stm32_rng_private *priv = dev_get_drvdata(dev);

	reg = readl_relaxed(priv->base + RNG_CR);
	reg &= ~RNG_CR_RNGEN;
	writel_relaxed(reg, priv->base + RNG_CR);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int stm32_rng_runtime_resume(struct device *dev)
{
	u32 reg;
	struct stm32_rng_private *priv = dev_get_drvdata(dev);

	clk_prepare_enable(priv->clk);
	reg = readl_relaxed(priv->base + RNG_CR);
	reg |= RNG_CR_RNGEN;
	writel_relaxed(reg, priv->base + RNG_CR);

	return 0;
}
#endif

static const struct dev_pm_ops stm32_rng_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32_rng_runtime_suspend,
			   stm32_rng_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct stm32_rng_data stm32mp13_rng_data = {
	.has_cond_reset = true,
	.cr = 0x00F00D00,
	.nscr = 0x2B5BB,
	.htcr = 0x969D,
};

static const struct stm32_rng_data stm32_rng_data = {
	.has_cond_reset = false,
};

static const struct of_device_id stm32_rng_match[] = {
	{
		.compatible = "st,stm32mp13-rng",
		.data = &stm32mp13_rng_data,
	},
	{
		.compatible = "st,stm32-rng",
		.data = &stm32_rng_data,
	},
	{},
};
MODULE_DEVICE_TABLE(of, stm32_rng_match);

static int stm32_rng_probe(struct platform_device *ofdev)
{
	struct device *dev = &ofdev->dev;
	struct device_node *np = ofdev->dev.of_node;
	struct stm32_rng_private *priv;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(struct stm32_rng_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_get_and_ioremap_resource(ofdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(&ofdev->dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->rst = devm_reset_control_get(&ofdev->dev, NULL);
	if (!IS_ERR(priv->rst)) {
		reset_control_assert(priv->rst);
		udelay(2);
		reset_control_deassert(priv->rst);
	}

	priv->ced = of_property_read_bool(np, "clock-error-detect");

	priv->data = of_device_get_match_data(dev);
	if (!priv->data)
		return -ENODEV;

	dev_set_drvdata(dev, priv);

	priv->rng.name = dev_driver_string(dev);
	priv->rng.init = stm32_rng_init;
	priv->rng.read = stm32_rng_read;
	priv->rng.priv = (unsigned long) dev;
	priv->rng.quality = 900;

	pm_runtime_set_autosuspend_delay(dev, 100);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	return devm_hwrng_register(dev, &priv->rng);
}

static struct platform_driver stm32_rng_driver = {
	.driver = {
		.name = "stm32-rng",
		.pm = &stm32_rng_pm_ops,
		.of_match_table = stm32_rng_match,
	},
	.probe = stm32_rng_probe,
	.remove = stm32_rng_remove,
};

module_platform_driver(stm32_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Thompson <daniel.thompson@linaro.org>");
MODULE_DESCRIPTION("STMicroelectronics STM32 RNG device driver");

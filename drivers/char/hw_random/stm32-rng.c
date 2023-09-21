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
#define RNG_CR_CLKDIV_SHIFT	16
#define RNG_CR_CLKDIV		GENMASK(19, 16)
#define RNG_CR_CONFIG3		GENMASK(25, 20)
#define RNG_CR_CONDRST		BIT(30)
#define RNG_CR_CONFLOCK		BIT(31)
#define RNG_CR_ENTROPY_SRC_MASK	(RNG_CR_CONFIG1 | RNG_CR_NISTC | RNG_CR_CONFIG2 | RNG_CR_CONFIG3)
#define RNG_CR_CONFIG_MASK	(RNG_CR_ENTROPY_SRC_MASK | RNG_CR_CED | RNG_CR_CLKDIV)

#define RNG_SR			0x04
#define RNG_SR_DRDY		BIT(0)
#define RNG_SR_CECS		BIT(1)
#define RNG_SR_SECS		BIT(2)
#define RNG_SR_CEIS		BIT(5)
#define RNG_SR_SEIS		BIT(6)

#define RNG_DR			0x08

#define RNG_NSCR		0x0C
#define RNG_NSCR_MASK		GENMASK(17, 0)

#define RNG_HTCR		0x10

#define RNG_NB_RECOVER_TRIES	3

struct stm32_rng_data {
	uint	max_clock_rate;
	u32	cr;
	u32	nscr;
	u32	htcr;
	bool	has_cond_reset;
};

/**
 * struct stm32_rng_config - RNG configuration data
 *
 * @cr:			RNG configuration. 0 means default hardware RNG configuration
 * @nscr:		Noise sources control configuration.
 * @htcr:		Health tests configuration.
 */
struct stm32_rng_config {
	u32 cr;
	u32 nscr;
	u32 htcr;
};

struct stm32_rng_private {
	struct hwrng rng;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	struct stm32_rng_config pm_conf;
	const struct stm32_rng_data *data;
	bool ced;
	bool lock_conf;
};

/*
 * Extracts from the STM32 RNG specification when RNG supports CONDRST.
 *
 * When a noise source (or seed) error occurs, the RNG stops generating
 * random numbers and sets to “1” both SEIS and SECS bits to indicate
 * that a seed error occurred. (...)
 *
 * 1. Software reset by writing CONDRST at 1 and at 0 (see bitfield
 * description for details). This step is needed only if SECS is set.
 * Indeed, when SEIS is set and SECS is cleared it means RNG performed
 * the reset automatically (auto-reset).
 * 2. If SECS was set in step 1 (no auto-reset) wait for CONDRST
 * to be cleared in the RNG_CR register, then confirm that SEIS is
 * cleared in the RNG_SR register. Otherwise just clear SEIS bit in
 * the RNG_SR register.
 * 3. If SECS was set in step 1 (no auto-reset) wait for SECS to be
 * cleared by RNG. The random number generation is now back to normal.
 */
static int stm32_rng_conceal_seed_error_cond_reset(struct stm32_rng_private *priv)
{
	struct device *dev = (struct device *)priv->rng.priv;
	u32 sr = readl_relaxed(priv->base + RNG_SR);
	u32 cr = readl_relaxed(priv->base + RNG_CR);
	int err;

	if (sr & RNG_SR_SECS) {
		/* Conceal by resetting the subsystem (step 1.) */
		writel_relaxed(cr | RNG_CR_CONDRST, priv->base + RNG_CR);
		writel_relaxed(cr & ~RNG_CR_CONDRST, priv->base + RNG_CR);
	} else {
		/* RNG auto-reset (step 2.) */
		writel_relaxed(sr & ~RNG_SR_SEIS, priv->base + RNG_SR);
		goto end;
	}

	err = readl_relaxed_poll_timeout_atomic(priv->base + RNG_CR, cr, !(cr & RNG_CR_CONDRST), 10,
						100000);
	if (err) {
		dev_err(dev, "%s: timeout %x\n", __func__, sr);
		return err;
	}

	/* Check SEIS is cleared (step 2.) */
	if (readl_relaxed(priv->base + RNG_SR) & RNG_SR_SEIS)
		return -EINVAL;

	err = readl_relaxed_poll_timeout_atomic(priv->base + RNG_SR, sr, !(sr & RNG_SR_SECS), 10,
						100000);
	if (err) {
		dev_err(dev, "%s: timeout %x\n", __func__, sr);
		return err;
	}

end:
	return 0;
}

/*
 * Extracts from the STM32 RNG specification, when CONDRST is not supported
 *
 * When a noise source (or seed) error occurs, the RNG stops generating
 * random numbers and sets to “1” both SEIS and SECS bits to indicate
 * that a seed error occurred. (...)
 *
 * The following sequence shall be used to fully recover from a seed
 * error after the RNG initialization:
 * 1. Clear the SEIS bit by writing it to “0”.
 * 2. Read out 12 words from the RNG_DR register, and discard each of
 * them in order to clean the pipeline.
 * 3. Confirm that SEIS is still cleared. Random number generation is
 * back to normal.
 */
static int stm32_rng_conceal_seed_error_sw_reset(struct stm32_rng_private *priv)
{
	unsigned int i = 0;
	u32 sr = readl_relaxed(priv->base + RNG_SR);

	writel_relaxed(sr & ~RNG_SR_SEIS, priv->base + RNG_SR);

	for (i = 12; i != 0; i--)
		(void)readl_relaxed(priv->base + RNG_DR);

	if (readl_relaxed(priv->base + RNG_SR) & RNG_SR_SEIS)
		return -EINVAL;

	return 0;
}

static int stm32_rng_conceal_seed_error(struct hwrng *rng)
{
	struct stm32_rng_private *priv = container_of(rng, struct stm32_rng_private, rng);

	dev_dbg((struct device *)priv->rng.priv, "Concealing seed error\n");

	if (priv->data->has_cond_reset)
		return stm32_rng_conceal_seed_error_cond_reset(priv);
	else
		return stm32_rng_conceal_seed_error_sw_reset(priv);
};


static int stm32_rng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct stm32_rng_private *priv = container_of(rng, struct stm32_rng_private, rng);
	unsigned int i = 0;
	int retval = 0, err = 0;
	u32 sr;

	pm_runtime_get_sync((struct device *) priv->rng.priv);

	if (readl_relaxed(priv->base + RNG_SR) & RNG_SR_SEIS)
		stm32_rng_conceal_seed_error(rng);

	while (max >= sizeof(u32)) {
		sr = readl_relaxed(priv->base + RNG_SR);
		/*
		 * Manage timeout which is based on timer and take
		 * care of initial delay time when enabling the RNG.
		 */
		if (!sr && wait) {
			err = readl_relaxed_poll_timeout_atomic(priv->base
								   + RNG_SR,
								   sr, sr,
								   10, 50000);
			if (err) {
				dev_err((struct device *)priv->rng.priv,
					"%s: timeout %x!\n", __func__, sr);
				break;
			}
		} else if (!sr) {
			/* The FIFO is being filled up */
			break;
		}

		if (sr != RNG_SR_DRDY) {
			if (sr & RNG_SR_SEIS) {
				err = stm32_rng_conceal_seed_error(rng);
				i++;
				if (err && i > RNG_NB_RECOVER_TRIES) {
					dev_err((struct device *)priv->rng.priv,
						"Couldn't recover from seed error\n");
					return -ENOTRECOVERABLE;
				}

				continue;
			}

			if (WARN_ONCE((sr & RNG_SR_CEIS), "RNG clock too slow - %x\n", sr))
				writel_relaxed(0, priv->base + RNG_SR);
		}

		/* Late seed error case: DR being 0 is an error status */
		*(u32 *)data = readl_relaxed(priv->base + RNG_DR);
		if (!*(u32 *)data) {
			err = stm32_rng_conceal_seed_error(rng);
			i++;
			if (err && i > RNG_NB_RECOVER_TRIES) {
				dev_err((struct device *)priv->rng.priv,
					"Couldn't recover from seed error");
				return -ENOTRECOVERABLE;
			}

			continue;
		}

		i = 0;
		retval += sizeof(u32);
		data += sizeof(u32);
		max -= sizeof(u32);
	}

	pm_runtime_mark_last_busy((struct device *) priv->rng.priv);
	pm_runtime_put_sync_autosuspend((struct device *) priv->rng.priv);

	return retval || !wait ? retval : -EIO;
}

static uint stm32_rng_clock_freq_restrain(struct hwrng *rng)
{
	struct stm32_rng_private *priv =
	    container_of(rng, struct stm32_rng_private, rng);
	unsigned long clock_rate = 0;
	uint clock_div = 0;

	clock_rate = clk_get_rate(priv->clk);

	/*
	 * Get the exponent to apply on the CLKDIV field in RNG_CR register
	 * No need to handle the case when clock-div > 0xF as it is physically
	 * impossible
	 */
	while ((clock_rate >> clock_div) > priv->data->max_clock_rate)
		clock_div++;

	pr_debug("RNG clk rate : %lu\n", clk_get_rate(priv->clk) >> clock_div);

	return clock_div;
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
		uint clock_div = stm32_rng_clock_freq_restrain(rng);

		reg &= ~RNG_CR_CONFIG_MASK;
		reg |= RNG_CR_CONDRST | (priv->data->cr & RNG_CR_ENTROPY_SRC_MASK) |
		       (clock_div << RNG_CR_CLKDIV_SHIFT);
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
		if (priv->lock_conf)
			reg |= RNG_CR_CONFLOCK;

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

static int __maybe_unused stm32_rng_runtime_suspend(struct device *dev)
{
	struct stm32_rng_private *priv = dev_get_drvdata(dev);
	u32 reg;

	reg = readl_relaxed(priv->base + RNG_CR);
	reg &= ~RNG_CR_RNGEN;
	writel_relaxed(reg, priv->base + RNG_CR);
	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused stm32_rng_suspend(struct device *dev)
{
	struct stm32_rng_private *priv = dev_get_drvdata(dev);

	if (priv->data->has_cond_reset) {
		priv->pm_conf.nscr = readl_relaxed(priv->base + RNG_NSCR);
		priv->pm_conf.htcr = readl_relaxed(priv->base + RNG_HTCR);
	}

	/* Do not save that RNG is enabled as it will be handled at resume */
	priv->pm_conf.cr = readl_relaxed(priv->base + RNG_CR) & ~RNG_CR_RNGEN;

	writel_relaxed(priv->pm_conf.cr, priv->base + RNG_CR);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int __maybe_unused stm32_rng_runtime_resume(struct device *dev)
{
	struct stm32_rng_private *priv = dev_get_drvdata(dev);
	int err;
	u32 reg;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return err;

	/* Clean error indications */
	writel_relaxed(0, priv->base + RNG_SR);

	reg = readl_relaxed(priv->base + RNG_CR);
	reg |= RNG_CR_RNGEN;
	writel_relaxed(reg, priv->base + RNG_CR);

	return 0;
}

static int __maybe_unused stm32_rng_resume(struct device *dev)
{
	struct stm32_rng_private *priv = dev_get_drvdata(dev);
	int err;
	u32 reg;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return err;

	/* Clean error indications */
	writel_relaxed(0, priv->base + RNG_SR);

	if (priv->data->has_cond_reset) {
		/*
		 * Correct configuration in bits [29:4] must be set in the same
		 * access that set RNG_CR_CONDRST bit. Else config setting is
		 * not taken into account. CONFIGLOCK bit must also be unset but
		 * it is not handled at the moment.
		 */
		writel_relaxed(priv->pm_conf.cr | RNG_CR_CONDRST, priv->base + RNG_CR);

		writel_relaxed(priv->pm_conf.nscr, priv->base + RNG_NSCR);
		writel_relaxed(priv->pm_conf.htcr, priv->base + RNG_HTCR);

		reg = readl_relaxed(priv->base + RNG_CR);
		reg |= RNG_CR_RNGEN;
		reg &= ~RNG_CR_CONDRST;
		writel_relaxed(reg, priv->base + RNG_CR);

		err = readl_relaxed_poll_timeout_atomic(priv->base + RNG_CR, reg,
							reg & ~RNG_CR_CONDRST, 10, 100000);

		if (err) {
			clk_disable_unprepare(priv->clk);
			dev_err((struct device *)priv->rng.priv,
				"%s: timeout:%x CR: %x!\n", __func__, err, reg);
			return -EINVAL;
		}
	} else {
		reg = priv->pm_conf.cr;
		reg |= RNG_CR_RNGEN;
		writel_relaxed(reg, priv->base + RNG_CR);
	}

	return 0;
}

static const struct dev_pm_ops __maybe_unused stm32_rng_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32_rng_runtime_suspend,
			   stm32_rng_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(stm32_rng_suspend,
				stm32_rng_resume)
};

static const struct stm32_rng_data stm32mp13_rng_data = {
	.has_cond_reset = true,
	.max_clock_rate = 48000000,
	.cr = 0x00F00D00,
	.nscr = 0x2B5BB,
	.htcr = 0x969D,
};

static const struct stm32_rng_data stm32_rng_data = {
	.has_cond_reset = false,
	.max_clock_rate = 3000000,
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
	priv->lock_conf = of_property_read_bool(np, "st,rng-lock-conf");

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
		.pm = pm_ptr(&stm32_rng_pm_ops),
		.of_match_table = stm32_rng_match,
	},
	.probe = stm32_rng_probe,
	.remove = stm32_rng_remove,
};

module_platform_driver(stm32_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Thompson <daniel.thompson@linaro.org>");
MODULE_DESCRIPTION("STMicroelectronics STM32 RNG device driver");

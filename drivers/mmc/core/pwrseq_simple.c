/*
 *  Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 *  Simple MMC power sequence management
 */
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_simple {
	struct mmc_pwrseq pwrseq;
	bool clk_enabled;
	struct clk *ext_clk;
	struct gpio_descs *reset_gpios;
};

#define to_pwrseq_simple(p) container_of(p, struct mmc_pwrseq_simple, pwrseq)

static void mmc_pwrseq_simple_set_gpios_value(struct mmc_pwrseq_simple *pwrseq,
					      int value)
{
	struct gpio_descs *reset_gpios = pwrseq->reset_gpios;

	if (!IS_ERR(reset_gpios)) {
		int i;
		int values[reset_gpios->ndescs];

		for (i = 0; i < reset_gpios->ndescs; i++)
			values[i] = value;

		gpiod_set_array_value_cansleep(
			reset_gpios->ndescs, reset_gpios->desc, values);
	}
}

static void mmc_pwrseq_simple_pre_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	if (!IS_ERR(pwrseq->ext_clk) && !pwrseq->clk_enabled) {
		clk_prepare_enable(pwrseq->ext_clk);
		pwrseq->clk_enabled = true;
	}

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);
}

static void mmc_pwrseq_simple_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 0);
}

static void mmc_pwrseq_simple_power_off(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = to_pwrseq_simple(host->pwrseq);

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);

	if (!IS_ERR(pwrseq->ext_clk) && pwrseq->clk_enabled) {
		clk_disable_unprepare(pwrseq->ext_clk);
		pwrseq->clk_enabled = false;
	}
}

static const struct mmc_pwrseq_ops mmc_pwrseq_simple_ops = {
	.pre_power_on = mmc_pwrseq_simple_pre_power_on,
	.post_power_on = mmc_pwrseq_simple_post_power_on,
	.power_off = mmc_pwrseq_simple_power_off,
};

static const struct of_device_id mmc_pwrseq_simple_of_match[] = {
	{ .compatible = "mmc-pwrseq-simple",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mmc_pwrseq_simple_of_match);

static int mmc_pwrseq_simple_probe(struct platform_device *pdev)
{
	struct mmc_pwrseq_simple *pwrseq;
	struct device *dev = &pdev->dev;

	pwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->ext_clk = devm_clk_get(dev, "ext_clock");
	if (IS_ERR(pwrseq->ext_clk) && PTR_ERR(pwrseq->ext_clk) != -ENOENT)
		return PTR_ERR(pwrseq->ext_clk);

	pwrseq->reset_gpios = devm_gpiod_get_array(dev, "reset",
							GPIOD_OUT_HIGH);
	if (IS_ERR(pwrseq->reset_gpios) &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOENT &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOSYS) {
		return PTR_ERR(pwrseq->reset_gpios);
	}

	pwrseq->pwrseq.dev = dev;
	pwrseq->pwrseq.ops = &mmc_pwrseq_simple_ops;
	pwrseq->pwrseq.owner = THIS_MODULE;
	platform_set_drvdata(pdev, pwrseq);

	return mmc_pwrseq_register(&pwrseq->pwrseq);
}

static int mmc_pwrseq_simple_remove(struct platform_device *pdev)
{
	struct mmc_pwrseq_simple *pwrseq = platform_get_drvdata(pdev);

	mmc_pwrseq_unregister(&pwrseq->pwrseq);

	return 0;
}

static struct platform_driver mmc_pwrseq_simple_driver = {
	.probe = mmc_pwrseq_simple_probe,
	.remove = mmc_pwrseq_simple_remove,
	.driver = {
		.name = "pwrseq_simple",
		.of_match_table = mmc_pwrseq_simple_of_match,
	},
};

module_platform_driver(mmc_pwrseq_simple_driver);
MODULE_LICENSE("GPL v2");

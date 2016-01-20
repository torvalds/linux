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
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_simple {
	struct mmc_pwrseq pwrseq;
	bool clk_enabled;
	struct clk *ext_clk;
	struct gpio_descs *reset_gpios;
};

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
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	if (!IS_ERR(pwrseq->ext_clk) && !pwrseq->clk_enabled) {
		clk_prepare_enable(pwrseq->ext_clk);
		pwrseq->clk_enabled = true;
	}

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);
}

static void mmc_pwrseq_simple_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 0);
}

static void mmc_pwrseq_simple_power_off(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	mmc_pwrseq_simple_set_gpios_value(pwrseq, 1);

	if (!IS_ERR(pwrseq->ext_clk) && pwrseq->clk_enabled) {
		clk_disable_unprepare(pwrseq->ext_clk);
		pwrseq->clk_enabled = false;
	}
}

static void mmc_pwrseq_simple_free(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	if (!IS_ERR(pwrseq->reset_gpios))
		gpiod_put_array(pwrseq->reset_gpios);

	if (!IS_ERR(pwrseq->ext_clk))
		clk_put(pwrseq->ext_clk);

	kfree(pwrseq);
}

static const struct mmc_pwrseq_ops mmc_pwrseq_simple_ops = {
	.pre_power_on = mmc_pwrseq_simple_pre_power_on,
	.post_power_on = mmc_pwrseq_simple_post_power_on,
	.power_off = mmc_pwrseq_simple_power_off,
	.free = mmc_pwrseq_simple_free,
};

struct mmc_pwrseq *mmc_pwrseq_simple_alloc(struct mmc_host *host,
					   struct device *dev)
{
	struct mmc_pwrseq_simple *pwrseq;
	int ret = 0;

	pwrseq = kzalloc(sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return ERR_PTR(-ENOMEM);

	pwrseq->ext_clk = clk_get(dev, "ext_clock");
	if (IS_ERR(pwrseq->ext_clk) &&
	    PTR_ERR(pwrseq->ext_clk) != -ENOENT) {
		ret = PTR_ERR(pwrseq->ext_clk);
		goto free;
	}

	pwrseq->reset_gpios = gpiod_get_array(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pwrseq->reset_gpios) &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOENT &&
	    PTR_ERR(pwrseq->reset_gpios) != -ENOSYS) {
		ret = PTR_ERR(pwrseq->reset_gpios);
		goto clk_put;
	}

	pwrseq->pwrseq.ops = &mmc_pwrseq_simple_ops;

	return &pwrseq->pwrseq;
clk_put:
	if (!IS_ERR(pwrseq->ext_clk))
		clk_put(pwrseq->ext_clk);
free:
	kfree(pwrseq);
	return ERR_PTR(ret);
}

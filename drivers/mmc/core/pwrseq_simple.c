/*
 *  Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 *  Simple MMC power sequence management
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_simple {
	struct mmc_pwrseq pwrseq;
	struct gpio_desc *reset_gpio;
};

static void mmc_pwrseq_simple_pre_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	if (!IS_ERR(pwrseq->reset_gpio))
		gpiod_set_value_cansleep(pwrseq->reset_gpio, 1);
}

static void mmc_pwrseq_simple_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	if (!IS_ERR(pwrseq->reset_gpio))
		gpiod_set_value_cansleep(pwrseq->reset_gpio, 0);
}

static void mmc_pwrseq_simple_free(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	if (!IS_ERR(pwrseq->reset_gpio))
		gpiod_put(pwrseq->reset_gpio);

	kfree(pwrseq);
	host->pwrseq = NULL;
}

static struct mmc_pwrseq_ops mmc_pwrseq_simple_ops = {
	.pre_power_on = mmc_pwrseq_simple_pre_power_on,
	.post_power_on = mmc_pwrseq_simple_post_power_on,
	.power_off = mmc_pwrseq_simple_pre_power_on,
	.free = mmc_pwrseq_simple_free,
};

int mmc_pwrseq_simple_alloc(struct mmc_host *host, struct device *dev)
{
	struct mmc_pwrseq_simple *pwrseq;
	int ret = 0;

	pwrseq = kzalloc(sizeof(struct mmc_pwrseq_simple), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->reset_gpio = gpiod_get_index(dev, "reset", 0, GPIOD_OUT_HIGH);
	if (IS_ERR(pwrseq->reset_gpio) &&
		PTR_ERR(pwrseq->reset_gpio) != -ENOENT &&
		PTR_ERR(pwrseq->reset_gpio) != -ENOSYS) {
		ret = PTR_ERR(pwrseq->reset_gpio);
		goto free;
	}

	pwrseq->pwrseq.ops = &mmc_pwrseq_simple_ops;
	host->pwrseq = &pwrseq->pwrseq;

	return 0;
free:
	kfree(pwrseq);
	return ret;
}

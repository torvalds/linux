/*
 * Copyright (C) 2015, Samsung Electronics Co., Ltd.
 *
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 * Simple eMMC hardware reset provider
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/reboot.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_emmc {
	struct mmc_pwrseq pwrseq;
	struct notifier_block reset_nb;
	struct gpio_desc *reset_gpio;
};

static void __mmc_pwrseq_emmc_reset(struct mmc_pwrseq_emmc *pwrseq)
{
	gpiod_set_value(pwrseq->reset_gpio, 1);
	udelay(1);
	gpiod_set_value(pwrseq->reset_gpio, 0);
	udelay(200);
}

static void mmc_pwrseq_emmc_reset(struct mmc_host *host)
{
	struct mmc_pwrseq_emmc *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_emmc, pwrseq);

	__mmc_pwrseq_emmc_reset(pwrseq);
}

static void mmc_pwrseq_emmc_free(struct mmc_host *host)
{
	struct mmc_pwrseq_emmc *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_emmc, pwrseq);

	unregister_restart_handler(&pwrseq->reset_nb);
	gpiod_put(pwrseq->reset_gpio);
	kfree(pwrseq);
}

static struct mmc_pwrseq_ops mmc_pwrseq_emmc_ops = {
	.post_power_on = mmc_pwrseq_emmc_reset,
	.free = mmc_pwrseq_emmc_free,
};

static int mmc_pwrseq_emmc_reset_nb(struct notifier_block *this,
				    unsigned long mode, void *cmd)
{
	struct mmc_pwrseq_emmc *pwrseq = container_of(this,
					struct mmc_pwrseq_emmc, reset_nb);

	__mmc_pwrseq_emmc_reset(pwrseq);
	return NOTIFY_DONE;
}

struct mmc_pwrseq *mmc_pwrseq_emmc_alloc(struct mmc_host *host,
					 struct device *dev)
{
	struct mmc_pwrseq_emmc *pwrseq;
	int ret = 0;

	pwrseq = kzalloc(sizeof(struct mmc_pwrseq_emmc), GFP_KERNEL);
	if (!pwrseq)
		return ERR_PTR(-ENOMEM);

	pwrseq->reset_gpio = gpiod_get_index(dev, "reset", 0, GPIOD_OUT_LOW);
	if (IS_ERR(pwrseq->reset_gpio)) {
		ret = PTR_ERR(pwrseq->reset_gpio);
		goto free;
	}

	/*
	 * register reset handler to ensure emmc reset also from
	 * emergency_reboot(), priority 129 schedules it just before
	 * system reboot
	 */
	pwrseq->reset_nb.notifier_call = mmc_pwrseq_emmc_reset_nb;
	pwrseq->reset_nb.priority = 129;
	register_restart_handler(&pwrseq->reset_nb);

	pwrseq->pwrseq.ops = &mmc_pwrseq_emmc_ops;

	return &pwrseq->pwrseq;
free:
	kfree(pwrseq);
	return ERR_PTR(ret);
}

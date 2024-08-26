// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015, Samsung Electronics Co., Ltd.
 *
 * Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Simple eMMC hardware reset provider
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
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

#define to_pwrseq_emmc(p) container_of(p, struct mmc_pwrseq_emmc, pwrseq)

static void mmc_pwrseq_emmc_reset(struct mmc_host *host)
{
	struct mmc_pwrseq_emmc *pwrseq =  to_pwrseq_emmc(host->pwrseq);

	gpiod_set_value_cansleep(pwrseq->reset_gpio, 1);
	udelay(1);
	gpiod_set_value_cansleep(pwrseq->reset_gpio, 0);
	udelay(200);
}

static int mmc_pwrseq_emmc_reset_nb(struct notifier_block *this,
				    unsigned long mode, void *cmd)
{
	struct mmc_pwrseq_emmc *pwrseq = container_of(this,
					struct mmc_pwrseq_emmc, reset_nb);
	gpiod_set_value(pwrseq->reset_gpio, 1);
	udelay(1);
	gpiod_set_value(pwrseq->reset_gpio, 0);
	udelay(200);

	return NOTIFY_DONE;
}

static const struct mmc_pwrseq_ops mmc_pwrseq_emmc_ops = {
	.reset = mmc_pwrseq_emmc_reset,
};

static int mmc_pwrseq_emmc_probe(struct platform_device *pdev)
{
	struct mmc_pwrseq_emmc *pwrseq;
	struct device *dev = &pdev->dev;

	pwrseq = devm_kzalloc(dev, sizeof(*pwrseq), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(pwrseq->reset_gpio))
		return PTR_ERR(pwrseq->reset_gpio);

	if (!gpiod_cansleep(pwrseq->reset_gpio)) {
		/*
		 * register reset handler to ensure emmc reset also from
		 * emergency_reboot(), priority 255 is the highest priority
		 * so it will be executed before any system reboot handler.
		 */
		pwrseq->reset_nb.notifier_call = mmc_pwrseq_emmc_reset_nb;
		pwrseq->reset_nb.priority = 255;
		register_restart_handler(&pwrseq->reset_nb);
	} else {
		dev_notice(dev, "EMMC reset pin tied to a sleepy GPIO driver; reset on emergency-reboot disabled\n");
	}

	pwrseq->pwrseq.ops = &mmc_pwrseq_emmc_ops;
	pwrseq->pwrseq.dev = dev;
	pwrseq->pwrseq.owner = THIS_MODULE;
	platform_set_drvdata(pdev, pwrseq);

	return mmc_pwrseq_register(&pwrseq->pwrseq);
}

static void mmc_pwrseq_emmc_remove(struct platform_device *pdev)
{
	struct mmc_pwrseq_emmc *pwrseq = platform_get_drvdata(pdev);

	unregister_restart_handler(&pwrseq->reset_nb);
	mmc_pwrseq_unregister(&pwrseq->pwrseq);
}

static const struct of_device_id mmc_pwrseq_emmc_of_match[] = {
	{ .compatible = "mmc-pwrseq-emmc",},
	{/* sentinel */},
};

MODULE_DEVICE_TABLE(of, mmc_pwrseq_emmc_of_match);

static struct platform_driver mmc_pwrseq_emmc_driver = {
	.probe = mmc_pwrseq_emmc_probe,
	.remove_new = mmc_pwrseq_emmc_remove,
	.driver = {
		.name = "pwrseq_emmc",
		.of_match_table = mmc_pwrseq_emmc_of_match,
	},
};

module_platform_driver(mmc_pwrseq_emmc_driver);
MODULE_DESCRIPTION("Hardware reset support for eMMC");
MODULE_LICENSE("GPL v2");

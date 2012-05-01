/*
 * Generic GPIO card-detect helper
 *
 * Copyright (C) 2011, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/slab.h>

struct mmc_gpio {
	int cd_gpio;
	char cd_label[0];
};

static irqreturn_t mmc_gpio_cd_irqt(int irq, void *dev_id)
{
	/* Schedule a card detection after a debounce timeout */
	mmc_detect_change(dev_id, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

int mmc_gpio_get_cd(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;

	if (!ctx || !gpio_is_valid(ctx->cd_gpio))
		return -ENOSYS;

	return !gpio_get_value_cansleep(ctx->cd_gpio) ^
		!!(host->caps2 & MMC_CAP2_CD_ACTIVE_HIGH);
}
EXPORT_SYMBOL(mmc_gpio_get_cd);

int mmc_gpio_request_cd(struct mmc_host *host, unsigned int gpio)
{
	size_t len = strlen(dev_name(host->parent)) + 4;
	struct mmc_gpio *ctx;
	int irq = gpio_to_irq(gpio);
	int ret;

	ctx = kmalloc(sizeof(*ctx) + len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	snprintf(ctx->cd_label, len, "%s cd", dev_name(host->parent));

	ret = gpio_request_one(gpio, GPIOF_DIR_IN, ctx->cd_label);
	if (ret < 0)
		goto egpioreq;

	/*
	 * Even if gpio_to_irq() returns a valid IRQ number, the platform might
	 * still prefer to poll, e.g., because that IRQ number is already used
	 * by another unit and cannot be shared.
	 */
	if (irq >= 0 && host->caps & MMC_CAP_NEEDS_POLL)
		irq = -EINVAL;

	if (irq >= 0) {
		ret = request_threaded_irq(irq, NULL, mmc_gpio_cd_irqt,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			ctx->cd_label, host);
		if (ret < 0)
			irq = ret;
	}

	host->slot.cd_irq = irq;

	if (irq < 0)
		host->caps |= MMC_CAP_NEEDS_POLL;

	ctx->cd_gpio = gpio;
	host->slot.handler_priv = ctx;

	return 0;

egpioreq:
	kfree(ctx);
	return ret;
}
EXPORT_SYMBOL(mmc_gpio_request_cd);

void mmc_gpio_free_cd(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->slot.handler_priv;
	int gpio;

	if (!ctx || !gpio_is_valid(ctx->cd_gpio))
		return;

	if (host->slot.cd_irq >= 0) {
		free_irq(host->slot.cd_irq, host);
		host->slot.cd_irq = -EINVAL;
	}

	gpio = ctx->cd_gpio;
	ctx->cd_gpio = -EINVAL;

	gpio_free(gpio);
	host->slot.handler_priv = NULL;
	kfree(ctx);
}
EXPORT_SYMBOL(mmc_gpio_free_cd);

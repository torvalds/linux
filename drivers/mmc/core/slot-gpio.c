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
	unsigned int cd_gpio;
	char cd_label[0];
};

static irqreturn_t mmc_gpio_cd_irqt(int irq, void *dev_id)
{
	/* Schedule a card detection after a debounce timeout */
	mmc_detect_change(dev_id, msecs_to_jiffies(100));
	return IRQ_HANDLED;
}

int mmc_gpio_request_cd(struct mmc_host *host, unsigned int gpio)
{
	size_t len = strlen(dev_name(host->parent)) + 4;
	struct mmc_gpio *ctx;
	int irq = gpio_to_irq(gpio);
	int ret;

	if (irq < 0)
		return irq;

	ctx = kmalloc(sizeof(*ctx) + len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	snprintf(ctx->cd_label, len, "%s cd", dev_name(host->parent));

	ret = gpio_request_one(gpio, GPIOF_DIR_IN, ctx->cd_label);
	if (ret < 0)
		goto egpioreq;

	ret = request_threaded_irq(irq, NULL, mmc_gpio_cd_irqt,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			ctx->cd_label, host);
	if (ret < 0)
		goto eirqreq;

	ctx->cd_gpio = gpio;
	host->hotplug.irq = irq;
	host->hotplug.handler_priv = ctx;

	return 0;

eirqreq:
	gpio_free(gpio);
egpioreq:
	kfree(ctx);
	return ret;
}
EXPORT_SYMBOL(mmc_gpio_request_cd);

void mmc_gpio_free_cd(struct mmc_host *host)
{
	struct mmc_gpio *ctx = host->hotplug.handler_priv;

	if (!ctx)
		return;

	free_irq(host->hotplug.irq, host);
	gpio_free(ctx->cd_gpio);
	kfree(ctx);
}
EXPORT_SYMBOL(mmc_gpio_free_cd);

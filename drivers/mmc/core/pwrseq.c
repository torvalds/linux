/*
 *  Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 *
 *  MMC power sequence management
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_platform.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_match {
	const char *compatible;
	int (*alloc)(struct mmc_host *host, struct device *dev);
};

static struct mmc_pwrseq_match pwrseq_match[] = {
	{
		.compatible = "mmc-pwrseq-simple",
		.alloc = mmc_pwrseq_simple_alloc,
	}, {
		.compatible = "mmc-pwrseq-emmc",
		.alloc = mmc_pwrseq_emmc_alloc,
	},
};

static struct mmc_pwrseq_match *mmc_pwrseq_find(struct device_node *np)
{
	struct mmc_pwrseq_match *match = ERR_PTR(-ENODEV);
	int i;

	for (i = 0; i < ARRAY_SIZE(pwrseq_match); i++) {
		if (of_device_is_compatible(np,	pwrseq_match[i].compatible)) {
			match = &pwrseq_match[i];
			break;
		}
	}

	return match;
}

int mmc_pwrseq_alloc(struct mmc_host *host)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct mmc_pwrseq_match *match;
	int ret = 0;

	np = of_parse_phandle(host->parent->of_node, "mmc-pwrseq", 0);
	if (!np)
		return 0;

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		ret = -ENODEV;
		goto err;
	}

	match = mmc_pwrseq_find(np);
	if (IS_ERR(match)) {
		ret = PTR_ERR(match);
		goto err;
	}

	ret = match->alloc(host, &pdev->dev);
	if (!ret)
		dev_info(host->parent, "allocated mmc-pwrseq\n");

err:
	of_node_put(np);
	return ret;
}

void mmc_pwrseq_pre_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops && pwrseq->ops->pre_power_on)
		pwrseq->ops->pre_power_on(host);
}

void mmc_pwrseq_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops && pwrseq->ops->post_power_on)
		pwrseq->ops->post_power_on(host);
}

void mmc_pwrseq_power_off(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops && pwrseq->ops->power_off)
		pwrseq->ops->power_off(host);
}

void mmc_pwrseq_free(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops && pwrseq->ops->free)
		pwrseq->ops->free(host);
}

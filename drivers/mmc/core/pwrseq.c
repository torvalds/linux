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
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>

#include <linux/mmc/host.h>

#include "pwrseq.h"

static DEFINE_MUTEX(pwrseq_list_mutex);
static LIST_HEAD(pwrseq_list);

int mmc_pwrseq_alloc(struct mmc_host *host)
{
	struct device_node *np;
	struct mmc_pwrseq *p;

	np = of_parse_phandle(host->parent->of_node, "mmc-pwrseq", 0);
	if (!np)
		return 0;

	mutex_lock(&pwrseq_list_mutex);
	list_for_each_entry(p, &pwrseq_list, pwrseq_node) {
		if (p->dev->of_node == np) {
			if (!try_module_get(p->owner))
				dev_err(host->parent,
					"increasing module refcount failed\n");
			else
				host->pwrseq = p;

			break;
		}
	}

	of_node_put(np);
	mutex_unlock(&pwrseq_list_mutex);

	if (!host->pwrseq)
		return -EPROBE_DEFER;

	dev_info(host->parent, "allocated mmc-pwrseq\n");

	return 0;
}

void mmc_pwrseq_pre_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops->pre_power_on)
		pwrseq->ops->pre_power_on(host);
}

void mmc_pwrseq_post_power_on(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops->post_power_on)
		pwrseq->ops->post_power_on(host);
}

void mmc_pwrseq_power_off(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops->power_off)
		pwrseq->ops->power_off(host);
}

void mmc_pwrseq_reset(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq && pwrseq->ops->reset)
		pwrseq->ops->reset(host);
}

void mmc_pwrseq_free(struct mmc_host *host)
{
	struct mmc_pwrseq *pwrseq = host->pwrseq;

	if (pwrseq) {
		module_put(pwrseq->owner);
		host->pwrseq = NULL;
	}
}

int mmc_pwrseq_register(struct mmc_pwrseq *pwrseq)
{
	if (!pwrseq || !pwrseq->ops || !pwrseq->dev)
		return -EINVAL;

	mutex_lock(&pwrseq_list_mutex);
	list_add(&pwrseq->pwrseq_node, &pwrseq_list);
	mutex_unlock(&pwrseq_list_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(mmc_pwrseq_register);

void mmc_pwrseq_unregister(struct mmc_pwrseq *pwrseq)
{
	if (pwrseq) {
		mutex_lock(&pwrseq_list_mutex);
		list_del(&pwrseq->pwrseq_node);
		mutex_unlock(&pwrseq_list_mutex);
	}
}
EXPORT_SYMBOL_GPL(mmc_pwrseq_unregister);

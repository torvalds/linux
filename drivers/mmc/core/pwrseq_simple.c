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

#include <linux/mmc/host.h>

#include "pwrseq.h"

struct mmc_pwrseq_simple {
	struct mmc_pwrseq pwrseq;
};

static void mmc_pwrseq_simple_free(struct mmc_host *host)
{
	struct mmc_pwrseq_simple *pwrseq = container_of(host->pwrseq,
					struct mmc_pwrseq_simple, pwrseq);

	kfree(pwrseq);
	host->pwrseq = NULL;
}

static struct mmc_pwrseq_ops mmc_pwrseq_simple_ops = {
	.free = mmc_pwrseq_simple_free,
};

int mmc_pwrseq_simple_alloc(struct mmc_host *host, struct device *dev)
{
	struct mmc_pwrseq_simple *pwrseq;

	pwrseq = kzalloc(sizeof(struct mmc_pwrseq_simple), GFP_KERNEL);
	if (!pwrseq)
		return -ENOMEM;

	pwrseq->pwrseq.ops = &mmc_pwrseq_simple_ops;
	host->pwrseq = &pwrseq->pwrseq;

	return 0;
}

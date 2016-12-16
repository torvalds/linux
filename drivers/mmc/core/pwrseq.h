/*
 * Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef _MMC_CORE_PWRSEQ_H
#define _MMC_CORE_PWRSEQ_H

#include <linux/mmc/host.h>

struct mmc_pwrseq_ops {
	void (*pre_power_on)(struct mmc_host *host);
	void (*post_power_on)(struct mmc_host *host);
	void (*power_off)(struct mmc_host *host);
};

struct mmc_pwrseq {
	const struct mmc_pwrseq_ops *ops;
	struct device *dev;
	struct list_head pwrseq_node;
	struct module *owner;
};

#ifdef CONFIG_OF

int mmc_pwrseq_register(struct mmc_pwrseq *pwrseq);
void mmc_pwrseq_unregister(struct mmc_pwrseq *pwrseq);

int mmc_pwrseq_alloc(struct mmc_host *host);
void mmc_pwrseq_pre_power_on(struct mmc_host *host);
void mmc_pwrseq_post_power_on(struct mmc_host *host);
void mmc_pwrseq_power_off(struct mmc_host *host);
void mmc_pwrseq_free(struct mmc_host *host);

#else

static inline int mmc_pwrseq_register(struct mmc_pwrseq *pwrseq)
{
	return -ENOSYS;
}
static inline void mmc_pwrseq_unregister(struct mmc_pwrseq *pwrseq) {}
static inline int mmc_pwrseq_alloc(struct mmc_host *host) { return 0; }
static inline void mmc_pwrseq_pre_power_on(struct mmc_host *host) {}
static inline void mmc_pwrseq_post_power_on(struct mmc_host *host) {}
static inline void mmc_pwrseq_power_off(struct mmc_host *host) {}
static inline void mmc_pwrseq_free(struct mmc_host *host) {}

#endif

#endif

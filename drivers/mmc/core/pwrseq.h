/*
 * Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef _MMC_CORE_PWRSEQ_H
#define _MMC_CORE_PWRSEQ_H

struct mmc_pwrseq_ops {
	void (*pre_power_on)(struct mmc_host *host);
	void (*post_power_on)(struct mmc_host *host);
	void (*power_off)(struct mmc_host *host);
	void (*free)(struct mmc_host *host);
};

struct mmc_pwrseq {
	struct mmc_pwrseq_ops *ops;
};

#ifdef CONFIG_OF

int mmc_pwrseq_alloc(struct mmc_host *host);
void mmc_pwrseq_pre_power_on(struct mmc_host *host);
void mmc_pwrseq_post_power_on(struct mmc_host *host);
void mmc_pwrseq_power_off(struct mmc_host *host);
void mmc_pwrseq_free(struct mmc_host *host);

int mmc_pwrseq_simple_alloc(struct mmc_host *host, struct device *dev);
int mmc_pwrseq_emmc_alloc(struct mmc_host *host, struct device *dev);

#else

static inline int mmc_pwrseq_alloc(struct mmc_host *host) { return 0; }
static inline void mmc_pwrseq_pre_power_on(struct mmc_host *host) {}
static inline void mmc_pwrseq_post_power_on(struct mmc_host *host) {}
static inline void mmc_pwrseq_power_off(struct mmc_host *host) {}
static inline void mmc_pwrseq_free(struct mmc_host *host) {}

#endif

#endif

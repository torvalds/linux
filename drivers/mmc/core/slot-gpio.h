/* SPDX-License-Identifier: GPL-2.0-only */
/*
  * Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 */
#ifndef _MMC_CORE_SLOTGPIO_H
#define _MMC_CORE_SLOTGPIO_H

struct mmc_host;

int mmc_gpio_alloc(struct mmc_host *host);

#endif

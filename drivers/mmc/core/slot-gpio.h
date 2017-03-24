/*
  * Copyright (C) 2014 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef _MMC_CORE_SLOTGPIO_H
#define _MMC_CORE_SLOTGPIO_H

struct mmc_host;

int mmc_gpio_alloc(struct mmc_host *host);

#endif

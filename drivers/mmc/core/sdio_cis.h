/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * linux/drivers/mmc/core/sdio_cis.h
 *
 * Author:	Nicolas Pitre
 * Created:	June 11, 2007
 * Copyright:	MontaVista Software Inc.
 */

#ifndef _MMC_SDIO_CIS_H
#define _MMC_SDIO_CIS_H

struct mmc_card;
struct sdio_func;

int sdio_read_common_cis(struct mmc_card *card);
void sdio_free_common_cis(struct mmc_card *card);

int sdio_read_func_cis(struct sdio_func *func);
void sdio_free_func_cis(struct sdio_func *func);

#endif

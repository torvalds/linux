/*
 * linux/drivers/mmc/core/sdio_cis.h
 *
 * Author:	Nicolas Pitre
 * Created:	June 11, 2007
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef _MMC_SDIO_CIS_H
#define _MMC_SDIO_CIS_H

int sdio_read_cis(struct sdio_func *func);
void sdio_free_cis(struct sdio_func *func);

#endif

/*
 * Renesas Mobile SDHI
 *
 * Copyright (C) 2017 Horms Solutions Ltd., Simon Horman
 * Copyright (C) 2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RENESAS_SDHI_H
#define RENESAS_SDHI_H

#include "tmio_mmc.h"

const struct tmio_mmc_dma_ops *renesas_sdhi_get_dma_ops(void);
#endif

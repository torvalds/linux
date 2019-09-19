/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common bus abstraction layer.
 *
 * Copyright (c) 2017-2018, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#ifndef WFX_BUS_H
#define WFX_BUS_H

#include <linux/mmc/sdio_func.h>
#include <linux/spi/spi.h>

extern struct sdio_driver wfx_sdio_driver;
extern struct spi_driver wfx_spi_driver;

#endif

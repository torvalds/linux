/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Forward declarations needed by the bma220 sources.
 *
 * Copyright 2025 Petre Rodan <petre.rodan@subdimension.ro>
 */

#ifndef _BMA220_H
#define _BMA220_H

#include <linux/pm.h>

struct spi_device;

extern const struct dev_pm_ops bma220_pm_ops;

int bma220_common_probe(struct spi_device *dev);

#endif

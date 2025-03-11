/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel PCH/PCU SPI flash driver.
 *
 * Copyright (C) 2016 - 2022, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#ifndef SPI_INTEL_H
#define SPI_INTEL_H

#include <linux/platform_data/x86/spi-intel.h>

struct resource;

extern const struct attribute_group *intel_spi_groups[];

int intel_spi_probe(struct device *dev, struct resource *mem,
		    const struct intel_spi_boardinfo *info);

#endif /* SPI_INTEL_H */

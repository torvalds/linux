// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple MFD - I2C
 *
 * Author: Lee Jones <lee.jones@linaro.org>
 *
 * This driver creates a single register map with the intention for it to be
 * shared by all sub-devices.  Children can use their parent's device structure
 * (dev.parent) in order to reference it.
 *
 * This driver creates a single register map with the intention for it to be
 * shared by all sub-devices.  Children can use their parent's device structure
 * (dev.parent) in order to reference it.
 *
 * Once the register map has been successfully initialised, any sub-devices
 * represented by child nodes in Device Tree or via the MFD cells in the
 * associated C file will be subsequently registered.
 */

#ifndef __MFD_SIMPLE_MFD_I2C_H
#define __MFD_SIMPLE_MFD_I2C_H

#include <linux/mfd/core.h>
#include <linux/regmap.h>

struct simple_mfd_data {
	const struct regmap_config *regmap_config;
	const struct mfd_cell *mfd_cell;
	size_t mfd_cell_size;
};

#endif /* __MFD_SIMPLE_MFD_I2C_H */

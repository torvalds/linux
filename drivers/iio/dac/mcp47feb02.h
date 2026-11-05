/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __DRIVERS_IIO_DAC_MCP47FEB02_H__
#define __DRIVERS_IIO_DAC_MCP47FEB02_H__

#include <linux/pm.h>
#include <linux/types.h>

#define MCP47FEB02_CHIP_INFO(_name, _channels, _res, _vref1, _eeprom) \
static const struct mcp47feb02_features _name##_chip_features = { \
	.name = #_name, \
	.phys_channels = _channels, \
	.resolution = _res, \
	.have_ext_vref1 = _vref1, \
	.have_eeprom = _eeprom, \
}

/**
 * struct mcp47feb02_features - chip specific data
 * @name: device name
 * @phys_channels: number of hardware channels
 * @resolution: DAC resolution
 * @have_ext_vref1: does the hardware have an the second external voltage reference?
 * @have_eeprom: does the hardware have an internal eeprom?
 */
struct mcp47feb02_features {
	const char *name;
	unsigned int phys_channels;
	unsigned int resolution;
	bool have_ext_vref1;
	bool have_eeprom;
};

extern const struct regmap_config mcp47feb02_regmap_config;
extern const struct regmap_config mcp47fvb02_regmap_config;

int mcp47feb02_common_probe(const struct mcp47feb02_features *chip_features,
			    struct regmap *regmap);

extern const struct dev_pm_ops mcp47feb02_pm_ops;

#endif /* __DRIVERS_IIO_DAC_MCP47FEB02_H__ */

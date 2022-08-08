// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 ROHM Semiconductors

#include <linux/errno.h>
#include <linux/mfd/rohm-generic.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

static int set_dvs_level(const struct regulator_desc *desc,
			 struct device_node *np, struct regmap *regmap,
			 char *prop, unsigned int reg, unsigned int mask,
			 unsigned int omask, unsigned int oreg)
{
	int ret, i;
	uint32_t uv;

	ret = of_property_read_u32(np, prop, &uv);
	if (ret) {
		if (ret != -EINVAL)
			return ret;
		return 0;
	}

	if (uv == 0) {
		if (omask)
			return regmap_update_bits(regmap, oreg, omask, 0);
	}
	for (i = 0; i < desc->n_voltages; i++) {
		ret = regulator_desc_list_voltage_linear_range(desc, i);
		if (ret < 0)
			continue;
		if (ret == uv) {
			i <<= ffs(desc->vsel_mask) - 1;
			ret = regmap_update_bits(regmap, reg, mask, i);
			if (omask && !ret)
				ret = regmap_update_bits(regmap, oreg, omask,
							 omask);
			break;
		}
	}
	return ret;
}

int rohm_regulator_set_dvs_levels(const struct rohm_dvs_config *dvs,
			  struct device_node *np,
			  const struct regulator_desc *desc,
			  struct regmap *regmap)
{
	int i, ret = 0;
	char *prop;
	unsigned int reg, mask, omask, oreg = desc->enable_reg;

	for (i = 0; i < ROHM_DVS_LEVEL_VALID_AMOUNT && !ret; i++) {
		int bit;

		bit = BIT(i);
		if (dvs->level_map & bit) {
			switch (bit) {
			case ROHM_DVS_LEVEL_RUN:
				prop = "rohm,dvs-run-voltage";
				reg = dvs->run_reg;
				mask = dvs->run_mask;
				omask = dvs->run_on_mask;
				break;
			case ROHM_DVS_LEVEL_IDLE:
				prop = "rohm,dvs-idle-voltage";
				reg = dvs->idle_reg;
				mask = dvs->idle_mask;
				omask = dvs->idle_on_mask;
				break;
			case ROHM_DVS_LEVEL_SUSPEND:
				prop = "rohm,dvs-suspend-voltage";
				reg = dvs->suspend_reg;
				mask = dvs->suspend_mask;
				omask = dvs->suspend_on_mask;
				break;
			case ROHM_DVS_LEVEL_LPSR:
				prop = "rohm,dvs-lpsr-voltage";
				reg = dvs->lpsr_reg;
				mask = dvs->lpsr_mask;
				omask = dvs->lpsr_on_mask;
				break;
			default:
				return -EINVAL;
			}
			ret = set_dvs_level(desc, np, regmap, prop, reg, mask,
					    omask, oreg);
		}
	}
	return ret;
}
EXPORT_SYMBOL(rohm_regulator_set_dvs_levels);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Matti Vaittinen <matti.vaittinen@fi.rohmeurope.com>");
MODULE_DESCRIPTION("Generic helpers for ROHM PMIC regulator drivers");

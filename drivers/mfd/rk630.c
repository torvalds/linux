// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/rk630.h>

static const struct mfd_cell rk630_devs[] = {
	{
		.name = "rk630-tve",
		.of_compatible = "rockchip,rk630-tve",
	},
};

static const struct regmap_range rk630_grf_readable_ranges[] = {
	regmap_reg_range(PLUMAGE_GRF_GPIO0A_IOMUX, PLUMAGE_GRF_GPIO0A_IOMUX),
	regmap_reg_range(PLUMAGE_GRF_GPIO0B_IOMUX, PLUMAGE_GRF_GPIO0B_IOMUX),
	regmap_reg_range(PLUMAGE_GRF_GPIO0C_IOMUX, PLUMAGE_GRF_GPIO0C_IOMUX),
	regmap_reg_range(PLUMAGE_GRF_GPIO0D_IOMUX, PLUMAGE_GRF_GPIO0D_IOMUX),
	regmap_reg_range(PLUMAGE_GRF_GPIO1A_IOMUX, PLUMAGE_GRF_GPIO1A_IOMUX),
	regmap_reg_range(PLUMAGE_GRF_GPIO1B_IOMUX, PLUMAGE_GRF_GPIO1B_IOMUX),
	regmap_reg_range(PLUMAGE_GRF_GPIO0A_P, PLUMAGE_GRF_GPIO1B_P),
	regmap_reg_range(PLUMAGE_GRF_GPIO1B_SR, PLUMAGE_GRF_GPIO1B_SR),
	regmap_reg_range(PLUMAGE_GRF_GPIO1B_E, PLUMAGE_GRF_GPIO1B_E),
	regmap_reg_range(PLUMAGE_GRF_SOC_CON0, PLUMAGE_GRF_SOC_CON4),
	regmap_reg_range(PLUMAGE_GRF_SOC_STATUS, PLUMAGE_GRF_SOC_STATUS),
	regmap_reg_range(PLUMAGE_GRF_GPIO0_REN0, PLUMAGE_GRF_GPIO1_REN0),
};

static const struct regmap_access_table rk630_grf_readable_table = {
	.yes_ranges = rk630_grf_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk630_grf_readable_ranges),
};

const struct regmap_config rk630_grf_regmap_config = {
	.name = "grf",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = GRF_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.rd_table = &rk630_grf_readable_table,
};
EXPORT_SYMBOL_GPL(rk630_grf_regmap_config);

static const struct regmap_range rk630_cru_readable_ranges[] = {
	regmap_reg_range(CRU_SPLL_CON0, CRU_SPLL_CON2),
	regmap_reg_range(CRU_MODE_CON, CRU_MODE_CON),
	regmap_reg_range(CRU_CLKSEL_CON0, CRU_CLKSEL_CON3),
	regmap_reg_range(CRU_GATE_CON0, CRU_GATE_CON0),
	regmap_reg_range(CRU_SOFTRST_CON0, CRU_SOFTRST_CON0),
};

static const struct regmap_access_table rk630_cru_readable_table = {
	.yes_ranges = rk630_cru_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(rk630_cru_readable_ranges),
};

const struct regmap_config rk630_cru_regmap_config = {
	.name = "cru",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = CRU_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_NATIVE,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.rd_table = &rk630_cru_readable_table,
};

int rk630_core_probe(struct rk630 *rk630)
{
	int ret;

	rk630->reset_gpio = devm_gpiod_get(rk630->dev, "reset", 0);
	if (IS_ERR(rk630->reset_gpio)) {
		ret = PTR_ERR(rk630->reset_gpio);
		dev_err(rk630->dev, "failed to request reset GPIO: %d\n", ret);
		return ret;
	}

	gpiod_direction_output(rk630->reset_gpio, 0);
	usleep_range(2000, 4000);
	gpiod_direction_output(rk630->reset_gpio, 1);
	usleep_range(50000, 60000);
	gpiod_direction_output(rk630->reset_gpio, 0);

	ret = devm_mfd_add_devices(rk630->dev, PLATFORM_DEVID_NONE,
				   rk630_devs, ARRAY_SIZE(rk630_devs),
				   NULL, 0, NULL);
	if (ret) {
		dev_err(rk630->dev, "failed to add MFD children: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rk630_core_probe);

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip rk630 MFD Core driver");
MODULE_LICENSE("GPL v2");

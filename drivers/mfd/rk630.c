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

static int rk630_macphy_enable(struct rk630 *rk630)
{
	u32 val;
	int ret;

	/* IOMUX */
	val = 0xfffc5554;
	ret = regmap_write(rk630->grf, GRF_REG(0x8), val);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	/* IOMUX */
	val = 0x00330021;
	ret = regmap_write(rk630->grf, GRF_REG(0x10), val);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	/* reset */
	val = BIT(12 + 16) | BIT(12);
	ret = regmap_write(rk630->cru, CRU_REG(0x50), val);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to CRU: %d\n", ret);
		return ret;
	}
	udelay(20);

	val = BIT(12 + 16);
	ret = regmap_write(rk630->cru, CRU_REG(0x50), val);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to CRU: %d\n", ret);
		return ret;
	}
	udelay(20);

	/* power up && led*/
	val = BIT(1 + 16) | BIT(1) | BIT(2 + 16);
	ret = regmap_write(rk630->grf, GRF_REG(0x408), val);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}
	usleep_range(20000, 50000);

	/* mdio_sel: mdio */
	val = BIT(8 + 16) | BIT(8);
	ret = regmap_write(rk630->grf, GRF_REG(0x400), val);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	/* mode sel: RMII && clock sel: 24M && BGS value: OTP && id */
	val = (2 << 14) | (0 << 12) | (0x1 << 8) | (6 << 5) | 1;
	ret = regmap_write(rk630->grf, GRF_REG(0x404), val | 0xffff0000);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}
	usleep_range(100, 150);

	return 0;
}

static int rk630_macphy_disable(struct rk630 *rk630)
{
	u32 val;
	int ret;

	/* GRF_SOC_CON2_CFG */
	val = BIT(2) | BIT(16 + 2);
	ret = regmap_write(rk630->grf, GRF_REG(0x408), val);
	if (ret != 0) {
		dev_err(rk630->dev, "Could not write to GRF: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct mfd_cell rk630_devs[] = {
	{
		.name = "rk630-tve",
		.of_compatible = "rockchip,rk630-tve",
	},
	{
		.name = "rk630-macphy",
		.of_compatible = "rockchip,rk630-macphy",
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
EXPORT_SYMBOL_GPL(rk630_cru_regmap_config);

int rk630_core_probe(struct rk630 *rk630)
{
	bool macphy_enabled = false;
	struct device_node *np;
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

	for_each_child_of_node(rk630->dev->of_node, np) {
		if (!of_device_is_compatible(np, "rockchip,rk630-macphy"))
			continue;

		if (!of_device_is_available(np)) {
			continue;
		} else {
			macphy_enabled = true;
			break;
		}
	}

	if (macphy_enabled)
		rk630_macphy_enable(rk630);
	else
		rk630_macphy_disable(rk630);

	return 0;
}
EXPORT_SYMBOL_GPL(rk630_core_probe);

MODULE_AUTHOR("Algea Cao <Algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip rk630 MFD Core driver");
MODULE_LICENSE("GPL v2");

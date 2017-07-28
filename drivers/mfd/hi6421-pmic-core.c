/*
 * Device driver for Hi6421 IC
 *
 * Copyright (c) <2011-2014> HiSilicon Technologies Co., Ltd.
 *              http://www.hisilicon.com
 * Copyright (c) <2013-2014> Linaro Ltd.
 *              http://www.linaro.org
 *
 * Author: Guodong Xu <guodong.xu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/hi6421-pmic.h>

static const struct mfd_cell hi6421_devs[] = {
	{ .name = "hi6421-regulator", },
};

static const struct regmap_config hi6421_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 8,
	.max_register = HI6421_REG_TO_BUS_ADDR(HI6421_REG_MAX),
};

static int hi6421_pmic_probe(struct platform_device *pdev)
{
	struct hi6421_pmic *pmic;
	struct resource *res;
	void __iomem *base;
	int ret;

	pmic = devm_kzalloc(&pdev->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pmic->regmap = devm_regmap_init_mmio_clk(&pdev->dev, NULL, base,
						 &hi6421_regmap_config);
	if (IS_ERR(pmic->regmap)) {
		dev_err(&pdev->dev,
			"regmap init failed: %ld\n", PTR_ERR(pmic->regmap));
		return PTR_ERR(pmic->regmap);
	}

	/* set over-current protection debounce 8ms */
	regmap_update_bits(pmic->regmap, HI6421_OCP_DEB_CTRL_REG,
				(HI6421_OCP_DEB_SEL_MASK
				 | HI6421_OCP_EN_DEBOUNCE_MASK
				 | HI6421_OCP_AUTO_STOP_MASK),
				(HI6421_OCP_DEB_SEL_8MS
				 | HI6421_OCP_EN_DEBOUNCE_ENABLE));

	platform_set_drvdata(pdev, pmic);

	ret = devm_mfd_add_devices(&pdev->dev, 0, hi6421_devs,
				   ARRAY_SIZE(hi6421_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "add mfd devices failed: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id of_hi6421_pmic_match_tbl[] = {
	{ .compatible = "hisilicon,hi6421-pmic", },
	{ },
};
MODULE_DEVICE_TABLE(of, of_hi6421_pmic_match_tbl);

static struct platform_driver hi6421_pmic_driver = {
	.driver = {
		.name	= "hi6421_pmic",
		.of_match_table = of_hi6421_pmic_match_tbl,
	},
	.probe	= hi6421_pmic_probe,
};
module_platform_driver(hi6421_pmic_driver);

MODULE_AUTHOR("Guodong Xu <guodong.xu@linaro.org>");
MODULE_DESCRIPTION("Hi6421 PMIC driver");
MODULE_LICENSE("GPL v2");

/*
 * arch/arm/mach-tegra/board-harmony-pcie.c
 *
 * Copyright (C) 2010 CompuLab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#include <asm/mach-types.h>

#include "board.h"

#ifdef CONFIG_TEGRA_PCI

int __init harmony_pcie_init(void)
{
	struct device_node *np;
	int en_vdd_1v05;
	struct regulator *regulator = NULL;
	int err;

	np = of_find_node_by_path("/regulators/regulator@3");
	if (!np) {
		pr_err("%s: of_find_node_by_path failed\n", __func__);
		return -ENODEV;
	}

	en_vdd_1v05 = of_get_named_gpio(np, "gpio", 0);
	if (en_vdd_1v05 < 0) {
		pr_err("%s: of_get_named_gpio failed: %d\n", __func__,
		       en_vdd_1v05);
		return en_vdd_1v05;
	}

	err = gpio_request(en_vdd_1v05, "EN_VDD_1V05");
	if (err) {
		pr_err("%s: gpio_request failed: %d\n", __func__, err);
		return err;
	}

	gpio_direction_output(en_vdd_1v05, 1);

	regulator = regulator_get(NULL, "vdd_ldo0,vddio_pex_clk");
	if (IS_ERR_OR_NULL(regulator)) {
		pr_err("%s: regulator_get failed: %d\n", __func__,
		       (int)PTR_ERR(regulator));
		goto err_reg;
	}

	regulator_enable(regulator);

	err = tegra_pcie_init(true, true);
	if (err) {
		pr_err("%s: tegra_pcie_init failed: %d\n", __func__, err);
		goto err_pcie;
	}

	return 0;

err_pcie:
	regulator_disable(regulator);
	regulator_put(regulator);
err_reg:
	gpio_free(en_vdd_1v05);

	return err;
}

#endif

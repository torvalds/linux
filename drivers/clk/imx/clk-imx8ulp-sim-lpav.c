// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 NXP
 */

#include <dt-bindings/clock/imx8ulp-clock.h>

#include <linux/auxiliary_bus.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define SYSCTRL0 0x8

#define IMX8ULP_HIFI_CLK_GATE(gname, cname, pname, bidx)	\
	{							\
		.name = gname "_cg",				\
		.id = IMX8ULP_CLK_SIM_LPAV_HIFI_##cname,	\
		.parent = { .fw_name = pname },			\
		.bit = bidx,					\
	}

struct clk_imx8ulp_sim_lpav_data {
	spinlock_t lock; /* shared by MUX, clock gate and reset */
	unsigned long flags; /* for spinlock usage */
	struct clk_hw_onecell_data clk_data; /*  keep last */
};

struct clk_imx8ulp_sim_lpav_gate {
	const char *name;
	int id;
	const struct clk_parent_data parent;
	u8 bit;
};

static struct clk_imx8ulp_sim_lpav_gate gates[] = {
	IMX8ULP_HIFI_CLK_GATE("hifi_core", CORE, "core", 17),
	IMX8ULP_HIFI_CLK_GATE("hifi_pbclk", PBCLK, "bus", 18),
	IMX8ULP_HIFI_CLK_GATE("hifi_plat", PLAT, "plat", 19)
};

static void clk_imx8ulp_sim_lpav_lock(void *arg) __acquires(&data->lock)
{
	struct clk_imx8ulp_sim_lpav_data *data = dev_get_drvdata(arg);

	spin_lock_irqsave(&data->lock, data->flags);
}

static void clk_imx8ulp_sim_lpav_unlock(void *arg) __releases(&data->lock)
{
	struct clk_imx8ulp_sim_lpav_data *data = dev_get_drvdata(arg);

	spin_unlock_irqrestore(&data->lock, data->flags);
}

static int clk_imx8ulp_sim_lpav_probe(struct platform_device *pdev)
{
	const struct regmap_config regmap_config = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
		.lock = clk_imx8ulp_sim_lpav_lock,
		.unlock = clk_imx8ulp_sim_lpav_unlock,
		.lock_arg = &pdev->dev,
	};
	struct clk_imx8ulp_sim_lpav_data *data;
	struct auxiliary_device *adev;
	struct regmap *regmap;
	void __iomem *base;
	struct clk_hw *hw;
	int i, ret;

	data = devm_kzalloc(&pdev->dev,
			    struct_size(data, clk_data.hws, ARRAY_SIZE(gates)),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(&pdev->dev, data);

	/*
	 * this lock is used directly by the clock gate and indirectly
	 * by the reset and mux controller via the regmap API
	 */
	spin_lock_init(&data->lock);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return dev_err_probe(&pdev->dev, PTR_ERR(base),
				     "failed to ioremap base\n");
	/*
	 * although the clock gate doesn't use the regmap API to modify the
	 * registers, we still need the regmap because of the reset auxiliary
	 * driver and the MUX drivers, which use the parent device's regmap
	 */
	regmap = devm_regmap_init_mmio(&pdev->dev, base, &regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
				     "failed to initialize regmap\n");

	data->clk_data.num = ARRAY_SIZE(gates);

	for (i = 0; i < ARRAY_SIZE(gates); i++) {
		hw = devm_clk_hw_register_gate_parent_data(&pdev->dev,
							   gates[i].name,
							   &gates[i].parent,
							   CLK_SET_RATE_PARENT,
							   base + SYSCTRL0,
							   gates[i].bit,
							   0x0, &data->lock);
		if (IS_ERR(hw))
			return dev_err_probe(&pdev->dev, PTR_ERR(hw),
					     "failed to register %s gate\n",
					     gates[i].name);

		data->clk_data.hws[i] = hw;
	}

	adev = devm_auxiliary_device_create(&pdev->dev, "reset", NULL);
	if (!adev)
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "failed to register aux reset\n");

	ret = devm_of_clk_add_hw_provider(&pdev->dev,
					  of_clk_hw_onecell_get,
					  &data->clk_data);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register clk hw provider\n");

	/* used to probe MUX child device */
	return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id clk_imx8ulp_sim_lpav_of_match[] = {
	{ .compatible = "fsl,imx8ulp-sim-lpav" },
	{ }
};
MODULE_DEVICE_TABLE(of, clk_imx8ulp_sim_lpav_of_match);

static struct platform_driver clk_imx8ulp_sim_lpav_driver = {
	.probe = clk_imx8ulp_sim_lpav_probe,
	.driver = {
		.name = "clk-imx8ulp-sim-lpav",
		.of_match_table = clk_imx8ulp_sim_lpav_of_match,
	},
};
module_platform_driver(clk_imx8ulp_sim_lpav_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("i.MX8ULP LPAV System Integration Module (SIM) clock driver");
MODULE_AUTHOR("Laurentiu Mihalcea <laurentiu.mihalcea@nxp.com>");

// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch Reset driver
 *
 * Copyright (c) 2020 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>

struct reset_props {
	u32 protect_reg;
	u32 protect_bit;
	u32 reset_reg;
	u32 reset_bit;
};

struct mchp_reset_context {
	struct regmap *cpu_ctrl;
	struct regmap *gcb_ctrl;
	struct reset_controller_dev rcdev;
	const struct reset_props *props;
};

static struct regmap_config sparx5_reset_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
};

static int sparx5_switch_reset(struct mchp_reset_context *ctx)
{
	u32 val;

	/* Make sure the core is PROTECTED from reset */
	regmap_update_bits(ctx->cpu_ctrl, ctx->props->protect_reg,
			   ctx->props->protect_bit, ctx->props->protect_bit);

	/* Start soft reset */
	regmap_write(ctx->gcb_ctrl, ctx->props->reset_reg,
		     ctx->props->reset_bit);

	/* Wait for soft reset done */
	return regmap_read_poll_timeout(ctx->gcb_ctrl, ctx->props->reset_reg, val,
					(val & ctx->props->reset_bit) == 0,
					1, 100);
}

static int sparx5_reset_noop(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	return 0;
}

static const struct reset_control_ops sparx5_reset_ops = {
	.reset = sparx5_reset_noop,
};

static const struct regmap_config mchp_lan966x_syscon_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static struct regmap *mchp_lan966x_syscon_to_regmap(struct device *dev,
						    struct device_node *syscon_np)
{
	struct regmap_config regmap_config = mchp_lan966x_syscon_regmap_config;
	resource_size_t size;
	void __iomem *base;

	base = devm_of_iomap(dev, syscon_np, 0, &size);
	if (IS_ERR(base))
		return ERR_CAST(base);

	regmap_config.max_register = size - 4;

	return devm_regmap_init_mmio(dev, base, &regmap_config);
}

static int mchp_sparx5_map_syscon(struct platform_device *pdev, char *name,
				  struct regmap **target)
{
	struct device_node *syscon_np;
	struct regmap *regmap;
	int err;

	syscon_np = of_parse_phandle(pdev->dev.of_node, name, 0);
	if (!syscon_np)
		return -ENODEV;

	/*
	 * The syscon API doesn't support syscon device removal.
	 * When used in LAN966x PCI device, the cpu-syscon device needs to be
	 * removed when the PCI device is removed.
	 * In case of LAN966x, map the syscon device locally to support the
	 * device removal.
	 */
	if (of_device_is_compatible(pdev->dev.of_node, "microchip,lan966x-switch-reset"))
		regmap = mchp_lan966x_syscon_to_regmap(&pdev->dev, syscon_np);
	else
		regmap = syscon_node_to_regmap(syscon_np);
	of_node_put(syscon_np);
	if (IS_ERR(regmap)) {
		err = PTR_ERR(regmap);
		dev_err(&pdev->dev, "No '%s' map: %d\n", name, err);
		return err;
	}
	*target = regmap;
	return 0;
}

static int mchp_sparx5_map_io(struct platform_device *pdev, int index,
			      struct regmap **target)
{
	struct resource *res;
	struct regmap *map;
	void __iomem *mem;

	mem = devm_platform_get_and_ioremap_resource(pdev, index, &res);
	if (IS_ERR(mem)) {
		dev_err(&pdev->dev, "Could not map resource %d\n", index);
		return PTR_ERR(mem);
	}
	sparx5_reset_regmap_config.name = res->name;
	map = devm_regmap_init_mmio(&pdev->dev, mem, &sparx5_reset_regmap_config);
	if (IS_ERR(map))
		return PTR_ERR(map);
	*target = map;
	return 0;
}

static int mchp_sparx5_reset_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	struct mchp_reset_context *ctx;
	int err;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	err = mchp_sparx5_map_syscon(pdev, "cpu-syscon", &ctx->cpu_ctrl);
	if (err)
		return err;
	err = mchp_sparx5_map_io(pdev, 0, &ctx->gcb_ctrl);
	if (err)
		return err;

	ctx->rcdev.owner = THIS_MODULE;
	ctx->rcdev.dev = &pdev->dev;
	ctx->rcdev.nr_resets = 1;
	ctx->rcdev.ops = &sparx5_reset_ops;
	ctx->rcdev.of_node = dn;
	ctx->props = device_get_match_data(&pdev->dev);

	/* Issue the reset very early, our actual reset callback is a noop. */
	err = sparx5_switch_reset(ctx);
	if (err)
		return err;

	return devm_reset_controller_register(&pdev->dev, &ctx->rcdev);
}

static const struct reset_props reset_props_sparx5 = {
	.protect_reg    = 0x84,
	.protect_bit    = BIT(10),
	.reset_reg      = 0x0,
	.reset_bit      = BIT(1),
};

static const struct reset_props reset_props_lan966x = {
	.protect_reg    = 0x88,
	.protect_bit    = BIT(5),
	.reset_reg      = 0x0,
	.reset_bit      = BIT(1),
};

static const struct of_device_id mchp_sparx5_reset_of_match[] = {
	{
		.compatible = "microchip,sparx5-switch-reset",
		.data = &reset_props_sparx5,
	}, {
		.compatible = "microchip,lan966x-switch-reset",
		.data = &reset_props_lan966x,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mchp_sparx5_reset_of_match);

static struct platform_driver mchp_sparx5_reset_driver = {
	.probe = mchp_sparx5_reset_probe,
	.driver = {
		.name = "sparx5-switch-reset",
		.of_match_table = mchp_sparx5_reset_of_match,
	},
};

static int __init mchp_sparx5_reset_init(void)
{
	return platform_driver_register(&mchp_sparx5_reset_driver);
}

/*
 * Because this is a global reset, keep this postcore_initcall() to issue the
 * reset as early as possible during the kernel startup.
 */
postcore_initcall(mchp_sparx5_reset_init);

MODULE_DESCRIPTION("Microchip Sparx5 switch reset driver");
MODULE_AUTHOR("Steen Hegelund <steen.hegelund@microchip.com>");
MODULE_LICENSE("GPL");

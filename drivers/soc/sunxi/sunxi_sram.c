/*
 * Allwinner SoCs SRAM Controller Driver
 *
 * Copyright (C) 2015 Maxime Ripard
 *
 * Author: Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/soc/sunxi/sunxi_sram.h>

struct sunxi_sram_func {
	char	*func;
	u8	val;
	u32	reg_val;
};

struct sunxi_sram_data {
	char			*name;
	u8			reg;
	u8			offset;
	u8			width;
	struct sunxi_sram_func	*func;
	struct list_head	list;
};

struct sunxi_sram_desc {
	struct sunxi_sram_data	data;
	bool			claimed;
};

#define SUNXI_SRAM_MAP(_reg_val, _val, _func)			\
	{							\
		.func = _func,					\
		.val = _val,					\
		.reg_val = _reg_val,				\
	}

#define SUNXI_SRAM_DATA(_name, _reg, _off, _width, ...)		\
	{							\
		.name = _name,					\
		.reg = _reg,					\
		.offset = _off,					\
		.width = _width,				\
		.func = (struct sunxi_sram_func[]){		\
			__VA_ARGS__, { } },			\
	}

static struct sunxi_sram_desc sun4i_a10_sram_a3_a4 = {
	.data	= SUNXI_SRAM_DATA("A3-A4", 0x4, 0x4, 2,
				  SUNXI_SRAM_MAP(0, 0, "cpu"),
				  SUNXI_SRAM_MAP(1, 1, "emac")),
};

static struct sunxi_sram_desc sun4i_a10_sram_c1 = {
	.data	= SUNXI_SRAM_DATA("C1", 0x0, 0x0, 31,
				  SUNXI_SRAM_MAP(0, 0, "cpu"),
				  SUNXI_SRAM_MAP(0x7fffffff, 1, "ve")),
};

static struct sunxi_sram_desc sun4i_a10_sram_d = {
	.data	= SUNXI_SRAM_DATA("D", 0x4, 0x0, 1,
				  SUNXI_SRAM_MAP(0, 0, "cpu"),
				  SUNXI_SRAM_MAP(1, 1, "usb-otg")),
};

static struct sunxi_sram_desc sun50i_a64_sram_c = {
	.data	= SUNXI_SRAM_DATA("C", 0x4, 24, 1,
				  SUNXI_SRAM_MAP(1, 0, "cpu"),
				  SUNXI_SRAM_MAP(0, 1, "de2")),
};

static const struct of_device_id sunxi_sram_dt_ids[] = {
	{
		.compatible	= "allwinner,sun4i-a10-sram-a3-a4",
		.data		= &sun4i_a10_sram_a3_a4.data,
	},
	{
		.compatible	= "allwinner,sun4i-a10-sram-c1",
		.data		= &sun4i_a10_sram_c1.data,
	},
	{
		.compatible	= "allwinner,sun4i-a10-sram-d",
		.data		= &sun4i_a10_sram_d.data,
	},
	{
		.compatible	= "allwinner,sun50i-a64-sram-c",
		.data		= &sun50i_a64_sram_c.data,
	},
	{}
};

static struct device *sram_dev;
static LIST_HEAD(claimed_sram);
static DEFINE_SPINLOCK(sram_lock);
static void __iomem *base;

static int sunxi_sram_show(struct seq_file *s, void *data)
{
	struct device_node *sram_node, *section_node;
	const struct sunxi_sram_data *sram_data;
	const struct of_device_id *match;
	struct sunxi_sram_func *func;
	const __be32 *sram_addr_p, *section_addr_p;
	u32 val;

	seq_puts(s, "Allwinner sunXi SRAM\n");
	seq_puts(s, "--------------------\n\n");

	for_each_child_of_node(sram_dev->of_node, sram_node) {
		if (!of_device_is_compatible(sram_node, "mmio-sram"))
			continue;

		sram_addr_p = of_get_address(sram_node, 0, NULL, NULL);

		seq_printf(s, "sram@%08x\n",
			   be32_to_cpu(*sram_addr_p));

		for_each_child_of_node(sram_node, section_node) {
			match = of_match_node(sunxi_sram_dt_ids, section_node);
			if (!match)
				continue;
			sram_data = match->data;

			section_addr_p = of_get_address(section_node, 0,
							NULL, NULL);

			seq_printf(s, "\tsection@%04x\t(%s)\n",
				   be32_to_cpu(*section_addr_p),
				   sram_data->name);

			val = readl(base + sram_data->reg);
			val >>= sram_data->offset;
			val &= GENMASK(sram_data->width - 1, 0);

			for (func = sram_data->func; func->func; func++) {
				seq_printf(s, "\t\t%s%c\n", func->func,
					   func->reg_val == val ?
					   '*' : ' ');
			}
		}

		seq_puts(s, "\n");
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sunxi_sram);

static inline struct sunxi_sram_desc *to_sram_desc(const struct sunxi_sram_data *data)
{
	return container_of(data, struct sunxi_sram_desc, data);
}

static const struct sunxi_sram_data *sunxi_sram_of_parse(struct device_node *node,
							 unsigned int *reg_value)
{
	const struct of_device_id *match;
	const struct sunxi_sram_data *data;
	struct sunxi_sram_func *func;
	struct of_phandle_args args;
	u8 val;
	int ret;

	ret = of_parse_phandle_with_fixed_args(node, "allwinner,sram", 1, 0,
					       &args);
	if (ret)
		return ERR_PTR(ret);

	if (!of_device_is_available(args.np)) {
		ret = -EBUSY;
		goto err;
	}

	val = args.args[0];

	match = of_match_node(sunxi_sram_dt_ids, args.np);
	if (!match) {
		ret = -EINVAL;
		goto err;
	}

	data = match->data;
	if (!data) {
		ret = -EINVAL;
		goto err;
	}

	for (func = data->func; func->func; func++) {
		if (val == func->val) {
			if (reg_value)
				*reg_value = func->reg_val;

			break;
		}
	}

	if (!func->func) {
		ret = -EINVAL;
		goto err;
	}

	of_node_put(args.np);
	return match->data;

err:
	of_node_put(args.np);
	return ERR_PTR(ret);
}

int sunxi_sram_claim(struct device *dev)
{
	const struct sunxi_sram_data *sram_data;
	struct sunxi_sram_desc *sram_desc;
	unsigned int device;
	u32 val, mask;

	if (IS_ERR(base))
		return PTR_ERR(base);

	if (!base)
		return -EPROBE_DEFER;

	if (!dev || !dev->of_node)
		return -EINVAL;

	sram_data = sunxi_sram_of_parse(dev->of_node, &device);
	if (IS_ERR(sram_data))
		return PTR_ERR(sram_data);

	sram_desc = to_sram_desc(sram_data);

	spin_lock(&sram_lock);

	if (sram_desc->claimed) {
		spin_unlock(&sram_lock);
		return -EBUSY;
	}

	mask = GENMASK(sram_data->offset + sram_data->width - 1,
		       sram_data->offset);
	val = readl(base + sram_data->reg);
	val &= ~mask;
	writel(val | ((device << sram_data->offset) & mask),
	       base + sram_data->reg);

	sram_desc->claimed = true;
	spin_unlock(&sram_lock);

	return 0;
}
EXPORT_SYMBOL(sunxi_sram_claim);

void sunxi_sram_release(struct device *dev)
{
	const struct sunxi_sram_data *sram_data;
	struct sunxi_sram_desc *sram_desc;

	if (!dev || !dev->of_node)
		return;

	sram_data = sunxi_sram_of_parse(dev->of_node, NULL);
	if (IS_ERR(sram_data))
		return;

	sram_desc = to_sram_desc(sram_data);

	spin_lock(&sram_lock);
	sram_desc->claimed = false;
	spin_unlock(&sram_lock);
}
EXPORT_SYMBOL(sunxi_sram_release);

struct sunxi_sramc_variant {
	int num_emac_clocks;
	bool has_ldo_ctrl;
};

static const struct sunxi_sramc_variant sun4i_a10_sramc_variant = {
	/* Nothing special */
};

static const struct sunxi_sramc_variant sun8i_h3_sramc_variant = {
	.num_emac_clocks = 1,
};

static const struct sunxi_sramc_variant sun20i_d1_sramc_variant = {
	.num_emac_clocks = 1,
	.has_ldo_ctrl = true,
};

static const struct sunxi_sramc_variant sun50i_a64_sramc_variant = {
	.num_emac_clocks = 1,
};

static const struct sunxi_sramc_variant sun50i_h616_sramc_variant = {
	.num_emac_clocks = 2,
};

#define SUNXI_SRAM_EMAC_CLOCK_REG	0x30
#define SUNXI_SYS_LDO_CTRL_REG		0x150

static bool sunxi_sram_regmap_accessible_reg(struct device *dev,
					     unsigned int reg)
{
	const struct sunxi_sramc_variant *variant = dev_get_drvdata(dev);

	if (reg >= SUNXI_SRAM_EMAC_CLOCK_REG &&
	    reg <  SUNXI_SRAM_EMAC_CLOCK_REG + variant->num_emac_clocks * 4)
		return true;
	if (reg == SUNXI_SYS_LDO_CTRL_REG && variant->has_ldo_ctrl)
		return true;

	return false;
}

static struct regmap_config sunxi_sram_regmap_config = {
	.reg_bits       = 32,
	.val_bits       = 32,
	.reg_stride     = 4,
	/* last defined register */
	.max_register   = SUNXI_SYS_LDO_CTRL_REG,
	/* other devices have no business accessing other registers */
	.readable_reg	= sunxi_sram_regmap_accessible_reg,
	.writeable_reg	= sunxi_sram_regmap_accessible_reg,
};

static int __init sunxi_sram_probe(struct platform_device *pdev)
{
	const struct sunxi_sramc_variant *variant;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;

	sram_dev = &pdev->dev;

	variant = of_device_get_match_data(&pdev->dev);
	if (!variant)
		return -EINVAL;

	dev_set_drvdata(dev, (struct sunxi_sramc_variant *)variant);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (variant->num_emac_clocks || variant->has_ldo_ctrl) {
		regmap = devm_regmap_init_mmio(dev, base, &sunxi_sram_regmap_config);
		if (IS_ERR(regmap))
			return PTR_ERR(regmap);
	}

	of_platform_populate(dev->of_node, NULL, NULL, dev);

	debugfs_create_file("sram", 0444, NULL, NULL, &sunxi_sram_fops);

	return 0;
}

static const struct of_device_id sunxi_sram_dt_match[] = {
	{
		.compatible = "allwinner,sun4i-a10-sram-controller",
		.data = &sun4i_a10_sramc_variant,
	},
	{
		.compatible = "allwinner,sun4i-a10-system-control",
		.data = &sun4i_a10_sramc_variant,
	},
	{
		.compatible = "allwinner,sun5i-a13-system-control",
		.data = &sun4i_a10_sramc_variant,
	},
	{
		.compatible = "allwinner,sun8i-a23-system-control",
		.data = &sun4i_a10_sramc_variant,
	},
	{
		.compatible = "allwinner,sun8i-h3-system-control",
		.data = &sun8i_h3_sramc_variant,
	},
	{
		.compatible = "allwinner,sun20i-d1-system-control",
		.data = &sun20i_d1_sramc_variant,
	},
	{
		.compatible = "allwinner,sun50i-a64-sram-controller",
		.data = &sun50i_a64_sramc_variant,
	},
	{
		.compatible = "allwinner,sun50i-a64-system-control",
		.data = &sun50i_a64_sramc_variant,
	},
	{
		.compatible = "allwinner,sun50i-h5-system-control",
		.data = &sun50i_a64_sramc_variant,
	},
	{
		.compatible = "allwinner,sun50i-h616-system-control",
		.data = &sun50i_h616_sramc_variant,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, sunxi_sram_dt_match);

static struct platform_driver sunxi_sram_driver = {
	.driver = {
		.name		= "sunxi-sram",
		.of_match_table	= sunxi_sram_dt_match,
	},
};
builtin_platform_driver_probe(sunxi_sram_driver, sunxi_sram_probe);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner sunXi SRAM Controller Driver");

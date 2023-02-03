// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel IXP4xx Expansion Bus Controller
 * Copyright (C) 2021 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/log2.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define IXP4XX_EXP_NUM_CS		8

#define IXP4XX_EXP_TIMING_CS0		0x00
#define IXP4XX_EXP_TIMING_CS1		0x04
#define IXP4XX_EXP_TIMING_CS2		0x08
#define IXP4XX_EXP_TIMING_CS3		0x0c
#define IXP4XX_EXP_TIMING_CS4		0x10
#define IXP4XX_EXP_TIMING_CS5		0x14
#define IXP4XX_EXP_TIMING_CS6		0x18
#define IXP4XX_EXP_TIMING_CS7		0x1c

/* Bits inside each CS timing register */
#define IXP4XX_EXP_TIMING_STRIDE	0x04
#define IXP4XX_EXP_CS_EN		BIT(31)
#define IXP456_EXP_PAR_EN		BIT(30) /* Only on IXP45x and IXP46x */
#define IXP4XX_EXP_T1_MASK		GENMASK(28, 27)
#define IXP4XX_EXP_T1_SHIFT		28
#define IXP4XX_EXP_T2_MASK		GENMASK(27, 26)
#define IXP4XX_EXP_T2_SHIFT		26
#define IXP4XX_EXP_T3_MASK		GENMASK(25, 22)
#define IXP4XX_EXP_T3_SHIFT		22
#define IXP4XX_EXP_T4_MASK		GENMASK(21, 20)
#define IXP4XX_EXP_T4_SHIFT		20
#define IXP4XX_EXP_T5_MASK		GENMASK(19, 16)
#define IXP4XX_EXP_T5_SHIFT		16
#define IXP4XX_EXP_CYC_TYPE_MASK	GENMASK(15, 14)
#define IXP4XX_EXP_CYC_TYPE_SHIFT	14
#define IXP4XX_EXP_SIZE_MASK		GENMASK(13, 10)
#define IXP4XX_EXP_SIZE_SHIFT		10
#define IXP4XX_EXP_CNFG_0		BIT(9) /* Always zero */
#define IXP43X_EXP_SYNC_INTEL		BIT(8) /* Only on IXP43x */
#define IXP43X_EXP_EXP_CHIP		BIT(7) /* Only on IXP43x, dangerous to touch on IXP42x */
#define IXP4XX_EXP_BYTE_RD16		BIT(6)
#define IXP4XX_EXP_HRDY_POL		BIT(5) /* Only on IXP42x */
#define IXP4XX_EXP_MUX_EN		BIT(4)
#define IXP4XX_EXP_SPLT_EN		BIT(3)
#define IXP4XX_EXP_WORD			BIT(2) /* Always zero */
#define IXP4XX_EXP_WR_EN		BIT(1)
#define IXP4XX_EXP_BYTE_EN		BIT(0)

#define IXP4XX_EXP_CNFG0		0x20
#define IXP4XX_EXP_CNFG0_MEM_MAP	BIT(31)
#define IXP4XX_EXP_CNFG1		0x24

#define IXP4XX_EXP_BOOT_BASE		0x00000000
#define IXP4XX_EXP_NORMAL_BASE		0x50000000
#define IXP4XX_EXP_STRIDE		0x01000000

/* Fuses on the IXP43x */
#define IXP43X_EXP_UNIT_FUSE_RESET	0x28
#define IXP43x_EXP_FUSE_SPEED_MASK	GENMASK(23, 22)

/* Number of device tree values in "reg" */
#define IXP4XX_OF_REG_SIZE		3

struct ixp4xx_eb {
	struct device *dev;
	struct regmap *rmap;
	u32 bus_base;
	bool is_42x;
	bool is_43x;
};

struct ixp4xx_exp_tim_prop {
	const char *prop;
	u32 max;
	u32 mask;
	u16 shift;
};

static const struct ixp4xx_exp_tim_prop ixp4xx_exp_tim_props[] = {
	{
		.prop = "intel,ixp4xx-eb-t1",
		.max = 3,
		.mask = IXP4XX_EXP_T1_MASK,
		.shift = IXP4XX_EXP_T1_SHIFT,
	},
	{
		.prop = "intel,ixp4xx-eb-t2",
		.max = 3,
		.mask = IXP4XX_EXP_T2_MASK,
		.shift = IXP4XX_EXP_T2_SHIFT,
	},
	{
		.prop = "intel,ixp4xx-eb-t3",
		.max = 15,
		.mask = IXP4XX_EXP_T3_MASK,
		.shift = IXP4XX_EXP_T3_SHIFT,
	},
	{
		.prop = "intel,ixp4xx-eb-t4",
		.max = 3,
		.mask = IXP4XX_EXP_T4_MASK,
		.shift = IXP4XX_EXP_T4_SHIFT,
	},
	{
		.prop = "intel,ixp4xx-eb-t5",
		.max = 15,
		.mask = IXP4XX_EXP_T5_MASK,
		.shift = IXP4XX_EXP_T5_SHIFT,
	},
	{
		.prop = "intel,ixp4xx-eb-byte-access-on-halfword",
		.max = 1,
		.mask = IXP4XX_EXP_BYTE_RD16,
	},
	{
		.prop = "intel,ixp4xx-eb-hpi-hrdy-pol-high",
		.max = 1,
		.mask = IXP4XX_EXP_HRDY_POL,
	},
	{
		.prop = "intel,ixp4xx-eb-mux-address-and-data",
		.max = 1,
		.mask = IXP4XX_EXP_MUX_EN,
	},
	{
		.prop = "intel,ixp4xx-eb-ahb-split-transfers",
		.max = 1,
		.mask = IXP4XX_EXP_SPLT_EN,
	},
	{
		.prop = "intel,ixp4xx-eb-write-enable",
		.max = 1,
		.mask = IXP4XX_EXP_WR_EN,
	},
	{
		.prop = "intel,ixp4xx-eb-byte-access",
		.max = 1,
		.mask = IXP4XX_EXP_BYTE_EN,
	},
};

static void ixp4xx_exp_setup_chipselect(struct ixp4xx_eb *eb,
					struct device_node *np,
					u32 cs_index,
					u32 cs_size)
{
	u32 cs_cfg;
	u32 val;
	u32 cur_cssize;
	u32 cs_order;
	int ret;
	int i;

	if (eb->is_42x && (cs_index > 7)) {
		dev_err(eb->dev,
			"invalid chipselect %u, we only support 0-7\n",
			cs_index);
		return;
	}
	if (eb->is_43x && (cs_index > 3)) {
		dev_err(eb->dev,
			"invalid chipselect %u, we only support 0-3\n",
			cs_index);
		return;
	}

	/* Several chip selects can be joined into one device */
	if (cs_size > IXP4XX_EXP_STRIDE)
		cur_cssize = IXP4XX_EXP_STRIDE;
	else
		cur_cssize = cs_size;


	/*
	 * The following will read/modify/write the configuration for one
	 * chipselect, attempting to leave the boot defaults in place unless
	 * something is explicitly defined.
	 */
	regmap_read(eb->rmap, IXP4XX_EXP_TIMING_CS0 +
		    IXP4XX_EXP_TIMING_STRIDE * cs_index, &cs_cfg);
	dev_info(eb->dev, "CS%d at %#08x, size %#08x, config before: %#08x\n",
		 cs_index, eb->bus_base + IXP4XX_EXP_STRIDE * cs_index,
		 cur_cssize, cs_cfg);

	/* Size set-up first align to 2^9 .. 2^24 */
	cur_cssize = roundup_pow_of_two(cur_cssize);
	if (cur_cssize < 512)
		cur_cssize = 512;
	cs_order = ilog2(cur_cssize);
	if (cs_order < 9 || cs_order > 24) {
		dev_err(eb->dev, "illegal size order %d\n", cs_order);
		return;
	}
	dev_dbg(eb->dev, "CS%d size order: %d\n", cs_index, cs_order);
	cs_cfg &= ~(IXP4XX_EXP_SIZE_MASK);
	cs_cfg |= ((cs_order - 9) << IXP4XX_EXP_SIZE_SHIFT);

	for (i = 0; i < ARRAY_SIZE(ixp4xx_exp_tim_props); i++) {
		const struct ixp4xx_exp_tim_prop *ip = &ixp4xx_exp_tim_props[i];

		/* All are regular u32 values */
		ret = of_property_read_u32(np, ip->prop, &val);
		if (ret)
			continue;

		/* Handle bools (single bits) first */
		if (ip->max == 1) {
			if (val)
				cs_cfg |= ip->mask;
			else
				cs_cfg &= ~ip->mask;
			dev_info(eb->dev, "CS%d %s %s\n", cs_index,
				 val ? "enabled" : "disabled",
				 ip->prop);
			continue;
		}

		if (val > ip->max) {
			dev_err(eb->dev,
				"CS%d too high value for %s: %u, capped at %u\n",
				cs_index, ip->prop, val, ip->max);
			val = ip->max;
		}
		/* This assumes max value fills all the assigned bits (and it does) */
		cs_cfg &= ~ip->mask;
		cs_cfg |= (val << ip->shift);
		dev_info(eb->dev, "CS%d set %s to %u\n", cs_index, ip->prop, val);
	}

	ret = of_property_read_u32(np, "intel,ixp4xx-eb-cycle-type", &val);
	if (!ret) {
		if (val > 3) {
			dev_err(eb->dev, "illegal cycle type %d\n", val);
			return;
		}
		dev_info(eb->dev, "CS%d set cycle type %d\n", cs_index, val);
		cs_cfg &= ~IXP4XX_EXP_CYC_TYPE_MASK;
		cs_cfg |= val << IXP4XX_EXP_CYC_TYPE_SHIFT;
	}

	if (eb->is_43x) {
		/* Should always be zero */
		cs_cfg &= ~IXP4XX_EXP_WORD;
		/*
		 * This bit for Intel strata flash is currently unused, but let's
		 * report it if we find one.
		 */
		if (cs_cfg & IXP43X_EXP_SYNC_INTEL)
			dev_info(eb->dev, "claims to be Intel strata flash\n");
	}
	cs_cfg |= IXP4XX_EXP_CS_EN;

	regmap_write(eb->rmap,
		     IXP4XX_EXP_TIMING_CS0 + IXP4XX_EXP_TIMING_STRIDE * cs_index,
		     cs_cfg);
	dev_info(eb->dev, "CS%d wrote %#08x into CS config\n", cs_index, cs_cfg);

	/*
	 * If several chip selects are joined together into one big
	 * device area, we call ourselves recursively for each successive
	 * chip select. For a 32MB flash chip this results in two calls
	 * for example.
	 */
	if (cs_size > IXP4XX_EXP_STRIDE)
		ixp4xx_exp_setup_chipselect(eb, np,
					    cs_index + 1,
					    cs_size - IXP4XX_EXP_STRIDE);
}

static void ixp4xx_exp_setup_child(struct ixp4xx_eb *eb,
				   struct device_node *np)
{
	u32 cs_sizes[IXP4XX_EXP_NUM_CS];
	int num_regs;
	u32 csindex;
	u32 cssize;
	int ret;
	int i;

	num_regs = of_property_count_elems_of_size(np, "reg", IXP4XX_OF_REG_SIZE);
	if (num_regs <= 0)
		return;
	dev_dbg(eb->dev, "child %s has %d register sets\n",
		of_node_full_name(np), num_regs);

	for (csindex = 0; csindex < IXP4XX_EXP_NUM_CS; csindex++)
		cs_sizes[csindex] = 0;

	for (i = 0; i < num_regs; i++) {
		u32 rbase, rsize;

		ret = of_property_read_u32_index(np, "reg",
						 i * IXP4XX_OF_REG_SIZE, &csindex);
		if (ret)
			break;
		ret = of_property_read_u32_index(np, "reg",
						 i * IXP4XX_OF_REG_SIZE + 1, &rbase);
		if (ret)
			break;
		ret = of_property_read_u32_index(np, "reg",
						 i * IXP4XX_OF_REG_SIZE + 2, &rsize);
		if (ret)
			break;

		if (csindex >= IXP4XX_EXP_NUM_CS) {
			dev_err(eb->dev, "illegal CS %d\n", csindex);
			continue;
		}
		/*
		 * The memory window always starts from CS base so we need to add
		 * the start and size to get to the size from the start of the CS
		 * base. For example if CS0 is at 0x50000000 and the reg is
		 * <0 0xe40000 0x40000> the size is e80000.
		 *
		 * Roof this if we have several regs setting the same CS.
		 */
		cssize = rbase + rsize;
		dev_dbg(eb->dev, "CS%d size %#08x\n", csindex, cssize);
		if (cs_sizes[csindex] < cssize)
			cs_sizes[csindex] = cssize;
	}

	for (csindex = 0; csindex < IXP4XX_EXP_NUM_CS; csindex++) {
		cssize = cs_sizes[csindex];
		if (!cssize)
			continue;
		/* Just this one, so set it up and return */
		ixp4xx_exp_setup_chipselect(eb, np, csindex, cssize);
	}
}

static int ixp4xx_exp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct ixp4xx_eb *eb;
	struct device_node *child;
	bool have_children = false;
	u32 val;
	int ret;

	eb = devm_kzalloc(dev, sizeof(*eb), GFP_KERNEL);
	if (!eb)
		return -ENOMEM;

	eb->dev = dev;
	eb->is_42x = of_device_is_compatible(np, "intel,ixp42x-expansion-bus-controller");
	eb->is_43x = of_device_is_compatible(np, "intel,ixp43x-expansion-bus-controller");

	eb->rmap = syscon_node_to_regmap(np);
	if (IS_ERR(eb->rmap))
		return dev_err_probe(dev, PTR_ERR(eb->rmap), "no regmap\n");

	/* We check that the regmap work only on first read */
	ret = regmap_read(eb->rmap, IXP4XX_EXP_CNFG0, &val);
	if (ret)
		return dev_err_probe(dev, ret, "cannot read regmap\n");
	if (val & IXP4XX_EXP_CNFG0_MEM_MAP)
		eb->bus_base = IXP4XX_EXP_BOOT_BASE;
	else
		eb->bus_base = IXP4XX_EXP_NORMAL_BASE;
	dev_info(dev, "expansion bus at %08x\n", eb->bus_base);

	if (eb->is_43x) {
		/* Check some fuses */
		regmap_read(eb->rmap, IXP43X_EXP_UNIT_FUSE_RESET, &val);
		switch (FIELD_GET(IXP43x_EXP_FUSE_SPEED_MASK, val)) {
		case 0:
			dev_info(dev, "IXP43x at 533 MHz\n");
			break;
		case 1:
			dev_info(dev, "IXP43x at 400 MHz\n");
			break;
		case 2:
			dev_info(dev, "IXP43x at 667 MHz\n");
			break;
		default:
			dev_info(dev, "IXP43x unknown speed\n");
			break;
		}
	}

	/* Walk over the child nodes and see what chipselects we use */
	for_each_available_child_of_node(np, child) {
		ixp4xx_exp_setup_child(eb, child);
		/* We have at least one child */
		have_children = true;
	}

	if (have_children)
		return of_platform_default_populate(np, NULL, dev);

	return 0;
}

static const struct of_device_id ixp4xx_exp_of_match[] = {
	{ .compatible = "intel,ixp42x-expansion-bus-controller", },
	{ .compatible = "intel,ixp43x-expansion-bus-controller", },
	{ .compatible = "intel,ixp45x-expansion-bus-controller", },
	{ .compatible = "intel,ixp46x-expansion-bus-controller", },
	{ }
};

static struct platform_driver ixp4xx_exp_driver = {
	.probe = ixp4xx_exp_probe,
	.driver = {
		.name = "intel-extbus",
		.of_match_table = ixp4xx_exp_of_match,
	},
};
module_platform_driver(ixp4xx_exp_driver);
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Intel IXP4xx external bus driver");
MODULE_LICENSE("GPL");

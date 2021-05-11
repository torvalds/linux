// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm External Bus Interface 2 (EBI2) driver
 * an older version of the Qualcomm Parallel Interface Controller (QPIC)
 *
 * Copyright (C) 2016 Linaro Ltd.
 *
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * See the device tree bindings for this block for more details on the
 * hardware.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>

/*
 * CS0, CS1, CS4 and CS5 are two bits wide, CS2 and CS3 are one bit.
 */
#define EBI2_CS0_ENABLE_MASK BIT(0)|BIT(1)
#define EBI2_CS1_ENABLE_MASK BIT(2)|BIT(3)
#define EBI2_CS2_ENABLE_MASK BIT(4)
#define EBI2_CS3_ENABLE_MASK BIT(5)
#define EBI2_CS4_ENABLE_MASK BIT(6)|BIT(7)
#define EBI2_CS5_ENABLE_MASK BIT(8)|BIT(9)
#define EBI2_CSN_MASK GENMASK(9, 0)

#define EBI2_XMEM_CFG 0x0000 /* Power management etc */

/*
 * SLOW CSn CFG
 *
 * Bits 31-28: RECOVERY recovery cycles (0 = 1, 1 = 2 etc) this is the time the
 *             memory continues to drive the data bus after OE is de-asserted.
 *             Inserted when reading one CS and switching to another CS or read
 *             followed by write on the same CS. Valid values 0 thru 15.
 * Bits 27-24: WR_HOLD write hold cycles, these are extra cycles inserted after
 *             every write minimum 1. The data out is driven from the time WE is
 *             asserted until CS is asserted. With a hold of 1, the CS stays
 *             active for 1 extra cycle etc. Valid values 0 thru 15.
 * Bits 23-16: WR_DELTA initial latency for write cycles inserted for the first
 *             write to a page or burst memory
 * Bits 15-8:  RD_DELTA initial latency for read cycles inserted for the first
 *             read to a page or burst memory
 * Bits 7-4:   WR_WAIT number of wait cycles for every write access, 0=1 cycle
 *             so 1 thru 16 cycles.
 * Bits 3-0:   RD_WAIT number of wait cycles for every read access, 0=1 cycle
 *             so 1 thru 16 cycles.
 */
#define EBI2_XMEM_CS0_SLOW_CFG 0x0008
#define EBI2_XMEM_CS1_SLOW_CFG 0x000C
#define EBI2_XMEM_CS2_SLOW_CFG 0x0010
#define EBI2_XMEM_CS3_SLOW_CFG 0x0014
#define EBI2_XMEM_CS4_SLOW_CFG 0x0018
#define EBI2_XMEM_CS5_SLOW_CFG 0x001C

#define EBI2_XMEM_RECOVERY_SHIFT	28
#define EBI2_XMEM_WR_HOLD_SHIFT		24
#define EBI2_XMEM_WR_DELTA_SHIFT	16
#define EBI2_XMEM_RD_DELTA_SHIFT	8
#define EBI2_XMEM_WR_WAIT_SHIFT		4
#define EBI2_XMEM_RD_WAIT_SHIFT		0

/*
 * FAST CSn CFG
 * Bits 31-28: ?
 * Bits 27-24: RD_HOLD: the length in cycles of the first segment of a read
 *             transfer. For a single read trandfer this will be the time
 *             from CS assertion to OE assertion.
 * Bits 18-24: ?
 * Bits 17-16: ADV_OE_RECOVERY, the number of cycles elapsed before an OE
 *             assertion, with respect to the cycle where ADV is asserted.
 *             2 means 2 cycles between ADV and OE. Values 0, 1, 2 or 3.
 * Bits 5:     ADDR_HOLD_ENA, The address is held for an extra cycle to meet
 *             hold time requirements with ADV assertion.
 *
 * The manual mentions "write precharge cycles" and "precharge cycles".
 * We have not been able to figure out which bit fields these correspond to
 * in the hardware, or what valid values exist. The current hypothesis is that
 * this is something just used on the FAST chip selects. There is also a "byte
 * device enable" flag somewhere for 8bit memories.
 */
#define EBI2_XMEM_CS0_FAST_CFG 0x0028
#define EBI2_XMEM_CS1_FAST_CFG 0x002C
#define EBI2_XMEM_CS2_FAST_CFG 0x0030
#define EBI2_XMEM_CS3_FAST_CFG 0x0034
#define EBI2_XMEM_CS4_FAST_CFG 0x0038
#define EBI2_XMEM_CS5_FAST_CFG 0x003C

#define EBI2_XMEM_RD_HOLD_SHIFT		24
#define EBI2_XMEM_ADV_OE_RECOVERY_SHIFT	16
#define EBI2_XMEM_ADDR_HOLD_ENA_SHIFT	5

/**
 * struct cs_data - struct with info on a chipselect setting
 * @enable_mask: mask to enable the chipselect in the EBI2 config
 * @slow_cfg0: offset to XMEMC slow CS config
 * @fast_cfg1: offset to XMEMC fast CS config
 */
struct cs_data {
	u32 enable_mask;
	u16 slow_cfg;
	u16 fast_cfg;
};

static const struct cs_data cs_info[] = {
	{
		/* CS0 */
		.enable_mask = EBI2_CS0_ENABLE_MASK,
		.slow_cfg = EBI2_XMEM_CS0_SLOW_CFG,
		.fast_cfg = EBI2_XMEM_CS0_FAST_CFG,
	},
	{
		/* CS1 */
		.enable_mask = EBI2_CS1_ENABLE_MASK,
		.slow_cfg = EBI2_XMEM_CS1_SLOW_CFG,
		.fast_cfg = EBI2_XMEM_CS1_FAST_CFG,
	},
	{
		/* CS2 */
		.enable_mask = EBI2_CS2_ENABLE_MASK,
		.slow_cfg = EBI2_XMEM_CS2_SLOW_CFG,
		.fast_cfg = EBI2_XMEM_CS2_FAST_CFG,
	},
	{
		/* CS3 */
		.enable_mask = EBI2_CS3_ENABLE_MASK,
		.slow_cfg = EBI2_XMEM_CS3_SLOW_CFG,
		.fast_cfg = EBI2_XMEM_CS3_FAST_CFG,
	},
	{
		/* CS4 */
		.enable_mask = EBI2_CS4_ENABLE_MASK,
		.slow_cfg = EBI2_XMEM_CS4_SLOW_CFG,
		.fast_cfg = EBI2_XMEM_CS4_FAST_CFG,
	},
	{
		/* CS5 */
		.enable_mask = EBI2_CS5_ENABLE_MASK,
		.slow_cfg = EBI2_XMEM_CS5_SLOW_CFG,
		.fast_cfg = EBI2_XMEM_CS5_FAST_CFG,
	},
};

/**
 * struct ebi2_xmem_prop - describes an XMEM config property
 * @prop: the device tree binding name
 * @max: maximum value for the property
 * @slowreg: true if this property is in the SLOW CS config register
 * else it is assumed to be in the FAST config register
 * @shift: the bit field start in the SLOW or FAST register for this
 * property
 */
struct ebi2_xmem_prop {
	const char *prop;
	u32 max;
	bool slowreg;
	u16 shift;
};

static const struct ebi2_xmem_prop xmem_props[] = {
	{
		.prop = "qcom,xmem-recovery-cycles",
		.max = 15,
		.slowreg = true,
		.shift = EBI2_XMEM_RECOVERY_SHIFT,
	},
	{
		.prop = "qcom,xmem-write-hold-cycles",
		.max = 15,
		.slowreg = true,
		.shift = EBI2_XMEM_WR_HOLD_SHIFT,
	},
	{
		.prop = "qcom,xmem-write-delta-cycles",
		.max = 255,
		.slowreg = true,
		.shift = EBI2_XMEM_WR_DELTA_SHIFT,
	},
	{
		.prop = "qcom,xmem-read-delta-cycles",
		.max = 255,
		.slowreg = true,
		.shift = EBI2_XMEM_RD_DELTA_SHIFT,
	},
	{
		.prop = "qcom,xmem-write-wait-cycles",
		.max = 15,
		.slowreg = true,
		.shift = EBI2_XMEM_WR_WAIT_SHIFT,
	},
	{
		.prop = "qcom,xmem-read-wait-cycles",
		.max = 15,
		.slowreg = true,
		.shift = EBI2_XMEM_RD_WAIT_SHIFT,
	},
	{
		.prop = "qcom,xmem-address-hold-enable",
		.max = 1, /* boolean prop */
		.slowreg = false,
		.shift = EBI2_XMEM_ADDR_HOLD_ENA_SHIFT,
	},
	{
		.prop = "qcom,xmem-adv-to-oe-recovery-cycles",
		.max = 3,
		.slowreg = false,
		.shift = EBI2_XMEM_ADV_OE_RECOVERY_SHIFT,
	},
	{
		.prop = "qcom,xmem-read-hold-cycles",
		.max = 15,
		.slowreg = false,
		.shift = EBI2_XMEM_RD_HOLD_SHIFT,
	},
};

static void qcom_ebi2_setup_chipselect(struct device_node *np,
				       struct device *dev,
				       void __iomem *ebi2_base,
				       void __iomem *ebi2_xmem,
				       u32 csindex)
{
	const struct cs_data *csd;
	u32 slowcfg, fastcfg;
	u32 val;
	int ret;
	int i;

	csd = &cs_info[csindex];
	val = readl(ebi2_base);
	val |= csd->enable_mask;
	writel(val, ebi2_base);
	dev_dbg(dev, "enabled CS%u\n", csindex);

	/* Next set up the XMEMC */
	slowcfg = 0;
	fastcfg = 0;

	for (i = 0; i < ARRAY_SIZE(xmem_props); i++) {
		const struct ebi2_xmem_prop *xp = &xmem_props[i];

		/* All are regular u32 values */
		ret = of_property_read_u32(np, xp->prop, &val);
		if (ret) {
			dev_dbg(dev, "could not read %s for CS%d\n",
				xp->prop, csindex);
			continue;
		}

		/* First check boolean props */
		if (xp->max == 1 && val) {
			if (xp->slowreg)
				slowcfg |= BIT(xp->shift);
			else
				fastcfg |= BIT(xp->shift);
			dev_dbg(dev, "set %s flag\n", xp->prop);
			continue;
		}

		/* We're dealing with an u32 */
		if (val > xp->max) {
			dev_err(dev,
				"too high value for %s: %u, capped at %u\n",
				xp->prop, val, xp->max);
			val = xp->max;
		}
		if (xp->slowreg)
			slowcfg |= (val << xp->shift);
		else
			fastcfg |= (val << xp->shift);
		dev_dbg(dev, "set %s to %u\n", xp->prop, val);
	}

	dev_info(dev, "CS%u: SLOW CFG 0x%08x, FAST CFG 0x%08x\n",
		 csindex, slowcfg, fastcfg);

	if (slowcfg)
		writel(slowcfg, ebi2_xmem + csd->slow_cfg);
	if (fastcfg)
		writel(fastcfg, ebi2_xmem + csd->fast_cfg);
}

static int qcom_ebi2_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	struct device *dev = &pdev->dev;
	struct resource *res;
	void __iomem *ebi2_base;
	void __iomem *ebi2_xmem;
	struct clk *ebi2xclk;
	struct clk *ebi2clk;
	bool have_children = false;
	u32 val;
	int ret;

	ebi2xclk = devm_clk_get(dev, "ebi2x");
	if (IS_ERR(ebi2xclk))
		return PTR_ERR(ebi2xclk);

	ret = clk_prepare_enable(ebi2xclk);
	if (ret) {
		dev_err(dev, "could not enable EBI2X clk (%d)\n", ret);
		return ret;
	}

	ebi2clk = devm_clk_get(dev, "ebi2");
	if (IS_ERR(ebi2clk)) {
		ret = PTR_ERR(ebi2clk);
		goto err_disable_2x_clk;
	}

	ret = clk_prepare_enable(ebi2clk);
	if (ret) {
		dev_err(dev, "could not enable EBI2 clk\n");
		goto err_disable_2x_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ebi2_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(ebi2_base)) {
		ret = PTR_ERR(ebi2_base);
		goto err_disable_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ebi2_xmem = devm_ioremap_resource(dev, res);
	if (IS_ERR(ebi2_xmem)) {
		ret = PTR_ERR(ebi2_xmem);
		goto err_disable_clk;
	}

	/* Allegedly this turns the power save mode off */
	writel(0UL, ebi2_xmem + EBI2_XMEM_CFG);

	/* Disable all chipselects */
	val = readl(ebi2_base);
	val &= ~EBI2_CSN_MASK;
	writel(val, ebi2_base);

	/* Walk over the child nodes and see what chipselects we use */
	for_each_available_child_of_node(np, child) {
		u32 csindex;

		/* Figure out the chipselect */
		ret = of_property_read_u32(child, "reg", &csindex);
		if (ret) {
			of_node_put(child);
			return ret;
		}

		if (csindex > 5) {
			dev_err(dev,
				"invalid chipselect %u, we only support 0-5\n",
				csindex);
			continue;
		}

		qcom_ebi2_setup_chipselect(child,
					   dev,
					   ebi2_base,
					   ebi2_xmem,
					   csindex);

		/* We have at least one child */
		have_children = true;
	}

	if (have_children)
		return of_platform_default_populate(np, NULL, dev);
	return 0;

err_disable_clk:
	clk_disable_unprepare(ebi2clk);
err_disable_2x_clk:
	clk_disable_unprepare(ebi2xclk);

	return ret;
}

static const struct of_device_id qcom_ebi2_of_match[] = {
	{ .compatible = "qcom,msm8660-ebi2", },
	{ .compatible = "qcom,apq8060-ebi2", },
	{ }
};

static struct platform_driver qcom_ebi2_driver = {
	.probe = qcom_ebi2_probe,
	.driver = {
		.name = "qcom-ebi2",
		.of_match_table = qcom_ebi2_of_match,
	},
};
module_platform_driver(qcom_ebi2_driver);
MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Qualcomm EBI2 driver");
MODULE_LICENSE("GPL");

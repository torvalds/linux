// SPDX-License-Identifier: GPL-2.0
/*
 * STMicroelectronics STM32 USB PHY Controller driver
 *
 * Copyright (C) 2018 STMicroelectronics
 * Author(s): Amelie Delaunay <amelie.delaunay@st.com>.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>

#define STM32_USBPHYC_PLL	0x0
#define STM32_USBPHYC_MISC	0x8
#define STM32_USBPHYC_VERSION	0x3F4

/* STM32_USBPHYC_PLL bit fields */
#define PLLNDIV			GENMASK(6, 0)
#define PLLFRACIN		GENMASK(25, 10)
#define PLLEN			BIT(26)
#define PLLSTRB			BIT(27)
#define PLLSTRBYP		BIT(28)
#define PLLFRACCTL		BIT(29)
#define PLLDITHEN0		BIT(30)
#define PLLDITHEN1		BIT(31)

/* STM32_USBPHYC_MISC bit fields */
#define SWITHOST		BIT(0)

/* STM32_USBPHYC_VERSION bit fields */
#define MINREV			GENMASK(3, 0)
#define MAJREV			GENMASK(7, 4)

static const char * const supplies_names[] = {
	"vdda1v1",	/* 1V1 */
	"vdda1v8",	/* 1V8 */
};

#define NUM_SUPPLIES		ARRAY_SIZE(supplies_names)

#define PLL_LOCK_TIME_US	100
#define PLL_PWR_DOWN_TIME_US	5
#define PLL_FVCO_MHZ		2880
#define PLL_INFF_MIN_RATE_HZ	19200000
#define PLL_INFF_MAX_RATE_HZ	38400000
#define HZ_PER_MHZ		1000000L

struct pll_params {
	u8 ndiv;
	u16 frac;
};

struct stm32_usbphyc_phy {
	struct phy *phy;
	struct stm32_usbphyc *usbphyc;
	u32 index;
	bool active;
};

struct stm32_usbphyc {
	struct device *dev;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rst;
	struct stm32_usbphyc_phy **phys;
	int nphys;
	struct regulator_bulk_data supplies[NUM_SUPPLIES];
	int switch_setup;
};

static inline void stm32_usbphyc_set_bits(void __iomem *reg, u32 bits)
{
	writel_relaxed(readl_relaxed(reg) | bits, reg);
}

static inline void stm32_usbphyc_clr_bits(void __iomem *reg, u32 bits)
{
	writel_relaxed(readl_relaxed(reg) & ~bits, reg);
}

static void stm32_usbphyc_get_pll_params(u32 clk_rate,
					 struct pll_params *pll_params)
{
	unsigned long long fvco, ndiv, frac;

	/*    _
	 *   | FVCO = INFF*2*(NDIV + FRACT/2^16) when DITHER_DISABLE[1] = 1
	 *   | FVCO = 2880MHz
	 *  <
	 *   | NDIV = integer part of input bits to set the LDF
	 *   |_FRACT = fractional part of input bits to set the LDF
	 *  =>	PLLNDIV = integer part of (FVCO / (INFF*2))
	 *  =>	PLLFRACIN = fractional part of(FVCO / INFF*2) * 2^16
	 * <=>  PLLFRACIN = ((FVCO / (INFF*2)) - PLLNDIV) * 2^16
	 */
	fvco = (unsigned long long)PLL_FVCO_MHZ * HZ_PER_MHZ;

	ndiv = fvco;
	do_div(ndiv, (clk_rate * 2));
	pll_params->ndiv = (u8)ndiv;

	frac = fvco * (1 << 16);
	do_div(frac, (clk_rate * 2));
	frac = frac - (ndiv * (1 << 16));
	pll_params->frac = (u16)frac;
}

static int stm32_usbphyc_pll_init(struct stm32_usbphyc *usbphyc)
{
	struct pll_params pll_params;
	u32 clk_rate = clk_get_rate(usbphyc->clk);
	u32 ndiv, frac;
	u32 usbphyc_pll;

	if ((clk_rate < PLL_INFF_MIN_RATE_HZ) ||
	    (clk_rate > PLL_INFF_MAX_RATE_HZ)) {
		dev_err(usbphyc->dev, "input clk freq (%dHz) out of range\n",
			clk_rate);
		return -EINVAL;
	}

	stm32_usbphyc_get_pll_params(clk_rate, &pll_params);
	ndiv = FIELD_PREP(PLLNDIV, pll_params.ndiv);
	frac = FIELD_PREP(PLLFRACIN, pll_params.frac);

	usbphyc_pll = PLLDITHEN1 | PLLDITHEN0 | PLLSTRBYP | ndiv;

	if (pll_params.frac)
		usbphyc_pll |= PLLFRACCTL | frac;

	writel_relaxed(usbphyc_pll, usbphyc->base + STM32_USBPHYC_PLL);

	dev_dbg(usbphyc->dev, "input clk freq=%dHz, ndiv=%lu, frac=%lu\n",
		clk_rate, FIELD_GET(PLLNDIV, usbphyc_pll),
		FIELD_GET(PLLFRACIN, usbphyc_pll));

	return 0;
}

static bool stm32_usbphyc_has_one_phy_active(struct stm32_usbphyc *usbphyc)
{
	int i;

	for (i = 0; i < usbphyc->nphys; i++)
		if (usbphyc->phys[i]->active)
			return true;

	return false;
}

static int stm32_usbphyc_pll_disable(struct stm32_usbphyc *usbphyc)
{
	void __iomem *pll_reg = usbphyc->base + STM32_USBPHYC_PLL;

	/* Check if other phy port active */
	if (stm32_usbphyc_has_one_phy_active(usbphyc))
		return 0;

	stm32_usbphyc_clr_bits(pll_reg, PLLEN);
	/* Wait for minimum width of powerdown pulse (ENABLE = Low) */
	udelay(PLL_PWR_DOWN_TIME_US);

	if (readl_relaxed(pll_reg) & PLLEN) {
		dev_err(usbphyc->dev, "PLL not reset\n");
		return -EIO;
	}

	return regulator_bulk_disable(NUM_SUPPLIES, usbphyc->supplies);
}

static int stm32_usbphyc_pll_enable(struct stm32_usbphyc *usbphyc)
{
	void __iomem *pll_reg = usbphyc->base + STM32_USBPHYC_PLL;
	bool pllen = readl_relaxed(pll_reg) & PLLEN;
	int ret;

	/* Check if one phy port has already configured the pll */
	if (pllen && stm32_usbphyc_has_one_phy_active(usbphyc))
		return 0;

	if (pllen) {
		ret = stm32_usbphyc_pll_disable(usbphyc);
		if (ret)
			return ret;
	}

	ret = regulator_bulk_enable(NUM_SUPPLIES, usbphyc->supplies);
	if (ret)
		return ret;

	ret = stm32_usbphyc_pll_init(usbphyc);
	if (ret)
		goto reg_disable;

	stm32_usbphyc_set_bits(pll_reg, PLLEN);
	/* Wait for maximum lock time */
	udelay(PLL_LOCK_TIME_US);

	if (!(readl_relaxed(pll_reg) & PLLEN)) {
		dev_err(usbphyc->dev, "PLLEN not set\n");
		ret = -EIO;
		goto reg_disable;
	}

	return 0;

reg_disable:
	regulator_bulk_disable(NUM_SUPPLIES, usbphyc->supplies);

	return ret;
}

static int stm32_usbphyc_phy_init(struct phy *phy)
{
	struct stm32_usbphyc_phy *usbphyc_phy = phy_get_drvdata(phy);
	struct stm32_usbphyc *usbphyc = usbphyc_phy->usbphyc;
	int ret;

	ret = stm32_usbphyc_pll_enable(usbphyc);
	if (ret)
		return ret;

	usbphyc_phy->active = true;

	return 0;
}

static int stm32_usbphyc_phy_exit(struct phy *phy)
{
	struct stm32_usbphyc_phy *usbphyc_phy = phy_get_drvdata(phy);
	struct stm32_usbphyc *usbphyc = usbphyc_phy->usbphyc;

	usbphyc_phy->active = false;

	return stm32_usbphyc_pll_disable(usbphyc);
}

static const struct phy_ops stm32_usbphyc_phy_ops = {
	.init = stm32_usbphyc_phy_init,
	.exit = stm32_usbphyc_phy_exit,
	.owner = THIS_MODULE,
};

static void stm32_usbphyc_switch_setup(struct stm32_usbphyc *usbphyc,
				       u32 utmi_switch)
{
	if (!utmi_switch)
		stm32_usbphyc_clr_bits(usbphyc->base + STM32_USBPHYC_MISC,
				       SWITHOST);
	else
		stm32_usbphyc_set_bits(usbphyc->base + STM32_USBPHYC_MISC,
				       SWITHOST);
	usbphyc->switch_setup = utmi_switch;
}

static struct phy *stm32_usbphyc_of_xlate(struct device *dev,
					  struct of_phandle_args *args)
{
	struct stm32_usbphyc *usbphyc = dev_get_drvdata(dev);
	struct stm32_usbphyc_phy *usbphyc_phy = NULL;
	struct device_node *phynode = args->np;
	int port = 0;

	for (port = 0; port < usbphyc->nphys; port++) {
		if (phynode == usbphyc->phys[port]->phy->dev.of_node) {
			usbphyc_phy = usbphyc->phys[port];
			break;
		}
	}
	if (!usbphyc_phy) {
		dev_err(dev, "failed to find phy\n");
		return ERR_PTR(-EINVAL);
	}

	if (((usbphyc_phy->index == 0) && (args->args_count != 0)) ||
	    ((usbphyc_phy->index == 1) && (args->args_count != 1))) {
		dev_err(dev, "invalid number of cells for phy port%d\n",
			usbphyc_phy->index);
		return ERR_PTR(-EINVAL);
	}

	/* Configure the UTMI switch for PHY port#2 */
	if (usbphyc_phy->index == 1) {
		if (usbphyc->switch_setup < 0) {
			stm32_usbphyc_switch_setup(usbphyc, args->args[0]);
		} else {
			if (args->args[0] != usbphyc->switch_setup) {
				dev_err(dev, "phy port1 already used\n");
				return ERR_PTR(-EBUSY);
			}
		}
	}

	return usbphyc_phy->phy;
}

static int stm32_usbphyc_probe(struct platform_device *pdev)
{
	struct stm32_usbphyc *usbphyc;
	struct device *dev = &pdev->dev;
	struct device_node *child, *np = dev->of_node;
	struct phy_provider *phy_provider;
	u32 version;
	int ret, i, port = 0;

	usbphyc = devm_kzalloc(dev, sizeof(*usbphyc), GFP_KERNEL);
	if (!usbphyc)
		return -ENOMEM;
	usbphyc->dev = dev;
	dev_set_drvdata(dev, usbphyc);

	usbphyc->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(usbphyc->base))
		return PTR_ERR(usbphyc->base);

	usbphyc->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(usbphyc->clk))
		return dev_err_probe(dev, PTR_ERR(usbphyc->clk), "clk get_failed\n");

	ret = clk_prepare_enable(usbphyc->clk);
	if (ret) {
		dev_err(dev, "clk enable failed: %d\n", ret);
		return ret;
	}

	usbphyc->rst = devm_reset_control_get(dev, NULL);
	if (!IS_ERR(usbphyc->rst)) {
		reset_control_assert(usbphyc->rst);
		udelay(2);
		reset_control_deassert(usbphyc->rst);
	} else {
		ret = PTR_ERR(usbphyc->rst);
		if (ret == -EPROBE_DEFER)
			goto clk_disable;
	}

	usbphyc->switch_setup = -EINVAL;
	usbphyc->nphys = of_get_child_count(np);
	usbphyc->phys = devm_kcalloc(dev, usbphyc->nphys,
				     sizeof(*usbphyc->phys), GFP_KERNEL);
	if (!usbphyc->phys) {
		ret = -ENOMEM;
		goto clk_disable;
	}

	for (i = 0; i < NUM_SUPPLIES; i++)
		usbphyc->supplies[i].supply = supplies_names[i];

	ret = devm_regulator_bulk_get(dev, NUM_SUPPLIES, usbphyc->supplies);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get regulators: %d\n", ret);
		goto clk_disable;
	}

	for_each_child_of_node(np, child) {
		struct stm32_usbphyc_phy *usbphyc_phy;
		struct phy *phy;
		u32 index;

		phy = devm_phy_create(dev, child, &stm32_usbphyc_phy_ops);
		if (IS_ERR(phy)) {
			ret = PTR_ERR(phy);
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "failed to create phy%d: %d\n",
					port, ret);
			goto put_child;
		}

		usbphyc_phy = devm_kzalloc(dev, sizeof(*usbphyc_phy),
					   GFP_KERNEL);
		if (!usbphyc_phy) {
			ret = -ENOMEM;
			goto put_child;
		}

		ret = of_property_read_u32(child, "reg", &index);
		if (ret || index > usbphyc->nphys) {
			dev_err(&phy->dev, "invalid reg property: %d\n", ret);
			goto put_child;
		}

		usbphyc->phys[port] = usbphyc_phy;
		phy_set_bus_width(phy, 8);
		phy_set_drvdata(phy, usbphyc_phy);

		usbphyc->phys[port]->phy = phy;
		usbphyc->phys[port]->usbphyc = usbphyc;
		usbphyc->phys[port]->index = index;
		usbphyc->phys[port]->active = false;

		port++;
	}

	phy_provider = devm_of_phy_provider_register(dev,
						     stm32_usbphyc_of_xlate);
	if (IS_ERR(phy_provider)) {
		ret = PTR_ERR(phy_provider);
		dev_err(dev, "failed to register phy provider: %d\n", ret);
		goto clk_disable;
	}

	version = readl_relaxed(usbphyc->base + STM32_USBPHYC_VERSION);
	dev_info(dev, "registered rev:%lu.%lu\n",
		 FIELD_GET(MAJREV, version), FIELD_GET(MINREV, version));

	return 0;

put_child:
	of_node_put(child);
clk_disable:
	clk_disable_unprepare(usbphyc->clk);

	return ret;
}

static int stm32_usbphyc_remove(struct platform_device *pdev)
{
	struct stm32_usbphyc *usbphyc = dev_get_drvdata(&pdev->dev);

	clk_disable_unprepare(usbphyc->clk);

	return 0;
}

static const struct of_device_id stm32_usbphyc_of_match[] = {
	{ .compatible = "st,stm32mp1-usbphyc", },
	{ },
};
MODULE_DEVICE_TABLE(of, stm32_usbphyc_of_match);

static struct platform_driver stm32_usbphyc_driver = {
	.probe = stm32_usbphyc_probe,
	.remove = stm32_usbphyc_remove,
	.driver = {
		.of_match_table = stm32_usbphyc_of_match,
		.name = "stm32-usbphyc",
	}
};
module_platform_driver(stm32_usbphyc_driver);

MODULE_DESCRIPTION("STMicroelectronics STM32 USBPHYC driver");
MODULE_AUTHOR("Amelie Delaunay <amelie.delaunay@st.com>");
MODULE_LICENSE("GPL v2");

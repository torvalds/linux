// SPDX-License-Identifier: GPL-2.0
/*
 * Clock driver for TI Davinci PSC controllers
 *
 * Copyright (C) 2017 David Lechner <david@lechnology.com>
 *
 * Based on: drivers/clk/keystone/gate.c
 * Copyright (C) 2013 Texas Instruments.
 *	Murali Karicheri <m-karicheri2@ti.com>
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * And: arch/arm/mach-davinci/psc.c
 * Copyright (C) 2006 Texas Instruments.
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clk/davinci.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pm_clock.h>
#include <linux/pm_domain.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "psc.h"

/* PSC register offsets */
#define EPCPR			0x070
#define PTCMD			0x120
#define PTSTAT			0x128
#define PDSTAT(n)		(0x200 + 4 * (n))
#define PDCTL(n)		(0x300 + 4 * (n))
#define MDSTAT(n)		(0x800 + 4 * (n))
#define MDCTL(n)		(0xa00 + 4 * (n))

/* PSC module states */
enum davinci_lpsc_state {
	LPSC_STATE_SWRSTDISABLE	= 0,
	LPSC_STATE_SYNCRST	= 1,
	LPSC_STATE_DISABLE	= 2,
	LPSC_STATE_ENABLE	= 3,
};

#define MDSTAT_STATE_MASK	GENMASK(5, 0)
#define MDSTAT_MCKOUT		BIT(12)
#define PDSTAT_STATE_MASK	GENMASK(4, 0)
#define MDCTL_FORCE		BIT(31)
#define MDCTL_LRESET		BIT(8)
#define PDCTL_EPCGOOD		BIT(8)
#define PDCTL_NEXT		BIT(0)

struct davinci_psc_data {
	struct clk_onecell_data clk_data;
	struct genpd_onecell_data pm_data;
	struct reset_controller_dev rcdev;
};

/**
 * struct davinci_lpsc_clk - LPSC clock structure
 * @dev: the device that provides this LPSC or NULL
 * @hw: clk_hw for the LPSC
 * @pm_domain: power domain for the LPSC
 * @genpd_clk: clock reference owned by @pm_domain
 * @regmap: PSC MMIO region
 * @md: Module domain (LPSC module id)
 * @pd: Power domain
 * @flags: LPSC_* quirk flags
 */
struct davinci_lpsc_clk {
	struct device *dev;
	struct clk_hw hw;
	struct generic_pm_domain pm_domain;
	struct clk *genpd_clk;
	struct regmap *regmap;
	u32 md;
	u32 pd;
	u32 flags;
};

#define to_davinci_psc_data(x) container_of(x, struct davinci_psc_data, x)
#define to_davinci_lpsc_clk(x) container_of(x, struct davinci_lpsc_clk, x)

/**
 * best_dev_name - get the "best" device name.
 * @dev: the device
 *
 * Returns the device tree compatible name if the device has a DT node,
 * otherwise return the device name. This is mainly needed because clkdev
 * lookups are limited to 20 chars for dev_id and when using device tree,
 * dev_name(dev) is much longer than that.
 */
static inline const char *best_dev_name(struct device *dev)
{
	const char *compatible;

	if (!of_property_read_string(dev->of_node, "compatible", &compatible))
		return compatible;

	return dev_name(dev);
}

static void davinci_lpsc_config(struct davinci_lpsc_clk *lpsc,
				enum davinci_lpsc_state next_state)
{
	u32 epcpr, pdstat, mdstat, ptstat;

	regmap_write_bits(lpsc->regmap, MDCTL(lpsc->md), MDSTAT_STATE_MASK,
			  next_state);

	if (lpsc->flags & LPSC_FORCE)
		regmap_write_bits(lpsc->regmap, MDCTL(lpsc->md), MDCTL_FORCE,
				  MDCTL_FORCE);

	regmap_read(lpsc->regmap, PDSTAT(lpsc->pd), &pdstat);
	if ((pdstat & PDSTAT_STATE_MASK) == 0) {
		regmap_write_bits(lpsc->regmap, PDCTL(lpsc->pd), PDCTL_NEXT,
				  PDCTL_NEXT);

		regmap_write(lpsc->regmap, PTCMD, BIT(lpsc->pd));

		regmap_read_poll_timeout(lpsc->regmap, EPCPR, epcpr,
					 epcpr & BIT(lpsc->pd), 0, 0);

		regmap_write_bits(lpsc->regmap, PDCTL(lpsc->pd), PDCTL_EPCGOOD,
				  PDCTL_EPCGOOD);
	} else {
		regmap_write(lpsc->regmap, PTCMD, BIT(lpsc->pd));
	}

	regmap_read_poll_timeout(lpsc->regmap, PTSTAT, ptstat,
				 !(ptstat & BIT(lpsc->pd)), 0, 0);

	regmap_read_poll_timeout(lpsc->regmap, MDSTAT(lpsc->md), mdstat,
				 (mdstat & MDSTAT_STATE_MASK) == next_state,
				 0, 0);
}

static int davinci_lpsc_clk_enable(struct clk_hw *hw)
{
	struct davinci_lpsc_clk *lpsc = to_davinci_lpsc_clk(hw);

	davinci_lpsc_config(lpsc, LPSC_STATE_ENABLE);

	return 0;
}

static void davinci_lpsc_clk_disable(struct clk_hw *hw)
{
	struct davinci_lpsc_clk *lpsc = to_davinci_lpsc_clk(hw);

	davinci_lpsc_config(lpsc, LPSC_STATE_DISABLE);
}

static int davinci_lpsc_clk_is_enabled(struct clk_hw *hw)
{
	struct davinci_lpsc_clk *lpsc = to_davinci_lpsc_clk(hw);
	u32 mdstat;

	regmap_read(lpsc->regmap, MDSTAT(lpsc->md), &mdstat);

	return (mdstat & MDSTAT_MCKOUT) ? 1 : 0;
}

static const struct clk_ops davinci_lpsc_clk_ops = {
	.enable		= davinci_lpsc_clk_enable,
	.disable	= davinci_lpsc_clk_disable,
	.is_enabled	= davinci_lpsc_clk_is_enabled,
};

static int davinci_psc_genpd_attach_dev(struct generic_pm_domain *pm_domain,
					struct device *dev)
{
	struct davinci_lpsc_clk *lpsc = to_davinci_lpsc_clk(pm_domain);
	struct clk *clk;
	int ret;

	/*
	 * pm_clk_remove_clk() will call clk_put(), so we have to use clk_get()
	 * to get the clock instead of using lpsc->hw.clk directly.
	 */
	clk = clk_get_sys(best_dev_name(lpsc->dev), clk_hw_get_name(&lpsc->hw));
	if (IS_ERR(clk))
		return (PTR_ERR(clk));

	ret = pm_clk_create(dev);
	if (ret < 0)
		goto fail_clk_put;

	ret = pm_clk_add_clk(dev, clk);
	if (ret < 0)
		goto fail_pm_clk_destroy;

	lpsc->genpd_clk = clk;

	return 0;

fail_pm_clk_destroy:
	pm_clk_destroy(dev);
fail_clk_put:
	clk_put(clk);

	return ret;
}

static void davinci_psc_genpd_detach_dev(struct generic_pm_domain *pm_domain,
					 struct device *dev)
{
	struct davinci_lpsc_clk *lpsc = to_davinci_lpsc_clk(pm_domain);

	pm_clk_remove_clk(dev, lpsc->genpd_clk);
	pm_clk_destroy(dev);

	lpsc->genpd_clk = NULL;
}

/**
 * davinci_lpsc_clk_register - register LPSC clock
 * @dev: the clocks's device or NULL
 * @name: name of this clock
 * @parent_name: name of clock's parent
 * @regmap: PSC MMIO region
 * @md: local PSC number
 * @pd: power domain
 * @flags: LPSC_* flags
 */
static struct davinci_lpsc_clk *
davinci_lpsc_clk_register(struct device *dev, const char *name,
			  const char *parent_name, struct regmap *regmap,
			  u32 md, u32 pd, u32 flags)
{
	struct clk_init_data init;
	struct davinci_lpsc_clk *lpsc;
	int ret;
	bool is_on;

	lpsc = kzalloc(sizeof(*lpsc), GFP_KERNEL);
	if (!lpsc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &davinci_lpsc_clk_ops;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	init.flags = 0;

	if (flags & LPSC_ALWAYS_ENABLED)
		init.flags |= CLK_IS_CRITICAL;

	if (flags & LPSC_SET_RATE_PARENT)
		init.flags |= CLK_SET_RATE_PARENT;

	lpsc->dev = dev;
	lpsc->regmap = regmap;
	lpsc->hw.init = &init;
	lpsc->md = md;
	lpsc->pd = pd;
	lpsc->flags = flags;

	ret = clk_hw_register(dev, &lpsc->hw);
	if (ret < 0) {
		kfree(lpsc);
		return ERR_PTR(ret);
	}

	/* for now, genpd is only registered when using device-tree */
	if (!dev || !dev->of_node)
		return lpsc;

	/* genpd attach needs a way to look up this clock */
	ret = clk_hw_register_clkdev(&lpsc->hw, name, best_dev_name(dev));

	lpsc->pm_domain.name = devm_kasprintf(dev, GFP_KERNEL, "%s: %s",
					      best_dev_name(dev), name);
	if (!lpsc->pm_domain.name) {
		clk_hw_unregister(&lpsc->hw);
		kfree(lpsc);
		return ERR_PTR(-ENOMEM);
	}
	lpsc->pm_domain.attach_dev = davinci_psc_genpd_attach_dev;
	lpsc->pm_domain.detach_dev = davinci_psc_genpd_detach_dev;
	lpsc->pm_domain.flags = GENPD_FLAG_PM_CLK;

	is_on = davinci_lpsc_clk_is_enabled(&lpsc->hw);
	pm_genpd_init(&lpsc->pm_domain, NULL, is_on);

	return lpsc;
}

static int davinci_lpsc_clk_reset(struct clk *clk, bool reset)
{
	struct clk_hw *hw = __clk_get_hw(clk);
	struct davinci_lpsc_clk *lpsc = to_davinci_lpsc_clk(hw);
	u32 mdctl;

	if (IS_ERR_OR_NULL(lpsc))
		return -EINVAL;

	mdctl = reset ? 0 : MDCTL_LRESET;
	regmap_write_bits(lpsc->regmap, MDCTL(lpsc->md), MDCTL_LRESET, mdctl);

	return 0;
}

static int davinci_psc_reset_assert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct davinci_psc_data *psc = to_davinci_psc_data(rcdev);
	struct clk *clk = psc->clk_data.clks[id];

	return davinci_lpsc_clk_reset(clk, true);
}

static int davinci_psc_reset_deassert(struct reset_controller_dev *rcdev,
				      unsigned long id)
{
	struct davinci_psc_data *psc = to_davinci_psc_data(rcdev);
	struct clk *clk = psc->clk_data.clks[id];

	return davinci_lpsc_clk_reset(clk, false);
}

static const struct reset_control_ops davinci_psc_reset_ops = {
	.assert		= davinci_psc_reset_assert,
	.deassert	= davinci_psc_reset_deassert,
};

static int davinci_psc_reset_of_xlate(struct reset_controller_dev *rcdev,
				      const struct of_phandle_args *reset_spec)
{
	struct of_phandle_args clkspec = *reset_spec; /* discard const qualifier */
	struct clk *clk;
	struct clk_hw *hw;
	struct davinci_lpsc_clk *lpsc;

	/* the clock node is the same as the reset node */
	clk = of_clk_get_from_provider(&clkspec);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	hw = __clk_get_hw(clk);
	lpsc = to_davinci_lpsc_clk(hw);
	clk_put(clk);

	/* not all modules support local reset */
	if (!(lpsc->flags & LPSC_LOCAL_RESET))
		return -EINVAL;

	return lpsc->md;
}

static const struct regmap_config davinci_psc_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
};

static struct davinci_psc_data *
__davinci_psc_register_clocks(struct device *dev,
			      const struct davinci_lpsc_clk_info *info,
			      int num_clks,
			      void __iomem *base)
{
	struct davinci_psc_data *psc;
	struct clk **clks;
	struct generic_pm_domain **pm_domains;
	struct regmap *regmap;
	int i, ret;

	psc = kzalloc(sizeof(*psc), GFP_KERNEL);
	if (!psc)
		return ERR_PTR(-ENOMEM);

	clks = kmalloc_array(num_clks, sizeof(*clks), GFP_KERNEL);
	if (!clks) {
		ret = -ENOMEM;
		goto err_free_psc;
	}

	psc->clk_data.clks = clks;
	psc->clk_data.clk_num = num_clks;

	/*
	 * init array with error so that of_clk_src_onecell_get() doesn't
	 * return NULL for gaps in the sparse array
	 */
	for (i = 0; i < num_clks; i++)
		clks[i] = ERR_PTR(-ENOENT);

	pm_domains = kcalloc(num_clks, sizeof(*pm_domains), GFP_KERNEL);
	if (!pm_domains) {
		ret = -ENOMEM;
		goto err_free_clks;
	}

	psc->pm_data.domains = pm_domains;
	psc->pm_data.num_domains = num_clks;

	regmap = regmap_init_mmio(dev, base, &davinci_psc_regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto err_free_pm_domains;
	}

	for (; info->name; info++) {
		struct davinci_lpsc_clk *lpsc;

		lpsc = davinci_lpsc_clk_register(dev, info->name, info->parent,
						 regmap, info->md, info->pd,
						 info->flags);
		if (IS_ERR(lpsc)) {
			dev_warn(dev, "Failed to register %s (%ld)\n",
				 info->name, PTR_ERR(lpsc));
			continue;
		}

		clks[info->md] = lpsc->hw.clk;
		pm_domains[info->md] = &lpsc->pm_domain;
	}

	/*
	 * for now, a reset controller is only registered when there is a device
	 * to associate it with.
	 */
	if (!dev)
		return psc;

	psc->rcdev.ops = &davinci_psc_reset_ops;
	psc->rcdev.owner = THIS_MODULE;
	psc->rcdev.dev = dev;
	psc->rcdev.of_node = dev->of_node;
	psc->rcdev.of_reset_n_cells = 1;
	psc->rcdev.of_xlate = davinci_psc_reset_of_xlate;
	psc->rcdev.nr_resets = num_clks;

	ret = devm_reset_controller_register(dev, &psc->rcdev);
	if (ret < 0)
		dev_warn(dev, "Failed to register reset controller (%d)\n", ret);

	return psc;

err_free_pm_domains:
	kfree(pm_domains);
err_free_clks:
	kfree(clks);
err_free_psc:
	kfree(psc);

	return ERR_PTR(ret);
}

int davinci_psc_register_clocks(struct device *dev,
				const struct davinci_lpsc_clk_info *info,
				u8 num_clks,
				void __iomem *base)
{
	struct davinci_psc_data *psc;

	psc = __davinci_psc_register_clocks(dev, info, num_clks, base);
	if (IS_ERR(psc))
		return PTR_ERR(psc);

	for (; info->name; info++) {
		const struct davinci_lpsc_clkdev_info *cdevs = info->cdevs;
		struct clk *clk = psc->clk_data.clks[info->md];

		if (!cdevs || IS_ERR_OR_NULL(clk))
			continue;

		for (; cdevs->con_id || cdevs->dev_id; cdevs++)
			clk_register_clkdev(clk, cdevs->con_id, cdevs->dev_id);
	}

	return 0;
}

int of_davinci_psc_clk_init(struct device *dev,
			    const struct davinci_lpsc_clk_info *info,
			    u8 num_clks,
			    void __iomem *base)
{
	struct device_node *node = dev->of_node;
	struct davinci_psc_data *psc;

	psc = __davinci_psc_register_clocks(dev, info, num_clks, base);
	if (IS_ERR(psc))
		return PTR_ERR(psc);

	of_genpd_add_provider_onecell(node, &psc->pm_data);

	of_clk_add_provider(node, of_clk_src_onecell_get, &psc->clk_data);

	return 0;
}

static const struct of_device_id davinci_psc_of_match[] = {
	{ .compatible = "ti,da850-psc0", .data = &of_da850_psc0_init_data },
	{ .compatible = "ti,da850-psc1", .data = &of_da850_psc1_init_data },
	{ }
};

static const struct platform_device_id davinci_psc_id_table[] = {
	{ .name = "da850-psc0", .driver_data = (kernel_ulong_t)&da850_psc0_init_data },
	{ .name = "da850-psc1", .driver_data = (kernel_ulong_t)&da850_psc1_init_data },
	{ }
};

static int davinci_psc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct davinci_psc_init_data *init_data = NULL;
	void __iomem *base;
	int ret;

	init_data = device_get_match_data(dev);
	if (!init_data && pdev->id_entry)
		init_data = (void *)pdev->id_entry->driver_data;

	if (!init_data) {
		dev_err(dev, "unable to find driver init data\n");
		return -EINVAL;
	}

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	ret = devm_clk_bulk_get(dev, init_data->num_parent_clks,
				init_data->parent_clks);
	if (ret < 0)
		return ret;

	return init_data->psc_init(dev, base);
}

static struct platform_driver davinci_psc_driver = {
	.probe		= davinci_psc_probe,
	.driver		= {
		.name		= "davinci-psc-clk",
		.of_match_table	= davinci_psc_of_match,
	},
	.id_table	= davinci_psc_id_table,
};

static int __init davinci_psc_driver_init(void)
{
	return platform_driver_register(&davinci_psc_driver);
}

/* has to be postcore_initcall because davinci_gpio depend on PSC clocks */
postcore_initcall(davinci_psc_driver_init);

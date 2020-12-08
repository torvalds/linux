// SPDX-License-Identifier: GPL-2.0
/*
 * OMAP2+ PRM driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated - http://www.ti.com/
 *	Tero Kristo <t-kristo@ti.com>
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/reset-controller.h>
#include <linux/delay.h>

#include <linux/platform_data/ti-prm.h>

enum omap_prm_domain_mode {
	OMAP_PRMD_OFF,
	OMAP_PRMD_RETENTION,
	OMAP_PRMD_ON_INACTIVE,
	OMAP_PRMD_ON_ACTIVE,
};

struct omap_prm_domain_map {
	unsigned int usable_modes;	/* Mask of hardware supported modes */
	unsigned long statechange:1;	/* Optional low-power state change */
	unsigned long logicretstate:1;	/* Optional logic off mode */
};

struct omap_prm_domain {
	struct device *dev;
	struct omap_prm *prm;
	struct generic_pm_domain pd;
	u16 pwrstctrl;
	u16 pwrstst;
	const struct omap_prm_domain_map *cap;
	u32 pwrstctrl_saved;
};

struct omap_rst_map {
	s8 rst;
	s8 st;
};

struct omap_prm_data {
	u32 base;
	const char *name;
	const char *clkdm_name;
	u16 pwrstctrl;
	u16 pwrstst;
	const struct omap_prm_domain_map *dmap;
	u16 rstctrl;
	u16 rstst;
	const struct omap_rst_map *rstmap;
	u8 flags;
};

struct omap_prm {
	const struct omap_prm_data *data;
	void __iomem *base;
	struct omap_prm_domain *prmd;
};

struct omap_reset_data {
	struct reset_controller_dev rcdev;
	struct omap_prm *prm;
	u32 mask;
	spinlock_t lock;
	struct clockdomain *clkdm;
	struct device *dev;
};

#define genpd_to_prm_domain(gpd) container_of(gpd, struct omap_prm_domain, pd)
#define to_omap_reset_data(p) container_of((p), struct omap_reset_data, rcdev)

#define OMAP_MAX_RESETS		8
#define OMAP_RESET_MAX_WAIT	10000

#define OMAP_PRM_HAS_RSTCTRL	BIT(0)
#define OMAP_PRM_HAS_RSTST	BIT(1)
#define OMAP_PRM_HAS_NO_CLKDM	BIT(2)

#define OMAP_PRM_HAS_RESETS	(OMAP_PRM_HAS_RSTCTRL | OMAP_PRM_HAS_RSTST)

#define PRM_STATE_MAX_WAIT	10000
#define PRM_LOGICRETSTATE	BIT(2)
#define PRM_LOWPOWERSTATECHANGE	BIT(4)
#define PRM_POWERSTATE_MASK	OMAP_PRMD_ON_ACTIVE

#define PRM_ST_INTRANSITION	BIT(20)

static const struct omap_prm_domain_map omap_prm_all = {
	.usable_modes = BIT(OMAP_PRMD_ON_ACTIVE) | BIT(OMAP_PRMD_ON_INACTIVE) |
			BIT(OMAP_PRMD_RETENTION) | BIT(OMAP_PRMD_OFF),
	.statechange = 1,
	.logicretstate = 1,
};

static const struct omap_prm_domain_map omap_prm_noinact = {
	.usable_modes = BIT(OMAP_PRMD_ON_ACTIVE) | BIT(OMAP_PRMD_RETENTION) |
			BIT(OMAP_PRMD_OFF),
	.statechange = 1,
	.logicretstate = 1,
};

static const struct omap_prm_domain_map omap_prm_nooff = {
	.usable_modes = BIT(OMAP_PRMD_ON_ACTIVE) | BIT(OMAP_PRMD_ON_INACTIVE) |
			BIT(OMAP_PRMD_RETENTION),
	.statechange = 1,
	.logicretstate = 1,
};

static const struct omap_prm_domain_map omap_prm_onoff_noauto = {
	.usable_modes = BIT(OMAP_PRMD_ON_ACTIVE) | BIT(OMAP_PRMD_OFF),
	.statechange = 1,
};

static const struct omap_rst_map rst_map_0[] = {
	{ .rst = 0, .st = 0 },
	{ .rst = -1 },
};

static const struct omap_rst_map rst_map_01[] = {
	{ .rst = 0, .st = 0 },
	{ .rst = 1, .st = 1 },
	{ .rst = -1 },
};

static const struct omap_rst_map rst_map_012[] = {
	{ .rst = 0, .st = 0 },
	{ .rst = 1, .st = 1 },
	{ .rst = 2, .st = 2 },
	{ .rst = -1 },
};

static const struct omap_prm_data omap4_prm_data[] = {
	{ .name = "tesla", .base = 0x4a306400, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{
		.name = "abe", .base = 0x4a306500,
		.pwrstctrl = 0, .pwrstst = 0x4, .dmap = &omap_prm_all,
	},
	{ .name = "core", .base = 0x4a306700, .rstctrl = 0x210, .rstst = 0x214, .clkdm_name = "ducati", .rstmap = rst_map_012 },
	{ .name = "ivahd", .base = 0x4a306f00, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_012 },
	{ .name = "device", .base = 0x4a307b00, .rstctrl = 0x0, .rstst = 0x4, .rstmap = rst_map_01, .flags = OMAP_PRM_HAS_RSTCTRL | OMAP_PRM_HAS_NO_CLKDM },
	{ },
};

static const struct omap_prm_data omap5_prm_data[] = {
	{ .name = "dsp", .base = 0x4ae06400, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{
		.name = "abe", .base = 0x4ae06500,
		.pwrstctrl = 0, .pwrstst = 0x4, .dmap = &omap_prm_nooff,
	},
	{ .name = "core", .base = 0x4ae06700, .rstctrl = 0x210, .rstst = 0x214, .clkdm_name = "ipu", .rstmap = rst_map_012 },
	{ .name = "iva", .base = 0x4ae07200, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_012 },
	{ .name = "device", .base = 0x4ae07c00, .rstctrl = 0x0, .rstst = 0x4, .rstmap = rst_map_01, .flags = OMAP_PRM_HAS_RSTCTRL | OMAP_PRM_HAS_NO_CLKDM },
	{ },
};

static const struct omap_prm_data dra7_prm_data[] = {
	{ .name = "dsp1", .base = 0x4ae06400, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{ .name = "ipu", .base = 0x4ae06500, .rstctrl = 0x10, .rstst = 0x14, .clkdm_name = "ipu1", .rstmap = rst_map_012 },
	{ .name = "core", .base = 0x4ae06700, .rstctrl = 0x210, .rstst = 0x214, .clkdm_name = "ipu2", .rstmap = rst_map_012 },
	{ .name = "iva", .base = 0x4ae06f00, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_012 },
	{ .name = "dsp2", .base = 0x4ae07b00, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{ .name = "eve1", .base = 0x4ae07b40, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{ .name = "eve2", .base = 0x4ae07b80, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{ .name = "eve3", .base = 0x4ae07bc0, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{ .name = "eve4", .base = 0x4ae07c00, .rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_01 },
	{ },
};

static const struct omap_rst_map am3_per_rst_map[] = {
	{ .rst = 1 },
	{ .rst = -1 },
};

static const struct omap_rst_map am3_wkup_rst_map[] = {
	{ .rst = 3, .st = 5 },
	{ .rst = -1 },
};

static const struct omap_prm_data am3_prm_data[] = {
	{ .name = "per", .base = 0x44e00c00, .rstctrl = 0x0, .rstmap = am3_per_rst_map, .flags = OMAP_PRM_HAS_RSTCTRL, .clkdm_name = "pruss_ocp" },
	{ .name = "wkup", .base = 0x44e00d00, .rstctrl = 0x0, .rstst = 0xc, .rstmap = am3_wkup_rst_map, .flags = OMAP_PRM_HAS_RSTCTRL | OMAP_PRM_HAS_NO_CLKDM },
	{ .name = "device", .base = 0x44e00f00, .rstctrl = 0x0, .rstst = 0x8, .rstmap = rst_map_01, .flags = OMAP_PRM_HAS_RSTCTRL | OMAP_PRM_HAS_NO_CLKDM },
	{
		.name = "gfx", .base = 0x44e01100,
		.pwrstctrl = 0, .pwrstst = 0x10, .dmap = &omap_prm_noinact,
		.rstctrl = 0x4, .rstst = 0x14, .rstmap = rst_map_0, .clkdm_name = "gfx_l3",
	},
	{ },
};

static const struct omap_rst_map am4_per_rst_map[] = {
	{ .rst = 1, .st = 0 },
	{ .rst = -1 },
};

static const struct omap_rst_map am4_device_rst_map[] = {
	{ .rst = 0, .st = 1 },
	{ .rst = 1, .st = 0 },
	{ .rst = -1 },
};

static const struct omap_prm_data am4_prm_data[] = {
	{
		.name = "gfx", .base = 0x44df0400,
		.pwrstctrl = 0, .pwrstst = 0x4, .dmap = &omap_prm_onoff_noauto,
		.rstctrl = 0x10, .rstst = 0x14, .rstmap = rst_map_0, .clkdm_name = "gfx_l3",
	},
	{ .name = "per", .base = 0x44df0800, .rstctrl = 0x10, .rstst = 0x14, .rstmap = am4_per_rst_map, .clkdm_name = "pruss_ocp" },
	{ .name = "wkup", .base = 0x44df2000, .rstctrl = 0x10, .rstst = 0x14, .rstmap = am3_wkup_rst_map, .flags = OMAP_PRM_HAS_NO_CLKDM },
	{ .name = "device", .base = 0x44df4000, .rstctrl = 0x0, .rstst = 0x4, .rstmap = am4_device_rst_map, .flags = OMAP_PRM_HAS_RSTCTRL | OMAP_PRM_HAS_NO_CLKDM },
	{ },
};

static const struct of_device_id omap_prm_id_table[] = {
	{ .compatible = "ti,omap4-prm-inst", .data = omap4_prm_data },
	{ .compatible = "ti,omap5-prm-inst", .data = omap5_prm_data },
	{ .compatible = "ti,dra7-prm-inst", .data = dra7_prm_data },
	{ .compatible = "ti,am3-prm-inst", .data = am3_prm_data },
	{ .compatible = "ti,am4-prm-inst", .data = am4_prm_data },
	{ },
};

#ifdef DEBUG
static void omap_prm_domain_show_state(struct omap_prm_domain *prmd,
				       const char *desc)
{
	dev_dbg(prmd->dev, "%s %s: %08x/%08x\n",
		prmd->pd.name, desc,
		readl_relaxed(prmd->prm->base + prmd->pwrstctrl),
		readl_relaxed(prmd->prm->base + prmd->pwrstst));
}
#else
static inline void omap_prm_domain_show_state(struct omap_prm_domain *prmd,
					      const char *desc)
{
}
#endif

static int omap_prm_domain_power_on(struct generic_pm_domain *domain)
{
	struct omap_prm_domain *prmd;
	int ret;
	u32 v;

	prmd = genpd_to_prm_domain(domain);
	if (!prmd->cap)
		return 0;

	omap_prm_domain_show_state(prmd, "on: previous state");

	if (prmd->pwrstctrl_saved)
		v = prmd->pwrstctrl_saved;
	else
		v = readl_relaxed(prmd->prm->base + prmd->pwrstctrl);

	writel_relaxed(v | OMAP_PRMD_ON_ACTIVE,
		       prmd->prm->base + prmd->pwrstctrl);

	/* wait for the transition bit to get cleared */
	ret = readl_relaxed_poll_timeout(prmd->prm->base + prmd->pwrstst,
					 v, !(v & PRM_ST_INTRANSITION), 1,
					 PRM_STATE_MAX_WAIT);
	if (ret)
		dev_err(prmd->dev, "%s: %s timed out\n",
			prmd->pd.name, __func__);

	omap_prm_domain_show_state(prmd, "on: new state");

	return ret;
}

/* No need to check for holes in the mask for the lowest mode */
static int omap_prm_domain_find_lowest(struct omap_prm_domain *prmd)
{
	return __ffs(prmd->cap->usable_modes);
}

static int omap_prm_domain_power_off(struct generic_pm_domain *domain)
{
	struct omap_prm_domain *prmd;
	int ret;
	u32 v;

	prmd = genpd_to_prm_domain(domain);
	if (!prmd->cap)
		return 0;

	omap_prm_domain_show_state(prmd, "off: previous state");

	v = readl_relaxed(prmd->prm->base + prmd->pwrstctrl);
	prmd->pwrstctrl_saved = v;

	v &= ~PRM_POWERSTATE_MASK;
	v |= omap_prm_domain_find_lowest(prmd);

	if (prmd->cap->statechange)
		v |= PRM_LOWPOWERSTATECHANGE;
	if (prmd->cap->logicretstate)
		v &= ~PRM_LOGICRETSTATE;
	else
		v |= PRM_LOGICRETSTATE;

	writel_relaxed(v, prmd->prm->base + prmd->pwrstctrl);

	/* wait for the transition bit to get cleared */
	ret = readl_relaxed_poll_timeout(prmd->prm->base + prmd->pwrstst,
					 v, !(v & PRM_ST_INTRANSITION), 1,
					 PRM_STATE_MAX_WAIT);
	if (ret)
		dev_warn(prmd->dev, "%s: %s timed out\n",
			 __func__, prmd->pd.name);

	omap_prm_domain_show_state(prmd, "off: new state");

	return 0;
}

static int omap_prm_domain_attach_dev(struct generic_pm_domain *domain,
				      struct device *dev)
{
	struct generic_pm_domain_data *genpd_data;
	struct of_phandle_args pd_args;
	struct omap_prm_domain *prmd;
	struct device_node *np;
	int ret;

	prmd = genpd_to_prm_domain(domain);
	np = dev->of_node;

	ret = of_parse_phandle_with_args(np, "power-domains",
					 "#power-domain-cells", 0, &pd_args);
	if (ret < 0)
		return ret;

	if (pd_args.args_count != 0)
		dev_warn(dev, "%s: unusupported #power-domain-cells: %i\n",
			 prmd->pd.name, pd_args.args_count);

	genpd_data = dev_gpd_data(dev);
	genpd_data->data = NULL;

	return 0;
}

static void omap_prm_domain_detach_dev(struct generic_pm_domain *domain,
				       struct device *dev)
{
	struct generic_pm_domain_data *genpd_data;

	genpd_data = dev_gpd_data(dev);
	genpd_data->data = NULL;
}

static int omap_prm_domain_init(struct device *dev, struct omap_prm *prm)
{
	struct omap_prm_domain *prmd;
	struct device_node *np = dev->of_node;
	const struct omap_prm_data *data;
	const char *name;
	int error;

	if (!of_find_property(dev->of_node, "#power-domain-cells", NULL))
		return 0;

	of_node_put(dev->of_node);

	prmd = devm_kzalloc(dev, sizeof(*prmd), GFP_KERNEL);
	if (!prmd)
		return -ENOMEM;

	data = prm->data;
	name = devm_kasprintf(dev, GFP_KERNEL, "prm_%s",
			      data->name);

	prmd->dev = dev;
	prmd->prm = prm;
	prmd->cap = prmd->prm->data->dmap;
	prmd->pwrstctrl = prmd->prm->data->pwrstctrl;
	prmd->pwrstst = prmd->prm->data->pwrstst;

	prmd->pd.name = name;
	prmd->pd.power_on = omap_prm_domain_power_on;
	prmd->pd.power_off = omap_prm_domain_power_off;
	prmd->pd.attach_dev = omap_prm_domain_attach_dev;
	prmd->pd.detach_dev = omap_prm_domain_detach_dev;

	pm_genpd_init(&prmd->pd, NULL, true);
	error = of_genpd_add_provider_simple(np, &prmd->pd);
	if (error)
		pm_genpd_remove(&prmd->pd);
	else
		prm->prmd = prmd;

	return error;
}

static bool _is_valid_reset(struct omap_reset_data *reset, unsigned long id)
{
	if (reset->mask & BIT(id))
		return true;

	return false;
}

static int omap_reset_get_st_bit(struct omap_reset_data *reset,
				 unsigned long id)
{
	const struct omap_rst_map *map = reset->prm->data->rstmap;

	while (map->rst >= 0) {
		if (map->rst == id)
			return map->st;

		map++;
	}

	return id;
}

static int omap_reset_status(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct omap_reset_data *reset = to_omap_reset_data(rcdev);
	u32 v;
	int st_bit = omap_reset_get_st_bit(reset, id);
	bool has_rstst = reset->prm->data->rstst ||
		(reset->prm->data->flags & OMAP_PRM_HAS_RSTST);

	/* Check if we have rstst */
	if (!has_rstst)
		return -ENOTSUPP;

	/* Check if hw reset line is asserted */
	v = readl_relaxed(reset->prm->base + reset->prm->data->rstctrl);
	if (v & BIT(id))
		return 1;

	/*
	 * Check reset status, high value means reset sequence has been
	 * completed successfully so we can return 0 here (reset deasserted)
	 */
	v = readl_relaxed(reset->prm->base + reset->prm->data->rstst);
	v >>= st_bit;
	v &= 1;

	return !v;
}

static int omap_reset_assert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct omap_reset_data *reset = to_omap_reset_data(rcdev);
	u32 v;
	unsigned long flags;

	/* assert the reset control line */
	spin_lock_irqsave(&reset->lock, flags);
	v = readl_relaxed(reset->prm->base + reset->prm->data->rstctrl);
	v |= 1 << id;
	writel_relaxed(v, reset->prm->base + reset->prm->data->rstctrl);
	spin_unlock_irqrestore(&reset->lock, flags);

	return 0;
}

static int omap_reset_deassert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct omap_reset_data *reset = to_omap_reset_data(rcdev);
	u32 v;
	int st_bit;
	bool has_rstst;
	unsigned long flags;
	struct ti_prm_platform_data *pdata = dev_get_platdata(reset->dev);
	int ret = 0;

	/* Nothing to do if the reset is already deasserted */
	if (!omap_reset_status(rcdev, id))
		return 0;

	has_rstst = reset->prm->data->rstst ||
		(reset->prm->data->flags & OMAP_PRM_HAS_RSTST);

	if (has_rstst) {
		st_bit = omap_reset_get_st_bit(reset, id);

		/* Clear the reset status by writing 1 to the status bit */
		v = 1 << st_bit;
		writel_relaxed(v, reset->prm->base + reset->prm->data->rstst);
	}

	if (reset->clkdm)
		pdata->clkdm_deny_idle(reset->clkdm);

	/* de-assert the reset control line */
	spin_lock_irqsave(&reset->lock, flags);
	v = readl_relaxed(reset->prm->base + reset->prm->data->rstctrl);
	v &= ~(1 << id);
	writel_relaxed(v, reset->prm->base + reset->prm->data->rstctrl);
	spin_unlock_irqrestore(&reset->lock, flags);

	if (!has_rstst)
		goto exit;

	/* wait for the status to be set */
	ret = readl_relaxed_poll_timeout_atomic(reset->prm->base +
						 reset->prm->data->rstst,
						 v, v & BIT(st_bit), 1,
						 OMAP_RESET_MAX_WAIT);
	if (ret)
		pr_err("%s: timedout waiting for %s:%lu\n", __func__,
		       reset->prm->data->name, id);

exit:
	if (reset->clkdm)
		pdata->clkdm_allow_idle(reset->clkdm);

	return ret;
}

static const struct reset_control_ops omap_reset_ops = {
	.assert		= omap_reset_assert,
	.deassert	= omap_reset_deassert,
	.status		= omap_reset_status,
};

static int omap_prm_reset_xlate(struct reset_controller_dev *rcdev,
				const struct of_phandle_args *reset_spec)
{
	struct omap_reset_data *reset = to_omap_reset_data(rcdev);

	if (!_is_valid_reset(reset, reset_spec->args[0]))
		return -EINVAL;

	return reset_spec->args[0];
}

static int omap_prm_reset_init(struct platform_device *pdev,
			       struct omap_prm *prm)
{
	struct omap_reset_data *reset;
	const struct omap_rst_map *map;
	struct ti_prm_platform_data *pdata = dev_get_platdata(&pdev->dev);
	char buf[32];
	u32 v;

	/*
	 * Check if we have controllable resets. If either rstctrl is non-zero
	 * or OMAP_PRM_HAS_RSTCTRL flag is set, we have reset control register
	 * for the domain.
	 */
	if (!prm->data->rstctrl && !(prm->data->flags & OMAP_PRM_HAS_RSTCTRL))
		return 0;

	/* Check if we have the pdata callbacks in place */
	if (!pdata || !pdata->clkdm_lookup || !pdata->clkdm_deny_idle ||
	    !pdata->clkdm_allow_idle)
		return -EINVAL;

	map = prm->data->rstmap;
	if (!map)
		return -EINVAL;

	reset = devm_kzalloc(&pdev->dev, sizeof(*reset), GFP_KERNEL);
	if (!reset)
		return -ENOMEM;

	reset->rcdev.owner = THIS_MODULE;
	reset->rcdev.ops = &omap_reset_ops;
	reset->rcdev.of_node = pdev->dev.of_node;
	reset->rcdev.nr_resets = OMAP_MAX_RESETS;
	reset->rcdev.of_xlate = omap_prm_reset_xlate;
	reset->rcdev.of_reset_n_cells = 1;
	reset->dev = &pdev->dev;
	spin_lock_init(&reset->lock);

	reset->prm = prm;

	sprintf(buf, "%s_clkdm", prm->data->clkdm_name ? prm->data->clkdm_name :
		prm->data->name);

	if (!(prm->data->flags & OMAP_PRM_HAS_NO_CLKDM)) {
		reset->clkdm = pdata->clkdm_lookup(buf);
		if (!reset->clkdm)
			return -EINVAL;
	}

	while (map->rst >= 0) {
		reset->mask |= BIT(map->rst);
		map++;
	}

	/* Quirk handling to assert rst_map_012 bits on reset and avoid errors */
	if (prm->data->rstmap == rst_map_012) {
		v = readl_relaxed(reset->prm->base + reset->prm->data->rstctrl);
		if ((v & reset->mask) != reset->mask) {
			dev_dbg(&pdev->dev, "Asserting all resets: %08x\n", v);
			writel_relaxed(reset->mask, reset->prm->base +
				       reset->prm->data->rstctrl);
		}
	}

	return devm_reset_controller_register(&pdev->dev, &reset->rcdev);
}

static int omap_prm_probe(struct platform_device *pdev)
{
	struct resource *res;
	const struct omap_prm_data *data;
	struct omap_prm *prm;
	const struct of_device_id *match;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	match = of_match_device(omap_prm_id_table, &pdev->dev);
	if (!match)
		return -ENOTSUPP;

	prm = devm_kzalloc(&pdev->dev, sizeof(*prm), GFP_KERNEL);
	if (!prm)
		return -ENOMEM;

	data = match->data;

	while (data->base != res->start) {
		if (!data->base)
			return -EINVAL;
		data++;
	}

	prm->data = data;

	prm->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(prm->base))
		return PTR_ERR(prm->base);

	ret = omap_prm_domain_init(&pdev->dev, prm);
	if (ret)
		return ret;

	ret = omap_prm_reset_init(pdev, prm);
	if (ret)
		goto err_domain;

	return 0;

err_domain:
	of_genpd_del_provider(pdev->dev.of_node);
	pm_genpd_remove(&prm->prmd->pd);

	return ret;
}

static struct platform_driver omap_prm_driver = {
	.probe = omap_prm_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= omap_prm_id_table,
	},
};
builtin_platform_driver(omap_prm_driver);

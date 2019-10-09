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
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/delay.h>

struct omap_rst_map {
	s8 rst;
	s8 st;
};

struct omap_prm_data {
	u32 base;
	const char *name;
	u16 rstctrl;
	u16 rstst;
	const struct omap_rst_map *rstmap;
	u8 flags;
};

struct omap_prm {
	const struct omap_prm_data *data;
	void __iomem *base;
};

struct omap_reset_data {
	struct reset_controller_dev rcdev;
	struct omap_prm *prm;
	u32 mask;
	spinlock_t lock;
};

#define to_omap_reset_data(p) container_of((p), struct omap_reset_data, rcdev)

#define OMAP_MAX_RESETS		8
#define OMAP_RESET_MAX_WAIT	10000

#define OMAP_PRM_HAS_RSTCTRL	BIT(0)
#define OMAP_PRM_HAS_RSTST	BIT(1)

#define OMAP_PRM_HAS_RESETS	(OMAP_PRM_HAS_RSTCTRL | OMAP_PRM_HAS_RSTST)

static const struct of_device_id omap_prm_id_table[] = {
	{ },
};

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

	has_rstst = reset->prm->data->rstst ||
		(reset->prm->data->flags & OMAP_PRM_HAS_RSTST);

	if (has_rstst) {
		st_bit = omap_reset_get_st_bit(reset, id);

		/* Clear the reset status by writing 1 to the status bit */
		v = 1 << st_bit;
		writel_relaxed(v, reset->prm->base + reset->prm->data->rstst);
	}

	/* de-assert the reset control line */
	spin_lock_irqsave(&reset->lock, flags);
	v = readl_relaxed(reset->prm->base + reset->prm->data->rstctrl);
	v &= ~(1 << id);
	writel_relaxed(v, reset->prm->base + reset->prm->data->rstctrl);
	spin_unlock_irqrestore(&reset->lock, flags);

	if (!has_rstst)
		return 0;

	/* wait for the status to be set */
	ret = readl_relaxed_poll_timeout(reset->prm->base +
					 reset->prm->data->rstst,
					 v, v & BIT(st_bit), 1,
					 OMAP_RESET_MAX_WAIT);
	if (ret)
		pr_err("%s: timedout waiting for %s:%lu\n", __func__,
		       reset->prm->data->name, id);

	return 0;
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

	/*
	 * Check if we have controllable resets. If either rstctrl is non-zero
	 * or OMAP_PRM_HAS_RSTCTRL flag is set, we have reset control register
	 * for the domain.
	 */
	if (!prm->data->rstctrl && !(prm->data->flags & OMAP_PRM_HAS_RSTCTRL))
		return 0;

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
	spin_lock_init(&reset->lock);

	reset->prm = prm;

	while (map->rst >= 0) {
		reset->mask |= BIT(map->rst);
		map++;
	}

	return devm_reset_controller_register(&pdev->dev, &reset->rcdev);
}

static int omap_prm_probe(struct platform_device *pdev)
{
	struct resource *res;
	const struct omap_prm_data *data;
	struct omap_prm *prm;
	const struct of_device_id *match;

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
	if (!prm->base)
		return -ENOMEM;

	return omap_prm_reset_init(pdev, prm);
}

static struct platform_driver omap_prm_driver = {
	.probe = omap_prm_probe,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= omap_prm_id_table,
	},
};
builtin_platform_driver(omap_prm_driver);

/*
 * Clock driver for Keystone 2 based devices
 *
 * Copyright (C) 2013 Texas Instruments.
 *	Murali Karicheri <m-karicheri2@ti.com>
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/module.h>

/* PSC register offsets */
#define PTCMD			0x120
#define PTSTAT			0x128
#define PDSTAT			0x200
#define PDCTL			0x300
#define MDSTAT			0x800
#define MDCTL			0xa00

/* PSC module states */
#define PSC_STATE_SWRSTDISABLE	0
#define PSC_STATE_SYNCRST	1
#define PSC_STATE_DISABLE	2
#define PSC_STATE_ENABLE	3

#define MDSTAT_STATE_MASK	0x3f
#define MDSTAT_MCKOUT		BIT(12)
#define PDSTAT_STATE_MASK	0x1f
#define MDCTL_FORCE		BIT(31)
#define MDCTL_LRESET		BIT(8)
#define PDCTL_NEXT		BIT(0)

/* Maximum timeout to bail out state transition for module */
#define STATE_TRANS_MAX_COUNT	0xffff

static void __iomem *domain_transition_base;

/**
 * struct clk_psc_data - PSC data
 * @control_base: Base address for a PSC control
 * @domain_base: Base address for a PSC domain
 * @domain_id: PSC domain id number
 */
struct clk_psc_data {
	void __iomem *control_base;
	void __iomem *domain_base;
	u32 domain_id;
};

/**
 * struct clk_psc - PSC clock structure
 * @hw: clk_hw for the psc
 * @psc_data: PSC driver specific data
 * @lock: Spinlock used by the driver
 */
struct clk_psc {
	struct clk_hw hw;
	struct clk_psc_data *psc_data;
	spinlock_t *lock;
};

static DEFINE_SPINLOCK(psc_lock);

#define to_clk_psc(_hw) container_of(_hw, struct clk_psc, hw)

static void psc_config(void __iomem *control_base, void __iomem *domain_base,
						u32 next_state, u32 domain_id)
{
	u32 ptcmd, pdstat, pdctl, mdstat, mdctl, ptstat;
	u32 count = STATE_TRANS_MAX_COUNT;

	mdctl = readl(control_base + MDCTL);
	mdctl &= ~MDSTAT_STATE_MASK;
	mdctl |= next_state;
	/* For disable, we always put the module in local reset */
	if (next_state == PSC_STATE_DISABLE)
		mdctl &= ~MDCTL_LRESET;
	writel(mdctl, control_base + MDCTL);

	pdstat = readl(domain_base + PDSTAT);
	if (!(pdstat & PDSTAT_STATE_MASK)) {
		pdctl = readl(domain_base + PDCTL);
		pdctl |= PDCTL_NEXT;
		writel(pdctl, domain_base + PDCTL);
	}

	ptcmd = 1 << domain_id;
	writel(ptcmd, domain_transition_base + PTCMD);
	do {
		ptstat = readl(domain_transition_base + PTSTAT);
	} while (((ptstat >> domain_id) & 1) && count--);

	count = STATE_TRANS_MAX_COUNT;
	do {
		mdstat = readl(control_base + MDSTAT);
	} while (!((mdstat & MDSTAT_STATE_MASK) == next_state) && count--);
}

static int keystone_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_psc *psc = to_clk_psc(hw);
	struct clk_psc_data *data = psc->psc_data;
	u32 mdstat = readl(data->control_base + MDSTAT);

	return (mdstat & MDSTAT_MCKOUT) ? 1 : 0;
}

static int keystone_clk_enable(struct clk_hw *hw)
{
	struct clk_psc *psc = to_clk_psc(hw);
	struct clk_psc_data *data = psc->psc_data;
	unsigned long flags = 0;

	if (psc->lock)
		spin_lock_irqsave(psc->lock, flags);

	psc_config(data->control_base, data->domain_base,
				PSC_STATE_ENABLE, data->domain_id);

	if (psc->lock)
		spin_unlock_irqrestore(psc->lock, flags);

	return 0;
}

static void keystone_clk_disable(struct clk_hw *hw)
{
	struct clk_psc *psc = to_clk_psc(hw);
	struct clk_psc_data *data = psc->psc_data;
	unsigned long flags = 0;

	if (psc->lock)
		spin_lock_irqsave(psc->lock, flags);

	psc_config(data->control_base, data->domain_base,
				PSC_STATE_DISABLE, data->domain_id);

	if (psc->lock)
		spin_unlock_irqrestore(psc->lock, flags);
}

static const struct clk_ops clk_psc_ops = {
	.enable = keystone_clk_enable,
	.disable = keystone_clk_disable,
	.is_enabled = keystone_clk_is_enabled,
};

/**
 * clk_register_psc - register psc clock
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of clock's parent
 * @psc_data: platform data to configure this clock
 * @lock: spinlock used by this clock
 */
static struct clk *clk_register_psc(struct device *dev,
			const char *name,
			const char *parent_name,
			struct clk_psc_data *psc_data,
			spinlock_t *lock)
{
	struct clk_init_data init;
	struct clk_psc *psc;
	struct clk *clk;

	psc = kzalloc(sizeof(*psc), GFP_KERNEL);
	if (!psc)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &clk_psc_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	psc->psc_data = psc_data;
	psc->lock = lock;
	psc->hw.init = &init;

	clk = clk_register(NULL, &psc->hw);
	if (IS_ERR(clk))
		kfree(psc);

	return clk;
}

/**
 * of_psc_clk_init - initialize psc clock through DT
 * @node: device tree node for this clock
 * @lock: spinlock used by this clock
 */
static void __init of_psc_clk_init(struct device_node *node, spinlock_t *lock)
{
	const char *clk_name = node->name;
	const char *parent_name;
	struct clk_psc_data *data;
	struct clk *clk;
	int i;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		pr_err("%s: Out of memory\n", __func__);
		return;
	}

	i = of_property_match_string(node, "reg-names", "control");
	data->control_base = of_iomap(node, i);
	if (!data->control_base) {
		pr_err("%s: control ioremap failed\n", __func__);
		goto out;
	}

	i = of_property_match_string(node, "reg-names", "domain");
	data->domain_base = of_iomap(node, i);
	if (!data->domain_base) {
		pr_err("%s: domain ioremap failed\n", __func__);
		goto unmap_ctrl;
	}

	of_property_read_u32(node, "domain-id", &data->domain_id);

	/* Domain transition registers at fixed address space of domain_id 0 */
	if (!domain_transition_base && !data->domain_id)
		domain_transition_base = data->domain_base;

	of_property_read_string(node, "clock-output-names", &clk_name);
	parent_name = of_clk_get_parent_name(node, 0);
	if (!parent_name) {
		pr_err("%s: Parent clock not found\n", __func__);
		goto unmap_domain;
	}

	clk = clk_register_psc(NULL, clk_name, parent_name, data, lock);
	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		return;
	}

	pr_err("%s: error registering clk %s\n", __func__, node->name);

unmap_domain:
	iounmap(data->domain_base);
unmap_ctrl:
	iounmap(data->control_base);
out:
	kfree(data);
	return;
}

/**
 * of_keystone_psc_clk_init - initialize psc clock through DT
 * @node: device tree node for this clock
 */
static void __init of_keystone_psc_clk_init(struct device_node *node)
{
	of_psc_clk_init(node, &psc_lock);
}
CLK_OF_DECLARE(keystone_gate_clk, "ti,keystone,psc-clock",
					of_keystone_psc_clk_init);

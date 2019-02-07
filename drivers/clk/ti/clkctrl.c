/*
 * OMAP clkctrl clock support
 *
 * Copyright (C) 2017 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>
#include "clock.h"

#define NO_IDLEST			0x1

#define OMAP4_MODULEMODE_MASK		0x3

#define MODULEMODE_HWCTRL		0x1
#define MODULEMODE_SWCTRL		0x2

#define OMAP4_IDLEST_MASK		(0x3 << 16)
#define OMAP4_IDLEST_SHIFT		16

#define CLKCTRL_IDLEST_FUNCTIONAL	0x0
#define CLKCTRL_IDLEST_INTERFACE_IDLE	0x2
#define CLKCTRL_IDLEST_DISABLED		0x3

/* These timeouts are in us */
#define OMAP4_MAX_MODULE_READY_TIME	2000
#define OMAP4_MAX_MODULE_DISABLE_TIME	5000

static bool _early_timeout = true;

struct omap_clkctrl_provider {
	void __iomem *base;
	struct list_head clocks;
	char *clkdm_name;
};

struct omap_clkctrl_clk {
	struct clk_hw *clk;
	u16 reg_offset;
	int bit_offset;
	struct list_head node;
};

union omap4_timeout {
	u32 cycles;
	ktime_t start;
};

static const struct omap_clkctrl_data default_clkctrl_data[] __initconst = {
	{ 0 },
};

static u32 _omap4_idlest(u32 val)
{
	val &= OMAP4_IDLEST_MASK;
	val >>= OMAP4_IDLEST_SHIFT;

	return val;
}

static bool _omap4_is_idle(u32 val)
{
	val = _omap4_idlest(val);

	return val == CLKCTRL_IDLEST_DISABLED;
}

static bool _omap4_is_ready(u32 val)
{
	val = _omap4_idlest(val);

	return val == CLKCTRL_IDLEST_FUNCTIONAL ||
	       val == CLKCTRL_IDLEST_INTERFACE_IDLE;
}

static bool _omap4_is_timeout(union omap4_timeout *time, u32 timeout)
{
	/*
	 * There are two special cases where ktime_to_ns() can't be
	 * used to track the timeouts. First one is during early boot
	 * when the timers haven't been initialized yet. The second
	 * one is during suspend-resume cycle while timekeeping is
	 * being suspended / resumed. Clocksource for the system
	 * can be from a timer that requires pm_runtime access, which
	 * will eventually bring us here with timekeeping_suspended,
	 * during both suspend entry and resume paths. This happens
	 * at least on am43xx platform.
	 */
	if (unlikely(_early_timeout || timekeeping_suspended)) {
		if (time->cycles++ < timeout) {
			udelay(1);
			return false;
		}
	} else {
		if (!ktime_to_ns(time->start)) {
			time->start = ktime_get();
			return false;
		}

		if (ktime_us_delta(ktime_get(), time->start) < timeout) {
			cpu_relax();
			return false;
		}
	}

	return true;
}

static int __init _omap4_disable_early_timeout(void)
{
	_early_timeout = false;

	return 0;
}
arch_initcall(_omap4_disable_early_timeout);

static int _omap4_clkctrl_clk_enable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 val;
	int ret;
	union omap4_timeout timeout = { 0 };

	if (!clk->enable_bit)
		return 0;

	if (clk->clkdm) {
		ret = ti_clk_ll_ops->clkdm_clk_enable(clk->clkdm, hw->clk);
		if (ret) {
			WARN(1,
			     "%s: could not enable %s's clockdomain %s: %d\n",
			     __func__, clk_hw_get_name(hw),
			     clk->clkdm_name, ret);
			return ret;
		}
	}

	val = ti_clk_ll_ops->clk_readl(&clk->enable_reg);

	val &= ~OMAP4_MODULEMODE_MASK;
	val |= clk->enable_bit;

	ti_clk_ll_ops->clk_writel(val, &clk->enable_reg);

	if (clk->flags & NO_IDLEST)
		return 0;

	/* Wait until module is enabled */
	while (!_omap4_is_ready(ti_clk_ll_ops->clk_readl(&clk->enable_reg))) {
		if (_omap4_is_timeout(&timeout, OMAP4_MAX_MODULE_READY_TIME)) {
			pr_err("%s: failed to enable\n", clk_hw_get_name(hw));
			return -EBUSY;
		}
	}

	return 0;
}

static void _omap4_clkctrl_clk_disable(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 val;
	union omap4_timeout timeout = { 0 };

	if (!clk->enable_bit)
		return;

	val = ti_clk_ll_ops->clk_readl(&clk->enable_reg);

	val &= ~OMAP4_MODULEMODE_MASK;

	ti_clk_ll_ops->clk_writel(val, &clk->enable_reg);

	if (clk->flags & NO_IDLEST)
		goto exit;

	/* Wait until module is disabled */
	while (!_omap4_is_idle(ti_clk_ll_ops->clk_readl(&clk->enable_reg))) {
		if (_omap4_is_timeout(&timeout,
				      OMAP4_MAX_MODULE_DISABLE_TIME)) {
			pr_err("%s: failed to disable\n", clk_hw_get_name(hw));
			break;
		}
	}

exit:
	if (clk->clkdm)
		ti_clk_ll_ops->clkdm_clk_disable(clk->clkdm, hw->clk);
}

static int _omap4_clkctrl_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_hw_omap *clk = to_clk_hw_omap(hw);
	u32 val;

	val = ti_clk_ll_ops->clk_readl(&clk->enable_reg);

	if (val & clk->enable_bit)
		return 1;

	return 0;
}

static const struct clk_ops omap4_clkctrl_clk_ops = {
	.enable		= _omap4_clkctrl_clk_enable,
	.disable	= _omap4_clkctrl_clk_disable,
	.is_enabled	= _omap4_clkctrl_clk_is_enabled,
	.init		= omap2_init_clk_clkdm,
};

static struct clk_hw *_ti_omap4_clkctrl_xlate(struct of_phandle_args *clkspec,
					      void *data)
{
	struct omap_clkctrl_provider *provider = data;
	struct omap_clkctrl_clk *entry;

	if (clkspec->args_count != 2)
		return ERR_PTR(-EINVAL);

	pr_debug("%s: looking for %x:%x\n", __func__,
		 clkspec->args[0], clkspec->args[1]);

	list_for_each_entry(entry, &provider->clocks, node) {
		if (entry->reg_offset == clkspec->args[0] &&
		    entry->bit_offset == clkspec->args[1])
			break;
	}

	if (!entry)
		return ERR_PTR(-EINVAL);

	return entry->clk;
}

static int __init
_ti_clkctrl_clk_register(struct omap_clkctrl_provider *provider,
			 struct device_node *node, struct clk_hw *clk_hw,
			 u16 offset, u8 bit, const char * const *parents,
			 int num_parents, const struct clk_ops *ops)
{
	struct clk_init_data init = { NULL };
	struct clk *clk;
	struct omap_clkctrl_clk *clkctrl_clk;
	int ret = 0;

	if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT)
		init.name = kasprintf(GFP_KERNEL, "%pOFn:%pOFn:%04x:%d",
				      node->parent, node, offset,
				      bit);
	else
		init.name = kasprintf(GFP_KERNEL, "%pOFn:%04x:%d", node,
				      offset, bit);
	clkctrl_clk = kzalloc(sizeof(*clkctrl_clk), GFP_KERNEL);
	if (!init.name || !clkctrl_clk) {
		ret = -ENOMEM;
		goto cleanup;
	}

	clk_hw->init = &init;
	init.parent_names = parents;
	init.num_parents = num_parents;
	init.ops = ops;
	init.flags = CLK_IS_BASIC;

	clk = ti_clk_register(NULL, clk_hw, init.name);
	if (IS_ERR_OR_NULL(clk)) {
		ret = -EINVAL;
		goto cleanup;
	}

	clkctrl_clk->reg_offset = offset;
	clkctrl_clk->bit_offset = bit;
	clkctrl_clk->clk = clk_hw;

	list_add(&clkctrl_clk->node, &provider->clocks);

	return 0;

cleanup:
	kfree(init.name);
	kfree(clkctrl_clk);
	return ret;
}

static void __init
_ti_clkctrl_setup_gate(struct omap_clkctrl_provider *provider,
		       struct device_node *node, u16 offset,
		       const struct omap_clkctrl_bit_data *data,
		       void __iomem *reg)
{
	struct clk_hw_omap *clk_hw;

	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	if (!clk_hw)
		return;

	clk_hw->enable_bit = data->bit;
	clk_hw->enable_reg.ptr = reg;

	if (_ti_clkctrl_clk_register(provider, node, &clk_hw->hw, offset,
				     data->bit, data->parents, 1,
				     &omap_gate_clk_ops))
		kfree(clk_hw);
}

static void __init
_ti_clkctrl_setup_mux(struct omap_clkctrl_provider *provider,
		      struct device_node *node, u16 offset,
		      const struct omap_clkctrl_bit_data *data,
		      void __iomem *reg)
{
	struct clk_omap_mux *mux;
	int num_parents = 0;
	const char * const *pname;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return;

	pname = data->parents;
	while (*pname) {
		num_parents++;
		pname++;
	}

	mux->mask = num_parents;
	if (!(mux->flags & CLK_MUX_INDEX_ONE))
		mux->mask--;

	mux->mask = (1 << fls(mux->mask)) - 1;

	mux->shift = data->bit;
	mux->reg.ptr = reg;

	if (_ti_clkctrl_clk_register(provider, node, &mux->hw, offset,
				     data->bit, data->parents, num_parents,
				     &ti_clk_mux_ops))
		kfree(mux);
}

static void __init
_ti_clkctrl_setup_div(struct omap_clkctrl_provider *provider,
		      struct device_node *node, u16 offset,
		      const struct omap_clkctrl_bit_data *data,
		      void __iomem *reg)
{
	struct clk_omap_divider *div;
	const struct omap_clkctrl_div_data *div_data = data->data;
	u8 div_flags = 0;

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (!div)
		return;

	div->reg.ptr = reg;
	div->shift = data->bit;
	div->flags = div_data->flags;

	if (div->flags & CLK_DIVIDER_POWER_OF_TWO)
		div_flags |= CLKF_INDEX_POWER_OF_TWO;

	if (ti_clk_parse_divider_data((int *)div_data->dividers, 0,
				      div_data->max_div, div_flags,
				      &div->width, &div->table)) {
		pr_err("%s: Data parsing for %pOF:%04x:%d failed\n", __func__,
		       node, offset, data->bit);
		kfree(div);
		return;
	}

	if (_ti_clkctrl_clk_register(provider, node, &div->hw, offset,
				     data->bit, data->parents, 1,
				     &ti_clk_divider_ops))
		kfree(div);
}

static void __init
_ti_clkctrl_setup_subclks(struct omap_clkctrl_provider *provider,
			  struct device_node *node,
			  const struct omap_clkctrl_reg_data *data,
			  void __iomem *reg)
{
	const struct omap_clkctrl_bit_data *bits = data->bit_data;

	if (!bits)
		return;

	while (bits->bit) {
		switch (bits->type) {
		case TI_CLK_GATE:
			_ti_clkctrl_setup_gate(provider, node, data->offset,
					       bits, reg);
			break;

		case TI_CLK_DIVIDER:
			_ti_clkctrl_setup_div(provider, node, data->offset,
					      bits, reg);
			break;

		case TI_CLK_MUX:
			_ti_clkctrl_setup_mux(provider, node, data->offset,
					      bits, reg);
			break;

		default:
			pr_err("%s: bad subclk type: %d\n", __func__,
			       bits->type);
			return;
		}
		bits++;
	}
}

static void __init _clkctrl_add_provider(void *data,
					 struct device_node *np)
{
	of_clk_add_hw_provider(np, _ti_omap4_clkctrl_xlate, data);
}

static void __init _ti_omap4_clkctrl_setup(struct device_node *node)
{
	struct omap_clkctrl_provider *provider;
	const struct omap_clkctrl_data *data = default_clkctrl_data;
	const struct omap_clkctrl_reg_data *reg_data;
	struct clk_init_data init = { NULL };
	struct clk_hw_omap *hw;
	struct clk *clk;
	struct omap_clkctrl_clk *clkctrl_clk;
	const __be32 *addrp;
	u32 addr;
	int ret;
	char *c;

	if (!(ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT) &&
	    of_node_name_eq(node, "clk"))
		ti_clk_features.flags |= TI_CLK_CLKCTRL_COMPAT;

	addrp = of_get_address(node, 0, NULL, NULL);
	addr = (u32)of_translate_address(node, addrp);

#ifdef CONFIG_ARCH_OMAP4
	if (of_machine_is_compatible("ti,omap4"))
		data = omap4_clkctrl_data;
#endif
#ifdef CONFIG_SOC_OMAP5
	if (of_machine_is_compatible("ti,omap5"))
		data = omap5_clkctrl_data;
#endif
#ifdef CONFIG_SOC_DRA7XX
	if (of_machine_is_compatible("ti,dra7")) {
		if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT)
			data = dra7_clkctrl_compat_data;
		else
			data = dra7_clkctrl_data;
	}
#endif
#ifdef CONFIG_SOC_AM33XX
	if (of_machine_is_compatible("ti,am33xx")) {
		if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT)
			data = am3_clkctrl_compat_data;
		else
			data = am3_clkctrl_data;
	}
#endif
#ifdef CONFIG_SOC_AM43XX
	if (of_machine_is_compatible("ti,am4372")) {
		if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT)
			data = am4_clkctrl_compat_data;
		else
			data = am4_clkctrl_data;
	}

	if (of_machine_is_compatible("ti,am438x")) {
		if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT)
			data = am438x_clkctrl_compat_data;
		else
			data = am438x_clkctrl_data;
	}
#endif
#ifdef CONFIG_SOC_TI81XX
	if (of_machine_is_compatible("ti,dm814"))
		data = dm814_clkctrl_data;

	if (of_machine_is_compatible("ti,dm816"))
		data = dm816_clkctrl_data;
#endif

	while (data->addr) {
		if (addr == data->addr)
			break;

		data++;
	}

	if (!data->addr) {
		pr_err("%pOF not found from clkctrl data.\n", node);
		return;
	}

	provider = kzalloc(sizeof(*provider), GFP_KERNEL);
	if (!provider)
		return;

	provider->base = of_iomap(node, 0);

	if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT) {
		provider->clkdm_name = kasprintf(GFP_KERNEL, "%pOFnxxx", node->parent);
		if (!provider->clkdm_name) {
			kfree(provider);
			return;
		}

		/*
		 * Create default clkdm name, replace _cm from end of parent
		 * node name with _clkdm
		 */
		provider->clkdm_name[strlen(provider->clkdm_name) - 5] = 0;
	} else {
		provider->clkdm_name = kasprintf(GFP_KERNEL, "%pOFn", node);
		if (!provider->clkdm_name) {
			kfree(provider);
			return;
		}

		/*
		 * Create default clkdm name, replace _clkctrl from end of
		 * node name with _clkdm
		 */
		provider->clkdm_name[strlen(provider->clkdm_name) - 7] = 0;
	}

	strcat(provider->clkdm_name, "clkdm");

	/* Replace any dash from the clkdm name with underscore */
	c = provider->clkdm_name;

	while (*c) {
		if (*c == '-')
			*c = '_';
		c++;
	}

	INIT_LIST_HEAD(&provider->clocks);

	/* Generate clocks */
	reg_data = data->regs;

	while (reg_data->parent) {
		hw = kzalloc(sizeof(*hw), GFP_KERNEL);
		if (!hw)
			return;

		hw->enable_reg.ptr = provider->base + reg_data->offset;

		_ti_clkctrl_setup_subclks(provider, node, reg_data,
					  hw->enable_reg.ptr);

		if (reg_data->flags & CLKF_SW_SUP)
			hw->enable_bit = MODULEMODE_SWCTRL;
		if (reg_data->flags & CLKF_HW_SUP)
			hw->enable_bit = MODULEMODE_HWCTRL;
		if (reg_data->flags & CLKF_NO_IDLEST)
			hw->flags |= NO_IDLEST;

		if (reg_data->clkdm_name)
			hw->clkdm_name = reg_data->clkdm_name;
		else
			hw->clkdm_name = provider->clkdm_name;

		init.parent_names = &reg_data->parent;
		init.num_parents = 1;
		init.flags = 0;
		if (reg_data->flags & CLKF_SET_RATE_PARENT)
			init.flags |= CLK_SET_RATE_PARENT;
		if (ti_clk_get_features()->flags & TI_CLK_CLKCTRL_COMPAT)
			init.name = kasprintf(GFP_KERNEL, "%pOFn:%pOFn:%04x:%d",
					      node->parent, node,
					      reg_data->offset, 0);
		else
			init.name = kasprintf(GFP_KERNEL, "%pOFn:%04x:%d",
					      node, reg_data->offset, 0);
		clkctrl_clk = kzalloc(sizeof(*clkctrl_clk), GFP_KERNEL);
		if (!init.name || !clkctrl_clk)
			goto cleanup;

		init.ops = &omap4_clkctrl_clk_ops;
		hw->hw.init = &init;

		clk = ti_clk_register(NULL, &hw->hw, init.name);
		if (IS_ERR_OR_NULL(clk))
			goto cleanup;

		clkctrl_clk->reg_offset = reg_data->offset;
		clkctrl_clk->clk = &hw->hw;

		list_add(&clkctrl_clk->node, &provider->clocks);

		reg_data++;
	}

	ret = of_clk_add_hw_provider(node, _ti_omap4_clkctrl_xlate, provider);
	if (ret == -EPROBE_DEFER)
		ti_clk_retry_init(node, provider, _clkctrl_add_provider);

	return;

cleanup:
	kfree(hw);
	kfree(init.name);
	kfree(clkctrl_clk);
}
CLK_OF_DECLARE(ti_omap4_clkctrl_clock, "ti,clkctrl",
	       _ti_omap4_clkctrl_setup);

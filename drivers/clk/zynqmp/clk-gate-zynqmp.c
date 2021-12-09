// SPDX-License-Identifier: GPL-2.0
/*
 * Zynq UltraScale+ MPSoC clock controller
 *
 *  Copyright (C) 2016-2018 Xilinx
 *
 * Gated clock implementation
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include "clk-zynqmp.h"

/**
 * struct zynqmp_clk_gate - gating clock
 * @hw:		handle between common and hardware-specific interfaces
 * @flags:	hardware-specific flags
 * @clk_id:	Id of clock
 */
struct zynqmp_clk_gate {
	struct clk_hw hw;
	u8 flags;
	u32 clk_id;
};

#define to_zynqmp_clk_gate(_hw) container_of(_hw, struct zynqmp_clk_gate, hw)

/**
 * zynqmp_clk_gate_enable() - Enable clock
 * @hw:		handle between common and hardware-specific interfaces
 *
 * Return: 0 on success else error code
 */
static int zynqmp_clk_gate_enable(struct clk_hw *hw)
{
	struct zynqmp_clk_gate *gate = to_zynqmp_clk_gate(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = gate->clk_id;
	int ret;

	ret = zynqmp_pm_clock_enable(clk_id);

	if (ret)
		pr_warn_once("%s() clock enabled failed for %s, ret = %d\n",
			     __func__, clk_name, ret);

	return ret;
}

/*
 * zynqmp_clk_gate_disable() - Disable clock
 * @hw:		handle between common and hardware-specific interfaces
 */
static void zynqmp_clk_gate_disable(struct clk_hw *hw)
{
	struct zynqmp_clk_gate *gate = to_zynqmp_clk_gate(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = gate->clk_id;
	int ret;

	ret = zynqmp_pm_clock_disable(clk_id);

	if (ret)
		pr_warn_once("%s() clock disable failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
}

/**
 * zynqmp_clk_gate_is_enabled() - Check clock state
 * @hw:		handle between common and hardware-specific interfaces
 *
 * Return: 1 if enabled, 0 if disabled else error code
 */
static int zynqmp_clk_gate_is_enabled(struct clk_hw *hw)
{
	struct zynqmp_clk_gate *gate = to_zynqmp_clk_gate(hw);
	const char *clk_name = clk_hw_get_name(hw);
	u32 clk_id = gate->clk_id;
	int state, ret;

	ret = zynqmp_pm_clock_getstate(clk_id, &state);
	if (ret) {
		pr_warn_once("%s() clock get state failed for %s, ret = %d\n",
			     __func__, clk_name, ret);
		return -EIO;
	}

	return state ? 1 : 0;
}

static const struct clk_ops zynqmp_clk_gate_ops = {
	.enable = zynqmp_clk_gate_enable,
	.disable = zynqmp_clk_gate_disable,
	.is_enabled = zynqmp_clk_gate_is_enabled,
};

/**
 * zynqmp_clk_register_gate() - Register a gate clock with the clock framework
 * @name:		Name of this clock
 * @clk_id:		Id of this clock
 * @parents:		Name of this clock's parents
 * @num_parents:	Number of parents
 * @nodes:		Clock topology node
 *
 * Return: clock hardware of the registered clock gate
 */
struct clk_hw *zynqmp_clk_register_gate(const char *name, u32 clk_id,
					const char * const *parents,
					u8 num_parents,
					const struct clock_topology *nodes)
{
	struct zynqmp_clk_gate *gate;
	struct clk_hw *hw;
	int ret;
	struct clk_init_data init;

	/* allocate the gate */
	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &zynqmp_clk_gate_ops;

	init.flags = zynqmp_clk_map_common_ccf_flags(nodes->flag);

	init.parent_names = parents;
	init.num_parents = 1;

	/* struct clk_gate assignments */
	gate->flags = nodes->type_flag;
	gate->hw.init = &init;
	gate->clk_id = clk_id;

	hw = &gate->hw;
	ret = clk_hw_register(NULL, hw);
	if (ret) {
		kfree(gate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

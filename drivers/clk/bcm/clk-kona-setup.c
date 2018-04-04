/*
 * Copyright (C) 2013 Broadcom Corporation
 * Copyright 2013 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/of_address.h>

#include "clk-kona.h"

/* These are used when a selector or trigger is found to be unneeded */
#define selector_clear_exists(sel)	((sel)->width = 0)
#define trigger_clear_exists(trig)	FLAG_CLEAR(trig, TRIG, EXISTS)

/* Validity checking */

static bool ccu_data_offsets_valid(struct ccu_data *ccu)
{
	struct ccu_policy *ccu_policy = &ccu->policy;
	u32 limit;

	limit = ccu->range - sizeof(u32);
	limit = round_down(limit, sizeof(u32));
	if (ccu_policy_exists(ccu_policy)) {
		if (ccu_policy->enable.offset > limit) {
			pr_err("%s: bad policy enable offset for %s "
					"(%u > %u)\n", __func__,
				ccu->name, ccu_policy->enable.offset, limit);
			return false;
		}
		if (ccu_policy->control.offset > limit) {
			pr_err("%s: bad policy control offset for %s "
					"(%u > %u)\n", __func__,
				ccu->name, ccu_policy->control.offset, limit);
			return false;
		}
	}

	return true;
}

static bool clk_requires_trigger(struct kona_clk *bcm_clk)
{
	struct peri_clk_data *peri = bcm_clk->u.peri;
	struct bcm_clk_sel *sel;
	struct bcm_clk_div *div;

	if (bcm_clk->type != bcm_clk_peri)
		return false;

	sel = &peri->sel;
	if (sel->parent_count && selector_exists(sel))
		return true;

	div = &peri->div;
	if (!divider_exists(div))
		return false;

	/* Fixed dividers don't need triggers */
	if (!divider_is_fixed(div))
		return true;

	div = &peri->pre_div;

	return divider_exists(div) && !divider_is_fixed(div);
}

static bool peri_clk_data_offsets_valid(struct kona_clk *bcm_clk)
{
	struct peri_clk_data *peri;
	struct bcm_clk_policy *policy;
	struct bcm_clk_gate *gate;
	struct bcm_clk_hyst *hyst;
	struct bcm_clk_div *div;
	struct bcm_clk_sel *sel;
	struct bcm_clk_trig *trig;
	const char *name;
	u32 range;
	u32 limit;

	BUG_ON(bcm_clk->type != bcm_clk_peri);
	peri = bcm_clk->u.peri;
	name = bcm_clk->init_data.name;
	range = bcm_clk->ccu->range;

	limit = range - sizeof(u32);
	limit = round_down(limit, sizeof(u32));

	policy = &peri->policy;
	if (policy_exists(policy)) {
		if (policy->offset > limit) {
			pr_err("%s: bad policy offset for %s (%u > %u)\n",
				__func__, name, policy->offset, limit);
			return false;
		}
	}

	gate = &peri->gate;
	hyst = &peri->hyst;
	if (gate_exists(gate)) {
		if (gate->offset > limit) {
			pr_err("%s: bad gate offset for %s (%u > %u)\n",
				__func__, name, gate->offset, limit);
			return false;
		}

		if (hyst_exists(hyst)) {
			if (hyst->offset > limit) {
				pr_err("%s: bad hysteresis offset for %s "
					"(%u > %u)\n", __func__,
					name, hyst->offset, limit);
				return false;
			}
		}
	} else if (hyst_exists(hyst)) {
		pr_err("%s: hysteresis but no gate for %s\n", __func__, name);
		return false;
	}

	div = &peri->div;
	if (divider_exists(div)) {
		if (div->u.s.offset > limit) {
			pr_err("%s: bad divider offset for %s (%u > %u)\n",
				__func__, name, div->u.s.offset, limit);
			return false;
		}
	}

	div = &peri->pre_div;
	if (divider_exists(div)) {
		if (div->u.s.offset > limit) {
			pr_err("%s: bad pre-divider offset for %s "
					"(%u > %u)\n",
				__func__, name, div->u.s.offset, limit);
			return false;
		}
	}

	sel = &peri->sel;
	if (selector_exists(sel)) {
		if (sel->offset > limit) {
			pr_err("%s: bad selector offset for %s (%u > %u)\n",
				__func__, name, sel->offset, limit);
			return false;
		}
	}

	trig = &peri->trig;
	if (trigger_exists(trig)) {
		if (trig->offset > limit) {
			pr_err("%s: bad trigger offset for %s (%u > %u)\n",
				__func__, name, trig->offset, limit);
			return false;
		}
	}

	trig = &peri->pre_trig;
	if (trigger_exists(trig)) {
		if (trig->offset > limit) {
			pr_err("%s: bad pre-trigger offset for %s (%u > %u)\n",
				__func__, name, trig->offset, limit);
			return false;
		}
	}

	return true;
}

/* A bit position must be less than the number of bits in a 32-bit register. */
static bool bit_posn_valid(u32 bit_posn, const char *field_name,
			const char *clock_name)
{
	u32 limit = BITS_PER_BYTE * sizeof(u32) - 1;

	if (bit_posn > limit) {
		pr_err("%s: bad %s bit for %s (%u > %u)\n", __func__,
			field_name, clock_name, bit_posn, limit);
		return false;
	}
	return true;
}

/*
 * A bitfield must be at least 1 bit wide.  Both the low-order and
 * high-order bits must lie within a 32-bit register.  We require
 * fields to be less than 32 bits wide, mainly because we use
 * shifting to produce field masks, and shifting a full word width
 * is not well-defined by the C standard.
 */
static bool bitfield_valid(u32 shift, u32 width, const char *field_name,
			const char *clock_name)
{
	u32 limit = BITS_PER_BYTE * sizeof(u32);

	if (!width) {
		pr_err("%s: bad %s field width 0 for %s\n", __func__,
			field_name, clock_name);
		return false;
	}
	if (shift + width > limit) {
		pr_err("%s: bad %s for %s (%u + %u > %u)\n", __func__,
			field_name, clock_name, shift, width, limit);
		return false;
	}
	return true;
}

static bool
ccu_policy_valid(struct ccu_policy *ccu_policy, const char *ccu_name)
{
	struct bcm_lvm_en *enable = &ccu_policy->enable;
	struct bcm_policy_ctl *control;

	if (!bit_posn_valid(enable->bit, "policy enable", ccu_name))
		return false;

	control = &ccu_policy->control;
	if (!bit_posn_valid(control->go_bit, "policy control GO", ccu_name))
		return false;

	if (!bit_posn_valid(control->atl_bit, "policy control ATL", ccu_name))
		return false;

	if (!bit_posn_valid(control->ac_bit, "policy control AC", ccu_name))
		return false;

	return true;
}

static bool policy_valid(struct bcm_clk_policy *policy, const char *clock_name)
{
	if (!bit_posn_valid(policy->bit, "policy", clock_name))
		return false;

	return true;
}

/*
 * All gates, if defined, have a status bit, and for hardware-only
 * gates, that's it.  Gates that can be software controlled also
 * have an enable bit.  And a gate that can be hardware or software
 * controlled will have a hardware/software select bit.
 */
static bool gate_valid(struct bcm_clk_gate *gate, const char *field_name,
			const char *clock_name)
{
	if (!bit_posn_valid(gate->status_bit, "gate status", clock_name))
		return false;

	if (gate_is_sw_controllable(gate)) {
		if (!bit_posn_valid(gate->en_bit, "gate enable", clock_name))
			return false;

		if (gate_is_hw_controllable(gate)) {
			if (!bit_posn_valid(gate->hw_sw_sel_bit,
						"gate hw/sw select",
						clock_name))
				return false;
		}
	} else {
		BUG_ON(!gate_is_hw_controllable(gate));
	}

	return true;
}

static bool hyst_valid(struct bcm_clk_hyst *hyst, const char *clock_name)
{
	if (!bit_posn_valid(hyst->en_bit, "hysteresis enable", clock_name))
		return false;

	if (!bit_posn_valid(hyst->val_bit, "hysteresis value", clock_name))
		return false;

	return true;
}

/*
 * A selector bitfield must be valid.  Its parent_sel array must
 * also be reasonable for the field.
 */
static bool sel_valid(struct bcm_clk_sel *sel, const char *field_name,
			const char *clock_name)
{
	if (!bitfield_valid(sel->shift, sel->width, field_name, clock_name))
		return false;

	if (sel->parent_count) {
		u32 max_sel;
		u32 limit;

		/*
		 * Make sure the selector field can hold all the
		 * selector values we expect to be able to use.  A
		 * clock only needs to have a selector defined if it
		 * has more than one parent.  And in that case the
		 * highest selector value will be in the last entry
		 * in the array.
		 */
		max_sel = sel->parent_sel[sel->parent_count - 1];
		limit = (1 << sel->width) - 1;
		if (max_sel > limit) {
			pr_err("%s: bad selector for %s "
					"(%u needs > %u bits)\n",
				__func__, clock_name, max_sel,
				sel->width);
			return false;
		}
	} else {
		pr_warn("%s: ignoring selector for %s (no parents)\n",
			__func__, clock_name);
		selector_clear_exists(sel);
		kfree(sel->parent_sel);
		sel->parent_sel = NULL;
	}

	return true;
}

/*
 * A fixed divider just needs to be non-zero.  A variable divider
 * has to have a valid divider bitfield, and if it has a fraction,
 * the width of the fraction must not be no more than the width of
 * the divider as a whole.
 */
static bool div_valid(struct bcm_clk_div *div, const char *field_name,
			const char *clock_name)
{
	if (divider_is_fixed(div)) {
		/* Any fixed divider value but 0 is OK */
		if (div->u.fixed == 0) {
			pr_err("%s: bad %s fixed value 0 for %s\n", __func__,
				field_name, clock_name);
			return false;
		}
		return true;
	}
	if (!bitfield_valid(div->u.s.shift, div->u.s.width,
				field_name, clock_name))
		return false;

	if (divider_has_fraction(div))
		if (div->u.s.frac_width > div->u.s.width) {
			pr_warn("%s: bad %s fraction width for %s (%u > %u)\n",
				__func__, field_name, clock_name,
				div->u.s.frac_width, div->u.s.width);
			return false;
		}

	return true;
}

/*
 * If a clock has two dividers, the combined number of fractional
 * bits must be representable in a 32-bit unsigned value.  This
 * is because we scale up a dividend using both dividers before
 * dividing to improve accuracy, and we need to avoid overflow.
 */
static bool kona_dividers_valid(struct kona_clk *bcm_clk)
{
	struct peri_clk_data *peri = bcm_clk->u.peri;
	struct bcm_clk_div *div;
	struct bcm_clk_div *pre_div;
	u32 limit;

	BUG_ON(bcm_clk->type != bcm_clk_peri);

	if (!divider_exists(&peri->div) || !divider_exists(&peri->pre_div))
		return true;

	div = &peri->div;
	pre_div = &peri->pre_div;
	if (divider_is_fixed(div) || divider_is_fixed(pre_div))
		return true;

	limit = BITS_PER_BYTE * sizeof(u32);

	return div->u.s.frac_width + pre_div->u.s.frac_width <= limit;
}


/* A trigger just needs to represent a valid bit position */
static bool trig_valid(struct bcm_clk_trig *trig, const char *field_name,
			const char *clock_name)
{
	return bit_posn_valid(trig->bit, field_name, clock_name);
}

/* Determine whether the set of peripheral clock registers are valid. */
static bool
peri_clk_data_valid(struct kona_clk *bcm_clk)
{
	struct peri_clk_data *peri;
	struct bcm_clk_policy *policy;
	struct bcm_clk_gate *gate;
	struct bcm_clk_hyst *hyst;
	struct bcm_clk_sel *sel;
	struct bcm_clk_div *div;
	struct bcm_clk_div *pre_div;
	struct bcm_clk_trig *trig;
	const char *name;

	BUG_ON(bcm_clk->type != bcm_clk_peri);

	/*
	 * First validate register offsets.  This is the only place
	 * where we need something from the ccu, so we do these
	 * together.
	 */
	if (!peri_clk_data_offsets_valid(bcm_clk))
		return false;

	peri = bcm_clk->u.peri;
	name = bcm_clk->init_data.name;

	policy = &peri->policy;
	if (policy_exists(policy) && !policy_valid(policy, name))
		return false;

	gate = &peri->gate;
	if (gate_exists(gate) && !gate_valid(gate, "gate", name))
		return false;

	hyst = &peri->hyst;
	if (hyst_exists(hyst) && !hyst_valid(hyst, name))
		return false;

	sel = &peri->sel;
	if (selector_exists(sel)) {
		if (!sel_valid(sel, "selector", name))
			return false;

	} else if (sel->parent_count > 1) {
		pr_err("%s: multiple parents but no selector for %s\n",
			__func__, name);

		return false;
	}

	div = &peri->div;
	pre_div = &peri->pre_div;
	if (divider_exists(div)) {
		if (!div_valid(div, "divider", name))
			return false;

		if (divider_exists(pre_div))
			if (!div_valid(pre_div, "pre-divider", name))
				return false;
	} else if (divider_exists(pre_div)) {
		pr_err("%s: pre-divider but no divider for %s\n", __func__,
			name);
		return false;
	}

	trig = &peri->trig;
	if (trigger_exists(trig)) {
		if (!trig_valid(trig, "trigger", name))
			return false;

		if (trigger_exists(&peri->pre_trig)) {
			if (!trig_valid(trig, "pre-trigger", name)) {
				return false;
			}
		}
		if (!clk_requires_trigger(bcm_clk)) {
			pr_warn("%s: ignoring trigger for %s (not needed)\n",
				__func__, name);
			trigger_clear_exists(trig);
		}
	} else if (trigger_exists(&peri->pre_trig)) {
		pr_err("%s: pre-trigger but no trigger for %s\n", __func__,
			name);
		return false;
	} else if (clk_requires_trigger(bcm_clk)) {
		pr_err("%s: required trigger missing for %s\n", __func__,
			name);
		return false;
	}

	return kona_dividers_valid(bcm_clk);
}

static bool kona_clk_valid(struct kona_clk *bcm_clk)
{
	switch (bcm_clk->type) {
	case bcm_clk_peri:
		if (!peri_clk_data_valid(bcm_clk))
			return false;
		break;
	default:
		pr_err("%s: unrecognized clock type (%d)\n", __func__,
			(int)bcm_clk->type);
		return false;
	}
	return true;
}

/*
 * Scan an array of parent clock names to determine whether there
 * are any entries containing BAD_CLK_NAME.  Such entries are
 * placeholders for non-supported clocks.  Keep track of the
 * position of each clock name in the original array.
 *
 * Allocates an array of pointers to to hold the names of all
 * non-null entries in the original array, and returns a pointer to
 * that array in *names.  This will be used for registering the
 * clock with the common clock code.  On successful return,
 * *count indicates how many entries are in that names array.
 *
 * If there is more than one entry in the resulting names array,
 * another array is allocated to record the parent selector value
 * for each (defined) parent clock.  This is the value that
 * represents this parent clock in the clock's source selector
 * register.  The position of the clock in the original parent array
 * defines that selector value.  The number of entries in this array
 * is the same as the number of entries in the parent names array.
 *
 * The array of selector values is returned.  If the clock has no
 * parents, no selector is required and a null pointer is returned.
 *
 * Returns a null pointer if the clock names array supplied was
 * null.  (This is not an error.)
 *
 * Returns a pointer-coded error if an error occurs.
 */
static u32 *parent_process(const char *clocks[],
			u32 *count, const char ***names)
{
	static const char **parent_names;
	static u32 *parent_sel;
	const char **clock;
	u32 parent_count;
	u32 bad_count = 0;
	u32 orig_count;
	u32 i;
	u32 j;

	*count = 0;	/* In case of early return */
	*names = NULL;
	if (!clocks)
		return NULL;

	/*
	 * Count the number of names in the null-terminated array,
	 * and find out how many of those are actually clock names.
	 */
	for (clock = clocks; *clock; clock++)
		if (*clock == BAD_CLK_NAME)
			bad_count++;
	orig_count = (u32)(clock - clocks);
	parent_count = orig_count - bad_count;

	/* If all clocks are unsupported, we treat it as no clock */
	if (!parent_count)
		return NULL;

	/* Avoid exceeding our parent clock limit */
	if (parent_count > PARENT_COUNT_MAX) {
		pr_err("%s: too many parents (%u > %u)\n", __func__,
			parent_count, PARENT_COUNT_MAX);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * There is one parent name for each defined parent clock.
	 * We also maintain an array containing the selector value
	 * for each defined clock.  If there's only one clock, the
	 * selector is not required, but we allocate space for the
	 * array anyway to keep things simple.
	 */
	parent_names = kmalloc_array(parent_count, sizeof(*parent_names),
			       GFP_KERNEL);
	if (!parent_names)
		return ERR_PTR(-ENOMEM);

	/* There is at least one parent, so allocate a selector array */
	parent_sel = kmalloc_array(parent_count, sizeof(*parent_sel),
				   GFP_KERNEL);
	if (!parent_sel) {
		kfree(parent_names);

		return ERR_PTR(-ENOMEM);
	}

	/* Now fill in the parent names and selector arrays */
	for (i = 0, j = 0; i < orig_count; i++) {
		if (clocks[i] != BAD_CLK_NAME) {
			parent_names[j] = clocks[i];
			parent_sel[j] = i;
			j++;
		}
	}
	*names = parent_names;
	*count = parent_count;

	return parent_sel;
}

static int
clk_sel_setup(const char **clocks, struct bcm_clk_sel *sel,
		struct clk_init_data *init_data)
{
	const char **parent_names = NULL;
	u32 parent_count = 0;
	u32 *parent_sel;

	/*
	 * If a peripheral clock has multiple parents, the value
	 * used by the hardware to select that parent is represented
	 * by the parent clock's position in the "clocks" list.  Some
	 * values don't have defined or supported clocks; these will
	 * have BAD_CLK_NAME entries in the parents[] array.  The
	 * list is terminated by a NULL entry.
	 *
	 * We need to supply (only) the names of defined parent
	 * clocks when registering a clock though, so we use an
	 * array of parent selector values to map between the
	 * indexes the common clock code uses and the selector
	 * values we need.
	 */
	parent_sel = parent_process(clocks, &parent_count, &parent_names);
	if (IS_ERR(parent_sel)) {
		int ret = PTR_ERR(parent_sel);

		pr_err("%s: error processing parent clocks for %s (%d)\n",
			__func__, init_data->name, ret);

		return ret;
	}

	init_data->parent_names = parent_names;
	init_data->num_parents = parent_count;

	sel->parent_count = parent_count;
	sel->parent_sel = parent_sel;

	return 0;
}

static void clk_sel_teardown(struct bcm_clk_sel *sel,
		struct clk_init_data *init_data)
{
	kfree(sel->parent_sel);
	sel->parent_sel = NULL;
	sel->parent_count = 0;

	init_data->num_parents = 0;
	kfree(init_data->parent_names);
	init_data->parent_names = NULL;
}

static void peri_clk_teardown(struct peri_clk_data *data,
				struct clk_init_data *init_data)
{
	clk_sel_teardown(&data->sel, init_data);
}

/*
 * Caller is responsible for freeing the parent_names[] and
 * parent_sel[] arrays in the peripheral clock's "data" structure
 * that can be assigned if the clock has one or more parent clocks
 * associated with it.
 */
static int
peri_clk_setup(struct peri_clk_data *data, struct clk_init_data *init_data)
{
	init_data->flags = CLK_IGNORE_UNUSED;

	return clk_sel_setup(data->clocks, &data->sel, init_data);
}

static void bcm_clk_teardown(struct kona_clk *bcm_clk)
{
	switch (bcm_clk->type) {
	case bcm_clk_peri:
		peri_clk_teardown(bcm_clk->u.data, &bcm_clk->init_data);
		break;
	default:
		break;
	}
	bcm_clk->u.data = NULL;
	bcm_clk->type = bcm_clk_none;
}

static void kona_clk_teardown(struct clk_hw *hw)
{
	struct kona_clk *bcm_clk;

	if (!hw)
		return;

	clk_hw_unregister(hw);

	bcm_clk = to_kona_clk(hw);
	bcm_clk_teardown(bcm_clk);
}

static int kona_clk_setup(struct kona_clk *bcm_clk)
{
	int ret;
	struct clk_init_data *init_data = &bcm_clk->init_data;

	switch (bcm_clk->type) {
	case bcm_clk_peri:
		ret = peri_clk_setup(bcm_clk->u.data, init_data);
		if (ret)
			return ret;
		break;
	default:
		pr_err("%s: clock type %d invalid for %s\n", __func__,
			(int)bcm_clk->type, init_data->name);
		return -EINVAL;
	}

	/* Make sure everything makes sense before we set it up */
	if (!kona_clk_valid(bcm_clk)) {
		pr_err("%s: clock data invalid for %s\n", __func__,
			init_data->name);
		ret = -EINVAL;
		goto out_teardown;
	}

	bcm_clk->hw.init = init_data;
	ret = clk_hw_register(NULL, &bcm_clk->hw);
	if (ret) {
		pr_err("%s: error registering clock %s (%d)\n", __func__,
			init_data->name, ret);
		goto out_teardown;
	}

	return 0;
out_teardown:
	bcm_clk_teardown(bcm_clk);

	return ret;
}

static void ccu_clks_teardown(struct ccu_data *ccu)
{
	u32 i;

	for (i = 0; i < ccu->clk_num; i++)
		kona_clk_teardown(&ccu->kona_clks[i].hw);
}

static void kona_ccu_teardown(struct ccu_data *ccu)
{
	if (!ccu->base)
		return;

	of_clk_del_provider(ccu->node);	/* safe if never added */
	ccu_clks_teardown(ccu);
	of_node_put(ccu->node);
	ccu->node = NULL;
	iounmap(ccu->base);
	ccu->base = NULL;
}

static bool ccu_data_valid(struct ccu_data *ccu)
{
	struct ccu_policy *ccu_policy;

	if (!ccu_data_offsets_valid(ccu))
		return false;

	ccu_policy = &ccu->policy;
	if (ccu_policy_exists(ccu_policy))
		if (!ccu_policy_valid(ccu_policy, ccu->name))
			return false;

	return true;
}

static struct clk_hw *
of_clk_kona_onecell_get(struct of_phandle_args *clkspec, void *data)
{
	struct ccu_data *ccu = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= ccu->clk_num) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return &ccu->kona_clks[idx].hw;
}

/*
 * Set up a CCU.  Call the provided ccu_clks_setup callback to
 * initialize the array of clocks provided by the CCU.
 */
void __init kona_dt_ccu_setup(struct ccu_data *ccu,
			struct device_node *node)
{
	struct resource res = { 0 };
	resource_size_t range;
	unsigned int i;
	int ret;

	ret = of_address_to_resource(node, 0, &res);
	if (ret) {
		pr_err("%s: no valid CCU registers found for %s\n", __func__,
			node->name);
		goto out_err;
	}

	range = resource_size(&res);
	if (range > (resource_size_t)U32_MAX) {
		pr_err("%s: address range too large for %s\n", __func__,
			node->name);
		goto out_err;
	}

	ccu->range = (u32)range;

	if (!ccu_data_valid(ccu)) {
		pr_err("%s: ccu data not valid for %s\n", __func__, node->name);
		goto out_err;
	}

	ccu->base = ioremap(res.start, ccu->range);
	if (!ccu->base) {
		pr_err("%s: unable to map CCU registers for %s\n", __func__,
			node->name);
		goto out_err;
	}
	ccu->node = of_node_get(node);

	/*
	 * Set up each defined kona clock and save the result in
	 * the clock framework clock array (in ccu->data).  Then
	 * register as a provider for these clocks.
	 */
	for (i = 0; i < ccu->clk_num; i++) {
		if (!ccu->kona_clks[i].ccu)
			continue;
		kona_clk_setup(&ccu->kona_clks[i]);
	}

	ret = of_clk_add_hw_provider(node, of_clk_kona_onecell_get, ccu);
	if (ret) {
		pr_err("%s: error adding ccu %s as provider (%d)\n", __func__,
				node->name, ret);
		goto out_err;
	}

	if (!kona_ccu_init(ccu))
		pr_err("Broadcom %s initialization had errors\n", node->name);

	return;
out_err:
	kona_ccu_teardown(ccu);
	pr_err("Broadcom %s setup aborted\n", node->name);
}

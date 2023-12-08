// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2019 IBM Corp. */

/* Pieces to enable drivers to implement the .set callback */

#include "pinmux-aspeed.h"

static const char *const aspeed_pinmux_ips[] = {
	[ASPEED_IP_SCU] = "SCU",
	[ASPEED_IP_GFX] = "GFX",
	[ASPEED_IP_LPC] = "LPC",
};

static inline void aspeed_sig_desc_print_val(
		const struct aspeed_sig_desc *desc, bool enable, u32 rv)
{
	pr_debug("Want %s%X[0x%08X]=0x%X, got 0x%X from 0x%08X\n",
			aspeed_pinmux_ips[desc->ip], desc->reg,
			desc->mask, enable ? desc->enable : desc->disable,
			(rv & desc->mask) >> __ffs(desc->mask), rv);
}

/**
 * aspeed_sig_desc_eval() - Query the enabled or disabled state of a signal
 * descriptor.
 *
 * @desc: The signal descriptor of interest
 * @enabled: True to query the enabled state, false to query disabled state
 * @map: The IP block's regmap instance
 *
 * Return: 1 if the descriptor's bitfield is configured to the state
 * selected by @enabled, 0 if not, and less than zero if an unrecoverable
 * failure occurred
 *
 * Evaluation of descriptor state is non-trivial in that it is not a binary
 * outcome: The bitfields can be greater than one bit in size and thus can take
 * a value that is neither the enabled nor disabled state recorded in the
 * descriptor (typically this means a different function to the one of interest
 * is enabled). Thus we must explicitly test for either condition as required.
 */
int aspeed_sig_desc_eval(const struct aspeed_sig_desc *desc,
			 bool enabled, struct regmap *map)
{
	int ret;
	unsigned int raw;
	u32 want;

	if (!map)
		return -ENODEV;

	ret = regmap_read(map, desc->reg, &raw);
	if (ret)
		return ret;

	aspeed_sig_desc_print_val(desc, enabled, raw);
	want = enabled ? desc->enable : desc->disable;

	return ((raw & desc->mask) >> __ffs(desc->mask)) == want;
}

/**
 * aspeed_sig_expr_eval - Query the enabled or disabled state for a
 * mux function's signal on a pin
 *
 * @ctx: The driver context for the pinctrl IP
 * @expr: An expression controlling the signal for a mux function on a pin
 * @enabled: True to query the enabled state, false to query disabled state
 *
 * Return: 1 if the expression composed by @enabled evaluates true, 0 if not,
 * and less than zero if an unrecoverable failure occurred.
 *
 * A mux function is enabled or disabled if the function's signal expression
 * for each pin in the function's pin group evaluates true for the desired
 * state. An signal expression evaluates true if all of its associated signal
 * descriptors evaluate true for the desired state.
 *
 * If an expression's state is described by more than one bit, either through
 * multi-bit bitfields in a single signal descriptor or through multiple signal
 * descriptors of a single bit then it is possible for the expression to be in
 * neither the enabled nor disabled state. Thus we must explicitly test for
 * either condition as required.
 */
int aspeed_sig_expr_eval(struct aspeed_pinmux_data *ctx,
			 const struct aspeed_sig_expr *expr, bool enabled)
{
	int ret;
	int i;

	if (ctx->ops->eval)
		return ctx->ops->eval(ctx, expr, enabled);

	for (i = 0; i < expr->ndescs; i++) {
		const struct aspeed_sig_desc *desc = &expr->descs[i];

		ret = aspeed_sig_desc_eval(desc, enabled, ctx->maps[desc->ip]);
		if (ret <= 0)
			return ret;
	}

	return 1;
}

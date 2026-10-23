// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 David Yang
 */

#include <linux/iopoll.h>

#include "chip.h"
#include "smi.h"

#define YT921X_POLL_SLEEP_US	10000
#define YT921X_POLL_TIMEOUT_US	100000

int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp)
{
	lockdep_assert_held_once(&priv->reg_lock);

	return priv->reg_ops->read(priv->reg_ctx, reg, valp);
}

int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val)
{
	lockdep_assert_held_once(&priv->reg_lock);

	return priv->reg_ops->write(priv->reg_ctx, reg, val);
}

int yt921x_reg_wait(struct yt921x_priv *priv, u32 reg, u32 mask, u32 *valp)
{
	u32 val;
	int res;
	int ret;

	ret = read_poll_timeout(yt921x_reg_read, res,
				res || (val & mask) == *valp,
				YT921X_POLL_SLEEP_US, YT921X_POLL_TIMEOUT_US,
				false, priv, reg, &val);
	if (ret)
		return ret;
	if (res)
		return res;

	*valp = val;
	return 0;
}

int yt921x_reg_update_bits(struct yt921x_priv *priv, u32 reg, u32 mask, u32 val)
{
	int res;
	u32 v;
	u32 u;

	res = yt921x_reg_read(priv, reg, &v);
	if (res)
		return res;

	u = v;
	u &= ~mask;
	u |= val;
	if (u == v)
		return 0;

	return yt921x_reg_write(priv, reg, u);
}

static int
yt921x_regs_read(struct yt921x_priv *priv, u32 reg, u32 *vals,
		 unsigned int num_regs)
{
	int res;

	for (unsigned int i = 0; i < num_regs; i++) {
		res = yt921x_reg_read(priv, reg + 4 * i, &vals[i]);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_regs_write(struct yt921x_priv *priv, u32 reg, const u32 *vals,
		  unsigned int num_regs)
{
	int res;

	for (unsigned int i = 0; i < num_regs; i++) {
		res = yt921x_reg_write(priv, reg + 4 * i, vals[i]);
		if (res)
			return res;
	}

	return 0;
}

static int
yt921x_regs_update_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks,
			const u32 *vals, unsigned int num_regs)
{
	bool changed = false;
	u32 vs[4];
	int res;

	BUILD_BUG_ON(num_regs > ARRAY_SIZE(vs));

	res = yt921x_regs_read(priv, reg, vs, num_regs);
	if (res)
		return res;

	for (unsigned int i = 0; i < num_regs; i++) {
		u32 u = vs[i];

		u &= ~masks[i];
		u |= vals[i];
		if (u != vs[i])
			changed = true;

		vs[i] = u;
	}

	if (!changed)
		return 0;

	return yt921x_regs_write(priv, reg, vs, num_regs);
}

static int
yt921x_regs_clear_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks,
		       unsigned int num_regs)
{
	bool changed = false;
	u32 vs[4];
	int res;

	BUILD_BUG_ON(num_regs > ARRAY_SIZE(vs));

	res = yt921x_regs_read(priv, reg, vs, num_regs);
	if (res)
		return res;

	for (unsigned int i = 0; i < num_regs; i++) {
		u32 u = vs[i];

		u &= ~masks[i];
		if (u != vs[i])
			changed = true;

		vs[i] = u;
	}

	if (!changed)
		return 0;

	return yt921x_regs_write(priv, reg, vs, num_regs);
}

int
yt921x_reg64_write(struct yt921x_priv *priv, u32 reg, const u32 *vals)
{
	return yt921x_regs_write(priv, reg, vals, 2);
}

int
yt921x_reg64_update_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks,
			 const u32 *vals)
{
	return yt921x_regs_update_bits(priv, reg, masks, vals, 2);
}

int
yt921x_reg64_clear_bits(struct yt921x_priv *priv, u32 reg, const u32 *masks)
{
	return yt921x_regs_clear_bits(priv, reg, masks, 2);
}

int
yt921x_reg96_write(struct yt921x_priv *priv, u32 reg, const u32 *vals)
{
	return yt921x_regs_write(priv, reg, vals, 3);
}

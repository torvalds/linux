/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 David Yang
 */

#ifndef _YT_SMI_H
#define _YT_SMI_H

#include <linux/types.h>
#include <linux/wordpart.h>

struct yt921x_priv;

int yt921x_reg_read(struct yt921x_priv *priv, u32 reg, u32 *valp);
int yt921x_reg_write(struct yt921x_priv *priv, u32 reg, u32 val);
int yt921x_reg_wait(struct yt921x_priv *priv, u32 reg, u32 mask, u32 *valp);
int yt921x_reg_update_bits(struct yt921x_priv *priv, u32 reg, u32 mask,
			   u32 val);

static inline int
yt921x_reg_set_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, 0, mask);
}

static inline int
yt921x_reg_clear_bits(struct yt921x_priv *priv, u32 reg, u32 mask)
{
	return yt921x_reg_update_bits(priv, reg, mask, 0);
}

static inline int
yt921x_reg_toggle_bits(struct yt921x_priv *priv, u32 reg, u32 mask, bool set)
{
	return yt921x_reg_update_bits(priv, reg, mask, !set ? 0 : mask);
}

/* Some multi-word registers, like VLANn_CTRL, should be treated as a single
 * long register. More specifically, writes to parts of its words won't become
 * visible, until the last word is written.
 *
 * Here we require full read and write operations over these registers to
 * eliminate potential issues, although partial reads/writes are also possible.
 */

static inline void update_ctrls_unaligned(u32 *lo, u32 *hi, u64 mask, u64 val)
{
	*lo &= ~lower_32_bits(mask);
	*hi &= ~upper_32_bits(mask);
	*lo |= lower_32_bits(val);
	*hi |= upper_32_bits(val);
}

int yt921x_reg64_write(struct yt921x_priv *priv, u32 reg, const u32 *vals);
int yt921x_reg64_update_bits(struct yt921x_priv *priv, u32 reg,
			     const u32 *masks, const u32 *vals);
int yt921x_reg64_clear_bits(struct yt921x_priv *priv, u32 reg,
			    const u32 *masks);
int yt921x_reg96_write(struct yt921x_priv *priv, u32 reg, const u32 *vals);

#endif

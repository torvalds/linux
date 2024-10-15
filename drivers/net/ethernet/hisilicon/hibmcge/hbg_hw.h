/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2024 Hisilicon Limited. */

#ifndef __HBG_HW_H
#define __HBG_HW_H

#include <linux/bitfield.h>
#include <linux/io-64-nonatomic-lo-hi.h>

static inline u32 hbg_reg_read(struct hbg_priv *priv, u32 addr)
{
	return readl(priv->io_base + addr);
}

static inline void hbg_reg_write(struct hbg_priv *priv, u32 addr, u32 value)
{
	writel(value, priv->io_base + addr);
}

static inline u64 hbg_reg_read64(struct hbg_priv *priv, u32 addr)
{
	return lo_hi_readq(priv->io_base + addr);
}

static inline void hbg_reg_write64(struct hbg_priv *priv, u32 addr, u64 value)
{
	lo_hi_writeq(value, priv->io_base + addr);
}

int hbg_hw_event_notify(struct hbg_priv *priv,
			enum hbg_hw_event_type event_type);
int hbg_hw_init(struct hbg_priv *priv);

#endif

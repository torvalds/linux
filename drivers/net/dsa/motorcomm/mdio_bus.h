/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 David Yang
 */

#ifndef _YT_MDIO_BUS_H
#define _YT_MDIO_BUS_H

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/of.h>

#define YT921X_EXT_MBUS_OP		0x6a000
#define YT921X_INT_MBUS_OP		0xf0000
#define  YT921X_MBUS_OP_START			BIT(0)
#define YT921X_EXT_MBUS_CTRL		0x6a004
#define YT921X_INT_MBUS_CTRL		0xf0004
#define  YT921X_MBUS_CTRL_PORT_M		GENMASK(25, 21)
#define   YT921X_MBUS_CTRL_PORT(x)			FIELD_PREP(YT921X_MBUS_CTRL_PORT_M, (x))
#define  YT921X_MBUS_CTRL_REG_M			GENMASK(20, 16)
#define   YT921X_MBUS_CTRL_REG(x)			FIELD_PREP(YT921X_MBUS_CTRL_REG_M, (x))
#define  YT921X_MBUS_CTRL_TYPE_M		GENMASK(11, 8)  /* wild guess */
#define   YT921X_MBUS_CTRL_TYPE(x)			FIELD_PREP(YT921X_MBUS_CTRL_TYPE_M, (x))
#define   YT921X_MBUS_CTRL_TYPE_C22			YT921X_MBUS_CTRL_TYPE(4)
#define  YT921X_MBUS_CTRL_OP_M			GENMASK(3, 2)  /* wild guess */
#define   YT921X_MBUS_CTRL_OP(x)			FIELD_PREP(YT921X_MBUS_CTRL_OP_M, (x))
#define   YT921X_MBUS_CTRL_WRITE			YT921X_MBUS_CTRL_OP(1)
#define   YT921X_MBUS_CTRL_READ				YT921X_MBUS_CTRL_OP(2)
#define YT921X_EXT_MBUS_DOUT		0x6a008
#define YT921X_INT_MBUS_DOUT		0xf0008
#define YT921X_EXT_MBUS_DIN		0x6a00c
#define YT921X_INT_MBUS_DIN		0xf000c

struct yt921x_priv;

int yt921x_intif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp);
int yt921x_intif_write(struct yt921x_priv *priv, int port, int reg, u16 val);
int yt921x_intif_modify_changed(struct yt921x_priv *priv, int port, int reg,
				u16 mask, u16 val);

static inline int
yt921x_intif_modify(struct yt921x_priv *priv, int port, int reg, u16 mask,
		    u16 val)
{
	int res;

	res = yt921x_intif_modify_changed(priv, port, reg, mask, val);
	return res >= 0 ? 0 : res;
}

int yt921x_mbus_int_init(struct yt921x_priv *priv, struct device_node *mnp);
int yt921x_mbus_ext_init(struct yt921x_priv *priv, struct device_node *mnp);

#endif

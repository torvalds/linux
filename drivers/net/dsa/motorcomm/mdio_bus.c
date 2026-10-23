// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 David Yang
 */

#include <linux/of_mdio.h>

#include "chip.h"
#include "mdio_bus.h"
#include "smi.h"

static int yt921x_intif_wait(struct yt921x_priv *priv)
{
	u32 val = 0;

	return yt921x_reg_wait(priv, YT921X_INT_MBUS_OP, YT921X_MBUS_OP_START,
			       &val);
}

int yt921x_intif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	struct device *dev = yt921x_priv_to_device(priv);
	u32 mask;
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_intif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_READ;
	res = yt921x_reg_update_bits(priv, YT921X_INT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_INT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	res = yt921x_intif_wait(priv);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_INT_MBUS_DIN, &val);
	if (res)
		return res;

	if ((u16)val != val)
		dev_info(dev,
			 "%s: port %d, reg 0x%x: Expected u16, got 0x%08x\n",
			 __func__, port, reg, val);
	*valp = (u16)val;
	return 0;
}

int yt921x_intif_write(struct yt921x_priv *priv, int port, int reg, u16 val)
{
	u32 mask;
	u32 ctrl;
	int res;

	res = yt921x_intif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_WRITE;
	res = yt921x_reg_update_bits(priv, YT921X_INT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_INT_MBUS_DOUT, val);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_INT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	return yt921x_intif_wait(priv);
}

int
yt921x_intif_modify_changed(struct yt921x_priv *priv, int port, int reg,
			    u16 mask, u16 val)
{
	int res;
	u16 v;
	u16 u;

	res = yt921x_intif_read(priv, port, reg, &v);
	if (res)
		return res;

	u = v;
	u &= ~mask;
	u |= val;
	if (u == v)
		return 0;

	res = yt921x_intif_write(priv, port, reg, u);
	if (res)
		return res;

	return 1;
}

static int yt921x_mbus_int_read(struct mii_bus *mbus, int port, int reg)
{
	struct yt921x_priv *priv = mbus->priv;
	u16 val;
	int res;

	if (port >= YT921X_PORT_NUM)
		return U16_MAX;

	mutex_lock(&priv->reg_lock);
	res = yt921x_intif_read(priv, port, reg, &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;
	return val;
}

static int
yt921x_mbus_int_write(struct mii_bus *mbus, int port, int reg, u16 data)
{
	struct yt921x_priv *priv = mbus->priv;
	int res;

	if (port >= YT921X_PORT_NUM)
		return -ENODEV;

	mutex_lock(&priv->reg_lock);
	res = yt921x_intif_write(priv, port, reg, data);
	mutex_unlock(&priv->reg_lock);

	return res;
}

int yt921x_mbus_int_init(struct yt921x_priv *priv, struct device_node *mnp)
{
	struct device *dev = yt921x_priv_to_device(priv);
	struct mii_bus *mbus;
	int res;

	mbus = devm_mdiobus_alloc(dev);
	if (!mbus)
		return -ENOMEM;

	mbus->name = "YT921x internal MDIO bus";
	snprintf(mbus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	mbus->priv = priv;
	mbus->read = yt921x_mbus_int_read;
	mbus->write = yt921x_mbus_int_write;
	mbus->parent = dev;
	mbus->phy_mask = (u32)~GENMASK(YT921X_PORT_NUM - 1, 0);

	res = devm_of_mdiobus_register(dev, mbus, mnp);
	if (res)
		return res;

	priv->mbus_int = mbus;

	return 0;
}

static int yt921x_extif_wait(struct yt921x_priv *priv)
{
	u32 val = 0;

	return yt921x_reg_wait(priv, YT921X_EXT_MBUS_OP, YT921X_MBUS_OP_START,
			       &val);
}

static int
yt921x_extif_read(struct yt921x_priv *priv, int port, int reg, u16 *valp)
{
	struct device *dev = yt921x_priv_to_device(priv);
	u32 mask;
	u32 ctrl;
	u32 val;
	int res;

	res = yt921x_extif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_TYPE_M | YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_TYPE_C22 | YT921X_MBUS_CTRL_READ;
	res = yt921x_reg_update_bits(priv, YT921X_EXT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_EXT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	res = yt921x_extif_wait(priv);
	if (res)
		return res;
	res = yt921x_reg_read(priv, YT921X_EXT_MBUS_DIN, &val);
	if (res)
		return res;

	if ((u16)val != val)
		dev_info(dev,
			 "%s: port %d, reg 0x%x: Expected u16, got 0x%08x\n",
			 __func__, port, reg, val);
	*valp = (u16)val;
	return 0;
}

static int
yt921x_extif_write(struct yt921x_priv *priv, int port, int reg, u16 val)
{
	u32 mask;
	u32 ctrl;
	int res;

	res = yt921x_extif_wait(priv);
	if (res)
		return res;

	mask = YT921X_MBUS_CTRL_PORT_M | YT921X_MBUS_CTRL_REG_M |
	       YT921X_MBUS_CTRL_TYPE_M | YT921X_MBUS_CTRL_OP_M;
	ctrl = YT921X_MBUS_CTRL_PORT(port) | YT921X_MBUS_CTRL_REG(reg) |
	       YT921X_MBUS_CTRL_TYPE_C22 | YT921X_MBUS_CTRL_WRITE;
	res = yt921x_reg_update_bits(priv, YT921X_EXT_MBUS_CTRL, mask, ctrl);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_EXT_MBUS_DOUT, val);
	if (res)
		return res;
	res = yt921x_reg_write(priv, YT921X_EXT_MBUS_OP, YT921X_MBUS_OP_START);
	if (res)
		return res;

	return yt921x_extif_wait(priv);
}

static int yt921x_mbus_ext_read(struct mii_bus *mbus, int port, int reg)
{
	struct yt921x_priv *priv = mbus->priv;
	u16 val;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_extif_read(priv, port, reg, &val);
	mutex_unlock(&priv->reg_lock);

	if (res)
		return res;
	return val;
}

static int
yt921x_mbus_ext_write(struct mii_bus *mbus, int port, int reg, u16 data)
{
	struct yt921x_priv *priv = mbus->priv;
	int res;

	mutex_lock(&priv->reg_lock);
	res = yt921x_extif_write(priv, port, reg, data);
	mutex_unlock(&priv->reg_lock);

	return res;
}

int yt921x_mbus_ext_init(struct yt921x_priv *priv, struct device_node *mnp)
{
	struct device *dev = yt921x_priv_to_device(priv);
	struct mii_bus *mbus;
	int res;

	mbus = devm_mdiobus_alloc(dev);
	if (!mbus)
		return -ENOMEM;

	mbus->name = "YT921x external MDIO bus";
	snprintf(mbus->id, MII_BUS_ID_SIZE, "%s@ext", dev_name(dev));
	mbus->priv = priv;
	/* TODO: c45? */
	mbus->read = yt921x_mbus_ext_read;
	mbus->write = yt921x_mbus_ext_write;
	mbus->parent = dev;

	res = devm_of_mdiobus_register(dev, mbus, mnp);
	if (res)
		return res;

	priv->mbus_ext = mbus;

	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * X-Powers AC200 Ethernet PHY package backend
 *
 * Copyright (c) 2022 Arm Ltd. (Andre Przywara <andre.przywara@arm.com>)
 * Copyright (C) 2026 James Hilliard <james.hilliard1@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include "acx00.h"

#define AC200_EPHY_BPS_EFFUSE_OFFSET	3
#define AC200_EPHY_CLK_RATE_24_MHZ	24000000
#define AC200_EPHY_CLK_RATE_27_MHZ	27000000

#define AC200_SYS_EPHY_CTL0_REG			0x0014
#define AC200_EPHY_RESET_DEASSERT		BIT(0)
#define AC200_EPHY_SYSCLK_ENABLE			BIT(1)

#define AC200_SYS_EPHY_CTL1_REG			0x0016
#define AC200_EPHY_MII_IO_ENABLE			BIT(0)

/* AC200-internal copy of the Ethernet PHY calibration eFuse. */
#define AC200_EFUSE_EPHY_REG			0x8004

#define AC200_EPHY_CTL_REG			0x6000
#define AC200_EPHY_SHUTDOWN			BIT(0)
#define AC200_EPHY_CLK_SEL_24_MHZ		BIT(2)
#define AC200_EPHY_PHY_ADDR_MASK			GENMASK(8, 4)
#define AC200_EPHY_RMII_SEL			BIT(11)
#define AC200_EPHY_BPS_EFFUSE_MASK		GENMASK(15, 12)

struct ac200_ephy_ctl {
	struct acx00_ephy_control control;
	struct regmap *regmap;
	struct regulator *vcc;
	struct device *dev;
	u16 ephy_ctl;
	unsigned int phy_addr;
	phy_interface_t interface;
	bool supply_enabled;
	bool powered;
};

static u16 ac200_ephy_ctl_config(const struct ac200_ephy_ctl *priv)
{
	return priv->ephy_ctl |
		(priv->interface == PHY_INTERFACE_MODE_RMII ?
		 AC200_EPHY_RMII_SEL : 0) |
		FIELD_PREP(AC200_EPHY_PHY_ADDR_MASK, priv->phy_addr);
}

static int ac200_ephy_ctl_write(struct ac200_ephy_ctl *priv,
				unsigned int reg, u16 value)
{
	int ret;

	ret = regmap_write(priv->regmap, reg, value);
	if (ret)
		dev_err(priv->dev, "failed to write register %#x: %pe\n",
			reg, ERR_PTR(ret));

	return ret;
}

static int ac200_ephy_ctl_disable(struct ac200_ephy_ctl *priv)
{
	int err;
	int ret = 0;

	if (priv->powered) {
		ret = ac200_ephy_ctl_write(priv, AC200_EPHY_CTL_REG,
					   ac200_ephy_ctl_config(priv) |
					   AC200_EPHY_SHUTDOWN);
		err = ac200_ephy_ctl_write(priv, AC200_SYS_EPHY_CTL1_REG, 0);
		if (!ret)
			ret = err;
		err = ac200_ephy_ctl_write(priv, AC200_SYS_EPHY_CTL0_REG, 0);
		if (!ret)
			ret = err;

		priv->powered = false;
	}

	if (priv->supply_enabled) {
		err = regulator_disable(priv->vcc);
		if (!err)
			priv->supply_enabled = false;
		else if (!ret)
			ret = err;
	}

	return ret;
}

static int ac200_ephy_ctl_power_off(struct acx00_ephy_control *control)
{
	struct ac200_ephy_ctl *priv =
		container_of(control, struct ac200_ephy_ctl, control);

	return ac200_ephy_ctl_disable(priv);
}

static int
ac200_ephy_ctl_set_interface(struct acx00_ephy_control *control,
			     phy_interface_t interface)
{
	struct ac200_ephy_ctl *priv =
		container_of(control, struct ac200_ephy_ctl, control);
	int ret = 0;

	if (interface != PHY_INTERFACE_MODE_MII &&
	    interface != PHY_INTERFACE_MODE_RMII)
		return -EINVAL;

	if (priv->interface == interface)
		return 0;

	if (priv->powered)
		ret = regmap_update_bits(priv->regmap, AC200_EPHY_CTL_REG,
					 AC200_EPHY_RMII_SEL,
					 interface == PHY_INTERFACE_MODE_RMII ?
					 AC200_EPHY_RMII_SEL : 0);
	if (!ret)
		priv->interface = interface;

	return ret;
}

static int ac200_ephy_ctl_power_on(struct acx00_ephy_control *control,
				   unsigned int phy_addr)
{
	struct ac200_ephy_ctl *priv =
		container_of(control, struct ac200_ephy_ctl, control);
	u16 ephy_ctl;
	int ret;

	if (phy_addr > FIELD_MAX(AC200_EPHY_PHY_ADDR_MASK))
		return -EINVAL;

	if (priv->powered && priv->phy_addr == phy_addr)
		return 0;

	if (priv->powered) {
		ret = ac200_ephy_ctl_disable(priv);
		if (ret)
			return ret;
	}
	priv->phy_addr = phy_addr;

	if (!priv->supply_enabled) {
		ret = regulator_enable(priv->vcc);
		if (ret)
			return ret;
		priv->supply_enabled = true;
	}

	ephy_ctl = ac200_ephy_ctl_config(priv);

	/*
	 * Start from a disabled state before applying the configuration. AC200
	 * specifies no additional delay between these controls. The companion
	 * MFD provider has completed its input-clock startup delay before
	 * publishing the regmap.
	 */
	ret = ac200_ephy_ctl_write(priv, AC200_SYS_EPHY_CTL0_REG, 0);
	if (ret)
		goto err_disable;

	ret = ac200_ephy_ctl_write(priv, AC200_SYS_EPHY_CTL1_REG,
				   AC200_EPHY_MII_IO_ENABLE);
	if (ret)
		goto err_disable;

	ret = ac200_ephy_ctl_write(priv, AC200_EPHY_CTL_REG,
				   ephy_ctl | AC200_EPHY_SHUTDOWN);
	if (ret)
		goto err_disable;

	ret = ac200_ephy_ctl_write(priv, AC200_SYS_EPHY_CTL0_REG,
				   AC200_EPHY_RESET_DEASSERT |
				   AC200_EPHY_SYSCLK_ENABLE);
	if (ret)
		goto err_disable;

	ret = ac200_ephy_ctl_write(priv, AC200_EPHY_CTL_REG, ephy_ctl);
	if (ret)
		goto err_disable;

	priv->powered = true;
	return 0;

err_disable:
	/* Attempt every step of the shutdown sequence after a partial start. */
	priv->powered = true;
	ac200_ephy_ctl_disable(priv);

	return ret;
}

struct acx00_ephy_control *
ac200_ephy_ctl_create(struct phy_device *phydev,
		      struct device_node *package_node,
		      bool has_calibration, u8 calibration)
{
	struct device *dev = &phydev->mdio.dev;
	unsigned int internal_calibration;
	struct device_node *ac200_node;
	struct ac200_ephy_ctl *priv;
	struct i2c_client *client;
	unsigned long clk_rate;
	u8 bps_effuse_code;
	struct clk *clk;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);
	priv->dev = dev;
	priv->vcc = devm_of_regulator_get(dev, package_node, "vcc");
	if (IS_ERR(priv->vcc))
		return ERR_PTR(dev_err_probe(dev, PTR_ERR(priv->vcc),
					     "failed to get package supply\n"));

	ac200_node = of_parse_phandle(package_node, "x-powers,ac200", 0);
	if (!ac200_node)
		return ERR_PTR(dev_err_probe(dev, -EINVAL,
					     "missing x-powers,ac200 reference\n"));
	client = of_find_i2c_device_by_node(ac200_node);
	of_node_put(ac200_node);
	if (!client) {
		ret = IS_ENABLED(CONFIG_I2C) ? -EPROBE_DEFER : -ENODEV;
		return ERR_PTR(dev_err_probe(dev, ret,
					     "AC200 device is not registered\n"));
	}

	if (!device_link_add(dev, &client->dev,
			     DL_FLAG_AUTOREMOVE_CONSUMER)) {
		ret = dev_err_probe(dev, -EINVAL,
				    "failed to link AC200 device\n");
		goto out_put_client;
	}

	device_lock(&client->dev);
	if (device_is_bound(&client->dev))
		priv->regmap = dev_get_regmap(&client->dev, NULL);
	device_unlock(&client->dev);
	if (!priv->regmap) {
		ret = dev_err_probe(dev, -EPROBE_DEFER,
				    "AC200 driver is not ready\n");
		goto out_put_client;
	}

	if (!has_calibration) {
		ret = regmap_read(priv->regmap, AC200_EFUSE_EPHY_REG,
				  &internal_calibration);
		if (ret)
			goto out_error;
		calibration = internal_calibration;
	}

	/* The vendor driver supplies no transfer function beyond this offset. */
	bps_effuse_code = (calibration + AC200_EPHY_BPS_EFFUSE_OFFSET) &
			   FIELD_MAX(AC200_EPHY_BPS_EFFUSE_MASK);
	priv->ephy_ctl =
		FIELD_PREP(AC200_EPHY_BPS_EFFUSE_MASK, bps_effuse_code);
	/* EPHY_MODE and BIST_CLK_EN stay clear for normal operation. */

	clk = clk_get(&client->dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto out_error;
	}

	/* The bound AC200 provider enables and exclusively pins this rate. */
	clk_rate = clk_get_rate(clk);
	clk_put(clk);

	switch (clk_rate) {
	case AC200_EPHY_CLK_RATE_24_MHZ:
		priv->ephy_ctl |= AC200_EPHY_CLK_SEL_24_MHZ;
		break;
	case AC200_EPHY_CLK_RATE_27_MHZ:
		break;
	default:
		ret = dev_err_probe(dev, -EINVAL,
				    "unsupported AC200 clock rate %lu Hz\n",
				    clk_rate);
		goto out_put_client;
	}

	priv->control.power_on = ac200_ephy_ctl_power_on;
	priv->control.power_off = ac200_ephy_ctl_power_off;
	priv->control.set_interface = ac200_ephy_ctl_set_interface;
	/* MII is the reset default used until the MAC supplies its interface. */
	priv->interface = PHY_INTERFACE_MODE_MII;
	put_device(&client->dev);

	return &priv->control;

out_error:
	ret = dev_err_probe(dev, ret, "failed to initialize AC200 control\n");
out_put_client:
	put_device(&client->dev);
	return ERR_PTR(ret);
}

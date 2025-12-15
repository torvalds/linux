// SPDX-License-Identifier: GPL-2.0
/* Airoha AN7583 MDIO interface driver
 *
 * Copyright (C) 2025 Christian Marangi <ansuelsmth@gmail.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

/* MII address register definitions */
#define   AN7583_MII_BUSY			BIT(31)
#define   AN7583_MII_RDY			BIT(30) /* RO signal BUS is ready */
#define   AN7583_MII_CL22_REG_ADDR		GENMASK(29, 25)
#define   AN7583_MII_CL45_DEV_ADDR		AN7583_MII_CL22_REG_ADDR
#define   AN7583_MII_PHY_ADDR			GENMASK(24, 20)
#define   AN7583_MII_CMD			GENMASK(19, 18)
#define   AN7583_MII_CMD_CL22_WRITE		FIELD_PREP_CONST(AN7583_MII_CMD, 0x1)
#define   AN7583_MII_CMD_CL22_READ		FIELD_PREP_CONST(AN7583_MII_CMD, 0x2)
#define   AN7583_MII_CMD_CL45_ADDR		FIELD_PREP_CONST(AN7583_MII_CMD, 0x0)
#define   AN7583_MII_CMD_CL45_WRITE		FIELD_PREP_CONST(AN7583_MII_CMD, 0x1)
#define   AN7583_MII_CMD_CL45_POSTREAD_INCADDR	FIELD_PREP_CONST(AN7583_MII_CMD, 0x2)
#define   AN7583_MII_CMD_CL45_READ		FIELD_PREP_CONST(AN7583_MII_CMD, 0x3)
#define   AN7583_MII_ST				GENMASK(17, 16)
#define   AN7583_MII_ST_CL45			FIELD_PREP_CONST(AN7583_MII_ST, 0x0)
#define   AN7583_MII_ST_CL22			FIELD_PREP_CONST(AN7583_MII_ST, 0x1)
#define   AN7583_MII_RWDATA			GENMASK(15, 0)
#define   AN7583_MII_CL45_REG_ADDR		AN7583_MII_RWDATA

#define AN7583_MII_MDIO_DELAY_USEC		100
#define AN7583_MII_MDIO_RETRY_MSEC		100

struct airoha_mdio_data {
	u32 base_addr;
	struct regmap *regmap;
	struct clk *clk;
	struct reset_control *reset;
};

static int airoha_mdio_wait_busy(struct airoha_mdio_data *priv)
{
	u32 busy;

	return regmap_read_poll_timeout(priv->regmap, priv->base_addr, busy,
					!(busy & AN7583_MII_BUSY),
					AN7583_MII_MDIO_DELAY_USEC,
					AN7583_MII_MDIO_RETRY_MSEC * USEC_PER_MSEC);
}

static void airoha_mdio_reset(struct airoha_mdio_data *priv)
{
	/* There seems to be Hardware bug where AN7583_MII_RWDATA
	 * is not wiped in the context of unconnected PHY and the
	 * previous read value is returned.
	 *
	 * Example: (only one PHY on the BUS at 0x1f)
	 *  - read at 0x1f report at 0x2 0x7500
	 *  - read at 0x0 report 0x7500 on every address
	 *
	 * To workaround this, we reset the Mdio BUS at every read
	 * to have consistent values on read operation.
	 */
	reset_control_assert(priv->reset);
	reset_control_deassert(priv->reset);
}

static int airoha_mdio_read(struct mii_bus *bus, int addr, int regnum)
{
	struct airoha_mdio_data *priv = bus->priv;
	u32 val;
	int ret;

	airoha_mdio_reset(priv);

	val = AN7583_MII_BUSY | AN7583_MII_ST_CL22 |
	      AN7583_MII_CMD_CL22_READ;
	val |= FIELD_PREP(AN7583_MII_PHY_ADDR, addr);
	val |= FIELD_PREP(AN7583_MII_CL22_REG_ADDR, regnum);

	ret = regmap_write(priv->regmap, priv->base_addr, val);
	if (ret)
		return ret;

	ret = airoha_mdio_wait_busy(priv);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, priv->base_addr, &val);
	if (ret)
		return ret;

	return FIELD_GET(AN7583_MII_RWDATA, val);
}

static int airoha_mdio_write(struct mii_bus *bus, int addr, int regnum,
			     u16 value)
{
	struct airoha_mdio_data *priv = bus->priv;
	u32 val;
	int ret;

	val = AN7583_MII_BUSY | AN7583_MII_ST_CL22 |
	      AN7583_MII_CMD_CL22_WRITE;
	val |= FIELD_PREP(AN7583_MII_PHY_ADDR, addr);
	val |= FIELD_PREP(AN7583_MII_CL22_REG_ADDR, regnum);
	val |= FIELD_PREP(AN7583_MII_RWDATA, value);

	ret = regmap_write(priv->regmap, priv->base_addr, val);
	if (ret)
		return ret;

	ret = airoha_mdio_wait_busy(priv);

	return ret;
}

static int airoha_mdio_cl45_read(struct mii_bus *bus, int addr, int devnum,
				 int regnum)
{
	struct airoha_mdio_data *priv = bus->priv;
	u32 val;
	int ret;

	airoha_mdio_reset(priv);

	val = AN7583_MII_BUSY | AN7583_MII_ST_CL45 |
	      AN7583_MII_CMD_CL45_ADDR;
	val |= FIELD_PREP(AN7583_MII_PHY_ADDR, addr);
	val |= FIELD_PREP(AN7583_MII_CL45_DEV_ADDR, devnum);
	val |= FIELD_PREP(AN7583_MII_CL45_REG_ADDR, regnum);

	ret = regmap_write(priv->regmap, priv->base_addr, val);
	if (ret)
		return ret;

	ret = airoha_mdio_wait_busy(priv);
	if (ret)
		return ret;

	val = AN7583_MII_BUSY | AN7583_MII_ST_CL45 |
	      AN7583_MII_CMD_CL45_READ;
	val |= FIELD_PREP(AN7583_MII_PHY_ADDR, addr);
	val |= FIELD_PREP(AN7583_MII_CL45_DEV_ADDR, devnum);

	ret = regmap_write(priv->regmap, priv->base_addr, val);
	if (ret)
		return ret;

	ret = airoha_mdio_wait_busy(priv);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, priv->base_addr, &val);
	if (ret)
		return ret;

	return FIELD_GET(AN7583_MII_RWDATA, val);
}

static int airoha_mdio_cl45_write(struct mii_bus *bus, int addr, int devnum,
				  int regnum, u16 value)
{
	struct airoha_mdio_data *priv = bus->priv;
	u32 val;
	int ret;

	val = AN7583_MII_BUSY | AN7583_MII_ST_CL45 |
	      AN7583_MII_CMD_CL45_ADDR;
	val |= FIELD_PREP(AN7583_MII_PHY_ADDR, addr);
	val |= FIELD_PREP(AN7583_MII_CL45_DEV_ADDR, devnum);
	val |= FIELD_PREP(AN7583_MII_CL45_REG_ADDR, regnum);

	ret = regmap_write(priv->regmap, priv->base_addr, val);
	if (ret)
		return ret;

	ret = airoha_mdio_wait_busy(priv);
	if (ret)
		return ret;

	val = AN7583_MII_BUSY | AN7583_MII_ST_CL45 |
	      AN7583_MII_CMD_CL45_WRITE;
	val |= FIELD_PREP(AN7583_MII_PHY_ADDR, addr);
	val |= FIELD_PREP(AN7583_MII_CL45_DEV_ADDR, devnum);
	val |= FIELD_PREP(AN7583_MII_RWDATA, value);

	ret = regmap_write(priv->regmap, priv->base_addr, val);
	if (ret)
		return ret;

	ret = airoha_mdio_wait_busy(priv);

	return ret;
}

static int airoha_mdio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct airoha_mdio_data *priv;
	struct mii_bus *bus;
	u32 addr, freq;
	int ret;

	ret = of_property_read_u32(dev->of_node, "reg", &addr);
	if (ret)
		return ret;

	bus = devm_mdiobus_alloc_size(dev, sizeof(*priv));
	if (!bus)
		return -ENOMEM;

	priv = bus->priv;
	priv->base_addr = addr;
	priv->regmap = device_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

	priv->reset = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(priv->reset))
		return PTR_ERR(priv->reset);

	reset_control_deassert(priv->reset);

	bus->name = "airoha_mdio_bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii", dev_name(dev));
	bus->parent = dev;
	bus->read = airoha_mdio_read;
	bus->write = airoha_mdio_write;
	bus->read_c45 = airoha_mdio_cl45_read;
	bus->write_c45 = airoha_mdio_cl45_write;

	/* Check if a custom frequency is defined in DT or default to 2.5 MHz */
	if (of_property_read_u32(dev->of_node, "clock-frequency", &freq))
		freq = 2500000;

	ret = clk_set_rate(priv->clk, freq);
	if (ret)
		return ret;

	ret = devm_of_mdiobus_register(dev, bus, dev->of_node);
	if (ret) {
		reset_control_assert(priv->reset);
		return ret;
	}

	return 0;
}

static const struct of_device_id airoha_mdio_dt_ids[] = {
	{ .compatible = "airoha,an7583-mdio" },
	{ }
};
MODULE_DEVICE_TABLE(of, airoha_mdio_dt_ids);

static struct platform_driver airoha_mdio_driver = {
	.probe = airoha_mdio_probe,
	.driver = {
		.name = "airoha-mdio",
		.of_match_table = airoha_mdio_dt_ids,
	},
};

module_platform_driver(airoha_mdio_driver);

MODULE_DESCRIPTION("Airoha AN7583 MDIO interface driver");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");

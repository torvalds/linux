// SPDX-License-Identifier: GPL-2.0-only
/*
 * MDIO controller for RTL9300 switches with integrated SoC.
 *
 * The MDIO communication is abstracted by the switch. At the software level
 * communication uses the switch port to address the PHY. We work out the
 * mapping based on the MDIO bus described in device tree and phandles on the
 * ethernet-ports property.
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/find.h>
#include <linux/mdio.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define SMI_GLB_CTRL			0xca00
#define   GLB_CTRL_INTF_SEL(intf)	BIT(16 + (intf))
#define SMI_PORT0_15_POLLING_SEL	0xca08
#define SMI_ACCESS_PHY_CTRL_0		0xcb70
#define SMI_ACCESS_PHY_CTRL_1		0xcb74
#define   PHY_CTRL_REG_ADDR		GENMASK(24, 20)
#define   PHY_CTRL_PARK_PAGE		GENMASK(19, 15)
#define   PHY_CTRL_MAIN_PAGE		GENMASK(14, 3)
#define   PHY_CTRL_WRITE		BIT(2)
#define   PHY_CTRL_READ			0
#define   PHY_CTRL_TYPE_C45		BIT(1)
#define   PHY_CTRL_TYPE_C22		0
#define   PHY_CTRL_CMD			BIT(0)
#define   PHY_CTRL_FAIL			BIT(25)
#define SMI_ACCESS_PHY_CTRL_2		0xcb78
#define   PHY_CTRL_INDATA		GENMASK(31, 16)
#define   PHY_CTRL_DATA			GENMASK(15, 0)
#define SMI_ACCESS_PHY_CTRL_3		0xcb7c
#define   PHY_CTRL_MMD_DEVAD		GENMASK(20, 16)
#define   PHY_CTRL_MMD_REG		GENMASK(15, 0)
#define SMI_PORT0_5_ADDR_CTRL		0xcb80

#define MAX_PORTS       28
#define MAX_SMI_BUSSES  4
#define MAX_SMI_ADDR	0x1f

struct rtl9300_mdio_priv {
	struct regmap *regmap;
	struct mutex lock; /* protect HW access */
	DECLARE_BITMAP(valid_ports, MAX_PORTS);
	u8 smi_bus[MAX_PORTS];
	u8 smi_addr[MAX_PORTS];
	bool smi_bus_is_c45[MAX_SMI_BUSSES];
	struct mii_bus *bus[MAX_SMI_BUSSES];
};

struct rtl9300_mdio_chan {
	struct rtl9300_mdio_priv *priv;
	u8 mdio_bus;
};

static int rtl9300_mdio_phy_to_port(struct mii_bus *bus, int phy_id)
{
	struct rtl9300_mdio_chan *chan = bus->priv;
	struct rtl9300_mdio_priv *priv;
	int i;

	priv = chan->priv;

	for_each_set_bit(i, priv->valid_ports, MAX_PORTS)
		if (priv->smi_bus[i] == chan->mdio_bus &&
		    priv->smi_addr[i] == phy_id)
			return i;

	return -ENOENT;
}

static int rtl9300_mdio_wait_ready(struct rtl9300_mdio_priv *priv)
{
	struct regmap *regmap = priv->regmap;
	u32 val;

	lockdep_assert_held(&priv->lock);

	return regmap_read_poll_timeout(regmap, SMI_ACCESS_PHY_CTRL_1,
					val, !(val & PHY_CTRL_CMD), 10, 1000);
}

static int rtl9300_mdio_read_c22(struct mii_bus *bus, int phy_id, int regnum)
{
	struct rtl9300_mdio_chan *chan = bus->priv;
	struct rtl9300_mdio_priv *priv;
	struct regmap *regmap;
	int port;
	u32 val;
	int err;

	priv = chan->priv;
	regmap = priv->regmap;

	port = rtl9300_mdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	mutex_lock(&priv->lock);
	err = rtl9300_mdio_wait_ready(priv);
	if (err)
		goto out_err;

	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_2, FIELD_PREP(PHY_CTRL_INDATA, port));
	if (err)
		goto out_err;

	val = FIELD_PREP(PHY_CTRL_REG_ADDR, regnum) |
	      FIELD_PREP(PHY_CTRL_PARK_PAGE, 0x1f) |
	      FIELD_PREP(PHY_CTRL_MAIN_PAGE, 0xfff) |
	      PHY_CTRL_READ | PHY_CTRL_TYPE_C22 | PHY_CTRL_CMD;
	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_1, val);
	if (err)
		goto out_err;

	err = rtl9300_mdio_wait_ready(priv);
	if (err)
		goto out_err;

	err = regmap_read(regmap, SMI_ACCESS_PHY_CTRL_2, &val);
	if (err)
		goto out_err;

	mutex_unlock(&priv->lock);
	return FIELD_GET(PHY_CTRL_DATA, val);

out_err:
	mutex_unlock(&priv->lock);
	return err;
}

static int rtl9300_mdio_write_c22(struct mii_bus *bus, int phy_id, int regnum, u16 value)
{
	struct rtl9300_mdio_chan *chan = bus->priv;
	struct rtl9300_mdio_priv *priv;
	struct regmap *regmap;
	int port;
	u32 val;
	int err;

	priv = chan->priv;
	regmap = priv->regmap;

	port = rtl9300_mdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	mutex_lock(&priv->lock);
	err = rtl9300_mdio_wait_ready(priv);
	if (err)
		goto out_err;

	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_0, BIT(port));
	if (err)
		goto out_err;

	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_2, FIELD_PREP(PHY_CTRL_INDATA, value));
	if (err)
		goto out_err;

	val = FIELD_PREP(PHY_CTRL_REG_ADDR, regnum) |
	      FIELD_PREP(PHY_CTRL_PARK_PAGE, 0x1f) |
	      FIELD_PREP(PHY_CTRL_MAIN_PAGE, 0xfff) |
	      PHY_CTRL_WRITE | PHY_CTRL_TYPE_C22 | PHY_CTRL_CMD;
	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_1, val);
	if (err)
		goto out_err;

	err = regmap_read_poll_timeout(regmap, SMI_ACCESS_PHY_CTRL_1,
				       val, !(val & PHY_CTRL_CMD), 10, 100);
	if (err)
		goto out_err;

	if (val & PHY_CTRL_FAIL) {
		err = -ENXIO;
		goto out_err;
	}

	mutex_unlock(&priv->lock);
	return 0;

out_err:
	mutex_unlock(&priv->lock);
	return err;
}

static int rtl9300_mdio_read_c45(struct mii_bus *bus, int phy_id, int dev_addr, int regnum)
{
	struct rtl9300_mdio_chan *chan = bus->priv;
	struct rtl9300_mdio_priv *priv;
	struct regmap *regmap;
	int port;
	u32 val;
	int err;

	priv = chan->priv;
	regmap = priv->regmap;

	port = rtl9300_mdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	mutex_lock(&priv->lock);
	err = rtl9300_mdio_wait_ready(priv);
	if (err)
		goto out_err;

	val = FIELD_PREP(PHY_CTRL_INDATA, port);
	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_2, val);
	if (err)
		goto out_err;

	val = FIELD_PREP(PHY_CTRL_MMD_DEVAD, dev_addr) |
	      FIELD_PREP(PHY_CTRL_MMD_REG, regnum);
	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_3, val);
	if (err)
		goto out_err;

	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_1,
			   PHY_CTRL_READ | PHY_CTRL_TYPE_C45 | PHY_CTRL_CMD);
	if (err)
		goto out_err;

	err = rtl9300_mdio_wait_ready(priv);
	if (err)
		goto out_err;

	err = regmap_read(regmap, SMI_ACCESS_PHY_CTRL_2, &val);
	if (err)
		goto out_err;

	mutex_unlock(&priv->lock);
	return FIELD_GET(PHY_CTRL_DATA, val);

out_err:
	mutex_unlock(&priv->lock);
	return err;
}

static int rtl9300_mdio_write_c45(struct mii_bus *bus, int phy_id, int dev_addr,
				  int regnum, u16 value)
{
	struct rtl9300_mdio_chan *chan = bus->priv;
	struct rtl9300_mdio_priv *priv;
	struct regmap *regmap;
	int port;
	u32 val;
	int err;

	priv = chan->priv;
	regmap = priv->regmap;

	port = rtl9300_mdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	mutex_lock(&priv->lock);
	err = rtl9300_mdio_wait_ready(priv);
	if (err)
		goto out_err;

	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_0, BIT(port));
	if (err)
		goto out_err;

	val = FIELD_PREP(PHY_CTRL_INDATA, value);
	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_2, val);
	if (err)
		goto out_err;

	val = FIELD_PREP(PHY_CTRL_MMD_DEVAD, dev_addr) |
	      FIELD_PREP(PHY_CTRL_MMD_REG, regnum);
	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_3, val);
	if (err)
		goto out_err;

	err = regmap_write(regmap, SMI_ACCESS_PHY_CTRL_1,
			   PHY_CTRL_TYPE_C45 | PHY_CTRL_WRITE | PHY_CTRL_CMD);
	if (err)
		goto out_err;

	err = regmap_read_poll_timeout(regmap, SMI_ACCESS_PHY_CTRL_1,
				       val, !(val & PHY_CTRL_CMD), 10, 100);
	if (err)
		goto out_err;

	if (val & PHY_CTRL_FAIL) {
		err = -ENXIO;
		goto out_err;
	}

	mutex_unlock(&priv->lock);
	return 0;

out_err:
	mutex_unlock(&priv->lock);
	return err;
}

static int rtl9300_mdiobus_init(struct rtl9300_mdio_priv *priv)
{
	u32 glb_ctrl_mask = 0, glb_ctrl_val = 0;
	struct regmap *regmap = priv->regmap;
	u32 port_addr[5] = { 0 };
	u32 poll_sel[2] = { 0 };
	int i, err;

	/* Associate the port with the SMI interface and PHY */
	for_each_set_bit(i, priv->valid_ports, MAX_PORTS) {
		int pos;

		pos = (i % 6) * 5;
		port_addr[i / 6] |= (priv->smi_addr[i] & 0x1f) << pos;

		pos = (i % 16) * 2;
		poll_sel[i / 16] |= (priv->smi_bus[i] & 0x3) << pos;
	}

	/* Put the interfaces into C45 mode if required */
	glb_ctrl_mask = GENMASK(19, 16);
	for (i = 0; i < MAX_SMI_BUSSES; i++)
		if (priv->smi_bus_is_c45[i])
			glb_ctrl_val |= GLB_CTRL_INTF_SEL(i);

	err = regmap_bulk_write(regmap, SMI_PORT0_5_ADDR_CTRL,
				port_addr, 5);
	if (err)
		return err;

	err = regmap_bulk_write(regmap, SMI_PORT0_15_POLLING_SEL,
				poll_sel, 2);
	if (err)
		return err;

	err = regmap_update_bits(regmap, SMI_GLB_CTRL,
				 glb_ctrl_mask, glb_ctrl_val);
	if (err)
		return err;

	return 0;
}

static int rtl9300_mdiobus_probe_one(struct device *dev, struct rtl9300_mdio_priv *priv,
				     struct fwnode_handle *node)
{
	struct rtl9300_mdio_chan *chan;
	struct fwnode_handle *child;
	struct mii_bus *bus;
	u32 mdio_bus;
	int err;

	err = fwnode_property_read_u32(node, "reg", &mdio_bus);
	if (err)
		return err;

	/* The MDIO accesses from the kernel work with the PHY polling unit in
	 * the switch. We need to tell the PPU to operate either in GPHY (i.e.
	 * clause 22) or 10GPHY mode (i.e. clause 45).
	 *
	 * We select 10GPHY mode if there is at least one PHY that declares
	 * compatible = "ethernet-phy-ieee802.3-c45". This does mean we can't
	 * support both c45 and c22 on the same MDIO bus.
	 */
	fwnode_for_each_child_node(node, child)
		if (fwnode_device_is_compatible(child, "ethernet-phy-ieee802.3-c45"))
			priv->smi_bus_is_c45[mdio_bus] = true;

	bus = devm_mdiobus_alloc_size(dev, sizeof(*chan));
	if (!bus)
		return -ENOMEM;

	bus->name = "Realtek Switch MDIO Bus";
	if (priv->smi_bus_is_c45[mdio_bus]) {
		bus->read_c45 = rtl9300_mdio_read_c45;
		bus->write_c45 =  rtl9300_mdio_write_c45;
	} else {
		bus->read = rtl9300_mdio_read_c22;
		bus->write = rtl9300_mdio_write_c22;
	}
	bus->parent = dev;
	chan = bus->priv;
	chan->mdio_bus = mdio_bus;
	chan->priv = priv;

	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-%d", dev_name(dev), mdio_bus);

	err = devm_of_mdiobus_register(dev, bus, to_of_node(node));
	if (err)
		return dev_err_probe(dev, err, "cannot register MDIO bus\n");

	return 0;
}

/* The mdio-controller is part of a switch block so we parse the sibling
 * ethernet-ports node and build a mapping of the switch port to MDIO bus/addr
 * based on the phy-handle.
 */
static int rtl9300_mdiobus_map_ports(struct device *dev)
{
	struct rtl9300_mdio_priv *priv = dev_get_drvdata(dev);
	struct device *parent = dev->parent;
	struct fwnode_handle *port;
	int err;

	struct fwnode_handle *ports __free(fwnode_handle) =
		device_get_named_child_node(parent, "ethernet-ports");
	if (!ports)
		return dev_err_probe(dev, -EINVAL, "%pfwP missing ethernet-ports\n",
				     dev_fwnode(parent));

	fwnode_for_each_child_node(ports, port) {
		struct device_node *mdio_dn;
		u32 addr;
		u32 bus;
		u32 pn;

		struct device_node *phy_dn __free(device_node) =
			of_parse_phandle(to_of_node(port), "phy-handle", 0);
		/* skip ports without phys */
		if (!phy_dn)
			continue;

		mdio_dn = phy_dn->parent;
		/* only map ports that are connected to this mdio-controller */
		if (mdio_dn->parent != dev->of_node)
			continue;

		err = fwnode_property_read_u32(port, "reg", &pn);
		if (err)
			return err;

		if (pn >= MAX_PORTS)
			return dev_err_probe(dev, -EINVAL, "illegal port number %d\n", pn);

		if (test_bit(pn, priv->valid_ports))
			return dev_err_probe(dev, -EINVAL, "duplicated port number %d\n", pn);

		err = of_property_read_u32(mdio_dn, "reg", &bus);
		if (err)
			return err;

		if (bus >= MAX_SMI_BUSSES)
			return dev_err_probe(dev, -EINVAL, "illegal smi bus number %d\n", bus);

		err = of_property_read_u32(phy_dn, "reg", &addr);
		if (err)
			return err;

		__set_bit(pn, priv->valid_ports);
		priv->smi_bus[pn] = bus;
		priv->smi_addr[pn] = addr;
	}

	return 0;
}

static int rtl9300_mdiobus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rtl9300_mdio_priv *priv;
	struct fwnode_handle *child;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	err = devm_mutex_init(dev, &priv->lock);
	if (err)
		return err;

	priv->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	platform_set_drvdata(pdev, priv);

	err = rtl9300_mdiobus_map_ports(dev);
	if (err)
		return err;

	device_for_each_child_node(dev, child) {
		err = rtl9300_mdiobus_probe_one(dev, priv, child);
		if (err)
			return err;
	}

	err = rtl9300_mdiobus_init(priv);
	if (err)
		return dev_err_probe(dev, err, "failed to initialise MDIO bus controller\n");

	return 0;
}

static const struct of_device_id rtl9300_mdio_ids[] = {
	{ .compatible = "realtek,rtl9301-mdio" },
	{}
};
MODULE_DEVICE_TABLE(of, rtl9300_mdio_ids);

static struct platform_driver rtl9300_mdio_driver = {
	.probe = rtl9300_mdiobus_probe,
	.driver = {
		.name = "mdio-rtl9300",
		.of_match_table = rtl9300_mdio_ids,
	},
};

module_platform_driver(rtl9300_mdio_driver);

MODULE_DESCRIPTION("RTL9300 MDIO driver");
MODULE_LICENSE("GPL");

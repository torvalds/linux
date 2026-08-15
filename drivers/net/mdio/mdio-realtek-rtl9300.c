// SPDX-License-Identifier: GPL-2.0-only
/*
 * Realtek switches of the Otto series (RTL838x, RTL839x, RTL930x and RTL931x SoCs) have multiple
 * integrated MDIO controllers. This driver targets the ethernet MDIO controller. It serves only
 * 1G/2.5G/10G ethernet PHYs attached to up to 4 individual buses.
 *
 * The controller is programmed through MMIO. The MDIO communication is abstracted by the hardware
 * and uses the switch port number for its addressing. For this to work, mapping registers need to
 * be setup in advance. With that the controller translates each port based I/O operation into the
 * physical bus and address. This gives the following end-to-end communication
 *
 *     +----------+       +----------+           +----------+       +----------+
 *     |  phydev  |  ...  |  phydev  |           |  phydev  |  ...  |  phydev  |
 *     +----------+       +----------+           +----------+       +----------+
 *              |                  |               |                  |
 *   mii_bus 0  +------------------+               +------------------+  mii_bus 1
 *                                 |               |
 *           +-----------------------------------------------------+
 *           |  MDIO driver                                        |
 *           |                      translate bus/address -> port  |
 *           +-----------------------------------------------------+
 *                                        |                             Software
 *                             - - - - - - - - - - - - - - - - - - - - - - - - -
 *                                        |                             Hardware
 *           +-----------------------------------------------------+
 *           | MDIO controller                                     |
 *           |                      translate port -> bus/address  |
 *           +-----------------------------------------------------+
 *                                 |               |
 *       bus 0  +------------------+               +------------------+  bus 1
 *              |                  |               |                  |
 *     +----------+       +----------+           +----------+       +----------+
 *     | PHY 0/1  |  ...  | PHY 0/31 |           | PHY 1/1  |  ...  | PHY 1/31 |
 *     +----------+       +----------+           +----------+       +----------+
 *
 * The driver works out the mapping based on the MDIO bus described in device tree and phandles on
 * the ethernet-ports property.
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/find.h>
#include <linux/mdio.h>
#include <linux/mfd/syscon.h>
#include <linux/mutex.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#define RTL9300_NUM_BUSES			4
#define RTL9300_NUM_PAGES			4096
#define RTL9300_NUM_PORTS			28
#define RTL9300_SMI_GLB_CTRL			0xca00
#define   RTL9300_GLB_CTRL_INTF_SEL(intf)	BIT(16 + (intf))
#define RTL9300_SMI_PORT0_15_POLLING_SEL	0xca08
#define RTL9300_SMI_ACCESS_PHY_CTRL_0		0xcb70
#define RTL9300_SMI_ACCESS_PHY_CTRL_1		0xcb74
#define   RTL9300_PHY_CTRL_REG_ADDR		GENMASK(24, 20)
#define   RTL9300_PHY_CTRL_PARK_PAGE		GENMASK(19, 15)
#define   RTL9300_PHY_CTRL_MAIN_PAGE		GENMASK(14, 3)
#define   RTL9300_PHY_CTRL_WRITE		BIT(2)
#define   RTL9300_PHY_CTRL_READ			0
#define   RTL9300_PHY_CTRL_TYPE_C45		BIT(1)
#define   RTL9300_PHY_CTRL_TYPE_C22		0
#define   RTL9300_PHY_CTRL_FAIL			BIT(25)
#define RTL9300_SMI_ACCESS_PHY_CTRL_2		0xcb78
#define   RTL9300_PHY_CTRL_INDATA		GENMASK(31, 16)
#define   RTL9300_PHY_CTRL_DATA			GENMASK(15, 0)
#define RTL9300_SMI_ACCESS_PHY_CTRL_3		0xcb7c
#define RTL9300_SMI_PORT0_5_ADDR_CTRL		0xcb80

#define RTL9310_NUM_BUSES			4
#define RTL9310_NUM_PAGES			8192
#define RTL9310_NUM_PORTS			56
#define RTL9310_SMI_GLB_CTRL1			0x0cbc
#define   RTL9310_SMI_GLB_FMT_SEL_C45(intf)	BIT((intf) * 2 + 1)
#define RTL9310_SMI_INDRT_ACCESS_CTRL_0		0x0c00
#define   RTL9310_PHY_CTRL_REG_ADDR		GENMASK(10, 6)
#define   RTL9310_PHY_CTRL_MAIN_PAGE		GENMASK(23, 11)
#define   RTL9310_PHY_CTRL_READ			0
#define   RTL9310_PHY_CTRL_WRITE		BIT(4)
#define   RTL9310_PHY_CTRL_TYPE_C45		BIT(3)
#define   RTL9310_PHY_CTRL_TYPE_C22		0
#define   RTL9310_PHY_CTRL_FAIL			BIT(1)
#define RTL9310_SMI_INDRT_ACCESS_BC_PHYID_CTRL	0x0c14
#define   RTL9310_BC_PORT_ID			GENMASK(10, 5)
#define RTL9310_SMI_INDRT_ACCESS_CTRL_1		0x0c04
#define RTL9310_SMI_INDRT_ACCESS_CTRL_2_LOW	0x0c08
#define RTL9310_SMI_INDRT_ACCESS_CTRL_2_HIGH	0x0c0c
#define RTL9310_SMI_INDRT_ACCESS_CTRL_3		0x0c10 /* I/O fields flipped */
#define   RTL9310_PHY_CTRL_DATA			GENMASK(31, 16)
#define   RTL9310_PHY_CTRL_INDATA		GENMASK(15, 0)
#define RTL9310_SMI_INDRT_ACCESS_MMD_CTRL	0x0c18
#define RTL9310_SMI_PORT_ADDR_CTRL		0x0c74
#define RTL9310_SMI_PORT_POLLING_SEL		0x0c9c

#define PHY_CTRL_CMD				BIT(0)
#define PHY_CTRL_MMD_DEVAD			GENMASK(20, 16)
#define PHY_CTRL_MMD_REG			GENMASK(15, 0)

#define MAP_ADDRS_PER_REG			6
#define MAP_BITS_PER_ADDR			5
#define MAP_BITS_PER_BUS			2
#define MAP_BUSES_PER_REG			16
#define MAX_PORTS				56
#define MAX_SMI_BUSSES				4
#define RAW_PAGE(priv)				((priv)->info->num_pages - 1)


struct otto_emdio_cmd_regs {
	u32 c22_data;
	u32 c45_data;
	u32 io_data;
	u32 port_mask_low;
	/* additional registers for high port count models RTL839x/RTL931x */
	u32 port_mask_high;
	u32 broadcast;
	u32 ext_page;
};

struct otto_emdio_priv {
	const struct otto_emdio_info *info;
	struct regmap *regmap;
	struct mutex lock; /* protect HW access */
	DECLARE_BITMAP(valid_ports, MAX_PORTS);
	u8 smi_bus[MAX_PORTS];
	u8 smi_addr[MAX_PORTS];
	bool smi_bus_is_c45[MAX_SMI_BUSSES];
	struct mii_bus *bus[MAX_SMI_BUSSES];
};

struct otto_emdio_info {
	u32 addr_map_base;
	u32 bus_map_base;
	u32 cmd_fail;
	u32 cmd_read;
	u32 cmd_write;
	struct otto_emdio_cmd_regs cmd_regs;
	u8 num_buses;
	u8 num_ports;
	u16 num_pages;
	int (*setup_controller)(struct otto_emdio_priv *priv);
	int (*read_c22)(struct mii_bus *bus, int port, int regnum, u32 *value);
	int (*read_c45)(struct mii_bus *bus, int port, int dev_addr, int regnum, u32 *value);
	int (*write_c22)(struct mii_bus *bus, int port, int regnum, u16 value);
	int (*write_c45)(struct mii_bus *bus, int port, int dev_addr, int regnum, u16 value);
};

struct otto_emdio_chan {
	struct otto_emdio_priv *priv;
	u8 mdio_bus;
};

static int otto_emdio_phy_to_port(struct mii_bus *bus, int phy_id)
{
	struct otto_emdio_chan *chan = bus->priv;
	struct otto_emdio_priv *priv;
	int i;

	priv = chan->priv;

	for_each_set_bit(i, priv->valid_ports, priv->info->num_ports)
		if (priv->smi_bus[i] == chan->mdio_bus &&
		    priv->smi_addr[i] == phy_id)
			return i;

	return -ENOENT;
}

static struct otto_emdio_priv *otto_emdio_bus_to_priv(struct mii_bus *bus)
{
	struct otto_emdio_chan *chan = bus->priv;

	return chan->priv;
}

static int otto_emdio_run_cmd(struct mii_bus *bus, u32 cmd,
			      struct otto_emdio_cmd_regs *cmd_data)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	const struct otto_emdio_info *info = priv->info;
	u32 cmdstate;
	int ret;

	/* Defensive pre check just in case something goes horrible wrong */
	ret = regmap_read_poll_timeout(priv->regmap, info->cmd_regs.c22_data,
				       cmdstate, !(cmdstate & PHY_CTRL_CMD), 10, 1000);
	if (ret)
		return ret;

	/* Fill all registers. Hardware will read only the needed bits depending on command */
	if (info->cmd_regs.port_mask_high) {
		/* Fill extra registers for high port count models */
		ret = regmap_write(priv->regmap, info->cmd_regs.broadcast, cmd_data->broadcast);
		if (ret)
			return ret;

		ret = regmap_write(priv->regmap, info->cmd_regs.ext_page, cmd_data->ext_page);
		if (ret)
			return ret;

		ret = regmap_write(priv->regmap,
				   info->cmd_regs.port_mask_high, cmd_data->port_mask_high);
		if (ret)
			return ret;
	}

	ret = regmap_write(priv->regmap, info->cmd_regs.port_mask_low, cmd_data->port_mask_low);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, info->cmd_regs.io_data, cmd_data->io_data);
	if (ret)
		return ret;

	ret = regmap_write(priv->regmap, info->cmd_regs.c45_data, cmd_data->c45_data);
	if (ret)
		return ret;

	/* C22 data and command bits share the same register. */
	ret = regmap_write(priv->regmap, info->cmd_regs.c22_data,
			   cmd_data->c22_data | cmd | PHY_CTRL_CMD);
	if (ret)
		return ret;

	ret = regmap_read_poll_timeout(priv->regmap, info->cmd_regs.c22_data,
				       cmdstate, !(cmdstate & PHY_CTRL_CMD), 10, 1000);
	if (ret)
		return ret;

	return cmdstate & info->cmd_fail ? -ENXIO : 0;
}

static int otto_emdio_read_cmd(struct mii_bus *bus, u32 cmd,
			       struct otto_emdio_cmd_regs *cmd_data, u32 mask, u32 *value)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	int ret;

	lockdep_assert_held(&priv->lock);
	ret = otto_emdio_run_cmd(bus, cmd | priv->info->cmd_read, cmd_data);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, priv->info->cmd_regs.io_data, value);
	if (ret)
		return ret;

	*value = field_get(mask, *value);

	return 0;
}

static int otto_emdio_write_cmd(struct mii_bus *bus, u32 cmd,
				struct otto_emdio_cmd_regs *cmd_data)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);

	lockdep_assert_held(&priv->lock);

	return otto_emdio_run_cmd(bus, cmd | priv->info->cmd_write, cmd_data);
}

static int otto_emdio_9300_read_c22(struct mii_bus *bus, int port, int regnum, u32 *value)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	struct otto_emdio_cmd_regs cmd_data = {
		.c22_data	= FIELD_PREP(RTL9300_PHY_CTRL_REG_ADDR, regnum) |
				  FIELD_PREP(RTL9300_PHY_CTRL_PARK_PAGE, 0x1f) |
				  FIELD_PREP(RTL9300_PHY_CTRL_MAIN_PAGE, RAW_PAGE(priv)),
		.io_data	= FIELD_PREP(RTL9300_PHY_CTRL_INDATA, port),
	};

	return otto_emdio_read_cmd(bus, RTL9300_PHY_CTRL_TYPE_C22, &cmd_data,
				   RTL9300_PHY_CTRL_DATA, value);
}

static int otto_emdio_9300_write_c22(struct mii_bus *bus, int port, int regnum, u16 value)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	struct otto_emdio_cmd_regs cmd_data = {
		.c22_data	= FIELD_PREP(RTL9300_PHY_CTRL_REG_ADDR, regnum) |
				  FIELD_PREP(RTL9300_PHY_CTRL_PARK_PAGE, 0x1f) |
				  FIELD_PREP(RTL9300_PHY_CTRL_MAIN_PAGE, RAW_PAGE(priv)),
		.io_data	= FIELD_PREP(RTL9300_PHY_CTRL_INDATA, value),
		.port_mask_low	= BIT(port),
	};

	return otto_emdio_write_cmd(bus, RTL9300_PHY_CTRL_TYPE_C22, &cmd_data);
}

static int otto_emdio_9300_read_c45(struct mii_bus *bus, int port,
				    int dev_addr, int regnum, u32 *value)
{
	struct otto_emdio_cmd_regs cmd_data = {
		.c45_data	= FIELD_PREP(PHY_CTRL_MMD_DEVAD, dev_addr) |
				  FIELD_PREP(PHY_CTRL_MMD_REG, regnum),
		.io_data	= FIELD_PREP(RTL9300_PHY_CTRL_INDATA, port),
	};

	return otto_emdio_read_cmd(bus, RTL9300_PHY_CTRL_TYPE_C45, &cmd_data,
				   RTL9300_PHY_CTRL_DATA, value);
}

static int otto_emdio_9300_write_c45(struct mii_bus *bus, int port,
				     int dev_addr, int regnum, u16 value)
{
	struct otto_emdio_cmd_regs cmd_data = {
		.c45_data	= FIELD_PREP(PHY_CTRL_MMD_DEVAD, dev_addr) |
				  FIELD_PREP(PHY_CTRL_MMD_REG, regnum),
		.io_data	= FIELD_PREP(RTL9300_PHY_CTRL_INDATA, value),
		.port_mask_low	= BIT(port),
	};

	return otto_emdio_write_cmd(bus, RTL9300_PHY_CTRL_TYPE_C45, &cmd_data);
}

static int otto_emdio_9310_read_c22(struct mii_bus *bus, int port, int regnum, u32 *value)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	struct otto_emdio_cmd_regs cmd_data = {
		.broadcast	= FIELD_PREP(RTL9310_BC_PORT_ID, port),
		.c22_data	= FIELD_PREP(RTL9310_PHY_CTRL_REG_ADDR, regnum) |
				  FIELD_PREP(RTL9310_PHY_CTRL_MAIN_PAGE, RAW_PAGE(priv)),
	};

	return otto_emdio_read_cmd(bus, RTL9310_PHY_CTRL_TYPE_C22, &cmd_data,
				   RTL9310_PHY_CTRL_DATA, value);
}

static int otto_emdio_9310_write_c22(struct mii_bus *bus, int port, int regnum, u16 value)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	struct otto_emdio_cmd_regs cmd_data = {
		.c22_data	= FIELD_PREP(RTL9310_PHY_CTRL_REG_ADDR, regnum) |
				  FIELD_PREP(RTL9310_PHY_CTRL_MAIN_PAGE, RAW_PAGE(priv)),
		.io_data	= FIELD_PREP(RTL9310_PHY_CTRL_INDATA, value),
		.port_mask_high	= (u32)(BIT_ULL(port) >> 32),
		.port_mask_low	= (u32)(BIT_ULL(port)),
	};

	return otto_emdio_write_cmd(bus, RTL9310_PHY_CTRL_TYPE_C22, &cmd_data);
}

static int otto_emdio_9310_read_c45(struct mii_bus *bus, int port,
				    int dev_addr, int regnum, u32 *value)
{
	struct otto_emdio_cmd_regs cmd_data = {
		.broadcast	= FIELD_PREP(RTL9310_BC_PORT_ID, port),
		.c45_data	= FIELD_PREP(PHY_CTRL_MMD_DEVAD, dev_addr) |
				  FIELD_PREP(PHY_CTRL_MMD_REG, regnum),
	};

	return otto_emdio_read_cmd(bus, RTL9310_PHY_CTRL_TYPE_C45, &cmd_data,
				   RTL9310_PHY_CTRL_DATA, value);
}

static int otto_emdio_9310_write_c45(struct mii_bus *bus, int port,
				     int dev_addr, int regnum, u16 value)
{
	struct otto_emdio_cmd_regs cmd_data = {
		.c45_data	= FIELD_PREP(PHY_CTRL_MMD_DEVAD, dev_addr) |
				  FIELD_PREP(PHY_CTRL_MMD_REG, regnum),
		.io_data	= FIELD_PREP(RTL9310_PHY_CTRL_INDATA, value),
		.port_mask_high	= (u32)(BIT_ULL(port) >> 32),
		.port_mask_low	= (u32)(BIT_ULL(port)),
	};

	return otto_emdio_write_cmd(bus, RTL9310_PHY_CTRL_TYPE_C45, &cmd_data);
}

static int otto_emdio_read_c22(struct mii_bus *bus, int phy_id, int regnum)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	int ret, port;
	u32 value;

	port = otto_emdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	scoped_guard(mutex, &priv->lock)
		ret = priv->info->read_c22(bus, port, regnum, &value);

	return ret ? ret : value;
}

static int otto_emdio_write_c22(struct mii_bus *bus, int phy_id, int regnum, u16 value)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	int ret, port;

	port = otto_emdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	scoped_guard(mutex, &priv->lock)
		ret = priv->info->write_c22(bus, port, regnum, value);

	return ret;
}

static int otto_emdio_read_c45(struct mii_bus *bus, int phy_id, int dev_addr, int regnum)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	int ret, port;
	u32 value;

	port = otto_emdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	scoped_guard(mutex, &priv->lock)
		ret = priv->info->read_c45(bus, port, dev_addr, regnum, &value);

	return ret ? ret : value;
}

static int otto_emdio_write_c45(struct mii_bus *bus, int phy_id,
				int dev_addr, int regnum, u16 value)
{
	struct otto_emdio_priv *priv = otto_emdio_bus_to_priv(bus);
	int ret, port;

	port = otto_emdio_phy_to_port(bus, phy_id);
	if (port < 0)
		return port;

	scoped_guard(mutex, &priv->lock)
		ret = priv->info->write_c45(bus, port, dev_addr, regnum, value);

	return ret;
}

static int otto_emdio_write_mapping(struct otto_emdio_priv *priv, u32 base, u32 port,
				    u32 vals_per_reg, u32 bits_per_val, u32 value)
{
	u32 shift = (port % vals_per_reg) * bits_per_val;
	u32 reg = base + (port / vals_per_reg) * 4;
	u32 mask = GENMASK(bits_per_val - 1, 0);

	return regmap_update_bits(priv->regmap, reg, mask << shift, value << shift);
}

static int otto_emdio_setup_topology(struct otto_emdio_priv *priv)
{
	const struct otto_emdio_info *info = priv->info;
	u32 port;
	int ret;

	for_each_set_bit(port, priv->valid_ports, info->num_ports) {
		if (info->bus_map_base) {
			ret = otto_emdio_write_mapping(priv, info->bus_map_base, port,
						       MAP_BUSES_PER_REG, MAP_BITS_PER_BUS,
						       priv->smi_bus[port]);
			if (ret)
				return ret;
		}
		if (info->addr_map_base) {
			ret = otto_emdio_write_mapping(priv, info->addr_map_base, port,
						       MAP_ADDRS_PER_REG, MAP_BITS_PER_ADDR,
						       priv->smi_addr[port]);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int otto_emdio_9300_setup_controller(struct otto_emdio_priv *priv)
{
	u32 glb_ctrl_mask = 0, glb_ctrl_val = 0;
	struct regmap *regmap = priv->regmap;
	int i, err;

	/* Put the interfaces into C45 mode if required */
	glb_ctrl_mask = GENMASK(19, 16);
	for (i = 0; i < priv->info->num_buses; i++)
		if (priv->smi_bus_is_c45[i])
			glb_ctrl_val |= RTL9300_GLB_CTRL_INTF_SEL(i);

	err = regmap_update_bits(regmap, RTL9300_SMI_GLB_CTRL,
				 glb_ctrl_mask, glb_ctrl_val);
	if (err)
		return err;

	return 0;
}

static int otto_emdio_9310_setup_controller(struct otto_emdio_priv *priv)
{
	int i, err;

	/* Put the interfaces into C45 mode if required */
	for (i = 0; i < priv->info->num_buses; i++) {
		err = regmap_assign_bits(priv->regmap, RTL9310_SMI_GLB_CTRL1,
					 RTL9310_SMI_GLB_FMT_SEL_C45(i),
					 priv->smi_bus_is_c45[i]);
		if (err)
			return err;
	}

	return 0;
}

static int otto_emdio_probe_one(struct device *dev, struct otto_emdio_priv *priv,
				 struct fwnode_handle *node)
{
	struct otto_emdio_chan *chan;
	struct mii_bus *bus;
	u32 mdio_bus;
	int err;

	err = fwnode_property_read_u32(node, "reg", &mdio_bus);
	if (err)
		return dev_err_probe(dev, err, "undefined smi bus number\n");

	if (mdio_bus >= priv->info->num_buses)
		return dev_err_probe(dev, -EINVAL,
				     "illegal (dangling) smi bus number %d\n", mdio_bus);

	bus = devm_mdiobus_alloc_size(dev, sizeof(*chan));
	if (!bus)
		return -ENOMEM;

	bus->name = "Realtek Switch MDIO Bus";
	if (priv->smi_bus_is_c45[mdio_bus]) {
		bus->read_c45 = otto_emdio_read_c45;
		bus->write_c45 = otto_emdio_write_c45;
	} else {
		bus->read = otto_emdio_read_c22;
		bus->write = otto_emdio_write_c22;
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

static struct device_node *otto_emdio_get_bus_node(struct device_node *dn)
{
	struct device_node *parent = of_get_parent(dn);
	struct device_node *grandparent;

	if (parent && of_node_name_eq(parent, "ethernet-phy-package")) {
		grandparent = of_get_parent(parent);
		of_node_put(parent);

		return grandparent;
	}

	return parent;
}

/* The mdio-controller is part of a switch block so we parse the sibling
 * ethernet-ports node and build a mapping of the switch port to MDIO bus/addr
 * based on the phy-handle.
 */
static int otto_emdio_map_ports(struct device *dev)
{
	struct device_node *ports_dn, *phy_dn, *bus_dn, *ctrl_dn;
	struct otto_emdio_priv *priv = dev_get_drvdata(dev);
	struct device *parent = dev->parent;
	int addr, err = 0;
	u32 bus, pn;

	ports_dn = of_get_child_by_name(parent->of_node, "ethernet-ports");
	if (!ports_dn)
		return dev_err_probe(dev, -EINVAL, "%pfwP missing ethernet-ports\n",
				     dev_fwnode(parent));

	for_each_available_child_of_node_scoped(ports_dn, port_dn) {
		ctrl_dn = NULL;
		bus_dn = NULL;
		phy_dn = of_parse_phandle(port_dn, "phy-handle", 0);
		/* skip ports without phys */
		if (!phy_dn)
			continue;

		bus_dn = otto_emdio_get_bus_node(phy_dn);
		if (!bus_dn)
			goto put_nodes;

		ctrl_dn = of_get_parent(bus_dn);
		/* only map ports that are connected to this mdio-controller */
		if (ctrl_dn != dev->of_node)
			goto put_nodes;

		err = of_property_read_u32(port_dn, "reg", &pn);
		if (err)
			goto put_nodes;

		if (pn >= priv->info->num_ports) {
			err = dev_err_probe(dev, -EINVAL, "illegal port number %d\n", pn);
			goto put_nodes;
		}

		if (test_bit(pn, priv->valid_ports)) {
			err = dev_err_probe(dev, -EINVAL, "duplicated port number %d\n", pn);
			goto put_nodes;
		}

		err = of_property_read_u32(bus_dn, "reg", &bus);
		if (err)
			goto put_nodes;

		if (bus >= priv->info->num_buses) {
			err = dev_err_probe(dev, -EINVAL, "illegal smi bus number %d\n", bus);
			goto put_nodes;
		}

		addr = of_mdio_parse_addr(dev, phy_dn);
		if (addr < 0) {
			err = addr;
			goto put_nodes;
		}

		/*
		 * The MDIO accesses from the kernel work with the PHY polling unit in the
		 * switch. The PPU either operates in GPHY (i.e. clause 22) or 10GPHY mode
		 * (i.e. clause 45). Select 10GPHY mode if there is at least one PHY that
		 * declares compatible = "ethernet-phy-ieee802.3-c45".
		 */
		if (of_device_is_compatible(phy_dn, "ethernet-phy-ieee802.3-c45"))
			priv->smi_bus_is_c45[bus] = true;

		__set_bit(pn, priv->valid_ports);
		priv->smi_bus[pn] = bus;
		priv->smi_addr[pn] = addr;
put_nodes:
		of_node_put(bus_dn);
		of_node_put(phy_dn);
		of_node_put(ctrl_dn);
		if (err)
			break;
	}

	of_node_put(ports_dn);

	return err;
}

static int otto_emdio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct otto_emdio_priv *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	err = devm_mutex_init(dev, &priv->lock);
	if (err)
		return err;

	priv->info = device_get_match_data(dev);
	priv->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	platform_set_drvdata(pdev, priv);

	err = otto_emdio_map_ports(dev);
	if (err)
		return err;

	err = otto_emdio_setup_topology(priv);
	if (err)
		return err;

	if (priv->info->setup_controller) {
		err = priv->info->setup_controller(priv);
		if (err)
			return dev_err_probe(dev, err, "failed to setup MDIO bus controller\n");
	}

	device_for_each_child_node_scoped(dev, child) {
		err = otto_emdio_probe_one(dev, priv, child);
		if (err)
			return err;
	}

	return 0;
}

static const struct otto_emdio_info otto_emdio_9300_info = {
	.addr_map_base = RTL9300_SMI_PORT0_5_ADDR_CTRL,
	.bus_map_base = RTL9300_SMI_PORT0_15_POLLING_SEL,
	.cmd_fail = RTL9300_PHY_CTRL_FAIL,
	.cmd_read = RTL9300_PHY_CTRL_READ,
	.cmd_write = RTL9300_PHY_CTRL_WRITE,
	.cmd_regs = {
		.c22_data = RTL9300_SMI_ACCESS_PHY_CTRL_1,
		.c45_data = RTL9300_SMI_ACCESS_PHY_CTRL_3,
		.io_data = RTL9300_SMI_ACCESS_PHY_CTRL_2,
		.port_mask_low = RTL9300_SMI_ACCESS_PHY_CTRL_0,
	},
	.num_buses = RTL9300_NUM_BUSES,
	.num_ports = RTL9300_NUM_PORTS,
	.num_pages = RTL9300_NUM_PAGES,
	.setup_controller = otto_emdio_9300_setup_controller,
	.read_c22 = otto_emdio_9300_read_c22,
	.read_c45 = otto_emdio_9300_read_c45,
	.write_c22 = otto_emdio_9300_write_c22,
	.write_c45 = otto_emdio_9300_write_c45,
};

static const struct otto_emdio_info otto_emdio_9310_info = {
	.addr_map_base = RTL9310_SMI_PORT_ADDR_CTRL,
	.bus_map_base = RTL9310_SMI_PORT_POLLING_SEL,
	.cmd_fail = RTL9310_PHY_CTRL_FAIL,
	.cmd_read = RTL9310_PHY_CTRL_READ,
	.cmd_write = RTL9310_PHY_CTRL_WRITE,
	.cmd_regs = {
		.broadcast = RTL9310_SMI_INDRT_ACCESS_BC_PHYID_CTRL,
		.c22_data = RTL9310_SMI_INDRT_ACCESS_CTRL_0,
		.c45_data = RTL9310_SMI_INDRT_ACCESS_MMD_CTRL,
		.ext_page = RTL9310_SMI_INDRT_ACCESS_CTRL_1,
		.io_data = RTL9310_SMI_INDRT_ACCESS_CTRL_3,
		.port_mask_low = RTL9310_SMI_INDRT_ACCESS_CTRL_2_LOW,
		.port_mask_high = RTL9310_SMI_INDRT_ACCESS_CTRL_2_HIGH,
	},
	.num_buses = RTL9310_NUM_BUSES,
	.num_pages = RTL9310_NUM_PAGES,
	.num_ports = RTL9310_NUM_PORTS,
	.setup_controller = otto_emdio_9310_setup_controller,
	.read_c22 = otto_emdio_9310_read_c22,
	.read_c45 = otto_emdio_9310_read_c45,
	.write_c22 = otto_emdio_9310_write_c22,
	.write_c45 = otto_emdio_9310_write_c45,
};

static const struct of_device_id otto_emdio_ids[] = {
	{ .compatible = "realtek,rtl9301-mdio", .data = &otto_emdio_9300_info },
	{ .compatible = "realtek,rtl9311-mdio", .data = &otto_emdio_9310_info },
	{}
};
MODULE_DEVICE_TABLE(of, otto_emdio_ids);

static struct platform_driver otto_emdio_driver = {
	.probe = otto_emdio_probe,
	.driver = {
		.name = "mdio-rtl9300",
		.of_match_table = otto_emdio_ids,
	},
};

module_platform_driver(otto_emdio_driver);

MODULE_DESCRIPTION("RTL9300 MDIO driver");
MODULE_LICENSE("GPL");

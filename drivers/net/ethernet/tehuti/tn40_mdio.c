// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) Tehuti Networks Ltd. */

#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/phylink.h>

#include "tn40.h"

#define TN40_MDIO_DEVAD_MASK GENMASK(4, 0)
#define TN40_MDIO_PRTAD_MASK GENMASK(9, 5)
#define TN40_MDIO_CMD_VAL(device, port)			\
	(FIELD_PREP(TN40_MDIO_DEVAD_MASK, (device)) |	\
	 (FIELD_PREP(TN40_MDIO_PRTAD_MASK, (port))))
#define TN40_MDIO_CMD_READ BIT(15)

#define AQR105_FIRMWARE "tehuti/aqr105-tn40xx.cld"

static void tn40_mdio_set_speed(struct tn40_priv *priv, u32 speed)
{
	void __iomem *regs = priv->regs;
	int mdio_cfg;

	if (speed == TN40_MDIO_SPEED_1MHZ)
		mdio_cfg = (0x7d << 7) | 0x08;	/* 1MHz */
	else
		mdio_cfg = 0xA08;	/* 6MHz */
	mdio_cfg |= (1 << 6);
	writel(mdio_cfg, regs + TN40_REG_MDIO_CMD_STAT);
	msleep(100);
}

static u32 tn40_mdio_stat(struct tn40_priv *priv)
{
	void __iomem *regs = priv->regs;

	return readl(regs + TN40_REG_MDIO_CMD_STAT);
}

static int tn40_mdio_wait_nobusy(struct tn40_priv *priv, u32 *val)
{
	u32 stat;
	int ret;

	ret = readx_poll_timeout_atomic(tn40_mdio_stat, priv, stat,
					TN40_GET_MDIO_BUSY(stat) == 0, 10,
					10000);
	if (val)
		*val = stat;
	return ret;
}

static int tn40_mdio_read(struct tn40_priv *priv, int port, int device,
			  u16 regnum)
{
	void __iomem *regs = priv->regs;
	u32 i;

	/* wait until MDIO is not busy */
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;

	i = TN40_MDIO_CMD_VAL(device, port);
	writel(i, regs + TN40_REG_MDIO_CMD);
	writel((u32)regnum, regs + TN40_REG_MDIO_ADDR);
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;

	writel(TN40_MDIO_CMD_READ | i, regs + TN40_REG_MDIO_CMD);
	/* read CMD_STAT until not busy */
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;

	return lower_16_bits(readl(regs + TN40_REG_MDIO_DATA));
}

static int tn40_mdio_write(struct tn40_priv *priv, int port, int device,
			   u16 regnum, u16 data)
{
	void __iomem *regs = priv->regs;
	u32 tmp_reg = 0;
	int ret;

	/* wait until MDIO is not busy */
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;
	writel(TN40_MDIO_CMD_VAL(device, port), regs + TN40_REG_MDIO_CMD);
	writel((u32)regnum, regs + TN40_REG_MDIO_ADDR);
	if (tn40_mdio_wait_nobusy(priv, NULL))
		return -EIO;
	writel((u32)data, regs + TN40_REG_MDIO_DATA);
	/* read CMD_STAT until not busy */
	ret = tn40_mdio_wait_nobusy(priv, &tmp_reg);
	if (ret)
		return -EIO;

	if (TN40_GET_MDIO_RD_ERR(tmp_reg)) {
		dev_err(&priv->pdev->dev, "MDIO error after write command\n");
		return -EIO;
	}
	return 0;
}

static int tn40_mdio_read_c45(struct mii_bus *mii_bus, int addr, int devnum,
			      int regnum)
{
	return tn40_mdio_read(mii_bus->priv, addr, devnum, regnum);
}

static int tn40_mdio_write_c45(struct mii_bus *mii_bus, int addr, int devnum,
			       int regnum, u16 val)
{
	return  tn40_mdio_write(mii_bus->priv, addr, devnum, regnum, val);
}

/* registers an mdio node and an aqr105 PHY at address 1
 * tn40_mdio-%id {
 *	ethernet-phy@1 {
 *		compatible = "ethernet-phy-id03a1.b4a3";
 *		reg = <1>;
 *		firmware-name = AQR105_FIRMWARE;
 *	};
 * };
 */
static int tn40_swnodes_register(struct tn40_priv *priv)
{
	struct tn40_nodes *nodes = &priv->nodes;
	struct pci_dev *pdev = priv->pdev;
	struct software_node *swnodes;
	u32 id;

	id = pci_dev_id(pdev);

	snprintf(nodes->phy_name, sizeof(nodes->phy_name), "ethernet-phy@1");
	snprintf(nodes->mdio_name, sizeof(nodes->mdio_name), "tn40_mdio-%x",
		 id);

	swnodes = nodes->swnodes;

	swnodes[SWNODE_MDIO] = NODE_PROP(nodes->mdio_name, NULL);

	nodes->phy_props[0] = PROPERTY_ENTRY_STRING("compatible",
						    "ethernet-phy-id03a1.b4a3");
	nodes->phy_props[1] = PROPERTY_ENTRY_U32("reg", 1);
	nodes->phy_props[2] = PROPERTY_ENTRY_STRING("firmware-name",
						    AQR105_FIRMWARE);
	swnodes[SWNODE_PHY] = NODE_PAR_PROP(nodes->phy_name,
					    &swnodes[SWNODE_MDIO],
					    nodes->phy_props);

	nodes->group[SWNODE_PHY] = &swnodes[SWNODE_PHY];
	nodes->group[SWNODE_MDIO] = &swnodes[SWNODE_MDIO];
	return software_node_register_node_group(nodes->group);
}

void tn40_swnodes_cleanup(struct tn40_priv *priv)
{
	/* cleanup of swnodes is only needed for AQR105-based cards */
	if (priv->pdev->device == PCI_DEVICE_ID_TEHUTI_TN9510) {
		fwnode_handle_put(dev_fwnode(&priv->mdio->dev));
		device_remove_software_node(&priv->mdio->dev);
		software_node_unregister_node_group(priv->nodes.group);
	}
}

int tn40_mdiobus_init(struct tn40_priv *priv)
{
	struct pci_dev *pdev = priv->pdev;
	struct mii_bus *bus;
	int ret;

	bus = devm_mdiobus_alloc(&pdev->dev);
	if (!bus)
		return -ENOMEM;

	bus->name = TN40_DRV_NAME;
	bus->parent = &pdev->dev;
	snprintf(bus->id, MII_BUS_ID_SIZE, "tn40xx-%x-%x",
		 pci_domain_nr(pdev->bus), pci_dev_id(pdev));
	bus->priv = priv;

	bus->read_c45 = tn40_mdio_read_c45;
	bus->write_c45 = tn40_mdio_write_c45;
	priv->mdio = bus;

	/* provide swnodes for AQR105-based cards only */
	if (pdev->device == PCI_DEVICE_ID_TEHUTI_TN9510) {
		ret = tn40_swnodes_register(priv);
		if (ret) {
			pr_err("swnodes failed\n");
			return ret;
		}

		ret = device_add_software_node(&bus->dev,
					       priv->nodes.group[SWNODE_MDIO]);
		if (ret) {
			dev_err(&pdev->dev,
				"device_add_software_node failed: %d\n", ret);
			goto err_swnodes_unregister;
		}
	}

	ret = devm_mdiobus_register(&pdev->dev, bus);
	if (ret) {
		dev_err(&pdev->dev, "failed to register mdiobus %d %u %u\n",
			ret, bus->state, MDIOBUS_UNREGISTERED);
		goto err_swnodes_cleanup;
	}
	tn40_mdio_set_speed(priv, TN40_MDIO_SPEED_6MHZ);
	return 0;

err_swnodes_unregister:
	software_node_unregister_node_group(priv->nodes.group);
	return ret;
err_swnodes_cleanup:
	tn40_swnodes_cleanup(priv);
	return ret;
}

MODULE_FIRMWARE(AQR105_FIRMWARE);

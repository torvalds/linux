// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  STMMAC Ethernet Driver -- MDIO bus implementation
  Provides Bus interface for MII registers

  Copyright (C) 2007-2009  STMicroelectronics Ltd


  Author: Carl Shaw <carl.shaw@st.com>
  Maintainer: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mii.h>
#include <linux/of_mdio.h>
#include <linux/pm_runtime.h>
#include <linux/phy.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "dwxgmac2.h"
#include "stmmac.h"

#define MII_BUSY 0x00000001
#define MII_WRITE 0x00000002
#define MII_DATA_MASK GENMASK(15, 0)

/* GMAC4 defines */
#define MII_GMAC4_GOC_SHIFT		2
#define MII_GMAC4_REG_ADDR_SHIFT	16
#define MII_GMAC4_WRITE			(1 << MII_GMAC4_GOC_SHIFT)
#define MII_GMAC4_READ			(3 << MII_GMAC4_GOC_SHIFT)
#define MII_GMAC4_C45E			BIT(1)

/* XGMAC defines */
#define MII_XGMAC_SADDR			BIT(18)
#define MII_XGMAC_CMD_SHIFT		16
#define MII_XGMAC_WRITE			(1 << MII_XGMAC_CMD_SHIFT)
#define MII_XGMAC_READ			(3 << MII_XGMAC_CMD_SHIFT)
#define MII_XGMAC_BUSY			BIT(22)
#define MII_XGMAC_MAX_C22ADDR		3
#define MII_XGMAC_C22P_MASK		GENMASK(MII_XGMAC_MAX_C22ADDR, 0)
#define MII_XGMAC_PA_SHIFT		16
#define MII_XGMAC_DA_SHIFT		21

static void stmmac_xgmac2_c45_format(struct stmmac_priv *priv, int phyaddr,
				     int devad, int phyreg, u32 *hw_addr)
{
	u32 tmp;

	/* Set port as Clause 45 */
	tmp = readl(priv->ioaddr + XGMAC_MDIO_C22P);
	tmp &= ~BIT(phyaddr);
	writel(tmp, priv->ioaddr + XGMAC_MDIO_C22P);

	*hw_addr = (phyaddr << MII_XGMAC_PA_SHIFT) | (phyreg & 0xffff);
	*hw_addr |= devad << MII_XGMAC_DA_SHIFT;
}

static void stmmac_xgmac2_c22_format(struct stmmac_priv *priv, int phyaddr,
				     int phyreg, u32 *hw_addr)
{
	u32 tmp;

	/* Set port as Clause 22 */
	tmp = readl(priv->ioaddr + XGMAC_MDIO_C22P);
	tmp &= ~MII_XGMAC_C22P_MASK;
	tmp |= BIT(phyaddr);
	writel(tmp, priv->ioaddr + XGMAC_MDIO_C22P);

	*hw_addr = (phyaddr << MII_XGMAC_PA_SHIFT) | (phyreg & 0x1f);
}

static int stmmac_xgmac2_mdio_read(struct stmmac_priv *priv, u32 addr,
				   u32 value)
{
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 tmp;
	int ret;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), 100, 10000)) {
		ret = -EBUSY;
		goto err_disable_clks;
	}

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	value |= MII_XGMAC_READ;

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), 100, 10000)) {
		ret = -EBUSY;
		goto err_disable_clks;
	}

	/* Set the MII address register to read */
	writel(addr, priv->ioaddr + mii_address);
	writel(value, priv->ioaddr + mii_data);

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), 100, 10000)) {
		ret = -EBUSY;
		goto err_disable_clks;
	}

	/* Read the data from the MII data register */
	ret = (int)readl(priv->ioaddr + mii_data) & GENMASK(15, 0);

err_disable_clks:
	pm_runtime_put(priv->device);

	return ret;
}

static int stmmac_xgmac2_mdio_read_c22(struct mii_bus *bus, int phyaddr,
				       int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv;
	u32 addr;

	priv = netdev_priv(ndev);

	/* HW does not support C22 addr >= 4 */
	if (phyaddr > MII_XGMAC_MAX_C22ADDR)
		return -ENODEV;

	stmmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);

	return stmmac_xgmac2_mdio_read(priv, addr, MII_XGMAC_BUSY);
}

static int stmmac_xgmac2_mdio_read_c45(struct mii_bus *bus, int phyaddr,
				       int devad, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv;
	u32 addr;

	priv = netdev_priv(ndev);

	stmmac_xgmac2_c45_format(priv, phyaddr, devad, phyreg, &addr);

	return stmmac_xgmac2_mdio_read(priv, addr, MII_XGMAC_BUSY);
}

static int stmmac_xgmac2_mdio_write(struct stmmac_priv *priv, u32 addr,
				    u32 value, u16 phydata)
{
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 tmp;
	int ret;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), 100, 10000)) {
		ret = -EBUSY;
		goto err_disable_clks;
	}

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	value |= phydata;
	value |= MII_XGMAC_WRITE;

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_data, tmp,
			       !(tmp & MII_XGMAC_BUSY), 100, 10000)) {
		ret = -EBUSY;
		goto err_disable_clks;
	}

	/* Set the MII address register to write */
	writel(addr, priv->ioaddr + mii_address);
	writel(value, priv->ioaddr + mii_data);

	/* Wait until any existing MII operation is complete */
	ret = readl_poll_timeout(priv->ioaddr + mii_data, tmp,
				 !(tmp & MII_XGMAC_BUSY), 100, 10000);

err_disable_clks:
	pm_runtime_put(priv->device);

	return ret;
}

static int stmmac_xgmac2_mdio_write_c22(struct mii_bus *bus, int phyaddr,
					int phyreg, u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv;
	u32 addr;

	priv = netdev_priv(ndev);

	/* HW does not support C22 addr >= 4 */
	if (phyaddr > MII_XGMAC_MAX_C22ADDR)
		return -ENODEV;

	stmmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);

	return stmmac_xgmac2_mdio_write(priv, addr,
					MII_XGMAC_BUSY | MII_XGMAC_SADDR, phydata);
}

static int stmmac_xgmac2_mdio_write_c45(struct mii_bus *bus, int phyaddr,
					int devad, int phyreg, u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv;
	u32 addr;

	priv = netdev_priv(ndev);

	stmmac_xgmac2_c45_format(priv, phyaddr, devad, phyreg, &addr);

	return stmmac_xgmac2_mdio_write(priv, addr, MII_XGMAC_BUSY,
					phydata);
}

static int stmmac_mdio_read(struct stmmac_priv *priv, int data, u32 value)
{
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 v;

	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000))
		return -EBUSY;

	writel(data, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000))
		return -EBUSY;

	/* Read the data from the MII data register */
	return readl(priv->ioaddr + mii_data) & MII_DATA_MASK;
}

/**
 * stmmac_mdio_read_c22
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * Description: it reads data from the MII register from within the phy device.
 * For the 7111 GMAC, we must set the bit 0 in the MII address register while
 * accessing the PHY registers.
 * Fortunately, it seems this has no drawback for the 7109 MAC.
 */
static int stmmac_mdio_read_c22(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	u32 value = MII_BUSY;
	int data = 0;

	data = pm_runtime_resume_and_get(priv->device);
	if (data < 0)
		return data;

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;
	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	if (priv->plat->has_gmac4)
		value |= MII_GMAC4_READ;

	data = stmmac_mdio_read(priv, data, value);

	pm_runtime_put(priv->device);

	return data;
}

/**
 * stmmac_mdio_read_c45
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @devad: device address to read
 * @phyreg: MII reg
 * Description: it reads data from the MII register from within the phy device.
 * For the 7111 GMAC, we must set the bit 0 in the MII address register while
 * accessing the PHY registers.
 * Fortunately, it seems this has no drawback for the 7109 MAC.
 */
static int stmmac_mdio_read_c45(struct mii_bus *bus, int phyaddr, int devad,
				int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	u32 value = MII_BUSY;
	int data = 0;

	data = pm_runtime_get_sync(priv->device);
	if (data < 0) {
		pm_runtime_put_noidle(priv->device);
		return data;
	}

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;
	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	value |= MII_GMAC4_READ;
	value |= MII_GMAC4_C45E;
	value &= ~priv->hw->mii.reg_mask;
	value |= (devad << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;

	data |= phyreg << MII_GMAC4_REG_ADDR_SHIFT;

	data = stmmac_mdio_read(priv, data, value);

	pm_runtime_put(priv->device);

	return data;
}

static int stmmac_mdio_write(struct stmmac_priv *priv, int data, u32 value)
{
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 v;

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000))
		return -EBUSY;

	/* Set the MII address register to write */
	writel(data, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	/* Wait until any existing MII operation is complete */
	return readl_poll_timeout(priv->ioaddr + mii_address, v,
				  !(v & MII_BUSY), 100, 10000);
}

/**
 * stmmac_mdio_write_c22
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * @phydata: phy data
 * Description: it writes the data into the MII register from within the device.
 */
static int stmmac_mdio_write_c22(struct mii_bus *bus, int phyaddr, int phyreg,
				 u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret, data = phydata;
	u32 value = MII_BUSY;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	if (priv->plat->has_gmac4)
		value |= MII_GMAC4_WRITE;
	else
		value |= MII_WRITE;

	ret = stmmac_mdio_write(priv, data, value);

	pm_runtime_put(priv->device);

	return ret;
}

/**
 * stmmac_mdio_write_c45
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * @devad: device address to read
 * @phydata: phy data
 * Description: it writes the data into the MII register from within the device.
 */
static int stmmac_mdio_write_c45(struct mii_bus *bus, int phyaddr,
				 int devad, int phyreg, u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	int ret, data = phydata;
	u32 value = MII_BUSY;

	ret = pm_runtime_get_sync(priv->device);
	if (ret < 0) {
		pm_runtime_put_noidle(priv->device);
		return ret;
	}

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;

	value |= MII_GMAC4_WRITE;
	value |= MII_GMAC4_C45E;
	value &= ~priv->hw->mii.reg_mask;
	value |= (devad << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;

	data |= phyreg << MII_GMAC4_REG_ADDR_SHIFT;

	ret = stmmac_mdio_write(priv, data, value);

	pm_runtime_put(priv->device);

	return ret;
}

/**
 * stmmac_mdio_reset
 * @bus: points to the mii_bus structure
 * Description: reset the MII bus
 */
int stmmac_mdio_reset(struct mii_bus *bus)
{
#if IS_ENABLED(CONFIG_STMMAC_PLATFORM)
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;

#ifdef CONFIG_OF
	if (priv->device->of_node) {
		struct gpio_desc *reset_gpio;
		u32 delays[3] = { 0, 0, 0 };

		reset_gpio = devm_gpiod_get_optional(priv->device,
						     "snps,reset",
						     GPIOD_OUT_LOW);
		if (IS_ERR(reset_gpio))
			return PTR_ERR(reset_gpio);

		device_property_read_u32_array(priv->device,
					       "snps,reset-delays-us",
					       delays, ARRAY_SIZE(delays));

		if (delays[0])
			msleep(DIV_ROUND_UP(delays[0], 1000));

		gpiod_set_value_cansleep(reset_gpio, 1);
		if (delays[1])
			msleep(DIV_ROUND_UP(delays[1], 1000));

		gpiod_set_value_cansleep(reset_gpio, 0);
		if (delays[2])
			msleep(DIV_ROUND_UP(delays[2], 1000));
	}
#endif

	/* This is a workaround for problems with the STE101P PHY.
	 * It doesn't complete its reset until at least one clock cycle
	 * on MDC, so perform a dummy mdio read. To be updated for GMAC4
	 * if needed.
	 */
	if (!priv->plat->has_gmac4)
		writel(0, priv->ioaddr + mii_address);
#endif
	return 0;
}

int stmmac_xpcs_setup(struct mii_bus *bus)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv;
	struct dw_xpcs *xpcs;
	int mode, addr;

	priv = netdev_priv(ndev);
	mode = priv->plat->phy_interface;

	/* Try to probe the XPCS by scanning all addresses. */
	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		xpcs = xpcs_create_mdiodev(bus, addr, mode);
		if (IS_ERR(xpcs))
			continue;

		priv->hw->xpcs = xpcs;
		break;
	}

	if (!priv->hw->xpcs) {
		dev_warn(priv->device, "No xPCS found\n");
		return -ENODEV;
	}

	return 0;
}

/**
 * stmmac_mdio_register
 * @ndev: net device structure
 * Description: it registers the MII bus
 */
int stmmac_mdio_register(struct net_device *ndev)
{
	int err = 0;
	struct mii_bus *new_bus;
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct fwnode_handle *fwnode = of_fwnode_handle(priv->plat->phylink_node);
	struct stmmac_mdio_bus_data *mdio_bus_data = priv->plat->mdio_bus_data;
	struct device_node *mdio_node = priv->plat->mdio_node;
	struct device *dev = ndev->dev.parent;
	struct fwnode_handle *fixed_node;
	int addr, found, max_addr;

	if (!mdio_bus_data)
		return 0;

	new_bus = mdiobus_alloc();
	if (!new_bus)
		return -ENOMEM;

	if (mdio_bus_data->irqs)
		memcpy(new_bus->irq, mdio_bus_data->irqs, sizeof(new_bus->irq));

	new_bus->name = "stmmac";

	if (priv->plat->has_xgmac) {
		new_bus->read = &stmmac_xgmac2_mdio_read_c22;
		new_bus->write = &stmmac_xgmac2_mdio_write_c22;
		new_bus->read_c45 = &stmmac_xgmac2_mdio_read_c45;
		new_bus->write_c45 = &stmmac_xgmac2_mdio_write_c45;

		/* Right now only C22 phys are supported */
		max_addr = MII_XGMAC_MAX_C22ADDR + 1;

		/* Check if DT specified an unsupported phy addr */
		if (priv->plat->phy_addr > MII_XGMAC_MAX_C22ADDR)
			dev_err(dev, "Unsupported phy_addr (max=%d)\n",
					MII_XGMAC_MAX_C22ADDR);
	} else {
		new_bus->read = &stmmac_mdio_read_c22;
		new_bus->write = &stmmac_mdio_write_c22;
		if (priv->plat->has_gmac4) {
			new_bus->read_c45 = &stmmac_mdio_read_c45;
			new_bus->write_c45 = &stmmac_mdio_write_c45;
		}

		max_addr = PHY_MAX_ADDR;
	}

	if (mdio_bus_data->needs_reset)
		new_bus->reset = &stmmac_mdio_reset;

	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 new_bus->name, priv->plat->bus_id);
	new_bus->priv = ndev;
	new_bus->phy_mask = mdio_bus_data->phy_mask;
	new_bus->parent = priv->device;

	err = of_mdiobus_register(new_bus, mdio_node);
	if (err != 0) {
		dev_err_probe(dev, err, "Cannot register the MDIO bus\n");
		goto bus_register_fail;
	}

	/* Looks like we need a dummy read for XGMAC only and C45 PHYs */
	if (priv->plat->has_xgmac)
		stmmac_xgmac2_mdio_read_c45(new_bus, 0, 0, 0);

	/* If fixed-link is set, skip PHY scanning */
	if (!fwnode)
		fwnode = dev_fwnode(priv->device);

	if (fwnode) {
		fixed_node = fwnode_get_named_child_node(fwnode, "fixed-link");
		if (fixed_node) {
			fwnode_handle_put(fixed_node);
			goto bus_register_done;
		}
	}

	if (priv->plat->phy_node || mdio_node)
		goto bus_register_done;

	found = 0;
	for (addr = 0; addr < max_addr; addr++) {
		struct phy_device *phydev = mdiobus_get_phy(new_bus, addr);

		if (!phydev)
			continue;

		/*
		 * If an IRQ was provided to be assigned after
		 * the bus probe, do it here.
		 */
		if (!mdio_bus_data->irqs &&
		    (mdio_bus_data->probed_phy_irq > 0)) {
			new_bus->irq[addr] = mdio_bus_data->probed_phy_irq;
			phydev->irq = mdio_bus_data->probed_phy_irq;
		}

		/*
		 * If we're going to bind the MAC to this PHY bus,
		 * and no PHY number was provided to the MAC,
		 * use the one probed here.
		 */
		if (priv->plat->phy_addr == -1)
			priv->plat->phy_addr = addr;

		phy_attached_info(phydev);
		found = 1;
	}

	if (!found && !mdio_node) {
		dev_warn(dev, "No PHY found\n");
		err = -ENODEV;
		goto no_phy_found;
	}

bus_register_done:
	priv->mii = new_bus;

	return 0;

no_phy_found:
	mdiobus_unregister(new_bus);
bus_register_fail:
	mdiobus_free(new_bus);
	return err;
}

/**
 * stmmac_mdio_unregister
 * @ndev: net device structure
 * Description: it unregisters the MII bus
 */
int stmmac_mdio_unregister(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (!priv->mii)
		return 0;

	if (priv->hw->xpcs)
		xpcs_destroy(priv->hw->xpcs);

	mdiobus_unregister(priv->mii);
	priv->mii->priv = NULL;
	mdiobus_free(priv->mii);
	priv->mii = NULL;

	return 0;
}

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

#include "emac_mdio_fe.h"
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

static int stmmac_xgmac2_c45_format(struct stmmac_priv *priv, int phyaddr,
				    int phyreg, u32 *hw_addr)
{
	u32 tmp;

	/* Set port as Clause 45 */
	tmp = readl(priv->ioaddr + XGMAC_MDIO_C22P);
	tmp &= ~BIT(phyaddr);
	writel(tmp, priv->ioaddr + XGMAC_MDIO_C22P);

	*hw_addr = (phyaddr << MII_XGMAC_PA_SHIFT) | (phyreg & 0xffff);
	*hw_addr |= (phyreg >> MII_DEVADDR_C45_SHIFT) << MII_XGMAC_DA_SHIFT;
	return 0;
}

static int stmmac_xgmac2_c22_format(struct stmmac_priv *priv, int phyaddr,
				    int phyreg, u32 *hw_addr)
{
	u32 tmp;

	/* HW does not support C22 addr >= 4 */
	if (phyaddr > MII_XGMAC_MAX_C22ADDR)
		return -ENODEV;

	/* Set port as Clause 22 */
	tmp = readl(priv->ioaddr + XGMAC_MDIO_C22P);
	tmp &= ~MII_XGMAC_C22P_MASK;
	tmp |= BIT(phyaddr);
	writel(tmp, priv->ioaddr + XGMAC_MDIO_C22P);

	*hw_addr = (phyaddr << MII_XGMAC_PA_SHIFT) | (phyreg & 0x1f);
	return 0;
}

static int stmmac_xgmac2_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 tmp, addr, value = MII_XGMAC_BUSY;
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

	if (phyreg & MII_ADDR_C45) {
		phyreg &= ~MII_ADDR_C45;

		ret = stmmac_xgmac2_c45_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			goto err_disable_clks;
	} else {
		ret = stmmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			goto err_disable_clks;

		value |= MII_XGMAC_SADDR;
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

static int stmmac_xgmac2_mdio_write(struct mii_bus *bus, int phyaddr,
				    int phyreg, u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 addr, tmp, value = MII_XGMAC_BUSY;
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

	if (phyreg & MII_ADDR_C45) {
		phyreg &= ~MII_ADDR_C45;

		ret = stmmac_xgmac2_c45_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			goto err_disable_clks;
	} else {
		ret = stmmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);
		if (ret)
			goto err_disable_clks;

		value |= MII_XGMAC_SADDR;
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

/**
 * stmmac_mdio_read
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * Description: it reads data from the MII register from within the phy device.
 * For the 7111 GMAC, we must set the bit 0 in the MII address register while
 * accessing the PHY registers.
 * Fortunately, it seems this has no drawback for the 7109 MAC.
 */
static int stmmac_mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	u32 value = MII_BUSY;
	int data = 0;
	u32 v;

	data = pm_runtime_resume_and_get(priv->device);
	if (data < 0)
		return data;

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;
	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	if (priv->plat->has_gmac4) {
		value |= MII_GMAC4_READ;
		if (phyreg & MII_ADDR_C45) {
			value |= MII_GMAC4_C45E;
			value &= ~priv->hw->mii.reg_mask;
			value |= ((phyreg >> MII_DEVADDR_C45_SHIFT) <<
			       priv->hw->mii.reg_shift) &
			       priv->hw->mii.reg_mask;

			data |= (phyreg & MII_REGADDR_C45_MASK) <<
				MII_GMAC4_REG_ADDR_SHIFT;
		}
	}

	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000)) {
		data = -EBUSY;
		goto err_disable_clks;
	}

	writel(data, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000)) {
		data = -EBUSY;
		goto err_disable_clks;
	}

	/* Read the data from the MII data register */
	data = (int)readl(priv->ioaddr + mii_data) & MII_DATA_MASK;

err_disable_clks:
	pm_runtime_put(priv->device);

	return data;
}

/**
 * stmmac_mdio_write
 * @bus: points to the mii_bus structure
 * @phyaddr: MII addr
 * @phyreg: MII reg
 * @phydata: phy data
 * Description: it writes the data into the MII register from within the device.
 */
static int stmmac_mdio_write(struct mii_bus *bus, int phyaddr, int phyreg,
			     u16 phydata)
{
	struct net_device *ndev = bus->priv;
	struct stmmac_priv *priv = netdev_priv(ndev);
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	int ret, data = phydata;
	u32 value = MII_BUSY;
	u32 v;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	value |= (phyaddr << priv->hw->mii.addr_shift)
		& priv->hw->mii.addr_mask;
	value |= (phyreg << priv->hw->mii.reg_shift) & priv->hw->mii.reg_mask;

	value |= (priv->clk_csr << priv->hw->mii.clk_csr_shift)
		& priv->hw->mii.clk_csr_mask;
	if (priv->plat->has_gmac4) {
		value |= MII_GMAC4_WRITE;
		if (phyreg & MII_ADDR_C45) {
			value |= MII_GMAC4_C45E;
			value &= ~priv->hw->mii.reg_mask;
			value |= ((phyreg >> MII_DEVADDR_C45_SHIFT) <<
			       priv->hw->mii.reg_shift) &
			       priv->hw->mii.reg_mask;

			data |= (phyreg & MII_REGADDR_C45_MASK) <<
				MII_GMAC4_REG_ADDR_SHIFT;
		}
	} else {
		value |= MII_WRITE;
	}

	/* Wait until any existing MII operation is complete */
	if (readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
			       100, 10000)) {
		ret = -EBUSY;
		goto err_disable_clks;
	}

	/* Set the MII address register to write */
	writel(data, priv->ioaddr + mii_data);
	writel(value, priv->ioaddr + mii_address);

	/* Wait until any existing MII operation is complete */
	ret = readl_poll_timeout(priv->ioaddr + mii_address, v, !(v & MII_BUSY),
				 100, 10000);

err_disable_clks:
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
	bool active_high = false;

	if (priv->plat->early_eth && !priv->plat->need_reset)
		return 0;

#ifdef CONFIG_OF
	if (priv->device->of_node) {
		struct gpio_desc *reset_gpio;
		u32 delays[3] = { 0, 0, 0 };

		reset_gpio = devm_gpiod_get_optional(priv->device,
						     "snps,reset",
						     GPIOD_OUT_LOW);
		if (IS_ERR(reset_gpio)) {
			pr_err("qcom-ethqos: %s failed to get gpio with err %d\n",
			       __func__, PTR_ERR(reset_gpio));
			return PTR_ERR(reset_gpio);
		}

		device_property_read_u32_array(priv->device,
					       "snps,reset-delays-us",
					       delays, ARRAY_SIZE(delays));

		if (delays[0])
			msleep(DIV_ROUND_UP(delays[0], 1000));

		gpiod_set_value_cansleep(reset_gpio, active_high ? 1 : 0);
		if (delays[1])
			msleep(DIV_ROUND_UP(delays[1], 1000));

		gpiod_set_value_cansleep(reset_gpio, active_high ? 0 : 1);
		if (delays[2])
			msleep(DIV_ROUND_UP(delays[2], 1000));
		devm_gpiod_put(priv->device, reset_gpio);
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
	struct mdio_device *mdiodev;
	struct stmmac_priv *priv;
	struct dw_xpcs *xpcs;
	int mode, addr;

	priv = netdev_priv(ndev);
	mode = priv->plat->phy_interface;

	/* Try to probe the XPCS by scanning all addresses. */
	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		mdiodev = mdio_device_create(bus, addr);
		if (IS_ERR(mdiodev))
			continue;

		xpcs = xpcs_create(mdiodev, mode);
		if (IS_ERR_OR_NULL(xpcs)) {
			mdio_device_free(mdiodev);
			continue;
		}

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
 * stmmac_get_phy_addr
 * @priv: net device structure
 * @new_bus: points to the mii_bus structure
 * Description: it finds the PHY address from board and phy_type
 */
int stmmac_get_phy_addr(struct stmmac_priv *priv, struct mii_bus *new_bus,
			struct net_device *ndev)
{
	struct stmmac_mdio_bus_data *mdio_bus_data = priv->plat->mdio_bus_data;
	struct device_node *np = priv->device->of_node;
	unsigned int phyaddr;
	int err = 0;

	init_completion(&priv->plat->mdio_op);
	new_bus->reset = &stmmac_mdio_reset;
	new_bus->priv = ndev;

	if (priv->plat->phy_type != -1) {
		if (priv->plat->phy_type == PHY_1G) {
			err = of_property_read_u32(np, "emac-1g-phy-addr", &phyaddr);
			new_bus->read = &virtio_mdio_read;
			new_bus->write = &virtio_mdio_write;
		} else {
			new_bus->read = &virtio_mdio_read_c45_indirect;
			new_bus->write = &virtio_mdio_write_c45_indirect;
			new_bus->probe_capabilities = MDIOBUS_C22_C45;
			if (priv->plat->phy_type == PHY_25G &&
			    priv->plat->board_type == STAR_BOARD) {
				err = of_property_read_u32(np,
							   "emac-star-cl45-phy-addr", &phyaddr);
			} else {
				err = of_property_read_u32(np,
							   "emac-air-cl45-phy-addr", &phyaddr);
			}
		}
	} else {
		err = of_property_read_u32(np, "emac-1g-phy-addr", &phyaddr);
		if (err) {
			new_bus->phy_mask = mdio_bus_data->phy_mask;
			return -1;
		}
		new_bus->read = &virtio_mdio_read;
		new_bus->write = &virtio_mdio_write;
		/* Do MDIO reset before the bus->read call */
		err = new_bus->reset(new_bus);
		if (err) {
			new_bus->phy_mask = ~(1 << phyaddr);
			return phyaddr;
		}
		/* 1G phy check */
		err = new_bus->read(new_bus, phyaddr, MII_BMSR);
		if (err == -EBUSY || err == 0xffff) {
			/* 2.5 G PHY case */
			new_bus->read = &virtio_mdio_read_c45_indirect;
			new_bus->write = &virtio_mdio_write_c45_indirect;
			new_bus->probe_capabilities = MDIOBUS_C22_C45;

			err = of_property_read_u32(np,
						   "emac-air-cl45-phy-addr", &phyaddr);
			/* Board Type check */
			err = new_bus->read(new_bus, phyaddr, MII_BMSR);
			if (err == -EBUSY || !err || err == 0xffff)
				err = of_property_read_u32(np,
							   "emac-star-cl45-phy-addr", &phyaddr);
		}
	}
	new_bus->phy_mask = ~(1 << phyaddr);
	return phyaddr;
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
	struct device_node *np = priv->device->of_node;
	struct device *dev = ndev->dev.parent;
	struct phy_device *phydev;
	struct fwnode_handle *fixed_node;
	int addr, found, max_addr, skip_phy_detect = 0;
	unsigned int phyaddr;

	if (!mdio_bus_data)
		return 0;

	new_bus = mdiobus_alloc();
	if (!new_bus)
		return -ENOMEM;

	if (mdio_bus_data->irqs)
		memcpy(new_bus->irq, mdio_bus_data->irqs, sizeof(new_bus->irq));

	new_bus->name = "stmmac";

	if (priv->plat->has_gmac4) {
		if (priv->plat->has_c22_mdio_probe_capability)
			new_bus->probe_capabilities = MDIOBUS_C22;
		else
			new_bus->probe_capabilities = MDIOBUS_C22_C45;
	}

	err = of_property_read_bool(np, "virtio-mdio");
	if (err) {
		phyaddr = stmmac_get_phy_addr(priv, new_bus, ndev);
		max_addr = PHY_MAX_ADDR;
		skip_phy_detect = 1;
	} else if (priv->plat->has_xgmac) {
		new_bus->read = &stmmac_xgmac2_mdio_read;
		new_bus->write = &stmmac_xgmac2_mdio_write;

		/* Right now only C22 phys are supported */
		max_addr = MII_XGMAC_MAX_C22ADDR + 1;

		/* Check if DT specified an unsupported phy addr */
		if (priv->plat->phy_addr > MII_XGMAC_MAX_C22ADDR)
			dev_err(dev, "Unsupported phy_addr (max=%d)\n",
					MII_XGMAC_MAX_C22ADDR);
	} else {
		new_bus->read = &stmmac_mdio_read;
		new_bus->write = &stmmac_mdio_write;
		max_addr = PHY_MAX_ADDR;
	}

	if (mdio_bus_data->needs_reset)
		new_bus->reset = &stmmac_mdio_reset;

	snprintf(new_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		 new_bus->name, priv->plat->bus_id);
	new_bus->priv = ndev;
	new_bus->parent = priv->device;

	err = of_mdiobus_register(new_bus, mdio_node);
	if (err == -ENODEV) {
		err = 0;
		dev_info(dev, "MDIO bus is disabled\n");
		goto bus_register_fail;
	} else if (err) {
		dev_err_probe(dev, err, "Cannot register the MDIO bus\n");
		goto bus_register_fail;
	}

	/* Looks like we need a dummy read for XGMAC only and C45 PHYs */
	if (priv->plat->has_xgmac)
		stmmac_xgmac2_mdio_read(new_bus, 0, MII_ADDR_C45);

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
	if (skip_phy_detect) {
		phydev = mdiobus_get_phy(new_bus, phyaddr);
		if (!phydev || phydev->phy_id == 0xffff) {
			dev_err(dev, "Cannot attach phy addr %d from dtsi\n",
				phyaddr);
		} else {
			priv->plat->phy_addr = phyaddr;
			priv->phydev = phydev;
			phy_attached_info(phydev);
			goto bus_register_done;
		}
	}

	if (priv->plat->phy_node || mdio_node)
		goto bus_register_done;

	found = 0;
	for (addr = 0; addr < max_addr; addr++) {
		phydev = mdiobus_get_phy(new_bus, addr);

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

	if (priv->hw->xpcs) {
		mdio_device_free(priv->hw->xpcs->mdiodev);
		xpcs_destroy(priv->hw->xpcs);
	}

	mdiobus_unregister(priv->mii);
	priv->mii->priv = NULL;
	mdiobus_free(priv->mii);
	priv->mii = NULL;

	return 0;
}

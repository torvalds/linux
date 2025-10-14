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

#define MII_ADDR_GBUSY			BIT(0)
#define MII_ADDR_GWRITE			BIT(1)
#define MII_DATA_GD_MASK		GENMASK(15, 0)

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

static int stmmac_mdio_wait(void __iomem *reg, u32 mask)
{
	u32 v;

	if (readl_poll_timeout(reg, v, !(v & mask), 100, 10000))
		return -EBUSY;

	return 0;
}

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
	u32 tmp = 0;

	if (priv->synopsys_id < DWXGMAC_CORE_2_20) {
		/* Until ver 2.20 XGMAC does not support C22 addr >= 4. Those
		 * bits above bit 3 of XGMAC_MDIO_C22P register are reserved.
		 */
		tmp = readl(priv->ioaddr + XGMAC_MDIO_C22P);
		tmp &= ~MII_XGMAC_C22P_MASK;
	}
	/* Set port as Clause 22 */
	tmp |= BIT(phyaddr);
	writel(tmp, priv->ioaddr + XGMAC_MDIO_C22P);

	*hw_addr = (phyaddr << MII_XGMAC_PA_SHIFT) | (phyreg & 0x1f);
}

static int stmmac_xgmac2_mdio_read(struct stmmac_priv *priv, u32 addr,
				   u32 value)
{
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	int ret;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	/* Wait until any existing MII operation is complete */
	ret = stmmac_mdio_wait(priv->ioaddr + mii_data, MII_XGMAC_BUSY);
	if (ret)
		goto err_disable_clks;

	value |= priv->gmii_address_bus_config | MII_XGMAC_READ;

	/* Wait until any existing MII operation is complete */
	ret = stmmac_mdio_wait(priv->ioaddr + mii_data, MII_XGMAC_BUSY);
	if (ret)
		goto err_disable_clks;

	/* Set the MII address register to read */
	writel(addr, priv->ioaddr + mii_address);
	writel(value, priv->ioaddr + mii_data);

	/* Wait until any existing MII operation is complete */
	ret = stmmac_mdio_wait(priv->ioaddr + mii_data, MII_XGMAC_BUSY);
	if (ret)
		goto err_disable_clks;

	/* Read the data from the MII data register */
	ret = (int)readl(priv->ioaddr + mii_data) & GENMASK(15, 0);

err_disable_clks:
	pm_runtime_put(priv->device);

	return ret;
}

static int stmmac_xgmac2_mdio_read_c22(struct mii_bus *bus, int phyaddr,
				       int phyreg)
{
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	u32 addr;

	/* Until ver 2.20 XGMAC does not support C22 addr >= 4 */
	if (priv->synopsys_id < DWXGMAC_CORE_2_20 &&
	    phyaddr > MII_XGMAC_MAX_C22ADDR)
		return -ENODEV;

	stmmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);

	return stmmac_xgmac2_mdio_read(priv, addr, MII_XGMAC_BUSY);
}

static int stmmac_xgmac2_mdio_read_c45(struct mii_bus *bus, int phyaddr,
				       int devad, int phyreg)
{
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	u32 addr;

	stmmac_xgmac2_c45_format(priv, phyaddr, devad, phyreg, &addr);

	return stmmac_xgmac2_mdio_read(priv, addr, MII_XGMAC_BUSY);
}

static int stmmac_xgmac2_mdio_write(struct stmmac_priv *priv, u32 addr,
				    u32 value, u16 phydata)
{
	unsigned int mii_address = priv->hw->mii.addr;
	unsigned int mii_data = priv->hw->mii.data;
	int ret;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	/* Wait until any existing MII operation is complete */
	ret = stmmac_mdio_wait(priv->ioaddr + mii_data, MII_XGMAC_BUSY);
	if (ret)
		goto err_disable_clks;

	value |= priv->gmii_address_bus_config | phydata | MII_XGMAC_WRITE;

	/* Wait until any existing MII operation is complete */
	ret = stmmac_mdio_wait(priv->ioaddr + mii_data, MII_XGMAC_BUSY);
	if (ret)
		goto err_disable_clks;

	/* Set the MII address register to write */
	writel(addr, priv->ioaddr + mii_address);
	writel(value, priv->ioaddr + mii_data);

	/* Wait until any existing MII operation is complete */
	ret = stmmac_mdio_wait(priv->ioaddr + mii_data, MII_XGMAC_BUSY);

err_disable_clks:
	pm_runtime_put(priv->device);

	return ret;
}

static int stmmac_xgmac2_mdio_write_c22(struct mii_bus *bus, int phyaddr,
					int phyreg, u16 phydata)
{
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	u32 addr;

	/* Until ver 2.20 XGMAC does not support C22 addr >= 4 */
	if (priv->synopsys_id < DWXGMAC_CORE_2_20 &&
	    phyaddr > MII_XGMAC_MAX_C22ADDR)
		return -ENODEV;

	stmmac_xgmac2_c22_format(priv, phyaddr, phyreg, &addr);

	return stmmac_xgmac2_mdio_write(priv, addr,
					MII_XGMAC_BUSY | MII_XGMAC_SADDR, phydata);
}

static int stmmac_xgmac2_mdio_write_c45(struct mii_bus *bus, int phyaddr,
					int devad, int phyreg, u16 phydata)
{
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	u32 addr;

	stmmac_xgmac2_c45_format(priv, phyaddr, devad, phyreg, &addr);

	return stmmac_xgmac2_mdio_write(priv, addr, MII_XGMAC_BUSY,
					phydata);
}

/**
 * stmmac_mdio_format_addr() - format the address register
 * @priv: struct stmmac_priv pointer
 * @pa: 5-bit MDIO package address
 * @gr: 5-bit MDIO register address (C22) or MDIO device address (C45)
 *
 * Return: formatted address register
 */
static u32 stmmac_mdio_format_addr(struct stmmac_priv *priv,
				   unsigned int pa, unsigned int gr)
{
	const struct mii_regs *mii_regs = &priv->hw->mii;

	return ((pa << mii_regs->addr_shift) & mii_regs->addr_mask) |
	       ((gr << mii_regs->reg_shift) & mii_regs->reg_mask) |
	       priv->gmii_address_bus_config |
	       MII_ADDR_GBUSY;
}

static int stmmac_mdio_access(struct stmmac_priv *priv, unsigned int pa,
			      unsigned int gr, u32 cmd, u32 data, bool read)
{
	void __iomem *mii_address = priv->ioaddr + priv->hw->mii.addr;
	void __iomem *mii_data = priv->ioaddr + priv->hw->mii.data;
	u32 addr;
	int ret;

	ret = pm_runtime_resume_and_get(priv->device);
	if (ret < 0)
		return ret;

	ret = stmmac_mdio_wait(mii_address, MII_ADDR_GBUSY);
	if (ret)
		goto out;

	addr = stmmac_mdio_format_addr(priv, pa, gr) | cmd;

	writel(data, mii_data);
	writel(addr, mii_address);

	ret = stmmac_mdio_wait(mii_address, MII_ADDR_GBUSY);
	if (ret)
		goto out;

	/* Read the data from the MII data register if in read mode */
	ret = read ? readl(mii_data) & MII_DATA_GD_MASK : 0;

out:
	pm_runtime_put(priv->device);

	return ret;
}

static int stmmac_mdio_read(struct stmmac_priv *priv, unsigned int pa,
			    unsigned int gr, u32 cmd, int data)
{
	return stmmac_mdio_access(priv, pa, gr, cmd, data, true);
}

static int stmmac_mdio_write(struct stmmac_priv *priv, unsigned int pa,
			     unsigned int gr, u32 cmd, int data)
{
	return stmmac_mdio_access(priv, pa, gr, cmd, data, false);
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
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	u32 cmd;

	if (priv->plat->has_gmac4)
		cmd = MII_GMAC4_READ;
	else
		cmd = 0;

	return stmmac_mdio_read(priv, phyaddr, phyreg, cmd, 0);
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
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	int data = phyreg << MII_GMAC4_REG_ADDR_SHIFT;
	u32 cmd = MII_GMAC4_READ | MII_GMAC4_C45E;

	return stmmac_mdio_read(priv, phyaddr, devad, cmd, data);
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
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	u32 cmd;

	if (priv->plat->has_gmac4)
		cmd = MII_GMAC4_WRITE;
	else
		cmd = MII_ADDR_GWRITE;

	return stmmac_mdio_write(priv, phyaddr, phyreg, cmd, phydata);
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
	struct stmmac_priv *priv = netdev_priv(bus->priv);
	u32 cmd = MII_GMAC4_WRITE | MII_GMAC4_C45E;
	int data = phydata;

	data |= phyreg << MII_GMAC4_REG_ADDR_SHIFT;

	return stmmac_mdio_write(priv, phyaddr, devad, cmd, data);
}

/**
 * stmmac_mdio_reset
 * @bus: points to the mii_bus structure
 * Description: reset the MII bus
 */
int stmmac_mdio_reset(struct mii_bus *bus)
{
#if IS_ENABLED(CONFIG_STMMAC_PLATFORM)
	struct stmmac_priv *priv = netdev_priv(bus->priv);
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

int stmmac_pcs_setup(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);
	struct fwnode_handle *devnode, *pcsnode;
	struct dw_xpcs *xpcs = NULL;
	int addr, ret;

	devnode = priv->plat->port_node;

	if (priv->plat->pcs_init) {
		ret = priv->plat->pcs_init(priv);
	} else if (fwnode_property_present(devnode, "pcs-handle")) {
		pcsnode = fwnode_find_reference(devnode, "pcs-handle", 0);
		xpcs = xpcs_create_fwnode(pcsnode);
		fwnode_handle_put(pcsnode);
		ret = PTR_ERR_OR_ZERO(xpcs);
	} else if (priv->plat->mdio_bus_data &&
		   priv->plat->mdio_bus_data->pcs_mask) {
		addr = ffs(priv->plat->mdio_bus_data->pcs_mask) - 1;
		xpcs = xpcs_create_mdiodev(priv->mii, addr);
		ret = PTR_ERR_OR_ZERO(xpcs);
	} else {
		return 0;
	}

	if (ret)
		return dev_err_probe(priv->device, ret, "No xPCS found\n");

	if (xpcs)
		xpcs_config_eee_mult_fact(xpcs, priv->plat->mult_fact_100ns);

	priv->hw->xpcs = xpcs;

	return 0;
}

void stmmac_pcs_clean(struct net_device *ndev)
{
	struct stmmac_priv *priv = netdev_priv(ndev);

	if (priv->plat->pcs_exit)
		priv->plat->pcs_exit(priv);

	if (!priv->hw->xpcs)
		return;

	xpcs_destroy(priv->hw->xpcs);
	priv->hw->xpcs = NULL;
}

/**
 * stmmac_clk_csr_set - dynamically set the MDC clock
 * @priv: driver private structure
 * Description: this is to dynamically set the MDC clock according to the csr
 * clock input.
 * Return: MII register CR field value
 * Note:
 *	If a specific clk_csr value is passed from the platform
 *	this means that the CSR Clock Range selection cannot be
 *	changed at run-time and it is fixed (as reported in the driver
 *	documentation). Viceversa the driver will try to set the MDC
 *	clock dynamically according to the actual clock input.
 */
static u32 stmmac_clk_csr_set(struct stmmac_priv *priv)
{
	unsigned long clk_rate;
	u32 value = ~0;

	clk_rate = clk_get_rate(priv->plat->stmmac_clk);

	/* Platform provided default clk_csr would be assumed valid
	 * for all other cases except for the below mentioned ones.
	 * For values higher than the IEEE 802.3 specified frequency
	 * we can not estimate the proper divider as it is not known
	 * the frequency of clk_csr_i. So we do not change the default
	 * divider.
	 */
	if (clk_rate < CSR_F_35M)
		value = STMMAC_CSR_20_35M;
	else if (clk_rate < CSR_F_60M)
		value = STMMAC_CSR_35_60M;
	else if (clk_rate < CSR_F_100M)
		value = STMMAC_CSR_60_100M;
	else if (clk_rate < CSR_F_150M)
		value = STMMAC_CSR_100_150M;
	else if (clk_rate < CSR_F_250M)
		value = STMMAC_CSR_150_250M;
	else if (clk_rate <= CSR_F_300M)
		value = STMMAC_CSR_250_300M;
	else if (clk_rate < CSR_F_500M)
		value = STMMAC_CSR_300_500M;
	else if (clk_rate < CSR_F_800M)
		value = STMMAC_CSR_500_800M;

	if (priv->plat->flags & STMMAC_FLAG_HAS_SUN8I) {
		if (clk_rate > 160000000)
			value = 0x03;
		else if (clk_rate > 80000000)
			value = 0x02;
		else if (clk_rate > 40000000)
			value = 0x01;
		else
			value = 0;
	}

	if (priv->plat->has_xgmac) {
		if (clk_rate > 400000000)
			value = 0x5;
		else if (clk_rate > 350000000)
			value = 0x4;
		else if (clk_rate > 300000000)
			value = 0x3;
		else if (clk_rate > 250000000)
			value = 0x2;
		else if (clk_rate > 150000000)
			value = 0x1;
		else
			value = 0x0;
	}

	return value;
}

static void stmmac_mdio_bus_config(struct stmmac_priv *priv)
{
	u32 value;

	/* If a specific clk_csr value is passed from the platform, this means
	 * that the CSR Clock Range value should not be computed from the CSR
	 * clock.
	 */
	if (priv->plat->clk_csr >= 0)
		value = priv->plat->clk_csr;
	else
		value = stmmac_clk_csr_set(priv);

	value <<= priv->hw->mii.clk_csr_shift;

	if (value & ~priv->hw->mii.clk_csr_mask)
		dev_warn(priv->device,
			 "clk_csr value out of range (0x%08x exceeds mask 0x%08x), truncating\n",
			 value, priv->hw->mii.clk_csr_mask);

	priv->gmii_address_bus_config = value & priv->hw->mii.clk_csr_mask;
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
	struct stmmac_mdio_bus_data *mdio_bus_data = priv->plat->mdio_bus_data;
	struct device_node *mdio_node = priv->plat->mdio_node;
	struct device *dev = ndev->dev.parent;
	struct fwnode_handle *fixed_node;
	struct fwnode_handle *fwnode;
	int addr, found, max_addr;

	if (!mdio_bus_data)
		return 0;

	stmmac_mdio_bus_config(priv);

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

		if (priv->synopsys_id < DWXGMAC_CORE_2_20) {
			/* Right now only C22 phys are supported */
			max_addr = MII_XGMAC_MAX_C22ADDR + 1;

			/* Check if DT specified an unsupported phy addr */
			if (priv->plat->phy_addr > MII_XGMAC_MAX_C22ADDR)
				dev_err(dev, "Unsupported phy_addr (max=%d)\n",
					MII_XGMAC_MAX_C22ADDR);
		} else {
			/* XGMAC version 2.20 onwards support 32 phy addr */
			max_addr = PHY_MAX_ADDR;
		}
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
	new_bus->phy_mask = mdio_bus_data->phy_mask | mdio_bus_data->pcs_mask;
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
		stmmac_xgmac2_mdio_read_c45(new_bus, 0, 0, 0);

	/* If fixed-link is set, skip PHY scanning */
	fwnode = priv->plat->port_node;
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

	mdiobus_unregister(priv->mii);
	priv->mii->priv = NULL;
	mdiobus_free(priv->mii);
	priv->mii = NULL;

	return 0;
}

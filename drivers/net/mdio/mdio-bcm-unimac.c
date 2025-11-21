// SPDX-License-Identifier: GPL-2.0+
/*
 * Broadcom UniMAC MDIO bus controller driver
 *
 * Copyright (C) 2014-2017 Broadcom
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/phy.h>
#include <linux/platform_data/mdio-bcm-unimac.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#define MDIO_CMD		0x00
#define  MDIO_START_BUSY	(1 << 29)
#define  MDIO_READ_FAIL		(1 << 28)
#define  MDIO_RD		(2 << 26)
#define  MDIO_WR		(1 << 26)
#define  MDIO_PMD_SHIFT		21
#define  MDIO_PMD_MASK		0x1F
#define  MDIO_REG_SHIFT		16
#define  MDIO_REG_MASK		0x1F

#define MDIO_CFG		0x04
#define  MDIO_C22		(1 << 0)
#define  MDIO_C45		0
#define  MDIO_CLK_DIV_SHIFT	4
#define  MDIO_CLK_DIV_MASK	0x3F
#define  MDIO_SUPP_PREAMBLE	(1 << 12)

struct unimac_mdio_priv {
	struct mii_bus		*mii_bus;
	void __iomem		*base;
	int (*wait_func)	(void *wait_func_data);
	void			*wait_func_data;
	struct clk		*clk;
	u32			clk_freq;
};

static inline u32 unimac_mdio_readl(struct unimac_mdio_priv *priv, u32 offset)
{
	/* MIPS chips strapped for BE will automagically configure the
	 * peripheral registers for CPU-native byte order.
	 */
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return __raw_readl(priv->base + offset);
	else
		return readl_relaxed(priv->base + offset);
}

static inline void unimac_mdio_writel(struct unimac_mdio_priv *priv, u32 val,
				      u32 offset)
{
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + offset);
	else
		writel_relaxed(val, priv->base + offset);
}

static inline void unimac_mdio_start(struct unimac_mdio_priv *priv)
{
	u32 reg;

	reg = unimac_mdio_readl(priv, MDIO_CMD);
	reg |= MDIO_START_BUSY;
	unimac_mdio_writel(priv, reg, MDIO_CMD);
}

static int unimac_mdio_poll(void *wait_func_data)
{
	struct unimac_mdio_priv *priv = wait_func_data;
	u32 val;

	/*
	 * C22 transactions should take ~25 usec, will need to adjust
	 * if C45 support is added.
	 */
	udelay(30);

	return read_poll_timeout(unimac_mdio_readl, val, !(val & MDIO_START_BUSY),
				 2000, 100000, false, priv, MDIO_CMD);
}

static int unimac_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct unimac_mdio_priv *priv = bus->priv;
	int ret;
	u32 cmd;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	/* Prepare the read operation */
	cmd = MDIO_RD | (phy_id << MDIO_PMD_SHIFT) | (reg << MDIO_REG_SHIFT);
	unimac_mdio_writel(priv, cmd, MDIO_CMD);

	/* Start MDIO transaction */
	unimac_mdio_start(priv);

	ret = priv->wait_func(priv->wait_func_data);
	if (ret)
		goto out;

	cmd = unimac_mdio_readl(priv, MDIO_CMD);

	/* Some broken devices are known not to release the line during
	 * turn-around, e.g: Broadcom BCM53125 external switches, so check for
	 * that condition here and ignore the MDIO controller read failure
	 * indication.
	 */
	if (!(bus->phy_ignore_ta_mask & 1 << phy_id) && (cmd & MDIO_READ_FAIL)) {
		ret = -EIO;
		goto out;
	}

	ret = cmd & 0xffff;
out:
	clk_disable_unprepare(priv->clk);
	return ret;
}

static int unimac_mdio_write(struct mii_bus *bus, int phy_id,
			     int reg, u16 val)
{
	struct unimac_mdio_priv *priv = bus->priv;
	u32 cmd;
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	/* Prepare the write operation */
	cmd = MDIO_WR | (phy_id << MDIO_PMD_SHIFT) |
		(reg << MDIO_REG_SHIFT) | (0xffff & val);
	unimac_mdio_writel(priv, cmd, MDIO_CMD);

	unimac_mdio_start(priv);

	ret = priv->wait_func(priv->wait_func_data);
	clk_disable_unprepare(priv->clk);

	return ret;
}

/* Workaround for integrated BCM7xxx Gigabit PHYs which have a problem with
 * their internal MDIO management controller making them fail to successfully
 * be read from or written to for the first transaction.  We insert a dummy
 * BMSR read here to make sure that phy_get_device() and get_phy_id() can
 * correctly read the PHY MII_PHYSID1/2 registers and successfully register a
 * PHY device for this peripheral.
 *
 * Once the PHY driver is registered, we can workaround subsequent reads from
 * there (e.g: during system-wide power management).
 *
 * bus->reset is invoked before mdiobus_scan during mdiobus_register and is
 * therefore the right location to stick that workaround. Since we do not want
 * to read from non-existing PHYs, we either use bus->phy_mask or do a manual
 * Device Tree scan to limit the search area.
 */
static int unimac_mdio_reset(struct mii_bus *bus)
{
	struct device_node *np = bus->dev.of_node;
	struct device_node *child;
	u32 read_mask = 0;
	int addr;

	if (!np) {
		read_mask = ~bus->phy_mask;
	} else {
		for_each_available_child_of_node(np, child) {
			addr = of_mdio_parse_addr(&bus->dev, child);
			if (addr < 0)
				continue;

			read_mask |= 1 << addr;
		}
	}

	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		if (read_mask & 1 << addr) {
			dev_dbg(&bus->dev, "Workaround for PHY @ %d\n", addr);
			mdiobus_read(bus, addr, MII_BMSR);
		}
	}

	return 0;
}

static int unimac_mdio_clk_set(struct unimac_mdio_priv *priv)
{
	unsigned long rate;
	u32 reg, div;
	int ret;

	/* Keep the hardware default values */
	if (!priv->clk_freq)
		return 0;

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		return ret;

	rate = clk_get_rate(priv->clk);
	if (!rate)
		rate = 250000000;

	div = (rate / (2 * priv->clk_freq)) - 1;
	if (div & ~MDIO_CLK_DIV_MASK) {
		dev_warn(priv->mii_bus->parent,
			 "Ignoring MDIO clock frequency request: %d vs. rate: %ld\n",
			 priv->clk_freq, rate);
		ret = 0;
		goto out;
	}

	/* The MDIO clock is the reference clock (typically 250Mhz) divided by
	 * 2 x (MDIO_CLK_DIV + 1)
	 */
	reg = unimac_mdio_readl(priv, MDIO_CFG);
	reg &= ~(MDIO_CLK_DIV_MASK << MDIO_CLK_DIV_SHIFT);
	reg |= div << MDIO_CLK_DIV_SHIFT;
	unimac_mdio_writel(priv, reg, MDIO_CFG);
out:
	clk_disable_unprepare(priv->clk);
	return ret;
}

static int unimac_mdio_probe(struct platform_device *pdev)
{
	struct unimac_mdio_pdata *pdata = pdev->dev.platform_data;
	struct unimac_mdio_priv *priv;
	struct device_node *np;
	struct mii_bus *bus;
	struct resource *r;
	int ret;

	np = pdev->dev.of_node;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -EINVAL;

	/* Just ioremap, as this MDIO block is usually integrated into an
	 * Ethernet MAC controller register range
	 */
	priv->base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!priv->base) {
		dev_err(&pdev->dev, "failed to remap register\n");
		return -ENOMEM;
	}

	if (of_property_read_u32(np, "clock-frequency", &priv->clk_freq))
		priv->clk_freq = 0;

	priv->mii_bus = mdiobus_alloc();
	if (!priv->mii_bus)
		return -ENOMEM;

	bus = priv->mii_bus;
	bus->priv = priv;
	if (pdata) {
		bus->name = pdata->bus_name;
		priv->wait_func = pdata->wait_func;
		priv->wait_func_data = pdata->wait_func_data;
		bus->phy_mask = ~pdata->phy_mask;
		priv->clk = pdata->clk;
	} else {
		bus->name = "unimac MII bus";
		priv->wait_func_data = priv;
		priv->wait_func = unimac_mdio_poll;
		priv->clk = devm_clk_get_optional(&pdev->dev, NULL);
	}

	if (IS_ERR(priv->clk)) {
		ret = PTR_ERR(priv->clk);
		goto out_mdio_free;
	}

	bus->parent = &pdev->dev;
	bus->read = unimac_mdio_read;
	bus->write = unimac_mdio_write;
	bus->reset = unimac_mdio_reset;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-%d", pdev->name, pdev->id);

	ret = unimac_mdio_clk_set(priv);
	if (ret)
		goto out_mdio_free;

	ret = of_mdiobus_register(bus, np);
	if (ret) {
		dev_err(&pdev->dev, "MDIO bus registration failed\n");
		goto out_mdio_free;
	}

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "Broadcom UniMAC MDIO bus\n");

	return 0;

out_mdio_free:
	mdiobus_free(bus);
	return ret;
}

static void unimac_mdio_remove(struct platform_device *pdev)
{
	struct unimac_mdio_priv *priv = platform_get_drvdata(pdev);

	mdiobus_unregister(priv->mii_bus);
	mdiobus_free(priv->mii_bus);
}

static int __maybe_unused unimac_mdio_resume(struct device *d)
{
	struct unimac_mdio_priv *priv = dev_get_drvdata(d);

	return unimac_mdio_clk_set(priv);
}

static SIMPLE_DEV_PM_OPS(unimac_mdio_pm_ops,
			 NULL, unimac_mdio_resume);

static const struct of_device_id unimac_mdio_ids[] = {
	{ .compatible = "brcm,asp-v3.0-mdio", },
	{ .compatible = "brcm,asp-v2.2-mdio", },
	{ .compatible = "brcm,asp-v2.1-mdio", },
	{ .compatible = "brcm,bcm6846-mdio", },
	{ .compatible = "brcm,genet-mdio-v5", },
	{ .compatible = "brcm,genet-mdio-v4", },
	{ .compatible = "brcm,genet-mdio-v3", },
	{ .compatible = "brcm,genet-mdio-v2", },
	{ .compatible = "brcm,genet-mdio-v1", },
	{ .compatible = "brcm,unimac-mdio", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, unimac_mdio_ids);

static struct platform_driver unimac_mdio_driver = {
	.driver = {
		.name = UNIMAC_MDIO_DRV_NAME,
		.of_match_table = unimac_mdio_ids,
		.pm = &unimac_mdio_pm_ops,
	},
	.probe	= unimac_mdio_probe,
	.remove = unimac_mdio_remove,
};
module_platform_driver(unimac_mdio_driver);

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom UniMAC MDIO bus controller");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" UNIMAC_MDIO_DRV_NAME);

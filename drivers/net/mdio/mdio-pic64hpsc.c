// SPDX-License-Identifier: GPL-2.0
/* Microchip PIC64-HPSC/HX MDIO controller driver
 *
 * Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_mdio.h>
#include <linux/platform_device.h>

#define MDIO_REG_PRESCALER     0x20
#define MDIO_CFG_PRESCALE_MASK GENMASK(7, 0)

#define MDIO_REG_FRAME_CFG_1 0x24
#define MDIO_WDATA_MASK	     GENMASK(15, 0)

#define MDIO_REG_FRAME_CFG_2	 0x28
#define MDIO_TRIGGER_BIT	 BIT(31)
#define MDIO_REG_DEV_ADDR_MASK	 GENMASK(20, 16)
#define MDIO_PHY_PRT_ADDR_MASK	 GENMASK(8, 4)
#define MDIO_OPERATION_MASK	 GENMASK(3, 2)
#define MDIO_START_OF_FRAME_MASK GENMASK(1, 0)

/* Possible value of MDIO_OPERATION_MASK */
#define MDIO_OPERATION_WRITE BIT(0)
#define MDIO_OPERATION_READ  BIT(1)

#define MDIO_REG_FRAME_STATUS 0x2C
#define MDIO_READOK_BIT	      BIT(24)
#define MDIO_RDATA_MASK	      GENMASK(15, 0)

struct pic64hpsc_mdio_dev {
	void __iomem *regs;
};

static int pic64hpsc_mdio_wait_trigger(struct mii_bus *bus)
{
	struct pic64hpsc_mdio_dev *priv = bus->priv;
	u32 val;
	int ret;

	/* The MDIO_TRIGGER bit returns 0 when a transaction has completed. */
	ret = readl_poll_timeout(priv->regs + MDIO_REG_FRAME_CFG_2, val,
				 !(val & MDIO_TRIGGER_BIT), 50, 10000);

	if (ret < 0)
		dev_dbg(&bus->dev, "TRIGGER bit timeout: %x\n", val);

	return ret;
}

static int pic64hpsc_mdio_c22_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct pic64hpsc_mdio_dev *priv = bus->priv;
	u32 val;
	int ret;

	ret = pic64hpsc_mdio_wait_trigger(bus);
	if (ret)
		return ret;

	writel(MDIO_TRIGGER_BIT | FIELD_PREP(MDIO_REG_DEV_ADDR_MASK, regnum) |
		       FIELD_PREP(MDIO_PHY_PRT_ADDR_MASK, mii_id) |
		       FIELD_PREP(MDIO_OPERATION_MASK, MDIO_OPERATION_READ) |
		       FIELD_PREP(MDIO_START_OF_FRAME_MASK, 1),
	       priv->regs + MDIO_REG_FRAME_CFG_2);

	ret = pic64hpsc_mdio_wait_trigger(bus);
	if (ret)
		return ret;

	val = readl(priv->regs + MDIO_REG_FRAME_STATUS);

	/* The MDIO_READOK is a 1-bit value reflecting the inverse of the MDIO
	 * bus value captured during the 2nd TA cycle. A PHY/Port should drive
	 * the MDIO bus with a logic 0 on the 2nd TA cycle, however, the
	 * PHY/Port could optionally drive a logic 1, to communicate a read
	 * failure. This feature is optional, not defined by the 802.3 standard
	 * and not supported in standard external PHYs.
	 */
	if (!(bus->phy_ignore_ta_mask & 1 << mii_id) &&
	    !FIELD_GET(MDIO_READOK_BIT, val)) {
		dev_dbg(&bus->dev, "READOK bit cleared\n");
		return -EIO;
	}

	return FIELD_GET(MDIO_RDATA_MASK, val);
}

static int pic64hpsc_mdio_c22_write(struct mii_bus *bus, int mii_id, int regnum,
				    u16 value)
{
	struct pic64hpsc_mdio_dev *priv = bus->priv;
	int ret;

	ret = pic64hpsc_mdio_wait_trigger(bus);
	if (ret < 0)
		return ret;

	writel(FIELD_PREP(MDIO_WDATA_MASK, value),
	       priv->regs + MDIO_REG_FRAME_CFG_1);

	writel(MDIO_TRIGGER_BIT | FIELD_PREP(MDIO_REG_DEV_ADDR_MASK, regnum) |
		       FIELD_PREP(MDIO_PHY_PRT_ADDR_MASK, mii_id) |
		       FIELD_PREP(MDIO_OPERATION_MASK, MDIO_OPERATION_WRITE) |
		       FIELD_PREP(MDIO_START_OF_FRAME_MASK, 1),
	       priv->regs + MDIO_REG_FRAME_CFG_2);

	return 0;
}

static int pic64hpsc_mdio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct pic64hpsc_mdio_dev *priv;
	struct mii_bus *bus;
	unsigned long rate;
	struct clk *clk;
	u32 bus_freq;
	u32 div;
	int ret;

	bus = devm_mdiobus_alloc_size(dev, sizeof(*priv));
	if (!bus)
		return -ENOMEM;

	priv = bus->priv;

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	bus->name = KBUILD_MODNAME;
	bus->read = pic64hpsc_mdio_c22_read;
	bus->write = pic64hpsc_mdio_c22_write;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));
	bus->parent = dev;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (of_property_read_u32(np, "clock-frequency", &bus_freq))
		bus_freq = 2500000;

	rate = clk_get_rate(clk);

	div = DIV_ROUND_UP(rate, 2 * bus_freq) - 1;
	if (div == 0 || div & ~MDIO_CFG_PRESCALE_MASK) {
		dev_err(dev, "MDIO clock-frequency out of range\n");
		return -EINVAL;
	}

	dev_dbg(dev, "rate=%lu bus_freq=%u real_bus_freq=%lu div=%u\n", rate,
		bus_freq, rate / (2 * (1 + div)), div);
	writel(div, priv->regs + MDIO_REG_PRESCALER);

	ret = devm_of_mdiobus_register(dev, bus, np);
	if (ret) {
		dev_err(dev, "Cannot register MDIO bus (%d)\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id pic64hpsc_mdio_match[] = {
	{ .compatible = "microchip,pic64hpsc-mdio" },
	{}
};
MODULE_DEVICE_TABLE(of, pic64hpsc_mdio_match);

static struct platform_driver pic64hpsc_mdio_driver = {
	.probe = pic64hpsc_mdio_probe,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = pic64hpsc_mdio_match,
	},
};
module_platform_driver(pic64hpsc_mdio_driver);

MODULE_AUTHOR("Charles Perry <charles.perry@microchip.com>");
MODULE_DESCRIPTION("Microchip PIC64-HPSC/HX MDIO driver");
MODULE_LICENSE("GPL");

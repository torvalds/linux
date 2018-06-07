/*
 * Driver for the MDIO interface of Marvell network interfaces.
 *
 * Since the MDIO interface of Marvell network interfaces is shared
 * between all network interfaces, having a single driver allows to
 * handle concurrent accesses properly (you may have four Ethernet
 * ports, but they in fact share the same SMI interface to access
 * the MDIO bus). This driver is currently used by the mvneta and
 * mv643xx_eth drivers.
 *
 * Copyright (C) 2012 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/wait.h>

#define MVMDIO_SMI_DATA_SHIFT		0
#define MVMDIO_SMI_PHY_ADDR_SHIFT	16
#define MVMDIO_SMI_PHY_REG_SHIFT	21
#define MVMDIO_SMI_READ_OPERATION	BIT(26)
#define MVMDIO_SMI_WRITE_OPERATION	0
#define MVMDIO_SMI_READ_VALID		BIT(27)
#define MVMDIO_SMI_BUSY			BIT(28)
#define MVMDIO_ERR_INT_CAUSE		0x007C
#define  MVMDIO_ERR_INT_SMI_DONE	0x00000010
#define MVMDIO_ERR_INT_MASK		0x0080

#define MVMDIO_XSMI_MGNT_REG		0x0
#define  MVMDIO_XSMI_PHYADDR_SHIFT	16
#define  MVMDIO_XSMI_DEVADDR_SHIFT	21
#define  MVMDIO_XSMI_WRITE_OPERATION	(0x5 << 26)
#define  MVMDIO_XSMI_READ_OPERATION	(0x7 << 26)
#define  MVMDIO_XSMI_READ_VALID		BIT(29)
#define  MVMDIO_XSMI_BUSY		BIT(30)
#define MVMDIO_XSMI_ADDR_REG		0x8

/*
 * SMI Timeout measurements:
 * - Kirkwood 88F6281 (Globalscale Dreamplug): 45us to 95us (Interrupt)
 * - Armada 370       (Globalscale Mirabox):   41us to 43us (Polled)
 */
#define MVMDIO_SMI_TIMEOUT		1000 /* 1000us = 1ms */
#define MVMDIO_SMI_POLL_INTERVAL_MIN	45
#define MVMDIO_SMI_POLL_INTERVAL_MAX	55

#define MVMDIO_XSMI_POLL_INTERVAL_MIN	150
#define MVMDIO_XSMI_POLL_INTERVAL_MAX	160

struct orion_mdio_dev {
	void __iomem *regs;
	struct clk *clk[3];
	/*
	 * If we have access to the error interrupt pin (which is
	 * somewhat misnamed as it not only reflects internal errors
	 * but also reflects SMI completion), use that to wait for
	 * SMI access completion instead of polling the SMI busy bit.
	 */
	int err_interrupt;
	wait_queue_head_t smi_busy_wait;
};

enum orion_mdio_bus_type {
	BUS_TYPE_SMI,
	BUS_TYPE_XSMI
};

struct orion_mdio_ops {
	int (*is_done)(struct orion_mdio_dev *);
	unsigned int poll_interval_min;
	unsigned int poll_interval_max;
};

/* Wait for the SMI unit to be ready for another operation
 */
static int orion_mdio_wait_ready(const struct orion_mdio_ops *ops,
				 struct mii_bus *bus)
{
	struct orion_mdio_dev *dev = bus->priv;
	unsigned long timeout = usecs_to_jiffies(MVMDIO_SMI_TIMEOUT);
	unsigned long end = jiffies + timeout;
	int timedout = 0;

	while (1) {
	        if (ops->is_done(dev))
			return 0;
	        else if (timedout)
			break;

	        if (dev->err_interrupt <= 0) {
			usleep_range(ops->poll_interval_min,
				     ops->poll_interval_max);

			if (time_is_before_jiffies(end))
				++timedout;
	        } else {
			/* wait_event_timeout does not guarantee a delay of at
			 * least one whole jiffie, so timeout must be no less
			 * than two.
			 */
			if (timeout < 2)
				timeout = 2;
			wait_event_timeout(dev->smi_busy_wait,
				           ops->is_done(dev), timeout);

			++timedout;
	        }
	}

	dev_err(bus->parent, "Timeout: SMI busy for too long\n");
	return  -ETIMEDOUT;
}

static int orion_mdio_smi_is_done(struct orion_mdio_dev *dev)
{
	return !(readl(dev->regs) & MVMDIO_SMI_BUSY);
}

static const struct orion_mdio_ops orion_mdio_smi_ops = {
	.is_done = orion_mdio_smi_is_done,
	.poll_interval_min = MVMDIO_SMI_POLL_INTERVAL_MIN,
	.poll_interval_max = MVMDIO_SMI_POLL_INTERVAL_MAX,
};

static int orion_mdio_smi_read(struct mii_bus *bus, int mii_id,
			       int regnum)
{
	struct orion_mdio_dev *dev = bus->priv;
	u32 val;
	int ret;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	ret = orion_mdio_wait_ready(&orion_mdio_smi_ops, bus);
	if (ret < 0)
		return ret;

	writel(((mii_id << MVMDIO_SMI_PHY_ADDR_SHIFT) |
		(regnum << MVMDIO_SMI_PHY_REG_SHIFT)  |
		MVMDIO_SMI_READ_OPERATION),
	       dev->regs);

	ret = orion_mdio_wait_ready(&orion_mdio_smi_ops, bus);
	if (ret < 0)
		return ret;

	val = readl(dev->regs);
	if (!(val & MVMDIO_SMI_READ_VALID)) {
		dev_err(bus->parent, "SMI bus read not valid\n");
		return -ENODEV;
	}

	return val & GENMASK(15, 0);
}

static int orion_mdio_smi_write(struct mii_bus *bus, int mii_id,
				int regnum, u16 value)
{
	struct orion_mdio_dev *dev = bus->priv;
	int ret;

	if (regnum & MII_ADDR_C45)
		return -EOPNOTSUPP;

	ret = orion_mdio_wait_ready(&orion_mdio_smi_ops, bus);
	if (ret < 0)
		return ret;

	writel(((mii_id << MVMDIO_SMI_PHY_ADDR_SHIFT) |
		(regnum << MVMDIO_SMI_PHY_REG_SHIFT)  |
		MVMDIO_SMI_WRITE_OPERATION            |
		(value << MVMDIO_SMI_DATA_SHIFT)),
	       dev->regs);

	return 0;
}

static int orion_mdio_xsmi_is_done(struct orion_mdio_dev *dev)
{
	return !(readl(dev->regs + MVMDIO_XSMI_MGNT_REG) & MVMDIO_XSMI_BUSY);
}

static const struct orion_mdio_ops orion_mdio_xsmi_ops = {
	.is_done = orion_mdio_xsmi_is_done,
	.poll_interval_min = MVMDIO_XSMI_POLL_INTERVAL_MIN,
	.poll_interval_max = MVMDIO_XSMI_POLL_INTERVAL_MAX,
};

static int orion_mdio_xsmi_read(struct mii_bus *bus, int mii_id,
				int regnum)
{
	struct orion_mdio_dev *dev = bus->priv;
	u16 dev_addr = (regnum >> 16) & GENMASK(4, 0);
	int ret;

	if (!(regnum & MII_ADDR_C45))
		return -EOPNOTSUPP;

	ret = orion_mdio_wait_ready(&orion_mdio_xsmi_ops, bus);
	if (ret < 0)
		return ret;

	writel(regnum & GENMASK(15, 0), dev->regs + MVMDIO_XSMI_ADDR_REG);
	writel((mii_id << MVMDIO_XSMI_PHYADDR_SHIFT) |
	       (dev_addr << MVMDIO_XSMI_DEVADDR_SHIFT) |
	       MVMDIO_XSMI_READ_OPERATION,
	       dev->regs + MVMDIO_XSMI_MGNT_REG);

	ret = orion_mdio_wait_ready(&orion_mdio_xsmi_ops, bus);
	if (ret < 0)
		return ret;

	if (!(readl(dev->regs + MVMDIO_XSMI_MGNT_REG) &
	      MVMDIO_XSMI_READ_VALID)) {
		dev_err(bus->parent, "XSMI bus read not valid\n");
		return -ENODEV;
	}

	return readl(dev->regs + MVMDIO_XSMI_MGNT_REG) & GENMASK(15, 0);
}

static int orion_mdio_xsmi_write(struct mii_bus *bus, int mii_id,
				int regnum, u16 value)
{
	struct orion_mdio_dev *dev = bus->priv;
	u16 dev_addr = (regnum >> 16) & GENMASK(4, 0);
	int ret;

	if (!(regnum & MII_ADDR_C45))
		return -EOPNOTSUPP;

	ret = orion_mdio_wait_ready(&orion_mdio_xsmi_ops, bus);
	if (ret < 0)
		return ret;

	writel(regnum & GENMASK(15, 0), dev->regs + MVMDIO_XSMI_ADDR_REG);
	writel((mii_id << MVMDIO_XSMI_PHYADDR_SHIFT) |
	       (dev_addr << MVMDIO_XSMI_DEVADDR_SHIFT) |
	       MVMDIO_XSMI_WRITE_OPERATION | value,
	       dev->regs + MVMDIO_XSMI_MGNT_REG);

	return 0;
}

static irqreturn_t orion_mdio_err_irq(int irq, void *dev_id)
{
	struct orion_mdio_dev *dev = dev_id;

	if (readl(dev->regs + MVMDIO_ERR_INT_CAUSE) &
			MVMDIO_ERR_INT_SMI_DONE) {
		writel(~MVMDIO_ERR_INT_SMI_DONE,
				dev->regs + MVMDIO_ERR_INT_CAUSE);
		wake_up(&dev->smi_busy_wait);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int orion_mdio_probe(struct platform_device *pdev)
{
	enum orion_mdio_bus_type type;
	struct resource *r;
	struct mii_bus *bus;
	struct orion_mdio_dev *dev;
	int i, ret;

	type = (enum orion_mdio_bus_type)of_device_get_match_data(&pdev->dev);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "No SMI register address given\n");
		return -ENODEV;
	}

	bus = devm_mdiobus_alloc_size(&pdev->dev,
				      sizeof(struct orion_mdio_dev));
	if (!bus)
		return -ENOMEM;

	switch (type) {
	case BUS_TYPE_SMI:
		bus->read = orion_mdio_smi_read;
		bus->write = orion_mdio_smi_write;
		break;
	case BUS_TYPE_XSMI:
		bus->read = orion_mdio_xsmi_read;
		bus->write = orion_mdio_xsmi_write;
		break;
	}

	bus->name = "orion_mdio_bus";
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s-mii",
		 dev_name(&pdev->dev));
	bus->parent = &pdev->dev;

	dev = bus->priv;
	dev->regs = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!dev->regs) {
		dev_err(&pdev->dev, "Unable to remap SMI register\n");
		return -ENODEV;
	}

	init_waitqueue_head(&dev->smi_busy_wait);

	for (i = 0; i < ARRAY_SIZE(dev->clk); i++) {
		dev->clk[i] = of_clk_get(pdev->dev.of_node, i);
		if (IS_ERR(dev->clk[i]))
			break;
		clk_prepare_enable(dev->clk[i]);
	}

	dev->err_interrupt = platform_get_irq(pdev, 0);
	if (dev->err_interrupt > 0 &&
	    resource_size(r) < MVMDIO_ERR_INT_MASK + 4) {
		dev_err(&pdev->dev,
			"disabling interrupt, resource size is too small\n");
		dev->err_interrupt = 0;
	}
	if (dev->err_interrupt > 0) {
		ret = devm_request_irq(&pdev->dev, dev->err_interrupt,
					orion_mdio_err_irq,
					IRQF_SHARED, pdev->name, dev);
		if (ret)
			goto out_mdio;

		writel(MVMDIO_ERR_INT_SMI_DONE,
			dev->regs + MVMDIO_ERR_INT_MASK);

	} else if (dev->err_interrupt == -EPROBE_DEFER) {
		ret = -EPROBE_DEFER;
		goto out_mdio;
	}

	ret = of_mdiobus_register(bus, pdev->dev.of_node);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot register MDIO bus (%d)\n", ret);
		goto out_mdio;
	}

	platform_set_drvdata(pdev, bus);

	return 0;

out_mdio:
	if (dev->err_interrupt > 0)
		writel(0, dev->regs + MVMDIO_ERR_INT_MASK);

	for (i = 0; i < ARRAY_SIZE(dev->clk); i++) {
		if (IS_ERR(dev->clk[i]))
			break;
		clk_disable_unprepare(dev->clk[i]);
		clk_put(dev->clk[i]);
	}

	return ret;
}

static int orion_mdio_remove(struct platform_device *pdev)
{
	struct mii_bus *bus = platform_get_drvdata(pdev);
	struct orion_mdio_dev *dev = bus->priv;
	int i;

	if (dev->err_interrupt > 0)
		writel(0, dev->regs + MVMDIO_ERR_INT_MASK);
	mdiobus_unregister(bus);

	for (i = 0; i < ARRAY_SIZE(dev->clk); i++) {
		if (IS_ERR(dev->clk[i]))
			break;
		clk_disable_unprepare(dev->clk[i]);
		clk_put(dev->clk[i]);
	}

	return 0;
}

static const struct of_device_id orion_mdio_match[] = {
	{ .compatible = "marvell,orion-mdio", .data = (void *)BUS_TYPE_SMI },
	{ .compatible = "marvell,xmdio", .data = (void *)BUS_TYPE_XSMI },
	{ }
};
MODULE_DEVICE_TABLE(of, orion_mdio_match);

static struct platform_driver orion_mdio_driver = {
	.probe = orion_mdio_probe,
	.remove = orion_mdio_remove,
	.driver = {
		.name = "orion-mdio",
		.of_match_table = orion_mdio_match,
	},
};

module_platform_driver(orion_mdio_driver);

MODULE_DESCRIPTION("Marvell MDIO interface driver");
MODULE_AUTHOR("Thomas Petazzoni <thomas.petazzoni@free-electrons.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:orion-mdio");

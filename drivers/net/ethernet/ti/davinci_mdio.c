/*
 * DaVinci MDIO Module driver
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * Shamelessly ripped out of davinci_emac.c, original copyrights follow:
 *
 * Copyright (C) 2009 Texas Instruments.
 *
 * ---------------------------------------------------------------------------
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ---------------------------------------------------------------------------
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/phy.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/davinci_emac.h>
#include <linux/of.h>
#include <linux/of_device.h>

/*
 * This timeout definition is a worst-case ultra defensive measure against
 * unexpected controller lock ups.  Ideally, we should never ever hit this
 * scenario in practice.
 */
#define MDIO_TIMEOUT		100 /* msecs */

#define PHY_REG_MASK		0x1f
#define PHY_ID_MASK		0x1f

#define DEF_OUT_FREQ		2200000		/* 2.2 MHz */

struct davinci_mdio_regs {
	u32	version;
	u32	control;
#define CONTROL_IDLE		BIT(31)
#define CONTROL_ENABLE		BIT(30)
#define CONTROL_MAX_DIV		(0xffff)

	u32	alive;
	u32	link;
	u32	linkintraw;
	u32	linkintmasked;
	u32	__reserved_0[2];
	u32	userintraw;
	u32	userintmasked;
	u32	userintmaskset;
	u32	userintmaskclr;
	u32	__reserved_1[20];

	struct {
		u32	access;
#define USERACCESS_GO		BIT(31)
#define USERACCESS_WRITE	BIT(30)
#define USERACCESS_ACK		BIT(29)
#define USERACCESS_READ		(0)
#define USERACCESS_DATA		(0xffff)

		u32	physel;
	}	user[0];
};

struct mdio_platform_data default_pdata = {
	.bus_freq = DEF_OUT_FREQ,
};

struct davinci_mdio_data {
	struct mdio_platform_data pdata;
	struct davinci_mdio_regs __iomem *regs;
	spinlock_t	lock;
	struct clk	*clk;
	struct device	*dev;
	struct mii_bus	*bus;
	bool		suspended;
	unsigned long	access_time; /* jiffies */
};

static void __davinci_mdio_reset(struct davinci_mdio_data *data)
{
	u32 mdio_in, div, mdio_out_khz, access_time;

	mdio_in = clk_get_rate(data->clk);
	div = (mdio_in / data->pdata.bus_freq) - 1;
	if (div > CONTROL_MAX_DIV)
		div = CONTROL_MAX_DIV;

	/* set enable and clock divider */
	__raw_writel(div | CONTROL_ENABLE, &data->regs->control);

	/*
	 * One mdio transaction consists of:
	 *	32 bits of preamble
	 *	32 bits of transferred data
	 *	24 bits of bus yield (not needed unless shared?)
	 */
	mdio_out_khz = mdio_in / (1000 * (div + 1));
	access_time  = (88 * 1000) / mdio_out_khz;

	/*
	 * In the worst case, we could be kicking off a user-access immediately
	 * after the mdio bus scan state-machine triggered its own read.  If
	 * so, our request could get deferred by one access cycle.  We
	 * defensively allow for 4 access cycles.
	 */
	data->access_time = usecs_to_jiffies(access_time * 4);
	if (!data->access_time)
		data->access_time = 1;
}

static int davinci_mdio_reset(struct mii_bus *bus)
{
	struct davinci_mdio_data *data = bus->priv;
	u32 phy_mask, ver;

	__davinci_mdio_reset(data);

	/* wait for scan logic to settle */
	msleep(PHY_MAX_ADDR * data->access_time);

	/* dump hardware version info */
	ver = __raw_readl(&data->regs->version);
	dev_info(data->dev, "davinci mdio revision %d.%d\n",
		 (ver >> 8) & 0xff, ver & 0xff);

	/* get phy mask from the alive register */
	phy_mask = __raw_readl(&data->regs->alive);
	if (phy_mask) {
		/* restrict mdio bus to live phys only */
		dev_info(data->dev, "detected phy mask %x\n", ~phy_mask);
		phy_mask = ~phy_mask;
	} else {
		/* desperately scan all phys */
		dev_warn(data->dev, "no live phy, scanning all\n");
		phy_mask = 0;
	}
	data->bus->phy_mask = phy_mask;

	return 0;
}

/* wait until hardware is ready for another user access */
static inline int wait_for_user_access(struct davinci_mdio_data *data)
{
	struct davinci_mdio_regs __iomem *regs = data->regs;
	unsigned long timeout = jiffies + msecs_to_jiffies(MDIO_TIMEOUT);
	u32 reg;

	while (time_after(timeout, jiffies)) {
		reg = __raw_readl(&regs->user[0].access);
		if ((reg & USERACCESS_GO) == 0)
			return 0;

		reg = __raw_readl(&regs->control);
		if ((reg & CONTROL_IDLE) == 0)
			continue;

		/*
		 * An emac soft_reset may have clobbered the mdio controller's
		 * state machine.  We need to reset and retry the current
		 * operation
		 */
		dev_warn(data->dev, "resetting idled controller\n");
		__davinci_mdio_reset(data);
		return -EAGAIN;
	}

	reg = __raw_readl(&regs->user[0].access);
	if ((reg & USERACCESS_GO) == 0)
		return 0;

	dev_err(data->dev, "timed out waiting for user access\n");
	return -ETIMEDOUT;
}

/* wait until hardware state machine is idle */
static inline int wait_for_idle(struct davinci_mdio_data *data)
{
	struct davinci_mdio_regs __iomem *regs = data->regs;
	unsigned long timeout = jiffies + msecs_to_jiffies(MDIO_TIMEOUT);

	while (time_after(timeout, jiffies)) {
		if (__raw_readl(&regs->control) & CONTROL_IDLE)
			return 0;
	}
	dev_err(data->dev, "timed out waiting for idle\n");
	return -ETIMEDOUT;
}

static int davinci_mdio_read(struct mii_bus *bus, int phy_id, int phy_reg)
{
	struct davinci_mdio_data *data = bus->priv;
	u32 reg;
	int ret;

	if (phy_reg & ~PHY_REG_MASK || phy_id & ~PHY_ID_MASK)
		return -EINVAL;

	spin_lock(&data->lock);

	if (data->suspended) {
		spin_unlock(&data->lock);
		return -ENODEV;
	}

	reg = (USERACCESS_GO | USERACCESS_READ | (phy_reg << 21) |
	       (phy_id << 16));

	while (1) {
		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		if (ret < 0)
			break;

		__raw_writel(reg, &data->regs->user[0].access);

		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		if (ret < 0)
			break;

		reg = __raw_readl(&data->regs->user[0].access);
		ret = (reg & USERACCESS_ACK) ? (reg & USERACCESS_DATA) : -EIO;
		break;
	}

	spin_unlock(&data->lock);

	return ret;
}

static int davinci_mdio_write(struct mii_bus *bus, int phy_id,
			      int phy_reg, u16 phy_data)
{
	struct davinci_mdio_data *data = bus->priv;
	u32 reg;
	int ret;

	if (phy_reg & ~PHY_REG_MASK || phy_id & ~PHY_ID_MASK)
		return -EINVAL;

	spin_lock(&data->lock);

	if (data->suspended) {
		spin_unlock(&data->lock);
		return -ENODEV;
	}

	reg = (USERACCESS_GO | USERACCESS_WRITE | (phy_reg << 21) |
		   (phy_id << 16) | (phy_data & USERACCESS_DATA));

	while (1) {
		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		if (ret < 0)
			break;

		__raw_writel(reg, &data->regs->user[0].access);

		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		break;
	}

	spin_unlock(&data->lock);

	return 0;
}

static int davinci_mdio_probe_dt(struct mdio_platform_data *data,
			 struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	u32 prop;

	if (!node)
		return -EINVAL;

	if (of_property_read_u32(node, "bus_freq", &prop)) {
		pr_err("Missing bus_freq property in the DT.\n");
		return -EINVAL;
	}
	data->bus_freq = prop;

	return 0;
}


static int davinci_mdio_probe(struct platform_device *pdev)
{
	struct mdio_platform_data *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct davinci_mdio_data *data;
	struct resource *res;
	struct phy_device *phy;
	int ret, addr;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->bus = mdiobus_alloc();
	if (!data->bus) {
		dev_err(dev, "failed to alloc mii bus\n");
		ret = -ENOMEM;
		goto bail_out;
	}

	if (dev->of_node) {
		if (davinci_mdio_probe_dt(&data->pdata, pdev))
			data->pdata = default_pdata;
		snprintf(data->bus->id, MII_BUS_ID_SIZE, "%s", pdev->name);
	} else {
		data->pdata = pdata ? (*pdata) : default_pdata;
		snprintf(data->bus->id, MII_BUS_ID_SIZE, "%s-%x",
			 pdev->name, pdev->id);
	}

	data->bus->name		= dev_name(dev);
	data->bus->read		= davinci_mdio_read,
	data->bus->write	= davinci_mdio_write,
	data->bus->reset	= davinci_mdio_reset,
	data->bus->parent	= dev;
	data->bus->priv		= data;

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
	data->clk = clk_get(&pdev->dev, "fck");
	if (IS_ERR(data->clk)) {
		dev_err(dev, "failed to get device clock\n");
		ret = PTR_ERR(data->clk);
		data->clk = NULL;
		goto bail_out;
	}

	dev_set_drvdata(dev, data);
	data->dev = dev;
	spin_lock_init(&data->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "could not find register map resource\n");
		ret = -ENOENT;
		goto bail_out;
	}

	res = devm_request_mem_region(dev, res->start, resource_size(res),
					    dev_name(dev));
	if (!res) {
		dev_err(dev, "could not allocate register map resource\n");
		ret = -ENXIO;
		goto bail_out;
	}

	data->regs = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!data->regs) {
		dev_err(dev, "could not map mdio registers\n");
		ret = -ENOMEM;
		goto bail_out;
	}

	/* register the mii bus */
	ret = mdiobus_register(data->bus);
	if (ret)
		goto bail_out;

	/* scan and dump the bus */
	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		phy = data->bus->phy_map[addr];
		if (phy) {
			dev_info(dev, "phy[%d]: device %s, driver %s\n",
				 phy->addr, dev_name(&phy->dev),
				 phy->drv ? phy->drv->name : "unknown");
		}
	}

	return 0;

bail_out:
	if (data->bus)
		mdiobus_free(data->bus);

	if (data->clk)
		clk_put(data->clk);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	kfree(data);

	return ret;
}

static int davinci_mdio_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct davinci_mdio_data *data = dev_get_drvdata(dev);

	if (data->bus) {
		mdiobus_unregister(data->bus);
		mdiobus_free(data->bus);
	}

	if (data->clk)
		clk_put(data->clk);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	dev_set_drvdata(dev, NULL);

	kfree(data);

	return 0;
}

static int davinci_mdio_suspend(struct device *dev)
{
	struct davinci_mdio_data *data = dev_get_drvdata(dev);
	u32 ctrl;

	spin_lock(&data->lock);

	/* shutdown the scan state machine */
	ctrl = __raw_readl(&data->regs->control);
	ctrl &= ~CONTROL_ENABLE;
	__raw_writel(ctrl, &data->regs->control);
	wait_for_idle(data);

	data->suspended = true;
	spin_unlock(&data->lock);
	pm_runtime_put_sync(data->dev);

	return 0;
}

static int davinci_mdio_resume(struct device *dev)
{
	struct davinci_mdio_data *data = dev_get_drvdata(dev);
	u32 ctrl;

	pm_runtime_get_sync(data->dev);

	spin_lock(&data->lock);
	/* restart the scan state machine */
	ctrl = __raw_readl(&data->regs->control);
	ctrl |= CONTROL_ENABLE;
	__raw_writel(ctrl, &data->regs->control);

	data->suspended = false;
	spin_unlock(&data->lock);

	return 0;
}

static const struct dev_pm_ops davinci_mdio_pm_ops = {
	.suspend_late	= davinci_mdio_suspend,
	.resume_early	= davinci_mdio_resume,
};

static const struct of_device_id davinci_mdio_of_mtable[] = {
	{ .compatible = "ti,davinci_mdio", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, davinci_mdio_of_mtable);

static struct platform_driver davinci_mdio_driver = {
	.driver = {
		.name	 = "davinci_mdio",
		.owner	 = THIS_MODULE,
		.pm	 = &davinci_mdio_pm_ops,
		.of_match_table = of_match_ptr(davinci_mdio_of_mtable),
	},
	.probe = davinci_mdio_probe,
	.remove = davinci_mdio_remove,
};

static int __init davinci_mdio_init(void)
{
	return platform_driver_register(&davinci_mdio_driver);
}
device_initcall(davinci_mdio_init);

static void __exit davinci_mdio_exit(void)
{
	platform_driver_unregister(&davinci_mdio_driver);
}
module_exit(davinci_mdio_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DaVinci MDIO driver");

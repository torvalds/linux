// SPDX-License-Identifier: GPL-2.0+
/*
 * DaVinci MDIO Module driver
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * Shamelessly ripped out of davinci_emac.c, original copyrights follow:
 *
 * Copyright (C) 2009 Texas Instruments.
 *
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
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/davinci_emac.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/pinctrl/consumer.h>

/*
 * This timeout definition is a worst-case ultra defensive measure against
 * unexpected controller lock ups.  Ideally, we should never ever hit this
 * scenario in practice.
 */
#define MDIO_TIMEOUT		100 /* msecs */

#define PHY_REG_MASK		0x1f
#define PHY_ID_MASK		0x1f

#define DEF_OUT_FREQ		2200000		/* 2.2 MHz */

struct davinci_mdio_of_param {
	int autosuspend_delay_ms;
};

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

static const struct mdio_platform_data default_pdata = {
	.bus_freq = DEF_OUT_FREQ,
};

struct davinci_mdio_data {
	struct mdio_platform_data pdata;
	struct davinci_mdio_regs __iomem *regs;
	struct clk	*clk;
	struct device	*dev;
	struct mii_bus	*bus;
	bool            active_in_suspend;
	unsigned long	access_time; /* jiffies */
	/* Indicates that driver shouldn't modify phy_mask in case
	 * if MDIO bus is registered from DT.
	 */
	bool		skip_scan;
	u32		clk_div;
};

static void davinci_mdio_init_clk(struct davinci_mdio_data *data)
{
	u32 mdio_in, div, mdio_out_khz, access_time;

	mdio_in = clk_get_rate(data->clk);
	div = (mdio_in / data->pdata.bus_freq) - 1;
	if (div > CONTROL_MAX_DIV)
		div = CONTROL_MAX_DIV;

	data->clk_div = div;
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

static void davinci_mdio_enable(struct davinci_mdio_data *data)
{
	/* set enable and clock divider */
	writel(data->clk_div | CONTROL_ENABLE, &data->regs->control);
}

static int davinci_mdio_reset(struct mii_bus *bus)
{
	struct davinci_mdio_data *data = bus->priv;
	u32 phy_mask, ver;
	int ret;

	ret = pm_runtime_get_sync(data->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(data->dev);
		return ret;
	}

	/* wait for scan logic to settle */
	msleep(PHY_MAX_ADDR * data->access_time);

	/* dump hardware version info */
	ver = readl(&data->regs->version);
	dev_info(data->dev,
		 "davinci mdio revision %d.%d, bus freq %ld\n",
		 (ver >> 8) & 0xff, ver & 0xff,
		 data->pdata.bus_freq);

	if (data->skip_scan)
		goto done;

	/* get phy mask from the alive register */
	phy_mask = readl(&data->regs->alive);
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

done:
	pm_runtime_mark_last_busy(data->dev);
	pm_runtime_put_autosuspend(data->dev);

	return 0;
}

/* wait until hardware is ready for another user access */
static inline int wait_for_user_access(struct davinci_mdio_data *data)
{
	struct davinci_mdio_regs __iomem *regs = data->regs;
	unsigned long timeout = jiffies + msecs_to_jiffies(MDIO_TIMEOUT);
	u32 reg;

	while (time_after(timeout, jiffies)) {
		reg = readl(&regs->user[0].access);
		if ((reg & USERACCESS_GO) == 0)
			return 0;

		reg = readl(&regs->control);
		if ((reg & CONTROL_IDLE) == 0) {
			usleep_range(100, 200);
			continue;
		}

		/*
		 * An emac soft_reset may have clobbered the mdio controller's
		 * state machine.  We need to reset and retry the current
		 * operation
		 */
		dev_warn(data->dev, "resetting idled controller\n");
		davinci_mdio_enable(data);
		return -EAGAIN;
	}

	reg = readl(&regs->user[0].access);
	if ((reg & USERACCESS_GO) == 0)
		return 0;

	dev_err(data->dev, "timed out waiting for user access\n");
	return -ETIMEDOUT;
}

/* wait until hardware state machine is idle */
static inline int wait_for_idle(struct davinci_mdio_data *data)
{
	struct davinci_mdio_regs __iomem *regs = data->regs;
	u32 val, ret;

	ret = readl_poll_timeout(&regs->control, val, val & CONTROL_IDLE,
				 0, MDIO_TIMEOUT * 1000);
	if (ret)
		dev_err(data->dev, "timed out waiting for idle\n");

	return ret;
}

static int davinci_mdio_read(struct mii_bus *bus, int phy_id, int phy_reg)
{
	struct davinci_mdio_data *data = bus->priv;
	u32 reg;
	int ret;

	if (phy_reg & ~PHY_REG_MASK || phy_id & ~PHY_ID_MASK)
		return -EINVAL;

	ret = pm_runtime_get_sync(data->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(data->dev);
		return ret;
	}

	reg = (USERACCESS_GO | USERACCESS_READ | (phy_reg << 21) |
	       (phy_id << 16));

	while (1) {
		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		if (ret < 0)
			break;

		writel(reg, &data->regs->user[0].access);

		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		if (ret < 0)
			break;

		reg = readl(&data->regs->user[0].access);
		ret = (reg & USERACCESS_ACK) ? (reg & USERACCESS_DATA) : -EIO;
		break;
	}

	pm_runtime_mark_last_busy(data->dev);
	pm_runtime_put_autosuspend(data->dev);
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

	ret = pm_runtime_get_sync(data->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(data->dev);
		return ret;
	}

	reg = (USERACCESS_GO | USERACCESS_WRITE | (phy_reg << 21) |
		   (phy_id << 16) | (phy_data & USERACCESS_DATA));

	while (1) {
		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		if (ret < 0)
			break;

		writel(reg, &data->regs->user[0].access);

		ret = wait_for_user_access(data);
		if (ret == -EAGAIN)
			continue;
		break;
	}

	pm_runtime_mark_last_busy(data->dev);
	pm_runtime_put_autosuspend(data->dev);

	return ret;
}

static int davinci_mdio_probe_dt(struct mdio_platform_data *data,
			 struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	u32 prop;

	if (!node)
		return -EINVAL;

	if (of_property_read_u32(node, "bus_freq", &prop)) {
		dev_err(&pdev->dev, "Missing bus_freq property in the DT.\n");
		return -EINVAL;
	}
	data->bus_freq = prop;

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct davinci_mdio_of_param of_cpsw_mdio_data = {
	.autosuspend_delay_ms = 100,
};

static const struct of_device_id davinci_mdio_of_mtable[] = {
	{ .compatible = "ti,davinci_mdio", },
	{ .compatible = "ti,cpsw-mdio", .data = &of_cpsw_mdio_data},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, davinci_mdio_of_mtable);
#endif

static int davinci_mdio_probe(struct platform_device *pdev)
{
	struct mdio_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct davinci_mdio_data *data;
	struct resource *res;
	struct phy_device *phy;
	int ret, addr;
	int autosuspend_delay_ms = -1;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->bus = devm_mdiobus_alloc(dev);
	if (!data->bus) {
		dev_err(dev, "failed to alloc mii bus\n");
		return -ENOMEM;
	}

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		const struct of_device_id	*of_id;

		ret = davinci_mdio_probe_dt(&data->pdata, pdev);
		if (ret)
			return ret;
		snprintf(data->bus->id, MII_BUS_ID_SIZE, "%s", pdev->name);

		of_id = of_match_device(davinci_mdio_of_mtable, &pdev->dev);
		if (of_id) {
			const struct davinci_mdio_of_param *of_mdio_data;

			of_mdio_data = of_id->data;
			if (of_mdio_data)
				autosuspend_delay_ms =
					of_mdio_data->autosuspend_delay_ms;
		}
	} else {
		data->pdata = pdata ? (*pdata) : default_pdata;
		snprintf(data->bus->id, MII_BUS_ID_SIZE, "%s-%x",
			 pdev->name, pdev->id);
	}

	data->bus->name		= dev_name(dev);
	data->bus->read		= davinci_mdio_read;
	data->bus->write	= davinci_mdio_write;
	data->bus->reset	= davinci_mdio_reset;
	data->bus->parent	= dev;
	data->bus->priv		= data;

	data->clk = devm_clk_get(dev, "fck");
	if (IS_ERR(data->clk)) {
		dev_err(dev, "failed to get device clock\n");
		return PTR_ERR(data->clk);
	}

	dev_set_drvdata(dev, data);
	data->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	data->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!data->regs)
		return -ENOMEM;

	davinci_mdio_init_clk(data);

	pm_runtime_set_autosuspend_delay(&pdev->dev, autosuspend_delay_ms);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	/* register the mii bus
	 * Create PHYs from DT only in case if PHY child nodes are explicitly
	 * defined to support backward compatibility with DTs which assume that
	 * Davinci MDIO will always scan the bus for PHYs detection.
	 */
	if (dev->of_node && of_get_child_count(dev->of_node))
		data->skip_scan = true;

	ret = of_mdiobus_register(data->bus, dev->of_node);
	if (ret)
		goto bail_out;

	/* scan and dump the bus */
	for (addr = 0; addr < PHY_MAX_ADDR; addr++) {
		phy = mdiobus_get_phy(data->bus, addr);
		if (phy) {
			dev_info(dev, "phy[%d]: device %s, driver %s\n",
				 phy->mdio.addr, phydev_name(phy),
				 phy->drv ? phy->drv->name : "unknown");
		}
	}

	return 0;

bail_out:
	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int davinci_mdio_remove(struct platform_device *pdev)
{
	struct davinci_mdio_data *data = platform_get_drvdata(pdev);

	if (data->bus)
		mdiobus_unregister(data->bus);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int davinci_mdio_runtime_suspend(struct device *dev)
{
	struct davinci_mdio_data *data = dev_get_drvdata(dev);
	u32 ctrl;

	/* shutdown the scan state machine */
	ctrl = readl(&data->regs->control);
	ctrl &= ~CONTROL_ENABLE;
	writel(ctrl, &data->regs->control);
	wait_for_idle(data);

	return 0;
}

static int davinci_mdio_runtime_resume(struct device *dev)
{
	struct davinci_mdio_data *data = dev_get_drvdata(dev);

	davinci_mdio_enable(data);
	return 0;
}
#endif

#ifdef CONFIG_PM_SLEEP
static int davinci_mdio_suspend(struct device *dev)
{
	struct davinci_mdio_data *data = dev_get_drvdata(dev);
	int ret = 0;

	data->active_in_suspend = !pm_runtime_status_suspended(dev);
	if (data->active_in_suspend)
		ret = pm_runtime_force_suspend(dev);
	if (ret < 0)
		return ret;

	/* Select sleep pin state */
	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int davinci_mdio_resume(struct device *dev)
{
	struct davinci_mdio_data *data = dev_get_drvdata(dev);

	/* Select default pin state */
	pinctrl_pm_select_default_state(dev);

	if (data->active_in_suspend)
		pm_runtime_force_resume(dev);

	return 0;
}
#endif

static const struct dev_pm_ops davinci_mdio_pm_ops = {
	SET_RUNTIME_PM_OPS(davinci_mdio_runtime_suspend,
			   davinci_mdio_runtime_resume, NULL)
	SET_LATE_SYSTEM_SLEEP_PM_OPS(davinci_mdio_suspend, davinci_mdio_resume)
};

static struct platform_driver davinci_mdio_driver = {
	.driver = {
		.name	 = "davinci_mdio",
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

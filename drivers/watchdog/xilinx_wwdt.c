// SPDX-License-Identifier: GPL-2.0
/*
 * Window watchdog device driver for Xilinx Versal WWDT
 *
 * Copyright (C) 2022 - 2023, Advanced Micro Devices, Inc.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

/* Max timeout is calculated at 100MHz source clock */
#define XWWDT_DEFAULT_TIMEOUT	42
#define XWWDT_MIN_TIMEOUT	1

/* Register offsets for the WWDT device */
#define XWWDT_MWR_OFFSET	0x00
#define XWWDT_ESR_OFFSET	0x04
#define XWWDT_FCR_OFFSET	0x08
#define XWWDT_FWR_OFFSET	0x0c
#define XWWDT_SWR_OFFSET	0x10

/* Master Write Control Register Masks */
#define XWWDT_MWR_MASK		BIT(0)

/* Enable and Status Register Masks */
#define XWWDT_ESR_WINT_MASK	BIT(16)
#define XWWDT_ESR_WSW_MASK	BIT(8)
#define XWWDT_ESR_WEN_MASK	BIT(0)

#define XWWDT_CLOSE_WINDOW_PERCENT	50

static int wwdt_timeout;
static int closed_window_percent;

module_param(wwdt_timeout, int, 0);
MODULE_PARM_DESC(wwdt_timeout,
		 "Watchdog time in seconds. (default="
		 __MODULE_STRING(XWWDT_DEFAULT_TIMEOUT) ")");
module_param(closed_window_percent, int, 0);
MODULE_PARM_DESC(closed_window_percent,
		 "Watchdog closed window percentage. (default="
		 __MODULE_STRING(XWWDT_CLOSE_WINDOW_PERCENT) ")");
/**
 * struct xwwdt_device - Watchdog device structure
 * @base: base io address of WDT device
 * @spinlock: spinlock for IO register access
 * @xilinx_wwdt_wdd: watchdog device structure
 * @freq: source clock frequency of WWDT
 * @close_percent: Closed window percent
 */
struct xwwdt_device {
	void __iomem *base;
	spinlock_t spinlock; /* spinlock for register handling */
	struct watchdog_device xilinx_wwdt_wdd;
	unsigned long freq;
	u32 close_percent;
};

static int xilinx_wwdt_start(struct watchdog_device *wdd)
{
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wwdt_wdd = &xdev->xilinx_wwdt_wdd;
	u64 time_out, closed_timeout, open_timeout;
	u32 control_status_reg;

	/* Calculate timeout count */
	time_out = xdev->freq * wdd->timeout;
	closed_timeout = div_u64(time_out * xdev->close_percent, 100);
	open_timeout = time_out - closed_timeout;
	wdd->min_hw_heartbeat_ms = xdev->close_percent * 10 * wdd->timeout;

	spin_lock(&xdev->spinlock);

	iowrite32(XWWDT_MWR_MASK, xdev->base + XWWDT_MWR_OFFSET);
	iowrite32(~(u32)XWWDT_ESR_WEN_MASK, xdev->base + XWWDT_ESR_OFFSET);
	iowrite32((u32)closed_timeout, xdev->base + XWWDT_FWR_OFFSET);
	iowrite32((u32)open_timeout, xdev->base + XWWDT_SWR_OFFSET);

	/* Enable the window watchdog timer */
	control_status_reg = ioread32(xdev->base + XWWDT_ESR_OFFSET);
	control_status_reg |= XWWDT_ESR_WEN_MASK;
	iowrite32(control_status_reg, xdev->base + XWWDT_ESR_OFFSET);

	spin_unlock(&xdev->spinlock);

	dev_dbg(xilinx_wwdt_wdd->parent, "Watchdog Started!\n");

	return 0;
}

static int xilinx_wwdt_keepalive(struct watchdog_device *wdd)
{
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);
	u32 control_status_reg;

	spin_lock(&xdev->spinlock);

	/* Enable write access control bit for the window watchdog */
	iowrite32(XWWDT_MWR_MASK, xdev->base + XWWDT_MWR_OFFSET);

	/* Trigger restart kick to watchdog */
	control_status_reg = ioread32(xdev->base + XWWDT_ESR_OFFSET);
	control_status_reg |= XWWDT_ESR_WSW_MASK;
	iowrite32(control_status_reg, xdev->base + XWWDT_ESR_OFFSET);

	spin_unlock(&xdev->spinlock);

	return 0;
}

static const struct watchdog_info xilinx_wwdt_ident = {
	.options = WDIOF_KEEPALIVEPING |
		WDIOF_SETTIMEOUT,
	.firmware_version = 1,
	.identity = "xlnx_window watchdog",
};

static const struct watchdog_ops xilinx_wwdt_ops = {
	.owner = THIS_MODULE,
	.start = xilinx_wwdt_start,
	.ping = xilinx_wwdt_keepalive,
};

static int xwwdt_probe(struct platform_device *pdev)
{
	struct watchdog_device *xilinx_wwdt_wdd;
	struct device *dev = &pdev->dev;
	struct xwwdt_device *xdev;
	struct clk *clk;
	int ret;

	xdev = devm_kzalloc(dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xilinx_wwdt_wdd = &xdev->xilinx_wwdt_wdd;
	xilinx_wwdt_wdd->info = &xilinx_wwdt_ident;
	xilinx_wwdt_wdd->ops = &xilinx_wwdt_ops;
	xilinx_wwdt_wdd->parent = dev;

	xdev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xdev->base))
		return PTR_ERR(xdev->base);

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	xdev->freq = clk_get_rate(clk);
	if (!xdev->freq)
		return -EINVAL;

	xilinx_wwdt_wdd->min_timeout = XWWDT_MIN_TIMEOUT;
	xilinx_wwdt_wdd->timeout = XWWDT_DEFAULT_TIMEOUT;
	xilinx_wwdt_wdd->max_hw_heartbeat_ms = 1000 * xilinx_wwdt_wdd->timeout;

	if (closed_window_percent == 0 || closed_window_percent >= 100)
		xdev->close_percent = XWWDT_CLOSE_WINDOW_PERCENT;
	else
		xdev->close_percent = closed_window_percent;

	watchdog_init_timeout(xilinx_wwdt_wdd, wwdt_timeout, &pdev->dev);
	spin_lock_init(&xdev->spinlock);
	watchdog_set_drvdata(xilinx_wwdt_wdd, xdev);
	watchdog_set_nowayout(xilinx_wwdt_wdd, 1);

	ret = devm_watchdog_register_device(dev, xilinx_wwdt_wdd);
	if (ret)
		return ret;

	dev_info(dev, "Xilinx window watchdog Timer with timeout %ds\n",
		 xilinx_wwdt_wdd->timeout);

	return 0;
}

static const struct of_device_id xwwdt_of_match[] = {
	{ .compatible = "xlnx,versal-wwdt", },
	{},
};
MODULE_DEVICE_TABLE(of, xwwdt_of_match);

static struct platform_driver xwwdt_driver = {
	.probe = xwwdt_probe,
	.driver = {
		.name = "Xilinx window watchdog",
		.of_match_table = xwwdt_of_match,
	},
};

module_platform_driver(xwwdt_driver);

MODULE_AUTHOR("Neeli Srinivas <srinivas.neeli@amd.com>");
MODULE_DESCRIPTION("Xilinx window watchdog driver");
MODULE_LICENSE("GPL");

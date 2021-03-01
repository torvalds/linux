// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Sunrisepoint LPSS core support.
 *
 * Copyright (C) 2015, Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *          Jarkko Nikula <jarkko.nikula@linux.intel.com>
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/debugfs.h>
#include <linux/idr.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include <linux/dma/idma64.h>

#include "intel-lpss.h"

#define LPSS_DEV_OFFSET		0x000
#define LPSS_DEV_SIZE		0x200
#define LPSS_PRIV_OFFSET	0x200
#define LPSS_PRIV_SIZE		0x100
#define LPSS_PRIV_REG_COUNT	(LPSS_PRIV_SIZE / 4)
#define LPSS_IDMA64_OFFSET	0x800
#define LPSS_IDMA64_SIZE	0x800

/* Offsets from lpss->priv */
#define LPSS_PRIV_RESETS		0x04
#define LPSS_PRIV_RESETS_IDMA		BIT(2)
#define LPSS_PRIV_RESETS_FUNC		0x3

#define LPSS_PRIV_ACTIVELTR		0x10
#define LPSS_PRIV_IDLELTR		0x14

#define LPSS_PRIV_LTR_REQ		BIT(15)
#define LPSS_PRIV_LTR_SCALE_MASK	GENMASK(11, 10)
#define LPSS_PRIV_LTR_SCALE_1US		(2 << 10)
#define LPSS_PRIV_LTR_SCALE_32US	(3 << 10)
#define LPSS_PRIV_LTR_VALUE_MASK	GENMASK(9, 0)

#define LPSS_PRIV_SSP_REG		0x20
#define LPSS_PRIV_SSP_REG_DIS_DMA_FIN	BIT(0)

#define LPSS_PRIV_REMAP_ADDR		0x40

#define LPSS_PRIV_CAPS			0xfc
#define LPSS_PRIV_CAPS_NO_IDMA		BIT(8)
#define LPSS_PRIV_CAPS_TYPE_MASK	GENMASK(7, 4)
#define LPSS_PRIV_CAPS_TYPE_SHIFT	4

/* This matches the type field in CAPS register */
enum intel_lpss_dev_type {
	LPSS_DEV_I2C = 0,
	LPSS_DEV_UART,
	LPSS_DEV_SPI,
};

struct intel_lpss {
	const struct intel_lpss_platform_info *info;
	enum intel_lpss_dev_type type;
	struct clk *clk;
	struct clk_lookup *clock;
	struct mfd_cell *cell;
	struct device *dev;
	void __iomem *priv;
	u32 priv_ctx[LPSS_PRIV_REG_COUNT];
	int devid;
	u32 caps;
	u32 active_ltr;
	u32 idle_ltr;
	struct dentry *debugfs;
};

static const struct resource intel_lpss_dev_resources[] = {
	DEFINE_RES_MEM_NAMED(LPSS_DEV_OFFSET, LPSS_DEV_SIZE, "lpss_dev"),
	DEFINE_RES_MEM_NAMED(LPSS_PRIV_OFFSET, LPSS_PRIV_SIZE, "lpss_priv"),
	DEFINE_RES_IRQ(0),
};

static const struct resource intel_lpss_idma64_resources[] = {
	DEFINE_RES_MEM(LPSS_IDMA64_OFFSET, LPSS_IDMA64_SIZE),
	DEFINE_RES_IRQ(0),
};

/*
 * Cells needs to be ordered so that the iDMA is created first. This is
 * because we need to be sure the DMA is available when the host controller
 * driver is probed.
 */
static const struct mfd_cell intel_lpss_idma64_cell = {
	.name = LPSS_IDMA64_DRIVER_NAME,
	.num_resources = ARRAY_SIZE(intel_lpss_idma64_resources),
	.resources = intel_lpss_idma64_resources,
};

static const struct mfd_cell intel_lpss_i2c_cell = {
	.name = "i2c_designware",
	.num_resources = ARRAY_SIZE(intel_lpss_dev_resources),
	.resources = intel_lpss_dev_resources,
};

static const struct mfd_cell intel_lpss_uart_cell = {
	.name = "dw-apb-uart",
	.num_resources = ARRAY_SIZE(intel_lpss_dev_resources),
	.resources = intel_lpss_dev_resources,
};

static const struct mfd_cell intel_lpss_spi_cell = {
	.name = "pxa2xx-spi",
	.num_resources = ARRAY_SIZE(intel_lpss_dev_resources),
	.resources = intel_lpss_dev_resources,
};

static DEFINE_IDA(intel_lpss_devid_ida);
static struct dentry *intel_lpss_debugfs;

static void intel_lpss_cache_ltr(struct intel_lpss *lpss)
{
	lpss->active_ltr = readl(lpss->priv + LPSS_PRIV_ACTIVELTR);
	lpss->idle_ltr = readl(lpss->priv + LPSS_PRIV_IDLELTR);
}

static int intel_lpss_debugfs_add(struct intel_lpss *lpss)
{
	struct dentry *dir;

	dir = debugfs_create_dir(dev_name(lpss->dev), intel_lpss_debugfs);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	/* Cache the values into lpss structure */
	intel_lpss_cache_ltr(lpss);

	debugfs_create_x32("capabilities", S_IRUGO, dir, &lpss->caps);
	debugfs_create_x32("active_ltr", S_IRUGO, dir, &lpss->active_ltr);
	debugfs_create_x32("idle_ltr", S_IRUGO, dir, &lpss->idle_ltr);

	lpss->debugfs = dir;
	return 0;
}

static void intel_lpss_debugfs_remove(struct intel_lpss *lpss)
{
	debugfs_remove_recursive(lpss->debugfs);
}

static void intel_lpss_ltr_set(struct device *dev, s32 val)
{
	struct intel_lpss *lpss = dev_get_drvdata(dev);
	u32 ltr;

	/*
	 * Program latency tolerance (LTR) accordingly what has been asked
	 * by the PM QoS layer or disable it in case we were passed
	 * negative value or PM_QOS_LATENCY_ANY.
	 */
	ltr = readl(lpss->priv + LPSS_PRIV_ACTIVELTR);

	if (val == PM_QOS_LATENCY_ANY || val < 0) {
		ltr &= ~LPSS_PRIV_LTR_REQ;
	} else {
		ltr |= LPSS_PRIV_LTR_REQ;
		ltr &= ~LPSS_PRIV_LTR_SCALE_MASK;
		ltr &= ~LPSS_PRIV_LTR_VALUE_MASK;

		if (val > LPSS_PRIV_LTR_VALUE_MASK)
			ltr |= LPSS_PRIV_LTR_SCALE_32US | val >> 5;
		else
			ltr |= LPSS_PRIV_LTR_SCALE_1US | val;
	}

	if (ltr == lpss->active_ltr)
		return;

	writel(ltr, lpss->priv + LPSS_PRIV_ACTIVELTR);
	writel(ltr, lpss->priv + LPSS_PRIV_IDLELTR);

	/* Cache the values into lpss structure */
	intel_lpss_cache_ltr(lpss);
}

static void intel_lpss_ltr_expose(struct intel_lpss *lpss)
{
	lpss->dev->power.set_latency_tolerance = intel_lpss_ltr_set;
	dev_pm_qos_expose_latency_tolerance(lpss->dev);
}

static void intel_lpss_ltr_hide(struct intel_lpss *lpss)
{
	dev_pm_qos_hide_latency_tolerance(lpss->dev);
	lpss->dev->power.set_latency_tolerance = NULL;
}

static int intel_lpss_assign_devs(struct intel_lpss *lpss)
{
	const struct mfd_cell *cell;
	unsigned int type;

	type = lpss->caps & LPSS_PRIV_CAPS_TYPE_MASK;
	type >>= LPSS_PRIV_CAPS_TYPE_SHIFT;

	switch (type) {
	case LPSS_DEV_I2C:
		cell = &intel_lpss_i2c_cell;
		break;
	case LPSS_DEV_UART:
		cell = &intel_lpss_uart_cell;
		break;
	case LPSS_DEV_SPI:
		cell = &intel_lpss_spi_cell;
		break;
	default:
		return -ENODEV;
	}

	lpss->cell = devm_kmemdup(lpss->dev, cell, sizeof(*cell), GFP_KERNEL);
	if (!lpss->cell)
		return -ENOMEM;

	lpss->type = type;

	return 0;
}

static bool intel_lpss_has_idma(const struct intel_lpss *lpss)
{
	return (lpss->caps & LPSS_PRIV_CAPS_NO_IDMA) == 0;
}

static void intel_lpss_set_remap_addr(const struct intel_lpss *lpss)
{
	resource_size_t addr = lpss->info->mem->start;

	lo_hi_writeq(addr, lpss->priv + LPSS_PRIV_REMAP_ADDR);
}

static void intel_lpss_deassert_reset(const struct intel_lpss *lpss)
{
	u32 value = LPSS_PRIV_RESETS_FUNC | LPSS_PRIV_RESETS_IDMA;

	/* Bring out the device from reset */
	writel(value, lpss->priv + LPSS_PRIV_RESETS);
}

static void intel_lpss_init_dev(const struct intel_lpss *lpss)
{
	u32 value = LPSS_PRIV_SSP_REG_DIS_DMA_FIN;

	/* Set the device in reset state */
	writel(0, lpss->priv + LPSS_PRIV_RESETS);

	intel_lpss_deassert_reset(lpss);

	intel_lpss_set_remap_addr(lpss);

	if (!intel_lpss_has_idma(lpss))
		return;

	/* Make sure that SPI multiblock DMA transfers are re-enabled */
	if (lpss->type == LPSS_DEV_SPI)
		writel(value, lpss->priv + LPSS_PRIV_SSP_REG);
}

static void intel_lpss_unregister_clock_tree(struct clk *clk)
{
	struct clk *parent;

	while (clk) {
		parent = clk_get_parent(clk);
		clk_unregister(clk);
		clk = parent;
	}
}

static int intel_lpss_register_clock_divider(struct intel_lpss *lpss,
					     const char *devname,
					     struct clk **clk)
{
	char name[32];
	struct clk *tmp = *clk;

	snprintf(name, sizeof(name), "%s-enable", devname);
	tmp = clk_register_gate(NULL, name, __clk_get_name(tmp), 0,
				lpss->priv, 0, 0, NULL);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);

	snprintf(name, sizeof(name), "%s-div", devname);
	tmp = clk_register_fractional_divider(NULL, name, __clk_get_name(tmp),
					      0, lpss->priv, 1, 15, 16, 15, 0,
					      NULL);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	*clk = tmp;

	snprintf(name, sizeof(name), "%s-update", devname);
	tmp = clk_register_gate(NULL, name, __clk_get_name(tmp),
				CLK_SET_RATE_PARENT, lpss->priv, 31, 0, NULL);
	if (IS_ERR(tmp))
		return PTR_ERR(tmp);
	*clk = tmp;

	return 0;
}

static int intel_lpss_register_clock(struct intel_lpss *lpss)
{
	const struct mfd_cell *cell = lpss->cell;
	struct clk *clk;
	char devname[24];
	int ret;

	if (!lpss->info->clk_rate)
		return 0;

	/* Root clock */
	clk = clk_register_fixed_rate(NULL, dev_name(lpss->dev), NULL, 0,
				      lpss->info->clk_rate);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	snprintf(devname, sizeof(devname), "%s.%d", cell->name, lpss->devid);

	/*
	 * Support for clock divider only if it has some preset value.
	 * Otherwise we assume that the divider is not used.
	 */
	if (lpss->type != LPSS_DEV_I2C) {
		ret = intel_lpss_register_clock_divider(lpss, devname, &clk);
		if (ret)
			goto err_clk_register;
	}

	ret = -ENOMEM;

	/* Clock for the host controller */
	lpss->clock = clkdev_create(clk, lpss->info->clk_con_id, "%s", devname);
	if (!lpss->clock)
		goto err_clk_register;

	lpss->clk = clk;

	return 0;

err_clk_register:
	intel_lpss_unregister_clock_tree(clk);

	return ret;
}

static void intel_lpss_unregister_clock(struct intel_lpss *lpss)
{
	if (IS_ERR_OR_NULL(lpss->clk))
		return;

	clkdev_drop(lpss->clock);
	intel_lpss_unregister_clock_tree(lpss->clk);
}

int intel_lpss_probe(struct device *dev,
		     const struct intel_lpss_platform_info *info)
{
	struct intel_lpss *lpss;
	int ret;

	if (!info || !info->mem || info->irq <= 0)
		return -EINVAL;

	lpss = devm_kzalloc(dev, sizeof(*lpss), GFP_KERNEL);
	if (!lpss)
		return -ENOMEM;

	lpss->priv = devm_ioremap_uc(dev, info->mem->start + LPSS_PRIV_OFFSET,
				  LPSS_PRIV_SIZE);
	if (!lpss->priv)
		return -ENOMEM;

	lpss->info = info;
	lpss->dev = dev;
	lpss->caps = readl(lpss->priv + LPSS_PRIV_CAPS);

	dev_set_drvdata(dev, lpss);

	ret = intel_lpss_assign_devs(lpss);
	if (ret)
		return ret;

	lpss->cell->swnode = info->swnode;

	intel_lpss_init_dev(lpss);

	lpss->devid = ida_simple_get(&intel_lpss_devid_ida, 0, 0, GFP_KERNEL);
	if (lpss->devid < 0)
		return lpss->devid;

	ret = intel_lpss_register_clock(lpss);
	if (ret)
		goto err_clk_register;

	intel_lpss_ltr_expose(lpss);

	ret = intel_lpss_debugfs_add(lpss);
	if (ret)
		dev_warn(dev, "Failed to create debugfs entries\n");

	if (intel_lpss_has_idma(lpss)) {
		ret = mfd_add_devices(dev, lpss->devid, &intel_lpss_idma64_cell,
				      1, info->mem, info->irq, NULL);
		if (ret)
			dev_warn(dev, "Failed to add %s, fallback to PIO\n",
				 LPSS_IDMA64_DRIVER_NAME);
	}

	ret = mfd_add_devices(dev, lpss->devid, lpss->cell,
			      1, info->mem, info->irq, NULL);
	if (ret)
		goto err_remove_ltr;

	dev_pm_set_driver_flags(dev, DPM_FLAG_SMART_SUSPEND);

	return 0;

err_remove_ltr:
	intel_lpss_debugfs_remove(lpss);
	intel_lpss_ltr_hide(lpss);
	intel_lpss_unregister_clock(lpss);

err_clk_register:
	ida_simple_remove(&intel_lpss_devid_ida, lpss->devid);

	return ret;
}
EXPORT_SYMBOL_GPL(intel_lpss_probe);

void intel_lpss_remove(struct device *dev)
{
	struct intel_lpss *lpss = dev_get_drvdata(dev);

	mfd_remove_devices(dev);
	intel_lpss_debugfs_remove(lpss);
	intel_lpss_ltr_hide(lpss);
	intel_lpss_unregister_clock(lpss);
	ida_simple_remove(&intel_lpss_devid_ida, lpss->devid);
}
EXPORT_SYMBOL_GPL(intel_lpss_remove);

static int resume_lpss_device(struct device *dev, void *data)
{
	if (!dev_pm_test_driver_flags(dev, DPM_FLAG_SMART_SUSPEND))
		pm_runtime_resume(dev);

	return 0;
}

int intel_lpss_prepare(struct device *dev)
{
	/*
	 * Resume both child devices before entering system sleep. This
	 * ensures that they are in proper state before they get suspended.
	 */
	device_for_each_child_reverse(dev, NULL, resume_lpss_device);
	return 0;
}
EXPORT_SYMBOL_GPL(intel_lpss_prepare);

int intel_lpss_suspend(struct device *dev)
{
	struct intel_lpss *lpss = dev_get_drvdata(dev);
	unsigned int i;

	/* Save device context */
	for (i = 0; i < LPSS_PRIV_REG_COUNT; i++)
		lpss->priv_ctx[i] = readl(lpss->priv + i * 4);

	/*
	 * If the device type is not UART, then put the controller into
	 * reset. UART cannot be put into reset since S3/S0ix fail when
	 * no_console_suspend flag is enabled.
	 */
	if (lpss->type != LPSS_DEV_UART)
		writel(0, lpss->priv + LPSS_PRIV_RESETS);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_lpss_suspend);

int intel_lpss_resume(struct device *dev)
{
	struct intel_lpss *lpss = dev_get_drvdata(dev);
	unsigned int i;

	intel_lpss_deassert_reset(lpss);

	/* Restore device context */
	for (i = 0; i < LPSS_PRIV_REG_COUNT; i++)
		writel(lpss->priv_ctx[i], lpss->priv + i * 4);

	return 0;
}
EXPORT_SYMBOL_GPL(intel_lpss_resume);

static int __init intel_lpss_init(void)
{
	intel_lpss_debugfs = debugfs_create_dir("intel_lpss", NULL);
	return 0;
}
module_init(intel_lpss_init);

static void __exit intel_lpss_exit(void)
{
	ida_destroy(&intel_lpss_devid_ida);
	debugfs_remove(intel_lpss_debugfs);
}
module_exit(intel_lpss_exit);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_AUTHOR("Jarkko Nikula <jarkko.nikula@linux.intel.com>");
MODULE_DESCRIPTION("Intel LPSS core driver");
MODULE_LICENSE("GPL v2");
/*
 * Ensure the DMA driver is loaded before the host controller device appears,
 * so that the host controller driver can request its DMA channels as early
 * as possible.
 *
 * If the DMA module is not there that's OK as well.
 */
MODULE_SOFTDEP("pre: platform:" LPSS_IDMA64_DRIVER_NAME);

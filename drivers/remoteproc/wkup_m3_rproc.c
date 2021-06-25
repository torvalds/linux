// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI AMx3 Wakeup M3 Remote Processor driver
 *
 * Copyright (C) 2014-2015 Texas Instruments, Inc.
 *
 * Dave Gerlach <d-gerlach@ti.com>
 * Suman Anna <s-anna@ti.com>
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>

#include <linux/platform_data/wkup_m3.h>

#include "remoteproc_internal.h"

#define WKUPM3_MEM_MAX	2

/**
 * struct wkup_m3_mem - WkupM3 internal memory structure
 * @cpu_addr: MPU virtual address of the memory region
 * @bus_addr: Bus address used to access the memory region
 * @dev_addr: Device address from Wakeup M3 view
 * @size: Size of the memory region
 */
struct wkup_m3_mem {
	void __iomem *cpu_addr;
	phys_addr_t bus_addr;
	u32 dev_addr;
	size_t size;
};

/**
 * struct wkup_m3_rproc - WkupM3 remote processor state
 * @rproc: rproc handle
 * @pdev: pointer to platform device
 * @mem: WkupM3 memory information
 */
struct wkup_m3_rproc {
	struct rproc *rproc;
	struct platform_device *pdev;
	struct wkup_m3_mem mem[WKUPM3_MEM_MAX];
};

static int wkup_m3_rproc_start(struct rproc *rproc)
{
	struct wkup_m3_rproc *wkupm3 = rproc->priv;
	struct platform_device *pdev = wkupm3->pdev;
	struct device *dev = &pdev->dev;
	struct wkup_m3_platform_data *pdata = dev_get_platdata(dev);

	if (pdata->deassert_reset(pdev, pdata->reset_name)) {
		dev_err(dev, "Unable to reset wkup_m3!\n");
		return -ENODEV;
	}

	return 0;
}

static int wkup_m3_rproc_stop(struct rproc *rproc)
{
	struct wkup_m3_rproc *wkupm3 = rproc->priv;
	struct platform_device *pdev = wkupm3->pdev;
	struct device *dev = &pdev->dev;
	struct wkup_m3_platform_data *pdata = dev_get_platdata(dev);

	if (pdata->assert_reset(pdev, pdata->reset_name)) {
		dev_err(dev, "Unable to assert reset of wkup_m3!\n");
		return -ENODEV;
	}

	return 0;
}

static void *wkup_m3_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct wkup_m3_rproc *wkupm3 = rproc->priv;
	void *va = NULL;
	int i;
	u32 offset;

	if (len == 0)
		return NULL;

	for (i = 0; i < WKUPM3_MEM_MAX; i++) {
		if (da >= wkupm3->mem[i].dev_addr && da + len <=
		    wkupm3->mem[i].dev_addr +  wkupm3->mem[i].size) {
			offset = da -  wkupm3->mem[i].dev_addr;
			/* __force to make sparse happy with type conversion */
			va = (__force void *)(wkupm3->mem[i].cpu_addr + offset);
			break;
		}
	}

	return va;
}

static const struct rproc_ops wkup_m3_rproc_ops = {
	.start		= wkup_m3_rproc_start,
	.stop		= wkup_m3_rproc_stop,
	.da_to_va	= wkup_m3_rproc_da_to_va,
};

static const struct of_device_id wkup_m3_rproc_of_match[] = {
	{ .compatible = "ti,am3352-wkup-m3", },
	{ .compatible = "ti,am4372-wkup-m3", },
	{},
};
MODULE_DEVICE_TABLE(of, wkup_m3_rproc_of_match);

static int wkup_m3_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wkup_m3_platform_data *pdata = dev->platform_data;
	/* umem always needs to be processed first */
	const char *mem_names[WKUPM3_MEM_MAX] = { "umem", "dmem" };
	struct wkup_m3_rproc *wkupm3;
	const char *fw_name;
	struct rproc *rproc;
	struct resource *res;
	const __be32 *addrp;
	u32 l4_offset = 0;
	u64 size;
	int ret;
	int i;

	if (!(pdata && pdata->deassert_reset && pdata->assert_reset &&
	      pdata->reset_name)) {
		dev_err(dev, "Platform data missing!\n");
		return -ENODEV;
	}

	ret = of_property_read_string(dev->of_node, "ti,pm-firmware",
				      &fw_name);
	if (ret) {
		dev_err(dev, "No firmware filename given\n");
		return -ENODEV;
	}

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "pm_runtime_get_sync() failed\n");
		goto err;
	}

	rproc = rproc_alloc(dev, "wkup_m3", &wkup_m3_rproc_ops,
			    fw_name, sizeof(*wkupm3));
	if (!rproc) {
		ret = -ENOMEM;
		goto err;
	}

	rproc->auto_boot = false;

	wkupm3 = rproc->priv;
	wkupm3->rproc = rproc;
	wkupm3->pdev = pdev;

	for (i = 0; i < ARRAY_SIZE(mem_names); i++) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   mem_names[i]);
		wkupm3->mem[i].cpu_addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(wkupm3->mem[i].cpu_addr)) {
			dev_err(&pdev->dev, "devm_ioremap_resource failed for resource %d\n",
				i);
			ret = PTR_ERR(wkupm3->mem[i].cpu_addr);
			goto err;
		}
		wkupm3->mem[i].bus_addr = res->start;
		wkupm3->mem[i].size = resource_size(res);
		addrp = of_get_address(dev->of_node, i, &size, NULL);
		/*
		 * The wkupm3 has umem at address 0 in its view, so the device
		 * addresses for each memory region is computed as a relative
		 * offset of the bus address for umem, and therefore needs to be
		 * processed first.
		 */
		if (!strcmp(mem_names[i], "umem"))
			l4_offset = be32_to_cpu(*addrp);
		wkupm3->mem[i].dev_addr = be32_to_cpu(*addrp) - l4_offset;
	}

	dev_set_drvdata(dev, rproc);

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
		goto err_put_rproc;
	}

	return 0;

err_put_rproc:
	rproc_free(rproc);
err:
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	return ret;
}

static int wkup_m3_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_free(rproc);
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM
static int wkup_m3_rpm_suspend(struct device *dev)
{
	return -EBUSY;
}

static int wkup_m3_rpm_resume(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops wkup_m3_rproc_pm_ops = {
	SET_RUNTIME_PM_OPS(wkup_m3_rpm_suspend, wkup_m3_rpm_resume, NULL)
};

static struct platform_driver wkup_m3_rproc_driver = {
	.probe = wkup_m3_rproc_probe,
	.remove = wkup_m3_rproc_remove,
	.driver = {
		.name = "wkup_m3_rproc",
		.of_match_table = wkup_m3_rproc_of_match,
		.pm = &wkup_m3_rproc_pm_ops,
	},
};

module_platform_driver(wkup_m3_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI Wakeup M3 remote processor control driver");
MODULE_AUTHOR("Dave Gerlach <d-gerlach@ti.com>");

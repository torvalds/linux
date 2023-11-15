// SPDX-License-Identifier: GPL-2.0
/*
 * Cadence PCIe platform  driver.
 *
 * Copyright (c) 2019, Cadence Design Systems
 * Author: Tom Joseph <tjoseph@cadence.com>
 */
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_pci.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include "pcie-cadence.h"

#define CDNS_PLAT_CPU_TO_BUS_ADDR	0x0FFFFFFF

/**
 * struct cdns_plat_pcie - private data for this PCIe platform driver
 * @pcie: Cadence PCIe controller
 */
struct cdns_plat_pcie {
	struct cdns_pcie        *pcie;
};

struct cdns_plat_pcie_of_data {
	bool is_rc;
};

static const struct of_device_id cdns_plat_pcie_of_match[];

static u64 cdns_plat_cpu_addr_fixup(struct cdns_pcie *pcie, u64 cpu_addr)
{
	return cpu_addr & CDNS_PLAT_CPU_TO_BUS_ADDR;
}

static const struct cdns_pcie_ops cdns_plat_ops = {
	.cpu_addr_fixup = cdns_plat_cpu_addr_fixup,
};

static int cdns_plat_pcie_probe(struct platform_device *pdev)
{
	const struct cdns_plat_pcie_of_data *data;
	struct cdns_plat_pcie *cdns_plat_pcie;
	struct device *dev = &pdev->dev;
	struct pci_host_bridge *bridge;
	struct cdns_pcie_ep *ep;
	struct cdns_pcie_rc *rc;
	int phy_count;
	bool is_rc;
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	is_rc = data->is_rc;

	pr_debug(" Started %s with is_rc: %d\n", __func__, is_rc);
	cdns_plat_pcie = devm_kzalloc(dev, sizeof(*cdns_plat_pcie), GFP_KERNEL);
	if (!cdns_plat_pcie)
		return -ENOMEM;

	platform_set_drvdata(pdev, cdns_plat_pcie);
	if (is_rc) {
		if (!IS_ENABLED(CONFIG_PCIE_CADENCE_PLAT_HOST))
			return -ENODEV;

		bridge = devm_pci_alloc_host_bridge(dev, sizeof(*rc));
		if (!bridge)
			return -ENOMEM;

		rc = pci_host_bridge_priv(bridge);
		rc->pcie.dev = dev;
		rc->pcie.ops = &cdns_plat_ops;
		cdns_plat_pcie->pcie = &rc->pcie;

		ret = cdns_pcie_init_phy(dev, cdns_plat_pcie->pcie);
		if (ret) {
			dev_err(dev, "failed to init phy\n");
			return ret;
		}
		pm_runtime_enable(dev);
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			dev_err(dev, "pm_runtime_get_sync() failed\n");
			goto err_get_sync;
		}

		ret = cdns_pcie_host_setup(rc);
		if (ret)
			goto err_init;
	} else {
		if (!IS_ENABLED(CONFIG_PCIE_CADENCE_PLAT_EP))
			return -ENODEV;

		ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
		if (!ep)
			return -ENOMEM;

		ep->pcie.dev = dev;
		ep->pcie.ops = &cdns_plat_ops;
		cdns_plat_pcie->pcie = &ep->pcie;

		ret = cdns_pcie_init_phy(dev, cdns_plat_pcie->pcie);
		if (ret) {
			dev_err(dev, "failed to init phy\n");
			return ret;
		}

		pm_runtime_enable(dev);
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			dev_err(dev, "pm_runtime_get_sync() failed\n");
			goto err_get_sync;
		}

		ret = cdns_pcie_ep_setup(ep);
		if (ret)
			goto err_init;
	}

	return 0;

 err_init:
 err_get_sync:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);
	cdns_pcie_disable_phy(cdns_plat_pcie->pcie);
	phy_count = cdns_plat_pcie->pcie->phy_count;
	while (phy_count--)
		device_link_del(cdns_plat_pcie->pcie->link[phy_count]);

	return 0;
}

static void cdns_plat_pcie_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cdns_pcie *pcie = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_put_sync(dev);
	if (ret < 0)
		dev_dbg(dev, "pm_runtime_put_sync failed\n");

	pm_runtime_disable(dev);

	cdns_pcie_disable_phy(pcie);
}

static const struct cdns_plat_pcie_of_data cdns_plat_pcie_host_of_data = {
	.is_rc = true,
};

static const struct cdns_plat_pcie_of_data cdns_plat_pcie_ep_of_data = {
	.is_rc = false,
};

static const struct of_device_id cdns_plat_pcie_of_match[] = {
	{
		.compatible = "cdns,cdns-pcie-host",
		.data = &cdns_plat_pcie_host_of_data,
	},
	{
		.compatible = "cdns,cdns-pcie-ep",
		.data = &cdns_plat_pcie_ep_of_data,
	},
	{},
};

static struct platform_driver cdns_plat_pcie_driver = {
	.driver = {
		.name = "cdns-pcie",
		.of_match_table = cdns_plat_pcie_of_match,
		.pm	= &cdns_pcie_pm_ops,
	},
	.probe = cdns_plat_pcie_probe,
	.shutdown = cdns_plat_pcie_shutdown,
};
builtin_platform_driver(cdns_plat_pcie_driver);

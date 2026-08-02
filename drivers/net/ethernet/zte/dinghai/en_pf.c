// SPDX-License-Identifier: GPL-2.0-only
/*
 * ZTE DingHai Ethernet driver
 * Copyright (c) 2022-2026, ZTE Corporation.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <net/devlink.h>
#include <linux/dma-mapping.h>
#include "en_pf.h"

MODULE_AUTHOR("Junyang Han <han.junyang@zte.com.cn>");
MODULE_DESCRIPTION("ZTE DingHai series Ethernet driver");
MODULE_LICENSE("GPL");

static const struct devlink_ops zxdh_pf_devlink_ops = {};

static const struct pci_device_id zxdh_pf_pci_table[] = {
	{ PCI_DEVICE(ZXDH_PF_VENDOR_ID, ZXDH_PF_DEVICE_ID) },
	{ PCI_DEVICE(ZXDH_PF_VENDOR_ID, ZXDH_VF_DEVICE_ID) },
	{ }
};

MODULE_DEVICE_TABLE(pci, zxdh_pf_pci_table);

void *zxdh_core_alloc_priv(struct zxdh_core_dev *zxdh_dev, size_t size)
{
	void *priv = kzalloc(size, GFP_KERNEL);

	if (priv)
		zxdh_dev->priv = priv;
	return priv;
}

void zxdh_core_free_priv(struct zxdh_core_dev *zxdh_dev)
{
	kfree(zxdh_dev->priv);
}

static int zxdh_pf_pci_init(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;
	int ret;

	pci_set_drvdata(zxdh_dev->pdev, zxdh_dev);

	ret = pci_enable_device(zxdh_dev->pdev);
	if (ret) {
		dev_err(zxdh_dev->device, "pci_enable_device failed: %d\n", ret);
		return ret;
	}

	dma_set_mask_and_coherent(zxdh_dev->device, DMA_BIT_MASK(64));

	ret = pci_request_selected_regions(zxdh_dev->pdev,
					   pci_select_bars(zxdh_dev->pdev, IORESOURCE_MEM),
					   "dh-pf");
	if (ret) {
		dev_err(zxdh_dev->device, "pci_request_selected_regions failed: %d\n", ret);
		goto err_pci;
	}

	pci_set_master(zxdh_dev->pdev);
	ret = pci_save_state(zxdh_dev->pdev);
	if (ret) {
		dev_err(zxdh_dev->device, "pci_save_state failed: %d\n", ret);
		goto err_pci_save_state;
	}

	if (!(pci_resource_flags(zxdh_dev->pdev, 0) & IORESOURCE_MEM)) {
		ret = -ENODEV;
		dev_err(zxdh_dev->device, "BAR 0 is not an MMIO resource\n");
		goto err_pci_save_state;
	}

	pf_dev->pci_ioremap_addr[0] =
		ioremap(pci_resource_start(zxdh_dev->pdev, 0),
			pci_resource_len(zxdh_dev->pdev, 0));
	if (!pf_dev->pci_ioremap_addr[0]) {
		ret = -ENOMEM;
		dev_err(zxdh_dev->device, "dh pf pci ioremap failed\n");
		goto err_pci_save_state;
	}

	return 0;

err_pci_save_state:
	pci_release_selected_regions(zxdh_dev->pdev,
				     pci_select_bars(zxdh_dev->pdev, IORESOURCE_MEM));
err_pci:
	pci_disable_device(zxdh_dev->pdev);
	return ret;
}

void zxdh_pf_pci_close(struct zxdh_core_dev *zxdh_dev)
{
	struct zxdh_pf_dev *pf_dev = zxdh_dev->priv;

	iounmap(pf_dev->pci_ioremap_addr[0]);
	pci_release_selected_regions(zxdh_dev->pdev,
				     pci_select_bars(zxdh_dev->pdev, IORESOURCE_MEM));
	pci_disable_device(zxdh_dev->pdev);
}

static int zxdh_pf_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct zxdh_core_dev *zxdh_dev;
	struct zxdh_pf_dev *pf_dev;
	struct devlink *devlink;
	int ret;

	devlink = devlink_alloc(&zxdh_pf_devlink_ops, sizeof(struct zxdh_core_dev),
				&pdev->dev);
	if (!devlink)
		return -ENOMEM;

	zxdh_dev = devlink_priv(devlink);
	zxdh_dev->device = &pdev->dev;
	zxdh_dev->pdev = pdev;
	zxdh_dev->devlink = devlink;

	pf_dev = zxdh_core_alloc_priv(zxdh_dev, sizeof(*pf_dev));
	if (!pf_dev) {
		dev_err(&pdev->dev, "zxdh_pf_dev alloc failed\n");
		ret = -ENOMEM;
		goto err_pf_dev;
	}

	ret = zxdh_pf_pci_init(zxdh_dev);
	if (ret) {
		dev_err(&pdev->dev, "zxdh_pf_pci_init failed: %d\n", ret);
		goto err_pci_init;
	}

	devlink_register(devlink);

	return 0;

err_pci_init:
	zxdh_core_free_priv(zxdh_dev);
err_pf_dev:
	devlink_free(devlink);
	return ret;
}

static void zxdh_pf_remove(struct pci_dev *pdev)
{
	struct zxdh_core_dev *zxdh_dev = pci_get_drvdata(pdev);
	struct devlink *devlink = priv_to_devlink(zxdh_dev);

	devlink_unregister(devlink);
	zxdh_pf_pci_close(zxdh_dev);
	zxdh_core_free_priv(zxdh_dev);
	devlink_free(devlink);
	pci_set_drvdata(pdev, NULL);
}

static void zxdh_pf_shutdown(struct pci_dev *pdev)
{
	if (system_state == SYSTEM_POWER_OFF)
		pci_set_power_state(pdev, PCI_D3hot);
	pci_disable_device(pdev);
}

static struct pci_driver zxdh_pf_driver = {
	.name = "dinghai10e",
	.id_table = zxdh_pf_pci_table,
	.probe = zxdh_pf_probe,
	.remove = zxdh_pf_remove,
	.shutdown = zxdh_pf_shutdown,
};

module_pci_driver(zxdh_pf_driver);

// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026 Nebula Matrix Limited.
 */

#include <linux/device.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/bits.h>
#include "nbl_include/nbl_include.h"
#include "nbl_include/nbl_def_hw.h"
#include "nbl_include/nbl_def_common.h"
#include "nbl_core.h"

struct nbl_adapter *nbl_core_init(struct pci_dev *pdev,
				  struct nbl_init_param *param)
{
	struct nbl_common_info *common;
	struct nbl_adapter *adapter;
	int ret;

	adapter = devm_kzalloc(&pdev->dev, sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return ERR_PTR(-ENOMEM);

	adapter->pdev = pdev;
	common = &adapter->common;

	common->pdev = pdev;
	common->dev = &pdev->dev;
	common->has_ctrl = param->caps.has_ctrl;
	common->has_net = param->caps.has_net;
	common->function = PCI_FUNC(pdev->devfn);
	common->devid = PCI_SLOT(pdev->devfn);
	common->bus = pdev->bus->number;

	ret = nbl_hw_init_leonis(adapter);
	if (ret)
		goto hw_init_fail;

	return adapter;
hw_init_fail:
	return ERR_PTR(ret);
}

void nbl_core_remove(struct nbl_adapter *adapter)
{
	nbl_hw_remove_leonis(adapter);
}

static void nbl_get_func_param(struct pci_dev *pdev, kernel_ulong_t driver_data,
			       struct nbl_init_param *param)
{
	param->caps.has_net = !!(driver_data & BIT(NBL_CAP_HAS_NET_BIT));

	/*
	 * Hardware fixed rule: physical PF0 is the only management PF with
	 * global ctrl capability. All PFs share identical PCI device ID, so
	 * distinguish control PF via physical function ID.
	 *
	 * Hardware & firmware design FORBID passing any PF through to virtual
	 * machines, there is no scenario where a non-management PF appears
	 * as Func 0 inside guest. Thus using PCI_FUNC(pdev->devfn) to identify
	 *  control PF is safe on our platform.
	 */
	if ((PCI_FUNC(pdev->devfn) == 0) && !pdev->is_virtfn)
		param->caps.has_ctrl = 1;
}

static int nbl_probe(struct pci_dev *pdev,
		     const struct pci_device_id *id)
{
	struct nbl_init_param param = { { 0 } };
	struct device *dev = &pdev->dev;
	struct nbl_adapter *adapter;
	int err;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI dev, err=%d\n", err);
		return err;
	}

	nbl_get_func_param(pdev, id->driver_data, &param);
	/* never return fail when DMA_BIT_MASK(64) */
	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));

	pci_set_master(pdev);

	adapter = nbl_core_init(pdev, &param);
	if (IS_ERR(adapter)) {
		dev_err(dev, "Nbl adapter init fail: %pe\n", adapter);
		err = PTR_ERR(adapter);
		goto adapter_init_err;
	}
	pci_set_drvdata(pdev, adapter);
	return 0;
adapter_init_err:
	pci_clear_master(pdev);
	return err;
}

static void nbl_remove(struct pci_dev *pdev)
{
	struct nbl_adapter *adapter = pci_get_drvdata(pdev);

	if (!adapter)
		return;
	pci_set_drvdata(pdev, NULL);
	nbl_core_remove(adapter);

	pci_clear_master(pdev);
}

/*
 * PCI Device IDs for Leonis/NBL Network Controllers
 *
 * Vendor ID: 0x1F0F
 * SNIC v3r1 product Device IDs range: 0x3403-0x3412
 */
#define NBL_VENDOR_ID				0x1F0F

#define NBL_DEVICE_ID_M18110			0x3403
#define NBL_DEVICE_ID_M18110_LX			0x3404
#define NBL_DEVICE_ID_M18110_BASE_T		0x3405
#define NBL_DEVICE_ID_M18110_LX_BASE_T		0x3406
#define NBL_DEVICE_ID_M18110_OCP		0x3407
#define NBL_DEVICE_ID_M18110_LX_OCP		0x3408
#define NBL_DEVICE_ID_M18110_BASE_T_OCP		0x3409
#define NBL_DEVICE_ID_M18110_LX_BASE_T_OCP	0x340a
#define NBL_DEVICE_ID_M18000			0x340b
#define NBL_DEVICE_ID_M18000_LX			0x340c
#define NBL_DEVICE_ID_M18000_BASE_T		0x340d
#define NBL_DEVICE_ID_M18000_LX_BASE_T		0x340e
#define NBL_DEVICE_ID_M18000_OCP		0x340f
#define NBL_DEVICE_ID_M18000_LX_OCP		0x3410
#define NBL_DEVICE_ID_M18000_BASE_T_OCP		0x3411
#define NBL_DEVICE_ID_M18000_LX_BASE_T_OCP	0x3412

/* All below IDs belong to Leonis ASIC family, different form-factor variants,
 * share the same hardware initialization flow without differentiated ops.
 */
static const struct pci_device_id nbl_id_table[] = {
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110_LX),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110_BASE_T),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110_LX_BASE_T),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110_LX_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110_BASE_T_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18110_LX_BASE_T_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000_LX),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000_BASE_T),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000_LX_BASE_T),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000_LX_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000_BASE_T_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	{ PCI_DEVICE(NBL_VENDOR_ID, NBL_DEVICE_ID_M18000_LX_BASE_T_OCP),
	  .driver_data = BIT(NBL_CAP_HAS_NET_BIT) },
	/* required as sentinel */
	{ }
};
MODULE_DEVICE_TABLE(pci, nbl_id_table);

static struct pci_driver nbl_driver = {
	.name = NBL_DRIVER_NAME,
	.id_table = nbl_id_table,
	.probe = nbl_probe,
	.remove = nbl_remove,
};

module_pci_driver(nbl_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nebula Matrix Network Driver");
MODULE_AUTHOR("Illusion Wang <illusion.wang@nebula-matrix.com>");

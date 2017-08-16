/* Synopsys DesignWare Core Enterprise Ethernet (XLGMAC) Driver
 *
 * Copyright (c) 2017 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is dual-licensed; you may select either version 2 of
 * the GNU General Public License ("GPL") or BSD license ("BSD").
 *
 * This Synopsys DWC XLGMAC software driver and associated documentation
 * (hereinafter the "Software") is an unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing between
 * Synopsys and you. The Software IS NOT an item of Licensed Software or a
 * Licensed Product under any End User Software License Agreement or
 * Agreement for Licensed Products with Synopsys or any supplement thereto.
 * Synopsys is a registered trademark of Synopsys, Inc. Other names included
 * in the SOFTWARE may be the trademarks of their respective owners.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "dwc-xlgmac.h"
#include "dwc-xlgmac-reg.h"

static int xlgmac_probe(struct pci_dev *pcidev, const struct pci_device_id *id)
{
	struct device *dev = &pcidev->dev;
	struct xlgmac_resources res;
	int i, ret;

	ret = pcim_enable_device(pcidev);
	if (ret) {
		dev_err(dev, "ERROR: failed to enable device\n");
		return ret;
	}

	for (i = 0; i <= PCI_STD_RESOURCE_END; i++) {
		if (pci_resource_len(pcidev, i) == 0)
			continue;
		ret = pcim_iomap_regions(pcidev, BIT(i), XLGMAC_DRV_NAME);
		if (ret)
			return ret;
		break;
	}

	pci_set_master(pcidev);

	memset(&res, 0, sizeof(res));
	res.irq = pcidev->irq;
	res.addr = pcim_iomap_table(pcidev)[i];

	return xlgmac_drv_probe(&pcidev->dev, &res);
}

static void xlgmac_remove(struct pci_dev *pcidev)
{
	xlgmac_drv_remove(&pcidev->dev);
}

static const struct pci_device_id xlgmac_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS, 0x7302) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, xlgmac_pci_tbl);

static struct pci_driver xlgmac_pci_driver = {
	.name		= XLGMAC_DRV_NAME,
	.id_table	= xlgmac_pci_tbl,
	.probe		= xlgmac_probe,
	.remove		= xlgmac_remove,
};

module_pci_driver(xlgmac_pci_driver);

MODULE_DESCRIPTION(XLGMAC_DRV_DESC);
MODULE_VERSION(XLGMAC_DRV_VERSION);
MODULE_AUTHOR("Jie Deng <jiedeng@synopsys.com>");
MODULE_LICENSE("Dual BSD/GPL");

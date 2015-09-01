/*
 * PCIe endpoint skeleton driver for IMX6 SOCs
 *
 * Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci-aspm.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/of.h>

#define DRV_DESCRIPTION	"i.MX PCIE endpoint device driver"
#define DRV_VERSION	"version 0.1"
#define DRV_NAME	"imx_pcie_ep"

struct imx_pcie_ep_priv {
	struct pci_dev *pci_dev;
	void __iomem *hw_base;
};

/**
 * imx_pcie_ep_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @id: entry in id_tbl
 *
 * Returns 0 on success, negative on failure
 **/
static int imx_pcie_ep_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	int ret = 0;
	unsigned int msi_addr = 0;
	struct device_node *np;
	struct device *dev = &pdev->dev;
	struct imx_pcie_ep_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "can't alloc imx pcie priv\n");
		return -ENOMEM;
	}

	priv->pci_dev = pdev;

	if (pci_enable_device(pdev)) {
		ret = -ENODEV;
		goto out;
	}
	pci_set_master(pdev);

	pci_set_drvdata(pdev, priv);

	priv->hw_base = pci_iomap(pdev, 0, 0);
	if (!priv->hw_base) {
		ret = -ENODEV;
		goto out;
	}

	pr_info("pci_resource_len = 0x%08llx\n",
		(unsigned long long) pci_resource_len(pdev, 0));
	pr_info("pci_resource_base = %p\n", priv->hw_base);

	ret = pci_enable_msi(priv->pci_dev);
	if (ret < 0) {
		dev_err(dev, "can't enable msi\n");
		return ret;
	}

	np = of_find_compatible_node(NULL, NULL, "snps,dw-pcie");

	/*
	 * Force to use the hardcode MSI address to do the MSI demo
	 */
	if (of_device_is_compatible(np, "fsl,imx7d-pcie"))
		msi_addr = 0x4FFC0000;
	else if (of_device_is_compatible(np, "fsl,imx6sx-pcie"))
		msi_addr = 0x08FF8000;
	else
		msi_addr = 0x01FF8000;

	pr_info("pci_msi_addr = 0x%08x\n", msi_addr);
	pci_bus_write_config_dword(pdev->bus, 0, 0x54, msi_addr);
	pci_bus_write_config_dword(pdev->bus->parent, 0, 0x820, msi_addr);

	/* configure rc's msi cap */
	pci_bus_read_config_dword(pdev->bus->parent, 0, 0x50, &ret);
	ret |= (PCI_MSI_FLAGS_ENABLE << 16);
	pci_bus_write_config_dword(pdev->bus->parent, 0, 0x50, ret);
	pci_bus_write_config_dword(pdev->bus->parent, 0, 0x828, 0x1);
	pci_bus_write_config_dword(pdev->bus->parent, 0, 0x82C, 0xFFFFFFFE);

	return 0;

out:
	return ret;
}

static void imx_pcie_ep_remove(struct pci_dev *pdev)
{
	struct imx_pcie_ep_priv *priv = pci_get_drvdata(pdev);

	if (!priv)
		return;
	pr_info("***imx pcie ep driver unload***\n");
}

static struct pci_device_id imx_pcie_ep_ids[] = {
	{
	.class =	PCI_CLASS_MEMORY_RAM << 8,
	.class_mask =	~0,
	.vendor =	0xbeaf,
	.device =	0xdead,
	.subvendor =	PCI_ANY_ID,
	.subdevice =	PCI_ANY_ID,
	},
	{ }	/* terminate list */
};
MODULE_DEVICE_TABLE(pci, imx_pcie_ep_ids);

static struct pci_driver imx_pcie_ep_driver = {
	.name = DRV_NAME,
	.id_table = imx_pcie_ep_ids,
	.probe = imx_pcie_ep_probe,
	.remove = imx_pcie_ep_remove,
};

static int __init imx_pcie_ep_init(void)
{
	int ret;
	pr_info(DRV_DESCRIPTION ", " DRV_VERSION "\n");

	ret = pci_register_driver(&imx_pcie_ep_driver);
	if (ret)
		pr_err("Unable to initialize PCI module\n");

	return ret;
}

static void __exit imx_pcie_ep_exit(void)
{
	pci_unregister_driver(&imx_pcie_ep_driver);
}

module_exit(imx_pcie_ep_exit);
module_init(imx_pcie_ep_init);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("imx_pcie_ep");

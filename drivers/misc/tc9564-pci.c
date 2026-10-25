// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2026 by RISCstar Solutions Corporation.  All rights reserved.
 */

/*
 * The Toshiba TC9564 implements a PCIe Gen 3 switch that connects an
 * upstream x4 port to three downstream PCIe ports.  Two of the downstream
 * ports are external, and the third is internal, implementing a PCIe
 * endpoint which implements two PCIe functions.  Each PCIe function drives
 * a Synopsys XGMAC Ethernet interface capable of 10 Gbps operation.
 *
 * The TC9564 implements other functionality, including an embedded MCU,
 * a UART, a GPIO controller, a reset controller, a clock controller, and
 * interrupt handling.  These features are separate from (and in some
 * cases used by) both Ethernet XGMACs.  Each Ethernet MAC must be
 * attached to a working PHY for it to be functional, and for this
 * reason either of them (or both!) might not be usable/used.
 *
 * This PCI driver binds to the Toshiba TC9564 (physical) PCI function
 * (VID 0x1179, DID 0x0220).
 */

#include <linux/device.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>

static void tc9564_function_remove(struct pci_dev *pdev)
{
	of_platform_depopulate(&pdev->dev);
	pci_clear_master(pdev);
	pci_disable_device(pdev);
}

static int
tc9564_function_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	int ret;

	/* Despite being a PCI device, we require devicetree */
	np = dev_of_node(dev);
	if (!np)
		return dev_err_probe(dev, -EINVAL, "no devicetree node\n");

	ret = pci_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable device\n");

	pci_set_master(pdev);

	/* Scan for pci-ep-bus (simple-bus) nodes and probe their sub-devices */
	ret = of_platform_default_populate(np, NULL, dev);
	if (ret)
		goto err_depopulate;

	return 0;

err_depopulate:
	tc9564_function_remove(pdev);

	return dev_err_probe(dev, ret, "failed to populate platform bus\n");
}

static const struct pci_device_id tc9564_function_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA, 0x0220), },
	{ },
};
MODULE_DEVICE_TABLE(pci, tc9564_function_id_table);

static struct pci_driver tc9564_function_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= tc9564_function_id_table,
	.probe		= tc9564_function_probe,
	.remove		= tc9564_function_remove,
};
module_pci_driver(tc9564_function_driver);

MODULE_DESCRIPTION("Toshiba TC9564 PCIe Embedded Function Driver");
MODULE_LICENSE("GPL");

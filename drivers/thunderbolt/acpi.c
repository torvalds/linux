// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI support
 *
 * Copyright (C) 2020, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/pm_runtime.h>

#include "tb.h"

static acpi_status tb_acpi_add_link(acpi_handle handle, u32 level, void *data,
				    void **return_value)
{
	struct fwnode_reference_args args;
	struct fwnode_handle *fwnode;
	struct tb_nhi *nhi = data;
	struct acpi_device *adev;
	struct pci_dev *pdev;
	struct device *dev;
	int ret;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	fwnode = acpi_fwnode_handle(adev);
	ret = fwnode_property_get_reference_args(fwnode, "usb4-host-interface",
						 NULL, 0, 0, &args);
	if (ret)
		return AE_OK;

	/* It needs to reference this NHI */
	if (nhi->pdev->dev.fwnode != args.fwnode)
		goto out_put;

	/*
	 * Try to find physical device walking upwards to the hierarcy.
	 * We need to do this because the xHCI driver might not yet be
	 * bound so the USB3 SuperSpeed ports are not yet created.
	 */
	dev = acpi_get_first_physical_node(adev);
	while (!dev) {
		adev = adev->parent;
		if (!adev)
			break;
		dev = acpi_get_first_physical_node(adev);
	}

	if (!dev)
		goto out_put;

	/*
	 * Check that the device is PCIe. This is because USB3
	 * SuperSpeed ports have this property and they are not power
	 * managed with the xHCI and the SuperSpeed hub so we create the
	 * link from xHCI instead.
	 */
	while (dev && !dev_is_pci(dev))
		dev = dev->parent;

	if (!dev)
		goto out_put;

	/*
	 * Check that this actually matches the type of device we
	 * expect. It should either be xHCI or PCIe root/downstream
	 * port.
	 */
	pdev = to_pci_dev(dev);
	if (pdev->class == PCI_CLASS_SERIAL_USB_XHCI ||
	    (pci_is_pcie(pdev) &&
		(pci_pcie_type(pdev) == PCI_EXP_TYPE_ROOT_PORT ||
		 pci_pcie_type(pdev) == PCI_EXP_TYPE_DOWNSTREAM))) {
		const struct device_link *link;

		/*
		 * Make them both active first to make sure the NHI does
		 * not runtime suspend before the consumer. The
		 * pm_runtime_put() below then allows the consumer to
		 * runtime suspend again (which then allows NHI runtime
		 * suspend too now that the device link is established).
		 */
		pm_runtime_get_sync(&pdev->dev);

		link = device_link_add(&pdev->dev, &nhi->pdev->dev,
				       DL_FLAG_AUTOREMOVE_SUPPLIER |
				       DL_FLAG_RPM_ACTIVE |
				       DL_FLAG_PM_RUNTIME);
		if (link) {
			dev_dbg(&nhi->pdev->dev, "created link from %s\n",
				dev_name(&pdev->dev));
		} else {
			dev_warn(&nhi->pdev->dev, "device link creation from %s failed\n",
				 dev_name(&pdev->dev));
		}

		pm_runtime_put(&pdev->dev);
	}

out_put:
	fwnode_handle_put(args.fwnode);
	return AE_OK;
}

/**
 * tb_acpi_add_links() - Add device links based on ACPI description
 * @nhi: Pointer to NHI
 *
 * Goes over ACPI namespace finding tunneled ports that reference to
 * @nhi ACPI node. For each reference a device link is added. The link
 * is automatically removed by the driver core.
 */
void tb_acpi_add_links(struct tb_nhi *nhi)
{
	acpi_status status;

	if (!has_acpi_companion(&nhi->pdev->dev))
		return;

	/*
	 * Find all devices that have usb4-host-controller interface
	 * property that references to this NHI.
	 */
	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, 32,
				     tb_acpi_add_link, NULL, nhi, NULL);
	if (ACPI_FAILURE(status))
		dev_warn(&nhi->pdev->dev, "failed to enumerate tunneled ports\n");
}

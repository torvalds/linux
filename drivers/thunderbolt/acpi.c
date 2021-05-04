// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI support
 *
 * Copyright (C) 2020, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/acpi.h>

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

		link = device_link_add(&pdev->dev, &nhi->pdev->dev,
				       DL_FLAG_AUTOREMOVE_SUPPLIER |
				       DL_FLAG_PM_RUNTIME);
		if (link) {
			dev_dbg(&nhi->pdev->dev, "created link from %s\n",
				dev_name(&pdev->dev));
		} else {
			dev_warn(&nhi->pdev->dev, "device link creation from %s failed\n",
				 dev_name(&pdev->dev));
		}
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

/**
 * tb_acpi_is_native() - Did the platform grant native TBT/USB4 control
 *
 * Returns %true if the platform granted OS native control over
 * TBT/USB4. In this case software based connection manager can be used,
 * otherwise there is firmware based connection manager running.
 */
bool tb_acpi_is_native(void)
{
	return osc_sb_native_usb4_support_confirmed &&
	       osc_sb_native_usb4_control;
}

/**
 * tb_acpi_may_tunnel_usb3() - Is USB3 tunneling allowed by the platform
 *
 * When software based connection manager is used, this function
 * returns %true if platform allows native USB3 tunneling.
 */
bool tb_acpi_may_tunnel_usb3(void)
{
	if (tb_acpi_is_native())
		return osc_sb_native_usb4_control & OSC_USB_USB3_TUNNELING;
	return true;
}

/**
 * tb_acpi_may_tunnel_dp() - Is DisplayPort tunneling allowed by the platform
 *
 * When software based connection manager is used, this function
 * returns %true if platform allows native DP tunneling.
 */
bool tb_acpi_may_tunnel_dp(void)
{
	if (tb_acpi_is_native())
		return osc_sb_native_usb4_control & OSC_USB_DP_TUNNELING;
	return true;
}

/**
 * tb_acpi_may_tunnel_pcie() - Is PCIe tunneling allowed by the platform
 *
 * When software based connection manager is used, this function
 * returns %true if platform allows native PCIe tunneling.
 */
bool tb_acpi_may_tunnel_pcie(void)
{
	if (tb_acpi_is_native())
		return osc_sb_native_usb4_control & OSC_USB_PCIE_TUNNELING;
	return true;
}

/**
 * tb_acpi_is_xdomain_allowed() - Are XDomain connections allowed
 *
 * When software based connection manager is used, this function
 * returns %true if platform allows XDomain connections.
 */
bool tb_acpi_is_xdomain_allowed(void)
{
	if (tb_acpi_is_native())
		return osc_sb_native_usb4_control & OSC_USB_XDOMAIN;
	return true;
}

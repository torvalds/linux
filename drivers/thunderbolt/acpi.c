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
	struct acpi_device *adev = acpi_fetch_acpi_dev(handle);
	struct fwnode_handle *fwnode;
	struct tb_nhi *nhi = data;
	struct pci_dev *pdev;
	struct device *dev;

	if (!adev)
		return AE_OK;

	fwnode = fwnode_find_reference(acpi_fwnode_handle(adev), "usb4-host-interface", 0);
	if (IS_ERR(fwnode))
		return AE_OK;

	/* It needs to reference this NHI */
	if (dev_fwnode(&nhi->pdev->dev) != fwnode)
		goto out_put;

	/*
	 * Try to find physical device walking upwards to the hierarcy.
	 * We need to do this because the xHCI driver might not yet be
	 * bound so the USB3 SuperSpeed ports are not yet created.
	 */
	dev = acpi_get_first_physical_node(adev);
	while (!dev) {
		adev = acpi_dev_parent(adev);
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
	fwnode_handle_put(fwnode);
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

/* UUID for retimer _DSM: e0053122-795b-4122-8a5e-57be1d26acb3 */
static const guid_t retimer_dsm_guid =
	GUID_INIT(0xe0053122, 0x795b, 0x4122,
		  0x8a, 0x5e, 0x57, 0xbe, 0x1d, 0x26, 0xac, 0xb3);

#define RETIMER_DSM_QUERY_ONLINE_STATE	1
#define RETIMER_DSM_SET_ONLINE_STATE	2

static int tb_acpi_retimer_set_power(struct tb_port *port, bool power)
{
	struct usb4_port *usb4 = port->usb4;
	union acpi_object argv4[2];
	struct acpi_device *adev;
	union acpi_object *obj;
	int ret;

	if (!usb4->can_offline)
		return 0;

	adev = ACPI_COMPANION(&usb4->dev);
	if (WARN_ON(!adev))
		return 0;

	/* Check if we are already powered on (and in correct mode) */
	obj = acpi_evaluate_dsm_typed(adev->handle, &retimer_dsm_guid, 1,
				      RETIMER_DSM_QUERY_ONLINE_STATE, NULL,
				      ACPI_TYPE_INTEGER);
	if (!obj) {
		tb_port_warn(port, "ACPI: query online _DSM failed\n");
		return -EIO;
	}

	ret = obj->integer.value;
	ACPI_FREE(obj);

	if (power == ret)
		return 0;

	tb_port_dbg(port, "ACPI: calling _DSM to power %s retimers\n",
		    power ? "on" : "off");

	argv4[0].type = ACPI_TYPE_PACKAGE;
	argv4[0].package.count = 1;
	argv4[0].package.elements = &argv4[1];
	argv4[1].integer.type = ACPI_TYPE_INTEGER;
	argv4[1].integer.value = power;

	obj = acpi_evaluate_dsm_typed(adev->handle, &retimer_dsm_guid, 1,
				      RETIMER_DSM_SET_ONLINE_STATE, argv4,
				      ACPI_TYPE_INTEGER);
	if (!obj) {
		tb_port_warn(port,
			     "ACPI: set online state _DSM evaluation failed\n");
		return -EIO;
	}

	ret = obj->integer.value;
	ACPI_FREE(obj);

	if (ret >= 0) {
		if (power)
			return ret == 1 ? 0 : -EBUSY;
		return 0;
	}

	tb_port_warn(port, "ACPI: set online state _DSM failed with error %d\n", ret);
	return -EIO;
}

/**
 * tb_acpi_power_on_retimers() - Call platform to power on retimers
 * @port: USB4 port
 *
 * Calls platform to turn on power to all retimers behind this USB4
 * port. After this function returns successfully the caller can
 * continue with the normal retimer flows (as specified in the USB4
 * spec). Note if this returns %-EBUSY it means the type-C port is in
 * non-USB4/TBT mode (there is non-USB4/TBT device connected).
 *
 * This should only be called if the USB4/TBT link is not up.
 *
 * Returns %0 on success.
 */
int tb_acpi_power_on_retimers(struct tb_port *port)
{
	return tb_acpi_retimer_set_power(port, true);
}

/**
 * tb_acpi_power_off_retimers() - Call platform to power off retimers
 * @port: USB4 port
 *
 * This is the opposite of tb_acpi_power_on_retimers(). After returning
 * successfully the normal operations with the @port can continue.
 *
 * Returns %0 on success.
 */
int tb_acpi_power_off_retimers(struct tb_port *port)
{
	return tb_acpi_retimer_set_power(port, false);
}

static bool tb_acpi_bus_match(struct device *dev)
{
	return tb_is_switch(dev) || tb_is_usb4_port_device(dev);
}

static struct acpi_device *tb_acpi_switch_find_companion(struct tb_switch *sw)
{
	struct acpi_device *adev = NULL;
	struct tb_switch *parent_sw;

	/*
	 * Device routers exists under the downstream facing USB4 port
	 * of the parent router. Their _ADR is always 0.
	 */
	parent_sw = tb_switch_parent(sw);
	if (parent_sw) {
		struct tb_port *port = tb_port_at(tb_route(sw), parent_sw);
		struct acpi_device *port_adev;

		port_adev = acpi_find_child_by_adr(ACPI_COMPANION(&parent_sw->dev),
						   port->port);
		if (port_adev)
			adev = acpi_find_child_device(port_adev, 0, false);
	} else {
		struct tb_nhi *nhi = sw->tb->nhi;
		struct acpi_device *parent_adev;

		parent_adev = ACPI_COMPANION(&nhi->pdev->dev);
		if (parent_adev)
			adev = acpi_find_child_device(parent_adev, 0, false);
	}

	return adev;
}

static struct acpi_device *tb_acpi_find_companion(struct device *dev)
{
	/*
	 * The Thunderbolt/USB4 hierarchy looks like following:
	 *
	 * Device (NHI)
	 *   Device (HR)		// Host router _ADR == 0
	 *      Device (DFP0)		// Downstream port _ADR == lane 0 adapter
	 *        Device (DR)		// Device router _ADR == 0
	 *          Device (UFP)	// Upstream port _ADR == lane 0 adapter
	 *      Device (DFP1)		// Downstream port _ADR == lane 0 adapter number
	 *
	 * At the moment we bind the host router to the corresponding
	 * Linux device.
	 */
	if (tb_is_switch(dev))
		return tb_acpi_switch_find_companion(tb_to_switch(dev));
	else if (tb_is_usb4_port_device(dev))
		return acpi_find_child_by_adr(ACPI_COMPANION(dev->parent),
					      tb_to_usb4_port_device(dev)->port->port);
	return NULL;
}

static void tb_acpi_setup(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct usb4_port *usb4 = tb_to_usb4_port_device(dev);

	if (!adev || !usb4)
		return;

	if (acpi_check_dsm(adev->handle, &retimer_dsm_guid, 1,
			   BIT(RETIMER_DSM_QUERY_ONLINE_STATE) |
			   BIT(RETIMER_DSM_SET_ONLINE_STATE)))
		usb4->can_offline = true;
}

static struct acpi_bus_type tb_acpi_bus = {
	.name = "thunderbolt",
	.match = tb_acpi_bus_match,
	.find_companion = tb_acpi_find_companion,
	.setup = tb_acpi_setup,
};

int tb_acpi_init(void)
{
	return register_acpi_bus_type(&tb_acpi_bus);
}

void tb_acpi_exit(void)
{
	unregister_acpi_bus_type(&tb_acpi_bus);
}

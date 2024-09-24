// SPDX-License-Identifier: GPL-2.0+
/*
 * Intel XHCI (Cherry Trail, Broxton and others) USB OTG role switch driver
 *
 * Copyright (c) 2016-2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Loosely based on android x86 kernel code which is:
 *
 * Copyright (C) 2014 Intel Corp.
 *
 * Author: Wu, Hao
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/usb/role.h>

/* register definition */
#define DUAL_ROLE_CFG0			0x68
#define SW_VBUS_VALID			BIT(24)
#define SW_IDPIN_EN			BIT(21)
#define SW_IDPIN			BIT(20)
#define SW_SWITCH_EN			BIT(16)

#define DRD_CONFIG_DYNAMIC		0
#define DRD_CONFIG_STATIC_HOST		1
#define DRD_CONFIG_STATIC_DEVICE	2
#define DRD_CONFIG_MASK			3

#define DUAL_ROLE_CFG1			0x6c
#define HOST_MODE			BIT(29)

#define DUAL_ROLE_CFG1_POLL_TIMEOUT	1000

#define DRV_NAME			"intel_xhci_usb_sw"

struct intel_xhci_usb_data {
	struct device *dev;
	struct usb_role_switch *role_sw;
	void __iomem *base;
	bool enable_sw_switch;
};

static const struct software_node intel_xhci_usb_node = {
	"intel-xhci-usb-sw",
};

static int intel_xhci_usb_set_role(struct usb_role_switch *sw,
				   enum usb_role role)
{
	struct intel_xhci_usb_data *data = usb_role_switch_get_drvdata(sw);
	unsigned long timeout;
	acpi_status status;
	u32 glk, val;
	u32 drd_config = DRD_CONFIG_DYNAMIC;

	/*
	 * On many CHT devices ACPI event (_AEI) handlers read / modify /
	 * write the cfg0 register, just like we do. Take the ACPI lock
	 * to avoid us racing with the AML code.
	 */
	status = acpi_acquire_global_lock(ACPI_WAIT_FOREVER, &glk);
	if (ACPI_FAILURE(status) && status != AE_NOT_CONFIGURED) {
		dev_err(data->dev, "Error could not acquire lock\n");
		return -EIO;
	}

	pm_runtime_get_sync(data->dev);

	/*
	 * Set idpin value as requested.
	 * Since some devices rely on firmware setting DRD_CONFIG and
	 * SW_SWITCH_EN bits to be zero for role switch,
	 * do not set these bits for those devices.
	 */
	val = readl(data->base + DUAL_ROLE_CFG0);
	switch (role) {
	case USB_ROLE_NONE:
		val |= SW_IDPIN;
		val &= ~SW_VBUS_VALID;
		drd_config = DRD_CONFIG_DYNAMIC;
		break;
	case USB_ROLE_HOST:
		val &= ~SW_IDPIN;
		val &= ~SW_VBUS_VALID;
		drd_config = DRD_CONFIG_STATIC_HOST;
		break;
	case USB_ROLE_DEVICE:
		val |= SW_IDPIN;
		val |= SW_VBUS_VALID;
		drd_config = DRD_CONFIG_STATIC_DEVICE;
		break;
	}
	val |= SW_IDPIN_EN;
	if (data->enable_sw_switch) {
		val &= ~DRD_CONFIG_MASK;
		val |= SW_SWITCH_EN | drd_config;
	}
	writel(val, data->base + DUAL_ROLE_CFG0);

	acpi_release_global_lock(glk);

	/* In most case it takes about 600ms to finish mode switching */
	timeout = jiffies + msecs_to_jiffies(DUAL_ROLE_CFG1_POLL_TIMEOUT);

	/* Polling on CFG1 register to confirm mode switch.*/
	do {
		val = readl(data->base + DUAL_ROLE_CFG1);
		if (!!(val & HOST_MODE) == (role == USB_ROLE_HOST)) {
			pm_runtime_put(data->dev);
			return 0;
		}

		/* Interval for polling is set to about 5 - 10 ms */
		usleep_range(5000, 10000);
	} while (time_before(jiffies, timeout));

	pm_runtime_put(data->dev);

	dev_warn(data->dev, "Timeout waiting for role-switch\n");
	return -ETIMEDOUT;
}

static enum usb_role intel_xhci_usb_get_role(struct usb_role_switch *sw)
{
	struct intel_xhci_usb_data *data = usb_role_switch_get_drvdata(sw);
	enum usb_role role;
	u32 val;

	pm_runtime_get_sync(data->dev);
	val = readl(data->base + DUAL_ROLE_CFG0);
	pm_runtime_put(data->dev);

	if (!(val & SW_IDPIN))
		role = USB_ROLE_HOST;
	else if (val & SW_VBUS_VALID)
		role = USB_ROLE_DEVICE;
	else
		role = USB_ROLE_NONE;

	return role;
}

static int intel_xhci_usb_probe(struct platform_device *pdev)
{
	struct usb_role_switch_desc sw_desc = { };
	struct device *dev = &pdev->dev;
	struct intel_xhci_usb_data *data;
	struct resource *res;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	data->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!data->base)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	ret = software_node_register(&intel_xhci_usb_node);
	if (ret)
		return ret;

	sw_desc.set = intel_xhci_usb_set_role,
	sw_desc.get = intel_xhci_usb_get_role,
	sw_desc.allow_userspace_control = true,
	sw_desc.fwnode = software_node_fwnode(&intel_xhci_usb_node);
	sw_desc.driver_data = data;

	data->dev = dev;
	data->enable_sw_switch = !device_property_read_bool(dev,
						"sw_switch_disable");

	data->role_sw = usb_role_switch_register(dev, &sw_desc);
	if (IS_ERR(data->role_sw)) {
		fwnode_handle_put(sw_desc.fwnode);
		return PTR_ERR(data->role_sw);
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	return 0;
}

static void intel_xhci_usb_remove(struct platform_device *pdev)
{
	struct intel_xhci_usb_data *data = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	usb_role_switch_unregister(data->role_sw);
	fwnode_handle_put(software_node_fwnode(&intel_xhci_usb_node));
}

static const struct platform_device_id intel_xhci_usb_table[] = {
	{ .name = DRV_NAME },
	{}
};
MODULE_DEVICE_TABLE(platform, intel_xhci_usb_table);

static struct platform_driver intel_xhci_usb_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.id_table = intel_xhci_usb_table,
	.probe = intel_xhci_usb_probe,
	.remove = intel_xhci_usb_remove,
};

module_platform_driver(intel_xhci_usb_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_DESCRIPTION("Intel XHCI USB role switch driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0
/*
 * ACPI PCI Hot Plug Extension for Ampere Altra. Allows control of
 * attention LEDs via requests to system firmware.
 *
 * Copyright (C) 2023 Ampere Computing LLC
 */

#define pr_fmt(fmt) "acpiphp_ampere_altra: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/platform_device.h>

#include "acpiphp.h"

#define HANDLE_OPEN	0xb0200000
#define HANDLE_CLOSE	0xb0300000
#define REQUEST		0xf0700000
#define LED_CMD		0x00000004
#define LED_ATTENTION	0x00000002
#define LED_SET_ON	0x00000001
#define LED_SET_OFF	0x00000002
#define LED_SET_BLINK	0x00000003

static u32 led_service_id[4];

static int led_status(u8 status)
{
	switch (status) {
	case 1: return LED_SET_ON;
	case 2: return LED_SET_BLINK;
	default: return LED_SET_OFF;
	}
}

static int set_attention_status(struct hotplug_slot *slot, u8 status)
{
	struct arm_smccc_res res;
	struct pci_bus *bus;
	struct pci_dev *root_port;
	unsigned long flags;
	u32 handle;
	int ret = 0;

	bus = slot->pci_slot->bus;
	root_port = pcie_find_root_port(bus->self);
	if (!root_port)
		return -ENODEV;

	local_irq_save(flags);
	arm_smccc_smc(HANDLE_OPEN, led_service_id[0], led_service_id[1],
		      led_service_id[2], led_service_id[3], 0, 0, 0, &res);
	if (res.a0) {
		ret = -ENODEV;
		goto out;
	}
	handle = res.a1 & 0xffff0000;

	arm_smccc_smc(REQUEST, LED_CMD, led_status(status), LED_ATTENTION,
		 (PCI_SLOT(root_port->devfn) << 4) | (pci_domain_nr(bus) & 0xf),
		 0, 0, handle, &res);
	if (res.a0)
		ret = -ENODEV;

	arm_smccc_smc(HANDLE_CLOSE, handle, 0, 0, 0, 0, 0, 0, &res);

 out:
	local_irq_restore(flags);
	return ret;
}

static int get_attention_status(struct hotplug_slot *slot, u8 *status)
{
	return -EINVAL;
}

static struct acpiphp_attention_info ampere_altra_attn = {
	.set_attn = set_attention_status,
	.get_attn = get_attention_status,
	.owner = THIS_MODULE,
};

static int altra_led_probe(struct platform_device *pdev)
{
	struct fwnode_handle *fwnode = dev_fwnode(&pdev->dev);
	int ret;

	ret = fwnode_property_read_u32_array(fwnode, "uuid", led_service_id, 4);
	if (ret) {
		dev_err(&pdev->dev, "can't find uuid\n");
		return ret;
	}

	ret = acpiphp_register_attention(&ampere_altra_attn);
	if (ret) {
		dev_err(&pdev->dev, "can't register driver\n");
		return ret;
	}
	return 0;
}

static void altra_led_remove(struct platform_device *pdev)
{
	acpiphp_unregister_attention(&ampere_altra_attn);
}

static const struct acpi_device_id altra_led_ids[] = {
	{ "AMPC0008", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, altra_led_ids);

static struct platform_driver altra_led_driver = {
	.driver = {
		.name = "ampere-altra-leds",
		.acpi_match_table = altra_led_ids,
	},
	.probe = altra_led_probe,
	.remove_new = altra_led_remove,
};
module_platform_driver(altra_led_driver);

MODULE_AUTHOR("D Scott Phillips <scott@os.amperecomputing.com>");
MODULE_LICENSE("GPL");

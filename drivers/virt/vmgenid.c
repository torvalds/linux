// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * The "Virtual Machine Generation ID" is exposed via ACPI or DT and changes when a
 * virtual machine forks or is cloned. This driver exists for shepherding that
 * information to random.c.
 */

#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>

ACPI_MODULE_NAME("vmgenid");

enum { VMGENID_SIZE = 16 };

struct vmgenid_state {
	u8 *next_id;
	u8 this_id[VMGENID_SIZE];
};

static void vmgenid_notify(struct device *device)
{
	struct vmgenid_state *state = device->driver_data;
	u8 old_id[VMGENID_SIZE];

	memcpy(old_id, state->this_id, sizeof(old_id));
	memcpy(state->this_id, state->next_id, sizeof(state->this_id));
	if (!memcmp(old_id, state->this_id, sizeof(old_id)))
		return;
	add_vmfork_randomness(state->this_id, sizeof(state->this_id));
}

static void setup_vmgenid_state(struct vmgenid_state *state, void *virt_addr)
{
	state->next_id = virt_addr;
	memcpy(state->this_id, state->next_id, sizeof(state->this_id));
	add_device_randomness(state->this_id, sizeof(state->this_id));
}

#ifdef CONFIG_ACPI
static void vmgenid_acpi_handler(acpi_handle __always_unused handle,
				 u32 __always_unused event, void *dev)
{
	vmgenid_notify(dev);
}

static int vmgenid_add_acpi(struct device *dev, struct vmgenid_state *state)
{
	struct acpi_device *device = ACPI_COMPANION(dev);
	struct acpi_buffer parsed = { ACPI_ALLOCATE_BUFFER };
	union acpi_object *obj;
	phys_addr_t phys_addr;
	acpi_status status;
	void *virt_addr;
	int ret = 0;

	status = acpi_evaluate_object(device->handle, "ADDR", NULL, &parsed);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating ADDR"));
		return -ENODEV;
	}
	obj = parsed.pointer;
	if (!obj || obj->type != ACPI_TYPE_PACKAGE || obj->package.count != 2 ||
	    obj->package.elements[0].type != ACPI_TYPE_INTEGER ||
	    obj->package.elements[1].type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out;
	}

	phys_addr = (obj->package.elements[0].integer.value << 0) |
		    (obj->package.elements[1].integer.value << 32);

	virt_addr = devm_memremap(&device->dev, phys_addr, VMGENID_SIZE, MEMREMAP_WB);
	if (IS_ERR(virt_addr)) {
		ret = PTR_ERR(virt_addr);
		goto out;
	}
	setup_vmgenid_state(state, virt_addr);

	status = acpi_install_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
					     vmgenid_acpi_handler, dev);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto out;
	}

	dev->driver_data = state;
out:
	ACPI_FREE(parsed.pointer);
	return ret;
}
#else
static int vmgenid_add_acpi(struct device *dev, struct vmgenid_state *state)
{
	return -EINVAL;
}
#endif

static irqreturn_t vmgenid_of_irq_handler(int __always_unused irq, void *dev)
{
	vmgenid_notify(dev);
	return IRQ_HANDLED;
}

static int vmgenid_add_of(struct platform_device *pdev,
			  struct vmgenid_state *state)
{
	void *virt_addr;
	int ret;

	virt_addr = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(virt_addr))
		return PTR_ERR(virt_addr);

	setup_vmgenid_state(state, virt_addr);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(&pdev->dev, ret, vmgenid_of_irq_handler,
			       IRQF_SHARED, "vmgenid", &pdev->dev);
	if (ret < 0)
		return ret;

	pdev->dev.driver_data = state;
	return 0;
}

static int vmgenid_add(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vmgenid_state *state;
	int ret;

	state = devm_kmalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	if (dev->of_node)
		ret = vmgenid_add_of(pdev, state);
	else
		ret = vmgenid_add_acpi(dev, state);

	if (ret < 0)
		devm_kfree(dev, state);
	return ret;
}

static const struct of_device_id vmgenid_of_ids[] = {
	{ .compatible = "microsoft,vmgenid", },
	{ },
};
MODULE_DEVICE_TABLE(of, vmgenid_of_ids);

static const struct acpi_device_id vmgenid_acpi_ids[] = {
	{ "VMGENCTR", 0 },
	{ "VM_GEN_COUNTER", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, vmgenid_acpi_ids);

static struct platform_driver vmgenid_plaform_driver = {
	.probe      = vmgenid_add,
	.driver     = {
		.name   = "vmgenid",
		.acpi_match_table = vmgenid_acpi_ids,
		.of_match_table = vmgenid_of_ids,
	},
};

module_platform_driver(vmgenid_plaform_driver)

MODULE_DESCRIPTION("Virtual Machine Generation ID");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");

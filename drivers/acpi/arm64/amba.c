// SPDX-License-Identifier: GPL-2.0-only

/*
 * ACPI support for platform bus type.
 *
 * Copyright (C) 2015, Linaro Ltd
 * Author: Graeme Gregory <graeme.gregory@linaro.org>
 */

#include <linux/acpi.h>
#include <linux/amba/bus.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "init.h"

static const struct acpi_device_id amba_id_list[] = {
	{"ARMH0061", 0}, /* PL061 GPIO Device */
	{"ARMH0330", 0}, /* ARM DMA Controller DMA-330 */
	{"", 0},
};

static void amba_register_dummy_clk(void)
{
	static struct clk *amba_dummy_clk;

	/* If clock already registered */
	if (amba_dummy_clk)
		return;

	amba_dummy_clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, 0, 0);
	clk_register_clkdev(amba_dummy_clk, "apb_pclk", NULL);
}

static int amba_handler_attach(struct acpi_device *adev,
				const struct acpi_device_id *id)
{
	struct acpi_device *parent = acpi_dev_parent(adev);
	struct amba_device *dev;
	struct resource_entry *rentry;
	struct list_head resource_list;
	bool address_found = false;
	int irq_no = 0;
	int ret;

	/* If the ACPI node already has a physical device attached, skip it. */
	if (adev->physical_node_count)
		return 0;

	dev = amba_device_alloc(dev_name(&adev->dev), 0, 0);
	if (!dev) {
		dev_err(&adev->dev, "%s(): amba_device_alloc() failed\n",
			__func__);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, NULL, NULL);
	if (ret < 0)
		goto err_free;

	list_for_each_entry(rentry, &resource_list, node) {
		switch (resource_type(rentry->res)) {
		case IORESOURCE_MEM:
			if (!address_found) {
				dev->res = *rentry->res;
				dev->res.name = dev_name(&dev->dev);
				address_found = true;
			}
			break;
		case IORESOURCE_IRQ:
			if (irq_no < AMBA_NR_IRQS)
				dev->irq[irq_no++] = rentry->res->start;
			break;
		default:
			dev_warn(&adev->dev, "Invalid resource\n");
			break;
		}
	}

	acpi_dev_free_resource_list(&resource_list);

	/*
	 * If the ACPI node has a parent and that parent has a physical device
	 * attached to it, that physical device should be the parent of
	 * the amba device we are about to create.
	 */
	if (parent)
		dev->dev.parent = acpi_get_first_physical_node(parent);

	device_set_node(&dev->dev, acpi_fwnode_handle(adev));

	ret = amba_device_add(dev, &iomem_resource);
	if (ret) {
		dev_err(&adev->dev, "%s(): amba_device_add() failed (%d)\n",
		       __func__, ret);
		goto err_free;
	}

	return 1;

err_free:
	amba_device_put(dev);
	return ret;
}

static struct acpi_scan_handler amba_handler = {
	.ids = amba_id_list,
	.attach = amba_handler_attach,
};

void __init acpi_amba_init(void)
{
	amba_register_dummy_clk();
	acpi_scan_add_handler(&amba_handler);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic Event Device for ACPI.
 *
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 *
 * Generic Event Device allows platforms to handle interrupts in ACPI
 * ASL statements. It follows very similar to  _EVT method approach
 * from GPIO events. All interrupts are listed in _CRS and the handler
 * is written in _EVT method. Here is an example.
 *
 * Device (GED0)
 * {
 *
 *     Name (_HID, "ACPI0013")
 *     Name (_UID, 0)
 *     Method (_CRS, 0x0, Serialized)
 *     {
 *		Name (RBUF, ResourceTemplate ()
 *		{
 *		Interrupt(ResourceConsumer, Edge, ActiveHigh, Shared, , , )
 *		{123}
 *		}
 *     })
 *
 *     Method (_EVT, 1) {
 *             if (Lequal(123, Arg0))
 *             {
 *             }
 *     }
 * }
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>

#define MODULE_NAME	"acpi-ged"

struct acpi_ged_device {
	struct device *dev;
	struct list_head event_list;
};

struct acpi_ged_event {
	struct list_head node;
	struct device *dev;
	unsigned int gsi;
	unsigned int irq;
	acpi_handle handle;
};

static irqreturn_t acpi_ged_irq_handler(int irq, void *data)
{
	struct acpi_ged_event *event = data;
	acpi_status acpi_ret;

	acpi_ret = acpi_execute_simple_method(event->handle, NULL, event->gsi);
	if (ACPI_FAILURE(acpi_ret))
		dev_err_once(event->dev, "IRQ method execution failed\n");

	return IRQ_HANDLED;
}

static acpi_status acpi_ged_request_interrupt(struct acpi_resource *ares,
					      void *context)
{
	struct acpi_ged_event *event;
	unsigned int irq;
	unsigned int gsi;
	unsigned int irqflags = IRQF_ONESHOT;
	struct acpi_ged_device *geddev = context;
	struct device *dev = geddev->dev;
	acpi_handle handle = ACPI_HANDLE(dev);
	acpi_handle evt_handle;
	struct resource r;
	struct acpi_resource_irq *p = &ares->data.irq;
	struct acpi_resource_extended_irq *pext = &ares->data.extended_irq;
	char ev_name[5];
	u8 trigger;

	if (ares->type == ACPI_RESOURCE_TYPE_END_TAG)
		return AE_OK;

	if (!acpi_dev_resource_interrupt(ares, 0, &r)) {
		dev_err(dev, "unable to parse IRQ resource\n");
		return AE_ERROR;
	}
	if (ares->type == ACPI_RESOURCE_TYPE_IRQ) {
		gsi = p->interrupts[0];
		trigger = p->triggering;
	} else {
		gsi = pext->interrupts[0];
		trigger = pext->triggering;
	}

	irq = r.start;

	switch (gsi) {
	case 0 ... 255:
		sprintf(ev_name, "_%c%02X",
			trigger == ACPI_EDGE_SENSITIVE ? 'E' : 'L', gsi);

		if (ACPI_SUCCESS(acpi_get_handle(handle, ev_name, &evt_handle)))
			break;
		fallthrough;
	default:
		if (ACPI_SUCCESS(acpi_get_handle(handle, "_EVT", &evt_handle)))
			break;

		dev_err(dev, "cannot locate _EVT method\n");
		return AE_ERROR;
	}

	event = devm_kzalloc(dev, sizeof(*event), GFP_KERNEL);
	if (!event)
		return AE_ERROR;

	event->gsi = gsi;
	event->dev = dev;
	event->irq = irq;
	event->handle = evt_handle;

	if (r.flags & IORESOURCE_IRQ_SHAREABLE)
		irqflags |= IRQF_SHARED;

	if (request_threaded_irq(irq, NULL, acpi_ged_irq_handler,
				 irqflags, "ACPI:Ged", event)) {
		dev_err(dev, "failed to setup event handler for irq %u\n", irq);
		return AE_ERROR;
	}

	dev_dbg(dev, "GED listening GSI %u @ IRQ %u\n", gsi, irq);
	list_add_tail(&event->node, &geddev->event_list);
	return AE_OK;
}

static int ged_probe(struct platform_device *pdev)
{
	struct acpi_ged_device *geddev;
	acpi_status acpi_ret;

	geddev = devm_kzalloc(&pdev->dev, sizeof(*geddev), GFP_KERNEL);
	if (!geddev)
		return -ENOMEM;

	geddev->dev = &pdev->dev;
	INIT_LIST_HEAD(&geddev->event_list);
	acpi_ret = acpi_walk_resources(ACPI_HANDLE(&pdev->dev), "_CRS",
				       acpi_ged_request_interrupt, geddev);
	if (ACPI_FAILURE(acpi_ret)) {
		dev_err(&pdev->dev, "unable to parse the _CRS record\n");
		return -EINVAL;
	}
	platform_set_drvdata(pdev, geddev);

	return 0;
}

static void ged_shutdown(struct platform_device *pdev)
{
	struct acpi_ged_device *geddev = platform_get_drvdata(pdev);
	struct acpi_ged_event *event, *next;

	list_for_each_entry_safe(event, next, &geddev->event_list, node) {
		free_irq(event->irq, event);
		list_del(&event->node);
		dev_dbg(geddev->dev, "GED releasing GSI %u @ IRQ %u\n",
			 event->gsi, event->irq);
	}
}

static void ged_remove(struct platform_device *pdev)
{
	ged_shutdown(pdev);
}

static const struct acpi_device_id ged_acpi_ids[] = {
	{"ACPI0013"},
	{},
};

static struct platform_driver ged_driver = {
	.probe = ged_probe,
	.remove_new = ged_remove,
	.shutdown = ged_shutdown,
	.driver = {
		.name = MODULE_NAME,
		.acpi_match_table = ACPI_PTR(ged_acpi_ids),
	},
};
builtin_platform_driver(ged_driver);

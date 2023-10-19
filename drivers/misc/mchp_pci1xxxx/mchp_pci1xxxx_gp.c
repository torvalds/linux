// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Microchip Technology Inc.

#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/idr.h>
#include "mchp_pci1xxxx_gp.h"

struct aux_bus_device {
	struct auxiliary_device_wrapper *aux_device_wrapper[2];
};

static DEFINE_IDA(gp_client_ida);
static const char aux_dev_otp_e2p_name[15] = "gp_otp_e2p";
static const char aux_dev_gpio_name[15] = "gp_gpio";

static void gp_auxiliary_device_release(struct device *dev)
{
	struct auxiliary_device_wrapper *aux_device_wrapper =
		(struct auxiliary_device_wrapper *)container_of(dev,
				struct auxiliary_device_wrapper, aux_dev.dev);

	ida_free(&gp_client_ida, aux_device_wrapper->aux_dev.id);
	kfree(aux_device_wrapper);
}

static int gp_aux_bus_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct aux_bus_device *aux_bus;
	int retval;

	retval = pcim_enable_device(pdev);
	if (retval)
		return retval;

	aux_bus = devm_kzalloc(&pdev->dev, sizeof(*aux_bus), GFP_KERNEL);
	if (!aux_bus)
		return -ENOMEM;

	aux_bus->aux_device_wrapper[0] = kzalloc(sizeof(*aux_bus->aux_device_wrapper[0]),
						 GFP_KERNEL);
	if (!aux_bus->aux_device_wrapper[0])
		return -ENOMEM;

	retval = ida_alloc(&gp_client_ida, GFP_KERNEL);
	if (retval < 0)
		goto err_ida_alloc_0;

	aux_bus->aux_device_wrapper[0]->aux_dev.name = aux_dev_otp_e2p_name;
	aux_bus->aux_device_wrapper[0]->aux_dev.dev.parent = &pdev->dev;
	aux_bus->aux_device_wrapper[0]->aux_dev.dev.release = gp_auxiliary_device_release;
	aux_bus->aux_device_wrapper[0]->aux_dev.id = retval;

	aux_bus->aux_device_wrapper[0]->gp_aux_data.region_start = pci_resource_start(pdev, 0);
	aux_bus->aux_device_wrapper[0]->gp_aux_data.region_length = pci_resource_end(pdev, 0);

	retval = auxiliary_device_init(&aux_bus->aux_device_wrapper[0]->aux_dev);
	if (retval < 0)
		goto err_aux_dev_init_0;

	retval = auxiliary_device_add(&aux_bus->aux_device_wrapper[0]->aux_dev);
	if (retval)
		goto err_aux_dev_add_0;

	aux_bus->aux_device_wrapper[1] = kzalloc(sizeof(*aux_bus->aux_device_wrapper[1]),
						 GFP_KERNEL);
	if (!aux_bus->aux_device_wrapper[1])
		return -ENOMEM;

	retval = ida_alloc(&gp_client_ida, GFP_KERNEL);
	if (retval < 0)
		goto err_ida_alloc_1;

	aux_bus->aux_device_wrapper[1]->aux_dev.name = aux_dev_gpio_name;
	aux_bus->aux_device_wrapper[1]->aux_dev.dev.parent = &pdev->dev;
	aux_bus->aux_device_wrapper[1]->aux_dev.dev.release = gp_auxiliary_device_release;
	aux_bus->aux_device_wrapper[1]->aux_dev.id = retval;

	aux_bus->aux_device_wrapper[1]->gp_aux_data.region_start = pci_resource_start(pdev, 0);
	aux_bus->aux_device_wrapper[1]->gp_aux_data.region_length = pci_resource_end(pdev, 0);

	retval = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);

	if (retval < 0)
		goto err_aux_dev_init_1;

	retval = pci_irq_vector(pdev, 0);
	if (retval < 0)
		goto err_aux_dev_init_1;

	pdev->irq = retval;
	aux_bus->aux_device_wrapper[1]->gp_aux_data.irq_num = pdev->irq;

	retval = auxiliary_device_init(&aux_bus->aux_device_wrapper[1]->aux_dev);
	if (retval < 0)
		goto err_aux_dev_init_1;

	retval = auxiliary_device_add(&aux_bus->aux_device_wrapper[1]->aux_dev);
	if (retval)
		goto err_aux_dev_add_1;

	pci_set_drvdata(pdev, aux_bus);
	pci_set_master(pdev);

	return 0;

err_aux_dev_add_1:
	auxiliary_device_uninit(&aux_bus->aux_device_wrapper[1]->aux_dev);

err_aux_dev_init_1:
	ida_free(&gp_client_ida, aux_bus->aux_device_wrapper[1]->aux_dev.id);

err_ida_alloc_1:
	kfree(aux_bus->aux_device_wrapper[1]);

err_aux_dev_add_0:
	auxiliary_device_uninit(&aux_bus->aux_device_wrapper[0]->aux_dev);

err_aux_dev_init_0:
	ida_free(&gp_client_ida, aux_bus->aux_device_wrapper[0]->aux_dev.id);

err_ida_alloc_0:
	kfree(aux_bus->aux_device_wrapper[0]);

	return retval;
}

static void gp_aux_bus_remove(struct pci_dev *pdev)
{
	struct aux_bus_device *aux_bus = pci_get_drvdata(pdev);

	auxiliary_device_delete(&aux_bus->aux_device_wrapper[0]->aux_dev);
	auxiliary_device_uninit(&aux_bus->aux_device_wrapper[0]->aux_dev);
	auxiliary_device_delete(&aux_bus->aux_device_wrapper[1]->aux_dev);
	auxiliary_device_uninit(&aux_bus->aux_device_wrapper[1]->aux_dev);
}

static const struct pci_device_id pci1xxxx_tbl[] = {
	{ PCI_DEVICE(0x1055, 0xA005) },
	{ PCI_DEVICE(0x1055, 0xA015) },
	{ PCI_DEVICE(0x1055, 0xA025) },
	{ PCI_DEVICE(0x1055, 0xA035) },
	{ PCI_DEVICE(0x1055, 0xA045) },
	{ PCI_DEVICE(0x1055, 0xA055) },
	{0,}
};
MODULE_DEVICE_TABLE(pci, pci1xxxx_tbl);

static struct pci_driver pci1xxxx_gp_driver = {
	.name = "PCI1xxxxGP",
	.id_table = pci1xxxx_tbl,
	.probe = gp_aux_bus_probe,
	.remove = gp_aux_bus_remove,
};

module_pci_driver(pci1xxxx_gp_driver);

MODULE_DESCRIPTION("Microchip Technology Inc. PCI1xxxx GP expander");
MODULE_AUTHOR("Kumaravel Thiagarajan <kumaravel.thiagarajan@microchip.com>");
MODULE_LICENSE("GPL");

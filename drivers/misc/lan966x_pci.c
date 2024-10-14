// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip LAN966x PCI driver
 *
 * Copyright (c) 2024 Microchip Technology Inc. and its subsidiaries.
 *
 * Authors:
 *	Clément Léger <clement.leger@bootlin.com>
 *	Hervé Codina <herve.codina@bootlin.com>
 */

#include <linux/device.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>

/* Embedded dtbo symbols created by cmd_wrap_S_dtb in scripts/Makefile.lib */
extern char __dtbo_lan966x_pci_begin[];
extern char __dtbo_lan966x_pci_end[];

struct pci_dev_intr_ctrl {
	struct pci_dev *pci_dev;
	struct irq_domain *irq_domain;
	int irq;
};

static int pci_dev_irq_domain_map(struct irq_domain *d, unsigned int virq, irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &dummy_irq_chip, handle_simple_irq);
	return 0;
}

static const struct irq_domain_ops pci_dev_irq_domain_ops = {
	.map = pci_dev_irq_domain_map,
	.xlate = irq_domain_xlate_onecell,
};

static irqreturn_t pci_dev_irq_handler(int irq, void *data)
{
	struct pci_dev_intr_ctrl *intr_ctrl = data;
	int ret;

	ret = generic_handle_domain_irq(intr_ctrl->irq_domain, 0);
	return ret ? IRQ_NONE : IRQ_HANDLED;
}

static struct pci_dev_intr_ctrl *pci_dev_create_intr_ctrl(struct pci_dev *pdev)
{
	struct pci_dev_intr_ctrl *intr_ctrl __free(kfree) = NULL;
	struct fwnode_handle *fwnode;
	int ret;

	fwnode = dev_fwnode(&pdev->dev);
	if (!fwnode)
		return ERR_PTR(-ENODEV);

	intr_ctrl = kmalloc(sizeof(*intr_ctrl), GFP_KERNEL);
	if (!intr_ctrl)
		return ERR_PTR(-ENOMEM);

	intr_ctrl->pci_dev = pdev;

	intr_ctrl->irq_domain = irq_domain_create_linear(fwnode, 1, &pci_dev_irq_domain_ops,
							 intr_ctrl);
	if (!intr_ctrl->irq_domain) {
		pci_err(pdev, "Failed to create irqdomain\n");
		return ERR_PTR(-ENOMEM);
	}

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_INTX);
	if (ret < 0) {
		pci_err(pdev, "Unable alloc irq vector (%d)\n", ret);
		goto err_remove_domain;
	}
	intr_ctrl->irq = pci_irq_vector(pdev, 0);
	ret = request_irq(intr_ctrl->irq, pci_dev_irq_handler, IRQF_SHARED,
			  pci_name(pdev), intr_ctrl);
	if (ret) {
		pci_err(pdev, "Unable to request irq %d (%d)\n", intr_ctrl->irq, ret);
		goto err_free_irq_vector;
	}

	return_ptr(intr_ctrl);

err_free_irq_vector:
	pci_free_irq_vectors(pdev);
err_remove_domain:
	irq_domain_remove(intr_ctrl->irq_domain);
	return ERR_PTR(ret);
}

static void pci_dev_remove_intr_ctrl(struct pci_dev_intr_ctrl *intr_ctrl)
{
	free_irq(intr_ctrl->irq, intr_ctrl);
	pci_free_irq_vectors(intr_ctrl->pci_dev);
	irq_dispose_mapping(irq_find_mapping(intr_ctrl->irq_domain, 0));
	irq_domain_remove(intr_ctrl->irq_domain);
	kfree(intr_ctrl);
}

static void devm_pci_dev_remove_intr_ctrl(void *intr_ctrl)
{
	pci_dev_remove_intr_ctrl(intr_ctrl);
}

static int devm_pci_dev_create_intr_ctrl(struct pci_dev *pdev)
{
	struct pci_dev_intr_ctrl *intr_ctrl;

	intr_ctrl = pci_dev_create_intr_ctrl(pdev);
	if (IS_ERR(intr_ctrl))
		return PTR_ERR(intr_ctrl);

	return devm_add_action_or_reset(&pdev->dev, devm_pci_dev_remove_intr_ctrl, intr_ctrl);
}

struct lan966x_pci {
	struct device *dev;
	int ovcs_id;
};

static int lan966x_pci_load_overlay(struct lan966x_pci *data)
{
	u32 dtbo_size = __dtbo_lan966x_pci_end - __dtbo_lan966x_pci_begin;
	void *dtbo_start = __dtbo_lan966x_pci_begin;

	return of_overlay_fdt_apply(dtbo_start, dtbo_size, &data->ovcs_id, dev_of_node(data->dev));
}

static void lan966x_pci_unload_overlay(struct lan966x_pci *data)
{
	of_overlay_remove(&data->ovcs_id);
}

static int lan966x_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct lan966x_pci *data;
	int ret;

	/*
	 * On ACPI system, fwnode can point to the ACPI node.
	 * This driver needs an of_node to be used as the device-tree overlay
	 * target. This of_node should be set by the PCI core if it succeeds in
	 * creating it (CONFIG_PCI_DYNAMIC_OF_NODES feature).
	 * Check here for the validity of this of_node.
	 */
	if (!dev_of_node(dev))
		return dev_err_probe(dev, -EINVAL, "Missing of_node for device\n");

	/* Need to be done before devm_pci_dev_create_intr_ctrl.
	 * It allocates an IRQ and so pdev->irq is updated.
	 */
	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = devm_pci_dev_create_intr_ctrl(pdev);
	if (ret)
		return ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	pci_set_drvdata(pdev, data);
	data->dev = dev;

	ret = lan966x_pci_load_overlay(data);
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = of_platform_default_populate(dev_of_node(dev), NULL, dev);
	if (ret)
		goto err_unload_overlay;

	return 0;

err_unload_overlay:
	lan966x_pci_unload_overlay(data);
	return ret;
}

static void lan966x_pci_remove(struct pci_dev *pdev)
{
	struct lan966x_pci *data = pci_get_drvdata(pdev);

	of_platform_depopulate(data->dev);

	lan966x_pci_unload_overlay(data);
}

static struct pci_device_id lan966x_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_EFAR, 0x9660) },
	{ }
};
MODULE_DEVICE_TABLE(pci, lan966x_pci_ids);

static struct pci_driver lan966x_pci_driver = {
	.name = "mchp_lan966x_pci",
	.id_table = lan966x_pci_ids,
	.probe = lan966x_pci_probe,
	.remove = lan966x_pci_remove,
};
module_pci_driver(lan966x_pci_driver);

MODULE_AUTHOR("Herve Codina <herve.codina@bootlin.com>");
MODULE_DESCRIPTION("Microchip LAN966x PCI driver");
MODULE_LICENSE("GPL");

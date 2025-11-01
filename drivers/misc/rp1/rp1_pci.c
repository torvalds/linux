// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2025 Raspberry Pi Ltd.
 *
 * All rights reserved.
 */

#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#define RP1_HW_IRQ_MASK		GENMASK(5, 0)

#define REG_SET			0x800
#define REG_CLR			0xc00

/* MSI-X CFG registers start at 0x8 */
#define MSIX_CFG(x) (0x8 + (4 * (x)))

#define MSIX_CFG_IACK_EN        BIT(3)
#define MSIX_CFG_IACK           BIT(2)
#define MSIX_CFG_ENABLE         BIT(0)

/* Address map */
#define RP1_PCIE_APBS_BASE	0x108000

/* Interrupts */
#define RP1_INT_END		61

/* Embedded dtbo symbols created by cmd_wrap_S_dtb in scripts/Makefile.lib */
extern char __dtbo_rp1_pci_begin[];
extern char __dtbo_rp1_pci_end[];

struct rp1_dev {
	struct pci_dev *pdev;
	struct irq_domain *domain;
	struct irq_data *pcie_irqds[64];
	void __iomem *bar1;
	int ovcs_id;	/* overlay changeset id */
	bool level_triggered_irq[RP1_INT_END];
};

static void msix_cfg_set(struct rp1_dev *rp1, unsigned int hwirq, u32 value)
{
	iowrite32(value, rp1->bar1 + RP1_PCIE_APBS_BASE + REG_SET + MSIX_CFG(hwirq));
}

static void msix_cfg_clr(struct rp1_dev *rp1, unsigned int hwirq, u32 value)
{
	iowrite32(value, rp1->bar1 + RP1_PCIE_APBS_BASE + REG_CLR + MSIX_CFG(hwirq));
}

static void rp1_mask_irq(struct irq_data *irqd)
{
	struct rp1_dev *rp1 = irqd->domain->host_data;
	struct irq_data *pcie_irqd = rp1->pcie_irqds[irqd->hwirq];

	pci_msi_mask_irq(pcie_irqd);
}

static void rp1_unmask_irq(struct irq_data *irqd)
{
	struct rp1_dev *rp1 = irqd->domain->host_data;
	struct irq_data *pcie_irqd = rp1->pcie_irqds[irqd->hwirq];

	pci_msi_unmask_irq(pcie_irqd);
}

static int rp1_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct rp1_dev *rp1 = irqd->domain->host_data;
	unsigned int hwirq = (unsigned int)irqd->hwirq;

	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		dev_dbg(&rp1->pdev->dev, "MSIX IACK EN for IRQ %u\n", hwirq);
		msix_cfg_set(rp1, hwirq, MSIX_CFG_IACK_EN);
		rp1->level_triggered_irq[hwirq] = true;
	break;
	case IRQ_TYPE_EDGE_RISING:
		msix_cfg_clr(rp1, hwirq, MSIX_CFG_IACK_EN);
		rp1->level_triggered_irq[hwirq] = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip rp1_irq_chip = {
	.name		= "rp1_irq_chip",
	.irq_mask	= rp1_mask_irq,
	.irq_unmask	= rp1_unmask_irq,
	.irq_set_type	= rp1_irq_set_type,
};

static void rp1_chained_handle_irq(struct irq_desc *desc)
{
	unsigned int hwirq = desc->irq_data.hwirq & RP1_HW_IRQ_MASK;
	struct rp1_dev *rp1 = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int virq;

	chained_irq_enter(chip, desc);

	virq = irq_find_mapping(rp1->domain, hwirq);
	generic_handle_irq(virq);
	if (rp1->level_triggered_irq[hwirq])
		msix_cfg_set(rp1, hwirq, MSIX_CFG_IACK);

	chained_irq_exit(chip, desc);
}

static int rp1_irq_xlate(struct irq_domain *d, struct device_node *node,
			 const u32 *intspec, unsigned int intsize,
			 unsigned long *out_hwirq, unsigned int *out_type)
{
	struct rp1_dev *rp1 = d->host_data;
	struct irq_data *pcie_irqd;
	unsigned long hwirq;
	int pcie_irq;
	int ret;

	ret = irq_domain_xlate_twocell(d, node, intspec, intsize,
				       &hwirq, out_type);
	if (ret)
		return ret;

	pcie_irq = pci_irq_vector(rp1->pdev, hwirq);
	pcie_irqd = irq_get_irq_data(pcie_irq);
	rp1->pcie_irqds[hwirq] = pcie_irqd;
	*out_hwirq = hwirq;

	return 0;
}

static int rp1_irq_activate(struct irq_domain *d, struct irq_data *irqd,
			    bool reserve)
{
	struct rp1_dev *rp1 = d->host_data;

	msix_cfg_set(rp1, (unsigned int)irqd->hwirq, MSIX_CFG_ENABLE);

	return 0;
}

static void rp1_irq_deactivate(struct irq_domain *d, struct irq_data *irqd)
{
	struct rp1_dev *rp1 = d->host_data;

	msix_cfg_clr(rp1, (unsigned int)irqd->hwirq, MSIX_CFG_ENABLE);
}

static const struct irq_domain_ops rp1_domain_ops = {
	.xlate      = rp1_irq_xlate,
	.activate   = rp1_irq_activate,
	.deactivate = rp1_irq_deactivate,
};

static void rp1_unregister_interrupts(struct pci_dev *pdev)
{
	struct rp1_dev *rp1 = pci_get_drvdata(pdev);
	int irq, i;

	if (rp1->domain) {
		for (i = 0; i < RP1_INT_END; i++) {
			irq = irq_find_mapping(rp1->domain, i);
			irq_dispose_mapping(irq);
		}

		irq_domain_remove(rp1->domain);
	}

	pci_free_irq_vectors(pdev);
}

static int rp1_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	u32 dtbo_size = __dtbo_rp1_pci_end - __dtbo_rp1_pci_begin;
	void *dtbo_start = __dtbo_rp1_pci_begin;
	struct device *dev = &pdev->dev;
	struct device_node *rp1_node;
	bool skip_ovl = true;
	struct rp1_dev *rp1;
	int err = 0;
	int i;

	/*
	 * Either use rp1_nexus node if already present in DT, or
	 * set a flag to load it from overlay at runtime
	 */
	rp1_node = of_find_node_by_name(NULL, "rp1_nexus");
	if (!rp1_node) {
		rp1_node = dev_of_node(dev);
		skip_ovl = false;
	}

	if (!rp1_node) {
		dev_err(dev, "Missing of_node for device\n");
		err = -EINVAL;
		goto err_put_node;
	}

	rp1 = devm_kzalloc(&pdev->dev, sizeof(*rp1), GFP_KERNEL);
	if (!rp1) {
		err = -ENOMEM;
		goto err_put_node;
	}

	rp1->pdev = pdev;

	if (pci_resource_len(pdev, 1) <= 0x10000) {
		dev_err(&pdev->dev,
			"Not initialized - is the firmware running?\n");
		err = -EINVAL;
		goto err_put_node;
	}

	err = pcim_enable_device(pdev);
	if (err < 0) {
		err = dev_err_probe(&pdev->dev, err,
				    "Enabling PCI device has failed");
		goto err_put_node;
	}

	rp1->bar1 = pcim_iomap(pdev, 1, 0);
	if (!rp1->bar1) {
		dev_err(&pdev->dev, "Cannot map PCI BAR\n");
		err = -EIO;
		goto err_put_node;
	}

	pci_set_master(pdev);

	err = pci_alloc_irq_vectors(pdev, RP1_INT_END, RP1_INT_END,
				    PCI_IRQ_MSIX);
	if (err < 0) {
		err = dev_err_probe(&pdev->dev, err,
				    "Failed to allocate MSI-X vectors\n");
		goto err_put_node;
	} else if (err != RP1_INT_END) {
		dev_err(&pdev->dev, "Cannot allocate enough interrupts\n");
		err = -EINVAL;
		goto err_put_node;
	}

	pci_set_drvdata(pdev, rp1);
	rp1->domain = irq_domain_add_linear(rp1_node, RP1_INT_END,
					    &rp1_domain_ops, rp1);
	if (!rp1->domain) {
		dev_err(&pdev->dev, "Error creating IRQ domain\n");
		err = -ENOMEM;
		goto err_unregister_interrupts;
	}

	for (i = 0; i < RP1_INT_END; i++) {
		unsigned int irq = irq_create_mapping(rp1->domain, i);

		if (!irq) {
			dev_err(&pdev->dev, "Failed to create IRQ mapping\n");
			err = -EINVAL;
			goto err_unregister_interrupts;
		}

		irq_set_chip_and_handler(irq, &rp1_irq_chip, handle_level_irq);
		irq_set_probe(irq);
		irq_set_chained_handler_and_data(pci_irq_vector(pdev, i),
						 rp1_chained_handle_irq, rp1);
	}

	if (!skip_ovl) {
		err = of_overlay_fdt_apply(dtbo_start, dtbo_size, &rp1->ovcs_id,
					   rp1_node);
		if (err)
			goto err_unregister_interrupts;
	}

	err = of_platform_default_populate(rp1_node, NULL, dev);
	if (err) {
		dev_err_probe(&pdev->dev, err, "Error populating devicetree\n");
		goto err_unload_overlay;
	}

	return 0;

err_unload_overlay:
	of_overlay_remove(&rp1->ovcs_id);
err_unregister_interrupts:
	rp1_unregister_interrupts(pdev);
err_put_node:
	if (skip_ovl)
		of_node_put(rp1_node);

	return err;
}

static void rp1_remove(struct pci_dev *pdev)
{
	struct rp1_dev *rp1 = pci_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	of_platform_depopulate(dev);
	of_overlay_remove(&rp1->ovcs_id);
	rp1_unregister_interrupts(pdev);
}

static const struct pci_device_id dev_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_RPI, PCI_DEVICE_ID_RPI_RP1_C0), },
	{ }
};
MODULE_DEVICE_TABLE(pci, dev_id_table);

static struct pci_driver rp1_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= dev_id_table,
	.probe		= rp1_probe,
	.remove		= rp1_remove,
};

module_pci_driver(rp1_driver);

MODULE_AUTHOR("Phil Elwell <phil@raspberrypi.com>");
MODULE_AUTHOR("Andrea della Porta <andrea.porta@suse.com>");
MODULE_DESCRIPTION("RaspberryPi RP1 misc device");
MODULE_LICENSE("GPL");

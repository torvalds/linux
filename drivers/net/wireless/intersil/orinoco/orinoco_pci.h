/* orinoco_pci.h
 *
 * Common code for all Orinoco drivers for PCI devices, including
 * both native PCI and PCMCIA-to-PCI bridges.
 *
 * Copyright (C) 2005, Pavel Roskin.
 * See main.c for license.
 */

#ifndef _ORINOCO_PCI_H
#define _ORINOCO_PCI_H

#include <linux/netdevice.h>

/* Driver specific data */
struct orinoco_pci_card {
	void __iomem *bridge_io;
	void __iomem *attr_io;
};

static int __maybe_unused orinoco_pci_suspend(struct device *dev_d)
{
	struct pci_dev *pdev = to_pci_dev(dev_d);
	struct orinoco_private *priv = pci_get_drvdata(pdev);

	orinoco_down(priv);
	free_irq(pdev->irq, priv);

	return 0;
}

static int __maybe_unused orinoco_pci_resume(struct device *dev_d)
{
	struct pci_dev *pdev = to_pci_dev(dev_d);
	struct orinoco_private *priv = pci_get_drvdata(pdev);
	struct net_device *dev = priv->ndev;
	int err;

	err = request_irq(pdev->irq, orinoco_interrupt, IRQF_SHARED,
			  dev->name, priv);
	if (err) {
		printk(KERN_ERR "%s: cannot re-allocate IRQ on resume\n",
		       dev->name);
		return -EBUSY;
	}

	return orinoco_up(priv);
}

static SIMPLE_DEV_PM_OPS(orinoco_pci_pm_ops,
			 orinoco_pci_suspend,
			 orinoco_pci_resume);

#endif /* _ORINOCO_PCI_H */

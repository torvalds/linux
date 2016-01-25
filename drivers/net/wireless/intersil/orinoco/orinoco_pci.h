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

#ifdef CONFIG_PM
static int orinoco_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct orinoco_private *priv = pci_get_drvdata(pdev);

	orinoco_down(priv);
	free_irq(pdev->irq, priv);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int orinoco_pci_resume(struct pci_dev *pdev)
{
	struct orinoco_private *priv = pci_get_drvdata(pdev);
	struct net_device *dev = priv->ndev;
	int err;

	pci_set_power_state(pdev, PCI_D0);
	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s: pci_enable_device failed on resume\n",
		       dev->name);
		return err;
	}
	pci_restore_state(pdev);

	err = request_irq(pdev->irq, orinoco_interrupt, IRQF_SHARED,
			  dev->name, priv);
	if (err) {
		printk(KERN_ERR "%s: cannot re-allocate IRQ on resume\n",
		       dev->name);
		pci_disable_device(pdev);
		return -EBUSY;
	}

	err = orinoco_up(priv);

	return err;
}
#else
#define orinoco_pci_suspend NULL
#define orinoco_pci_resume NULL
#endif

#endif /* _ORINOCO_PCI_H */

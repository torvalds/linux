/* orinoco_pci.h
 * 
 * Common code for all Orinoco drivers for PCI devices, including
 * both native PCI and PCMCIA-to-PCI bridges.
 *
 * Copyright (C) 2005, Pavel Roskin.
 * See orinoco.c for license.
 */

#ifndef _ORINOCO_PCI_H
#define _ORINOCO_PCI_H

#include <linux/netdevice.h>

/* Driver specific data */
struct orinoco_pci_card {
	void __iomem *bridge_io;
	void __iomem *attr_io;
};

/* Set base address or memory range of the network device based on
 * the PCI device it's using.  Specify BAR of the "main" resource.
 * To be used after request_irq().  */
static inline void orinoco_pci_setup_netdev(struct net_device *dev,
					    struct pci_dev *pdev, int bar)
{
	char *range_type;
	unsigned long start = pci_resource_start(pdev, bar);
	unsigned long len = pci_resource_len(pdev, bar);
	unsigned long flags = pci_resource_flags(pdev, bar);
	unsigned long end = start + len - 1;

	dev->irq = pdev->irq;
	if (flags & IORESOURCE_IO) {
		dev->base_addr = start;
		range_type = "ports";
	} else {
		dev->mem_start = start;
		dev->mem_end = end;
		range_type = "memory";
	}

	printk(KERN_DEBUG PFX "%s: irq %d, %s 0x%lx-0x%lx\n",
	       pci_name(pdev), pdev->irq, range_type, start, end);
}

static int orinoco_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err;

	err = orinoco_lock(priv, &flags);
	if (err) {
		printk(KERN_ERR "%s: cannot lock hardware for suspend\n",
		       dev->name);
		return err;
	}

	err = __orinoco_down(dev);
	if (err)
		printk(KERN_WARNING "%s: error %d bringing interface down "
		       "for suspend\n", dev->name, err);
	
	netif_device_detach(dev);

	priv->hw_unavailable++;
	
	orinoco_unlock(priv, &flags);

	free_irq(pdev->irq, dev);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int orinoco_pci_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct orinoco_private *priv = netdev_priv(dev);
	unsigned long flags;
	int err;

	pci_set_power_state(pdev, 0);
	pci_enable_device(pdev);
	pci_restore_state(pdev);

	err = request_irq(pdev->irq, orinoco_interrupt, SA_SHIRQ,
			  dev->name, dev);
	if (err) {
		printk(KERN_ERR "%s: cannot re-allocate IRQ on resume\n",
		       dev->name);
		pci_disable_device(pdev);
		return -EBUSY;
	}

	err = orinoco_reinit_firmware(dev);
	if (err) {
		printk(KERN_ERR "%s: error %d re-initializing firmware "
		       "on resume\n", dev->name, err);
		return err;
	}

	spin_lock_irqsave(&priv->lock, flags);

	netif_device_attach(dev);

	priv->hw_unavailable--;

	if (priv->open && (! priv->hw_unavailable)) {
		err = __orinoco_up(dev);
		if (err)
			printk(KERN_ERR "%s: Error %d restarting card on resume\n",
			       dev->name, err);
	}
	
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

#endif /* _ORINOCO_PCI_H */

/*
 * Toshiba rbtx4938 pci routines
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * 2003-2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for TX4938 in 2.6 - Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/tx4938/rbtx4938.h>

extern struct pci_controller tx4938_pci_controller[];

static int pci_get_irq(const struct pci_dev *dev, int pin)
{
	int irq = pin;
	u8 slot = PCI_SLOT(dev->devfn);
	struct pci_controller *controller = (struct pci_controller *)dev->sysdata;

	if (controller == &tx4938_pci_controller[1]) {
		/* TX4938 PCIC1 */
		switch (slot) {
		case TX4938_PCIC_IDSEL_AD_TO_SLOT(31):
			if (tx4938_ccfgptr->pcfg & TX4938_PCFG_ETH0_SEL)
				return RBTX4938_IRQ_IRC + TX4938_IR_ETH0;
			break;
		case TX4938_PCIC_IDSEL_AD_TO_SLOT(30):
			if (tx4938_ccfgptr->pcfg & TX4938_PCFG_ETH1_SEL)
				return RBTX4938_IRQ_IRC + TX4938_IR_ETH1;
			break;
		}
		return 0;
	}

	/* IRQ rotation */
	irq--;	/* 0-3 */
	if (dev->bus->parent == NULL &&
	    (slot == TX4938_PCIC_IDSEL_AD_TO_SLOT(23))) {
		/* PCI CardSlot (IDSEL=A23) */
		/* PCIA => PCIA (IDSEL=A23) */
		irq = (irq + 0 + slot) % 4;
	} else {
		/* PCI Backplane */
		irq = (irq + 33 - slot) % 4;
	}
	irq++;	/* 1-4 */

	switch (irq) {
	case 1:
		irq = RBTX4938_IRQ_IOC_PCIA;
		break;
	case 2:
		irq = RBTX4938_IRQ_IOC_PCIB;
		break;
	case 3:
		irq = RBTX4938_IRQ_IOC_PCIC;
		break;
	case 4:
		irq = RBTX4938_IRQ_IOC_PCID;
		break;
	}
	return irq;
}

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	unsigned char irq = 0;

	irq = pci_get_irq(dev, pin);

	printk(KERN_INFO "PCI: 0x%02x:0x%02x(0x%02x,0x%02x) IRQ=%d\n",
	       dev->bus->number, dev->devfn, PCI_SLOT(dev->devfn),
	       PCI_FUNC(dev->devfn), irq);

	return irq;
}

/*
 * Do platform specific device initialization at pci_enable_device() time
 */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}


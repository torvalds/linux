/* pci-irq.c: PCI IRQ routing on the FRV motherboard
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * derived from: arch/i386/kernel/pci-irq.c: (c) 1999--2000 Martin Mares <mj@suse.cz>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq-routing.h>

#include "pci-frv.h"

/*
 *	DEVICE	DEVNO	INT#A	INT#B	INT#C	INT#D
 *	=======	=======	=======	=======	=======	=======
 *	MB86943	0	fpga.10	-	-	-
 *	RTL8029	16	fpga.12	-	-	-
 *	SLOT 1	19	fpga.6	fpga.5	fpga.4	fpga.3
 *	SLOT 2	18	fpga.5	fpga.4	fpga.3	fpga.6
 *	SLOT 3	17	fpga.4	fpga.3	fpga.6	fpga.5
 *
 */

static const uint8_t __initdata pci_bus0_irq_routing[32][4] = {
	[0 ] {	IRQ_FPGA_MB86943_PCI_INTA },
	[16] {	IRQ_FPGA_RTL8029_INTA },
	[17] {	IRQ_FPGA_PCI_INTC, IRQ_FPGA_PCI_INTD, IRQ_FPGA_PCI_INTA, IRQ_FPGA_PCI_INTB },
	[18] {	IRQ_FPGA_PCI_INTB, IRQ_FPGA_PCI_INTC, IRQ_FPGA_PCI_INTD, IRQ_FPGA_PCI_INTA },
	[19] {	IRQ_FPGA_PCI_INTA, IRQ_FPGA_PCI_INTB, IRQ_FPGA_PCI_INTC, IRQ_FPGA_PCI_INTD },
};

void __init pcibios_irq_init(void)
{
}

void __init pcibios_fixup_irqs(void)
{
	struct pci_dev *dev = NULL;
	uint8_t line, pin;

	while (dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev),
	       dev != NULL
	       ) {
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
		if (pin) {
			dev->irq = pci_bus0_irq_routing[PCI_SLOT(dev->devfn)][pin - 1];
			pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
		}
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &line);
	}
}

void __init pcibios_penalize_isa_irq(int irq, int active)
{
}

void pcibios_enable_irq(struct pci_dev *dev)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
}

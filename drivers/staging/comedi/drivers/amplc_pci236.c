/*
 * comedi/drivers/amplc_pci236.c
 * Driver for Amplicon PCI236 DIO boards.
 *
 * Copyright (C) 2002-2014 MEV Ltd. <http://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
/*
 * Driver: amplc_pci236
 * Description: Amplicon PCI236
 * Author: Ian Abbott <abbotti@mev.co.uk>
 * Devices: [Amplicon] PCI236 (amplc_pci236)
 * Updated: Fri, 25 Jul 2014 15:32:40 +0000
 * Status: works
 *
 * Configuration options:
 *   none
 *
 * Manual configuration of PCI board (PCI236) is not supported; it is
 * configured automatically.
 *
 * The PCI236 board has a single 8255 appearing as subdevice 0.
 *
 * Subdevice 1 pretends to be a digital input device, but it always
 * returns 0 when read. However, if you run a command with
 * scan_begin_src=TRIG_EXT, a rising edge on port C bit 3 acts as an
 * external trigger, which can be used to wake up tasks.  This is like
 * the comedi_parport device.  If no interrupt is connected, then
 * subdevice 1 is unused.
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#include "amplc_pc236.h"
#include "plx9052.h"

/* Disable, and clear, interrupts */
#define PCI236_INTR_DISABLE	(PLX9052_INTCSR_LI1POL |	\
				 PLX9052_INTCSR_LI2POL |	\
				 PLX9052_INTCSR_LI1SEL |	\
				 PLX9052_INTCSR_LI1CLRINT)

/* Enable, and clear, interrupts */
#define PCI236_INTR_ENABLE	(PLX9052_INTCSR_LI1ENAB |	\
				 PLX9052_INTCSR_LI1POL |	\
				 PLX9052_INTCSR_LI2POL |	\
				 PLX9052_INTCSR_PCIENAB |	\
				 PLX9052_INTCSR_LI1SEL |	\
				 PLX9052_INTCSR_LI1CLRINT)

static void pci236_intr_update_cb(struct comedi_device *dev, bool enable)
{
	struct pc236_private *devpriv = dev->private;

	/* this will also clear the "local interrupt 1" latch */
	outl(enable ? PCI236_INTR_ENABLE : PCI236_INTR_DISABLE,
	     devpriv->lcr_iobase + PLX9052_INTCSR);
}

static bool pci236_intr_chk_clr_cb(struct comedi_device *dev)
{
	struct pc236_private *devpriv = dev->private;

	/* check if interrupt occurred */
	if (!(inl(devpriv->lcr_iobase + PLX9052_INTCSR) &
	      PLX9052_INTCSR_LI1STAT))
		return false;
	/* clear the interrupt */
	pci236_intr_update_cb(dev, devpriv->enable_irq);
	return true;
}

static const struct pc236_board pc236_pci_board = {
	.name = "pci236",
	.intr_update_cb = pci236_intr_update_cb,
	.intr_chk_clr_cb = pci236_intr_chk_clr_cb,
};

static int pci236_auto_attach(struct comedi_device *dev,
			      unsigned long context_unused)
{
	struct pci_dev *pci_dev = comedi_to_pci_dev(dev);
	struct pc236_private *devpriv;
	unsigned long iobase;
	int ret;

	dev_info(dev->class_dev, "amplc_pci236: attach pci %s\n",
		 pci_name(pci_dev));

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	dev->board_ptr = &pc236_pci_board;
	dev->board_name = pc236_pci_board.name;
	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv->lcr_iobase = pci_resource_start(pci_dev, 1);
	iobase = pci_resource_start(pci_dev, 2);
	return amplc_pc236_common_attach(dev, iobase, pci_dev->irq,
					 IRQF_SHARED);
}

static struct comedi_driver amplc_pci236_driver = {
	.driver_name = "amplc_pci236",
	.module = THIS_MODULE,
	.auto_attach = pci236_auto_attach,
	.detach = comedi_pci_detach,
};

static const struct pci_device_id pci236_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, 0x0009) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, pci236_pci_table);

static int amplc_pci236_pci_probe(struct pci_dev *dev,
				  const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &amplc_pci236_driver,
				      id->driver_data);
}

static struct pci_driver amplc_pci236_pci_driver = {
	.name		= "amplc_pci236",
	.id_table	= pci236_pci_table,
	.probe		= &amplc_pci236_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};

module_comedi_pci_driver(amplc_pci236_driver, amplc_pci236_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Amplicon PCI236 DIO boards");
MODULE_LICENSE("GPL");

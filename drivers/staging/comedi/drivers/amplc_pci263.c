// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Amplicon PCI263 relay board.
 *
 * Copyright (C) 2002 MEV Ltd. <https://www.mev.co.uk/>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: amplc_pci263
 * Description: Amplicon PCI263
 * Author: Ian Abbott <abbotti@mev.co.uk>
 * Devices: [Amplicon] PCI263 (amplc_pci263)
 * Updated: Fri, 12 Apr 2013 15:19:36 +0100
 * Status: works
 *
 * Configuration options: not applicable, uses PCI auto config
 *
 * The board appears as one subdevice, with 16 digital outputs, each
 * connected to a reed-relay. Relay contacts are closed when output is 1.
 * The state of the outputs can be read.
 */

#include <linux/module.h>

#include "../comedi_pci.h"

/* PCI263 registers */
#define PCI263_DO_0_7_REG	0x00
#define PCI263_DO_8_15_REG	0x01

static int pci263_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data)) {
		outb(s->state & 0xff, dev->iobase + PCI263_DO_0_7_REG);
		outb((s->state >> 8) & 0xff, dev->iobase + PCI263_DO_8_15_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static int pci263_auto_attach(struct comedi_device *dev,
			      unsigned long context_unused)
{
	struct pci_dev *pci_dev = comedi_to_pci_dev(dev);
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pci_dev, 2);
	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* Digital Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci263_do_insn_bits;

	/* read initial relay state */
	s->state = inb(dev->iobase + PCI263_DO_0_7_REG) |
		   (inb(dev->iobase + PCI263_DO_8_15_REG) << 8);

	return 0;
}

static struct comedi_driver amplc_pci263_driver = {
	.driver_name	= "amplc_pci263",
	.module		= THIS_MODULE,
	.auto_attach	= pci263_auto_attach,
	.detach		= comedi_pci_detach,
};

static const struct pci_device_id pci263_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, 0x000c) },
	{0}
};
MODULE_DEVICE_TABLE(pci, pci263_pci_table);

static int amplc_pci263_pci_probe(struct pci_dev *dev,
				  const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &amplc_pci263_driver,
				      id->driver_data);
}

static struct pci_driver amplc_pci263_pci_driver = {
	.name		= "amplc_pci263",
	.id_table	= pci263_pci_table,
	.probe		= &amplc_pci263_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(amplc_pci263_driver, amplc_pci263_pci_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Amplicon PCI263 relay board");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0+
/*
 * COMEDI driver for Advantech PCI-1720U
 * Copyright (c) 2015 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * Separated from the adv_pci1710 driver written by:
 * Michal Dobes <dobes@tesnet.cz>
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
 * Driver: adv_pci1720
 * Description: 4-channel Isolated D/A Output board
 * Devices: [Advantech] PCI-7120U (adv_pci1720)
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Fri, 29 Oct 2015 17:19:35 -0700
 * Status: untested
 *
 * Configuration options: not applicable, uses PCI auto config
 *
 * The PCI-1720 has 4 isolated 12-bit analog output channels with multiple
 * output ranges. It also has a BoardID switch to allow differentiating
 * multiple boards in the system.
 *
 * The analog outputs can operate in two modes, immediate and synchronized.
 * This driver currently does not support the synchronized output mode.
 *
 * Jumpers JP1 to JP4 are used to set the current sink ranges for each
 * analog output channel. In order to use the current sink ranges, the
 * unipolar 5V range must be used. The voltage output and sink output for
 * each channel is available on the connector as separate pins.
 *
 * Jumper JP5 controls the "hot" reset state of the analog outputs.
 * Depending on its setting, the analog outputs will either keep the
 * last settings and output values or reset to the default state after
 * a "hot" reset. The default state for all channels is uniploar 5V range
 * and all the output values are 0V. To allow this feature to work, the
 * analog outputs are not "reset" when the driver attaches.
 */

#include <linux/module.h>
#include <linux/delay.h>

#include "../comedi_pci.h"

/*
 * PCI BAR2 Register map (dev->iobase)
 */
#define PCI1720_AO_LSB_REG(x)		(0x00 + ((x) * 2))
#define PCI1720_AO_MSB_REG(x)		(0x01 + ((x) * 2))
#define PCI1720_AO_RANGE_REG		0x08
#define PCI1720_AO_RANGE(c, r)		(((r) & 0x3) << ((c) * 2))
#define PCI1720_AO_RANGE_MASK(c)	PCI1720_AO_RANGE((c), 0x3)
#define PCI1720_SYNC_REG		0x09
#define PCI1720_SYNC_CTRL_REG		0x0f
#define PCI1720_SYNC_CTRL_SC0		BIT(0)
#define PCI1720_BOARDID_REG		0x14

static const struct comedi_lrange pci1720_ao_range = {
	4, {
		UNI_RANGE(5),
		UNI_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(10)
	}
};

static int pci1720_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int i;

	/* set the channel range and polarity */
	val = inb(dev->iobase + PCI1720_AO_RANGE_REG);
	val &= ~PCI1720_AO_RANGE_MASK(chan);
	val |= PCI1720_AO_RANGE(chan, range);
	outb(val, dev->iobase + PCI1720_AO_RANGE_REG);

	val = s->readback[chan];
	for (i = 0; i < insn->n; i++) {
		val = data[i];

		outb(val & 0xff, dev->iobase + PCI1720_AO_LSB_REG(chan));
		outb((val >> 8) & 0xff, dev->iobase + PCI1720_AO_MSB_REG(chan));

		/* conversion time is 2us (500 kHz throughput) */
		usleep_range(2, 100);
	}

	s->readback[chan] = val;

	return insn->n;
}

static int pci1720_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inb(dev->iobase + PCI1720_BOARDID_REG);

	return insn->n;
}

static int pci1720_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 2);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/* Analog Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 0x0fff;
	s->range_table	= &pci1720_ao_range;
	s->insn_write	= pci1720_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital Input subdevice (BoardID SW1) */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci1720_di_insn_bits;

	/* disable synchronized output, channels update when written */
	outb(0, dev->iobase + PCI1720_SYNC_CTRL_REG);

	return 0;
}

static struct comedi_driver adv_pci1720_driver = {
	.driver_name	= "adv_pci1720",
	.module		= THIS_MODULE,
	.auto_attach	= pci1720_auto_attach,
	.detach		= comedi_pci_detach,
};

static int adv_pci1720_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adv_pci1720_driver,
				      id->driver_data);
}

static const struct pci_device_id adv_pci1720_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADVANTECH, 0x1720) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adv_pci1720_pci_table);

static struct pci_driver adv_pci1720_pci_driver = {
	.name		= "adv_pci1720",
	.id_table	= adv_pci1720_pci_table,
	.probe		= adv_pci1720_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adv_pci1720_driver, adv_pci1720_pci_driver);

MODULE_AUTHOR("H Hartley Sweeten <hsweeten@visionengravers.com>");
MODULE_DESCRIPTION("Comedi driver for Advantech PCI-1720 Analog Output board");
MODULE_LICENSE("GPL");

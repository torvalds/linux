// SPDX-License-Identifier: GPL-2.0+
/*
 * ke_counter.c
 * Comedi driver for Kolter-Electronic PCI Counter 1 Card
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
 * Driver: ke_counter
 * Description: Driver for Kolter Electronic Counter Card
 * Devices: [Kolter Electronic] PCI Counter Card (ke_counter)
 * Author: Michael Hillmann
 * Updated: Mon, 14 Apr 2008 15:42:42 +0100
 * Status: tested
 *
 * Configuration Options: not applicable, uses PCI auto config
 */

#include <linux/module.h>

#include "../comedi_pci.h"

/*
 * PCI BAR 0 Register I/O map
 */
#define KE_RESET_REG(x)			(0x00 + ((x) * 0x20))
#define KE_LATCH_REG(x)			(0x00 + ((x) * 0x20))
#define KE_LSB_REG(x)			(0x04 + ((x) * 0x20))
#define KE_MID_REG(x)			(0x08 + ((x) * 0x20))
#define KE_MSB_REG(x)			(0x0c + ((x) * 0x20))
#define KE_SIGN_REG(x)			(0x10 + ((x) * 0x20))
#define KE_OSC_SEL_REG			0xf8
#define KE_OSC_SEL_CLK(x)		(((x) & 0x3) << 0)
#define KE_OSC_SEL_EXT			KE_OSC_SEL_CLK(1)
#define KE_OSC_SEL_4MHZ			KE_OSC_SEL_CLK(2)
#define KE_OSC_SEL_20MHZ		KE_OSC_SEL_CLK(3)
#define KE_DO_REG			0xfc

static int ke_counter_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[0];

		/* Order matters */
		outb((val >> 24) & 0xff, dev->iobase + KE_SIGN_REG(chan));
		outb((val >> 16) & 0xff, dev->iobase + KE_MSB_REG(chan));
		outb((val >> 8) & 0xff, dev->iobase + KE_MID_REG(chan));
		outb((val >> 0) & 0xff, dev->iobase + KE_LSB_REG(chan));
	}

	return insn->n;
}

static int ke_counter_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;
	int i;

	for (i = 0; i < insn->n; i++) {
		/* Order matters */
		inb(dev->iobase + KE_LATCH_REG(chan));

		val = inb(dev->iobase + KE_LSB_REG(chan));
		val |= (inb(dev->iobase + KE_MID_REG(chan)) << 8);
		val |= (inb(dev->iobase + KE_MSB_REG(chan)) << 16);
		val |= (inb(dev->iobase + KE_SIGN_REG(chan)) << 24);

		data[i] = val;
	}

	return insn->n;
}

static void ke_counter_reset(struct comedi_device *dev)
{
	unsigned int chan;

	for (chan = 0; chan < 3; chan++)
		outb(0, dev->iobase + KE_RESET_REG(chan));
}

static int ke_counter_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned char src;

	switch (data[0]) {
	case INSN_CONFIG_SET_CLOCK_SRC:
		switch (data[1]) {
		case KE_CLK_20MHZ:	/* default */
			src = KE_OSC_SEL_20MHZ;
			break;
		case KE_CLK_4MHZ:	/* option */
			src = KE_OSC_SEL_4MHZ;
			break;
		case KE_CLK_EXT:	/* Pin 21 on D-sub */
			src = KE_OSC_SEL_EXT;
			break;
		default:
			return -EINVAL;
		}
		outb(src, dev->iobase + KE_OSC_SEL_REG);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		src = inb(dev->iobase + KE_OSC_SEL_REG);
		switch (src) {
		case KE_OSC_SEL_20MHZ:
			data[1] = KE_CLK_20MHZ;
			data[2] = 50;	/* 50ns */
			break;
		case KE_OSC_SEL_4MHZ:
			data[1] = KE_CLK_4MHZ;
			data[2] = 250;	/* 250ns */
			break;
		case KE_OSC_SEL_EXT:
			data[1] = KE_CLK_EXT;
			data[2] = 0;	/* Unknown */
			break;
		default:
			return -EINVAL;
		}
		break;
	case INSN_CONFIG_RESET:
		ke_counter_reset(dev);
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static int ke_counter_do_insn_bits(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + KE_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int ke_counter_auto_attach(struct comedi_device *dev,
				  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 0);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 3;
	s->maxdata	= 0x01ffffff;
	s->range_table	= &range_unknown;
	s->insn_read	= ke_counter_insn_read;
	s->insn_write	= ke_counter_insn_write;
	s->insn_config	= ke_counter_insn_config;

	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 3;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= ke_counter_do_insn_bits;

	outb(KE_OSC_SEL_20MHZ, dev->iobase + KE_OSC_SEL_REG);

	ke_counter_reset(dev);

	return 0;
}

static struct comedi_driver ke_counter_driver = {
	.driver_name	= "ke_counter",
	.module		= THIS_MODULE,
	.auto_attach	= ke_counter_auto_attach,
	.detach		= comedi_pci_detach,
};

static int ke_counter_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ke_counter_driver,
				      id->driver_data);
}

static const struct pci_device_id ke_counter_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_KOLTER, 0x0014) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ke_counter_pci_table);

static struct pci_driver ke_counter_pci_driver = {
	.name		= "ke_counter",
	.id_table	= ke_counter_pci_table,
	.probe		= ke_counter_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ke_counter_driver, ke_counter_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Kolter Electronic Counter Card");
MODULE_LICENSE("GPL");

/*
 * adl_pci6208.c
 * Comedi driver for ADLink 6208 series cards
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
 * Driver: adl_pci6208
 * Description: ADLink PCI-6208/6216 Series Multi-channel Analog Output Cards
 * Devices: (ADLink) PCI-6208 [adl_pci6208]
 *	    (ADLink) PCI-6216 [adl_pci6216]
 * Author: nsyeow <nsyeow@pd.jaring.my>
 * Updated: Fri, 30 Jan 2004 14:44:27 +0800
 * Status: untested
 *
 * Configuration Options: not applicable, uses PCI auto config
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "../comedidev.h"

/*
 * PCI-6208/6216-GL register map
 */
#define PCI6208_AO_CONTROL(x)		(0x00 + (2 * (x)))
#define PCI6208_AO_STATUS		0x00
#define PCI6208_AO_STATUS_DATA_SEND	(1 << 0)
#define PCI6208_DIO			0x40
#define PCI6208_DIO_DO_MASK		(0x0f)
#define PCI6208_DIO_DO_SHIFT		(0)
#define PCI6208_DIO_DI_MASK		(0xf0)
#define PCI6208_DIO_DI_SHIFT		(4)

enum pci6208_boardid {
	BOARD_PCI6208,
	BOARD_PCI6216,
};

struct pci6208_board {
	const char *name;
	int ao_chans;
};

static const struct pci6208_board pci6208_boards[] = {
	[BOARD_PCI6208] = {
		.name		= "adl_pci6208",
		.ao_chans	= 8,
	},
	[BOARD_PCI6216] = {
		.name		= "adl_pci6216",
		.ao_chans	= 16,
	},
};

static int pci6208_ao_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + PCI6208_AO_STATUS);
	if ((status & PCI6208_AO_STATUS_DATA_SEND) == 0)
		return 0;
	return -EBUSY;
}

static int pci6208_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int ret;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		/* D/A transfer rate is 2.2us */
		ret = comedi_timeout(dev, s, insn, pci6208_ao_eoc, 0);
		if (ret)
			return ret;

		/* the hardware expects two's complement values */
		outw(comedi_offset_munge(s, val),
		     dev->iobase + PCI6208_AO_CONTROL(chan));

		s->readback[chan] = val;
	}

	return insn->n;
}

static int pci6208_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int val;

	val = inw(dev->iobase + PCI6208_DIO);
	val = (val & PCI6208_DIO_DI_MASK) >> PCI6208_DIO_DI_SHIFT;

	data[1] = val;

	return insn->n;
}

static int pci6208_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + PCI6208_DIO);

	data[1] = s->state;

	return insn->n;
}

static int pci6208_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct pci6208_board *boardinfo = NULL;
	struct comedi_subdevice *s;
	unsigned int val;
	int ret;

	if (context < ARRAY_SIZE(pci6208_boards))
		boardinfo = &pci6208_boards[context];
	if (!boardinfo)
		return -ENODEV;
	dev->board_ptr = boardinfo;
	dev->board_name = boardinfo->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 2);

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog output subdevice */
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= boardinfo->ao_chans;
	s->maxdata	= 0xffff;
	s->range_table	= &range_bipolar10;
	s->insn_write	= pci6208_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s = &dev->subdevices[1];
	/* digital input subdevice */
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci6208_di_insn_bits;

	s = &dev->subdevices[2];
	/* digital output subdevice */
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pci6208_do_insn_bits;

	/*
	 * Get the read back signals from the digital outputs
	 * and save it as the initial state for the subdevice.
	 */
	val = inw(dev->iobase + PCI6208_DIO);
	val = (val & PCI6208_DIO_DO_MASK) >> PCI6208_DIO_DO_SHIFT;
	s->state	= val;

	return 0;
}

static struct comedi_driver adl_pci6208_driver = {
	.driver_name	= "adl_pci6208",
	.module		= THIS_MODULE,
	.auto_attach	= pci6208_auto_attach,
	.detach		= comedi_pci_detach,
};

static int adl_pci6208_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adl_pci6208_driver,
				      id->driver_data);
}

static const struct pci_device_id adl_pci6208_pci_table[] = {
	{ PCI_VDEVICE(ADLINK, 0x6208), BOARD_PCI6208 },
	{ PCI_VDEVICE(ADLINK, 0x6216), BOARD_PCI6216 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adl_pci6208_pci_table);

static struct pci_driver adl_pci6208_pci_driver = {
	.name		= "adl_pci6208",
	.id_table	= adl_pci6208_pci_table,
	.probe		= adl_pci6208_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adl_pci6208_driver, adl_pci6208_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for ADLink 6208 series cards");
MODULE_LICENSE("GPL");

/*
    comedi/drivers/adl_pci6208.c

    Hardware driver for ADLink 6208 series cards:
	card	     | voltage output    | current output
	-------------+-------------------+---------------
	PCI-6208V    |  8 channels       | -
	PCI-6216V    | 16 channels       | -
	PCI-6208A    |  8 channels       | 8 channels

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
/*
Driver: adl_pci6208
Description: ADLink PCI-6208/6216 Series Multi-channel Analog Output Cards
Devices: (ADLink) PCI-6208 [adl_pci6208]
	 (ADLink) PCI-6216 [adl_pci6216]
Author: nsyeow <nsyeow@pd.jaring.my>
Updated: Fri, 30 Jan 2004 14:44:27 +0800
Status: untested

Configuration Options: not applicable, uses PCI auto config

References:
	- ni_660x.c
	- adl_pci9111.c		copied the entire pci setup section
	- adl_pci9118.c
*/

#include "../comedidev.h"

/*
 * ADLINK PCI Device ID's supported by this driver
 */
#define PCI_DEVICE_ID_PCI6208		0x6208
#define PCI_DEVICE_ID_PCI6216		0x6216

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

#define PCI6208_MAX_AO_CHANNELS		16

struct pci6208_board {
	const char *name;
	unsigned short dev_id;
	int ao_chans;
};

static const struct pci6208_board pci6208_boards[] = {
	{
		.name		= "adl_pci6208",
		.dev_id		= PCI_DEVICE_ID_PCI6208,
		.ao_chans	= 8,
	}, {
		.name		= "adl_pci6216",
		.dev_id		= PCI_DEVICE_ID_PCI6216,
		.ao_chans	= 16,
	},
};

struct pci6208_private {
	unsigned int ao_readback[PCI6208_MAX_AO_CHANNELS];
};

static int pci6208_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct pci6208_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long invert = 1 << (16 - 1);
	unsigned long value = 0;
	unsigned short status;
	int i;

	for (i = 0; i < insn->n; i++) {
		value = data[i] ^ invert;

		do {
			status = inw(dev->iobase + PCI6208_AO_STATUS);
		} while (status & PCI6208_AO_STATUS_DATA_SEND);

		outw(value, dev->iobase + PCI6208_AO_CONTROL(chan));
	}
	devpriv->ao_readback[chan] = value;

	return insn->n;
}

static int pci6208_ao_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct pci6208_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

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
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outw(s->state, dev->iobase + PCI6208_DIO);
	}

	data[1] = s->state;

	return insn->n;
}

static const void *pci6208_find_boardinfo(struct comedi_device *dev,
					  struct pci_dev *pcidev)
{
	const struct pci6208_board *boardinfo;
	int i;

	for (i = 0; i < ARRAY_SIZE(pci6208_boards); i++) {
		boardinfo = &pci6208_boards[i];
		if (boardinfo->dev_id == pcidev->device)
			return boardinfo;
	}
	return NULL;
}

static int pci6208_auto_attach(struct comedi_device *dev,
					 unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct pci6208_board *boardinfo;
	struct pci6208_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int val;
	int ret;

	boardinfo = pci6208_find_boardinfo(dev, pcidev);
	if (!boardinfo)
		return -ENODEV;
	dev->board_ptr = boardinfo;
	dev->board_name = boardinfo->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
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
	s->insn_write	= pci6208_ao_winsn;
	s->insn_read	= pci6208_ao_rinsn;

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
	s->io_bits	= 0x0f;

	dev_info(dev->class_dev, "%s: %s, I/O base=0x%04lx\n",
		dev->driver->driver_name, dev->board_name, dev->iobase);

	return 0;
}

static void pci6208_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver adl_pci6208_driver = {
	.driver_name	= "adl_pci6208",
	.module		= THIS_MODULE,
	.auto_attach	= pci6208_auto_attach,
	.detach		= pci6208_detach,
};

static int adl_pci6208_pci_probe(struct pci_dev *dev,
					   const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &adl_pci6208_driver);
}

static void __devexit adl_pci6208_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(adl_pci6208_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_PCI6208) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_PCI6216) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adl_pci6208_pci_table);

static struct pci_driver adl_pci6208_pci_driver = {
	.name		= "adl_pci6208",
	.id_table	= adl_pci6208_pci_table,
	.probe		= adl_pci6208_pci_probe,
	.remove		= adl_pci6208_pci_remove,
};
module_comedi_pci_driver(adl_pci6208_driver, adl_pci6208_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

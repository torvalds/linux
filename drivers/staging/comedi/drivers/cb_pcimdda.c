/*
    comedi/drivers/cb_pcimdda.c
    Computer Boards PCIM-DDA06-16 Comedi driver
    Author: Calin Culianu <calin@ajvar.org>

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
*/
/*
Driver: cb_pcimdda
Description: Measurement Computing PCIM-DDA06-16
Devices: [Measurement Computing] PCIM-DDA06-16 (cb_pcimdda)
Author: Calin Culianu <calin@ajvar.org>
Updated: Mon, 14 Apr 2008 15:15:51 +0100
Status: works

All features of the PCIM-DDA06-16 board are supported.  This board
has 6 16-bit AO channels, and the usual 8255 DIO setup.  (24 channels,
configurable in banks of 8 and 4, etc.).  This board does not support commands.

The board has a peculiar way of specifying AO gain/range settings -- You have
1 jumper bank on the card, which either makes all 6 AO channels either
5 Volt unipolar, 5V bipolar, 10 Volt unipolar or 10V bipolar.

Since there is absolutely _no_ way to tell in software how this jumper is set
(well, at least according  to the rather thin spec. from Measurement Computing
 that comes with the board), the driver assumes the jumper is at its factory
default setting of +/-5V.

Also of note is the fact that this board features another jumper, whose
state is also completely invisible to software.  It toggles two possible AO
output modes on the board:

  - Update Mode: Writing to an AO channel instantaneously updates the actual
    signal output by the DAC on the board (this is the factory default).
  - Simultaneous XFER Mode: Writing to an AO channel has no effect until
    you read from any one of the AO channels.  This is useful for loading
    all 6 AO values, and then reading from any one of the AO channels on the
    device to instantly update all 6 AO values in unison.  Useful for some
    control apps, I would assume?  If your jumper is in this setting, then you
    need to issue your comedi_data_write()s to load all the values you want,
    then issue one comedi_data_read() on any channel on the AO subdevice
    to initiate the simultaneous XFER.

Configuration Options: not applicable, uses PCI auto config
*/

/*
    This is a driver for the Computer Boards PCIM-DDA06-16 Analog Output
    card.  This board has a unique register layout and as such probably
    deserves its own driver file.

    It is theoretically possible to integrate this board into the cb_pcidda
    file, but since that isn't my code, I didn't want to significantly
    modify that file to support this board (I thought it impolite to do so).

    At any rate, if you feel ambitious, please feel free to take
    the code out of this file and combine it with a more unified driver
    file.

    I would like to thank Timothy Curry <Timothy.Curry@rdec.redstone.army.mil>
    for lending me a board so that I could write this driver.

    -Calin Culianu <calin@ajvar.org>
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"

#include "8255.h"

/* device ids of the cards we support -- currently only 1 card supported */
#define PCI_ID_PCIM_DDA06_16		0x0053

/*
 * Register map, 8-bit access only
 */
#define PCIMDDA_DA_CHAN(x)		(0x00 + (x) * 2)
#define PCIMDDA_8255_BASE_REG		0x0c

#define MAX_AO_READBACK_CHANNELS	6

struct cb_pcimdda_private {
	unsigned int ao_readback[MAX_AO_READBACK_CHANNELS];
};

static int cb_pcimdda_ao_winsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct cb_pcimdda_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned long offset = dev->iobase + PCIMDDA_DA_CHAN(chan);
	unsigned int val = 0;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		/*
		 * Write the LSB then MSB.
		 *
		 * If the simultaneous xfer mode is selected by the
		 * jumper on the card, a read instruction is needed
		 * in order to initiate the simultaneous transfer.
		 * Otherwise, the DAC will be updated when the MSB
		 * is written.
		 */
		outb(val & 0x00ff, offset);
		outb((val >> 8) & 0x00ff, offset + 1);
	}

	/* Cache the last value for readback */
	devpriv->ao_readback[chan] = val;

	return insn->n;
}

static int cb_pcimdda_ao_rinsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct cb_pcimdda_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long offset = dev->iobase + PCIMDDA_DA_CHAN(chan);
	int i;

	for (i = 0; i < insn->n; i++) {
		/* Initiate the simultaneous transfer */
		inw(offset);

		data[i] = devpriv->ao_readback[chan];
	}

	return insn->n;
}

static int cb_pcimdda_auto_attach(struct comedi_device *dev,
					    unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct cb_pcimdda_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 3);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog output subdevice */
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= 6;
	s->maxdata	= 0xffff;
	s->range_table	= &range_bipolar5;
	s->insn_write	= cb_pcimdda_ao_winsn;
	s->insn_read	= cb_pcimdda_ao_rinsn;

	s = &dev->subdevices[1];
	/* digital i/o subdevice */
	ret = subdev_8255_init(dev, s, NULL,
			dev->iobase + PCIMDDA_8255_BASE_REG);
	if (ret)
		return ret;

	dev_info(dev->class_dev, "%s attached\n", dev->board_name);

	return 1;
}

static struct comedi_driver cb_pcimdda_driver = {
	.driver_name	= "cb_pcimdda",
	.module		= THIS_MODULE,
	.auto_attach	= cb_pcimdda_auto_attach,
	.detach		= comedi_pci_disable,
};

static int cb_pcimdda_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &cb_pcimdda_driver,
				      id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(cb_pcimdda_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_ID_PCIM_DDA06_16) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcimdda_pci_table);

static struct pci_driver cb_pcimdda_driver_pci_driver = {
	.name		= "cb_pcimdda",
	.id_table	= cb_pcimdda_pci_table,
	.probe		= cb_pcimdda_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(cb_pcimdda_driver, cb_pcimdda_driver_pci_driver);

MODULE_AUTHOR("Calin A. Culianu <calin@rtlab.org>");
MODULE_DESCRIPTION("Comedi low-level driver for the Computerboards PCIM-DDA "
		   "series.  Currently only supports PCIM-DDA06-16 (which "
		   "also happens to be the only board in this series. :) ) ");
MODULE_LICENSE("GPL");

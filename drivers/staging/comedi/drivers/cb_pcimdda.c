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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

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

Configuration Options:
  [0] PCI bus (optional)
  [1] PCI slot (optional)
  [2] analog output range jumper setting
      0 == +/- 5 V
      1 == +/- 10 V
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

#include "../comedidev.h"

#include "8255.h"

/* device ids of the cards we support -- currently only 1 card supported */
#define PCI_VENDOR_ID_COMPUTERBOARDS	0x1307
#define PCI_ID_PCIM_DDA06_16		0x0053

/*
 * Register map, 8-bit access only
 */
#define PCIMDDA_DA_CHAN(x)		(0x00 + (x) * 2)
#define PCIMDDA_8255_BASE_REG		0x0c

/*
 * This is straight from skel.c -- I did this in case this source file
 * will someday support more than 1 board...
 */
struct cb_pcimdda_board {
	const char *name;
	unsigned short device_id;
	int ao_chans;
	int ao_bits;
	int regs_badrindex;	/* IO Region for the control, analog output,
				   and DIO registers */
	int reg_sz;		/* number of bytes of registers in io region */
};

static const struct cb_pcimdda_board cb_pcimdda_boards[] = {
	{
	 .name = "cb_pcimdda06-16",
	 .device_id = PCI_ID_PCIM_DDA06_16,
	 .ao_chans = 6,
	 .ao_bits = 16,
	 .regs_badrindex = 3,
	 .reg_sz = 16,
	 }
};

/*
 * this structure is for data unique to this hardware driver.  If
 * several hardware drivers keep similar information in this structure,
 * feel free to suggest moving the variable to the struct comedi_device
 * struct.
 */
struct cb_pcimdda_private {
	char attached_to_8255;	/* boolean */

#define MAX_AO_READBACK_CHANNELS 6
	/* Used for AO readback */
	unsigned int ao_readback[MAX_AO_READBACK_CHANNELS];

};

static int cb_pcimdda_ao_winsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct cb_pcimdda_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long offset = dev->iobase + chan * 2;

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; i++) {
		/*  first, load the low byte */
		outb((char)(data[i] & 0x00ff), offset);
		/*  next, write the high byte -- only after this is written is
		   the channel voltage updated in the DAC, unless
		   we're in simultaneous xfer mode (jumper on card)
		   then a rinsn is necessary to actually update the DAC --
		   see cb_pcimdda_ao_rinsn() below... */
		outb((char)(data[i] >> 8 & 0x00ff), offset + 1);

		/* for testing only.. the actual rinsn SHOULD do an inw!
		   (see the stuff about simultaneous XFER mode on this board) */
		devpriv->ao_readback[chan] = data[i];
	}

	/* return the number of samples read/written */
	return i;
}

/* AO subdevices should have a read insn as well as a write insn.

   Usually this means copying a value stored in devpriv->ao_readback.
   However, since this board has this jumper setting called "Simultaneous
   Xfer mode" (off by default), we will support it.  Simultaneaous xfer
   mode is accomplished by loading ALL the values you want for AO in all the
   channels, then READing off one of the AO registers to initiate the
   instantaneous simultaneous update of all DAC outputs, which makes
   all AO channels update simultaneously.  This is useful for some control
   applications, I would imagine.
*/
static int cb_pcimdda_ao_rinsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct cb_pcimdda_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		inw(dev->iobase + chan * 2);
		/*
		 * should I set data[i] to the result of the actual read
		 * on the register or the cached unsigned int in
		 * devpriv->ao_readback[]?
		 */
		data[i] = devpriv->ao_readback[chan];
	}

	return i;
}

static struct pci_dev *cb_pcimdda_probe(struct comedi_device *dev,
					struct comedi_devconfig *it)
{
	struct pci_dev *pcidev = NULL;
	int index;

	for_each_pci_dev(pcidev) {
		if (pcidev->vendor != PCI_VENDOR_ID_COMPUTERBOARDS)
			continue;
		for (index = 0; index < ARRAY_SIZE(cb_pcimdda_boards); index++) {
			if (cb_pcimdda_boards[index].device_id != pcidev->device)
				continue;
			if (it->options[0] || it->options[1]) {
				if (pcidev->bus->number != it->options[0] ||
				    PCI_SLOT(pcidev->devfn) != it->options[1]) {
					continue;
				}
			}

			dev->board_ptr = cb_pcimdda_boards + index;
			return pcidev;
		}
	}
	return NULL;
}

static int cb_pcimdda_attach(struct comedi_device *dev,
			     struct comedi_devconfig *it)
{
	const struct cb_pcimdda_board *thisboard;
	struct cb_pcimdda_private *devpriv;
	struct pci_dev *pcidev;
	struct comedi_subdevice *s;
	int ret;

	ret = alloc_private(dev, sizeof(*devpriv));
	if (ret)
		return ret;
	devpriv = dev->private;

	pcidev = cb_pcimdda_probe(dev, it);
	if (!pcidev)
		return -EIO;
	comedi_set_hw_dev(dev, &pcidev->dev);
	thisboard = comedi_board(dev);
	dev->board_name = thisboard->name;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, thisboard->regs_badrindex);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = dev->subdevices + 0;

	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = thisboard->ao_chans;
	s->maxdata = (1 << thisboard->ao_bits) - 1;
	/* this is hard-coded here */
	if (it->options[2])
		s->range_table = &range_bipolar10;
	else
		s->range_table = &range_bipolar5;
	s->insn_write = &cb_pcimdda_ao_winsn;
	s->insn_read = &cb_pcimdda_ao_rinsn;

	s = dev->subdevices + 1;
	/* digital i/o subdevice */
	ret = subdev_8255_init(dev, s, NULL,
			dev->iobase + PCIMDDA_8255_BASE_REG);
	if (ret)
		return ret;
	devpriv->attached_to_8255 = 1;

	dev_info(dev->class_dev, "%s attached\n", dev->board_name);

	return 1;
}

static void cb_pcimdda_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct cb_pcimdda_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->subdevices && devpriv->attached_to_8255) {
			subdev_8255_cleanup(dev, dev->subdevices + 2);
			devpriv->attached_to_8255 = 0;
		}
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
		pci_dev_put(pcidev);
	}
}

static struct comedi_driver cb_pcimdda_driver = {
	.driver_name	= "cb_pcimdda",
	.module		= THIS_MODULE,
	.attach		= cb_pcimdda_attach,
	.detach		= cb_pcimdda_detach,
};

static int __devinit cb_pcimdda_pci_probe(struct pci_dev *dev,
					  const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &cb_pcimdda_driver);
}

static void __devexit cb_pcimdda_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(cb_pcimdda_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_COMPUTERBOARDS, PCI_ID_PCIM_DDA06_16) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcimdda_pci_table);

static struct pci_driver cb_pcimdda_driver_pci_driver = {
	.name		= "cb_pcimdda",
	.id_table	= cb_pcimdda_pci_table,
	.probe		= cb_pcimdda_pci_probe,
	.remove		= __devexit_p(cb_pcimdda_pci_remove),
};
module_comedi_pci_driver(cb_pcimdda_driver, cb_pcimdda_driver_pci_driver);

MODULE_AUTHOR("Calin A. Culianu <calin@rtlab.org>");
MODULE_DESCRIPTION("Comedi low-level driver for the Computerboards PCIM-DDA "
		   "series.  Currently only supports PCIM-DDA06-16 (which "
		   "also happens to be the only board in this series. :) ) ");
MODULE_LICENSE("GPL");

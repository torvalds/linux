/*
    comedi/drivers/adv_pci1724.c
    This is a driver for the Advantech PCI-1724U card.

    Author:  Frank Mori Hess <fmh6jj@gmail.com>
    Copyright (C) 2013 GnuBIO Inc

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>

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

Driver: adv_1724
Description: Advantech PCI-1724U
Author: Frank Mori Hess <fmh6jj@gmail.com>
Status: works
Updated: 2013-02-09
Devices: [Advantech] PCI-1724U (adv_pci1724)

Subdevice 0 is the analog output.
Subdevice 1 is the offset calibration for the analog output.
Subdevice 2 is the gain calibration for the analog output.

The calibration offset and gains have quite a large effect
on the analog output, so it is possible to adjust the analog output to
have an output range significantly different from the board's
nominal output ranges.  For a calibrated +/- 10V range, the analog
output's offset will be set somewhere near mid-range (0x2000) and its
gain will be near maximum (0x3fff).

There is really no difference between the board's documented 0-20mA
versus 4-20mA output ranges.  To pick one or the other is simply a matter
of adjusting the offset and gain calibration until the board outputs in
the desired range.

Configuration options:
   None

Manual configuration of comedi devices is not supported by this driver;
supported PCI devices are configured as comedi devices automatically.

*/

#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"

/*
 * PCI bar 2 Register I/O map (dev->iobase)
 */
#define PCI1724_DAC_CTRL_REG		0x00
#define PCI1724_DAC_CTRL_GX(x)		(1 << (20 + ((x) / 8)))
#define PCI1724_DAC_CTRL_CX(x)		(((x) % 8) << 16)
#define PCI1724_DAC_CTRL_MODE_GAIN	(1 << 14)
#define PCI1724_DAC_CTRL_MODE_OFFSET	(2 << 14)
#define PCI1724_DAC_CTRL_MODE_NORMAL	(3 << 14)
#define PCI1724_DAC_CTRL_MODE_MASK	(3 << 14)
#define PCI1724_DAC_CTRL_DATA(x)	(((x) & 0x3fff) << 0)
#define PCI1724_SYNC_OUTPUT_REG		0x04
#define PCI1724_EEPROM_CTRL_REG		0x08
#define PCI1724_SYNC_OUTPUT_TRIG_REG	0x0c
#define PCI1724_BOARD_ID_REG		0x10

enum sync_output_contents {
	SYNC_MODE = 0x1,
	DAC_BUSY = 0x2, /* dac state machine is not ready */
};

enum sync_output_trigger_contents {
	SYNC_TRIGGER_BITS = 0x0 /* any value works */
};

enum board_id_contents {
	BOARD_ID_MASK = 0xf
};

static const struct comedi_lrange ao_ranges_1724 = {
	4, {
		BIP_RANGE(10),
		RANGE_mA(0, 20),
		RANGE_mA(4, 20),
		RANGE_unitless(0, 1)
	}
};

static int adv_pci1724_dac_idle(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned long context)
{
	unsigned int status;

	status = inl(dev->iobase + PCI1724_SYNC_OUTPUT_REG);
	if ((status & DAC_BUSY) == 0)
		return 0;
	return -EBUSY;
}

static int adv_pci1724_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned long mode = (unsigned long)s->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int ctrl;
	int ret;
	int i;

	ctrl = PCI1724_DAC_CTRL_GX(chan) | PCI1724_DAC_CTRL_CX(chan) | mode;

	/* turn off synchronous mode */
	outl(0, dev->iobase + PCI1724_SYNC_OUTPUT_REG);

	for (i = 0; i < insn->n; ++i) {
		unsigned int val = data[i];

		ret = comedi_timeout(dev, s, insn, adv_pci1724_dac_idle, 0);
		if (ret)
			return ret;

		outl(ctrl | PCI1724_DAC_CTRL_DATA(val),
		     dev->iobase + PCI1724_DAC_CTRL_REG);

		s->readback[chan] = val;
	}

	return insn->n;
}

/* Allocate and initialize the subdevice structures.
 */
static int setup_subdevices(struct comedi_device *dev)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* analog output subdevice */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_GROUND;
	s->n_chan = 32;
	s->maxdata = 0x3fff;
	s->range_table = &ao_ranges_1724;
	s->insn_write = adv_pci1724_insn_write;
	s->private = (void *)PCI1724_DAC_CTRL_MODE_NORMAL;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* offset calibration */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_CALIB;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
	s->n_chan = 32;
	s->maxdata = 0x3fff;
	s->insn_write = adv_pci1724_insn_write;
	s->private = (void *)PCI1724_DAC_CTRL_MODE_OFFSET;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* gain calibration */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_CALIB;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
	s->n_chan = 32;
	s->maxdata = 0x3fff;
	s->insn_write = adv_pci1724_insn_write;
	s->private = (void *)PCI1724_DAC_CTRL_MODE_GAIN;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	return 0;
}

static int adv_pci1724_auto_attach(struct comedi_device *dev,
				   unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	int retval;
	unsigned int board_id;

	retval = comedi_pci_enable(dev);
	if (retval)
		return retval;

	dev->iobase = pci_resource_start(pcidev, 2);
	board_id = inl(dev->iobase + PCI1724_BOARD_ID_REG) & BOARD_ID_MASK;
	dev_info(dev->class_dev, "board id: %d\n", board_id);

	retval = setup_subdevices(dev);
	if (retval < 0)
		return retval;

	dev_info(dev->class_dev, "%s (pci %s) attached, board id: %u\n",
		 dev->board_name, pci_name(pcidev), board_id);
	return 0;
}

static struct comedi_driver adv_pci1724_driver = {
	.driver_name = "adv_pci1724",
	.module = THIS_MODULE,
	.auto_attach = adv_pci1724_auto_attach,
	.detach = comedi_pci_detach,
};

static int adv_pci1724_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adv_pci1724_driver,
				      id->driver_data);
}

static const struct pci_device_id adv_pci1724_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADVANTECH, 0x1724) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adv_pci1724_pci_table);

static struct pci_driver adv_pci1724_pci_driver = {
	.name = "adv_pci1724",
	.id_table = adv_pci1724_pci_table,
	.probe = adv_pci1724_pci_probe,
	.remove = comedi_pci_auto_unconfig,
};

module_comedi_pci_driver(adv_pci1724_driver, adv_pci1724_pci_driver);

MODULE_AUTHOR("Frank Mori Hess <fmh6jj@gmail.com>");
MODULE_DESCRIPTION("Advantech PCI-1724U Comedi driver");
MODULE_LICENSE("GPL");

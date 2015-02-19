/*
 * comedi/drivers/cb_pcimdas.c
 * Comedi driver for Computer Boards PCIM-DAS1602/16 and PCIe-DAS1602/16
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
 * Driver: cb_pcimdas
 * Description: Measurement Computing PCI Migration series boards
 * Devices: [ComputerBoards] PCIM-DAS1602/16 (cb_pcimdas), PCIe-DAS1602/16
 * Author: Richard Bytheway
 * Updated: Mon, 13 Oct 2014 11:57:39 +0000
 * Status: experimental
 *
 * Written to support the PCIM-DAS1602/16 and PCIe-DAS1602/16.
 *
 * Configuration Options:
 *   none
 *
 * Manual configuration of PCI(e) cards is not supported; they are configured
 * automatically.
 *
 * Developed from cb_pcidas and skel by Richard Bytheway (mocelet@sucs.org).
 * Only supports DIO, AO and simple AI in it's present form.
 * No interrupts, multi channel or FIFO AI,
 * although the card looks like it could support this.
 *
 * http://www.mccdaq.com/PDFs/Manuals/pcim-das1602-16.pdf
 * http://www.mccdaq.com/PDFs/Manuals/pcie-das1602-16.pdf
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "plx9052.h"
#include "8255.h"

/* Registers for the PCIM-DAS1602/16 and PCIe-DAS1602/16 */

/* DAC Offsets */
#define ADC_TRIG 0
#define DAC0_OFFSET 2
#define DAC1_OFFSET 4

/* AI and Counter Constants */
#define MUX_LIMITS 0
#define MAIN_CONN_DIO 1
#define ADC_STAT 2
#define ADC_CONV_STAT 3
#define ADC_INT 4
#define ADC_PACER 5
#define BURST_MODE 6
#define PROG_GAIN 7
#define CLK8254_1_DATA 8
#define CLK8254_2_DATA 9
#define CLK8254_3_DATA 10
#define CLK8254_CONTROL 11
#define USER_COUNTER 12
#define RESID_COUNT_H 13
#define RESID_COUNT_L 14

/*
 * this structure is for data unique to this hardware driver.  If
 * several hardware drivers keep similar information in this structure,
 * feel free to suggest moving the variable to the struct comedi_device
 * struct.
 */
struct cb_pcimdas_private {
	/* base addresses */
	unsigned long daqio;
	unsigned long BADR3;
};

static int cb_pcimdas_ai_eoc(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned long context)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int status;

	status = inb(devpriv->BADR3 + 2);
	if ((status & 0x80) == 0)
		return 0;
	return -EBUSY;
}

static int cb_pcimdas_ai_rinsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	int n;
	unsigned int d;
	int chan = CR_CHAN(insn->chanspec);
	unsigned short chanlims;
	int maxchans;
	int ret;

	/*  only support sw initiated reads from a single channel */

	/* check channel number */
	if ((inb(devpriv->BADR3 + 2) & 0x20) == 0)	/* differential mode */
		maxchans = s->n_chan / 2;
	else
		maxchans = s->n_chan;

	if (chan > (maxchans - 1))
		return -ETIMEDOUT;	/* *** Wrong error code. Fixme. */

	/* configure for sw initiated read */
	d = inb(devpriv->BADR3 + 5);
	if ((d & 0x03) > 0) {	/* only reset if needed. */
		d = d & 0xfd;
		outb(d, devpriv->BADR3 + 5);
	}

	/* set bursting off, conversions on */
	outb(0x01, devpriv->BADR3 + 6);

	/* set range to 10V. UP/BP is controlled by a switch on the board */
	outb(0x00, devpriv->BADR3 + 7);

	/*
	 * write channel limits to multiplexer, set Low (bits 0-3) and
	 * High (bits 4-7) channels to chan.
	 */
	chanlims = chan | (chan << 4);
	outb(chanlims, devpriv->BADR3 + 0);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outw(0, devpriv->daqio + 0);

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, cb_pcimdas_ai_eoc, 0);
		if (ret)
			return ret;

		/* read data */
		data[n] = inw(devpriv->daqio + 0);
	}

	/* return the number of samples read/written */
	return n;
}

static int cb_pcimdas_ao_insn_write(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct cb_pcimdas_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	unsigned int reg = (chan) ? DAC1_OFFSET : DAC0_OFFSET;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, devpriv->daqio + reg);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int cb_pcimdas_auto_attach(struct comedi_device *dev,
					    unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct cb_pcimdas_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv->daqio = pci_resource_start(pcidev, 2);
	devpriv->BADR3 = pci_resource_start(pcidev, 3);
	dev->iobase = pci_resource_start(pcidev, 4);

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* dev->read_subdev=s; */
	/*  analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 16;
	s->maxdata = 0xffff;
	s->range_table = &range_unknown;
	s->len_chanlist = 1;	/*  This is the maximum chanlist length that */
	/*  the board can handle */
	s->insn_read = cb_pcimdas_ai_rinsn;

	s = &dev->subdevices[1];
	/*  analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->maxdata = 0xfff;
	/* ranges are hardware settable, but not software readable. */
	s->range_table = &range_unknown;
	s->insn_write = cb_pcimdas_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	s = &dev->subdevices[2];
	/* digital i/o subdevice */
	ret = subdev_8255_init(dev, s, NULL, 0x00);
	if (ret)
		return ret;

	return 0;
}

static struct comedi_driver cb_pcimdas_driver = {
	.driver_name	= "cb_pcimdas",
	.module		= THIS_MODULE,
	.auto_attach	= cb_pcimdas_auto_attach,
	.detach		= comedi_pci_detach,
};

static int cb_pcimdas_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &cb_pcimdas_driver,
				      id->driver_data);
}

static const struct pci_device_id cb_pcimdas_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0056) },	/* PCIM-DAS1602/16 */
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0115) },	/* PCIe-DAS1602/16 */
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcimdas_pci_table);

static struct pci_driver cb_pcimdas_pci_driver = {
	.name		= "cb_pcimdas",
	.id_table	= cb_pcimdas_pci_table,
	.probe		= cb_pcimdas_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(cb_pcimdas_driver, cb_pcimdas_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for PCIM-DAS1602/16 and PCIe-DAS1602/16");
MODULE_LICENSE("GPL");

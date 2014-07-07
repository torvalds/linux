/*
   comedi/drivers/pci1723.c

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
Driver: adv_pci1723
Description: Advantech PCI-1723
Author: yonggang <rsmgnu@gmail.com>, Ian Abbott <abbotti@mev.co.uk>
Devices: [Advantech] PCI-1723 (adv_pci1723)
Updated: Mon, 14 Apr 2008 15:12:56 +0100
Status: works

Configuration Options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)

  If bus/slot is not specified, the first supported
  PCI device found will be used.

Subdevice 0 is 8-channel AO, 16-bit, range +/- 10 V.

Subdevice 1 is 16-channel DIO.  The channels are configurable as input or
output in 2 groups (0 to 7, 8 to 15).  Configuring any channel implicitly
configures all channels in the same group.

TODO:

1. Add the two milliamp ranges to the AO subdevice (0 to 20 mA, 4 to 20 mA).
2. Read the initial ranges and values of the AO subdevice at start-up instead
   of reinitializing them.
3. Implement calibration.
*/

#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"

/* all the registers for the pci1723 board */
#define PCI1723_DA(N)   ((N)<<1)	/* W: D/A register N (0 to 7) */

#define PCI1723_SYN_SET  0x12		/* synchronized set register */
#define PCI1723_ALL_CHNNELE_SYN_STROBE  0x12
					/* synchronized status register */

#define PCI1723_RANGE_CALIBRATION_MODE 0x14
					/* range and calibration mode */
#define PCI1723_RANGE_CALIBRATION_STATUS 0x14
					/* range and calibration status */

#define PCI1723_CONTROL_CMD_CALIBRATION_FUN 0x16
						/*
						 * SADC control command for
						 * calibration function
						 */
#define PCI1723_STATUS_CMD_CALIBRATION_FUN 0x16
						/*
						 * SADC control status for
						 * calibration function
						 */

#define PCI1723_CALIBRATION_PARA_STROBE 0x18
					/* Calibration parameter strobe */

#define PCI1723_DIGITAL_IO_PORT_SET 0x1A	/* Digital I/O port setting */
#define PCI1723_DIGITAL_IO_PORT_MODE 0x1A	/* Digital I/O port mode */

#define PCI1723_WRITE_DIGITAL_OUTPUT_CMD 0x1C
					/* Write digital output command */
#define PCI1723_READ_DIGITAL_INPUT_DATA 0x1C	/* Read digital input data */

#define PCI1723_WRITE_CAL_CMD 0x1E		/* Write calibration command */
#define PCI1723_READ_CAL_STATUS 0x1E		/* Read calibration status */

#define PCI1723_SYN_STROBE 0x20			/* Synchronized strobe */

#define PCI1723_RESET_ALL_CHN_STROBE 0x22
					/* Reset all D/A channels strobe */

#define PCI1723_RESET_CAL_CONTROL_STROBE 0x24
						/*
						 * Reset the calibration
						 * controller strobe
						 */

#define PCI1723_CHANGE_CHA_OUTPUT_TYPE_STROBE 0x26
						/*
						 * Change D/A channels output
						 * type strobe
						 */

#define PCI1723_SELECT_CALIBRATION 0x28	/* Select the calibration Ref_V */

struct pci1723_private {
	unsigned char da_range[8];	/* D/A output range for each channel */
	unsigned short ao_data[8];	/* data output buffer */
};

/*
 * The pci1723 card reset;
 */
static int pci1723_reset(struct comedi_device *dev)
{
	struct pci1723_private *devpriv = dev->private;
	int i;

	outw(0x01, dev->iobase + PCI1723_SYN_SET);
					       /* set synchronous output mode */

	for (i = 0; i < 8; i++) {
		/* set all outputs to 0V */
		devpriv->ao_data[i] = 0x8000;
		outw(devpriv->ao_data[i], dev->iobase + PCI1723_DA(i));
		/* set all ranges to +/- 10V */
		devpriv->da_range[i] = 0;
		outw(((devpriv->da_range[i] << 4) | i),
		     PCI1723_RANGE_CALIBRATION_MODE);
	}

	outw(0, dev->iobase + PCI1723_CHANGE_CHA_OUTPUT_TYPE_STROBE);
							    /* update ranges */
	outw(0, dev->iobase + PCI1723_SYN_STROBE);	    /* update outputs */

	/* set asynchronous output mode */
	outw(0, dev->iobase + PCI1723_SYN_SET);

	return 0;
}

static int pci1723_insn_read_ao(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	struct pci1723_private *devpriv = dev->private;
	int n, chan;

	chan = CR_CHAN(insn->chanspec);
	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_data[chan];

	return n;
}

/*
  analog data output;
*/
static int pci1723_ao_write_winsn(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	struct pci1723_private *devpriv = dev->private;
	int n, chan;
	chan = CR_CHAN(insn->chanspec);

	for (n = 0; n < insn->n; n++) {

		devpriv->ao_data[chan] = data[n];
		outw(data[n], dev->iobase + PCI1723_DA(chan));
	}

	return n;
}

/*
  digital i/o config/query
*/
static int pci1723_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	unsigned short mode;
	int ret;

	if (chan < 8)
		mask = 0x00ff;
	else
		mask = 0xff00;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	/* update hardware DIO mode */
	mode = 0x0000;			/* assume output */
	if (!(s->io_bits & 0x00ff))
		mode |= 0x0001;		/* low byte input */
	if (!(s->io_bits & 0xff00))
		mode |= 0x0002;		/* high byte input */
	outw(mode, dev->iobase + PCI1723_DIGITAL_IO_PORT_SET);

	return insn->n;
}

static int pci1723_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + PCI1723_WRITE_DIGITAL_OUTPUT_CMD);

	data[1] = inw(dev->iobase + PCI1723_READ_DIGITAL_INPUT_DATA);

	return insn->n;
}

static int pci1723_auto_attach(struct comedi_device *dev,
					 unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct pci1723_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 2);

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	dev->write_subdev = s;
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan	= 8;
	s->maxdata	= 0xffff;
	s->len_chanlist	= 8;
	s->range_table	= &range_bipolar10;
	s->insn_write	= pci1723_ao_write_winsn;
	s->insn_read	= pci1723_insn_read_ao;

	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->len_chanlist	= 16;
	s->range_table	= &range_digital;
	s->insn_config	= pci1723_dio_insn_config;
	s->insn_bits	= pci1723_dio_insn_bits;

	/* read DIO config */
	switch (inw(dev->iobase + PCI1723_DIGITAL_IO_PORT_MODE) & 0x03) {
	case 0x00:	/* low byte output, high byte output */
		s->io_bits = 0xFFFF;
		break;
	case 0x01:	/* low byte input, high byte output */
		s->io_bits = 0xFF00;
		break;
	case 0x02:	/* low byte output, high byte input */
		s->io_bits = 0x00FF;
		break;
	case 0x03:	/* low byte input, high byte input */
		s->io_bits = 0x0000;
		break;
	}
	/* read DIO port state */
	s->state = inw(dev->iobase + PCI1723_READ_DIGITAL_INPUT_DATA);

	pci1723_reset(dev);

	return 0;
}

static void pci1723_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		pci1723_reset(dev);
	comedi_pci_disable(dev);
}

static struct comedi_driver adv_pci1723_driver = {
	.driver_name	= "adv_pci1723",
	.module		= THIS_MODULE,
	.auto_attach	= pci1723_auto_attach,
	.detach		= pci1723_detach,
};

static int adv_pci1723_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adv_pci1723_driver,
				      id->driver_data);
}

static const struct pci_device_id adv_pci1723_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADVANTECH, 0x1723) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adv_pci1723_pci_table);

static struct pci_driver adv_pci1723_pci_driver = {
	.name		= "adv_pci1723",
	.id_table	= adv_pci1723_pci_table,
	.probe		= adv_pci1723_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adv_pci1723_driver, adv_pci1723_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

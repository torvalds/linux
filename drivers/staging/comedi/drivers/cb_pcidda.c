/*
 * comedi/drivers/cb_pcidda.c
 * Driver for the ComputerBoards / MeasurementComputing PCI-DDA series.
 *
 * Copyright (C) 2001 Ivan Martinez <ivanmr@altavista.com>
 * Copyright (C) 2001 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Driver: cb_pcidda
 * Description: MeasurementComputing PCI-DDA series
 * Devices: (Measurement Computing) PCI-DDA08/12 [pci-dda08/12]
 *	    (Measurement Computing) PCI-DDA04/12 [pci-dda04/12]
 *	    (Measurement Computing) PCI-DDA02/12 [pci-dda02/12]
 *	    (Measurement Computing) PCI-DDA08/16 [pci-dda08/16]
 *	    (Measurement Computing) PCI-DDA04/16 [pci-dda04/16]
 *	    (Measurement Computing) PCI-DDA02/16 [pci-dda02/16]
 * Author: Ivan Martinez <ivanmr@altavista.com>
 *	   Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: works
 *
 * Configuration options: not applicable, uses PCI auto config
 *
 * Only simple analog output writing is supported.
 */

#include "../comedidev.h"

#include "comedi_fc.h"
#include "8255.h"

/*
 * ComputerBoards PCI Device ID's supported by this driver
 */
#define PCI_DEVICE_ID_DDA02_12		0x0020
#define PCI_DEVICE_ID_DDA04_12		0x0021
#define PCI_DEVICE_ID_DDA08_12		0x0022
#define PCI_DEVICE_ID_DDA02_16		0x0023
#define PCI_DEVICE_ID_DDA04_16		0x0024
#define PCI_DEVICE_ID_DDA08_16		0x0025

#define EEPROM_SIZE	128	/*  number of entries in eeprom */
/* maximum number of ao channels for supported boards */
#define MAX_AO_CHANNELS 8

/* Digital I/O registers */
#define CB_DDA_DIO0_8255_BASE		0x00
#define CB_DDA_DIO1_8255_BASE		0x04

/* DAC registers */
#define CB_DDA_DA_CTRL_REG		0x00	   /* D/A Control Register  */
#define CB_DDA_DA_CTRL_SU		(1 << 0)   /*  Simultaneous update  */
#define CB_DDA_DA_CTRL_EN		(1 << 1)   /*  Enable specified DAC */
#define CB_DDA_DA_CTRL_DAC(x)		((x) << 2) /*  Specify DAC channel  */
#define CB_DDA_DA_CTRL_RANGE2V5		(0 << 6)   /*  2.5V range           */
#define CB_DDA_DA_CTRL_RANGE5V		(2 << 6)   /*  5V range             */
#define CB_DDA_DA_CTRL_RANGE10V		(3 << 6)   /*  10V range            */
#define CB_DDA_DA_CTRL_UNIP		(1 << 8)   /*  Unipolar range       */

#define DACALIBRATION1	4	/*  D/A CALIBRATION REGISTER 1 */
/* write bits */
/* serial data input for eeprom, caldacs, reference dac */
#define SERIAL_IN_BIT   0x1
#define	CAL_CHANNEL_MASK	(0x7 << 1)
#define	CAL_CHANNEL_BITS(channel)	(((channel) << 1) & CAL_CHANNEL_MASK)
/* read bits */
#define	CAL_COUNTER_MASK	0x1f
/* calibration counter overflow status bit */
#define CAL_COUNTER_OVERFLOW_BIT        0x20
/* analog output is less than reference dac voltage */
#define AO_BELOW_REF_BIT        0x40
#define	SERIAL_OUT_BIT	0x80	/*  serial data out, for reading from eeprom */

#define DACALIBRATION2	6	/*  D/A CALIBRATION REGISTER 2 */
#define	SELECT_EEPROM_BIT	0x1	/*  send serial data in to eeprom */
/* don't send serial data to MAX542 reference dac */
#define DESELECT_REF_DAC_BIT    0x2
/* don't send serial data to caldac n */
#define DESELECT_CALDAC_BIT(n)  (0x4 << (n))
/* manual says to set this bit with no explanation */
#define DUMMY_BIT       0x40

#define CB_DDA_DA_DATA_REG(x)		(0x08 + ((x) * 2))

/* Offsets for the caldac channels */
#define CB_DDA_CALDAC_FINE_GAIN		0
#define CB_DDA_CALDAC_COURSE_GAIN	1
#define CB_DDA_CALDAC_COURSE_OFFSET	2
#define CB_DDA_CALDAC_FINE_OFFSET	3

static const struct comedi_lrange cb_pcidda_ranges = {
	6, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5)
	}
};

struct cb_pcidda_board {
	const char *name;
	unsigned short device_id;
	int ao_chans;
	int ao_bits;
};

static const struct cb_pcidda_board cb_pcidda_boards[] = {
	{
		.name		= "pci-dda02/12",
		.device_id	= PCI_DEVICE_ID_DDA02_12,
		.ao_chans	= 2,
		.ao_bits	= 12,
	}, {
		.name		= "pci-dda04/12",
		.device_id	= PCI_DEVICE_ID_DDA04_12,
		.ao_chans	= 4,
		.ao_bits	= 12,
	}, {
		.name		= "pci-dda08/12",
		.device_id	= PCI_DEVICE_ID_DDA08_12,
		.ao_chans	= 8,
		.ao_bits	= 12,
	}, {
		.name		= "pci-dda02/16",
		.device_id	= PCI_DEVICE_ID_DDA02_16,
		.ao_chans	= 2,
		.ao_bits	= 16,
	}, {
		.name		= "pci-dda04/16",
		.device_id	= PCI_DEVICE_ID_DDA04_16,
		.ao_chans	= 4,
		.ao_bits	= 16,
	}, {
		.name		= "pci-dda08/16",
		.device_id	= PCI_DEVICE_ID_DDA08_16,
		.ao_chans	= 8,
		.ao_bits	= 16,
	},
};

struct cb_pcidda_private {
	/* bits last written to da calibration register 1 */
	unsigned int dac_cal1_bits;
	/* current range settings for output channels */
	unsigned int ao_range[MAX_AO_CHANNELS];
	u16 eeprom_data[EEPROM_SIZE];	/*  software copy of board's eeprom */
};

/* lowlevel read from eeprom */
static unsigned int cb_pcidda_serial_in(struct comedi_device *dev)
{
	unsigned int value = 0;
	int i;
	const int value_width = 16;	/*  number of bits wide values are */

	for (i = 1; i <= value_width; i++) {
		/*  read bits most significant bit first */
		if (inw_p(dev->iobase + DACALIBRATION1) & SERIAL_OUT_BIT)
			value |= 1 << (value_width - i);
	}

	return value;
}

/* lowlevel write to eeprom/dac */
static void cb_pcidda_serial_out(struct comedi_device *dev, unsigned int value,
				 unsigned int num_bits)
{
	struct cb_pcidda_private *devpriv = dev->private;
	int i;

	for (i = 1; i <= num_bits; i++) {
		/*  send bits most significant bit first */
		if (value & (1 << (num_bits - i)))
			devpriv->dac_cal1_bits |= SERIAL_IN_BIT;
		else
			devpriv->dac_cal1_bits &= ~SERIAL_IN_BIT;
		outw_p(devpriv->dac_cal1_bits, dev->iobase + DACALIBRATION1);
	}
}

/* reads a 16 bit value from board's eeprom */
static unsigned int cb_pcidda_read_eeprom(struct comedi_device *dev,
					  unsigned int address)
{
	unsigned int i;
	unsigned int cal2_bits;
	unsigned int value;
	/* one caldac for every two dac channels */
	const int max_num_caldacs = 4;
	/* bits to send to tell eeprom we want to read */
	const int read_instruction = 0x6;
	const int instruction_length = 3;
	const int address_length = 8;

	/*  send serial output stream to eeprom */
	cal2_bits = SELECT_EEPROM_BIT | DESELECT_REF_DAC_BIT | DUMMY_BIT;
	/*  deactivate caldacs (one caldac for every two channels) */
	for (i = 0; i < max_num_caldacs; i++)
		cal2_bits |= DESELECT_CALDAC_BIT(i);
	outw_p(cal2_bits, dev->iobase + DACALIBRATION2);

	/*  tell eeprom we want to read */
	cb_pcidda_serial_out(dev, read_instruction, instruction_length);
	/*  send address we want to read from */
	cb_pcidda_serial_out(dev, address, address_length);

	value = cb_pcidda_serial_in(dev);

	/*  deactivate eeprom */
	cal2_bits &= ~SELECT_EEPROM_BIT;
	outw_p(cal2_bits, dev->iobase + DACALIBRATION2);

	return value;
}

/* writes to 8 bit calibration dacs */
static void cb_pcidda_write_caldac(struct comedi_device *dev,
				   unsigned int caldac, unsigned int channel,
				   unsigned int value)
{
	unsigned int cal2_bits;
	unsigned int i;
	/* caldacs use 3 bit channel specification */
	const int num_channel_bits = 3;
	const int num_caldac_bits = 8;	/*  8 bit calibration dacs */
	/* one caldac for every two dac channels */
	const int max_num_caldacs = 4;

	/* write 3 bit channel */
	cb_pcidda_serial_out(dev, channel, num_channel_bits);
	/*  write 8 bit caldac value */
	cb_pcidda_serial_out(dev, value, num_caldac_bits);

/*
* latch stream into appropriate caldac deselect reference dac
*/
	cal2_bits = DESELECT_REF_DAC_BIT | DUMMY_BIT;
	/*  deactivate caldacs (one caldac for every two channels) */
	for (i = 0; i < max_num_caldacs; i++)
		cal2_bits |= DESELECT_CALDAC_BIT(i);
	/*  activate the caldac we want */
	cal2_bits &= ~DESELECT_CALDAC_BIT(caldac);
	outw_p(cal2_bits, dev->iobase + DACALIBRATION2);
	/*  deactivate caldac */
	cal2_bits |= DESELECT_CALDAC_BIT(caldac);
	outw_p(cal2_bits, dev->iobase + DACALIBRATION2);
}

/* set caldacs to eeprom values for given channel and range */
static void cb_pcidda_calibrate(struct comedi_device *dev, unsigned int channel,
				unsigned int range)
{
	struct cb_pcidda_private *devpriv = dev->private;
	unsigned int caldac = channel / 2;	/* two caldacs per channel */
	unsigned int chan = 4 * (channel % 2);	/* caldac channel base */
	unsigned int index = 2 * range + 12 * channel;
	unsigned int offset;
	unsigned int gain;

	/* save range so we can tell when we need to readjust calibration */
	devpriv->ao_range[channel] = range;

	/* get values from eeprom data */
	offset = devpriv->eeprom_data[0x7 + index];
	gain = devpriv->eeprom_data[0x8 + index];

	/* set caldacs */
	cb_pcidda_write_caldac(dev, caldac, chan + CB_DDA_CALDAC_COURSE_OFFSET,
			       (offset >> 8) & 0xff);
	cb_pcidda_write_caldac(dev, caldac, chan + CB_DDA_CALDAC_FINE_OFFSET,
			       offset & 0xff);
	cb_pcidda_write_caldac(dev, caldac, chan + CB_DDA_CALDAC_COURSE_GAIN,
			       (gain >> 8) & 0xff);
	cb_pcidda_write_caldac(dev, caldac, chan + CB_DDA_CALDAC_FINE_GAIN,
			       gain & 0xff);
}

static int cb_pcidda_ao_insn_write(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct cb_pcidda_private *devpriv = dev->private;
	unsigned int channel = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int ctrl;

	if (range != devpriv->ao_range[channel])
		cb_pcidda_calibrate(dev, channel, range);

	ctrl = CB_DDA_DA_CTRL_EN | CB_DDA_DA_CTRL_DAC(channel);

	switch (range) {
	case 0:
	case 3:
		ctrl |= CB_DDA_DA_CTRL_RANGE10V;
		break;
	case 1:
	case 4:
		ctrl |= CB_DDA_DA_CTRL_RANGE5V;
		break;
	case 2:
	case 5:
		ctrl |= CB_DDA_DA_CTRL_RANGE2V5;
		break;
	}

	if (range > 2)
		ctrl |= CB_DDA_DA_CTRL_UNIP;

	outw(ctrl, dev->iobase + CB_DDA_DA_CTRL_REG);

	outw(data[0], dev->iobase + CB_DDA_DA_DATA_REG(channel));

	return insn->n;
}

static const void *cb_pcidda_find_boardinfo(struct comedi_device *dev,
					    struct pci_dev *pcidev)
{
	const struct cb_pcidda_board *thisboard;
	int i;

	for (i = 0; i < ARRAY_SIZE(cb_pcidda_boards); i++) {
		thisboard = &cb_pcidda_boards[i];
		if (thisboard->device_id != pcidev->device)
			return thisboard;
	}
	return NULL;
}

static int __devinit cb_pcidda_auto_attach(struct comedi_device *dev,
					   unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct cb_pcidda_board *thisboard;
	struct cb_pcidda_private *devpriv;
	struct comedi_subdevice *s;
	unsigned long iobase_8255;
	int i;
	int ret;

	thisboard = cb_pcidda_find_boardinfo(dev, pcidev);
	if (!thisboard)
		return -ENODEV;
	dev->board_ptr = thisboard;
	dev->board_name = thisboard->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 3);
	iobase_8255 = pci_resource_start(pcidev, 2);

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = thisboard->ao_chans;
	s->maxdata = (1 << thisboard->ao_bits) - 1;
	s->range_table = &cb_pcidda_ranges;
	s->insn_write = cb_pcidda_ao_insn_write;

	/* two 8255 digital io subdevices */
	for (i = 0; i < 2; i++) {
		s = &dev->subdevices[1 + i];
		ret = subdev_8255_init(dev, s, NULL, iobase_8255 + (i * 4));
		if (ret)
			return ret;
	}

	/* Read the caldac eeprom data */
	for (i = 0; i < EEPROM_SIZE; i++)
		devpriv->eeprom_data[i] = cb_pcidda_read_eeprom(dev, i);

	/*  set calibrations dacs */
	for (i = 0; i < thisboard->ao_chans; i++)
		cb_pcidda_calibrate(dev, i, devpriv->ao_range[i]);

	dev_info(dev->class_dev, "%s attached\n", dev->board_name);

	return 0;
}

static void cb_pcidda_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->subdevices) {
		subdev_8255_cleanup(dev, &dev->subdevices[1]);
		subdev_8255_cleanup(dev, &dev->subdevices[2]);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver cb_pcidda_driver = {
	.driver_name	= "cb_pcidda",
	.module		= THIS_MODULE,
	.auto_attach	= cb_pcidda_auto_attach,
	.detach		= cb_pcidda_detach,
};

static int __devinit cb_pcidda_pci_probe(struct pci_dev *dev,
					 const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &cb_pcidda_driver);
}

static void __devexit cb_pcidda_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(cb_pcidda_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_DDA02_12) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_DDA04_12) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_DDA08_12) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_DDA02_16) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_DDA04_16) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, PCI_DEVICE_ID_DDA08_16) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcidda_pci_table);

static struct pci_driver cb_pcidda_pci_driver = {
	.name		= "cb_pcidda",
	.id_table	= cb_pcidda_pci_table,
	.probe		= cb_pcidda_pci_probe,
	.remove		= __devexit_p(cb_pcidda_pci_remove),
};
module_comedi_pci_driver(cb_pcidda_driver, cb_pcidda_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

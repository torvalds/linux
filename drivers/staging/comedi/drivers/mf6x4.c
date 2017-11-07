// SPDX-License-Identifier: GPL-2.0+
/*
 *  comedi/drivers/mf6x4.c
 *  Driver for Humusoft MF634 and MF624 Data acquisition cards
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */
/*
 * Driver: mf6x4
 * Description: Humusoft MF634 and MF624 Data acquisition card driver
 * Devices: [Humusoft] MF634 (mf634), MF624 (mf624)
 * Author: Rostislav Lisovy <lisovy@gmail.com>
 * Status: works
 * Updated:
 * Configuration Options: none
 */

#include <linux/module.h>
#include <linux/delay.h>

#include "../comedi_pci.h"

/* Registers present in BAR0 memory region */
#define MF624_GPIOC_REG		0x54

#define MF6X4_GPIOC_EOLC	BIT(17)	/* End Of Last Conversion */
#define MF6X4_GPIOC_LDAC	BIT(23)	/* Load DACs */
#define MF6X4_GPIOC_DACEN	BIT(26)

/* BAR1 registers */
#define MF6X4_ADDATA_REG	0x00
#define MF6X4_ADCTRL_REG	0x00
#define MF6X4_ADCTRL_CHAN(x)	BIT(chan)
#define MF6X4_DIN_REG		0x10
#define MF6X4_DIN_MASK		0xff
#define MF6X4_DOUT_REG		0x10
#define MF6X4_ADSTART_REG	0x20
#define MF6X4_DAC_REG(x)	(0x20 + ((x) * 2))

/* BAR2 registers */
#define MF634_GPIOC_REG		0x68

enum mf6x4_boardid {
	BOARD_MF634,
	BOARD_MF624,
};

struct mf6x4_board {
	const char *name;
	/* We need to keep track of the order of BARs used by the cards */
	unsigned int bar_nums[3];
};

static const struct mf6x4_board mf6x4_boards[] = {
	[BOARD_MF634] = {
		.name           = "mf634",
		.bar_nums	= {0, 2, 3},
	},
	[BOARD_MF624] = {
		.name           = "mf624",
		.bar_nums	= {0, 2, 4},
	},
};

struct mf6x4_private {
	/*
	 * Documentation for both MF634 and MF624 describes registers
	 * present in BAR0, 1 and 2 regions.
	 * The real (i.e. in HW) BAR numbers are different for MF624
	 * and MF634 yet we will call them 0, 1, 2
	 */
	void __iomem *bar0_mem;
	void __iomem *bar2_mem;

	/*
	 * This configuration register has the same function and fields
	 * for both cards however it lies in different BARs on different
	 * offsets -- this variable makes the access easier
	 */
	void __iomem *gpioc_reg;
};

static int mf6x4_di_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	data[1] = ioread16(dev->mmio + MF6X4_DIN_REG) & MF6X4_DIN_MASK;

	return insn->n;
}

static int mf6x4_do_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		iowrite16(s->state, dev->mmio + MF6X4_DOUT_REG);

	data[1] = s->state;

	return insn->n;
}

static int mf6x4_ai_eoc(struct comedi_device *dev,
			struct comedi_subdevice *s,
			struct comedi_insn *insn,
			unsigned long context)
{
	struct mf6x4_private *devpriv = dev->private;
	unsigned int status;

	status = ioread32(devpriv->gpioc_reg);
	if (status & MF6X4_GPIOC_EOLC)
		return 0;
	return -EBUSY;
}

static int mf6x4_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int d;
	int ret;
	int i;

	/* Set the ADC channel number in the scan list */
	iowrite16(MF6X4_ADCTRL_CHAN(chan), dev->mmio + MF6X4_ADCTRL_REG);

	for (i = 0; i < insn->n; i++) {
		/* Trigger ADC conversion by reading ADSTART */
		ioread16(dev->mmio + MF6X4_ADSTART_REG);

		ret = comedi_timeout(dev, s, insn, mf6x4_ai_eoc, 0);
		if (ret)
			return ret;

		/* Read the actual value */
		d = ioread16(dev->mmio + MF6X4_ADDATA_REG);
		d &= s->maxdata;
		/* munge the 2's complement data to offset binary */
		data[i] = comedi_offset_munge(s, d);
	}

	iowrite16(0x0, dev->mmio + MF6X4_ADCTRL_REG);

	return insn->n;
}

static int mf6x4_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct mf6x4_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	unsigned int gpioc;
	int i;

	/* Enable instantaneous update of converters outputs + Enable DACs */
	gpioc = ioread32(devpriv->gpioc_reg);
	iowrite32((gpioc & ~MF6X4_GPIOC_LDAC) | MF6X4_GPIOC_DACEN,
		  devpriv->gpioc_reg);

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		iowrite16(val, dev->mmio + MF6X4_DAC_REG(chan));
	}
	s->readback[chan] = val;

	return insn->n;
}

static int mf6x4_auto_attach(struct comedi_device *dev, unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct mf6x4_board *board = NULL;
	struct mf6x4_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	if (context < ARRAY_SIZE(mf6x4_boards))
		board = &mf6x4_boards[context];
	else
		return -ENODEV;

	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	devpriv->bar0_mem = pci_ioremap_bar(pcidev, board->bar_nums[0]);
	if (!devpriv->bar0_mem)
		return -ENODEV;

	dev->mmio = pci_ioremap_bar(pcidev, board->bar_nums[1]);
	if (!dev->mmio)
		return -ENODEV;

	devpriv->bar2_mem = pci_ioremap_bar(pcidev, board->bar_nums[2]);
	if (!devpriv->bar2_mem)
		return -ENODEV;

	if (board == &mf6x4_boards[BOARD_MF634])
		devpriv->gpioc_reg = devpriv->bar2_mem + MF634_GPIOC_REG;
	else
		devpriv->gpioc_reg = devpriv->bar0_mem + MF624_GPIOC_REG;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	s->n_chan	= 8;
	s->maxdata	= 0x3fff;
	s->range_table	= &range_bipolar10;
	s->insn_read	= mf6x4_ai_insn_read;

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 0x3fff;
	s->range_table	= &range_bipolar10;
	s->insn_write	= mf6x4_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= mf6x4_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= mf6x4_do_insn_bits;

	return 0;
}

static void mf6x4_detach(struct comedi_device *dev)
{
	struct mf6x4_private *devpriv = dev->private;

	if (devpriv) {
		if (devpriv->bar0_mem)
			iounmap(devpriv->bar0_mem);
		if (devpriv->bar2_mem)
			iounmap(devpriv->bar2_mem);
	}
	comedi_pci_detach(dev);
}

static struct comedi_driver mf6x4_driver = {
	.driver_name    = "mf6x4",
	.module         = THIS_MODULE,
	.auto_attach    = mf6x4_auto_attach,
	.detach         = mf6x4_detach,
};

static int mf6x4_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &mf6x4_driver, id->driver_data);
}

static const struct pci_device_id mf6x4_pci_table[] = {
	{ PCI_VDEVICE(HUMUSOFT, 0x0634), BOARD_MF634 },
	{ PCI_VDEVICE(HUMUSOFT, 0x0624), BOARD_MF624 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, mf6x4_pci_table);

static struct pci_driver mf6x4_pci_driver = {
	.name           = "mf6x4",
	.id_table       = mf6x4_pci_table,
	.probe          = mf6x4_pci_probe,
	.remove         = comedi_pci_auto_unconfig,
};

module_comedi_pci_driver(mf6x4_driver, mf6x4_pci_driver);

MODULE_AUTHOR("Rostislav Lisovy <lisovy@gmail.com>");
MODULE_DESCRIPTION("Comedi MF634 and MF624 DAQ cards driver");
MODULE_LICENSE("GPL");

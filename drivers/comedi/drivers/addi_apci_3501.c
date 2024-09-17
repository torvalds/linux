// SPDX-License-Identifier: GPL-2.0+
/*
 * addi_apci_3501.c
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 * Project manager: Eric Stolz
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 */

/*
 * Driver: addi_apci_3501
 * Description: ADDI-DATA APCI-3501 Analog output board
 * Devices: [ADDI-DATA] APCI-3501 (addi_apci_3501)
 * Author: H Hartley Sweeten <hsweeten@visionengravers.com>
 * Updated: Mon, 20 Jun 2016 10:57:01 -0700
 * Status: untested
 *
 * Configuration Options: not applicable, uses comedi PCI auto config
 *
 * This board has the following features:
 *   - 4 or 8 analog output channels
 *   - 2 optically isolated digital inputs
 *   - 2 optically isolated digital outputs
 *   - 1 12-bit watchdog/timer
 *
 * There are 2 versions of the APCI-3501:
 *   - APCI-3501-4  4 analog output channels
 *   - APCI-3501-8  8 analog output channels
 *
 * These boards use the same PCI Vendor/Device IDs. The number of output
 * channels used by this driver is determined by reading the EEPROM on
 * the board.
 *
 * The watchdog/timer subdevice is not currently supported.
 */

#include <linux/module.h>
#include <linux/comedi/comedi_pci.h>

#include "amcc_s5933.h"

/*
 * PCI bar 1 register I/O map
 */
#define APCI3501_AO_CTRL_STATUS_REG		0x00
#define APCI3501_AO_CTRL_BIPOLAR		BIT(0)
#define APCI3501_AO_STATUS_READY		BIT(8)
#define APCI3501_AO_DATA_REG			0x04
#define APCI3501_AO_DATA_CHAN(x)		((x) << 0)
#define APCI3501_AO_DATA_VAL(x)			((x) << 8)
#define APCI3501_AO_DATA_BIPOLAR		BIT(31)
#define APCI3501_AO_TRIG_SCS_REG		0x08
#define APCI3501_TIMER_BASE			0x20
#define APCI3501_DO_REG				0x40
#define APCI3501_DI_REG				0x50

/*
 * AMCC S5933 NVRAM
 */
#define NVRAM_USER_DATA_START	0x100

#define NVCMD_BEGIN_READ	(0x7 << 5)
#define NVCMD_LOAD_LOW		(0x4 << 5)
#define NVCMD_LOAD_HIGH		(0x5 << 5)

/*
 * Function types stored in the eeprom
 */
#define EEPROM_DIGITALINPUT		0
#define EEPROM_DIGITALOUTPUT		1
#define EEPROM_ANALOGINPUT		2
#define EEPROM_ANALOGOUTPUT		3
#define EEPROM_TIMER			4
#define EEPROM_WATCHDOG			5
#define EEPROM_TIMER_WATCHDOG_COUNTER	10

struct apci3501_private {
	unsigned long amcc;
	unsigned char timer_mode;
};

static const struct comedi_lrange apci3501_ao_range = {
	2, {
		BIP_RANGE(10),
		UNI_RANGE(10)
	}
};

static int apci3501_wait_for_dac(struct comedi_device *dev)
{
	unsigned int status;

	do {
		status = inl(dev->iobase + APCI3501_AO_CTRL_STATUS_REG);
	} while (!(status & APCI3501_AO_STATUS_READY));

	return 0;
}

static int apci3501_ao_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int cfg = APCI3501_AO_DATA_CHAN(chan);
	int ret;
	int i;

	/*
	 * All analog output channels have the same output range.
	 *	14-bit bipolar: 0-10V
	 *	13-bit unipolar: +/-10V
	 * Changing the range of one channel changes all of them!
	 */
	if (range) {
		outl(0, dev->iobase + APCI3501_AO_CTRL_STATUS_REG);
	} else {
		cfg |= APCI3501_AO_DATA_BIPOLAR;
		outl(APCI3501_AO_CTRL_BIPOLAR,
		     dev->iobase + APCI3501_AO_CTRL_STATUS_REG);
	}

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		if (range == 1) {
			if (data[i] > 0x1fff) {
				dev_err(dev->class_dev,
					"Unipolar resolution is only 13-bits\n");
				return -EINVAL;
			}
		}

		ret = apci3501_wait_for_dac(dev);
		if (ret)
			return ret;

		outl(cfg | APCI3501_AO_DATA_VAL(val),
		     dev->iobase + APCI3501_AO_DATA_REG);

		s->readback[chan] = val;
	}

	return insn->n;
}

static int apci3501_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = inl(dev->iobase + APCI3501_DI_REG) & 0x3;

	return insn->n;
}

static int apci3501_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	s->state = inl(dev->iobase + APCI3501_DO_REG);

	if (comedi_dio_update_state(s, data))
		outl(s->state, dev->iobase + APCI3501_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static void apci3501_eeprom_wait(unsigned long iobase)
{
	unsigned char val;

	do {
		val = inb(iobase + AMCC_OP_REG_MCSR_NVCMD);
	} while (val & 0x80);
}

static unsigned short apci3501_eeprom_readw(unsigned long iobase,
					    unsigned short addr)
{
	unsigned short val = 0;
	unsigned char tmp;
	unsigned char i;

	/* Add the offset to the start of the user data */
	addr += NVRAM_USER_DATA_START;

	for (i = 0; i < 2; i++) {
		/* Load the low 8 bit address */
		outb(NVCMD_LOAD_LOW, iobase + AMCC_OP_REG_MCSR_NVCMD);
		apci3501_eeprom_wait(iobase);
		outb((addr + i) & 0xff, iobase + AMCC_OP_REG_MCSR_NVDATA);
		apci3501_eeprom_wait(iobase);

		/* Load the high 8 bit address */
		outb(NVCMD_LOAD_HIGH, iobase + AMCC_OP_REG_MCSR_NVCMD);
		apci3501_eeprom_wait(iobase);
		outb(((addr + i) >> 8) & 0xff,
		     iobase + AMCC_OP_REG_MCSR_NVDATA);
		apci3501_eeprom_wait(iobase);

		/* Read the eeprom data byte */
		outb(NVCMD_BEGIN_READ, iobase + AMCC_OP_REG_MCSR_NVCMD);
		apci3501_eeprom_wait(iobase);
		tmp = inb(iobase + AMCC_OP_REG_MCSR_NVDATA);
		apci3501_eeprom_wait(iobase);

		if (i == 0)
			val |= tmp;
		else
			val |= (tmp << 8);
	}

	return val;
}

static int apci3501_eeprom_get_ao_n_chan(struct comedi_device *dev)
{
	struct apci3501_private *devpriv = dev->private;
	unsigned char nfuncs;
	int i;

	nfuncs = apci3501_eeprom_readw(devpriv->amcc, 10) & 0xff;

	/* Read functionality details */
	for (i = 0; i < nfuncs; i++) {
		unsigned short offset = i * 4;
		unsigned short addr;
		unsigned char func;
		unsigned short val;

		func = apci3501_eeprom_readw(devpriv->amcc, 12 + offset) & 0x3f;
		addr = apci3501_eeprom_readw(devpriv->amcc, 14 + offset);

		if (func == EEPROM_ANALOGOUTPUT) {
			val = apci3501_eeprom_readw(devpriv->amcc, addr + 10);
			return (val >> 4) & 0x3ff;
		}
	}
	return 0;
}

static int apci3501_eeprom_insn_read(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct apci3501_private *devpriv = dev->private;
	unsigned short addr = CR_CHAN(insn->chanspec);
	unsigned int val;
	unsigned int i;

	if (insn->n) {
		/* No point reading the same EEPROM location more than once. */
		val = apci3501_eeprom_readw(devpriv->amcc, 2 * addr);
		for (i = 0; i < insn->n; i++)
			data[i] = val;
	}

	return insn->n;
}

static int apci3501_reset(struct comedi_device *dev)
{
	unsigned int val;
	int chan;
	int ret;

	/* Reset all digital outputs to "0" */
	outl(0x0, dev->iobase + APCI3501_DO_REG);

	/* Default all analog outputs to 0V (bipolar) */
	outl(APCI3501_AO_CTRL_BIPOLAR,
	     dev->iobase + APCI3501_AO_CTRL_STATUS_REG);
	val = APCI3501_AO_DATA_BIPOLAR | APCI3501_AO_DATA_VAL(0);

	/* Set all analog output channels */
	for (chan = 0; chan < 8; chan++) {
		ret = apci3501_wait_for_dac(dev);
		if (ret) {
			dev_warn(dev->class_dev,
				 "%s: DAC not-ready for channel %i\n",
				 __func__, chan);
		} else {
			outl(val | APCI3501_AO_DATA_CHAN(chan),
			     dev->iobase + APCI3501_AO_DATA_REG);
		}
	}

	return 0;
}

static int apci3501_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct apci3501_private *devpriv;
	struct comedi_subdevice *s;
	int ao_n_chan;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv->amcc = pci_resource_start(pcidev, 0);
	dev->iobase = pci_resource_start(pcidev, 1);

	ao_n_chan = apci3501_eeprom_get_ao_n_chan(dev);

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	/* Initialize the analog output subdevice */
	s = &dev->subdevices[0];
	if (ao_n_chan) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan	= ao_n_chan;
		s->maxdata	= 0x3fff;
		s->range_table	= &apci3501_ao_range;
		s->insn_write	= apci3501_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Initialize the digital input subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 2;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci3501_di_insn_bits;

	/* Initialize the digital output subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 2;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci3501_do_insn_bits;

	/* Timer/Watchdog subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_UNUSED;

	/* Initialize the eeprom subdevice */
	s = &dev->subdevices[4];
	s->type		= COMEDI_SUBD_MEMORY;
	s->subdev_flags	= SDF_READABLE | SDF_INTERNAL;
	s->n_chan	= 256;
	s->maxdata	= 0xffff;
	s->insn_read	= apci3501_eeprom_insn_read;

	apci3501_reset(dev);
	return 0;
}

static void apci3501_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		apci3501_reset(dev);
	comedi_pci_detach(dev);
}

static struct comedi_driver apci3501_driver = {
	.driver_name	= "addi_apci_3501",
	.module		= THIS_MODULE,
	.auto_attach	= apci3501_auto_attach,
	.detach		= apci3501_detach,
};

static int apci3501_pci_probe(struct pci_dev *dev,
			      const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &apci3501_driver, id->driver_data);
}

static const struct pci_device_id apci3501_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x3001) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci3501_pci_table);

static struct pci_driver apci3501_pci_driver = {
	.name		= "addi_apci_3501",
	.id_table	= apci3501_pci_table,
	.probe		= apci3501_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(apci3501_driver, apci3501_pci_driver);

MODULE_DESCRIPTION("ADDI-DATA APCI-3501 Analog output board");
MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_LICENSE("GPL");

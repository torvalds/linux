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
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

/*
 * PCI bar 1 register I/O map
 */
#define APCI3501_AO_CTRL_STATUS_REG		0x00
#define APCI3501_AO_CTRL_BIPOLAR		(1 << 0)
#define APCI3501_AO_STATUS_READY		(1 << 8)
#define APCI3501_AO_DATA_REG			0x04
#define APCI3501_AO_DATA_CHAN(x)		((x) << 0)
#define APCI3501_AO_DATA_VAL(x)			((x) << 8)
#define APCI3501_AO_DATA_BIPOLAR		(1 << 31)
#define APCI3501_AO_TRIG_SCS_REG		0x08
#define APCI3501_TIMER_SYNC_REG			0x20
#define APCI3501_TIMER_RELOAD_REG		0x24
#define APCI3501_TIMER_TIMEBASE_REG		0x28
#define APCI3501_TIMER_CTRL_REG			0x2c
#define APCI3501_TIMER_STATUS_REG		0x30
#define APCI3501_TIMER_IRQ_REG			0x34
#define APCI3501_TIMER_WARN_RELOAD_REG		0x38
#define APCI3501_TIMER_WARN_TIMEBASE_REG	0x3c
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
	int i_IobaseAmcc;
	struct task_struct *tsk_Current;
	unsigned char b_TimerSelectMode;
};

static struct comedi_lrange apci3501_ao_range = {
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

#include "addi-data/hwdrv_apci3501.c"

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
	unsigned long iobase = devpriv->i_IobaseAmcc;
	unsigned char nfuncs;
	int i;

	nfuncs = apci3501_eeprom_readw(iobase, 10) & 0xff;

	/* Read functionality details */
	for (i = 0; i < nfuncs; i++) {
		unsigned short offset = i * 4;
		unsigned short addr;
		unsigned char func;
		unsigned short val;

		func = apci3501_eeprom_readw(iobase, 12 + offset) & 0x3f;
		addr = apci3501_eeprom_readw(iobase, 14 + offset);

		if (func == EEPROM_ANALOGOUTPUT) {
			val = apci3501_eeprom_readw(iobase, addr + 10);
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

	data[0] = apci3501_eeprom_readw(devpriv->i_IobaseAmcc, 2 * addr);

	return insn->n;
}

static irqreturn_t apci3501_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci3501_private *devpriv = dev->private;
	unsigned int ui_Timer_AOWatchdog;
	unsigned long ul_Command1;

	/*  Disable Interrupt */
	ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
	ul_Command1 = ul_Command1 & 0xFFFFF9FDul;
	outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);

	ui_Timer_AOWatchdog = inl(dev->iobase + APCI3501_TIMER_IRQ_REG) & 0x1;
	if ((!ui_Timer_AOWatchdog)) {
		dev_err(dev->class_dev, "IRQ from unknown source\n");
		return IRQ_NONE;
	}

	/* Enable Interrupt Send a signal to from kernel to user space */
	send_sig(SIGIO, devpriv->tsk_Current, 0);
	ul_Command1 = inl(dev->iobase + APCI3501_TIMER_CTRL_REG);
	ul_Command1 = (ul_Command1 & 0xFFFFF9FDul) | 1 << 1;
	outl(ul_Command1, dev->iobase + APCI3501_TIMER_CTRL_REG);
	inl(dev->iobase + APCI3501_TIMER_STATUS_REG);

	return IRQ_HANDLED;
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

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);

	ao_n_chan = apci3501_eeprom_get_ao_n_chan(dev);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci3501_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

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

	/* Initialize the timer/watchdog subdevice */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;
	s->insn_write = apci3501_write_insn_timer;
	s->insn_read = apci3501_read_insn_timer;
	s->insn_config = apci3501_config_insn_timer;

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
MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_LICENSE("GPL");

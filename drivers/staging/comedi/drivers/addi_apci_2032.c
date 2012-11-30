/*
 * addi_apci_2032.c
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * You should also find the complete GPL in the COPYING file accompanying
 * this source code.
 */

#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"

/*
 * PCI bar 1 I/O Register map
 */
#define APCI2032_DIGITAL_OP				0
#define APCI2032_DIGITAL_OP_RW				0
#define APCI2032_DIGITAL_OP_INTERRUPT			4
#define APCI2032_DIGITAL_OP_INTERRUPT_STATUS		8
#define APCI2032_DIGITAL_OP_IRQ				12

#define APCI2032_DIGITAL_OP_VCC_INTERRUPT_ENABLE	0x1
#define APCI2032_DIGITAL_OP_VCC_INTERRUPT_DISABLE	0xfffffffe
#define APCI2032_DIGITAL_OP_CC_INTERRUPT_ENABLE		0x2
#define APCI2032_DIGITAL_OP_CC_INTERRUPT_DISABLE	0xfffffffd

#define APCI2032_DIGITAL_OP_WATCHDOG			16
#define APCI2032_TCW_RELOAD_VALUE			4
#define APCI2032_TCW_TIMEBASE				8
#define APCI2032_TCW_PROG				12
#define APCI2032_TCW_TRIG_STATUS			16
#define APCI2032_TCW_IRQ				20

static unsigned int ui_InterruptData, ui_Type;

static int i_APCI2032_ConfigDigitalOutput(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	struct addi_private *devpriv = dev->private;
	unsigned int ul_Command = 0;

	devpriv->tsk_Current = current;

	if ((data[0] != 0) && (data[0] != 1)) {
		comedi_error(dev,
			"Not a valid Data !!! ,Data should be 1 or 0\n");
		return -EINVAL;
	}			/* if  ( (data[0]!=0) && (data[0]!=1) ) */
	if (data[0]) {
		devpriv->b_OutputMemoryStatus = ADDIDATA_ENABLE;
	}			/*  if  (data[0]) */
	else {
		devpriv->b_OutputMemoryStatus = ADDIDATA_DISABLE;
	}			/* else if  (data[0]) */

	if (data[1] == ADDIDATA_ENABLE) {
		ul_Command = ul_Command | 0x1;
	}			/* if  (data[1] == ADDIDATA_ENABLE) */
	else {
		ul_Command = ul_Command & 0xFFFFFFFE;
	}			/* elseif  (data[1] == ADDIDATA_ENABLE) */
	if (data[2] == ADDIDATA_ENABLE) {
		ul_Command = ul_Command | 0x2;
	}			/* if  (data[2] == ADDIDATA_ENABLE) */
	else {
		ul_Command = ul_Command & 0xFFFFFFFD;
	}			/* elseif  (data[2] == ADDIDATA_ENABLE) */
	outl(ul_Command, dev->iobase + APCI2032_DIGITAL_OP_INTERRUPT);
	ui_InterruptData = inl(dev->iobase + APCI2032_DIGITAL_OP_INTERRUPT);
	return insn->n;
}

static int apci2032_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	s->state = inl(dev->iobase + APCI2032_DIGITAL_OP_RW);
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outl(s->state, dev->iobase + APCI2032_DIGITAL_OP);
	}

	data[1] = s->state;

	return insn->n;
}

static int i_APCI2032_ConfigWatchdog(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	if (data[0] == 0) {
		/* Disable the watchdog */
		outl(0x0,
			dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_PROG);
		/* Loading the Reload value */
		outl(data[1],
			dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_RELOAD_VALUE);
	} else {
		printk("\nThe input parameters are wrong\n");
		return -EINVAL;
	}

	return insn->n;
}

static int i_APCI2032_StartStopWriteWatchdog(struct comedi_device *dev,
					     struct comedi_subdevice *s,
					     struct comedi_insn *insn,
					     unsigned int *data)
{
	switch (data[0]) {
	case 0:		/* stop the watchdog */
		outl(0x0, dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_PROG);	/* disable the watchdog */
		break;
	case 1:		/* start the watchdog */
		outl(0x0001,
			dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_PROG);
		break;
	case 2:		/* Software trigger */
		outl(0x0201,
			dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
			APCI2032_TCW_PROG);
		break;
	default:
		printk("\nSpecified functionality does not exist\n");
		return -EINVAL;
	}
	return insn->n;
}

static int i_APCI2032_ReadWatchdog(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	data[0] =
		inl(dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG +
		APCI2032_TCW_TRIG_STATUS) & 0x1;
	return insn->n;
}

static int i_APCI2032_ReadInterruptStatus(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	*data = ui_Type;
	return insn->n;
}
static void v_APCI2032_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct addi_private *devpriv = dev->private;
	unsigned int ui_DO;

	ui_DO = inl(dev->iobase + APCI2032_DIGITAL_OP_IRQ) & 0x1;	/* Check if VCC OR CC interrupt has occurred. */

	if (ui_DO == 0) {
		printk("\nInterrupt from unKnown source\n");
	}			/*  if(ui_DO==0) */
	if (ui_DO) {
		/*  Check for Digital Output interrupt Type - 1: Vcc interrupt 2: CC interrupt. */
		ui_Type =
			inl(dev->iobase +
			APCI2032_DIGITAL_OP_INTERRUPT_STATUS) & 0x3;
		outl(0x0,
			dev->iobase + APCI2032_DIGITAL_OP +
			APCI2032_DIGITAL_OP_INTERRUPT);
		if (ui_Type == 1) {
			/* Sends signal to user space */
			send_sig(SIGIO, devpriv->tsk_Current, 0);
		}		/*  if (ui_Type==1) */
		else {
			if (ui_Type == 2) {
				/*  Sends signal to user space */
				send_sig(SIGIO, devpriv->tsk_Current, 0);
			}	/* if (ui_Type==2) */
		}		/* else if (ui_Type==1) */
	}			/* if(ui_DO) */

	return;

}

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	v_APCI2032_Interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int apci2032_reset(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	devpriv->b_DigitalOutputRegister = 0;
	ui_Type = 0;
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP);
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP_INTERRUPT);
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_PROG);
	outl(0x0, dev->iobase + APCI2032_DIGITAL_OP_WATCHDOG + APCI2032_TCW_RELOAD_VALUE);

	return 0;
}

static int apci2032_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	dev->board_name = dev->driver->driver_name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 1);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/* Initialize the digital output subdevice */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 32;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_config = i_APCI2032_ConfigDigitalOutput;
	s->insn_bits = apci2032_do_insn_bits;
	s->insn_read = i_APCI2032_ReadInterruptStatus;

	/* Initialize the watchdog subdevice */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_TIMER;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 1;
	s->maxdata = 0;
	s->len_chanlist = 1;
	s->range_table = &range_digital;
	s->insn_write = i_APCI2032_StartStopWriteWatchdog;
	s->insn_read = i_APCI2032_ReadWatchdog;
	s->insn_config = i_APCI2032_ConfigWatchdog;

	apci2032_reset(dev);
	return 0;
}

static void apci2032_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			apci2032_reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci2032_driver = {
	.driver_name	= "addi_apci_2032",
	.module		= THIS_MODULE,
	.auto_attach	= apci2032_auto_attach,
	.detach		= apci2032_detach,
};

static int apci2032_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci2032_driver);
}

static void apci2032_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci2032_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1004) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci2032_pci_table);

static struct pci_driver apci2032_pci_driver = {
	.name		= "addi_apci_2032",
	.id_table	= apci2032_pci_table,
	.probe		= apci2032_pci_probe,
	.remove		= apci2032_pci_remove,
};
module_comedi_pci_driver(apci2032_driver, apci2032_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

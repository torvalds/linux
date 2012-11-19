/*
 * addi_apci_1032.c
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * You should also find the complete GPL in the COPYING file accompanying this
 * source code.
 */

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

/*
 * I/O Register Map
 */
#define APCI1032_DI_REG			0x00
#define APCI1032_MODE1_REG		0x04
#define APCI1032_MODE2_REG		0x08
#define APCI1032_STATUS_REG		0x0c
#define APCI1032_CTRL_REG		0x10
#define APCI1032_CTRL_INT_OR		(0 << 1)
#define APCI1032_CTRL_INT_AND		(1 << 1)
#define APCI1032_CTRL_INT_ENA		(1 << 2)

struct apci1032_private {
	unsigned long amcc_iobase;	/* base of AMCC I/O registers */
	unsigned int mode1;	/* rising-edge/high level channels */
	unsigned int mode2;	/* falling-edge/low level channels */
	unsigned int ctrl;	/* interrupt mode OR (edge) . AND (level) */
};

static int apci1032_reset(struct comedi_device *dev)
{
	/* disable the interrupts */
	outl(0x0, dev->iobase + APCI1032_CTRL_REG);
	/* Reset the interrupt status register */
	inl(dev->iobase + APCI1032_STATUS_REG);
	/* Disable the and/or interrupt */
	outl(0x0, dev->iobase + APCI1032_MODE1_REG);
	outl(0x0, dev->iobase + APCI1032_MODE2_REG);

	return 0;
}

/*
 * Change-Of-State (COS) interrupt configuration
 *
 * Channels 0 to 15 are interruptible. These channels can be configured
 * to generate interrupts based on AND/OR logic for the desired channels.
 *
 *	OR logic
 *		- reacts to rising or falling edges
 *		- interrupt is generated when any enabled channel
 *		  meet the desired interrupt condition
 *
 *	AND logic
 *		- reacts to changes in level of the selected inputs
 *		- interrupt is generated when all enabled channels
 *		  meet the desired interrupt condition
 *		- after an interrupt, a change in level must occur on
 *		  the selected inputs to release the IRQ logic
 *
 * The COS interrupt must be configured before it can be enabled.
 *
 *	data[0] : INSN_CONFIG_DIGITAL_TRIG
 *	data[1] : trigger number (= 0)
 *	data[2] : configuration operation:
 *	          COMEDI_DIGITAL_TRIG_DISABLE = no interrupts
 *	          COMEDI_DIGITAL_TRIG_ENABLE_EDGES = OR (edge) interrupts
 *	          COMEDI_DIGITAL_TRIG_ENABLE_LEVELS = AND (level) interrupts
 *	data[3] : left-shift for data[4] and data[5]
 *	data[4] : rising-edge/high level channels
 *	data[5] : falling-edge/low level channels
 */
static int apci1032_cos_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci1032_private *devpriv = dev->private;
	unsigned int shift, oldmask;

	switch (data[0]) {
	case INSN_CONFIG_DIGITAL_TRIG:
		if (data[1] != 0)
			return -EINVAL;
		shift = data[3];
		oldmask = (1U << shift) - 1;
		switch (data[2]) {
		case COMEDI_DIGITAL_TRIG_DISABLE:
			devpriv->ctrl = 0;
			devpriv->mode1 = 0;
			devpriv->mode2 = 0;
			apci1032_reset(dev);
			break;
		case COMEDI_DIGITAL_TRIG_ENABLE_EDGES:
			if (devpriv->ctrl != (APCI1032_CTRL_INT_ENA |
					      APCI1032_CTRL_INT_OR)) {
				/* switching to 'OR' mode */
				devpriv->ctrl = APCI1032_CTRL_INT_ENA |
						APCI1032_CTRL_INT_OR;
				/* wipe old channels */
				devpriv->mode1 = 0;
				devpriv->mode2 = 0;
			} else {
				/* preserve unspecified channels */
				devpriv->mode1 &= oldmask;
				devpriv->mode2 &= oldmask;
			}
			/* configure specified channels */
			devpriv->mode1 |= data[4] << shift;
			devpriv->mode2 |= data[5] << shift;
			break;
		case COMEDI_DIGITAL_TRIG_ENABLE_LEVELS:
			if (devpriv->ctrl != (APCI1032_CTRL_INT_ENA |
					      APCI1032_CTRL_INT_AND)) {
				/* switching to 'AND' mode */
				devpriv->ctrl = APCI1032_CTRL_INT_ENA |
						APCI1032_CTRL_INT_AND;
				/* wipe old channels */
				devpriv->mode1 = 0;
				devpriv->mode2 = 0;
			} else {
				/* preserve unspecified channels */
				devpriv->mode1 &= oldmask;
				devpriv->mode2 &= oldmask;
			}
			/* configure specified channels */
			devpriv->mode1 |= data[4] << shift;
			devpriv->mode2 |= data[5] << shift;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static int apci1032_cos_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	data[1] = s->state;

	return 0;
}

static int apci1032_cos_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, 1);
	err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: ignored */

	if (err)
		return 4;

	return 0;
}

/*
 * Change-Of-State (COS) 'do_cmd' operation
 *
 * Enable the COS interrupt as configured by apci1032_cos_insn_config().
 */
static int apci1032_cos_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct apci1032_private *devpriv = dev->private;

	if (!devpriv->ctrl) {
		dev_warn(dev->class_dev,
			"Interrupts disabled due to mode configuration!\n");
		return -EINVAL;
	}

	outl(devpriv->mode1, dev->iobase + APCI1032_MODE1_REG);
	outl(devpriv->mode2, dev->iobase + APCI1032_MODE2_REG);
	outl(devpriv->ctrl, dev->iobase + APCI1032_CTRL_REG);

	return 0;
}

static int apci1032_cos_cancel(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	return apci1032_reset(dev);
}

static irqreturn_t apci1032_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct apci1032_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int ctrl;

	/* check interrupt is from this device */
	if ((inl(devpriv->amcc_iobase + AMCC_OP_REG_INTCSR) &
	     INTCSR_INTR_ASSERTED) == 0)
		return IRQ_NONE;

	/* check interrupt is enabled */
	ctrl = inl(dev->iobase + APCI1032_CTRL_REG);
	if ((ctrl & APCI1032_CTRL_INT_ENA) == 0)
		return IRQ_HANDLED;

	/* disable the interrupt */
	outl(ctrl & ~APCI1032_CTRL_INT_ENA, dev->iobase + APCI1032_CTRL_REG);

	s->state = inl(dev->iobase + APCI1032_STATUS_REG) & 0xffff;
	comedi_buf_put(s->async, s->state);
	s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;
	comedi_event(dev, s);

	/* enable the interrupt */
	outl(ctrl, dev->iobase + APCI1032_CTRL_REG);

	return IRQ_HANDLED;
}

static int apci1032_di_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	data[1] = inl(dev->iobase + APCI1032_DI_REG);

	return insn->n;
}

static int __devinit apci1032_auto_attach(struct comedi_device *dev,
					  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct apci1032_private *devpriv;
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

	devpriv->amcc_iobase = pci_resource_start(pcidev, 0);
	dev->iobase = pci_resource_start(pcidev, 1);
	apci1032_reset(dev);
	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci1032_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 32;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci1032_di_insn_bits;

	/* Change-Of-State (COS) interrupt subdevice */
	s = &dev->subdevices[1];
	if (dev->irq) {
		dev->read_subdev = s;
		s->type		= COMEDI_SUBD_DI | SDF_CMD_READ;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= 1;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_config	= apci1032_cos_insn_config;
		s->insn_bits	= apci1032_cos_insn_bits;
		s->do_cmdtest	= apci1032_cos_cmdtest;
		s->do_cmd	= apci1032_cos_cmd;
		s->cancel	= apci1032_cos_cancel;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	return 0;
}

static void apci1032_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->iobase)
		apci1032_reset(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci1032_driver = {
	.driver_name	= "addi_apci_1032",
	.module		= THIS_MODULE,
	.auto_attach	= apci1032_auto_attach,
	.detach		= apci1032_detach,
};

static int __devinit apci1032_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci1032_driver);
}

static void __devexit apci1032_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci1032_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1003) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1032_pci_table);

static struct pci_driver apci1032_pci_driver = {
	.name		= "addi_apci_1032",
	.id_table	= apci1032_pci_table,
	.probe		= apci1032_pci_probe,
	.remove		= apci1032_pci_remove,
};
module_comedi_pci_driver(apci1032_driver, apci1032_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

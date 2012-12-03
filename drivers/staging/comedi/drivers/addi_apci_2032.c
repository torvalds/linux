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

/*
 * PCI bar 1 I/O Register map
 */
#define APCI2032_DO_REG			0x00
#define APCI2032_INT_CTRL_REG		0x04
#define APCI2032_INT_CTRL_VCC_ENA	(1 << 0)
#define APCI2032_INT_CTRL_CC_ENA	(1 << 1)
#define APCI2032_INT_STATUS_REG		0x08
#define APCI2032_INT_STATUS_VCC		(1 << 0)
#define APCI2032_INT_STATUS_CC		(1 << 1)
#define APCI2032_STATUS_REG		0x0c
#define APCI2032_STATUS_IRQ		(1 << 0)
#define APCI2032_WDOG_REG		0x10
#define APCI2032_WDOG_RELOAD_REG	0x14
#define APCI2032_WDOG_TIMEBASE		0x18
#define APCI2032_WDOG_CTRL_REG		0x1c
#define APCI2032_WDOG_CTRL_ENABLE	(1 << 0)
#define APCI2032_WDOG_CTRL_SW_TRIG	(1 << 9)
#define APCI2032_WDOG_STATUS_REG	0x20
#define APCI2032_WDOG_STATUS_ENABLED	(1 << 0)
#define APCI2032_WDOG_STATUS_SW_TRIG	(1 << 1)

struct apci2032_private {
	unsigned int wdog_ctrl;
};

struct apci2032_int_private {
	spinlock_t spinlock;
	bool active;
	unsigned char enabled_isns;
};

static int apci2032_do_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	s->state = inl(dev->iobase + APCI2032_DO_REG);
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outl(s->state, dev->iobase + APCI2032_DO_REG);
	}

	data[1] = s->state;

	return insn->n;
}

/*
 * The watchdog subdevice is configured with two INSN_CONFIG instructions:
 *
 * Enable the watchdog and set the reload timeout:
 *	data[0] = INSN_CONFIG_ARM
 *	data[1] = timeout reload value
 *
 * Disable the watchdog:
 *	data[0] = INSN_CONFIG_DISARM
 */
static int apci2032_wdog_insn_config(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct apci2032_private *devpriv = dev->private;
	unsigned int reload;

	switch (data[0]) {
	case INSN_CONFIG_ARM:
		devpriv->wdog_ctrl = APCI2032_WDOG_CTRL_ENABLE;
		reload = data[1] & s->maxdata;
		outw(reload, dev->iobase + APCI2032_WDOG_RELOAD_REG);

		/* Time base is 20ms, let the user know the timeout */
		dev_info(dev->class_dev, "watchdog enabled, timeout:%dms\n",
			20 * reload + 20);
		break;
	case INSN_CONFIG_DISARM:
		devpriv->wdog_ctrl = 0;
		break;
	default:
		return -EINVAL;
	}

	outw(devpriv->wdog_ctrl, dev->iobase + APCI2032_WDOG_CTRL_REG);

	return insn->n;
}

static int apci2032_wdog_insn_write(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct apci2032_private *devpriv = dev->private;
	int i;

	if (devpriv->wdog_ctrl == 0) {
		dev_warn(dev->class_dev, "watchdog is disabled\n");
		return -EINVAL;
	}

	/* "ping" the watchdog */
	for (i = 0; i < insn->n; i++) {
		outw(devpriv->wdog_ctrl | APCI2032_WDOG_CTRL_SW_TRIG,
			dev->iobase + APCI2032_WDOG_CTRL_REG);
	}

	return insn->n;
}

static int apci2032_wdog_insn_read(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = inl(dev->iobase + APCI2032_WDOG_STATUS_REG);

	return insn->n;
}

static int apci2032_int_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	data[1] = inl(dev->iobase + APCI2032_INT_STATUS_REG) & 3;
	return insn->n;
}

static void apci2032_int_stop(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct apci2032_int_private *subpriv = s->private;

	subpriv->active = false;
	subpriv->enabled_isns = 0;
	outl(0x0, dev->iobase + APCI2032_INT_CTRL_REG);
}

static int apci2032_int_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
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
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);
	err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: ignored */

	if (err)
		return 4;

	return 0;
}

static int apci2032_int_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	struct apci2032_int_private *subpriv = s->private;
	unsigned char enabled_isns;
	unsigned int n;
	unsigned long flags;

	enabled_isns = 0;
	for (n = 0; n < cmd->chanlist_len; n++)
		enabled_isns |= 1 << CR_CHAN(cmd->chanlist[n]);

	spin_lock_irqsave(&subpriv->spinlock, flags);
	subpriv->enabled_isns = enabled_isns;
	subpriv->active = true;
	outl(subpriv->enabled_isns, dev->iobase + APCI2032_INT_CTRL_REG);
	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 0;
}

static int apci2032_int_cancel(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct apci2032_int_private *subpriv = s->private;
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	if (subpriv->active)
		apci2032_int_stop(dev, s);
	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 0;
}

static irqreturn_t apci2032_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct apci2032_int_private *subpriv;
	unsigned int val;
	bool do_event = false;

	if (!dev->attached)
		return IRQ_NONE;

	/* Check if VCC OR CC interrupt has occurred */
	val = inl(dev->iobase + APCI2032_STATUS_REG) & APCI2032_STATUS_IRQ;
	if (!val)
		return IRQ_NONE;

	subpriv = s->private;
	spin_lock(&subpriv->spinlock);

	val = inl(dev->iobase + APCI2032_INT_STATUS_REG) & 3;
	/* Disable triggered interrupt sources. */
	outl(~val & 3, dev->iobase + APCI2032_INT_CTRL_REG);
	/*
	 * Note: We don't reenable the triggered interrupt sources because they
	 * are level-sensitive, hardware error status interrupt sources and
	 * they'd keep triggering interrupts repeatedly.
	 */

	if (subpriv->active && (val & subpriv->enabled_isns) != 0) {
		unsigned short bits;
		unsigned int n, len;
		unsigned int *chanlist;

		/* Bits in scan data correspond to indices in channel list. */
		bits = 0;
		len = s->async->cmd.chanlist_len;
		chanlist = &s->async->cmd.chanlist[0];
		for (n = 0; n < len; n++)
			if ((val & (1U << CR_CHAN(chanlist[n]))) != 0)
				bits |= 1U << n;

		if (comedi_buf_put(s->async, bits)) {
			s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;
		} else {
			apci2032_int_stop(dev, s);
			s->async->events |= COMEDI_CB_OVERFLOW;
		}
		do_event = true;
	}

	spin_unlock(&subpriv->spinlock);
	if (do_event)
		comedi_event(dev, s);

	return IRQ_HANDLED;
}

static int apci2032_reset(struct comedi_device *dev)
{
	outl(0x0, dev->iobase + APCI2032_DO_REG);
	outl(0x0, dev->iobase + APCI2032_INT_CTRL_REG);
	outl(0x0, dev->iobase + APCI2032_WDOG_CTRL_REG);
	outl(0x0, dev->iobase + APCI2032_WDOG_RELOAD_REG);

	return 0;
}

static int apci2032_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct apci2032_private *devpriv;
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
	apci2032_reset(dev);

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, apci2032_interrupt,
				  IRQF_SHARED, dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/* Initialize the digital output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 32;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci2032_do_insn_bits;

	/* Initialize the watchdog subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_TIMER;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 1;
	s->maxdata	= 0xff;
	s->insn_write	= apci2032_wdog_insn_write;
	s->insn_read	= apci2032_wdog_insn_read;
	s->insn_config	= apci2032_wdog_insn_config;

	/* Initialize the interrupt subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 2;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= apci2032_int_insn_bits;
	if (dev->irq) {
		struct apci2032_int_private *subpriv;

		dev->read_subdev = s;
		subpriv = kzalloc(sizeof(*subpriv), GFP_KERNEL);
		if (!subpriv)
			return -ENOMEM;
		spin_lock_init(&subpriv->spinlock);
		s->private	= subpriv;
		s->subdev_flags	= SDF_READABLE | SDF_CMD_READ;
		s->len_chanlist = 2;
		s->do_cmdtest	= apci2032_int_cmdtest;
		s->do_cmd	= apci2032_int_cmd;
		s->cancel	= apci2032_int_cancel;
	}

	return 0;
}

static void apci2032_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->iobase)
		apci2032_reset(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->read_subdev)
		kfree(dev->read_subdev->private);
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

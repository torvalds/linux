/*
    comedi/drivers/ni_6527.c
    driver for National Instruments PCI-6527

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1999,2002,2003 David A. Schleef <ds@schleef.org>

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
Driver: ni_6527
Description: National Instruments 6527
Author: ds
Status: works
Devices: [National Instruments] PCI-6527 (ni6527), PXI-6527
Updated: Sat, 25 Jan 2003 13:24:40 -0800


*/

/*
   Manuals (available from ftp://ftp.natinst.com/support/manuals)

	370106b.pdf	6527 Register Level Programmer Manual

 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "mite.h"

#define NI6527_DI_REG(x)		(0x00 + (x))
#define NI6527_DO_REG(x)		(0x03 + (x))
#define NI6527_ID_REG			0x06

#define Clear_Register				0x07
#define ClrEdge				0x08
#define ClrOverflow			0x04
#define ClrFilter			0x02
#define ClrInterval			0x01

#define NI6527_FILT_INTERVAL_REG(x)	(0x08 + (x))
#define NI6527_FILT_ENA_REG(x)		(0x0c + (x))

#define Change_Status				0x14
#define MasterInterruptStatus		0x04
#define Overflow			0x02
#define EdgeStatus			0x01

#define Master_Interrupt_Control		0x15
#define FallingEdgeIntEnable		0x10
#define RisingEdgeIntEnable		0x08
#define MasterInterruptEnable		0x04
#define OverflowIntEnable		0x02
#define EdgeIntEnable			0x01

#define Rising_Edge_Detection_Enable(x)		(0x018+(x))
#define Falling_Edge_Detection_Enable(x)	(0x020+(x))

enum ni6527_boardid {
	BOARD_PCI6527,
	BOARD_PXI6527,
};

struct ni6527_board {
	const char *name;
};

static const struct ni6527_board ni6527_boards[] = {
	[BOARD_PCI6527] = {
		.name		= "pci-6527",
	},
	[BOARD_PXI6527] = {
		.name		= "pxi-6527",
	},
};

struct ni6527_private {
	struct mite_struct *mite;
	unsigned int filter_interval;
	unsigned int filter_enable;
};

static void ni6527_set_filter_interval(struct comedi_device *dev,
				       unsigned int val)
{
	struct ni6527_private *devpriv = dev->private;
	void __iomem *mmio = devpriv->mite->daq_io_addr;

	if (val != devpriv->filter_interval) {
		writeb(val & 0xff, mmio + NI6527_FILT_INTERVAL_REG(0));
		writeb((val >> 8) & 0xff, mmio + NI6527_FILT_INTERVAL_REG(1));
		writeb((val >> 16) & 0x0f, mmio + NI6527_FILT_INTERVAL_REG(2));

		writeb(ClrInterval, mmio + Clear_Register);

		devpriv->filter_interval = val;
	}
}

static void ni6527_set_filter_enable(struct comedi_device *dev,
				     unsigned int val)
{
	struct ni6527_private *devpriv = dev->private;
	void __iomem *mmio = devpriv->mite->daq_io_addr;

	writeb(val & 0xff, mmio + NI6527_FILT_ENA_REG(0));
	writeb((val >> 8) & 0xff, mmio + NI6527_FILT_ENA_REG(1));
	writeb((val >> 16) & 0xff, mmio + NI6527_FILT_ENA_REG(2));
}

static int ni6527_di_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct ni6527_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int interval;

	switch (data[0]) {
	case INSN_CONFIG_FILTER:
		/*
		 * The deglitch filter interval is specified in nanoseconds.
		 * The hardware supports intervals in 200ns increments. Round
		 * the user values up and return the actual interval.
		 */
		interval = (data[1] + 100) / 200;
		data[1] = interval * 200;

		if (interval) {
			ni6527_set_filter_interval(dev, interval);
			devpriv->filter_enable |= 1 << chan;
		} else {
			devpriv->filter_enable &= ~(1 << chan);
		}
		ni6527_set_filter_enable(dev, devpriv->filter_enable);
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static int ni6527_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct ni6527_private *devpriv = dev->private;
	void __iomem *mmio = devpriv->mite->daq_io_addr;
	unsigned int val;

	val = readb(mmio + NI6527_DI_REG(0));
	val |= (readb(mmio + NI6527_DI_REG(1)) << 8);
	val |= (readb(mmio + NI6527_DI_REG(2)) << 16);

	data[1] = val;

	return insn->n;
}

static int ni6527_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct ni6527_private *devpriv = dev->private;
	void __iomem *mmio = devpriv->mite->daq_io_addr;
	unsigned int mask;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		/* Outputs are inverted */
		unsigned int val = s->state ^ 0xffffff;

		if (mask & 0x0000ff)
			writeb(val & 0xff, mmio + NI6527_DO_REG(0));
		if (mask & 0x00ff00)
			writeb((val >> 8) & 0xff, mmio + NI6527_DO_REG(1));
		if (mask & 0xff0000)
			writeb((val >> 16) & 0xff, mmio + NI6527_DO_REG(2));
	}

	data[1] = s->state;

	return insn->n;
}

static irqreturn_t ni6527_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct ni6527_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status;

	status = readb(devpriv->mite->daq_io_addr + Change_Status);
	if ((status & MasterInterruptStatus) == 0)
		return IRQ_NONE;
	if ((status & EdgeStatus) == 0)
		return IRQ_NONE;

	writeb(ClrEdge | ClrOverflow,
	       devpriv->mite->daq_io_addr + Clear_Register);

	comedi_buf_put(s->async, 0);
	s->async->events |= COMEDI_CB_EOS;
	comedi_event(dev, s);
	return IRQ_HANDLED;
}

static int ni6527_intr_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_OTHER);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT);

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

	/* step 4: fix up any arguments */

	if (err)
		return 4;

	return 0;
}

static int ni6527_intr_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct ni6527_private *devpriv = dev->private;
	/* struct comedi_cmd *cmd = &s->async->cmd; */

	writeb(ClrEdge | ClrOverflow,
	       devpriv->mite->daq_io_addr + Clear_Register);
	writeb(FallingEdgeIntEnable | RisingEdgeIntEnable |
	       MasterInterruptEnable | EdgeIntEnable,
	       devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	return 0;
}

static int ni6527_intr_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct ni6527_private *devpriv = dev->private;

	writeb(0x00, devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	return 0;
}

static int ni6527_intr_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	data[1] = 0;
	return insn->n;
}

static int ni6527_intr_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct ni6527_private *devpriv = dev->private;

	if (insn->n < 1)
		return -EINVAL;
	if (data[0] != INSN_CONFIG_CHANGE_NOTIFY)
		return -EINVAL;

	writeb(data[1],
	       devpriv->mite->daq_io_addr + Rising_Edge_Detection_Enable(0));
	writeb(data[1] >> 8,
	       devpriv->mite->daq_io_addr + Rising_Edge_Detection_Enable(1));
	writeb(data[1] >> 16,
	       devpriv->mite->daq_io_addr + Rising_Edge_Detection_Enable(2));

	writeb(data[2],
	       devpriv->mite->daq_io_addr + Falling_Edge_Detection_Enable(0));
	writeb(data[2] >> 8,
	       devpriv->mite->daq_io_addr + Falling_Edge_Detection_Enable(1));
	writeb(data[2] >> 16,
	       devpriv->mite->daq_io_addr + Falling_Edge_Detection_Enable(2));

	return 2;
}

static int ni6527_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct ni6527_board *board = NULL;
	struct ni6527_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	if (context < ARRAY_SIZE(ni6527_boards))
		board = &ni6527_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	devpriv->mite = mite_alloc(pcidev);
	if (!devpriv->mite)
		return -ENOMEM;

	ret = mite_setup(devpriv->mite);
	if (ret < 0) {
		dev_err(dev->class_dev, "error setting up mite\n");
		return ret;
	}

	/* make sure this is actually a 6527 device */
	if (readb(devpriv->mite->daq_io_addr + NI6527_ID_REG) != 0x27)
		return -ENODEV;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 24;
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_config = ni6527_di_insn_config;
	s->insn_bits = ni6527_di_insn_bits;

	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 24;
	s->range_table = &range_unknown;  /* FIXME: actually conductance */
	s->maxdata = 1;
	s->insn_bits = ni6527_do_insn_bits;

	s = &dev->subdevices[2];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
	s->n_chan = 1;
	s->range_table = &range_unknown;
	s->maxdata = 1;
	s->do_cmdtest = ni6527_intr_cmdtest;
	s->do_cmd = ni6527_intr_cmd;
	s->cancel = ni6527_intr_cancel;
	s->insn_bits = ni6527_intr_insn_bits;
	s->insn_config = ni6527_intr_insn_config;

	ni6527_set_filter_enable(dev, 0);

	writeb(ClrEdge | ClrOverflow | ClrFilter | ClrInterval,
	       devpriv->mite->daq_io_addr + Clear_Register);
	writeb(0x00, devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	ret = request_irq(mite_irq(devpriv->mite), ni6527_interrupt,
			  IRQF_SHARED, dev->board_name, dev);
	if (ret < 0)
		dev_warn(dev->class_dev, "irq not available\n");
	else
		dev->irq = mite_irq(devpriv->mite);

	return 0;
}

static void ni6527_detach(struct comedi_device *dev)
{
	struct ni6527_private *devpriv = dev->private;

	if (devpriv && devpriv->mite && devpriv->mite->daq_io_addr)
		writeb(0x00,
		       devpriv->mite->daq_io_addr + Master_Interrupt_Control);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv && devpriv->mite) {
		mite_unsetup(devpriv->mite);
		mite_free(devpriv->mite);
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver ni6527_driver = {
	.driver_name = "ni_6527",
	.module = THIS_MODULE,
	.auto_attach = ni6527_auto_attach,
	.detach = ni6527_detach,
};

static int ni6527_pci_probe(struct pci_dev *dev,
			    const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ni6527_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(ni6527_pci_table) = {
	{ PCI_VDEVICE(NI, 0x2b10), BOARD_PXI6527 },
	{ PCI_VDEVICE(NI, 0x2b20), BOARD_PCI6527 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni6527_pci_table);

static struct pci_driver ni6527_pci_driver = {
	.name		= "ni_6527",
	.id_table	= ni6527_pci_table,
	.probe		= ni6527_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ni6527_driver, ni6527_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

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

#define DEBUG 1
#define DEBUG_FLAGS

#include <linux/interrupt.h>
#include "../comedidev.h"

#include "mite.h"

#define NI6527_DIO_SIZE 4096
#define NI6527_MITE_SIZE 4096

#define Port_Register(x)			(0x00+(x))
#define ID_Register				0x06

#define Clear_Register				0x07
#define ClrEdge				0x08
#define ClrOverflow			0x04
#define ClrFilter			0x02
#define ClrInterval			0x01

#define Filter_Interval(x)			(0x08+(x))
#define Filter_Enable(x)			(0x0c+(x))

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

static int ni6527_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static void ni6527_detach(struct comedi_device *dev);
static struct comedi_driver driver_ni6527 = {
	.driver_name = "ni6527",
	.module = THIS_MODULE,
	.attach = ni6527_attach,
	.detach = ni6527_detach,
};

struct ni6527_board {

	int dev_id;
	const char *name;
};

static const struct ni6527_board ni6527_boards[] = {
	{
	 .dev_id = 0x2b20,
	 .name = "pci-6527",
	 },
	{
	 .dev_id = 0x2b10,
	 .name = "pxi-6527",
	 },
};

#define n_ni6527_boards ARRAY_SIZE(ni6527_boards)
#define this_board ((const struct ni6527_board *)dev->board_ptr)

static DEFINE_PCI_DEVICE_TABLE(ni6527_pci_table) = {
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x2b10)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x2b20)},
	{0}
};

MODULE_DEVICE_TABLE(pci, ni6527_pci_table);

struct ni6527_private {
	struct mite_struct *mite;
	unsigned int filter_interval;
	unsigned int filter_enable;
};

#define devpriv ((struct ni6527_private *)dev->private)

static int ni6527_find_device(struct comedi_device *dev, int bus, int slot);

static int ni6527_di_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	unsigned int interval;

	if (insn->n != 2)
		return -EINVAL;

	if (data[0] != INSN_CONFIG_FILTER)
		return -EINVAL;

	if (data[1]) {
		interval = (data[1] + 100) / 200;
		data[1] = interval * 200;

		if (interval != devpriv->filter_interval) {
			writeb(interval & 0xff,
			       devpriv->mite->daq_io_addr + Filter_Interval(0));
			writeb((interval >> 8) & 0xff,
			       devpriv->mite->daq_io_addr + Filter_Interval(1));
			writeb((interval >> 16) & 0x0f,
			       devpriv->mite->daq_io_addr + Filter_Interval(2));

			writeb(ClrInterval,
			       devpriv->mite->daq_io_addr + Clear_Register);

			devpriv->filter_interval = interval;
		}

		devpriv->filter_enable |= 1 << chan;
	} else {
		devpriv->filter_enable &= ~(1 << chan);
	}

	writeb(devpriv->filter_enable,
	       devpriv->mite->daq_io_addr + Filter_Enable(0));
	writeb(devpriv->filter_enable >> 8,
	       devpriv->mite->daq_io_addr + Filter_Enable(1));
	writeb(devpriv->filter_enable >> 16,
	       devpriv->mite->daq_io_addr + Filter_Enable(2));

	return 2;
}

static int ni6527_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = readb(devpriv->mite->daq_io_addr + Port_Register(0));
	data[1] |= readb(devpriv->mite->daq_io_addr + Port_Register(1)) << 8;
	data[1] |= readb(devpriv->mite->daq_io_addr + Port_Register(2)) << 16;

	return 2;
}

static int ni6527_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);

		/* The open relay state on the board cooresponds to 1,
		 * but in Comedi, it is represented by 0. */
		if (data[0] & 0x0000ff) {
			writeb((s->state ^ 0xff),
			       devpriv->mite->daq_io_addr + Port_Register(3));
		}
		if (data[0] & 0x00ff00) {
			writeb((s->state >> 8) ^ 0xff,
			       devpriv->mite->daq_io_addr + Port_Register(4));
		}
		if (data[0] & 0xff0000) {
			writeb((s->state >> 16) ^ 0xff,
			       devpriv->mite->daq_io_addr + Port_Register(5));
		}
	}
	data[1] = s->state;

	return 2;
}

static irqreturn_t ni6527_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->subdevices + 2;
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
	int tmp;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_OTHER;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_FOLLOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and */
	/*         are mutually compatible */

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
	if (cmd->scan_begin_arg != 0) {
		cmd->scan_begin_arg = 0;
		err++;
	}
	if (cmd->convert_arg != 0) {
		cmd->convert_arg = 0;
		err++;
	}

	if (cmd->scan_end_arg != 1) {
		cmd->scan_end_arg = 1;
		err++;
	}
	if (cmd->stop_arg != 0) {
		cmd->stop_arg = 0;
		err++;
	}

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
	writeb(0x00, devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	return 0;
}

static int ni6527_intr_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n < 1)
		return -EINVAL;

	data[1] = 0;
	return 2;
}

static int ni6527_intr_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
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

static int ni6527_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	printk(KERN_INFO "comedi%d: ni6527\n", dev->minor);

	ret = alloc_private(dev, sizeof(struct ni6527_private));
	if (ret < 0)
		return ret;

	ret = ni6527_find_device(dev, it->options[0], it->options[1]);
	if (ret < 0)
		return ret;

	ret = mite_setup(devpriv->mite);
	if (ret < 0) {
		printk(KERN_ERR "comedi: error setting up mite\n");
		return ret;
	}

	dev->board_name = this_board->name;
	printk(KERN_INFO "comedi board: %s, ID=0x%02x\n", dev->board_name,
		readb(devpriv->mite->daq_io_addr + ID_Register));

	ret = alloc_subdevices(dev, 3);
	if (ret < 0)
		return ret;

	s = dev->subdevices + 0;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 24;
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_config = ni6527_di_insn_config;
	s->insn_bits = ni6527_di_insn_bits;

	s = dev->subdevices + 1;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 24;
	s->range_table = &range_unknown;  /* FIXME: actually conductance */
	s->maxdata = 1;
	s->insn_bits = ni6527_do_insn_bits;

	s = dev->subdevices + 2;
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

	writeb(0x00, devpriv->mite->daq_io_addr + Filter_Enable(0));
	writeb(0x00, devpriv->mite->daq_io_addr + Filter_Enable(1));
	writeb(0x00, devpriv->mite->daq_io_addr + Filter_Enable(2));

	writeb(ClrEdge | ClrOverflow | ClrFilter | ClrInterval,
	       devpriv->mite->daq_io_addr + Clear_Register);
	writeb(0x00, devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	ret = request_irq(mite_irq(devpriv->mite), ni6527_interrupt,
			  IRQF_SHARED, "ni6527", dev);
	if (ret < 0)
		printk(KERN_WARNING "comedi i6527 irq not available\n");
	else
		dev->irq = mite_irq(devpriv->mite);

	return 0;
}

static void ni6527_detach(struct comedi_device *dev)
{
	if (devpriv && devpriv->mite && devpriv->mite->daq_io_addr)
		writeb(0x00,
		       devpriv->mite->daq_io_addr + Master_Interrupt_Control);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv && devpriv->mite)
		mite_unsetup(devpriv->mite);
}

static int ni6527_find_device(struct comedi_device *dev, int bus, int slot)
{
	struct mite_struct *mite;
	int i;

	for (mite = mite_devices; mite; mite = mite->next) {
		if (mite->used)
			continue;
		if (bus || slot) {
			if (bus != mite->pcidev->bus->number ||
			    slot != PCI_SLOT(mite->pcidev->devfn))
				continue;
		}
		for (i = 0; i < n_ni6527_boards; i++) {
			if (mite_device_id(mite) == ni6527_boards[i].dev_id) {
				dev->board_ptr = ni6527_boards + i;
				devpriv->mite = mite;
				return 0;
			}
		}
	}
	printk(KERN_ERR "comedi 6527: no device found\n");
	mite_list_devices();
	return -EIO;
}

static int __devinit driver_ni6527_pci_probe(struct pci_dev *dev,
					     const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &driver_ni6527);
}

static void __devexit driver_ni6527_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver driver_ni6527_pci_driver = {
	.id_table = ni6527_pci_table,
	.probe = &driver_ni6527_pci_probe,
	.remove = __devexit_p(&driver_ni6527_pci_remove)
};

static int __init driver_ni6527_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&driver_ni6527);
	if (retval < 0)
		return retval;

	driver_ni6527_pci_driver.name = (char *)driver_ni6527.driver_name;
	return pci_register_driver(&driver_ni6527_pci_driver);
}

static void __exit driver_ni6527_cleanup_module(void)
{
	pci_unregister_driver(&driver_ni6527_pci_driver);
	comedi_driver_unregister(&driver_ni6527);
}

module_init(driver_ni6527_init_module);
module_exit(driver_ni6527_cleanup_module);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

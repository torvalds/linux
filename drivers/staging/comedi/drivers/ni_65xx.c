/*
    comedi/drivers/ni_6514.c
    driver for National Instruments PCI-6514

    Copyright (C) 2006 Jon Grierson <jd@renko.co.uk>
    Copyright (C) 2006 Frank Mori Hess <fmhess@users.sourceforge.net>

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
Driver: ni_65xx
Description: National Instruments 65xx static dio boards
Author: Jon Grierson <jd@renko.co.uk>,
	Frank Mori Hess <fmhess@users.sourceforge.net>
Status: testing
Devices: [National Instruments] PCI-6509 (ni_65xx), PXI-6509, PCI-6510,
  PCI-6511, PXI-6511, PCI-6512, PXI-6512, PCI-6513, PXI-6513, PCI-6514,
  PXI-6514, PCI-6515, PXI-6515, PCI-6516, PCI-6517, PCI-6518, PCI-6519,
  PCI-6520, PCI-6521, PXI-6521, PCI-6528, PXI-6528
Updated: Wed Oct 18 08:59:11 EDT 2006

Based on the PCI-6527 driver by ds.
The interrupt subdevice (subdevice 3) is probably broken for all boards
except maybe the 6514.

*/

/*
   Manuals (available from ftp://ftp.natinst.com/support/manuals)

	370106b.pdf	6514 Register Level Programmer Manual

 */

#define DEBUG 1
#define DEBUG_FLAGS

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "mite.h"

#define NI6514_DIO_SIZE 4096
#define NI6514_MITE_SIZE 4096

#define NI_65XX_MAX_NUM_PORTS 12
static const unsigned ni_65xx_channels_per_port = 8;
static const unsigned ni_65xx_port_offset = 0x10;

static inline unsigned Port_Data(unsigned port)
{
	return 0x40 + port * ni_65xx_port_offset;
}

static inline unsigned Port_Select(unsigned port)
{
	return 0x41 + port * ni_65xx_port_offset;
}

static inline unsigned Rising_Edge_Detection_Enable(unsigned port)
{
	return 0x42 + port * ni_65xx_port_offset;
}

static inline unsigned Falling_Edge_Detection_Enable(unsigned port)
{
	return 0x43 + port * ni_65xx_port_offset;
}

static inline unsigned Filter_Enable(unsigned port)
{
	return 0x44 + port * ni_65xx_port_offset;
}

#define ID_Register				0x00

#define Clear_Register				0x01
#define ClrEdge				0x08
#define ClrOverflow			0x04

#define Filter_Interval			0x08

#define Change_Status				0x02
#define MasterInterruptStatus		0x04
#define Overflow			0x02
#define EdgeStatus			0x01

#define Master_Interrupt_Control		0x03
#define FallingEdgeIntEnable		0x10
#define RisingEdgeIntEnable		0x08
#define MasterInterruptEnable		0x04
#define OverflowIntEnable		0x02
#define EdgeIntEnable			0x01

enum ni_65xx_boardid {
	BOARD_PCI6509,
	BOARD_PXI6509,
	BOARD_PCI6510,
	BOARD_PCI6511,
	BOARD_PXI6511,
	BOARD_PCI6512,
	BOARD_PXI6512,
	BOARD_PCI6513,
	BOARD_PXI6513,
	BOARD_PCI6514,
	BOARD_PXI6514,
	BOARD_PCI6515,
	BOARD_PXI6515,
	BOARD_PCI6516,
	BOARD_PCI6517,
	BOARD_PCI6518,
	BOARD_PCI6519,
	BOARD_PCI6520,
	BOARD_PCI6521,
	BOARD_PXI6521,
	BOARD_PCI6528,
	BOARD_PXI6528,
};

struct ni_65xx_board {
	const char *name;
	unsigned num_dio_ports;
	unsigned num_di_ports;
	unsigned num_do_ports;
	unsigned invert_outputs:1;
};

static const struct ni_65xx_board ni_65xx_boards[] = {
	[BOARD_PCI6509] = {
		.name		= "pci-6509",
		.num_dio_ports	= 12,
	},
	[BOARD_PXI6509] = {
		.name		= "pxi-6509",
		.num_dio_ports	= 12,
	},
	[BOARD_PCI6510] = {
		.name		= "pci-6510",
		.num_di_ports	= 4,
	},
	[BOARD_PCI6511] = {
		.name		= "pci-6511",
		.num_di_ports	= 8,
	},
	[BOARD_PXI6511] = {
		.name		= "pxi-6511",
		.num_di_ports	= 8,
	},
	[BOARD_PCI6512] = {
		.name		= "pci-6512",
		.num_do_ports	= 8,
	},
	[BOARD_PXI6512] = {
		.name		= "pxi-6512",
		.num_do_ports	= 8,
	},
	[BOARD_PCI6513] = {
		.name		= "pci-6513",
		.num_do_ports	= 8,
		.invert_outputs	= 1,
	},
	[BOARD_PXI6513] = {
		.name		= "pxi-6513",
		.num_do_ports	= 8,
		.invert_outputs	= 1,
	},
	[BOARD_PCI6514] = {
		.name		= "pci-6514",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.invert_outputs	= 1,
	},
	[BOARD_PXI6514] = {
		.name		= "pxi-6514",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.invert_outputs	= 1,
	},
	[BOARD_PCI6515] = {
		.name		= "pci-6515",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.invert_outputs	= 1,
	},
	[BOARD_PXI6515] = {
		.name		= "pxi-6515",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.invert_outputs	= 1,
	},
	[BOARD_PCI6516] = {
		.name		= "pci-6516",
		.num_do_ports	= 4,
		.invert_outputs	= 1,
	},
	[BOARD_PCI6517] = {
		.name		= "pci-6517",
		.num_do_ports	= 4,
		.invert_outputs	= 1,
	},
	[BOARD_PCI6518] = {
		.name		= "pci-6518",
		.num_di_ports	= 2,
		.num_do_ports	= 2,
		.invert_outputs	= 1,
	},
	[BOARD_PCI6519] = {
		.name		= "pci-6519",
		.num_di_ports	= 2,
		.num_do_ports	= 2,
		.invert_outputs	= 1,
	},
	[BOARD_PCI6520] = {
		.name		= "pci-6520",
		.num_di_ports	= 1,
		.num_do_ports	= 1,
	},
	[BOARD_PCI6521] = {
		.name		= "pci-6521",
		.num_di_ports	= 1,
		.num_do_ports	= 1,
	},
	[BOARD_PXI6521] = {
		.name		= "pxi-6521",
		.num_di_ports	= 1,
		.num_do_ports	= 1,
	},
	[BOARD_PCI6528] = {
		.name		= "pci-6528",
		.num_di_ports	= 3,
		.num_do_ports	= 3,
	},
	[BOARD_PXI6528] = {
		.name		= "pxi-6528",
		.num_di_ports	= 3,
		.num_do_ports	= 3,
	},
};

static inline unsigned ni_65xx_port_by_channel(unsigned channel)
{
	return channel / ni_65xx_channels_per_port;
}

static inline unsigned ni_65xx_total_num_ports(const struct ni_65xx_board
					       *board)
{
	return board->num_dio_ports + board->num_di_ports + board->num_do_ports;
}

struct ni_65xx_private {
	struct mite_struct *mite;
	unsigned int filter_interval;
	unsigned short filter_enable[NI_65XX_MAX_NUM_PORTS];
	unsigned short output_bits[NI_65XX_MAX_NUM_PORTS];
	unsigned short dio_direction[NI_65XX_MAX_NUM_PORTS];
};

struct ni_65xx_subdevice_private {
	unsigned base_port;
};

static inline struct ni_65xx_subdevice_private *sprivate(struct comedi_subdevice
							 *subdev)
{
	return subdev->private;
}

static int ni_65xx_config_filter(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	struct ni_65xx_private *devpriv = dev->private;
	const unsigned chan = CR_CHAN(insn->chanspec);
	const unsigned port =
	    sprivate(s)->base_port + ni_65xx_port_by_channel(chan);

	if (data[0] != INSN_CONFIG_FILTER)
		return -EINVAL;
	if (data[1]) {
		static const unsigned filter_resolution_ns = 200;
		static const unsigned max_filter_interval = 0xfffff;
		unsigned interval =
		    (data[1] +
		     (filter_resolution_ns / 2)) / filter_resolution_ns;
		if (interval > max_filter_interval)
			interval = max_filter_interval;
		data[1] = interval * filter_resolution_ns;

		if (interval != devpriv->filter_interval) {
			writeb(interval,
			       devpriv->mite->daq_io_addr +
			       Filter_Interval);
			devpriv->filter_interval = interval;
		}

		devpriv->filter_enable[port] |=
		    1 << (chan % ni_65xx_channels_per_port);
	} else {
		devpriv->filter_enable[port] &=
		    ~(1 << (chan % ni_65xx_channels_per_port));
	}

	writeb(devpriv->filter_enable[port],
	       devpriv->mite->daq_io_addr + Filter_Enable(port));

	return 2;
}

static int ni_65xx_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct ni_65xx_private *devpriv = dev->private;
	unsigned port;

	if (insn->n < 1)
		return -EINVAL;
	port = sprivate(s)->base_port +
	    ni_65xx_port_by_channel(CR_CHAN(insn->chanspec));
	switch (data[0]) {
	case INSN_CONFIG_FILTER:
		return ni_65xx_config_filter(dev, s, insn, data);
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		devpriv->dio_direction[port] = COMEDI_OUTPUT;
		writeb(0, devpriv->mite->daq_io_addr + Port_Select(port));
		return 1;
		break;
	case INSN_CONFIG_DIO_INPUT:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		devpriv->dio_direction[port] = COMEDI_INPUT;
		writeb(1, devpriv->mite->daq_io_addr + Port_Select(port));
		return 1;
		break;
	case INSN_CONFIG_DIO_QUERY:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		data[1] = devpriv->dio_direction[port];
		return insn->n;
		break;
	default:
		break;
	}
	return -EINVAL;
}

static int ni_65xx_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	const struct ni_65xx_board *board = comedi_board(dev);
	struct ni_65xx_private *devpriv = dev->private;
	unsigned base_bitfield_channel;
	const unsigned max_ports_per_bitfield = 5;
	unsigned read_bits = 0;
	unsigned j;

	base_bitfield_channel = CR_CHAN(insn->chanspec);
	for (j = 0; j < max_ports_per_bitfield; ++j) {
		const unsigned port_offset =
			ni_65xx_port_by_channel(base_bitfield_channel) + j;
		const unsigned port =
			sprivate(s)->base_port + port_offset;
		unsigned base_port_channel;
		unsigned port_mask, port_data, port_read_bits;
		int bitshift;
		if (port >= ni_65xx_total_num_ports(board))
			break;
		base_port_channel = port_offset * ni_65xx_channels_per_port;
		port_mask = data[0];
		port_data = data[1];
		bitshift = base_port_channel - base_bitfield_channel;
		if (bitshift >= 32 || bitshift <= -32)
			break;
		if (bitshift > 0) {
			port_mask >>= bitshift;
			port_data >>= bitshift;
		} else {
			port_mask <<= -bitshift;
			port_data <<= -bitshift;
		}
		port_mask &= 0xff;
		port_data &= 0xff;
		if (port_mask) {
			unsigned bits;
			devpriv->output_bits[port] &= ~port_mask;
			devpriv->output_bits[port] |=
			    port_data & port_mask;
			bits = devpriv->output_bits[port];
			if (board->invert_outputs)
				bits = ~bits;
			writeb(bits,
			       devpriv->mite->daq_io_addr +
			       Port_Data(port));
		}
		port_read_bits =
		    readb(devpriv->mite->daq_io_addr + Port_Data(port));
		if (s->type == COMEDI_SUBD_DO && board->invert_outputs) {
			/* Outputs inverted, so invert value read back from
			 * DO subdevice.  (Does not apply to boards with DIO
			 * subdevice.) */
			port_read_bits ^= 0xFF;
		}
		if (bitshift > 0)
			port_read_bits <<= bitshift;
		else
			port_read_bits >>= -bitshift;

		read_bits |= port_read_bits;
	}
	data[1] = read_bits;
	return insn->n;
}

static irqreturn_t ni_65xx_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct ni_65xx_private *devpriv = dev->private;
	struct comedi_subdevice *s = &dev->subdevices[2];
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

static int ni_65xx_intr_cmdtest(struct comedi_device *dev,
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

static int ni_65xx_intr_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct ni_65xx_private *devpriv = dev->private;
	/* struct comedi_cmd *cmd = &s->async->cmd; */

	writeb(ClrEdge | ClrOverflow,
	       devpriv->mite->daq_io_addr + Clear_Register);
	writeb(FallingEdgeIntEnable | RisingEdgeIntEnable |
	       MasterInterruptEnable | EdgeIntEnable,
	       devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	return 0;
}

static int ni_65xx_intr_cancel(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct ni_65xx_private *devpriv = dev->private;

	writeb(0x00, devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	return 0;
}

static int ni_65xx_intr_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	data[1] = 0;
	return insn->n;
}

static int ni_65xx_intr_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	struct ni_65xx_private *devpriv = dev->private;

	if (insn->n < 1)
		return -EINVAL;
	if (data[0] != INSN_CONFIG_CHANGE_NOTIFY)
		return -EINVAL;

	writeb(data[1],
	       devpriv->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0));
	writeb(data[1] >> 8,
	       devpriv->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0x10));
	writeb(data[1] >> 16,
	       devpriv->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0x20));
	writeb(data[1] >> 24,
	       devpriv->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0x30));

	writeb(data[2],
	       devpriv->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0));
	writeb(data[2] >> 8,
	       devpriv->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0x10));
	writeb(data[2] >> 16,
	       devpriv->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0x20));
	writeb(data[2] >> 24,
	       devpriv->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0x30));

	return 2;
}

static int ni_65xx_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct ni_65xx_board *board = NULL;
	struct ni_65xx_private *devpriv;
	struct ni_65xx_subdevice_private *spriv;
	struct comedi_subdevice *s;
	unsigned i;
	int ret;

	if (context < ARRAY_SIZE(ni_65xx_boards))
		board = &ni_65xx_boards[context];
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
		dev_warn(dev->class_dev, "error setting up mite\n");
		return ret;
	}

	dev->irq = mite_irq(devpriv->mite);
	dev_info(dev->class_dev, "board: %s, ID=0x%02x", dev->board_name,
	       readb(devpriv->mite->daq_io_addr + ID_Register));

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	if (board->num_di_ports) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE;
		s->n_chan =
		    board->num_di_ports * ni_65xx_channels_per_port;
		s->range_table = &range_digital;
		s->maxdata = 1;
		s->insn_config = ni_65xx_dio_insn_config;
		s->insn_bits = ni_65xx_dio_insn_bits;
		spriv = comedi_alloc_spriv(s, sizeof(*spriv));
		if (!spriv)
			return -ENOMEM;
		spriv->base_port = 0;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[1];
	if (board->num_do_ports) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan =
		    board->num_do_ports * ni_65xx_channels_per_port;
		s->range_table = &range_digital;
		s->maxdata = 1;
		s->insn_bits = ni_65xx_dio_insn_bits;
		spriv = comedi_alloc_spriv(s, sizeof(*spriv));
		if (!spriv)
			return -ENOMEM;
		spriv->base_port = board->num_di_ports;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[2];
	if (board->num_dio_ports) {
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan =
		    board->num_dio_ports * ni_65xx_channels_per_port;
		s->range_table = &range_digital;
		s->maxdata = 1;
		s->insn_config = ni_65xx_dio_insn_config;
		s->insn_bits = ni_65xx_dio_insn_bits;
		spriv = comedi_alloc_spriv(s, sizeof(*spriv));
		if (!spriv)
			return -ENOMEM;
		spriv->base_port = 0;
		for (i = 0; i < board->num_dio_ports; ++i) {
			/*  configure all ports for input */
			writeb(0x1,
			       devpriv->mite->daq_io_addr +
			       Port_Select(i));
		}
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[3];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
	s->n_chan = 1;
	s->range_table = &range_unknown;
	s->maxdata = 1;
	s->do_cmdtest = ni_65xx_intr_cmdtest;
	s->do_cmd = ni_65xx_intr_cmd;
	s->cancel = ni_65xx_intr_cancel;
	s->insn_bits = ni_65xx_intr_insn_bits;
	s->insn_config = ni_65xx_intr_insn_config;

	for (i = 0; i < ni_65xx_total_num_ports(board); ++i) {
		writeb(0x00,
		       devpriv->mite->daq_io_addr + Filter_Enable(i));
		if (board->invert_outputs)
			writeb(0x01,
			       devpriv->mite->daq_io_addr + Port_Data(i));
		else
			writeb(0x00,
			       devpriv->mite->daq_io_addr + Port_Data(i));
	}
	writeb(ClrEdge | ClrOverflow,
	       devpriv->mite->daq_io_addr + Clear_Register);
	writeb(0x00,
	       devpriv->mite->daq_io_addr + Master_Interrupt_Control);

	/* Set filter interval to 0  (32bit reg) */
	writeb(0x00000000, devpriv->mite->daq_io_addr + Filter_Interval);

	ret = request_irq(dev->irq, ni_65xx_interrupt, IRQF_SHARED,
			  "ni_65xx", dev);
	if (ret < 0) {
		dev->irq = 0;
		dev_warn(dev->class_dev, "irq not available\n");
	}

	return 0;
}

static void ni_65xx_detach(struct comedi_device *dev)
{
	struct ni_65xx_private *devpriv = dev->private;

	if (devpriv && devpriv->mite && devpriv->mite->daq_io_addr) {
		writeb(0x00,
		       devpriv->mite->daq_io_addr +
		       Master_Interrupt_Control);
	}
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->mite) {
			mite_unsetup(devpriv->mite);
			mite_free(devpriv->mite);
		}
	}
	comedi_pci_disable(dev);
}

static struct comedi_driver ni_65xx_driver = {
	.driver_name = "ni_65xx",
	.module = THIS_MODULE,
	.auto_attach = ni_65xx_auto_attach,
	.detach = ni_65xx_detach,
};

static int ni_65xx_pci_probe(struct pci_dev *dev,
			     const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ni_65xx_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(ni_65xx_pci_table) = {
	{ PCI_VDEVICE(NI, 0x1710), BOARD_PXI6509 },
	{ PCI_VDEVICE(NI, 0x7085), BOARD_PCI6509 },
	{ PCI_VDEVICE(NI, 0x7086), BOARD_PXI6528 },
	{ PCI_VDEVICE(NI, 0x7087), BOARD_PCI6515 },
	{ PCI_VDEVICE(NI, 0x7088), BOARD_PCI6514 },
	{ PCI_VDEVICE(NI, 0x70a9), BOARD_PCI6528 },
	{ PCI_VDEVICE(NI, 0x70c3), BOARD_PCI6511 },
	{ PCI_VDEVICE(NI, 0x70c8), BOARD_PCI6513 },
	{ PCI_VDEVICE(NI, 0x70c9), BOARD_PXI6515 },
	{ PCI_VDEVICE(NI, 0x70cc), BOARD_PCI6512 },
	{ PCI_VDEVICE(NI, 0x70cd), BOARD_PXI6514 },
	{ PCI_VDEVICE(NI, 0x70d1), BOARD_PXI6513 },
	{ PCI_VDEVICE(NI, 0x70d2), BOARD_PXI6512 },
	{ PCI_VDEVICE(NI, 0x70d3), BOARD_PXI6511 },
	{ PCI_VDEVICE(NI, 0x7124), BOARD_PCI6510 },
	{ PCI_VDEVICE(NI, 0x7125), BOARD_PCI6516 },
	{ PCI_VDEVICE(NI, 0x7126), BOARD_PCI6517 },
	{ PCI_VDEVICE(NI, 0x7127), BOARD_PCI6518 },
	{ PCI_VDEVICE(NI, 0x7128), BOARD_PCI6519 },
	{ PCI_VDEVICE(NI, 0x718b), BOARD_PCI6521 },
	{ PCI_VDEVICE(NI, 0x718c), BOARD_PXI6521 },
	{ PCI_VDEVICE(NI, 0x71c5), BOARD_PCI6520 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_65xx_pci_table);

static struct pci_driver ni_65xx_pci_driver = {
	.name		= "ni_65xx",
	.id_table	= ni_65xx_pci_table,
	.probe		= ni_65xx_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ni_65xx_driver, ni_65xx_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

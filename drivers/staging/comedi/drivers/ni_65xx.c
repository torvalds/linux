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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

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

#define _GNU_SOURCE
#define DEBUG 1
#define DEBUG_FLAGS
#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"

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

static int ni_65xx_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static void ni_65xx_detach(struct comedi_device *dev);
static struct comedi_driver ni_65xx_driver = {
	.driver_name = "ni_65xx",
	.module = THIS_MODULE,
	.attach = ni_65xx_attach,
	.detach = ni_65xx_detach,
};

struct ni_65xx_board {

	int dev_id;
	const char *name;
	unsigned num_dio_ports;
	unsigned num_di_ports;
	unsigned num_do_ports;
	unsigned invert_outputs:1;
};

static const struct ni_65xx_board ni_65xx_boards[] = {
	{
	 .dev_id = 0x7085,
	 .name = "pci-6509",
	 .num_dio_ports = 12,
	 .invert_outputs = 0},
	{
	 .dev_id = 0x1710,
	 .name = "pxi-6509",
	 .num_dio_ports = 12,
	 .invert_outputs = 0},
	{
	 .dev_id = 0x7124,
	 .name = "pci-6510",
	 .num_di_ports = 4},
	{
	 .dev_id = 0x70c3,
	 .name = "pci-6511",
	 .num_di_ports = 8},
	{
	 .dev_id = 0x70d3,
	 .name = "pxi-6511",
	 .num_di_ports = 8},
	{
	 .dev_id = 0x70cc,
	 .name = "pci-6512",
	 .num_do_ports = 8},
	{
	 .dev_id = 0x70d2,
	 .name = "pxi-6512",
	 .num_do_ports = 8},
	{
	 .dev_id = 0x70c8,
	 .name = "pci-6513",
	 .num_do_ports = 8,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x70d1,
	 .name = "pxi-6513",
	 .num_do_ports = 8,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x7088,
	 .name = "pci-6514",
	 .num_di_ports = 4,
	 .num_do_ports = 4,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x70CD,
	 .name = "pxi-6514",
	 .num_di_ports = 4,
	 .num_do_ports = 4,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x7087,
	 .name = "pci-6515",
	 .num_di_ports = 4,
	 .num_do_ports = 4,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x70c9,
	 .name = "pxi-6515",
	 .num_di_ports = 4,
	 .num_do_ports = 4,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x7125,
	 .name = "pci-6516",
	 .num_do_ports = 4,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x7126,
	 .name = "pci-6517",
	 .num_do_ports = 4,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x7127,
	 .name = "pci-6518",
	 .num_di_ports = 2,
	 .num_do_ports = 2,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x7128,
	 .name = "pci-6519",
	 .num_di_ports = 2,
	 .num_do_ports = 2,
	 .invert_outputs = 1},
	{
	 .dev_id = 0x71c5,
	 .name = "pci-6520",
	 .num_di_ports = 1,
	 .num_do_ports = 1,
	 },
	{
	 .dev_id = 0x718b,
	 .name = "pci-6521",
	 .num_di_ports = 1,
	 .num_do_ports = 1,
	 },
	{
	 .dev_id = 0x718c,
	 .name = "pxi-6521",
	 .num_di_ports = 1,
	 .num_do_ports = 1,
	 },
	{
	 .dev_id = 0x70a9,
	 .name = "pci-6528",
	 .num_di_ports = 3,
	 .num_do_ports = 3,
	 },
	{
	 .dev_id = 0x7086,
	 .name = "pxi-6528",
	 .num_di_ports = 3,
	 .num_do_ports = 3,
	 },
};

#define n_ni_65xx_boards ARRAY_SIZE(ni_65xx_boards)
static inline const struct ni_65xx_board *board(struct comedi_device *dev)
{
	return dev->board_ptr;
}

static inline unsigned ni_65xx_port_by_channel(unsigned channel)
{
	return channel / ni_65xx_channels_per_port;
}

static inline unsigned ni_65xx_total_num_ports(const struct ni_65xx_board
					       *board)
{
	return board->num_dio_ports + board->num_di_ports + board->num_do_ports;
}

static DEFINE_PCI_DEVICE_TABLE(ni_65xx_pci_table) = {
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x1710)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7085)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7086)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7087)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7088)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70a9)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70c3)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70c8)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70c9)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70cc)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70CD)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70d1)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70d2)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x70d3)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7124)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7125)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7126)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7127)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x7128)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x718b)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x718c)},
	{PCI_DEVICE(PCI_VENDOR_ID_NI, 0x71c5)},
	{0}
};

MODULE_DEVICE_TABLE(pci, ni_65xx_pci_table);

struct ni_65xx_private {
	struct mite_struct *mite;
	unsigned int filter_interval;
	unsigned short filter_enable[NI_65XX_MAX_NUM_PORTS];
	unsigned short output_bits[NI_65XX_MAX_NUM_PORTS];
	unsigned short dio_direction[NI_65XX_MAX_NUM_PORTS];
};

static inline struct ni_65xx_private *private(struct comedi_device *dev)
{
	return dev->private;
}

struct ni_65xx_subdevice_private {
	unsigned base_port;
};

static inline struct ni_65xx_subdevice_private *sprivate(struct comedi_subdevice
							 *subdev)
{
	return subdev->private;
}

static struct ni_65xx_subdevice_private *ni_65xx_alloc_subdevice_private(void)
{
	struct ni_65xx_subdevice_private *subdev_private =
	    kzalloc(sizeof(struct ni_65xx_subdevice_private), GFP_KERNEL);
	if (subdev_private == NULL)
		return NULL;
	return subdev_private;
}

static int ni_65xx_find_device(struct comedi_device *dev, int bus, int slot);

static int ni_65xx_config_filter(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
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

		if (interval != private(dev)->filter_interval) {
			writeb(interval,
			       private(dev)->mite->daq_io_addr +
			       Filter_Interval);
			private(dev)->filter_interval = interval;
		}

		private(dev)->filter_enable[port] |=
		    1 << (chan % ni_65xx_channels_per_port);
	} else {
		private(dev)->filter_enable[port] &=
		    ~(1 << (chan % ni_65xx_channels_per_port));
	}

	writeb(private(dev)->filter_enable[port],
	       private(dev)->mite->daq_io_addr + Filter_Enable(port));

	return 2;
}

static int ni_65xx_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
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
		private(dev)->dio_direction[port] = COMEDI_OUTPUT;
		writeb(0, private(dev)->mite->daq_io_addr + Port_Select(port));
		return 1;
		break;
	case INSN_CONFIG_DIO_INPUT:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		private(dev)->dio_direction[port] = COMEDI_INPUT;
		writeb(1, private(dev)->mite->daq_io_addr + Port_Select(port));
		return 1;
		break;
	case INSN_CONFIG_DIO_QUERY:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		data[1] = private(dev)->dio_direction[port];
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
		if (port >= ni_65xx_total_num_ports(board(dev)))
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
			private(dev)->output_bits[port] &= ~port_mask;
			private(dev)->output_bits[port] |=
			    port_data & port_mask;
			bits = private(dev)->output_bits[port];
			if (board(dev)->invert_outputs)
				bits = ~bits;
			writeb(bits,
			       private(dev)->mite->daq_io_addr +
			       Port_Data(port));
		}
		port_read_bits =
		    readb(private(dev)->mite->daq_io_addr + Port_Data(port));
		if (s->type == COMEDI_SUBD_DO && board(dev)->invert_outputs) {
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
	struct comedi_subdevice *s = &dev->subdevices[2];
	unsigned int status;

	status = readb(private(dev)->mite->daq_io_addr + Change_Status);
	if ((status & MasterInterruptStatus) == 0)
		return IRQ_NONE;
	if ((status & EdgeStatus) == 0)
		return IRQ_NONE;

	writeb(ClrEdge | ClrOverflow,
	       private(dev)->mite->daq_io_addr + Clear_Register);

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

	/* step 2: make sure trigger sources are unique and mutually
	compatible */

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

static int ni_65xx_intr_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	/* struct comedi_cmd *cmd = &s->async->cmd; */

	writeb(ClrEdge | ClrOverflow,
	       private(dev)->mite->daq_io_addr + Clear_Register);
	writeb(FallingEdgeIntEnable | RisingEdgeIntEnable |
	       MasterInterruptEnable | EdgeIntEnable,
	       private(dev)->mite->daq_io_addr + Master_Interrupt_Control);

	return 0;
}

static int ni_65xx_intr_cancel(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	writeb(0x00,
	       private(dev)->mite->daq_io_addr + Master_Interrupt_Control);

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
	if (insn->n < 1)
		return -EINVAL;
	if (data[0] != INSN_CONFIG_CHANGE_NOTIFY)
		return -EINVAL;

	writeb(data[1],
	       private(dev)->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0));
	writeb(data[1] >> 8,
	       private(dev)->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0x10));
	writeb(data[1] >> 16,
	       private(dev)->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0x20));
	writeb(data[1] >> 24,
	       private(dev)->mite->daq_io_addr +
	       Rising_Edge_Detection_Enable(0x30));

	writeb(data[2],
	       private(dev)->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0));
	writeb(data[2] >> 8,
	       private(dev)->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0x10));
	writeb(data[2] >> 16,
	       private(dev)->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0x20));
	writeb(data[2] >> 24,
	       private(dev)->mite->daq_io_addr +
	       Falling_Edge_Detection_Enable(0x30));

	return 2;
}

static int ni_65xx_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned i;
	int ret;

	ret = alloc_private(dev, sizeof(struct ni_65xx_private));
	if (ret < 0)
		return ret;

	ret = ni_65xx_find_device(dev, it->options[0], it->options[1]);
	if (ret < 0)
		return ret;

	ret = mite_setup(private(dev)->mite);
	if (ret < 0) {
		dev_warn(dev->class_dev, "error setting up mite\n");
		return ret;
	}

	dev->board_name = board(dev)->name;
	dev->irq = mite_irq(private(dev)->mite);
	dev_info(dev->class_dev, "board: %s, ID=0x%02x", dev->board_name,
	       readb(private(dev)->mite->daq_io_addr + ID_Register));

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	if (board(dev)->num_di_ports) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE;
		s->n_chan =
		    board(dev)->num_di_ports * ni_65xx_channels_per_port;
		s->range_table = &range_digital;
		s->maxdata = 1;
		s->insn_config = ni_65xx_dio_insn_config;
		s->insn_bits = ni_65xx_dio_insn_bits;
		s->private = ni_65xx_alloc_subdevice_private();
		if (s->private == NULL)
			return -ENOMEM;
		sprivate(s)->base_port = 0;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[1];
	if (board(dev)->num_do_ports) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan =
		    board(dev)->num_do_ports * ni_65xx_channels_per_port;
		s->range_table = &range_digital;
		s->maxdata = 1;
		s->insn_bits = ni_65xx_dio_insn_bits;
		s->private = ni_65xx_alloc_subdevice_private();
		if (s->private == NULL)
			return -ENOMEM;
		sprivate(s)->base_port = board(dev)->num_di_ports;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[2];
	if (board(dev)->num_dio_ports) {
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan =
		    board(dev)->num_dio_ports * ni_65xx_channels_per_port;
		s->range_table = &range_digital;
		s->maxdata = 1;
		s->insn_config = ni_65xx_dio_insn_config;
		s->insn_bits = ni_65xx_dio_insn_bits;
		s->private = ni_65xx_alloc_subdevice_private();
		if (s->private == NULL)
			return -ENOMEM;
		sprivate(s)->base_port = 0;
		for (i = 0; i < board(dev)->num_dio_ports; ++i) {
			/*  configure all ports for input */
			writeb(0x1,
			       private(dev)->mite->daq_io_addr +
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

	for (i = 0; i < ni_65xx_total_num_ports(board(dev)); ++i) {
		writeb(0x00,
		       private(dev)->mite->daq_io_addr + Filter_Enable(i));
		if (board(dev)->invert_outputs)
			writeb(0x01,
			       private(dev)->mite->daq_io_addr + Port_Data(i));
		else
			writeb(0x00,
			       private(dev)->mite->daq_io_addr + Port_Data(i));
	}
	writeb(ClrEdge | ClrOverflow,
	       private(dev)->mite->daq_io_addr + Clear_Register);
	writeb(0x00,
	       private(dev)->mite->daq_io_addr + Master_Interrupt_Control);

	/* Set filter interval to 0  (32bit reg) */
	writeb(0x00000000, private(dev)->mite->daq_io_addr + Filter_Interval);

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
	if (private(dev) && private(dev)->mite
	    && private(dev)->mite->daq_io_addr) {
		writeb(0x00,
		       private(dev)->mite->daq_io_addr +
		       Master_Interrupt_Control);
	}
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (private(dev)) {
		struct comedi_subdevice *s;
		unsigned i;

		for (i = 0; i < dev->n_subdevices; ++i) {
			s = &dev->subdevices[i];
			kfree(s->private);
			s->private = NULL;
		}
		if (private(dev)->mite)
			mite_unsetup(private(dev)->mite);
	}
}

static int ni_65xx_find_device(struct comedi_device *dev, int bus, int slot)
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
		for (i = 0; i < n_ni_65xx_boards; i++) {
			if (mite_device_id(mite) == ni_65xx_boards[i].dev_id) {
				dev->board_ptr = ni_65xx_boards + i;
				private(dev)->mite = mite;
				return 0;
			}
		}
	}
	dev_warn(dev->class_dev, "no device found\n");
	mite_list_devices();
	return -EIO;
}

static int __devinit ni_65xx_pci_probe(struct pci_dev *dev,
				       const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &ni_65xx_driver);
}

static void __devexit ni_65xx_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver ni_65xx_pci_driver = {
	.name = "ni_65xx",
	.id_table = ni_65xx_pci_table,
	.probe = ni_65xx_pci_probe,
	.remove = __devexit_p(ni_65xx_pci_remove)
};
module_comedi_pci_driver(ni_65xx_driver, ni_65xx_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

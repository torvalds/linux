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

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_fc.h"

/*
 * PCI BAR1 Register Map
 */

/* Non-recurring Registers (8-bit except where noted) */
#define NI_65XX_ID_REG			0x00
#define NI_65XX_CLR_REG			0x01
#define NI_65XX_CLR_WDOG_INT		(1 << 6)
#define NI_65XX_CLR_WDOG_PING		(1 << 5)
#define NI_65XX_CLR_WDOG_EXP		(1 << 4)
#define NI_65XX_CLR_EDGE_INT		(1 << 3)
#define NI_65XX_CLR_OVERFLOW_INT	(1 << 2)
#define NI_65XX_STATUS_REG		0x02
#define NI_65XX_STATUS_WDOG_INT		(1 << 5)
#define NI_65XX_STATUS_FALL_EDGE	(1 << 4)
#define NI_65XX_STATUS_RISE_EDGE	(1 << 3)
#define NI_65XX_STATUS_INT		(1 << 2)
#define NI_65XX_STATUS_OVERFLOW_INT	(1 << 1)
#define NI_65XX_STATUS_EDGE_INT		(1 << 0)
#define NI_65XX_CTRL_REG		0x03
#define NI_65XX_CTRL_WDOG_ENA		(1 << 5)
#define NI_65XX_CTRL_FALL_EDGE_ENA	(1 << 4)
#define NI_65XX_CTRL_RISE_EDGE_ENA	(1 << 3)
#define NI_65XX_CTRL_INT_ENA		(1 << 2)
#define NI_65XX_CTRL_OVERFLOW_ENA	(1 << 1)
#define NI_65XX_CTRL_EDGE_ENA		(1 << 0)
#define NI_65XX_REV_REG			0x04 /* 32-bit */
#define NI_65XX_FILTER_REG		0x08 /* 32-bit */
#define NI_65XX_RTSI_ROUTE_REG		0x0c /* 16-bit */
#define NI_65XX_RTSI_EDGE_REG		0x0e /* 16-bit */
#define NI_65XX_RTSI_WDOG_REG		0x10 /* 16-bit */
#define NI_65XX_RTSI_TRIG_REG		0x12 /* 16-bit */
#define NI_65XX_AUTO_CLK_SEL_REG	0x14 /* PXI-6528 only */
#define NI_65XX_AUTO_CLK_SEL_STATUS	(1 << 1)
#define NI_65XX_AUTO_CLK_SEL_DISABLE	(1 << 0)
#define NI_65XX_WDOG_CTRL_REG		0x15
#define NI_65XX_WDOG_CTRL_ENA		(1 << 0)
#define NI_65XX_RTSI_CFG_REG		0x16
#define NI_65XX_RTSI_CFG_RISE_SENSE	(1 << 2)
#define NI_65XX_RTSI_CFG_FALL_SENSE	(1 << 1)
#define NI_65XX_RTSI_CFG_SYNC_DETECT	(1 << 0)
#define NI_65XX_WDOG_STATUS_REG		0x17
#define NI_65XX_WDOG_STATUS_EXP		(1 << 0)
#define NI_65XX_WDOG_INTERVAL_REG	0x18 /* 32-bit */

/* Recurring port registers (8-bit) */
#define NI_65XX_PORT(x)			((x) * 0x10)
#define NI_65XX_IO_DATA_REG(x)		(0x40 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_REG(x)		(0x41 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_OUTPUT		(0 << 0)
#define NI_65XX_IO_SEL_INPUT		(1 << 0)
#define NI_65XX_RISE_EDGE_ENA_REG(x)	(0x42 + NI_65XX_PORT(x))
#define NI_65XX_FALL_EDGE_ENA_REG(x)	(0x43 + NI_65XX_PORT(x))
#define NI_65XX_FILTER_ENA(x)		(0x44 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HIZ_REG(x)		(0x46 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_ENA(x)		(0x47 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HI_LO_REG(x)	(0x48 + NI_65XX_PORT(x))
#define NI_65XX_RTSI_ENA(x)		(0x49 + NI_65XX_PORT(x))

#define NI_65XX_MAX_NUM_PORTS 12
static const unsigned ni_65xx_channels_per_port = 8;

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
	void __iomem *mmio;
	unsigned short output_bits[NI_65XX_MAX_NUM_PORTS];
};

struct ni_65xx_subdevice_private {
	unsigned base_port;
};

static inline struct ni_65xx_subdevice_private *sprivate(struct comedi_subdevice
							 *subdev)
{
	return subdev->private;
}

static int ni_65xx_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct ni_65xx_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int chan_mask = 1 << (chan % ni_65xx_channels_per_port);
	unsigned port = sprivate(s)->base_port + ni_65xx_port_by_channel(chan);
	unsigned int interval;
	unsigned int val;

	switch (data[0]) {
	case INSN_CONFIG_FILTER:
		/*
		 * The deglitch filter interval is specified in nanoseconds.
		 * The hardware supports intervals in 200ns increments. Round
		 * the user values up and return the actual interval.
		 */
		interval = (data[1] + 100) / 200;
		if (interval > 0xfffff)
			interval = 0xfffff;
		data[1] = interval * 200;

		/*
		 * Enable/disable the channel for deglitch filtering. Note
		 * that the filter interval is never set to '0'. This is done
		 * because other channels might still be enabled for filtering.
		 */
		val = readb(devpriv->mmio + NI_65XX_FILTER_ENA(port));
		if (interval) {
			writel(interval, devpriv->mmio + NI_65XX_FILTER_REG);
			val |= chan_mask;
		} else {
			val &= ~chan_mask;
		}
		writeb(val, devpriv->mmio + NI_65XX_FILTER_ENA(port));
		break;

	case INSN_CONFIG_DIO_OUTPUT:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		writeb(NI_65XX_IO_SEL_OUTPUT,
		       devpriv->mmio + NI_65XX_IO_SEL_REG(port));
		break;

	case INSN_CONFIG_DIO_INPUT:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		writeb(NI_65XX_IO_SEL_INPUT,
		       devpriv->mmio + NI_65XX_IO_SEL_REG(port));
		break;

	case INSN_CONFIG_DIO_QUERY:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		val = readb(devpriv->mmio + NI_65XX_IO_SEL_REG(port));
		data[1] = (val == NI_65XX_IO_SEL_INPUT) ? COMEDI_INPUT
							: COMEDI_OUTPUT;
		break;

	default:
		return -EINVAL;
	}

	return insn->n;
}

static int ni_65xx_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	const struct ni_65xx_board *board = comedi_board(dev);
	struct ni_65xx_private *devpriv = dev->private;
	int base_bitfield_channel;
	unsigned read_bits = 0;
	int last_port_offset = ni_65xx_port_by_channel(s->n_chan - 1);
	int port_offset;

	base_bitfield_channel = CR_CHAN(insn->chanspec);
	for (port_offset = ni_65xx_port_by_channel(base_bitfield_channel);
	     port_offset <= last_port_offset; port_offset++) {
		unsigned port = sprivate(s)->base_port + port_offset;
		int base_port_channel = port_offset * ni_65xx_channels_per_port;
		unsigned port_mask, port_data, port_read_bits;
		int bitshift = base_port_channel - base_bitfield_channel;

		if (bitshift >= 32)
			break;
		port_mask = data[0];
		port_data = data[1];
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
			writeb(bits, devpriv->mmio + NI_65XX_IO_DATA_REG(port));
		}
		port_read_bits = readb(devpriv->mmio +
				       NI_65XX_IO_DATA_REG(port));
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
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status;

	status = readb(devpriv->mmio + NI_65XX_STATUS_REG);
	if ((status & NI_65XX_STATUS_INT) == 0)
		return IRQ_NONE;
	if ((status & NI_65XX_STATUS_EDGE_INT) == 0)
		return IRQ_NONE;

	writeb(NI_65XX_CLR_EDGE_INT | NI_65XX_CLR_OVERFLOW_INT,
	       devpriv->mmio + NI_65XX_CLR_REG);

	comedi_buf_put(s, 0);
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
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);
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

	writeb(NI_65XX_CLR_EDGE_INT | NI_65XX_CLR_OVERFLOW_INT,
	       devpriv->mmio + NI_65XX_CLR_REG);
	writeb(NI_65XX_CTRL_FALL_EDGE_ENA | NI_65XX_CTRL_RISE_EDGE_ENA |
	       NI_65XX_CTRL_INT_ENA | NI_65XX_CTRL_EDGE_ENA,
	       devpriv->mmio + NI_65XX_CTRL_REG);

	return 0;
}

static int ni_65xx_intr_cancel(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct ni_65xx_private *devpriv = dev->private;

	writeb(0x00, devpriv->mmio + NI_65XX_CTRL_REG);

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

	writeb(data[1], devpriv->mmio + NI_65XX_RISE_EDGE_ENA_REG(0));
	writeb(data[1] >> 8, devpriv->mmio + NI_65XX_RISE_EDGE_ENA_REG(0x10));
	writeb(data[1] >> 16, devpriv->mmio + NI_65XX_RISE_EDGE_ENA_REG(0x20));
	writeb(data[1] >> 24, devpriv->mmio + NI_65XX_RISE_EDGE_ENA_REG(0x30));

	writeb(data[2], devpriv->mmio + NI_65XX_FALL_EDGE_ENA_REG(0));
	writeb(data[2] >> 8, devpriv->mmio + NI_65XX_FALL_EDGE_ENA_REG(0x10));
	writeb(data[2] >> 16, devpriv->mmio + NI_65XX_FALL_EDGE_ENA_REG(0x20));
	writeb(data[2] >> 24, devpriv->mmio + NI_65XX_FALL_EDGE_ENA_REG(0x30));

	return 2;
}

/* ripped from mite.h and mite_setup2() to avoid mite dependancy */
#define MITE_IODWBSR	0xc0	 /* IO Device Window Base Size Register */
#define WENAB		(1 << 7) /* window enable */

static int ni_65xx_mite_init(struct pci_dev *pcidev)
{
	void __iomem *mite_base;
	u32 main_phys_addr;

	/* ioremap the MITE registers (BAR 0) temporarily */
	mite_base = pci_ioremap_bar(pcidev, 0);
	if (!mite_base)
		return -ENOMEM;

	/* set data window to main registers (BAR 1) */
	main_phys_addr = pci_resource_start(pcidev, 1);
	writel(main_phys_addr | WENAB, mite_base + MITE_IODWBSR);

	/* finished with MITE registers */
	iounmap(mite_base);
	return 0;
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

	ret = ni_65xx_mite_init(pcidev);
	if (ret)
		return ret;

	devpriv->mmio = pci_ioremap_bar(pcidev, 1);
	if (!devpriv->mmio)
		return -ENOMEM;

	dev->irq = pcidev->irq;
	dev_info(dev->class_dev, "board: %s, ID=0x%02x", dev->board_name,
	       readb(devpriv->mmio + NI_65XX_ID_REG));

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
		/* configure all ports for input */
		for (i = 0; i < board->num_dio_ports; ++i) {
			writeb(NI_65XX_IO_SEL_INPUT,
			       devpriv->mmio + NI_65XX_IO_SEL_REG(i));
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
	s->len_chanlist	= 1;
	s->do_cmdtest = ni_65xx_intr_cmdtest;
	s->do_cmd = ni_65xx_intr_cmd;
	s->cancel = ni_65xx_intr_cancel;
	s->insn_bits = ni_65xx_intr_insn_bits;
	s->insn_config = ni_65xx_intr_insn_config;

	for (i = 0; i < ni_65xx_total_num_ports(board); ++i) {
		writeb(0x00, devpriv->mmio + NI_65XX_FILTER_ENA(i));
		if (board->invert_outputs)
			writeb(0x01, devpriv->mmio + NI_65XX_IO_DATA_REG(i));
		else
			writeb(0x00, devpriv->mmio + NI_65XX_IO_DATA_REG(i));
	}
	writeb(NI_65XX_CLR_EDGE_INT | NI_65XX_CLR_OVERFLOW_INT,
	       devpriv->mmio + NI_65XX_CLR_REG);
	writeb(0x00, devpriv->mmio + NI_65XX_CTRL_REG);

	/* Set filter interval to 0  (32bit reg) */
	writel(0x00000000, devpriv->mmio + NI_65XX_FILTER_REG);

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

	if (devpriv && devpriv->mmio) {
		writeb(0x00, devpriv->mmio + NI_65XX_CTRL_REG);
		iounmap(devpriv->mmio);
	}
	if (dev->irq)
		free_irq(dev->irq, dev);
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

static const struct pci_device_id ni_65xx_pci_table[] = {
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

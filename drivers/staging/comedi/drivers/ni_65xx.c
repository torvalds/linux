/*
 * ni_65xx.c
 * Comedi driver for National Instruments PCI-65xx static dio boards
 *
 * Copyright (C) 2006 Jon Grierson <jd@renko.co.uk>
 * Copyright (C) 2006 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1999,2002,2003 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: ni_65xx
 * Description: National Instruments 65xx static dio boards
 * Author: Jon Grierson <jd@renko.co.uk>,
 *	   Frank Mori Hess <fmhess@users.sourceforge.net>
 * Status: testing
 * Devices: [National Instruments] PCI-6509 (pci-6509), PXI-6509 (pxi-6509),
 *   PCI-6510 (pci-6510), PCI-6511 (pci-6511), PXI-6511 (pxi-6511),
 *   PCI-6512 (pci-6512), PXI-6512 (pxi-6512), PCI-6513 (pci-6513),
 *   PXI-6513 (pxi-6513), PCI-6514 (pci-6514), PXI-6514 (pxi-6514),
 *   PCI-6515 (pxi-6515), PXI-6515 (pxi-6515), PCI-6516 (pci-6516),
 *   PCI-6517 (pci-6517), PCI-6518 (pci-6518), PCI-6519 (pci-6519),
 *   PCI-6520 (pci-6520), PCI-6521 (pci-6521), PXI-6521 (pxi-6521),
 *   PCI-6528 (pci-6528), PXI-6528 (pxi-6528)
 * Updated: Mon, 21 Jul 2014 12:49:58 +0000
 *
 * Configuration Options: not applicable, uses PCI auto config
 *
 * Based on the PCI-6527 driver by ds.
 * The interrupt subdevice (subdevice 3) is probably broken for all
 * boards except maybe the 6514.
 *
 * This driver previously inverted the outputs on PCI-6513 through to
 * PCI-6519 and on PXI-6513 through to PXI-6515.  It no longer inverts
 * outputs on those cards by default as it didn't make much sense.  If
 * you require the outputs to be inverted on those cards for legacy
 * reasons, set the module parameter "legacy_invert_outputs=true" when
 * loading the module, or set "ni_65xx.legacy_invert_outputs=true" on
 * the kernel command line if the driver is built in to the kernel.
 */

/*
 * Manuals (available from ftp://ftp.natinst.com/support/manuals)
 *
 *	370106b.pdf	6514 Register Level Programmer Manual
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

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

#define NI_65XX_PORT_TO_CHAN(x)		((x) * 8)
#define NI_65XX_CHAN_TO_PORT(x)		((x) / 8)
#define NI_65XX_CHAN_TO_MASK(x)		(1 << ((x) % 8))

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
	unsigned legacy_invert:1;
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
		.legacy_invert	= 1,
	},
	[BOARD_PXI6513] = {
		.name		= "pxi-6513",
		.num_do_ports	= 8,
		.legacy_invert	= 1,
	},
	[BOARD_PCI6514] = {
		.name		= "pci-6514",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.legacy_invert	= 1,
	},
	[BOARD_PXI6514] = {
		.name		= "pxi-6514",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.legacy_invert	= 1,
	},
	[BOARD_PCI6515] = {
		.name		= "pci-6515",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.legacy_invert	= 1,
	},
	[BOARD_PXI6515] = {
		.name		= "pxi-6515",
		.num_di_ports	= 4,
		.num_do_ports	= 4,
		.legacy_invert	= 1,
	},
	[BOARD_PCI6516] = {
		.name		= "pci-6516",
		.num_do_ports	= 4,
		.legacy_invert	= 1,
	},
	[BOARD_PCI6517] = {
		.name		= "pci-6517",
		.num_do_ports	= 4,
		.legacy_invert	= 1,
	},
	[BOARD_PCI6518] = {
		.name		= "pci-6518",
		.num_di_ports	= 2,
		.num_do_ports	= 2,
		.legacy_invert	= 1,
	},
	[BOARD_PCI6519] = {
		.name		= "pci-6519",
		.num_di_ports	= 2,
		.num_do_ports	= 2,
		.legacy_invert	= 1,
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

static bool ni_65xx_legacy_invert_outputs;
module_param_named(legacy_invert_outputs, ni_65xx_legacy_invert_outputs,
		   bool, 0444);
MODULE_PARM_DESC(legacy_invert_outputs,
		 "invert outputs of PCI/PXI-6513/6514/6515/6516/6517/6518/6519 for compatibility with old user code");

static unsigned int ni_65xx_num_ports(struct comedi_device *dev)
{
	const struct ni_65xx_board *board = dev->board_ptr;

	return board->num_dio_ports + board->num_di_ports + board->num_do_ports;
}

static void ni_65xx_disable_input_filters(struct comedi_device *dev)
{
	unsigned int num_ports = ni_65xx_num_ports(dev);
	int i;

	/* disable input filtering on all ports */
	for (i = 0; i < num_ports; ++i)
		writeb(0x00, dev->mmio + NI_65XX_FILTER_ENA(i));

	/* set filter interval to 0 (32bit reg) */
	writel(0x00000000, dev->mmio + NI_65XX_FILTER_REG);
}

/* updates edge detection for base_chan to base_chan+31 */
static void ni_65xx_update_edge_detection(struct comedi_device *dev,
					  unsigned int base_chan,
					  unsigned int rising,
					  unsigned int falling)
{
	unsigned int num_ports = ni_65xx_num_ports(dev);
	unsigned int port;

	if (base_chan >= NI_65XX_PORT_TO_CHAN(num_ports))
		return;

	for (port = NI_65XX_CHAN_TO_PORT(base_chan); port < num_ports; port++) {
		int bitshift = (int)(NI_65XX_PORT_TO_CHAN(port) - base_chan);
		unsigned int port_mask, port_rising, port_falling;

		if (bitshift >= 32)
			break;

		if (bitshift >= 0) {
			port_mask = ~0U >> bitshift;
			port_rising = rising >> bitshift;
			port_falling = falling >> bitshift;
		} else {
			port_mask = ~0U << -bitshift;
			port_rising = rising << -bitshift;
			port_falling = falling << -bitshift;
		}
		if (port_mask & 0xff) {
			if (~port_mask & 0xff) {
				port_rising |=
				    readb(dev->mmio +
					  NI_65XX_RISE_EDGE_ENA_REG(port)) &
				    ~port_mask;
				port_falling |=
				    readb(dev->mmio +
					  NI_65XX_FALL_EDGE_ENA_REG(port)) &
				    ~port_mask;
			}
			writeb(port_rising & 0xff,
			       dev->mmio + NI_65XX_RISE_EDGE_ENA_REG(port));
			writeb(port_falling & 0xff,
			       dev->mmio + NI_65XX_FALL_EDGE_ENA_REG(port));
		}
	}
}

static void ni_65xx_disable_edge_detection(struct comedi_device *dev)
{
	/* clear edge detection for channels 0 to 31 */
	ni_65xx_update_edge_detection(dev, 0, 0, 0);
	/* clear edge detection for channels 32 to 63 */
	ni_65xx_update_edge_detection(dev, 32, 0, 0);
	/* clear edge detection for channels 64 to 95 */
	ni_65xx_update_edge_detection(dev, 64, 0, 0);
}

static int ni_65xx_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	unsigned long base_port = (unsigned long)s->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int chan_mask = NI_65XX_CHAN_TO_MASK(chan);
	unsigned port = base_port + NI_65XX_CHAN_TO_PORT(chan);
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
		val = readb(dev->mmio + NI_65XX_FILTER_ENA(port));
		if (interval) {
			writel(interval, dev->mmio + NI_65XX_FILTER_REG);
			val |= chan_mask;
		} else {
			val &= ~chan_mask;
		}
		writeb(val, dev->mmio + NI_65XX_FILTER_ENA(port));
		break;

	case INSN_CONFIG_DIO_OUTPUT:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		writeb(NI_65XX_IO_SEL_OUTPUT,
		       dev->mmio + NI_65XX_IO_SEL_REG(port));
		break;

	case INSN_CONFIG_DIO_INPUT:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		writeb(NI_65XX_IO_SEL_INPUT,
		       dev->mmio + NI_65XX_IO_SEL_REG(port));
		break;

	case INSN_CONFIG_DIO_QUERY:
		if (s->type != COMEDI_SUBD_DIO)
			return -EINVAL;
		val = readb(dev->mmio + NI_65XX_IO_SEL_REG(port));
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
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	unsigned long base_port = (unsigned long)s->private;
	unsigned int base_chan = CR_CHAN(insn->chanspec);
	int last_port_offset = NI_65XX_CHAN_TO_PORT(s->n_chan - 1);
	unsigned read_bits = 0;
	int port_offset;

	for (port_offset = NI_65XX_CHAN_TO_PORT(base_chan);
	     port_offset <= last_port_offset; port_offset++) {
		unsigned port = base_port + port_offset;
		int base_port_channel = NI_65XX_PORT_TO_CHAN(port_offset);
		unsigned port_mask, port_data, bits;
		int bitshift = base_port_channel - base_chan;

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

		/* update the outputs */
		if (port_mask) {
			bits = readb(dev->mmio + NI_65XX_IO_DATA_REG(port));
			bits ^= s->io_bits;	/* invert if necessary */
			bits &= ~port_mask;
			bits |= (port_data & port_mask);
			bits ^= s->io_bits;	/* invert back */
			writeb(bits, dev->mmio + NI_65XX_IO_DATA_REG(port));
		}

		/* read back the actual state */
		bits = readb(dev->mmio + NI_65XX_IO_DATA_REG(port));
		bits ^= s->io_bits;	/* invert if necessary */
		if (bitshift > 0)
			bits <<= bitshift;
		else
			bits >>= -bitshift;

		read_bits |= bits;
	}
	data[1] = read_bits;
	return insn->n;
}

static irqreturn_t ni_65xx_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int status;

	status = readb(dev->mmio + NI_65XX_STATUS_REG);
	if ((status & NI_65XX_STATUS_INT) == 0)
		return IRQ_NONE;
	if ((status & NI_65XX_STATUS_EDGE_INT) == 0)
		return IRQ_NONE;

	writeb(NI_65XX_CLR_EDGE_INT | NI_65XX_CLR_OVERFLOW_INT,
	       dev->mmio + NI_65XX_CLR_REG);

	comedi_buf_write_samples(s, &s->state, 1);
	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static int ni_65xx_intr_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_OTHER);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

	/* Step 5: check channel list if it exists */

	return 0;
}

static int ni_65xx_intr_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	writeb(NI_65XX_CLR_EDGE_INT | NI_65XX_CLR_OVERFLOW_INT,
	       dev->mmio + NI_65XX_CLR_REG);
	writeb(NI_65XX_CTRL_FALL_EDGE_ENA | NI_65XX_CTRL_RISE_EDGE_ENA |
	       NI_65XX_CTRL_INT_ENA | NI_65XX_CTRL_EDGE_ENA,
	       dev->mmio + NI_65XX_CTRL_REG);

	return 0;
}

static int ni_65xx_intr_cancel(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	writeb(0x00, dev->mmio + NI_65XX_CTRL_REG);

	return 0;
}

static int ni_65xx_intr_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	data[1] = 0;
	return insn->n;
}

static int ni_65xx_intr_insn_config(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	switch (data[0]) {
	case INSN_CONFIG_CHANGE_NOTIFY:
		/* add instruction to check_insn_config_length() */
		if (insn->n != 3)
			return -EINVAL;

		/* update edge detection for channels 0 to 31 */
		ni_65xx_update_edge_detection(dev, 0, data[1], data[2]);
		/* clear edge detection for channels 32 to 63 */
		ni_65xx_update_edge_detection(dev, 32, 0, 0);
		/* clear edge detection for channels 64 to 95 */
		ni_65xx_update_edge_detection(dev, 64, 0, 0);
		break;
	case INSN_CONFIG_DIGITAL_TRIG:
		/* check trigger number */
		if (data[1] != 0)
			return -EINVAL;
		/* check digital trigger operation */
		switch (data[2]) {
		case COMEDI_DIGITAL_TRIG_DISABLE:
			ni_65xx_disable_edge_detection(dev);
			break;
		case COMEDI_DIGITAL_TRIG_ENABLE_EDGES:
			/*
			 * update edge detection for channels data[3]
			 * to (data[3] + 31)
			 */
			ni_65xx_update_edge_detection(dev, data[3],
						      data[4], data[5]);
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

/* ripped from mite.h and mite_setup2() to avoid mite dependency */
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

	ret = ni_65xx_mite_init(pcidev);
	if (ret)
		return ret;

	dev->mmio = pci_ioremap_bar(pcidev, 1);
	if (!dev->mmio)
		return -ENOMEM;

	writeb(NI_65XX_CLR_EDGE_INT | NI_65XX_CLR_OVERFLOW_INT,
	       dev->mmio + NI_65XX_CLR_REG);
	writeb(0x00, dev->mmio + NI_65XX_CTRL_REG);

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, ni_65xx_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	dev_info(dev->class_dev, "board: %s, ID=0x%02x", dev->board_name,
		 readb(dev->mmio + NI_65XX_ID_REG));

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	if (board->num_di_ports) {
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= NI_65XX_PORT_TO_CHAN(board->num_di_ports);
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= ni_65xx_dio_insn_bits;
		s->insn_config	= ni_65xx_dio_insn_config;

		/* the input ports always start at port 0 */
		s->private = (void *)0;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[1];
	if (board->num_do_ports) {
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= NI_65XX_PORT_TO_CHAN(board->num_do_ports);
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= ni_65xx_dio_insn_bits;

		/* the output ports always start after the input ports */
		s->private = (void *)(unsigned long)board->num_di_ports;

		/*
		 * Use the io_bits to handle the inverted outputs.  Inverted
		 * outputs are only supported if the "legacy_invert_outputs"
		 * module parameter is set to "true".
		 */
		if (ni_65xx_legacy_invert_outputs && board->legacy_invert)
			s->io_bits = 0xff;

		/* reset all output ports to comedi '0' */
		for (i = 0; i < board->num_do_ports; ++i) {
			writeb(s->io_bits,	/* inverted if necessary */
			       dev->mmio +
			       NI_65XX_IO_DATA_REG(board->num_di_ports + i));
		}
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[2];
	if (board->num_dio_ports) {
		s->type		= COMEDI_SUBD_DIO;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
		s->n_chan	= NI_65XX_PORT_TO_CHAN(board->num_dio_ports);
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= ni_65xx_dio_insn_bits;
		s->insn_config	= ni_65xx_dio_insn_config;

		/* the input/output ports always start at port 0 */
		s->private = (void *)0;

		/* configure all ports for input */
		for (i = 0; i < board->num_dio_ports; ++i) {
			writeb(NI_65XX_IO_SEL_INPUT,
			       dev->mmio + NI_65XX_IO_SEL_REG(i));
		}
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 1;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= ni_65xx_intr_insn_bits;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 1;
		s->insn_config	= ni_65xx_intr_insn_config;
		s->do_cmdtest	= ni_65xx_intr_cmdtest;
		s->do_cmd	= ni_65xx_intr_cmd;
		s->cancel	= ni_65xx_intr_cancel;
	}

	ni_65xx_disable_input_filters(dev);
	ni_65xx_disable_edge_detection(dev);

	return 0;
}

static void ni_65xx_detach(struct comedi_device *dev)
{
	if (dev->mmio)
		writeb(0x00, dev->mmio + NI_65XX_CTRL_REG);
	comedi_pci_detach(dev);
}

static struct comedi_driver ni_65xx_driver = {
	.driver_name	= "ni_65xx",
	.module		= THIS_MODULE,
	.auto_attach	= ni_65xx_auto_attach,
	.detach		= ni_65xx_detach,
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
MODULE_DESCRIPTION("Comedi driver for NI PCI-65xx static dio boards");
MODULE_LICENSE("GPL");

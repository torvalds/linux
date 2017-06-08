/*
 * pcmuio.c
 * Comedi driver for Winsystems PC-104 based 48/96-channel DIO boards.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2006 Calin A. Culianu <calin@ajvar.org>
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
 * Driver: pcmuio
 * Description: Winsystems PC-104 based 48/96-channel DIO boards.
 * Devices: [Winsystems] PCM-UIO48A (pcmuio48), PCM-UIO96A (pcmuio96)
 * Author: Calin Culianu <calin@ajvar.org>
 * Updated: Fri, 13 Jan 2006 12:01:01 -0500
 * Status: works
 *
 * A driver for the relatively straightforward-to-program PCM-UIO48A and
 * PCM-UIO96A boards from Winsystems. These boards use either one or two
 * (in the 96-DIO version) WS16C48 ASIC HighDensity I/O Chips (HDIO). This
 * chip is interesting in that each I/O line is individually programmable
 * for INPUT or OUTPUT (thus comedi_dio_config can be done on a per-channel
 * basis). Also, each chip supports edge-triggered interrupts for the first
 * 24 I/O lines. Of course, since the 96-channel version of the board has
 * two ASICs, it can detect polarity changes on up to 48 I/O lines. Since
 * this is essentially an (non-PnP) ISA board, I/O Address and IRQ selection
 * are done through jumpers on the board. You need to pass that information
 * to this driver as the first and second comedi_config option, respectively.
 * Note that the 48-channel version uses 16 bytes of IO memory and the 96-
 * channel version uses 32-bytes (in case you are worried about conflicts).
 * The 48-channel board is split into two 24-channel comedi subdevices. The
 * 96-channel board is split into 4 24-channel DIO subdevices.
 *
 * Note that IRQ support has been added, but it is untested.
 *
 * To use edge-detection IRQ support, pass the IRQs of both ASICS (for the
 * 96 channel version) or just 1 ASIC (for 48-channel version). Then, use
 * comedi_commands with TRIG_NOW. Your callback will be called each time an
 * edge is triggered, and the data values will be two sample_t's, which
 * should be concatenated to form one 32-bit unsigned int.  This value is
 * the mask of channels that had edges detected from your channel list. Note
 * that the bits positions in the mask correspond to positions in your
 * chanlist when you specified the command and *not* channel id's!
 *
 * To set the polarity of the edge-detection interrupts pass a nonzero value
 * for either CR_RANGE or CR_AREF for edge-up polarity, or a zero value for
 * both CR_RANGE and CR_AREF if you want edge-down polarity.
 *
 * In the 48-channel version:
 *
 * On subdev 0, the first 24 channels channels are edge-detect channels.
 *
 * In the 96-channel board you have the following channels that can do edge
 * detection:
 *
 * subdev 0, channels 0-24  (first 24 channels of 1st ASIC)
 * subdev 2, channels 0-24  (first 24 channels of 2nd ASIC)
 *
 * Configuration Options:
 *  [0] - I/O port base address
 *  [1] - IRQ (for first ASIC, or first 24 channels)
 *  [2] - IRQ (for second ASIC, pcmuio96 only - IRQ for chans 48-72
 *             can be the same as first irq!)
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

/*
 * Register I/O map
 *
 * Offset    Page 0       Page 1       Page 2       Page 3
 * ------  -----------  -----------  -----------  -----------
 *  0x00   Port 0 I/O   Port 0 I/O   Port 0 I/O   Port 0 I/O
 *  0x01   Port 1 I/O   Port 1 I/O   Port 1 I/O   Port 1 I/O
 *  0x02   Port 2 I/O   Port 2 I/O   Port 2 I/O   Port 2 I/O
 *  0x03   Port 3 I/O   Port 3 I/O   Port 3 I/O   Port 3 I/O
 *  0x04   Port 4 I/O   Port 4 I/O   Port 4 I/O   Port 4 I/O
 *  0x05   Port 5 I/O   Port 5 I/O   Port 5 I/O   Port 5 I/O
 *  0x06   INT_PENDING  INT_PENDING  INT_PENDING  INT_PENDING
 *  0x07    Page/Lock    Page/Lock    Page/Lock    Page/Lock
 *  0x08       N/A         POL_0       ENAB_0       INT_ID0
 *  0x09       N/A         POL_1       ENAB_1       INT_ID1
 *  0x0a       N/A         POL_2       ENAB_2       INT_ID2
 */
#define PCMUIO_PORT_REG(x)		(0x00 + (x))
#define PCMUIO_INT_PENDING_REG		0x06
#define PCMUIO_PAGE_LOCK_REG		0x07
#define PCMUIO_LOCK_PORT(x)		((1 << (x)) & 0x3f)
#define PCMUIO_PAGE(x)			(((x) & 0x3) << 6)
#define PCMUIO_PAGE_MASK		PCMUIO_PAGE(3)
#define PCMUIO_PAGE_POL			1
#define PCMUIO_PAGE_ENAB		2
#define PCMUIO_PAGE_INT_ID		3
#define PCMUIO_PAGE_REG(x)		(0x08 + (x))

#define PCMUIO_ASIC_IOSIZE		0x10
#define PCMUIO_MAX_ASICS		2

struct pcmuio_board {
	const char *name;
	const int num_asics;
};

static const struct pcmuio_board pcmuio_boards[] = {
	{
		.name		= "pcmuio48",
		.num_asics	= 1,
	}, {
		.name		= "pcmuio96",
		.num_asics	= 2,
	},
};

struct pcmuio_asic {
	spinlock_t pagelock;	/* protects the page registers */
	spinlock_t spinlock;	/* protects member variables */
	unsigned int enabled_mask;
	unsigned int active:1;
};

struct pcmuio_private {
	struct pcmuio_asic asics[PCMUIO_MAX_ASICS];
	unsigned int irq2;
};

static inline unsigned long pcmuio_asic_iobase(struct comedi_device *dev,
					       int asic)
{
	return dev->iobase + (asic * PCMUIO_ASIC_IOSIZE);
}

static inline int pcmuio_subdevice_to_asic(struct comedi_subdevice *s)
{
	/*
	 * subdevice 0 and 1 are handled by the first asic
	 * subdevice 2 and 3 are handled by the second asic
	 */
	return s->index / 2;
}

static inline int pcmuio_subdevice_to_port(struct comedi_subdevice *s)
{
	/*
	 * subdevice 0 and 2 use port registers 0-2
	 * subdevice 1 and 3 use port registers 3-5
	 */
	return (s->index % 2) ? 3 : 0;
}

static void pcmuio_write(struct comedi_device *dev, unsigned int val,
			 int asic, int page, int port)
{
	struct pcmuio_private *devpriv = dev->private;
	struct pcmuio_asic *chip = &devpriv->asics[asic];
	unsigned long iobase = pcmuio_asic_iobase(dev, asic);
	unsigned long flags;

	spin_lock_irqsave(&chip->pagelock, flags);
	if (page == 0) {
		/* Port registers are valid for any page */
		outb(val & 0xff, iobase + PCMUIO_PORT_REG(port + 0));
		outb((val >> 8) & 0xff, iobase + PCMUIO_PORT_REG(port + 1));
		outb((val >> 16) & 0xff, iobase + PCMUIO_PORT_REG(port + 2));
	} else {
		outb(PCMUIO_PAGE(page), iobase + PCMUIO_PAGE_LOCK_REG);
		outb(val & 0xff, iobase + PCMUIO_PAGE_REG(0));
		outb((val >> 8) & 0xff, iobase + PCMUIO_PAGE_REG(1));
		outb((val >> 16) & 0xff, iobase + PCMUIO_PAGE_REG(2));
	}
	spin_unlock_irqrestore(&chip->pagelock, flags);
}

static unsigned int pcmuio_read(struct comedi_device *dev,
				int asic, int page, int port)
{
	struct pcmuio_private *devpriv = dev->private;
	struct pcmuio_asic *chip = &devpriv->asics[asic];
	unsigned long iobase = pcmuio_asic_iobase(dev, asic);
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&chip->pagelock, flags);
	if (page == 0) {
		/* Port registers are valid for any page */
		val = inb(iobase + PCMUIO_PORT_REG(port + 0));
		val |= (inb(iobase + PCMUIO_PORT_REG(port + 1)) << 8);
		val |= (inb(iobase + PCMUIO_PORT_REG(port + 2)) << 16);
	} else {
		outb(PCMUIO_PAGE(page), iobase + PCMUIO_PAGE_LOCK_REG);
		val = inb(iobase + PCMUIO_PAGE_REG(0));
		val |= (inb(iobase + PCMUIO_PAGE_REG(1)) << 8);
		val |= (inb(iobase + PCMUIO_PAGE_REG(2)) << 16);
	}
	spin_unlock_irqrestore(&chip->pagelock, flags);

	return val;
}

/*
 * Each channel can be individually programmed for input or output.
 * Writing a '0' to a channel causes the corresponding output pin
 * to go to a high-z state (pulled high by an external 10K resistor).
 * This allows it to be used as an input. When used in the input mode,
 * a read reflects the inverted state of the I/O pin, such that a
 * high on the pin will read as a '0' in the register. Writing a '1'
 * to a bit position causes the pin to sink current (up to 12mA),
 * effectively pulling it low.
 */
static int pcmuio_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	int asic = pcmuio_subdevice_to_asic(s);
	int port = pcmuio_subdevice_to_port(s);
	unsigned int chanmask = (1 << s->n_chan) - 1;
	unsigned int mask;
	unsigned int val;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		/*
		 * Outputs are inverted, invert the state and
		 * update the channels.
		 *
		 * The s->io_bits mask makes sure the input channels
		 * are '0' so that the outputs pins stay in a high
		 * z-state.
		 */
		val = ~s->state & chanmask;
		val &= s->io_bits;
		pcmuio_write(dev, val, asic, 0, port);
	}

	/* get inverted state of the channels from the port */
	val = pcmuio_read(dev, asic, 0, port);

	/* return the true state of the channels */
	data[1] = ~val & chanmask;

	return insn->n;
}

static int pcmuio_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	int asic = pcmuio_subdevice_to_asic(s);
	int port = pcmuio_subdevice_to_port(s);
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	if (data[0] == INSN_CONFIG_DIO_INPUT)
		pcmuio_write(dev, s->io_bits, asic, 0, port);

	return insn->n;
}

static void pcmuio_reset(struct comedi_device *dev)
{
	const struct pcmuio_board *board = dev->board_ptr;
	int asic;

	for (asic = 0; asic < board->num_asics; ++asic) {
		/* first, clear all the DIO port bits */
		pcmuio_write(dev, 0, asic, 0, 0);
		pcmuio_write(dev, 0, asic, 0, 3);

		/* Next, clear all the paged registers for each page */
		pcmuio_write(dev, 0, asic, PCMUIO_PAGE_POL, 0);
		pcmuio_write(dev, 0, asic, PCMUIO_PAGE_ENAB, 0);
		pcmuio_write(dev, 0, asic, PCMUIO_PAGE_INT_ID, 0);
	}
}

/* chip->spinlock is already locked */
static void pcmuio_stop_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pcmuio_private *devpriv = dev->private;
	int asic = pcmuio_subdevice_to_asic(s);
	struct pcmuio_asic *chip = &devpriv->asics[asic];

	chip->enabled_mask = 0;
	chip->active = 0;
	s->async->inttrig = NULL;

	/* disable all intrs for this subdev.. */
	pcmuio_write(dev, 0, asic, PCMUIO_PAGE_ENAB, 0);
}

static void pcmuio_handle_intr_subdev(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      unsigned int triggered)
{
	struct pcmuio_private *devpriv = dev->private;
	int asic = pcmuio_subdevice_to_asic(s);
	struct pcmuio_asic *chip = &devpriv->asics[asic];
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int val = 0;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&chip->spinlock, flags);

	if (!chip->active)
		goto done;

	if (!(triggered & chip->enabled_mask))
		goto done;

	for (i = 0; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);

		if (triggered & (1 << chan))
			val |= (1 << i);
	}

	comedi_buf_write_samples(s, &val, 1);

	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg)
		s->async->events |= COMEDI_CB_EOA;

done:
	spin_unlock_irqrestore(&chip->spinlock, flags);

	comedi_handle_events(dev, s);
}

static int pcmuio_handle_asic_interrupt(struct comedi_device *dev, int asic)
{
	/* there are could be two asics so we can't use dev->read_subdev */
	struct comedi_subdevice *s = &dev->subdevices[asic * 2];
	unsigned long iobase = pcmuio_asic_iobase(dev, asic);
	unsigned int val;

	/* are there any interrupts pending */
	val = inb(iobase + PCMUIO_INT_PENDING_REG) & 0x07;
	if (!val)
		return 0;

	/* get, and clear, the pending interrupts */
	val = pcmuio_read(dev, asic, PCMUIO_PAGE_INT_ID, 0);
	pcmuio_write(dev, 0, asic, PCMUIO_PAGE_INT_ID, 0);

	/* handle the pending interrupts */
	pcmuio_handle_intr_subdev(dev, s, val);

	return 1;
}

static irqreturn_t pcmuio_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcmuio_private *devpriv = dev->private;
	int handled = 0;

	if (irq == dev->irq)
		handled += pcmuio_handle_asic_interrupt(dev, 0);
	if (irq == devpriv->irq2)
		handled += pcmuio_handle_asic_interrupt(dev, 1);

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

/* chip->spinlock is already locked */
static void pcmuio_start_intr(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct pcmuio_private *devpriv = dev->private;
	int asic = pcmuio_subdevice_to_asic(s);
	struct pcmuio_asic *chip = &devpriv->asics[asic];
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int bits = 0;
	unsigned int pol_bits = 0;
	int i;

	chip->enabled_mask = 0;
	chip->active = 1;
	if (cmd->chanlist) {
		for (i = 0; i < cmd->chanlist_len; i++) {
			unsigned int chanspec = cmd->chanlist[i];
			unsigned int chan = CR_CHAN(chanspec);
			unsigned int range = CR_RANGE(chanspec);
			unsigned int aref = CR_AREF(chanspec);

			bits |= (1 << chan);
			pol_bits |= ((aref || range) ? 1 : 0) << chan;
		}
	}
	bits &= ((1 << s->n_chan) - 1);
	chip->enabled_mask = bits;

	/* set pol and enab intrs for this subdev.. */
	pcmuio_write(dev, pol_bits, asic, PCMUIO_PAGE_POL, 0);
	pcmuio_write(dev, bits, asic, PCMUIO_PAGE_ENAB, 0);
}

static int pcmuio_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcmuio_private *devpriv = dev->private;
	int asic = pcmuio_subdevice_to_asic(s);
	struct pcmuio_asic *chip = &devpriv->asics[asic];
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	if (chip->active)
		pcmuio_stop_intr(dev, s);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int pcmuio_inttrig_start_intr(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     unsigned int trig_num)
{
	struct pcmuio_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int asic = pcmuio_subdevice_to_asic(s);
	struct pcmuio_asic *chip = &devpriv->asics[asic];
	unsigned long flags;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	spin_lock_irqsave(&chip->spinlock, flags);
	s->async->inttrig = NULL;
	if (chip->active)
		pcmuio_start_intr(dev, s);

	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 1;
}

/*
 * 'do_cmd' function for an 'INTERRUPT' subdevice.
 */
static int pcmuio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcmuio_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int asic = pcmuio_subdevice_to_asic(s);
	struct pcmuio_asic *chip = &devpriv->asics[asic];
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	chip->active = 1;

	/* Set up start of acquisition. */
	if (cmd->start_src == TRIG_INT)
		s->async->inttrig = pcmuio_inttrig_start_intr;
	else	/* TRIG_NOW */
		pcmuio_start_intr(dev, s);

	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int pcmuio_cmdtest(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	/* if (err) return 4; */

	return 0;
}

static int pcmuio_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcmuio_board *board = dev->board_ptr;
	struct comedi_subdevice *s;
	struct pcmuio_private *devpriv;
	int ret;
	int i;

	ret = comedi_request_region(dev, it->options[0],
				    board->num_asics * PCMUIO_ASIC_IOSIZE);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	for (i = 0; i < PCMUIO_MAX_ASICS; ++i) {
		struct pcmuio_asic *chip = &devpriv->asics[i];

		spin_lock_init(&chip->pagelock);
		spin_lock_init(&chip->spinlock);
	}

	pcmuio_reset(dev);

	if (it->options[1]) {
		/* request the irq for the 1st asic */
		ret = request_irq(it->options[1], pcmuio_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	if (board->num_asics == 2) {
		if (it->options[2] == dev->irq) {
			/* the same irq (or none) is used by both asics */
			devpriv->irq2 = it->options[2];
		} else if (it->options[2]) {
			/* request the irq for the 2nd asic */
			ret = request_irq(it->options[2], pcmuio_interrupt, 0,
					  dev->board_name, dev);
			if (ret == 0)
				devpriv->irq2 = it->options[2];
		}
	}

	ret = comedi_alloc_subdevices(dev, board->num_asics * 2);
	if (ret)
		return ret;

	for (i = 0; i < dev->n_subdevices; ++i) {
		s = &dev->subdevices[i];
		s->type		= COMEDI_SUBD_DIO;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
		s->n_chan	= 24;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcmuio_dio_insn_bits;
		s->insn_config	= pcmuio_dio_insn_config;

		/* subdevices 0 and 2 can support interrupts */
		if ((i == 0 && dev->irq) || (i == 2 && devpriv->irq2)) {
			/* setup the interrupt subdevice */
			dev->read_subdev = s;
			s->subdev_flags	|= SDF_CMD_READ | SDF_LSAMPL |
					   SDF_PACKED;
			s->len_chanlist	= s->n_chan;
			s->cancel	= pcmuio_cancel;
			s->do_cmd	= pcmuio_cmd;
			s->do_cmdtest	= pcmuio_cmdtest;
		}
	}

	return 0;
}

static void pcmuio_detach(struct comedi_device *dev)
{
	struct pcmuio_private *devpriv = dev->private;

	if (devpriv) {
		pcmuio_reset(dev);

		/* free the 2nd irq if used, the core will free the 1st one */
		if (devpriv->irq2 && devpriv->irq2 != dev->irq)
			free_irq(devpriv->irq2, dev);
	}
	comedi_legacy_detach(dev);
}

static struct comedi_driver pcmuio_driver = {
	.driver_name	= "pcmuio",
	.module		= THIS_MODULE,
	.attach		= pcmuio_attach,
	.detach		= pcmuio_detach,
	.board_name	= &pcmuio_boards[0].name,
	.offset		= sizeof(struct pcmuio_board),
	.num_names	= ARRAY_SIZE(pcmuio_boards),
};
module_comedi_driver(pcmuio_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

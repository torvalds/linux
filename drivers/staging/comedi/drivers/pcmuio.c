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
 * Devices: (Winsystems) PCM-UIO48A [pcmuio48]
 *	    (Winsystems) PCM-UIO96A [pcmuio96]
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

#include <linux/interrupt.h>
#include <linux/slab.h>

#include "../comedidev.h"

#include "comedi_fc.h"

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

struct pcmuio_subdev_private {
	/* The below is only used for intr subdevices */
	struct {
		/* if non-negative, this subdev has an interrupt asic */
		int asic;
		/*
		 * subdev-relative channel mask for channels
		 * we are interested in
		 */
		int enabled_mask;
		int active;
		int stop_count;
		int continuous;
		spinlock_t spinlock;
	} intr;
};

struct pcmuio_private {
	struct {
		unsigned int irq;
		spinlock_t spinlock;
	} asics[PCMUIO_MAX_ASICS];
	struct pcmuio_subdev_private *sprivs;
};

static void pcmuio_write(struct comedi_device *dev, unsigned int val,
			 int asic, int page, int port)
{
	unsigned long iobase = dev->iobase + (asic * PCMUIO_ASIC_IOSIZE);

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
}

static unsigned int pcmuio_read(struct comedi_device *dev,
				int asic, int page, int port)
{
	unsigned long iobase = dev->iobase + (asic * PCMUIO_ASIC_IOSIZE);
	unsigned int val;

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
				struct comedi_insn *insn, unsigned int *data)
{
	unsigned int mask = data[0] & s->io_bits;	/* outputs only */
	unsigned int bits = data[1];
	int asic = s->index / 2;
	int port = (s->index % 2) ? 3 : 0;
	unsigned int val;

	/* get inverted state of the channels from the port */
	val = pcmuio_read(dev, asic, 0, port);

	/* get the true state of the channels */
	s->state = val ^ ((0x1 << s->n_chan) - 1);

	if (mask) {
		s->state &= ~mask;
		s->state |= (mask & bits);

		/* invert the state and update the channels */
		val = s->state ^ ((0x1 << s->n_chan) - 1);
		pcmuio_write(dev, val, asic, 0, port);
	}

	data[1] = s->state;

	return insn->n;
}

static int pcmuio_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan_mask = 1 << CR_CHAN(insn->chanspec);
	int asic = s->index / 2;
	int port = (s->index % 2) ? 3 : 0;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= chan_mask;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~chan_mask;
		pcmuio_write(dev, s->io_bits, asic, 0, port);
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & chan_mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		break;
	default:
		return -EINVAL;
		break;
	}

	return insn->n;
}

static void init_asics(struct comedi_device *dev)
{
	const struct pcmuio_board *board = comedi_board(dev);
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

static void pcmuio_stop_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pcmuio_subdev_private *subpriv = s->private;
	int asic;

	asic = subpriv->intr.asic;
	if (asic < 0)
		return;		/* not an interrupt subdev */

	subpriv->intr.enabled_mask = 0;
	subpriv->intr.active = 0;
	s->async->inttrig = NULL;

	/* disable all intrs for this subdev.. */
	pcmuio_write(dev, 0, asic, PCMUIO_PAGE_ENAB, 0);
}

static void pcmuio_handle_intr_subdev(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      unsigned triggered)
{
	struct pcmuio_subdev_private *subpriv = s->private;
	unsigned int len = s->async->cmd.chanlist_len;
	unsigned oldevents = s->async->events;
	unsigned int val = 0;
	unsigned long flags;
	unsigned mytrig;
	unsigned int i;

	spin_lock_irqsave(&subpriv->intr.spinlock, flags);

	if (!subpriv->intr.active)
		goto done;

	mytrig = triggered;
	mytrig &= ((0x1 << s->n_chan) - 1);

	if (!(mytrig & subpriv->intr.enabled_mask))
		goto done;

	for (i = 0; i < len; i++) {
		unsigned int chan = CR_CHAN(s->async->cmd.chanlist[i]);
		if (mytrig & (1U << chan))
			val |= (1U << i);
	}

	/* Write the scan to the buffer. */
	if (comedi_buf_put(s->async, ((short *)&val)[0]) &&
	    comedi_buf_put(s->async, ((short *)&val)[1])) {
		s->async->events |= (COMEDI_CB_BLOCK | COMEDI_CB_EOS);
	} else {
		/* Overflow! Stop acquisition!! */
		/* TODO: STOP_ACQUISITION_CALL_HERE!! */
		pcmuio_stop_intr(dev, s);
	}

	/* Check for end of acquisition. */
	if (!subpriv->intr.continuous) {
		/* stop_src == TRIG_COUNT */
		if (subpriv->intr.stop_count > 0) {
			subpriv->intr.stop_count--;
			if (subpriv->intr.stop_count == 0) {
				s->async->events |= COMEDI_CB_EOA;
				/* TODO: STOP_ACQUISITION_CALL_HERE!! */
				pcmuio_stop_intr(dev, s);
			}
		}
	}

done:
	spin_unlock_irqrestore(&subpriv->intr.spinlock, flags);

	if (oldevents != s->async->events)
		comedi_event(dev, s);
}

static int pcmuio_handle_asic_interrupt(struct comedi_device *dev, int asic)
{
	struct pcmuio_private *devpriv = dev->private;
	struct pcmuio_subdev_private *subpriv;
	unsigned long iobase = dev->iobase + (asic * PCMUIO_ASIC_IOSIZE);
	unsigned int triggered = 0;
	int got1 = 0;
	unsigned long flags;
	unsigned char int_pend;
	int i;

	spin_lock_irqsave(&devpriv->asics[asic].spinlock, flags);

	int_pend = inb(iobase + PCMUIO_INT_PENDING_REG) & 0x07;
	if (int_pend) {
		triggered = pcmuio_read(dev, asic, PCMUIO_PAGE_INT_ID, 0);
		pcmuio_write(dev, 0, asic, PCMUIO_PAGE_INT_ID, 0);

		++got1;
	}

	spin_unlock_irqrestore(&devpriv->asics[asic].spinlock, flags);

	if (triggered) {
		struct comedi_subdevice *s;
		/* TODO here: dispatch io lines to subdevs with commands.. */
		for (i = 0; i < dev->n_subdevices; i++) {
			s = &dev->subdevices[i];
			subpriv = s->private;
			if (subpriv->intr.asic == asic) {
				/*
				 * This is an interrupt subdev, and it
				 * matches this asic!
				 */
				pcmuio_handle_intr_subdev(dev, s,
							  triggered);
			}
		}
	}
	return got1;
}

static irqreturn_t interrupt_pcmuio(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcmuio_private *devpriv = dev->private;
	int got1 = 0;
	int asic;

	for (asic = 0; asic < PCMUIO_MAX_ASICS; ++asic) {
		if (irq == devpriv->asics[asic].irq) {
			/* it is an interrupt for ASIC #asic */
			if (pcmuio_handle_asic_interrupt(dev, asic))
				got1++;
		}
	}
	if (!got1)
		return IRQ_NONE;	/* interrupt from other source */
	return IRQ_HANDLED;
}

static int pcmuio_start_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pcmuio_subdev_private *subpriv = s->private;

	if (!subpriv->intr.continuous && subpriv->intr.stop_count == 0) {
		/* An empty acquisition! */
		s->async->events |= COMEDI_CB_EOA;
		subpriv->intr.active = 0;
		return 1;
	} else {
		unsigned bits = 0, pol_bits = 0, n;
		int asic;
		struct comedi_cmd *cmd = &s->async->cmd;

		asic = subpriv->intr.asic;
		if (asic < 0)
			return 1;	/* not an interrupt
					   subdev */
		subpriv->intr.enabled_mask = 0;
		subpriv->intr.active = 1;
		if (cmd->chanlist) {
			for (n = 0; n < cmd->chanlist_len; n++) {
				bits |= (1U << CR_CHAN(cmd->chanlist[n]));
				pol_bits |= (CR_AREF(cmd->chanlist[n])
					     || CR_RANGE(cmd->
							 chanlist[n]) ? 1U : 0U)
				    << CR_CHAN(cmd->chanlist[n]);
			}
		}
		bits &= ((0x1 << s->n_chan) - 1);
		subpriv->intr.enabled_mask = bits;

		/* set pol and enab intrs for this subdev.. */
		pcmuio_write(dev, pol_bits, asic, PCMUIO_PAGE_POL, 0);
		pcmuio_write(dev, bits, asic, PCMUIO_PAGE_ENAB, 0);
	}
	return 0;
}

static int pcmuio_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcmuio_subdev_private *subpriv = s->private;
	unsigned long flags;

	spin_lock_irqsave(&subpriv->intr.spinlock, flags);
	if (subpriv->intr.active)
		pcmuio_stop_intr(dev, s);
	spin_unlock_irqrestore(&subpriv->intr.spinlock, flags);

	return 0;
}

/*
 * Internal trigger function to start acquisition for an 'INTERRUPT' subdevice.
 */
static int
pcmuio_inttrig_start_intr(struct comedi_device *dev, struct comedi_subdevice *s,
			  unsigned int trignum)
{
	struct pcmuio_subdev_private *subpriv = s->private;
	unsigned long flags;
	int event = 0;

	if (trignum != 0)
		return -EINVAL;

	spin_lock_irqsave(&subpriv->intr.spinlock, flags);
	s->async->inttrig = NULL;
	if (subpriv->intr.active)
		event = pcmuio_start_intr(dev, s);

	spin_unlock_irqrestore(&subpriv->intr.spinlock, flags);

	if (event)
		comedi_event(dev, s);

	return 1;
}

/*
 * 'do_cmd' function for an 'INTERRUPT' subdevice.
 */
static int pcmuio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcmuio_subdev_private *subpriv = s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long flags;
	int event = 0;

	spin_lock_irqsave(&subpriv->intr.spinlock, flags);
	subpriv->intr.active = 1;

	/* Set up end of acquisition. */
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		subpriv->intr.continuous = 0;
		subpriv->intr.stop_count = cmd->stop_arg;
		break;
	default:
		/* TRIG_NONE */
		subpriv->intr.continuous = 1;
		subpriv->intr.stop_count = 0;
		break;
	}

	/* Set up start of acquisition. */
	switch (cmd->start_src) {
	case TRIG_INT:
		s->async->inttrig = pcmuio_inttrig_start_intr;
		break;
	default:
		/* TRIG_NOW */
		event = pcmuio_start_intr(dev, s);
		break;
	}
	spin_unlock_irqrestore(&subpriv->intr.spinlock, flags);

	if (event)
		comedi_event(dev, s);

	return 0;
}

static int pcmuio_cmdtest(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_INT);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->start_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		/* any count allowed */
		break;
	case TRIG_NONE:
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	/* if (err) return 4; */

	return 0;
}

static int pcmuio_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcmuio_board *board = comedi_board(dev);
	struct comedi_subdevice *s;
	struct pcmuio_private *devpriv;
	struct pcmuio_subdev_private *subpriv;
	int sdev_no, n_subdevs, asic;
	unsigned int irq[PCMUIO_MAX_ASICS];
	int ret;

	irq[0] = it->options[1];
	irq[1] = it->options[2];

	ret = comedi_request_region(dev, it->options[0],
				    board->num_asics * PCMUIO_ASIC_IOSIZE);
	if (ret)
		return ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	for (asic = 0; asic < PCMUIO_MAX_ASICS; ++asic)
		spin_lock_init(&devpriv->asics[asic].spinlock);

	n_subdevs = board->num_asics * 2;
	devpriv->sprivs = kcalloc(n_subdevs, sizeof(*subpriv), GFP_KERNEL);
	if (!devpriv->sprivs)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, n_subdevs);
	if (ret)
		return ret;

	for (sdev_no = 0; sdev_no < (int)dev->n_subdevices; ++sdev_no) {
		s = &dev->subdevices[sdev_no];
		subpriv = &devpriv->sprivs[sdev_no];
		s->private = subpriv;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->type = COMEDI_SUBD_DIO;
		s->insn_bits = pcmuio_dio_insn_bits;
		s->insn_config = pcmuio_dio_insn_config;
		s->n_chan = 24;

		/* subdevices 0 and 2 suppport interrupts */
		if ((sdev_no % 2) == 0) {
			/* setup the interrupt subdevice */
			subpriv->intr.asic = sdev_no / 2;
			dev->read_subdev = s;
			s->subdev_flags |= SDF_CMD_READ;
			s->cancel = pcmuio_cancel;
			s->do_cmd = pcmuio_cmd;
			s->do_cmdtest = pcmuio_cmdtest;
			s->len_chanlist = s->n_chan;
		} else {
			subpriv->intr.asic = -1;
			s->len_chanlist = 1;
		}
		spin_lock_init(&subpriv->intr.spinlock);
	}

	init_asics(dev);	/* clear out all the registers, basically */

	for (asic = 0; irq[0] && asic < PCMUIO_MAX_ASICS; ++asic) {
		if (irq[asic]
		    && request_irq(irq[asic], interrupt_pcmuio,
				   IRQF_SHARED, board->name, dev)) {
			int i;
			/* unroll the allocated irqs.. */
			for (i = asic - 1; i >= 0; --i) {
				free_irq(irq[i], dev);
				devpriv->asics[i].irq = irq[i] = 0;
			}
			irq[asic] = 0;
		}
		devpriv->asics[asic].irq = irq[asic];
	}

	return 0;
}

static void pcmuio_detach(struct comedi_device *dev)
{
	struct pcmuio_private *devpriv = dev->private;
	int i;

	for (i = 0; i < PCMUIO_MAX_ASICS; ++i) {
		if (devpriv->asics[i].irq)
			free_irq(devpriv->asics[i].irq, dev);
	}
	if (devpriv && devpriv->sprivs)
		kfree(devpriv->sprivs);
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

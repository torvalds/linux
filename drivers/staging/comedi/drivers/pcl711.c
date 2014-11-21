/*
 * pcl711.c
 * Comedi driver for PC-LabCard PCL-711 and AdSys ACL-8112 and compatibles
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 *		      Janne Jalkanen <jalkanen@cs.hut.fi>
 *		      Eric Bunn <ebu@cs.hut.fi>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
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
 * Driver: pcl711
 * Description: Advantech PCL-711 and 711b, ADLink ACL-8112
 * Devices: (Advantech) PCL-711 [pcl711]
 *	    (Advantech) PCL-711B [pcl711b]
 *	    (AdLink) ACL-8112HG [acl8112hg]
 *	    (AdLink) ACL-8112DG [acl8112dg]
 * Author: David A. Schleef <ds@schleef.org>
 *	   Janne Jalkanen <jalkanen@cs.hut.fi>
 *	   Eric Bunn <ebu@cs.hut.fi>
 * Updated:
 * Status: mostly complete
 *
 * Configuration Options:
 *   [0] - I/O port base
 *   [1] - IRQ, optional
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_fc.h"
#include "8253.h"

/*
 * I/O port register map
 */
#define PCL711_TIMER_BASE	0x00
#define PCL711_AI_LSB_REG	0x04
#define PCL711_AI_MSB_REG	0x05
#define PCL711_AI_MSB_DRDY	(1 << 4)
#define PCL711_AO_LSB_REG(x)	(0x04 + ((x) * 2))
#define PCL711_AO_MSB_REG(x)	(0x05 + ((x) * 2))
#define PCL711_DI_LSB_REG	0x06
#define PCL711_DI_MSB_REG	0x07
#define PCL711_INT_STAT_REG	0x08
#define PCL711_INT_STAT_CLR	(0 << 0)  /* any value will work */
#define PCL711_AI_GAIN_REG	0x09
#define PCL711_AI_GAIN(x)	(((x) & 0xf) << 0)
#define PCL711_MUX_REG		0x0a
#define PCL711_MUX_CHAN(x)	(((x) & 0xf) << 0)
#define PCL711_MUX_CS0		(1 << 4)
#define PCL711_MUX_CS1		(1 << 5)
#define PCL711_MUX_DIFF		(PCL711_MUX_CS0 | PCL711_MUX_CS1)
#define PCL711_MODE_REG		0x0b
#define PCL711_MODE_DEFAULT	(0 << 0)
#define PCL711_MODE_SOFTTRIG	(1 << 0)
#define PCL711_MODE_EXT		(2 << 0)
#define PCL711_MODE_EXT_IRQ	(3 << 0)
#define PCL711_MODE_PACER	(4 << 0)
#define PCL711_MODE_PACER_IRQ	(6 << 0)
#define PCL711_MODE_IRQ(x)	(((x) & 0x7) << 4)
#define PCL711_SOFTTRIG_REG	0x0c
#define PCL711_SOFTTRIG		(0 << 0)  /* any value will work */
#define PCL711_DO_LSB_REG	0x0d
#define PCL711_DO_MSB_REG	0x0e

static const struct comedi_lrange range_pcl711b_ai = {
	5, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		BIP_RANGE(0.3125)
	}
};

static const struct comedi_lrange range_acl8112hg_ai = {
	12, {
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.005),
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.01),
		BIP_RANGE(10),
		BIP_RANGE(1),
		BIP_RANGE(0.1),
		BIP_RANGE(0.01)
	}
};

static const struct comedi_lrange range_acl8112dg_ai = {
	9, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		BIP_RANGE(10)
	}
};

struct pcl711_board {
	const char *name;
	int n_aichan;
	int n_aochan;
	int maxirq;
	const struct comedi_lrange *ai_range_type;
};

static const struct pcl711_board boardtypes[] = {
	{
		.name		= "pcl711",
		.n_aichan	= 8,
		.n_aochan	= 1,
		.ai_range_type	= &range_bipolar5,
	}, {
		.name		= "pcl711b",
		.n_aichan	= 8,
		.n_aochan	= 1,
		.maxirq		= 7,
		.ai_range_type	= &range_pcl711b_ai,
	}, {
		.name		= "acl8112hg",
		.n_aichan	= 16,
		.n_aochan	= 2,
		.maxirq		= 15,
		.ai_range_type	= &range_acl8112hg_ai,
	}, {
		.name		= "acl8112dg",
		.n_aichan	= 16,
		.n_aochan	= 2,
		.maxirq		= 15,
		.ai_range_type	= &range_acl8112dg_ai,
	},
};

struct pcl711_private {
	unsigned int divisor1;
	unsigned int divisor2;
};

static void pcl711_ai_set_mode(struct comedi_device *dev, unsigned int mode)
{
	/*
	 * The pcl711b board uses bits in the mode register to select the
	 * interrupt. The other boards supported by this driver all use
	 * jumpers on the board.
	 *
	 * Enables the interrupt when needed on the pcl711b board. These
	 * bits do nothing on the other boards.
	 */
	if (mode == PCL711_MODE_EXT_IRQ || mode == PCL711_MODE_PACER_IRQ)
		mode |= PCL711_MODE_IRQ(dev->irq);

	outb(mode, dev->iobase + PCL711_MODE_REG);
}

static unsigned int pcl711_ai_get_sample(struct comedi_device *dev,
					 struct comedi_subdevice *s)
{
	unsigned int val;

	val = inb(dev->iobase + PCL711_AI_MSB_REG) << 8;
	val |= inb(dev->iobase + PCL711_AI_LSB_REG);

	return val & s->maxdata;
}

static int pcl711_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	outb(PCL711_INT_STAT_CLR, dev->iobase + PCL711_INT_STAT_REG);
	pcl711_ai_set_mode(dev, PCL711_MODE_SOFTTRIG);
	return 0;
}

static irqreturn_t pcl711_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int data;

	if (!dev->attached) {
		dev_err(dev->class_dev, "spurious interrupt\n");
		return IRQ_HANDLED;
	}

	data = pcl711_ai_get_sample(dev, s);

	outb(PCL711_INT_STAT_CLR, dev->iobase + PCL711_INT_STAT_REG);

	comedi_buf_write_samples(s, &data, 1);

	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg)
		s->async->events |= COMEDI_CB_EOA;

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static void pcl711_set_changain(struct comedi_device *dev,
				struct comedi_subdevice *s,
				unsigned int chanspec)
{
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);
	unsigned int mux = 0;

	outb(PCL711_AI_GAIN(range), dev->iobase + PCL711_AI_GAIN_REG);

	if (s->n_chan > 8) {
		/* Select the correct MPC508A chip */
		if (aref == AREF_DIFF) {
			chan &= 0x7;
			mux |= PCL711_MUX_DIFF;
		} else {
			if (chan < 8)
				mux |= PCL711_MUX_CS0;
			else
				mux |= PCL711_MUX_CS1;
		}
	}
	outb(mux | PCL711_MUX_CHAN(chan), dev->iobase + PCL711_MUX_REG);
}

static int pcl711_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + PCL711_AI_MSB_REG);
	if ((status & PCL711_AI_MSB_DRDY) == 0)
		return 0;
	return -EBUSY;
}

static int pcl711_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	int ret;
	int i;

	pcl711_set_changain(dev, s, insn->chanspec);

	pcl711_ai_set_mode(dev, PCL711_MODE_SOFTTRIG);

	for (i = 0; i < insn->n; i++) {
		outb(PCL711_SOFTTRIG, dev->iobase + PCL711_SOFTTRIG_REG);

		ret = comedi_timeout(dev, s, insn, pcl711_ai_eoc, 0);
		if (ret)
			return ret;

		data[i] = pcl711_ai_get_sample(dev, s);
	}

	return insn->n;
}

static int pcl711_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	struct pcl711_private *devpriv = dev->private;
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

	if (cmd->scan_begin_src == TRIG_EXT) {
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	} else {
#define MAX_SPEED 1000
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 MAX_SPEED);
	}

	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4 */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		i8253_cascade_ns_to_timer(I8254_OSC_BASE_2MHZ,
					  &devpriv->divisor1,
					  &devpriv->divisor2,
					  &arg, cmd->flags);
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	return 0;
}

static void pcl711_ai_load_counters(struct comedi_device *dev)
{
	struct pcl711_private *devpriv = dev->private;
	unsigned long timer_base = dev->iobase + PCL711_TIMER_BASE;

	i8254_set_mode(timer_base, 0, 1, I8254_MODE2 | I8254_BINARY);
	i8254_set_mode(timer_base, 0, 2, I8254_MODE2 | I8254_BINARY);

	i8254_write(timer_base, 0, 1, devpriv->divisor1);
	i8254_write(timer_base, 0, 2, devpriv->divisor2);
}

static int pcl711_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	pcl711_set_changain(dev, s, cmd->chanlist[0]);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		pcl711_ai_load_counters(dev);
		outb(PCL711_INT_STAT_CLR, dev->iobase + PCL711_INT_STAT_REG);
		pcl711_ai_set_mode(dev, PCL711_MODE_PACER_IRQ);
	} else {
		pcl711_ai_set_mode(dev, PCL711_MODE_EXT_IRQ);
	}

	return 0;
}

static void pcl711_ao_write(struct comedi_device *dev,
			    unsigned int chan, unsigned int val)
{
	outb(val & 0xff, dev->iobase + PCL711_AO_LSB_REG(chan));
	outb((val >> 8) & 0xff, dev->iobase + PCL711_AO_MSB_REG(chan));
}

static int pcl711_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		pcl711_ao_write(dev, chan, val);
	}
	s->readback[chan] = val;

	return insn->n;
}

static int pcl711_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int val;

	val = inb(dev->iobase + PCL711_DI_LSB_REG);
	val |= (inb(dev->iobase + PCL711_DI_MSB_REG) << 8);

	data[1] = val;

	return insn->n;
}

static int pcl711_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int mask;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (mask & 0x00ff)
			outb(s->state & 0xff, dev->iobase + PCL711_DO_LSB_REG);
		if (mask & 0xff00)
			outb((s->state >> 8), dev->iobase + PCL711_DO_MSB_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static int pcl711_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcl711_board *board = dev->board_ptr;
	struct pcl711_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	if (it->options[1] && it->options[1] <= board->maxirq) {
		ret = request_irq(it->options[1], pcl711_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	if (board->n_aichan > 8)
		s->subdev_flags	|= SDF_DIFF;
	s->n_chan	= board->n_aichan;
	s->maxdata	= 0xfff;
	s->range_table	= board->ai_range_type;
	s->insn_read	= pcl711_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 1;
		s->do_cmdtest	= pcl711_ai_cmdtest;
		s->do_cmd	= pcl711_ai_cmd;
		s->cancel	= pcl711_ai_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= board->n_aochan;
	s->maxdata	= 0xfff;
	s->range_table	= &range_bipolar5;
	s->insn_write	= pcl711_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcl711_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcl711_do_insn_bits;

	/* clear DAC */
	pcl711_ao_write(dev, 0, 0x0);
	pcl711_ao_write(dev, 1, 0x0);

	return 0;
}

static struct comedi_driver pcl711_driver = {
	.driver_name	= "pcl711",
	.module		= THIS_MODULE,
	.attach		= pcl711_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct pcl711_board),
};
module_comedi_driver(pcl711_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for PCL-711 compatible boards");
MODULE_LICENSE("GPL");

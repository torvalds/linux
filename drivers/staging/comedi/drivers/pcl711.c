/*
   comedi/drivers/pcl711.c
   hardware driver for PC-LabCard PCL-711 and AdSys ACL-8112
   and compatibles

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 1998 David A. Schleef <ds@schleef.org>
   Janne Jalkanen <jalkanen@cs.hut.fi>
   Eric Bunn <ebu@cs.hut.fi>

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
Driver: pcl711
Description: Advantech PCL-711 and 711b, ADLink ACL-8112
Author: ds, Janne Jalkanen <jalkanen@cs.hut.fi>, Eric Bunn <ebu@cs.hut.fi>
Status: mostly complete
Devices: [Advantech] PCL-711 (pcl711), PCL-711B (pcl711b),
  [AdLink] ACL-8112HG (acl8112hg), ACL-8112DG (acl8112dg)

Since these boards do not have DMA or FIFOs, only immediate mode is
supported.

*/

/*
   Dave Andruczyk <dave@tech.buffalostate.edu> also wrote a
   driver for the PCL-711.  I used a few ideas from his driver
   here.  His driver also has more comments, if you are
   interested in understanding how this driver works.
   http://tech.buffalostate.edu/~dave/driver/

   The ACL-8112 driver was hacked from the sources of the PCL-711
   driver (the 744 chip used on the 8112 is almost the same as
   the 711b chip, but it has more I/O channels) by
   Janne Jalkanen (jalkanen@cs.hut.fi) and
   Erik Bunn (ebu@cs.hut.fi).  Remerged with the PCL-711 driver
   by ds.

   [acl-8112]
   This driver supports both TRIGNOW and TRIGCLK,
   but does not yet support DMA transfers.  It also supports
   both high (HG) and low (DG) versions of the card, though
   the HG version has been untested.

 */

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/ioport.h>
#include <linux/delay.h>

#include "comedi_fc.h"
#include "8253.h"

#define PCL711_SIZE 16

#define PCL711_CTR0 0
#define PCL711_CTR1 1
#define PCL711_CTR2 2
#define PCL711_CTRCTL 3
#define PCL711_AD_LO 4
#define PCL711_DA0_LO 4
#define PCL711_AD_HI 5
#define PCL711_DA0_HI 5
#define PCL711_DI_LO 6
#define PCL711_DA1_LO 6
#define PCL711_DI_HI 7
#define PCL711_DA1_HI 7
#define PCL711_CLRINTR 8
#define PCL711_GAIN 9
#define PCL711_MUX 10
#define PCL711_MODE 11
#define PCL711_SOFTTRIG 12
#define PCL711_DO_LO 13
#define PCL711_DO_HI 14

static const struct comedi_lrange range_pcl711b_ai = { 5, {
							   BIP_RANGE(5),
							   BIP_RANGE(2.5),
							   BIP_RANGE(1.25),
							   BIP_RANGE(0.625),
							   BIP_RANGE(0.3125)
							   }
};

static const struct comedi_lrange range_acl8112hg_ai = { 12, {
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

static const struct comedi_lrange range_acl8112dg_ai = { 9, {
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

/*
 * flags
 */

#define PCL711_TIMEOUT 100
#define PCL711_DRDY 0x10

static const int i8253_osc_base = 500;	/* 2 Mhz */

struct pcl711_board {

	const char *name;
	int is_pcl711b;
	int is_8112;
	int is_dg;
	int n_ranges;
	int n_aichan;
	int n_aochan;
	int maxirq;
	const struct comedi_lrange *ai_range_type;
};

struct pcl711_private {

	int board;
	int adchan;
	int ntrig;
	int aip[8];
	int mode;
	unsigned int ao_readback[2];
	unsigned int divisor1;
	unsigned int divisor2;
};

static irqreturn_t pcl711_interrupt(int irq, void *d)
{
	int lo, hi;
	int data;
	struct comedi_device *dev = d;
	const struct pcl711_board *board = comedi_board(dev);
	struct pcl711_private *devpriv = dev->private;
	struct comedi_subdevice *s = &dev->subdevices[0];

	if (!dev->attached) {
		comedi_error(dev, "spurious interrupt");
		return IRQ_HANDLED;
	}

	hi = inb(dev->iobase + PCL711_AD_HI);
	lo = inb(dev->iobase + PCL711_AD_LO);
	outb(0, dev->iobase + PCL711_CLRINTR);

	data = (hi << 8) | lo;

	/* FIXME! Nothing else sets ntrig! */
	if (!(--devpriv->ntrig)) {
		if (board->is_8112)
			outb(1, dev->iobase + PCL711_MODE);
		else
			outb(0, dev->iobase + PCL711_MODE);

		s->async->events |= COMEDI_CB_EOA;
	}
	comedi_event(dev, s);
	return IRQ_HANDLED;
}

static void pcl711_set_changain(struct comedi_device *dev, int chan)
{
	const struct pcl711_board *board = comedi_board(dev);
	int chan_register;

	outb(CR_RANGE(chan), dev->iobase + PCL711_GAIN);

	chan_register = CR_CHAN(chan);

	if (board->is_8112) {

		/*
		 *  Set the correct channel.  The two channel banks are switched
		 *  using the mask value.
		 *  NB: To use differential channels, you should use
		 *  mask = 0x30, but I haven't written the support for this
		 *  yet. /JJ
		 */

		if (chan_register >= 8)
			chan_register = 0x20 | (chan_register & 0x7);
		else
			chan_register |= 0x10;
	} else {
		outb(chan_register, dev->iobase + PCL711_MUX);
	}
}

static int pcl711_ai_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	const struct pcl711_board *board = comedi_board(dev);
	int i, n;
	int hi, lo;

	pcl711_set_changain(dev, insn->chanspec);

	for (n = 0; n < insn->n; n++) {
		/*
		 *  Write the correct mode (software polling) and start polling
		 *  by writing to the trigger register
		 */
		outb(1, dev->iobase + PCL711_MODE);

		if (!board->is_8112)
			outb(0, dev->iobase + PCL711_SOFTTRIG);

		i = PCL711_TIMEOUT;
		while (--i) {
			hi = inb(dev->iobase + PCL711_AD_HI);
			if (!(hi & PCL711_DRDY))
				goto ok;
			udelay(1);
		}
		printk(KERN_ERR "comedi%d: pcl711: A/D timeout\n", dev->minor);
		return -ETIME;

ok:
		lo = inb(dev->iobase + PCL711_AD_LO);

		data[n] = ((hi & 0xf) << 8) | lo;
	}

	return n;
}

static int pcl711_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	struct pcl711_private *devpriv = dev->private;
	int tmp;
	int err = 0;

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
#define TIMER_BASE 100
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 MAX_SPEED);
	}

	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_NONE) {
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);
	} else {
		/* ignore */
	}

	if (err)
		return 3;

	/* step 4 */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		i8253_cascade_ns_to_timer_2div(TIMER_BASE,
					       &devpriv->divisor1,
					       &devpriv->divisor2,
					       &cmd->scan_begin_arg,
					       cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}

	if (err)
		return 4;

	return 0;
}

static int pcl711_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcl711_private *devpriv = dev->private;
	int timer1, timer2;
	struct comedi_cmd *cmd = &s->async->cmd;

	pcl711_set_changain(dev, cmd->chanlist[0]);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		/*
		 *  Set timers
		 *      timer chip is an 8253, with timers 1 and 2
		 *      cascaded
		 *  0x74 = Select Counter 1 | LSB/MSB | Mode=2 | Binary
		 *        Mode 2 = Rate generator
		 *
		 *  0xb4 = Select Counter 2 | LSB/MSB | Mode=2 | Binary
		 */

		timer1 = timer2 = 0;
		i8253_cascade_ns_to_timer(i8253_osc_base, &timer1, &timer2,
					  &cmd->scan_begin_arg,
					  TRIG_ROUND_NEAREST);

		outb(0x74, dev->iobase + PCL711_CTRCTL);
		outb(timer1 & 0xff, dev->iobase + PCL711_CTR1);
		outb((timer1 >> 8) & 0xff, dev->iobase + PCL711_CTR1);
		outb(0xb4, dev->iobase + PCL711_CTRCTL);
		outb(timer2 & 0xff, dev->iobase + PCL711_CTR2);
		outb((timer2 >> 8) & 0xff, dev->iobase + PCL711_CTR2);

		/* clear pending interrupts (just in case) */
		outb(0, dev->iobase + PCL711_CLRINTR);

		/*
		 *  Set mode to IRQ transfer
		 */
		outb(devpriv->mode | 6, dev->iobase + PCL711_MODE);
	} else {
		/* external trigger */
		outb(devpriv->mode | 3, dev->iobase + PCL711_MODE);
	}

	return 0;
}

/*
   analog output
*/
static int pcl711_ao_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	struct pcl711_private *devpriv = dev->private;
	int n;
	int chan = CR_CHAN(insn->chanspec);

	for (n = 0; n < insn->n; n++) {
		outb((data[n] & 0xff),
		     dev->iobase + (chan ? PCL711_DA1_LO : PCL711_DA0_LO));
		outb((data[n] >> 8),
		     dev->iobase + (chan ? PCL711_DA1_HI : PCL711_DA0_HI));

		devpriv->ao_readback[chan] = data[n];
	}

	return n;
}

static int pcl711_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct pcl711_private *devpriv = dev->private;
	int n;
	int chan = CR_CHAN(insn->chanspec);

	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_readback[chan];

	return n;

}

/* Digital port read - Untested on 8112 */
static int pcl711_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	data[1] = inb(dev->iobase + PCL711_DI_LO) |
	    (inb(dev->iobase + PCL711_DI_HI) << 8);

	return insn->n;
}

/* Digital port write - Untested on 8112 */
static int pcl711_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
	}
	if (data[0] & 0x00ff)
		outb(s->state & 0xff, dev->iobase + PCL711_DO_LO);
	if (data[0] & 0xff00)
		outb((s->state >> 8), dev->iobase + PCL711_DO_HI);

	data[1] = s->state;

	return insn->n;
}

static int pcl711_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcl711_board *board = comedi_board(dev);
	struct pcl711_private *devpriv;
	int ret;
	unsigned int irq;
	struct comedi_subdevice *s;

	ret = comedi_request_region(dev, it->options[0], PCL711_SIZE);
	if (ret)
		return ret;

	/* grab our IRQ */
	irq = it->options[1];
	if (irq > board->maxirq) {
		printk(KERN_ERR "irq out of range\n");
		return -EINVAL;
	}
	if (irq) {
		if (request_irq(irq, pcl711_interrupt, 0, dev->board_name,
			        dev)) {
			printk(KERN_ERR "unable to allocate irq %u\n", irq);
			return -EINVAL;
		} else {
			printk(KERN_INFO "( irq = %u )\n", irq);
		}
	}
	dev->irq = irq;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	s = &dev->subdevices[0];
	/* AI subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = board->n_aichan;
	s->maxdata = 0xfff;
	s->len_chanlist = 1;
	s->range_table = board->ai_range_type;
	s->insn_read = pcl711_ai_insn;
	if (irq) {
		dev->read_subdev = s;
		s->subdev_flags |= SDF_CMD_READ;
		s->do_cmdtest = pcl711_ai_cmdtest;
		s->do_cmd = pcl711_ai_cmd;
	}

	s = &dev->subdevices[1];
	/* AO subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = board->n_aochan;
	s->maxdata = 0xfff;
	s->len_chanlist = 1;
	s->range_table = &range_bipolar5;
	s->insn_write = pcl711_ao_insn;
	s->insn_read = pcl711_ao_insn_read;

	s = &dev->subdevices[2];
	/* 16-bit digital input */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->len_chanlist = 16;
	s->range_table = &range_digital;
	s->insn_bits = pcl711_di_insn_bits;

	s = &dev->subdevices[3];
	/* 16-bit digital out */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->len_chanlist = 16;
	s->range_table = &range_digital;
	s->state = 0;
	s->insn_bits = pcl711_do_insn_bits;

	/*
	   this is the "base value" for the mode register, which is
	   used for the irq on the PCL711
	 */
	if (board->is_pcl711b)
		devpriv->mode = (dev->irq << 4);

	/* clear DAC */
	outb(0, dev->iobase + PCL711_DA0_LO);
	outb(0, dev->iobase + PCL711_DA0_HI);
	outb(0, dev->iobase + PCL711_DA1_LO);
	outb(0, dev->iobase + PCL711_DA1_HI);

	printk(KERN_INFO "\n");

	return 0;
}

static const struct pcl711_board boardtypes[] = {
	{ "pcl711", 0, 0, 0, 5, 8, 1, 0, &range_bipolar5 },
	{ "pcl711b", 1, 0, 0, 5, 8, 1, 7, &range_pcl711b_ai },
	{ "acl8112hg", 0, 1, 0, 12, 16, 2, 15, &range_acl8112hg_ai },
	{ "acl8112dg", 0, 1, 1, 9, 16, 2, 15, &range_acl8112dg_ai },
};

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
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

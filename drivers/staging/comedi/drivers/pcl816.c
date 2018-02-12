// SPDX-License-Identifier: GPL-2.0
/*
 * pcl816.c
 * Comedi driver for Advantech PCL-816 cards
 *
 * Author:  Juan Grigera <juan@grigera.com.ar>
 * based on pcl818 by Michal Dobes <dobes@tesnet.cz> and bits of pcl812
 */

/*
 * Driver: pcl816
 * Description: Advantech PCL-816 cards, PCL-814
 * Devices: [Advantech] PCL-816 (pcl816), PCL-814B (pcl814b)
 * Author: Juan Grigera <juan@grigera.com.ar>
 * Status: works
 * Updated: Tue,  2 Apr 2002 23:15:21 -0800
 *
 * PCL 816 and 814B have 16 SE/DIFF ADCs, 16 DACs, 16 DI and 16 DO.
 * Differences are at resolution (16 vs 12 bits).
 *
 * The driver support AI command mode, other subdevices not written.
 *
 * Analog output and digital input and output are not supported.
 *
 * Configuration Options:
 *   [0] - IO Base
 *   [1] - IRQ	(0=disable, 2, 3, 4, 5, 6, 7)
 *   [2] - DMA	(0=disable, 1, 3)
 *   [3] - 0, 10=10MHz clock for 8254
 *	       1= 1MHz clock for 8254
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_isadma.h"
#include "comedi_8254.h"

/*
 * Register I/O map
 */
#define PCL816_DO_DI_LSB_REG			0x00
#define PCL816_DO_DI_MSB_REG			0x01
#define PCL816_TIMER_BASE			0x04
#define PCL816_AI_LSB_REG			0x08
#define PCL816_AI_MSB_REG			0x09
#define PCL816_RANGE_REG			0x09
#define PCL816_CLRINT_REG			0x0a
#define PCL816_MUX_REG				0x0b
#define PCL816_MUX_SCAN(_first, _last)		(((_last) << 4) | (_first))
#define PCL816_CTRL_REG				0x0c
#define PCL816_CTRL_SOFT_TRIG			BIT(0)
#define PCL816_CTRL_PACER_TRIG			BIT(1)
#define PCL816_CTRL_EXT_TRIG			BIT(2)
#define PCL816_CTRL_POE				BIT(3)
#define PCL816_CTRL_DMAEN			BIT(4)
#define PCL816_CTRL_INTEN			BIT(5)
#define PCL816_CTRL_DMASRC_SLOT(x)		(((x) & 0x3) << 6)
#define PCL816_STATUS_REG			0x0d
#define PCL816_STATUS_NEXT_CHAN_MASK		(0xf << 0)
#define PCL816_STATUS_INTSRC_SLOT(x)		(((x) & 0x3) << 4)
#define PCL816_STATUS_INTSRC_DMA		PCL816_STATUS_INTSRC_SLOT(3)
#define PCL816_STATUS_INTSRC_MASK		PCL816_STATUS_INTSRC_SLOT(3)
#define PCL816_STATUS_INTACT			BIT(6)
#define PCL816_STATUS_DRDY			BIT(7)

#define MAGIC_DMA_WORD 0x5a5a

static const struct comedi_lrange range_pcl816 = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

struct pcl816_board {
	const char *name;
	int ai_maxdata;
	int ai_chanlist;
};

static const struct pcl816_board boardtypes[] = {
	{
		.name		= "pcl816",
		.ai_maxdata	= 0xffff,
		.ai_chanlist	= 1024,
	}, {
		.name		= "pcl814b",
		.ai_maxdata	= 0x3fff,
		.ai_chanlist	= 1024,
	},
};

struct pcl816_private {
	struct comedi_isadma *dma;
	unsigned int ai_poll_ptr;	/*  how many sampes transfer poll */
	unsigned int ai_cmd_running:1;
	unsigned int ai_cmd_canceled:1;
};

static void pcl816_ai_setup_dma(struct comedi_device *dev,
				struct comedi_subdevice *s,
				unsigned int unread_samples)
{
	struct pcl816_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	unsigned int max_samples = comedi_bytes_to_samples(s, desc->maxsize);
	unsigned int nsamples;

	comedi_isadma_disable(dma->chan);

	/*
	 * Determine dma size based on the buffer maxsize plus the number of
	 * unread samples and the number of samples remaining in the command.
	 */
	nsamples = comedi_nsamples_left(s, max_samples + unread_samples);
	if (nsamples > unread_samples) {
		nsamples -= unread_samples;
		desc->size = comedi_samples_to_bytes(s, nsamples);
		comedi_isadma_program(desc);
	}
}

static void pcl816_ai_set_chan_range(struct comedi_device *dev,
				     unsigned int chan,
				     unsigned int range)
{
	outb(chan, dev->iobase + PCL816_MUX_REG);
	outb(range, dev->iobase + PCL816_RANGE_REG);
}

static void pcl816_ai_set_chan_scan(struct comedi_device *dev,
				    unsigned int first_chan,
				    unsigned int last_chan)
{
	outb(PCL816_MUX_SCAN(first_chan, last_chan),
	     dev->iobase + PCL816_MUX_REG);
}

static void pcl816_ai_setup_chanlist(struct comedi_device *dev,
				     unsigned int *chanlist,
				     unsigned int seglen)
{
	unsigned int first_chan = CR_CHAN(chanlist[0]);
	unsigned int last_chan;
	unsigned int range;
	unsigned int i;

	/* store range list to card */
	for (i = 0; i < seglen; i++) {
		last_chan = CR_CHAN(chanlist[i]);
		range = CR_RANGE(chanlist[i]);

		pcl816_ai_set_chan_range(dev, last_chan, range);
	}

	udelay(1);

	pcl816_ai_set_chan_scan(dev, first_chan, last_chan);
}

static void pcl816_ai_clear_eoc(struct comedi_device *dev)
{
	/* writing any value clears the interrupt request */
	outb(0, dev->iobase + PCL816_CLRINT_REG);
}

static void pcl816_ai_soft_trig(struct comedi_device *dev)
{
	/* writing any value triggers a software conversion */
	outb(0, dev->iobase + PCL816_AI_LSB_REG);
}

static unsigned int pcl816_ai_get_sample(struct comedi_device *dev,
					 struct comedi_subdevice *s)
{
	unsigned int val;

	val = inb(dev->iobase + PCL816_AI_MSB_REG) << 8;
	val |= inb(dev->iobase + PCL816_AI_LSB_REG);

	return val & s->maxdata;
}

static int pcl816_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + PCL816_STATUS_REG);
	if ((status & PCL816_STATUS_DRDY) == 0)
		return 0;
	return -EBUSY;
}

static bool pcl816_ai_next_chan(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg) {
		s->async->events |= COMEDI_CB_EOA;
		return false;
	}

	return true;
}

static void transfer_from_dma_buf(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned short *ptr,
				  unsigned int bufptr, unsigned int len)
{
	unsigned short val;
	int i;

	for (i = 0; i < len; i++) {
		val = ptr[bufptr++];
		comedi_buf_write_samples(s, &val, 1);

		if (!pcl816_ai_next_chan(dev, s))
			return;
	}
}

static irqreturn_t pcl816_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->read_subdev;
	struct pcl816_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	unsigned int nsamples;
	unsigned int bufptr;

	if (!dev->attached || !devpriv->ai_cmd_running) {
		pcl816_ai_clear_eoc(dev);
		return IRQ_HANDLED;
	}

	if (devpriv->ai_cmd_canceled) {
		devpriv->ai_cmd_canceled = 0;
		pcl816_ai_clear_eoc(dev);
		return IRQ_HANDLED;
	}

	nsamples = comedi_bytes_to_samples(s, desc->size) -
		   devpriv->ai_poll_ptr;
	bufptr = devpriv->ai_poll_ptr;
	devpriv->ai_poll_ptr = 0;

	/* restart dma with the next buffer */
	dma->cur_dma = 1 - dma->cur_dma;
	pcl816_ai_setup_dma(dev, s, nsamples);

	transfer_from_dma_buf(dev, s, desc->virt_addr, bufptr, nsamples);

	pcl816_ai_clear_eoc(dev);

	comedi_handle_events(dev, s);
	return IRQ_HANDLED;
}

static int check_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      unsigned int *chanlist,
			      unsigned int chanlen)
{
	unsigned int chansegment[16];
	unsigned int i, nowmustbechan, seglen, segpos;

	/*  correct channel and range number check itself comedi/range.c */
	if (chanlen < 1) {
		dev_err(dev->class_dev, "range/channel list is empty!\n");
		return 0;
	}

	if (chanlen > 1) {
		/*  first channel is every time ok */
		chansegment[0] = chanlist[0];
		for (i = 1, seglen = 1; i < chanlen; i++, seglen++) {
			/*  we detect loop, this must by finish */
			if (chanlist[0] == chanlist[i])
				break;
			nowmustbechan =
			    (CR_CHAN(chansegment[i - 1]) + 1) % chanlen;
			if (nowmustbechan != CR_CHAN(chanlist[i])) {
				/*  channel list isn't continuous :-( */
				dev_dbg(dev->class_dev,
					"channel list must be continuous! chanlist[%i]=%d but must be %d or %d!\n",
					i, CR_CHAN(chanlist[i]), nowmustbechan,
					CR_CHAN(chanlist[0]));
				return 0;
			}
			/*  well, this is next correct channel in list */
			chansegment[i] = chanlist[i];
		}

		/*  check whole chanlist */
		for (i = 0, segpos = 0; i < chanlen; i++) {
			if (chanlist[i] != chansegment[i % seglen]) {
				dev_dbg(dev->class_dev,
					"bad channel or range number! chanlist[%i]=%d,%d,%d and not %d,%d,%d!\n",
					i, CR_CHAN(chansegment[i]),
					CR_RANGE(chansegment[i]),
					CR_AREF(chansegment[i]),
					CR_CHAN(chanlist[i % seglen]),
					CR_RANGE(chanlist[i % seglen]),
					CR_AREF(chansegment[i % seglen]));
				return 0;	/*  chan/gain list is strange */
			}
		}
	} else {
		seglen = 1;
	}

	return seglen;	/*  we can serve this with MUX logic */
}

static int pcl816_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_EXT | TRIG_TIMER);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->convert_src == TRIG_TIMER)
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg, 10000);
	else	/* TRIG_EXT */
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
	if (cmd->convert_src == TRIG_TIMER) {
		unsigned int arg = cmd->convert_arg;

		comedi_8254_cascade_ns_to_timer(dev->pacer, &arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	}

	if (err)
		return 4;

	/* step 5: complain about special chanlist considerations */

	if (cmd->chanlist) {
		if (!check_channel_list(dev, s, cmd->chanlist,
					cmd->chanlist_len))
			return 5;	/*  incorrect channels list */
	}

	return 0;
}

static int pcl816_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcl816_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int ctrl;
	unsigned int seglen;

	if (devpriv->ai_cmd_running)
		return -EBUSY;

	seglen = check_channel_list(dev, s, cmd->chanlist, cmd->chanlist_len);
	if (seglen < 1)
		return -EINVAL;
	pcl816_ai_setup_chanlist(dev, cmd->chanlist, seglen);
	udelay(1);

	devpriv->ai_cmd_running = 1;
	devpriv->ai_poll_ptr = 0;
	devpriv->ai_cmd_canceled = 0;

	/* setup and enable dma for the first buffer */
	dma->cur_dma = 0;
	pcl816_ai_setup_dma(dev, s, 0);

	comedi_8254_set_mode(dev->pacer, 0, I8254_MODE1 | I8254_BINARY);
	comedi_8254_write(dev->pacer, 0, 0x0ff);
	udelay(1);
	comedi_8254_update_divisors(dev->pacer);
	comedi_8254_pacer_enable(dev->pacer, 1, 2, true);

	ctrl = PCL816_CTRL_INTEN | PCL816_CTRL_DMAEN |
	       PCL816_CTRL_DMASRC_SLOT(0);
	if (cmd->convert_src == TRIG_TIMER)
		ctrl |= PCL816_CTRL_PACER_TRIG;
	else	/* TRIG_EXT */
		ctrl |= PCL816_CTRL_EXT_TRIG;

	outb(ctrl, dev->iobase + PCL816_CTRL_REG);
	outb((dma->chan << 4) | dev->irq,
	     dev->iobase + PCL816_STATUS_REG);

	return 0;
}

static int pcl816_ai_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcl816_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc;
	unsigned long flags;
	unsigned int poll;
	int ret;

	spin_lock_irqsave(&dev->spinlock, flags);

	poll = comedi_isadma_poll(dma);
	poll = comedi_bytes_to_samples(s, poll);
	if (poll > devpriv->ai_poll_ptr) {
		desc = &dma->desc[dma->cur_dma];
		transfer_from_dma_buf(dev, s, desc->virt_addr,
				      devpriv->ai_poll_ptr,
				      poll - devpriv->ai_poll_ptr);
		/* new buffer position */
		devpriv->ai_poll_ptr = poll;

		comedi_handle_events(dev, s);

		ret = comedi_buf_n_bytes_ready(s);
	} else {
		/* no new samples */
		ret = 0;
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return ret;
}

static int pcl816_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct pcl816_private *devpriv = dev->private;

	if (!devpriv->ai_cmd_running)
		return 0;

	outb(0, dev->iobase + PCL816_CTRL_REG);
	pcl816_ai_clear_eoc(dev);

	comedi_8254_pacer_enable(dev->pacer, 1, 2, false);

	devpriv->ai_cmd_running = 0;
	devpriv->ai_cmd_canceled = 1;

	return 0;
}

static int pcl816_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	int ret = 0;
	int i;

	outb(PCL816_CTRL_SOFT_TRIG, dev->iobase + PCL816_CTRL_REG);

	pcl816_ai_set_chan_range(dev, chan, range);
	pcl816_ai_set_chan_scan(dev, chan, chan);

	for (i = 0; i < insn->n; i++) {
		pcl816_ai_clear_eoc(dev);
		pcl816_ai_soft_trig(dev);

		ret = comedi_timeout(dev, s, insn, pcl816_ai_eoc, 0);
		if (ret)
			break;

		data[i] = pcl816_ai_get_sample(dev, s);
	}
	outb(0, dev->iobase + PCL816_CTRL_REG);
	pcl816_ai_clear_eoc(dev);

	return ret ? ret : insn->n;
}

static int pcl816_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[1] = inb(dev->iobase + PCL816_DO_DI_LSB_REG) |
		  (inb(dev->iobase + PCL816_DO_DI_MSB_REG) << 8);

	return insn->n;
}

static int pcl816_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data)) {
		outb(s->state & 0xff, dev->iobase + PCL816_DO_DI_LSB_REG);
		outb((s->state >> 8), dev->iobase + PCL816_DO_DI_MSB_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static void pcl816_reset(struct comedi_device *dev)
{
	outb(0, dev->iobase + PCL816_CTRL_REG);
	pcl816_ai_set_chan_range(dev, 0, 0);
	pcl816_ai_clear_eoc(dev);

	/* set all digital outputs low */
	outb(0, dev->iobase + PCL816_DO_DI_LSB_REG);
	outb(0, dev->iobase + PCL816_DO_DI_MSB_REG);
}

static void pcl816_alloc_irq_and_dma(struct comedi_device *dev,
				     struct comedi_devconfig *it)
{
	struct pcl816_private *devpriv = dev->private;
	unsigned int irq_num = it->options[1];
	unsigned int dma_chan = it->options[2];

	/* only IRQs 2-7 and DMA channels 3 and 1 are valid */
	if (!(irq_num >= 2 && irq_num <= 7) ||
	    !(dma_chan == 3 || dma_chan == 1))
		return;

	if (request_irq(irq_num, pcl816_interrupt, 0, dev->board_name, dev))
		return;

	/* DMA uses two 16K buffers */
	devpriv->dma = comedi_isadma_alloc(dev, 2, dma_chan, dma_chan,
					   PAGE_SIZE * 4, COMEDI_ISADMA_READ);
	if (!devpriv->dma)
		free_irq(irq_num, dev);
	else
		dev->irq = irq_num;
}

static void pcl816_free_dma(struct comedi_device *dev)
{
	struct pcl816_private *devpriv = dev->private;

	if (devpriv)
		comedi_isadma_free(devpriv->dma);
}

static int pcl816_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcl816_board *board = dev->board_ptr;
	struct pcl816_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	/* an IRQ and DMA are required to support async commands */
	pcl816_alloc_irq_and_dma(dev, it);

	dev->pacer = comedi_8254_init(dev->iobase + PCL816_TIMER_BASE,
				      I8254_OSC_BASE_10MHZ, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_CMD_READ | SDF_DIFF;
	s->n_chan	= 16;
	s->maxdata	= board->ai_maxdata;
	s->range_table	= &range_pcl816;
	s->insn_read	= pcl816_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= board->ai_chanlist;
		s->do_cmdtest	= pcl816_ai_cmdtest;
		s->do_cmd	= pcl816_ai_cmd;
		s->poll		= pcl816_ai_poll;
		s->cancel	= pcl816_ai_cancel;
	}

	/* Piggyback Slot1 subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_UNUSED;

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcl816_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcl816_do_insn_bits;

	pcl816_reset(dev);

	return 0;
}

static void pcl816_detach(struct comedi_device *dev)
{
	if (dev->private) {
		pcl816_ai_cancel(dev, dev->read_subdev);
		pcl816_reset(dev);
	}
	pcl816_free_dma(dev);
	comedi_legacy_detach(dev);
}

static struct comedi_driver pcl816_driver = {
	.driver_name	= "pcl816",
	.module		= THIS_MODULE,
	.attach		= pcl816_attach,
	.detach		= pcl816_detach,
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct pcl816_board),
};
module_comedi_driver(pcl816_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

/*
   comedi/drivers/pcl816.c

   Author:  Juan Grigera <juan@grigera.com.ar>
	    based on pcl818 by Michal Dobes <dobes@tesnet.cz> and bits of pcl812

   hardware driver for Advantech cards:
    card:   PCL-816, PCL814B
    driver: pcl816
*/
/*
Driver: pcl816
Description: Advantech PCL-816 cards, PCL-814
Author: Juan Grigera <juan@grigera.com.ar>
Devices: [Advantech] PCL-816 (pcl816), PCL-814B (pcl814b)
Status: works
Updated: Tue,  2 Apr 2002 23:15:21 -0800

PCL 816 and 814B have 16 SE/DIFF ADCs, 16 DACs, 16 DI and 16 DO.
Differences are at resolution (16 vs 12 bits).

The driver support AI command mode, other subdevices not written.

Analog output and digital input and output are not supported.

Configuration Options:
  [0] - IO Base
  [1] - IRQ	(0=disable, 2, 3, 4, 5, 6, 7)
  [2] - DMA	(0=disable, 1, 3)
  [3] - 0, 10=10MHz clock for 8254
	    1= 1MHz clock for 8254

*/

#include <linux/module.h>
#include "../comedidev.h"

#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <asm/dma.h>

#include "comedi_fc.h"
#include "8253.h"

#define PCL816_TIMER_BASE			0x04

/* R: A/D high byte W: A/D range control */
#define PCL816_RANGE 9
/* W: clear INT request */
#define PCL816_CLRINT 10
/* R: next mux scan channel W: mux scan channel & range control pointer */
#define PCL816_MUX 11
/* R/W: operation control register */
#define PCL816_CONTROL 12

/* R: return status byte  W: set DMA/IRQ */
#define PCL816_STATUS 13
#define PCL816_STATUS_DRDY_MASK 0x80

/* R: low byte of A/D W: soft A/D trigger */
#define PCL816_AD_LO 8
/* R: high byte of A/D W: A/D range control */
#define PCL816_AD_HI 9

/* type of interrupt handler */
#define INT_TYPE_AI1_INT 1
#define INT_TYPE_AI1_DMA 2
#define INT_TYPE_AI3_INT 4
#define INT_TYPE_AI3_DMA 5

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
	unsigned int IRQbits;
	int ai_maxdata;
	int ao_maxdata;
	int ai_chanlist;
};

static const struct pcl816_board boardtypes[] = {
	{
		.name		= "pcl816",
		.IRQbits	= 0x00fc,
		.ai_maxdata	= 0xffff,
		.ao_maxdata	= 0xffff,
		.ai_chanlist	= 1024,
	}, {
		.name		= "pcl814b",
		.IRQbits	= 0x00fc,
		.ai_maxdata	= 0x3fff,
		.ao_maxdata	= 0x3fff,
		.ai_chanlist	= 1024,
	},
};

struct pcl816_private {
	unsigned int dma;	/*  used DMA, 0=don't use DMA */
	unsigned int dmapages;
	unsigned int hwdmasize;
	unsigned long dmabuf[2];	/*  pointers to begin of DMA buffers */
	unsigned int hwdmaptr[2];	/*  hardware address of DMA buffers */
	unsigned int dmasamplsize;	/*  size in samples hwdmasize[0]/2 */
	int next_dma_buf;	/*  which DMA buffer will be used next round */
	long dma_runs_to_end;	/*  how many we must permorm DMA transfer to end of record */
	unsigned long last_dma_run;	/*  how many bytes we must transfer on last DMA page */
	unsigned char ai_neverending;	/*  if=1, then we do neverending record (you must use cancel()) */
	int irq_blocked;	/*  1=IRQ now uses any subdev */
	int irq_was_now_closed;	/*  when IRQ finish, there's stored int816_mode for last interrupt */
	int int816_mode;	/*  who now uses IRQ - 1=AI1 int, 2=AI1 dma, 3=AI3 int, 4AI3 dma */
	int ai_act_scan;	/*  how many scans we finished */
	unsigned int ai_act_chanlist[16];	/*  MUX setting for actual AI operations */
	unsigned int ai_act_chanlist_len;	/*  how long is actual MUX list */
	unsigned int ai_act_chanlist_pos;	/*  actual position in MUX list */
	unsigned int ai_poll_ptr;	/*  how many sampes transfer poll */
};

/*
==============================================================================
*/
static int check_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      unsigned int *chanlist, unsigned int chanlen);
static void setup_channel_list(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned int *chanlist, unsigned int seglen);
static int pcl816_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s);

static void pcl816_start_pacer(struct comedi_device *dev, bool load_counters,
			       unsigned int divisor1, unsigned int divisor2)
{
	unsigned long timer_base = dev->iobase + PCL816_TIMER_BASE;

	i8254_set_mode(timer_base, 0, 0, I8254_MODE1 | I8254_BINARY);
	i8254_write(timer_base, 0, 0, 0x00ff);
	udelay(1);

	i8254_set_mode(timer_base, 0, 2, I8254_MODE2 | I8254_BINARY);
	i8254_set_mode(timer_base, 0, 1, I8254_MODE2 | I8254_BINARY);
	udelay(1);

	if (load_counters) {
		i8254_write(timer_base, 0, 2, divisor2);
		i8254_write(timer_base, 0, 1, divisor1);
	}
}

static unsigned int pcl816_ai_get_sample(struct comedi_device *dev,
					 struct comedi_subdevice *s)
{
	unsigned int val;

	val = inb(dev->iobase + PCL816_AD_HI) << 8;
	val |= inb(dev->iobase + PCL816_AD_LO);

	return val & s->maxdata;
}

static int pcl816_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + PCL816_STATUS);
	if ((status & PCL816_STATUS_DRDY_MASK) == 0)
		return 0;
	return -EBUSY;
}

static int pcl816_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int ret;
	int n;

	/*  software trigger, DMA and INT off */
	outb(0, dev->iobase + PCL816_CONTROL);
	/*  clear INT (conversion end) flag */
	outb(0, dev->iobase + PCL816_CLRINT);

	/*  Set the input channel */
	outb(CR_CHAN(insn->chanspec) & 0xf, dev->iobase + PCL816_MUX);
	/* select gain */
	outb(CR_RANGE(insn->chanspec), dev->iobase + PCL816_RANGE);

	for (n = 0; n < insn->n; n++) {

		outb(0, dev->iobase + PCL816_AD_LO);	/* start conversion */

		ret = comedi_timeout(dev, s, insn, pcl816_ai_eoc, 0);
		if (ret) {
			/* clear INT (conversion end) flag */
			outb(0, dev->iobase + PCL816_CLRINT);
			return ret;
		}

		data[n] = pcl816_ai_get_sample(dev, s);

		/* clear INT (conversion end) flag */
		outb(0, dev->iobase + PCL816_CLRINT);
	}
	return n;
}

/*
==============================================================================
   analog input interrupt mode 1 & 3, 818 cards
   one sample per interrupt version
*/
static irqreturn_t interrupt_pcl816_ai_mode13_int(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcl816_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;
	int timeout = 50;	/* wait max 50us */

	while (timeout--) {
		if (!(inb(dev->iobase + PCL816_STATUS) &
		      PCL816_STATUS_DRDY_MASK))
			break;
		udelay(1);
	}
	if (!timeout) {		/*  timeout, bail error */
		outb(0, dev->iobase + PCL816_CLRINT);	/* clear INT request */
		comedi_error(dev, "A/D mode1/3 IRQ without DRDY!");
		pcl816_ai_cancel(dev, s);
		s->async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
		comedi_event(dev, s);
		return IRQ_HANDLED;

	}

	comedi_buf_put(s->async, pcl816_ai_get_sample(dev, s));

	outb(0, dev->iobase + PCL816_CLRINT);	/* clear INT request */

	if (++devpriv->ai_act_chanlist_pos >= devpriv->ai_act_chanlist_len)
		devpriv->ai_act_chanlist_pos = 0;

	s->async->cur_chan++;
	if (s->async->cur_chan >= cmd->chanlist_len) {
		s->async->cur_chan = 0;
		devpriv->ai_act_scan++;
	}

	if (!devpriv->ai_neverending)
					/* all data sampled */
		if (devpriv->ai_act_scan >= cmd->stop_arg) {
			/* all data sampled */
			pcl816_ai_cancel(dev, s);
			s->async->events |= COMEDI_CB_EOA;
		}
	comedi_event(dev, s);
	return IRQ_HANDLED;
}

/*
==============================================================================
   analog input dma mode 1 & 3, 816 cards
*/
static void transfer_from_dma_buf(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned short *ptr,
				  unsigned int bufptr, unsigned int len)
{
	struct pcl816_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int i;

	s->async->events = 0;

	for (i = 0; i < len; i++) {

		comedi_buf_put(s->async, ptr[bufptr++]);

		if (++devpriv->ai_act_chanlist_pos >=
		    devpriv->ai_act_chanlist_len) {
			devpriv->ai_act_chanlist_pos = 0;
		}

		s->async->cur_chan++;
		if (s->async->cur_chan >= cmd->chanlist_len) {
			s->async->cur_chan = 0;
			devpriv->ai_act_scan++;
		}

		if (!devpriv->ai_neverending)
						/*  all data sampled */
			if (devpriv->ai_act_scan >= cmd->stop_arg) {
				pcl816_ai_cancel(dev, s);
				s->async->events |= COMEDI_CB_EOA;
				s->async->events |= COMEDI_CB_BLOCK;
				break;
			}
	}

	comedi_event(dev, s);
}

static irqreturn_t interrupt_pcl816_ai_mode13_dma(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcl816_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	int len, bufptr, this_dma_buf;
	unsigned long dma_flags;
	unsigned short *ptr;

	disable_dma(devpriv->dma);
	this_dma_buf = devpriv->next_dma_buf;

	/*  switch dma bufs */
	if ((devpriv->dma_runs_to_end > -1) || devpriv->ai_neverending) {

		devpriv->next_dma_buf = 1 - devpriv->next_dma_buf;
		set_dma_mode(devpriv->dma, DMA_MODE_READ);
		dma_flags = claim_dma_lock();
/* clear_dma_ff (devpriv->dma); */
		set_dma_addr(devpriv->dma,
			     devpriv->hwdmaptr[devpriv->next_dma_buf]);
		if (devpriv->dma_runs_to_end)
			set_dma_count(devpriv->dma, devpriv->hwdmasize);
		else
			set_dma_count(devpriv->dma, devpriv->last_dma_run);
		release_dma_lock(dma_flags);
		enable_dma(devpriv->dma);
	}

	devpriv->dma_runs_to_end--;
	outb(0, dev->iobase + PCL816_CLRINT);	/* clear INT request */

	ptr = (unsigned short *)devpriv->dmabuf[this_dma_buf];

	len = (devpriv->hwdmasize >> 1) - devpriv->ai_poll_ptr;
	bufptr = devpriv->ai_poll_ptr;
	devpriv->ai_poll_ptr = 0;

	transfer_from_dma_buf(dev, s, ptr, bufptr, len);
	return IRQ_HANDLED;
}

/*
==============================================================================
    INT procedure
*/
static irqreturn_t interrupt_pcl816(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcl816_private *devpriv = dev->private;

	if (!dev->attached) {
		comedi_error(dev, "premature interrupt");
		return IRQ_HANDLED;
	}

	switch (devpriv->int816_mode) {
	case INT_TYPE_AI1_DMA:
	case INT_TYPE_AI3_DMA:
		return interrupt_pcl816_ai_mode13_dma(irq, d);
	case INT_TYPE_AI1_INT:
	case INT_TYPE_AI3_INT:
		return interrupt_pcl816_ai_mode13_int(irq, d);
	}

	outb(0, dev->iobase + PCL816_CLRINT);	/* clear INT request */
	if (!dev->irq || !devpriv->irq_blocked || !devpriv->int816_mode) {
		if (devpriv->irq_was_now_closed) {
			devpriv->irq_was_now_closed = 0;
			/*  comedi_error(dev,"last IRQ.."); */
			return IRQ_HANDLED;
		}
		comedi_error(dev, "bad IRQ!");
		return IRQ_NONE;
	}
	comedi_error(dev, "IRQ from unknown source!");
	return IRQ_NONE;
}

/*
==============================================================================
*/
static int pcl816_ai_cmdtest(struct comedi_device *dev,
			     struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp, divisor1 = 0, divisor2 = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_EXT | TRIG_TIMER);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;


	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->convert_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg, 10000);
	else	/* TRIG_EXT */
		err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_min(&cmd->stop_arg, 1);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;


	/* step 4: fix up any arguments */
	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		i8253_cascade_ns_to_timer(I8254_OSC_BASE_10MHZ,
					  &divisor1, &divisor2,
					  &cmd->convert_arg, cmd->flags);
		if (cmd->convert_arg < 10000)
			cmd->convert_arg = 10000;
		if (tmp != cmd->convert_arg)
			err++;
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
	unsigned int divisor1 = 0, divisor2 = 0, dma_flags, bytes, dmairq;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int seglen;

	if (devpriv->irq_blocked)
		return -EBUSY;

	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < 10000)
			cmd->convert_arg = 10000;

		i8253_cascade_ns_to_timer(I8254_OSC_BASE_10MHZ,
					  &divisor1, &divisor2,
					  &cmd->convert_arg, cmd->flags);

		/*  PCL816 crash if any divisor is set to 1 */
		if (divisor1 == 1) {
			divisor1 = 2;
			divisor2 /= 2;
		}
		if (divisor2 == 1) {
			divisor2 = 2;
			divisor1 /= 2;
		}
	}

	pcl816_start_pacer(dev, false, 0, 0);

	seglen = check_channel_list(dev, s, cmd->chanlist, cmd->chanlist_len);
	if (seglen < 1)
		return -EINVAL;
	setup_channel_list(dev, s, cmd->chanlist, seglen);
	udelay(1);

	devpriv->ai_act_scan = 0;
	s->async->cur_chan = 0;
	devpriv->irq_blocked = 1;
	devpriv->ai_poll_ptr = 0;
	devpriv->irq_was_now_closed = 0;

	if (cmd->stop_src == TRIG_COUNT)
		devpriv->ai_neverending = 0;
	else
		devpriv->ai_neverending = 1;

	if (devpriv->dma) {
		bytes = devpriv->hwdmasize;
		if (!devpriv->ai_neverending) {
			/*  how many */
			bytes = s->async->cmd.chanlist_len *
			s->async->cmd.chanlist_len *
			sizeof(short);

			/*  how many DMA pages we must fill */
			devpriv->dma_runs_to_end = bytes / devpriv->hwdmasize;

			/* on last dma transfer must be moved */
			devpriv->last_dma_run = bytes % devpriv->hwdmasize;
			devpriv->dma_runs_to_end--;
			if (devpriv->dma_runs_to_end >= 0)
				bytes = devpriv->hwdmasize;
		} else
			devpriv->dma_runs_to_end = -1;

		devpriv->next_dma_buf = 0;
		set_dma_mode(devpriv->dma, DMA_MODE_READ);
		dma_flags = claim_dma_lock();
		clear_dma_ff(devpriv->dma);
		set_dma_addr(devpriv->dma, devpriv->hwdmaptr[0]);
		set_dma_count(devpriv->dma, bytes);
		release_dma_lock(dma_flags);
		enable_dma(devpriv->dma);
	}

	pcl816_start_pacer(dev, true, divisor1, divisor2);
	dmairq = ((devpriv->dma & 0x3) << 4) | (dev->irq & 0x7);

	switch (cmd->convert_src) {
	case TRIG_TIMER:
		devpriv->int816_mode = INT_TYPE_AI1_DMA;

		/*  Pacer+IRQ+DMA */
		outb(0x32, dev->iobase + PCL816_CONTROL);

		/*  write irq and DMA to card */
		outb(dmairq, dev->iobase + PCL816_STATUS);
		break;

	default:
		devpriv->int816_mode = INT_TYPE_AI3_DMA;

		/*  Ext trig+IRQ+DMA */
		outb(0x34, dev->iobase + PCL816_CONTROL);

		/*  write irq to card */
		outb(dmairq, dev->iobase + PCL816_STATUS);
		break;
	}

	return 0;
}

static int pcl816_ai_poll(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pcl816_private *devpriv = dev->private;
	unsigned long flags;
	unsigned int top1, top2, i;

	if (!devpriv->dma)
		return 0;	/*  poll is valid only for DMA transfer */

	spin_lock_irqsave(&dev->spinlock, flags);

	for (i = 0; i < 20; i++) {
		top1 = get_dma_residue(devpriv->dma);	/*  where is now DMA */
		top2 = get_dma_residue(devpriv->dma);
		if (top1 == top2)
			break;
	}
	if (top1 != top2) {
		spin_unlock_irqrestore(&dev->spinlock, flags);
		return 0;
	}

	/*  where is now DMA in buffer */
	top1 = devpriv->hwdmasize - top1;
	top1 >>= 1;		/*  sample position */
	top2 = top1 - devpriv->ai_poll_ptr;
	if (top2 < 1) {		/*  no new samples */
		spin_unlock_irqrestore(&dev->spinlock, flags);
		return 0;
	}

	transfer_from_dma_buf(dev, s,
			      (unsigned short *)devpriv->dmabuf[devpriv->
								next_dma_buf],
			      devpriv->ai_poll_ptr, top2);

	devpriv->ai_poll_ptr = top1;	/*  new buffer position */
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return s->async->buf_write_count - s->async->buf_read_count;
}

/*
==============================================================================
 cancel any mode 1-4 AI
*/
static int pcl816_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct pcl816_private *devpriv = dev->private;

	if (devpriv->irq_blocked > 0) {
		switch (devpriv->int816_mode) {
		case INT_TYPE_AI1_DMA:
		case INT_TYPE_AI3_DMA:
			disable_dma(devpriv->dma);
		case INT_TYPE_AI1_INT:
		case INT_TYPE_AI3_INT:
			outb(inb(dev->iobase + PCL816_CONTROL) & 0x73,
			     dev->iobase + PCL816_CONTROL);	/* Stop A/D */
			udelay(1);
			outb(0, dev->iobase + PCL816_CONTROL);	/* Stop A/D */

			/* Stop pacer */
			i8254_set_mode(dev->iobase + PCL816_TIMER_BASE, 0,
				       2, I8254_MODE0 | I8254_BINARY);
			i8254_set_mode(dev->iobase + PCL816_TIMER_BASE, 0,
				       1, I8254_MODE0 | I8254_BINARY);

			outb(0, dev->iobase + PCL816_AD_LO);
			pcl816_ai_get_sample(dev, s);

			/* clear INT request */
			outb(0, dev->iobase + PCL816_CLRINT);

			/* Stop A/D */
			outb(0, dev->iobase + PCL816_CONTROL);
			devpriv->irq_blocked = 0;
			devpriv->irq_was_now_closed = devpriv->int816_mode;
			devpriv->int816_mode = 0;
/* s->busy = 0; */
			break;
		}
	}
	return 0;
}

/*
==============================================================================
 chech for PCL816
*/
static int pcl816_check(unsigned long iobase)
{
	outb(0x00, iobase + PCL816_MUX);
	udelay(1);
	if (inb(iobase + PCL816_MUX) != 0x00)
		return 1;	/* there isn't card */
	outb(0x55, iobase + PCL816_MUX);
	udelay(1);
	if (inb(iobase + PCL816_MUX) != 0x55)
		return 1;	/* there isn't card */
	outb(0x00, iobase + PCL816_MUX);
	udelay(1);
	outb(0x18, iobase + PCL816_CONTROL);
	udelay(1);
	if (inb(iobase + PCL816_CONTROL) != 0x18)
		return 1;	/* there isn't card */
	return 0;		/*  ok, card exist */
}

/*
==============================================================================
 reset whole PCL-816 cards
*/
static void pcl816_reset(struct comedi_device *dev)
{
	unsigned long timer_base = dev->iobase + PCL816_TIMER_BASE;

/* outb (0, dev->iobase + PCL818_DA_LO);         DAC=0V */
/* outb (0, dev->iobase + PCL818_DA_HI); */
/* udelay (1); */
/* outb (0, dev->iobase + PCL818_DO_HI);        DO=$0000 */
/* outb (0, dev->iobase + PCL818_DO_LO); */
/* udelay (1); */
	outb(0, dev->iobase + PCL816_CONTROL);
	outb(0, dev->iobase + PCL816_MUX);
	outb(0, dev->iobase + PCL816_CLRINT);

	/* Stop pacer */
	i8254_set_mode(timer_base, 0, 2, I8254_MODE0 | I8254_BINARY);
	i8254_set_mode(timer_base, 0, 1, I8254_MODE0 | I8254_BINARY);
	i8254_set_mode(timer_base, 0, 0, I8254_MODE0 | I8254_BINARY);

	outb(0, dev->iobase + PCL816_RANGE);
}

/*
==============================================================================
 Check if channel list from user is built correctly
 If it's ok, then return non-zero length of repeated segment of channel list
*/
static int
check_channel_list(struct comedi_device *dev,
		   struct comedi_subdevice *s, unsigned int *chanlist,
		   unsigned int chanlen)
{
	unsigned int chansegment[16];
	unsigned int i, nowmustbechan, seglen, segpos;

	/*  correct channel and range number check itself comedi/range.c */
	if (chanlen < 1) {
		comedi_error(dev, "range/channel list is empty!");
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

/*
==============================================================================
 Program scan/gain logic with channel list.
*/
static void
setup_channel_list(struct comedi_device *dev,
		   struct comedi_subdevice *s, unsigned int *chanlist,
		   unsigned int seglen)
{
	struct pcl816_private *devpriv = dev->private;
	unsigned int i;

	devpriv->ai_act_chanlist_len = seglen;
	devpriv->ai_act_chanlist_pos = 0;

	for (i = 0; i < seglen; i++) {	/*  store range list to card */
		devpriv->ai_act_chanlist[i] = CR_CHAN(chanlist[i]);
		outb(CR_CHAN(chanlist[0]) & 0xf, dev->iobase + PCL816_MUX);
		/* select gain */
		outb(CR_RANGE(chanlist[0]), dev->iobase + PCL816_RANGE);
	}

	udelay(1);
	/* select channel interval to scan */
	outb(devpriv->ai_act_chanlist[0] |
	     (devpriv->ai_act_chanlist[seglen - 1] << 4),
	     dev->iobase + PCL816_MUX);
}

static int pcl816_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcl816_board *board = comedi_board(dev);
	struct pcl816_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;

	ret = comedi_request_region(dev, it->options[0], 0x10);
	if (ret)
		return ret;

	if (pcl816_check(dev->iobase)) {
		dev_err(dev->class_dev, "I can't detect board. FAIL!\n");
		return -EIO;
	}

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	if ((1 << it->options[1]) & board->IRQbits) {
		ret = request_irq(it->options[1], interrupt_pcl816, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	devpriv->irq_blocked = 0;	/* number of subdevice which use IRQ */
	devpriv->int816_mode = 0;	/* mode of irq */

	/* we need an IRQ to do DMA on channel 3 or 1 */
	if (dev->irq && (it->options[2] == 3 || it->options[2] == 1)) {
		ret = request_dma(it->options[2], dev->board_name);
		if (ret) {
			dev_err(dev->class_dev,
				"unable to request DMA channel %d\n",
				it->options[2]);
			return -EBUSY;
		}
		devpriv->dma = it->options[2];

		devpriv->dmapages = 2;	/* we need 16KB */
		devpriv->hwdmasize = (1 << devpriv->dmapages) * PAGE_SIZE;

		for (i = 0; i < 2; i++) {
			unsigned long dmabuf;

			dmabuf = __get_dma_pages(GFP_KERNEL, devpriv->dmapages);
			if (!dmabuf)
				return -ENOMEM;

			devpriv->dmabuf[i] = dmabuf;
			devpriv->hwdmaptr[i] = virt_to_bus((void *)dmabuf);
		}
	}

	ret = comedi_alloc_subdevices(dev, 1);
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

#if 0
	subdevs[1] = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND;
	s->n_chan = 1;
	s->maxdata = board->ao_maxdata;
	s->range_table = &range_pcl816;
	break;

	subdevs[2] = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	break;

	subdevs[3] = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	break;
#endif

	pcl816_reset(dev);

	return 0;
}

static void pcl816_detach(struct comedi_device *dev)
{
	struct pcl816_private *devpriv = dev->private;

	if (dev->private) {
		pcl816_ai_cancel(dev, dev->read_subdev);
		pcl816_reset(dev);
		if (devpriv->dma)
			free_dma(devpriv->dma);
		if (devpriv->dmabuf[0])
			free_pages(devpriv->dmabuf[0], devpriv->dmapages);
		if (devpriv->dmabuf[1])
			free_pages(devpriv->dmabuf[1], devpriv->dmapages);
	}
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

// SPDX-License-Identifier: GPL-2.0
/*
 * comedi/drivers/pcl818.c
 *
 * Driver: pcl818
 * Description: Advantech PCL-818 cards, PCL-718
 * Author: Michal Dobes <dobes@tesnet.cz>
 * Devices: [Advantech] PCL-818L (pcl818l), PCL-818H (pcl818h),
 *   PCL-818HD (pcl818hd), PCL-818HG (pcl818hg), PCL-818 (pcl818),
 *   PCL-718 (pcl718)
 * Status: works
 *
 * All cards have 16 SE/8 DIFF ADCs, one or two DACs, 16 DI and 16 DO.
 * Differences are only at maximal sample speed, range list and FIFO
 * support.
 * The driver support AI mode 0, 1, 3 other subdevices (AO, DI, DO) support
 * only mode 0. If DMA/FIFO/INT are disabled then AI support only mode 0.
 * PCL-818HD and PCL-818HG support 1kword FIFO. Driver support this FIFO
 * but this code is untested.
 * A word or two about DMA. Driver support DMA operations at two ways:
 * 1) DMA uses two buffers and after one is filled then is generated
 *    INT and DMA restart with second buffer. With this mode I'm unable run
 *    more that 80Ksamples/secs without data dropouts on K6/233.
 * 2) DMA uses one buffer and run in autoinit mode and the data are
 *    from DMA buffer moved on the fly with 2kHz interrupts from RTC.
 *    This mode is used if the interrupt 8 is available for allocation.
 *    If not, then first DMA mode is used. With this I can run at
 *    full speed one card (100ksamples/secs) or two cards with
 *    60ksamples/secs each (more is problem on account of ISA limitations).
 *    To use this mode you must have compiled  kernel with disabled
 *    "Enhanced Real Time Clock Support".
 *    Maybe you can have problems if you use xntpd or similar.
 *    If you've data dropouts with DMA mode 2 then:
 *     a) disable IDE DMA
 *     b) switch text mode console to fb.
 *
 *  Options for PCL-818L:
 *  [0] - IO Base
 *  [1] - IRQ        (0=disable, 2, 3, 4, 5, 6, 7)
 *  [2] - DMA        (0=disable, 1, 3)
 *  [3] - 0, 10=10MHz clock for 8254
 *            1= 1MHz clock for 8254
 *  [4] - 0,  5=A/D input  -5V.. +5V
 *        1, 10=A/D input -10V..+10V
 *  [5] - 0,  5=D/A output 0-5V  (internal reference -5V)
 *        1, 10=D/A output 0-10V (internal reference -10V)
 *        2    =D/A output unknown (external reference)
 *
 *  Options for PCL-818, PCL-818H:
 *  [0] - IO Base
 *  [1] - IRQ        (0=disable, 2, 3, 4, 5, 6, 7)
 *  [2] - DMA        (0=disable, 1, 3)
 *  [3] - 0, 10=10MHz clock for 8254
 *            1= 1MHz clock for 8254
 *  [4] - 0,  5=D/A output 0-5V  (internal reference -5V)
 *        1, 10=D/A output 0-10V (internal reference -10V)
 *        2    =D/A output unknown (external reference)
 *
 *  Options for PCL-818HD, PCL-818HG:
 *  [0] - IO Base
 *  [1] - IRQ        (0=disable, 2, 3, 4, 5, 6, 7)
 *  [2] - DMA/FIFO  (-1=use FIFO, 0=disable both FIFO and DMA,
 *                    1=use DMA ch 1, 3=use DMA ch 3)
 *  [3] - 0, 10=10MHz clock for 8254
 *            1= 1MHz clock for 8254
 *  [4] - 0,  5=D/A output 0-5V  (internal reference -5V)
 *        1, 10=D/A output 0-10V (internal reference -10V)
 *        2    =D/A output unknown (external reference)
 *
 *  Options for PCL-718:
 *  [0] - IO Base
 *  [1] - IRQ        (0=disable, 2, 3, 4, 5, 6, 7)
 *  [2] - DMA        (0=disable, 1, 3)
 *  [3] - 0, 10=10MHz clock for 8254
 *            1= 1MHz clock for 8254
 *  [4] -     0=A/D Range is +/-10V
 *            1=             +/-5V
 *            2=             +/-2.5V
 *            3=             +/-1V
 *            4=             +/-0.5V
 *            5=             user defined bipolar
 *            6=             0-10V
 *            7=             0-5V
 *            8=             0-2V
 *            9=             0-1V
 *           10=             user defined unipolar
 *  [5] - 0,  5=D/A outputs 0-5V  (internal reference -5V)
 *        1, 10=D/A outputs 0-10V (internal reference -10V)
 *            2=D/A outputs unknown (external reference)
 *  [6] - 0, 60=max  60kHz A/D sampling
 *        1,100=max 100kHz A/D sampling (PCL-718 with Option 001 installed)
 *
 */

#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/comedi/comedidev.h>
#include <linux/comedi/comedi_8254.h>
#include <linux/comedi/comedi_isadma.h>

/*
 * Register I/O map
 */
#define PCL818_AI_LSB_REG			0x00
#define PCL818_AI_MSB_REG			0x01
#define PCL818_RANGE_REG			0x01
#define PCL818_MUX_REG				0x02
#define PCL818_MUX_SCAN(_first, _last)		(((_last) << 4) | (_first))
#define PCL818_DO_DI_LSB_REG			0x03
#define PCL818_AO_LSB_REG(x)			(0x04 + ((x) * 2))
#define PCL818_AO_MSB_REG(x)			(0x05 + ((x) * 2))
#define PCL818_STATUS_REG			0x08
#define PCL818_STATUS_NEXT_CHAN_MASK		(0xf << 0)
#define PCL818_STATUS_INT			BIT(4)
#define PCL818_STATUS_MUX			BIT(5)
#define PCL818_STATUS_UNI			BIT(6)
#define PCL818_STATUS_EOC			BIT(7)
#define PCL818_CTRL_REG				0x09
#define PCL818_CTRL_TRIG(x)			(((x) & 0x3) << 0)
#define PCL818_CTRL_DISABLE_TRIG		PCL818_CTRL_TRIG(0)
#define PCL818_CTRL_SOFT_TRIG			PCL818_CTRL_TRIG(1)
#define PCL818_CTRL_EXT_TRIG			PCL818_CTRL_TRIG(2)
#define PCL818_CTRL_PACER_TRIG			PCL818_CTRL_TRIG(3)
#define PCL818_CTRL_DMAE			BIT(2)
#define PCL818_CTRL_IRQ(x)			((x) << 4)
#define PCL818_CTRL_INTE			BIT(7)
#define PCL818_CNTENABLE_REG			0x0a
#define PCL818_CNTENABLE_PACER_TRIG0		BIT(0)
#define PCL818_CNTENABLE_CNT0_INT_CLK		BIT(1)	/* 0=ext clk */
#define PCL818_DO_DI_MSB_REG			0x0b
#define PCL818_TIMER_BASE			0x0c

/* W: fifo enable/disable */
#define PCL818_FI_ENABLE 6
/* W: fifo interrupt clear */
#define PCL818_FI_INTCLR 20
/* W: fifo interrupt clear */
#define PCL818_FI_FLUSH 25
/* R: fifo status */
#define PCL818_FI_STATUS 25
/* R: one record from FIFO */
#define PCL818_FI_DATALO 23
#define PCL818_FI_DATAHI 24

#define MAGIC_DMA_WORD 0x5a5a

static const struct comedi_lrange range_pcl818h_ai = {
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

static const struct comedi_lrange range_pcl818hg_ai = {
	10, {
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

static const struct comedi_lrange range_pcl818l_l_ai = {
	4, {
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625)
	}
};

static const struct comedi_lrange range_pcl818l_h_ai = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange range718_bipolar1 = {
	1, {
		BIP_RANGE(1)
	}
};

static const struct comedi_lrange range718_bipolar0_5 = {
	1, {
		BIP_RANGE(0.5)
	}
};

static const struct comedi_lrange range718_unipolar2 = {
	1, {
		UNI_RANGE(2)
	}
};

static const struct comedi_lrange range718_unipolar1 = {
	1, {
		BIP_RANGE(1)
	}
};

struct pcl818_board {
	const char *name;
	unsigned int ns_min;
	int n_aochan;
	const struct comedi_lrange *ai_range_type;
	unsigned int has_dma:1;
	unsigned int has_fifo:1;
	unsigned int is_818:1;
};

static const struct pcl818_board boardtypes[] = {
	{
		.name		= "pcl818l",
		.ns_min		= 25000,
		.n_aochan	= 1,
		.ai_range_type	= &range_pcl818l_l_ai,
		.has_dma	= 1,
		.is_818		= 1,
	}, {
		.name		= "pcl818h",
		.ns_min		= 10000,
		.n_aochan	= 1,
		.ai_range_type	= &range_pcl818h_ai,
		.has_dma	= 1,
		.is_818		= 1,
	}, {
		.name		= "pcl818hd",
		.ns_min		= 10000,
		.n_aochan	= 1,
		.ai_range_type	= &range_pcl818h_ai,
		.has_dma	= 1,
		.has_fifo	= 1,
		.is_818		= 1,
	}, {
		.name		= "pcl818hg",
		.ns_min		= 10000,
		.n_aochan	= 1,
		.ai_range_type	= &range_pcl818hg_ai,
		.has_dma	= 1,
		.has_fifo	= 1,
		.is_818		= 1,
	}, {
		.name		= "pcl818",
		.ns_min		= 10000,
		.n_aochan	= 2,
		.ai_range_type	= &range_pcl818h_ai,
		.has_dma	= 1,
		.is_818		= 1,
	}, {
		.name		= "pcl718",
		.ns_min		= 16000,
		.n_aochan	= 2,
		.ai_range_type	= &range_unipolar5,
		.has_dma	= 1,
	}, {
		.name		= "pcm3718",
		.ns_min		= 10000,
		.ai_range_type	= &range_pcl818h_ai,
		.has_dma	= 1,
		.is_818		= 1,
	},
};

struct pcl818_private {
	struct comedi_isadma *dma;
	/*  manimal allowed delay between samples (in us) for actual card */
	unsigned int ns_min;
	/*  MUX setting for actual AI operations */
	unsigned int act_chanlist[16];
	unsigned int act_chanlist_len;	/*  how long is actual MUX list */
	unsigned int act_chanlist_pos;	/*  actual position in MUX list */
	unsigned int usefifo:1;
	unsigned int ai_cmd_running:1;
	unsigned int ai_cmd_canceled:1;
};

static void pcl818_ai_setup_dma(struct comedi_device *dev,
				struct comedi_subdevice *s,
				unsigned int unread_samples)
{
	struct pcl818_private *devpriv = dev->private;
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

static void pcl818_ai_set_chan_range(struct comedi_device *dev,
				     unsigned int chan,
				     unsigned int range)
{
	outb(chan, dev->iobase + PCL818_MUX_REG);
	outb(range, dev->iobase + PCL818_RANGE_REG);
}

static void pcl818_ai_set_chan_scan(struct comedi_device *dev,
				    unsigned int first_chan,
				    unsigned int last_chan)
{
	outb(PCL818_MUX_SCAN(first_chan, last_chan),
	     dev->iobase + PCL818_MUX_REG);
}

static void pcl818_ai_setup_chanlist(struct comedi_device *dev,
				     unsigned int *chanlist,
				     unsigned int seglen)
{
	struct pcl818_private *devpriv = dev->private;
	unsigned int first_chan = CR_CHAN(chanlist[0]);
	unsigned int last_chan;
	unsigned int range;
	int i;

	devpriv->act_chanlist_len = seglen;
	devpriv->act_chanlist_pos = 0;

	/* store range list to card */
	for (i = 0; i < seglen; i++) {
		last_chan = CR_CHAN(chanlist[i]);
		range = CR_RANGE(chanlist[i]);

		devpriv->act_chanlist[i] = last_chan;

		pcl818_ai_set_chan_range(dev, last_chan, range);
	}

	udelay(1);

	pcl818_ai_set_chan_scan(dev, first_chan, last_chan);
}

static void pcl818_ai_clear_eoc(struct comedi_device *dev)
{
	/* writing any value clears the interrupt request */
	outb(0, dev->iobase + PCL818_STATUS_REG);
}

static void pcl818_ai_soft_trig(struct comedi_device *dev)
{
	/* writing any value triggers a software conversion */
	outb(0, dev->iobase + PCL818_AI_LSB_REG);
}

static unsigned int pcl818_ai_get_fifo_sample(struct comedi_device *dev,
					      struct comedi_subdevice *s,
					      unsigned int *chan)
{
	unsigned int val;

	val = inb(dev->iobase + PCL818_FI_DATALO);
	val |= (inb(dev->iobase + PCL818_FI_DATAHI) << 8);

	if (chan)
		*chan = val & 0xf;

	return (val >> 4) & s->maxdata;
}

static unsigned int pcl818_ai_get_sample(struct comedi_device *dev,
					 struct comedi_subdevice *s,
					 unsigned int *chan)
{
	unsigned int val;

	val = inb(dev->iobase + PCL818_AI_MSB_REG) << 8;
	val |= inb(dev->iobase + PCL818_AI_LSB_REG);

	if (chan)
		*chan = val & 0xf;

	return (val >> 4) & s->maxdata;
}

static int pcl818_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + PCL818_STATUS_REG);
	if (status & PCL818_STATUS_INT)
		return 0;
	return -EBUSY;
}

static bool pcl818_ai_write_sample(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned int chan, unsigned short val)
{
	struct pcl818_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int expected_chan;

	expected_chan = devpriv->act_chanlist[devpriv->act_chanlist_pos];
	if (chan != expected_chan) {
		dev_dbg(dev->class_dev,
			"A/D mode1/3 %s - channel dropout %d!=%d !\n",
			(devpriv->dma) ? "DMA" :
			(devpriv->usefifo) ? "FIFO" : "IRQ",
			chan, expected_chan);
		s->async->events |= COMEDI_CB_ERROR;
		return false;
	}

	comedi_buf_write_samples(s, &val, 1);

	devpriv->act_chanlist_pos++;
	if (devpriv->act_chanlist_pos >= devpriv->act_chanlist_len)
		devpriv->act_chanlist_pos = 0;

	if (cmd->stop_src == TRIG_COUNT &&
	    s->async->scans_done >= cmd->stop_arg) {
		s->async->events |= COMEDI_CB_EOA;
		return false;
	}

	return true;
}

static void pcl818_handle_eoc(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	unsigned int chan;
	unsigned int val;

	if (pcl818_ai_eoc(dev, s, NULL, 0)) {
		dev_err(dev->class_dev, "A/D mode1/3 IRQ without DRDY!\n");
		s->async->events |= COMEDI_CB_ERROR;
		return;
	}

	val = pcl818_ai_get_sample(dev, s, &chan);
	pcl818_ai_write_sample(dev, s, chan, val);
}

static void pcl818_handle_dma(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	struct pcl818_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_isadma_desc *desc = &dma->desc[dma->cur_dma];
	unsigned short *ptr = desc->virt_addr;
	unsigned int nsamples = comedi_bytes_to_samples(s, desc->size);
	unsigned int chan;
	unsigned int val;
	int i;

	/* restart dma with the next buffer */
	dma->cur_dma = 1 - dma->cur_dma;
	pcl818_ai_setup_dma(dev, s, nsamples);

	for (i = 0; i < nsamples; i++) {
		val = ptr[i];
		chan = val & 0xf;
		val = (val >> 4) & s->maxdata;
		if (!pcl818_ai_write_sample(dev, s, chan, val))
			break;
	}
}

static void pcl818_handle_fifo(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	unsigned int status;
	unsigned int chan;
	unsigned int val;
	int i, len;

	status = inb(dev->iobase + PCL818_FI_STATUS);

	if (status & 4) {
		dev_err(dev->class_dev, "A/D mode1/3 FIFO overflow!\n");
		s->async->events |= COMEDI_CB_ERROR;
		return;
	}

	if (status & 1) {
		dev_err(dev->class_dev,
			"A/D mode1/3 FIFO interrupt without data!\n");
		s->async->events |= COMEDI_CB_ERROR;
		return;
	}

	if (status & 2)
		len = 512;
	else
		len = 0;

	for (i = 0; i < len; i++) {
		val = pcl818_ai_get_fifo_sample(dev, s, &chan);
		if (!pcl818_ai_write_sample(dev, s, chan, val))
			break;
	}
}

static irqreturn_t pcl818_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pcl818_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;

	if (!dev->attached || !devpriv->ai_cmd_running) {
		pcl818_ai_clear_eoc(dev);
		return IRQ_HANDLED;
	}

	if (devpriv->ai_cmd_canceled) {
		/*
		 * The cleanup from ai_cancel() has been delayed
		 * until now because the card doesn't seem to like
		 * being reprogrammed while a DMA transfer is in
		 * progress.
		 */
		s->async->scans_done = cmd->stop_arg;
		s->cancel(dev, s);
		return IRQ_HANDLED;
	}

	if (devpriv->dma)
		pcl818_handle_dma(dev, s);
	else if (devpriv->usefifo)
		pcl818_handle_fifo(dev, s);
	else
		pcl818_handle_eoc(dev, s);

	pcl818_ai_clear_eoc(dev);

	comedi_handle_events(dev, s);
	return IRQ_HANDLED;
}

static int check_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      unsigned int *chanlist, unsigned int n_chan)
{
	unsigned int chansegment[16];
	unsigned int i, nowmustbechan, seglen;

	/* correct channel and range number check itself comedi/range.c */
	if (n_chan < 1) {
		dev_err(dev->class_dev, "range/channel list is empty!\n");
		return 0;
	}

	if (n_chan > 1) {
		/*  first channel is every time ok */
		chansegment[0] = chanlist[0];
		/*  build part of chanlist */
		for (i = 1, seglen = 1; i < n_chan; i++, seglen++) {
			/* we detect loop, this must by finish */

			if (chanlist[0] == chanlist[i])
				break;
			nowmustbechan =
			    (CR_CHAN(chansegment[i - 1]) + 1) % s->n_chan;
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
		for (i = 0; i < n_chan; i++) {
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
	return seglen;
}

static int check_single_ended(unsigned int port)
{
	if (inb(port + PCL818_STATUS_REG) & PCL818_STATUS_MUX)
		return 1;
	return 0;
}

static int ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
		      struct comedi_cmd *cmd)
{
	const struct pcl818_board *board = dev->board_ptr;
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
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

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    board->ns_min);
	} else {	/* TRIG_EXT */
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	}

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

static int pcl818_ai_cmd(struct comedi_device *dev,
			 struct comedi_subdevice *s)
{
	struct pcl818_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int ctrl = 0;
	unsigned int seglen;

	if (devpriv->ai_cmd_running)
		return -EBUSY;

	seglen = check_channel_list(dev, s, cmd->chanlist, cmd->chanlist_len);
	if (seglen < 1)
		return -EINVAL;
	pcl818_ai_setup_chanlist(dev, cmd->chanlist, seglen);

	devpriv->ai_cmd_running = 1;
	devpriv->ai_cmd_canceled = 0;
	devpriv->act_chanlist_pos = 0;

	if (cmd->convert_src == TRIG_TIMER)
		ctrl |= PCL818_CTRL_PACER_TRIG;
	else
		ctrl |= PCL818_CTRL_EXT_TRIG;

	outb(0, dev->iobase + PCL818_CNTENABLE_REG);

	if (dma) {
		/* setup and enable dma for the first buffer */
		dma->cur_dma = 0;
		pcl818_ai_setup_dma(dev, s, 0);

		ctrl |= PCL818_CTRL_INTE | PCL818_CTRL_IRQ(dev->irq) |
			PCL818_CTRL_DMAE;
	} else if (devpriv->usefifo) {
		/* enable FIFO */
		outb(1, dev->iobase + PCL818_FI_ENABLE);
	} else {
		ctrl |= PCL818_CTRL_INTE | PCL818_CTRL_IRQ(dev->irq);
	}
	outb(ctrl, dev->iobase + PCL818_CTRL_REG);

	if (cmd->convert_src == TRIG_TIMER) {
		comedi_8254_update_divisors(dev->pacer);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
	}

	return 0;
}

static int pcl818_ai_cancel(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct pcl818_private *devpriv = dev->private;
	struct comedi_isadma *dma = devpriv->dma;
	struct comedi_cmd *cmd = &s->async->cmd;

	if (!devpriv->ai_cmd_running)
		return 0;

	if (dma) {
		if (cmd->stop_src == TRIG_NONE ||
		    (cmd->stop_src == TRIG_COUNT &&
		     s->async->scans_done < cmd->stop_arg)) {
			if (!devpriv->ai_cmd_canceled) {
				/*
				 * Wait for running dma transfer to end,
				 * do cleanup in interrupt.
				 */
				devpriv->ai_cmd_canceled = 1;
				return 0;
			}
		}
		comedi_isadma_disable(dma->chan);
	}

	outb(PCL818_CTRL_DISABLE_TRIG, dev->iobase + PCL818_CTRL_REG);
	comedi_8254_pacer_enable(dev->pacer, 1, 2, false);
	pcl818_ai_clear_eoc(dev);

	if (devpriv->usefifo) {	/*  FIFO shutdown */
		outb(0, dev->iobase + PCL818_FI_INTCLR);
		outb(0, dev->iobase + PCL818_FI_FLUSH);
		outb(0, dev->iobase + PCL818_FI_ENABLE);
	}
	devpriv->ai_cmd_running = 0;
	devpriv->ai_cmd_canceled = 0;

	return 0;
}

static int pcl818_ai_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	int ret = 0;
	int i;

	outb(PCL818_CTRL_SOFT_TRIG, dev->iobase + PCL818_CTRL_REG);

	pcl818_ai_set_chan_range(dev, chan, range);
	pcl818_ai_set_chan_scan(dev, chan, chan);

	for (i = 0; i < insn->n; i++) {
		pcl818_ai_clear_eoc(dev);
		pcl818_ai_soft_trig(dev);

		ret = comedi_timeout(dev, s, insn, pcl818_ai_eoc, 0);
		if (ret)
			break;

		data[i] = pcl818_ai_get_sample(dev, s, NULL);
	}
	pcl818_ai_clear_eoc(dev);

	return ret ? ret : insn->n;
}

static int pcl818_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outb((val & 0x000f) << 4,
		     dev->iobase + PCL818_AO_LSB_REG(chan));
		outb((val & 0x0ff0) >> 4,
		     dev->iobase + PCL818_AO_MSB_REG(chan));
	}
	s->readback[chan] = val;

	return insn->n;
}

static int pcl818_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[1] = inb(dev->iobase + PCL818_DO_DI_LSB_REG) |
		  (inb(dev->iobase + PCL818_DO_DI_MSB_REG) << 8);

	return insn->n;
}

static int pcl818_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	if (comedi_dio_update_state(s, data)) {
		outb(s->state & 0xff, dev->iobase + PCL818_DO_DI_LSB_REG);
		outb((s->state >> 8), dev->iobase + PCL818_DO_DI_MSB_REG);
	}

	data[1] = s->state;

	return insn->n;
}

static void pcl818_reset(struct comedi_device *dev)
{
	const struct pcl818_board *board = dev->board_ptr;
	unsigned int chan;

	/* flush and disable the FIFO */
	if (board->has_fifo) {
		outb(0, dev->iobase + PCL818_FI_INTCLR);
		outb(0, dev->iobase + PCL818_FI_FLUSH);
		outb(0, dev->iobase + PCL818_FI_ENABLE);
	}

	/* disable analog input trigger */
	outb(PCL818_CTRL_DISABLE_TRIG, dev->iobase + PCL818_CTRL_REG);
	pcl818_ai_clear_eoc(dev);

	pcl818_ai_set_chan_range(dev, 0, 0);

	/* stop pacer */
	outb(0, dev->iobase + PCL818_CNTENABLE_REG);

	/* set analog output channels to 0V */
	for (chan = 0; chan < board->n_aochan; chan++) {
		outb(0, dev->iobase + PCL818_AO_LSB_REG(chan));
		outb(0, dev->iobase + PCL818_AO_MSB_REG(chan));
	}

	/* set all digital outputs low */
	outb(0, dev->iobase + PCL818_DO_DI_MSB_REG);
	outb(0, dev->iobase + PCL818_DO_DI_LSB_REG);
}

static void pcl818_set_ai_range_table(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_devconfig *it)
{
	const struct pcl818_board *board = dev->board_ptr;

	/* default to the range table from the boardinfo */
	s->range_table = board->ai_range_type;

	/* now check the user config option based on the boardtype */
	if (board->is_818) {
		if (it->options[4] == 1 || it->options[4] == 10) {
			/* secondary range list jumper selectable */
			s->range_table = &range_pcl818l_h_ai;
		}
	} else {
		switch (it->options[4]) {
		case 0:
			s->range_table = &range_bipolar10;
			break;
		case 1:
			s->range_table = &range_bipolar5;
			break;
		case 2:
			s->range_table = &range_bipolar2_5;
			break;
		case 3:
			s->range_table = &range718_bipolar1;
			break;
		case 4:
			s->range_table = &range718_bipolar0_5;
			break;
		case 6:
			s->range_table = &range_unipolar10;
			break;
		case 7:
			s->range_table = &range_unipolar5;
			break;
		case 8:
			s->range_table = &range718_unipolar2;
			break;
		case 9:
			s->range_table = &range718_unipolar1;
			break;
		default:
			s->range_table = &range_unknown;
			break;
		}
	}
}

static void pcl818_alloc_dma(struct comedi_device *dev, unsigned int dma_chan)
{
	struct pcl818_private *devpriv = dev->private;

	/* only DMA channels 3 and 1 are valid */
	if (!(dma_chan == 3 || dma_chan == 1))
		return;

	/* DMA uses two 16K buffers */
	devpriv->dma = comedi_isadma_alloc(dev, 2, dma_chan, dma_chan,
					   PAGE_SIZE * 4, COMEDI_ISADMA_READ);
}

static void pcl818_free_dma(struct comedi_device *dev)
{
	struct pcl818_private *devpriv = dev->private;

	if (devpriv)
		comedi_isadma_free(devpriv->dma);
}

static int pcl818_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcl818_board *board = dev->board_ptr;
	struct pcl818_private *devpriv;
	struct comedi_subdevice *s;
	unsigned int osc_base;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_request_region(dev, it->options[0],
				    board->has_fifo ? 0x20 : 0x10);
	if (ret)
		return ret;

	/* we can use IRQ 2-7 for async command support */
	if (it->options[1] >= 2 && it->options[1] <= 7) {
		ret = request_irq(it->options[1], pcl818_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = it->options[1];
	}

	/* should we use the FIFO? */
	if (dev->irq && board->has_fifo && it->options[2] == -1)
		devpriv->usefifo = 1;

	/* we need an IRQ to do DMA on channel 3 or 1 */
	if (dev->irq && board->has_dma)
		pcl818_alloc_dma(dev, it->options[2]);

	/* use 1MHz or 10MHz oscilator */
	if ((it->options[3] == 0) || (it->options[3] == 10))
		osc_base = I8254_OSC_BASE_10MHZ;
	else
		osc_base = I8254_OSC_BASE_1MHZ;

	dev->pacer = comedi_8254_init(dev->iobase + PCL818_TIMER_BASE,
				      osc_base, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	/* max sampling speed */
	devpriv->ns_min = board->ns_min;
	if (!board->is_818) {
		/* extended PCL718 to 100kHz DAC */
		if ((it->options[6] == 1) || (it->options[6] == 100))
			devpriv->ns_min = 10000;
	}

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE;
	if (check_single_ended(dev->iobase)) {
		s->n_chan	= 16;
		s->subdev_flags	|= SDF_COMMON | SDF_GROUND;
	} else {
		s->n_chan	= 8;
		s->subdev_flags	|= SDF_DIFF;
	}
	s->maxdata	= 0x0fff;

	pcl818_set_ai_range_table(dev, s, it);

	s->insn_read	= pcl818_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= ai_cmdtest;
		s->do_cmd	= pcl818_ai_cmd;
		s->cancel	= pcl818_ai_cancel;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->n_aochan) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND;
		s->n_chan	= board->n_aochan;
		s->maxdata	= 0x0fff;
		s->range_table	= &range_unipolar5;
		if (board->is_818) {
			if ((it->options[4] == 1) || (it->options[4] == 10))
				s->range_table = &range_unipolar10;
			if (it->options[4] == 2)
				s->range_table = &range_unknown;
		} else {
			if ((it->options[5] == 1) || (it->options[5] == 10))
				s->range_table = &range_unipolar10;
			if (it->options[5] == 2)
				s->range_table = &range_unknown;
		}
		s->insn_write	= pcl818_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Digital Input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcl818_di_insn_bits;

	/* Digital Output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 16;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= pcl818_do_insn_bits;

	pcl818_reset(dev);

	return 0;
}

static void pcl818_detach(struct comedi_device *dev)
{
	struct pcl818_private *devpriv = dev->private;

	if (devpriv) {
		pcl818_ai_cancel(dev, dev->read_subdev);
		pcl818_reset(dev);
	}
	pcl818_free_dma(dev);
	comedi_legacy_detach(dev);
}

static struct comedi_driver pcl818_driver = {
	.driver_name	= "pcl818",
	.module		= THIS_MODULE,
	.attach		= pcl818_attach,
	.detach		= pcl818_detach,
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct pcl818_board),
};
module_comedi_driver(pcl818_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

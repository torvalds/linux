// SPDX-License-Identifier: GPL-2.0
/*
 * adv_pci1710.c
 * Comedi driver for Advantech PCI-1710 series boards
 * Author: Michal Dobes <dobes@tesnet.cz>
 *
 * Thanks to ZhenGang Shang <ZhenGang.Shang@Advantech.com.cn>
 * for testing and information.
 */

/*
 * Driver: adv_pci1710
 * Description: Comedi driver for Advantech PCI-1710 series boards
 * Devices: [Advantech] PCI-1710 (adv_pci1710), PCI-1710HG, PCI-1711,
 *   PCI-1713, PCI-1731
 * Author: Michal Dobes <dobes@tesnet.cz>
 * Updated: Fri, 29 Oct 2015 17:19:35 -0700
 * Status: works
 *
 * Configuration options: not applicable, uses PCI auto config
 *
 * This driver supports AI, AO, DI and DO subdevices.
 * AI subdevice supports cmd and insn interface,
 * other subdevices support only insn interface.
 *
 * The PCI-1710 and PCI-1710HG have the same PCI device ID, so the
 * driver cannot distinguish between them, as would be normal for a
 * PCI driver.
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#include "comedi_8254.h"
#include "amcc_s5933.h"

/*
 * PCI BAR2 Register map (dev->iobase)
 */
#define PCI171X_AD_DATA_REG	0x00	/* R:   A/D data */
#define PCI171X_SOFTTRG_REG	0x00	/* W:   soft trigger for A/D */
#define PCI171X_RANGE_REG	0x02	/* W:   A/D gain/range register */
#define PCI171X_RANGE_DIFF	BIT(5)
#define PCI171X_RANGE_UNI	BIT(4)
#define PCI171X_RANGE_GAIN(x)	(((x) & 0x7) << 0)
#define PCI171X_MUX_REG		0x04	/* W:   A/D multiplexor control */
#define PCI171X_MUX_CHANH(x)	(((x) & 0xff) << 8)
#define PCI171X_MUX_CHANL(x)	(((x) & 0xff) << 0)
#define PCI171X_MUX_CHAN(x)	(PCI171X_MUX_CHANH(x) | PCI171X_MUX_CHANL(x))
#define PCI171X_STATUS_REG	0x06	/* R:   status register */
#define PCI171X_STATUS_IRQ	BIT(11)	/* 1=IRQ occurred */
#define PCI171X_STATUS_FF	BIT(10)	/* 1=FIFO is full, fatal error */
#define PCI171X_STATUS_FH	BIT(9)	/* 1=FIFO is half full */
#define PCI171X_STATUS_FE	BIT(8)	/* 1=FIFO is empty */
#define PCI171X_CTRL_REG	0x06	/* W:   control register */
#define PCI171X_CTRL_CNT0	BIT(6)	/* 1=ext. clk, 0=int. 100kHz clk */
#define PCI171X_CTRL_ONEFH	BIT(5)	/* 1=on FIFO half full, 0=on sample */
#define PCI171X_CTRL_IRQEN	BIT(4)	/* 1=enable IRQ */
#define PCI171X_CTRL_GATE	BIT(3)	/* 1=enable ext. trigger GATE (8254?) */
#define PCI171X_CTRL_EXT	BIT(2)	/* 1=enable ext. trigger source */
#define PCI171X_CTRL_PACER	BIT(1)	/* 1=enable int. 8254 trigger source */
#define PCI171X_CTRL_SW		BIT(0)	/* 1=enable software trigger source */
#define PCI171X_CLRINT_REG	0x08	/* W:   clear interrupts request */
#define PCI171X_CLRFIFO_REG	0x09	/* W:   clear FIFO */
#define PCI171X_DA_REG(x)	(0x0a + ((x) * 2)) /* W:   D/A register */
#define PCI171X_DAREF_REG	0x0e	/* W:   D/A reference control */
#define PCI171X_DAREF(c, r)	(((r) & 0x3) << ((c) * 2))
#define PCI171X_DAREF_MASK(c)	PCI171X_DAREF((c), 0x3)
#define PCI171X_DI_REG		0x10	/* R:   digital inputs */
#define PCI171X_DO_REG		0x10	/* W:   digital outputs */
#define PCI171X_TIMER_BASE	0x18	/* R/W: 8254 timer */

static const struct comedi_lrange pci1710_ai_range = {
	9, {
		BIP_RANGE(5),		/* gain 1   (0x00) */
		BIP_RANGE(2.5),		/* gain 2   (0x01) */
		BIP_RANGE(1.25),	/* gain 4   (0x02) */
		BIP_RANGE(0.625),	/* gain 8   (0x03) */
		BIP_RANGE(10),		/* gain 0.5 (0x04) */
		UNI_RANGE(10),		/* gain 1   (0x00 | UNI) */
		UNI_RANGE(5),		/* gain 2   (0x01 | UNI) */
		UNI_RANGE(2.5),		/* gain 4   (0x02 | UNI) */
		UNI_RANGE(1.25)		/* gain 8   (0x03 | UNI) */
	}
};

static const struct comedi_lrange pci1710hg_ai_range = {
	12, {
		BIP_RANGE(5),		/* gain 1    (0x00) */
		BIP_RANGE(0.5),		/* gain 10   (0x01) */
		BIP_RANGE(0.05),	/* gain 100  (0x02) */
		BIP_RANGE(0.005),	/* gain 1000 (0x03) */
		BIP_RANGE(10),		/* gain 0.5  (0x04) */
		BIP_RANGE(1),		/* gain 5    (0x05) */
		BIP_RANGE(0.1),		/* gain 50   (0x06) */
		BIP_RANGE(0.01),	/* gain 500  (0x07) */
		UNI_RANGE(10),		/* gain 1    (0x00 | UNI) */
		UNI_RANGE(1),		/* gain 10   (0x01 | UNI) */
		UNI_RANGE(0.1),		/* gain 100  (0x02 | UNI) */
		UNI_RANGE(0.01)		/* gain 1000 (0x03 | UNI) */
	}
};

static const struct comedi_lrange pci1711_ai_range = {
	5, {
		BIP_RANGE(10),		/* gain 1  (0x00) */
		BIP_RANGE(5),		/* gain 2  (0x01) */
		BIP_RANGE(2.5),		/* gain 4  (0x02) */
		BIP_RANGE(1.25),	/* gain 8  (0x03) */
		BIP_RANGE(0.625)	/* gain 16 (0x04) */
	}
};

static const struct comedi_lrange pci171x_ao_range = {
	3, {
		UNI_RANGE(5),		/* internal -5V ref */
		UNI_RANGE(10),		/* internal -10V ref */
		RANGE_ext(0, 1)		/* external -Vref (+/-10V max) */
	}
};

enum pci1710_boardid {
	BOARD_PCI1710,
	BOARD_PCI1710HG,
	BOARD_PCI1711,
	BOARD_PCI1713,
	BOARD_PCI1731,
};

struct boardtype {
	const char *name;
	const struct comedi_lrange *ai_range;
	unsigned int is_pci1711:1;
	unsigned int is_pci1713:1;
	unsigned int has_ao:1;
};

static const struct boardtype boardtypes[] = {
	[BOARD_PCI1710] = {
		.name		= "pci1710",
		.ai_range	= &pci1710_ai_range,
		.has_ao		= 1,
	},
	[BOARD_PCI1710HG] = {
		.name		= "pci1710hg",
		.ai_range	= &pci1710hg_ai_range,
		.has_ao		= 1,
	},
	[BOARD_PCI1711] = {
		.name		= "pci1711",
		.ai_range	= &pci1711_ai_range,
		.is_pci1711	= 1,
		.has_ao		= 1,
	},
	[BOARD_PCI1713] = {
		.name		= "pci1713",
		.ai_range	= &pci1710_ai_range,
		.is_pci1713	= 1,
	},
	[BOARD_PCI1731] = {
		.name		= "pci1731",
		.ai_range	= &pci1711_ai_range,
		.is_pci1711	= 1,
	},
};

struct pci1710_private {
	unsigned int max_samples;
	unsigned int ctrl;	/* control register value */
	unsigned int ctrl_ext;	/* used to switch from TRIG_EXT to TRIG_xxx */
	unsigned int mux_scan;	/* used to set the channel interval to scan */
	unsigned char ai_et;
	unsigned int act_chanlist[32];	/*  list of scanned channel */
	unsigned char saved_seglen;	/* len of the non-repeating chanlist */
	unsigned char da_ranges;	/*  copy of D/A outpit range register */
	unsigned char unipolar_gain;	/* adjust for unipolar gain codes */
};

static int pci1710_ai_check_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_cmd *cmd)
{
	struct pci1710_private *devpriv = dev->private;
	unsigned int chan0 = CR_CHAN(cmd->chanlist[0]);
	unsigned int last_aref = CR_AREF(cmd->chanlist[0]);
	unsigned int next_chan = (chan0 + 1) % s->n_chan;
	unsigned int chansegment[32];
	unsigned int seglen;
	int i;

	if (cmd->chanlist_len == 1) {
		devpriv->saved_seglen = cmd->chanlist_len;
		return 0;
	}

	/* first channel is always ok */
	chansegment[0] = cmd->chanlist[0];

	for (i = 1; i < cmd->chanlist_len; i++) {
		unsigned int chan = CR_CHAN(cmd->chanlist[i]);
		unsigned int aref = CR_AREF(cmd->chanlist[i]);

		if (cmd->chanlist[0] == cmd->chanlist[i])
			break;	/*  we detected a loop, stop */

		if (aref == AREF_DIFF && (chan & 1)) {
			dev_err(dev->class_dev,
				"Odd channel cannot be differential input!\n");
			return -EINVAL;
		}

		if (last_aref == AREF_DIFF)
			next_chan = (next_chan + 1) % s->n_chan;
		if (chan != next_chan) {
			dev_err(dev->class_dev,
				"channel list must be continuous! chanlist[%i]=%d but must be %d or %d!\n",
				i, chan, next_chan, chan0);
			return -EINVAL;
		}

		/* next correct channel in list */
		chansegment[i] = cmd->chanlist[i];
		last_aref = aref;
	}
	seglen = i;

	for (i = 0; i < cmd->chanlist_len; i++) {
		if (cmd->chanlist[i] != chansegment[i % seglen]) {
			dev_err(dev->class_dev,
				"bad channel, reference or range number! chanlist[%i]=%d,%d,%d and not %d,%d,%d!\n",
				i, CR_CHAN(chansegment[i]),
				CR_RANGE(chansegment[i]),
				CR_AREF(chansegment[i]),
				CR_CHAN(cmd->chanlist[i % seglen]),
				CR_RANGE(cmd->chanlist[i % seglen]),
				CR_AREF(chansegment[i % seglen]));
			return -EINVAL;
		}
	}
	devpriv->saved_seglen = seglen;

	return 0;
}

static void pci1710_ai_setup_chanlist(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      unsigned int *chanlist,
				      unsigned int n_chan,
				      unsigned int seglen)
{
	struct pci1710_private *devpriv = dev->private;
	unsigned int first_chan = CR_CHAN(chanlist[0]);
	unsigned int last_chan = CR_CHAN(chanlist[seglen - 1]);
	unsigned int i;

	for (i = 0; i < seglen; i++) {	/*  store range list to card */
		unsigned int chan = CR_CHAN(chanlist[i]);
		unsigned int range = CR_RANGE(chanlist[i]);
		unsigned int aref = CR_AREF(chanlist[i]);
		unsigned int rangeval = 0;

		if (aref == AREF_DIFF)
			rangeval |= PCI171X_RANGE_DIFF;
		if (comedi_range_is_unipolar(s, range)) {
			rangeval |= PCI171X_RANGE_UNI;
			range -= devpriv->unipolar_gain;
		}
		rangeval |= PCI171X_RANGE_GAIN(range);

		/* select channel and set range */
		outw(PCI171X_MUX_CHAN(chan), dev->iobase + PCI171X_MUX_REG);
		outw(rangeval, dev->iobase + PCI171X_RANGE_REG);

		devpriv->act_chanlist[i] = chan;
	}
	for ( ; i < n_chan; i++)	/* store remainder of channel list */
		devpriv->act_chanlist[i] = CR_CHAN(chanlist[i]);

	/* select channel interval to scan */
	devpriv->mux_scan = PCI171X_MUX_CHANL(first_chan) |
			    PCI171X_MUX_CHANH(last_chan);
	outw(devpriv->mux_scan, dev->iobase + PCI171X_MUX_REG);
}

static int pci1710_ai_eoc(struct comedi_device *dev,
			  struct comedi_subdevice *s,
			  struct comedi_insn *insn,
			  unsigned long context)
{
	unsigned int status;

	status = inw(dev->iobase + PCI171X_STATUS_REG);
	if ((status & PCI171X_STATUS_FE) == 0)
		return 0;
	return -EBUSY;
}

static int pci1710_ai_read_sample(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  unsigned int cur_chan,
				  unsigned int *val)
{
	const struct boardtype *board = dev->board_ptr;
	struct pci1710_private *devpriv = dev->private;
	unsigned int sample;
	unsigned int chan;

	sample = inw(dev->iobase + PCI171X_AD_DATA_REG);
	if (!board->is_pci1713) {
		/*
		 * The upper 4 bits of the 16-bit sample are the channel number
		 * that the sample was acquired from. Verify that this channel
		 * number matches the expected channel number.
		 */
		chan = sample >> 12;
		if (chan != devpriv->act_chanlist[cur_chan]) {
			dev_err(dev->class_dev,
				"A/D data dropout: received from channel %d, expected %d\n",
				chan, devpriv->act_chanlist[cur_chan]);
			return -ENODATA;
		}
	}
	*val = sample & s->maxdata;
	return 0;
}

static int pci1710_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct pci1710_private *devpriv = dev->private;
	int ret = 0;
	int i;

	/* enable software trigger */
	devpriv->ctrl |= PCI171X_CTRL_SW;
	outw(devpriv->ctrl, dev->iobase + PCI171X_CTRL_REG);

	outb(0, dev->iobase + PCI171X_CLRFIFO_REG);
	outb(0, dev->iobase + PCI171X_CLRINT_REG);

	pci1710_ai_setup_chanlist(dev, s, &insn->chanspec, 1, 1);

	for (i = 0; i < insn->n; i++) {
		unsigned int val;

		/* start conversion */
		outw(0, dev->iobase + PCI171X_SOFTTRG_REG);

		ret = comedi_timeout(dev, s, insn, pci1710_ai_eoc, 0);
		if (ret)
			break;

		ret = pci1710_ai_read_sample(dev, s, 0, &val);
		if (ret)
			break;

		data[i] = val;
	}

	/* disable software trigger */
	devpriv->ctrl &= ~PCI171X_CTRL_SW;
	outw(devpriv->ctrl, dev->iobase + PCI171X_CTRL_REG);

	outb(0, dev->iobase + PCI171X_CLRFIFO_REG);
	outb(0, dev->iobase + PCI171X_CLRINT_REG);

	return ret ? ret : insn->n;
}

static int pci1710_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct pci1710_private *devpriv = dev->private;

	/* disable A/D triggers and interrupt sources */
	devpriv->ctrl &= PCI171X_CTRL_CNT0;	/* preserve counter 0 clk src */
	outw(devpriv->ctrl, dev->iobase + PCI171X_CTRL_REG);

	/* disable pacer */
	comedi_8254_pacer_enable(dev->pacer, 1, 2, false);

	/* clear A/D FIFO and any pending interrutps */
	outb(0, dev->iobase + PCI171X_CLRFIFO_REG);
	outb(0, dev->iobase + PCI171X_CLRINT_REG);

	return 0;
}

static void pci1710_handle_every_sample(struct comedi_device *dev,
					struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int status;
	unsigned int val;
	int ret;

	status = inw(dev->iobase + PCI171X_STATUS_REG);
	if (status & PCI171X_STATUS_FE) {
		dev_dbg(dev->class_dev, "A/D FIFO empty (%4x)\n", status);
		s->async->events |= COMEDI_CB_ERROR;
		return;
	}
	if (status & PCI171X_STATUS_FF) {
		dev_dbg(dev->class_dev,
			"A/D FIFO Full status (Fatal Error!) (%4x)\n", status);
		s->async->events |= COMEDI_CB_ERROR;
		return;
	}

	outb(0, dev->iobase + PCI171X_CLRINT_REG);

	for (; !(inw(dev->iobase + PCI171X_STATUS_REG) & PCI171X_STATUS_FE);) {
		ret = pci1710_ai_read_sample(dev, s, s->async->cur_chan, &val);
		if (ret) {
			s->async->events |= COMEDI_CB_ERROR;
			break;
		}

		comedi_buf_write_samples(s, &val, 1);

		if (cmd->stop_src == TRIG_COUNT &&
		    s->async->scans_done >= cmd->stop_arg) {
			s->async->events |= COMEDI_CB_EOA;
			break;
		}
	}

	outb(0, dev->iobase + PCI171X_CLRINT_REG);
}

static void pci1710_handle_fifo(struct comedi_device *dev,
				struct comedi_subdevice *s)
{
	struct pci1710_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int status;
	int i;

	status = inw(dev->iobase + PCI171X_STATUS_REG);
	if (!(status & PCI171X_STATUS_FH)) {
		dev_dbg(dev->class_dev, "A/D FIFO not half full!\n");
		async->events |= COMEDI_CB_ERROR;
		return;
	}
	if (status & PCI171X_STATUS_FF) {
		dev_dbg(dev->class_dev,
			"A/D FIFO Full status (Fatal Error!)\n");
		async->events |= COMEDI_CB_ERROR;
		return;
	}

	for (i = 0; i < devpriv->max_samples; i++) {
		unsigned int val;
		int ret;

		ret = pci1710_ai_read_sample(dev, s, s->async->cur_chan, &val);
		if (ret) {
			s->async->events |= COMEDI_CB_ERROR;
			break;
		}

		if (!comedi_buf_write_samples(s, &val, 1))
			break;

		if (cmd->stop_src == TRIG_COUNT &&
		    async->scans_done >= cmd->stop_arg) {
			async->events |= COMEDI_CB_EOA;
			break;
		}
	}

	outb(0, dev->iobase + PCI171X_CLRINT_REG);
}

static irqreturn_t pci1710_irq_handler(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct pci1710_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	struct comedi_cmd *cmd;

	if (!dev->attached)	/*  is device attached? */
		return IRQ_NONE;	/*  no, exit */

	s = dev->read_subdev;
	cmd = &s->async->cmd;

	/*  is this interrupt from our board? */
	if (!(inw(dev->iobase + PCI171X_STATUS_REG) & PCI171X_STATUS_IRQ))
		return IRQ_NONE;	/*  no, exit */

	if (devpriv->ai_et) {	/*  Switch from initial TRIG_EXT to TRIG_xxx. */
		devpriv->ai_et = 0;
		devpriv->ctrl &= PCI171X_CTRL_CNT0;
		devpriv->ctrl |= PCI171X_CTRL_SW; /* set software trigger */
		outw(devpriv->ctrl, dev->iobase + PCI171X_CTRL_REG);
		devpriv->ctrl = devpriv->ctrl_ext;
		outb(0, dev->iobase + PCI171X_CLRFIFO_REG);
		outb(0, dev->iobase + PCI171X_CLRINT_REG);
		/* no sample on this interrupt; reset the channel interval */
		outw(devpriv->mux_scan, dev->iobase + PCI171X_MUX_REG);
		outw(devpriv->ctrl, dev->iobase + PCI171X_CTRL_REG);
		comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
		return IRQ_HANDLED;
	}

	if (cmd->flags & CMDF_WAKE_EOS)
		pci1710_handle_every_sample(dev, s);
	else
		pci1710_handle_fifo(dev, s);

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static int pci1710_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct pci1710_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;

	pci1710_ai_setup_chanlist(dev, s, cmd->chanlist, cmd->chanlist_len,
				  devpriv->saved_seglen);

	outb(0, dev->iobase + PCI171X_CLRFIFO_REG);
	outb(0, dev->iobase + PCI171X_CLRINT_REG);

	devpriv->ctrl &= PCI171X_CTRL_CNT0;
	if ((cmd->flags & CMDF_WAKE_EOS) == 0)
		devpriv->ctrl |= PCI171X_CTRL_ONEFH;

	if (cmd->convert_src == TRIG_TIMER) {
		comedi_8254_update_divisors(dev->pacer);

		devpriv->ctrl |= PCI171X_CTRL_PACER | PCI171X_CTRL_IRQEN;
		if (cmd->start_src == TRIG_EXT) {
			devpriv->ctrl_ext = devpriv->ctrl;
			devpriv->ctrl &= ~(PCI171X_CTRL_PACER |
					   PCI171X_CTRL_ONEFH |
					   PCI171X_CTRL_GATE);
			devpriv->ctrl |= PCI171X_CTRL_EXT;
			devpriv->ai_et = 1;
		} else {	/* TRIG_NOW */
			devpriv->ai_et = 0;
		}
		outw(devpriv->ctrl, dev->iobase + PCI171X_CTRL_REG);

		if (cmd->start_src == TRIG_NOW)
			comedi_8254_pacer_enable(dev->pacer, 1, 2, true);
	} else {	/* TRIG_EXT */
		devpriv->ctrl |= PCI171X_CTRL_EXT | PCI171X_CTRL_IRQEN;
		outw(devpriv->ctrl, dev->iobase + PCI171X_CTRL_REG);
	}

	return 0;
}

static int pci1710_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* step 2a: make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* step 2b: and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);

	if (cmd->convert_src == TRIG_TIMER)
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg, 10000);
	else	/* TRIG_FOLLOW */
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

	/* Step 5: check channel list */

	err |= pci1710_ai_check_chanlist(dev, s, cmd);

	if (err)
		return 5;

	return 0;
}

static int pci1710_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct pci1710_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	devpriv->da_ranges &= ~PCI171X_DAREF_MASK(chan);
	devpriv->da_ranges |= PCI171X_DAREF(chan, range);
	outw(devpriv->da_ranges, dev->iobase + PCI171X_DAREF_REG);

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, dev->iobase + PCI171X_DA_REG(chan));
	}

	s->readback[chan] = val;

	return insn->n;
}

static int pci1710_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	data[1] = inw(dev->iobase + PCI171X_DI_REG);

	return insn->n;
}

static int pci1710_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + PCI171X_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int pci1710_counter_insn_config(struct comedi_device *dev,
				       struct comedi_subdevice *s,
				       struct comedi_insn *insn,
				       unsigned int *data)
{
	struct pci1710_private *devpriv = dev->private;

	switch (data[0]) {
	case INSN_CONFIG_SET_CLOCK_SRC:
		switch (data[1]) {
		case 0:	/* internal */
			devpriv->ctrl_ext &= ~PCI171X_CTRL_CNT0;
			break;
		case 1:	/* external */
			devpriv->ctrl_ext |= PCI171X_CTRL_CNT0;
			break;
		default:
			return -EINVAL;
		}
		outw(devpriv->ctrl_ext, dev->iobase + PCI171X_CTRL_REG);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		if (devpriv->ctrl_ext & PCI171X_CTRL_CNT0) {
			data[1] = 1;
			data[2] = 0;
		} else {
			data[1] = 0;
			data[2] = I8254_OSC_BASE_1MHZ;
		}
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static void pci1710_reset(struct comedi_device *dev)
{
	const struct boardtype *board = dev->board_ptr;

	/*
	 * Disable A/D triggers and interrupt sources, set counter 0
	 * to use internal 1 MHz clock.
	 */
	outw(0, dev->iobase + PCI171X_CTRL_REG);

	/* clear A/D FIFO and any pending interrutps */
	outb(0, dev->iobase + PCI171X_CLRFIFO_REG);
	outb(0, dev->iobase + PCI171X_CLRINT_REG);

	if (board->has_ao) {
		/* set DACs to 0..5V and outputs to 0V */
		outb(0, dev->iobase + PCI171X_DAREF_REG);
		outw(0, dev->iobase + PCI171X_DA_REG(0));
		outw(0, dev->iobase + PCI171X_DA_REG(1));
	}

	/* set digital outputs to 0 */
	outw(0, dev->iobase + PCI171X_DO_REG);
}

static int pci1710_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct boardtype *board = NULL;
	struct pci1710_private *devpriv;
	struct comedi_subdevice *s;
	int ret, subdev, n_subdevices;
	int i;

	if (context < ARRAY_SIZE(boardtypes))
		board = &boardtypes[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, 2);

	dev->pacer = comedi_8254_init(dev->iobase + PCI171X_TIMER_BASE,
				      I8254_OSC_BASE_10MHZ, I8254_IO16, 0);
	if (!dev->pacer)
		return -ENOMEM;

	n_subdevices = 1;	/* all boards have analog inputs */
	if (board->has_ao)
		n_subdevices++;
	if (!board->is_pci1713) {
		/*
		 * All other boards have digital inputs and outputs as
		 * well as a user counter.
		 */
		n_subdevices += 3;
	}

	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	pci1710_reset(dev);

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, pci1710_irq_handler,
				  IRQF_SHARED, dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	subdev = 0;

	/* Analog Input subdevice */
	s = &dev->subdevices[subdev++];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	if (!board->is_pci1711)
		s->subdev_flags	|= SDF_DIFF;
	s->n_chan	= board->is_pci1713 ? 32 : 16;
	s->maxdata	= 0x0fff;
	s->range_table	= board->ai_range;
	s->insn_read	= pci1710_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= s->n_chan;
		s->do_cmdtest	= pci1710_ai_cmdtest;
		s->do_cmd	= pci1710_ai_cmd;
		s->cancel	= pci1710_ai_cancel;
	}

	/* find the value needed to adjust for unipolar gain codes */
	for (i = 0; i < s->range_table->length; i++) {
		if (comedi_range_is_unipolar(s, i)) {
			devpriv->unipolar_gain = i;
			break;
		}
	}

	if (board->has_ao) {
		/* Analog Output subdevice */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND;
		s->n_chan	= 2;
		s->maxdata	= 0x0fff;
		s->range_table	= &pci171x_ao_range;
		s->insn_write	= pci1710_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
	}

	if (!board->is_pci1713) {
		/* Digital Input subdevice */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pci1710_di_insn_bits;

		/* Digital Output subdevice */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pci1710_do_insn_bits;

		/* Counter subdevice (8254) */
		s = &dev->subdevices[subdev++];
		comedi_8254_subdevice_init(s, dev->pacer);

		dev->pacer->insn_config = pci1710_counter_insn_config;

		/* counters 1 and 2 are used internally for the pacer */
		comedi_8254_set_busy(dev->pacer, 1, true);
		comedi_8254_set_busy(dev->pacer, 2, true);
	}

	/* max_samples is half the FIFO size (2 bytes/sample) */
	devpriv->max_samples = (board->is_pci1711) ? 512 : 2048;

	return 0;
}

static struct comedi_driver adv_pci1710_driver = {
	.driver_name	= "adv_pci1710",
	.module		= THIS_MODULE,
	.auto_attach	= pci1710_auto_attach,
	.detach		= comedi_pci_detach,
};

static int adv_pci1710_pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &adv_pci1710_driver,
				      id->driver_data);
}

static const struct pci_device_id adv_pci1710_pci_table[] = {
	{
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050),
		.driver_data = BOARD_PCI1710,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0x0000),
		.driver_data = BOARD_PCI1710,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xb100),
		.driver_data = BOARD_PCI1710,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xb200),
		.driver_data = BOARD_PCI1710,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xc100),
		.driver_data = BOARD_PCI1710,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xc200),
		.driver_data = BOARD_PCI1710,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710, 0x1000, 0xd100),
		.driver_data = BOARD_PCI1710,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0x0002),
		.driver_data = BOARD_PCI1710HG,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xb102),
		.driver_data = BOARD_PCI1710HG,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xb202),
		.driver_data = BOARD_PCI1710HG,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xc102),
		.driver_data = BOARD_PCI1710HG,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710,
			       PCI_VENDOR_ID_ADVANTECH, 0xc202),
		.driver_data = BOARD_PCI1710HG,
	}, {
		PCI_DEVICE_SUB(PCI_VENDOR_ID_ADVANTECH, 0x1710, 0x1000, 0xd102),
		.driver_data = BOARD_PCI1710HG,
	},
	{ PCI_VDEVICE(ADVANTECH, 0x1711), BOARD_PCI1711 },
	{ PCI_VDEVICE(ADVANTECH, 0x1713), BOARD_PCI1713 },
	{ PCI_VDEVICE(ADVANTECH, 0x1731), BOARD_PCI1731 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adv_pci1710_pci_table);

static struct pci_driver adv_pci1710_pci_driver = {
	.name		= "adv_pci1710",
	.id_table	= adv_pci1710_pci_table,
	.probe		= adv_pci1710_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(adv_pci1710_driver, adv_pci1710_pci_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi: Advantech PCI-1710 Series Multifunction DAS Cards");
MODULE_LICENSE("GPL");

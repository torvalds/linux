// SPDX-License-Identifier: GPL-2.0
/*
 * quatech_daqp_cs.c
 * Quatech DAQP PCMCIA data capture cards COMEDI client driver
 * Copyright (C) 2000, 2003 Brent Baccala <baccala@freesoft.org>
 * The DAQP interface code in this file is released into the public domain.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 * http://www.comedi.org/
 *
 * Documentation for the DAQP PCMCIA cards can be found on Quatech's site:
 *	ftp://ftp.quatech.com/Manuals/daqp-208.pdf
 *
 * This manual is for both the DAQP-208 and the DAQP-308.
 *
 * What works:
 * - A/D conversion
 *	- 8 channels
 *	- 4 gain ranges
 *	- ground ref or differential
 *	- single-shot and timed both supported
 * - D/A conversion, single-shot
 * - digital I/O
 *
 * What doesn't:
 * - any kind of triggering - external or D/A channel 1
 * - the card's optional expansion board
 * - the card's timer (for anything other than A/D conversion)
 * - D/A update modes other than immediate (i.e, timed)
 * - fancier timing modes
 * - setting card's FIFO buffer thresholds to anything but default
 */

/*
 * Driver: quatech_daqp_cs
 * Description: Quatech DAQP PCMCIA data capture cards
 * Devices: [Quatech] DAQP-208 (daqp), DAQP-308
 * Author: Brent Baccala <baccala@freesoft.org>
 * Status: works
 */

#include <linux/module.h>

#include "../comedi_pcmcia.h"

/*
 * Register I/O map
 *
 * The D/A and timer registers can be accessed with 16-bit or 8-bit I/O
 * instructions. All other registers can only use 8-bit instructions.
 *
 * The FIFO and scanlist registers require two 8-bit instructions to
 * access the 16-bit data. Data is transferred LSB then MSB.
 */
#define DAQP_AI_FIFO_REG		0x00

#define DAQP_SCANLIST_REG		0x01
#define DAQP_SCANLIST_DIFFERENTIAL	BIT(14)
#define DAQP_SCANLIST_GAIN(x)		(((x) & 0x3) << 12)
#define DAQP_SCANLIST_CHANNEL(x)	(((x) & 0xf) << 8)
#define DAQP_SCANLIST_START		BIT(7)
#define DAQP_SCANLIST_EXT_GAIN(x)	(((x) & 0x3) << 4)
#define DAQP_SCANLIST_EXT_CHANNEL(x)	(((x) & 0xf) << 0)

#define DAQP_CTRL_REG			0x02
#define DAQP_CTRL_PACER_CLK(x)		(((x) & 0x3) << 6)
#define DAQP_CTRL_PACER_CLK_EXT		DAQP_CTRL_PACER_CLK(0)
#define DAQP_CTRL_PACER_CLK_5MHZ	DAQP_CTRL_PACER_CLK(1)
#define DAQP_CTRL_PACER_CLK_1MHZ	DAQP_CTRL_PACER_CLK(2)
#define DAQP_CTRL_PACER_CLK_100KHZ	DAQP_CTRL_PACER_CLK(3)
#define DAQP_CTRL_EXPANSION		BIT(5)
#define DAQP_CTRL_EOS_INT_ENA		BIT(4)
#define DAQP_CTRL_FIFO_INT_ENA		BIT(3)
#define DAQP_CTRL_TRIG_MODE		BIT(2)	/* 0=one-shot; 1=continuous */
#define DAQP_CTRL_TRIG_SRC		BIT(1)	/* 0=internal; 1=external */
#define DAQP_CTRL_TRIG_EDGE		BIT(0)	/* 0=rising; 1=falling */

#define DAQP_STATUS_REG			0x02
#define DAQP_STATUS_IDLE		BIT(7)
#define DAQP_STATUS_RUNNING		BIT(6)
#define DAQP_STATUS_DATA_LOST		BIT(5)
#define DAQP_STATUS_END_OF_SCAN		BIT(4)
#define DAQP_STATUS_FIFO_THRESHOLD	BIT(3)
#define DAQP_STATUS_FIFO_FULL		BIT(2)
#define DAQP_STATUS_FIFO_NEARFULL	BIT(1)
#define DAQP_STATUS_FIFO_EMPTY		BIT(0)
/* these bits clear when the status register is read */
#define DAQP_STATUS_EVENTS		(DAQP_STATUS_DATA_LOST |	\
					 DAQP_STATUS_END_OF_SCAN |	\
					 DAQP_STATUS_FIFO_THRESHOLD)

#define DAQP_DI_REG			0x03
#define DAQP_DO_REG			0x03

#define DAQP_PACER_LOW_REG		0x04
#define DAQP_PACER_MID_REG		0x05
#define DAQP_PACER_HIGH_REG		0x06

#define DAQP_CMD_REG			0x07
/* the monostable bits are self-clearing after the function is complete */
#define DAQP_CMD_ARM			BIT(7)	/* monostable */
#define DAQP_CMD_RSTF			BIT(6)	/* monostable */
#define DAQP_CMD_RSTQ			BIT(5)	/* monostable */
#define DAQP_CMD_STOP			BIT(4)	/* monostable */
#define DAQP_CMD_LATCH			BIT(3)	/* monostable */
#define DAQP_CMD_SCANRATE(x)		(((x) & 0x3) << 1)
#define DAQP_CMD_SCANRATE_100KHZ	DAQP_CMD_SCANRATE(0)
#define DAQP_CMD_SCANRATE_50KHZ		DAQP_CMD_SCANRATE(1)
#define DAQP_CMD_SCANRATE_25KHZ		DAQP_CMD_SCANRATE(2)
#define DAQP_CMD_FIFO_DATA		BIT(0)

#define DAQP_AO_REG			0x08	/* and 0x09 (16-bit) */

#define DAQP_TIMER_REG			0x0a	/* and 0x0b (16-bit) */

#define DAQP_AUX_REG			0x0f
/* Auxiliary Control register bits (write) */
#define DAQP_AUX_EXT_ANALOG_TRIG	BIT(7)
#define DAQP_AUX_PRETRIG		BIT(6)
#define DAQP_AUX_TIMER_INT_ENA		BIT(5)
#define DAQP_AUX_TIMER_MODE(x)		(((x) & 0x3) << 3)
#define DAQP_AUX_TIMER_MODE_RELOAD	DAQP_AUX_TIMER_MODE(0)
#define DAQP_AUX_TIMER_MODE_PAUSE	DAQP_AUX_TIMER_MODE(1)
#define DAQP_AUX_TIMER_MODE_GO		DAQP_AUX_TIMER_MODE(2)
#define DAQP_AUX_TIMER_MODE_EXT		DAQP_AUX_TIMER_MODE(3)
#define DAQP_AUX_TIMER_CLK_SRC_EXT	BIT(2)
#define DAQP_AUX_DA_UPDATE(x)		(((x) & 0x3) << 0)
#define DAQP_AUX_DA_UPDATE_DIRECT	DAQP_AUX_DA_UPDATE(0)
#define DAQP_AUX_DA_UPDATE_OVERFLOW	DAQP_AUX_DA_UPDATE(1)
#define DAQP_AUX_DA_UPDATE_EXTERNAL	DAQP_AUX_DA_UPDATE(2)
#define DAQP_AUX_DA_UPDATE_PACER	DAQP_AUX_DA_UPDATE(3)
/* Auxiliary Status register bits (read) */
#define DAQP_AUX_RUNNING		BIT(7)
#define DAQP_AUX_TRIGGERED		BIT(6)
#define DAQP_AUX_DA_BUFFER		BIT(5)
#define DAQP_AUX_TIMER_OVERFLOW		BIT(4)
#define DAQP_AUX_CONVERSION		BIT(3)
#define DAQP_AUX_DATA_LOST		BIT(2)
#define DAQP_AUX_FIFO_NEARFULL		BIT(1)
#define DAQP_AUX_FIFO_EMPTY		BIT(0)

#define DAQP_FIFO_SIZE			4096

#define DAQP_MAX_TIMER_SPEED		10000	/* 100 kHz in nanoseconds */

struct daqp_private {
	unsigned int pacer_div;
	int stop;
};

static const struct comedi_lrange range_daqp_ai = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

static int daqp_clear_events(struct comedi_device *dev, int loops)
{
	unsigned int status;

	/*
	 * Reset any pending interrupts (my card has a tendency to require
	 * require multiple reads on the status register to achieve this).
	 */
	while (--loops) {
		status = inb(dev->iobase + DAQP_STATUS_REG);
		if ((status & DAQP_STATUS_EVENTS) == 0)
			return 0;
	}
	dev_err(dev->class_dev, "couldn't clear events in status register\n");
	return -EBUSY;
}

static int daqp_ai_cancel(struct comedi_device *dev,
			  struct comedi_subdevice *s)
{
	struct daqp_private *devpriv = dev->private;

	if (devpriv->stop)
		return -EIO;

	/*
	 * Stop any conversions, disable interrupts, and clear
	 * the status event flags.
	 */
	outb(DAQP_CMD_STOP, dev->iobase + DAQP_CMD_REG);
	outb(0, dev->iobase + DAQP_CTRL_REG);
	inb(dev->iobase + DAQP_STATUS_REG);

	return 0;
}

static unsigned int daqp_ai_get_sample(struct comedi_device *dev,
				       struct comedi_subdevice *s)
{
	unsigned int val;

	/*
	 * Get a two's complement sample from the FIFO and
	 * return the munged offset binary value.
	 */
	val = inb(dev->iobase + DAQP_AI_FIFO_REG);
	val |= inb(dev->iobase + DAQP_AI_FIFO_REG) << 8;
	return comedi_offset_munge(s, val);
}

static irqreturn_t daqp_interrupt(int irq, void *dev_id)
{
	struct comedi_device *dev = dev_id;
	struct comedi_subdevice *s = dev->read_subdev;
	struct comedi_cmd *cmd = &s->async->cmd;
	int loop_limit = 10000;
	int status;

	if (!dev->attached)
		return IRQ_NONE;

	status = inb(dev->iobase + DAQP_STATUS_REG);
	if (!(status & DAQP_STATUS_EVENTS))
		return IRQ_NONE;

	while (!(status & DAQP_STATUS_FIFO_EMPTY)) {
		unsigned short data;

		if (status & DAQP_STATUS_DATA_LOST) {
			s->async->events |= COMEDI_CB_OVERFLOW;
			dev_warn(dev->class_dev, "data lost\n");
			break;
		}

		data = daqp_ai_get_sample(dev, s);
		comedi_buf_write_samples(s, &data, 1);

		if (cmd->stop_src == TRIG_COUNT &&
		    s->async->scans_done >= cmd->stop_arg) {
			s->async->events |= COMEDI_CB_EOA;
			break;
		}

		if ((loop_limit--) <= 0)
			break;

		status = inb(dev->iobase + DAQP_STATUS_REG);
	}

	if (loop_limit <= 0) {
		dev_warn(dev->class_dev,
			 "loop_limit reached in %s()\n", __func__);
		s->async->events |= COMEDI_CB_ERROR;
	}

	comedi_handle_events(dev, s);

	return IRQ_HANDLED;
}

static void daqp_ai_set_one_scanlist_entry(struct comedi_device *dev,
					   unsigned int chanspec,
					   int start)
{
	unsigned int chan = CR_CHAN(chanspec);
	unsigned int range = CR_RANGE(chanspec);
	unsigned int aref = CR_AREF(chanspec);
	unsigned int val;

	val = DAQP_SCANLIST_CHANNEL(chan) | DAQP_SCANLIST_GAIN(range);

	if (aref == AREF_DIFF)
		val |= DAQP_SCANLIST_DIFFERENTIAL;

	if (start)
		val |= DAQP_SCANLIST_START;

	outb(val & 0xff, dev->iobase + DAQP_SCANLIST_REG);
	outb((val >> 8) & 0xff, dev->iobase + DAQP_SCANLIST_REG);
}

static int daqp_ai_eos(struct comedi_device *dev,
		       struct comedi_subdevice *s,
		       struct comedi_insn *insn,
		       unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAQP_AUX_REG);
	if (status & DAQP_AUX_CONVERSION)
		return 0;
	return -EBUSY;
}

static int daqp_ai_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct daqp_private *devpriv = dev->private;
	int ret = 0;
	int i;

	if (devpriv->stop)
		return -EIO;

	outb(0, dev->iobase + DAQP_AUX_REG);

	/* Reset scan list queue */
	outb(DAQP_CMD_RSTQ, dev->iobase + DAQP_CMD_REG);

	/* Program one scan list entry */
	daqp_ai_set_one_scanlist_entry(dev, insn->chanspec, 1);

	/* Reset data FIFO (see page 28 of DAQP User's Manual) */
	outb(DAQP_CMD_RSTF, dev->iobase + DAQP_CMD_REG);

	/* Set trigger - one-shot, internal, no interrupts */
	outb(DAQP_CTRL_PACER_CLK_100KHZ, dev->iobase + DAQP_CTRL_REG);

	ret = daqp_clear_events(dev, 10000);
	if (ret)
		return ret;

	for (i = 0; i < insn->n; i++) {
		/* Start conversion */
		outb(DAQP_CMD_ARM | DAQP_CMD_FIFO_DATA,
		     dev->iobase + DAQP_CMD_REG);

		ret = comedi_timeout(dev, s, insn, daqp_ai_eos, 0);
		if (ret)
			break;

		/* clear the status event flags */
		inb(dev->iobase + DAQP_STATUS_REG);

		data[i] = daqp_ai_get_sample(dev, s);
	}

	/* stop any conversions and clear the status event flags */
	outb(DAQP_CMD_STOP, dev->iobase + DAQP_CMD_REG);
	inb(dev->iobase + DAQP_STATUS_REG);

	return ret ? ret : insn->n;
}

/* This function converts ns nanoseconds to a counter value suitable
 * for programming the device.  We always use the DAQP's 5 MHz clock,
 * which with its 24-bit counter, allows values up to 84 seconds.
 * Also, the function adjusts ns so that it cooresponds to the actual
 * time that the device will use.
 */

static int daqp_ns_to_timer(unsigned int *ns, unsigned int flags)
{
	int timer;

	timer = *ns / 200;
	*ns = timer * 200;

	return timer;
}

static void daqp_set_pacer(struct comedi_device *dev, unsigned int val)
{
	outb(val & 0xff, dev->iobase + DAQP_PACER_LOW_REG);
	outb((val >> 8) & 0xff, dev->iobase + DAQP_PACER_MID_REG);
	outb((val >> 16) & 0xff, dev->iobase + DAQP_PACER_HIGH_REG);
}

static int daqp_ai_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_cmd *cmd)
{
	struct daqp_private *devpriv = dev->private;
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW);
	err |= comedi_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	/* the async command requires a pacer */
	if (cmd->scan_begin_src != TRIG_TIMER && cmd->convert_src != TRIG_TIMER)
		err |= -EINVAL;

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	err |= comedi_check_trigger_arg_min(&cmd->chanlist_len, 1);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->scan_begin_src == TRIG_TIMER)
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    DAQP_MAX_TIMER_SPEED);

	if (cmd->convert_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
						    DAQP_MAX_TIMER_SPEED);

		if (cmd->scan_begin_src == TRIG_TIMER) {
			/*
			 * If both scan_begin and convert are both timer
			 * values, the only way that can make sense is if
			 * the scan time is the number of conversions times
			 * the convert time.
			 */
			arg = cmd->convert_arg * cmd->scan_end_arg;
			err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg,
							   arg);
		}
	}

	if (cmd->stop_src == TRIG_COUNT)
		err |= comedi_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);
	else	/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->convert_src == TRIG_TIMER) {
		arg = cmd->convert_arg;
		devpriv->pacer_div = daqp_ns_to_timer(&arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, arg);
	} else if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		devpriv->pacer_div = daqp_ns_to_timer(&arg, cmd->flags);
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}

	if (err)
		return 4;

	return 0;
}

static int daqp_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct daqp_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int scanlist_start_on_every_entry;
	int threshold;
	int ret;
	int i;

	if (devpriv->stop)
		return -EIO;

	outb(0, dev->iobase + DAQP_AUX_REG);

	/* Reset scan list queue */
	outb(DAQP_CMD_RSTQ, dev->iobase + DAQP_CMD_REG);

	/* Program pacer clock
	 *
	 * There's two modes we can operate in.  If convert_src is
	 * TRIG_TIMER, then convert_arg specifies the time between
	 * each conversion, so we program the pacer clock to that
	 * frequency and set the SCANLIST_START bit on every scanlist
	 * entry.  Otherwise, convert_src is TRIG_NOW, which means
	 * we want the fastest possible conversions, scan_begin_src
	 * is TRIG_TIMER, and scan_begin_arg specifies the time between
	 * each scan, so we program the pacer clock to this frequency
	 * and only set the SCANLIST_START bit on the first entry.
	 */
	daqp_set_pacer(dev, devpriv->pacer_div);

	if (cmd->convert_src == TRIG_TIMER)
		scanlist_start_on_every_entry = 1;
	else
		scanlist_start_on_every_entry = 0;

	/* Program scan list */
	for (i = 0; i < cmd->chanlist_len; i++) {
		int start = (i == 0 || scanlist_start_on_every_entry);

		daqp_ai_set_one_scanlist_entry(dev, cmd->chanlist[i], start);
	}

	/* Now it's time to program the FIFO threshold, basically the
	 * number of samples the card will buffer before it interrupts
	 * the CPU.
	 *
	 * If we don't have a stop count, then use half the size of
	 * the FIFO (the manufacturer's recommendation).  Consider
	 * that the FIFO can hold 2K samples (4K bytes).  With the
	 * threshold set at half the FIFO size, we have a margin of
	 * error of 1024 samples.  At the chip's maximum sample rate
	 * of 100,000 Hz, the CPU would have to delay interrupt
	 * service for a full 10 milliseconds in order to lose data
	 * here (as opposed to higher up in the kernel).  I've never
	 * seen it happen.  However, for slow sample rates it may
	 * buffer too much data and introduce too much delay for the
	 * user application.
	 *
	 * If we have a stop count, then things get more interesting.
	 * If the stop count is less than the FIFO size (actually
	 * three-quarters of the FIFO size - see below), we just use
	 * the stop count itself as the threshold, the card interrupts
	 * us when that many samples have been taken, and we kill the
	 * acquisition at that point and are done.  If the stop count
	 * is larger than that, then we divide it by 2 until it's less
	 * than three quarters of the FIFO size (we always leave the
	 * top quarter of the FIFO as protection against sluggish CPU
	 * interrupt response) and use that as the threshold.  So, if
	 * the stop count is 4000 samples, we divide by two twice to
	 * get 1000 samples, use that as the threshold, take four
	 * interrupts to get our 4000 samples and are done.
	 *
	 * The algorithm could be more clever.  For example, if 81000
	 * samples are requested, we could set the threshold to 1500
	 * samples and take 54 interrupts to get 81000.  But 54 isn't
	 * a power of two, so this algorithm won't find that option.
	 * Instead, it'll set the threshold at 1266 and take 64
	 * interrupts to get 81024 samples, of which the last 24 will
	 * be discarded... but we won't get the last interrupt until
	 * they've been collected.  To find the first option, the
	 * computer could look at the prime decomposition of the
	 * sample count (81000 = 3^4 * 5^3 * 2^3) and factor it into a
	 * threshold (1500 = 3 * 5^3 * 2^2) and an interrupt count (54
	 * = 3^3 * 2).  Hmmm... a one-line while loop or prime
	 * decomposition of integers... I'll leave it the way it is.
	 *
	 * I'll also note a mini-race condition before ignoring it in
	 * the code.  Let's say we're taking 4000 samples, as before.
	 * After 1000 samples, we get an interrupt.  But before that
	 * interrupt is completely serviced, another sample is taken
	 * and loaded into the FIFO.  Since the interrupt handler
	 * empties the FIFO before returning, it will read 1001 samples.
	 * If that happens four times, we'll end up taking 4004 samples,
	 * not 4000.  The interrupt handler will discard the extra four
	 * samples (by halting the acquisition with four samples still
	 * in the FIFO), but we will have to wait for them.
	 *
	 * In short, this code works pretty well, but for either of
	 * the two reasons noted, might end up waiting for a few more
	 * samples than actually requested.  Shouldn't make too much
	 * of a difference.
	 */

	/* Save away the number of conversions we should perform, and
	 * compute the FIFO threshold (in bytes, not samples - that's
	 * why we multiple devpriv->count by 2 = sizeof(sample))
	 */

	if (cmd->stop_src == TRIG_COUNT) {
		unsigned long long nsamples;
		unsigned long long nbytes;

		nsamples = (unsigned long long)cmd->stop_arg *
			   cmd->scan_end_arg;
		nbytes = nsamples * comedi_bytes_per_sample(s);
		while (nbytes > DAQP_FIFO_SIZE * 3 / 4)
			nbytes /= 2;
		threshold = nbytes;
	} else {
		threshold = DAQP_FIFO_SIZE / 2;
	}

	/* Reset data FIFO (see page 28 of DAQP User's Manual) */

	outb(DAQP_CMD_RSTF, dev->iobase + DAQP_CMD_REG);

	/* Set FIFO threshold.  First two bytes are near-empty
	 * threshold, which is unused; next two bytes are near-full
	 * threshold.  We computed the number of bytes we want in the
	 * FIFO when the interrupt is generated, what the card wants
	 * is actually the number of available bytes left in the FIFO
	 * when the interrupt is to happen.
	 */

	outb(0x00, dev->iobase + DAQP_AI_FIFO_REG);
	outb(0x00, dev->iobase + DAQP_AI_FIFO_REG);

	outb((DAQP_FIFO_SIZE - threshold) & 0xff,
	     dev->iobase + DAQP_AI_FIFO_REG);
	outb((DAQP_FIFO_SIZE - threshold) >> 8, dev->iobase + DAQP_AI_FIFO_REG);

	/* Set trigger - continuous, internal */
	outb(DAQP_CTRL_TRIG_MODE | DAQP_CTRL_PACER_CLK_5MHZ |
	     DAQP_CTRL_FIFO_INT_ENA, dev->iobase + DAQP_CTRL_REG);

	ret = daqp_clear_events(dev, 100);
	if (ret)
		return ret;

	/* Start conversion */
	outb(DAQP_CMD_ARM | DAQP_CMD_FIFO_DATA, dev->iobase + DAQP_CMD_REG);

	return 0;
}

static int daqp_ao_empty(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAQP_AUX_REG);
	if ((status & DAQP_AUX_DA_BUFFER) == 0)
		return 0;
	return -EBUSY;
}

static int daqp_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct daqp_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	if (devpriv->stop)
		return -EIO;

	/* Make sure D/A update mode is direct update */
	outb(0, dev->iobase + DAQP_AUX_REG);

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];
		int ret;

		/* D/A transfer rate is about 8ms */
		ret = comedi_timeout(dev, s, insn, daqp_ao_empty, 0);
		if (ret)
			return ret;

		/* write the two's complement value to the channel */
		outw((chan << 12) | comedi_offset_munge(s, val),
		     dev->iobase + DAQP_AO_REG);

		s->readback[chan] = val;
	}

	return insn->n;
}

static int daqp_di_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct daqp_private *devpriv = dev->private;

	if (devpriv->stop)
		return -EIO;

	data[0] = inb(dev->iobase + DAQP_DI_REG);

	return insn->n;
}

static int daqp_do_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct daqp_private *devpriv = dev->private;

	if (devpriv->stop)
		return -EIO;

	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAQP_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static int daqp_auto_attach(struct comedi_device *dev,
			    unsigned long context)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	struct daqp_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	link->config_flags |= CONF_AUTO_SET_IO | CONF_ENABLE_IRQ;
	ret = comedi_pcmcia_enable(dev, NULL);
	if (ret)
		return ret;
	dev->iobase = link->resource[0]->start;

	link->priv = dev;
	ret = pcmcia_request_irq(link, daqp_interrupt);
	if (ret == 0)
		dev->irq = link->irq;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan	= 8;
	s->maxdata	= 0xffff;
	s->range_table	= &range_daqp_ai;
	s->insn_read	= daqp_ai_insn_read;
	if (dev->irq) {
		dev->read_subdev = s;
		s->subdev_flags	|= SDF_CMD_READ;
		s->len_chanlist	= 2048;
		s->do_cmdtest	= daqp_ai_cmdtest;
		s->do_cmd	= daqp_ai_cmd;
		s->cancel	= daqp_ai_cancel;
	}

	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 2;
	s->maxdata	= 0x0fff;
	s->range_table	= &range_bipolar5;
	s->insn_write	= daqp_ao_insn_write;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/*
	 * Digital Input subdevice
	 * NOTE: The digital input lines are shared:
	 *
	 * Chan  Normal Mode        Expansion Mode
	 * ----  -----------------  ----------------------------
	 *  0    DI0, ext. trigger  Same as normal mode
	 *  1    DI1                External gain select, lo bit
	 *  2    DI2, ext. clock    Same as normal mode
	 *  3    DI3                External gain select, hi bit
	 */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->insn_bits	= daqp_di_insn_bits;

	/*
	 * Digital Output subdevice
	 * NOTE: The digital output lines share the same pins on the
	 * interface connector as the four external channel selection
	 * bits. If expansion mode is used the digital outputs do not
	 * work.
	 */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= 4;
	s->maxdata	= 1;
	s->insn_bits	= daqp_do_insn_bits;

	return 0;
}

static struct comedi_driver driver_daqp = {
	.driver_name	= "quatech_daqp_cs",
	.module		= THIS_MODULE,
	.auto_attach	= daqp_auto_attach,
	.detach		= comedi_pcmcia_disable,
};

static int daqp_cs_suspend(struct pcmcia_device *link)
{
	struct comedi_device *dev = link->priv;
	struct daqp_private *devpriv = dev ? dev->private : NULL;

	/* Mark the device as stopped, to block IO until later */
	if (devpriv)
		devpriv->stop = 1;

	return 0;
}

static int daqp_cs_resume(struct pcmcia_device *link)
{
	struct comedi_device *dev = link->priv;
	struct daqp_private *devpriv = dev ? dev->private : NULL;

	if (devpriv)
		devpriv->stop = 0;

	return 0;
}

static int daqp_cs_attach(struct pcmcia_device *link)
{
	return comedi_pcmcia_auto_config(link, &driver_daqp);
}

static const struct pcmcia_device_id daqp_cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x0027),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, daqp_cs_id_table);

static struct pcmcia_driver daqp_cs_driver = {
	.name		= "quatech_daqp_cs",
	.owner		= THIS_MODULE,
	.id_table	= daqp_cs_id_table,
	.probe		= daqp_cs_attach,
	.remove		= comedi_pcmcia_auto_unconfig,
	.suspend	= daqp_cs_suspend,
	.resume		= daqp_cs_resume,
};
module_comedi_pcmcia_driver(driver_daqp, daqp_cs_driver);

MODULE_DESCRIPTION("Comedi driver for Quatech DAQP PCMCIA data capture cards");
MODULE_AUTHOR("Brent Baccala <baccala@freesoft.org>");
MODULE_LICENSE("GPL");

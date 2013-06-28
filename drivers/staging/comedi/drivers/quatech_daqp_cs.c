/*======================================================================

    comedi/drivers/quatech_daqp_cs.c

    Quatech DAQP PCMCIA data capture cards COMEDI client driver
    Copyright (C) 2000, 2003 Brent Baccala <baccala@freesoft.org>
    The DAQP interface code in this file is released into the public domain.

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998 David A. Schleef <ds@schleef.org>
    http://www.comedi.org/

    quatech_daqp_cs.c 1.10

    Documentation for the DAQP PCMCIA cards can be found on Quatech's site:

		ftp://ftp.quatech.com/Manuals/daqp-208.pdf

    This manual is for both the DAQP-208 and the DAQP-308.

    What works:

	- A/D conversion
	    - 8 channels
	    - 4 gain ranges
	    - ground ref or differential
	    - single-shot and timed both supported
	- D/A conversion, single-shot
	- digital I/O

    What doesn't:

	- any kind of triggering - external or D/A channel 1
	- the card's optional expansion board
	- the card's timer (for anything other than A/D conversion)
	- D/A update modes other than immediate (i.e, timed)
	- fancier timing modes
	- setting card's FIFO buffer thresholds to anything but default

======================================================================*/

/*
Driver: quatech_daqp_cs
Description: Quatech DAQP PCMCIA data capture cards
Author: Brent Baccala <baccala@freesoft.org>
Status: works
Devices: [Quatech] DAQP-208 (daqp), DAQP-308
*/

#include "../comedidev.h"
#include <linux/semaphore.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <linux/completion.h>

#include "comedi_fc.h"

struct daqp_private {
	int stop;

	enum { semaphore, buffer } interrupt_mode;

	struct completion eos;

	int count;
};

/* The DAQP communicates with the system through a 16 byte I/O window. */

#define DAQP_FIFO_SIZE		4096

#define DAQP_FIFO		0
#define DAQP_SCANLIST		1
#define DAQP_CONTROL		2
#define DAQP_STATUS		2
#define DAQP_DIGITAL_IO		3
#define DAQP_PACER_LOW		4
#define DAQP_PACER_MID		5
#define DAQP_PACER_HIGH		6
#define DAQP_COMMAND		7
#define DAQP_DA			8
#define DAQP_TIMER		10
#define DAQP_AUX		15

#define DAQP_SCANLIST_DIFFERENTIAL	0x4000
#define DAQP_SCANLIST_GAIN(x)		((x)<<12)
#define DAQP_SCANLIST_CHANNEL(x)	((x)<<8)
#define DAQP_SCANLIST_START		0x0080
#define DAQP_SCANLIST_EXT_GAIN(x)	((x)<<4)
#define DAQP_SCANLIST_EXT_CHANNEL(x)	(x)

#define DAQP_CONTROL_PACER_100kHz	0xc0
#define DAQP_CONTROL_PACER_1MHz		0x80
#define DAQP_CONTROL_PACER_5MHz		0x40
#define DAQP_CONTROL_PACER_EXTERNAL	0x00
#define DAQP_CONTORL_EXPANSION		0x20
#define DAQP_CONTROL_EOS_INT_ENABLE	0x10
#define DAQP_CONTROL_FIFO_INT_ENABLE	0x08
#define DAQP_CONTROL_TRIGGER_ONESHOT	0x00
#define DAQP_CONTROL_TRIGGER_CONTINUOUS	0x04
#define DAQP_CONTROL_TRIGGER_INTERNAL	0x00
#define DAQP_CONTROL_TRIGGER_EXTERNAL	0x02
#define DAQP_CONTROL_TRIGGER_RISING	0x00
#define DAQP_CONTROL_TRIGGER_FALLING	0x01

#define DAQP_STATUS_IDLE		0x80
#define DAQP_STATUS_RUNNING		0x40
#define DAQP_STATUS_EVENTS		0x38
#define DAQP_STATUS_DATA_LOST		0x20
#define DAQP_STATUS_END_OF_SCAN		0x10
#define DAQP_STATUS_FIFO_THRESHOLD	0x08
#define DAQP_STATUS_FIFO_FULL		0x04
#define DAQP_STATUS_FIFO_NEARFULL	0x02
#define DAQP_STATUS_FIFO_EMPTY		0x01

#define DAQP_COMMAND_ARM		0x80
#define DAQP_COMMAND_RSTF		0x40
#define DAQP_COMMAND_RSTQ		0x20
#define DAQP_COMMAND_STOP		0x10
#define DAQP_COMMAND_LATCH		0x08
#define DAQP_COMMAND_100kHz		0x00
#define DAQP_COMMAND_50kHz		0x02
#define DAQP_COMMAND_25kHz		0x04
#define DAQP_COMMAND_FIFO_DATA		0x01
#define DAQP_COMMAND_FIFO_PROGRAM	0x00

#define DAQP_AUX_TRIGGER_TTL		0x00
#define DAQP_AUX_TRIGGER_ANALOG		0x80
#define DAQP_AUX_TRIGGER_PRETRIGGER	0x40
#define DAQP_AUX_TIMER_INT_ENABLE	0x20
#define DAQP_AUX_TIMER_RELOAD		0x00
#define DAQP_AUX_TIMER_PAUSE		0x08
#define DAQP_AUX_TIMER_GO		0x10
#define DAQP_AUX_TIMER_GO_EXTERNAL	0x18
#define DAQP_AUX_TIMER_EXTERNAL_SRC	0x04
#define DAQP_AUX_TIMER_INTERNAL_SRC	0x00
#define DAQP_AUX_DA_DIRECT		0x00
#define DAQP_AUX_DA_OVERFLOW		0x01
#define DAQP_AUX_DA_EXTERNAL		0x02
#define DAQP_AUX_DA_PACER		0x03

#define DAQP_AUX_RUNNING		0x80
#define DAQP_AUX_TRIGGERED		0x40
#define DAQP_AUX_DA_BUFFER		0x20
#define DAQP_AUX_TIMER_OVERFLOW		0x10
#define DAQP_AUX_CONVERSION		0x08
#define DAQP_AUX_DATA_LOST		0x04
#define DAQP_AUX_FIFO_NEARFULL		0x02
#define DAQP_AUX_FIFO_EMPTY		0x01

static const struct comedi_lrange range_daqp_ai = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25)
	}
};

/* Cancel a running acquisition */

static int daqp_ai_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct daqp_private *devpriv = dev->private;

	if (devpriv->stop)
		return -EIO;

	outb(DAQP_COMMAND_STOP, dev->iobase + DAQP_COMMAND);

	/* flush any linguring data in FIFO - superfluous here */
	/* outb(DAQP_COMMAND_RSTF, dev->iobase+DAQP_COMMAND); */

	devpriv->interrupt_mode = semaphore;

	return 0;
}

/* Interrupt handler
 *
 * Operates in one of two modes.  If devpriv->interrupt_mode is
 * 'semaphore', just signal the devpriv->eos completion and return
 * (one-shot mode).  Otherwise (continuous mode), read data in from
 * the card, transfer it to the buffer provided by the higher-level
 * comedi kernel module, and signal various comedi callback routines,
 * which run pretty quick.
 */
static enum irqreturn daqp_interrupt(int irq, void *dev_id)
{
	struct comedi_device *dev = dev_id;
	struct daqp_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	int loop_limit = 10000;
	int status;

	if (!dev->attached)
		return IRQ_NONE;

	switch (devpriv->interrupt_mode) {
	case semaphore:
		complete(&devpriv->eos);
		break;

	case buffer:
		while (!((status = inb(dev->iobase + DAQP_STATUS))
			 & DAQP_STATUS_FIFO_EMPTY)) {

			short data;

			if (status & DAQP_STATUS_DATA_LOST) {
				s->async->events |=
				    COMEDI_CB_EOA | COMEDI_CB_OVERFLOW;
				dev_warn(dev->class_dev, "data lost\n");
				daqp_ai_cancel(dev, s);
				break;
			}

			data = inb(dev->iobase + DAQP_FIFO);
			data |= inb(dev->iobase + DAQP_FIFO) << 8;
			data ^= 0x8000;

			comedi_buf_put(s->async, data);

			/* If there's a limit, decrement it
			 * and stop conversion if zero
			 */

			if (devpriv->count > 0) {
				devpriv->count--;
				if (devpriv->count == 0) {
					daqp_ai_cancel(dev, s);
					s->async->events |= COMEDI_CB_EOA;
					break;
				}
			}

			if ((loop_limit--) <= 0)
				break;
		}

		if (loop_limit <= 0) {
			dev_warn(dev->class_dev,
				 "loop_limit reached in daqp_interrupt()\n");
			daqp_ai_cancel(dev, s);
			s->async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
		}

		s->async->events |= COMEDI_CB_BLOCK;

		comedi_event(dev, s);
	}
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

	outb(val & 0xff, dev->iobase + DAQP_SCANLIST);
	outb((val >> 8) & 0xff, dev->iobase + DAQP_SCANLIST);
}

/* One-shot analog data acquisition routine */

static int daqp_ai_insn_read(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct daqp_private *devpriv = dev->private;
	int i;
	int v;
	int counter = 10000;

	if (devpriv->stop)
		return -EIO;

	/* Stop any running conversion */
	daqp_ai_cancel(dev, s);

	outb(0, dev->iobase + DAQP_AUX);

	/* Reset scan list queue */
	outb(DAQP_COMMAND_RSTQ, dev->iobase + DAQP_COMMAND);

	/* Program one scan list entry */
	daqp_ai_set_one_scanlist_entry(dev, insn->chanspec, 1);

	/* Reset data FIFO (see page 28 of DAQP User's Manual) */

	outb(DAQP_COMMAND_RSTF, dev->iobase + DAQP_COMMAND);

	/* Set trigger */

	v = DAQP_CONTROL_TRIGGER_ONESHOT | DAQP_CONTROL_TRIGGER_INTERNAL
	    | DAQP_CONTROL_PACER_100kHz | DAQP_CONTROL_EOS_INT_ENABLE;

	outb(v, dev->iobase + DAQP_CONTROL);

	/* Reset any pending interrupts (my card has a tendency to require
	 * require multiple reads on the status register to achieve this)
	 */

	while (--counter
	       && (inb(dev->iobase + DAQP_STATUS) & DAQP_STATUS_EVENTS))
		;
	if (!counter) {
		dev_err(dev->class_dev,
			"couldn't clear interrupts in status register\n");
		return -1;
	}

	init_completion(&devpriv->eos);
	devpriv->interrupt_mode = semaphore;

	for (i = 0; i < insn->n; i++) {

		/* Start conversion */
		outb(DAQP_COMMAND_ARM | DAQP_COMMAND_FIFO_DATA,
		     dev->iobase + DAQP_COMMAND);

		/* Wait for interrupt service routine to unblock completion */
		/* Maybe could use a timeout here, but it's interruptible */
		if (wait_for_completion_interruptible(&devpriv->eos))
			return -EINTR;

		data[i] = inb(dev->iobase + DAQP_FIFO);
		data[i] |= inb(dev->iobase + DAQP_FIFO) << 8;
		data[i] ^= 0x8000;
	}

	return insn->n;
}

/* This function converts ns nanoseconds to a counter value suitable
 * for programming the device.  We always use the DAQP's 5 MHz clock,
 * which with its 24-bit counter, allows values up to 84 seconds.
 * Also, the function adjusts ns so that it cooresponds to the actual
 * time that the device will use.
 */

static int daqp_ns_to_timer(unsigned int *ns, int round)
{
	int timer;

	timer = *ns / 200;
	*ns = timer * 200;

	return timer;
}

/* cmdtest tests a particular command to see if it is valid.
 * Using the cmdtest ioctl, a user can create a valid cmd
 * and then have it executed by the cmd ioctl.
 *
 * cmdtest returns 1,2,3,4 or 0, depending on which tests
 * the command passes.
 */

static int daqp_ai_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

#define MAX_SPEED	10000	/* 100 kHz - in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 MAX_SPEED);

	/* If both scan_begin and convert are both timer values, the only
	 * way that can make sense is if the scan time is the number of
	 * conversions times the convert time
	 */

	if (cmd->scan_begin_src == TRIG_TIMER && cmd->convert_src == TRIG_TIMER
	    && cmd->scan_begin_arg != cmd->convert_arg * cmd->scan_end_arg) {
		err |= -EINVAL;
	}

	if (cmd->convert_src == TRIG_TIMER)
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg, MAX_SPEED);

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		daqp_ns_to_timer(&cmd->scan_begin_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}

	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		daqp_ns_to_timer(&cmd->convert_arg,
				 cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
	}

	if (err)
		return 4;

	return 0;
}

static int daqp_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct daqp_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int counter;
	int scanlist_start_on_every_entry;
	int threshold;

	int i;
	int v;

	if (devpriv->stop)
		return -EIO;

	/* Stop any running conversion */
	daqp_ai_cancel(dev, s);

	outb(0, dev->iobase + DAQP_AUX);

	/* Reset scan list queue */
	outb(DAQP_COMMAND_RSTQ, dev->iobase + DAQP_COMMAND);

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

	if (cmd->convert_src == TRIG_TIMER) {
		counter = daqp_ns_to_timer(&cmd->convert_arg,
					       cmd->flags & TRIG_ROUND_MASK);
		outb(counter & 0xff, dev->iobase + DAQP_PACER_LOW);
		outb((counter >> 8) & 0xff, dev->iobase + DAQP_PACER_MID);
		outb((counter >> 16) & 0xff, dev->iobase + DAQP_PACER_HIGH);
		scanlist_start_on_every_entry = 1;
	} else {
		counter = daqp_ns_to_timer(&cmd->scan_begin_arg,
					       cmd->flags & TRIG_ROUND_MASK);
		outb(counter & 0xff, dev->iobase + DAQP_PACER_LOW);
		outb((counter >> 8) & 0xff, dev->iobase + DAQP_PACER_MID);
		outb((counter >> 16) & 0xff, dev->iobase + DAQP_PACER_HIGH);
		scanlist_start_on_every_entry = 0;
	}

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
		devpriv->count = cmd->stop_arg * cmd->scan_end_arg;
		threshold = 2 * devpriv->count;
		while (threshold > DAQP_FIFO_SIZE * 3 / 4)
			threshold /= 2;
	} else {
		devpriv->count = -1;
		threshold = DAQP_FIFO_SIZE / 2;
	}

	/* Reset data FIFO (see page 28 of DAQP User's Manual) */

	outb(DAQP_COMMAND_RSTF, dev->iobase + DAQP_COMMAND);

	/* Set FIFO threshold.  First two bytes are near-empty
	 * threshold, which is unused; next two bytes are near-full
	 * threshold.  We computed the number of bytes we want in the
	 * FIFO when the interrupt is generated, what the card wants
	 * is actually the number of available bytes left in the FIFO
	 * when the interrupt is to happen.
	 */

	outb(0x00, dev->iobase + DAQP_FIFO);
	outb(0x00, dev->iobase + DAQP_FIFO);

	outb((DAQP_FIFO_SIZE - threshold) & 0xff, dev->iobase + DAQP_FIFO);
	outb((DAQP_FIFO_SIZE - threshold) >> 8, dev->iobase + DAQP_FIFO);

	/* Set trigger */

	v = DAQP_CONTROL_TRIGGER_CONTINUOUS | DAQP_CONTROL_TRIGGER_INTERNAL
	    | DAQP_CONTROL_PACER_5MHz | DAQP_CONTROL_FIFO_INT_ENABLE;

	outb(v, dev->iobase + DAQP_CONTROL);

	/* Reset any pending interrupts (my card has a tendency to require
	 * require multiple reads on the status register to achieve this)
	 */
	counter = 100;
	while (--counter
	       && (inb(dev->iobase + DAQP_STATUS) & DAQP_STATUS_EVENTS))
		;
	if (!counter) {
		dev_err(dev->class_dev,
			"couldn't clear interrupts in status register\n");
		return -1;
	}

	devpriv->interrupt_mode = buffer;

	/* Start conversion */
	outb(DAQP_COMMAND_ARM | DAQP_COMMAND_FIFO_DATA,
	     dev->iobase + DAQP_COMMAND);

	return 0;
}

static int daqp_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct daqp_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val;
	int i;

	if (devpriv->stop)
		return -EIO;

	/* Make sure D/A update mode is direct update */
	outb(0, dev->iobase + DAQP_AUX);

	for (i = 0; i > insn->n; i++) {
		val = data[0];
		val &= 0x0fff;
		val ^= 0x0800;		/* Flip the sign */
		val |= (chan << 12);

		outw(val, dev->iobase + DAQP_DA);
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

	data[0] = inb(dev->iobase + DAQP_DIGITAL_IO);

	return insn->n;
}

static int daqp_do_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	struct daqp_private *devpriv = dev->private;
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	if (devpriv->stop)
		return -EIO;

	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outb(s->state, dev->iobase + DAQP_DIGITAL_IO);
	}

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

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	link->config_flags |= CONF_AUTO_SET_IO | CONF_ENABLE_IRQ;
	ret = comedi_pcmcia_enable(dev, NULL);
	if (ret)
		return ret;
	dev->iobase = link->resource[0]->start;

	link->priv = dev;
	ret = pcmcia_request_irq(link, daqp_interrupt);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF | SDF_CMD_READ;
	s->n_chan	= 8;
	s->len_chanlist	= 2048;
	s->maxdata	= 0xffff;
	s->range_table	= &range_daqp_ai;
	s->insn_read	= daqp_ai_insn_read;
	s->do_cmdtest	= daqp_ai_cmdtest;
	s->do_cmd	= daqp_ai_cmd;
	s->cancel	= daqp_ai_cancel;

	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 2;
	s->maxdata	= 0x0fff;
	s->range_table	= &range_bipolar5;
	s->insn_write	= daqp_ao_insn_write;

	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 1;
	s->maxdata	= 1;
	s->insn_bits	= daqp_di_insn_bits;

	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 1;
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

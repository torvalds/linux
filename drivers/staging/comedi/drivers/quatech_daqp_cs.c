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

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
*/

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0644);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static char *version = "quatech_daqp_cs.c 1.10 2003/04/21 (Brent Baccala)";
#else
#define DEBUG(n, args...)
#endif

/* Maximum number of separate DAQP devices we'll allow */
#define MAX_DEV         4

struct local_info_t {
	struct pcmcia_device *link;
	dev_node_t node;
	int stop;
	int table_index;
	char board_name[32];

	enum { semaphore, buffer } interrupt_mode;

	struct semaphore eos;

	struct comedi_device *dev;
	struct comedi_subdevice *s;
	int count;
};

/* A list of "instances" of the device. */

static struct local_info_t *dev_table[MAX_DEV] = { NULL, /* ... */  };

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

/* These range structures tell COMEDI how the sample values map to
 * voltages.  The A/D converter has four ranges: +/- 10V through
 * +/- 1.25V, and the D/A converter has only one: +/- 5V.
 */

static const struct comedi_lrange range_daqp_ai = { 4, {
			BIP_RANGE(10),
			BIP_RANGE(5),
			BIP_RANGE(2.5),
			BIP_RANGE(1.25)
	}
};

static const struct comedi_lrange range_daqp_ao = { 1, {BIP_RANGE(5)} };

/*====================================================================*/

/* comedi interface code */

static int daqp_attach(struct comedi_device * dev, struct comedi_devconfig * it);
static int daqp_detach(struct comedi_device * dev);
static struct comedi_driver driver_daqp = {
      driver_name:"quatech_daqp_cs",
      module:THIS_MODULE,
      attach:daqp_attach,
      detach:daqp_detach,
};

#ifdef DAQP_DEBUG

static void daqp_dump(struct comedi_device * dev)
{
	printk("DAQP: status %02x; aux status %02x\n",
		inb(dev->iobase + DAQP_STATUS), inb(dev->iobase + DAQP_AUX));
}

static void hex_dump(char *str, void *ptr, int len)
{
	unsigned char *cptr = ptr;
	int i;

	printk(str);

	for (i = 0; i < len; i++) {
		if (i % 16 == 0) {
			printk("\n0x%08x:", (unsigned int)cptr);
		}
		printk(" %02x", *(cptr++));
	}
	printk("\n");
}

#endif

/* Cancel a running acquisition */

static int daqp_ai_cancel(struct comedi_device * dev, struct comedi_subdevice * s)
{
	struct local_info_t *local = (struct local_info_t *) s->private;

	if (local->stop) {
		return -EIO;
	}

	outb(DAQP_COMMAND_STOP, dev->iobase + DAQP_COMMAND);

	/* flush any linguring data in FIFO - superfluous here */
	/* outb(DAQP_COMMAND_RSTF, dev->iobase+DAQP_COMMAND); */

	local->interrupt_mode = semaphore;

	return 0;
}

/* Interrupt handler
 *
 * Operates in one of two modes.  If local->interrupt_mode is
 * 'semaphore', just signal the local->eos semaphore and return
 * (one-shot mode).  Otherwise (continuous mode), read data in from
 * the card, transfer it to the buffer provided by the higher-level
 * comedi kernel module, and signal various comedi callback routines,
 * which run pretty quick.
 */

static void daqp_interrupt(int irq, void *dev_id)
{
	struct local_info_t *local = (struct local_info_t *) dev_id;
	struct comedi_device *dev;
	struct comedi_subdevice *s;
	int loop_limit = 10000;
	int status;

	if (local == NULL) {
		printk(KERN_WARNING
			"daqp_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}

	dev = local->dev;
	if (dev == NULL) {
		printk(KERN_WARNING "daqp_interrupt(): NULL comedi_device.\n");
		return;
	}

	if (!dev->attached) {
		printk(KERN_WARNING
			"daqp_interrupt(): struct comedi_device not yet attached.\n");
		return;
	}

	s = local->s;
	if (s == NULL) {
		printk(KERN_WARNING
			"daqp_interrupt(): NULL comedi_subdevice.\n");
		return;
	}

	if ((struct local_info_t *) s->private != local) {
		printk(KERN_WARNING
			"daqp_interrupt(): invalid comedi_subdevice.\n");
		return;
	}

	switch (local->interrupt_mode) {

	case semaphore:

		up(&local->eos);
		break;

	case buffer:

		while (!((status = inb(dev->iobase + DAQP_STATUS))
				& DAQP_STATUS_FIFO_EMPTY)) {

			short data;

			if (status & DAQP_STATUS_DATA_LOST) {
				s->async->events |=
					COMEDI_CB_EOA | COMEDI_CB_OVERFLOW;
				printk("daqp: data lost\n");
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

			if (local->count > 0) {
				local->count--;
				if (local->count == 0) {
					daqp_ai_cancel(dev, s);
					s->async->events |= COMEDI_CB_EOA;
					break;
				}
			}

			if ((loop_limit--) <= 0)
				break;
		}

		if (loop_limit <= 0) {
			printk(KERN_WARNING
				"loop_limit reached in daqp_interrupt()\n");
			daqp_ai_cancel(dev, s);
			s->async->events |= COMEDI_CB_EOA | COMEDI_CB_ERROR;
		}

		s->async->events |= COMEDI_CB_BLOCK;

		comedi_event(dev, s);
	}
}

/* One-shot analog data acquisition routine */

static int daqp_ai_insn_read(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	struct local_info_t *local = (struct local_info_t *) s->private;
	int i;
	int v;
	int counter = 10000;

	if (local->stop) {
		return -EIO;
	}

	/* Stop any running conversion */
	daqp_ai_cancel(dev, s);

	outb(0, dev->iobase + DAQP_AUX);

	/* Reset scan list queue */
	outb(DAQP_COMMAND_RSTQ, dev->iobase + DAQP_COMMAND);

	/* Program one scan list entry */

	v = DAQP_SCANLIST_CHANNEL(CR_CHAN(insn->chanspec))
		| DAQP_SCANLIST_GAIN(CR_RANGE(insn->chanspec));

	if (CR_AREF(insn->chanspec) == AREF_DIFF) {
		v |= DAQP_SCANLIST_DIFFERENTIAL;
	}

	v |= DAQP_SCANLIST_START;

	outb(v & 0xff, dev->iobase + DAQP_SCANLIST);
	outb(v >> 8, dev->iobase + DAQP_SCANLIST);

	/* Reset data FIFO (see page 28 of DAQP User's Manual) */

	outb(DAQP_COMMAND_RSTF, dev->iobase + DAQP_COMMAND);

	/* Set trigger */

	v = DAQP_CONTROL_TRIGGER_ONESHOT | DAQP_CONTROL_TRIGGER_INTERNAL
		| DAQP_CONTROL_PACER_100kHz | DAQP_CONTROL_EOS_INT_ENABLE;

	outb(v, dev->iobase + DAQP_CONTROL);

	/* Reset any pending interrupts (my card has a tendancy to require
	 * require multiple reads on the status register to achieve this)
	 */

	while (--counter
		&& (inb(dev->iobase + DAQP_STATUS) & DAQP_STATUS_EVENTS)) ;
	if (!counter) {
		printk("daqp: couldn't clear interrupts in status register\n");
		return -1;
	}

	/* Make sure semaphore is blocked */
	sema_init(&local->eos, 0);
	local->interrupt_mode = semaphore;
	local->dev = dev;
	local->s = s;

	for (i = 0; i < insn->n; i++) {

		/* Start conversion */
		outb(DAQP_COMMAND_ARM | DAQP_COMMAND_FIFO_DATA,
			dev->iobase + DAQP_COMMAND);

		/* Wait for interrupt service routine to unblock semaphore */
		/* Maybe could use a timeout here, but it's interruptible */
		if (down_interruptible(&local->eos))
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

static int daqp_ai_cmdtest(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_cmd * cmd)
{
	int err = 0;
	int tmp;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_TIMER | TRIG_FOLLOW;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER | TRIG_NOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually compatible */

	/* note that mutual compatiblity is not an issue here */
	if (cmd->scan_begin_src != TRIG_TIMER &&
		cmd->scan_begin_src != TRIG_FOLLOW)
		err++;
	if (cmd->convert_src != TRIG_NOW && cmd->convert_src != TRIG_TIMER)
		err++;
	if (cmd->scan_begin_src == TRIG_FOLLOW && cmd->convert_src == TRIG_NOW)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
#define MAX_SPEED	10000	/* 100 kHz - in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER
		&& cmd->scan_begin_arg < MAX_SPEED) {
		cmd->scan_begin_arg = MAX_SPEED;
		err++;
	}

	/* If both scan_begin and convert are both timer values, the only
	 * way that can make sense is if the scan time is the number of
	 * conversions times the convert time
	 */

	if (cmd->scan_begin_src == TRIG_TIMER && cmd->convert_src == TRIG_TIMER
		&& cmd->scan_begin_arg !=
		cmd->convert_arg * cmd->scan_end_arg) {
		err++;
	}

	if (cmd->convert_src == TRIG_TIMER && cmd->convert_arg < MAX_SPEED) {
		cmd->convert_arg = MAX_SPEED;
		err++;
	}

	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (cmd->stop_arg > 0x00ffffff) {
			cmd->stop_arg = 0x00ffffff;
			err++;
		}
	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

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

static int daqp_ai_cmd(struct comedi_device * dev, struct comedi_subdevice * s)
{
	struct local_info_t *local = (struct local_info_t *) s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int counter = 100;
	int scanlist_start_on_every_entry;
	int threshold;

	int i;
	int v;

	if (local->stop) {
		return -EIO;
	}

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
		int counter = daqp_ns_to_timer(&cmd->convert_arg,
			cmd->flags & TRIG_ROUND_MASK);
		outb(counter & 0xff, dev->iobase + DAQP_PACER_LOW);
		outb((counter >> 8) & 0xff, dev->iobase + DAQP_PACER_MID);
		outb((counter >> 16) & 0xff, dev->iobase + DAQP_PACER_HIGH);
		scanlist_start_on_every_entry = 1;
	} else {
		int counter = daqp_ns_to_timer(&cmd->scan_begin_arg,
			cmd->flags & TRIG_ROUND_MASK);
		outb(counter & 0xff, dev->iobase + DAQP_PACER_LOW);
		outb((counter >> 8) & 0xff, dev->iobase + DAQP_PACER_MID);
		outb((counter >> 16) & 0xff, dev->iobase + DAQP_PACER_HIGH);
		scanlist_start_on_every_entry = 0;
	}

	/* Program scan list */

	for (i = 0; i < cmd->chanlist_len; i++) {

		int chanspec = cmd->chanlist[i];

		/* Program one scan list entry */

		v = DAQP_SCANLIST_CHANNEL(CR_CHAN(chanspec))
			| DAQP_SCANLIST_GAIN(CR_RANGE(chanspec));

		if (CR_AREF(chanspec) == AREF_DIFF) {
			v |= DAQP_SCANLIST_DIFFERENTIAL;
		}

		if (i == 0 || scanlist_start_on_every_entry) {
			v |= DAQP_SCANLIST_START;
		}

		outb(v & 0xff, dev->iobase + DAQP_SCANLIST);
		outb(v >> 8, dev->iobase + DAQP_SCANLIST);
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
	 * why we multiple local->count by 2 = sizeof(sample))
	 */

	if (cmd->stop_src == TRIG_COUNT) {
		local->count = cmd->stop_arg * cmd->scan_end_arg;
		threshold = 2 * local->count;
		while (threshold > DAQP_FIFO_SIZE * 3 / 4)
			threshold /= 2;
	} else {
		local->count = -1;
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

	/* Reset any pending interrupts (my card has a tendancy to require
	 * require multiple reads on the status register to achieve this)
	 */

	while (--counter
		&& (inb(dev->iobase + DAQP_STATUS) & DAQP_STATUS_EVENTS)) ;
	if (!counter) {
		printk("daqp: couldn't clear interrupts in status register\n");
		return -1;
	}

	local->interrupt_mode = buffer;
	local->dev = dev;
	local->s = s;

	/* Start conversion */
	outb(DAQP_COMMAND_ARM | DAQP_COMMAND_FIFO_DATA,
		dev->iobase + DAQP_COMMAND);

	return 0;
}

/* Single-shot analog output routine */

static int daqp_ao_insn_write(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	struct local_info_t *local = (struct local_info_t *) s->private;
	int d;
	unsigned int chan;

	if (local->stop) {
		return -EIO;
	}

	chan = CR_CHAN(insn->chanspec);
	d = data[0];
	d &= 0x0fff;
	d ^= 0x0800;		/* Flip the sign */
	d |= chan << 12;

	/* Make sure D/A update mode is direct update */
	outb(0, dev->iobase + DAQP_AUX);

	outw(d, dev->iobase + DAQP_DA);

	return 1;
}

/* Digital input routine */

static int daqp_di_insn_read(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	struct local_info_t *local = (struct local_info_t *) s->private;

	if (local->stop) {
		return -EIO;
	}

	data[0] = inb(dev->iobase + DAQP_DIGITAL_IO);

	return 1;
}

/* Digital output routine */

static int daqp_do_insn_write(struct comedi_device * dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	struct local_info_t *local = (struct local_info_t *) s->private;

	if (local->stop) {
		return -EIO;
	}

	outw(data[0] & 0xf, dev->iobase + DAQP_DIGITAL_IO);

	return 1;
}

/* daqp_attach is called via comedi_config to attach a comedi device
 * to a /dev/comedi*.  Note that this is different from daqp_cs_attach()
 * which is called by the pcmcia subsystem to attach the PCMCIA card
 * when it is inserted.
 */

static int daqp_attach(struct comedi_device * dev, struct comedi_devconfig * it)
{
	int ret;
	struct local_info_t *local = dev_table[it->options[0]];
	tuple_t tuple;
	int i;
	struct comedi_subdevice *s;

	if (it->options[0] < 0 || it->options[0] >= MAX_DEV || !local) {
		printk("comedi%d: No such daqp device %d\n",
			dev->minor, it->options[0]);
		return -EIO;
	}

	/* Typically brittle code that I don't completely understand,
	 * but "it works on my card".  The intent is to pull the model
	 * number of the card out the PCMCIA CIS and stash it away as
	 * the COMEDI board_name.  Looks like the third field in
	 * CISTPL_VERS_1 (offset 2) holds what we're looking for.  If
	 * it doesn't work, who cares, just leave it as "DAQP".
	 */

	strcpy(local->board_name, "DAQP");
	dev->board_name = local->board_name;

	tuple.DesiredTuple = CISTPL_VERS_1;
	if (pcmcia_get_first_tuple(local->link, &tuple) == 0) {
		u_char buf[128];

		buf[0] = buf[sizeof(buf) - 1] = 0;
		tuple.TupleData = buf;
		tuple.TupleDataMax = sizeof(buf);
		tuple.TupleOffset = 2;
		if (pcmcia_get_tuple_data(local->link, &tuple) == 0) {

			for (i = 0; i < tuple.TupleDataLen - 4; i++)
				if (buf[i] == 0)
					break;
			for (i++; i < tuple.TupleDataLen - 4; i++)
				if (buf[i] == 0)
					break;
			i++;
			if ((i < tuple.TupleDataLen - 4)
				&& (strncmp(buf + i, "DAQP", 4) == 0)) {
				strncpy(local->board_name, buf + i,
					sizeof(local->board_name));
			}
		}
	}

	dev->iobase = local->link->io.BasePort1;

	if ((ret = alloc_subdevices(dev, 4)) < 0)
		return ret;

	printk("comedi%d: attaching daqp%d (io 0x%04lx)\n",
		dev->minor, it->options[0], dev->iobase);

	s = dev->subdevices + 0;
	dev->read_subdev = s;
	s->private = local;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF | SDF_CMD_READ;
	s->n_chan = 8;
	s->len_chanlist = 2048;
	s->maxdata = 0xffff;
	s->range_table = &range_daqp_ai;
	s->insn_read = daqp_ai_insn_read;
	s->do_cmdtest = daqp_ai_cmdtest;
	s->do_cmd = daqp_ai_cmd;
	s->cancel = daqp_ai_cancel;

	s = dev->subdevices + 1;
	dev->write_subdev = s;
	s->private = local;
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 2;
	s->len_chanlist = 1;
	s->maxdata = 0x0fff;
	s->range_table = &range_daqp_ao;
	s->insn_write = daqp_ao_insn_write;

	s = dev->subdevices + 2;
	s->private = local;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 1;
	s->len_chanlist = 1;
	s->insn_read = daqp_di_insn_read;

	s = dev->subdevices + 3;
	s->private = local;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 1;
	s->len_chanlist = 1;
	s->insn_write = daqp_do_insn_write;

	return 1;
}

/* daqp_detach (called from comedi_comdig) does nothing. If the PCMCIA
 * card is removed, daqp_cs_detach() is called by the pcmcia subsystem.
 */

static int daqp_detach(struct comedi_device * dev)
{
	printk("comedi%d: detaching daqp\n", dev->minor);

	return 0;
}

/*====================================================================

    PCMCIA interface code

    The rest of the code in this file is based on dummy_cs.c v1.24
    from the Linux pcmcia_cs distribution v3.1.8 and is subject
    to the following license agreement.

    The remaining contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.

======================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card
   insertion and ejection events.

   Kernel version 2.6.16 upwards uses suspend() and resume() functions
   instead of an event() function.
*/

static void daqp_cs_config(struct pcmcia_device *link);
static void daqp_cs_release(struct pcmcia_device *link);
static int daqp_cs_suspend(struct pcmcia_device *p_dev);
static int daqp_cs_resume(struct pcmcia_device *p_dev);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static int daqp_cs_attach(struct pcmcia_device *);
static void daqp_cs_detach(struct pcmcia_device *);

/*
   The dev_info variable is the "key" that is used to match up this
   device driver with appropriate cards, through the card configuration
   database.
*/

static const dev_info_t dev_info = "quatech_daqp_cs";

/*======================================================================

    daqp_cs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.

======================================================================*/

static int daqp_cs_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;
	int i;

	DEBUG(0, "daqp_cs_attach()\n");

	for (i = 0; i < MAX_DEV; i++)
		if (dev_table[i] == NULL)
			break;
	if (i == MAX_DEV) {
		printk(KERN_NOTICE "daqp_cs: no devices available\n");
		return -ENODEV;
	}

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(struct local_info_t), GFP_KERNEL);
	if (!local)
		return -ENOMEM;

	local->table_index = i;
	dev_table[i] = local;
	local->link = link;
	link->priv = local;

	/* Interrupt setup */
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;
	link->irq.Handler = daqp_interrupt;
	link->irq.Instance = local;

	/*
	   General socket configuration defaults can go here.  In this
	   client, we assume very little, and rely on the CIS for almost
	   everything.  In most clients, many details (i.e., number, sizes,
	   and attributes of IO windows) are fixed by the nature of the
	   device, and can be hard-wired here.
	 */
	link->conf.Attributes = 0;
	link->conf.IntType = INT_MEMORY_AND_IO;

	daqp_cs_config(link);

	return 0;
}				/* daqp_cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void daqp_cs_detach(struct pcmcia_device *link)
{
	struct local_info_t *dev = link->priv;

	DEBUG(0, "daqp_cs_detach(0x%p)\n", link);

	if (link->dev_node) {
		dev->stop = 1;
		daqp_cs_release(link);
	}

	/* Unlink device structure, and free it */
	dev_table[dev->table_index] = NULL;
	if (dev)
		kfree(dev);

}				/* daqp_cs_detach */

/*======================================================================

    daqp_cs_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

static void daqp_cs_config(struct pcmcia_device *link)
{
	struct local_info_t *dev = link->priv;
	tuple_t tuple;
	cisparse_t parse;
	int last_ret;
	u_char buf[64];

	DEBUG(0, "daqp_cs_config(0x%p)\n", link);

	/*
	   This reads the card's CONFIG tuple to find its configuration
	   registers.
	 */
	tuple.DesiredTuple = CISTPL_CONFIG;
	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
	if ((last_ret = pcmcia_get_first_tuple(link, &tuple))) {
		cs_error(link, GetFirstTuple, last_ret);
		goto cs_failed;
	}
	if ((last_ret = pcmcia_get_tuple_data(link, &tuple))) {
		cs_error(link, GetTupleData, last_ret);
		goto cs_failed;
	}
	if ((last_ret = pcmcia_parse_tuple(&tuple, &parse))) {
		cs_error(link, ParseTuple, last_ret);
		goto cs_failed;
	}
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/*
	   In this loop, we scan the CIS for configuration table entries,
	   each of which describes a valid card configuration, including
	   voltage, IO window, memory window, and interrupt settings.

	   We make no assumptions about the card to be configured: we use
	   just the information available in the CIS.  In an ideal world,
	   this would work for any PCMCIA card, but it requires a complete
	   and accurate CIS.  In practice, a driver usually "knows" most of
	   these things without consulting the CIS, and most client drivers
	   will only use the CIS to fill in implementation-defined details.
	 */
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	if ((last_ret = pcmcia_get_first_tuple(link, &tuple))) {
		cs_error(link, GetFirstTuple, last_ret);
		goto cs_failed;
	}
	while (1) {
		cistpl_cftable_entry_t dflt = { 0 };
		cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
		if (pcmcia_get_tuple_data(link, &tuple))
			goto next_entry;
		if (pcmcia_parse_tuple(&tuple, &parse))
			goto next_entry;

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Do we need to allocate an interrupt? */
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
			link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			if (!(io->flags & CISTPL_IO_16BIT))
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 = link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
		}

		/* This reserves IO space but doesn't actually enable it */
		if (pcmcia_request_io(link, &link->io))
			goto next_entry;

		/* If we got this far, we're cool! */
		break;

	      next_entry:
		if ((last_ret = pcmcia_get_next_tuple(link, &tuple))) {
			cs_error(link, GetNextTuple, last_ret);
			goto cs_failed;
		}
	}

	/*
	   Allocate an interrupt line.  Note that this does not assign a
	   handler to the interrupt, unless the 'Handler' member of the
	   irq structure is initialized.
	 */
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		if ((last_ret = pcmcia_request_irq(link, &link->irq))) {
			cs_error(link, RequestIRQ, last_ret);
			goto cs_failed;
		}

	/*
	   This actually configures the PCMCIA socket -- setting up
	   the I/O windows and the interrupt mapping, and putting the
	   card and host interface into "Memory and IO" mode.
	 */
	if ((last_ret = pcmcia_request_configuration(link, &link->conf))) {
		cs_error(link, RequestConfiguration, last_ret);
		goto cs_failed;
	}

	/*
	   At this point, the dev_node_t structure(s) need to be
	   initialized and arranged in a linked list at link->dev.
	 */
	/* Comedi's PCMCIA script uses this device name (extracted
	 * from /var/lib/pcmcia/stab) to pass to comedi_config
	 */
	/* sprintf(dev->node.dev_name, "daqp%d", dev->table_index); */
	sprintf(dev->node.dev_name, "quatech_daqp_cs");
	dev->node.major = dev->node.minor = 0;
	link->dev_node = &dev->node;

	/* Finally, report what we've done */
	printk(KERN_INFO "%s: index 0x%02x",
		dev->node.dev_name, link->conf.ConfigIndex);
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		printk(", irq %u", link->irq.AssignedIRQ);
	if (link->io.NumPorts1)
		printk(", io 0x%04x-0x%04x", link->io.BasePort1,
			link->io.BasePort1 + link->io.NumPorts1 - 1);
	if (link->io.NumPorts2)
		printk(" & 0x%04x-0x%04x", link->io.BasePort2,
			link->io.BasePort2 + link->io.NumPorts2 - 1);
	printk("\n");

	return;

      cs_failed:
	daqp_cs_release(link);

}				/* daqp_cs_config */

static void daqp_cs_release(struct pcmcia_device *link)
{
	DEBUG(0, "daqp_cs_release(0x%p)\n", link);

	pcmcia_disable_device(link);
}				/* daqp_cs_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.

    When a CARD_REMOVAL event is received, we immediately set a
    private flag to block future accesses to this device.  All the
    functions that actually access the device should check this flag
    to make sure the card is still present.

======================================================================*/

static int daqp_cs_suspend(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	/* Mark the device as stopped, to block IO until later */
	local->stop = 1;
	return 0;
}

static int daqp_cs_resume(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	local->stop = 0;

	return 0;
}

/*====================================================================*/

#ifdef MODULE

static struct pcmcia_device_id daqp_cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x0137, 0x0027),
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, daqp_cs_id_table);

struct pcmcia_driver daqp_cs_driver = {
	.probe = daqp_cs_attach,
	.remove = daqp_cs_detach,
	.suspend = daqp_cs_suspend,
	.resume = daqp_cs_resume,
	.id_table = daqp_cs_id_table,
	.owner = THIS_MODULE,
	.drv = {
			.name = dev_info,
		},
};

int __init init_module(void)
{
	DEBUG(0, "%s\n", version);
	pcmcia_register_driver(&daqp_cs_driver);
	comedi_driver_register(&driver_daqp);
	return 0;
}

void __exit cleanup_module(void)
{
	DEBUG(0, "daqp_cs: unloading\n");
	comedi_driver_unregister(&driver_daqp);
	pcmcia_unregister_driver(&daqp_cs_driver);
}

#endif

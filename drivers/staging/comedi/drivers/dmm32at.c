/*
    comedi/drivers/dmm32at.c
    Diamond Systems mm32at code for a Comedi driver

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

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
Driver: dmm32at
Description: Diamond Systems mm32at driver.
Devices:
Author: Perry J. Piplani <perry.j.piplani@nasa.gov>
Updated: Fri Jun  4 09:13:24 CDT 2004
Status: experimental

This driver is for the Diamond Systems MM-32-AT board
http://www.diamondsystems.com/products/diamondmm32at It is being used
on serveral projects inside NASA, without problems so far. For analog
input commands, TRIG_EXT is not yet supported at all..

Configuration Options:
  comedi_config /dev/comedi0 dmm32at baseaddr,irq
*/

/*
 * The previous block comment is used to automatically generate
 * documentation in Comedi and Comedilib.  The fields:
 *
 * Driver: the name of the driver
 * Description: a short phrase describing the driver.  Don't list boards.
 * Devices: a full list of the boards that attempt to be supported by
 *   the driver.  Format is "(manufacturer) board name [comedi name]",
 *   where comedi_name is the name that is used to configure the board.
 *   See the comment near board_name: in the struct comedi_driver structure
 *   below.  If (manufacturer) or [comedi name] is missing, the previous
 *   value is used.
 * Author: you
 * Updated: date when the _documentation_ was last updated.  Use 'date -R'
 *   to get a value for this.
 * Status: a one-word description of the status.  Valid values are:
 *   works - driver works correctly on most boards supported, and
 *     passes comedi_test.
 *   unknown - unknown.  Usually put there by ds.
 *   experimental - may not work in any particular release.  Author
 *     probably wants assistance testing it.
 *   bitrotten - driver has not been update in a long time, probably
 *     doesn't work, and probably is missing support for significant
 *     Comedi interface features.
 *   untested - author probably wrote it "blind", and is believed to
 *     work, but no confirmation.
 *
 * These headers should be followed by a blank line, and any comments
 * you wish to say about the driver.  The comment area is the place
 * to put any known bugs, limitations, unsupported features, supported
 * command triggers, whether or not commands are supported on particular
 * subdevices, etc.
 *
 * Somewhere in the comment should be information about configuration
 * options that are used with comedi_config.
 */

#include <linux/interrupt.h>
#include "../comedidev.h"
#include <linux/ioport.h>

/* Board register addresses */

#define DMM32AT_MEMSIZE 0x10

#define DMM32AT_CONV 0x00
#define DMM32AT_AILSB 0x00
#define DMM32AT_AUXDOUT 0x01
#define DMM32AT_AIMSB 0x01
#define DMM32AT_AILOW 0x02
#define DMM32AT_AIHIGH 0x03

#define DMM32AT_DACLSB 0x04
#define DMM32AT_DACSTAT 0x04
#define DMM32AT_DACMSB 0x05

#define DMM32AT_FIFOCNTRL 0x07
#define DMM32AT_FIFOSTAT 0x07

#define DMM32AT_CNTRL 0x08
#define DMM32AT_AISTAT 0x08

#define DMM32AT_INTCLOCK 0x09

#define DMM32AT_CNTRDIO 0x0a

#define DMM32AT_AICONF 0x0b
#define DMM32AT_AIRBACK 0x0b

#define DMM32AT_CLK1 0x0d
#define DMM32AT_CLK2 0x0e
#define DMM32AT_CLKCT 0x0f

#define DMM32AT_DIOA 0x0c
#define DMM32AT_DIOB 0x0d
#define DMM32AT_DIOC 0x0e
#define DMM32AT_DIOCONF 0x0f

#define dmm_inb(cdev, reg) inb((cdev->iobase)+reg)
#define dmm_outb(cdev, reg, valu) outb(valu, (cdev->iobase)+reg)

/* Board register values. */

/* DMM32AT_DACSTAT 0x04 */
#define DMM32AT_DACBUSY 0x80

/* DMM32AT_FIFOCNTRL 0x07 */
#define DMM32AT_FIFORESET 0x02
#define DMM32AT_SCANENABLE 0x04

/* DMM32AT_CNTRL 0x08 */
#define DMM32AT_RESET 0x20
#define DMM32AT_INTRESET 0x08
#define DMM32AT_CLKACC 0x00
#define DMM32AT_DIOACC 0x01

/* DMM32AT_AISTAT 0x08 */
#define DMM32AT_STATUS 0x80

/* DMM32AT_INTCLOCK 0x09 */
#define DMM32AT_ADINT 0x80
#define DMM32AT_CLKSEL 0x03

/* DMM32AT_CNTRDIO 0x0a */
#define DMM32AT_FREQ12 0x80

/* DMM32AT_AICONF 0x0b */
#define DMM32AT_RANGE_U10 0x0c
#define DMM32AT_RANGE_U5 0x0d
#define DMM32AT_RANGE_B10 0x08
#define DMM32AT_RANGE_B5 0x00
#define DMM32AT_SCINT_20 0x00
#define DMM32AT_SCINT_15 0x10
#define DMM32AT_SCINT_10 0x20
#define DMM32AT_SCINT_5 0x30

/* DMM32AT_CLKCT 0x0f */
#define DMM32AT_CLKCT1 0x56	/* mode3 counter 1 - write low byte only */
#define DMM32AT_CLKCT2 0xb6	/*  mode3 counter 2 - write high and low byte */

/* DMM32AT_DIOCONF 0x0f */
#define DMM32AT_DIENABLE 0x80
#define DMM32AT_DIRA 0x10
#define DMM32AT_DIRB 0x02
#define DMM32AT_DIRCL 0x01
#define DMM32AT_DIRCH 0x08

/* board AI ranges in comedi structure */
static const struct comedi_lrange dmm32at_airanges = {
	4,
	{
	 UNI_RANGE(10),
	 UNI_RANGE(5),
	 BIP_RANGE(10),
	 BIP_RANGE(5),
	 }
};

/* register values for above ranges */
static const unsigned char dmm32at_rangebits[] = {
	DMM32AT_RANGE_U10,
	DMM32AT_RANGE_U5,
	DMM32AT_RANGE_B10,
	DMM32AT_RANGE_B5,
};

/* only one of these ranges is valid, as set by a jumper on the
 * board. The application should only use the range set by the jumper
 */
static const struct comedi_lrange dmm32at_aoranges = {
	4,
	{
	 UNI_RANGE(10),
	 UNI_RANGE(5),
	 BIP_RANGE(10),
	 BIP_RANGE(5),
	 }
};

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
struct dmm32at_board {
	const char *name;
	int ai_chans;
	int ai_bits;
	const struct comedi_lrange *ai_ranges;
	int ao_chans;
	int ao_bits;
	const struct comedi_lrange *ao_ranges;
	int have_dio;
	int dio_chans;
};
static const struct dmm32at_board dmm32at_boards[] = {
	{
	 .name = "dmm32at",
	 .ai_chans = 32,
	 .ai_bits = 16,
	 .ai_ranges = &dmm32at_airanges,
	 .ao_chans = 4,
	 .ao_bits = 12,
	 .ao_ranges = &dmm32at_aoranges,
	 .have_dio = 1,
	 .dio_chans = 24,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct dmm32at_board *)dev->board_ptr)

/* this structure is for data unique to this hardware driver.  If
 * several hardware drivers keep similar information in this structure,
 * feel free to suggest moving the variable to the struct comedi_device struct.
 */
struct dmm32at_private {

	int data;
	int ai_inuse;
	unsigned int ai_scans_left;

	/* Used for AO readback */
	unsigned int ao_readback[4];
	unsigned char dio_config;

};

/*
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct dmm32at_private *)dev->private)

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int dmm32at_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static int dmm32at_detach(struct comedi_device *dev);
static struct comedi_driver driver_dmm32at = {
	.driver_name = "dmm32at",
	.module = THIS_MODULE,
	.attach = dmm32at_attach,
	.detach = dmm32at_detach,
/* It is not necessary to implement the following members if you are
 * writing a driver for a ISA PnP or PCI card */
/* Most drivers will support multiple types of boards by
 * having an array of board structures.  These were defined
 * in dmm32at_boards[] above.  Note that the element 'name'
 * was first in the structure -- Comedi uses this fact to
 * extract the name of the board without knowing any details
 * about the structure except for its length.
 * When a device is attached (by comedi_config), the name
 * of the device is given to Comedi, and Comedi tries to
 * match it by going through the list of board names.  If
 * there is a match, the address of the pointer is put
 * into dev->board_ptr and driver->attach() is called.
 *
 * Note that these are not necessary if you can determine
 * the type of board in software.  ISA PnP, PCI, and PCMCIA
 * devices are such boards.
 */
	.board_name = &dmm32at_boards[0].name,
	.offset = sizeof(struct dmm32at_board),
	.num_names = ARRAY_SIZE(dmm32at_boards),
};

/* prototypes for driver functions below */
static int dmm32at_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);
static int dmm32at_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);
static int dmm32at_ao_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data);
static int dmm32at_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);
static int dmm32at_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data);
static int dmm32at_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd);
static int dmm32at_ai_cmd(struct comedi_device *dev,
			  struct comedi_subdevice *s);
static int dmm32at_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s);
static int dmm32at_ns_to_timer(unsigned int *ns, int round);
static irqreturn_t dmm32at_isr(int irq, void *d);
void dmm32at_setaitimer(struct comedi_device *dev, unsigned int nansec);

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int dmm32at_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	int ret;
	struct comedi_subdevice *s;
	unsigned char aihi, ailo, fifostat, aistat, intstat, airback;
	unsigned long iobase;
	unsigned int irq;

	iobase = it->options[0];
	irq = it->options[1];

	printk("comedi%d: dmm32at: attaching\n", dev->minor);
	printk("dmm32at: probing at address 0x%04lx, irq %u\n", iobase, irq);

	/* register address space */
	if (!request_region(iobase, DMM32AT_MEMSIZE, thisboard->name)) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

	/* the following just makes sure the board is there and gets
	   it to a known state */

	/* reset the board */
	dmm_outb(dev, DMM32AT_CNTRL, DMM32AT_RESET);

	/* allow a millisecond to reset */
	udelay(1000);

	/* zero scan and fifo control */
	dmm_outb(dev, DMM32AT_FIFOCNTRL, 0x0);

	/* zero interrupt and clock control */
	dmm_outb(dev, DMM32AT_INTCLOCK, 0x0);

	/* write a test channel range, the high 3 bits should drop */
	dmm_outb(dev, DMM32AT_AILOW, 0x80);
	dmm_outb(dev, DMM32AT_AIHIGH, 0xff);

	/* set the range at 10v unipolar */
	dmm_outb(dev, DMM32AT_AICONF, DMM32AT_RANGE_U10);

	/* should take 10 us to settle, here's a hundred */
	udelay(100);

	/* read back the values */
	ailo = dmm_inb(dev, DMM32AT_AILOW);
	aihi = dmm_inb(dev, DMM32AT_AIHIGH);
	fifostat = dmm_inb(dev, DMM32AT_FIFOSTAT);
	aistat = dmm_inb(dev, DMM32AT_AISTAT);
	intstat = dmm_inb(dev, DMM32AT_INTCLOCK);
	airback = dmm_inb(dev, DMM32AT_AIRBACK);

	printk("dmm32at: lo=0x%02x hi=0x%02x fifostat=0x%02x\n",
	       ailo, aihi, fifostat);
	printk("dmm32at: aistat=0x%02x intstat=0x%02x airback=0x%02x\n",
	       aistat, intstat, airback);

	if ((ailo != 0x00) || (aihi != 0x1f) || (fifostat != 0x80) ||
	    (aistat != 0x60 || (intstat != 0x00) || airback != 0x0c)) {
		printk("dmmat32: board detection failed\n");
		return -EIO;
	}

	/* board is there, register interrupt */
	if (irq) {
		ret = request_irq(irq, dmm32at_isr, 0, thisboard->name, dev);
		if (ret < 0) {
			printk("irq conflict\n");
			return ret;
		}
		dev->irq = irq;
	}

/*
 * If you can probe the device to determine what device in a series
 * it is, this is the place to do it.  Otherwise, dev->board_ptr
 * should already be initialized.
 */
	/* dev->board_ptr = dmm32at_probe(dev); */

/*
 * Initialize dev->board_name.  Note that we can use the "thisboard"
 * macro now, since we just initialized it in the last line.
 */
	dev->board_name = thisboard->name;

/*
 * Allocate the private structure area.  alloc_private() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_private(dev, sizeof(struct dmm32at_private)) < 0)
		return -ENOMEM;

/*
 * Allocate the subdevice structures.  alloc_subdevice() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_subdevices(dev, 3) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	dev->read_subdev = s;
	/* analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	/* we support single-ended (ground) and differential */
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF | SDF_CMD_READ;
	s->n_chan = thisboard->ai_chans;
	s->maxdata = (1 << thisboard->ai_bits) - 1;
	s->range_table = thisboard->ai_ranges;
	s->len_chanlist = 32;	/* This is the maximum chanlist length that
				   the board can handle */
	s->insn_read = dmm32at_ai_rinsn;
	s->do_cmd = dmm32at_ai_cmd;
	s->do_cmdtest = dmm32at_ai_cmdtest;
	s->cancel = dmm32at_ai_cancel;

	s = dev->subdevices + 1;
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = thisboard->ao_chans;
	s->maxdata = (1 << thisboard->ao_bits) - 1;
	s->range_table = thisboard->ao_ranges;
	s->insn_write = dmm32at_ao_winsn;
	s->insn_read = dmm32at_ao_rinsn;

	s = dev->subdevices + 2;
	/* digital i/o subdevice */
	if (thisboard->have_dio) {

		/* get access to the DIO regs */
		dmm_outb(dev, DMM32AT_CNTRL, DMM32AT_DIOACC);
		/* set the DIO's to the defualt input setting */
		devpriv->dio_config = DMM32AT_DIRA | DMM32AT_DIRB |
		    DMM32AT_DIRCL | DMM32AT_DIRCH | DMM32AT_DIENABLE;
		dmm_outb(dev, DMM32AT_DIOCONF, devpriv->dio_config);

		/* set up the subdevice */
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan = thisboard->dio_chans;
		s->maxdata = 1;
		s->state = 0;
		s->range_table = &range_digital;
		s->insn_bits = dmm32at_dio_insn_bits;
		s->insn_config = dmm32at_dio_insn_config;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* success */
	printk("comedi%d: dmm32at: attached\n", dev->minor);

	return 1;

}

/*
 * _detach is called to deconfigure a device.  It should deallocate
 * resources.
 * This function is also called when _attach() fails, so it should be
 * careful not to release resources that were not necessarily
 * allocated by _attach().  dev->private and dev->subdevices are
 * deallocated automatically by the core.
 */
static int dmm32at_detach(struct comedi_device *dev)
{
	printk("comedi%d: dmm32at: remove\n", dev->minor);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->iobase)
		release_region(dev->iobase, DMM32AT_MEMSIZE);

	return 0;
}

/*
 * "instructions" read/write data in "one-shot" or "software-triggered"
 * mode.
 */

static int dmm32at_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	int n, i;
	unsigned int d;
	unsigned char status;
	unsigned short msb, lsb;
	unsigned char chan;
	int range;

	/* get the channel and range number */

	chan = CR_CHAN(insn->chanspec) & (s->n_chan - 1);
	range = CR_RANGE(insn->chanspec);

	/* printk("channel=0x%02x, range=%d\n",chan,range); */

	/* zero scan and fifo control and reset fifo */
	dmm_outb(dev, DMM32AT_FIFOCNTRL, DMM32AT_FIFORESET);

	/* write the ai channel range regs */
	dmm_outb(dev, DMM32AT_AILOW, chan);
	dmm_outb(dev, DMM32AT_AIHIGH, chan);
	/* set the range bits */
	dmm_outb(dev, DMM32AT_AICONF, dmm32at_rangebits[range]);

	/* wait for circuit to settle */
	for (i = 0; i < 40000; i++) {
		status = dmm_inb(dev, DMM32AT_AIRBACK);
		if ((status & DMM32AT_STATUS) == 0)
			break;
	}
	if (i == 40000) {
		printk("timeout\n");
		return -ETIMEDOUT;
	}

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		dmm_outb(dev, DMM32AT_CONV, 0xff);
		/* wait for conversion to end */
		for (i = 0; i < 40000; i++) {
			status = dmm_inb(dev, DMM32AT_AISTAT);
			if ((status & DMM32AT_STATUS) == 0)
				break;
		}
		if (i == 40000) {
			printk("timeout\n");
			return -ETIMEDOUT;
		}

		/* read data */
		lsb = dmm_inb(dev, DMM32AT_AILSB);
		msb = dmm_inb(dev, DMM32AT_AIMSB);

		/* invert sign bit to make range unsigned, this is an
		   idiosyncracy of the diamond board, it return
		   conversions as a signed value, i.e. -32768 to
		   32767, flipping the bit and interpreting it as
		   signed gives you a range of 0 to 65535 which is
		   used by comedi */
		d = ((msb ^ 0x0080) << 8) + lsb;

		data[n] = d;
	}

	/* return the number of samples read/written */
	return n;
}

static int dmm32at_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;
	int start_chan, gain, i;

	/* printk("dmmat32 in command test\n"); */

	/* cmdtest tests a particular command to see if it is valid.
	 * Using the cmdtest ioctl, a user can create a valid cmd
	 * and then have it executes by the cmd ioctl.
	 *
	 * cmdtest returns 1,2,3,4 or 0, depending on which tests
	 * the command passes. */

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_TIMER /*| TRIG_EXT */ ;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER /*| TRIG_EXT */ ;
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

	/* note that mutual compatibility is not an issue here */
	if (cmd->scan_begin_src != TRIG_TIMER &&
	    cmd->scan_begin_src != TRIG_EXT)
		err++;
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_EXT)
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
#define MAX_SCAN_SPEED	1000000	/* in nanoseconds */
#define MIN_SCAN_SPEED	1000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->scan_begin_arg < MAX_SCAN_SPEED) {
			cmd->scan_begin_arg = MAX_SCAN_SPEED;
			err++;
		}
		if (cmd->scan_begin_arg > MIN_SCAN_SPEED) {
			cmd->scan_begin_arg = MIN_SCAN_SPEED;
			err++;
		}
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
		if (cmd->scan_begin_arg > 9) {
			cmd->scan_begin_arg = 9;
			err++;
		}
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg >= 17500)
			cmd->convert_arg = 20000;
		else if (cmd->convert_arg >= 12500)
			cmd->convert_arg = 15000;
		else if (cmd->convert_arg >= 7500)
			cmd->convert_arg = 10000;
		else
			cmd->convert_arg = 5000;

	} else {
		/* external trigger */
		/* see above */
		if (cmd->convert_arg > 9) {
			cmd->convert_arg = 9;
			err++;
		}
	}

	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (cmd->stop_arg > 0xfffffff0) {
			cmd->stop_arg = 0xfffffff0;
			err++;
		}
		if (cmd->stop_arg == 0) {
			cmd->stop_arg = 1;
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
		dmm32at_ns_to_timer(&cmd->scan_begin_arg,
				    cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		dmm32at_ns_to_timer(&cmd->convert_arg,
				    cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
		if (cmd->scan_begin_src == TRIG_TIMER &&
		    cmd->scan_begin_arg <
		    cmd->convert_arg * cmd->scan_end_arg) {
			cmd->scan_begin_arg =
			    cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	}

	if (err)
		return 4;

	/* step 5 check the channel list, the channel list for this
	   board must be consecutive and gains must be the same */

	if (cmd->chanlist) {
		gain = CR_RANGE(cmd->chanlist[0]);
		start_chan = CR_CHAN(cmd->chanlist[0]);
		for (i = 1; i < cmd->chanlist_len; i++) {
			if (CR_CHAN(cmd->chanlist[i]) !=
			    (start_chan + i) % s->n_chan) {
				comedi_error(dev,
					     "entries in chanlist must be consecutive channels, counting upwards\n");
				err++;
			}
			if (CR_RANGE(cmd->chanlist[i]) != gain) {
				comedi_error(dev,
					     "entries in chanlist must all have the same gain\n");
				err++;
			}
		}
	}

	if (err)
		return 5;

	return 0;
}

static int dmm32at_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	int i, range;
	unsigned char chanlo, chanhi, status;

	if (!cmd->chanlist)
		return -EINVAL;

	/* get the channel list and range */
	chanlo = CR_CHAN(cmd->chanlist[0]) & (s->n_chan - 1);
	chanhi = chanlo + cmd->chanlist_len - 1;
	if (chanhi >= s->n_chan)
		return -EINVAL;
	range = CR_RANGE(cmd->chanlist[0]);

	/* reset fifo */
	dmm_outb(dev, DMM32AT_FIFOCNTRL, DMM32AT_FIFORESET);

	/* set scan enable */
	dmm_outb(dev, DMM32AT_FIFOCNTRL, DMM32AT_SCANENABLE);

	/* write the ai channel range regs */
	dmm_outb(dev, DMM32AT_AILOW, chanlo);
	dmm_outb(dev, DMM32AT_AIHIGH, chanhi);

	/* set the range bits */
	dmm_outb(dev, DMM32AT_AICONF, dmm32at_rangebits[range]);

	/* reset the interrupt just in case */
	dmm_outb(dev, DMM32AT_CNTRL, DMM32AT_INTRESET);

	if (cmd->stop_src == TRIG_COUNT)
		devpriv->ai_scans_left = cmd->stop_arg;
	else {			/* TRIG_NONE */
		devpriv->ai_scans_left = 0xffffffff;	/* indicates TRIG_NONE to isr */
	}

	/* wait for circuit to settle */
	for (i = 0; i < 40000; i++) {
		status = dmm_inb(dev, DMM32AT_AIRBACK);
		if ((status & DMM32AT_STATUS) == 0)
			break;
	}
	if (i == 40000) {
		printk("timeout\n");
		return -ETIMEDOUT;
	}

	if (devpriv->ai_scans_left > 1) {
		/* start the clock and enable the interrupts */
		dmm32at_setaitimer(dev, cmd->scan_begin_arg);
	} else {
		/* start the interrups and initiate a single scan */
		dmm_outb(dev, DMM32AT_INTCLOCK, DMM32AT_ADINT);
		dmm_outb(dev, DMM32AT_CONV, 0xff);
	}

/* 	printk("dmmat32 in command\n"); */

/* 	for(i=0;i<cmd->chanlist_len;i++) */
/* 		comedi_buf_put(s->async,i*100); */

/* 	s->async->events |= COMEDI_CB_EOA; */
/* 	comedi_event(dev, s); */

	return 0;

}

static int dmm32at_ai_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	devpriv->ai_scans_left = 1;
	return 0;
}

static irqreturn_t dmm32at_isr(int irq, void *d)
{
	unsigned char intstat;
	unsigned int samp;
	unsigned short msb, lsb;
	int i;
	struct comedi_device *dev = d;

	if (!dev->attached) {
		comedi_error(dev, "spurious interrupt");
		return IRQ_HANDLED;
	}

	intstat = dmm_inb(dev, DMM32AT_INTCLOCK);

	if (intstat & DMM32AT_ADINT) {
		struct comedi_subdevice *s = dev->read_subdev;
		struct comedi_cmd *cmd = &s->async->cmd;

		for (i = 0; i < cmd->chanlist_len; i++) {
			/* read data */
			lsb = dmm_inb(dev, DMM32AT_AILSB);
			msb = dmm_inb(dev, DMM32AT_AIMSB);

			/* invert sign bit to make range unsigned */
			samp = ((msb ^ 0x0080) << 8) + lsb;
			comedi_buf_put(s->async, samp);
		}

		if (devpriv->ai_scans_left != 0xffffffff) {	/* TRIG_COUNT */
			devpriv->ai_scans_left--;
			if (devpriv->ai_scans_left == 0) {
				/* disable further interrupts and clocks */
				dmm_outb(dev, DMM32AT_INTCLOCK, 0x0);
				/* set the buffer to be flushed with an EOF */
				s->async->events |= COMEDI_CB_EOA;
			}

		}
		/* flush the buffer */
		comedi_event(dev, s);
	}

	/* reset the interrupt */
	dmm_outb(dev, DMM32AT_CNTRL, DMM32AT_INTRESET);
	return IRQ_HANDLED;
}

/* This function doesn't require a particular form, this is just
 * what happens to be used in some of the drivers.  It should
 * convert ns nanoseconds to a counter value suitable for programming
 * the device.  Also, it should adjust ns so that it cooresponds to
 * the actual time that the device will use. */
static int dmm32at_ns_to_timer(unsigned int *ns, int round)
{
	/* trivial timer */
	/* if your timing is done through two cascaded timers, the
	 * i8253_cascade_ns_to_timer() function in 8253.h can be
	 * very helpful.  There are also i8254_load() and i8254_mm_load()
	 * which can be used to load values into the ubiquitous 8254 counters
	 */

	return *ns;
}

static int dmm32at_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned char hi, lo, status;

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; i++) {

		devpriv->ao_readback[chan] = data[i];

		/* get the low byte */
		lo = data[i] & 0x00ff;
		/* high byte also contains channel number */
		hi = (data[i] >> 8) + chan * (1 << 6);
		/* printk("writing 0x%02x  0x%02x\n",hi,lo); */
		/* write the low and high values to the board */
		dmm_outb(dev, DMM32AT_DACLSB, lo);
		dmm_outb(dev, DMM32AT_DACMSB, hi);

		/* wait for circuit to settle */
		for (i = 0; i < 40000; i++) {
			status = dmm_inb(dev, DMM32AT_DACSTAT);
			if ((status & DMM32AT_DACBUSY) == 0)
				break;
		}
		if (i == 40000) {
			printk("timeout\n");
			return -ETIMEDOUT;
		}
		/* dummy read to update trigger the output */
		status = dmm_inb(dev, DMM32AT_DACMSB);

	}

	/* return the number of samples read/written */
	return i;
}

/* AO subdevices should have a read insn as well as a write insn.
 * Usually this means copying a value stored in devpriv. */
static int dmm32at_ao_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

/* DIO devices are slightly special.  Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels.  The
 * comedi core can convert between insn_bits and insn_read/write */
static int dmm32at_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	unsigned char diobits;

	if (insn->n != 2)
		return -EINVAL;

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		/* Write out the new digital output lines */
		/* outw(s->state,dev->iobase + DMM32AT_DIO); */
	}

	/* get access to the DIO regs */
	dmm_outb(dev, DMM32AT_CNTRL, DMM32AT_DIOACC);

	/* if either part of dio is set for output */
	if (((devpriv->dio_config & DMM32AT_DIRCL) == 0) ||
	    ((devpriv->dio_config & DMM32AT_DIRCH) == 0)) {
		diobits = (s->state & 0x00ff0000) >> 16;
		dmm_outb(dev, DMM32AT_DIOC, diobits);
	}
	if ((devpriv->dio_config & DMM32AT_DIRB) == 0) {
		diobits = (s->state & 0x0000ff00) >> 8;
		dmm_outb(dev, DMM32AT_DIOB, diobits);
	}
	if ((devpriv->dio_config & DMM32AT_DIRA) == 0) {
		diobits = (s->state & 0x000000ff);
		dmm_outb(dev, DMM32AT_DIOA, diobits);
	}

	/* now read the state back in */
	s->state = dmm_inb(dev, DMM32AT_DIOC);
	s->state <<= 8;
	s->state |= dmm_inb(dev, DMM32AT_DIOB);
	s->state <<= 8;
	s->state |= dmm_inb(dev, DMM32AT_DIOA);
	data[1] = s->state;

	/* on return, data[1] contains the value of the digital
	 * input and output lines. */
	/* data[1]=inw(dev->iobase + DMM32AT_DIO); */
	/* or we could just return the software copy of the output values if
	 * it was a purely digital output subdevice */
	/* data[1]=s->state; */

	return 2;
}

static int dmm32at_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	unsigned char chanbit;
	int chan = CR_CHAN(insn->chanspec);

	if (insn->n != 1)
		return -EINVAL;

	if (chan < 8)
		chanbit = DMM32AT_DIRA;
	else if (chan < 16)
		chanbit = DMM32AT_DIRB;
	else if (chan < 20)
		chanbit = DMM32AT_DIRCL;
	else
		chanbit = DMM32AT_DIRCH;

	/* The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value COMEDI_INPUT or COMEDI_OUTPUT. */

	/* if output clear the bit, otherwise set it */
	if (data[0] == COMEDI_OUTPUT)
		devpriv->dio_config &= ~chanbit;
	else
		devpriv->dio_config |= chanbit;
	/* get access to the DIO regs */
	dmm_outb(dev, DMM32AT_CNTRL, DMM32AT_DIOACC);
	/* set the DIO's to the new configuration setting */
	dmm_outb(dev, DMM32AT_DIOCONF, devpriv->dio_config);

	return 1;
}

void dmm32at_setaitimer(struct comedi_device *dev, unsigned int nansec)
{
	unsigned char lo1, lo2, hi2;
	unsigned short both2;

	/* based on 10mhz clock */
	lo1 = 200;
	both2 = nansec / 20000;
	hi2 = (both2 & 0xff00) >> 8;
	lo2 = both2 & 0x00ff;

	/* set the counter frequency to 10mhz */
	dmm_outb(dev, DMM32AT_CNTRDIO, 0);

	/* get access to the clock regs */
	dmm_outb(dev, DMM32AT_CNTRL, DMM32AT_CLKACC);

	/* write the counter 1 control word and low byte to counter */
	dmm_outb(dev, DMM32AT_CLKCT, DMM32AT_CLKCT1);
	dmm_outb(dev, DMM32AT_CLK1, lo1);

	/* write the counter 2 control word and low byte then to counter */
	dmm_outb(dev, DMM32AT_CLKCT, DMM32AT_CLKCT2);
	dmm_outb(dev, DMM32AT_CLK2, lo2);
	dmm_outb(dev, DMM32AT_CLK2, hi2);

	/* enable the ai conversion interrupt and the clock to start scans */
	dmm_outb(dev, DMM32AT_INTCLOCK, DMM32AT_ADINT | DMM32AT_CLKSEL);

}

/*
 * A convenient macro that defines init_module() and cleanup_module(),
 * as necessary.
 */
COMEDI_INITCLEANUP(driver_dmm32at);

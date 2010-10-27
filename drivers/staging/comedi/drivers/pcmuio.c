/*
    comedi/drivers/pcmuio.c
    Driver for Winsystems PC-104 based 48-channel and 96-channel DIO boards.

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2006 Calin A. Culianu <calin@ajvar.org>

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
Driver: pcmuio
Description: A driver for the PCM-UIO48A and PCM-UIO96A boards from Winsystems.
Devices: [Winsystems] PCM-UIO48A (pcmuio48), PCM-UIO96A (pcmuio96)
Author: Calin Culianu <calin@ajvar.org>
Updated: Fri, 13 Jan 2006 12:01:01 -0500
Status: works

A driver for the relatively straightforward-to-program PCM-UIO48A and
PCM-UIO96A boards from Winsystems.  These boards use either one or two
(in the 96-DIO version) WS16C48 ASIC HighDensity I/O Chips (HDIO).
This chip is interesting in that each I/O line is individually
programmable for INPUT or OUTPUT (thus comedi_dio_config can be done
on a per-channel basis).  Also, each chip supports edge-triggered
interrupts for the first 24 I/O lines.  Of course, since the
96-channel version of the board has two ASICs, it can detect polarity
changes on up to 48 I/O lines.  Since this is essentially an (non-PnP)
ISA board, I/O Address and IRQ selection are done through jumpers on
the board.  You need to pass that information to this driver as the
first and second comedi_config option, respectively.  Note that the
48-channel version uses 16 bytes of IO memory and the 96-channel
version uses 32-bytes (in case you are worried about conflicts).  The
48-channel board is split into two 24-channel comedi subdevices.
The 96-channel board is split into 4 24-channel DIO subdevices.

Note that IRQ support has been added, but it is untested.

To use edge-detection IRQ support, pass the IRQs of both ASICS
(for the 96 channel version) or just 1 ASIC (for 48-channel version).
Then, use use comedi_commands with TRIG_NOW.
Your callback will be called each time an edge is triggered, and the data
values will be two sample_t's, which should be concatenated to form one
32-bit unsigned int.  This value is the mask of channels that had
edges detected from your channel list.  Note that the bits positions
in the mask correspond to positions in your chanlist when you specified
the command and *not* channel id's!

To set the polarity of the edge-detection interrupts pass a nonzero value for
either CR_RANGE or CR_AREF for edge-up polarity, or a zero value for both
CR_RANGE and CR_AREF if you want edge-down polarity.

In the 48-channel version:

On subdev 0, the first 24 channels channels are edge-detect channels.

In the 96-channel board you have the collowing channels that can do edge detection:

subdev 0, channels 0-24  (first 24 channels of 1st ASIC)
subdev 2, channels 0-24  (first 24 channels of 2nd ASIC)

Configuration Options:
  [0] - I/O port base address
  [1] - IRQ (for first ASIC, or first 24 channels)
  [2] - IRQ for second ASIC (pcmuio96 only - IRQ for chans 48-72 .. can be the same as first irq!)
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"
#include "pcm_common.h"

#include <linux/pci.h>		/* for PCI devices */

#define CHANS_PER_PORT   8
#define PORTS_PER_ASIC   6
#define INTR_PORTS_PER_ASIC   3
#define MAX_CHANS_PER_SUBDEV 24	/* number of channels per comedi subdevice */
#define PORTS_PER_SUBDEV (MAX_CHANS_PER_SUBDEV/CHANS_PER_PORT)
#define CHANS_PER_ASIC (CHANS_PER_PORT*PORTS_PER_ASIC)
#define INTR_CHANS_PER_ASIC 24
#define INTR_PORTS_PER_SUBDEV (INTR_CHANS_PER_ASIC/CHANS_PER_PORT)
#define MAX_DIO_CHANS   (PORTS_PER_ASIC*2*CHANS_PER_PORT)
#define MAX_ASICS       (MAX_DIO_CHANS/CHANS_PER_ASIC)
#define SDEV_NO ((int)(s - dev->subdevices))
#define CALC_N_SUBDEVS(nchans) ((nchans)/MAX_CHANS_PER_SUBDEV + (!!((nchans)%MAX_CHANS_PER_SUBDEV)) /*+ (nchans > INTR_CHANS_PER_ASIC ? 2 : 1)*/)
/* IO Memory sizes */
#define ASIC_IOSIZE (0x10)
#define PCMUIO48_IOSIZE ASIC_IOSIZE
#define PCMUIO96_IOSIZE (ASIC_IOSIZE*2)

/* Some offsets - these are all in the 16byte IO memory offset from
   the base address.  Note that there is a paging scheme to swap out
   offsets 0x8-0xA using the PAGELOCK register.  See the table below.

  Register(s)       Pages        R/W?        Description
  --------------------------------------------------------------
  REG_PORTx         All          R/W         Read/Write/Configure IO
  REG_INT_PENDING   All          ReadOnly    Quickly see which INT_IDx has int.
  REG_PAGELOCK      All          WriteOnly   Select a page
  REG_POLx          Pg. 1 only   WriteOnly   Select edge-detection polarity
  REG_ENABx         Pg. 2 only   WriteOnly   Enable/Disable edge-detect. int.
  REG_INT_IDx       Pg. 3 only   R/W         See which ports/bits have ints.
 */
#define REG_PORT0 0x0
#define REG_PORT1 0x1
#define REG_PORT2 0x2
#define REG_PORT3 0x3
#define REG_PORT4 0x4
#define REG_PORT5 0x5
#define REG_INT_PENDING 0x6
#define REG_PAGELOCK 0x7	/* page selector register, upper 2 bits select a page
				   and bits 0-5 are used to 'lock down' a particular
				   port above to make it readonly.  */
#define REG_POL0 0x8
#define REG_POL1 0x9
#define REG_POL2 0xA
#define REG_ENAB0 0x8
#define REG_ENAB1 0x9
#define REG_ENAB2 0xA
#define REG_INT_ID0 0x8
#define REG_INT_ID1 0x9
#define REG_INT_ID2 0xA

#define NUM_PAGED_REGS 3
#define NUM_PAGES 4
#define FIRST_PAGED_REG 0x8
#define REG_PAGE_BITOFFSET 6
#define REG_LOCK_BITOFFSET 0
#define REG_PAGE_MASK (~((0x1<<REG_PAGE_BITOFFSET)-1))
#define REG_LOCK_MASK ~(REG_PAGE_MASK)
#define PAGE_POL 1
#define PAGE_ENAB 2
#define PAGE_INT_ID 3

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
struct pcmuio_board {
	const char *name;
	const int num_asics;
	const int num_channels_per_port;
	const int num_ports;
};

static const struct pcmuio_board pcmuio_boards[] = {
	{
	 .name = "pcmuio48",
	 .num_asics = 1,
	 .num_ports = 6,
	 },
	{
	 .name = "pcmuio96",
	 .num_asics = 2,
	 .num_ports = 12,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct pcmuio_board *)dev->board_ptr)

/* this structure is for data unique to this subdevice.  */
struct pcmuio_subdev_private {
	/* mapping of halfwords (bytes) in port/chanarray to iobase */
	unsigned long iobases[PORTS_PER_SUBDEV];

	/* The below is only used for intr subdevices */
	struct {
		int asic;	/* if non-negative, this subdev has an interrupt asic */
		int first_chan;	/* if nonnegative, the first channel id for
				   interrupts. */
		int num_asic_chans;	/* the number of asic channels in this subdev
					   that have interrutps */
		int asic_chan;	/* if nonnegative, the first channel id with
				   respect to the asic that has interrupts */
		int enabled_mask;	/* subdev-relative channel mask for channels
					   we are interested in */
		int active;
		int stop_count;
		int continuous;
		spinlock_t spinlock;
	} intr;
};

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
struct pcmuio_private {
	struct {
		unsigned char pagelock;	/* current page and lock */
		unsigned char pol[NUM_PAGED_REGS];	/* shadow of POLx registers */
		unsigned char enab[NUM_PAGED_REGS];	/* shadow of ENABx registers */
		int num;
		unsigned long iobase;
		unsigned int irq;
		spinlock_t spinlock;
	} asics[MAX_ASICS];
	struct pcmuio_subdev_private *sprivs;
};

/*
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct pcmuio_private *)dev->private)
#define subpriv ((struct pcmuio_subdev_private *)s->private)
/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int pcmuio_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static int pcmuio_detach(struct comedi_device *dev);

static struct comedi_driver driver = {
	.driver_name = "pcmuio",
	.module = THIS_MODULE,
	.attach = pcmuio_attach,
	.detach = pcmuio_detach,
/* It is not necessary to implement the following members if you are
 * writing a driver for a ISA PnP or PCI card */
	/* Most drivers will support multiple types of boards by
	 * having an array of board structures.  These were defined
	 * in pcmuio_boards[] above.  Note that the element 'name'
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
	.board_name = &pcmuio_boards[0].name,
	.offset = sizeof(struct pcmuio_board),
	.num_names = ARRAY_SIZE(pcmuio_boards),
};

static int pcmuio_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data);
static int pcmuio_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data);

static irqreturn_t interrupt_pcmuio(int irq, void *d);
static void pcmuio_stop_intr(struct comedi_device *, struct comedi_subdevice *);
static int pcmuio_cancel(struct comedi_device *dev, struct comedi_subdevice *s);
static int pcmuio_cmd(struct comedi_device *dev, struct comedi_subdevice *s);
static int pcmuio_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_cmd *cmd);

/* some helper functions to deal with specifics of this device's registers */
static void init_asics(struct comedi_device *dev);	/* sets up/clears ASIC chips to defaults */
static void switch_page(struct comedi_device *dev, int asic, int page);
#ifdef notused
static void lock_port(struct comedi_device *dev, int asic, int port);
static void unlock_port(struct comedi_device *dev, int asic, int port);
#endif

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int pcmuio_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int sdev_no, chans_left, n_subdevs, port, asic, thisasic_chanct = 0;
	unsigned long iobase;
	unsigned int irq[MAX_ASICS];

	iobase = it->options[0];
	irq[0] = it->options[1];
	irq[1] = it->options[2];

	printk("comedi%d: %s: io: %lx ", dev->minor, driver.driver_name,
	       iobase);

	dev->iobase = iobase;

	if (!iobase || !request_region(iobase,
				       thisboard->num_asics * ASIC_IOSIZE,
				       driver.driver_name)) {
		printk("I/O port conflict\n");
		return -EIO;
	}

/*
 * Initialize dev->board_name.  Note that we can use the "thisboard"
 * macro now, since we just initialized it in the last line.
 */
	dev->board_name = thisboard->name;

/*
 * Allocate the private structure area.  alloc_private() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_private(dev, sizeof(struct pcmuio_private)) < 0) {
		printk("cannot allocate private data structure\n");
		return -ENOMEM;
	}

	for (asic = 0; asic < MAX_ASICS; ++asic) {
		devpriv->asics[asic].num = asic;
		devpriv->asics[asic].iobase = dev->iobase + asic * ASIC_IOSIZE;
		devpriv->asics[asic].irq = 0;	/* this gets actually set at the end of
						   this function when we
						   request_irqs */
		spin_lock_init(&devpriv->asics[asic].spinlock);
	}

	chans_left = CHANS_PER_ASIC * thisboard->num_asics;
	n_subdevs = CALC_N_SUBDEVS(chans_left);
	devpriv->sprivs =
	    kcalloc(n_subdevs, sizeof(struct pcmuio_subdev_private),
		    GFP_KERNEL);
	if (!devpriv->sprivs) {
		printk("cannot allocate subdevice private data structures\n");
		return -ENOMEM;
	}
	/*
	 * Allocate the subdevice structures.  alloc_subdevice() is a
	 * convenient macro defined in comedidev.h.
	 *
	 * Allocate 2 subdevs (32 + 16 DIO lines) or 3 32 DIO subdevs for the
	 * 96-channel version of the board.
	 */
	if (alloc_subdevices(dev, n_subdevs) < 0) {
		printk("cannot allocate subdevice data structures\n");
		return -ENOMEM;
	}

	port = 0;
	asic = 0;
	for (sdev_no = 0; sdev_no < (int)dev->n_subdevices; ++sdev_no) {
		int byte_no;

		s = dev->subdevices + sdev_no;
		s->private = devpriv->sprivs + sdev_no;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->type = COMEDI_SUBD_DIO;
		s->insn_bits = pcmuio_dio_insn_bits;
		s->insn_config = pcmuio_dio_insn_config;
		s->n_chan = min(chans_left, MAX_CHANS_PER_SUBDEV);
		subpriv->intr.asic = -1;
		subpriv->intr.first_chan = -1;
		subpriv->intr.asic_chan = -1;
		subpriv->intr.num_asic_chans = -1;
		subpriv->intr.active = 0;
		s->len_chanlist = 1;

		/* save the ioport address for each 'port' of 8 channels in the
		   subdevice */
		for (byte_no = 0; byte_no < PORTS_PER_SUBDEV; ++byte_no, ++port) {
			if (port >= PORTS_PER_ASIC) {
				port = 0;
				++asic;
				thisasic_chanct = 0;
			}
			subpriv->iobases[byte_no] =
			    devpriv->asics[asic].iobase + port;

			if (thisasic_chanct <
			    CHANS_PER_PORT * INTR_PORTS_PER_ASIC
			    && subpriv->intr.asic < 0) {
				/* this is an interrupt subdevice, so setup the struct */
				subpriv->intr.asic = asic;
				subpriv->intr.active = 0;
				subpriv->intr.stop_count = 0;
				subpriv->intr.first_chan = byte_no * 8;
				subpriv->intr.asic_chan = thisasic_chanct;
				subpriv->intr.num_asic_chans =
				    s->n_chan - subpriv->intr.first_chan;
				dev->read_subdev = s;
				s->subdev_flags |= SDF_CMD_READ;
				s->cancel = pcmuio_cancel;
				s->do_cmd = pcmuio_cmd;
				s->do_cmdtest = pcmuio_cmdtest;
				s->len_chanlist = subpriv->intr.num_asic_chans;
			}
			thisasic_chanct += CHANS_PER_PORT;
		}
		spin_lock_init(&subpriv->intr.spinlock);

		chans_left -= s->n_chan;

		if (!chans_left) {
			asic = 0;	/* reset the asic to our first asic, to do intr subdevs */
			port = 0;
		}

	}

	init_asics(dev);	/* clear out all the registers, basically */

	for (asic = 0; irq[0] && asic < MAX_ASICS; ++asic) {
		if (irq[asic]
		    && request_irq(irq[asic], interrupt_pcmuio,
				   IRQF_SHARED, thisboard->name, dev)) {
			int i;
			/* unroll the allocated irqs.. */
			for (i = asic - 1; i >= 0; --i) {
				free_irq(irq[i], dev);
				devpriv->asics[i].irq = irq[i] = 0;
			}
			irq[asic] = 0;
		}
		devpriv->asics[asic].irq = irq[asic];
	}

	dev->irq = irq[0];	/* grr.. wish comedi dev struct supported multiple
				   irqs.. */

	if (irq[0]) {
		printk("irq: %u ", irq[0]);
		if (irq[1] && thisboard->num_asics == 2)
			printk("second ASIC irq: %u ", irq[1]);
	} else {
		printk("(IRQ mode disabled) ");
	}

	printk("attached\n");

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
static int pcmuio_detach(struct comedi_device *dev)
{
	int i;

	printk("comedi%d: %s: remove\n", dev->minor, driver.driver_name);
	if (dev->iobase)
		release_region(dev->iobase, ASIC_IOSIZE * thisboard->num_asics);

	for (i = 0; i < MAX_ASICS; ++i) {
		if (devpriv->asics[i].irq)
			free_irq(devpriv->asics[i].irq, dev);
	}

	if (devpriv && devpriv->sprivs)
		kfree(devpriv->sprivs);

	return 0;
}

/* DIO devices are slightly special.  Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels.  The
 * comedi core can convert between insn_bits and insn_read/write */
static int pcmuio_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int byte_no;
	if (insn->n != 2)
		return -EINVAL;

	/* NOTE:
	   reading a 0 means this channel was high
	   writine a 0 sets the channel high
	   reading a 1 means this channel was low
	   writing a 1 means set this channel low

	   Therefore everything is always inverted. */

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */

#ifdef DAMMIT_ITS_BROKEN
	/* DEBUG */
	printk("write mask: %08x  data: %08x\n", data[0], data[1]);
#endif

	s->state = 0;

	for (byte_no = 0; byte_no < s->n_chan / CHANS_PER_PORT; ++byte_no) {
		/* address of 8-bit port */
		unsigned long ioaddr = subpriv->iobases[byte_no],
		    /* bit offset of port in 32-bit doubleword */
		    offset = byte_no * 8;
		/* this 8-bit port's data */
		unsigned char byte = 0,
		    /* The write mask for this port (if any) */
		    write_mask_byte = (data[0] >> offset) & 0xff,
		    /* The data byte for this port */
		    data_byte = (data[1] >> offset) & 0xff;

		byte = inb(ioaddr);	/* read all 8-bits for this port */

#ifdef DAMMIT_ITS_BROKEN
		/* DEBUG */
		printk
		    ("byte %d wmb %02x db %02x offset %02d io %04x, data_in %02x ",
		     byte_no, (unsigned)write_mask_byte, (unsigned)data_byte,
		     offset, ioaddr, (unsigned)byte);
#endif

		if (write_mask_byte) {
			/* this byte has some write_bits -- so set the output lines */
			byte &= ~write_mask_byte;	/* clear bits for write mask */
			byte |= ~data_byte & write_mask_byte;	/* set to inverted data_byte */
			/* Write out the new digital output state */
			outb(byte, ioaddr);
		}
#ifdef DAMMIT_ITS_BROKEN
		/* DEBUG */
		printk("data_out_byte %02x\n", (unsigned)byte);
#endif
		/* save the digital input lines for this byte.. */
		s->state |= ((unsigned int)byte) << offset;
	}

	/* now return the DIO lines to data[1] - note they came inverted! */
	data[1] = ~s->state;

#ifdef DAMMIT_ITS_BROKEN
	/* DEBUG */
	printk("s->state %08x data_out %08x\n", s->state, data[1]);
#endif

	return 2;
}

/* The input or output configuration of each digital line is
 * configured by a special insn_config instruction.  chanspec
 * contains the channel to be changed, and data[0] contains the
 * value COMEDI_INPUT or COMEDI_OUTPUT. */
static int pcmuio_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec), byte_no = chan / 8, bit_no =
	    chan % 8;
	unsigned long ioaddr;
	unsigned char byte;

	/* Compute ioaddr for this channel */
	ioaddr = subpriv->iobases[byte_no];

	/* NOTE:
	   writing a 0 an IO channel's bit sets the channel to INPUT
	   and pulls the line high as well

	   writing a 1 to an IO channel's  bit pulls the line low

	   All channels are implicitly always in OUTPUT mode -- but when
	   they are high they can be considered to be in INPUT mode..

	   Thus, we only force channels low if the config request was INPUT,
	   otherwise we do nothing to the hardware.    */

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		/* save to io_bits -- don't actually do anything since
		   all input channels are also output channels... */
		s->io_bits |= 1 << chan;
		break;
	case INSN_CONFIG_DIO_INPUT:
		/* write a 0 to the actual register representing the channel
		   to set it to 'input'.  0 means "float high". */
		byte = inb(ioaddr);
		byte &= ~(1 << bit_no);
				/**< set input channel to '0' */

		/* write out byte -- this is the only time we actually affect the
		   hardware as all channels are implicitly output -- but input
		   channels are set to float-high */
		outb(byte, ioaddr);

		/* save to io_bits */
		s->io_bits &= ~(1 << chan);
		break;

	case INSN_CONFIG_DIO_QUERY:
		/* retreive from shadow register */
		data[1] =
		    (s->io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;

	default:
		return -EINVAL;
		break;
	}

	return insn->n;
}

static void init_asics(struct comedi_device *dev)
{				/* sets up an
				   ASIC chip to defaults */
	int asic;

	for (asic = 0; asic < thisboard->num_asics; ++asic) {
		int port, page;
		unsigned long baseaddr = dev->iobase + asic * ASIC_IOSIZE;

		switch_page(dev, asic, 0);	/* switch back to page 0 */

		/* first, clear all the DIO port bits */
		for (port = 0; port < PORTS_PER_ASIC; ++port)
			outb(0, baseaddr + REG_PORT0 + port);

		/* Next, clear all the paged registers for each page */
		for (page = 1; page < NUM_PAGES; ++page) {
			int reg;
			/* now clear all the paged registers */
			switch_page(dev, asic, page);
			for (reg = FIRST_PAGED_REG;
			     reg < FIRST_PAGED_REG + NUM_PAGED_REGS; ++reg)
				outb(0, baseaddr + reg);
		}

		/* DEBUG  set rising edge interrupts on port0 of both asics */
		/*switch_page(dev, asic, PAGE_POL);
		   outb(0xff, baseaddr + REG_POL0);
		   switch_page(dev, asic, PAGE_ENAB);
		   outb(0xff, baseaddr + REG_ENAB0); */
		/* END DEBUG */

		switch_page(dev, asic, 0);	/* switch back to default page 0 */

	}
}

static void switch_page(struct comedi_device *dev, int asic, int page)
{
	if (asic < 0 || asic >= thisboard->num_asics)
		return;		/* paranoia */
	if (page < 0 || page >= NUM_PAGES)
		return;		/* more paranoia */

	devpriv->asics[asic].pagelock &= ~REG_PAGE_MASK;
	devpriv->asics[asic].pagelock |= page << REG_PAGE_BITOFFSET;

	/* now write out the shadow register */
	outb(devpriv->asics[asic].pagelock,
	     dev->iobase + ASIC_IOSIZE * asic + REG_PAGELOCK);
}

#ifdef notused
static void lock_port(struct comedi_device *dev, int asic, int port)
{
	if (asic < 0 || asic >= thisboard->num_asics)
		return;		/* paranoia */
	if (port < 0 || port >= PORTS_PER_ASIC)
		return;		/* more paranoia */

	devpriv->asics[asic].pagelock |= 0x1 << port;
	/* now write out the shadow register */
	outb(devpriv->asics[asic].pagelock,
	     dev->iobase + ASIC_IOSIZE * asic + REG_PAGELOCK);
}

static void unlock_port(struct comedi_device *dev, int asic, int port)
{
	if (asic < 0 || asic >= thisboard->num_asics)
		return;		/* paranoia */
	if (port < 0 || port >= PORTS_PER_ASIC)
		return;		/* more paranoia */
	devpriv->asics[asic].pagelock &= ~(0x1 << port) | REG_LOCK_MASK;
	/* now write out the shadow register */
	outb(devpriv->asics[asic].pagelock,
	     dev->iobase + ASIC_IOSIZE * asic + REG_PAGELOCK);
}
#endif /* notused */

static irqreturn_t interrupt_pcmuio(int irq, void *d)
{
	int asic, got1 = 0;
	struct comedi_device *dev = (struct comedi_device *)d;

	for (asic = 0; asic < MAX_ASICS; ++asic) {
		if (irq == devpriv->asics[asic].irq) {
			unsigned long flags;
			unsigned triggered = 0;
			unsigned long iobase = devpriv->asics[asic].iobase;
			/* it is an interrupt for ASIC #asic */
			unsigned char int_pend;

			spin_lock_irqsave(&devpriv->asics[asic].spinlock,
					  flags);

			int_pend = inb(iobase + REG_INT_PENDING) & 0x07;

			if (int_pend) {
				int port;
				for (port = 0; port < INTR_PORTS_PER_ASIC;
				     ++port) {
					if (int_pend & (0x1 << port)) {
						unsigned char
						    io_lines_with_edges = 0;
						switch_page(dev, asic,
							    PAGE_INT_ID);
						io_lines_with_edges =
						    inb(iobase +
							REG_INT_ID0 + port);

						if (io_lines_with_edges)
							/* clear pending interrupt */
							outb(0, iobase +
							     REG_INT_ID0 +
							     port);

						triggered |=
						    io_lines_with_edges <<
						    port * 8;
					}
				}

				++got1;
			}

			spin_unlock_irqrestore(&devpriv->asics[asic].spinlock,
					       flags);

			if (triggered) {
				struct comedi_subdevice *s;
				/* TODO here: dispatch io lines to subdevs with commands.. */
				printk
				    ("PCMUIO DEBUG: got edge detect interrupt %d asic %d which_chans: %06x\n",
				     irq, asic, triggered);
				for (s = dev->subdevices;
				     s < dev->subdevices + dev->n_subdevices;
				     ++s) {
					if (subpriv->intr.asic == asic) {	/* this is an interrupt subdev, and it matches this asic! */
						unsigned long flags;
						unsigned oldevents;

						spin_lock_irqsave(&subpriv->
								  intr.spinlock,
								  flags);

						oldevents = s->async->events;

						if (subpriv->intr.active) {
							unsigned mytrig =
							    ((triggered >>
							      subpriv->intr.asic_chan)
							     &
							     ((0x1 << subpriv->
							       intr.
							       num_asic_chans) -
							      1)) << subpriv->
							    intr.first_chan;
							if (mytrig &
							    subpriv->intr.enabled_mask)
							{
								unsigned int val
								    = 0;
								unsigned int n,
								    ch, len;

								len =
								    s->
								    async->cmd.chanlist_len;
								for (n = 0;
								     n < len;
								     n++) {
									ch = CR_CHAN(s->async->cmd.chanlist[n]);
									if (mytrig & (1U << ch)) {
										val |= (1U << n);
									}
								}
								/* Write the scan to the buffer. */
								if (comedi_buf_put(s->async, ((short *)&val)[0])
								    &&
								    comedi_buf_put
								    (s->async,
								     ((short *)
								      &val)[1]))
								{
									s->async->events |= (COMEDI_CB_BLOCK | COMEDI_CB_EOS);
								} else {
									/* Overflow! Stop acquisition!! */
									/* TODO: STOP_ACQUISITION_CALL_HERE!! */
									pcmuio_stop_intr
									    (dev,
									     s);
								}

								/* Check for end of acquisition. */
								if (!subpriv->intr.continuous) {
									/* stop_src == TRIG_COUNT */
									if (subpriv->intr.stop_count > 0) {
										subpriv->intr.stop_count--;
										if (subpriv->intr.stop_count == 0) {
											s->async->events |= COMEDI_CB_EOA;
											/* TODO: STOP_ACQUISITION_CALL_HERE!! */
											pcmuio_stop_intr
											    (dev,
											     s);
										}
									}
								}
							}
						}

						spin_unlock_irqrestore
						    (&subpriv->intr.spinlock,
						     flags);

						if (oldevents !=
						    s->async->events) {
							comedi_event(dev, s);
						}

					}

				}
			}

		}
	}
	if (!got1)
		return IRQ_NONE;	/* interrupt from other source */
	return IRQ_HANDLED;
}

static void pcmuio_stop_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	int nports, firstport, asic, port;

	asic = subpriv->intr.asic;
	if (asic < 0)
		return;		/* not an interrupt subdev */

	subpriv->intr.enabled_mask = 0;
	subpriv->intr.active = 0;
	s->async->inttrig = 0;
	nports = subpriv->intr.num_asic_chans / CHANS_PER_PORT;
	firstport = subpriv->intr.asic_chan / CHANS_PER_PORT;
	switch_page(dev, asic, PAGE_ENAB);
	for (port = firstport; port < firstport + nports; ++port) {
		/* disable all intrs for this subdev.. */
		outb(0, devpriv->asics[asic].iobase + REG_ENAB0 + port);
	}
}

static int pcmuio_start_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	if (!subpriv->intr.continuous && subpriv->intr.stop_count == 0) {
		/* An empty acquisition! */
		s->async->events |= COMEDI_CB_EOA;
		subpriv->intr.active = 0;
		return 1;
	} else {
		unsigned bits = 0, pol_bits = 0, n;
		int nports, firstport, asic, port;
		struct comedi_cmd *cmd = &s->async->cmd;

		asic = subpriv->intr.asic;
		if (asic < 0)
			return 1;	/* not an interrupt
					   subdev */
		subpriv->intr.enabled_mask = 0;
		subpriv->intr.active = 1;
		nports = subpriv->intr.num_asic_chans / CHANS_PER_PORT;
		firstport = subpriv->intr.asic_chan / CHANS_PER_PORT;
		if (cmd->chanlist) {
			for (n = 0; n < cmd->chanlist_len; n++) {
				bits |= (1U << CR_CHAN(cmd->chanlist[n]));
				pol_bits |= (CR_AREF(cmd->chanlist[n])
					     || CR_RANGE(cmd->
							 chanlist[n]) ? 1U : 0U)
				    << CR_CHAN(cmd->chanlist[n]);
			}
		}
		bits &= ((0x1 << subpriv->intr.num_asic_chans) -
			 1) << subpriv->intr.first_chan;
		subpriv->intr.enabled_mask = bits;

		switch_page(dev, asic, PAGE_ENAB);
		for (port = firstport; port < firstport + nports; ++port) {
			unsigned enab =
			    bits >> (subpriv->intr.first_chan + (port -
								 firstport) *
				     8) & 0xff, pol =
			    pol_bits >> (subpriv->intr.first_chan +
					 (port - firstport) * 8) & 0xff;
			/* set enab intrs for this subdev.. */
			outb(enab,
			     devpriv->asics[asic].iobase + REG_ENAB0 + port);
			switch_page(dev, asic, PAGE_POL);
			outb(pol,
			     devpriv->asics[asic].iobase + REG_ENAB0 + port);
		}
	}
	return 0;
}

static int pcmuio_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	unsigned long flags;

	spin_lock_irqsave(&subpriv->intr.spinlock, flags);
	if (subpriv->intr.active)
		pcmuio_stop_intr(dev, s);
	spin_unlock_irqrestore(&subpriv->intr.spinlock, flags);

	return 0;
}

/*
 * Internal trigger function to start acquisition for an 'INTERRUPT' subdevice.
 */
static int
pcmuio_inttrig_start_intr(struct comedi_device *dev, struct comedi_subdevice *s,
			  unsigned int trignum)
{
	unsigned long flags;
	int event = 0;

	if (trignum != 0)
		return -EINVAL;

	spin_lock_irqsave(&subpriv->intr.spinlock, flags);
	s->async->inttrig = 0;
	if (subpriv->intr.active) {
		event = pcmuio_start_intr(dev, s);
	}
	spin_unlock_irqrestore(&subpriv->intr.spinlock, flags);

	if (event) {
		comedi_event(dev, s);
	}

	return 1;
}

/*
 * 'do_cmd' function for an 'INTERRUPT' subdevice.
 */
static int pcmuio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned long flags;
	int event = 0;

	spin_lock_irqsave(&subpriv->intr.spinlock, flags);
	subpriv->intr.active = 1;

	/* Set up end of acquisition. */
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		subpriv->intr.continuous = 0;
		subpriv->intr.stop_count = cmd->stop_arg;
		break;
	default:
		/* TRIG_NONE */
		subpriv->intr.continuous = 1;
		subpriv->intr.stop_count = 0;
		break;
	}

	/* Set up start of acquisition. */
	switch (cmd->start_src) {
	case TRIG_INT:
		s->async->inttrig = pcmuio_inttrig_start_intr;
		break;
	default:
		/* TRIG_NOW */
		event = pcmuio_start_intr(dev, s);
		break;
	}
	spin_unlock_irqrestore(&subpriv->intr.spinlock, flags);

	if (event) {
		comedi_event(dev, s);
	}

	return 0;
}

static int
pcmuio_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
	       struct comedi_cmd *cmd)
{
	return comedi_pcm_cmdtest(dev, s, cmd);
}

/*
 * A convenient macro that defines init_module() and cleanup_module(),
 * as necessary.
 */
static int __init driver_init_module(void)
{
	return comedi_driver_register(&driver);
}

static void __exit driver_cleanup_module(void)
{
	comedi_driver_unregister(&driver);
}

module_init(driver_init_module);
module_exit(driver_cleanup_module);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

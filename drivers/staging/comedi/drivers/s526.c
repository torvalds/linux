/*
    comedi/drivers/s526.c
    Sensoray s526 Comedi driver

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
Driver: s526
Description: Sensoray 526 driver
Devices: [Sensoray] 526 (s526)
Author: Richie
	Everett Wang <everett.wang@everteq.com>
Updated: Thu, 14 Sep. 2006
Status: experimental

Encoder works
Analog input works
Analog output works
PWM output works
Commands are not supported yet.

Configuration Options:

comedi_config /dev/comedi0 s526 0x2C0,0x3

*/

#include "../comedidev.h"
#include <linux/ioport.h>
#include <asm/byteorder.h>

#define S526_SIZE 64

#define S526_START_AI_CONV	0
#define S526_AI_READ		0

/* Ports */
#define S526_IOSIZE 0x40
#define S526_NUM_PORTS 27

/* registers */
#define REG_TCR 0x00
#define REG_WDC 0x02
#define REG_DAC 0x04
#define REG_ADC 0x06
#define REG_ADD 0x08
#define REG_DIO 0x0A
#define REG_IER 0x0C
#define REG_ISR 0x0E
#define REG_MSC 0x10
#define REG_C0L 0x12
#define REG_C0H 0x14
#define REG_C0M 0x16
#define REG_C0C 0x18
#define REG_C1L 0x1A
#define REG_C1H 0x1C
#define REG_C1M 0x1E
#define REG_C1C 0x20
#define REG_C2L 0x22
#define REG_C2H 0x24
#define REG_C2M 0x26
#define REG_C2C 0x28
#define REG_C3L 0x2A
#define REG_C3H 0x2C
#define REG_C3M 0x2E
#define REG_C3C 0x30
#define REG_EED 0x32
#define REG_EEC 0x34

static const int s526_ports[] = {
	REG_TCR,
	REG_WDC,
	REG_DAC,
	REG_ADC,
	REG_ADD,
	REG_DIO,
	REG_IER,
	REG_ISR,
	REG_MSC,
	REG_C0L,
	REG_C0H,
	REG_C0M,
	REG_C0C,
	REG_C1L,
	REG_C1H,
	REG_C1M,
	REG_C1C,
	REG_C2L,
	REG_C2H,
	REG_C2M,
	REG_C2C,
	REG_C3L,
	REG_C3H,
	REG_C3M,
	REG_C3C,
	REG_EED,
	REG_EEC
};

struct counter_mode_register_t {
#if defined (__LITTLE_ENDIAN_BITFIELD)
	unsigned short coutSource:1;
	unsigned short coutPolarity:1;
	unsigned short autoLoadResetRcap:3;
	unsigned short hwCtEnableSource:2;
	unsigned short ctEnableCtrl:2;
	unsigned short clockSource:2;
	unsigned short countDir:1;
	unsigned short countDirCtrl:1;
	unsigned short outputRegLatchCtrl:1;
	unsigned short preloadRegSel:1;
	unsigned short reserved:1;
 #elif defined(__BIG_ENDIAN_BITFIELD)
	unsigned short reserved:1;
	unsigned short preloadRegSel:1;
	unsigned short outputRegLatchCtrl:1;
	unsigned short countDirCtrl:1;
	unsigned short countDir:1;
	unsigned short clockSource:2;
	unsigned short ctEnableCtrl:2;
	unsigned short hwCtEnableSource:2;
	unsigned short autoLoadResetRcap:3;
	unsigned short coutPolarity:1;
	unsigned short coutSource:1;
#else
#error Unknown bit field order
#endif
};

union cmReg {
	struct counter_mode_register_t reg;
	unsigned short value;
};

#define MAX_GPCT_CONFIG_DATA 6

/* Different Application Classes for GPCT Subdevices */
/* The list is not exhaustive and needs discussion! */
enum S526_GPCT_APP_CLASS {
	CountingAndTimeMeasurement,
	SinglePulseGeneration,
	PulseTrainGeneration,
	PositionMeasurement,
	Miscellaneous
};

/* Config struct for different GPCT subdevice Application Classes and
   their options
*/
struct s526GPCTConfig {
	enum S526_GPCT_APP_CLASS app;
	int data[MAX_GPCT_CONFIG_DATA];
};

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
struct s526_board {
	const char *name;
	int gpct_chans;
	int gpct_bits;
	int ad_chans;
	int ad_bits;
	int da_chans;
	int da_bits;
	int have_dio;
};

static const struct s526_board s526_boards[] = {
	{
	 .name = "s526",
	 .gpct_chans = 4,
	 .gpct_bits = 24,
	 .ad_chans = 8,
	 .ad_bits = 16,
	 .da_chans = 4,
	 .da_bits = 16,
	 .have_dio = 1,
	 }
};

#define ADDR_REG(reg) (dev->iobase + (reg))
#define ADDR_CHAN_REG(reg, chan) (dev->iobase + (reg) + (chan) * 8)

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct s526_board *)dev->board_ptr)

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
struct s526_private {

	int data;

	/* would be useful for a PCI device */
	struct pci_dev *pci_dev;

	/* Used for AO readback */
	unsigned int ao_readback[2];

	struct s526GPCTConfig s526_gpct_config[4];
	unsigned short s526_ai_config;
};

/*
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct s526_private *)dev->private)

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int s526_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int s526_detach(struct comedi_device *dev);
static struct comedi_driver driver_s526 = {
	.driver_name = "s526",
	.module = THIS_MODULE,
	.attach = s526_attach,
	.detach = s526_detach,
/* It is not necessary to implement the following members if you are
 * writing a driver for a ISA PnP or PCI card */
	/* Most drivers will support multiple types of boards by
	 * having an array of board structures.  These were defined
	 * in s526_boards[] above.  Note that the element 'name'
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
	.board_name = &s526_boards[0].name,
	.offset = sizeof(struct s526_board),
	.num_names = ARRAY_SIZE(s526_boards),
};

static int s526_gpct_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data);
static int s526_gpct_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);
static int s526_gpct_winsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data);
static int s526_ai_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);
static int s526_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data);
static int s526_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data);
static int s526_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data);
static int s526_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);
static int s526_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data);

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int s526_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int iobase;
	int i, n;
/* short value; */
/* int subdev_channel = 0; */
	union cmReg cmReg;

	printk("comedi%d: s526: ", dev->minor);

	iobase = it->options[0];
	if (!iobase || !request_region(iobase, S526_IOSIZE, thisboard->name)) {
		comedi_error(dev, "I/O port conflict");
		return -EIO;
	}
	dev->iobase = iobase;

	printk("iobase=0x%lx\n", dev->iobase);

	/*** make it a little quieter, exw, 8/29/06
	for (i = 0; i < S526_NUM_PORTS; i++) {
		printk("0x%02x: 0x%04x\n", ADDR_REG(s526_ports[i]), inw(ADDR_REG(s526_ports[i])));
	}
	***/

/*
 * Initialize dev->board_name.  Note that we can use the "thisboard"
 * macro now, since we just initialized it in the last line.
 */
	dev->board_ptr = &s526_boards[0];

	dev->board_name = thisboard->name;

/*
 * Allocate the private structure area.  alloc_private() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_private(dev, sizeof(struct s526_private)) < 0)
		return -ENOMEM;

/*
 * Allocate the subdevice structures.  alloc_subdevice() is a
 * convenient macro defined in comedidev.h.
 */
	dev->n_subdevices = 4;
	if (alloc_subdevices(dev, dev->n_subdevices) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	/* GENERAL-PURPOSE COUNTER/TIME (GPCT) */
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL;
	/* KG: What does SDF_LSAMPL (see multiq3.c) mean? */
	s->n_chan = thisboard->gpct_chans;
	s->maxdata = 0x00ffffff;	/* 24 bit counter */
	s->insn_read = s526_gpct_rinsn;
	s->insn_config = s526_gpct_insn_config;
	s->insn_write = s526_gpct_winsn;

	/* Command are not implemented yet, however they are necessary to
	   allocate the necessary memory for the comedi_async struct (used
	   to trigger the GPCT in case of pulsegenerator function */
	/* s->do_cmd = s526_gpct_cmd; */
	/* s->do_cmdtest = s526_gpct_cmdtest; */
	/* s->cancel = s526_gpct_cancel; */

	s = dev->subdevices + 1;
	/* dev->read_subdev=s; */
	/* analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	/* we support differential */
	s->subdev_flags = SDF_READABLE | SDF_DIFF;
	/* channels 0 to 7 are the regular differential inputs */
	/* channel 8 is "reference 0" (+10V), channel 9 is "reference 1" (0V) */
	s->n_chan = 10;
	s->maxdata = 0xffff;
	s->range_table = &range_bipolar10;
	s->len_chanlist = 16;	/* This is the maximum chanlist length that
				   the board can handle */
	s->insn_read = s526_ai_rinsn;
	s->insn_config = s526_ai_insn_config;

	s = dev->subdevices + 2;
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 4;
	s->maxdata = 0xffff;
	s->range_table = &range_bipolar10;
	s->insn_write = s526_ao_winsn;
	s->insn_read = s526_ao_rinsn;

	s = dev->subdevices + 3;
	/* digital i/o subdevice */
	if (thisboard->have_dio) {
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan = 8;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = s526_dio_insn_bits;
		s->insn_config = s526_dio_insn_config;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	printk("attached\n");

	return 1;

#if 0
	/*  Example of Counter Application */
	/* One-shot (software trigger) */
	cmReg.reg.coutSource = 0;	/*  out RCAP */
	cmReg.reg.coutPolarity = 1;	/*  Polarity inverted */
	cmReg.reg.autoLoadResetRcap = 1;	/*  Auto load 0:disabled, 1:enabled */
	cmReg.reg.hwCtEnableSource = 3;	/*  NOT RCAP */
	cmReg.reg.ctEnableCtrl = 2;	/*  Hardware */
	cmReg.reg.clockSource = 2;	/*  Internal */
	cmReg.reg.countDir = 1;	/*  Down */
	cmReg.reg.countDirCtrl = 1;	/*  Software */
	cmReg.reg.outputRegLatchCtrl = 0;	/*  latch on read */
	cmReg.reg.preloadRegSel = 0;	/*  PR0 */
	cmReg.reg.reserved = 0;

	outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

	outw(0x0001, ADDR_CHAN_REG(REG_C0H, subdev_channel));
	outw(0x3C68, ADDR_CHAN_REG(REG_C0L, subdev_channel));

	outw(0x8000, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Reset the counter */
	outw(0x4000, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Load the counter from PR0 */

	outw(0x0008, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Reset RCAP (fires one-shot) */

#else

	/*  Set Counter Mode Register */
	cmReg.reg.coutSource = 0;	/*  out RCAP */
	cmReg.reg.coutPolarity = 0;	/*  Polarity inverted */
	cmReg.reg.autoLoadResetRcap = 0;	/*  Auto load disabled */
	cmReg.reg.hwCtEnableSource = 2;	/*  NOT RCAP */
	cmReg.reg.ctEnableCtrl = 1;	/*  1: Software,  >1 : Hardware */
	cmReg.reg.clockSource = 3;	/*  x4 */
	cmReg.reg.countDir = 0;	/*  up */
	cmReg.reg.countDirCtrl = 0;	/*  quadrature */
	cmReg.reg.outputRegLatchCtrl = 0;	/*  latch on read */
	cmReg.reg.preloadRegSel = 0;	/*  PR0 */
	cmReg.reg.reserved = 0;

	n = 0;
	printk("Mode reg=0x%04x, 0x%04lx\n", cmReg.value, ADDR_CHAN_REG(REG_C0M,
									n));
	outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, n));
	udelay(1000);
	printk("Read back mode reg=0x%04x\n", inw(ADDR_CHAN_REG(REG_C0M, n)));

	/*  Load the pre-load register high word */
/* value = (short) (0x55); */
/* outw(value, ADDR_CHAN_REG(REG_C0H, n)); */

	/*  Load the pre-load register low word */
/* value = (short)(0xaa55); */
/* outw(value, ADDR_CHAN_REG(REG_C0L, n)); */

	/*  Write the Counter Control Register */
/* outw(value, ADDR_CHAN_REG(REG_C0C, 0)); */

	/*  Reset the counter if it is software preload */
	if (cmReg.reg.autoLoadResetRcap == 0) {
		outw(0x8000, ADDR_CHAN_REG(REG_C0C, n));	/*  Reset the counter */
		outw(0x4000, ADDR_CHAN_REG(REG_C0C, n));	/*  Load the counter from PR0 */
	}

	outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, n));
	udelay(1000);
	printk("Read back mode reg=0x%04x\n", inw(ADDR_CHAN_REG(REG_C0M, n)));

#endif
	printk("Current registres:\n");

	for (i = 0; i < S526_NUM_PORTS; i++) {
		printk("0x%02lx: 0x%04x\n", ADDR_REG(s526_ports[i]),
		       inw(ADDR_REG(s526_ports[i])));
	}
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
static int s526_detach(struct comedi_device *dev)
{
	printk("comedi%d: s526: remove\n", dev->minor);

	if (dev->iobase > 0)
		release_region(dev->iobase, S526_IOSIZE);

	return 0;
}

static int s526_gpct_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	int i;			/*  counts the Data */
	int counter_channel = CR_CHAN(insn->chanspec);
	unsigned short datalow;
	unsigned short datahigh;

	/*  Check if (n > 0) */
	if (insn->n <= 0) {
		printk("s526: INSN_READ: n should be > 0\n");
		return -EINVAL;
	}
	/*  Read the low word first */
	for (i = 0; i < insn->n; i++) {
		datalow = inw(ADDR_CHAN_REG(REG_C0L, counter_channel));
		datahigh = inw(ADDR_CHAN_REG(REG_C0H, counter_channel));
		data[i] = (int)(datahigh & 0x00FF);
		data[i] = (data[i] << 16) | (datalow & 0xFFFF);
/* printk("s526 GPCT[%d]: %x(0x%04x, 0x%04x)\n", counter_channel, data[i], datahigh, datalow); */
	}
	return i;
}

static int s526_gpct_insn_config(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	int subdev_channel = CR_CHAN(insn->chanspec);	/*  Unpack chanspec */
	int i;
	short value;
	union cmReg cmReg;

/* printk("s526: GPCT_INSN_CONFIG: Configuring Channel %d\n", subdev_channel); */

	for (i = 0; i < MAX_GPCT_CONFIG_DATA; i++) {
		devpriv->s526_gpct_config[subdev_channel].data[i] =
		    insn->data[i];
/* printk("data[%d]=%x\n", i, insn->data[i]); */
	}

	/*  Check what type of Counter the user requested, data[0] contains */
	/*  the Application type */
	switch (insn->data[0]) {
	case INSN_CONFIG_GPCT_QUADRATURE_ENCODER:
		/*
		   data[0]: Application Type
		   data[1]: Counter Mode Register Value
		   data[2]: Pre-load Register Value
		   data[3]: Conter Control Register
		 */
		printk("s526: GPCT_INSN_CONFIG: Configuring Encoder\n");
		devpriv->s526_gpct_config[subdev_channel].app =
		    PositionMeasurement;

#if 0
		/*  Example of Counter Application */
		/* One-shot (software trigger) */
		cmReg.reg.coutSource = 0;	/*  out RCAP */
		cmReg.reg.coutPolarity = 1;	/*  Polarity inverted */
		cmReg.reg.autoLoadResetRcap = 0;	/*  Auto load disabled */
		cmReg.reg.hwCtEnableSource = 3;	/*  NOT RCAP */
		cmReg.reg.ctEnableCtrl = 2;	/*  Hardware */
		cmReg.reg.clockSource = 2;	/*  Internal */
		cmReg.reg.countDir = 1;	/*  Down */
		cmReg.reg.countDirCtrl = 1;	/*  Software */
		cmReg.reg.outputRegLatchCtrl = 0;	/*  latch on read */
		cmReg.reg.preloadRegSel = 0;	/*  PR0 */
		cmReg.reg.reserved = 0;

		outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

		outw(0x0001, ADDR_CHAN_REG(REG_C0H, subdev_channel));
		outw(0x3C68, ADDR_CHAN_REG(REG_C0L, subdev_channel));

		outw(0x8000, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Reset the counter */
		outw(0x4000, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Load the counter from PR0 */

		outw(0x0008, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Reset RCAP (fires one-shot) */

#endif

#if 1
		/*  Set Counter Mode Register */
		cmReg.value = insn->data[1] & 0xFFFF;

/* printk("s526: Counter Mode register=%x\n", cmReg.value); */
		outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

		/*  Reset the counter if it is software preload */
		if (cmReg.reg.autoLoadResetRcap == 0) {
			outw(0x8000, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Reset the counter */
/* outw(0x4000, ADDR_CHAN_REG(REG_C0C, subdev_channel));    Load the counter from PR0 */
		}
#else
		cmReg.reg.countDirCtrl = 0;	/*  0 quadrature, 1 software control */

		/*  data[1] contains GPCT_X1, GPCT_X2 or GPCT_X4 */
		if (insn->data[1] == GPCT_X2) {
			cmReg.reg.clockSource = 1;
		} else if (insn->data[1] == GPCT_X4) {
			cmReg.reg.clockSource = 2;
		} else {
			cmReg.reg.clockSource = 0;
		}

		/*  When to take into account the indexpulse: */
		if (insn->data[2] == GPCT_IndexPhaseLowLow) {
		} else if (insn->data[2] == GPCT_IndexPhaseLowHigh) {
		} else if (insn->data[2] == GPCT_IndexPhaseHighLow) {
		} else if (insn->data[2] == GPCT_IndexPhaseHighHigh) {
		}
		/*  Take into account the index pulse? */
		if (insn->data[3] == GPCT_RESET_COUNTER_ON_INDEX)
			cmReg.reg.autoLoadResetRcap = 4;	/*  Auto load with INDEX^ */

		/*  Set Counter Mode Register */
		cmReg.value = (short)(insn->data[1] & 0xFFFF);
		outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

		/*  Load the pre-load register high word */
		value = (short)((insn->data[2] >> 16) & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0H, subdev_channel));

		/*  Load the pre-load register low word */
		value = (short)(insn->data[2] & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0L, subdev_channel));

		/*  Write the Counter Control Register */
		if (insn->data[3] != 0) {
			value = (short)(insn->data[3] & 0xFFFF);
			outw(value, ADDR_CHAN_REG(REG_C0C, subdev_channel));
		}
		/*  Reset the counter if it is software preload */
		if (cmReg.reg.autoLoadResetRcap == 0) {
			outw(0x8000, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Reset the counter */
			outw(0x4000, ADDR_CHAN_REG(REG_C0C, subdev_channel));	/*  Load the counter from PR0 */
		}
#endif
		break;

	case INSN_CONFIG_GPCT_SINGLE_PULSE_GENERATOR:
		/*
		   data[0]: Application Type
		   data[1]: Counter Mode Register Value
		   data[2]: Pre-load Register 0 Value
		   data[3]: Pre-load Register 1 Value
		   data[4]: Conter Control Register
		 */
		printk("s526: GPCT_INSN_CONFIG: Configuring SPG\n");
		devpriv->s526_gpct_config[subdev_channel].app =
		    SinglePulseGeneration;

		/*  Set Counter Mode Register */
		cmReg.value = (short)(insn->data[1] & 0xFFFF);
		cmReg.reg.preloadRegSel = 0;	/*  PR0 */
		outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

		/*  Load the pre-load register 0 high word */
		value = (short)((insn->data[2] >> 16) & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0H, subdev_channel));

		/*  Load the pre-load register 0 low word */
		value = (short)(insn->data[2] & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0L, subdev_channel));

		/*  Set Counter Mode Register */
		cmReg.value = (short)(insn->data[1] & 0xFFFF);
		cmReg.reg.preloadRegSel = 1;	/*  PR1 */
		outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

		/*  Load the pre-load register 1 high word */
		value = (short)((insn->data[3] >> 16) & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0H, subdev_channel));

		/*  Load the pre-load register 1 low word */
		value = (short)(insn->data[3] & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0L, subdev_channel));

		/*  Write the Counter Control Register */
		if (insn->data[4] != 0) {
			value = (short)(insn->data[4] & 0xFFFF);
			outw(value, ADDR_CHAN_REG(REG_C0C, subdev_channel));
		}
		break;

	case INSN_CONFIG_GPCT_PULSE_TRAIN_GENERATOR:
		/*
		   data[0]: Application Type
		   data[1]: Counter Mode Register Value
		   data[2]: Pre-load Register 0 Value
		   data[3]: Pre-load Register 1 Value
		   data[4]: Conter Control Register
		 */
		printk("s526: GPCT_INSN_CONFIG: Configuring PTG\n");
		devpriv->s526_gpct_config[subdev_channel].app =
		    PulseTrainGeneration;

		/*  Set Counter Mode Register */
		cmReg.value = (short)(insn->data[1] & 0xFFFF);
		cmReg.reg.preloadRegSel = 0;	/*  PR0 */
		outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

		/*  Load the pre-load register 0 high word */
		value = (short)((insn->data[2] >> 16) & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0H, subdev_channel));

		/*  Load the pre-load register 0 low word */
		value = (short)(insn->data[2] & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0L, subdev_channel));

		/*  Set Counter Mode Register */
		cmReg.value = (short)(insn->data[1] & 0xFFFF);
		cmReg.reg.preloadRegSel = 1;	/*  PR1 */
		outw(cmReg.value, ADDR_CHAN_REG(REG_C0M, subdev_channel));

		/*  Load the pre-load register 1 high word */
		value = (short)((insn->data[3] >> 16) & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0H, subdev_channel));

		/*  Load the pre-load register 1 low word */
		value = (short)(insn->data[3] & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0L, subdev_channel));

		/*  Write the Counter Control Register */
		if (insn->data[4] != 0) {
			value = (short)(insn->data[4] & 0xFFFF);
			outw(value, ADDR_CHAN_REG(REG_C0C, subdev_channel));
		}
		break;

	default:
		printk("s526: unsupported GPCT_insn_config\n");
		return -EINVAL;
		break;
	}

	return insn->n;
}

static int s526_gpct_winsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	int subdev_channel = CR_CHAN(insn->chanspec);	/*  Unpack chanspec */
	short value;
	union cmReg cmReg;

	printk("s526: GPCT_INSN_WRITE on channel %d\n", subdev_channel);
	cmReg.value = inw(ADDR_CHAN_REG(REG_C0M, subdev_channel));
	printk("s526: Counter Mode Register: %x\n", cmReg.value);
	/*  Check what Application of Counter this channel is configured for */
	switch (devpriv->s526_gpct_config[subdev_channel].app) {
	case PositionMeasurement:
		printk("S526: INSN_WRITE: PM\n");
		outw(0xFFFF & ((*data) >> 16), ADDR_CHAN_REG(REG_C0H,
							     subdev_channel));
		outw(0xFFFF & (*data), ADDR_CHAN_REG(REG_C0L, subdev_channel));
		break;

	case SinglePulseGeneration:
		printk("S526: INSN_WRITE: SPG\n");
		outw(0xFFFF & ((*data) >> 16), ADDR_CHAN_REG(REG_C0H,
							     subdev_channel));
		outw(0xFFFF & (*data), ADDR_CHAN_REG(REG_C0L, subdev_channel));
		break;

	case PulseTrainGeneration:
		/* data[0] contains the PULSE_WIDTH
		   data[1] contains the PULSE_PERIOD
		   @pre PULSE_PERIOD > PULSE_WIDTH > 0
		   The above periods must be expressed as a multiple of the
		   pulse frequency on the selected source
		 */
		printk("S526: INSN_WRITE: PTG\n");
		if ((insn->data[1] > insn->data[0]) && (insn->data[0] > 0)) {
			(devpriv->s526_gpct_config[subdev_channel]).data[0] =
			    insn->data[0];
			(devpriv->s526_gpct_config[subdev_channel]).data[1] =
			    insn->data[1];
		} else {
			printk("s526: INSN_WRITE: PTG: Problem with Pulse params -> %d %d\n",
				insn->data[0], insn->data[1]);
			return -EINVAL;
		}

		value = (short)((*data >> 16) & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0H, subdev_channel));
		value = (short)(*data & 0xFFFF);
		outw(value, ADDR_CHAN_REG(REG_C0L, subdev_channel));
		break;
	default:		/*  Impossible */
		printk
		    ("s526: INSN_WRITE: Functionality %d not implemented yet\n",
		     devpriv->s526_gpct_config[subdev_channel].app);
		return -EINVAL;
		break;
	}
	/*  return the number of samples written */
	return insn->n;
}

#define ISR_ADC_DONE 0x4
static int s526_ai_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int result = -EINVAL;

	if (insn->n < 1)
		return result;

	result = insn->n;

	/* data[0] : channels was set in relevant bits.
	   data[1] : delay
	 */
	/* COMMENT: abbotti 2008-07-24: I don't know why you'd want to
	 * enable channels here.  The channel should be enabled in the
	 * INSN_READ handler. */

	/*  Enable ADC interrupt */
	outw(ISR_ADC_DONE, ADDR_REG(REG_IER));
/* printk("s526: ADC current value: 0x%04x\n", inw(ADDR_REG(REG_ADC))); */
	devpriv->s526_ai_config = (data[0] & 0x3FF) << 5;
	if (data[1] > 0)
		devpriv->s526_ai_config |= 0x8000;	/* set the delay */

	devpriv->s526_ai_config |= 0x0001;	/*  ADC start bit. */

	return result;
}

/*
 * "instructions" read/write data in "one-shot" or "software-triggered"
 * mode.
 */
static int s526_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	int n, i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned short value;
	unsigned int d;
	unsigned int status;

	/* Set configured delay, enable channel for this channel only,
	 * select "ADC read" channel, set "ADC start" bit. */
	value = (devpriv->s526_ai_config & 0x8000) |
	    ((1 << 5) << chan) | (chan << 1) | 0x0001;

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outw(value, ADDR_REG(REG_ADC));
/* printk("s526: Wrote 0x%04x to ADC\n", value); */
/* printk("s526: ADC reg=0x%04x\n", inw(ADDR_REG(REG_ADC))); */

#define TIMEOUT 100
		/* wait for conversion to end */
		for (i = 0; i < TIMEOUT; i++) {
			status = inw(ADDR_REG(REG_ISR));
			if (status & ISR_ADC_DONE) {
				outw(ISR_ADC_DONE, ADDR_REG(REG_ISR));
				break;
			}
		}
		if (i == TIMEOUT) {
			/* printk() should be used instead of printk()
			 * whenever the code can be called from real-time. */
			printk("s526: ADC(0x%04x) timeout\n",
			       inw(ADDR_REG(REG_ISR)));
			return -ETIMEDOUT;
		}

		/* read data */
		d = inw(ADDR_REG(REG_ADD));
/* printk("AI[%d]=0x%04x\n", n, (unsigned short)(d & 0xFFFF)); */

		/* munge data */
		data[n] = d ^ 0x8000;
	}

	/* return the number of samples read/written */
	return n;
}

static int s526_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned short val;

/* printk("s526_ao_winsn\n"); */
	val = chan << 1;
/* outw(val, dev->iobase + REG_DAC); */
	outw(val, ADDR_REG(REG_DAC));

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; i++) {
		/* a typical programming sequence */
/* outw(data[i], dev->iobase + REG_ADD);    write the data to preload register */
		outw(data[i], ADDR_REG(REG_ADD));	/*  write the data to preload register */
		devpriv->ao_readback[chan] = data[i];
/* outw(val + 1, dev->iobase + REG_DAC);  starts the D/A conversion. */
		outw(val + 1, ADDR_REG(REG_DAC));	/*  starts the D/A conversion. */
	}

	/* return the number of samples read/written */
	return i;
}

/* AO subdevices should have a read insn as well as a write insn.
 * Usually this means copying a value stored in devpriv. */
static int s526_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
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
static int s526_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		/* Write out the new digital output lines */
		outw(s->state, ADDR_REG(REG_DIO));
	}

	/* on return, data[1] contains the value of the digital
	 * input and output lines. */
	data[1] = inw(ADDR_REG(REG_DIO)) & 0xFF;	/*  low 8 bits are the data */
	/* or we could just return the software copy of the output values if
	 * it was a purely digital output subdevice */
	/* data[1]=s->state & 0xFF; */

	return 2;
}

static int s526_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	int group, mask;

	printk("S526 DIO insn_config\n");

	/* The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value COMEDI_INPUT or COMEDI_OUTPUT. */

	group = chan >> 2;
	mask = 0xF << (group << 2);
	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->state |= 1 << (group + 10);  // bit 10/11 set the group 1/2's mode
		s->io_bits |= mask;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->state &= ~(1 << (group + 10));// 1 is output, 0 is input.
		s->io_bits &= ~mask;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & mask) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
	default:
		return -EINVAL;
	}
	outw(s->state, ADDR_REG(REG_DIO));

	return 1;
}

/*
 * A convenient macro that defines init_module() and cleanup_module(),
 * as necessary.
 */
COMEDI_INITCLEANUP(driver_s526);

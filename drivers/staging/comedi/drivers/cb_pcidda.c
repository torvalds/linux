/*
    comedi/drivers/cb_pcidda.c
    This intends to be a driver for the ComputerBoards / MeasurementComputing
    PCI-DDA series.

	 Copyright (C) 2001 Ivan Martinez <ivanmr@altavista.com>
    Copyright (C) 2001 Frank Mori Hess <fmhess@users.sourceforge.net>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>

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
Driver: cb_pcidda
Description: MeasurementComputing PCI-DDA series
Author: Ivan Martinez <ivanmr@altavista.com>, Frank Mori Hess <fmhess@users.sourceforge.net>
Status: Supports 08/16, 04/16, 02/16, 08/12, 04/12, and 02/12
Devices: [Measurement Computing] PCI-DDA08/12 (cb_pcidda), PCI-DDA04/12,
  PCI-DDA02/12, PCI-DDA08/16, PCI-DDA04/16, PCI-DDA02/16

Configuration options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first available PCI
  device will be used.

Only simple analog output writing is supported.

So far it has only been tested with:
  - PCI-DDA08/12
Please report success/failure with other different cards to
<comedi@comedi.org>.
*/

#include "../comedidev.h"

#include "8255.h"

/* PCI vendor number of ComputerBoards */
#define PCI_VENDOR_ID_CB        0x1307
#define EEPROM_SIZE	128	/*  number of entries in eeprom */
/* maximum number of ao channels for supported boards */
#define MAX_AO_CHANNELS 8

/* PCI-DDA base addresses */
#define DIGITALIO_BADRINDEX	2
	/*  DIGITAL I/O is pci_dev->resource[2] */
#define DIGITALIO_SIZE 8
	/*  DIGITAL I/O uses 8 I/O port addresses */
#define DAC_BADRINDEX	3
	/*  DAC is pci_dev->resource[3] */

/* Digital I/O registers */
#define PORT1A 0		/*  PORT 1A DATA */

#define PORT1B 1		/*  PORT 1B DATA */

#define PORT1C 2		/*  PORT 1C DATA */

#define CONTROL1 3		/*  CONTROL REGISTER 1 */

#define PORT2A 4		/*  PORT 2A DATA */

#define PORT2B 5		/*  PORT 2B DATA */

#define PORT2C 6		/*  PORT 2C DATA */

#define CONTROL2 7		/*  CONTROL REGISTER 2 */

/* DAC registers */
#define DACONTROL	0	/*  D/A CONTROL REGISTER */
#define	SU	0000001		/*  Simultaneous update enabled */
#define NOSU	0000000		/*  Simultaneous update disabled */
#define	ENABLEDAC	0000002	/*  Enable specified DAC */
#define	DISABLEDAC	0000000	/*  Disable specified DAC */
#define RANGE2V5	0000000	/*  2.5V */
#define RANGE5V	0000200		/*  5V */
#define RANGE10V	0000300	/*  10V */
#define UNIP	0000400		/*  Unipolar outputs */
#define BIP	0000000		/*  Bipolar outputs */

#define DACALIBRATION1	4	/*  D/A CALIBRATION REGISTER 1 */
/* write bits */
/* serial data input for eeprom, caldacs, reference dac */
#define SERIAL_IN_BIT   0x1
#define	CAL_CHANNEL_MASK	(0x7 << 1)
#define	CAL_CHANNEL_BITS(channel)	(((channel) << 1) & CAL_CHANNEL_MASK)
/* read bits */
#define	CAL_COUNTER_MASK	0x1f
/* calibration counter overflow status bit */
#define CAL_COUNTER_OVERFLOW_BIT        0x20
/* analog output is less than reference dac voltage */
#define AO_BELOW_REF_BIT        0x40
#define	SERIAL_OUT_BIT	0x80	/*  serial data out, for reading from eeprom */

#define DACALIBRATION2	6	/*  D/A CALIBRATION REGISTER 2 */
#define	SELECT_EEPROM_BIT	0x1	/*  send serial data in to eeprom */
/* don't send serial data to MAX542 reference dac */
#define DESELECT_REF_DAC_BIT    0x2
/* don't send serial data to caldac n */
#define DESELECT_CALDAC_BIT(n)  (0x4 << (n))
/* manual says to set this bit with no explanation */
#define DUMMY_BIT       0x40

#define DADATA	8		/*  FIRST D/A DATA REGISTER (0) */

static const struct comedi_lrange cb_pcidda_ranges = {
	6,
	{
	 BIP_RANGE(10),
	 BIP_RANGE(5),
	 BIP_RANGE(2.5),
	 UNI_RANGE(10),
	 UNI_RANGE(5),
	 UNI_RANGE(2.5),
	 }
};

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
struct cb_pcidda_board {
	const char *name;
	char status;		/*  Driver status: */

	/*
	 * 0 - tested
	 * 1 - manual read, not tested
	 * 2 - manual not read
	 */

	unsigned short device_id;
	int ao_chans;
	int ao_bits;
	const struct comedi_lrange *ranges;
};

static const struct cb_pcidda_board cb_pcidda_boards[] = {
	{
	 .name = "pci-dda02/12",
	 .status = 1,
	 .device_id = 0x20,
	 .ao_chans = 2,
	 .ao_bits = 12,
	 .ranges = &cb_pcidda_ranges,
	 },
	{
	 .name = "pci-dda04/12",
	 .status = 1,
	 .device_id = 0x21,
	 .ao_chans = 4,
	 .ao_bits = 12,
	 .ranges = &cb_pcidda_ranges,
	 },
	{
	 .name = "pci-dda08/12",
	 .status = 0,
	 .device_id = 0x22,
	 .ao_chans = 8,
	 .ao_bits = 12,
	 .ranges = &cb_pcidda_ranges,
	 },
	{
	 .name = "pci-dda02/16",
	 .status = 2,
	 .device_id = 0x23,
	 .ao_chans = 2,
	 .ao_bits = 16,
	 .ranges = &cb_pcidda_ranges,
	 },
	{
	 .name = "pci-dda04/16",
	 .status = 2,
	 .device_id = 0x24,
	 .ao_chans = 4,
	 .ao_bits = 16,
	 .ranges = &cb_pcidda_ranges,
	 },
	{
	 .name = "pci-dda08/16",
	 .status = 0,
	 .device_id = 0x25,
	 .ao_chans = 8,
	 .ao_bits = 16,
	 .ranges = &cb_pcidda_ranges,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct cb_pcidda_board *)dev->board_ptr)

/*
 * this structure is for data unique to this hardware driver.  If
 * several hardware drivers keep similar information in this structure,
 * feel free to suggest moving the variable to the struct comedi_device
 * struct.
 */
struct cb_pcidda_private {
	int data;

	/* would be useful for a PCI device */
	struct pci_dev *pci_dev;

	unsigned long digitalio;
	unsigned long dac;

	/* unsigned long control_status; */
	/* unsigned long adc_fifo; */

	/* bits last written to da calibration register 1 */
	unsigned int dac_cal1_bits;
	/* current range settings for output channels */
	unsigned int ao_range[MAX_AO_CHANNELS];
	u16 eeprom_data[EEPROM_SIZE];	/*  software copy of board's eeprom */
};

/*
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct cb_pcidda_private *)dev->private)

/* static int cb_pcidda_ai_rinsn(struct comedi_device *dev,struct comedi_subdevice *s,struct comedi_insn *insn,unsigned int *data); */
static int cb_pcidda_ao_winsn(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);

/* static int cb_pcidda_ai_cmd(struct comedi_device *dev, struct *comedi_subdevice *s);*/
/* static int cb_pcidda_ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s, struct comedi_cmd *cmd); */
/* static int cb_pcidda_ns_to_timer(unsigned int *ns,int *round); */

static unsigned int cb_pcidda_serial_in(struct comedi_device *dev);
static void cb_pcidda_serial_out(struct comedi_device *dev, unsigned int value,
				 unsigned int num_bits);
static unsigned int cb_pcidda_read_eeprom(struct comedi_device *dev,
					  unsigned int address);
static void cb_pcidda_calibrate(struct comedi_device *dev, unsigned int channel,
				unsigned int range);

static struct pci_dev *cb_pcidda_find_pci_dev(struct comedi_device *dev,
					      struct comedi_devconfig *it)
{
	struct pci_dev *pcidev = NULL;
	int index;

	for_each_pci_dev(pcidev) {
		if (pcidev->vendor == PCI_VENDOR_ID_CB) {
			if (it->options[0] || it->options[1]) {
				if (pcidev->bus->number != it->options[0] ||
				    PCI_SLOT(pcidev->devfn) != it->options[1]) {
					continue;
				}
			}
			for (index = 0; index < ARRAY_SIZE(cb_pcidda_boards); index++) {
				if (cb_pcidda_boards[index].device_id ==
				    pcidev->device) {
					dev->board_ptr = cb_pcidda_boards + index;
					dev_dbg(dev->class_dev,
						"Found %s at requested position\n",
						thisboard->name);
					return pcidev;
				}
			}
		}
	}
	dev_err(dev->class_dev,
		"Not a ComputerBoards/MeasurementComputing card on requested position\n");
	return NULL;
}

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.
 */
static int cb_pcidda_attach(struct comedi_device *dev,
			    struct comedi_devconfig *it)
{
	struct pci_dev *pcidev;
	struct comedi_subdevice *s;
	int index;
	int ret;

/*
 * Allocate the private structure area.
 */
	if (alloc_private(dev, sizeof(struct cb_pcidda_private)) < 0)
		return -ENOMEM;

	pcidev = cb_pcidda_find_pci_dev(dev, it);
	if (!pcidev)
		return -EIO;
	devpriv->pci_dev = pcidev;

	/*
	 * Enable PCI device and request regions.
	 */
	if (comedi_pci_enable(pcidev, thisboard->name)) {
		dev_err(dev->class_dev,
			"cb_pcidda: failed to enable PCI device and request regions\n");
		return -EIO;
	}

/*
 * Allocate the I/O ports.
 */
	devpriv->digitalio =
	    pci_resource_start(devpriv->pci_dev, DIGITALIO_BADRINDEX);
	devpriv->dac = pci_resource_start(devpriv->pci_dev, DAC_BADRINDEX);

/*
 * Warn about the status of the driver.
 */
	if (thisboard->status == 2)
		printk
		    ("WARNING: DRIVER FOR THIS BOARD NOT CHECKED WITH MANUAL. "
		     "WORKS ASSUMING FULL COMPATIBILITY WITH PCI-DDA08/12. "
		     "PLEASE REPORT USAGE TO <ivanmr@altavista.com>.\n");

/*
 * Initialize dev->board_name.
 */
	dev->board_name = thisboard->name;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = dev->subdevices + 0;
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = thisboard->ao_chans;
	s->maxdata = (1 << thisboard->ao_bits) - 1;
	s->range_table = thisboard->ranges;
	s->insn_write = cb_pcidda_ao_winsn;

	/* s->subdev_flags |= SDF_CMD_READ; */
	/* s->do_cmd = cb_pcidda_ai_cmd; */
	/* s->do_cmdtest = cb_pcidda_ai_cmdtest; */

	/*  two 8255 digital io subdevices */
	s = dev->subdevices + 1;
	subdev_8255_init(dev, s, NULL, devpriv->digitalio);
	s = dev->subdevices + 2;
	subdev_8255_init(dev, s, NULL, devpriv->digitalio + PORT2A);

	dev_dbg(dev->class_dev, "eeprom:\n");
	for (index = 0; index < EEPROM_SIZE; index++) {
		devpriv->eeprom_data[index] = cb_pcidda_read_eeprom(dev, index);
		dev_dbg(dev->class_dev, "%i:0x%x\n", index,
			devpriv->eeprom_data[index]);
	}

	/*  set calibrations dacs */
	for (index = 0; index < thisboard->ao_chans; index++)
		cb_pcidda_calibrate(dev, index, devpriv->ao_range[index]);

	return 1;
}

static void cb_pcidda_detach(struct comedi_device *dev)
{
	if (devpriv) {
		if (devpriv->pci_dev) {
			if (devpriv->dac)
				comedi_pci_disable(devpriv->pci_dev);
			pci_dev_put(devpriv->pci_dev);
		}
	}
	if (dev->subdevices) {
		subdev_8255_cleanup(dev, dev->subdevices + 1);
		subdev_8255_cleanup(dev, dev->subdevices + 2);
	}
}

/*
 * I will program this later... ;-)
 */
#if 0
static int cb_pcidda_ai_cmd(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	printk("cb_pcidda_ai_cmd\n");
	printk("subdev: %d\n", cmd->subdev);
	printk("flags: %d\n", cmd->flags);
	printk("start_src: %d\n", cmd->start_src);
	printk("start_arg: %d\n", cmd->start_arg);
	printk("scan_begin_src: %d\n", cmd->scan_begin_src);
	printk("convert_src: %d\n", cmd->convert_src);
	printk("convert_arg: %d\n", cmd->convert_arg);
	printk("scan_end_src: %d\n", cmd->scan_end_src);
	printk("scan_end_arg: %d\n", cmd->scan_end_arg);
	printk("stop_src: %d\n", cmd->stop_src);
	printk("stop_arg: %d\n", cmd->stop_arg);
	printk("chanlist_len: %d\n", cmd->chanlist_len);
}
#endif

#if 0
static int cb_pcidda_ai_cmdtest(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

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
	cmd->scan_begin_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER | TRIG_EXT;
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

	/*
	 * step 2: make sure trigger sources are unique and mutually
	 * compatible
	 */

	/* note that mutual compatibility is not an issue here */
	if (cmd->scan_begin_src != TRIG_TIMER
	    && cmd->scan_begin_src != TRIG_EXT)
		err++;
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_EXT)
		err++;
	if (cmd->stop_src != TRIG_TIMER && cmd->stop_src != TRIG_EXT)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
#define MAX_SPEED	10000	/* in nanoseconds */
#define MIN_SPEED	1000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->scan_begin_arg < MAX_SPEED) {
			cmd->scan_begin_arg = MAX_SPEED;
			err++;
		}
		if (cmd->scan_begin_arg > MIN_SPEED) {
			cmd->scan_begin_arg = MIN_SPEED;
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
		if (cmd->convert_arg < MAX_SPEED) {
			cmd->convert_arg = MAX_SPEED;
			err++;
		}
		if (cmd->convert_arg > MIN_SPEED) {
			cmd->convert_arg = MIN_SPEED;
			err++;
		}
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
		cb_pcidda_ns_to_timer(&cmd->scan_begin_arg,
				      cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		tmp = cmd->convert_arg;
		cb_pcidda_ns_to_timer(&cmd->convert_arg,
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

	return 0;
}
#endif

/* This function doesn't require a particular form, this is just
 * what happens to be used in some of the drivers.  It should
 * convert ns nanoseconds to a counter value suitable for programming
 * the device.  Also, it should adjust ns so that it cooresponds to
 * the actual time that the device will use. */
#if 0
static int cb_pcidda_ns_to_timer(unsigned int *ns, int round)
{
	/* trivial timer */
	return *ns;
}
#endif

static int cb_pcidda_ao_winsn(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	unsigned int command;
	unsigned int channel, range;

	channel = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);

	/*  adjust calibration dacs if range has changed */
	if (range != devpriv->ao_range[channel])
		cb_pcidda_calibrate(dev, channel, range);

	/* output channel configuration */
	command = NOSU | ENABLEDAC;

	/* output channel range */
	switch (range) {
	case 0:
		command |= BIP | RANGE10V;
		break;
	case 1:
		command |= BIP | RANGE5V;
		break;
	case 2:
		command |= BIP | RANGE2V5;
		break;
	case 3:
		command |= UNIP | RANGE10V;
		break;
	case 4:
		command |= UNIP | RANGE5V;
		break;
	case 5:
		command |= UNIP | RANGE2V5;
		break;
	}

	/* output channel specification */
	command |= channel << 2;
	outw(command, devpriv->dac + DACONTROL);

	/* write data */
	outw(data[0], devpriv->dac + DADATA + channel * 2);

	/* return the number of samples read/written */
	return 1;
}

/* lowlevel read from eeprom */
static unsigned int cb_pcidda_serial_in(struct comedi_device *dev)
{
	unsigned int value = 0;
	int i;
	const int value_width = 16;	/*  number of bits wide values are */

	for (i = 1; i <= value_width; i++) {
		/*  read bits most significant bit first */
		if (inw_p(devpriv->dac + DACALIBRATION1) & SERIAL_OUT_BIT)
			value |= 1 << (value_width - i);
	}

	return value;
}

/* lowlevel write to eeprom/dac */
static void cb_pcidda_serial_out(struct comedi_device *dev, unsigned int value,
				 unsigned int num_bits)
{
	int i;

	for (i = 1; i <= num_bits; i++) {
		/*  send bits most significant bit first */
		if (value & (1 << (num_bits - i)))
			devpriv->dac_cal1_bits |= SERIAL_IN_BIT;
		else
			devpriv->dac_cal1_bits &= ~SERIAL_IN_BIT;
		outw_p(devpriv->dac_cal1_bits, devpriv->dac + DACALIBRATION1);
	}
}

/* reads a 16 bit value from board's eeprom */
static unsigned int cb_pcidda_read_eeprom(struct comedi_device *dev,
					  unsigned int address)
{
	unsigned int i;
	unsigned int cal2_bits;
	unsigned int value;
	/* one caldac for every two dac channels */
	const int max_num_caldacs = 4;
	/* bits to send to tell eeprom we want to read */
	const int read_instruction = 0x6;
	const int instruction_length = 3;
	const int address_length = 8;

	/*  send serial output stream to eeprom */
	cal2_bits = SELECT_EEPROM_BIT | DESELECT_REF_DAC_BIT | DUMMY_BIT;
	/*  deactivate caldacs (one caldac for every two channels) */
	for (i = 0; i < max_num_caldacs; i++)
		cal2_bits |= DESELECT_CALDAC_BIT(i);
	outw_p(cal2_bits, devpriv->dac + DACALIBRATION2);

	/*  tell eeprom we want to read */
	cb_pcidda_serial_out(dev, read_instruction, instruction_length);
	/*  send address we want to read from */
	cb_pcidda_serial_out(dev, address, address_length);

	value = cb_pcidda_serial_in(dev);

	/*  deactivate eeprom */
	cal2_bits &= ~SELECT_EEPROM_BIT;
	outw_p(cal2_bits, devpriv->dac + DACALIBRATION2);

	return value;
}

/* writes to 8 bit calibration dacs */
static void cb_pcidda_write_caldac(struct comedi_device *dev,
				   unsigned int caldac, unsigned int channel,
				   unsigned int value)
{
	unsigned int cal2_bits;
	unsigned int i;
	/* caldacs use 3 bit channel specification */
	const int num_channel_bits = 3;
	const int num_caldac_bits = 8;	/*  8 bit calibration dacs */
	/* one caldac for every two dac channels */
	const int max_num_caldacs = 4;

	/* write 3 bit channel */
	cb_pcidda_serial_out(dev, channel, num_channel_bits);
	/*  write 8 bit caldac value */
	cb_pcidda_serial_out(dev, value, num_caldac_bits);

/*
* latch stream into appropriate caldac deselect reference dac
*/
	cal2_bits = DESELECT_REF_DAC_BIT | DUMMY_BIT;
	/*  deactivate caldacs (one caldac for every two channels) */
	for (i = 0; i < max_num_caldacs; i++)
		cal2_bits |= DESELECT_CALDAC_BIT(i);
	/*  activate the caldac we want */
	cal2_bits &= ~DESELECT_CALDAC_BIT(caldac);
	outw_p(cal2_bits, devpriv->dac + DACALIBRATION2);
	/*  deactivate caldac */
	cal2_bits |= DESELECT_CALDAC_BIT(caldac);
	outw_p(cal2_bits, devpriv->dac + DACALIBRATION2);
}

/* returns caldac that calibrates given analog out channel */
static unsigned int caldac_number(unsigned int channel)
{
	return channel / 2;
}

/* returns caldac channel that provides fine gain for given ao channel */
static unsigned int fine_gain_channel(unsigned int ao_channel)
{
	return 4 * (ao_channel % 2);
}

/* returns caldac channel that provides coarse gain for given ao channel */
static unsigned int coarse_gain_channel(unsigned int ao_channel)
{
	return 1 + 4 * (ao_channel % 2);
}

/* returns caldac channel that provides coarse offset for given ao channel */
static unsigned int coarse_offset_channel(unsigned int ao_channel)
{
	return 2 + 4 * (ao_channel % 2);
}

/* returns caldac channel that provides fine offset for given ao channel */
static unsigned int fine_offset_channel(unsigned int ao_channel)
{
	return 3 + 4 * (ao_channel % 2);
}

/* returns eeprom address that provides offset for given ao channel and range */
static unsigned int offset_eeprom_address(unsigned int ao_channel,
					  unsigned int range)
{
	return 0x7 + 2 * range + 12 * ao_channel;
}

/*
 * returns eeprom address that provides gain calibration for given ao
 * channel and range
 */
static unsigned int gain_eeprom_address(unsigned int ao_channel,
					unsigned int range)
{
	return 0x8 + 2 * range + 12 * ao_channel;
}

/*
 * returns upper byte of eeprom entry, which gives the coarse adjustment
 * values
 */
static unsigned int eeprom_coarse_byte(unsigned int word)
{
	return (word >> 8) & 0xff;
}

/* returns lower byte of eeprom entry, which gives the fine adjustment values */
static unsigned int eeprom_fine_byte(unsigned int word)
{
	return word & 0xff;
}

/* set caldacs to eeprom values for given channel and range */
static void cb_pcidda_calibrate(struct comedi_device *dev, unsigned int channel,
				unsigned int range)
{
	unsigned int coarse_offset, fine_offset, coarse_gain, fine_gain;

	/* remember range so we can tell when we need to readjust calibration */
	devpriv->ao_range[channel] = range;

	/*  get values from eeprom data */
	coarse_offset =
	    eeprom_coarse_byte(devpriv->eeprom_data
			       [offset_eeprom_address(channel, range)]);
	fine_offset =
	    eeprom_fine_byte(devpriv->eeprom_data
			     [offset_eeprom_address(channel, range)]);
	coarse_gain =
	    eeprom_coarse_byte(devpriv->eeprom_data
			       [gain_eeprom_address(channel, range)]);
	fine_gain =
	    eeprom_fine_byte(devpriv->eeprom_data
			     [gain_eeprom_address(channel, range)]);

	/*  set caldacs */
	cb_pcidda_write_caldac(dev, caldac_number(channel),
			       coarse_offset_channel(channel), coarse_offset);
	cb_pcidda_write_caldac(dev, caldac_number(channel),
			       fine_offset_channel(channel), fine_offset);
	cb_pcidda_write_caldac(dev, caldac_number(channel),
			       coarse_gain_channel(channel), coarse_gain);
	cb_pcidda_write_caldac(dev, caldac_number(channel),
			       fine_gain_channel(channel), fine_gain);
}

static struct comedi_driver cb_pcidda_driver = {
	.driver_name	= "cb_pcidda",
	.module		= THIS_MODULE,
	.attach		= cb_pcidda_attach,
	.detach		= cb_pcidda_detach,
};

static int __devinit cb_pcidda_pci_probe(struct pci_dev *dev,
					 const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &cb_pcidda_driver);
}

static void __devexit cb_pcidda_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(cb_pcidda_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0020) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0021) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0022) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0023) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0024) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0025) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcidda_pci_table);

static struct pci_driver cb_pcidda_pci_driver = {
	.name		= "cb_pcidda",
	.id_table	= cb_pcidda_pci_table,
	.probe		= cb_pcidda_pci_probe,
	.remove		= __devexit_p(cb_pcidda_pci_remove),
};
module_comedi_pci_driver(cb_pcidda_driver, cb_pcidda_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

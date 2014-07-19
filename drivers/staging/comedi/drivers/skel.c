/*
    comedi/drivers/skel.c
    Skeleton code for a Comedi driver

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
*/
/*
Driver: skel
Description: Skeleton driver, an example for driver writers
Devices:
Author: ds
Updated: Mon, 18 Mar 2002 15:34:01 -0800
Status: works

This driver is a documented example on how Comedi drivers are
written.

Configuration Options:
  none
*/

/*
 * The previous block comment is used to automatically generate
 * documentation in Comedi and Comedilib.  The fields:
 *
 *  Driver: the name of the driver
 *  Description: a short phrase describing the driver.  Don't list boards.
 *  Devices: a full list of the boards that attempt to be supported by
 *    the driver.  Format is "(manufacturer) board name [comedi name]",
 *    where comedi_name is the name that is used to configure the board.
 *    See the comment near board_name: in the struct comedi_driver structure
 *    below.  If (manufacturer) or [comedi name] is missing, the previous
 *    value is used.
 *  Author: you
 *  Updated: date when the _documentation_ was last updated.  Use 'date -R'
 *    to get a value for this.
 *  Status: a one-word description of the status.  Valid values are:
 *    works - driver works correctly on most boards supported, and
 *      passes comedi_test.
 *    unknown - unknown.  Usually put there by ds.
 *    experimental - may not work in any particular release.  Author
 *      probably wants assistance testing it.
 *    bitrotten - driver has not been update in a long time, probably
 *      doesn't work, and probably is missing support for significant
 *      Comedi interface features.
 *    untested - author probably wrote it "blind", and is believed to
 *      work, but no confirmation.
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

#include <linux/module.h>
#include <linux/pci.h>

#include "../comedidev.h"

#include "comedi_fc.h"

/* Imaginary registers for the imaginary board */
#define SKEL_START_AI_CONV	0
#define SKEL_AI_READ		0

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
enum skel_boardid {
	BOARD_SKEL100,
	BOARD_SKEL200,
};

struct skel_board {
	const char *name;
	int ai_chans;
	int ai_bits;
	int have_dio;
};

static const struct skel_board skel_boards[] = {
	[BOARD_SKEL100] = {
		.name		= "skel-100",
		.ai_chans	= 16,
		.ai_bits	= 12,
		.have_dio	= 1,
	},
	[BOARD_SKEL200] = {
		.name		= "skel-200",
		.ai_chans	= 8,
		.ai_bits	= 16,
	},
};

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.
 */
struct skel_private {

	int data;

	/* Used for AO readback */
	unsigned int ao_readback[2];
};

/* This function doesn't require a particular form, this is just
 * what happens to be used in some of the drivers.  It should
 * convert ns nanoseconds to a counter value suitable for programming
 * the device.  Also, it should adjust ns so that it cooresponds to
 * the actual time that the device will use. */
static int skel_ns_to_timer(unsigned int *ns, unsigned int flags)
{
	/* trivial timer */
	/* if your timing is done through two cascaded timers, the
	 * i8253_cascade_ns_to_timer() function in 8253.h can be
	 * very helpful.  There are also i8254_load() and i8254_mm_load()
	 * which can be used to load values into the ubiquitous 8254 counters
	 */

	return *ns;
}

/*
 * This function doesn't require a particular form, this is just
 * what happens to be used in some of the drivers. The comedi_timeout()
 * helper uses this callback to check for the end-of-conversion while
 * waiting for up to 1 second. This function should return 0 when the
 * conversion is finished and -EBUSY to keep waiting. Any other errno
 * will terminate comedi_timeout() and return that errno to the caller.
 * If the timeout occurs, comedi_timeout() will return -ETIMEDOUT.
 */
static int skel_ai_eoc(struct comedi_device *dev,
		       struct comedi_subdevice *s,
		       struct comedi_insn *insn,
		       unsigned long context)
{
	unsigned int status;

	/* status = inb(dev->iobase + SKEL_STATUS); */
	status = 1;
	if (status)
		return 0;
	return -EBUSY;
}

/*
 * "instructions" read/write data in "one-shot" or "software-triggered"
 * mode.
 */
static int skel_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	const struct skel_board *thisboard = comedi_board(dev);
	int n;
	unsigned int d;
	int ret;

	/* a typical programming sequence */

	/* write channel to multiplexer */
	/* outw(chan,dev->iobase + SKEL_MUX); */

	/* don't wait for mux to settle */

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		/* outw(0,dev->iobase + SKEL_CONVERT); */

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, skel_ai_eoc, 0);
		if (ret)
			return ret;

		/* read data */
		/* d = inw(dev->iobase + SKEL_AI_DATA); */
		d = 0;

		/* mangle the data as necessary */
		d ^= 1 << (thisboard->ai_bits - 1);

		data[n] = d;
	}

	/* return the number of samples read/written */
	return n;
}

/*
 * cmdtest tests a particular command to see if it is valid.
 * Using the cmdtest ioctl, a user can create a valid cmd
 * and then have it executes by the cmd ioctl.
 *
 * cmdtest returns 1,2,3,4 or 0, depending on which tests
 * the command passes.
 */
static int skel_ai_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int arg;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_TIMER | TRIG_EXT);
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

#define MAX_SPEED	10000	/* in nanoseconds */
#define MIN_SPEED	1000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 MAX_SPEED);
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						 MIN_SPEED);
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 9);
	}

	if (cmd->convert_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg, MAX_SPEED);
		err |= cfc_check_trigger_arg_max(&cmd->convert_arg, MIN_SPEED);
	} else {
		/* external trigger */
		/* see above */
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 9);
	}

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		arg = cmd->scan_begin_arg;
		skel_ns_to_timer(&arg, cmd->flags);
		err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, arg);
	}
	if (cmd->convert_src == TRIG_TIMER) {
		arg = cmd->convert_arg;
		skel_ns_to_timer(&arg, cmd->flags);
		err |= cfc_check_trigger_arg_is(&cmd->convert_arg, arg);

		if (cmd->scan_begin_src == TRIG_TIMER) {
			arg = cmd->convert_arg * cmd->scan_end_arg;
			err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
							 arg);
		}
	}

	if (err)
		return 4;

	return 0;
}

static int skel_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	struct skel_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; i++) {
		/* a typical programming sequence */
		/* outw(data[i],dev->iobase + SKEL_DA0 + chan); */
		devpriv->ao_readback[chan] = data[i];
	}

	/* return the number of samples read/written */
	return i;
}

/* AO subdevices should have a read insn as well as a write insn.
 * Usually this means copying a value stored in devpriv. */
static int skel_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	struct skel_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

/*
 * DIO devices are slightly special. Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels. The
 * comedi core can convert between insn_bits and insn_read/write.
 */
static int skel_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	/*
	 * The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit.
	 *
	 * The core provided comedi_dio_update_state() function can
	 * be used to handle the internal state update to DIO subdevices
	 * with <= 32 channels. This function will return '0' if the
	 * state does not change or the mask of the channels that need
	 * to be updated.
	 */
	if (comedi_dio_update_state(s, data)) {
		/* Write out the new digital output lines */
		/* outw(s->state, dev->iobase + SKEL_DIO); */
	}

	/*
	 * On return, data[1] contains the value of the digital
	 * input and output lines.
	 */
	/* data[1] = inw(dev->iobase + SKEL_DIO); */

	/*
	 * Or we could just return the software copy of the output
	 * values if it was a purely digital output subdevice.
	 */
	/* data[1] = s->state; */

	return insn->n;
}

static int skel_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	int ret;

	/*
	 * The input or output configuration of each digital line is
	 * configured by special insn_config instructions.
	 *
	 * chanspec contains the channel to be changed
	 * data[0] contains the instruction to perform on the channel
	 *
	 * Normally the core provided comedi_dio_insn_config() function
	 * can be used to handle the boilerplpate.
	 */
	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	/* Update the hardware to the new configuration */
	/* outw(s->io_bits, dev->iobase + SKEL_DIO_CONFIG); */

	return insn->n;
}

/*
 * Handle common part of skel_attach() and skel_auto_attach().
 */
static int skel_common_attach(struct comedi_device *dev)
{
	const struct skel_board *thisboard = comedi_board(dev);
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* dev->read_subdev=s; */
	/* analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	/* we support single-ended (ground) and differential */
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan = thisboard->ai_chans;
	s->maxdata = (1 << thisboard->ai_bits) - 1;
	s->range_table = &range_bipolar10;
	s->len_chanlist = 16;	/* This is the maximum chanlist length that
				   the board can handle */
	s->insn_read = skel_ai_rinsn;
/*
*       s->subdev_flags |= SDF_CMD_READ;
*       s->do_cmd = skel_ai_cmd;
*/
	s->do_cmdtest = skel_ai_cmdtest;

	s = &dev->subdevices[1];
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 1;
	s->maxdata = 0xffff;
	s->range_table = &range_bipolar5;
	s->insn_write = skel_ao_winsn;
	s->insn_read = skel_ao_rinsn;

	s = &dev->subdevices[2];
	/* digital i/o subdevice */
	if (thisboard->have_dio) {
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
		s->n_chan = 16;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = skel_dio_insn_bits;
		s->insn_config = skel_dio_insn_config;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	return 0;
}

/*
 * _attach is called by the Comedi core to configure the driver
 * for a particular board in response to the COMEDI_DEVCONFIG ioctl for
 * a matching board or driver name.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that address.
 *
 * Drivers that handle only PCI or USB devices do not usually support
 * manual attachment of those devices via the COMEDI_DEVCONFIG ioctl, so
 * those drivers do not have an _attach function; they just have an
 * _auto_attach function instead.  (See skel_auto_attach() for an example
 * of such a function.)
 */
static int skel_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct skel_board *thisboard;
	struct skel_private *devpriv;

/*
 * If you can probe the device to determine what device in a series
 * it is, this is the place to do it.  Otherwise, dev->board_ptr
 * should already be initialized.
 */
	/* dev->board_ptr = skel_probe(dev, it); */

	thisboard = comedi_board(dev);

	/*
	 * The dev->board_name is initialized by the comedi core before
	 * calling the (*attach) function. It can be optionally set by
	 * the driver if additional probing has been done.
	 */
	/* dev->board_name = thisboard->name; */

	/* Allocate the private data */
	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

/*
 * Supported boards are usually either auto-attached via the
 * Comedi driver's _auto_attach routine, or manually attached via the
 * Comedi driver's _attach routine.  In most cases, attempts to
 * manual attach boards that are usually auto-attached should be
 * rejected by this function.
 */
/*
 *	if (thisboard->bustype == pci_bustype) {
 *		dev_err(dev->class_dev,
 *			"Manual attachment of PCI board '%s' not supported\n",
 *			thisboard->name);
 *	}
 */

/*
 * For ISA boards, get the i/o base address from it->options[],
 * request the i/o region and set dev->iobase * from it->options[].
 * If using interrupts, get the IRQ number from it->options[].
 */

	/*
	 * Call a common function to handle the remaining things to do for
	 * attaching ISA or PCI boards.  (Extra parameters could be added
	 * to pass additional information such as IRQ number.)
	 */
	return skel_common_attach(dev);
}

/*
 * _auto_attach is called via comedi_pci_auto_config() (or
 * comedi_usb_auto_config(), etc.) to handle devices that can be attached
 * to the Comedi core automatically without the COMEDI_DEVCONFIG ioctl.
 *
 * The context parameter is driver dependent.
 */
static int skel_auto_attach(struct comedi_device *dev,
			    unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct skel_board *thisboard = NULL;
	struct skel_private *devpriv;
	int ret;

	/* Hack to allow unused code to be optimized out. */
	if (!IS_ENABLED(CONFIG_COMEDI_PCI_DRIVERS))
		return -EINVAL;

	/*
	 * In this example, the _auto_attach is for a PCI device.
	 *
	 * The 'context' passed to this function is the id->driver_data
	 * associated with the PCI device found in the id_table during
	 * the modprobe. This 'context' is the index of the entry in
	 * skel_boards[i] that contains the boardinfo for the PCI device.
	 */
	if (context < ARRAY_SIZE(skel_boards))
		thisboard = &skel_boards[context];
	if (!thisboard)
		return -ENODEV;

	/*
	 * Point the struct comedi_device to the matching board info
	 * and set the board name.
	 */
	dev->board_ptr = thisboard;
	dev->board_name = thisboard->name;

	/* Allocate the private data */
	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/* Enable the PCI device. */
	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	/*
	 * Record the fact that the PCI device is enabled so that it can
	 * be disabled during _detach().
	 *
	 * For this example driver, we assume PCI BAR 0 is the main I/O
	 * region for the board registers and use dev->iobase to hold the
	 * I/O base address and to indicate that the PCI device has been
	 * enabled.
	 *
	 * (For boards with memory-mapped registers, dev->iobase is not
	 * usually needed for register access, so can just be set to 1
	 * to indicate that the PCI device has been enabled.)
	 */
	dev->iobase = pci_resource_start(pcidev, 0);

	/*
	 * Call a common function to handle the remaining things to do for
	 * attaching ISA or PCI boards.  (Extra parameters could be added
	 * to pass additional information such as IRQ number.)
	 */
	return skel_common_attach(dev);
}

/*
 * _detach is called to deconfigure a device.  It should deallocate
 * resources.
 * This function is also called when _attach() fails, so it should be
 * careful not to release resources that were not necessarily
 * allocated by _attach().  dev->private and dev->subdevices are
 * deallocated automatically by the core.
 */
static void skel_detach(struct comedi_device *dev)
{
	const struct skel_board *thisboard = comedi_board(dev);
	struct skel_private *devpriv = dev->private;

	if (!thisboard || !devpriv)
		return;

/*
 * Do common stuff such as freeing IRQ, unmapping remapped memory
 * regions, etc., being careful to check that the stuff is valid given
 * that _detach() is called even when _attach() or _auto_attach() return
 * an error.
 */

	if (IS_ENABLED(CONFIG_COMEDI_PCI_DRIVERS) /* &&
	    thisboard->bustype == pci_bustype */) {
		/*
		 * PCI board
		 *
		 * If PCI device enabled by _auto_attach() (or _attach()),
		 * disable it here.
		 */
		comedi_pci_disable(dev);
	} else {
		/*
		 * ISA board
		 *
		 * Release the first I/O region requested during the
		 * _attach(). This is safe to call even if the request
		 * failed. If any additional I/O regions are requested
		 * they need to be released by the driver.
		 */
		comedi_legacy_detach(dev);
	}
}

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static struct comedi_driver skel_driver = {
	.driver_name = "dummy",
	.module = THIS_MODULE,
	.attach = skel_attach,
	.auto_attach = skel_auto_attach,
	.detach = skel_detach,
/* It is not necessary to implement the following members if you are
 * writing a driver for a ISA PnP or PCI card */
	/* Most drivers will support multiple types of boards by
	 * having an array of board structures.  These were defined
	 * in skel_boards[] above.  Note that the element 'name'
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
	.board_name = &skel_boards[0].name,
	.offset = sizeof(struct skel_board),
	.num_names = ARRAY_SIZE(skel_boards),
};

#ifdef CONFIG_COMEDI_PCI_DRIVERS

static int skel_pci_probe(struct pci_dev *dev,
			  const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &skel_driver, id->driver_data);
}

/*
 * Please add your PCI vendor ID to comedidev.h, and it will
 * be forwarded upstream.
 */
#define PCI_VENDOR_ID_SKEL	0xdafe

/*
 * This is used by modprobe to translate PCI IDs to drivers.
 * Should only be used for PCI and ISA-PnP devices
 */
static const struct pci_device_id skel_pci_table[] = {
	{ PCI_VDEVICE(SKEL, 0x0100), BOARD_SKEL100 },
	{ PCI_VDEVICE(SKEL, 0x0200), BOARD_SKEL200 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, skel_pci_table);

static struct pci_driver skel_pci_driver = {
	.name		= "dummy",
	.id_table	= skel_pci_table,
	.probe		= skel_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(skel_driver, skel_pci_driver);
#else
module_comedi_driver(skel_driver);
#endif

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

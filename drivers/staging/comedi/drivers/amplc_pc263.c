/*
    comedi/drivers/amplc_pc263.c
    Driver for Amplicon PC263 and PCI263 relay boards.

    Copyright (C) 2002 MEV Ltd. <http://www.mev.co.uk/>

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
Driver: amplc_pc263
Description: Amplicon PC263, PCI263
Author: Ian Abbott <abbotti@mev.co.uk>
Devices: [Amplicon] PC263 (pc263), PCI263 (pci263 or amplc_pc263)
Updated: Wed, 22 Oct 2008 14:10:53 +0100
Status: works

Configuration options - PC263:
  [0] - I/O port base address

Configuration options - PCI263:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first available PCI device will be
  used.

Each board appears as one subdevice, with 16 digital outputs, each
connected to a reed-relay. Relay contacts are closed when output is 1.
The state of the outputs can be read.
*/

#include "../comedidev.h"

#define PC263_DRIVER_NAME	"amplc_pc263"

/* PCI263 PCI configuration register information */
#define PCI_VENDOR_ID_AMPLICON 0x14dc
#define PCI_DEVICE_ID_AMPLICON_PCI263 0x000c
#define PCI_DEVICE_ID_INVALID 0xffff

/* PC263 / PCI263 registers */
#define PC263_IO_SIZE	2

/*
 * Board descriptions for Amplicon PC263 / PCI263.
 */

enum pc263_bustype { isa_bustype, pci_bustype };
enum pc263_model { pc263_model, pci263_model, anypci_model };

struct pc263_board {
	const char *name;
	unsigned short devid;
	enum pc263_bustype bustype;
	enum pc263_model model;
};
static const struct pc263_board pc263_boards[] = {
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_ISA)
	{
		.name = "pc263",
		.bustype = isa_bustype,
		.model = pc263_model,
	},
#endif
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI)
	{
		.name = "pci263",
		.devid = PCI_DEVICE_ID_AMPLICON_PCI263,
		.bustype = pci_bustype,
		.model = pci263_model,
	},
	{
		.name = PC263_DRIVER_NAME,
		.devid = PCI_DEVICE_ID_INVALID,
		.bustype = pci_bustype,
		.model = anypci_model,	/* wildcard */
	},
#endif
};

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.
*/
struct pc263_private {
	/* PCI device. */
	struct pci_dev *pci_dev;
};

/*
 * This function looks for a board matching the supplied PCI device.
 */
static const struct pc263_board *pc263_find_pci_board(struct pci_dev *pci_dev)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pc263_boards); i++)
		if (pc263_boards[i].bustype == pci_bustype &&
		    pci_dev->device == pc263_boards[i].devid)
			return &pc263_boards[i];
	return NULL;
}


/*
 * This function looks for a PCI device matching the requested board name,
 * bus and slot.
 */
static struct pci_dev *pc263_find_pci_dev(struct comedi_device *dev,
					  struct comedi_devconfig *it)
{
	const struct pc263_board *thisboard = comedi_board(dev);
	struct pci_dev *pci_dev = NULL;
	int bus = it->options[0];
	int slot = it->options[1];

	for_each_pci_dev(pci_dev) {
		if (bus || slot) {
			if (bus != pci_dev->bus->number ||
			    slot != PCI_SLOT(pci_dev->devfn))
				continue;
		}
		if (pci_dev->vendor != PCI_VENDOR_ID_AMPLICON)
			continue;

		if (thisboard->model == anypci_model) {
			/* Wildcard board matches any supported PCI board. */
			const struct pc263_board *foundboard;

			foundboard = pc263_find_pci_board(pci_dev);
			if (foundboard == NULL)
				continue;
			/* Replace wildcard board_ptr. */
			dev->board_ptr = thisboard = foundboard;
		} else {
			/* Match specific model name. */
			if (pci_dev->device != thisboard->devid)
				continue;
		}
		return pci_dev;
	}
	dev_err(dev->class_dev,
		"No supported board found! (req. bus %d, slot %d)\n",
		bus, slot);
	return NULL;
}
/*
 * This function checks and requests an I/O region, reporting an error
 * if there is a conflict.
 */
static int pc263_request_region(struct comedi_device *dev, unsigned long from,
				unsigned long extent)
{
	if (!from || !request_region(from, extent, PC263_DRIVER_NAME)) {
		dev_err(dev->class_dev, "I/O port conflict (%#lx,%lu)!\n",
			from, extent);
		return -EIO;
	}
	return 0;
}

static int pc263_do_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		/* Write out the new digital output lines */
		outb(s->state & 0xFF, dev->iobase);
		outb(s->state >> 8, dev->iobase + 1);
	}
	return insn->n;
}

static void pc263_report_attach(struct comedi_device *dev)
{
	const struct pc263_board *thisboard = comedi_board(dev);
	struct pc263_private *devpriv = dev->private;
	char tmpbuf[40];

	if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_ISA) &&
	    thisboard->bustype == isa_bustype)
		snprintf(tmpbuf, sizeof(tmpbuf), "(base %#lx) ", dev->iobase);
	else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI) &&
		 thisboard->bustype == pci_bustype)
		snprintf(tmpbuf, sizeof(tmpbuf), "(pci %s) ",
			 pci_name(devpriv->pci_dev));
	else
		tmpbuf[0] = '\0';
	dev_info(dev->class_dev, "%s %sattached\n", dev->board_name, tmpbuf);
}

static int pc263_common_attach(struct comedi_device *dev, unsigned long iobase)
{
	const struct pc263_board *thisboard = comedi_board(dev);
	struct comedi_subdevice *s;
	int ret;

	dev->board_name = thisboard->name;
	dev->iobase = iobase;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = dev->subdevices + 0;
	/* digital output subdevice */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = pc263_do_insn_bits;
	/* read initial relay state */
	s->state = inb(dev->iobase) | (inb(dev->iobase + 1) << 8);

	pc263_report_attach(dev);
	return 1;
}

static int pc263_pci_common_attach(struct comedi_device *dev,
				   struct pci_dev *pci_dev)
{
	struct pc263_private *devpriv = dev->private;
	unsigned long iobase;
	int ret;

	devpriv->pci_dev = pci_dev;
	ret = comedi_pci_enable(pci_dev, PC263_DRIVER_NAME);
	if (ret < 0) {
		dev_err(dev->class_dev,
			"error! cannot enable PCI device and request regions!\n");
		return ret;
	}
	iobase = pci_resource_start(pci_dev, 2);
	return pc263_common_attach(dev, iobase);
}

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int pc263_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pc263_board *thisboard = comedi_board(dev);
	int ret;

	dev_info(dev->class_dev, PC263_DRIVER_NAME ": attach\n");

	/* Process options and reserve resources according to bus type. */
	if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_ISA) &&
	    thisboard->bustype == isa_bustype) {
		unsigned long iobase = it->options[0];
		ret = pc263_request_region(dev, iobase, PC263_IO_SIZE);
		if (ret < 0)
			return ret;
		return pc263_common_attach(dev, iobase);
	} else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI) &&
		   thisboard->bustype == pci_bustype) {
		struct pci_dev *pci_dev;

		ret = alloc_private(dev, sizeof(struct pc263_private));
		if (ret < 0) {
			dev_err(dev->class_dev, "error! out of memory!\n");
			return ret;
		}
		pci_dev = pc263_find_pci_dev(dev, it);
		if (!pci_dev)
			return -EIO;
		return pc263_pci_common_attach(dev, pci_dev);
	} else {
		dev_err(dev->class_dev, PC263_DRIVER_NAME
			": BUG! cannot determine board type!\n");
		return -EINVAL;
	}
}
/*
 * The attach_pci hook (if non-NULL) is called at PCI probe time in preference
 * to the "manual" attach hook.  dev->board_ptr is NULL on entry.  There should
 * be a board entry matching the supplied PCI device.
 */
static int __devinit pc263_attach_pci(struct comedi_device *dev,
				      struct pci_dev *pci_dev)
{
	int ret;

	if (!IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI))
		return -EINVAL;

	dev_info(dev->class_dev, PC263_DRIVER_NAME ": attach pci %s\n",
		 pci_name(pci_dev));
	ret = alloc_private(dev, sizeof(struct pc263_private));
	if (ret < 0) {
		dev_err(dev->class_dev, "error! out of memory!\n");
		return ret;
	}
	dev->board_ptr = pc263_find_pci_board(pci_dev);
	if (dev->board_ptr == NULL) {
		dev_err(dev->class_dev, "BUG! cannot determine board type!\n");
		return -EINVAL;
	}
	return pc263_pci_common_attach(dev, pci_dev);
}

static void pc263_detach(struct comedi_device *dev)
{
	struct pc263_private *devpriv = dev->private;

	if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI) && devpriv &&
	    devpriv->pci_dev) {
		if (dev->iobase)
			comedi_pci_disable(devpriv->pci_dev);
		pci_dev_put(devpriv->pci_dev);
	} else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_ISA)) {
		if (dev->iobase)
			release_region(dev->iobase, PC263_IO_SIZE);
	}
}

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static struct comedi_driver amplc_pc263_driver = {
	.driver_name = PC263_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = pc263_attach,
	.attach_pci = pc263_attach_pci,
	.detach = pc263_detach,
	.board_name = &pc263_boards[0].name,
	.offset = sizeof(struct pc263_board),
	.num_names = ARRAY_SIZE(pc263_boards),
};

#if IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI)
static DEFINE_PCI_DEVICE_TABLE(pc263_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI263) },
	{0}
};
MODULE_DEVICE_TABLE(pci, pc263_pci_table);

static int __devinit amplc_pc263_pci_probe(struct pci_dev *dev,
						  const struct pci_device_id
						  *ent)
{
	return comedi_pci_auto_config(dev, &amplc_pc263_driver);
}

static void __devexit amplc_pc263_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver amplc_pc263_pci_driver = {
	.name = PC263_DRIVER_NAME,
	.id_table = pc263_pci_table,
	.probe = &amplc_pc263_pci_probe,
	.remove = __devexit_p(&amplc_pc263_pci_remove)
};
module_comedi_pci_driver(amplc_pc263_driver, amplc_pc263_pci_driver);
#else
module_comedi_driver(amplc_pc263_driver);
#endif

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

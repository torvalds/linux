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
	const char *fancy_name;
	unsigned short devid;
	enum pc263_bustype bustype;
	enum pc263_model model;
};
static const struct pc263_board pc263_boards[] = {
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_ISA)
	{
		.name = "pc263",
		.fancy_name = "PC263",
		.bustype = isa_bustype,
		.model = pc263_model,
	},
#endif
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI)
	{
		.name = "pci263",
		.fancy_name = "PCI263",
		.devid = PCI_DEVICE_ID_AMPLICON_PCI263,
		.bustype = pci_bustype,
		.model = pci263_model,
	},
	{
		.name = PC263_DRIVER_NAME,
		.fancy_name = PC263_DRIVER_NAME,
		.devid = PCI_DEVICE_ID_INVALID,
		.bustype = pci_bustype,
		.model = anypci_model,	/* wildcard */
	},
#endif
};

#if IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI)
static DEFINE_PCI_DEVICE_TABLE(pc263_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI263) },
	{0}
};

MODULE_DEVICE_TABLE(pci, pc263_pci_table);
#endif /* CONFIG_COMEDI_AMPLC_PC263_PCI */

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct pc263_board *)dev->board_ptr)

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.
*/
struct pc263_private {
	/* PCI device. */
	struct pci_dev *pci_dev;
};

#define devpriv ((struct pc263_private *)dev->private)

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int pc263_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static void pc263_detach(struct comedi_device *dev);
static struct comedi_driver amplc_pc263_driver = {
	.driver_name = PC263_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = pc263_attach,
	.detach = pc263_detach,
	.board_name = &pc263_boards[0].name,
	.offset = sizeof(struct pc263_board),
	.num_names = ARRAY_SIZE(pc263_boards),
};

static int pc263_request_region(unsigned minor, unsigned long from,
				unsigned long extent);
static int pc263_do_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data);

/*
 * This function looks for a PCI device matching the requested board name,
 * bus and slot.
 */
static int
pc263_find_pci(struct comedi_device *dev, int bus, int slot,
	       struct pci_dev **pci_dev_p)
{
	struct pci_dev *pci_dev = NULL;

	*pci_dev_p = NULL;

	/* Look for matching PCI device. */
	for (pci_dev = pci_get_device(PCI_VENDOR_ID_AMPLICON, PCI_ANY_ID, NULL);
	     pci_dev != NULL;
	     pci_dev = pci_get_device(PCI_VENDOR_ID_AMPLICON,
				      PCI_ANY_ID, pci_dev)) {
		/* If bus/slot specified, check them. */
		if (bus || slot) {
			if (bus != pci_dev->bus->number
			    || slot != PCI_SLOT(pci_dev->devfn))
				continue;
		}
		if (thisboard->model == anypci_model) {
			/* Match any supported model. */
			int i;

			for (i = 0; i < ARRAY_SIZE(pc263_boards); i++) {
				if (pc263_boards[i].bustype != pci_bustype)
					continue;
				if (pci_dev->device == pc263_boards[i].devid) {
					/* Change board_ptr to matched board. */
					dev->board_ptr = &pc263_boards[i];
					break;
				}
			}
			if (i == ARRAY_SIZE(pc263_boards))
				continue;
		} else {
			/* Match specific model name. */
			if (pci_dev->device != thisboard->devid)
				continue;
		}

		/* Found a match. */
		*pci_dev_p = pci_dev;
		return 0;
	}
	/* No match found. */
	if (bus || slot) {
		printk(KERN_ERR
		       "comedi%d: error! no %s found at pci %02x:%02x!\n",
		       dev->minor, thisboard->name, bus, slot);
	} else {
		printk(KERN_ERR "comedi%d: error! no %s found!\n",
		       dev->minor, thisboard->name);
	}
	return -EIO;
}

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int pc263_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase = 0;
	int ret;

	printk(KERN_DEBUG "comedi%d: %s: attach\n", dev->minor,
	       PC263_DRIVER_NAME);

	/* Process options and reserve resources according to bus type. */
	if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_ISA) &&
	    thisboard->bustype == isa_bustype) {
		iobase = it->options[0];
		ret = pc263_request_region(dev->minor, iobase, PC263_IO_SIZE);
		if (ret < 0)
			return ret;
	} else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI) &&
		   thisboard->bustype == pci_bustype) {
		struct pci_dev *pci_dev = NULL;
		int bus, slot;

		ret = alloc_private(dev, sizeof(struct pc263_private));
		if (ret < 0) {
			printk(KERN_ERR "comedi%d: error! out of memory!\n",
			       dev->minor);
			return ret;
		}
		bus = it->options[0];
		slot = it->options[1];
		ret = pc263_find_pci(dev, bus, slot, &pci_dev);
		if (ret < 0)
			return ret;
		devpriv->pci_dev = pci_dev;
		ret = comedi_pci_enable(pci_dev, PC263_DRIVER_NAME);
		if (ret < 0) {
			printk(KERN_ERR
			       "comedi%d: error! cannot enable PCI device and "
				"request regions!\n",
			       dev->minor);
			return ret;
		}
		iobase = pci_resource_start(pci_dev, 2);
	} else {
		printk(KERN_ERR
		       "comedi%d: %s: BUG! cannot determine board type!\n",
		       dev->minor, PC263_DRIVER_NAME);
		return -EINVAL;
	}

	dev->board_name = thisboard->name;
	dev->iobase = iobase;

	ret = alloc_subdevices(dev, 1);
	if (ret < 0) {
		printk(KERN_ERR "comedi%d: error! out of memory!\n",
		       dev->minor);
		return ret;
	}

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

	printk(KERN_INFO "comedi%d: %s ", dev->minor, dev->board_name);
	if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_ISA) &&
	    thisboard->bustype == isa_bustype)
		printk("(base %#lx) ", iobase);
	else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI) &&
		 thisboard->bustype == pci_bustype)
		printk("(pci %s) ", pci_name(devpriv->pci_dev));

	printk("attached\n");

	return 1;
}

static void pc263_detach(struct comedi_device *dev)
{
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
 * This function checks and requests an I/O region, reporting an error
 * if there is a conflict.
 */
static int pc263_request_region(unsigned minor, unsigned long from,
				unsigned long extent)
{
	if (!from || !request_region(from, extent, PC263_DRIVER_NAME)) {
		printk(KERN_ERR "comedi%d: I/O port conflict (%#lx,%lu)!\n",
		       minor, from, extent);
		return -EIO;
	}
	return 0;
}

static int pc263_do_insn_bits(struct comedi_device *dev,
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
		outb(s->state & 0xFF, dev->iobase);
		outb(s->state >> 8, dev->iobase + 1);
	}

	/* on return, data[1] contains the value of the digital
	 * input and output lines. */
	/* or we could just return the software copy of the output values if
	 * it was a purely digital output subdevice */
	data[1] = s->state;

	return 2;
}

#if IS_ENABLED(CONFIG_COMEDI_AMPLC_PC263_PCI)
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

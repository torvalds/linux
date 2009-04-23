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

#include "comedi_pci.h"

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
	{
	.name = "pc263",
	.fancy_name = "PC263",
	.bustype = isa_bustype,
	.model = pc263_model,
		},
#ifdef CONFIG_COMEDI_PCI
	{
	.name = "pci263",
	.fancy_name = "PCI263",
	.devid = PCI_DEVICE_ID_AMPLICON_PCI263,
	.bustype = pci_bustype,
	.model = pci263_model,
		},
#endif
#ifdef CONFIG_COMEDI_PCI
	{
	.name = PC263_DRIVER_NAME,
	.fancy_name = PC263_DRIVER_NAME,
	.devid = PCI_DEVICE_ID_INVALID,
	.bustype = pci_bustype,
	.model = anypci_model,	/* wildcard */
		},
#endif
};

#ifdef CONFIG_COMEDI_PCI
static DEFINE_PCI_DEVICE_TABLE(pc263_pci_table) = {
	{PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI263, PCI_ANY_ID,
		PCI_ANY_ID, 0, 0, 0},
	{0}
};

MODULE_DEVICE_TABLE(pci, pc263_pci_table);
#endif /* CONFIG_COMEDI_PCI */

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct pc263_board *)dev->board_ptr)

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
#ifdef CONFIG_COMEDI_PCI
struct pc263_private {
	/* PCI device. */
	struct pci_dev *pci_dev;
};

#define devpriv ((struct pc263_private *)dev->private)
#endif /* CONFIG_COMEDI_PCI */

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int pc263_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int pc263_detach(struct comedi_device *dev);
static struct comedi_driver driver_amplc_pc263 = {
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
static int pc263_dio_insn_bits(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);
static int pc263_dio_insn_config(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);

/*
 * This function looks for a PCI device matching the requested board name,
 * bus and slot.
 */
#ifdef CONFIG_COMEDI_PCI
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
#endif

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
#ifdef CONFIG_COMEDI_PCI
	struct pci_dev *pci_dev = NULL;
	int bus = 0, slot = 0;
#endif
	int ret;

	printk(KERN_DEBUG "comedi%d: %s: attach\n", dev->minor,
		PC263_DRIVER_NAME);
/*
 * Allocate the private structure area.  alloc_private() is a
 * convenient macro defined in comedidev.h.
 */
#ifdef CONFIG_COMEDI_PCI
	ret = alloc_private(dev, sizeof(struct pc263_private));
	if (ret < 0) {
		printk(KERN_ERR "comedi%d: error! out of memory!\n",
			dev->minor);
		return ret;
	}
#endif
	/* Process options. */
	switch (thisboard->bustype) {
	case isa_bustype:
		iobase = it->options[0];
		break;
#ifdef CONFIG_COMEDI_PCI
	case pci_bustype:
		bus = it->options[0];
		slot = it->options[1];

		ret = pc263_find_pci(dev, bus, slot, &pci_dev);
		if (ret < 0)
			return ret;
		devpriv->pci_dev = pci_dev;
		break;
#endif /* CONFIG_COMEDI_PCI */
	default:
		printk(KERN_ERR
			"comedi%d: %s: BUG! cannot determine board type!\n",
			dev->minor, PC263_DRIVER_NAME);
		return -EINVAL;
		break;
	}

/*
 * Initialize dev->board_name.
 */
	dev->board_name = thisboard->name;

	/* Enable device and reserve I/O spaces. */
#ifdef CONFIG_COMEDI_PCI
	if (pci_dev) {
		ret = comedi_pci_enable(pci_dev, PC263_DRIVER_NAME);
		if (ret < 0) {
			printk(KERN_ERR
				"comedi%d: error! cannot enable PCI device and request regions!\n",
				dev->minor);
			return ret;
		}
		iobase = pci_resource_start(pci_dev, 2);
	} else
#endif
	{
		ret = pc263_request_region(dev->minor, iobase, PC263_IO_SIZE);
		if (ret < 0) {
			return ret;
		}
	}
	dev->iobase = iobase;

/*
 * Allocate the subdevice structures.  alloc_subdevice() is a
 * convenient macro defined in comedidev.h.
 */
	ret = alloc_subdevices(dev, 1);
	if (ret < 0) {
		printk(KERN_ERR "comedi%d: error! out of memory!\n",
			dev->minor);
		return ret;
	}

	s = dev->subdevices + 0;
	/* digital i/o subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE | SDF_RT;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = pc263_dio_insn_bits;
	s->insn_config = pc263_dio_insn_config;
	/* all outputs */
	s->io_bits = 0xffff;
	/* read initial relay state */
	s->state = inb(dev->iobase);
	s->state = s->state | (inb(dev->iobase) << 8);

	printk(KERN_INFO "comedi%d: %s ", dev->minor, dev->board_name);
	if (thisboard->bustype == isa_bustype) {
		printk("(base %#lx) ", iobase);
	} else {
#ifdef CONFIG_COMEDI_PCI
		printk("(pci %s) ", pci_name(pci_dev));
#endif
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
static int pc263_detach(struct comedi_device *dev)
{
	printk(KERN_DEBUG "comedi%d: %s: detach\n", dev->minor,
		PC263_DRIVER_NAME);

#ifdef CONFIG_COMEDI_PCI
	if (devpriv)
#endif
	{
#ifdef CONFIG_COMEDI_PCI
		if (devpriv->pci_dev) {
			if (dev->iobase) {
				comedi_pci_disable(devpriv->pci_dev);
			}
			pci_dev_put(devpriv->pci_dev);
		} else
#endif
		{
			if (dev->iobase) {
				release_region(dev->iobase, PC263_IO_SIZE);
			}
		}
	}
	if (dev->board_name) {
		printk(KERN_INFO "comedi%d: %s removed\n",
			dev->minor, dev->board_name);
	}
	return 0;
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

/* DIO devices are slightly special.  Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels.  The
 * comedi core can convert between insn_bits and insn_read/write */
static int pc263_dio_insn_bits(struct comedi_device *dev, struct comedi_subdevice *s,
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

static int pc263_dio_insn_config(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 1)
		return -EINVAL;
	return 1;
}

/*
 * A convenient macro that defines init_module() and cleanup_module(),
 * as necessary.
 */
#ifdef CONFIG_COMEDI_PCI
COMEDI_PCI_INITCLEANUP(driver_amplc_pc263, pc263_pci_table);
#else
COMEDI_INITCLEANUP(driver_amplc_pc263);
#endif

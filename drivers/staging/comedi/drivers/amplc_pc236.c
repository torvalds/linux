/*
    comedi/drivers/amplc_pc236.c
    Driver for Amplicon PC36AT and PCI236 DIO boards.

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
Driver: amplc_pc236
Description: Amplicon PC36AT, PCI236
Author: Ian Abbott <abbotti@mev.co.uk>
Devices: [Amplicon] PC36AT (pc36at), PCI236 (pci236 or amplc_pc236)
Updated: Wed, 01 Apr 2009 15:41:25 +0100
Status: works

Configuration options - PC36AT:
  [0] - I/O port base address
  [1] - IRQ (optional)

Configuration options - PCI236:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first available PCI device will be
  used.

The PC36AT ISA board and PCI236 PCI board have a single 8255 appearing
as subdevice 0.

Subdevice 1 pretends to be a digital input device, but it always returns
0 when read. However, if you run a command with scan_begin_src=TRIG_EXT,
a rising edge on port C bit 3 acts as an external trigger, which can be
used to wake up tasks.  This is like the comedi_parport device, but the
only way to physically disable the interrupt on the PC36AT is to remove
the IRQ jumper.  If no interrupt is connected, then subdevice 1 is
unused.
*/

#include <linux/interrupt.h>

#include "../comedidev.h"

#include "comedi_pci.h"

#include "8255.h"
#include "plx9052.h"

#define PC236_DRIVER_NAME	"amplc_pc236"

/* PCI236 PCI configuration register information */
#define PCI_VENDOR_ID_AMPLICON 0x14dc
#define PCI_DEVICE_ID_AMPLICON_PCI236 0x0009
#define PCI_DEVICE_ID_INVALID 0xffff

/* PC36AT / PCI236 registers */

#define PC236_IO_SIZE		4
#define PC236_LCR_IO_SIZE	128

/*
 * INTCSR values for PCI236.
 */
/* Disable interrupt, also clear any interrupt there */
#define PCI236_INTR_DISABLE (PLX9052_INTCSR_LI1ENAB_DISABLED \
        | PLX9052_INTCSR_LI1POL_HIGH \
        | PLX9052_INTCSR_LI2POL_HIGH \
        | PLX9052_INTCSR_PCIENAB_DISABLED \
        | PLX9052_INTCSR_LI1SEL_EDGE \
        | PLX9052_INTCSR_LI1CLRINT_ASSERTED)
/* Enable interrupt, also clear any interrupt there. */
#define PCI236_INTR_ENABLE (PLX9052_INTCSR_LI1ENAB_ENABLED \
        | PLX9052_INTCSR_LI1POL_HIGH \
        | PLX9052_INTCSR_LI2POL_HIGH \
        | PLX9052_INTCSR_PCIENAB_ENABLED \
        | PLX9052_INTCSR_LI1SEL_EDGE \
        | PLX9052_INTCSR_LI1CLRINT_ASSERTED)

/*
 * Board descriptions for Amplicon PC36AT and PCI236.
 */

enum pc236_bustype { isa_bustype, pci_bustype };
enum pc236_model { pc36at_model, pci236_model, anypci_model };

struct pc236_board {
	const char *name;
	const char *fancy_name;
	unsigned short devid;
	enum pc236_bustype bustype;
	enum pc236_model model;
};
static const struct pc236_board pc236_boards[] = {
	{
	.name = "pc36at",
	.fancy_name = "PC36AT",
	.bustype = isa_bustype,
	.model = pc36at_model,
		},
#ifdef CONFIG_COMEDI_PCI
	{
	.name = "pci236",
	.fancy_name = "PCI236",
	.devid = PCI_DEVICE_ID_AMPLICON_PCI236,
	.bustype = pci_bustype,
	.model = pci236_model,
		},
#endif
#ifdef CONFIG_COMEDI_PCI
	{
	.name = PC236_DRIVER_NAME,
	.fancy_name = PC236_DRIVER_NAME,
	.devid = PCI_DEVICE_ID_INVALID,
	.bustype = pci_bustype,
	.model = anypci_model,	/* wildcard */
		},
#endif
};

#ifdef CONFIG_COMEDI_PCI
static DEFINE_PCI_DEVICE_TABLE(pc236_pci_table) = {
	{PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI236, PCI_ANY_ID,
		PCI_ANY_ID, 0, 0, 0},
	{0}
};

MODULE_DEVICE_TABLE(pci, pc236_pci_table);
#endif /* CONFIG_COMEDI_PCI */

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct pc236_board *)dev->board_ptr)

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
struct pc236_private {
#ifdef CONFIG_COMEDI_PCI
	/* PCI device */
	struct pci_dev *pci_dev;
	unsigned long lcr_iobase;	/* PLX PCI9052 config registers in PCIBAR1 */
#endif
	int enable_irq;
};

#define devpriv ((struct pc236_private *)dev->private)

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int pc236_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int pc236_detach(struct comedi_device *dev);
static struct comedi_driver driver_amplc_pc236 = {
	.driver_name = PC236_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = pc236_attach,
	.detach = pc236_detach,
	.board_name = &pc236_boards[0].name,
	.offset = sizeof(struct pc236_board),
	.num_names = ARRAY_SIZE(pc236_boards),
};

#ifdef CONFIG_COMEDI_PCI
COMEDI_PCI_INITCLEANUP(driver_amplc_pc236, pc236_pci_table);
#else
COMEDI_INITCLEANUP(driver_amplc_pc236);
#endif

static int pc236_request_region(unsigned minor, unsigned long from,
	unsigned long extent);
static void pc236_intr_disable(struct comedi_device *dev);
static void pc236_intr_enable(struct comedi_device *dev);
static int pc236_intr_check(struct comedi_device *dev);
static int pc236_intr_insn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data);
static int pc236_intr_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_cmd *cmd);
static int pc236_intr_cmd(struct comedi_device *dev, struct comedi_subdevice *s);
static int pc236_intr_cancel(struct comedi_device *dev, struct comedi_subdevice *s);
static irqreturn_t pc236_interrupt(int irq, void *d);

/*
 * This function looks for a PCI device matching the requested board name,
 * bus and slot.
 */
#ifdef CONFIG_COMEDI_PCI
static int
pc236_find_pci(struct comedi_device *dev, int bus, int slot,
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

			for (i = 0; i < ARRAY_SIZE(pc236_boards); i++) {
				if (pc236_boards[i].bustype != pci_bustype)
					continue;
				if (pci_dev->device == pc236_boards[i].devid) {
					/* Change board_ptr to matched board. */
					dev->board_ptr = &pc236_boards[i];
					break;
				}
			}
			if (i == ARRAY_SIZE(pc236_boards))
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
static int pc236_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase = 0;
	unsigned int irq = 0;
#ifdef CONFIG_COMEDI_PCI
	struct pci_dev *pci_dev = NULL;
	int bus = 0, slot = 0;
#endif
	int share_irq = 0;
	int ret;

	printk(KERN_DEBUG "comedi%d: %s: attach\n", dev->minor,
		PC236_DRIVER_NAME);
/*
 * Allocate the private structure area.  alloc_private() is a
 * convenient macro defined in comedidev.h.
 */
	ret = alloc_private(dev, sizeof(struct pc236_private));
	if (ret < 0) {
		printk(KERN_ERR "comedi%d: error! out of memory!\n",
			dev->minor);
		return ret;
	}
	/* Process options. */
	switch (thisboard->bustype) {
	case isa_bustype:
		iobase = it->options[0];
		irq = it->options[1];
		share_irq = 0;
		break;
#ifdef CONFIG_COMEDI_PCI
	case pci_bustype:
		bus = it->options[0];
		slot = it->options[1];
		share_irq = 1;

		ret = pc236_find_pci(dev, bus, slot, &pci_dev);
		if (ret < 0)
			return ret;
		devpriv->pci_dev = pci_dev;
		break;
#endif /* CONFIG_COMEDI_PCI */
	default:
		printk(KERN_ERR
			"comedi%d: %s: BUG! cannot determine board type!\n",
			dev->minor, PC236_DRIVER_NAME);
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

		ret = comedi_pci_enable(pci_dev, PC236_DRIVER_NAME);
		if (ret < 0) {
			printk(KERN_ERR
				"comedi%d: error! cannot enable PCI device and request regions!\n",
				dev->minor);
			return ret;
		}
		devpriv->lcr_iobase = pci_resource_start(pci_dev, 1);
		iobase = pci_resource_start(pci_dev, 2);
		irq = pci_dev->irq;
	} else
#endif
	{
		ret = pc236_request_region(dev->minor, iobase, PC236_IO_SIZE);
		if (ret < 0) {
			return ret;
		}
	}
	dev->iobase = iobase;

/*
 * Allocate the subdevice structures.  alloc_subdevice() is a
 * convenient macro defined in comedidev.h.
 */
	ret = alloc_subdevices(dev, 2);
	if (ret < 0) {
		printk(KERN_ERR "comedi%d: error! out of memory!\n",
			dev->minor);
		return ret;
	}

	s = dev->subdevices + 0;
	/* digital i/o subdevice (8255) */
	ret = subdev_8255_init(dev, s, NULL, iobase);
	if (ret < 0) {
		printk(KERN_ERR "comedi%d: error! out of memory!\n",
			dev->minor);
		return ret;
	}
	s = dev->subdevices + 1;
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_UNUSED;
	pc236_intr_disable(dev);
	if (irq) {
		unsigned long flags = share_irq ? IRQF_SHARED : 0;

		if (comedi_request_irq(irq, pc236_interrupt, flags,
				PC236_DRIVER_NAME, dev) >= 0) {
			dev->irq = irq;
			s->type = COMEDI_SUBD_DI;
			s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
			s->n_chan = 1;
			s->maxdata = 1;
			s->range_table = &range_digital;
			s->insn_bits = pc236_intr_insn;
			s->do_cmdtest = pc236_intr_cmdtest;
			s->do_cmd = pc236_intr_cmd;
			s->cancel = pc236_intr_cancel;
		}
	}
	printk(KERN_INFO "comedi%d: %s ", dev->minor, dev->board_name);
	if (thisboard->bustype == isa_bustype) {
		printk("(base %#lx) ", iobase);
	} else {
#ifdef CONFIG_COMEDI_PCI
		printk("(pci %s) ", pci_name(pci_dev));
#endif
	}
	if (irq) {
		printk("(irq %u%s) ", irq, (dev->irq ? "" : " UNAVAILABLE"));
	} else {
		printk("(no irq) ");
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
static int pc236_detach(struct comedi_device *dev)
{
	printk(KERN_DEBUG "comedi%d: %s: detach\n", dev->minor,
		PC236_DRIVER_NAME);
	if (devpriv) {
		pc236_intr_disable(dev);
	}
	if (dev->irq)
		comedi_free_irq(dev->irq, dev);
	if (dev->subdevices) {
		subdev_8255_cleanup(dev, dev->subdevices + 0);
	}
	if (devpriv) {
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
				release_region(dev->iobase, PC236_IO_SIZE);
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
static int pc236_request_region(unsigned minor, unsigned long from,
	unsigned long extent)
{
	if (!from || !request_region(from, extent, PC236_DRIVER_NAME)) {
		printk(KERN_ERR "comedi%d: I/O port conflict (%#lx,%lu)!\n",
			minor, from, extent);
		return -EIO;
	}
	return 0;
}

/*
 * This function is called to mark the interrupt as disabled (no command
 * configured on subdevice 1) and to physically disable the interrupt
 * (not possible on the PC36AT, except by removing the IRQ jumper!).
 */
static void pc236_intr_disable(struct comedi_device *dev)
{
	unsigned long flags;

	comedi_spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->enable_irq = 0;
#ifdef CONFIG_COMEDI_PCI
	if (devpriv->lcr_iobase)
		outl(PCI236_INTR_DISABLE, devpriv->lcr_iobase + PLX9052_INTCSR);
#endif
	comedi_spin_unlock_irqrestore(&dev->spinlock, flags);
}

/*
 * This function is called to mark the interrupt as enabled (a command
 * configured on subdevice 1) and to physically enable the interrupt
 * (not possible on the PC36AT, except by (re)connecting the IRQ jumper!).
 */
static void pc236_intr_enable(struct comedi_device *dev)
{
	unsigned long flags;

	comedi_spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->enable_irq = 1;
#ifdef CONFIG_COMEDI_PCI
	if (devpriv->lcr_iobase)
		outl(PCI236_INTR_ENABLE, devpriv->lcr_iobase + PLX9052_INTCSR);
#endif
	comedi_spin_unlock_irqrestore(&dev->spinlock, flags);
}

/*
 * This function is called when an interrupt occurs to check whether
 * the interrupt has been marked as enabled and was generated by the
 * board.  If so, the function prepares the hardware for the next
 * interrupt.
 * Returns 0 if the interrupt should be ignored.
 */
static int pc236_intr_check(struct comedi_device *dev)
{
	int retval = 0;
	unsigned long flags;

	comedi_spin_lock_irqsave(&dev->spinlock, flags);
	if (devpriv->enable_irq) {
		retval = 1;
#ifdef CONFIG_COMEDI_PCI
		if (devpriv->lcr_iobase) {
			if ((inl(devpriv->lcr_iobase + PLX9052_INTCSR)
					& PLX9052_INTCSR_LI1STAT_MASK)
				== PLX9052_INTCSR_LI1STAT_INACTIVE) {
				retval = 0;
			} else {
				/* Clear interrupt and keep it enabled. */
				outl(PCI236_INTR_ENABLE,
					devpriv->lcr_iobase + PLX9052_INTCSR);
			}
		}
#endif
	}
	comedi_spin_unlock_irqrestore(&dev->spinlock, flags);

	return retval;
}

/*
 * Input from subdevice 1.
 * Copied from the comedi_parport driver.
 */
static int pc236_intr_insn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	data[1] = 0;
	return 2;
}

/*
 * Subdevice 1 command test.
 * Copied from the comedi_parport driver.
 */
static int pc236_intr_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* step 1 */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_FOLLOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: ignored */

	if (err)
		return 2;

	/* step 3: */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
	if (cmd->scan_begin_arg != 0) {
		cmd->scan_begin_arg = 0;
		err++;
	}
	if (cmd->convert_arg != 0) {
		cmd->convert_arg = 0;
		err++;
	}
	if (cmd->scan_end_arg != 1) {
		cmd->scan_end_arg = 1;
		err++;
	}
	if (cmd->stop_arg != 0) {
		cmd->stop_arg = 0;
		err++;
	}

	if (err)
		return 3;

	/* step 4: ignored */

	if (err)
		return 4;

	return 0;
}

/*
 * Subdevice 1 command.
 */
static int pc236_intr_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	pc236_intr_enable(dev);

	return 0;
}

/*
 * Subdevice 1 cancel command.
 */
static int pc236_intr_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	pc236_intr_disable(dev);

	return 0;
}

/*
 * Interrupt service routine.
 * Based on the comedi_parport driver.
 */
static irqreturn_t pc236_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->subdevices + 1;
	int handled;

	handled = pc236_intr_check(dev);
	if (dev->attached && handled) {
		comedi_buf_put(s->async, 0);
		s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;
		comedi_event(dev, s);
	}
	return IRQ_RETVAL(handled);
}

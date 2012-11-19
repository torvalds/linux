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

#include "comedi_fc.h"
#include "8255.h"
#include "plx9052.h"

#define PC236_DRIVER_NAME	"amplc_pc236"

#define DO_ISA	IS_ENABLED(CONFIG_COMEDI_AMPLC_PC236_ISA)
#define DO_PCI	IS_ENABLED(CONFIG_COMEDI_AMPLC_PC236_PCI)

/* PCI236 PCI configuration register information */
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
	unsigned short devid;
	enum pc236_bustype bustype;
	enum pc236_model model;
};
static const struct pc236_board pc236_boards[] = {
#if DO_ISA
	{
		.name = "pc36at",
		.bustype = isa_bustype,
		.model = pc36at_model,
	},
#endif
#if DO_PCI
	{
		.name = "pci236",
		.devid = PCI_DEVICE_ID_AMPLICON_PCI236,
		.bustype = pci_bustype,
		.model = pci236_model,
	},
	{
		.name = PC236_DRIVER_NAME,
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
struct pc236_private {
	unsigned long lcr_iobase; /* PLX PCI9052 config registers in PCIBAR1 */
	int enable_irq;
};

/* test if ISA supported and this is an ISA board */
static inline bool is_isa_board(const struct pc236_board *board)
{
	return DO_ISA && board->bustype == isa_bustype;
}

/* test if PCI supported and this is a PCI board */
static inline bool is_pci_board(const struct pc236_board *board)
{
	return DO_PCI && board->bustype == pci_bustype;
}

/*
 * This function looks for a board matching the supplied PCI device.
 */
static const struct pc236_board *pc236_find_pci_board(struct pci_dev *pci_dev)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pc236_boards); i++)
		if (is_pci_board(&pc236_boards[i]) &&
		    pci_dev->device == pc236_boards[i].devid)
			return &pc236_boards[i];
	return NULL;
}

/*
 * This function looks for a PCI device matching the requested board name,
 * bus and slot.
 */
static struct pci_dev *pc236_find_pci_dev(struct comedi_device *dev,
					  struct comedi_devconfig *it)
{
	const struct pc236_board *thisboard = comedi_board(dev);
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
			const struct pc236_board *foundboard;

			foundboard = pc236_find_pci_board(pci_dev);
			if (foundboard == NULL)
				continue;
			/* Replace wildcard board_ptr. */
			dev->board_ptr = foundboard;
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
static int pc236_request_region(struct comedi_device *dev, unsigned long from,
				unsigned long extent)
{
	if (!from || !request_region(from, extent, PC236_DRIVER_NAME)) {
		dev_err(dev->class_dev, "I/O port conflict (%#lx,%lu)!\n",
		       from, extent);
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
	const struct pc236_board *thisboard = comedi_board(dev);
	struct pc236_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->enable_irq = 0;
	if (is_pci_board(thisboard))
		outl(PCI236_INTR_DISABLE, devpriv->lcr_iobase + PLX9052_INTCSR);
	spin_unlock_irqrestore(&dev->spinlock, flags);
}

/*
 * This function is called to mark the interrupt as enabled (a command
 * configured on subdevice 1) and to physically enable the interrupt
 * (not possible on the PC36AT, except by (re)connecting the IRQ jumper!).
 */
static void pc236_intr_enable(struct comedi_device *dev)
{
	const struct pc236_board *thisboard = comedi_board(dev);
	struct pc236_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	devpriv->enable_irq = 1;
	if (is_pci_board(thisboard))
		outl(PCI236_INTR_ENABLE, devpriv->lcr_iobase + PLX9052_INTCSR);
	spin_unlock_irqrestore(&dev->spinlock, flags);
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
	const struct pc236_board *thisboard = comedi_board(dev);
	struct pc236_private *devpriv = dev->private;
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&dev->spinlock, flags);
	if (devpriv->enable_irq) {
		retval = 1;
		if (is_pci_board(thisboard)) {
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
	}
	spin_unlock_irqrestore(&dev->spinlock, flags);

	return retval;
}

/*
 * Input from subdevice 1.
 * Copied from the comedi_parport driver.
 */
static int pc236_intr_insn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	data[1] = 0;
	return insn->n;
}

/*
 * Subdevice 1 command test.
 * Copied from the comedi_parport driver.
 */
static int pc236_intr_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	int err = 0;

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src, TRIG_FOLLOW);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check it arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, 1);
	err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

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
static int pc236_intr_cancel(struct comedi_device *dev,
			     struct comedi_subdevice *s)
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
	struct comedi_subdevice *s = &dev->subdevices[1];
	int handled;

	handled = pc236_intr_check(dev);
	if (dev->attached && handled) {
		comedi_buf_put(s->async, 0);
		s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;
		comedi_event(dev, s);
	}
	return IRQ_RETVAL(handled);
}

static void pc236_report_attach(struct comedi_device *dev, unsigned int irq)
{
	const struct pc236_board *thisboard = comedi_board(dev);
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	char tmpbuf[60];
	int tmplen;

	if (is_isa_board(thisboard))
		tmplen = scnprintf(tmpbuf, sizeof(tmpbuf),
				   "(base %#lx) ", dev->iobase);
	else if (is_pci_board(thisboard))
		tmplen = scnprintf(tmpbuf, sizeof(tmpbuf),
				   "(pci %s) ", pci_name(pcidev));
	else
		tmplen = 0;
	if (irq)
		tmplen += scnprintf(&tmpbuf[tmplen], sizeof(tmpbuf) - tmplen,
				    "(irq %u%s) ", irq,
				    (dev->irq ? "" : " UNAVAILABLE"));
	else
		tmplen += scnprintf(&tmpbuf[tmplen], sizeof(tmpbuf) - tmplen,
				    "(no irq) ");
	dev_info(dev->class_dev, "%s %sattached\n",
		 dev->board_name, tmpbuf);
}

static int pc236_common_attach(struct comedi_device *dev, unsigned long iobase,
			       unsigned int irq, unsigned long req_irq_flags)
{
	const struct pc236_board *thisboard = comedi_board(dev);
	struct comedi_subdevice *s;
	int ret;

	dev->board_name = thisboard->name;
	dev->iobase = iobase;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* digital i/o subdevice (8255) */
	ret = subdev_8255_init(dev, s, NULL, iobase);
	if (ret < 0) {
		dev_err(dev->class_dev, "error! out of memory!\n");
		return ret;
	}
	s = &dev->subdevices[1];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_UNUSED;
	pc236_intr_disable(dev);
	if (irq) {
		if (request_irq(irq, pc236_interrupt, req_irq_flags,
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
	pc236_report_attach(dev, irq);
	return 1;
}

static int pc236_pci_common_attach(struct comedi_device *dev,
				   struct pci_dev *pci_dev)
{
	struct pc236_private *devpriv = dev->private;
	unsigned long iobase;
	int ret;

	comedi_set_hw_dev(dev, &pci_dev->dev);

	ret = comedi_pci_enable(pci_dev, PC236_DRIVER_NAME);
	if (ret < 0) {
		dev_err(dev->class_dev,
			"error! cannot enable PCI device and request regions!\n");
		return ret;
	}
	devpriv->lcr_iobase = pci_resource_start(pci_dev, 1);
	iobase = pci_resource_start(pci_dev, 2);
	return pc236_common_attach(dev, iobase, pci_dev->irq, IRQF_SHARED);
}

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int pc236_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pc236_board *thisboard = comedi_board(dev);
	struct pc236_private *devpriv;
	int ret;

	dev_info(dev->class_dev, PC236_DRIVER_NAME ": attach\n");

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	/* Process options according to bus type. */
	if (is_isa_board(thisboard)) {
		unsigned long iobase = it->options[0];
		unsigned int irq = it->options[1];
		ret = pc236_request_region(dev, iobase, PC236_IO_SIZE);
		if (ret < 0)
			return ret;
		return pc236_common_attach(dev, iobase, irq, 0);
	} else if (is_pci_board(thisboard)) {
		struct pci_dev *pci_dev;

		pci_dev = pc236_find_pci_dev(dev, it);
		if (!pci_dev)
			return -EIO;
		return pc236_pci_common_attach(dev, pci_dev);
	} else {
		dev_err(dev->class_dev, PC236_DRIVER_NAME
			": BUG! cannot determine board type!\n");
		return -EINVAL;
	}
}

/*
 * The auto_attach hook is called at PCI probe time via
 * comedi_pci_auto_config().  dev->board_ptr is NULL on entry.
 * There should be a board entry matching the supplied PCI device.
 */
static int pc236_auto_attach(struct comedi_device *dev,
				       unsigned long context_unused)
{
	struct pci_dev *pci_dev = comedi_to_pci_dev(dev);
	struct pc236_private *devpriv;

	if (!DO_PCI)
		return -EINVAL;

	dev_info(dev->class_dev, PC236_DRIVER_NAME ": attach pci %s\n",
		 pci_name(pci_dev));

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	dev->board_ptr = pc236_find_pci_board(pci_dev);
	if (dev->board_ptr == NULL) {
		dev_err(dev->class_dev, "BUG! cannot determine board type!\n");
		return -EINVAL;
	}
	/*
	 * Need to 'get' the PCI device to match the 'put' in pc236_detach().
	 * TODO: Remove the pci_dev_get() and matching pci_dev_put() once
	 * support for manual attachment of PCI devices via pc236_attach()
	 * has been removed.
	 */
	pci_dev_get(pci_dev);
	return pc236_pci_common_attach(dev, pci_dev);
}

static void pc236_detach(struct comedi_device *dev)
{
	const struct pc236_board *thisboard = comedi_board(dev);

	if (!thisboard)
		return;
	if (dev->iobase)
		pc236_intr_disable(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->subdevices)
		subdev_8255_cleanup(dev, &dev->subdevices[0]);
	if (is_isa_board(thisboard)) {
		if (dev->iobase)
			release_region(dev->iobase, PC236_IO_SIZE);
	} else if (is_pci_board(thisboard)) {
		struct pci_dev *pcidev = comedi_to_pci_dev(dev);
		if (pcidev) {
			if (dev->iobase)
				comedi_pci_disable(pcidev);
			pci_dev_put(pcidev);
		}
	}
}

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static struct comedi_driver amplc_pc236_driver = {
	.driver_name = PC236_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = pc236_attach,
	.auto_attach = pc236_auto_attach,
	.detach = pc236_detach,
	.board_name = &pc236_boards[0].name,
	.offset = sizeof(struct pc236_board),
	.num_names = ARRAY_SIZE(pc236_boards),
};

#if DO_PCI
static DEFINE_PCI_DEVICE_TABLE(pc236_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI236) },
	{0}
};

MODULE_DEVICE_TABLE(pci, pc236_pci_table);

static int amplc_pc236_pci_probe(struct pci_dev *dev,
					   const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &amplc_pc236_driver);
}

static void __devexit amplc_pc236_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver amplc_pc236_pci_driver = {
	.name = PC236_DRIVER_NAME,
	.id_table = pc236_pci_table,
	.probe = &amplc_pc236_pci_probe,
	.remove = &amplc_pc236_pci_remove
};

module_comedi_pci_driver(amplc_pc236_driver, amplc_pc236_pci_driver);
#else
module_comedi_driver(amplc_pc236_driver);
#endif

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

/*
    comedi/drivers/cb_pcimdas.c
    Comedi driver for Computer Boards PCIM-DAS1602/16

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
Driver: cb_pcimdas
Description: Measurement Computing PCI Migration series boards
Devices: [ComputerBoards] PCIM-DAS1602/16 (cb_pcimdas)
Author: Richard Bytheway
Updated: Wed, 13 Nov 2002 12:34:56 +0000
Status: experimental

Written to support the PCIM-DAS1602/16 on a 2.4 series kernel.

Configuration Options:
    [0] - PCI bus number
    [1] - PCI slot number

Developed from cb_pcidas and skel by Richard Bytheway (mocelet@sucs.org).
Only supports DIO, AO and simple AI in it's present form.
No interrupts, multi channel or FIFO AI, although the card looks like it could support this.
See http://www.measurementcomputing.com/PDFManuals/pcim-das1602_16.pdf for more details.
*/

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/interrupt.h>

#include "comedi_pci.h"
#include "plx9052.h"
#include "8255.h"

/* #define CBPCIMDAS_DEBUG */
#undef CBPCIMDAS_DEBUG

#define PCI_VENDOR_ID_COMPUTERBOARDS	0x1307

/* Registers for the PCIM-DAS1602/16 */

/* sizes of io regions (bytes) */
#define BADR0_SIZE 2		/* ?? */
#define BADR1_SIZE 4
#define BADR2_SIZE 6
#define BADR3_SIZE 16
#define BADR4_SIZE 4

/* DAC Offsets */
#define ADC_TRIG 0
#define DAC0_OFFSET 2
#define DAC1_OFFSET 4

/* AI and Counter Constants */
#define MUX_LIMITS 0
#define MAIN_CONN_DIO 1
#define ADC_STAT 2
#define ADC_CONV_STAT 3
#define ADC_INT 4
#define ADC_PACER 5
#define BURST_MODE 6
#define PROG_GAIN 7
#define CLK8254_1_DATA 8
#define CLK8254_2_DATA 9
#define CLK8254_3_DATA 10
#define CLK8254_CONTROL 11
#define USER_COUNTER 12
#define RESID_COUNT_H 13
#define RESID_COUNT_L 14

/* Board description */
struct cb_pcimdas_board {
	const char *name;
	unsigned short device_id;
	int ai_se_chans;	/*  Inputs in single-ended mode */
	int ai_diff_chans;	/*  Inputs in differential mode */
	int ai_bits;		/*  analog input resolution */
	int ai_speed;		/*  fastest conversion period in ns */
	int ao_nchan;		/*  number of analog out channels */
	int ao_bits;		/*  analogue output resolution */
	int has_ao_fifo;	/*  analog output has fifo */
	int ao_scan_speed;	/*  analog output speed for 1602 series (for a scan, not conversion) */
	int fifo_size;		/*  number of samples fifo can hold */
	int dio_bits;		/*  number of dio bits */
	int has_dio;		/*  has DIO */
	const struct comedi_lrange *ranges;
};

static const struct cb_pcimdas_board cb_pcimdas_boards[] = {
	{
	 .name = "PCIM-DAS1602/16",
	 .device_id = 0x56,
	 .ai_se_chans = 16,
	 .ai_diff_chans = 8,
	 .ai_bits = 16,
	 .ai_speed = 10000,	/* ?? */
	 .ao_nchan = 2,
	 .ao_bits = 12,
	 .has_ao_fifo = 0,	/* ?? */
	 .ao_scan_speed = 10000,
	 /* ?? */
	 .fifo_size = 1024,
	 .dio_bits = 24,
	 .has_dio = 1,
/*	.ranges = &cb_pcimdas_ranges, */
	 },
};

/* This is used by modprobe to translate PCI IDs to drivers.  Should
 * only be used for PCI and ISA-PnP devices */
static DEFINE_PCI_DEVICE_TABLE(cb_pcimdas_pci_table) = {
	{
	PCI_VENDOR_ID_COMPUTERBOARDS, 0x0056, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{
	0}
};

MODULE_DEVICE_TABLE(pci, cb_pcimdas_pci_table);

#define N_BOARDS 1		/*  Max number of boards supported */

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct cb_pcimdas_board *)dev->board_ptr)

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
struct cb_pcimdas_private {
	int data;

	/*  would be useful for a PCI device */
	struct pci_dev *pci_dev;

	/* base addresses */
	unsigned long BADR0;
	unsigned long BADR1;
	unsigned long BADR2;
	unsigned long BADR3;
	unsigned long BADR4;

	/* Used for AO readback */
	unsigned int ao_readback[2];

	/*  Used for DIO */
	unsigned short int port_a;	/*  copy of BADR4+0 */
	unsigned short int port_b;	/*  copy of BADR4+1 */
	unsigned short int port_c;	/*  copy of BADR4+2 */
	unsigned short int dio_mode;	/*  copy of BADR4+3 */

};

/*
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct cb_pcimdas_private *)dev->private)

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int cb_pcimdas_attach(struct comedi_device *dev,
			     struct comedi_devconfig *it);
static int cb_pcimdas_detach(struct comedi_device *dev);
static struct comedi_driver driver_cb_pcimdas = {
	.driver_name = "cb_pcimdas",
	.module = THIS_MODULE,
	.attach = cb_pcimdas_attach,
	.detach = cb_pcimdas_detach,
};

static int cb_pcimdas_ai_rinsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);
static int cb_pcimdas_ao_winsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);
static int cb_pcimdas_ao_rinsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data);

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int cb_pcimdas_attach(struct comedi_device *dev,
			     struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	struct pci_dev *pcidev = NULL;
	int index;
	/* int i; */

	printk("comedi%d: cb_pcimdas: ", dev->minor);

/*
 * Allocate the private structure area.
 */
	if (alloc_private(dev, sizeof(struct cb_pcimdas_private)) < 0)
		return -ENOMEM;

/*
 * Probe the device to determine what device in the series it is.
 */
	printk("\n");

	for_each_pci_dev(pcidev) {
		/*  is it not a computer boards card? */
		if (pcidev->vendor != PCI_VENDOR_ID_COMPUTERBOARDS)
			continue;
		/*  loop through cards supported by this driver */
		for (index = 0; index < N_BOARDS; index++) {
			if (cb_pcimdas_boards[index].device_id !=
			    pcidev->device)
				continue;
			/*  was a particular bus/slot requested? */
			if (it->options[0] || it->options[1]) {
				/*  are we on the wrong bus/slot? */
				if (pcidev->bus->number != it->options[0] ||
				    PCI_SLOT(pcidev->devfn) != it->options[1]) {
					continue;
				}
			}
			devpriv->pci_dev = pcidev;
			dev->board_ptr = cb_pcimdas_boards + index;
			goto found;
		}
	}

	printk("No supported ComputerBoards/MeasurementComputing card found on "
	       "requested position\n");
	return -EIO;

found:

	printk("Found %s on bus %i, slot %i\n", cb_pcimdas_boards[index].name,
	       pcidev->bus->number, PCI_SLOT(pcidev->devfn));

	/*  Warn about non-tested features */
	switch (thisboard->device_id) {
	case 0x56:
		break;
	default:
		printk("THIS CARD IS UNSUPPORTED.\n"
		       "PLEASE REPORT USAGE TO <mocelet@sucs.org>\n");
	};

	if (comedi_pci_enable(pcidev, "cb_pcimdas")) {
		printk(" Failed to enable PCI device and request regions\n");
		return -EIO;
	}

	devpriv->BADR0 = pci_resource_start(devpriv->pci_dev, 0);
	devpriv->BADR1 = pci_resource_start(devpriv->pci_dev, 1);
	devpriv->BADR2 = pci_resource_start(devpriv->pci_dev, 2);
	devpriv->BADR3 = pci_resource_start(devpriv->pci_dev, 3);
	devpriv->BADR4 = pci_resource_start(devpriv->pci_dev, 4);

#ifdef CBPCIMDAS_DEBUG
	printk("devpriv->BADR0 = 0x%lx\n", devpriv->BADR0);
	printk("devpriv->BADR1 = 0x%lx\n", devpriv->BADR1);
	printk("devpriv->BADR2 = 0x%lx\n", devpriv->BADR2);
	printk("devpriv->BADR3 = 0x%lx\n", devpriv->BADR3);
	printk("devpriv->BADR4 = 0x%lx\n", devpriv->BADR4);
#endif

/* Dont support IRQ yet */
/*  get irq */
/* if(request_irq(devpriv->pci_dev->irq, cb_pcimdas_interrupt, IRQF_SHARED, "cb_pcimdas", dev )) */
/* { */
/* printk(" unable to allocate irq %u\n", devpriv->pci_dev->irq); */
/* return -EINVAL; */
/* } */
/* dev->irq = devpriv->pci_dev->irq; */

	/* Initialize dev->board_name */
	dev->board_name = thisboard->name;

/*
 * Allocate the subdevice structures.  alloc_subdevice() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_subdevices(dev, 3) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	/* dev->read_subdev=s; */
	/*  analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = thisboard->ai_se_chans;
	s->maxdata = (1 << thisboard->ai_bits) - 1;
	s->range_table = &range_unknown;
	s->len_chanlist = 1;	/*  This is the maximum chanlist length that */
	/*  the board can handle */
	s->insn_read = cb_pcimdas_ai_rinsn;

	s = dev->subdevices + 1;
	/*  analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = thisboard->ao_nchan;
	s->maxdata = 1 << thisboard->ao_bits;
	s->range_table = &range_unknown;	/* ranges are hardware settable, but not software readable. */
	s->insn_write = &cb_pcimdas_ao_winsn;
	s->insn_read = &cb_pcimdas_ao_rinsn;

	s = dev->subdevices + 2;
	/* digital i/o subdevice */
	if (thisboard->has_dio)
		subdev_8255_init(dev, s, NULL, devpriv->BADR4);
	else
		s->type = COMEDI_SUBD_UNUSED;

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
static int cb_pcimdas_detach(struct comedi_device *dev)
{
#ifdef CBPCIMDAS_DEBUG
	if (devpriv) {
		printk("devpriv->BADR0 = 0x%lx\n", devpriv->BADR0);
		printk("devpriv->BADR1 = 0x%lx\n", devpriv->BADR1);
		printk("devpriv->BADR2 = 0x%lx\n", devpriv->BADR2);
		printk("devpriv->BADR3 = 0x%lx\n", devpriv->BADR3);
		printk("devpriv->BADR4 = 0x%lx\n", devpriv->BADR4);
	}
#endif
	printk("comedi%d: cb_pcimdas: remove\n", dev->minor);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->pci_dev) {
			if (devpriv->BADR0)
				comedi_pci_disable(devpriv->pci_dev);
			pci_dev_put(devpriv->pci_dev);
		}
	}

	return 0;
}

/*
 * "instructions" read/write data in "one-shot" or "software-triggered"
 * mode.
 */
static int cb_pcimdas_ai_rinsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int n, i;
	unsigned int d;
	unsigned int busy;
	int chan = CR_CHAN(insn->chanspec);
	unsigned short chanlims;
	int maxchans;

	/*  only support sw initiated reads from a single channel */

	/* check channel number */
	if ((inb(devpriv->BADR3 + 2) & 0x20) == 0)	/* differential mode */
		maxchans = thisboard->ai_diff_chans;
	else
		maxchans = thisboard->ai_se_chans;

	if (chan > (maxchans - 1))
		return -ETIMEDOUT;	/* *** Wrong error code. Fixme. */

	/* configure for sw initiated read */
	d = inb(devpriv->BADR3 + 5);
	if ((d & 0x03) > 0) {	/* only reset if needed. */
		d = d & 0xfd;
		outb(d, devpriv->BADR3 + 5);
	}
	outb(0x01, devpriv->BADR3 + 6);	/* set bursting off, conversions on */
	outb(0x00, devpriv->BADR3 + 7);	/* set range to 10V. UP/BP is controlled by a switch on the board */

	/*  write channel limits to multiplexer, set Low (bits 0-3) and High (bits 4-7) channels to chan. */
	chanlims = chan | (chan << 4);
	outb(chanlims, devpriv->BADR3 + 0);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion */
		outw(0, devpriv->BADR2 + 0);

#define TIMEOUT 1000		/* typically takes 5 loops on a lightly loaded Pentium 100MHz, */
		/* this is likely to be 100 loops on a 2GHz machine, so set 1000 as the limit. */

		/* wait for conversion to end */
		for (i = 0; i < TIMEOUT; i++) {
			busy = inb(devpriv->BADR3 + 2) & 0x80;
			if (!busy)
				break;
		}
		if (i == TIMEOUT) {
			printk("timeout\n");
			return -ETIMEDOUT;
		}
		/* read data */
		d = inw(devpriv->BADR2 + 0);

		/* mangle the data as necessary */
		/* d ^= 1<<(thisboard->ai_bits-1); // 16 bit data from ADC, so no mangle needed. */

		data[n] = d;
	}

	/* return the number of samples read/written */
	return n;
}

static int cb_pcimdas_ao_winsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; i++) {
		switch (chan) {
		case 0:
			outw(data[i] & 0x0FFF, devpriv->BADR2 + DAC0_OFFSET);
			break;
		case 1:
			outw(data[i] & 0x0FFF, devpriv->BADR2 + DAC1_OFFSET);
			break;
		default:
			return -1;
		}
		devpriv->ao_readback[chan] = data[i];
	}

	/* return the number of samples read/written */
	return i;
}

/* AO subdevices should have a read insn as well as a write insn.
 * Usually this means copying a value stored in devpriv. */
static int cb_pcimdas_ao_rinsn(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

/*
 * A convenient macro that defines init_module() and cleanup_module(),
 * as necessary.
 */
static int __devinit driver_cb_pcimdas_pci_probe(struct pci_dev *dev,
						 const struct pci_device_id
						 *ent)
{
	return comedi_pci_auto_config(dev, driver_cb_pcimdas.driver_name);
}

static void __devexit driver_cb_pcimdas_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver driver_cb_pcimdas_pci_driver = {
	.id_table = cb_pcimdas_pci_table,
	.probe = &driver_cb_pcimdas_pci_probe,
	.remove = __devexit_p(&driver_cb_pcimdas_pci_remove)
};

static int __init driver_cb_pcimdas_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&driver_cb_pcimdas);
	if (retval < 0)
		return retval;

	driver_cb_pcimdas_pci_driver.name =
	    (char *)driver_cb_pcimdas.driver_name;
	return pci_register_driver(&driver_cb_pcimdas_pci_driver);
}

static void __exit driver_cb_pcimdas_cleanup_module(void)
{
	pci_unregister_driver(&driver_cb_pcimdas_pci_driver);
	comedi_driver_unregister(&driver_cb_pcimdas);
}

module_init(driver_cb_pcimdas_init_module);
module_exit(driver_cb_pcimdas_cleanup_module);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

/*
    comedi/drivers/cb_pcidio.c
    A Comedi driver for PCI-DIO24H & PCI-DIO48H of ComputerBoards (currently MeasurementComputing)

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
Driver: cb_pcidio
Description: ComputerBoards' DIO boards with PCI interface
Devices: [Measurement Computing] PCI-DIO24 (cb_pcidio), PCI-DIO24H, PCI-DIO48H
Author: Yoshiya Matsuzaka
Updated: Mon, 29 Oct 2007 15:40:47 +0000
Status: experimental

This driver has been modified from skel.c of comedi-0.7.70.

Configuration Options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first available PCI device will
  be used.

Passing a zero for an option is the same as leaving it unspecified.
*/

/*------------------------------ HEADER FILES ---------------------------------*/
#include "../comedidev.h"
#include "comedi_pci.h"
#include "8255.h"

/*-------------------------- MACROS and DATATYPES -----------------------------*/
#define PCI_VENDOR_ID_CB	0x1307

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
struct pcidio_board {
	const char *name;	/*  name of the board */
	int dev_id;
	int n_8255;		/*  number of 8255 chips on board */

	/*  indices of base address regions */
	int pcicontroler_badrindex;
	int dioregs_badrindex;
};

static const struct pcidio_board pcidio_boards[] = {
	{
	 .name = "pci-dio24",
	 .dev_id = 0x0028,
	 .n_8255 = 1,
	 .pcicontroler_badrindex = 1,
	 .dioregs_badrindex = 2,
	 },
	{
	 .name = "pci-dio24h",
	 .dev_id = 0x0014,
	 .n_8255 = 1,
	 .pcicontroler_badrindex = 1,
	 .dioregs_badrindex = 2,
	 },
	{
	 .name = "pci-dio48h",
	 .dev_id = 0x000b,
	 .n_8255 = 2,
	 .pcicontroler_badrindex = 0,
	 .dioregs_badrindex = 1,
	 },
};

/* This is used by modprobe to translate PCI IDs to drivers.  Should
 * only be used for PCI and ISA-PnP devices */
/* Please add your PCI vendor ID to comedidev.h, and it will be forwarded
 * upstream. */
static DEFINE_PCI_DEVICE_TABLE(pcidio_pci_table) = {
	{
	PCI_VENDOR_ID_CB, 0x0028, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, {
	PCI_VENDOR_ID_CB, 0x0014, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, {
	PCI_VENDOR_ID_CB, 0x000b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0}, {
	0}
};

MODULE_DEVICE_TABLE(pci, pcidio_pci_table);

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct pcidio_board *)dev->board_ptr)

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
struct pcidio_private {
	int data;		/*  curently unused */

	/* would be useful for a PCI device */
	struct pci_dev *pci_dev;

	/* used for DO readback, curently unused */
	unsigned int do_readback[4];	/* up to 4 unsigned int suffice to hold 96 bits for PCI-DIO96 */

	unsigned long dio_reg_base;	/*  address of port A of the first 8255 chip on board */
};

/*
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct pcidio_private *)dev->private)

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int pcidio_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static int pcidio_detach(struct comedi_device *dev);
static struct comedi_driver driver_cb_pcidio = {
	.driver_name = "cb_pcidio",
	.module = THIS_MODULE,
	.attach = pcidio_attach,
	.detach = pcidio_detach,

/* It is not necessary to implement the following members if you are
 * writing a driver for a ISA PnP or PCI card */

	/* Most drivers will support multiple types of boards by
	 * having an array of board structures.  These were defined
	 * in pcidio_boards[] above.  Note that the element 'name'
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

/* The following fields should NOT be initialized if you are dealing
 * with PCI devices
 *
 *	.board_name = pcidio_boards,
 *	.offset = sizeof(struct pcidio_board),
 *	.num_names = sizeof(pcidio_boards) / sizeof(structpcidio_board),
 */

};

/*------------------------------- FUNCTIONS -----------------------------------*/

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int pcidio_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct pci_dev *pcidev = NULL;
	int index;
	int i;

	printk("comedi%d: cb_pcidio: \n", dev->minor);

/*
 * Allocate the private structure area.  alloc_private() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_private(dev, sizeof(struct pcidio_private)) < 0)
		return -ENOMEM;
/*
 * If you can probe the device to determine what device in a series
 * it is, this is the place to do it.  Otherwise, dev->board_ptr
 * should already be initialized.
 */
/*
 * Probe the device to determine what device in the series it is.
 */

	for (pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, NULL);
	     pcidev != NULL;
	     pcidev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pcidev)) {
		/*  is it not a computer boards card? */
		if (pcidev->vendor != PCI_VENDOR_ID_CB)
			continue;
		/*  loop through cards supported by this driver */
		for (index = 0; index < ARRAY_SIZE(pcidio_boards); index++) {
			if (pcidio_boards[index].dev_id != pcidev->device)
				continue;

			/*  was a particular bus/slot requested? */
			if (it->options[0] || it->options[1]) {
				/*  are we on the wrong bus/slot? */
				if (pcidev->bus->number != it->options[0] ||
				    PCI_SLOT(pcidev->devfn) != it->options[1]) {
					continue;
				}
			}
			dev->board_ptr = pcidio_boards + index;
			goto found;
		}
	}

	printk("No supported ComputerBoards/MeasurementComputing card found on "
	       "requested position\n");
	return -EIO;

found:

/*
 * Initialize dev->board_name.  Note that we can use the "thisboard"
 * macro now, since we just initialized it in the last line.
 */
	dev->board_name = thisboard->name;

	devpriv->pci_dev = pcidev;
	printk("Found %s on bus %i, slot %i\n", thisboard->name,
	       devpriv->pci_dev->bus->number,
	       PCI_SLOT(devpriv->pci_dev->devfn));
	if (comedi_pci_enable(pcidev, thisboard->name)) {
		printk
		    ("cb_pcidio: failed to enable PCI device and request regions\n");
		return -EIO;
	}
	devpriv->dio_reg_base
	    =
	    pci_resource_start(devpriv->pci_dev,
			       pcidio_boards[index].dioregs_badrindex);

/*
 * Allocate the subdevice structures.  alloc_subdevice() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_subdevices(dev, thisboard->n_8255) < 0)
		return -ENOMEM;

	for (i = 0; i < thisboard->n_8255; i++) {
		subdev_8255_init(dev, dev->subdevices + i,
				 NULL, devpriv->dio_reg_base + i * 4);
		printk(" subdev %d: base = 0x%lx\n", i,
		       devpriv->dio_reg_base + i * 4);
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
static int pcidio_detach(struct comedi_device *dev)
{
	printk("comedi%d: cb_pcidio: remove\n", dev->minor);
	if (devpriv) {
		if (devpriv->pci_dev) {
			if (devpriv->dio_reg_base) {
				comedi_pci_disable(devpriv->pci_dev);
			}
			pci_dev_put(devpriv->pci_dev);
		}
	}
	if (dev->subdevices) {
		int i;
		for (i = 0; i < thisboard->n_8255; i++) {
			subdev_8255_cleanup(dev, dev->subdevices + i);
		}
	}
	return 0;
}

/*
 * A convenient macro that defines init_module() and cleanup_module(),
 * as necessary.
 */
COMEDI_PCI_INITCLEANUP(driver_cb_pcidio, pcidio_pci_table);

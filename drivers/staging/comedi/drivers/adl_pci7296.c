/*
    comedi/drivers/adl_pci7296.c

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
Driver: adl_pci7296
Description: Driver for the Adlink PCI-7296 96 ch. digital io board
Devices: [ADLink] PCI-7296 (adl_pci7296)
Author: Jon Grierson <jd@renko.co.uk>
Updated: Mon, 14 Apr 2008 15:05:56 +0100
Status: testing

Configuration Options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first supported
  PCI device found will be used.
*/

#include "../comedidev.h"
#include <linux/kernel.h>

#include "comedi_pci.h"
#include "8255.h"
/* #include "8253.h" */

#define PORT1A 0
#define PORT2A 4
#define PORT3A 8
#define PORT4A 12

#define PCI_DEVICE_ID_PCI7296 0x7296

static DEFINE_PCI_DEVICE_TABLE(adl_pci7296_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADLINK, PCI_DEVICE_ID_PCI7296) },
	{0}
};

MODULE_DEVICE_TABLE(pci, adl_pci7296_pci_table);

struct adl_pci7296_private {
	int data;
	struct pci_dev *pci_dev;
};

#define devpriv ((struct adl_pci7296_private *)dev->private)

static int adl_pci7296_attach(struct comedi_device *dev,
			      struct comedi_devconfig *it);
static int adl_pci7296_detach(struct comedi_device *dev);
static struct comedi_driver driver_adl_pci7296 = {
	.driver_name = "adl_pci7296",
	.module = THIS_MODULE,
	.attach = adl_pci7296_attach,
	.detach = adl_pci7296_detach,
};

static int adl_pci7296_attach(struct comedi_device *dev,
			      struct comedi_devconfig *it)
{
	struct pci_dev *pcidev = NULL;
	struct comedi_subdevice *s;
	int bus, slot;
	int ret;

	printk(KERN_INFO "comedi%d: attach adl_pci7432\n", dev->minor);

	dev->board_name = "pci7432";
	bus = it->options[0];
	slot = it->options[1];

	if (alloc_private(dev, sizeof(struct adl_pci7296_private)) < 0)
		return -ENOMEM;

	if (alloc_subdevices(dev, 4) < 0)
		return -ENOMEM;

	for_each_pci_dev(pcidev) {
		if (pcidev->vendor == PCI_VENDOR_ID_ADLINK &&
		    pcidev->device == PCI_DEVICE_ID_PCI7296) {
			if (bus || slot) {
				/* requested particular bus/slot */
				if (pcidev->bus->number != bus
				    || PCI_SLOT(pcidev->devfn) != slot) {
					continue;
				}
			}
			devpriv->pci_dev = pcidev;
			if (comedi_pci_enable(pcidev, "adl_pci7296") < 0) {
				printk(KERN_ERR "comedi%d: Failed to enable PCI device and request regions\n",
				     dev->minor);
				return -EIO;
			}

			dev->iobase = pci_resource_start(pcidev, 2);
			printk(KERN_INFO "comedi: base addr %4lx\n",
				dev->iobase);

			/*  four 8255 digital io subdevices */
			s = dev->subdevices + 0;
			subdev_8255_init(dev, s, NULL,
					 (unsigned long)(dev->iobase));

			s = dev->subdevices + 1;
			ret = subdev_8255_init(dev, s, NULL,
					       (unsigned long)(dev->iobase +
							       PORT2A));
			if (ret < 0)
				return ret;

			s = dev->subdevices + 2;
			ret = subdev_8255_init(dev, s, NULL,
					       (unsigned long)(dev->iobase +
							       PORT3A));
			if (ret < 0)
				return ret;

			s = dev->subdevices + 3;
			ret = subdev_8255_init(dev, s, NULL,
					       (unsigned long)(dev->iobase +
							       PORT4A));
			if (ret < 0)
				return ret;

			printk(KERN_DEBUG "comedi%d: adl_pci7432 attached\n",
				dev->minor);

			return 1;
		}
	}

	printk(KERN_ERR "comedi%d: no supported board found! (req. bus/slot : %d/%d)\n",
	       dev->minor, bus, slot);
	return -EIO;
}

static int adl_pci7296_detach(struct comedi_device *dev)
{
	printk(KERN_INFO "comedi%d: pci7432: remove\n", dev->minor);

	if (devpriv && devpriv->pci_dev) {
		if (dev->iobase)
			comedi_pci_disable(devpriv->pci_dev);
		pci_dev_put(devpriv->pci_dev);
	}
	/*  detach four 8255 digital io subdevices */
	if (dev->subdevices) {
		subdev_8255_cleanup(dev, dev->subdevices + 0);
		subdev_8255_cleanup(dev, dev->subdevices + 1);
		subdev_8255_cleanup(dev, dev->subdevices + 2);
		subdev_8255_cleanup(dev, dev->subdevices + 3);

	}

	return 0;
}

static int __devinit driver_adl_pci7296_pci_probe(struct pci_dev *dev,
						  const struct pci_device_id
						  *ent)
{
	return comedi_pci_auto_config(dev, driver_adl_pci7296.driver_name);
}

static void __devexit driver_adl_pci7296_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver driver_adl_pci7296_pci_driver = {
	.id_table = adl_pci7296_pci_table,
	.probe = &driver_adl_pci7296_pci_probe,
	.remove = __devexit_p(&driver_adl_pci7296_pci_remove)
};

static int __init driver_adl_pci7296_init_module(void)
{
	int retval;

	retval = comedi_driver_register(&driver_adl_pci7296);
	if (retval < 0)
		return retval;

	driver_adl_pci7296_pci_driver.name =
	    (char *)driver_adl_pci7296.driver_name;
	return pci_register_driver(&driver_adl_pci7296_pci_driver);
}

static void __exit driver_adl_pci7296_cleanup_module(void)
{
	pci_unregister_driver(&driver_adl_pci7296_pci_driver);
	comedi_driver_unregister(&driver_adl_pci7296);
}

module_init(driver_adl_pci7296_init_module);
module_exit(driver_adl_pci7296_cleanup_module);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

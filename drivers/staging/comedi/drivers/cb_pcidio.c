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

Configuration Options: not applicable, uses PCI auto config
*/

/*------------------------------ HEADER FILES ---------------------------------*/
#include "../comedidev.h"
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

static const void *pcidio_find_boardinfo(struct comedi_device *dev,
					 struct pci_dev *pcidev)
{
	const struct pcidio_board *board;
	int i;

	for (i = 0; i < ARRAY_SIZE(pcidio_boards); i++) {
		board = &pcidio_boards[i];
		if (board->dev_id == pcidev->device)
			return board;
	}
	return NULL;
}

static int pcidio_attach_pci(struct comedi_device *dev,
			     struct pci_dev *pcidev)
{
	const struct pcidio_board *board;
	struct comedi_subdevice *s;
	int i;
	int ret;

	comedi_set_hw_dev(dev, &pcidev->dev);

	board = pcidio_find_boardinfo(dev, pcidev);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	dev->iobase = pci_resource_start(pcidev, board->dioregs_badrindex);

	ret = comedi_alloc_subdevices(dev, board->n_8255);
	if (ret)
		return ret;

	for (i = 0; i < board->n_8255; i++) {
		s = &dev->subdevices[i];
		ret = subdev_8255_init(dev, s, NULL, dev->iobase + i * 4);
		if (ret)
			return ret;
	}

	dev_info(dev->class_dev, "%s attached (%d digital i/o channels)\n",
		dev->board_name, board->n_8255 * 24);

	return 0;
}

static void pcidio_detach(struct comedi_device *dev)
{
	const struct pcidio_board *board = comedi_board(dev);
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct comedi_subdevice *s;
	int i;

	if (dev->subdevices) {
		for (i = 0; i < board->n_8255; i++) {
			s = &dev->subdevices[i];
			subdev_8255_cleanup(dev, s);
		}
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver cb_pcidio_driver = {
	.driver_name	= "cb_pcidio",
	.module		= THIS_MODULE,
	.attach_pci	= pcidio_attach_pci,
	.detach		= pcidio_detach,
};

static int __devinit cb_pcidio_pci_probe(struct pci_dev *dev,
						const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &cb_pcidio_driver);
}

static void __devexit cb_pcidio_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(cb_pcidio_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0028) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x0014) },
	{ PCI_DEVICE(PCI_VENDOR_ID_CB, 0x000b) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, cb_pcidio_pci_table);

static struct pci_driver cb_pcidio_pci_driver = {
	.name		= "cb_pcidio",
	.id_table	= cb_pcidio_pci_table,
	.probe		= cb_pcidio_pci_probe,
	.remove		= __devexit_p(cb_pcidio_pci_remove),
};
module_comedi_pci_driver(cb_pcidio_driver, cb_pcidio_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

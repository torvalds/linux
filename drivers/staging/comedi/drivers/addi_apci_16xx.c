#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

#include "addi-data/hwdrv_apci16xx.c"

static const struct addi_board apci16xx_boardtypes[] = {
	{
		.pc_DriverName		= "apci1648",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x1009,
		.i_NbrTTLChannel	= 48,
		.ttl_config		= i_APCI16XX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI16XX_InsnBitsReadTTLIO,
		.ttl_read		= i_APCI16XX_InsnReadTTLIOAllPortValue,
		.ttl_write		= i_APCI16XX_InsnBitsWriteTTLIO,
	}, {
		.pc_DriverName		= "apci1696",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x100A,
		.i_NbrTTLChannel	= 96,
		.ttl_config		= i_APCI16XX_InsnConfigInitTTLIO,
		.ttl_bits		= i_APCI16XX_InsnBitsReadTTLIO,
		.ttl_read		= i_APCI16XX_InsnReadTTLIOAllPortValue,
		.ttl_write		= i_APCI16XX_InsnBitsWriteTTLIO,
	},
};

static const void *addi_find_boardinfo(struct comedi_device *dev,
				       struct pci_dev *pcidev)
{
	const void *p = dev->driver->board_name;
	const struct addi_board *this_board;
	int i;

	for (i = 0; i < dev->driver->num_names; i++) {
		this_board = p;
		if (this_board->i_VendorId == pcidev->vendor &&
		    this_board->i_DeviceId == pcidev->device)
			return this_board;
		p += dev->driver->offset;
	}
	return NULL;
}

static int apci16xx_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct addi_board *this_board;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, n_subdevices;

	this_board = addi_find_boardinfo(dev, pcidev);
	if (!this_board)
		return -ENODEV;
	dev->board_ptr = this_board;
	dev->board_name = this_board->pc_DriverName;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 0);

	n_subdevices = 7;
	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	s->type = COMEDI_SUBD_UNUSED;

	/*  Allocate and Initialise TTL */
	s = &dev->subdevices[5];
	if (this_board->i_NbrTTLChannel) {
		s->type = COMEDI_SUBD_TTLIO;
		s->subdev_flags =
			SDF_WRITEABLE | SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->i_NbrTTLChannel;
		s->maxdata = 1;
		s->io_bits = 0;	/* all bits input */
		s->len_chanlist = this_board->i_NbrTTLChannel;
		s->range_table = &range_digital;
		s->insn_config = this_board->ttl_config;
		s->insn_bits = this_board->ttl_bits;
		s->insn_read = this_board->ttl_read;
		s->insn_write = this_board->ttl_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* EEPROM */
	s = &dev->subdevices[6];
	s->type = COMEDI_SUBD_UNUSED;

	return 0;
}

static void apci16xx_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci16xx_driver = {
	.driver_name	= "addi_apci_16xx",
	.module		= THIS_MODULE,
	.auto_attach	= apci16xx_auto_attach,
	.detach		= apci16xx_detach,
	.num_names	= ARRAY_SIZE(apci16xx_boardtypes),
	.board_name	= &apci16xx_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int apci16xx_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci16xx_driver);
}

static void apci16xx_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci16xx_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1009) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x100a) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci16xx_pci_table);

static struct pci_driver apci16xx_pci_driver = {
	.name		= "addi_apci_16xx",
	.id_table	= apci16xx_pci_table,
	.probe		= apci16xx_pci_probe,
	.remove		= apci16xx_pci_remove,
};
module_comedi_pci_driver(apci16xx_driver, apci16xx_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"

#include "addi-data/hwdrv_apci16xx.c"

struct apci16xx_boardinfo {
	const char *name;
	unsigned short vendor;
	unsigned short device;
	int n_chan;
};

static const struct apci16xx_boardinfo apci16xx_boardtypes[] = {
	{
		.name		= "apci1648",
		.vendor		= PCI_VENDOR_ID_ADDIDATA,
		.device		= 0x1009,
		.n_chan		= 48,
	}, {
		.name		= "apci1696",
		.vendor		= PCI_VENDOR_ID_ADDIDATA,
		.device		= 0x100A,
		.n_chan		= 96,
	},
};

static const void *addi_find_boardinfo(struct comedi_device *dev,
				       struct pci_dev *pcidev)
{
	const void *p = dev->driver->board_name;
	const struct apci16xx_boardinfo *this_board;
	int i;

	for (i = 0; i < dev->driver->num_names; i++) {
		this_board = p;
		if (this_board->vendor == pcidev->vendor &&
		    this_board->device == pcidev->device)
			return this_board;
		p += dev->driver->offset;
	}
	return NULL;
}

static int apci16xx_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci16xx_boardinfo *this_board;
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	this_board = addi_find_boardinfo(dev, pcidev);
	if (!this_board)
		return -ENODEV;
	dev->board_ptr = this_board;
	dev->board_name = this_board->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 0);

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* Initialize the TTL digital i/o */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_WRITEABLE | SDF_READABLE;
	s->n_chan	= this_board->n_chan;
	s->maxdata	= 1;
	s->io_bits	= 0;	/* all bits input */
	s->len_chanlist	= this_board->n_chan;
	s->range_table	= &range_digital;
	s->insn_config	= i_APCI16XX_InsnConfigInitTTLIO;
	s->insn_bits	= i_APCI16XX_InsnBitsReadTTLIO;
	s->insn_read	= i_APCI16XX_InsnReadTTLIOAllPortValue;
	s->insn_write	= i_APCI16XX_InsnBitsWriteTTLIO;

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
	.board_name	= &apci16xx_boardtypes[0].name,
	.offset		= sizeof(struct apci16xx_boardinfo),
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

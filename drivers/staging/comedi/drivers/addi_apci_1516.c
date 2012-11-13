#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"

struct apci1516_private {
	unsigned long wdog_iobase;
};

#include "addi-data/hwdrv_apci1516.c"

struct apci1516_boardinfo {
	const char *name;
	unsigned short vendor;
	unsigned short device;
	int di_nchan;
	int do_nchan;
	int has_timer;
};

static const struct apci1516_boardinfo apci1516_boardtypes[] = {
	{
		.name		= "apci1016",
		.vendor		= PCI_VENDOR_ID_ADDIDATA,
		.device		= 0x1000,
		.di_nchan	= 16,
	}, {
		.name		= "apci1516",
		.vendor		= PCI_VENDOR_ID_ADDIDATA,
		.device		= 0x1001,
		.di_nchan	= 8,
		.do_nchan	= 8,
		.has_timer	= 1,
	}, {
		.name		= "apci2016",
		.vendor		= PCI_VENDOR_ID_ADDIDATA,
		.device		= 0x1002,
		.do_nchan	= 16,
		.has_timer	= 1,
	},
};

static int apci1516_reset(struct comedi_device *dev)
{
	const struct apci1516_boardinfo *this_board = comedi_board(dev);
	struct apci1516_private *devpriv = dev->private;

	if (!this_board->has_timer)
		return 0;

	outw(0x0, dev->iobase + APCI1516_DO_REG);
	outw(0x0, devpriv->wdog_iobase + APCI1516_WDOG_CTRL_REG);
	outw(0x0, devpriv->wdog_iobase + APCI1516_WDOG_RELOAD_LSB_REG);
	outw(0x0, devpriv->wdog_iobase + APCI1516_WDOG_RELOAD_MSB_REG);

	return 0;
}

static const void *addi_find_boardinfo(struct comedi_device *dev,
				       struct pci_dev *pcidev)
{
	const void *p = dev->driver->board_name;
	const struct apci1516_boardinfo *this_board;
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

static int __devinit apci1516_auto_attach(struct comedi_device *dev,
					  unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci1516_boardinfo *this_board;
	struct apci1516_private *devpriv;
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

	dev->iobase = pci_resource_start(pcidev, 1);
	devpriv->wdog_iobase = pci_resource_start(pcidev, 2);

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[0];
	if (this_board->di_nchan) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->di_nchan;
		s->maxdata = 1;
		s->len_chanlist = this_board->di_nchan;
		s->range_table = &range_digital;
		s->io_bits = 0;	/* all bits input */
		s->insn_bits = apci1516_di_insn_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[1];
	if (this_board->do_nchan) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags =
			SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->do_nchan;
		s->maxdata = 1;
		s->len_chanlist = this_board->do_nchan;
		s->range_table = &range_digital;
		s->io_bits = 0xf;	/* all bits output */
		s->insn_bits = apci1516_do_insn_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[2];
	if (this_board->has_timer) {
		s->type = COMEDI_SUBD_TIMER;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = 1;
		s->maxdata = 0;
		s->len_chanlist = 1;
		s->range_table = &range_digital;
		s->insn_write = i_APCI1516_StartStopWriteWatchdog;
		s->insn_read = i_APCI1516_ReadWatchdog;
		s->insn_config = i_APCI1516_ConfigWatchdog;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	apci1516_reset(dev);
	return 0;
}

static void apci1516_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);

	if (dev->iobase)
		apci1516_reset(dev);
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver apci1516_driver = {
	.driver_name	= "addi_apci_1516",
	.module		= THIS_MODULE,
	.auto_attach	= apci1516_auto_attach,
	.detach		= apci1516_detach,
	.num_names	= ARRAY_SIZE(apci1516_boardtypes),
	.board_name	= &apci1516_boardtypes[0].name,
	.offset		= sizeof(struct apci1516_boardinfo),
};

static int __devinit apci1516_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci1516_driver);
}

static void __devexit apci1516_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci1516_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1000) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1001) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA, 0x1002) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci1516_pci_table);

static struct pci_driver apci1516_pci_driver = {
	.name		= "addi_apci_1516",
	.id_table	= apci1516_pci_table,
	.probe		= apci1516_pci_probe,
	.remove		= __devexit_p(apci1516_pci_remove),
};
module_comedi_pci_driver(apci1516_driver, apci1516_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

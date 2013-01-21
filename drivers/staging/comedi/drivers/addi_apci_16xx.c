#include "../comedidev.h"
#include "comedi_fc.h"

/*
 * Register I/O map
 */
#define APCI16XX_IN_REG(x)		(((x) * 4) + 0x08)
#define APCI16XX_OUT_REG(x)		(((x) * 4) + 0x14)
#define APCI16XX_DIR_REG(x)		(((x) * 4) + 0x20)

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
		.n_chan		= 48,		/* 2 subdevices */
	}, {
		.name		= "apci1696",
		.vendor		= PCI_VENDOR_ID_ADDIDATA,
		.device		= 0x100A,
		.n_chan		= 96,		/* 3 subdevices */
	},
};

static int apci16xx_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int chan_mask = 1 << CR_CHAN(insn->chanspec);
	unsigned int bits;

	/*
	 * Each 8-bit "port" is configurable as either input or
	 * output. Changing the configuration of any channel in
	 * a port changes the entire port.
	 */
	if (chan_mask & 0x000000ff)
		bits = 0x000000ff;
	else if (chan_mask & 0x0000ff00)
		bits = 0x0000ff00;
	else if (chan_mask & 0x00ff0000)
		bits = 0x00ff0000;
	else
		bits = 0xff000000;

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~bits;
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= bits;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & bits) ? COMEDI_INPUT : COMEDI_OUTPUT;
		return insn->n;
	default:
		return -EINVAL;
	}

	outl(s->io_bits, dev->iobase + APCI16XX_DIR_REG(s->index));

	return insn->n;
}

static int apci16xx_dio_insn_bits(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	unsigned int mask = data[0];
	unsigned int bits = data[1];

	/* Only update the channels configured as outputs */
	mask &= s->io_bits;
	if (mask) {
		s->state &= ~mask;
		s->state |= (bits & mask);

		outl(s->state, dev->iobase + APCI16XX_OUT_REG(s->index));
	}

	data[1] = inl(dev->iobase + APCI16XX_IN_REG(s->index));

	return insn->n;
}

static const void *apci16xx_find_boardinfo(struct comedi_device *dev,
					   struct pci_dev *pcidev)
{
	const struct apci16xx_boardinfo *board;
	int i;

	for (i = 0; i < ARRAY_SIZE(apci16xx_boardtypes); i++) {
		board = &apci16xx_boardtypes[i];
		if (board->vendor == pcidev->vendor &&
		    board->device == pcidev->device)
			return board;
	}
	return NULL;
}

static int apci16xx_auto_attach(struct comedi_device *dev,
				unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct apci16xx_boardinfo *board;
	struct comedi_subdevice *s;
	unsigned int n_subdevs;
	unsigned int last;
	int i;
	int ret;

	board = apci16xx_find_boardinfo(dev, pcidev);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;

	dev->iobase = pci_resource_start(pcidev, 0);

	/*
	 * Work out the nubmer of subdevices needed to support all the
	 * digital i/o channels on the board. Each subdevice supports
	 * up to 32 channels.
	 */
	n_subdevs = board->n_chan / 32;
	if ((n_subdevs * 32) < board->n_chan) {
		last = board->n_chan - (n_subdevs * 32);
		n_subdevs++;
	} else {
		last = 0;
	}

	ret = comedi_alloc_subdevices(dev, n_subdevs);
	if (ret)
		return ret;

	/* Initialize the TTL digital i/o subdevices */
	for (i = 0; i < n_subdevs; i++) {
		s = &dev->subdevices[i];
		s->type		= COMEDI_SUBD_DIO;
		s->subdev_flags	= SDF_WRITEABLE | SDF_READABLE;
		s->n_chan	= ((i * 32) < board->n_chan) ? 32 : last;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_config	= apci16xx_insn_config;
		s->insn_bits	= apci16xx_dio_insn_bits;

		/* Default all channels to inputs */
		s->io_bits	= 0;
		outl(s->io_bits, dev->iobase + APCI16XX_DIR_REG(i));
	}

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

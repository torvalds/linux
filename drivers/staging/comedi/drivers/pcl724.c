/*
 * pcl724.c
 * Comedi driver for 8255 based ISA and PC/104 DIO boards
 *
 * Michal Dobes <dobes@tesnet.cz>
 */

/*
 * Driver: pcl724
 * Description: Comedi driver for 8255 based ISA DIO boards
 * Devices: (Advantech) PCL-724 [pcl724]
 *	    (Advantech) PCL-722 [pcl722]
 *	    (Advantech) PCL-731 [pcl731]
 *	    (ADLink) ACL-7122 [acl7122]
 *	    (ADLink) ACL-7124 [acl7124]
 *	    (ADLink) PET-48DIO [pet48dio]
 *	    (WinSystems) PCM-IO48 [pcmio48]
 *	    (Diamond Systems) ONYX-MM-DIO [onyx-mm-dio]
 * Author: Michal Dobes <dobes@tesnet.cz>
 * Status: untested
 *
 * Configuration options:
 *   [0] - IO Base
 *   [1] - IRQ (not supported)
 *   [2] - number of DIO (pcl722 and acl7122 boards)
 *	   0, 144: 144 DIO configuration
 *	   1,  96:  96 DIO configuration
 */

#include <linux/module.h>
#include "../comedidev.h"

#include "8255.h"

#define SIZE_8255	4

struct pcl724_board {
	const char *name;
	unsigned int io_range;
	unsigned int can_have96:1;
	unsigned int is_pet48:1;
	int numofports;
};

static const struct pcl724_board boardtypes[] = {
	{
		.name		= "pcl724",
		.io_range	= 0x04,
		.numofports	= 1,	/* 24 DIO channels */
	}, {
		.name		= "pcl722",
		.io_range	= 0x20,
		.can_have96	= 1,
		.numofports	= 6,	/* 144 (or 96) DIO channels */
	}, {
		.name		= "pcl731",
		.io_range	= 0x08,
		.numofports	= 2,	/* 48 DIO channels */
	}, {
		.name		= "acl7122",
		.io_range	= 0x20,
		.can_have96	= 1,
		.numofports	= 6,	/* 144 (or 96) DIO channels */
	}, {
		.name		= "acl7124",
		.io_range	= 0x04,
		.numofports	= 1,	/* 24 DIO channels */
	}, {
		.name		= "pet48dio",
		.io_range	= 0x02,
		.is_pet48	= 1,
		.numofports	= 2,	/* 48 DIO channels */
	}, {
		.name		= "pcmio48",
		.io_range	= 0x08,
		.numofports	= 2,	/* 48 DIO channels */
	}, {
		.name		= "onyx-mm-dio",
		.io_range	= 0x10,
		.numofports	= 2,	/* 48 DIO channels */
	},
};

static int pcl724_8255mapped_io(int dir, int port, int data,
				unsigned long iobase)
{
	int movport = SIZE_8255 * (iobase >> 12);

	iobase &= 0x0fff;

	outb(port + movport, iobase);
	if (dir) {
		outb(data, iobase + 1);
		return 0;
	}
	return inb(iobase + 1);
}

static int pcl724_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it)
{
	const struct pcl724_board *board = comedi_board(dev);
	struct comedi_subdevice *s;
	unsigned long iobase;
	unsigned int iorange;
	int n_subdevices;
	int ret;
	int i;

	iorange = board->io_range;
	n_subdevices = board->numofports;

	/* Handle PCL-724 in 96 DIO configuration */
	if (board->can_have96 &&
	    (it->options[2] == 1 || it->options[2] == 96)) {
		iorange = 0x10;
		n_subdevices = 4;
	}

	ret = comedi_request_region(dev, it->options[0], iorange);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		if (board->is_pet48) {
			iobase = dev->iobase + (i * 0x1000);
			ret = subdev_8255_init(dev, s, pcl724_8255mapped_io,
					       iobase);
		} else {
			iobase = dev->iobase + (i * SIZE_8255);
			ret = subdev_8255_init(dev, s, NULL, iobase);
		}
		if (ret)
			return ret;
	}

	return 0;
}

static struct comedi_driver pcl724_driver = {
	.driver_name	= "pcl724",
	.module		= THIS_MODULE,
	.attach		= pcl724_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct pcl724_board),
};
module_comedi_driver(pcl724_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for 8255 based ISA and PC/104 DIO boards");
MODULE_LICENSE("GPL");

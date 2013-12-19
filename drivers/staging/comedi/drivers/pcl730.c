/*
 * comedi/drivers/pcl730.c
 * Driver for Advantech PCL-730 and clones
 * José Luis Sánchez
 */

/*
 * Driver: pcl730
 * Description: Advantech PCL-730 (& compatibles)
 * Devices: (Advantech) PCL-730 [pcl730]
 *	    (ICP) ISO-730 [iso730]
 *	    (Adlink) ACL-7130 [acl7130]
 *	    (Advantech) PCM-3730 [pcm3730]
 *	    (Advantech) PCL-725 [pcl725]
 *	    (ICP) P8R8-DIO [p16r16dio]
 *	    (Adlink) ACL-7225b [acl7225b]
 *	    (ICP) P16R16-DIO [p16r16dio]
 *	    (Advantech) PCL-733 [pcl733]
 *	    (Advantech) PCL-734 [pcl734]
 * Author: José Luis Sánchez (jsanchezv@teleline.es)
 * Status: untested
 *
 * Configuration options:
 *   [0] - I/O port base
 *
 * Interrupts are not supported.
 * The ACL-7130 card has an 8254 timer/counter not supported by this driver.
 */

#include <linux/module.h>
#include "../comedidev.h"

/*
 * Register map
 *
 * The register map varies slightly depending on the board type but
 * all registers are 8-bit.
 *
 * The boardinfo 'io_range' is used to allow comedi to request the
 * proper range required by the board.
 *
 * The comedi_subdevice 'private' data is used to pass the register
 * offset to the (*insn_bits) functions to read/write the correct
 * registers.
 *
 * The basic register mapping looks like this:
 *
 *     BASE+0  Isolated outputs 0-7 (write) / inputs 0-7 (read)
 *     BASE+1  Isolated outputs 8-15 (write) / inputs 8-15 (read)
 *     BASE+2  TTL outputs 0-7 (write) / inputs 0-7 (read)
 *     BASE+3  TTL outputs 8-15 (write) / inputs 8-15 (read)
 *
 * The pcm3730 board does not have register BASE+1.
 *
 * The pcl725 and p8r8dio only have registers BASE+0 and BASE+1:
 *
 *     BASE+0  Isolated outputs 0-7 (write) (read back on p8r8dio)
 *     BASE+1  Isolated inputs 0-7 (read)
 *
 * The acl7225b and p16r16dio boards have this register mapping:
 *
 *     BASE+0  Isolated outputs 0-7 (write) (read back)
 *     BASE+1  Isolated outputs 8-15 (write) (read back)
 *     BASE+2  Isolated inputs 0-7 (read)
 *     BASE+3  Isolated inputs 8-15 (read)
 *
 * The pcl733 and pcl733 boards have this register mapping:
 *
 *     BASE+0  Isolated outputs 0-7 (write) or inputs 0-7 (read)
 *     BASE+1  Isolated outputs 8-15 (write) or inputs 8-15 (read)
 *     BASE+2  Isolated outputs 16-23 (write) or inputs 16-23 (read)
 *     BASE+3  Isolated outputs 24-31 (write) or inputs 24-31 (read)
 */

struct pcl730_board {
	const char *name;
	unsigned int io_range;
	unsigned is_pcl725:1;
	unsigned is_acl7225b:1;
	unsigned has_readback:1;
	unsigned has_ttl_io:1;
	int n_subdevs;
	int n_iso_out_chan;
	int n_iso_in_chan;
	int n_ttl_chan;
};

static const struct pcl730_board pcl730_boards[] = {
	{
		.name		= "pcl730",
		.io_range	= 0x04,
		.has_ttl_io	= 1,
		.n_subdevs	= 4,
		.n_iso_out_chan	= 16,
		.n_iso_in_chan	= 16,
		.n_ttl_chan	= 16,
	}, {
		.name		= "iso730",
		.io_range	= 0x04,
		.n_subdevs	= 4,
		.n_iso_out_chan	= 16,
		.n_iso_in_chan	= 16,
		.n_ttl_chan	= 16,
	}, {
		.name		= "acl7130",
		.io_range	= 0x08,
		.has_ttl_io	= 1,
		.n_subdevs	= 4,
		.n_iso_out_chan	= 16,
		.n_iso_in_chan	= 16,
		.n_ttl_chan	= 16,
	}, {
		.name		= "pcm3730",
		.io_range	= 0x04,
		.has_ttl_io	= 1,
		.n_subdevs	= 4,
		.n_iso_out_chan	= 8,
		.n_iso_in_chan	= 8,
		.n_ttl_chan	= 16,
	}, {
		.name		= "pcl725",
		.io_range	= 0x02,
		.is_pcl725	= 1,
		.n_subdevs	= 2,
		.n_iso_out_chan	= 8,
		.n_iso_in_chan	= 8,
	}, {
		.name		= "p8r8dio",
		.io_range	= 0x02,
		.is_pcl725	= 1,
		.has_readback	= 1,
		.n_subdevs	= 2,
		.n_iso_out_chan	= 8,
		.n_iso_in_chan	= 8,
	}, {
		.name		= "acl7225b",
		.io_range	= 0x08,		/* only 4 are used */
		.is_acl7225b	= 1,
		.has_readback	= 1,
		.n_subdevs	= 2,
		.n_iso_out_chan	= 16,
		.n_iso_in_chan	= 16,
	}, {
		.name		= "p16r16dio",
		.io_range	= 0x04,
		.is_acl7225b	= 1,
		.has_readback	= 1,
		.n_subdevs	= 2,
		.n_iso_out_chan	= 16,
		.n_iso_in_chan	= 16,
	}, {
		.name		= "pcl733",
		.io_range	= 0x04,
		.n_subdevs	= 1,
		.n_iso_in_chan	= 32,
	}, {
		.name		= "pcl734",
		.io_range	= 0x04,
		.n_subdevs	= 1,
		.n_iso_out_chan	= 32,
	},
};

static int pcl730_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned long reg = (unsigned long)s->private;
	unsigned int mask;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (mask & 0x00ff)
			outb(s->state & 0xff, dev->iobase + reg);
		if ((mask & 0xff00) && (s->n_chan > 8))
			outb((s->state >> 8) & 0xff, dev->iobase + reg + 1);
		if ((mask & 0xff0000) && (s->n_chan > 16))
			outb((s->state >> 16) & 0xff, dev->iobase + reg + 2);
		if ((mask & 0xff000000) && (s->n_chan > 24))
			outb((s->state >> 24) & 0xff, dev->iobase + reg + 3);
	}

	data[1] = s->state;

	return insn->n;
}

static unsigned int pcl730_get_bits(struct comedi_device *dev,
				    struct comedi_subdevice *s)
{
	unsigned long reg = (unsigned long)s->private;
	unsigned int val;

	val = inb(dev->iobase + reg);
	if (s->n_chan > 8)
		val |= (inb(dev->iobase + reg + 1) << 8);
	if (s->n_chan > 16)
		val |= (inb(dev->iobase + reg + 2) << 16);
	if (s->n_chan > 24)
		val |= (inb(dev->iobase + reg + 3) << 24);

	return val;
}

static int pcl730_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	data[1] = pcl730_get_bits(dev, s);

	return insn->n;
}

static int pcl730_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it)
{
	const struct pcl730_board *board = comedi_board(dev);
	struct comedi_subdevice *s;
	int subdev;
	int ret;

	ret = comedi_request_region(dev, it->options[0], board->io_range);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, board->n_subdevs);
	if (ret)
		return ret;

	subdev = 0;

	if (board->n_iso_out_chan) {
		/* Isolated Digital Outputs */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= board->n_iso_out_chan;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcl730_do_insn_bits;
		s->private	= (void *)0;

		/* get the initial state if supported */
		if (board->has_readback)
			s->state = pcl730_get_bits(dev, s);
	}

	if (board->n_iso_in_chan) {
		/* Isolated Digital Inputs */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= board->n_iso_in_chan;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcl730_di_insn_bits;
		s->private	= board->is_acl7225b ? (void *)2 :
				  board->is_pcl725 ? (void *)1 : (void *)0;
	}

	if (board->has_ttl_io) {
		/* TTL Digital Outputs */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= board->n_ttl_chan;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcl730_do_insn_bits;
		s->private	= (void *)2;

		/* TTL Digital Inputs */
		s = &dev->subdevices[subdev++];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= board->n_ttl_chan;
		s->maxdata	= 1;
		s->range_table	= &range_digital;
		s->insn_bits	= pcl730_di_insn_bits;
		s->private	= (void *)2;
	}

	return 0;
}

static struct comedi_driver pcl730_driver = {
	.driver_name	= "pcl730",
	.module		= THIS_MODULE,
	.attach		= pcl730_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &pcl730_boards[0].name,
	.num_names	= ARRAY_SIZE(pcl730_boards),
	.offset		= sizeof(struct pcl730_board),
};
module_comedi_driver(pcl730_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

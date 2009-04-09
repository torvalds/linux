/*
 * comedi/drivers/pcl730.c
 * Driver for Advantech PCL-730 and clones
 * José Luis Sánchez
 */
/*
Driver: pcl730
Description: Advantech PCL-730 (& compatibles)
Author: José Luis Sánchez (jsanchezv@teleline.es)
Status: untested
Devices: [Advantech] PCL-730 (pcl730), [ICP] ISO-730 (iso730),
		 [Adlink] ACL-7130 (acl7130)

Interrupts are not supported.
The ACL-7130 card have an 8254 timer/counter not supported by this driver.
*/

#include "../comedidev.h"

#include <linux/ioport.h>

#define PCL730_SIZE		4
#define ACL7130_SIZE	8
#define PCL730_IDIO_LO	0	/* Isolated Digital I/O low byte (ID0-ID7) */
#define PCL730_IDIO_HI	1	/* Isolated Digital I/O high byte (ID8-ID15) */
#define PCL730_DIO_LO	2	/* TTL Digital I/O low byte (D0-D7) */
#define PCL730_DIO_HI	3	/* TTL Digital I/O high byte (D8-D15) */

static int pcl730_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int pcl730_detach(struct comedi_device *dev);

struct pcl730_board {

	const char *name;	/*  board name */
	unsigned int io_range;	/*  len of I/O space */
};


static const struct pcl730_board boardtypes[] = {
	{"pcl730", PCL730_SIZE,},
	{"iso730", PCL730_SIZE,},
	{"acl7130", ACL7130_SIZE,},
};

#define n_boardtypes (sizeof(boardtypes)/sizeof(struct pcl730_board))
#define this_board ((const struct pcl730_board *)dev->board_ptr)

static struct comedi_driver driver_pcl730 = {
      driver_name:"pcl730",
      module:THIS_MODULE,
      attach:pcl730_attach,
      detach:pcl730_detach,
      board_name:&boardtypes[0].name,
      num_names:n_boardtypes,
      offset:sizeof(struct pcl730_board),
};

COMEDI_INITCLEANUP(driver_pcl730);

static int pcl730_do_insn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
	}
	if (data[0] & 0x00ff)
		outb(s->state & 0xff,
			dev->iobase + ((unsigned long)s->private));
	if (data[0] & 0xff00)
		outb((s->state >> 8),
			dev->iobase + ((unsigned long)s->private) + 1);

	data[1] = s->state;

	return 2;
}

static int pcl730_di_insn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inb(dev->iobase + ((unsigned long)s->private)) |
		(inb(dev->iobase + ((unsigned long)s->private) + 1) << 8);

	return 2;
}

static int pcl730_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase;
	unsigned int iorange;

	iobase = it->options[0];
	iorange = this_board->io_range;
	printk("comedi%d: pcl730: board=%s 0x%04lx ", dev->minor,
		this_board->name, iobase);
	if (!request_region(iobase, iorange, "pcl730")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->board_name = this_board->name;
	dev->iobase = iobase;
	dev->irq = 0;

	if (alloc_subdevices(dev, 4) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	/* Isolated do */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 1;
	s->n_chan = 16;
	s->insn_bits = pcl730_do_insn;
	s->range_table = &range_digital;
	s->private = (void *)PCL730_IDIO_LO;

	s = dev->subdevices + 1;
	/* Isolated di */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->maxdata = 1;
	s->n_chan = 16;
	s->insn_bits = pcl730_di_insn;
	s->range_table = &range_digital;
	s->private = (void *)PCL730_IDIO_LO;

	s = dev->subdevices + 2;
	/* TTL do */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 1;
	s->n_chan = 16;
	s->insn_bits = pcl730_do_insn;
	s->range_table = &range_digital;
	s->private = (void *)PCL730_DIO_LO;

	s = dev->subdevices + 3;
	/* TTL di */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->maxdata = 1;
	s->n_chan = 16;
	s->insn_bits = pcl730_di_insn;
	s->range_table = &range_digital;
	s->private = (void *)PCL730_DIO_LO;

	printk("\n");

	return 0;
}

static int pcl730_detach(struct comedi_device *dev)
{
	printk("comedi%d: pcl730: remove\n", dev->minor);

	if (dev->iobase)
		release_region(dev->iobase, this_board->io_range);

	return 0;
}

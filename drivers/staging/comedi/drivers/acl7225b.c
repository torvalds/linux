/*
 * comedi/drivers/acl7225b.c
 * Driver for Adlink NuDAQ ACL-7225b and clones
 * José Luis Sánchez
 */
/*
Driver: acl7225b
Description: Adlink NuDAQ ACL-7225b & compatibles
Author: José Luis Sánchez (jsanchezv@teleline.es)
Status: testing
Devices: [Adlink] ACL-7225b (acl7225b), [ICP] P16R16DIO (p16r16dio)
*/

#include "../comedidev.h"

#include <linux/ioport.h>

#define ACL7225_SIZE   8	/* Requires 8 ioports, but only 4 are used */
#define P16R16DIO_SIZE 4
#define ACL7225_RIO_LO 0	/* Relays input/output low byte (R0-R7) */
#define ACL7225_RIO_HI 1	/* Relays input/output high byte (R8-R15) */
#define ACL7225_DI_LO  2	/* Digital input low byte (DI0-DI7) */
#define ACL7225_DI_HI  3	/* Digital input high byte (DI8-DI15) */

static int acl7225b_attach(struct comedi_device *dev, struct comedi_devconfig * it);
static int acl7225b_detach(struct comedi_device *dev);

struct boardtype {
	const char *name;	/*  driver name */
	int io_range;		/*  len of I/O space */
};

static const struct boardtype boardtypes[] = {
	{"acl7225b", ACL7225_SIZE,},
	{"p16r16dio", P16R16DIO_SIZE,},
};

#define n_boardtypes (sizeof(boardtypes)/sizeof(struct boardtype))
#define this_board ((const struct boardtype *)dev->board_ptr)

static struct comedi_driver driver_acl7225b = {
      driver_name:"acl7225b",
      module:THIS_MODULE,
      attach:acl7225b_attach,
      detach:acl7225b_detach,
      board_name:&boardtypes[0].name,
      num_names:n_boardtypes,
      offset:sizeof(struct boardtype),
};

COMEDI_INITCLEANUP(driver_acl7225b);

static int acl7225b_do_insn(struct comedi_device *dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	if (insn->n != 2)
		return -EINVAL;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
	}
	if (data[0] & 0x00ff)
		outb(s->state & 0xff, dev->iobase + (unsigned long)s->private);
	if (data[0] & 0xff00)
		outb((s->state >> 8),
			dev->iobase + (unsigned long)s->private + 1);

	data[1] = s->state;

	return 2;
}

static int acl7225b_di_insn(struct comedi_device *dev, struct comedi_subdevice * s,
	struct comedi_insn * insn, unsigned int * data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inb(dev->iobase + (unsigned long)s->private) |
		(inb(dev->iobase + (unsigned long)s->private + 1) << 8);

	return 2;
}

static int acl7225b_attach(struct comedi_device *dev, struct comedi_devconfig * it)
{
	struct comedi_subdevice *s;
	int iobase, iorange;

	iobase = it->options[0];
	iorange = this_board->io_range;
	printk("comedi%d: acl7225b: board=%s 0x%04x ", dev->minor,
		this_board->name, iobase);
	if (!request_region(iobase, iorange, "acl7225b")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->board_name = this_board->name;
	dev->iobase = iobase;
	dev->irq = 0;

	if (alloc_subdevices(dev, 3) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	/* Relays outputs */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 1;
	s->n_chan = 16;
	s->insn_bits = acl7225b_do_insn;
	s->range_table = &range_digital;
	s->private = (void *)ACL7225_RIO_LO;

	s = dev->subdevices + 1;
	/* Relays status */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->maxdata = 1;
	s->n_chan = 16;
	s->insn_bits = acl7225b_di_insn;
	s->range_table = &range_digital;
	s->private = (void *)ACL7225_RIO_LO;

	s = dev->subdevices + 2;
	/* Isolated digital inputs */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->maxdata = 1;
	s->n_chan = 16;
	s->insn_bits = acl7225b_di_insn;
	s->range_table = &range_digital;
	s->private = (void *)ACL7225_DI_LO;

	printk("\n");

	return 0;
}

static int acl7225b_detach(struct comedi_device *dev)
{
	printk("comedi%d: acl7225b: remove\n", dev->minor);

	if (dev->iobase)
		release_region(dev->iobase, this_board->io_range);

	return 0;
}

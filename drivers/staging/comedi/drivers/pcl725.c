/*
 * comedi/drivers/pcl725.c
 * Driver for PCL725 and clones
 * David A. Schleef
 */
/*
Driver: pcl725
Description: Advantech PCL-725 (& compatibles)
Author: ds
Status: unknown
Devices: [Advantech] PCL-725 (pcl725)
*/

#include "../comedidev.h"

#include <linux/ioport.h>

#define PCL725_SIZE 2

#define PCL725_DO 0
#define PCL725_DI 1

static int pcl725_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it);
static int pcl725_detach(struct comedi_device *dev);
static struct comedi_driver driver_pcl725 = {
	.driver_name = "pcl725",
	.module = THIS_MODULE,
	.attach = pcl725_attach,
	.detach = pcl725_detach,
};

COMEDI_INITCLEANUP(driver_pcl725);

static int pcl725_do_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		outb(s->state, dev->iobase + PCL725_DO);
	}

	data[1] = s->state;

	return 2;
}

static int pcl725_di_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n != 2)
		return -EINVAL;

	data[1] = inb(dev->iobase + PCL725_DI);

	return 2;
}

static int pcl725_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase;

	iobase = it->options[0];
	printk("comedi%d: pcl725: 0x%04lx ", dev->minor, iobase);
	if (!request_region(iobase, PCL725_SIZE, "pcl725")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->board_name = "pcl725";
	dev->iobase = iobase;
	dev->irq = 0;

	if (alloc_subdevices(dev, 2) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	/* do */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 1;
	s->n_chan = 8;
	s->insn_bits = pcl725_do_insn;
	s->range_table = &range_digital;

	s = dev->subdevices + 1;
	/* di */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->maxdata = 1;
	s->n_chan = 8;
	s->insn_bits = pcl725_di_insn;
	s->range_table = &range_digital;

	printk("\n");

	return 0;
}

static int pcl725_detach(struct comedi_device *dev)
{
	printk("comedi%d: pcl725: remove\n", dev->minor);

	if (dev->iobase)
		release_region(dev->iobase, PCL725_SIZE);

	return 0;
}

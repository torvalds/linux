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

static int pcl725_do_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		outb(s->state, dev->iobase + PCL725_DO);
	}

	data[1] = s->state;

	return insn->n;
}

static int pcl725_di_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	data[1] = inb(dev->iobase + PCL725_DI);

	return insn->n;
}

static int pcl725_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], PCL725_SIZE);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* do */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 1;
	s->n_chan = 8;
	s->insn_bits = pcl725_do_insn;
	s->range_table = &range_digital;

	s = &dev->subdevices[1];
	/* di */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->maxdata = 1;
	s->n_chan = 8;
	s->insn_bits = pcl725_di_insn;
	s->range_table = &range_digital;

	printk(KERN_INFO "\n");

	return 0;
}

static struct comedi_driver pcl725_driver = {
	.driver_name	= "pcl725",
	.module		= THIS_MODULE,
	.attach		= pcl725_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(pcl725_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

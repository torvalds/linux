/*
    comedi/drivers/fl512.c
    Anders Gnistrup <ex18@kalman.iau.dtu.dk>
*/

/*
Driver: fl512
Description: unknown
Author: Anders Gnistrup <ex18@kalman.iau.dtu.dk>
Devices: [unknown] FL512 (fl512)
Status: unknown

Digital I/O is not supported.

Configuration options:
  [0] - I/O port base address
*/

#define DEBUG 0

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/ioport.h>

#define FL512_SIZE 16		/* the size of the used memory */
struct fl512_private {

	short ao_readback[2];
};

static const struct comedi_lrange range_fl512 = { 4, {
						      BIP_RANGE(0.5),
						      BIP_RANGE(1),
						      BIP_RANGE(5),
						      BIP_RANGE(10),
						      UNI_RANGE(1),
						      UNI_RANGE(5),
						      UNI_RANGE(10),
						      }
};

/*
 * fl512_ai_insn : this is the analog input function
 */
static int fl512_ai_insn(struct comedi_device *dev,
			 struct comedi_subdevice *s, struct comedi_insn *insn,
			 unsigned int *data)
{
	int n;
	unsigned int lo_byte, hi_byte;
	char chan = CR_CHAN(insn->chanspec);
	unsigned long iobase = dev->iobase;

	for (n = 0; n < insn->n; n++) {	/* sample n times on selected channel */
		/* XXX probably can move next step out of for() loop -- will
		 * make AI a little bit faster. */
		outb(chan, iobase + 2);	/* select chan */
		outb(0, iobase + 3);	/* start conversion */
		/* XXX should test "done" flag instead of delay */
		udelay(30);	/* sleep 30 usec */
		lo_byte = inb(iobase + 2);	/* low 8 byte */
		hi_byte = inb(iobase + 3) & 0xf; /* high 4 bit and mask */
		data[n] = lo_byte + (hi_byte << 8);
	}
	return n;
}

/*
 * fl512_ao_insn : used to write to a DA port n times
 */
static int fl512_ao_insn(struct comedi_device *dev,
			 struct comedi_subdevice *s, struct comedi_insn *insn,
			 unsigned int *data)
{
	struct fl512_private *devpriv = dev->private;
	int n;
	int chan = CR_CHAN(insn->chanspec);	/* get chan to write */
	unsigned long iobase = dev->iobase;	/* get base address  */

	for (n = 0; n < insn->n; n++) {	/* write n data set */
		/* write low byte   */
		outb(data[n] & 0x0ff, iobase + 4 + 2 * chan);
		/* write high byte  */
		outb((data[n] & 0xf00) >> 8, iobase + 4 + 2 * chan);
		inb(iobase + 4 + 2 * chan);	/* trig */

		devpriv->ao_readback[chan] = data[n];
	}
	return n;
}

/*
 * fl512_ao_insn_readback : used to read previous values written to
 * DA port
 */
static int fl512_ao_insn_readback(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	struct fl512_private *devpriv = dev->private;
	int n;
	int chan = CR_CHAN(insn->chanspec);

	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_readback[chan];

	return n;
}

static int fl512_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct fl512_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], FL512_SIZE);
	if (ret)
		return ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/*
	 * this if the definitions of the supdevices, 2 have been defined
	 */
	/* Analog indput */
	s = &dev->subdevices[0];
	/* define subdevice as Analog In */
	s->type = COMEDI_SUBD_AI;
	/* you can read it from userspace */
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	/* Number of Analog input channels */
	s->n_chan = 16;
	/* accept only 12 bits of data */
	s->maxdata = 0x0fff;
	/* device use one of the ranges */
	s->range_table = &range_fl512;
	/* function to call when read AD */
	s->insn_read = fl512_ai_insn;

	/* Analog output */
	s = &dev->subdevices[1];
	/* define subdevice as Analog OUT */
	s->type = COMEDI_SUBD_AO;
	/* you can write it from userspace */
	s->subdev_flags = SDF_WRITABLE;
	/* Number of Analog output channels */
	s->n_chan = 2;
	/* accept only 12 bits of data */
	s->maxdata = 0x0fff;
	/* device use one of the ranges */
	s->range_table = &range_fl512;
	/* function to call when write DA */
	s->insn_write = fl512_ao_insn;
	/* function to call when reading DA */
	s->insn_read = fl512_ao_insn_readback;

	return 1;
}

static struct comedi_driver fl512_driver = {
	.driver_name	= "fl512",
	.module		= THIS_MODULE,
	.attach		= fl512_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(fl512_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

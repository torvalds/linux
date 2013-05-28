/*
    comedi/drivers/adq12b.c
    driver for MicroAxial ADQ12-B data acquisition and control card

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/*
Driver: adq12b
Description: driver for MicroAxial ADQ12-B data acquisition and control card
Devices: [MicroAxial] ADQ12-B (adq12b)
Author: jeremy theler <thelerg@ib.cnea.gov.ar>
Updated: Thu, 21 Feb 2008 02:56:27 -0300
Status: works

Driver for the acquisition card ADQ12-B (without any add-on).

 - Analog input is subdevice 0 (16 channels single-ended or 8 differential)
 - Digital input is subdevice 1 (5 channels)
 - Digital output is subdevice 1 (8 channels)
 - The PACER is not supported in this version

If you do not specify any options, they will default to

  # comedi_config /dev/comedi0 adq12b 0x300,0,0

  option 1: I/O base address. The following table is provided as a help
   of the hardware jumpers.

	 address            jumper JADR
	  0x300                 1 (factory default)
	  0x320                 2
	  0x340                 3
	  0x360                 4
	  0x380                 5
	  0x3A0                 6

  option 2: unipolar/bipolar ADC selection: 0 -> bipolar, 1 -> unipolar

	selection         comedi_config option            JUB
	 bipolar                0                         2-3 (factory default)
	 unipolar               1                         1-2

  option 3: single-ended/differential AI selection: 0 -> SE, 1 -> differential

	selection         comedi_config option     JCHA    JCHB
       single-ended             0                  1-2     1-2 (factory default)
       differential             1                  2-3     2-3

   written by jeremy theler <thelerg@ib.cnea.gov.ar>

   instituto balseiro
   commission nacional de energia atomica
   universidad nacional de cuyo
   argentina

   21-feb-2008
     + changed supported devices string (missused the [] and ())

   13-oct-2007
     + first try


*/

#include "../comedidev.h"

/* address scheme (page 2.17 of the manual) */
#define ADQ12B_SIZE     16

#define ADQ12B_CTREG    0x00
#define ADQ12B_STINR    0x00
#define ADQ12B_OUTBR    0x04
#define ADQ12B_ADLOW    0x08
#define ADQ12B_ADHIG    0x09
#define ADQ12B_CONT0    0x0c
#define ADQ12B_CONT1    0x0d
#define ADQ12B_CONT2    0x0e
#define ADQ12B_COWORD   0x0f

/* mask of the bit at STINR to check end of conversion */
#define ADQ12B_EOC     0x20

#define TIMEOUT        20

/* available ranges through the PGA gains */
static const struct comedi_lrange range_adq12b_ai_bipolar = { 4, {
								  BIP_RANGE(5),
								  BIP_RANGE(2),
								  BIP_RANGE(1),
								  BIP_RANGE(0.5)
								  }
};

static const struct comedi_lrange range_adq12b_ai_unipolar = { 4, {
								   UNI_RANGE(5),
								   UNI_RANGE(2),
								   UNI_RANGE(1),
								   UNI_RANGE
								   (0.5)
								   }
};

struct adq12b_private {
	int unipolar;		/* option 2 of comedi_config (1 is iobase) */
	int differential;	/* option 3 of comedi_config */
	int last_channel;
	int last_range;
	unsigned int digital_state;
};

/*
 * "instructions" read/write data in "one-shot" or "software-triggered"
 * mode.
 */

static int adq12b_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	struct adq12b_private *devpriv = dev->private;
	int n, i;
	int range, channel;
	unsigned char hi, lo, status;

	/* change channel and range only if it is different from the previous */
	range = CR_RANGE(insn->chanspec);
	channel = CR_CHAN(insn->chanspec);
	if (channel != devpriv->last_channel || range != devpriv->last_range) {
		outb((range << 4) | channel, dev->iobase + ADQ12B_CTREG);
		udelay(50);	/* wait for the mux to settle */
	}

	/* trigger conversion */
	status = inb(dev->iobase + ADQ12B_ADLOW);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {

		/* wait for end of conversion */
		i = 0;
		do {
			/* udelay(1); */
			status = inb(dev->iobase + ADQ12B_STINR);
			status = status & ADQ12B_EOC;
		} while (status == 0 && ++i < TIMEOUT);
		/* } while (++i < 10); */

		/* read data */
		hi = inb(dev->iobase + ADQ12B_ADHIG);
		lo = inb(dev->iobase + ADQ12B_ADLOW);

		/* printk("debug: chan=%d range=%d status=%d hi=%d lo=%d\n",
		       channel, range, status,  hi, lo); */
		data[n] = (hi << 8) | lo;

	}

	/* return the number of samples read/written */
	return n;
}

static int adq12b_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{

	/* only bits 0-4 have information about digital inputs */
	data[1] = (inb(dev->iobase + ADQ12B_STINR) & (0x1f));

	return insn->n;
}

static int adq12b_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct adq12b_private *devpriv = dev->private;
	int channel;

	for (channel = 0; channel < 8; channel++)
		if (((data[0] >> channel) & 0x01) != 0)
			outb((((data[1] >> channel) & 0x01) << 3) | channel,
			     dev->iobase + ADQ12B_OUTBR);

	/* store information to retrieve when asked for reading */
	if (data[0]) {
		devpriv->digital_state &= ~data[0];
		devpriv->digital_state |= (data[0] & data[1]);
	}

	data[1] = devpriv->digital_state;

	return insn->n;
}

static int adq12b_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct adq12b_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], ADQ12B_SIZE);
	if (ret)
		return ret;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	devpriv->unipolar = it->options[1];
	devpriv->differential = it->options[2];
	devpriv->digital_state = 0;
	/*
	 * initialize channel and range to -1 so we make sure we
	 * always write at least once to the CTREG in the instruction
	 */
	devpriv->last_channel = -1;
	devpriv->last_range = -1;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog input subdevice */
	s->type = COMEDI_SUBD_AI;
	if (devpriv->differential) {
		s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF;
		s->n_chan = 8;
	} else {
		s->subdev_flags = SDF_READABLE | SDF_GROUND;
		s->n_chan = 16;
	}

	if (devpriv->unipolar)
		s->range_table = &range_adq12b_ai_unipolar;
	else
		s->range_table = &range_adq12b_ai_bipolar;

	s->maxdata = 0xfff;

	s->len_chanlist = 4;	/* This is the maximum chanlist length that
				   the board can handle */
	s->insn_read = adq12b_ai_rinsn;

	s = &dev->subdevices[1];
	/* digital input subdevice */
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 5;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = adq12b_di_insn_bits;

	s = &dev->subdevices[2];
	/* digital output subdevice */
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = adq12b_do_insn_bits;

	return 0;
}

static struct comedi_driver adq12b_driver = {
	.driver_name	= "adq12b",
	.module		= THIS_MODULE,
	.attach		= adq12b_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(adq12b_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

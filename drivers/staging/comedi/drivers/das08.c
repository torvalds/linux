/*
 * comedi/drivers/das08.c
 * comedi module for common DAS08 support (used by ISA/PCI/PCMCIA drivers)
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2001,2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 * Copyright (C) 2004 Salvador E. Tropea <set@users.sf.net> <set@ieee.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>

#include "../comedidev.h"

#include "8255.h"
#include "comedi_8254.h"
#include "das08.h"

/*
 * Data format of DAS08_AI_LSB_REG and DAS08_AI_MSB_REG depends on
 * 'ai_encoding' member of board structure:
 *
 * das08_encode12     : DATA[11..4] = MSB[7..0], DATA[3..0] = LSB[7..4].
 * das08_pcm_encode12 : DATA[11..8] = MSB[3..0], DATA[7..9] = LSB[7..0].
 * das08_encode16     : SIGN = MSB[7], MAGNITUDE[14..8] = MSB[6..0],
 *                      MAGNITUDE[7..0] = LSB[7..0].
 *                      SIGN==0 for negative input, SIGN==1 for positive input.
 *                      Note: when read a second time after conversion
 *                            complete, MSB[7] is an "over-range" bit.
 */
#define DAS08_AI_LSB_REG	0x00	/* (R) AI least significant bits */
#define DAS08_AI_MSB_REG	0x01	/* (R) AI most significant bits */
#define DAS08_AI_TRIG_REG	0x01	/* (W) AI software trigger */
#define DAS08_STATUS_REG	0x02	/* (R) status */
#define DAS08_STATUS_AI_BUSY	BIT(7)	/* AI conversion in progress */
/*
 * The IRQ status bit is set to 1 by a rising edge on the external interrupt
 * input (which may be jumpered to the pacer output).  It is cleared by
 * setting the INTE control bit to 0.  Not present on "JR" boards.
 */
#define DAS08_STATUS_IRQ	BIT(3)	/* latched interrupt input */
/* digital inputs (not "JR" boards) */
#define DAS08_STATUS_DI(x)	(((x) & 0x70) >> 4)
#define DAS08_CONTROL_REG	0x02	/* (W) control */
/*
 * Note: The AI multiplexor channel can also be read from status register using
 * the same mask.
 */
#define DAS08_CONTROL_MUX_MASK	0x7	/* multiplexor channel mask */
#define DAS08_CONTROL_MUX(x)	((x) & DAS08_CONTROL_MUX_MASK) /* mux channel */
#define DAS08_CONTROL_INTE	BIT(3)	/* interrupt enable (not "JR" boards) */
#define DAS08_CONTROL_DO_MASK	0xf0	/* digital outputs mask (not "JR") */
/* digital outputs (not "JR" boards) */
#define DAS08_CONTROL_DO(x)	(((x) << 4) & DAS08_CONTROL_DO_MASK)
/*
 * (R/W) programmable AI gain ("PGx" and "AOx" boards):
 * + bits 3..0 (R/W) show/set the gain for the current AI mux channel
 * + bits 6..4 (R) show the current AI mux channel
 * + bit 7 (R) not unused
 */
#define DAS08_GAIN_REG		0x03

#define DAS08JR_DI_REG		0x03	/* (R) digital inputs ("JR" boards) */
#define DAS08JR_DO_REG		0x03	/* (W) digital outputs ("JR" boards) */
/* (W) analog output l.s.b. registers for 2 channels ("JR" boards) */
#define DAS08JR_AO_LSB_REG(x)	((x) ? 0x06 : 0x04)
/* (W) analog output m.s.b. registers for 2 channels ("JR" boards) */
#define DAS08JR_AO_MSB_REG(x)	((x) ? 0x07 : 0x05)
/*
 * (R) update analog outputs ("JR" boards set for simultaneous output)
 *     (same register as digital inputs)
 */
#define DAS08JR_AO_UPDATE_REG	0x03

/* (W) analog output l.s.b. registers for 2 channels ("AOx" boards) */
#define DAS08AOX_AO_LSB_REG(x)	((x) ? 0x0a : 0x08)
/* (W) analog output m.s.b. registers for 2 channels ("AOx" boards) */
#define DAS08AOX_AO_MSB_REG(x)	((x) ? 0x0b : 0x09)
/*
 * (R) update analog outputs ("AOx" boards set for simultaneous output)
 *     (any of the analog output registers could be used for this)
 */
#define DAS08AOX_AO_UPDATE_REG	0x08

/* gainlist same as _pgx_ below */

static const struct comedi_lrange das08_pgl_ai_range = {
	9, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25)
	}
};

static const struct comedi_lrange das08_pgh_ai_range = {
	12, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(1),
		BIP_RANGE(0.5),
		BIP_RANGE(0.1),
		BIP_RANGE(0.05),
		BIP_RANGE(0.01),
		BIP_RANGE(0.005),
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.01)
	}
};

static const struct comedi_lrange das08_pgm_ai_range = {
	9, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(0.5),
		BIP_RANGE(0.05),
		BIP_RANGE(0.01),
		UNI_RANGE(10),
		UNI_RANGE(1),
		UNI_RANGE(0.1),
		UNI_RANGE(0.01)
	}
};

static const struct comedi_lrange *const das08_ai_lranges[] = {
	[das08_pg_none]		= &range_unknown,
	[das08_bipolar5]	= &range_bipolar5,
	[das08_pgh]		= &das08_pgh_ai_range,
	[das08_pgl]		= &das08_pgl_ai_range,
	[das08_pgm]		= &das08_pgm_ai_range,
};

static const int das08_pgh_ai_gainlist[] = {
	8, 0, 10, 2, 12, 4, 14, 6, 1, 3, 5, 7
};
static const int das08_pgl_ai_gainlist[] = { 8, 0, 2, 4, 6, 1, 3, 5, 7 };
static const int das08_pgm_ai_gainlist[] = { 8, 0, 10, 12, 14, 9, 11, 13, 15 };

static const int *const das08_ai_gainlists[] = {
	[das08_pg_none]		= NULL,
	[das08_bipolar5]	= NULL,
	[das08_pgh]		= das08_pgh_ai_gainlist,
	[das08_pgl]		= das08_pgl_ai_gainlist,
	[das08_pgm]		= das08_pgm_ai_gainlist,
};

static int das08_ai_eoc(struct comedi_device *dev,
			struct comedi_subdevice *s,
			struct comedi_insn *insn,
			unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + DAS08_STATUS_REG);
	if ((status & DAS08_STATUS_AI_BUSY) == 0)
		return 0;
	return -EBUSY;
}

static int das08_ai_insn_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	const struct das08_board_struct *board = dev->board_ptr;
	struct das08_private_struct *devpriv = dev->private;
	int n;
	int chan;
	int range;
	int lsb, msb;
	int ret;

	chan = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);

	/* clear crap */
	inb(dev->iobase + DAS08_AI_LSB_REG);
	inb(dev->iobase + DAS08_AI_MSB_REG);

	/* set multiplexer */
	/* lock to prevent race with digital output */
	spin_lock(&dev->spinlock);
	devpriv->do_mux_bits &= ~DAS08_CONTROL_MUX_MASK;
	devpriv->do_mux_bits |= DAS08_CONTROL_MUX(chan);
	outb(devpriv->do_mux_bits, dev->iobase + DAS08_CONTROL_REG);
	spin_unlock(&dev->spinlock);

	if (devpriv->pg_gainlist) {
		/* set gain/range */
		range = CR_RANGE(insn->chanspec);
		outb(devpriv->pg_gainlist[range],
		     dev->iobase + DAS08_GAIN_REG);
	}

	for (n = 0; n < insn->n; n++) {
		/* clear over-range bits for 16-bit boards */
		if (board->ai_nbits == 16)
			if (inb(dev->iobase + DAS08_AI_MSB_REG) & 0x80)
				dev_info(dev->class_dev, "over-range\n");

		/* trigger conversion */
		outb_p(0, dev->iobase + DAS08_AI_TRIG_REG);

		ret = comedi_timeout(dev, s, insn, das08_ai_eoc, 0);
		if (ret)
			return ret;

		msb = inb(dev->iobase + DAS08_AI_MSB_REG);
		lsb = inb(dev->iobase + DAS08_AI_LSB_REG);
		if (board->ai_encoding == das08_encode12) {
			data[n] = (lsb >> 4) | (msb << 4);
		} else if (board->ai_encoding == das08_pcm_encode12) {
			data[n] = (msb << 8) + lsb;
		} else if (board->ai_encoding == das08_encode16) {
			/*
			 * "JR" 16-bit boards are sign-magnitude.
			 *
			 * XXX The manual seems to imply that 0 is full-scale
			 * negative and 65535 is full-scale positive, but the
			 * original COMEDI patch to add support for the
			 * DAS08/JR/16 and DAS08/JR/16-AO boards have it
			 * encoded as sign-magnitude.  Assume the original
			 * COMEDI code is correct for now.
			 */
			unsigned int magnitude = lsb | ((msb & 0x7f) << 8);

			/*
			 * MSB bit 7 is 0 for negative, 1 for positive voltage.
			 * COMEDI 16-bit bipolar data value for 0V is 0x8000.
			 */
			if (msb & 0x80)
				data[n] = (1 << 15) + magnitude;
			else
				data[n] = (1 << 15) - magnitude;
		} else {
			dev_err(dev->class_dev, "bug! unknown ai encoding\n");
			return -1;
		}
	}

	return n;
}

static int das08_di_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	data[0] = 0;
	data[1] = DAS08_STATUS_DI(inb(dev->iobase + DAS08_STATUS_REG));

	return insn->n;
}

static int das08_do_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	struct das08_private_struct *devpriv = dev->private;

	if (comedi_dio_update_state(s, data)) {
		/* prevent race with setting of analog input mux */
		spin_lock(&dev->spinlock);
		devpriv->do_mux_bits &= ~DAS08_CONTROL_DO_MASK;
		devpriv->do_mux_bits |= DAS08_CONTROL_DO(s->state);
		outb(devpriv->do_mux_bits, dev->iobase + DAS08_CONTROL_REG);
		spin_unlock(&dev->spinlock);
	}

	data[1] = s->state;

	return insn->n;
}

static int das08jr_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	data[0] = 0;
	data[1] = inb(dev->iobase + DAS08JR_DI_REG);

	return insn->n;
}

static int das08jr_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outb(s->state, dev->iobase + DAS08JR_DO_REG);

	data[1] = s->state;

	return insn->n;
}

static void das08_ao_set_data(struct comedi_device *dev,
			      unsigned int chan, unsigned int data)
{
	const struct das08_board_struct *board = dev->board_ptr;
	unsigned char lsb;
	unsigned char msb;

	lsb = data & 0xff;
	msb = (data >> 8) & 0xff;
	if (board->is_jr) {
		outb(lsb, dev->iobase + DAS08JR_AO_LSB_REG(chan));
		outb(msb, dev->iobase + DAS08JR_AO_MSB_REG(chan));
		/* load DACs */
		inb(dev->iobase + DAS08JR_AO_UPDATE_REG);
	} else {
		outb(lsb, dev->iobase + DAS08AOX_AO_LSB_REG(chan));
		outb(msb, dev->iobase + DAS08AOX_AO_MSB_REG(chan));
		/* load DACs */
		inb(dev->iobase + DAS08AOX_AO_UPDATE_REG);
	}
}

static int das08_ao_insn_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		das08_ao_set_data(dev, chan, val);
	}
	s->readback[chan] = val;

	return insn->n;
}

int das08_common_attach(struct comedi_device *dev, unsigned long iobase)
{
	const struct das08_board_struct *board = dev->board_ptr;
	struct das08_private_struct *devpriv = dev->private;
	struct comedi_subdevice *s;
	int ret;
	int i;

	dev->iobase = iobase;

	dev->board_name = board->name;

	ret = comedi_alloc_subdevices(dev, 6);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* ai */
	if (board->ai_nbits) {
		s->type = COMEDI_SUBD_AI;
		/*
		 * XXX some boards actually have differential
		 * inputs instead of single ended.
		 * The driver does nothing with arefs though,
		 * so it's no big deal.
		 */
		s->subdev_flags = SDF_READABLE | SDF_GROUND;
		s->n_chan = 8;
		s->maxdata = (1 << board->ai_nbits) - 1;
		s->range_table = das08_ai_lranges[board->ai_pg];
		s->insn_read = das08_ai_insn_read;
		devpriv->pg_gainlist = das08_ai_gainlists[board->ai_pg];
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[1];
	/* ao */
	if (board->ao_nbits) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITABLE;
		s->n_chan = 2;
		s->maxdata = (1 << board->ao_nbits) - 1;
		s->range_table = &range_bipolar5;
		s->insn_write = das08_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		/* initialize all channels to 0V */
		for (i = 0; i < s->n_chan; i++) {
			s->readback[i] = s->maxdata / 2;
			das08_ao_set_data(dev, i, s->readback[i]);
		}
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[2];
	/* di */
	if (board->di_nchan) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE;
		s->n_chan = board->di_nchan;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = board->is_jr ? das08jr_di_insn_bits :
			       das08_di_insn_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[3];
	/* do */
	if (board->do_nchan) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_WRITABLE;
		s->n_chan = board->do_nchan;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = board->is_jr ? das08jr_do_insn_bits :
			       das08_do_insn_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[4];
	/* 8255 */
	if (board->i8255_offset != 0) {
		ret = subdev_8255_init(dev, s, NULL, board->i8255_offset);
		if (ret)
			return ret;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* Counter subdevice (8254) */
	s = &dev->subdevices[5];
	if (board->i8254_offset) {
		dev->pacer = comedi_8254_init(dev->iobase + board->i8254_offset,
					      0, I8254_IO8, 0);
		if (!dev->pacer)
			return -ENOMEM;

		comedi_8254_subdevice_init(s, dev->pacer);
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(das08_common_attach);

static int __init das08_init(void)
{
	return 0;
}
module_init(das08_init);

static void __exit das08_exit(void)
{
}
module_exit(das08_exit);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi common DAS08 support module");
MODULE_LICENSE("GPL");

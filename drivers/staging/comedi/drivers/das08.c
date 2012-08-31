/*
 *  comedi/drivers/das08.c
 *  DAS08 driver
 *
 *  COMEDI - Linux Control and Measurement Device Interface
 *  Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 *  Copyright (C) 2001,2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 *  Copyright (C) 2004 Salvador E. Tropea <set@users.sf.net> <set@ieee.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************
 */

/*
 * Driver: das08
 * Description: DAS-08 compatible boards
 * Author: Warren Jasper, ds, Frank Hess
 * Devices: [Keithley Metrabyte] DAS08 (isa-das08),
 *   [ComputerBoards] DAS08 (isa-das08), DAS08-PGM (das08-pgm),
 *   DAS08-PGH (das08-pgh), DAS08-PGL (das08-pgl), DAS08-AOH (das08-aoh),
 *   DAS08-AOL (das08-aol), DAS08-AOM (das08-aom), DAS08/JR-AO (das08/jr-ao),
 *   DAS08/JR-16-AO (das08jr-16-ao), PCI-DAS08 (pci-das08 or das08),
 *   PC104-DAS08 (pc104-das08), DAS08/JR/16 (das08jr/16)
 * Status: works
 *
 * This is a rewrite of the das08 and das08jr drivers.
 *
 * Options (for ISA cards):
 *		[0] - base io address
 *
 * Options (for pci-das08):
 *		[0] - bus  (optional)
 *		[1] = slot (optional)
 *
 * The das08 driver doesn't support asynchronous commands, since
 * the cheap das08 hardware doesn't really support them.  The
 * comedi_rt_timer driver can be used to emulate commands for this
 * driver.
 */

#include "../comedidev.h"

#include <linux/delay.h>

#include "8255.h"
#include "8253.h"
#include "das08.h"

#define DRV_NAME "das08"

#define DO_COMEDI_DRIVER_REGISTER \
	(IS_ENABLED(CONFIG_COMEDI_DAS08_ISA) || \
	 IS_ENABLED(CONFIG_COMEDI_DAS08_PCI))

#define PCI_VENDOR_ID_COMPUTERBOARDS 0x1307
#define PCI_DEVICE_ID_PCIDAS08 0x29
#define PCIDAS08_SIZE 0x54

/* pci configuration registers */
#define INTCSR               0x4c
#define   INTR1_ENABLE         0x1
#define   INTR1_HIGH_POLARITY  0x2
#define   PCI_INTR_ENABLE      0x40
#define   INTR1_EDGE_TRIG      0x100	/*  requires high polarity */
#define CNTRL                0x50
#define   CNTRL_DIR            0x2
#define   CNTRL_INTR           0x4

/*
    cio-das08.pdf

  "isa-das08"

  0	a/d bits 0-3		start 8 bit
  1	a/d bits 4-11		start 12 bit
  2	eoc, ip1-3, irq, mux	op1-4, inte, mux
  3	unused			unused
  4567	8254
  89ab	8255

  requires hard-wiring for async ai

*/

#define DAS08_LSB		0
#define DAS08_MSB		1
#define DAS08_TRIG_12BIT	1
#define DAS08_STATUS		2
#define   DAS08_EOC			(1<<7)
#define   DAS08_IRQ			(1<<3)
#define   DAS08_IP(x)			(((x)>>4)&0x7)
#define DAS08_CONTROL		2
#define   DAS08_MUX_MASK	0x7
#define   DAS08_MUX(x)		((x) & DAS08_MUX_MASK)
#define   DAS08_INTE			(1<<3)
#define   DAS08_DO_MASK		0xf0
#define   DAS08_OP(x)		(((x) << 4) & DAS08_DO_MASK)

/*
    cio-das08jr.pdf

  "das08/jr-ao"

  0	a/d bits 0-3		unused
  1	a/d bits 4-11		start 12 bit
  2	eoc, mux		mux
  3	di			do
  4	unused			ao0_lsb
  5	unused			ao0_msb
  6	unused			ao1_lsb
  7	unused			ao1_msb

*/

#define DAS08JR_DIO		3
#define DAS08JR_AO_LSB(x)	((x) ? 6 : 4)
#define DAS08JR_AO_MSB(x)	((x) ? 7 : 5)

/*
    cio-das08_aox.pdf

  "das08-aoh"
  "das08-aol"
  "das08-aom"

  0	a/d bits 0-3		start 8 bit
  1	a/d bits 4-11		start 12 bit
  2	eoc, ip1-3, irq, mux	op1-4, inte, mux
  3	mux, gain status	gain control
  4567	8254
  8	unused			ao0_lsb
  9	unused			ao0_msb
  a	unused			ao1_lsb
  b	unused			ao1_msb
  89ab
  cdef	8255
*/

#define DAS08AO_GAIN_CONTROL	3
#define DAS08AO_GAIN_STATUS	3

#define DAS08AO_AO_LSB(x)	((x) ? 0xa : 8)
#define DAS08AO_AO_MSB(x)	((x) ? 0xb : 9)
#define DAS08AO_AO_UPDATE	8

/* gainlist same as _pgx_ below */

static const struct comedi_lrange range_das08_pgl = { 9, {
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

static const struct comedi_lrange range_das08_pgh = { 12, {
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
							   UNI_RANGE(0.01),
							   }
};

static const struct comedi_lrange range_das08_pgm = { 9, {
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
};				/*
				   cio-das08jr.pdf

				   "das08/jr-ao"

				   0 a/d bits 0-3            unused
				   1 a/d bits 4-11           start 12 bit
				   2 eoc, mux                mux
				   3 di                      do
				   4 unused                  ao0_lsb
				   5 unused                  ao0_msb
				   6 unused                  ao1_lsb
				   7 unused                  ao1_msb

				 */

static const struct comedi_lrange *const das08_ai_lranges[] = {
	&range_unknown,
	&range_bipolar5,
	&range_das08_pgh,
	&range_das08_pgl,
	&range_das08_pgm,
};

static const int das08_pgh_gainlist[] = {
	8, 0, 10, 2, 12, 4, 14, 6, 1, 3, 5, 7
};
static const int das08_pgl_gainlist[] = { 8, 0, 2, 4, 6, 1, 3, 5, 7 };
static const int das08_pgm_gainlist[] = { 8, 0, 10, 12, 14, 9, 11, 13, 15 };

static const int *const das08_gainlists[] = {
	NULL,
	NULL,
	das08_pgh_gainlist,
	das08_pgl_gainlist,
	das08_pgm_gainlist,
};

#define TIMEOUT 100000

static int das08_ai_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	const struct das08_board_struct *thisboard = comedi_board(dev);
	struct das08_private_struct *devpriv = dev->private;
	int i, n;
	int chan;
	int range;
	int lsb, msb;

	chan = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);

	/* clear crap */
	inb(dev->iobase + DAS08_LSB);
	inb(dev->iobase + DAS08_MSB);

	/* set multiplexer */
	/*  lock to prevent race with digital output */
	spin_lock(&dev->spinlock);
	devpriv->do_mux_bits &= ~DAS08_MUX_MASK;
	devpriv->do_mux_bits |= DAS08_MUX(chan);
	outb(devpriv->do_mux_bits, dev->iobase + DAS08_CONTROL);
	spin_unlock(&dev->spinlock);

	if (s->range_table->length > 1) {
		/* set gain/range */
		range = CR_RANGE(insn->chanspec);
		outb(devpriv->pg_gainlist[range],
		     dev->iobase + DAS08AO_GAIN_CONTROL);
	}

	for (n = 0; n < insn->n; n++) {
		/* clear over-range bits for 16-bit boards */
		if (thisboard->ai_nbits == 16)
			if (inb(dev->iobase + DAS08_MSB) & 0x80)
				dev_info(dev->class_dev, "over-range\n");

		/* trigger conversion */
		outb_p(0, dev->iobase + DAS08_TRIG_12BIT);

		for (i = 0; i < TIMEOUT; i++) {
			if (!(inb(dev->iobase + DAS08_STATUS) & DAS08_EOC))
				break;
		}
		if (i == TIMEOUT) {
			dev_err(dev->class_dev, "timeout\n");
			return -ETIME;
		}
		msb = inb(dev->iobase + DAS08_MSB);
		lsb = inb(dev->iobase + DAS08_LSB);
		if (thisboard->ai_encoding == das08_encode12) {
			data[n] = (lsb >> 4) | (msb << 4);
		} else if (thisboard->ai_encoding == das08_pcm_encode12) {
			data[n] = (msb << 8) + lsb;
		} else if (thisboard->ai_encoding == das08_encode16) {
			/* FPOS 16-bit boards are sign-magnitude */
			if (msb & 0x80)
				data[n] = (1 << 15) | lsb | ((msb & 0x7f) << 8);
			else
				data[n] = (1 << 15) - (lsb | (msb & 0x7f) << 8);
		} else {
			comedi_error(dev, "bug! unknown ai encoding");
			return -1;
		}
	}

	return n;
}

static int das08_di_rbits(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	data[0] = 0;
	data[1] = DAS08_IP(inb(dev->iobase + DAS08_STATUS));

	return insn->n;
}

static int das08_do_wbits(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	struct das08_private_struct *devpriv = dev->private;
	int wbits;

	/*  get current settings of digital output lines */
	wbits = (devpriv->do_mux_bits >> 4) & 0xf;
	/*  null bits we are going to set */
	wbits &= ~data[0];
	/*  set new bit values */
	wbits |= data[0] & data[1];
	/*  remember digital output bits */
	/*  prevent race with setting of analog input mux */
	spin_lock(&dev->spinlock);
	devpriv->do_mux_bits &= ~DAS08_DO_MASK;
	devpriv->do_mux_bits |= DAS08_OP(wbits);
	outb(devpriv->do_mux_bits, dev->iobase + DAS08_CONTROL);
	spin_unlock(&dev->spinlock);

	data[1] = wbits;

	return insn->n;
}

static int __maybe_unused
das08jr_di_rbits(struct comedi_device *dev, struct comedi_subdevice *s,
		 struct comedi_insn *insn, unsigned int *data)
{
	data[0] = 0;
	data[1] = inb(dev->iobase + DAS08JR_DIO);

	return insn->n;
}

static int __maybe_unused
das08jr_do_wbits(struct comedi_device *dev, struct comedi_subdevice *s,
		 struct comedi_insn *insn, unsigned int *data)
{
	struct das08_private_struct *devpriv = dev->private;

	/*  null bits we are going to set */
	devpriv->do_bits &= ~data[0];
	/*  set new bit values */
	devpriv->do_bits |= data[0] & data[1];
	outb(devpriv->do_bits, dev->iobase + DAS08JR_DIO);

	data[1] = devpriv->do_bits;

	return insn->n;
}

static int __maybe_unused
das08jr_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
		 struct comedi_insn *insn, unsigned int *data)
{
	int n;
	int lsb, msb;
	int chan;

	lsb = data[0] & 0xff;
	msb = (data[0] >> 8) & 0xff;

	chan = CR_CHAN(insn->chanspec);

	for (n = 0; n < insn->n; n++) {
#if 0
		outb(lsb, dev->iobase + devpriv->ao_offset_lsb[chan]);
		outb(msb, dev->iobase + devpriv->ao_offset_msb[chan]);
#else
		outb(lsb, dev->iobase + DAS08JR_AO_LSB(chan));
		outb(msb, dev->iobase + DAS08JR_AO_MSB(chan));
#endif

		/* load DACs */
		inb(dev->iobase + DAS08JR_DIO);
	}

	return n;
}

/*
 *
 * The -aox boards have the DACs at a different offset and use
 * a different method to force an update.
 *
 */
static int __maybe_unused
das08ao_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
		 struct comedi_insn *insn, unsigned int *data)
{
	int n;
	int lsb, msb;
	int chan;

	lsb = data[0] & 0xff;
	msb = (data[0] >> 8) & 0xf;

	chan = CR_CHAN(insn->chanspec);

	for (n = 0; n < insn->n; n++) {
#if 0
		outb(lsb, dev->iobase + devpriv->ao_offset_lsb[chan]);
		outb(msb, dev->iobase + devpriv->ao_offset_msb[chan]);
#else
		outb(lsb, dev->iobase + DAS08AO_AO_LSB(chan));
		outb(msb, dev->iobase + DAS08AO_AO_MSB(chan));
#endif

		/* load DACs */
		inb(dev->iobase + DAS08AO_AO_UPDATE);
	}

	return n;
}

static void i8254_initialize(struct comedi_device *dev)
{
	struct das08_private_struct *devpriv = dev->private;
	unsigned int mode = I8254_MODE0 | I8254_BINARY;
	int i;

	for (i = 0; i < 3; ++i)
		i8254_set_mode(devpriv->i8254_iobase, 0, i, mode);
}

static int das08_counter_read(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	struct das08_private_struct *devpriv = dev->private;
	int chan = insn->chanspec;

	data[0] = i8254_read(devpriv->i8254_iobase, 0, chan);
	return 1;
}

static int das08_counter_write(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	struct das08_private_struct *devpriv = dev->private;
	int chan = insn->chanspec;

	i8254_write(devpriv->i8254_iobase, 0, chan, data[0]);
	return 1;
}

static int das08_counter_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	struct das08_private_struct *devpriv = dev->private;
	int chan = insn->chanspec;

	if (insn->n != 2)
		return -EINVAL;

	switch (data[0]) {
	case INSN_CONFIG_SET_COUNTER_MODE:
		i8254_set_mode(devpriv->i8254_iobase, 0, chan, data[1]);
		break;
	case INSN_CONFIG_8254_READ_STATUS:
		data[1] = i8254_status(devpriv->i8254_iobase, 0, chan);
		break;
	default:
		return -EINVAL;
		break;
	}
	return 2;
}

#if DO_COMEDI_DRIVER_REGISTER
static const struct das08_board_struct das08_boards[] = {
#if IS_ENABLED(CONFIG_COMEDI_DAS08_ISA)
	{
		.name = "isa-das08",	/*  cio-das08.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pg_none,
		.ai_encoding = das08_encode12,
		.ao = NULL,
		.ao_nbits = 12,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 8,
		.i8254_offset = 4,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08-pgm",	/*  cio-das08pgx.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pgm,
		.ai_encoding = das08_encode12,
		.ao = NULL,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0,
		.i8254_offset = 0x04,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08-pgh",	/*  cio-das08pgx.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pgh,
		.ai_encoding = das08_encode12,
		.ao = NULL,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0,
		.i8254_offset = 0x04,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08-pgl",	/*  cio-das08pgx.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pgl,
		.ai_encoding = das08_encode12,
		.ao = NULL,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0,
		.i8254_offset = 0x04,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08-aoh",	/*  cio-das08_aox.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pgh,
		.ai_encoding = das08_encode12,
		.ao = das08ao_ao_winsn,	/*  8 */
		.ao_nbits = 12,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0x0c,
		.i8254_offset = 0x04,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08-aol",	/*  cio-das08_aox.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pgl,
		.ai_encoding = das08_encode12,
		.ao = das08ao_ao_winsn,	/*  8 */
		.ao_nbits = 12,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0x0c,
		.i8254_offset = 0x04,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08-aom",	/*  cio-das08_aox.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pgm,
		.ai_encoding = das08_encode12,
		.ao = das08ao_ao_winsn,	/*  8 */
		.ao_nbits = 12,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0x0c,
		.i8254_offset = 0x04,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08/jr-ao",	/*  cio-das08-jr-ao.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pg_none,
		.ai_encoding = das08_encode12,
		.ao = das08jr_ao_winsn,
		.ao_nbits = 12,
		.di = das08jr_di_rbits,
		.di_nchan = 8,
		.do_ = das08jr_do_wbits,
		.do_nchan = 8,
		.i8255_offset = 0,
		.i8254_offset = 0,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "das08jr-16-ao",	/*  cio-das08jr-16-ao.pdf */
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 16,
		.ai_pg = das08_pg_none,
		.ai_encoding = das08_encode16,
		.ao = das08jr_ao_winsn,
		.ao_nbits = 16,
		.di = das08jr_di_rbits,
		.di_nchan = 8,
		.do_ = das08jr_do_wbits,
		.do_nchan = 8,
		.i8255_offset = 0,
		.i8254_offset = 0x04,
		.iosize = 16,		/*  unchecked */
	},
	{
		.name = "pc104-das08",
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_pg_none,
		.ai_encoding = das08_encode12,
		.ao = NULL,
		.ao_nbits = 0,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0,
		.i8254_offset = 4,
		.iosize = 16,		/*  unchecked */
	},
#if 0
	{
		.name = "das08/f",
	},
	{
		.name = "das08jr",
	},
#endif
	{
		.name = "das08jr/16",
		.bustype = isa,
		.ai = das08_ai_rinsn,
		.ai_nbits = 16,
		.ai_pg = das08_pg_none,
		.ai_encoding = das08_encode16,
		.ao = NULL,
		.ao_nbits = 0,
		.di = das08jr_di_rbits,
		.di_nchan = 8,
		.do_ = das08jr_do_wbits,
		.do_nchan = 8,
		.i8255_offset = 0,
		.i8254_offset = 0,
		.iosize = 16,		/*  unchecked */
	},
#if 0
	{
		.name = "das48-pga",	/*  cio-das48-pga.pdf */
	},
	{
		.name = "das08-pga-g2",	/*  a KM board */
	},
#endif
#endif /* IS_ENABLED(CONFIG_COMEDI_DAS08_ISA) */
#if IS_ENABLED(CONFIG_COMEDI_DAS08_PCI)
	{
		.name = "pci-das08",	/*  pci-das08 */
		.id = PCI_DEVICE_ID_PCIDAS08,
		.bustype = pci,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_bipolar5,
		.ai_encoding = das08_encode12,
		.ao = NULL,
		.ao_nbits = 0,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 4,
		.i8255_offset = 0,
		.i8254_offset = 4,
		.iosize = 8,
	},
	{ /* wildcard entry matches any supported PCI device */
		.name = DRV_NAME,
		.id = PCI_ANY_ID,
		.bustype = pci,
	},
#endif /* IS_ENABLED(CONFIG_COMEDI_DAS08_PCI) */
};
#endif /* DO_COMEDI_DRIVER_REGISTER */

#if IS_ENABLED(CONFIG_COMEDI_DAS08_CS)
struct das08_board_struct das08_cs_boards[NUM_DAS08_CS_BOARDS] = {
	{
		.name = "pcm-das08",
		.id = 0x0,		/*  XXX */
		.bustype = pcmcia,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_bipolar5,
		.ai_encoding = das08_pcm_encode12,
		.ao = NULL,
		.ao_nbits = 0,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 3,
		.i8255_offset = 0,
		.i8254_offset = 0,
		.iosize = 16,
	},
	/*  duplicate so driver name can be used also */
	{
		.name = "das08_cs",
		.id = 0x0,		/*  XXX */
		.bustype = pcmcia,
		.ai = das08_ai_rinsn,
		.ai_nbits = 12,
		.ai_pg = das08_bipolar5,
		.ai_encoding = das08_pcm_encode12,
		.ao = NULL,
		.ao_nbits = 0,
		.di = das08_di_rbits,
		.di_nchan = 3,
		.do_ = das08_do_wbits,
		.do_nchan = 3,
		.i8255_offset = 0,
		.i8254_offset = 0,
		.iosize = 16,
	},
};
EXPORT_SYMBOL_GPL(das08_cs_boards);
#endif

int das08_common_attach(struct comedi_device *dev, unsigned long iobase)
{
	const struct das08_board_struct *thisboard = comedi_board(dev);
	struct das08_private_struct *devpriv = dev->private;
	struct comedi_subdevice *s;
	int ret;

	dev->iobase = iobase;

	dev->board_name = thisboard->name;

	ret = comedi_alloc_subdevices(dev, 6);
	if (ret)
		return ret;

	s = dev->subdevices + 0;
	/* ai */
	if (thisboard->ai) {
		s->type = COMEDI_SUBD_AI;
		/* XXX some boards actually have differential
		 * inputs instead of single ended.
		 * The driver does nothing with arefs though,
		 * so it's no big deal.
		 */
		s->subdev_flags = SDF_READABLE | SDF_GROUND;
		s->n_chan = 8;
		s->maxdata = (1 << thisboard->ai_nbits) - 1;
		s->range_table = das08_ai_lranges[thisboard->ai_pg];
		s->insn_read = thisboard->ai;
		devpriv->pg_gainlist = das08_gainlists[thisboard->ai_pg];
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = dev->subdevices + 1;
	/* ao */
	if (thisboard->ao) {
		s->type = COMEDI_SUBD_AO;
/* XXX lacks read-back insn */
		s->subdev_flags = SDF_WRITABLE;
		s->n_chan = 2;
		s->maxdata = (1 << thisboard->ao_nbits) - 1;
		s->range_table = &range_bipolar5;
		s->insn_write = thisboard->ao;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = dev->subdevices + 2;
	/* di */
	if (thisboard->di) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE;
		s->n_chan = thisboard->di_nchan;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = thisboard->di;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = dev->subdevices + 3;
	/* do */
	if (thisboard->do_) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
		s->n_chan = thisboard->do_nchan;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = thisboard->do_;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = dev->subdevices + 4;
	/* 8255 */
	if (thisboard->i8255_offset != 0) {
		subdev_8255_init(dev, s, NULL, (unsigned long)(dev->iobase +
							       thisboard->
							       i8255_offset));
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = dev->subdevices + 5;
	/* 8254 */
	if (thisboard->i8254_offset != 0) {
		s->type = COMEDI_SUBD_COUNTER;
		s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
		s->n_chan = 3;
		s->maxdata = 0xFFFF;
		s->insn_read = das08_counter_read;
		s->insn_write = das08_counter_write;
		s->insn_config = das08_counter_config;

		devpriv->i8254_iobase = iobase + thisboard->i8254_offset;
		i8254_initialize(dev);
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(das08_common_attach);

static int das08_pci_attach_common(struct comedi_device *dev,
				   struct pci_dev *pdev)
{
	unsigned long iobase;
	unsigned long pci_iobase;
	struct das08_private_struct *devpriv = dev->private;

	if (!IS_ENABLED(CONFIG_COMEDI_DAS08_PCI))
		return -EINVAL;

	devpriv->pdev = pdev;
	/*  enable PCI device and reserve I/O spaces */
	if (comedi_pci_enable(pdev, dev->driver->driver_name)) {
		dev_err(dev->class_dev,
			"Error enabling PCI device and requesting regions\n");
		return -EIO;
	}
	/*  read base addresses */
	pci_iobase = pci_resource_start(pdev, 1);
	iobase = pci_resource_start(pdev, 2);
	dev_info(dev->class_dev, "pcibase 0x%lx  iobase 0x%lx\n",
		 pci_iobase, iobase);
	devpriv->pci_iobase = pci_iobase;
#if 0
	/* We could enable pci-das08's interrupt here to make it possible
	* to do timed input in this driver, but there is little point since
	* conversions would have to be started by the interrupt handler
	* so you might as well use comedi_rt_timer to emulate commands
	*/
	/* set source of interrupt trigger to counter2 output */
	outb(CNTRL_INTR | CNTRL_DIR, pci_iobase + CNTRL);
	/* Enable local interrupt 1 and pci interrupt */
	outw(INTR1_ENABLE | PCI_INTR_ENABLE, pci_iobase + INTCSR);
#endif
	return das08_common_attach(dev, iobase);
}

static const struct das08_board_struct *
das08_find_pci_board(struct pci_dev *pdev)
{
#if DO_COMEDI_DRIVER_REGISTER
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(das08_boards); i++)
		if (das08_boards[i].bustype == pci &&
		    pdev->device == das08_boards[i].id)
			return &das08_boards[i];
#endif
	return NULL;
}

/* only called in the PCI probe path, via comedi_pci_auto_config() */
static int __devinit __maybe_unused
das08_attach_pci(struct comedi_device *dev, struct pci_dev *pdev)
{
	int ret;

	if (!IS_ENABLED(CONFIG_COMEDI_DAS08_PCI))
		return -EINVAL;
	ret = alloc_private(dev, sizeof(struct das08_private_struct));
	if (ret < 0)
		return ret;
	dev_info(dev->class_dev, "attach pci %s\n", pci_name(pdev));
	dev->board_ptr = das08_find_pci_board(pdev);
	if (dev->board_ptr == NULL) {
		dev_err(dev->class_dev, "BUG! cannot determine board type!\n");
		return -EINVAL;
	}
	/*
	 * Need to 'get' the PCI device to match the 'put' in das08_detach().
	 * TODO: Remove the pci_dev_get() and matching pci_dev_put() once
	 * support for manual attachment of PCI devices via das08_attach()
	 * has been removed.
	 */
	pci_dev_get(pdev);
	return das08_pci_attach_common(dev, pdev);
}

static struct pci_dev *das08_find_pci(struct comedi_device *dev,
				      int bus, int slot)
{
	const struct das08_board_struct *thisboard = comedi_board(dev);
	struct pci_dev *pdev;
	unsigned int matchid;

	if (bus || slot)
		dev_dbg(dev->class_dev, "Looking for %s at PCI %02X:%02X\n",
			thisboard->name, bus, slot);
	else
		dev_dbg(dev->class_dev, "Looking for %s on PCI buses\n",
			thisboard->name);

	matchid = thisboard->id;
	pdev = NULL;
	for_each_pci_dev(pdev) {
		if ((bus || slot) &&
		    (bus != pdev->bus->number || slot != PCI_SLOT(pdev->devfn)))
			continue;
		if (pdev->vendor != PCI_VENDOR_ID_COMPUTERBOARDS)
			continue;
		if (matchid == PCI_ANY_ID) {
			/* wildcard board matches any supported PCI board */
			const struct das08_board_struct *foundboard;
			foundboard = das08_find_pci_board(pdev);
			if (foundboard == NULL)
				continue;
			/* replace wildcard board_ptr */
			dev->board_ptr = thisboard = foundboard;
		} else {
			/* match specific PCI board */
			if (pdev->device != matchid)
				continue;
		}
		/* found a match */
		dev_info(dev->class_dev, "Found %s at PCI %s\n",
			 thisboard->name, pci_name(pdev));
		return pdev;
	}
	/* no match found */
	if (bus || slot)
		dev_err(dev->class_dev,
			"No %s cards found at PCI %02X:%02X\n",
			thisboard->name, bus, slot);
	else
		dev_err(dev->class_dev, "No %s cards found on PCI buses\n",
			thisboard->name);
	return NULL;
}

static int __maybe_unused
das08_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct das08_board_struct *thisboard = comedi_board(dev);
	struct das08_private_struct *devpriv;
	int ret;
	unsigned long iobase;

	ret = alloc_private(dev, sizeof(struct das08_private_struct));
	if (ret < 0)
		return ret;
	devpriv = dev->private;

	dev_info(dev->class_dev, "attach\n");
	if (IS_ENABLED(CONFIG_COMEDI_DAS08_PCI) && thisboard->bustype == pci) {
		struct pci_dev *pdev;
		pdev = das08_find_pci(dev, it->options[0], it->options[1]);
		if (pdev == NULL)
			return -EIO;
		return das08_pci_attach_common(dev, pdev);
	} else if (IS_ENABLED(CONFIG_COMEDI_DAS08_ISA) &&
		   thisboard->bustype == isa) {
		iobase = it->options[0];
		dev_info(dev->class_dev, "iobase 0x%lx\n", iobase);
		if (!request_region(iobase, thisboard->iosize, DRV_NAME)) {
			dev_err(dev->class_dev, "I/O port conflict\n");
			return -EIO;
		}
		return das08_common_attach(dev, iobase);
	} else
		return -EIO;
}

void das08_common_detach(struct comedi_device *dev)
{
	if (dev->subdevices)
		subdev_8255_cleanup(dev, dev->subdevices + 4);
}
EXPORT_SYMBOL_GPL(das08_common_detach);

static void __maybe_unused das08_detach(struct comedi_device *dev)
{
	const struct das08_board_struct *thisboard = comedi_board(dev);
	struct das08_private_struct *devpriv = dev->private;

	das08_common_detach(dev);
	if (IS_ENABLED(CONFIG_COMEDI_DAS08_ISA) && thisboard->bustype == isa) {
		if (dev->iobase)
			release_region(dev->iobase, thisboard->iosize);
	} else if (IS_ENABLED(CONFIG_COMEDI_DAS08_PCI) &&
		   thisboard->bustype == pci) {
		if (devpriv && devpriv->pdev) {
			if (devpriv->pci_iobase)
				comedi_pci_disable(devpriv->pdev);
			pci_dev_put(devpriv->pdev);
		}
	}
}

#if DO_COMEDI_DRIVER_REGISTER
static struct comedi_driver das08_driver = {
	.driver_name = DRV_NAME,
	.module = THIS_MODULE,
	.attach = das08_attach,
	.attach_pci = das08_attach_pci,
	.detach = das08_detach,
	.board_name = &das08_boards[0].name,
	.num_names = sizeof(das08_boards) / sizeof(struct das08_board_struct),
	.offset = sizeof(struct das08_board_struct),
};
#endif

#if IS_ENABLED(CONFIG_COMEDI_DAS08_PCI)
static DEFINE_PCI_DEVICE_TABLE(das08_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_COMPUTERBOARDS, PCI_DEVICE_ID_PCIDAS08) },
	{0}
};

MODULE_DEVICE_TABLE(pci, das08_pci_table);

static int __devinit das08_pci_probe(struct pci_dev *dev,
					    const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &das08_driver);
}

static void __devexit das08_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver das08_pci_driver = {
	.id_table = das08_pci_table,
	.name =  DRV_NAME,
	.probe = &das08_pci_probe,
	.remove = __devexit_p(&das08_pci_remove)
};
#endif /* CONFIG_COMEDI_DAS08_PCI */

#if DO_COMEDI_DRIVER_REGISTER
#if IS_ENABLED(CONFIG_COMEDI_DAS08_PCI)
module_comedi_pci_driver(das08_driver, das08_pci_driver);
#else
module_comedi_driver(das08_driver);
#endif
#else /* DO_COMEDI_DRIVER_REGISTER */
static int __init das08_init(void)
{
	return 0;
}

static void __exit das08_exit(void)
{
}

module_init(das08_init);
module_exit(das08_exit);
#endif /* DO_COMEDI_DRIVER_REGISTER */

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

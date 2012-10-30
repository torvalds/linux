/*
    comedi/drivers/ni_at_ao.c
    Driver for NI AT-AO-6/10 boards

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000,2002 David A. Schleef <ds@schleef.org>

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
Driver: ni_at_ao
Description: National Instruments AT-AO-6/10
Devices: [National Instruments] AT-AO-6 (at-ao-6), AT-AO-10 (at-ao-10)
Status: should work
Author: ds
Updated: Sun Dec 26 12:26:28 EST 2004

Configuration options:
  [0] - I/O port base address
  [1] - IRQ (unused)
  [2] - DMA (unused)
  [3] - analog output range, set by jumpers on hardware (0 for -10 to 10V
	bipolar, 1 for 0V to 10V unipolar)

*/
/*
 * Register-level programming information can be found in NI
 * document 320379.pdf.
 */

#include "../comedidev.h"

#include <linux/ioport.h>

/* board egisters */
/* registers with _2_ are accessed when GRP2WR is set in CFG1 */

#define ATAO_SIZE 0x20

#define ATAO_2_DMATCCLR		0x00	/* W 16 */
#define ATAO_DIN		0x00	/* R 16 */
#define ATAO_DOUT		0x00	/* W 16 */

#define ATAO_CFG2		0x02	/* W 16 */
#define CALLD1	0x8000
#define CALLD0	0x4000
#define FFRTEN	0x2000
#define DAC2S8	0x1000
#define DAC2S6	0x0800
#define DAC2S4	0x0400
#define DAC2S2	0x0200
#define DAC2S0	0x0100
#define LDAC8		0x0080
#define LDAC6		0x0040
#define LDAC4		0x0020
#define LDAC2		0x0010
#define LDAC0		0x0008
#define PROMEN	0x0004
#define SCLK		0x0002
#define SDATA		0x0001

#define ATAO_2_INT1CLR		0x02	/* W 16 */

#define ATAO_CFG3		0x04	/* W 16 */
#define DMAMODE	0x0040
#define CLKOUT	0x0020
#define RCLKEN	0x0010
#define DOUTEN2	0x0008
#define DOUTEN1	0x0004
#define EN2_5V	0x0002
#define SCANEN	0x0001

#define ATAO_2_INT2CLR		0x04	/* W 16 */

#define ATAO_82C53_BASE		0x06	/* RW 8 */

#define ATAO_82C53_CNTR1	0x06	/* RW 8 */
#define ATAO_82C53_CNTR2	0x07	/* RW 8 */
#define ATAO_82C53_CNTR3	0x08	/* RW 8 */
#define ATAO_82C53_CNTRCMD	0x09	/* W 8 */
#define CNTRSEL1	0x80
#define CNTRSEL0	0x40
#define RWSEL1	0x20
#define RWSEL0	0x10
#define MODESEL2	0x08
#define MODESEL1	0x04
#define MODESEL0	0x02
#define BCDSEL	0x01
  /* read-back command */
#define COUNT		0x20
#define STATUS	0x10
#define CNTR3		0x08
#define CNTR2		0x04
#define CNTR1		0x02
  /* status */
#define OUT		0x80
#define _NULL		0x40
#define RW1		0x20
#define RW0		0x10
#define MODE2		0x08
#define MODE1		0x04
#define MODE0		0x02
#define BCD		0x01

#define ATAO_2_RTSISHFT		0x06	/* W 8 */
#define RSI		0x01

#define ATAO_2_RTSISTRB		0x07	/* W 8 */

#define ATAO_CFG1		0x0a	/* W 16 */
#define EXTINT2EN	0x8000
#define EXTINT1EN	0x4000
#define CNTINT2EN	0x2000
#define CNTINT1EN	0x1000
#define TCINTEN	0x0800
#define CNT1SRC	0x0400
#define CNT2SRC	0x0200
#define FIFOEN	0x0100
#define GRP2WR	0x0080
#define EXTUPDEN	0x0040
#define DMARQ		0x0020
#define DMAEN		0x0010
#define CH_mask	0x000f
#define ATAO_STATUS		0x0a	/* R 16 */
#define FH		0x0040
#define FE		0x0020
#define FF		0x0010
#define INT2		0x0008
#define INT1		0x0004
#define TCINT		0x0002
#define PROMOUT	0x0001

#define ATAO_FIFO_WRITE		0x0c	/* W 16 */
#define ATAO_FIFO_CLEAR		0x0c	/* R 16 */
#define ATAO_DACn(x)		(0x0c + 2*(x))	/* W */

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
struct atao_board {
	const char *name;
	int n_ao_chans;
};

struct atao_private {

	unsigned short cfg1;
	unsigned short cfg2;
	unsigned short cfg3;

	/* Used for AO readback */
	unsigned int ao_readback[10];
};

#define devpriv ((struct atao_private *)dev->private)

static void atao_reset(struct comedi_device *dev)
{
	/* This is the reset sequence described in the manual */

	devpriv->cfg1 = 0;
	outw(devpriv->cfg1, dev->iobase + ATAO_CFG1);

	outb(RWSEL0 | MODESEL2, dev->iobase + ATAO_82C53_CNTRCMD);
	outb(0x03, dev->iobase + ATAO_82C53_CNTR1);
	outb(CNTRSEL0 | RWSEL0 | MODESEL2, dev->iobase + ATAO_82C53_CNTRCMD);

	devpriv->cfg2 = 0;
	outw(devpriv->cfg2, dev->iobase + ATAO_CFG2);

	devpriv->cfg3 = 0;
	outw(devpriv->cfg3, dev->iobase + ATAO_CFG3);

	inw(dev->iobase + ATAO_FIFO_CLEAR);

	devpriv->cfg1 |= GRP2WR;
	outw(devpriv->cfg1, dev->iobase + ATAO_CFG1);

	outw(0, dev->iobase + ATAO_2_INT1CLR);
	outw(0, dev->iobase + ATAO_2_INT2CLR);
	outw(0, dev->iobase + ATAO_2_DMATCCLR);

	devpriv->cfg1 &= ~GRP2WR;
	outw(devpriv->cfg1, dev->iobase + ATAO_CFG1);
}

static int atao_ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	short bits;

	for (i = 0; i < insn->n; i++) {
		bits = data[i] - 0x800;
		if (chan == 0) {
			devpriv->cfg1 |= GRP2WR;
			outw(devpriv->cfg1, dev->iobase + ATAO_CFG1);
		}
		outw(bits, dev->iobase + ATAO_DACn(chan));
		if (chan == 0) {
			devpriv->cfg1 &= ~GRP2WR;
			outw(devpriv->cfg1, dev->iobase + ATAO_CFG1);
		}
		devpriv->ao_readback[chan] = data[i];
	}

	return i;
}

static int atao_ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int atao_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		outw(s->state, dev->iobase + ATAO_DOUT);
	}

	data[1] = inw(dev->iobase + ATAO_DIN);

	return insn->n;
}

static int atao_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int chan = CR_CHAN(insn->chanspec);
	unsigned int mask, bit;

	/* The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value COMEDI_INPUT or COMEDI_OUTPUT. */

	mask = (chan < 4) ? 0x0f : 0xf0;
	bit = (chan < 4) ? DOUTEN1 : DOUTEN2;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= mask;
		devpriv->cfg3 |= bit;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~mask;
		devpriv->cfg3 &= ~bit;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
		break;
	}

	outw(devpriv->cfg3, dev->iobase + ATAO_CFG3);

	return 1;
}

/*
 * Figure 2-1 in the manual shows 3 chips labeled DAC8800, which
 * are 8-channel 8-bit DACs.  These are most likely the calibration
 * DACs.  It is not explicitly stated in the manual how to access
 * the caldacs, but we can guess.
 */
static int atao_calib_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int i;
	for (i = 0; i < insn->n; i++)
		data[i] = 0;	/* XXX */
	return insn->n;
}

static int atao_calib_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	unsigned int bitstring, bit;
	unsigned int chan = CR_CHAN(insn->chanspec);

	bitstring = ((chan & 0x7) << 8) | (data[insn->n - 1] & 0xff);

	for (bit = 1 << (11 - 1); bit; bit >>= 1) {
		outw(devpriv->cfg2 | ((bit & bitstring) ? SDATA : 0),
		     dev->iobase + ATAO_CFG2);
		outw(devpriv->cfg2 | SCLK | ((bit & bitstring) ? SDATA : 0),
		     dev->iobase + ATAO_CFG2);
	}
	/* strobe the appropriate caldac */
	outw(devpriv->cfg2 | (((chan >> 3) + 1) << 14),
	     dev->iobase + ATAO_CFG2);
	outw(devpriv->cfg2, dev->iobase + ATAO_CFG2);

	return insn->n;
}

static int atao_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct atao_board *board = comedi_board(dev);
	struct comedi_subdevice *s;
	unsigned long iobase;
	int ao_unipolar;
	int ret;

	iobase = it->options[0];
	if (iobase == 0)
		iobase = 0x1c0;
	ao_unipolar = it->options[3];

	printk(KERN_INFO "comedi%d: ni_at_ao: 0x%04lx", dev->minor, iobase);

	if (!request_region(iobase, ATAO_SIZE, "ni_at_ao")) {
		printk(" I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

	dev->board_name = board->name;

	if (alloc_private(dev, sizeof(struct atao_private)) < 0)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* analog output subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = board->n_ao_chans;
	s->maxdata = (1 << 12) - 1;
	if (ao_unipolar)
		s->range_table = &range_unipolar10;
	else
		s->range_table = &range_bipolar10;
	s->insn_write = &atao_ao_winsn;
	s->insn_read = &atao_ao_rinsn;

	s = &dev->subdevices[1];
	/* digital i/o subdevice */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = atao_dio_insn_bits;
	s->insn_config = atao_dio_insn_config;

	s = &dev->subdevices[2];
	/* caldac subdevice */
	s->type = COMEDI_SUBD_CALIB;
	s->subdev_flags = SDF_WRITABLE | SDF_INTERNAL;
	s->n_chan = 21;
	s->maxdata = 0xff;
	s->insn_read = atao_calib_insn_read;
	s->insn_write = atao_calib_insn_write;

	s = &dev->subdevices[3];
	/* eeprom subdevice */
	/* s->type=COMEDI_SUBD_EEPROM; */
	s->type = COMEDI_SUBD_UNUSED;

	atao_reset(dev);

	printk(KERN_INFO "\n");

	return 0;
}

static void atao_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		release_region(dev->iobase, ATAO_SIZE);
}

static const struct atao_board atao_boards[] = {
	{
		.name		= "ai-ao-6",
		.n_ao_chans	= 6,
	}, {
		.name		= "ai-ao-10",
		.n_ao_chans	= 10,
	},
};

static struct comedi_driver ni_at_ao_driver = {
	.driver_name	= "ni_at_ao",
	.module		= THIS_MODULE,
	.attach		= atao_attach,
	.detach		= atao_detach,
	.board_name	= &atao_boards[0].name,
	.offset		= sizeof(struct atao_board),
	.num_names	= ARRAY_SIZE(atao_boards),
};
module_comedi_driver(ni_at_ao_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

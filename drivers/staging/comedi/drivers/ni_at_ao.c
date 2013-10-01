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

#include <linux/module.h>
#include "../comedidev.h"

/*
 * Register map
 */
#define ATAO_DIN		0x00	/* R 16 */
#define ATAO_DOUT		0x00	/* W 16 */
#define ATAO_CFG2		0x02	/* W 16 */
#define CALLD1			(1 << 15)
#define CALLD0			(1 << 14)
#define FFRTEN			(1 << 13)
#define DAC2S8			(1 << 12)
#define DAC2S6			(1 << 11)
#define DAC2S4			(1 << 10)
#define DAC2S2			(1 << 9)
#define DAC2S0			(1 << 8)
#define LDAC8			(1 << 7)
#define LDAC6			(1 << 6)
#define LDAC4			(1 << 5)
#define LDAC2			(1 << 4)
#define LDAC0			(1 << 3)
#define PROMEN			(1 << 2)
#define SCLK			(1 << 1)
#define SDATA			(1 << 0)
#define ATAO_CFG3		0x04	/* W 16 */
#define DMAMODE			(1 << 6)
#define CLKOUT			(1 << 5)
#define RCLKEN			(1 << 4)
#define DOUTEN2			(1 << 3)
#define DOUTEN1			(1 << 2)
#define EN2_5V			(1 << 1)
#define SCANEN			(1 << 0)
#define ATAO_82C53_BASE		0x06	/* RW 8 */
#define ATAO_82C53_CNTR1	0x06	/* RW 8 */
#define ATAO_82C53_CNTR2	0x07	/* RW 8 */
#define ATAO_82C53_CNTR3	0x08	/* RW 8 */
#define ATAO_82C53_CNTRCMD	0x09	/* W 8 */
#define CNTRSEL1		(1 << 7)
#define CNTRSEL0		(1 << 6)
#define RWSEL1			(1 << 5)
#define RWSEL0			(1 << 4)
#define MODESEL2		(1 << 3)
#define MODESEL1		(1 << 2)
#define MODESEL0		(1 << 1)
#define BCDSEL			(1 << 0)
  /* read-back command */
#define COUNT			(1 << 5)
#define STATUS			(1 << 4)
#define CNTR3			(1 << 3)
#define CNTR2			(1 << 2)
#define CNTR1			(1 << 1)
  /* status */
#define OUT			(1 << 7)
#define _NULL			(1 << 6)
#define RW1			(1 << 5)
#define RW0			(1 << 4)
#define MODE2			(1 << 3)
#define MODE1			(1 << 2)
#define MODE0			(1 << 1)
#define BCD			(1 << 0)
#define ATAO_CFG1		0x0a	/* W 16 */
#define EXTINT2EN		(1 << 15)
#define EXTINT1EN		(1 << 14)
#define CNTINT2EN		(1 << 13)
#define CNTINT1EN		(1 << 12)
#define TCINTEN			(1 << 11)
#define CNT1SRC			(1 << 10)
#define CNT2SRC			(1 << 9)
#define FIFOEN			(1 << 8)
#define GRP2WR			(1 << 7)
#define EXTUPDEN		(1 << 6)
#define DMARQ			(1 << 5)
#define DMAEN			(1 << 4)
#define CH_mask			(0xf << 0)
#define ATAO_STATUS		0x0a	/* R 16 */
#define FH			(1 << 6)
#define FE			(1 << 5)
#define FF			(1 << 4)
#define INT2			(1 << 3)
#define INT1			(1 << 2)
#define TCINT			(1 << 1)
#define PROMOUT			(1 << 0)
#define ATAO_FIFO_WRITE		0x0c	/* W 16 */
#define ATAO_FIFO_CLEAR		0x0c	/* R 16 */
#define ATAO_DACn(x)		(0x0c + ((x) * 2))	/* W */

/* registers with _2_ are accessed when GRP2WR is set in CFG1 */

#define ATAO_2_DMATCCLR		0x00	/* W 16 */
#define ATAO_2_INT1CLR		0x02	/* W 16 */
#define ATAO_2_INT2CLR		0x04	/* W 16 */
#define ATAO_2_RTSISHFT		0x06	/* W 8 */
#define ATAO_RTSISHFT_RSI	(1 << 0)
#define ATAO_2_RTSISTRB		0x07	/* W 8 */

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

static void atao_reset(struct comedi_device *dev)
{
	struct atao_private *devpriv = dev->private;

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
	struct atao_private *devpriv = dev->private;
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
	struct atao_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int atao_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + ATAO_DOUT);

	data[1] = inw(dev->iobase + ATAO_DIN);

	return insn->n;
}

static int atao_dio_insn_config(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct atao_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	if (s->io_bits & 0x0f)
		devpriv->cfg3 |= DOUTEN1;
	else
		devpriv->cfg3 &= ~DOUTEN1;
	if (s->io_bits & 0xf0)
		devpriv->cfg3 |= DOUTEN2;
	else
		devpriv->cfg3 &= ~DOUTEN2;

	outw(devpriv->cfg3, dev->iobase + ATAO_CFG3);

	return insn->n;
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
	struct atao_private *devpriv = dev->private;
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
	struct atao_private *devpriv;
	struct comedi_subdevice *s;
	int ao_unipolar;
	int ret;

	ao_unipolar = it->options[3];

	ret = comedi_request_region(dev, it->options[0], 0x20);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
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
	.detach		= comedi_legacy_detach,
	.board_name	= &atao_boards[0].name,
	.offset		= sizeof(struct atao_board),
	.num_names	= ARRAY_SIZE(atao_boards),
};
module_comedi_driver(ni_at_ao_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

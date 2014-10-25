/*
 * ni_at_ao.c
 * Driver for NI AT-AO-6/10 boards
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000,2002 David A. Schleef <ds@schleef.org>
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

/*
 * Driver: ni_at_ao
 * Description: National Instruments AT-AO-6/10
 * Devices: (National Instruments) AT-AO-6 [at-ao-6]
 *          (National Instruments) AT-AO-10 [at-ao-10]
 * Status: should work
 * Author: David A. Schleef <ds@schleef.org>
 * Updated: Sun Dec 26 12:26:28 EST 2004
 *
 * Configuration options:
 *   [0] - I/O port base address
 *   [1] - IRQ (unused)
 *   [2] - DMA (unused)
 *   [3] - analog output range, set by jumpers on hardware
 *         0 for -10 to 10V bipolar
 *         1 for 0V to 10V unipolar
 */

#include <linux/module.h>

#include "../comedidev.h"

#include "8253.h"

/*
 * Register map
 *
 * Register-level programming information can be found in NI
 * document 320379.pdf.
 */
#define ATAO_DIO_REG		0x00
#define ATAO_CFG2_REG		0x02
#define ATAO_CFG2_CALLD_NOP	(0 << 14)
#define ATAO_CFG2_CALLD(x)	((((x) >> 3) + 1) << 14)
#define ATAO_CFG2_FFRTEN	(1 << 13)
#define ATAO_CFG2_DACS(x)	(1 << (((x) / 2) + 8))
#define ATAO_CFG2_LDAC(x)	(1 << (((x) / 2) + 3))
#define ATAO_CFG2_PROMEN	(1 << 2)
#define ATAO_CFG2_SCLK		(1 << 1)
#define ATAO_CFG2_SDATA		(1 << 0)
#define ATAO_CFG3_REG		0x04
#define ATAO_CFG3_DMAMODE	(1 << 6)
#define ATAO_CFG3_CLKOUT	(1 << 5)
#define ATAO_CFG3_RCLKEN	(1 << 4)
#define ATAO_CFG3_DOUTEN2	(1 << 3)
#define ATAO_CFG3_DOUTEN1	(1 << 2)
#define ATAO_CFG3_EN2_5V	(1 << 1)
#define ATAO_CFG3_SCANEN	(1 << 0)
#define ATAO_82C53_BASE		0x06
#define ATAO_CFG1_REG		0x0a
#define ATAO_CFG1_EXTINT2EN	(1 << 15)
#define ATAO_CFG1_EXTINT1EN	(1 << 14)
#define ATAO_CFG1_CNTINT2EN	(1 << 13)
#define ATAO_CFG1_CNTINT1EN	(1 << 12)
#define ATAO_CFG1_TCINTEN	(1 << 11)
#define ATAO_CFG1_CNT1SRC	(1 << 10)
#define ATAO_CFG1_CNT2SRC	(1 << 9)
#define ATAO_CFG1_FIFOEN	(1 << 8)
#define ATAO_CFG1_GRP2WR	(1 << 7)
#define ATAO_CFG1_EXTUPDEN	(1 << 6)
#define ATAO_CFG1_DMARQ		(1 << 5)
#define ATAO_CFG1_DMAEN		(1 << 4)
#define ATAO_CFG1_CH(x)		(((x) & 0xf) << 0)
#define ATAO_STATUS_REG		0x0a
#define ATAO_STATUS_FH		(1 << 6)
#define ATAO_STATUS_FE		(1 << 5)
#define ATAO_STATUS_FF		(1 << 4)
#define ATAO_STATUS_INT2	(1 << 3)
#define ATAO_STATUS_INT1	(1 << 2)
#define ATAO_STATUS_TCINT	(1 << 1)
#define ATAO_STATUS_PROMOUT	(1 << 0)
#define ATAO_FIFO_WRITE_REG	0x0c
#define ATAO_FIFO_CLEAR_REG	0x0c
#define ATAO_AO_REG(x)		(0x0c + ((x) * 2))

/* registers with _2_ are accessed when GRP2WR is set in CFG1 */
#define ATAO_2_DMATCCLR_REG	0x00
#define ATAO_2_INT1CLR_REG	0x02
#define ATAO_2_INT2CLR_REG	0x04
#define ATAO_2_RTSISHFT_REG	0x06
#define ATAO_2_RTSISHFT_RSI	(1 << 0)
#define ATAO_2_RTSISTRB_REG	0x07

struct atao_board {
	const char *name;
	int n_ao_chans;
};

static const struct atao_board atao_boards[] = {
	{
		.name		= "at-ao-6",
		.n_ao_chans	= 6,
	}, {
		.name		= "at-ao-10",
		.n_ao_chans	= 10,
	},
};

struct atao_private {
	unsigned short cfg1;
	unsigned short cfg3;

	/* Used for caldac readback */
	unsigned char caldac[21];
};

static void atao_select_reg_group(struct comedi_device *dev, int group)
{
	struct atao_private *devpriv = dev->private;

	if (group)
		devpriv->cfg1 |= ATAO_CFG1_GRP2WR;
	else
		devpriv->cfg1 &= ~ATAO_CFG1_GRP2WR;
	outw(devpriv->cfg1, dev->iobase + ATAO_CFG1_REG);
}

static int atao_ao_insn_write(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	if (chan == 0)
		atao_select_reg_group(dev, 1);

	for (i = 0; i < insn->n; i++) {
		val = data[i];

		/* the hardware expects two's complement values */
		outw(comedi_offset_munge(s, val),
		     dev->iobase + ATAO_AO_REG(chan));
	}
	s->readback[chan] = val;

	if (chan == 0)
		atao_select_reg_group(dev, 0);

	return insn->n;
}

static int atao_dio_insn_bits(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + ATAO_DIO_REG);

	data[1] = inw(dev->iobase + ATAO_DIO_REG);

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
		devpriv->cfg3 |= ATAO_CFG3_DOUTEN1;
	else
		devpriv->cfg3 &= ~ATAO_CFG3_DOUTEN1;
	if (s->io_bits & 0xf0)
		devpriv->cfg3 |= ATAO_CFG3_DOUTEN2;
	else
		devpriv->cfg3 &= ~ATAO_CFG3_DOUTEN2;

	outw(devpriv->cfg3, dev->iobase + ATAO_CFG3_REG);

	return insn->n;
}

/*
 * There are three DAC8800 TrimDACs on the board. These are 8-channel,
 * 8-bit DACs that are used to calibrate the Analog Output channels.
 * The factory default calibration values are stored in the EEPROM.
 * The TrimDACs, and EEPROM addresses, are mapped as:
 *
 *        Channel       EEPROM  Description
 *   -----------------  ------  -----------------------------------
 *    0 - DAC0 Chan 0    0x30   AO Channel 0 Offset
 *    1 - DAC0 Chan 1    0x31   AO Channel 0 Gain
 *    2 - DAC0 Chan 2    0x32   AO Channel 1 Offset
 *    3 - DAC0 Chan 3    0x33   AO Channel 1 Gain
 *    4 - DAC0 Chan 4    0x34   AO Channel 2 Offset
 *    5 - DAC0 Chan 5    0x35   AO Channel 2 Gain
 *    6 - DAC0 Chan 6    0x36   AO Channel 3 Offset
 *    7 - DAC0 Chan 7    0x37   AO Channel 3 Gain
 *    8 - DAC1 Chan 0    0x38   AO Channel 4 Offset
 *    9 - DAC1 Chan 1    0x39   AO Channel 4 Gain
 *   10 - DAC1 Chan 2    0x3a   AO Channel 5 Offset
 *   11 - DAC1 Chan 3    0x3b   AO Channel 5 Gain
 *   12 - DAC1 Chan 4    0x3c   2.5V Offset
 *   13 - DAC1 Chan 5    0x3d   AO Channel 6 Offset (at-ao-10 only)
 *   14 - DAC1 Chan 6    0x3e   AO Channel 6 Gain   (at-ao-10 only)
 *   15 - DAC1 Chan 7    0x3f   AO Channel 7 Offset (at-ao-10 only)
 *   16 - DAC2 Chan 0    0x40   AO Channel 7 Gain   (at-ao-10 only)
 *   17 - DAC2 Chan 1    0x41   AO Channel 8 Offset (at-ao-10 only)
 *   18 - DAC2 Chan 2    0x42   AO Channel 8 Gain   (at-ao-10 only)
 *   19 - DAC2 Chan 3    0x43   AO Channel 9 Offset (at-ao-10 only)
 *   20 - DAC2 Chan 4    0x44   AO Channel 9 Gain   (at-ao-10 only)
 *        DAC2 Chan 5    0x45   Reserved
 *        DAC2 Chan 6    0x46   Reserved
 *        DAC2 Chan 7    0x47   Reserved
 */
static int atao_calib_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct atao_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int bitstring;
	unsigned int val;
	int bit;

	if (insn->n == 0)
		return 0;

	devpriv->caldac[chan] = data[insn->n - 1] & s->maxdata;

	/* write the channel and last data value to the caldac */
	bitstring = ((chan & 0x7) << 8) | devpriv->caldac[chan];

	/* clock the bitstring to the caldac; MSB -> LSB */
	for (bit = 1 << 10; bit; bit >>= 1) {
		val = (bit & bitstring) ? ATAO_CFG2_SDATA : 0;

		outw(val, dev->iobase + ATAO_CFG2_REG);
		outw(val | ATAO_CFG2_SCLK, dev->iobase + ATAO_CFG2_REG);
	}

	/* strobe the caldac to load the value */
	outw(ATAO_CFG2_CALLD(chan), dev->iobase + ATAO_CFG2_REG);
	outw(ATAO_CFG2_CALLD_NOP, dev->iobase + ATAO_CFG2_REG);

	return insn->n;
}

static int atao_calib_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct atao_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->caldac[chan];

	return insn->n;
}

static void atao_reset(struct comedi_device *dev)
{
	struct atao_private *devpriv = dev->private;
	unsigned long timer_base = dev->iobase + ATAO_82C53_BASE;

	/* This is the reset sequence described in the manual */

	devpriv->cfg1 = 0;
	outw(devpriv->cfg1, dev->iobase + ATAO_CFG1_REG);

	/* Put outputs of counter 1 and counter 2 in a high state */
	i8254_set_mode(timer_base, 0, 0, I8254_MODE4 | I8254_BINARY);
	i8254_set_mode(timer_base, 0, 1, I8254_MODE4 | I8254_BINARY);
	i8254_write(timer_base, 0, 0, 0x0003);

	outw(ATAO_CFG2_CALLD_NOP, dev->iobase + ATAO_CFG2_REG);

	devpriv->cfg3 = 0;
	outw(devpriv->cfg3, dev->iobase + ATAO_CFG3_REG);

	inw(dev->iobase + ATAO_FIFO_CLEAR_REG);

	atao_select_reg_group(dev, 1);
	outw(0, dev->iobase + ATAO_2_INT1CLR_REG);
	outw(0, dev->iobase + ATAO_2_INT2CLR_REG);
	outw(0, dev->iobase + ATAO_2_DMATCCLR_REG);
	atao_select_reg_group(dev, 0);
}

static int atao_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct atao_board *board = dev->board_ptr;
	struct atao_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x20);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE;
	s->n_chan	= board->n_ao_chans;
	s->maxdata	= 0x0fff;
	s->range_table	= it->options[3] ? &range_unipolar10 : &range_bipolar10;
	s->insn_write	= atao_ao_insn_write;
	s->insn_read	= comedi_readback_insn_read;

	ret = comedi_alloc_subdev_readback(s);
	if (ret)
		return ret;

	/* Digital I/O subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= atao_dio_insn_bits;
	s->insn_config	= atao_dio_insn_config;

	/* caldac subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_CALIB;
	s->subdev_flags	= SDF_WRITABLE | SDF_INTERNAL;
	s->n_chan	= (board->n_ao_chans * 2) + 1;
	s->maxdata	= 0xff;
	s->insn_read	= atao_calib_insn_read;
	s->insn_write	= atao_calib_insn_write;

	/* EEPROM subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_UNUSED;

	atao_reset(dev);

	return 0;
}

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
MODULE_DESCRIPTION("Comedi driver for NI AT-AO-6/10 boards");
MODULE_LICENSE("GPL");

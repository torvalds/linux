/*

    comedi/drivers/aio_aio12_8.c

    Driver for Access I/O Products PC-104 AIO12-8 Analog I/O Board
    Copyright (C) 2006 C&C Technologies, Inc.

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

Driver: aio_aio12_8
Description: Access I/O Products PC-104 AIO12-8 Analog I/O Board
Author: Pablo Mejia <pablo.mejia@cctechnol.com>
Devices: [Access I/O] PC-104 AIO12-8 (aio_aio12_8)
	 [Access I/O] PC-104 AI12-8 (aio_ai12_8)
	 [Access I/O] PC-104 AO12-8 (aio_ao12_8)
Status: experimental

Configuration Options:
  [0] - I/O port base address

Notes:

  Only synchronous operations are supported.

*/

#include <linux/module.h>
#include "../comedidev.h"
#include "8255.h"

/*
 * Register map
 */
#define AIO12_8_STATUS_REG		0x00
#define AIO12_8_STATUS_ADC_EOC		(1 << 7)
#define AIO12_8_STATUS_PORT_C_COS	(1 << 6)
#define AIO12_8_STATUS_IRQ_ENA		(1 << 2)
#define AIO12_8_INTERRUPT_REG		0x01
#define AIO12_8_INTERRUPT_ADC		(1 << 7)
#define AIO12_8_INTERRUPT_COS		(1 << 6)
#define AIO12_8_INTERRUPT_COUNTER1	(1 << 5)
#define AIO12_8_INTERRUPT_PORT_C3	(1 << 4)
#define AIO12_8_INTERRUPT_PORT_C0	(1 << 3)
#define AIO12_8_INTERRUPT_ENA		(1 << 2)
#define AIO12_8_ADC_REG			0x02
#define AIO12_8_ADC_MODE_NORMAL		(0 << 6)
#define AIO12_8_ADC_MODE_INT_CLK	(1 << 6)
#define AIO12_8_ADC_MODE_STANDBY	(2 << 6)
#define AIO12_8_ADC_MODE_POWERDOWN	(3 << 6)
#define AIO12_8_ADC_ACQ_3USEC		(0 << 5)
#define AIO12_8_ADC_ACQ_PROGRAM		(1 << 5)
#define AIO12_8_ADC_RANGE(x)		((x) << 3)
#define AIO12_8_ADC_CHAN(x)		((x) << 0)
#define AIO12_8_DAC_REG(x)		(0x04 + (x) * 2)
#define AIO12_8_8254_BASE_REG		0x0c
#define AIO12_8_8255_BASE_REG		0x10
#define AIO12_8_DIO_CONTROL_REG		0x14
#define AIO12_8_DIO_CONTROL_TST		(1 << 0)
#define AIO12_8_ADC_TRIGGER_REG		0x15
#define AIO12_8_ADC_TRIGGER_RANGE(x)	((x) << 3)
#define AIO12_8_ADC_TRIGGER_CHAN(x)	((x) << 0)
#define AIO12_8_TRIGGER_REG		0x16
#define AIO12_8_TRIGGER_ADTRIG		(1 << 1)
#define AIO12_8_TRIGGER_DACTRIG		(1 << 0)
#define AIO12_8_COS_REG			0x17
#define AIO12_8_DAC_ENABLE_REG		0x18
#define AIO12_8_DAC_ENABLE_REF_ENA	(1 << 0)

struct aio12_8_boardtype {
	const char *name;
	int ai_nchan;
	int ao_nchan;
};

static const struct aio12_8_boardtype board_types[] = {
	{
		.name		= "aio_aio12_8",
		.ai_nchan	= 8,
		.ao_nchan	= 4,
	}, {
		.name		= "aio_ai12_8",
		.ai_nchan	= 8,
	}, {
		.name		= "aio_ao12_8",
		.ao_nchan	= 4,
	},
};

static int aio_aio12_8_ai_eoc(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + AIO12_8_STATUS_REG);
	if (status & AIO12_8_STATUS_ADC_EOC)
		return 0;
	return -EBUSY;
}

static int aio_aio12_8_ai_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned char control;
	int ret;
	int n;

	/*
	 * Setup the control byte for internal 2MHz clock, 3uS conversion,
	 * at the desired range of the requested channel.
	 */
	control = AIO12_8_ADC_MODE_NORMAL | AIO12_8_ADC_ACQ_3USEC |
		  AIO12_8_ADC_RANGE(range) | AIO12_8_ADC_CHAN(chan);

	/* Read status to clear EOC latch */
	inb(dev->iobase + AIO12_8_STATUS_REG);

	for (n = 0; n < insn->n; n++) {
		/*  Setup and start conversion */
		outb(control, dev->iobase + AIO12_8_ADC_REG);

		/*  Wait for conversion to complete */
		ret = comedi_timeout(dev, s, insn, aio_aio12_8_ai_eoc, 0);
		if (ret)
			return ret;

		data[n] = inw(dev->iobase + AIO12_8_ADC_REG) & s->maxdata;
	}

	return insn->n;
}

static int aio_aio12_8_ao_insn_write(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int val = s->readback[chan];
	int i;

	/* enable DACs */
	outb(AIO12_8_DAC_ENABLE_REF_ENA, dev->iobase + AIO12_8_DAC_ENABLE_REG);

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		outw(val, dev->iobase + AIO12_8_DAC_REG(chan));
	}
	s->readback[chan] = val;

	return insn->n;
}

static const struct comedi_lrange range_aio_aio12_8 = {
	4, {
		UNI_RANGE(5),
		BIP_RANGE(5),
		UNI_RANGE(10),
		BIP_RANGE(10)
	}
};

static int aio_aio12_8_attach(struct comedi_device *dev,
			      struct comedi_devconfig *it)
{
	const struct aio12_8_boardtype *board = dev->board_ptr;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 32);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	if (board->ai_nchan) {
		/* Analog input subdevice */
		s->type		= COMEDI_SUBD_AI;
		s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
		s->n_chan	= board->ai_nchan;
		s->maxdata	= 0x0fff;
		s->range_table	= &range_aio_aio12_8;
		s->insn_read	= aio_aio12_8_ai_read;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[1];
	if (board->ao_nchan) {
		/* Analog output subdevice */
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND | SDF_DIFF;
		s->n_chan	= 4;
		s->maxdata	= 0x0fff;
		s->range_table	= &range_aio_aio12_8;
		s->insn_write	= aio_aio12_8_ao_insn_write;
		s->insn_read	= comedi_readback_insn_read;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[2];
	/* 8255 Digital i/o subdevice */
	ret = subdev_8255_init(dev, s, NULL, AIO12_8_8255_BASE_REG);
	if (ret)
		return ret;

	s = &dev->subdevices[3];
	/* 8254 counter/timer subdevice */
	s->type		= COMEDI_SUBD_UNUSED;

	return 0;
}

static struct comedi_driver aio_aio12_8_driver = {
	.driver_name	= "aio_aio12_8",
	.module		= THIS_MODULE,
	.attach		= aio_aio12_8_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &board_types[0].name,
	.num_names	= ARRAY_SIZE(board_types),
	.offset		= sizeof(struct aio12_8_boardtype),
};
module_comedi_driver(aio_aio12_8_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

// SPDX-License-Identifier: GPL-2.0+
/*
 * aio_aio12_8.c
 * Driver for Access I/O Products PC-104 AIO12-8 Analog I/O Board
 * Copyright (C) 2006 C&C Technologies, Inc.
 */

/*
 * Driver: aio_aio12_8
 * Description: Access I/O Products PC-104 AIO12-8 Analog I/O Board
 * Author: Pablo Mejia <pablo.mejia@cctechnol.com>
 * Devices: [Access I/O] PC-104 AIO12-8 (aio_aio12_8),
 *   [Access I/O] PC-104 AI12-8 (aio_ai12_8),
 *   [Access I/O] PC-104 AO12-4 (aio_ao12_4)
 * Status: experimental
 *
 * Configuration Options:
 *   [0] - I/O port base address
 *
 * Notes:
 * Only synchronous operations are supported.
 */

#include <linux/module.h>
#include "../comedidev.h"

#include "comedi_8254.h"
#include "8255.h"

/*
 * Register map
 */
#define AIO12_8_STATUS_REG		0x00
#define AIO12_8_STATUS_ADC_EOC		BIT(7)
#define AIO12_8_STATUS_PORT_C_COS	BIT(6)
#define AIO12_8_STATUS_IRQ_ENA		BIT(2)
#define AIO12_8_INTERRUPT_REG		0x01
#define AIO12_8_INTERRUPT_ADC		BIT(7)
#define AIO12_8_INTERRUPT_COS		BIT(6)
#define AIO12_8_INTERRUPT_COUNTER1	BIT(5)
#define AIO12_8_INTERRUPT_PORT_C3	BIT(4)
#define AIO12_8_INTERRUPT_PORT_C0	BIT(3)
#define AIO12_8_INTERRUPT_ENA		BIT(2)
#define AIO12_8_ADC_REG			0x02
#define AIO12_8_ADC_MODE(x)		(((x) & 0x3) << 6)
#define AIO12_8_ADC_MODE_NORMAL		AIO12_8_ADC_MODE(0)
#define AIO12_8_ADC_MODE_INT_CLK	AIO12_8_ADC_MODE(1)
#define AIO12_8_ADC_MODE_STANDBY	AIO12_8_ADC_MODE(2)
#define AIO12_8_ADC_MODE_POWERDOWN	AIO12_8_ADC_MODE(3)
#define AIO12_8_ADC_ACQ(x)		(((x) & 0x1) << 5)
#define AIO12_8_ADC_ACQ_3USEC		AIO12_8_ADC_ACQ(0)
#define AIO12_8_ADC_ACQ_PROGRAM		AIO12_8_ADC_ACQ(1)
#define AIO12_8_ADC_RANGE(x)		((x) << 3)
#define AIO12_8_ADC_CHAN(x)		((x) << 0)
#define AIO12_8_DAC_REG(x)		(0x04 + (x) * 2)
#define AIO12_8_8254_BASE_REG		0x0c
#define AIO12_8_8255_BASE_REG		0x10
#define AIO12_8_DIO_CONTROL_REG		0x14
#define AIO12_8_DIO_CONTROL_TST		BIT(0)
#define AIO12_8_ADC_TRIGGER_REG		0x15
#define AIO12_8_ADC_TRIGGER_RANGE(x)	((x) << 3)
#define AIO12_8_ADC_TRIGGER_CHAN(x)	((x) << 0)
#define AIO12_8_TRIGGER_REG		0x16
#define AIO12_8_TRIGGER_ADTRIG		BIT(1)
#define AIO12_8_TRIGGER_DACTRIG		BIT(0)
#define AIO12_8_COS_REG			0x17
#define AIO12_8_DAC_ENABLE_REG		0x18
#define AIO12_8_DAC_ENABLE_REF_ENA	BIT(0)

static const struct comedi_lrange aio_aio12_8_range = {
	4, {
		UNI_RANGE(5),
		BIP_RANGE(5),
		UNI_RANGE(10),
		BIP_RANGE(10)
	}
};

struct aio12_8_boardtype {
	const char *name;
	unsigned int has_ai:1;
	unsigned int has_ao:1;
};

static const struct aio12_8_boardtype board_types[] = {
	{
		.name		= "aio_aio12_8",
		.has_ai		= 1,
		.has_ao		= 1,
	}, {
		.name		= "aio_ai12_8",
		.has_ai		= 1,
	}, {
		.name		= "aio_ao12_4",
		.has_ao		= 1,
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
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	unsigned char control;
	int ret;
	int i;

	/*
	 * Setup the control byte for internal 2MHz clock, 3uS conversion,
	 * at the desired range of the requested channel.
	 */
	control = AIO12_8_ADC_MODE_NORMAL | AIO12_8_ADC_ACQ_3USEC |
		  AIO12_8_ADC_RANGE(range) | AIO12_8_ADC_CHAN(chan);

	/* Read status to clear EOC latch */
	inb(dev->iobase + AIO12_8_STATUS_REG);

	for (i = 0; i < insn->n; i++) {
		/*  Setup and start conversion */
		outb(control, dev->iobase + AIO12_8_ADC_REG);

		/*  Wait for conversion to complete */
		ret = comedi_timeout(dev, s, insn, aio_aio12_8_ai_eoc, 0);
		if (ret)
			return ret;

		val = inw(dev->iobase + AIO12_8_ADC_REG) & s->maxdata;

		/* munge bipolar 2's complement data to offset binary */
		if (comedi_range_is_bipolar(s, range))
			val = comedi_offset_munge(s, val);

		data[i] = val;
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

static int aio_aio12_8_counter_insn_config(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   struct comedi_insn *insn,
					   unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case INSN_CONFIG_GET_CLOCK_SRC:
		/*
		 * Channels 0 and 2 have external clock sources.
		 * Channel 1 has a fixed 1 MHz clock source.
		 */
		data[0] = 0;
		data[1] = (chan == 1) ? I8254_OSC_BASE_1MHZ : 0;
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static int aio_aio12_8_attach(struct comedi_device *dev,
			      struct comedi_devconfig *it)
{
	const struct aio12_8_boardtype *board = dev->board_ptr;
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 32);
	if (ret)
		return ret;

	dev->pacer = comedi_8254_init(dev->iobase + AIO12_8_8254_BASE_REG,
				      0, I8254_IO8, 0);
	if (!dev->pacer)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 4);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[0];
	if (board->has_ai) {
		s->type		= COMEDI_SUBD_AI;
		s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF;
		s->n_chan	= 8;
		s->maxdata	= 0x0fff;
		s->range_table	= &aio_aio12_8_range;
		s->insn_read	= aio_aio12_8_ai_read;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[1];
	if (board->has_ao) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_GROUND;
		s->n_chan	= 4;
		s->maxdata	= 0x0fff;
		s->range_table	= &aio_aio12_8_range;
		s->insn_write	= aio_aio12_8_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* Digital I/O subdevice (8255) */
	s = &dev->subdevices[2];
	ret = subdev_8255_init(dev, s, NULL, AIO12_8_8255_BASE_REG);
	if (ret)
		return ret;

	/* Counter subdevice (8254) */
	s = &dev->subdevices[3];
	comedi_8254_subdevice_init(s, dev->pacer);

	dev->pacer->insn_config = aio_aio12_8_counter_insn_config;

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

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Access I/O AIO12-8 Analog I/O Board");
MODULE_LICENSE("GPL");

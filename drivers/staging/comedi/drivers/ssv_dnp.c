// SPDX-License-Identifier: GPL-2.0+
/*
 * ssv_dnp.c
 * generic comedi driver for SSV Embedded Systems' DIL/Net-PCs
 * Copyright (C) 2001 Robert Schwebel <robert@schwebel.de>
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 */

/*
 * Driver: ssv_dnp
 * Description: SSV Embedded Systems DIL/Net-PC
 * Author: Robert Schwebel <robert@schwebel.de>
 * Devices: [SSV Embedded Systems] DIL/Net-PC 1486 (dnp-1486)
 * Status: unknown
 */

/* include files ----------------------------------------------------------- */

#include <linux/module.h>
#include "../comedidev.h"

/* Some global definitions: the registers of the DNP ----------------------- */
/*                                                                           */
/* For port A and B the mode register has bits corresponding to the output   */
/* pins, where Bit-N = 0 -> input, Bit-N = 1 -> output. Note that bits       */
/* 4 to 7 correspond to pin 0..3 for port C data register. Ensure that bits  */
/* 0..3 remain unchanged! For details about Port C Mode Register see         */
/* the remarks in dnp_insn_config() below.                                   */

#define CSCIR 0x22		/* Chip Setup and Control Index Register     */
#define CSCDR 0x23		/* Chip Setup and Control Data Register      */
#define PAMR  0xa5		/* Port A Mode Register                      */
#define PADR  0xa9		/* Port A Data Register                      */
#define PBMR  0xa4		/* Port B Mode Register                      */
#define PBDR  0xa8		/* Port B Data Register                      */
#define PCMR  0xa3		/* Port C Mode Register                      */
#define PCDR  0xa7		/* Port C Data Register                      */

static int dnp_dio_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn,
			     unsigned int *data)
{
	unsigned int mask;
	unsigned int val;

	/*
	 * Ports A and B are straight forward: each bit corresponds to an
	 * output pin with the same order. Port C is different: bits 0...3
	 * correspond to bits 4...7 of the output register (PCDR).
	 */

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		outb(PADR, CSCIR);
		outb(s->state & 0xff, CSCDR);

		outb(PBDR, CSCIR);
		outb((s->state >> 8) & 0xff, CSCDR);

		outb(PCDR, CSCIR);
		val = inb(CSCDR) & 0x0f;
		outb(((s->state >> 12) & 0xf0) | val, CSCDR);
	}

	outb(PADR, CSCIR);
	val = inb(CSCDR);
	outb(PBDR, CSCIR);
	val |= (inb(CSCDR) << 8);
	outb(PCDR, CSCIR);
	val |= ((inb(CSCDR) & 0xf0) << 12);

	data[1] = val;

	return insn->n;
}

static int dnp_dio_insn_config(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	unsigned int val;
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	if (chan < 8) {			/* Port A */
		mask = 1 << chan;
		outb(PAMR, CSCIR);
	} else if (chan < 16) {		/* Port B */
		mask = 1 << (chan - 8);
		outb(PBMR, CSCIR);
	} else {			/* Port C */
		/*
		 * We have to pay attention with port C.
		 * This is the meaning of PCMR:
		 *   Bit in PCMR:              7 6 5 4 3 2 1 0
		 *   Corresponding port C pin: d 3 d 2 d 1 d 0   d= don't touch
		 *
		 * Multiplication by 2 brings bits into correct position
		 * for PCMR!
		 */
		mask = 1 << ((chan - 16) * 2);
		outb(PCMR, CSCIR);
	}

	val = inb(CSCDR);
	if (data[0] == COMEDI_OUTPUT)
		val |= mask;
	else
		val &= ~mask;
	outb(val, CSCDR);

	return insn->n;
}

static int dnp_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	/*
	 * We use I/O ports 0x22, 0x23 and 0xa3-0xa9, which are always
	 * allocated for the primary 8259, so we don't need to allocate
	 * them ourselves.
	 */

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* digital i/o subdevice                                             */
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 20;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = dnp_dio_insn_bits;
	s->insn_config = dnp_dio_insn_config;

	/* configure all ports as input (default)                            */
	outb(PAMR, CSCIR);
	outb(0x00, CSCDR);
	outb(PBMR, CSCIR);
	outb(0x00, CSCDR);
	outb(PCMR, CSCIR);
	outb((inb(CSCDR) & 0xAA), CSCDR);

	return 0;
}

static void dnp_detach(struct comedi_device *dev)
{
	outb(PAMR, CSCIR);
	outb(0x00, CSCDR);
	outb(PBMR, CSCIR);
	outb(0x00, CSCDR);
	outb(PCMR, CSCIR);
	outb((inb(CSCDR) & 0xAA), CSCDR);
}

static struct comedi_driver dnp_driver = {
	.driver_name	= "dnp-1486",
	.module		= THIS_MODULE,
	.attach		= dnp_attach,
	.detach		= dnp_detach,
};
module_comedi_driver(dnp_driver);

MODULE_AUTHOR("Comedi https://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
